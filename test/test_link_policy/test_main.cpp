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
    const uint32_t LONG = link_policy::LONG_STANDBY_AFTER_MS;
    // never seen since the link started
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(false, 0, 0, 0, 0));
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(false, 0, 0, FIVE_MINUTES - 1, 0));
    TEST_ASSERT_EQUAL(SearchMode::Rare, link_policy::searchMode(false, 0, 0, FIVE_MINUTES, 0));
    // seen on, recently and long ago
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(true, 1000, lumix::STATE_ON, FIVE_MINUTES * 3, 0));
    TEST_ASSERT_EQUAL(SearchMode::Rare, link_policy::searchMode(true, FIVE_MINUTES, lumix::STATE_ON, FIVE_MINUTES, 0));
    // standby: short, then long; not seen for 5 minutes at all: rare
    TEST_ASSERT_EQUAL(SearchMode::Standby, link_policy::searchMode(true, 1000, lumix::STATE_STANDBY, 0, 0));
    TEST_ASSERT_EQUAL(SearchMode::Standby, link_policy::searchMode(true, 1000, lumix::STATE_STANDBY, 0, LONG - 1));
    TEST_ASSERT_EQUAL(SearchMode::LongStandby, link_policy::searchMode(true, 1000, lumix::STATE_STANDBY, 0, LONG));
    TEST_ASSERT_EQUAL(SearchMode::LongStandby,
                      link_policy::searchMode(true, FIVE_MINUTES - 1, lumix::STATE_STANDBY, 0, LONG * 10));
    TEST_ASSERT_EQUAL(SearchMode::Rare, link_policy::searchMode(true, FIVE_MINUTES, lumix::STATE_STANDBY, 0, 0));
    // pairing state is not standby: look normally
    TEST_ASSERT_EQUAL(SearchMode::Normal, link_policy::searchMode(true, 1000, lumix::STATE_PAIRING, 0, LONG));
}

void test_gps_needed()
{
    using link_policy::SearchMode;
    TEST_ASSERT_TRUE(link_policy::gpsNeeded(SearchMode::Normal));
    TEST_ASSERT_TRUE(link_policy::gpsNeeded(SearchMode::Standby)); // a break between photos: keep the fix
    TEST_ASSERT_FALSE(link_policy::gpsNeeded(SearchMode::LongStandby));
    TEST_ASSERT_FALSE(link_policy::gpsNeeded(SearchMode::Rare));
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
    const link_policy::LocationTiming &normal = link_policy::NORMAL_TIMING;
    TEST_ASSERT_EQUAL(Send::First, link_policy::locationDue(normal, false, 0, 0, false));
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(normal, true, 24999, 0, false));
    TEST_ASSERT_EQUAL(Send::Periodic, link_policy::locationDue(normal, true, 25000, 0, false));
    TEST_ASSERT_EQUAL(Send::Moved, link_policy::locationDue(normal, true, 5000, 21, false));
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(normal, true, 5000, 20, false)); // not more than 20 m
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(normal, true, 4999, 500, true)); // too soon after the last
    TEST_ASSERT_EQUAL(Send::Asked, link_policy::locationDue(normal, true, 5000, 0, true));
    TEST_ASSERT_EQUAL(Send::Periodic, link_policy::locationDue(normal, true, 30000, 500, true));
}

// Frequent: every 5 s, after more than 5 m, never within 2 s of the last.
void test_location_due_frequent()
{
    const link_policy::LocationTiming &frequent = link_policy::FREQUENT_TIMING;
    TEST_ASSERT_EQUAL(Send::First, link_policy::locationDue(frequent, false, 0, 0, false));
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(frequent, true, 4999, 0, false));
    TEST_ASSERT_EQUAL(Send::Periodic, link_policy::locationDue(frequent, true, 5000, 0, false));
    TEST_ASSERT_EQUAL(Send::Moved, link_policy::locationDue(frequent, true, 2000, 6, false));
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(frequent, true, 2000, 5, false)); // not more than 5 m
    TEST_ASSERT_EQUAL(Send::No, link_policy::locationDue(frequent, true, 1999, 500, true)); // too soon after the last
    TEST_ASSERT_EQUAL(Send::Asked, link_policy::locationDue(frequent, true, 2000, 0, true));
}

