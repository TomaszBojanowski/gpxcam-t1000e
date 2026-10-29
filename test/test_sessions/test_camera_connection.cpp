// camera_connection, what both introductions do alike, through the session of each dialect against the
// fake camera (fake.h): starting, one session at a time, cancelling and a disconnect in every step, a
// GATT timeout, the camera asking for encryption, the MTU and the time packet.
#include <string.h>
#include <unity.h>

#include <ble_gatt.h>
#include <ble_hci.h>

#include "camera_connection.h"
#include "camera_link.h"
#include "fake.h"
#include "newer_session.h"
#include "older_session.h"

using fake::Request;
using session_end::Reason;
using session_end::Result;

namespace {

struct Step {
    Request request;
    uint16_t handle;
    const char *name;
};

// The steps after connecting; the camera stays silent or disconnects at the request that starts each.
const Step NEWER_STEPS[] = {
    {Request::Mtu, 0, "MTU"},
    {Request::Service, 0, "service"},
    {Request::Characteristics, 0, "characteristics"},
    {Request::Read, fake::KEY, "key"},
    {Request::Write, fake::DEVICE_ID, "identifier"},
    {Request::Write, fake::APP_NAME, "app name"},
    {Request::Write, fake::DEVICE_NAME, "device name"},
    {Request::Write, fake::TIME, "time"},
};
const Step OLDER_STEPS[] = {
    {Request::Mtu, 0, "MTU"},
    {Request::Characteristics, 0, "characteristics"},
    {Request::Read, fake::OLDER_KEY, "key"},
    {Request::Write, fake::OLDER_DEVICE_ID, "identifier"},
    {Request::Write, fake::OLDER_DEVICE_NAME, "device name"},
    {Request::Read, fake::OLDER_CAMERA_NAME, "camera name"},
    {Request::Read, fake::OLDER_CAMERA_TEXT, "camera text"},
    {Request::Write, fake::OLDER_TIME, "time"},
};

struct Dialect {
    const char *name;
    fake::Dialect camera;
    bool (*start)(const camera_list::Report &camera, const uint8_t *cardUuid, bool keepOpen);
    Result (*poll)();
    Reason (*endReason)();
    const char *(*failure)();
    void (*cancel)();
    const Step *steps;
    size_t stepCount;
};

const Dialect DIALECTS[] = {
    {"newer", fake::Dialect::Newer, newer_session::start, newer_session::poll, newer_session::endReason,
     newer_session::failure, newer_session::cancel, NEWER_STEPS, sizeof(NEWER_STEPS) / sizeof(NEWER_STEPS[0])},
    {"older", fake::Dialect::Older, older_session::start, older_session::poll, older_session::endReason,
     older_session::failure, older_session::cancel, OLDER_STEPS, sizeof(OLDER_STEPS) / sizeof(OLDER_STEPS[0])},
};

const Dialect &other(const Dialect &dialect)
{
    return &dialect == &DIALECTS[0] ? DIALECTS[1] : DIALECTS[0];
}

// The session of the dialect with a camera of that dialect in this state, with a GPS fix.
bool start(const Dialect &dialect, uint8_t state = lumix::STATE_ON, bool keepOpen = true)
{
    fake::gpsOn = fake::gpsFix = true;
    fake::camera.dialect = dialect.camera;
    return dialect.start(fake::cameraIn(state), fake::record.uuid, keepOpen);
}

Result run(const Dialect &dialect)
{
    return fake::runSession(dialect.poll);
}

// Another session can start: the last one no longer has the connection.
void assertNextSessionStarts(const Dialect &dialect, const char *what)
{
    const Dialect &next = other(dialect);
    TEST_ASSERT_TRUE_MESSAGE(start(next), what);
    next.cancel();
}

} // namespace

