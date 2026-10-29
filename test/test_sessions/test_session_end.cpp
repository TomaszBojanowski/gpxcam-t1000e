// Every way a session ends (session_end.h), in both dialects: the reason the session gives, and how the
// link and pairing take it. Against the fake camera (fake.h).
#include <unity.h>

#include <ble_hci.h>

#include "newer_session.h"
#include "fake.h"
#include "link_control.h"
#include "older_session.h"
#include "pairing.h"

using fake::Request;
using session_end::Reason;

namespace {

// What the link does after such ends, one after another: link_policy's pauses of 5, 15 and 60 s, the
// quick retry after 1 s of a link not established, or stopping as after a refusal.
enum class LinkTakes {
    Pauses,               // 5, 15, 60 s
    QuickRetryThenPauses, // 1 s once, then 5, 15, 60 s
    StopsAtOnce,          // a refusal that shows an unknown card
    StopsAfterThree,      // 5, 15 s, and the third in a row stops it
};

// What pairing does: tries a link that never came up again at once (5 times), pauses 10 and 30 s
// between three attempts, or switches to the older dialect.
enum class PairingTakes { QuickRetries, Attempts, SwitchesToOlder };

struct EndCase {
    const char *name;
    fake::Dialect session; // the dialect of the session the card runs
    void (*makeCamera)();
    Reason reason;
    LinkTakes link;
    PairingTakes pairing;
};

void olderCamera()
{
    fake::camera.dialect = fake::Dialect::Older;
}

const EndCase CASES[] = {
    {"newer: time refused", fake::Dialect::Newer, [] { fake::camera.refuse = {Request::Write, fake::TIME}; },
     Reason::Refused, LinkTakes::StopsAtOnce, PairingTakes::Attempts},
    {"newer: no answer", fake::Dialect::Newer, [] { fake::camera.answer = fake::Answer::NoAnswer; },
     Reason::NoAnswer, LinkTakes::Pauses, PairingTakes::QuickRetries},
    {"newer: not established", fake::Dialect::Newer, [] { fake::camera.answer = fake::Answer::NotEstablished; },
     Reason::NotEstablished, LinkTakes::QuickRetryThenPauses, PairingTakes::QuickRetries},
    {"newer: disconnected at the key", fake::Dialect::Newer,
     [] {
         fake::camera.disconnectAt = {Request::Read, fake::KEY};
         fake::camera.disconnectReason = BLE_HCI_CONNECTION_TIMEOUT;
     },
     Reason::EndedEarly, LinkTakes::Pauses, PairingTakes::Attempts},
    // Without the time written an early end is the sign of an unknown card (link_policy).
    {"newer, clock off: disconnected at the key", fake::Dialect::Newer,
     [] {
         fake::clockOn = false;
         fake::camera.disconnectAt = {Request::Read, fake::KEY};
         fake::camera.disconnectReason = BLE_HCI_CONNECTION_TIMEOUT;
     },
     Reason::EndedEarly, LinkTakes::StopsAfterThree, PairingTakes::Attempts},
    {"newer: an older camera", fake::Dialect::Newer, olderCamera, Reason::NoNewerService, LinkTakes::Pauses,
     PairingTakes::SwitchesToOlder},
    {"newer: key unanswered", fake::Dialect::Newer, [] { fake::camera.silent = {Request::Read, fake::KEY}; },
     Reason::Failed, LinkTakes::Pauses, PairingTakes::Attempts},
    {"older: identifier refused", fake::Dialect::Older,
     [] {
         olderCamera();
         fake::camera.refuse = {Request::Write, fake::OLDER_DEVICE_ID};
     },
     Reason::Refused, LinkTakes::StopsAfterThree, PairingTakes::Attempts},
    {"older: no 01", fake::Dialect::Older,
     [] {
         olderCamera();
         fake::camera.knowsCard = false;
     },
     Reason::NotTaken, LinkTakes::StopsAfterThree, PairingTakes::Attempts},
    {"older: no answer", fake::Dialect::Older,
     [] {
         olderCamera();
         fake::camera.answer = fake::Answer::NoAnswer;
     },
     Reason::NoAnswer, LinkTakes::Pauses, PairingTakes::QuickRetries},
    {"older: not established", fake::Dialect::Older,
     [] {
         olderCamera();
         fake::camera.answer = fake::Answer::NotEstablished;
     },
     Reason::NotEstablished, LinkTakes::QuickRetryThenPauses, PairingTakes::QuickRetries},
    {"older: disconnected at the key", fake::Dialect::Older,
     [] {
         olderCamera();
         fake::camera.disconnectAt = {Request::Read, fake::OLDER_KEY};
         fake::camera.disconnectReason = BLE_HCI_CONNECTION_TIMEOUT;
     },
     Reason::EndedEarly, LinkTakes::Pauses, PairingTakes::Attempts},
    {"older: key unanswered", fake::Dialect::Older,
     [] {
         olderCamera();
         fake::camera.silent = {Request::Read, fake::OLDER_KEY};
     },
     Reason::Failed, LinkTakes::Pauses, PairingTakes::Attempts},
};

bool linkStopped()
{
    return link_control::state() == link_control::State::Stopped;
}

bool linkStoppedOrPausedLongest()
{
    return linkStopped() || fake::logged("looking again in 60 s");
}

bool pairingIdle()
{
    return !pairing::active();
}

bool waitingForWifi()
{
    return pairing::progress().phase == pairing::Phase::WaitingForWifi;
}

// The longest a pairing can run: three attempts that each wait 300 s for an older camera's 01, and the
// pauses between them.
constexpr uint32_t PAIRING_MAX_MS = 1000000;

} // namespace