void test_updates_names()
{
    using link_policy::Updates;
    TEST_ASSERT_EQUAL_STRING("normal", link_policy::updatesName(Updates::Normal));
    TEST_ASSERT_EQUAL_STRING("frequent", link_policy::updatesName(Updates::Frequent));
    Updates updates = Updates::Normal;
    TEST_ASSERT_TRUE(link_policy::updatesFromName("frequent", &updates));
    TEST_ASSERT_EQUAL(Updates::Frequent, updates);
    TEST_ASSERT_TRUE(link_policy::updatesFromName("normal", &updates));
    TEST_ASSERT_EQUAL(Updates::Normal, updates);
    updates = Updates::Frequent;
    TEST_ASSERT_FALSE(link_policy::updatesFromName("Frequent", &updates)); // the console lowers the case first
    TEST_ASSERT_FALSE(link_policy::updatesFromName("fast", &updates));
    TEST_ASSERT_FALSE(link_policy::updatesFromName("", &updates));
    TEST_ASSERT_EQUAL(Updates::Frequent, updates); // unchanged
    TEST_ASSERT_EQUAL_UINT32(25000, link_policy::timingOf(Updates::Normal).periodMs);
    TEST_ASSERT_EQUAL_UINT32(5000, link_policy::timingOf(Updates::Frequent).periodMs);
}

// The guard: two late answers in one connection slow it down to its end; on-time ones never
// count; the next connection is frequent again, unless the last one did not end by HCI 0x13.
void test_rate_guard()
{
    using link_policy::Updates;
    link_policy::RateGuard guard;
    guard.connectionStarted();
    TEST_ASSERT_FALSE(guard.slowedDown());
    TEST_ASSERT_EQUAL_UINT32(5000, guard.timing(Updates::Frequent).periodMs);
    TEST_ASSERT_FALSE(guard.answered(80));
    TEST_ASSERT_FALSE(guard.answered(link_policy::LATE_ANSWER_MS)); // exactly the limit: not late
    TEST_ASSERT_FALSE(guard.answered(1001));                        // the first late one
    TEST_ASSERT_FALSE(guard.slowedDown());
    TEST_ASSERT_FALSE(guard.answered(90));
    TEST_ASSERT_TRUE(guard.answered(2500)); // the second late one: slows down, said once
    TEST_ASSERT_TRUE(guard.slowedDown());
    TEST_ASSERT_EQUAL(Updates::Normal, guard.inEffect(Updates::Frequent));
    TEST_ASSERT_EQUAL_UINT32(25000, guard.timing(Updates::Frequent).periodMs);
    TEST_ASSERT_FALSE(guard.answered(3000)); // already slow
    TEST_ASSERT_EQUAL_UINT8(3, guard.lateAnswers());

    guard.connectionEnded(true); // the camera switched off: the next connection is frequent
    guard.connectionStarted();
    TEST_ASSERT_FALSE(guard.slowedDown());
    TEST_ASSERT_EQUAL_UINT8(0, guard.lateAnswers());
    TEST_ASSERT_FALSE(guard.answered(1500)); // one late answer per connection is not enough
    guard.connectionEnded(true);
    guard.connectionStarted();
    TEST_ASSERT_FALSE(guard.answered(1500));
    TEST_ASSERT_FALSE(guard.slowedDown());

    guard.connectionEnded(false); // out of range, a time limit, an error: the next one is normal
    guard.connectionStarted();
    TEST_ASSERT_TRUE(guard.slowedDown());
    TEST_ASSERT_EQUAL_UINT32(5000, guard.timing(Updates::Frequent).minGapMs); // normal: at most every 5 s
    guard.connectionEnded(true); // that one ended well: frequent again
    guard.connectionStarted();
    TEST_ASSERT_FALSE(guard.slowedDown());
}

