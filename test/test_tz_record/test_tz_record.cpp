// Unit tests for src/tz_record.cpp, run on the PC: pio test -e native
// The checksums were computed with Python's zlib.crc32 over the record layout of tz_record.h.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "pairing_record.h"
#include "tz_record.h"

namespace data {
#include "tz_table_data.inc"
}

using tz_record::Record;
using tz_record::Status;

namespace {

constexpr const char *WARSAW_RULE = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr size_t NAME_OFFSET = 8;
constexpr size_t RULE_OFFSET = 56;
constexpr size_t CRC_OFFSET = 120;

Record fromInput(const char *text)
{
    Record record = {};
    TEST_ASSERT_TRUE_MESSAGE(tz_record::fromInput(text, &record), text);
    return record;
}

void assertRecord(const char *name, const char *rule, const Record &record)
{
    TEST_ASSERT_EQUAL_STRING(name, record.name);
    TEST_ASSERT_EQUAL_STRING(rule, record.rule);
}

uint32_t storedCrc(const uint8_t *data)
{
    return (uint32_t)data[CRC_OFFSET] | (uint32_t)data[CRC_OFFSET + 1] << 8 | (uint32_t)data[CRC_OFFSET + 2] << 16 |
           (uint32_t)data[CRC_OFFSET + 3] << 24;
}

void resign(uint8_t *data)
{
    const uint32_t crc = pairing_record::crc32(data, CRC_OFFSET);
    for (size_t i = 0; i < 4; i++)
        data[CRC_OFFSET + i] = (uint8_t)(crc >> (8 * i));
}

void assertDecodeFails(Status expected, const uint8_t *data, size_t length)
{
    Record record = {};
    strcpy(record.rule, "unchanged");
    TEST_ASSERT_TRUE(tz_record::decode(data, length, &record) == expected);
    TEST_ASSERT_EQUAL_STRING("unchanged", record.rule);
}

} // namespace

void setUp() {}
void tearDown() {}

void test_input_is_a_table_name()
{
    assertRecord("Europe/Warsaw", WARSAW_RULE, fromInput("europe/warsaw"));
    assertRecord("Poland", WARSAW_RULE, fromInput("POLAND"));
    assertRecord("America/New_York", "EST5EDT,M3.2.0,M11.1.0", fromInput("America/New_York"));
}

void test_input_is_a_rule()
{
    assertRecord("", "<+0545>-5:45", fromInput("<+0545>-5:45"));
    assertRecord("", "<+03>-3<+04>,M3.5.0,M10.5.0/3", fromInput("<+03>-3<+04>,M3.5.0,M10.5.0/3"));
}

void test_input_refused()
{
    // A valid rule of 64 characters does not fit in the record.
    const char *tooLong = "<+000000000000000000000000000000000000000000000000000000000000>0";
    TEST_ASSERT_EQUAL_size_t(tz_record::RULE_SIZE, strlen(tooLong));
    const char *const cases[] = {"", "Mars/Olympus_Mons", "CET-1CEST", "Europe/Warsaw ", tooLong};
    for (const char *text : cases) {
        Record record = {};
        strcpy(record.rule, "unchanged");
        TEST_ASSERT_FALSE_MESSAGE(tz_record::fromInput(text, &record), text);
        TEST_ASSERT_EQUAL_STRING("unchanged", record.rule);
    }
}

// Without a record the card keeps the time zone of versions up to 1.1.
void test_default_is_warsaw()
{
    assertRecord("Europe/Warsaw", WARSAW_RULE, tz_record::defaultRecord());
}

void test_encode_layout()
{
    uint8_t data[tz_record::SIZE];
    tz_record::encode(fromInput("Europe/Warsaw"), data);
    TEST_ASSERT_EQUAL_MEMORY("GXTZ\x01\x00\x00\x00", data, 8);
    TEST_ASSERT_EQUAL_STRING("Europe/Warsaw", (const char *)&data[NAME_OFFSET]);
    TEST_ASSERT_EQUAL_STRING(WARSAW_RULE, (const char *)&data[RULE_OFFSET]);
    TEST_ASSERT_EQUAL_HEX32(0x37712f20, storedCrc(data));

    tz_record::encode(fromInput("<+0545>-5:45"), data);
    TEST_ASSERT_EQUAL_UINT8(0, data[NAME_OFFSET]);
    TEST_ASSERT_EQUAL_HEX32(0xac3c954d, storedCrc(data));
}

