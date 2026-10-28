// link_control, the normal work with the paired camera, against the fake camera (fake.h).
#include <unity.h>

#include <ble_hci.h>

#include "fake.h"
#include "link_control.h"

using link_control::State;

namespace {

bool linkConnected()
{
    return link_control::state() == State::Connected;
}

bool linkPausing()
{
    return link_control::state() == State::Pausing;
}

// 'link on' with the paired camera switched on next to the card and a GPS fix: connected.
void connectLink(uint8_t dialect = pairing_record::DIALECT_NEWER)
{
    fake::pairCamera(dialect);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    TEST_ASSERT_TRUE(fake::loopUntil(linkConnected, 10000));
}

// The camera ends the connection; the link pauses and connects again (the camera stays on).
void endAndReconnect(uint8_t reason)
{
    fake::endConnection(reason);
    TEST_ASSERT_TRUE(fake::loopUntil(linkPausing, 1000));
    TEST_ASSERT_TRUE(fake::loopUntil(linkConnected, 70000));
}

// The older dialect: connected once the camera has taken the card (a line each time).
constexpr const char *OLDER_CONNECTED = "link: connected to camera 123456789a (older dialect)";
int olderConnectionsBefore = 0;

bool olderConnectedAgain()
{
    return fake::loggedCount(OLDER_CONNECTED) > olderConnectionsBefore;
}

void connectOlderLink()
{
    fake::camera.dialect = fake::Dialect::Older;
    fake::pairCamera(pairing_record::DIALECT_OLDER);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    olderConnectionsBefore = 0;
    link_control::start();
    TEST_ASSERT_TRUE(fake::loopUntil(olderConnectedAgain, 10000));
}

void endAndReconnectOlder(uint8_t reason)
{
    olderConnectionsBefore = fake::loggedCount(OLDER_CONNECTED);
    fake::endConnection(reason);
    TEST_ASSERT_TRUE(fake::loopUntil(linkPausing, 1000));
    TEST_ASSERT_TRUE(fake::loopUntil(olderConnectedAgain, 90000));
}

} // namespace

void test_link_connects_and_sends_the_first_location()
{
    connectLink();
    fake::loop(100);
    TEST_ASSERT_TRUE(fake::logged("link: first location sent to camera 123456789a"));
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 001c")); // GPS mode
    TEST_ASSERT_TRUE(fake::askedCount("write 0022") >= 1); // a location
}

void test_link_finds_a_cccd_in_a_later_answer()
{
    fake::camera.descriptors = fake::Descriptors::CccdInSecondAnswer;
    fake::requestMs = 1; // time passes while the card works through an answer
    fake::answerMs = fake::LOOP_STEP_MS; // the next answer comes in a later loop step
    connectLink();
    TEST_ASSERT_EQUAL(1, fake::askedCount("descriptors 001f"));
    TEST_ASSERT_EQUAL(1, fake::askedCount("descriptors 0020"));
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 0020")); // notifications on
    TEST_ASSERT_FALSE(fake::logged("no answer in time"));
}

void test_link_waits_for_gps_time_before_connecting()
{
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    fake::loop(3000);
    TEST_ASSERT_TRUE(fake::logged("link: waiting for GPS time"));
    TEST_ASSERT_EQUAL(0, fake::askedCount("connect"));
}

