// Unit tests for src/camera_list.cpp, run on the PC: pio test -e native
// All values are made up: serial numbers 12 34 56 78 9a and similar, addresses from the
// documentation range 00:00:5e:00:53:xx (RFC 7042) and an RPA-shaped 4a:5e:00:53:00:01.
#include <string.h>
#include <unity.h>

#include "camera_list.h"

using camera_list::Change;

void setUp() {}
void tearDown() {}

namespace {

// Addresses are stored least significant byte first, as the SoftDevice gives them.
const uint8_t PUBLIC_ADDRESS[camera_list::ADDRESS_LENGTH] = {0x01, 0x53, 0x00, 0x5e, 0x00, 0x00};
const uint8_t RPA_ADDRESS[camera_list::ADDRESS_LENGTH] = {0x01, 0x00, 0x53, 0x00, 0x5e, 0x4a};
const uint8_t OTHER_RPA_ADDRESS[camera_list::ADDRESS_LENGTH] = {0x02, 0x00, 0x53, 0x00, 0x5e, 0x4a};

camera_list::Report report(uint8_t lastSerialByte, uint8_t state, const uint8_t *address, uint8_t addressType,
                           bool scanResponse = false)
{
    camera_list::Report r = {};
    const uint8_t serial[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, lastSerialByte};
    memcpy(r.camera.serial, serial, sizeof(serial));
    r.camera.state = state;
    memcpy(r.address, address, camera_list::ADDRESS_LENGTH);
    r.addressType = addressType;
    r.rssi = -60;
    r.scanResponse = scanResponse;
    return r;
}

camera_list::Report onRpa()
{
    return report(0x9a, lumix::STATE_ON, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE);
}

} // namespace

void test_new_then_same()
{
    camera_list::CameraList list;
    TEST_ASSERT_EQUAL(Change::New, list.add(onRpa(), 1000));
    camera_list::Report louder = onRpa();
    louder.rssi = -40; // RSSI alone is not a change
    TEST_ASSERT_EQUAL(Change::None, list.add(louder, 1100));
    const camera_list::Entry *entry = list.at(0);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL(-40, entry->last.rssi);
    TEST_ASSERT_EQUAL_UINT32(1100, entry->lastSeenMs);
    TEST_ASSERT_NULL(list.at(1));
}

void test_counts_adverts_and_scan_responses()
{
    camera_list::CameraList list;
    list.add(onRpa(), 0);
    list.add(onRpa(), 10);
    list.add(report(0x9a, lumix::STATE_ON, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE, true), 20);
    TEST_ASSERT_EQUAL_UINT32(2, list.at(0)->advertCount);
    TEST_ASSERT_EQUAL_UINT32(1, list.at(0)->scanResponseCount);
}

void test_state_and_address_changes()
{
    camera_list::CameraList list;
    list.add(onRpa(), 0);
    // pairing menu: same address, new state
    TEST_ASSERT_EQUAL(Change::Changed,
                      list.add(report(0x9a, lumix::STATE_PAIRING, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE), 10));
    // the RPA changes
    TEST_ASSERT_EQUAL(Change::Changed, list.add(report(0x9a, lumix::STATE_PAIRING, OTHER_RPA_ADDRESS,
                                                       camera_list::ADDRESS_RANDOM_RESOLVABLE),
                                                20));
    // switched off: standby with the public address, both change at once
    TEST_ASSERT_EQUAL(Change::Changed,
                      list.add(report(0x9a, lumix::STATE_STANDBY, PUBLIC_ADDRESS, camera_list::ADDRESS_PUBLIC), 30));
    TEST_ASSERT_EQUAL(Change::None,
                      list.add(report(0x9a, lumix::STATE_STANDBY, PUBLIC_ADDRESS, camera_list::ADDRESS_PUBLIC), 40));
    // only the address type differs
    TEST_ASSERT_EQUAL(Change::Changed,
                      list.add(report(0x9a, lumix::STATE_STANDBY, PUBLIC_ADDRESS, camera_list::ADDRESS_RANDOM_STATIC), 50));
    TEST_ASSERT_NULL(list.at(1)); // still one camera
}