void test_round_trip()
{
    const char *const inputs[] = {"Europe/Warsaw", "Australia/Lord_Howe", "<+0545>-5:45"};
    for (const char *input : inputs) {
        const Record record = fromInput(input);
        uint8_t data[tz_record::SIZE];
        tz_record::encode(record, data);
        Record decoded = {};
        TEST_ASSERT_TRUE(tz_record::decode(data, sizeof(data), &decoded) == Status::Ok);
        assertRecord(record.name, record.rule, decoded);
    }
}

void test_decode_refuses()
{
    uint8_t good[tz_record::SIZE];
    tz_record::encode(fromInput("Europe/Warsaw"), good);
    uint8_t data[tz_record::SIZE + 1] = {};

    assertDecodeFails(Status::WrongSize, nullptr, tz_record::SIZE);
    memcpy(data, good, sizeof(good));
    assertDecodeFails(Status::WrongSize, data, tz_record::SIZE - 1);
    assertDecodeFails(Status::WrongSize, data, tz_record::SIZE + 1);

    memcpy(data, good, sizeof(good));
    data[0] = 'X';
    assertDecodeFails(Status::WrongMagic, data, tz_record::SIZE);

    memcpy(data, good, sizeof(good));
    data[4] = 2;
    assertDecodeFails(Status::WrongVersion, data, tz_record::SIZE);

    memcpy(data, good, sizeof(good));
    data[RULE_OFFSET] = 'X';
    assertDecodeFails(Status::BadChecksum, data, tz_record::SIZE);

    memcpy(data, good, sizeof(good)); // a name without its NUL
    memset(&data[NAME_OFFSET], 'A', tz_record::NAME_SIZE);
    resign(data);
    assertDecodeFails(Status::BadText, data, tz_record::SIZE);

    memcpy(data, good, sizeof(good)); // a rule without its NUL
    memset(&data[RULE_OFFSET], 'A', tz_record::RULE_SIZE);
    resign(data);
    assertDecodeFails(Status::BadText, data, tz_record::SIZE);

    memcpy(data, good, sizeof(good)); // an empty rule
    memset(&data[RULE_OFFSET], 0, tz_record::RULE_SIZE);
    resign(data);
    assertDecodeFails(Status::BadText, data, tz_record::SIZE);
}

// A record saved by an older firmware takes the rule of the current tz table.
void test_update_from_table()
{
    Record record = {};
    strcpy(record.name, "Europe/Warsaw");
    strcpy(record.rule, "CET-1");
    TEST_ASSERT_TRUE(tz_record::updateFromTable(&record));
    assertRecord("Europe/Warsaw", WARSAW_RULE, record);
    TEST_ASSERT_FALSE(tz_record::updateFromTable(&record)); // already current

    Record rule = fromInput("<+0545>-5:45"); // no name: stays
    TEST_ASSERT_FALSE(tz_record::updateFromTable(&rule));
    assertRecord("", "<+0545>-5:45", rule);

    Record removed = {}; // a name no longer in the table: its saved rule stays
    strcpy(removed.name, "Old/Zone");
    strcpy(removed.rule, "<+03>-3");
    TEST_ASSERT_FALSE(tz_record::updateFromTable(&removed));
    assertRecord("Old/Zone", "<+03>-3", removed);
}

// Every name of the tz table and its rule fit in the record.
void test_every_table_name_fits()
{
    const char *name = data::ZONE_NAMES;
    for (size_t i = 0; i < data::ZONE_COUNT; i++) {
        TEST_ASSERT_TRUE_MESSAGE(strlen(name) < tz_record::NAME_SIZE, name);
        const Record record = fromInput(name);
        TEST_ASSERT_TRUE_MESSAGE(strlen(record.rule) < tz_record::RULE_SIZE, name);
        name += strlen(name) + 1;
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_is_a_table_name);
    RUN_TEST(test_input_is_a_rule);
    RUN_TEST(test_input_refused);
    RUN_TEST(test_default_is_warsaw);
    RUN_TEST(test_encode_layout);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_decode_refuses);
    RUN_TEST(test_update_from_table);
    RUN_TEST(test_every_table_name_fits);
    return UNITY_END();
}
