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

namespace {

struct Dialect {
    const char *name;
    fake::Dialect camera;
    bool (*start)(const camera_list::Report &camera, const uint8_t *cardUuid, bool keepOpen);
    bool (*poll)();
    void (*cancel)();
    const fake::Step *steps;
    size_t stepCount;
};

const Dialect DIALECTS[] = {
    {"newer", fake::Dialect::Newer, newer_session::start, newer_session::poll, newer_session::cancel,
     fake::NEWER_STEPS, sizeof(fake::NEWER_STEPS) / sizeof(fake::NEWER_STEPS[0])},
    {"older", fake::Dialect::Older, older_session::start, older_session::poll, older_session::cancel,
     fake::OLDER_STEPS, sizeof(fake::OLDER_STEPS) / sizeof(fake::OLDER_STEPS[0])},
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

// Polls the session until it ends. Why it ended.
Reason run(const Dialect &dialect)
{
    TEST_ASSERT_TRUE(fake::runSession(dialect.poll));
    return camera_connection::endReason();
}

// Another session can start: the last one let go of the connection, and one it closed has ended.
void assertNextSessionStarts(const Dialect &dialect, const char *what)
{
    const Dialect &next = other(dialect);
    fake::advance(fake::CLOSE_MS);
    TEST_ASSERT_TRUE_MESSAGE(start(next), what);
    next.cancel();
}

} // namespace

void test_no_session_starts_on_a_camera_in_standby()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_FALSE_MESSAGE(start(dialect, lumix::STATE_STANDBY), dialect.name);
        TEST_ASSERT_EQUAL_STRING("the camera is in standby", camera_connection::failure());
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
        TEST_ASSERT_EQUAL_STRING("an earlier connection was still open - closed now", camera_connection::failure());
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), dialect.name);
        fake::advance(fake::CLOSE_MS);
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
        TEST_ASSERT_EQUAL_STRING("a session is running", camera_connection::failure());
        TEST_ASSERT_EQUAL_MESSAGE(1, fake::askedCount("connect"), dialect.name);
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), dialect.name); // the running session keeps its connection
        dialect.cancel();
        assertNextSessionStarts(dialect, dialect.name);
    }
}

// The card's connection ends at its Disconnected, a connection event after close(): until then another
// session is refused. A session that must hand over to another (pairing's newer to older) waits for it.
void test_a_session_started_before_a_closed_connection_ended_is_refused()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        fake::camera.silent = {Request::Mtu, 0};
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        fake::pollFor(50, [&] { dialect.poll(); });
        dialect.cancel();
        const Dialect &next = other(dialect);
        TEST_ASSERT_FALSE_MESSAGE(start(next), dialect.name);
        TEST_ASSERT_EQUAL_STRING("an earlier connection was still open - closed now", camera_connection::failure());
        assertNextSessionStarts(dialect, dialect.name);
    }
}

void test_cancel_in_every_step_closes_the_connection_and_lets_the_next_session_start()
{
    for (const Dialect &dialect : DIALECTS) {
        for (size_t i = 0; i < dialect.stepCount; i++) {
            const fake::Step &step = dialect.steps[i];
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
            const fake::Step &step = dialect.steps[i];
            fake::reset();
            fake::camera.disconnectAt = {step.request, step.handle};
            fake::camera.disconnectReason = BLE_HCI_CONNECTION_TIMEOUT;
            TEST_ASSERT_TRUE_MESSAGE(start(dialect), step.name);
            TEST_ASSERT_EQUAL_MESSAGE(Reason::EndedEarly, run(dialect), step.name);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(camera_connection::failure(), "the camera disconnected (HCI 0x08)"), step.name);
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
        TEST_ASSERT_EQUAL_MESSAGE(Reason::Failed, run(dialect), dialect.name);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(camera_connection::failure(), "no ATT answer for 30 s"), dialect.name);
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
        TEST_ASSERT_EQUAL_MESSAGE(Reason::Accepted, run(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(1, fake::loggedCount(camera_link::SECURITY_REQUEST_LINE), dialect.name);
    }
}

// A lasting connection the camera took is the link's: the session let go of it, so the next start
// closes it as an earlier connection.
void test_a_lasting_connection_is_given_to_the_link()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(Reason::Accepted, run(dialect), dialect.name);
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), dialect.name);
        const Dialect &next = other(dialect);
        TEST_ASSERT_FALSE_MESSAGE(start(next), dialect.name);
        TEST_ASSERT_EQUAL_STRING("an earlier connection was still open - closed now", camera_connection::failure());
        assertNextSessionStarts(dialect, dialect.name);
    }
}

// Pairing: the newer session closes the connection at the end and waits for its Disconnected, so that
// 'pair' can switch to the older dialect at once; the older keeps it until pairing closes it (pairing.cpp).
void test_a_pairing_session_has_the_connection_until_it_ends()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        TEST_ASSERT_TRUE_MESSAGE(start(dialect, lumix::STATE_PAIRING, false), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(Reason::Accepted, run(dialect), dialect.name);
        if (dialect.camera == fake::Dialect::Older) {
            TEST_ASSERT_FALSE(start(other(dialect)));
            TEST_ASSERT_EQUAL_STRING("a session is running", camera_connection::failure());
            dialect.cancel();
        }
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), dialect.name);
        assertNextSessionStarts(dialect, dialect.name);
    }
}

// The second time write of a lasting connection only makes the zone of the first count: the newer dialect
// lets a refusal of it go; in the older one every refused write is a refusal, which the link counts.
void test_a_refused_second_time_write_is_a_refusal_only_in_the_older_dialect()
{
    for (const Dialect &dialect : DIALECTS) {
        fake::reset();
        const bool older = dialect.camera == fake::Dialect::Older;
        fake::camera.refuse = {Request::Write, older ? fake::OLDER_TIME : fake::TIME, 1};
        TEST_ASSERT_TRUE_MESSAGE(start(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(older ? Reason::Refused : Reason::Accepted, run(dialect), dialect.name);
        TEST_ASSERT_EQUAL_MESSAGE(2, fake::askedCount(older ? "write 004c" : "write 001a"), dialect.name);
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
    RUN_TEST(test_a_session_started_before_a_closed_connection_ended_is_refused);
    RUN_TEST(test_cancel_in_every_step_closes_the_connection_and_lets_the_next_session_start);
    RUN_TEST(test_a_disconnect_in_every_step_ends_the_session_early);
    RUN_TEST(test_a_gatt_timeout_ends_the_session);
    RUN_TEST(test_a_request_for_encryption_is_logged_and_the_session_goes_on);
    RUN_TEST(test_a_lasting_connection_is_given_to_the_link);
    RUN_TEST(test_a_pairing_session_has_the_connection_until_it_ends);
    RUN_TEST(test_a_refused_second_time_write_is_a_refusal_only_in_the_older_dialect);
    RUN_TEST(test_the_mtu_is_the_cameras_up_to_the_cards);
    RUN_TEST(test_the_time_packet_is_fresh_or_from_the_last_fix);
}
