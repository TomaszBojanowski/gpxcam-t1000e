#include "pairing.h"

#include <Arduino.h>
#include <string.h>

#include "bluetooth.h"
#include "camera_list.h"
#include "camera_session.h"
#include "gps.h"
#include "log.h"
#include "link_control.h"
#include "link_policy.h"
#include "lumix.h"
#include "pairing_store.h"
#include "sound.h"

namespace {

// M4 decisions.
constexpr uint32_t FIX_WAIT_MS = 120000;
constexpr uint32_t SEARCH_MS = 60000;
constexpr int MAX_ATTEMPTS = 3;
constexpr uint32_t PAUSE_MS[MAX_ATTEMPTS - 1] = {10000, 30000}; // after attempts 1 and 2


enum class Phase { Idle, WaitingForFix, Searching, Introducing, Pausing };

Phase phase = Phase::Idle;
uint32_t phaseStart = 0;
int attempt = 0;
bool gpsWasOn = false;
bool scanWasOn = false;
bool otherStateLogged = false; // "not in pairing mode" said once per session
bool linkWasOn = false;        // the link is paused during a session
bool paired = false;           // this session paired a camera
uint8_t cameraSerial[lumix::SERIAL_LENGTH];

void enter(Phase next)
{
    phase = next;
    phaseStart = millis();
}

void formatSerial(const uint8_t serial[lumix::SERIAL_LENGTH], char (&out)[2 * lumix::SERIAL_LENGTH + 1])
{
    lumix::formatSerial(serial, out, sizeof(out));
}

// Ends the session. The link starts again if it was on or a camera was just paired: it takes
// GPS and scanning over as they are. Otherwise they go back to how they were.
void finish(bool resumeLink = true)
{
    camera_session::cancel(); // does nothing when no session runs
    phase = Phase::Idle;
    if (resumeLink && (linkWasOn || paired)) {
        link_control::start();
        return;
    }
    if (scanWasOn && !bluetooth::scanning())
        bluetooth::scanOn();
    else if (!scanWasOn && bluetooth::scanning())
        bluetooth::scanOff();
    if (!gpsWasOn)
        gps::off(); // about 2 s: Backup mode for the next hot start
}

void giveUp(const char *reason)
{
    logger::line("pair: given up - %s", reason);
    sound::playRefuse();
    finish();
}

void succeed()
{
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    formatSerial(cameraSerial, serial);
    if (pairing_store::saveCamera(cameraSerial)) {
        logger::line("pair: paired with camera %s", serial);
        sound::playPaired();
        paired = true;
    } else {
        logger::line("pair: camera %s accepted the card, but the pairing file could not be written", serial);
        sound::playRefuse();
    }
    finish();
}

void attemptFailed(const char *reason)
{
    logger::line("pair: attempt %d/%d failed - %s", attempt, MAX_ATTEMPTS, reason);
    if (attempt >= MAX_ATTEMPTS) {
        giveUp("all attempts failed");
        return;
    }
    logger::line("pair: next attempt in %lu s", (unsigned long)(PAUSE_MS[attempt - 1] / 1000));
    enter(Phase::Pausing);
}

void introduce(const camera_list::Entry &camera)
{
    bluetooth::scanOff(); // connecting needs the radio
    attempt++;
    memcpy(cameraSerial, camera.last.camera.serial, sizeof(cameraSerial));
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    formatSerial(cameraSerial, serial);
    logger::line("pair: attempt %d/%d - camera %s in pairing mode, %d dBm, connecting", attempt, MAX_ATTEMPTS, serial,
                 camera.last.rssi);
    if (!camera_session::start(camera.last, pairing_store::record().uuid, false)) {
        attemptFailed(camera_session::failure());
        return;
    }
    enter(Phase::Introducing);
}

void startSearch()
{
    if (!bluetooth::scanning())
        bluetooth::scanOn();
    enter(Phase::Searching);
}

void waitForFix(uint32_t now)
{
    int64_t utc = 0;
    if (gps::utcNow(&utc)) {
        logger::line("pair: GPS time ok - looking for a camera in pairing mode (up to %lu s)",
                     (unsigned long)(SEARCH_MS / 1000));
        startSearch();
    } else if (now - phaseStart >= FIX_WAIT_MS) {
        logger::line("pair: no GPS fix in %lu s - pairing needs UTC time from the GPS; try near a window",
                     (unsigned long)(FIX_WAIT_MS / 1000));
        giveUp("no GPS fix");
    }
}

void logNotInPairingMode(const camera_list::Entry &camera)
{
    if (otherStateLogged)
        return;
    otherStateLogged = true;
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    formatSerial(camera.last.camera.serial, serial);
    logger::line("pair: camera %s is %s (%02x), not in pairing mode - choose pairing on the camera", serial,
                 lumix::stateName(camera.last.camera.state), camera.last.camera.state);
}

void search(uint32_t now)
{
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *camera = bluetooth::camera(i);
        if (camera == nullptr)
            continue;
        if (camera->last.camera.state == lumix::STATE_PAIRING && now - camera->lastSeenMs <= link_policy::REPORT_FRESH_MS) {
            introduce(*camera);
            return;
        }
        if (camera->last.camera.state != lumix::STATE_PAIRING)
            logNotInPairingMode(*camera);
    }
    if (now - phaseStart >= SEARCH_MS)
        giveUp("no camera in pairing mode in time");
}

