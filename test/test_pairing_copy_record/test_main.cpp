// Unit tests for src/pairing_copy_record.cpp, run on the PC: pio test -e native
// Made-up values: card UUID 00010203-0405-0607-0809-0a0b0c0d0e0f, camera serial number
// 12 34 56 78 9a. The expected copy was computed independently with Python's zlib.crc32().
#include <string.h>
#include <unity.h>

#include "pairing_copy_record.h"

using pairing_copy_record::Kind;
using pairing_copy_record::Page;

void setUp() {}
void tearDown() {}

namespace {

constexpr size_t PAGE_SIZE = 4096;

// Sequence 3, the paired record of test_pairing_record.
const uint8_t COPY_3[pairing_copy_record::SIZE] = {
    0x47, 0x58, 0x50, 0x43, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x47, 0x58, 0x50, 0x52,
    0x01, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x0e, 0x0f, 0x12, 0x34, 0x56, 0x78, 0x9a, 0x00, 0xb4, 0xba, 0xfb, 0x41, 0x57, 0x2b, 0x46, 0x1a};

uint8_t page[PAGE_SIZE];

pairing_record::Record madeUp(bool paired)
{
    pairing_record::Record record = {};
    for (uint8_t i = 0; i < lumix::UUID_LENGTH; i++)
        record.uuid[i] = i;
    record.paired = paired;
    if (paired) {
        const uint8_t serial[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, 0x9a};
        memcpy(record.serial, serial, sizeof(serial));
    }
    return record;
}

void erase()
{
    memset(page, 0xFF, sizeof(page));
}

// An erased page with a copy at its start.
Page pageWith(const uint8_t *copy)
{
    erase();
    memcpy(page, copy, pairing_copy_record::SIZE);
    return pairing_copy_record::classify(page, sizeof(page));
}

Page kindOnly(Kind kind, uint32_t sequence = 0)
{
    Page result = {};
    result.kind = kind;
    result.sequence = sequence;
    return result;
}

} // namespace

void test_encode_matches_the_expected_bytes()
{
    uint8_t out[pairing_copy_record::SIZE];
    pairing_copy_record::encode(madeUp(true), 3, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(COPY_3, out, sizeof(out));
}

void test_a_copy_is_read_back()
{
    const Page result = pageWith(COPY_3);
    TEST_ASSERT_EQUAL(Kind::Copy, result.kind);
    TEST_ASSERT_EQUAL_UINT32(3, result.sequence);
    TEST_ASSERT_TRUE(result.record.paired);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(madeUp(true).uuid, result.record.uuid, lumix::UUID_LENGTH);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(madeUp(true).serial, result.record.serial, lumix::SERIAL_LENGTH);
}

void test_the_dialect_rides_in_the_copy()
{
    pairing_record::Record older = madeUp(true);
    older.dialect = pairing_record::DIALECT_OLDER;
    erase();
    pairing_copy_record::encode(older, 7, page);
    const Page result = pairing_copy_record::classify(page, sizeof(page));
    TEST_ASSERT_EQUAL(Kind::Copy, result.kind);
    TEST_ASSERT_EQUAL_HEX8(pairing_record::DIALECT_OLDER, result.record.dialect);
    TEST_ASSERT_TRUE(pairing_record::isOlderDialect(result.record));
}

void test_an_erased_page_is_empty()
{
    erase();
    TEST_ASSERT_EQUAL(Kind::Empty, pairing_copy_record::classify(page, sizeof(page)).kind);
}

// One byte anywhere makes a page that is not ours: other data, never written.
void test_other_data_is_foreign()
{
    erase();
    page[PAGE_SIZE - 1] = 0x00; // the last byte of the page
    TEST_ASSERT_EQUAL(Kind::Foreign, pairing_copy_record::classify(page, sizeof(page)).kind);
    erase();
    page[0] = 'G'; // not the whole magic
    TEST_ASSERT_EQUAL(Kind::Foreign, pairing_copy_record::classify(page, sizeof(page)).kind);
}

// A write cut off: our magic, but no valid copy. It may be written again.
void test_a_cut_off_copy_is_broken()
{
    uint8_t copy[pairing_copy_record::SIZE];
    memcpy(copy, COPY_3, sizeof(copy));
    memset(&copy[20], 0xFF, sizeof(copy) - 20); // programming stopped after 20 bytes
    TEST_ASSERT_EQUAL(Kind::Broken, pageWith(copy).kind);

    memcpy(copy, COPY_3, sizeof(copy));
    copy[30] ^= 0x01; // a changed bit: the CRC does not match
    TEST_ASSERT_EQUAL(Kind::Broken, pageWith(copy).kind);

    pageWith(COPY_3);
    page[PAGE_SIZE / 2] = 0x00; // a valid copy, but the rest of the page is not erased
    TEST_ASSERT_EQUAL(Kind::Broken, pairing_copy_record::classify(page, sizeof(page)).kind);
}

// Meshtastic 2.8 keeps its node cache on these pages; after a return to gpxcam they may be written.
// Each page starts with the ring's magic and a sequence number (WarmNodeStore.cpp).
void test_meshtastic_ring_pages_are_recognised()
{
    const char *magics[] = {"WRNG", "WRN2", "WRN3"};
    for (const char *magic : magics) {
        erase();
        memcpy(page, magic, 4);
        page[4] = 0x07; // sequence 7
        page[4 + 4] = 0x2a; // a record follows
        TEST_ASSERT_EQUAL(Kind::Meshtastic, pairing_copy_record::classify(page, sizeof(page)).kind);
    }
    erase();
    memcpy(page, "WRN4", 4); // not a known version: someone else's data
    TEST_ASSERT_EQUAL(Kind::Foreign, pairing_copy_record::classify(page, sizeof(page)).kind);
    erase();
    memcpy(page, "WRM3", 4); // Meshtastic's warm.dat file magic, not the ring
    TEST_ASSERT_EQUAL(Kind::Foreign, pairing_copy_record::classify(page, sizeof(page)).kind);

    Page pages[2] = {kindOnly(Kind::Meshtastic), kindOnly(Kind::Meshtastic)};
    TEST_ASSERT_EQUAL_INT(0, pairing_copy_record::writeTarget(pages));
    pages[0] = kindOnly(Kind::Copy, 1);
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::writeTarget(pages));
}

