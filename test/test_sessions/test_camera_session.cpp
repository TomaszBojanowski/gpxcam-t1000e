// camera_session, the newer dialect's introduction, against the fake camera (fake.h).
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <ble_hci.h>

#include "camera_session.h"
#include "fake.h"

using camera_session::Result;
using fake::Request;

namespace {

camera_list::Report cameraIn(uint8_t state)
{
    camera_list::Report report = {};
    memcpy(report.camera.serial, fake::SERIAL, sizeof(report.camera.serial));
    report.camera.state = state;
    report.rssi = -50;
    report.kind = advert_kind::Kind::Connectable;
    return report;
}

bool startSession(bool keepOpen)
{
    fake::gpsOn = true;
    fake::gpsFix = true;
    return camera_session::start(cameraIn(keepOpen ? lumix::STATE_ON : lumix::STATE_PAIRING), fake::record.uuid,
                                 keepOpen);
}

// Polls the session on the test clock until it ends (at most 60 s); *tookMs: how long it ran.
Result run(uint32_t *tookMs = nullptr)
{
    const uint32_t start = fake::now();
    Result result = Result::Running;
    while (result == Result::Running && fake::now() - start < 60000) {
        fake::advance(fake::LOOP_STEP_MS);
        result = camera_session::poll();
    }
    if (tookMs != nullptr)
        *tookMs = fake::now() - start;
    return result;
}

// The steps after the connection, the request that starts each and its time limit.
struct StepCase {
    Request request;
    uint16_t handle;
    const char *name;
    uint32_t limitMs;
};
const StepCase STEPS[] = {
    {Request::Mtu, 0, "MTU", 3000},
    {Request::Service, 0, "service", 5000},
    {Request::Characteristics, 0, "characteristics", 5000},
    {Request::Read, fake::KEY, "key", 5000},
    {Request::Write, fake::DEVICE_ID, "identifier", 5000},
    {Request::Write, fake::APP_NAME, "app name", 5000},
    {Request::Write, fake::DEVICE_NAME, "device name", 5000},
    {Request::Write, fake::TIME, "time", 5000},
};

} // namespace

void test_session_introduces_the_card_and_closes_the_connection()
{
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Result::Accepted, run());
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 001a")); // the time, once when pairing
    TEST_ASSERT_EQUAL(1, fake::askedCount("disconnect"));
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_session_of_a_lasting_connection_writes_the_time_twice_and_stays()
{
    TEST_ASSERT_TRUE(startSession(true));
    TEST_ASSERT_EQUAL(Result::Accepted, run());
    TEST_ASSERT_EQUAL(2, fake::askedCount("write 001a"));
    TEST_ASSERT_TRUE(fake::linkUp());
}

void test_session_with_the_clock_off_writes_no_time()
{
    fake::clockOn = false;
    TEST_ASSERT_TRUE(startSession(true));
    TEST_ASSERT_EQUAL(Result::Accepted, run());
    TEST_ASSERT_EQUAL(0, fake::askedCount("write 001a"));
    TEST_ASSERT_FALSE(camera_session::writesTime());
}

void test_session_ends_when_the_camera_disconnects_in_any_step()
{
    for (const StepCase &step : STEPS) {
        for (const uint8_t reason : {BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION, BLE_HCI_CONNECTION_TIMEOUT}) {
            fake::reset();
            fake::camera.disconnectAt = {step.request, step.handle};
            fake::camera.disconnectReason = reason;
            TEST_ASSERT_TRUE(startSession(false));
            TEST_ASSERT_EQUAL_MESSAGE(Result::Failed, run(), step.name);
            char expected[64];
            snprintf(expected, sizeof(expected), "%s: the camera disconnected (HCI 0x%02x)", step.name, reason);
            TEST_ASSERT_EQUAL_STRING(expected, camera_session::failure());
            TEST_ASSERT_TRUE_MESSAGE(camera_session::endedAfterConnecting(), step.name);
            TEST_ASSERT_TRUE(camera_session::linkFailure() == camera_link::LinkFailure::None);
            TEST_ASSERT_FALSE(fake::linkUp());
        }
    }
}