void test_no_session_starts_on_a_camera_in_standby()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_FALSE_MESSAGE(start(dialect, lumix::STATE_STANDBY), dialect.name);
        TEST_ASSERT_EQUAL_STRING("the camera is in standby", dialect.failure());
        TEST_ASSERT_EQUAL_MESSAGE(0, fake::askedCount("connect"), dialect.name);
        assertNextSessionStarts(dialect, dialect.name);
    }
}

void test_a_session_closes_an_earlier_connection_instead_of_starting()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_TRUE(camera_link::connect(fake::cameraIn(lumix::STATE_ON)));
        TEST_ASSERT_TRUE(fake::linkUp());
        TEST_ASSERT_FALSE_MESSAGE(start(dialect), dialect.name);
        TEST_ASSERT_EQUAL_STRING("an earlier connection was still open - closed now", dialect.failure());
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), dialect.name);
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name); // the next start works
        dialect.cancel();
    }
}

void test_one_session_at_a_time()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        fake::camera.silent = {Request::Mtu, 0};
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        fake::pollFor(100, [&] { dialect.poll(); });
        const Dialect &next = other(dialect);
        TEST_ASSERT_FALSE_MESSAGE(start(next), dialect.name);
        TEST_ASSERT_EQUAL_STRING("a session is running", next.failure());
        TEST_ASSERT_EQUAL_MESSAGE(1, fake::askedCount("connect"), dialect.name);
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), dialect.name); // the running session keeps its connection
        dialect.cancel();
        assertNextSessionStarts(dialect, dialect.name);
    }
}

void test_cancel_in_every_step_closes_the_connection_and_lets_the_next_session_start()
{
    for (const Dialect &dialect : DIALECTS) {
        for (size_t i = 0; i < dialect.stepCount; i++) {
            const Step &step = dialect.steps[i];
            fake::reset();
            fake::camera.silent = {step.request, step.handle};
            TEST_ASSERT_TRUE_MESSAGE(start(dialect), step.name);
            fake::pollFor(50, [&] { dialect.poll(); });
            TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), step.name);
            dialect.cancel();
            TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), step.name);
            TEST_ASSERT_FALSE_MESSAGE(fake::connectionWanted(), step.name);
            assertNextSessionStarts(dialect, step.name);
        }
    }
}

void test_a_disconnect_in_every_step_ends_the_session_early()
{
    for (const Dialect &dialect : DIALECTS) {
        for (size_t i = 0; i < dialect.stepCount; i++) {
            const Step &step = dialect.steps[i];
            fake::reset();
            fake::camera.disconnectAt = {step.request, step.handle};
            fake::camera.disconnectReason = BLE_HCI_CONNECTION_TIMEOUT;
            TEST_ASSERT_TRUE_MESSAGE(start(dialect), step.name);
            TEST_ASSERT_EQUAL_MESSAGE(Result::Failed, run(dialect), step.name);
            TEST_ASSERT_EQUAL_MESSAGE(Reason::EndedEarly, dialect.endReason(), step.name);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(dialect.failure(), "the camera disconnected (HCI 0x08)"), step.name);
            TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), step.name);
            assertNextSessionStarts(dialect, step.name);
        }
    }
}

void test_a_gatt_timeout_ends_the_session()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        fake::camera.silent = {Request::Mtu, 0};
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        fake::pollFor(100, [&] { dialect.poll(); });
        fake::gattTimeout();
        TEST_ASSERT_EQUAL_MESSAGE(Result::Failed, run(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(Reason::Failed, dialect.endReason(), dialect.name);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(dialect.failure(), "no ATT answer for 30 s"), dialect.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), dialect.name);
        assertNextSessionStarts(dialect, dialect.name);
    }
}

// The card does not answer a request for encryption (as the gpxcam app); the introduction goes on.
void test_a_request_for_encryption_is_logged_and_the_session_goes_on()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        fake::askForEncryption();
        TEST_ASSERT_EQUAL_MESSAGE(Result::Accepted, run(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(1, fake::loggedCount(camera_link::SECURITY_REQUEST_LINE), dialect.name);
    }
}

