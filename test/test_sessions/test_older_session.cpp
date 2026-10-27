// older_session, the older dialect's connection, against the fake camera (fake.h).
#include <string.h>
#include <unity.h>

#include "fake.h"
#include "older_session.h"

using older_session::Result;

namespace {

camera_list::Report cameraOn()
{
    camera_list::Report report = {};
    memcpy(report.camera.serial, fake::SERIAL, sizeof(report.camera.serial));
    report.camera.state = lumix::STATE_ON;
    report.rssi = -50;
    report.kind = advert_kind::Kind::Connectable;
    return report;
}

// A lasting connection (the link's): polled on the test clock until it is not Running (at most 60 s).
Result runLasting()
{
    fake::gpsOn = true;
    fake::gpsFix = true;
    fake::camera.dialect = fake::Dialect::Older;
    TEST_ASSERT_TRUE(older_session::start(cameraOn(), fake::record.uuid, true));
    const uint32_t start = fake::now();
    Result result = Result::Running;
    while (result == Result::Running && fake::now() - start < 60000) {
        fake::advance(fake::LOOP_STEP_MS);
        result = older_session::poll();
    }
    return result;
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
    TEST_ASSERT_TRUE(older_session::cardNotTaken());
    TEST_ASSERT_EQUAL_STRING("no 01 from the camera in 15 s", older_session::failure());
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_older_session_refused_identifier_is_rejected()
{
    fake::camera.refuse = {fake::Request::Write, fake::OLDER_DEVICE_ID};
    TEST_ASSERT_EQUAL(Result::Rejected, runLasting());
    TEST_ASSERT_TRUE(older_session::cardNotTaken());
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_older_session_switch_off_gives_its_reason()
{
    TEST_ASSERT_EQUAL(Result::Accepted, runLasting());
    fake::endConnection(0x13);
    fake::advance(fake::LOOP_STEP_MS);
    TEST_ASSERT_EQUAL(Result::Failed, older_session::poll());
    TEST_ASSERT_EQUAL_HEX16(0x13, older_session::disconnectReason());
    TEST_ASSERT_FALSE(older_session::cardNotTaken());
}

void runOlderSessionTests()
{
    RUN_TEST(test_older_session_takes_01_after_the_device_name);
    RUN_TEST(test_older_session_takes_01_that_comes_before_the_device_name);
    RUN_TEST(test_older_session_takes_01_on_the_time_notify);
    RUN_TEST(test_older_session_without_01_is_a_card_not_taken_after_15_s);
    RUN_TEST(test_older_session_refused_identifier_is_rejected);
    RUN_TEST(test_older_session_switch_off_gives_its_reason);
}