void test_usable_pages()
{
    TEST_ASSERT_TRUE(pairing_copy_record::usable(kindOnly(Kind::Empty)));
    TEST_ASSERT_TRUE(pairing_copy_record::usable(kindOnly(Kind::Copy, 1)));
    TEST_ASSERT_TRUE(pairing_copy_record::usable(kindOnly(Kind::Broken)));
    TEST_ASSERT_TRUE(pairing_copy_record::usable(kindOnly(Kind::Meshtastic)));
    TEST_ASSERT_FALSE(pairing_copy_record::usable(kindOnly(Kind::Foreign)));
}

// The newest copy is read; the next one goes to the other usable page.
void test_two_pages_take_turns()
{
    Page pages[2] = {kindOnly(Kind::Empty), kindOnly(Kind::Empty)};
    TEST_ASSERT_EQUAL_INT(-1, pairing_copy_record::newest(pages));
    TEST_ASSERT_EQUAL_INT(0, pairing_copy_record::writeTarget(pages)); // the first copy: page 0
    TEST_ASSERT_EQUAL_UINT32(1, pairing_copy_record::nextSequence(pages));

    pages[0] = kindOnly(Kind::Copy, 1);
    TEST_ASSERT_EQUAL_INT(0, pairing_copy_record::newest(pages));
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::writeTarget(pages)); // the newest stays
    TEST_ASSERT_EQUAL_UINT32(2, pairing_copy_record::nextSequence(pages));

    pages[1] = kindOnly(Kind::Copy, 2);
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::newest(pages));
    TEST_ASSERT_EQUAL_INT(0, pairing_copy_record::writeTarget(pages)); // the older one
    TEST_ASSERT_EQUAL_UINT32(3, pairing_copy_record::nextSequence(pages));

    pages[0] = kindOnly(Kind::Broken); // the third write was cut off: copy 2 still there
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::newest(pages));
    TEST_ASSERT_EQUAL_INT(0, pairing_copy_record::writeTarget(pages));
    TEST_ASSERT_EQUAL_UINT32(3, pairing_copy_record::nextSequence(pages));
}

// Other data on one page: the copy uses the other page alone, also over its own newest copy.
void test_one_usable_page()
{
    Page pages[2] = {kindOnly(Kind::Foreign), kindOnly(Kind::Empty)};
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::writeTarget(pages));
    pages[1] = kindOnly(Kind::Copy, 5);
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::newest(pages));
    TEST_ASSERT_EQUAL_INT(1, pairing_copy_record::writeTarget(pages));
    TEST_ASSERT_EQUAL_UINT32(6, pairing_copy_record::nextSequence(pages));

    Page other[2] = {kindOnly(Kind::Copy, 4), kindOnly(Kind::Foreign)};
    TEST_ASSERT_EQUAL_INT(0, pairing_copy_record::writeTarget(other));
}

// Other data on both pages: no copy, and nothing is written.
void test_no_usable_page()
{
    const Page pages[2] = {kindOnly(Kind::Foreign), kindOnly(Kind::Foreign)};
    TEST_ASSERT_EQUAL_INT(-1, pairing_copy_record::newest(pages));
    TEST_ASSERT_EQUAL_INT(-1, pairing_copy_record::writeTarget(pages));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_matches_the_expected_bytes);
    RUN_TEST(test_a_copy_is_read_back);
    RUN_TEST(test_the_dialect_rides_in_the_copy);
    RUN_TEST(test_an_erased_page_is_empty);
    RUN_TEST(test_other_data_is_foreign);
    RUN_TEST(test_a_cut_off_copy_is_broken);
    RUN_TEST(test_meshtastic_ring_pages_are_recognised);
    RUN_TEST(test_usable_pages);
    RUN_TEST(test_two_pages_take_turns);
    RUN_TEST(test_one_usable_page);
    RUN_TEST(test_no_usable_page);
    return UNITY_END();
}
