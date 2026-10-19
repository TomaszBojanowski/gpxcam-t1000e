#include "link_control.h"

#include <Arduino.h>
#include <ble_gatt.h>
#include <string.h>

#include "bluetooth.h"
#include "camera_link.h"
#include "camera_session.h"
#include "gps.h"
#include "link_policy.h"
#include "log.h"
#include "lumix.h"
#include "pairing.h"
#include "pairing_store.h"
#include "sound.h"

namespace {

constexpr uint32_t STEP_MS = 5000; // GPS mode, descriptors, notifications, every location (M4/M5)

// CCCD values (Bluetooth Core Vol 3, Part G, 3.3.3.3), little-endian.
constexpr uint8_t CCCD_NOTIFICATIONS[] = {0x01, 0x00};
constexpr uint8_t CCCD_INDICATIONS[] = {0x02, 0x00};

enum class Phase { Off, Searching, Introducing, GpsMode, FindingCccd, EnablingNotifications, Connected, Pausing,
                   Rejected };

Phase phase = Phase::Off;
uint32_t phaseStart = 0;
uint32_t pauseLength = 0;
uint32_t failuresInARow = 0;
uint32_t connectedAt = 0;
char serialText[2 * lumix::SERIAL_LENGTH + 1] = "";

// The connection in Connected (from camera_session::discovered()).
uint16_t locationHandle = 0;
uint16_t notifyHandle = 0;
uint16_t serviceEnd = 0;

// Locations of the current connection.
bool sentBefore = false;
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
        return "25 s";
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
        bluetooth::scanOn();
    waitingForTimeLogged = false;
    enter(Phase::Searching);
}

bool connectionOpen()
{
    return phase == Phase::GpsMode || phase == Phase::FindingCccd || phase == Phase::EnablingNotifications ||
           phase == Phase::Connected;
}

void pause(const char *why)
{
    if (connectionOpen())
        sound::playDisconnected();
    // A connection that lasted starts the pauses again from 5 s, however it ended.
    if (connectionOpen() && millis() - connectedAt >= link_policy::STABLE_CONNECTION_MS)
        failuresInARow = 0;
    if (camera_link::connected())
        camera_link::disconnect(); // the Disconnected event is dropped by the next connect
    failuresInARow++;
    pauseLength = link_policy::pauseMs(failuresInARow);
    logger::line("link: %s - looking again in %lu s", why, (unsigned long)(pauseLength / 1000));
    enter(Phase::Pausing);
}

void connectionFailed(const char *reason)
{
    char why[96];
    snprintf(why, sizeof(why), "connection to camera %s failed (%s)", serialText, reason);
    pause(why);
}

void rejectedByCamera()
{
    logger::line("link: camera %s refused the card (it does not know it) - not connecting again; "
                 "'pair' again or 'link on'",
                 serialText);
    sound::playRefuse();
    bluetooth::scanOff();
    gps::off(); // nothing to send to
    enter(Phase::Rejected);
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
        logger::line("link: the camera offers no notifications - sending every 25 s only");
        enterConnected();
        return;
    }
    findCccd(notifyHandle + 1);
}

void onAccepted()
{
    const camera_session::Discovered &found = camera_session::discovered();
    const uint16_t gpsModeHandle = found.valueHandle[camera_link::GPS_MODE];
    locationHandle = found.valueHandle[camera_link::LOCATION];
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
    connectedAt = millis();
    logger::line("link: connected to camera %s (MTU %u)", serialText, found.mtu);
    sound::playConnected();
    const uint8_t gpsOn[] = {lumix::GPS_MODE_ON};
    if (writeTo(gpsModeHandle, gpsOn, sizeof(gpsOn)))
        enter(Phase::GpsMode);
    else
        connectionFailed("GPS mode: write refused by the SoftDevice");
}

void onDisconnected(uint16_t reason)
{
    const uint32_t lasted = millis() - connectedAt;
    char why[96];
    snprintf(why, sizeof(why), "disconnected from camera %s after %lu s (HCI 0x%02x)", serialText,
             (unsigned long)(lasted / 1000), reason);
    pause(why);
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
        logger::line("link: the camera asked for encryption - not answered");
        return true;
    case camera_link::EventType::Notified:
        if (event.read.handle == notifyHandle) {
            cameraAsked = true;
            logger::transient("link: the camera asks for a location");
        }
        return true;
    default:
        return false;
    }
}

