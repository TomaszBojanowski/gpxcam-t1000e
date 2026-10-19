// Unit tests for src/link_policy.cpp, run on the PC: pio test -e native
// Made-up values: camera serial numbers 12 34 56 78 9a / 9b, the point at sea 54.6, 19.0
// and the made-up S/W point. Reference distances computed with Python (haversine).
#include <string.h>
#include <unity.h>

#include "link_policy.h"

using link_policy::Send;

void setUp() {}
void tearDown() {}

namespace {

pairing_record::Record paired(uint8_t lastSerialByte)
{
    pairing_record::Record record = {};
    const uint8_t serial[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, lastSerialByte};
    record.paired = true;
    memcpy(record.serial, serial, sizeof(serial));
    return record;
}

camera_list::Entry seen(uint8_t lastSerialByte, uint8_t state, uint32_t lastSeenMs, int8_t rssi = -60)
{
    camera_list::Entry entry = {};
    const uint8_t serial[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, lastSerialByte};
    entry.used = true;
    memcpy(entry.last.camera.serial, serial, sizeof(serial));
    entry.last.camera.state = state;
    entry.last.rssi = rssi;
    entry.lastSeenMs = lastSeenMs;
    return entry;
}

} // namespace

void test_connect_only_to_the_paired_camera_when_on()
{
    const pairing_record::Record record = paired(0x9a);
    TEST_ASSERT_TRUE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000), 10500));
    TEST_ASSERT_TRUE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000), 11000)); // 1 s old
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000), 11001)); // too old
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_STANDBY, 10000), 10000));
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_PAIRING, 10000), 10000));
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, 0x01, 10000), 10000)); // unknown state
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9b, lumix::STATE_ON, 10000), 10000)); // other camera
}

void test_no_connection_without_pairing_or_entry()
{
    pairing_record::Record record = paired(0x9a);
    record.paired = false;
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000), 10000));
    camera_list::Entry empty = seen(0x9a, lumix::STATE_ON, 10000);
    empty.used = false;
    TEST_ASSERT_FALSE(link_policy::shouldConnect(paired(0x9a), empty, 10000));
}

void test_no_connection_below_the_rssi_threshold()
{
    const pairing_record::Record record = paired(0x9a);
    TEST_ASSERT_TRUE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000, -85), 10000));
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000, -86), 10000));
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_ON, 10000, -90), 10000));
    // a strong signal never makes standby connectable
    TEST_ASSERT_FALSE(link_policy::shouldConnect(record, seen(0x9a, lumix::STATE_STANDBY, 10000, -40), 10000));
}

void test_search_mode()
{
    using link_policy::SearchMode;
    const uint32_t FIVE_MINUTES = link_policy::RARE_AFTER_MS;
    // never seen since the link started
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(false, 0, 0, 0));
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(false, 0, 0, FIVE_MINUTES - 1));
    TEST_ASSERT_EQUAL(SearchMode::Rare, link_policy::searchMode(false, 0, 0, FIVE_MINUTES));
    // seen on, recently and long ago
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(true, 1000, lumix::STATE_ON, FIVE_MINUTES * 3));
    TEST_ASSERT_EQUAL(SearchMode::Rare, link_policy::searchMode(true, FIVE_MINUTES, lumix::STATE_ON, FIVE_MINUTES));
    // seen in standby, recently and long ago (Bluetooth off or carried away)
    TEST_ASSERT_EQUAL(SearchMode::Standby, link_policy::searchMode(true, 1000, lumix::STATE_STANDBY, 0));
    TEST_ASSERT_EQUAL(SearchMode::Standby,
                      link_policy::searchMode(true, FIVE_MINUTES - 1, lumix::STATE_STANDBY, FIVE_MINUTES * 2));
    TEST_ASSERT_EQUAL(SearchMode::Rare, link_policy::searchMode(true, FIVE_MINUTES, lumix::STATE_STANDBY, 0));
    // pairing state is not standby: look normally
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(true, 1000, lumix::STATE_PAIRING, 0));
}

