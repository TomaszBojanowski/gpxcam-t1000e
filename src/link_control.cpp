#include "link_control.h"

#include <Arduino.h>
#include <ble_gatt.h>
#include <ble_hci.h>
#include <stdio.h>
#include <string.h>

#include "bluetooth.h"
#include "camera_link.h"
#include "camera_session.h"
#include "gps.h"
#include "link_policy.h"
#include "log.h"
#include "lumix.h"
#include "older_session.h"
#include "pairing.h"
#include "pairing_record.h"
#include "pairing_store.h"
#include "settings_store.h"
#include "sound.h"

namespace {

// A connection that was not established (HCI 0x3e) is tried again after this pause, once,
// without counting as a failure; the next one is allowed after a successful connection
// (the S5II often refuses the very first connection after it is switched on).
constexpr uint32_t QUICK_RETRY_MS = 1000;

// Scan timing of the search modes (link_policy::SearchMode): the same window in each, every interval.
constexpr uint16_t SCAN_WINDOW_MS = 100;
constexpr uint16_t RARE_SCAN_INTERVAL_MS = 2000;
// Standby: every 1 s, so the camera is found soon after a break between
// photos; the GPS is on in this mode anyway, the scan adds little to the current.
constexpr uint16_t STANDBY_SCAN_INTERVAL_MS = 1000;
constexpr uint16_t LONG_STANDBY_SCAN_INTERVAL_MS = 10000;

// CCCD values (Bluetooth Core Vol 3, Part G, 3.3.3.3), little-endian.
constexpr uint8_t CCCD_NOTIFICATIONS[] = {0x01, 0x00};
constexpr uint8_t CCCD_INDICATIONS[] = {0x02, 0x00};

enum class Phase { Off, Searching, Introducing, GpsMode, FindingCccd, EnablingNotifications, Connected, Older,
                   Pausing, Rejected };

Phase phase = Phase::Off;
uint32_t phaseStart = 0;
uint32_t pauseLength = 0;
uint32_t failuresInARow = 0;
bool quickRetryUsed = false;
uint32_t connectedAt = 0;
bool olderLink = false; // the connection is in the older dialect (older_session introduced the card)
char serialText[2 * lumix::SERIAL_LENGTH + 1] = "";

// The connection in Connected (from camera_session::discovered(), or older_session in the older dialect).
uint16_t locationHandle = 0;
uint16_t notifyHandle = 0;      // the camera asks for a location (newer dialect)
uint16_t olderNotifyHandle = 0; // 02: the camera is switching off (older dialect)
uint16_t serviceEnd = 0;

// Locations of the current connection.
bool sentBefore = false;
link_policy::RateGuard rateGuard; // frequent locations ('updates'), normal while the camera lags
uint8_t earlyEndsInARow = 0; // connections that ended before the camera took a location
uint32_t lastSentMs = 0;
int32_t lastLatitudeE7 = 0;
int32_t lastLongitudeE7 = 0;
bool cameraAsked = false;
bool writePending = false;
uint32_t writeStartMs = 0;
int32_t pendingLatitudeE7 = 0;
int32_t pendingLongitudeE7 = 0;
link_policy::Send pendingReason = link_policy::Send::No;
bool noFixLogged = false;
bool waitingForTimeLogged = false; // "waiting for GPS time" said once per search
bool weakSignalLogged = false;     // "too weak" said once per search
uint32_t lastScanStartMs = 0;      // retries of a scan that failed to (re)start
constexpr uint32_t SCAN_RETRY_MS = 10000;

// The paired camera as the scan last saw it (kept when the camera list forgets it) and the
// search mode that follows from it.
bool cameraSeen = false;
uint32_t cameraSeenMs = 0;
uint8_t cameraState = 0;
uint32_t standbySinceMs = 0;
uint32_t searchStartMs = 0; // since 'link on'
link_policy::SearchMode mode = link_policy::SearchMode::Normal;

// Counters since 'link on'.
uint32_t sentCount = 0;
uint32_t failedCount = 0;
bool everSent = false;
uint32_t everSentMs = 0;

// Data of the write in progress; the SoftDevice may read it until WriteDone.
uint8_t txData[lumix::LOCATION_LENGTH];

void enter(Phase next)
{
    phase = next;
    phaseStart = millis();
}

const char *reasonText(link_policy::Send reason)
{
    switch (reason) {
    case link_policy::Send::First:
        return "first";
    case link_policy::Send::Periodic:
        return "period";
    case link_policy::Send::Moved:
        return "moved";
    case link_policy::Send::Asked:
        return "camera asked";
    case link_policy::Send::No:
        break;
    }
    return "-";
}

void startSearch()
{
    if (!bluetooth::scanning())
        bluetooth::scanOn(bluetooth::Lines::Sent); // one per connection attempt: kept out of the history
    waitingForTimeLogged = false;
    weakSignalLogged = false;
    enter(Phase::Searching);
}

const char *modeText()
{
    switch (mode) {
    case link_policy::SearchMode::Normal:
        return "normal";
    case link_policy::SearchMode::Rare:
        return "rare";
    case link_policy::SearchMode::Standby:
        return "standby";
    case link_policy::SearchMode::LongStandby:
        return "long standby";
    }
    return "?";
}

const camera_list::Entry *pairedCamera()
{
    const pairing_record::Record &record = pairing_store::record();
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *camera = bluetooth::camera(i);
        if (camera != nullptr && memcmp(camera->last.camera.serial, record.serial, lumix::SERIAL_LENGTH) == 0)
            return camera;
    }
    return nullptr;
}