void introducing()
{
    switch (camera_session::poll()) {
    case camera_session::Result::Running:
        break;
    case camera_session::Result::Accepted:
        succeed();
        break;
    case camera_session::Result::Rejected: // not expected in pairing mode
    case camera_session::Result::Failed:
        attemptFailed(camera_session::failure());
        break;
    }
}

const char *phaseText()
{
    switch (phase) {
    case Phase::Idle:
        return "idle";
    case Phase::WaitingForFix:
        return "waiting for GPS time";
    case Phase::Searching:
        return "looking for a camera in pairing mode";
    case Phase::Introducing:
        return "talking to the camera";
    case Phase::Pausing:
        return "pause before the next attempt";
    }
    return "?";
}

} // namespace

namespace pairing {

void start()
{
    if (phase != Phase::Idle) {
        logger::line("pair: already running");
        return;
    }
    if (!bluetooth::ready() || !pairing_store::ready()) {
        logger::line("pair: not possible - BLE or the pairing file is not available");
        sound::playRefuse();
        return;
    }
    linkWasOn = link_control::active();
    if (linkWasOn)
        link_control::stop(true); // keeps the GPS on
    gpsWasOn = gps::isOn();
    scanWasOn = bluetooth::scanning();
    attempt = 0;
    otherStateLogged = false;
    paired = false;
    sound::playPairing();
    if (!gpsWasOn)
        gps::on();
    logger::line("pair: started - waiting for GPS time (up to %lu s)", (unsigned long)(FIX_WAIT_MS / 1000));
    enter(Phase::WaitingForFix);
}

void stop(bool resumeLink)
{
    if (phase == Phase::Idle)
        return;
    logger::line("pair: cancelled");
    finish(resumeLink);
}

bool active()
{
    return phase != Phase::Idle;
}

void logState()
{
    if (phase == Phase::Idle) {
        logger::transient("pair: idle");
        return;
    }
    logger::transient("pair: %s for %lu s, attempt %d/%d", phaseText(), (unsigned long)((millis() - phaseStart) / 1000),
                 attempt, MAX_ATTEMPTS);
}

void poll()
{
    const uint32_t now = millis();
    switch (phase) {
    case Phase::Idle:
        break;
    case Phase::WaitingForFix:
        waitForFix(now);
        break;
    case Phase::Searching:
        search(now);
        break;
    case Phase::Introducing:
        introducing();
        break;
    case Phase::Pausing:
        if (now - phaseStart >= PAUSE_MS[attempt - 1])
            startSearch();
        break;
    }
}

} // namespace pairing
