// older_session, the older dialect's connection, against the fake camera (fake.h).
#include <string.h>
#include <unity.h>

#include "camera_connection.h"
#include "fake.h"
#include "older_session.h"

using session_end::Reason;

namespace {

// A lasting connection (the link's), polled on the test clock until it ends (at most 60 s). Why it ended.
Reason runLasting()
{
    fake::gpsOn = true;
    fake::gpsFix = true;
    fake::camera.dialect = fake::Dialect::Older;
    TEST_ASSERT_TRUE(older_session::start(fake::cameraIn(lumix::STATE_ON), fake::record.uuid, true));
    TEST_ASSERT_TRUE(fake::runSession(older_session::poll));
    return camera_connection::endReason();
}

// The steps of an older connection, as the card logs them.
const char *const STEP_LINES[] = {"older: connected", "older: MTU", "older: handles", "older: key read", " written",
                             "older: notification", "older: the camera knows the card", "older: the camera took"};

} // namespace

void test_older_session_takes_01_after_the_device_name()
{
    TEST_ASSERT_EQUAL(Reason::Accepted, runLasting());
    TEST_ASSERT_TRUE(fake::logged("older: the camera knows the card (01 on the location notify)"));
}

void test_older_session_takes_01_that_comes_before_the_device_name()
{
    fake::camera.knownAfter = fake::OLDER_DEVICE_ID;
    TEST_ASSERT_EQUAL(Reason::Accepted, runLasting());
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 0046")); // the device name is still written
}

void test_older_session_takes_01_on_the_time_notify()
{
    fake::camera.knownOn = fake::OLDER_TIME_NOTIFY;
    TEST_ASSERT_EQUAL(Reason::Accepted, runLasting());
    TEST_ASSERT_TRUE(fake::logged("older: the camera knows the card (01 on the time notify)"));
}

void test_older_session_without_01_is_a_card_not_taken_after_15_s()
{
    fake::camera.knowsCard = false;
    TEST_ASSERT_EQUAL(Reason::NotTaken, runLasting());
    TEST_ASSERT_EQUAL_STRING("no 01 from the camera in 15 s", camera_connection::failure());
    TEST_ASSERT_FALSE(fake::linkUp());
}

// A lasting connection: once the camera takes the card, the open connection is the caller's (link_control
// sends the locations); the session sends none and its cancel() no longer closes it.
void test_older_session_hands_a_lasting_connection_over()
{
    TEST_ASSERT_EQUAL(Reason::Accepted, runLasting());
    TEST_ASSERT_EQUAL_HEX16(fake::OLDER_LOCATION, older_session::locationHandle());
    TEST_ASSERT_EQUAL_HEX16(fake::OLDER_LOCATION_NOTIFY, older_session::locationNotifyHandle());
    fake::advance(30000);
    TEST_ASSERT_FALSE(older_session::poll()); // idle
    TEST_ASSERT_EQUAL(0, fake::askedCount("write 0050"));
    older_session::cancel();
    TEST_ASSERT_TRUE(fake::linkUp());
}

// The 64 lines of 'log' are for the events of a walk and a whole older pairing (log.h): the steps of a
// lasting connection are only sent, a pairing's are kept.
void test_older_session_only_sends_the_steps_of_a_lasting_connection()
{
    TEST_ASSERT_EQUAL(Reason::Accepted, runLasting());
    for (const char *step : STEP_LINES) {
        TEST_ASSERT_TRUE_MESSAGE(fake::logged(step), step);
        TEST_ASSERT_FALSE_MESSAGE(fake::kept(step), step);
    }
}

void test_older_session_keeps_the_steps_of_a_pairing()
{
    fake::gpsOn = true;
    fake::gpsFix = true;
    fake::camera.dialect = fake::Dialect::Older;
    TEST_ASSERT_TRUE(older_session::start(fake::cameraIn(lumix::STATE_PAIRING), fake::record.uuid, false));
    TEST_ASSERT_TRUE(fake::runSession(older_session::poll));
    TEST_ASSERT_EQUAL(Reason::Accepted, camera_connection::endReason());
    for (const char *step : STEP_LINES)
        TEST_ASSERT_TRUE_MESSAGE(fake::kept(step), step);
}

void runOlderSessionTests()
{
    RUN_TEST(test_older_session_takes_01_after_the_device_name);
    RUN_TEST(test_older_session_takes_01_that_comes_before_the_device_name);
    RUN_TEST(test_older_session_takes_01_on_the_time_notify);
    RUN_TEST(test_older_session_without_01_is_a_card_not_taken_after_15_s);
    RUN_TEST(test_older_session_hands_a_lasting_connection_over);
    RUN_TEST(test_older_session_only_sends_the_steps_of_a_lasting_connection);
    RUN_TEST(test_older_session_keeps_the_steps_of_a_pairing);
}