// With normal chosen the guard changes nothing: always the normal timing.
void test_rate_guard_with_normal_chosen()
{
    using link_policy::Updates;
    link_policy::RateGuard guard;
    guard.connectionStarted();
    TEST_ASSERT_EQUAL_UINT32(25000, guard.timing(Updates::Normal).periodMs);
    guard.answered(1500);
    guard.answered(1500);
    TEST_ASSERT_EQUAL_UINT32(25000, guard.timing(Updates::Normal).periodMs);
    TEST_ASSERT_EQUAL(Updates::Normal, guard.inEffect(Updates::Normal));
    guard.connectionEnded(true);
    guard.connectionStarted();
    TEST_ASSERT_EQUAL_UINT32(25000, guard.timing(Updates::Normal).periodMs);
    TEST_ASSERT_EQUAL_UINT32(5000, guard.timing(Updates::Frequent).periodMs); // chosen during a connection
    TEST_ASSERT_EQUAL(Updates::Frequent, guard.inEffect(Updates::Frequent));
}

// With the time written, only its refusal says the camera does not know the card (gpxcam
// notes: an unknown device gets through the identifier, app name and device name); without
// it ('clock off'), any refused write of the introduction and the first location does.
void test_refusal_means_unknown_card()
{
    using link_policy::IntroductionWrite;
    using link_policy::refusalMeansUnknownCard;
    const IntroductionWrite others[] = {IntroductionWrite::Identifier, IntroductionWrite::AppName,
                                        IntroductionWrite::DeviceName, IntroductionWrite::GpsMode,
                                        IntroductionWrite::FirstLocation};
    TEST_ASSERT_TRUE(refusalMeansUnknownCard(IntroductionWrite::Time, true));
    for (IntroductionWrite write : others) {
        TEST_ASSERT_FALSE(refusalMeansUnknownCard(write, true)); // clock on: retried as before
        TEST_ASSERT_TRUE(refusalMeansUnknownCard(write, false)); // clock off: the card stops
    }
}

// Without the time written, the third connection in a row that ends before the camera takes a
// location stops the card; with it, such ends never count.
void test_early_ends_mean_unknown_card()
{
    uint8_t inARow = 0;
    TEST_ASSERT_FALSE(link_policy::earlyEndMeansUnknownCard(false, &inARow));
    TEST_ASSERT_FALSE(link_policy::earlyEndMeansUnknownCard(false, &inARow));
    TEST_ASSERT_TRUE(link_policy::earlyEndMeansUnknownCard(false, &inARow));
    TEST_ASSERT_EQUAL_UINT8(link_policy::EARLY_ENDS_FOR_UNKNOWN_CARD, inARow);

    inARow = 0; // the camera took a location in between
    TEST_ASSERT_FALSE(link_policy::earlyEndMeansUnknownCard(false, &inARow));
    TEST_ASSERT_FALSE(link_policy::earlyEndMeansUnknownCard(false, &inARow));
    TEST_ASSERT_FALSE(link_policy::earlyEndMeansUnknownCard(true, &inARow)); // clock on again
    TEST_ASSERT_EQUAL_UINT8(0, inARow);
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_FALSE(link_policy::earlyEndMeansUnknownCard(true, &inARow));
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
    RUN_TEST(test_location_due_frequent);
    RUN_TEST(test_updates_names);
    RUN_TEST(test_rate_guard);
    RUN_TEST(test_rate_guard_with_normal_chosen);
    RUN_TEST(test_refusal_means_unknown_card);
    RUN_TEST(test_early_ends_mean_unknown_card);
    RUN_TEST(test_distance);
    return UNITY_END();
}