void onLocationWritten(const camera_link::Event &event)
{
    writePending = false;
    if (event.status != BLE_GATT_STATUS_SUCCESS) {
        failedCount++;
        logger::transient("link: location refused (GATT 0x%04x)", event.status);
        return;
    }
    sentCount++;
    if (!sentBefore)
        logger::line("link: first location sent to camera %s", serialText);
    else
        logger::transient("link: location %lu sent (%s)", (unsigned long)sentCount, reasonText(pendingReason));
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
    const link_policy::Send due = link_policy::locationDue(sentBefore, now - lastSentMs, moved, cameraAsked);
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

void connected(uint32_t now)
{
    camera_link::Event event;
    while (phase == Phase::Connected && camera_link::nextEvent(&event)) {
        if (handleConnectionEvent(event))
            continue;
        if (event.type == camera_link::EventType::WriteDone && writePending)
            onLocationWritten(event);
    }
    if (phase != Phase::Connected)
        return;
    if (writePending) {
        if (now - writeStartMs >= STEP_MS)
            connectionFailed("location: no answer in 5 s");
        return;
    }
    sendLocationIfDue(now);
}

// GPS mode, descriptors and notifications: one request each, with a time limit.
void settingUp(uint32_t now)
{
    camera_link::Event event;
    const Phase current = phase;
    while (phase == current && camera_link::nextEvent(&event)) {
        if (handleConnectionEvent(event))
            continue;
        if (phase == Phase::GpsMode && event.type == camera_link::EventType::WriteDone) {
            if (event.status != BLE_GATT_STATUS_SUCCESS)
                logger::line("link: GPS mode refused (GATT 0x%04x) - going on", event.status);
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
                // The S5II: the next characteristic follows the value at once (M5 step 2), and
                // the gpxcam app does not write a CCCD then either. Notifications still count.
                logger::line("link: no CCCD on the notification characteristic (as on the S5II) - listening without it");
                enterConnected();
            }
        } else if (phase == Phase::EnablingNotifications && event.type == camera_link::EventType::WriteDone) {
            if (event.status != BLE_GATT_STATUS_SUCCESS)
                logger::line("link: notifications refused (GATT 0x%04x) - sending every 25 s only", event.status);
            enterConnected();
        }
    }
    if (phase == current && now - phaseStart >= STEP_MS)
        connectionFailed(phase == Phase::GpsMode ? "GPS mode: no answer in 5 s" : "notifications: no answer in 5 s");
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
        connectionFailed(camera_session::failure());
        break;
    }
}

void search(uint32_t now)
{
    // The introduction ends with the time, so connecting before the GPS has it would only
    // be a connection the card has to drop (M5 step 2 test).
    int64_t utcNow = 0;
    if (!gps::utcNow(&utcNow)) {
        if (!waitingForTimeLogged)
            logger::line("link: waiting for GPS time - the camera needs it when the card introduces itself");
        waitingForTimeLogged = true;
        return;
    }
    const pairing_record::Record &record = pairing_store::record();
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *camera = bluetooth::camera(i);
        if (camera == nullptr || !link_policy::shouldConnect(record, *camera, now))
            continue;
        bluetooth::scanOff(); // connecting needs the radio
        logger::transient("link: camera %s is on, %d dBm - connecting", serialText, camera->last.rssi);
        if (camera_session::start(camera->last, record.uuid, true))
            enter(Phase::Introducing);
        else
            connectionFailed(camera_session::failure());
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
    if (!bluetooth::ready() || !pairing_store::ready() || !pairing_store::record().paired) {
        logger::line("link: no paired camera - use 'pair' first");
        return;
    }
    lumix::formatSerial(pairing_store::record().serial, serialText, sizeof(serialText));
    failuresInARow = 0;
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
    if (camera_link::connected())
        camera_link::disconnect();
    if (bluetooth::scanning())
        bluetooth::scanOff();
    if (!keepGps)
        gps::off();
    logger::line("link: off");
    phase = Phase::Off;
}

bool active()
{
    return phase != Phase::Off && phase != Phase::Rejected;
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
        logger::line("link: connected to camera %s for %lu s, last location %s, %lu sent, %lu refused", serialText,
                     (unsigned long)((now - connectedAt) / 1000), last, (unsigned long)sentCount,
                     (unsigned long)failedCount);
    else if (phase == Phase::Pausing)
        logger::line("link: pause, looking again in %lu s; last location %s, %lu sent",
                     (unsigned long)((pauseLength - min(pauseLength, now - phaseStart)) / 1000), last,
                     (unsigned long)sentCount);
    else
        logger::line("link: %s; last location %s, %lu sent", phaseText(), last, (unsigned long)sentCount);
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
        settingUp(now);
        break;
    case Phase::Connected:
        connected(now);
        break;
    case Phase::Pausing:
        if (now - phaseStart >= pauseLength)
            startSearch();
        break;
    }
}

} // namespace link_control
