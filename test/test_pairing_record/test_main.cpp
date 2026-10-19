// Unit tests for src/pairing_record.cpp, run on the PC: pio test -e native
// Made-up values: card UUID 00010203-0405-0607-0809-0a0b0c0d0e0f, camera serial number
// 12 34 56 78 9a. Expected records computed independently with Python's zlib.crc32().
#include <string.h>
#include <unity.h>

#include "pairing_record.h"

void setUp() {}
void tearDown() {}

namespace {

const uint8_t PAIRED[pairing_record::SIZE] = {0x47, 0x58, 0x50, 0x52, 0x01, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
                                              0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                              0x12, 0x34, 0x56, 0x78, 0x9a, 0x00, 0xb4, 0xba, 0xfb, 0x41};
const uint8_t NOT_PAIRED[pairing_record::SIZE] = {0x47, 0x58, 0x50, 0x52, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
                                                  0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xe6, 0x1d, 0xff};

pairing_record::Record madeUp(bool paired)
{
    pairing_record::Record record = {};
    for (uint8_t i = 0; i < lumix::UUID_LENGTH; i++)
        record.uuid[i] = i;
    record.paired = paired;
    const uint8_t serial[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, 0x9a};
    memcpy(record.serial, serial, sizeof(serial));
    return record;
}

} // namespace

void test_crc32()
{
    // the standard check value of CRC-32/ISO-HDLC
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, pairing_record::crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0x00000000, pairing_record::crc32(nullptr, 0));
}

void test_encode()
{
    uint8_t out[pairing_record::SIZE];
    pairing_record::encode(madeUp(true), out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAIRED, out, sizeof(PAIRED));
    // not paired: the serial number is not stored
    pairing_record::encode(madeUp(false), out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NOT_PAIRED, out, sizeof(NOT_PAIRED));
}

void test_decode()
{
    pairing_record::Record record = {};
    TEST_ASSERT_EQUAL(pairing_record::Status::Ok, pairing_record::decode(PAIRED, sizeof(PAIRED), &record));
    const pairing_record::Record expected = madeUp(true);
    TEST_ASSERT_TRUE(record.paired);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.uuid, record.uuid, lumix::UUID_LENGTH);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.serial, record.serial, lumix::SERIAL_LENGTH);
    TEST_ASSERT_EQUAL(pairing_record::Status::Ok, pairing_record::decode(NOT_PAIRED, sizeof(NOT_PAIRED), &record));
    TEST_ASSERT_FALSE(record.paired);
}

void test_decode_rejects()
{
    pairing_record::Record record = madeUp(true);
    const pairing_record::Record before = record;
    uint8_t data[pairing_record::SIZE + 1];
    memcpy(data, PAIRED, sizeof(PAIRED));
    TEST_ASSERT_EQUAL(pairing_record::Status::WrongSize, pairing_record::decode(data, sizeof(PAIRED) - 1, &record));
    TEST_ASSERT_EQUAL(pairing_record::Status::WrongSize, pairing_record::decode(data, sizeof(PAIRED) + 1, &record));
    TEST_ASSERT_EQUAL(pairing_record::Status::WrongSize, pairing_record::decode(nullptr, sizeof(PAIRED), &record));
    data[0] = 'g';
    TEST_ASSERT_EQUAL(pairing_record::Status::WrongMagic, pairing_record::decode(data, sizeof(PAIRED), &record));
    memcpy(data, PAIRED, sizeof(PAIRED));
    data[4] = 2;
    TEST_ASSERT_EQUAL(pairing_record::Status::WrongVersion, pairing_record::decode(data, sizeof(PAIRED), &record));
    memcpy(data, PAIRED, sizeof(PAIRED));
    data[24] ^= 0x01; // one bit of the serial number
    TEST_ASSERT_EQUAL(pairing_record::Status::BadChecksum, pairing_record::decode(data, sizeof(PAIRED), &record));
    // a flag of 2 with a matching checksum
    memcpy(data, PAIRED, sizeof(PAIRED));
    data[5] = 2;
    const uint32_t crc = pairing_record::crc32(data, 28);
    for (int i = 0; i < 4; i++)
        data[28 + i] = (uint8_t)(crc >> (8 * i));
    TEST_ASSERT_EQUAL(pairing_record::Status::BadFlag, pairing_record::decode(data, sizeof(PAIRED), &record));
    TEST_ASSERT_EQUAL_MEMORY(&before, &record, sizeof(record)); // untouched
}

void test_random_uuid_bits()
{
    uint8_t uuid[lumix::UUID_LENGTH];
    memset(uuid, 0xFF, sizeof(uuid));
    pairing_record::makeRandomUuid(uuid);
    TEST_ASSERT_EQUAL_HEX8(0x4F, uuid[6]); // version 4
    TEST_ASSERT_EQUAL_HEX8(0xBF, uuid[8]); // variant 10
    memset(uuid, 0x00, sizeof(uuid));
    pairing_record::makeRandomUuid(uuid);
    TEST_ASSERT_EQUAL_HEX8(0x40, uuid[6]);
    TEST_ASSERT_EQUAL_HEX8(0x80, uuid[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, uuid[7]); // other bytes unchanged
}

void test_uuid_text()
{
    const pairing_record::Record record = madeUp(false);
    char text[pairing_record::UUID_TEXT_LENGTH + 1];
    TEST_ASSERT_EQUAL(36, pairing_record::formatUuid(record.uuid, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("00010203-0405-0607-0809-0a0b0c0d0e0f", text);
    char small[pairing_record::UUID_TEXT_LENGTH];
    TEST_ASSERT_EQUAL(0, pairing_record::formatUuid(record.uuid, small, sizeof(small)));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32);
    RUN_TEST(test_encode);
    RUN_TEST(test_decode);
    RUN_TEST(test_decode_rejects);
    RUN_TEST(test_random_uuid_bits);
    RUN_TEST(test_uuid_text);
    return UNITY_END();
}