void test_session_ends_when_a_step_gets_no_answer()
{
    for (const StepCase &step : STEPS) {
        fake::reset();
        fake::camera.silent = {step.request, step.handle};
        TEST_ASSERT_TRUE(startSession(false));
        uint32_t took = 0;
        TEST_ASSERT_EQUAL_MESSAGE(Result::Failed, run(&took), step.name);
        TEST_ASSERT_UINT32_WITHIN_MESSAGE(2 * fake::LOOP_STEP_MS, step.limitMs, took, step.name);
        char expected[64];
        snprintf(expected, sizeof(expected), "%s: no answer in %lu s", step.name, (unsigned long)(step.limitMs / 1000));
        TEST_ASSERT_EQUAL_STRING(expected, camera_session::failure());
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), step.name); // closed by the card
    }
}

void test_session_without_an_answer_to_connect_cancels_it_after_5_s()
{
    fake::camera.answer = fake::Answer::NoAnswer;
    TEST_ASSERT_TRUE(startSession(false));
    uint32_t took = 0;
    TEST_ASSERT_EQUAL(Result::Failed, run(&took));
    TEST_ASSERT_UINT32_WITHIN(2 * fake::LOOP_STEP_MS, camera_link::CONNECT_TIMEOUT_MS, took);
    TEST_ASSERT_EQUAL_STRING("connect: no answer in 5 s", camera_session::failure());
    TEST_ASSERT_EQUAL(1, fake::askedCount("cancel connect"));
    TEST_ASSERT_TRUE(camera_session::linkFailure() == camera_link::LinkFailure::NoAnswer);
    TEST_ASSERT_FALSE(camera_session::endedAfterConnecting());
}

void test_session_tells_a_link_that_was_not_established()
{
    fake::camera.answer = fake::Answer::NotEstablished;
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Result::Failed, run());
    TEST_ASSERT_TRUE(camera_session::notEstablished());
    TEST_ASSERT_TRUE(camera_session::linkFailure() == camera_link::LinkFailure::NotEstablished);
    TEST_ASSERT_FALSE(camera_session::endedAfterConnecting());
}

void test_session_finds_no_newer_dialect_on_an_older_camera()
{
    fake::camera.dialect = fake::Dialect::Older;
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Result::Failed, run());
    TEST_ASSERT_TRUE(camera_session::lacksNewerService());
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_session_takes_another_error_of_the_service_search_as_a_failure_only()
{
    fake::camera.refuse = {Request::Service, 0};
    fake::camera.refuseStatus = 0x0105; // insufficient authentication: not "attribute not found"
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Result::Failed, run());
    TEST_ASSERT_FALSE(camera_session::lacksNewerService());
    TEST_ASSERT_EQUAL_STRING("service: not found (GATT 0x0105)", camera_session::failure());
}

void test_session_refused_time_means_an_unknown_card_but_other_refusals_do_not()
{
    fake::camera.refuse = {Request::Write, fake::TIME};
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Result::Rejected, run());

    fake::reset();
    fake::camera.refuse = {Request::Write, fake::DEVICE_ID};
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Result::Failed, run());
    TEST_ASSERT_EQUAL_STRING("identifier: refused (GATT 0x0103)", camera_session::failure());
}

void test_session_never_connects_to_a_camera_in_standby()
{
    fake::gpsOn = fake::gpsFix = true;
    TEST_ASSERT_FALSE(camera_session::start(cameraIn(lumix::STATE_STANDBY), fake::record.uuid, true));
    TEST_ASSERT_EQUAL(0, fake::askedCount("connect"));
}

void runCameraSessionTests()
{
    RUN_TEST(test_session_introduces_the_card_and_closes_the_connection);
    RUN_TEST(test_session_of_a_lasting_connection_writes_the_time_twice_and_stays);
    RUN_TEST(test_session_with_the_clock_off_writes_no_time);
    RUN_TEST(test_session_ends_when_the_camera_disconnects_in_any_step);
    RUN_TEST(test_session_ends_when_a_step_gets_no_answer);
    RUN_TEST(test_session_without_an_answer_to_connect_cancels_it_after_5_s);
    RUN_TEST(test_session_tells_a_link_that_was_not_established);
    RUN_TEST(test_session_finds_no_newer_dialect_on_an_older_camera);
    RUN_TEST(test_session_takes_another_error_of_the_service_search_as_a_failure_only);
    RUN_TEST(test_session_refused_time_means_an_unknown_card_but_other_refusals_do_not);
    RUN_TEST(test_session_never_connects_to_a_camera_in_standby);
}