// A lasting connection the camera took is the link's: the session no longer has it, so the next start
// closes it as an earlier connection.
void test_a_lasting_connection_is_given_to_the_link()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(Result::Accepted, run(dialect), dialect.name);
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), dialect.name);
        const Dialect &next = other(dialect);
        TEST_ASSERT_FALSE_MESSAGE(start(next), dialect.name);
        TEST_ASSERT_EQUAL_STRING("an earlier connection was still open - closed now", next.failure());
        assertNextSessionStarts(dialect, dialect.name);
    }
}

// Pairing: the newer session closes the connection at the end; the older keeps it until pairing cancels it
// (pairing.cpp).
void test_a_pairing_session_has_the_connection_until_it_ends()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_TRUE_MESSAGE(start(dialect, lumix::STATE_PAIRING, false), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(Result::Accepted, run(dialect), dialect.name);
        if (dialect.camera == fake::Dialect::Older) {
            TEST_ASSERT_FALSE(start(other(dialect)));
            TEST_ASSERT_EQUAL_STRING("a session is running", other(dialect).failure());
            dialect.cancel();
        }
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), dialect.name);
        assertNextSessionStarts(dialect, dialect.name);
    }
}

void test_the_mtu_is_the_cameras_up_to_the_cards()
{
    camera_link::Event event = {};
    event.type = camera_link::EventType::MtuExchanged;
    event.status = BLE_GATT_STATUS_SUCCESS;
    event.mtu = 57; // the S5II
    TEST_ASSERT_EQUAL_UINT16(57, camera_connection::mtu(event));
    event.mtu = 247;
    TEST_ASSERT_EQUAL_UINT16(camera_link::MAX_MTU, camera_connection::mtu(event));
    event.status = BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED; // any error
    TEST_ASSERT_EQUAL_UINT16(BLE_GATT_ATT_MTU_DEFAULT, camera_connection::mtu(event));
}

void test_the_time_packet_is_fresh_or_from_the_last_fix()
{
    uint8_t data[lumix::TIME_LENGTH] = {};
    camera_connection::TimeSource source = {};
    fake::gpsOn = fake::gpsFix = true;
    fake::loop(100);
    TEST_ASSERT_TRUE(camera_connection::timePacket(camera_connection::FRESH_TIME_ONLY, data, &source));
    TEST_ASSERT_FALSE(source.fromLastFix);
    TEST_ASSERT_EQUAL_HEX8(0xEA, data[0]); // 2026, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x07, data[1]);

    fake::gpsFix = false; // the card carried indoors
    fake::advance(120000);
    TEST_ASSERT_FALSE(camera_connection::timePacket(camera_connection::FRESH_TIME_ONLY, data, &source));
    TEST_ASSERT_TRUE(camera_connection::timePacket(600000, data, &source));
    TEST_ASSERT_TRUE(source.fromLastFix);
    TEST_ASSERT_EQUAL_UINT32(120000, source.fixAgeMs);
    fake::advance(480001);
    TEST_ASSERT_FALSE(camera_connection::timePacket(600000, data, &source));
}

void runCameraConnectionTests()
{
    RUN_TEST(test_no_session_starts_on_a_camera_in_standby);
    RUN_TEST(test_a_session_closes_an_earlier_connection_instead_of_starting);
    RUN_TEST(test_one_session_at_a_time);
    RUN_TEST(test_cancel_in_every_step_closes_the_connection_and_lets_the_next_session_start);
    RUN_TEST(test_a_disconnect_in_every_step_ends_the_session_early);
    RUN_TEST(test_a_gatt_timeout_ends_the_session);
    RUN_TEST(test_a_request_for_encryption_is_logged_and_the_session_goes_on);
    RUN_TEST(test_a_lasting_connection_is_given_to_the_link);
    RUN_TEST(test_a_pairing_session_has_the_connection_until_it_ends);
    RUN_TEST(test_the_mtu_is_the_cameras_up_to_the_cards);
    RUN_TEST(test_the_time_packet_is_fresh_or_from_the_last_fix);
}