// Remembers the last report of the paired camera, whatever its state or signal.
void noteSighting()
{
    const camera_list::Entry *camera = pairedCamera();
    if (camera == nullptr || (cameraSeen && camera->lastSeenMs == cameraSeenMs))
        return;
    const uint8_t state = camera->last.camera.state;
    if (state == lumix::STATE_STANDBY && (!cameraSeen || cameraState != lumix::STATE_STANDBY))
        standbySinceMs = camera->lastSeenMs;
    cameraSeen = true;
    cameraSeenMs = camera->lastSeenMs;
    cameraState = state;
}

void setScanFor(link_policy::SearchMode searchMode)
{
    switch (searchMode) {
    case link_policy::SearchMode::Normal:
        bluetooth::setNormalScanTiming();
        break;
    case link_policy::SearchMode::Rare:
        bluetooth::setScanTiming(RARE_SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
        break;
    case link_policy::SearchMode::Standby:
        bluetooth::setScanTiming(STANDBY_SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
        break;
    case link_policy::SearchMode::LongStandby:
        bluetooth::setScanTiming(LONG_STANDBY_SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
        break;
    }
}

// Picks the search mode and sets the scan and the GPS for it.
void applyMode(uint32_t now)
{
    const link_policy::SearchMode next = link_policy::searchMode(cameraSeen, now - cameraSeenMs, cameraState,
                                                                 now - searchStartMs, now - standbySinceMs);
    if (next != mode) {
        mode = next;
        setScanFor(mode);
        switch (mode) {
        case link_policy::SearchMode::Normal:
            logger::line("link: camera %s is back - normal scan, GPS on", serialText);
            break;
        case link_policy::SearchMode::Rare:
            logger::line("link: camera %s not seen for %lu min - scan %u ms every %u s, GPS in Backup", serialText,
                         (unsigned long)(link_policy::RARE_AFTER_MS / link_policy::MS_PER_MINUTE), SCAN_WINDOW_MS,
                         RARE_SCAN_INTERVAL_MS / 1000);
            break;
        case link_policy::SearchMode::Standby:
            logger::line("link: camera %s in standby - scan %u ms every %u s, waiting for it to switch on", serialText,
                         SCAN_WINDOW_MS, STANDBY_SCAN_INTERVAL_MS / 1000);
            break;
        case link_policy::SearchMode::LongStandby:
            logger::line("link: camera %s in standby for %lu min - scan %u ms every %u s, GPS in Backup", serialText,
                         (unsigned long)(link_policy::LONG_STANDBY_AFTER_MS / link_policy::MS_PER_MINUTE),
                         SCAN_WINDOW_MS, LONG_STANDBY_SCAN_INTERVAL_MS / 1000);
            break;
        }
    }
    // The GPS is switched on only in normal mode. In a short standby it stays on if it is on
    // (the fix is there when the camera wakes); coming from rare mode it stays off.
    if (mode == link_policy::SearchMode::Normal && !gps::isOn())
        gps::on();
    else if (!link_policy::gpsNeeded(mode) && gps::isOn())
        gps::off(); // about 2 s; the next start is a hot start
}

bool connectionOpen()
{
    return phase == Phase::GpsMode || phase == Phase::FindingCccd || phase == Phase::EnablingNotifications ||
           phase == Phase::Connected;
}

// Whether the introduction showed that the camera knows the card: in the older dialect its 01 says so;
// in the newer the accepted time write does, which a lasting connection skips with the clock off.
bool cameraKnowsCard()
{
    return olderLink || camera_session::writesTime();
}

// The setting chosen with 'updates', and the one in effect (link_control::updatesNow()).
bool frequentChosen()
{
    return settings_store::updates() == link_policy::Updates::Frequent;
}

const char *timingText()
{
    return link_policy::updatesName(link_control::updatesNow());
}

const link_policy::LocationTiming &timing()
{
    return rateGuard.timing(settings_store::updates());
}

// closedByCamera: the camera ended the connection itself (HCI 0x13), as when it is switched off.
void pause(const char *why, bool closedByCamera = false)
{
    if (connectionOpen()) {
        rateGuard.connectionEnded(closedByCamera);
        sound::playDisconnected();
        cameraSeen = true; // the connection showed it until now
        cameraSeenMs = millis();
        cameraState = lumix::STATE_ON;
    }
    // A connection that lasted starts the pauses again from 5 s, however it ended.
    if (connectionOpen() && millis() - connectedAt >= link_policy::STABLE_CONNECTION_MS)
        failuresInARow = 0;
    camera_link::close(); // its Disconnected event is dropped by the next connect
    failuresInARow++;
    pauseLength = link_policy::pauseMs(failuresInARow);
    char text[logger::LINE_MAX];
    const int length = snprintf(text, sizeof(text), "link: looking again in %lu s - %s",
                                (unsigned long)(pauseLength / 1000), why);
    if (length > 0 && (uint32_t)length <= logger::TEXT_MAX) {
        logger::line("%s", text);
    } else { // a long reason in a line of its own, so that nothing is cut
        logger::line("link: %s", why);
        logger::line("link: looking again in %lu s", (unsigned long)(pauseLength / 1000));
    }
    enter(Phase::Pausing);
}

void connectionFailed(const char *reason)
{
    // After the introduction the link uses the session's handles; an error may mean a wrong one.
    if (connectionOpen() && !olderLink)
        camera_session::forgetHandles(reason);
    char why[logger::LINE_MAX];
    snprintf(why, sizeof(why), "connection to camera %s failed (%s)", serialText, reason);
    pause(why);
}

void rejectedByCamera()
{
    logger::line("link: camera %s refused the card (it does not know it) - not connecting again; "
                 "'pair' again or 'link on'",
                 serialText);
    sound::playRefuse(sound::Kind::Event);
    bluetooth::scanOff();
    gps::off(); // nothing to send to
    enter(Phase::Rejected);
}

// A refused GPS mode or first location while the connection is open, when it means that the
// camera does not know the card (link_policy::refusalMeansUnknownCard()).
void rejectedWhileConnected(const char *what, uint16_t status)
{
    logger::line("link: %s refused (GATT 0x%04x)", what, status);
    camera_link::close(); // its Disconnected event is dropped by the next connect
    rejectedByCamera();
}

// A connection that ended before the camera took a location: without the time written, the
// third in a row stops the card as a refusal does (link_policy::earlyEndMeansUnknownCard()).
// True if it stopped.
bool earlyEndStopped()
{
    if (!link_policy::earlyEndMeansUnknownCard(cameraKnowsCard(), &earlyEndsInARow))
        return false;
    logger::line("link: %u connections in a row ended before the camera took a location",
                 (unsigned)earlyEndsInARow);
    rejectedByCamera();
    return true;
}

void enterConnected()
{
    sentBefore = false;
    cameraAsked = false;
    writePending = false;
    noFixLogged = false;
    enter(Phase::Connected);
}

bool writeTo(uint16_t handle, const uint8_t *data, uint16_t length)
{
    memcpy(txData, data, length);
    return camera_link::write(handle, txData, length);
}

void enableNotifications(uint16_t cccdHandle, bool indicate)
{
    const uint8_t *value = indicate ? CCCD_INDICATIONS : CCCD_NOTIFICATIONS;
    if (writeTo(cccdHandle, value, sizeof(CCCD_NOTIFICATIONS)))
        enter(Phase::EnablingNotifications);
    else
        connectionFailed("notifications: write refused by the SoftDevice");
}

void findCccd(uint16_t fromHandle)
{
    if (camera_link::findDescriptors(fromHandle, serviceEnd))
        enter(Phase::FindingCccd);
    else
        connectionFailed("notifications: descriptor search refused");
}

// After the GPS mode: notifications, if the camera offers them.
void afterGpsMode()
{
    if (notifyHandle == 0 || notifyHandle >= serviceEnd) { // no room for a descriptor
        logger::line("link: the camera offers no notifications - sending every %lu s only",
                     (unsigned long)(timing().periodMs / 1000));
        enterConnected();
        return;
    }
    findCccd(notifyHandle + 1);
}

// Both dialects, once the camera has taken the card.
void connectionUp()
{
    connectedAt = millis();
    rateGuard.connectionStarted();
    if (rateGuard.slowedDown() && frequentChosen())
        logger::line("link: the last connection did not end by the camera's own close - normal timing for this one");
    sound::playConnected();
}

void onAccepted()
{
    quickRetryUsed = false; // the next refused connection may get its quick attempt again
    sentBefore = false;     // nothing sent in this connection yet
    const camera_session::Discovered &found = camera_session::discovered();
    const uint16_t gpsModeHandle = found.valueHandle[camera_link::GPS_MODE];
    locationHandle = found.valueHandle[camera_link::LOCATION];
    olderNotifyHandle = 0;
    serviceEnd = found.serviceEnd;
    const uint8_t notifyProperties = found.properties[camera_link::GPS_NOTIFY];
    notifyHandle = (notifyProperties & (camera_link::PROPERTY_NOTIFY | camera_link::PROPERTY_INDICATE))
                       ? found.valueHandle[camera_link::GPS_NOTIFY]
                       : 0;
    if (gpsModeHandle == 0 || locationHandle == 0 ||
        (found.properties[camera_link::GPS_MODE] & camera_link::PROPERTY_WRITE) == 0 ||
        (found.properties[camera_link::LOCATION] & camera_link::PROPERTY_WRITE) == 0) {
        connectionFailed("no writable GPS mode or location characteristic");
        return;
    }
    logger::line("link: connected to camera %s (MTU %u)", serialText, found.mtu);
    connectionUp();
    const uint8_t gpsOn[] = {lumix::GPS_MODE_ON};
    if (writeTo(gpsModeHandle, gpsOn, sizeof(gpsOn)))
        enter(Phase::GpsMode);
    else
        connectionFailed("GPS mode: write refused by the SoftDevice");
}

void onDisconnected(uint16_t reason)
{
    const uint32_t lasted = millis() - connectedAt;
    // The camera closed the connection itself (HCI 0x13): that is how a switch-off ends it,
    // also seconds after connecting, so it is a normal end, not a failure: the pause is 5 s.
    // Other ends (out of range, errors) keep making the pauses longer.
    if (reason == BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION)
        failuresInARow = 0;
    if (phase != Phase::Connected) // ended while the link was being set up
        camera_session::forgetHandles("disconnected during the setup");
    if (!sentBefore && earlyEndStopped())
        return;
    char why[logger::LINE_MAX];
    snprintf(why, sizeof(why), "disconnected from camera %s after %lu s (HCI 0x%02x)", serialText,
             (unsigned long)(lasted / 1000), reason);
    pause(why, reason == BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}

// Events of an open connection, in any phase after the introduction. True if handled.
bool handleConnectionEvent(const camera_link::Event &event)
{
    switch (event.type) {
    case camera_link::EventType::Disconnected:
        onDisconnected(event.status);
        return true;
    case camera_link::EventType::GattTimeout:
        connectionFailed("no ATT answer for 30 s");
        return true;
    case camera_link::EventType::SecurityRequest:
        logger::line("camera: asked for encryption - not answered (as the gpxcam app)");
        return true;
    case camera_link::EventType::Notified:
        if (event.read.handle == notifyHandle) {
            cameraAsked = true;
            logger::transient("link: the camera asks for a location");
        } else if (event.read.handle == olderNotifyHandle && event.read.length > 0 &&
                   event.read.data[0] == lumix::OLDER_SWITCHING_OFF) {
            logger::line("link: the camera is switching off");
        }
        return true;
    default:
        return false;
    }
}

void onLocationWritten(const camera_link::Event &event)
{
    writePending = false;
    const uint32_t answerMs = millis() - writeStartMs;
    if (rateGuard.answered(answerMs) && frequentChosen())
        logger::line("link: %u answers later than %lu ms - normal timing until the end of this connection",
                     (unsigned)rateGuard.lateAnswers(), (unsigned long)link_policy::LATE_ANSWER_MS);
    if (event.status != BLE_GATT_STATUS_SUCCESS && !sentBefore &&
        link_policy::refusalMeansUnknownCard(link_policy::IntroductionWrite::FirstLocation, cameraKnowsCard())) {
        rejectedWhileConnected("first location", event.status);
        return;
    }
    if (event.status != BLE_GATT_STATUS_SUCCESS) {
        failedCount++;
        logger::transient("link: location refused (GATT 0x%04x), answer %lu ms", event.status, (unsigned long)answerMs);
        if (!olderLink)
            camera_session::forgetHandles("location refused");
        return;
    }
    sentCount++;
    earlyEndsInARow = 0;
    if (!sentBefore)
        logger::line("link: first location sent to camera %s, answer %lu ms, %s", serialText,
                     (unsigned long)answerMs, timingText());
    else
        logger::transient("link: location %lu sent (%s), answer %lu ms, %s", (unsigned long)sentCount,
                          reasonText(pendingReason), (unsigned long)answerMs, timingText());
    sentBefore = true;
    everSent = true;
    lastSentMs = writeStartMs;
    everSentMs = writeStartMs;
    lastLatitudeE7 = pendingLatitudeE7;
    lastLongitudeE7 = pendingLongitudeE7;
    cameraAsked = false; // whatever the reason, the camera has a fresh location
}

// Sends a location when one is due and there is a fix. The time is this moment.
void sendLocationIfDue(uint32_t now)
{
    int32_t latitudeE7 = 0;
    int32_t longitudeE7 = 0;
    int32_t altitudeDm = 0;
    int64_t utcNow = 0;
    if (!gps::position(&latitudeE7, &longitudeE7, &altitudeDm) || !gps::utcNow(&utcNow)) {
        if (!noFixLogged)
            logger::line("link: no GPS fix - locations paused, the connection stays");
        noFixLogged = true;
        return;
    }
    if (noFixLogged) {
        logger::line("link: GPS fix again - locations go on");
        noFixLogged = false;
    }
    const uint32_t moved =
        sentBefore ? link_policy::distanceMetres(lastLatitudeE7, lastLongitudeE7, latitudeE7, longitudeE7) : 0;
    const link_policy::Send due = link_policy::locationDue(timing(), sentBefore, now - lastSentMs, moved, cameraAsked);
    if (due == link_policy::Send::No)
        return;
    uint8_t data[lumix::LOCATION_LENGTH];
    lumix::locationData(utcNow, latitudeE7, longitudeE7, altitudeDm, data);
    if (!writeTo(locationHandle, data, sizeof(data))) {
        connectionFailed("location: write refused by the SoftDevice");
        return;
    }
    writePending = true;
    writeStartMs = now;
    pendingLatitudeE7 = latitudeE7;
    pendingLongitudeE7 = longitudeE7;
    pendingReason = due;
}

constexpr const char *LOST_WITHOUT_EVENT = "the connection ended without its event";

void connected(uint32_t now)
{
    // A connection gone with its Disconnected event lost (camera_link's queue was full; 'p' counts it):
    // without a GPS fix nothing is written, so no time limit would end the phase. Read before the
    // phase's events, so that a Disconnected queued meanwhile is handled as usual.
    const bool gone = !camera_link::connected();
    camera_link::Event event;
    while (phase == Phase::Connected && camera_link::nextEvent(&event)) {
        if (handleConnectionEvent(event))
            continue;
        if (event.type == camera_link::EventType::WriteDone && writePending)
            onLocationWritten(event);
    }
    if (phase != Phase::Connected)
        return;
    if (gone) {
        connectionFailed(LOST_WITHOUT_EVENT);
        return;
    }
    if (writePending) {
        if (now - writeStartMs >= camera_link::REQUEST_TIMEOUT_MS)
            connectionFailed("location: no answer in time");
        return;
    }
    sendLocationIfDue(now);
}

// GPS mode, descriptors and notifications: one request each, with a time limit. The limit counts from
// the request: a new descriptor search in the same phase (findCccd()) restarts it with millis(), later
// than a time read before the events.
void settingUp()
{
    const bool gone = !camera_link::connected(); // as in connected()
    camera_link::Event event;
    const Phase current = phase;
    while (phase == current && camera_link::nextEvent(&event)) {
        if (handleConnectionEvent(event))
            continue;
        if (phase == Phase::GpsMode && event.type == camera_link::EventType::WriteDone) {
            if (event.status != BLE_GATT_STATUS_SUCCESS &&
                link_policy::refusalMeansUnknownCard(link_policy::IntroductionWrite::GpsMode,
                                                     camera_session::writesTime())) {
                rejectedWhileConnected("GPS mode", event.status);
                continue;
            }
            if (event.status != BLE_GATT_STATUS_SUCCESS) {
                logger::line("link: GPS mode refused (GATT 0x%04x) - going on", event.status);
                camera_session::forgetHandles("GPS mode refused");
            }
            afterGpsMode();
        } else if (phase == Phase::FindingCccd && event.type == camera_link::EventType::DescriptorsFound) {
            const uint16_t last = event.descriptors.lastHandle;
            if (event.descriptors.cccdHandle != 0) {
                const camera_session::Discovered &found = camera_session::discovered();
                const bool indicate = (found.properties[camera_link::GPS_NOTIFY] & camera_link::PROPERTY_NOTIFY) == 0;
                enableNotifications(event.descriptors.cccdHandle, indicate);
            } else if (event.status == BLE_GATT_STATUS_SUCCESS && !event.descriptors.nextCharacteristic &&
                       last != 0 && last < serviceEnd) {
                findCccd(last + 1);
            } else {
                // The S5II: the next characteristic follows the value at once, and
                // the gpxcam app does not write a CCCD then either. Notifications still count.
                logger::line("link: no CCCD on the notification characteristic (as on the S5II) - listening without it");
                enterConnected();
            }
        } else if (phase == Phase::EnablingNotifications && event.type == camera_link::EventType::WriteDone) {
            if (event.status != BLE_GATT_STATUS_SUCCESS) {
                logger::line("link: notifications refused (GATT 0x%04x) - sending every %lu s only", event.status,
                             (unsigned long)(timing().periodMs / 1000));
                camera_session::forgetHandles("notifications refused");
            }
            enterConnected();
        }
    }
    if (phase == current && gone) {
        connectionFailed(LOST_WITHOUT_EVENT);
        return;
    }
    if (phase == current && millis() - phaseStart >= camera_link::REQUEST_TIMEOUT_MS)
        connectionFailed(phase == Phase::GpsMode ? "GPS mode: no answer in time" : "notifications: no answer in time");
}

// A connection that was not established (HCI 0x3e): tried again after QUICK_RETRY_MS, once until a
// connection works.
void quickRetry()
{
    quickRetryUsed = true;
    pauseLength = QUICK_RETRY_MS;
    logger::line("link: connection to camera %s not established - trying again in %lu s", serialText,
                 (unsigned long)(QUICK_RETRY_MS / 1000));
    enter(Phase::Pausing);
}

void introducing()
{
    switch (camera_session::poll()) {
    case camera_session::Result::Running:
        break;
    case camera_session::Result::Accepted:
        onAccepted();
        break;
    case camera_session::Result::Rejected:
        rejectedByCamera();
        break;
    case camera_session::Result::Failed:
        if (camera_session::endedAfterConnecting() && earlyEndStopped())
            break;
        if (camera_session::notEstablished() && !quickRetryUsed) {
            quickRetry();
        } else {
            connectionFailed(camera_session::failure());
        }
        break;
    }
}

void search(uint32_t now)
{
    // A scan restart with new timing can fail (bluetooth::setScanTiming()); try again.
    if (!bluetooth::scanning() && now - lastScanStartMs >= SCAN_RETRY_MS) {
        lastScanStartMs = now;
        bluetooth::scanOn();
    }
    noteSighting();
    applyMode(now);
    if (mode != link_policy::SearchMode::Normal)
        return; // rare or standby: only watch; never connect in standby
    // The introduction ends with the time, so connecting before the GPS has it would only
    // be a connection the card has to drop (tested on the S5II).
    int64_t utcNow = 0;
    if (!gps::utcNow(&utcNow)) {
        if (!waitingForTimeLogged)
            logger::line("link: waiting for GPS time - the camera needs it when the card introduces itself");
        waitingForTimeLogged = true;
        return;
    }
    const pairing_record::Record &record = pairing_store::record();
    const camera_list::Entry *paired = pairedCamera();
    if (paired != nullptr && paired->last.camera.state == lumix::STATE_ON &&
        paired->last.rssi < link_policy::MIN_RSSI_DBM && !weakSignalLogged) {
        logger::line("link: camera %s seen at %d dBm - too weak, waiting for %d dBm", serialText, paired->last.rssi,
                     link_policy::MIN_RSSI_DBM);
        weakSignalLogged = true;
    }
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *camera = bluetooth::camera(i);
        if (camera == nullptr || !link_policy::shouldConnect(record, *camera, now))
            continue;
        bluetooth::scanOff(bluetooth::Lines::Sent); // connecting needs the radio; one per attempt
        logger::transient("link: camera %s is on, %d dBm - connecting", serialText, camera->last.rssi);
        olderLink = pairing_record::isOlderDialect(record);
        if (olderLink) {
            if (older_session::start(camera->last, record.uuid, true))
                enter(Phase::Older);
            else
                connectionFailed(older_session::failure());
        } else if (camera_session::start(camera->last, record.uuid, true)) {
            enter(Phase::Introducing);
        } else {
            connectionFailed(camera_session::failure());
        }
        return;
    }
}

const char *phaseText()
{
    switch (phase) {
    case Phase::Off:
        return "off";
    case Phase::Searching:
        return "looking for the camera";
    case Phase::Introducing:
    case Phase::GpsMode:
    case Phase::FindingCccd:
    case Phase::EnablingNotifications:
        return "connecting";
    case Phase::Connected:
        return "connected";
    case Phase::Older:
        return "connecting (older dialect)";
    case Phase::Pausing:
        return "pause";
    case Phase::Rejected:
        return "stopped: the camera refused the card";
    }
    return "?";
}

} // namespace

namespace link_control {

void begin()
{
    if (pairing_store::ready() && pairing_store::record().paired)
        start();
}

void start()
{
    if (active()) {
        logger::line("link: already on");
        return;
    }
    if (pairing::active()) {
        logger::line("link: not now - pairing runs");
        return;
    }
    if (!bluetooth::ready() || !pairing_store::ready()) {
        logger::line("link: not possible - BLE or the pairing file is not available");
        return;
    }
    if (!pairing_store::record().paired) {
        logger::line("link: no paired camera - use 'pair' first");
        return;
    }
    lumix::formatSerial(pairing_store::record().serial, serialText, sizeof(serialText));
    failuresInARow = 0;
    quickRetryUsed = false;
    earlyEndsInARow = 0;
    rateGuard = link_policy::RateGuard(); // a bad end before 'link off' does not slow the next 'link on'
    cameraSeen = false;
    searchStartMs = millis();
    mode = link_policy::SearchMode::Normal;
    bluetooth::setNormalScanTiming();
    sentCount = failedCount = 0;
    everSent = false;
    if (!gps::isOn())
        gps::on();
    logger::line("link: on - looking for camera %s", serialText);
    startSearch();
}

void stop(bool keepGps)
{
    if (!active() && phase != Phase::Rejected)
        return;
    camera_session::cancel();
    older_session::cancel();
    camera_link::close();
    if (bluetooth::scanning())
        bluetooth::scanOff();
    bluetooth::setNormalScanTiming(); // for pairing's next scanOn()
    if (!keepGps)
        gps::off();
    logger::line("link: off");
    phase = Phase::Off;
}

bool active()
{
    return phase != Phase::Off && phase != Phase::Rejected;
}

Activity activity()
{
    if (!active())
        return Activity::Off;
    if (phase == Phase::Connected)
        return Activity::Connected;
    switch (mode) {
    case link_policy::SearchMode::Rare:
        return Activity::Rare;
    case link_policy::SearchMode::Standby:
        return Activity::Standby;
    case link_policy::SearchMode::LongStandby:
        return Activity::LongStandby;
    case link_policy::SearchMode::Normal:
        break;
    }
    return Activity::Normal;
}

const char *activityName(uint8_t value)
{
    static const char *const NAMES[] = {"off", "connected", "looking (normal)", "looking (rare)", "standby",
                                        "long standby"};
    return value < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[value] : "?";
}

State state()
{
    switch (phase) {
    case Phase::Off:
        return State::Off;
    case Phase::Searching:
        return State::Searching;
    case Phase::Introducing:
    case Phase::Older:
    case Phase::GpsMode:
    case Phase::FindingCccd:
    case Phase::EnablingNotifications:
        return State::Connecting;
    case Phase::Connected:
        return State::Connected;
    case Phase::Pausing:
        return State::Pausing;
    case Phase::Rejected:
        return State::Stopped;
    }
    return State::Off;
}

link_policy::Updates updatesNow()
{
    const link_policy::Updates chosen = settings_store::updates();
    return connectionOpen() ? rateGuard.inEffect(chosen) : chosen;
}

Indication indication()
{
    if (phase == Phase::Connected)
        return Indication::Connected;
    return active() ? Indication::Searching : Indication::Off;
}

void logState()
{
    const uint32_t now = millis();
    char last[32] = "none yet";
    if (everSent)
        snprintf(last, sizeof(last), "%lu s ago", (unsigned long)((now - everSentMs) / 1000));
    if (phase == Phase::Connected)
        logger::transient("link: connected to camera %s%s for %lu s, last location %s, %lu sent, %lu refused, "
                          "%s timing, %u late",
                          serialText, olderLink ? " (older dialect)" : "", (unsigned long)((now - connectedAt) / 1000),
                          last, (unsigned long)sentCount, (unsigned long)failedCount, timingText(),
                          (unsigned)rateGuard.lateAnswers());
    else if (phase == Phase::Older) {
        logger::transient("link: connecting to camera %s (older dialect)", serialText);
        older_session::logState();
    } else if (phase == Phase::Pausing)
        logger::transient("link: pause, looking again in %lu s; last location %s, %lu sent",
                     (unsigned long)((pauseLength - min(pauseLength, now - phaseStart)) / 1000), last,
                     (unsigned long)sentCount);
    else if (phase == Phase::Searching)
        logger::transient("link: looking for the camera, %s mode; last location %s, %lu sent", modeText(), last,
                     (unsigned long)sentCount);
    else
        logger::transient("link: %s; last location %s, %lu sent", phaseText(), last, (unsigned long)sentCount);
}

// The older dialect's introduction ended without the camera taking the card. A camera that refused it
// (a refused write, no 01 in time) counts, and the third such connection in a row stops the link as a
// refusal does (link_policy::olderRefusalMeansUnknownCard()). Otherwise as in the newer dialect: a
// connection that was not established gets a quick retry, anything else a longer pause each time.
void olderEnded()
{
    if (older_session::cardNotTaken()) {
        if (link_policy::olderRefusalMeansUnknownCard(&earlyEndsInARow)) {
            logger::line("link: %u connections in a row ended without camera %s taking the card (last: %s)",
                         (unsigned)earlyEndsInARow, serialText, older_session::failure());
            rejectedByCamera();
        } else {
            connectionFailed(older_session::failure());
        }
        return;
    }
    if (older_session::linkFailure() == camera_link::LinkFailure::NotEstablished && !quickRetryUsed) {
        quickRetry();
        return;
    }
    connectionFailed(older_session::failure());
}

// The camera took the card: the connection is the link's, and the locations go as in the newer dialect.
void onOlderAccepted()
{
    quickRetryUsed = false; // the next connection not established may get its quick retry again
    locationHandle = older_session::locationHandle();
    notifyHandle = 0; // the older dialect's notifications do not ask for locations
    olderNotifyHandle = older_session::locationNotifyHandle();
    logger::line("link: connected to camera %s (older dialect)", serialText);
    connectionUp();
    enterConnected();
}

// The older dialect's introduction is driven by older_session; this only goes on or pauses the link.
void olderPhase()
{
    switch (older_session::poll()) {
    case older_session::Result::Running:
        break;
    case older_session::Result::Accepted:
        onOlderAccepted();
        break;
    case older_session::Result::Rejected:
    case older_session::Result::Failed:
        olderEnded();
        break;
    }
}

void poll()
{
    const uint32_t now = millis();
    switch (phase) {
    case Phase::Off:
    case Phase::Rejected:
        break;
    case Phase::Searching:
        search(now);
        break;
    case Phase::Introducing:
        introducing();
        break;
    case Phase::GpsMode:
    case Phase::FindingCccd:
    case Phase::EnablingNotifications:
        settingUp();
        break;
    case Phase::Connected:
        connected(now);
        break;
    case Phase::Older:
        olderPhase();
        break;
    case Phase::Pausing:
        if (now - phaseStart >= pauseLength)
            startSearch();
        break;
    }
}

} // namespace link_control
