// Unit tests for src/battery_batch.cpp, run on the PC: pio test -e native
// Made-up entries.
#include <string.h>
#include <unity.h>

#include "battery_batch.h"

using battery_batch::Batch;
using battery_batch::StableUsb;
using battery_batch::USB_STABLE_MS;
using battery_rules::Entry;

void setUp() {}
void tearDown() {}

namespace {

Entry entryAt(uint32_t minutes)
{
    return Entry{minutes, (uint16_t)(4100 - minutes), 2, 0};
}

} // namespace

// Five entries stay in RAM; the sixth makes the batch due (1 h at one every 10 minutes).
void test_due_after_six_entries()
{
    Batch batch;
    TEST_ASSERT_FALSE(batch.due(0)); // empty
    for (uint32_t i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(batch.add(entryAt(10 * i), 0));
        TEST_ASSERT_FALSE(batch.due(0));
    }
    TEST_ASSERT_TRUE(batch.add(entryAt(50), 0));
    TEST_ASSERT_TRUE(batch.due(0));
    TEST_ASSERT_EQUAL_size_t(6, batch.size());
    TEST_ASSERT_FALSE(batch.add(entryAt(60), 0)); // full: written first
    batch.clear();
    TEST_ASSERT_EQUAL_size_t(0, batch.size());
    TEST_ASSERT_FALSE(batch.due(0));
    TEST_ASSERT_TRUE(batch.add(entryAt(60), 0));
}

// The bytes are the entries one after another, oldest first, as the file holds them.
void test_bytes_are_the_entries_in_order()
{
    Batch batch;
    uint8_t expected[3 * battery_rules::ENTRY_SIZE];
    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(batch.add(entryAt(i), 0));
        battery_rules::encode(entryAt(i), &expected[i * battery_rules::ENTRY_SIZE]);
    }
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), batch.byteCount());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, batch.bytes(), sizeof(expected));
    const Entry second = batch.entry(1);
    TEST_ASSERT_EQUAL_UINT32(1, second.uptimeMinutes);
    TEST_ASSERT_EQUAL_UINT16(4099, second.millivolts);
    TEST_ASSERT_EQUAL_UINT8(2, second.activity);
}

// The limit counts the file and RAM together; a batch that fills the log is due at once.
void test_the_limit_counts_file_and_ram()
{
    Batch batch;
    const uint32_t inFile = battery_batch::MAX_ENTRIES - 2;
    TEST_ASSERT_TRUE(batch.add(entryAt(0), inFile));
    TEST_ASSERT_FALSE(batch.due(inFile));
    TEST_ASSERT_TRUE(batch.add(entryAt(10), inFile));
    TEST_ASSERT_TRUE(batch.due(inFile)); // 2 entries, the log is full with them
    TEST_ASSERT_FALSE(batch.add(entryAt(20), inFile));
    TEST_ASSERT_EQUAL_size_t(2, batch.size());
    batch.clear();
    TEST_ASSERT_FALSE(batch.add(entryAt(30), battery_batch::MAX_ENTRIES)); // a full file
    TEST_ASSERT_FALSE(batch.due(battery_batch::MAX_ENTRIES)); // nothing to write
}

// USB counts only after USB_STABLE_MS without a break.
void test_usb_stable_after_ten_seconds()
{
    StableUsb usb;
    TEST_ASSERT_FALSE(usb.update(false, 1000));
    TEST_ASSERT_FALSE(usb.update(true, 2000));
    TEST_ASSERT_FALSE(usb.update(true, 2000 + USB_STABLE_MS - 1));
    TEST_ASSERT_TRUE(usb.update(true, 2000 + USB_STABLE_MS));
    TEST_ASSERT_TRUE(usb.update(true, 2000 + 5 * USB_STABLE_MS));
    TEST_ASSERT_FALSE(usb.update(false, 2000 + 5 * USB_STABLE_MS + 1)); // unplugged
}

// A bouncing cable: every break starts the time again.
void test_usb_bounces_do_not_count()
{
    StableUsb usb;
    uint32_t now = 0;
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_FALSE(usb.update(true, now));
        TEST_ASSERT_FALSE(usb.update(true, now + USB_STABLE_MS - 1));
        TEST_ASSERT_FALSE(usb.update(false, now + USB_STABLE_MS - 1));
        now += USB_STABLE_MS;
    }
    TEST_ASSERT_FALSE(usb.update(true, now));
    TEST_ASSERT_TRUE(usb.update(true, now + USB_STABLE_MS));
}

// millis() wraps after 49.7 days.
void test_usb_stable_across_millis_wrap()
{
    StableUsb usb;
    const uint32_t start = UINT32_MAX - USB_STABLE_MS / 2;
    TEST_ASSERT_FALSE(usb.update(true, start));
    TEST_ASSERT_FALSE(usb.update(true, start + USB_STABLE_MS - 1)); // wrapped
    TEST_ASSERT_TRUE(usb.update(true, start + USB_STABLE_MS));
}

void test_old_file()
{
    using battery_batch::OldFile;
    TEST_ASSERT_EQUAL(OldFile::None, battery_batch::oldFile(false, false));
    TEST_ASSERT_EQUAL(OldFile::None, battery_batch::oldFile(false, true));
    TEST_ASSERT_EQUAL(OldFile::Move, battery_batch::oldFile(true, false));
    TEST_ASSERT_EQUAL(OldFile::KeepBoth, battery_batch::oldFile(true, true));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_due_after_six_entries);
    RUN_TEST(test_bytes_are_the_entries_in_order);
    RUN_TEST(test_the_limit_counts_file_and_ram);
    RUN_TEST(test_usb_stable_after_ten_seconds);
    RUN_TEST(test_usb_bounces_do_not_count);
    RUN_TEST(test_usb_stable_across_millis_wrap);
    RUN_TEST(test_old_file);
    return UNITY_END();
}
