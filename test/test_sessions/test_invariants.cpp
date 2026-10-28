// After stop, cancel or the end of a session the connection is closed, in every step of every state
// machine: no connection is left open without an owner (the camera showing a connection that gets no
// locations). Against the fake camera (fake.h).
#include <string.h>
#include <unity.h>

#include "camera_session.h"
#include "fake.h"
#include "link_control.h"
#include "older_session.h"
#include "pairing.h"

using fake::Request;

namespace {

struct StepCase {
    Request request;
    uint16_t handle;
    const char *name;
};

// The camera stays silent at one of these requests, so the session waits in that step.
const StepCase NEWER_STEPS[] = {
    {Request::Mtu, 0, "MTU"},
    {Request::Service, 0, "service"},
    {Request::Characteristics, 0, "characteristics"},
    {Request::Read, fake::KEY, "key"},
    {Request::Write, fake::DEVICE_ID, "identifier"},
    {Request::Write, fake::APP_NAME, "app name"},
    {Request::Write, fake::DEVICE_NAME, "device name"},
    {Request::Write, fake::TIME, "time"},
};
const StepCase OLDER_STEPS[] = {
    {Request::Mtu, 0, "MTU"},
    {Request::Characteristics, 0, "characteristics"},
    {Request::Read, fake::OLDER_KEY, "key"},
    {Request::Write, fake::OLDER_DEVICE_ID, "identifier"},
    {Request::Write, fake::OLDER_DEVICE_NAME, "device name"},
    {Request::Read, fake::OLDER_CAMERA_NAME, "camera name"},
    {Request::Read, fake::OLDER_CAMERA_TEXT, "camera text"},
    {Request::Write, fake::OLDER_TIME, "time"},
};

camera_list::Report cameraOn()
{
    camera_list::Report report = {};
    memcpy(report.camera.serial, fake::SERIAL, sizeof(report.camera.serial));
    report.camera.state = lumix::STATE_ON;
    report.rssi = -50;
    report.kind = advert_kind::Kind::Connectable;
    return report;
}

void withGps()
{
    fake::gpsOn = true;
    fake::gpsFix = true;
}

template <class Poll> void pollFor(uint32_t ms, Poll poll)
{
    for (uint32_t done = 0; done < ms; done += fake::LOOP_STEP_MS) {
        fake::advance(fake::LOOP_STEP_MS);
        poll();
    }
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

void test_camera_session_cancel_closes_the_connection_in_every_step()
{
    for (const StepCase &step : NEWER_STEPS) {
        fake::reset();
        fake::camera.silent = {step.request, step.handle};
        withGps();
        TEST_ASSERT_TRUE(camera_session::start(cameraOn(), fake::record.uuid, true));
        pollFor(50, [] { camera_session::poll(); });
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), step.name);
        camera_session::cancel();
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), step.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::connectionWanted(), step.name);
    }
    fake::reset(); // and while connecting: a connection that comes up now is closed at once
    fake::camera.answer = fake::Answer::NoAnswer;
    withGps();
    TEST_ASSERT_TRUE(camera_session::start(cameraOn(), fake::record.uuid, true));
    camera_session::cancel();
    TEST_ASSERT_FALSE(fake::connectionWanted());
}

void test_older_session_cancel_closes_the_connection_in_every_step()
{
    for (const StepCase &step : OLDER_STEPS) {
        fake::reset();
        fake::camera.dialect = fake::Dialect::Older;
        fake::camera.silent = {step.request, step.handle};
        withGps();
        TEST_ASSERT_TRUE(older_session::start(cameraOn(), fake::record.uuid, true));
        pollFor(50, [] { older_session::poll(); });
        TEST_ASSERT_TRUE_MESSAGE(fake::linkUp(), step.name);
        older_session::cancel();
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), step.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::connectionWanted(), step.name);
    }
    fake::reset(); // and while it waits for 01
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.knowsCard = false;
    withGps();
    TEST_ASSERT_TRUE(older_session::start(cameraOn(), fake::record.uuid, false));
    pollFor(1000, [] { older_session::poll(); });
    TEST_ASSERT_TRUE(fake::linkUp());
    older_session::cancel();
    TEST_ASSERT_FALSE(fake::linkUp());
    TEST_ASSERT_FALSE(fake::connectionWanted());

    fake::reset(); // and while connecting
    fake::camera.dialect = fake::Dialect::Older;
    fake::camera.answer = fake::Answer::NoAnswer;
    withGps();
    TEST_ASSERT_TRUE(older_session::start(cameraOn(), fake::record.uuid, true));
    older_session::cancel();
    TEST_ASSERT_FALSE(fake::connectionWanted());
}

void test_link_off_closes_the_connection_in_every_phase()
{
    struct PhaseCase {
        uint8_t dialect;
        fake::Descriptors descriptors;
        StepCase silentAt;
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
    RUN_TEST(test_camera_session_cancel_closes_the_connection_in_every_step);
    RUN_TEST(test_older_session_cancel_closes_the_connection_in_every_step);
    RUN_TEST(test_link_off_closes_the_connection_in_every_phase);
    RUN_TEST(test_pairing_closes_the_connection_when_cancelled_or_done);
}