void test_each_session_end_has_its_reason()
{
    for (const EndCase &end : CASES) {
        fake::reset();
        end.makeCamera();
        fake::gpsOn = fake::gpsFix = true;
        const bool older = end.session == fake::Dialect::Older;
        const camera_list::Report camera = fake::cameraIn(lumix::STATE_ON);
        TEST_ASSERT_TRUE_MESSAGE(older ? older_session::start(camera, fake::record.uuid, true)
                                       : newer_session::start(camera, fake::record.uuid, true),
                                 end.name);
        const session_end::Result result =
            older ? fake::runSession(older_session::poll) : fake::runSession(newer_session::poll);
        TEST_ASSERT_EQUAL_MESSAGE(session_end::resultOf(end.reason), result, end.name);
        TEST_ASSERT_EQUAL_MESSAGE(end.reason, older ? older_session::endReason() : newer_session::endReason(),
                                  end.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), end.name);
    }
}

void test_the_link_takes_each_session_end()
{
    for (const EndCase &end : CASES) {
        fake::reset();
        end.makeCamera();
        fake::pairCamera(end.session == fake::Dialect::Older ? pairing_record::DIALECT_OLDER
                                                             : pairing_record::DIALECT_NEWER);
        fake::gpsFix = true;
        fake::advertise(lumix::STATE_ON);
        link_control::start();
        TEST_ASSERT_TRUE_MESSAGE(fake::loopUntil(linkStoppedOrPausedLongest, 300000), end.name);
        const bool stops = end.link == LinkTakes::StopsAtOnce || end.link == LinkTakes::StopsAfterThree;
        const bool pauses = end.link != LinkTakes::StopsAtOnce;
        TEST_ASSERT_EQUAL_MESSAGE(stops, linkStopped(), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(end.link == LinkTakes::QuickRetryThenPauses ? 1 : 0,
                                  fake::loggedCount("trying again in 1 s"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(pauses ? 1 : 0, fake::loggedCount("looking again in 5 s"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(pauses ? 1 : 0, fake::loggedCount("looking again in 15 s"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(stops ? 0 : 1, fake::loggedCount("looking again in 60 s"), end.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), end.name);
    }
}

void test_pairing_takes_each_session_end()
{
    for (const EndCase &end : CASES) {
        fake::reset();
        end.makeCamera();
        fake::gpsFix = true;
        fake::advertise(lumix::STATE_PAIRING);
        if (end.session == fake::Dialect::Older)
            pairing::startOlder();
        else
            pairing::start();
        if (end.pairing == PairingTakes::SwitchesToOlder) {
            TEST_ASSERT_TRUE_MESSAGE(fake::loopUntil(waitingForWifi, 30000), end.name);
            TEST_ASSERT_TRUE_MESSAGE(fake::logged("switching to the older dialect"), end.name);
            continue;
        }
        TEST_ASSERT_TRUE_MESSAGE(fake::loopUntil(pairingIdle, PAIRING_MAX_MS), end.name);
        TEST_ASSERT_TRUE_MESSAGE(pairing::progress().lastResult == pairing::Result::Failed, end.name);
        const bool quick = end.pairing == PairingTakes::QuickRetries;
        TEST_ASSERT_EQUAL_MESSAGE(quick ? 5 : 0, fake::loggedCount("pair: quick retry"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(quick ? 0 : 1, fake::loggedCount("pair: next attempt in 10 s"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(quick ? 0 : 1, fake::loggedCount("pair: next attempt in 30 s"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(quick, fake::logged("no connection to the camera after 5 quick retries"), end.name);
        TEST_ASSERT_EQUAL_MESSAGE(!quick, fake::logged("pair: given up - all attempts failed"), end.name);
        TEST_ASSERT_FALSE_MESSAGE(fake::linkUp(), end.name);
    }
}

void runSessionEndTests()
{
    RUN_TEST(test_each_session_end_has_its_reason);
    RUN_TEST(test_the_link_takes_each_session_end);
    RUN_TEST(test_pairing_takes_each_session_end);
}
