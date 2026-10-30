// older_session, the older dialect's connection, against the fake camera (fake.h).
#include <string.h>
#include <unity.h>

#include "fake.h"
#include "older_session.h"

using older_session::Result;
using session_end::Reason;

namespace {

// A lasting connection (the link's): polled on the test clock until it is not Running (at most 60 s).
Result runLasting()
{
    fake::gpsOn = true;
    fake::gpsFix = true;
    fake::camera.dialect = fake::Dialect::Older;
    TEST_ASSERT_TRUE(older_session::start(fake::cameraIn(lumix::STATE_ON), fake::record.uuid, true));
    return fake::runSession(older_session::poll);
}

} // namespace

void test_older_session_takes_01_after_the_device_name()
{
    TEST_ASSERT_EQUAL(Result::Accepted, runLasting());
    TEST_ASSERT_TRUE(fake::logged("older: the camera knows the card (01 on the location notify)"));
}

void test_older_session_takes_01_that_comes_before_the_device_name()
{
    fake::camera.knownAfter = fake::OLDER_DEVICE_ID;
    TEST_ASSERT_EQUAL(Result::Accepted, runLasting());
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 0046")); // the device name is still written
}

void test_older_session_takes_01_on_the_time_notify()
{
    fake::camera.knownOn = fake::OLDER_TIME_NOTIFY;
    TEST_ASSERT_EQUAL(Result::Accepted, runLasting());
    TEST_ASSERT_TRUE(fake::logged("older: the camera knows the card (01 on the time notify)"));
}

void test_older_session_without_01_is_a_card_not_taken_after_15_s()
{
    fake::camera.knowsCard = false;
    TEST_ASSERT_EQUAL(Result::Failed, runLasting());
    TEST_ASSERT_EQUAL(Reason::NotTaken, older_session::endReason());
    TEST_ASSERT_EQUAL_STRING("no 01 from the camera in 15 s", older_session::failure());
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_older_session_refused_identifier_is_rejected()
{
    fake::camera.refuse = {fake::Request::Write, fake::OLDER_DEVICE_ID};
    TEST_ASSERT_EQUAL(Result::Rejected, runLasting());
    TEST_ASSERT_EQUAL(Reason::Refused, older_session::endReason());
    TEST_ASSERT_FALSE(fake::linkUp());
}

// A lasting connection: once the camera takes the card, the open connection is the caller's (link_control
// sends the locations); the session sends none and its cancel() no longer closes it.
void test_older_session_hands_a_lasting_connection_over()
{
    TEST_ASSERT_EQUAL(Result::Accepted, runLasting());
    TEST_ASSERT_EQUAL_HEX16(fake::OLDER_LOCATION, older_session::locationHandle());
    TEST_ASSERT_EQUAL_HEX16(fake::OLDER_LOCATION_NOTIFY, older_session::locationNotifyHandle());
    fake::advance(30000);
    TEST_ASSERT_EQUAL(Result::Failed, older_session::poll()); // idle
    TEST_ASSERT_EQUAL(0, fake::askedCount("write 0050"));
    older_session::cancel();
    TEST_ASSERT_TRUE(fake::linkUp());
}

void runOlderSessionTests()
{
    RUN_TEST(test_older_session_takes_01_after_the_device_name);
    RUN_TEST(test_older_session_takes_01_that_comes_before_the_device_name);
    RUN_TEST(test_older_session_takes_01_on_the_time_notify);
    RUN_TEST(test_older_session_without_01_is_a_card_not_taken_after_15_s);
    RUN_TEST(test_older_session_refused_identifier_is_rejected);
    RUN_TEST(test_older_session_hands_a_lasting_connection_over);
}