void test_link_pauses_5_s_after_every_switch_off()
{
    connectLink();
    for (int i = 0; i < 3; i++)
        endAndReconnect(BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    TEST_ASSERT_EQUAL(3, fake::loggedCount("looking again in 5 s"));
}

void test_link_keeps_the_scan_lines_of_its_connection_attempts_out_of_the_history()
{
    connectLink();
    for (int i = 0; i < 3; i++)
        endAndReconnect(BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    TEST_ASSERT_EQUAL(0, fake::keptScanLines);
}

constexpr const char *SLOWED = "the last connection did not end by the camera's own close";

bool linkSearching()
{
    return link_control::state() == State::Searching || link_control::state() == State::Pausing;
}

// After a connection that ended badly the next uses the normal timing (link_policy::RateGuard); 'link off'
// and 'link on' start afresh.
void test_link_on_starts_the_rate_guard_afresh()
{
    fake::updates = link_policy::Updates::Frequent;
    connectLink();
    endAndReconnect(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_EQUAL(1, fake::loggedCount(SLOWED));
    fake::endConnection(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_TRUE(fake::loopUntil(linkSearching, 1000));
    link_control::stop(false);
    link_control::start();
    TEST_ASSERT_TRUE(fake::loopUntil(linkConnected, 10000));
    TEST_ASSERT_EQUAL(1, fake::loggedCount(SLOWED));
}

void test_link_pauses_longer_after_each_lost_connection()
{
    connectLink();
    for (int i = 0; i < 3; i++)
        endAndReconnect(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 5 s"));
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 15 s"));
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 60 s"));
    fake::loop(61000); // a connection that lasted starts the pauses again
    endAndReconnect(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_EQUAL(2, fake::loggedCount("looking again in 5 s"));
}

void test_link_tries_a_link_not_established_again_after_1_s_once()
{
    fake::camera.answer = fake::Answer::NotEstablished;
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    fake::loop(500);
    TEST_ASSERT_TRUE(fake::logged("not established - trying again in 1 s"));
    fake::loop(1500);
    TEST_ASSERT_EQUAL(2, fake::askedCount("connect"));
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 5 s"));
}

void test_link_stops_when_the_camera_refuses_the_time()
{
    fake::camera.refuse = {fake::Request::Write, fake::TIME};
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    fake::loop(1000);
    TEST_ASSERT_TRUE(link_control::state() == State::Stopped);
    TEST_ASSERT_TRUE(fake::logged("refused the card"));
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_link_scans_rarely_when_the_camera_is_away()
{
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::gpsFix = true;
    link_control::start();
    fake::loop(link_policy::RARE_AFTER_MS + 1000);
    TEST_ASSERT_EQUAL(2000, fake::scanIntervalMs);
    TEST_ASSERT_FALSE(fake::gpsOn);
    TEST_ASSERT_TRUE(link_control::activity() == link_control::Activity::Rare);
}

void test_link_stop_closes_the_connection()
{
    connectLink();
    link_control::stop(false);
    TEST_ASSERT_FALSE(fake::linkUp());
    TEST_ASSERT_FALSE(fake::scanning);
    TEST_ASSERT_FALSE(fake::gpsOn);
}

// The older dialect sends the locations as the newer: by the chosen timing, with the rate guard, and
// without a fix says so once.
void test_older_link_sends_locations_by_the_normal_timing()
{
    connectOlderLink();
    TEST_ASSERT_EQUAL(2, fake::askedCount("write 004c")); // the time twice, as in the newer dialect
    fake::loop(30000);
    TEST_ASSERT_EQUAL(2, fake::askedCount("write 0050")); // at once and after 25 s
}

void test_older_link_sends_locations_by_the_frequent_timing()
{
    fake::updates = link_policy::Updates::Frequent;
    connectOlderLink();
    fake::loop(11000);
    TEST_ASSERT_EQUAL(3, fake::askedCount("write 0050")); // at once, after 5 s and after 10 s
}

void test_older_link_slows_down_when_the_camera_answers_late()
{
    fake::updates = link_policy::Updates::Frequent;
    connectOlderLink();
    fake::answerMs = link_policy::LATE_ANSWER_MS + 100;
    fake::loop(30000);
    TEST_ASSERT_TRUE(fake::logged("normal timing until the end of this connection"));
    TEST_ASSERT_EQUAL(link_policy::Updates::Normal, link_control::updatesNow());
}

void test_older_link_without_a_fix_says_so_once()
{
    connectOlderLink();
    fake::loop(100);
    fake::gpsFix = false;
    fake::loop(30000);
    TEST_ASSERT_EQUAL(1, fake::loggedCount("no GPS fix - locations paused"));
    TEST_ASSERT_TRUE(linkConnected());
}

// The link sounds every connection and its end, in both dialects.
void test_link_sounds_each_connection_and_its_end_in_both_dialects()
{
    connectLink();
    endAndReconnect(BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    TEST_ASSERT_EQUAL(2, fake::connectedSounds);
    TEST_ASSERT_EQUAL(1, fake::disconnectedSounds);

    fake::reset();
    connectOlderLink();
    endAndReconnectOlder(BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    TEST_ASSERT_EQUAL(2, fake::connectedSounds);
    TEST_ASSERT_EQUAL(1, fake::disconnectedSounds);
}

void test_older_link_pauses_5_s_after_every_switch_off()
{
    connectOlderLink();
    for (int i = 0; i < 3; i++)
        endAndReconnectOlder(BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    TEST_ASSERT_EQUAL(3, fake::loggedCount("looking again in 5 s"));
}

void test_older_link_pauses_longer_after_each_lost_connection()
{
    connectOlderLink();
    for (int i = 0; i < 3; i++)
        endAndReconnectOlder(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 5 s"));
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 15 s"));
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 60 s"));
}

void test_older_link_tries_a_link_not_established_again_after_1_s_once()
{
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.answer = fake::Answer::NotEstablished;
    fake::pairCamera(pairing_record::DIALECT_OLDER);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    fake::loop(500);
    TEST_ASSERT_TRUE(fake::logged("not established - trying again in 1 s"));
    fake::loop(1500);
    TEST_ASSERT_EQUAL(2, fake::askedCount("connect"));
    TEST_ASSERT_EQUAL(1, fake::loggedCount("looking again in 5 s"));
}

void test_link_ends_a_connection_whose_disconnect_was_lost()
{
    connectLink();
    fake::loop(100);
    fake::gpsFix = false; // no locations: no time limit would end the connection
    fake::loseNextEvent();
    fake::endConnection(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_TRUE(fake::loopUntil(linkPausing, 1000));
    TEST_ASSERT_TRUE(fake::logged("the connection ended without its event"));
}

void test_older_link_ends_a_connection_whose_disconnect_was_lost()
{
    connectOlderLink();
    fake::loop(100);
    fake::gpsFix = false;
    fake::loseNextEvent();
    fake::endConnection(BLE_HCI_CONNECTION_TIMEOUT);
    TEST_ASSERT_TRUE(fake::loopUntil(linkPausing, 1000));
    TEST_ASSERT_TRUE(fake::logged("the connection ended without its event"));
}

bool linkStopped()
{
    return link_control::state() == State::Stopped;
}

void test_older_link_stops_after_three_connections_without_01()
{
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.knowsCard = false;
    fake::pairCamera(pairing_record::DIALECT_OLDER);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    TEST_ASSERT_TRUE(fake::loopUntil(linkStopped, 120000));
    TEST_ASSERT_EQUAL(3, fake::loggedCount("no 01 from the camera in 15 s"));
    TEST_ASSERT_TRUE(fake::logged("link: 3 connections in a row ended without camera 123456789a taking the card "
                                  "(last: no 01 from the camera in 15 s)"));
    TEST_ASSERT_FALSE(fake::linkUp());
}

int refusals()
{
    return fake::loggedCount("time: refused (GATT 0x0103)");
}

bool twoRefusals()
{
    return refusals() >= 2;
}

void test_older_link_counts_refusals_only_in_a_row()
{
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.refuse = {fake::Request::Write, fake::OLDER_TIME};
    fake::pairCamera(pairing_record::DIALECT_OLDER);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    TEST_ASSERT_TRUE(fake::loopUntil(twoRefusals, 70000));
    TEST_ASSERT_FALSE(linkStopped());

    fake::camera.refuse = {}; // the camera takes the card and a location: the count starts again
    olderConnectionsBefore = 0;
    TEST_ASSERT_TRUE(fake::loopUntil(olderConnectedAgain, 70000));
    fake::loop(1000);
    TEST_ASSERT_TRUE(fake::askedCount("write 0050") >= 1);

    fake::camera.refuse = {fake::Request::Write, fake::OLDER_TIME};
    fake::clearLog();
    fake::endConnection(BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    TEST_ASSERT_TRUE(fake::loopUntil(twoRefusals, 70000));
    TEST_ASSERT_FALSE(linkStopped());
    TEST_ASSERT_TRUE(fake::loopUntil(linkStopped, 70000)); // the third in a row
}

void runLinkTests()
{
    RUN_TEST(test_link_connects_and_sends_the_first_location);
    RUN_TEST(test_link_finds_a_cccd_in_a_later_answer);
    RUN_TEST(test_link_waits_for_gps_time_before_connecting);
    RUN_TEST(test_link_pauses_5_s_after_every_switch_off);
    RUN_TEST(test_link_keeps_the_scan_lines_of_its_connection_attempts_out_of_the_history);
    RUN_TEST(test_link_on_starts_the_rate_guard_afresh);
    RUN_TEST(test_link_pauses_longer_after_each_lost_connection);
    RUN_TEST(test_link_tries_a_link_not_established_again_after_1_s_once);
    RUN_TEST(test_link_stops_when_the_camera_refuses_the_time);
    RUN_TEST(test_link_scans_rarely_when_the_camera_is_away);
    RUN_TEST(test_link_stop_closes_the_connection);
    RUN_TEST(test_older_link_sends_locations_by_the_normal_timing);
    RUN_TEST(test_older_link_sends_locations_by_the_frequent_timing);
    RUN_TEST(test_older_link_slows_down_when_the_camera_answers_late);
    RUN_TEST(test_older_link_without_a_fix_says_so_once);
    RUN_TEST(test_link_sounds_each_connection_and_its_end_in_both_dialects);
    RUN_TEST(test_older_link_pauses_5_s_after_every_switch_off);
    RUN_TEST(test_older_link_pauses_longer_after_each_lost_connection);
    RUN_TEST(test_older_link_tries_a_link_not_established_again_after_1_s_once);
    RUN_TEST(test_link_ends_a_connection_whose_disconnect_was_lost);
    RUN_TEST(test_older_link_ends_a_connection_whose_disconnect_was_lost);
    RUN_TEST(test_older_link_stops_after_three_connections_without_01);
    RUN_TEST(test_older_link_counts_refusals_only_in_a_row);
}
