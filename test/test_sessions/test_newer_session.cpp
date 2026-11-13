// newer_session, the newer dialect's introduction, against the fake camera (fake.h).
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <ble_hci.h>

#include "camera_connection.h"
#include "fake.h"
#include "newer_session.h"

using fake::Request;
using session_end::Reason;

namespace {

bool startSession(bool keepOpen)
{
    fake::gpsOn = true;
    fake::gpsFix = true;
    return newer_session::start(fake::cameraIn(keepOpen ? lumix::STATE_ON : lumix::STATE_PAIRING), fake::record.uuid,
                                 keepOpen);
}

// Polls the session until it ends; *tookMs: how long it ran. Why it ended.
Reason run(uint32_t *tookMs = nullptr)
{
    TEST_ASSERT_TRUE(fake::runSession(newer_session::poll, tookMs));
    return camera_connection::endReason();
}

// The time limit of a step after connecting (camera_link.h).
uint32_t limitOf(const fake::Step &step)
{
    return step.request == Request::Mtu ? camera_link::MTU_TIMEOUT_MS : camera_link::REQUEST_TIMEOUT_MS;
}

} // namespace

void test_session_introduces_the_card_and_closes_the_connection()
{
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Reason::Accepted, run());
    TEST_ASSERT_EQUAL(1, fake::askedCount("write 001a")); // the time, once when pairing
    TEST_ASSERT_EQUAL(1, fake::askedCount("close"));
    TEST_ASSERT_FALSE(fake::linkUp());
}

void test_session_of_a_lasting_connection_writes_the_time_twice_and_stays()
{
    TEST_ASSERT_TRUE(startSession(true));
    TEST_ASSERT_EQUAL(Reason::Accepted, run());
    TEST_ASSERT_EQUAL(2, fake::askedCount("write 001a"));
    TEST_ASSERT_TRUE(fake::linkUp());
}

void test_session_with_the_clock_off_writes_no_time()
{
    fake::clockOn = false;
    TEST_ASSERT_TRUE(startSession(true));
    TEST_ASSERT_EQUAL(Reason::Accepted, run());
    TEST_ASSERT_EQUAL(0, fake::askedCount("write 001a"));
    TEST_ASSERT_FALSE(newer_session::writesTime());
}

void test_session_ends_when_the_camera_disconnects_in_any_step()
{
    for (const fake::Step &step : fake::NEWER_STEPS) {
        for (const uint8_t reason : {BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION, BLE_HCI_CONNECTION_TIMEOUT}) {
            fake::reset();
            fake::camera.disconnectAt = {step.request, step.handle};
            fake::camera.disconnectReason = reason;
            TEST_ASSERT_TRUE(startSession(false));
            TEST_ASSERT_EQUAL_MESSAGE(Reason::EndedEarly, run(), step.name);
            char expected[64];
            snprintf(expected, sizeof(expected), "%s: the camera disconnected (HCI 0x%02x)", step.name, reason);
            TEST_ASSERT_EQUAL_STRING(expected, camera_connection::failure());
            TEST_ASSERT_FALSE(fake::linkUp());
        }
    }
}

void test_session_ends_when_a_step_gets_no_answer()
{
    for (const fake::Step &step : fake::NEWER_STEPS) {
        fake::reset();
        fake::camera.silent = {step.request, step.handle};
        TEST_ASSERT_TRUE(startSession(false));
        uint32_t took = 0;
        TEST_ASSERT_EQUAL_MESSAGE(Reason::Failed, run(&took), step.name);
        // The step's limit, then the session waits for the end of the connection it closes.
        TEST_ASSERT_UINT32_WITHIN_MESSAGE(2 * fake::LOOP_STEP_MS, limitOf(step) + fake::CLOSE_MS, took, step.name);
        char expected[64];
        snprintf(expected, sizeof(expected), "%s: no answer in %lu s", step.name, (unsigned long)(limitOf(step) / 1000));
        TEST_ASSERT_EQUAL_STRING(expected, camera_connection::failure());
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), step.name); // closed by the card
    }
}

void test_session_without_an_answer_to_connect_cancels_it_after_5_s()
{
    fake::camera.answer = fake::Answer::NoAnswer;
    TEST_ASSERT_TRUE(startSession(false));
    uint32_t took = 0;
    TEST_ASSERT_EQUAL(Reason::NoAnswer, run(&took));
    TEST_ASSERT_UINT32_WITHIN(2 * fake::LOOP_STEP_MS, camera_link::CONNECT_TIMEOUT_MS, took);
    TEST_ASSERT_EQUAL_STRING("connect: no answer in 5 s", camera_connection::failure());
    TEST_ASSERT_EQUAL(1, fake::askedCount("close"));
    TEST_ASSERT_FALSE(fake::connectionWanted()); // one that comes up now is closed at once
}

void test_session_takes_another_error_of_the_service_search_as_a_failure_only()
{
    fake::camera.refuse = {Request::Service, 0};
    fake::camera.refuseStatus = 0x0105; // insufficient authentication: not "attribute not found"
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Reason::Failed, run());
    TEST_ASSERT_EQUAL_STRING("service: not found (GATT 0x0105)", camera_connection::failure());
}

void test_session_refused_time_means_an_unknown_card_but_other_refusals_do_not()
{
    fake::camera.refuse = {Request::Write, fake::TIME};
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Reason::Refused, run());

    fake::reset();
    fake::camera.refuse = {Request::Write, fake::DEVICE_ID};
    TEST_ASSERT_TRUE(startSession(false));
    TEST_ASSERT_EQUAL(Reason::Failed, run());
    TEST_ASSERT_EQUAL_STRING("identifier: refused (GATT 0x0103)", camera_connection::failure());
}

void runNewerSessionTests()
{
    RUN_TEST(test_session_introduces_the_card_and_closes_the_connection);
    RUN_TEST(test_session_of_a_lasting_connection_writes_the_time_twice_and_stays);
    RUN_TEST(test_session_with_the_clock_off_writes_no_time);
    RUN_TEST(test_session_ends_when_the_camera_disconnects_in_any_step);
    RUN_TEST(test_session_ends_when_a_step_gets_no_answer);
    RUN_TEST(test_session_without_an_answer_to_connect_cancels_it_after_5_s);
    RUN_TEST(test_session_takes_another_error_of_the_service_search_as_a_failure_only);
    RUN_TEST(test_session_refused_time_means_an_unknown_card_but_other_refusals_do_not);
}
