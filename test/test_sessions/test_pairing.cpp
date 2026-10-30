// pairing, with newer_session and older_session, against the fake camera (fake.h).
#include <unity.h>

#include <ble_hci.h>

#include "fake.h"
#include "link_control.h"
#include "pairing.h"

using pairing::Phase;
using pairing::Result;

namespace {

bool pairingIdle()
{
    return !pairing::active();
}

bool waitingForWifi()
{
    return pairing::progress().phase == Phase::WaitingForWifi;
}

// 'pair' with a GPS fix and the test camera in pairing mode next to the card.
void startPairing()
{
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_PAIRING);
    pairing::start();
}

} // namespace

void test_pairing_pairs_a_newer_camera_and_starts_the_link()
{
    startPairing();
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 30000));
    TEST_ASSERT_TRUE(pairing::progress().lastResult == Result::Paired);
    TEST_ASSERT_TRUE(fake::record.paired);
    TEST_ASSERT_EQUAL(pairing_record::DIALECT_NEWER, fake::record.dialect);
    TEST_ASSERT_EQUAL(1, fake::pairedSounds);
    TEST_ASSERT_TRUE(fake::logged("pair: paired with camera 123456789a"));
    TEST_ASSERT_TRUE(link_control::active());
}

void test_pairing_switches_to_the_older_dialect_and_waits_for_the_wifi_step()
{
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.knowsCard = false; // until the computer's Wi-Fi step
    startPairing();
    TEST_ASSERT_TRUE(fake::loopUntil(waitingForWifi, 30000));
    fake::loop(1000);
    TEST_ASSERT_TRUE(fake::logged("switching to the older dialect"));
    TEST_ASSERT_TRUE(fake::logged("older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E"));
    TEST_ASSERT_FALSE(fake::kept("register identity")); // sent for the app, not in 'log' and Copy log
    TEST_ASSERT_TRUE(pairing::active());
    fake::notify(fake::OLDER_LOCATION_NOTIFY, lumix::OLDER_KNOWN); // the Wi-Fi step is done
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 10000));
    TEST_ASSERT_TRUE(pairing::progress().lastResult == Result::Paired);
    TEST_ASSERT_EQUAL(pairing_record::DIALECT_OLDER, fake::record.dialect);
}

void test_pairing_an_older_camera_takes_the_time_of_the_last_fix_when_the_gps_lost_it()
{
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.knowsCard = false;
    startPairing();
    TEST_ASSERT_TRUE(fake::loopUntil(waitingForWifi, 30000));
    fake::gpsFix = false; // the card carried indoors to the computer
    fake::loop(120000);
    fake::notify(fake::OLDER_LOCATION_NOTIFY, lumix::OLDER_KNOWN); // the Wi-Fi step is done
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 10000));
    TEST_ASSERT_TRUE(pairing::progress().lastResult == Result::Paired);
    TEST_ASSERT_EQUAL(1, fake::loggedCount("older: no fresh GPS time - the time of the fix 120 s ago"));
}

void test_pairing_tries_a_link_that_never_came_up_five_times_at_once()
{
    fake::camera.answer = fake::Answer::NoAnswer;
    startPairing();
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 120000));
    TEST_ASSERT_EQUAL(5, fake::loggedCount("pair: quick retry"));
    TEST_ASSERT_EQUAL(6, fake::askedCount("connect"));
    TEST_ASSERT_TRUE(fake::logged("given up - no connection to the camera after 5 quick retries (last: no answer"));
    TEST_ASSERT_TRUE(pairing::progress().lastResult == Result::Failed);
}

void test_pairing_pauses_between_three_attempts_then_gives_up()
{
    fake::camera.disconnectAt = {fake::Request::Mtu, 0};
    fake::camera.disconnectReason = BLE_HCI_CONNECTION_TIMEOUT;
    startPairing();
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 120000));
    TEST_ASSERT_EQUAL(3, fake::askedCount("connect"));
    TEST_ASSERT_TRUE(fake::logged("pair: next attempt in 10 s"));
    TEST_ASSERT_TRUE(fake::logged("pair: next attempt in 30 s"));
    TEST_ASSERT_TRUE(fake::logged("pair: given up - all attempts failed"));
    TEST_ASSERT_EQUAL(1, fake::refuseSounds);
}

