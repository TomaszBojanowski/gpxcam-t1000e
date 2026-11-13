// After stop, cancel or the end of a session the connection is closed, in every step of every state
// machine: no connection is left open without an owner (the camera showing a connection that gets no
// locations). Against the fake camera (fake.h).
#include <string.h>
#include <unity.h>

#include "newer_session.h"
#include "fake.h"
#include "link_control.h"
#include "older_session.h"
#include "pairing.h"

using fake::Request;

namespace {

void withGps()
{
    fake::gpsOn = true;
    fake::gpsFix = true;
}

// 'link on' with the camera on and a fix; true once the link is in the connection (or still connecting).
void startLink(uint8_t dialect)
{
    fake::pairCamera(dialect);
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_ON);
    link_control::start();
    fake::loop(500);
}

} // namespace

// Cancel in every step after connecting: test_camera_connection.cpp. Here while connecting, and while an
// older pairing waits for 01.
void test_cancel_while_connecting_or_waiting_for_01_closes_the_connection()
{
    for (const fake::Dialect dialect : {fake::Dialect::Newer, fake::Dialect::Older}) {
        fake::reset(); // a connection that comes up now is closed at once
        fake::camera.dialect = dialect;
        fake::camera.answer = fake::Answer::NoAnswer;
        withGps();
        const camera_list::Report camera = fake::cameraIn(lumix::STATE_ON);
        if (dialect == fake::Dialect::Older) {
            TEST_ASSERT_TRUE(older_session::start(camera, fake::record.uuid, true));
            older_session::cancel();
        } else {
            TEST_ASSERT_TRUE(newer_session::start(camera, fake::record.uuid, true));
            newer_session::cancel();
        }
        TEST_ASSERT_FALSE(fake::connectionWanted());
    }
    fake::reset();
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.knowsCard = false;
    withGps();
    TEST_ASSERT_TRUE(older_session::start(fake::cameraIn(lumix::STATE_ON), fake::record.uuid, false));
    fake::pollFor(1000, [] { older_session::poll(); });
    TEST_ASSERT_TRUE(fake::linkUp());
    older_session::cancel();
    TEST_ASSERT_FALSE(fake::linkUp());
    TEST_ASSERT_FALSE(fake::connectionWanted());
}

void test_link_off_closes_the_connection_in_every_phase()
{
    struct PhaseCase {
        uint8_t dialect;
        fake::Descriptors descriptors;
        fake::Step silentAt;
    };
    const PhaseCase PHASES[] = {
        {pairing_record::DIALECT_NEWER, fake::Descriptors::NoCccd, {Request::Write, fake::DEVICE_ID, "introducing"}},
        {pairing_record::DIALECT_NEWER, fake::Descriptors::NoCccd, {Request::Write, fake::GPS_MODE, "GPS mode"}},
        {pairing_record::DIALECT_NEWER, fake::Descriptors::NoCccd, {Request::Descriptors, 0, "finding the CCCD"}},
        {pairing_record::DIALECT_NEWER, fake::Descriptors::Cccd, {Request::Write, 0x001F, "notifications"}},
        {pairing_record::DIALECT_NEWER, fake::Descriptors::NoCccd, {Request::None, 0, "connected"}},
        {pairing_record::DIALECT_OLDER, fake::Descriptors::NoCccd, {Request::Write, fake::OLDER_TIME, "older"}},
        {pairing_record::DIALECT_OLDER, fake::Descriptors::NoCccd, {Request::None, 0, "older, connected"}},
    };
    for (const PhaseCase &phase : PHASES) {
        fake::reset();
        if (phase.dialect == pairing_record::DIALECT_OLDER)
            fake::camera.dialect = fake::Dialect::Older;
        fake::camera.descriptors = phase.descriptors;
        fake::camera.silent = {phase.silentAt.request, phase.silentAt.handle};
        startLink(phase.dialect);
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), phase.silentAt.name);
        link_control::stop(false);
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), phase.silentAt.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::connectionWanted(), phase.silentAt.name);
    }
    for (const uint8_t dialect : {pairing_record::DIALECT_NEWER, pairing_record::DIALECT_OLDER}) {
        fake::reset(); // while connecting
        if (dialect == pairing_record::DIALECT_OLDER)
            fake::camera.dialect = fake::Dialect::Older;
        fake::camera.answer = fake::Answer::NoAnswer;
        startLink(dialect);
        TEST_ASSERT_EQUAL(1, fake::askedCount("connect"));
        link_control::stop(false);
        TEST_ASSERT_FALSE(fake::connectionWanted());
    }
}

void test_pairing_closes_the_connection_when_cancelled_or_done()
{
    fake::camera.silent = {Request::Write, fake::TIME}; // newer dialect, while it introduces the card
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_PAIRING);
    pairing::start();
    fake::loop(1000);
    TEST_ASSERT_TRUE(fake::linkUp());
    pairing::stop();
    TEST_ASSERT_FALSE(fake::linkUp());
    TEST_ASSERT_FALSE(fake::connectionWanted());

    fake::reset(); // older dialect, while it waits for the Wi-Fi step
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.knowsCard = false;
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_PAIRING);
    pairing::startOlder();
    fake::loop(3000);
    TEST_ASSERT_TRUE(fake::linkUp());
    pairing::stop();
    TEST_ASSERT_FALSE(fake::linkUp());
    TEST_ASSERT_FALSE(fake::connectionWanted());

    fake::reset(); // older dialect, accepted: pairing closes the connection the session kept open
    fake::camera.dialect = fake::Dialect::Older;
    fake::gpsFix = true;
    fake::advertise(lumix::STATE_PAIRING);
    pairing::startOlder();
    fake::loop(5000);
    TEST_ASSERT_FALSE(pairing::active());
    TEST_ASSERT_TRUE(fake::record.paired);
    TEST_ASSERT_FALSE(fake::linkUp());
    TEST_ASSERT_FALSE(fake::connectionWanted());
}

void runInvariantTests()
{
    RUN_TEST(test_cancel_while_connecting_or_waiting_for_01_closes_the_connection);
    RUN_TEST(test_link_off_closes_the_connection_in_every_phase);
    RUN_TEST(test_pairing_closes_the_connection_when_cancelled_or_done);
}