void test_gps_needed()
{
    using link_policy::SearchMode;
    TEST_ASSERT_TRUE(link_policy::gpsNeeded(SearchMode::Normal, 0));
    TEST_ASSERT_FALSE(link_policy::gpsNeeded(SearchMode::Rare, 0));
    TEST_ASSERT_TRUE(link_policy::gpsNeeded(SearchMode::Standby, link_policy::STANDBY_GPS_OFF_MS - 1));
    TEST_ASSERT_FALSE(link_policy::gpsNeeded(SearchMode::Standby, link_policy::STANDBY_GPS_OFF_MS));
}

void test_pause_sequence()
{
    TEST_ASSERT_EQUAL_UINT32(0, link_policy::pauseMs(0));
    TEST_ASSERT_EQUAL_UINT32(5000, link_policy::pauseMs(1));
    TEST_ASSERT_EQUAL_UINT32(15000, link_policy::pauseMs(2));
    TEST_ASSERT_EQUAL_UINT32(60000, link_policy::pauseMs(3));
    TEST_ASSERT_EQUAL_UINT32(60000, link_policy::pauseMs(4));
    TEST_ASSERT_EQUAL_UINT32(60000, link_policy::pauseMs(1000));
}

void test_location_due()
{
    TEST_ASSERT_EQUAL(Send::First, link_policy::locationDue(false, 0, 0, false));
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(true, 24999, 0, false));
    TEST_ASSERT_EQUAL(Send::Periodic, link_policy::locationDue(true, 25000, 0, false));
    TEST_ASSERT_EQUAL(Send::Moved, link_policy::locationDue(true, 5000, 21, false));
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(true, 5000, 20, false)); // not more than 20 m
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(true, 4999, 500, true)); // too soon after the last
    TEST_ASSERT_EQUAL(Send::Asked, link_policy::locationDue(true, 5000, 0, true));
    TEST_ASSERT_EQUAL(Send::Periodic, link_policy::locationDue(true, 30000, 500, true));
}

void test_distance()
{
    // 0.0001 degree of latitude, of longitude at 54.6 N, 0.00018 degree of latitude
    TEST_ASSERT_UINT32_WITHIN(1, 11, link_policy::distanceMetres(546000000, 190000000, 546001000, 190000000));
    TEST_ASSERT_UINT32_WITHIN(1, 6, link_policy::distanceMetres(546000000, 190000000, 546000000, 190001000));
    TEST_ASSERT_EQUAL_UINT32(20, link_policy::distanceMetres(546000000, 190000000, 546001800, 190000000));
    TEST_ASSERT_EQUAL_UINT32(19, link_policy::distanceMetres(546000000, 190000000, 546000000, 190003000));
    TEST_ASSERT_UINT32_WITHIN(5, 10008, link_policy::distanceMetres(546000000, 190000000, 546900000, 190000000));
    TEST_ASSERT_UINT32_WITHIN(1, 11, link_policy::distanceMetres(-338687233, -1512094633, -338686233, -1512094633));
    TEST_ASSERT_EQUAL_UINT32(0, link_policy::distanceMetres(546000000, 190000000, 546000000, 190000000));
    // across the 180th meridian: 0.0002 degree apart, not almost the whole Earth
    TEST_ASSERT_UINT32_WITHIN(1, 22, link_policy::distanceMetres(0, 1799999000, 0, -1799999000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_connect_only_to_the_paired_camera_when_on);
    RUN_TEST(test_no_connection_without_pairing_or_entry);
    RUN_TEST(test_no_connection_below_the_rssi_threshold);
    RUN_TEST(test_search_mode);
    RUN_TEST(test_gps_needed);
    RUN_TEST(test_pause_sequence);
    RUN_TEST(test_location_due);
    RUN_TEST(test_distance);
    return UNITY_END();
}