void test_pairing_without_a_camera_keeps_the_gps_for_another_try()
{
    fake::gpsFix = true;
    pairing::start();
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 130000));
    TEST_ASSERT_TRUE(pairing::progress().lastResult == Result::NoCamera);
    TEST_ASSERT_FALSE(fake::scanning); // as before the session
    TEST_ASSERT_TRUE(fake::gpsOn);
    TEST_ASSERT_TRUE(fake::logged("pair: GPS on for 600 s, for another try"));
    fake::loop(600000);
    TEST_ASSERT_FALSE(fake::gpsOn);
}

void test_pairing_leaves_no_scan_on_after_a_diag_scan_that_ended_meanwhile()
{
    fake::scanning = true; // a 'diag scan' run switched it on
    fake::scanForDump = true;
    fake::gpsFix = true;
    pairing::start();
    fake::loop(5000);
    fake::scanning = false; // the run ends and switches it off; the search switches it on again
    fake::scanForDump = false;
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 130000));
    TEST_ASSERT_FALSE(fake::scanning);
}

// 'link off' while pairing: the link stays off after it.
void test_pairing_keeps_the_link_off_when_asked_during_the_session()
{
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::gpsFix = true;
    link_control::start();
    fake::loop(100);
    pairing::start();
    TEST_ASSERT_TRUE(pairing::keepLinkOff());
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 130000));
    TEST_ASSERT_FALSE(link_control::active());
    TEST_ASSERT_FALSE(fake::scanning);
    fake::loop(600000); // the GPS kept for another try goes off
    TEST_ASSERT_FALSE(fake::gpsOn);
}

// 'unpair' while pairing (no camera paired any more): the link cannot start after it, and GPS and
// scanning go back as they were instead of staying on with no owner.
void test_pairing_puts_gps_and_scan_back_when_the_link_cannot_start_after_it()
{
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::gpsFix = true;
    link_control::start();
    fake::loop(100);
    pairing::start();
    fake::record.paired = false;
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 130000));
    TEST_ASSERT_FALSE(link_control::active());
    TEST_ASSERT_FALSE(fake::scanning);
    fake::loop(600000);
    TEST_ASSERT_FALSE(fake::gpsOn);
}

void test_pairing_starts_the_link_again_when_it_ends()
{
    fake::pairCamera(pairing_record::DIALECT_NEWER);
    fake::gpsFix = true;
    link_control::start();
    fake::loop(100);
    pairing::start();
    TEST_ASSERT_FALSE(link_control::active());
    TEST_ASSERT_TRUE(fake::loopUntil(pairingIdle, 130000));
    TEST_ASSERT_TRUE(link_control::active());
}

void runPairingTests()
{
    RUN_TEST(test_pairing_pairs_a_newer_camera_and_starts_the_link);
    RUN_TEST(test_pairing_switches_to_the_older_dialect_and_waits_for_the_wifi_step);
    RUN_TEST(test_pairing_an_older_camera_takes_the_time_of_the_last_fix_when_the_gps_lost_it);
    RUN_TEST(test_pairing_tries_a_link_that_never_came_up_five_times_at_once);
    RUN_TEST(test_pairing_pauses_between_three_attempts_then_gives_up);
    RUN_TEST(test_pairing_without_a_camera_keeps_the_gps_for_another_try);
    RUN_TEST(test_pairing_leaves_no_scan_on_after_a_diag_scan_that_ended_meanwhile);
    RUN_TEST(test_pairing_keeps_the_link_off_when_asked_during_the_session);
    RUN_TEST(test_pairing_puts_gps_and_scan_back_when_the_link_cannot_start_after_it);
    RUN_TEST(test_pairing_starts_the_link_again_when_it_ends);
}