void test_table_full()
{
    camera_list::CameraList list;
    for (uint8_t i = 0; i < camera_list::MAX_CAMERAS; i++)
        TEST_ASSERT_EQUAL(Change::New, list.add(report(i, lumix::STATE_ON, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE), 0));
    TEST_ASSERT_EQUAL(Change::TableFull, list.add(report(0x9a, lumix::STATE_ON, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE), 0));
    // known cameras are still updated
    TEST_ASSERT_EQUAL(Change::None, list.add(report(0, lumix::STATE_ON, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE), 5));
}

void test_lost_after_30_s()
{
    camera_list::CameraList list;
    list.add(onRpa(), 1000);
    list.add(report(0x9b, lumix::STATE_ON, RPA_ADDRESS, camera_list::ADDRESS_RANDOM_RESOLVABLE), 20000);
    camera_list::Entry lost;
    TEST_ASSERT_FALSE(list.removeLost(1000 + camera_list::LOST_AFTER_MS - 1, &lost));
    TEST_ASSERT_TRUE(list.removeLost(1000 + camera_list::LOST_AFTER_MS, &lost));
    TEST_ASSERT_EQUAL_HEX8(0x9a, lost.last.camera.serial[4]);
    TEST_ASSERT_FALSE(list.removeLost(1000 + camera_list::LOST_AFTER_MS, &lost)); // the other one is newer
    // seen again: a new camera, in the freed slot
    TEST_ASSERT_EQUAL(Change::New, list.add(onRpa(), 40000));
    TEST_ASSERT_NOT_NULL(list.at(0));
}

void test_lost_across_millis_wrap()
{
    camera_list::CameraList list;
    list.add(onRpa(), 0xFFFFF000); // 4 s before millis() wraps
    camera_list::Entry lost;
    TEST_ASSERT_FALSE(list.removeLost(0x00001000, &lost)); // 8 s later
    TEST_ASSERT_TRUE(list.removeLost(0xFFFFF000 + camera_list::LOST_AFTER_MS, &lost));
}

void test_clear()
{
    camera_list::CameraList list;
    list.add(onRpa(), 0);
    list.clear();
    TEST_ASSERT_NULL(list.at(0));
    TEST_ASSERT_EQUAL(Change::New, list.add(onRpa(), 0));
    TEST_ASSERT_EQUAL_UINT32(1, list.at(0)->advertCount); // counts start again
}

void test_address_text()
{
    char text[18];
    TEST_ASSERT_EQUAL(17, camera_list::formatAddress(PUBLIC_ADDRESS, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("00:00:5e:00:53:01", text);
    TEST_ASSERT_EQUAL(17, camera_list::formatAddress(RPA_ADDRESS, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("4a:5e:00:53:00:01", text);
    char small[17]; // no room for the terminator
    TEST_ASSERT_EQUAL(0, camera_list::formatAddress(PUBLIC_ADDRESS, small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("public", camera_list::addressTypeName(0x00));
    TEST_ASSERT_EQUAL_STRING("random static", camera_list::addressTypeName(0x01));
    TEST_ASSERT_EQUAL_STRING("RPA", camera_list::addressTypeName(0x02));
    TEST_ASSERT_EQUAL_STRING("random", camera_list::addressTypeName(0x03));
    TEST_ASSERT_EQUAL_STRING("anonymous", camera_list::addressTypeName(0x7F));
    TEST_ASSERT_EQUAL_STRING("unknown", camera_list::addressTypeName(0x04));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_new_then_same);
    RUN_TEST(test_counts_adverts_and_scan_responses);
    RUN_TEST(test_state_and_address_changes);
    RUN_TEST(test_table_full);
    RUN_TEST(test_lost_after_30_s);
    RUN_TEST(test_lost_across_millis_wrap);
    RUN_TEST(test_clear);
    RUN_TEST(test_address_text);
    return UNITY_END();
}
