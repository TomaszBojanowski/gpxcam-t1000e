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

} // namespace

void test_link_connects_and_sends_the_first_location()
{
    connectLink();
    fake::loop(100);
    TEST_ASSERT_TRUE(fake::logged("link: first location sent to camera 123456789a"));
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 001c")); // GPS mode
    TEST_ASSERT_TRUE(fake::askedCount("write 0022") >= 1); // a location
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

void runLinkTests()
{
    RUN_TEST(test_link_connects_and_sends_the_first_location);
    RUN_TEST(test_link_waits_for_gps_time_before_connecting);
    RUN_TEST(test_link_pauses_5_s_after_every_switch_off);
    RUN_TEST(test_link_pauses_longer_after_each_lost_connection);
    RUN_TEST(test_link_tries_a_link_not_established_again_after_1_s_once);
    RUN_TEST(test_link_stops_when_the_camera_refuses_the_time);
    RUN_TEST(test_link_scans_rarely_when_the_camera_is_away);
    RUN_TEST(test_link_stop_closes_the_connection);
}
