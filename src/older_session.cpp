#include "older_session.h"

#include <Arduino.h>
#include <ble_gatt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "camera_link.h"
#include "device_identity.h"
#include "gps.h"
#include "log.h"
#include "lumix.h"
#include "posix_tz.h"
#include "tz_store.h"
#include "utc.h"

// The order and byte formats come from a capture of the older Panasonic app with the S5II (lumix.h).

namespace {

// Time limits, as camera_session.cpp.
constexpr uint32_t CONNECT_MS = 10000;
constexpr uint32_t MTU_MS = 3000;
constexpr uint32_t STEP_MS = 5000; // every discovery answer, read and write
// Waiting for the camera's 01 (lumix::OLDER_KNOWN). Pairing: the camera waits for the one-time
// Wi-Fi step, so 300 s leaves time for a computer to join its network and send it. A lasting
// connection: the camera that already knows the card sends 01 right after the device name.
constexpr uint32_t KNOWN_WAIT_PAIR_MS = 300000;
constexpr uint32_t KNOWN_WAIT_CONNECT_MS = 15000;
constexpr uint32_t LOCATION_PERIOD_MS = 5000;   // a location every 5 s (the lasting connection)
constexpr uint32_t LOCATION_SUMMARY_EVERY = 12; // a log line about once a minute
constexpr uint32_t NO_LIMIT = 0;

constexpr uint16_t MTU_REQUEST = camera_link::MAX_MTU;
constexpr uint16_t FIRST_HANDLE = 0x0001;
constexpr uint16_t LAST_HANDLE = 0xFFFF;

// The name the camera shows and the computer registers over Wi-Fi; the same on both channels.
constexpr const char *DEVICE_NAME = "T1000-E";

enum class Step { Idle, Connecting, Mtu, Characteristics, Key, DeviceId, DeviceName, CameraName, CameraText,
                  WaitKnown, Time, TimeAgain, Sending, Done };

Step step = Step::Idle;
uint32_t stepStart = 0;
uint32_t stepLimit = NO_LIMIT;
bool keepOpen = false; // true: send locations after the camera accepts; false: end at Accepted
bool accepted = false;
bool justAccepted = false; // Accepted returned once
bool rejected = false;     // the camera did not take the card (vs a connection problem)
uint8_t cardUuid[lumix::UUID_LENGTH];
uint16_t handles[camera_link::OLDER_CHARACTERISTIC_COUNT];
uint8_t key[lumix::KEY_LENGTH];
bool knownByLocationNotify = false;
bool knownByTimeNotify = false;
bool writePending = false;
uint32_t lastLocationMs = 0;
uint32_t locationsWritten = 0;
uint32_t locationsRefused = 0;
char failureText[80] = "";

// The data of the write in progress; the SoftDevice may read it until WriteDone.
uint8_t txData[device_identity::FIELD_LENGTH];
static_assert(lumix::TIME_LENGTH <= sizeof(txData) && lumix::LOCATION_LENGTH <= sizeof(txData),
              "every write fits txData");

const char *const CHARACTERISTIC_NAMES[camera_link::OLDER_CHARACTERISTIC_COUNT] = {
    "key", "identifier", "device name", "camera name", "camera text", "time", "time notify", "location",
    "location notify"};

const char *stepName(Step s)
{
    switch (s) {
    case Step::Idle:
        return "idle";
    case Step::Connecting:
        return "connect";
    case Step::Mtu:
        return "MTU";
    case Step::Characteristics:
        return "characteristics";
    case Step::Key:
        return "key";
    case Step::DeviceId:
        return "identifier";
    case Step::DeviceName:
        return "device name";
    case Step::CameraName:
        return "camera name";
    case Step::CameraText:
        return "camera text";
    case Step::WaitKnown:
        return "waiting for 01";
    case Step::Time:
        return "time";
    case Step::TimeAgain:
        return "time again";
    case Step::Sending:
        return "sending locations";
    case Step::Done:
        return "accepted";
    }
    return "?";
}

void enter(Step next, uint32_t limitMs)
{
    step = next;
    stepStart = millis();
    stepLimit = limitMs;
}

void setFailure(const char *format, va_list args)
{
    vsnprintf(failureText, sizeof(failureText), format, args);
}

// Ends the session with a connection problem (Failed). Disconnects; the pending event is ignored.
void fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
void fail(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    setFailure(format, args);
    va_end(args);
    if (step == Step::Connecting)
        camera_link::cancelConnect();
    if (camera_link::connected())
        camera_link::disconnect();
    step = Step::Idle;
}

// Ends the session because the camera did not take the card (Rejected). Disconnects.
void reject(const char *format, ...) __attribute__((format(printf, 1, 2)));
void reject(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    setFailure(format, args);
    va_end(args);
    rejected = true;
    if (camera_link::connected())
        camera_link::disconnect();
    step = Step::Idle;
}

bool found(camera_link::OlderCharacteristic which)
{
    return handles[which] != 0;
}

void startWrite(camera_link::OlderCharacteristic which, size_t length, Step next)
{
    if (!camera_link::write(handles[which], txData, (uint16_t)length)) {
        fail("%s: write refused by the SoftDevice", CHARACTERISTIC_NAMES[which]);
        return;
    }
    enter(next, STEP_MS);
}

void startRead(camera_link::OlderCharacteristic which, Step next)
{
    if (!camera_link::read(handles[which])) {
        fail("%s: read refused by the SoftDevice", CHARACTERISTIC_NAMES[which]);
        return;
    }
    enter(next, STEP_MS);
}

// The time of this moment with the zone of 'tz', into txData (as camera_session.cpp).
bool makeTimePacket()
{
    int64_t now = 0;
    if (!gps::utcNow(&now))
        return false;
    const posix_tz::CameraZone zone = posix_tz::cameraZone(tz_store::rule(), now);
    lumix::timeData(utc::fromUnixSeconds(now), zone.standardOffsetMinutes, zone.summerTime, txData);
    return true;
}

void startSending()
{
    logger::line("older: the camera took the card - a location every %lu s", (unsigned long)(LOCATION_PERIOD_MS / 1000));
    enter(Step::Sending, NO_LIMIT);
    lastLocationMs = millis() - LOCATION_PERIOD_MS; // the first one at once
}

// The camera took the card: report it once, then either keep sending or end (pairing).
void accept()
{
    accepted = true;
    justAccepted = true;
    if (keepOpen)
        startSending();
    else
        enter(Step::Done, NO_LIMIT); // the connection stays open; the caller cancels after Accepted
}

void writeTime(Step next)
{
    if (!makeTimePacket()) {
        fail("time: no GPS time any more");
        return;
    }
    startWrite(camera_link::OLDER_TIME, lumix::TIME_LENGTH, next);
}

// After the introduction and the camera reads: the time once the camera has said 01. A lasting
// connection with the clock off ('clock off') writes none.
void afterKnown()
{
    logger::line("older: the camera knows the card (01 on %s)",
                 knownByLocationNotify && knownByTimeNotify ? "both notify characteristics"
                 : knownByLocationNotify                    ? "the location notify"
                                                            : "the time notify");
    if (keepOpen && !tz_store::clockOn()) {
        logger::line("older: clock off - no time written");
        accept();
        return;
    }
    writeTime(Step::Time);
}

// At the first pairing a computer (the gpxcam card app) does the one-time Wi-Fi step: it registers
// this same identity and device name over the camera's Wi-Fi network while the user joins that
// network by hand from the camera's screen. Print them in a line the app reads; local/mask.sed
// masks the identity (a UUID) when a log is shown. Only for pairing - a lasting connection needs
// no Wi-Fi step.
void logAppRegistration()
{
    uint8_t seed[device_identity::SEED_LENGTH];
    device_identity::cardSeed(cardUuid, seed);
    char identity[device_identity::TEXT_LENGTH + 1];
    if (device_identity::identityText(seed, identity, sizeof(identity)) == 0) {
        logger::line("older: the identity text for the app could not be built");
        return;
    }
    logger::line("older: app register identity %s name %s", identity, DEVICE_NAME);
}

void waitForKnown()
{
    if (knownByLocationNotify || knownByTimeNotify) {
        afterKnown();
        return;
    }
    const uint32_t limit = keepOpen ? KNOWN_WAIT_CONNECT_MS : KNOWN_WAIT_PAIR_MS;
    logger::line("older: introduced - waiting up to %lu s for the camera's 01%s", (unsigned long)(limit / 1000),
                 keepOpen ? "" : " (the camera now waits for the Wi-Fi step)");
    if (!keepOpen)
        logAppRegistration();
    enter(Step::WaitKnown, limit);
}

void readCameraText()
{
    if (found(camera_link::OLDER_CAMERA_TEXT))
        startRead(camera_link::OLDER_CAMERA_TEXT, Step::CameraText);
    else
        waitForKnown();
}

void readCameraName()
{
    if (found(camera_link::OLDER_CAMERA_NAME))
        startRead(camera_link::OLDER_CAMERA_NAME, Step::CameraName);
    else
        readCameraText();
}

// The card's seed: the last bytes of its own pairing UUID. The computer registers the same seed
// over Wi-Fi, so the Bluetooth and Wi-Fi identities match.
void seedFromUuid(uint8_t seed[device_identity::SEED_LENGTH])
{
    device_identity::cardSeed(cardUuid, seed);
}

void writeDeviceName()
{
    if (!device_identity::nameField(DEVICE_NAME, key, txData)) {
        fail("device name: could not be built");
        return;
    }
    startWrite(camera_link::OLDER_DEVICE_NAME, device_identity::FIELD_LENGTH, Step::DeviceName);
}

void writeDeviceId()
{
    uint8_t seed[device_identity::SEED_LENGTH];
    seedFromUuid(seed);
    uint8_t identity[device_identity::IDENTITY_LENGTH];
    device_identity::identityBytes(seed, identity);
    device_identity::identityField(identity, key, txData);
    startWrite(camera_link::OLDER_DEVICE_ID, device_identity::FIELD_LENGTH, Step::DeviceId);
}

// One line, in the order of camera_link::OlderCharacteristic; 0000 = not found.
void logHandles()
{
    static_assert(camera_link::OLDER_CHARACTERISTIC_COUNT == 9, "nine handles in the line");
    logger::line("older: handles (key id name cam-name cam-text time time-ntf loc loc-ntf): "
                 "%04x %04x %04x %04x %04x %04x %04x %04x %04x",
                 handles[0], handles[1], handles[2], handles[3], handles[4], handles[5], handles[6], handles[7],
                 handles[8]);
}

void onCharacteristics(const camera_link::Event &event)
{
    for (uint8_t i = 0; i < event.characteristics.olderCount; i++) {
        const camera_link::FoundOlderCharacteristic &entry = event.characteristics.olderList[i];
        if (entry.which < camera_link::OLDER_CHARACTERISTIC_COUNT && handles[entry.which] == 0)
            handles[entry.which] = entry.valueHandle;
    }
    // ATT "attribute not found" ends the discovery; so does reaching the last handle.
    const uint16_t last = event.characteristics.lastHandle;
    if (event.status == BLE_GATT_STATUS_SUCCESS && last != 0 && last < LAST_HANDLE) {
        if (!camera_link::findOlderCharacteristics(last + 1, LAST_HANDLE))
            fail("characteristics: request refused");
        else
            enter(Step::Characteristics, STEP_MS);
        return;
    }
    logHandles();
    const camera_link::OlderCharacteristic needed[] = {camera_link::OLDER_KEY, camera_link::OLDER_DEVICE_ID,
                                                       camera_link::OLDER_DEVICE_NAME, camera_link::OLDER_TIME,
                                                       camera_link::OLDER_LOCATION};
    for (const camera_link::OlderCharacteristic which : needed) {
        if (!found(which)) {
            fail("%s: not on this camera", CHARACTERISTIC_NAMES[which]);
            return;
        }
    }
    startRead(camera_link::OLDER_KEY, Step::Key);
}

void onKey(const camera_link::Event &event)
{
    if (event.status != BLE_GATT_STATUS_SUCCESS || event.read.length < lumix::KEY_LENGTH) {
        fail("key: not read (GATT 0x%04x, %u bytes)", event.status, event.read.length);
        return;
    }
    memcpy(key, event.read.data, lumix::KEY_LENGTH);
    logger::line("older: key read");
    writeDeviceId();
}

// A refused write of the introduction or the time means the camera does not take the card.
bool writeAccepted(const camera_link::Event &event, camera_link::OlderCharacteristic which)
{
    if (event.status == BLE_GATT_STATUS_SUCCESS) {
        logger::line("older: %s written", CHARACTERISTIC_NAMES[which]);
        return true;
    }
    reject("%s: refused (GATT 0x%04x)", CHARACTERISTIC_NAMES[which], event.status);
    return false;
}

// The two values the older app reads (the camera's Wi-Fi network and password): the content is
// never logged. A refusal does not end the session.
void onCameraRead(const camera_link::Event &event, camera_link::OlderCharacteristic which)
{
    if (event.status == BLE_GATT_STATUS_SUCCESS)
        logger::line("older: %s read (content not logged)", CHARACTERISTIC_NAMES[which]);
    else
        logger::line("older: %s not read (GATT 0x%04x) - going on", CHARACTERISTIC_NAMES[which], event.status);
}

void sendLocation(uint32_t now)
{
    lastLocationMs = now;
    int64_t utcNow = 0;
    int32_t latitudeE7 = 0;
    int32_t longitudeE7 = 0;
    int32_t altitudeDm = 0;
    if (!gps::utcNow(&utcNow) || !gps::position(&latitudeE7, &longitudeE7, &altitudeDm)) {
        logger::transient("older: no GPS fix - location skipped");
        return;
    }
    lumix::locationData(utcNow, latitudeE7, longitudeE7, altitudeDm, txData);
    if (!camera_link::write(handles[camera_link::OLDER_LOCATION], txData, lumix::LOCATION_LENGTH)) {
        fail("location: write refused by the SoftDevice");
        return;
    }
    writePending = true;
    stepStart = now;
    stepLimit = STEP_MS;
}

void onLocationWritten(const camera_link::Event &event)
{
    writePending = false;
    stepLimit = NO_LIMIT;
    if (event.status != BLE_GATT_STATUS_SUCCESS) {
        locationsRefused++;
        logger::line("older: location refused (GATT 0x%04x)", event.status);
        return;
    }
    locationsWritten++;
    if (locationsWritten == 1 || locationsWritten % LOCATION_SUMMARY_EVERY == 0)
        logger::line("older: %lu locations written, %lu refused", (unsigned long)locationsWritten,
                     (unsigned long)locationsRefused);
}

// A notification: its handle and up to 8 bytes. 01 on either notify characteristic means the
// camera knows the card.
void onNotified(const camera_link::Event &event)
{
    const char *name = "other";
    for (uint8_t i = 0; i < camera_link::OLDER_CHARACTERISTIC_COUNT; i++)
        if (handles[i] != 0 && handles[i] == event.read.handle)
            name = CHARACTERISTIC_NAMES[i];
    char bytes[3 * camera_link::MAX_READ_LENGTH + 1] = "";
    for (uint8_t i = 0; i < event.read.length; i++)
        snprintf(bytes + 3 * i, sizeof(bytes) - 3 * i, "%02x ", event.read.data[i]);
    logger::line("older: notification 0x%04x (%s), %u bytes: %s", event.read.handle, name, event.read.length, bytes);
    if (event.read.length == 0)
        return;
    const uint8_t value = event.read.data[0];
    if (value == lumix::OLDER_KNOWN && event.read.handle == handles[camera_link::OLDER_LOCATION_NOTIFY])
        knownByLocationNotify = true;
    if (value == lumix::OLDER_KNOWN && event.read.handle == handles[camera_link::OLDER_TIME_NOTIFY])
        knownByTimeNotify = true;
    if (value == lumix::OLDER_SWITCHING_OFF && event.read.handle == handles[camera_link::OLDER_LOCATION_NOTIFY])
        logger::line("older: the camera is switching off");
    if (step == Step::WaitKnown && (knownByLocationNotify || knownByTimeNotify))
        afterKnown();
}

void onConnected()
{
    logger::line("older: connected");
    if (!camera_link::exchangeMtu(MTU_REQUEST)) {
        fail("MTU: request refused");
        return;
    }
    enter(Step::Mtu, MTU_MS);
}

void onMtu(const camera_link::Event &event)
{
    const uint16_t mtu =
        event.status == BLE_GATT_STATUS_SUCCESS ? min(MTU_REQUEST, event.mtu) : BLE_GATT_ATT_MTU_DEFAULT;
    logger::line("older: MTU %u - looking for the characteristics in all handles", mtu);
    if (!camera_link::findOlderCharacteristics(FIRST_HANDLE, LAST_HANDLE))
        fail("characteristics: request refused");
    else
        enter(Step::Characteristics, STEP_MS);
}

// Events that may come in any step of a connection. Returns true if it handled the event.
bool handleCommonEvent(const camera_link::Event &event)
{
    using camera_link::EventType;
    switch (event.type) {
    case EventType::Disconnected:
        if (step == Step::Done)
            return true; // already accepted; the caller cancels
        fail("the camera disconnected (HCI 0x%02x)", event.status);
        return true;
    case EventType::GattTimeout:
        fail("no ATT answer for 30 s");
        return true;
    case EventType::SecurityRequest:
        logger::line("older: the camera asked for encryption - not answered");
        return true;
    case EventType::Notified:
        onNotified(event);
        return true;
    default:
        return false;
    }
}

void handleEvent(const camera_link::Event &event)
{
    if (handleCommonEvent(event))
        return;
    using camera_link::EventType;
    switch (step) {
    case Step::Connecting:
        if (event.type == EventType::Connected)
            onConnected();
        break;
    case Step::Mtu:
        if (event.type == EventType::MtuExchanged)
            onMtu(event);
        break;
    case Step::Characteristics:
        if (event.type == EventType::CharacteristicsFound)
            onCharacteristics(event);
        break;
    case Step::Key:
        if (event.type == EventType::ReadDone)
            onKey(event);
        break;
    case Step::DeviceId:
        if (event.type == EventType::WriteDone && writeAccepted(event, camera_link::OLDER_DEVICE_ID))
            writeDeviceName();
        break;
    case Step::DeviceName:
        if (event.type == EventType::WriteDone && writeAccepted(event, camera_link::OLDER_DEVICE_NAME))
            readCameraName();
        break;
    case Step::CameraName:
        if (event.type == EventType::ReadDone) {
            onCameraRead(event, camera_link::OLDER_CAMERA_NAME);
            readCameraText();
        }
        break;
    case Step::CameraText:
        if (event.type == EventType::ReadDone) {
            onCameraRead(event, camera_link::OLDER_CAMERA_TEXT);
            waitForKnown();
        }
        break;
    case Step::Time:
        if (event.type == EventType::WriteDone && writeAccepted(event, camera_link::OLDER_TIME)) {
            // As camera_session: in a lasting connection the camera takes the zone from the packet before.
            if (keepOpen)
                writeTime(Step::TimeAgain);
            else
                accept();
        }
        break;
    case Step::TimeAgain:
        if (event.type == EventType::WriteDone && writeAccepted(event, camera_link::OLDER_TIME))
            accept();
        break;
    case Step::Sending:
        if (event.type == EventType::WriteDone && writePending)
            onLocationWritten(event);
        break;
    case Step::Idle:
    case Step::WaitKnown:
    case Step::Done:
        break;
    }
}

void handleTimeout()
{
    if (step == Step::WaitKnown)
        reject("no 01 from the camera in %lu s", (unsigned long)(stepLimit / 1000));
    else
        fail("no answer in step '%s' in %lu s", stepName(step), (unsigned long)(stepLimit / 1000));
}

} // namespace

namespace older_session {

bool start(const camera_list::Report &camera, const uint8_t uuid[lumix::UUID_LENGTH], bool keepConnection)
{
    if (step != Step::Idle) {
        snprintf(failureText, sizeof(failureText), "a session is running");
        return false;
    }
    if (camera.camera.state == lumix::STATE_STANDBY) { // connecting would wake the camera
        snprintf(failureText, sizeof(failureText), "the camera is in standby");
        return false;
    }
    if (camera_link::connected()) {
        camera_link::disconnect();
        snprintf(failureText, sizeof(failureText), "an earlier connection was still open - closed now");
        return false;
    }
    memcpy(cardUuid, uuid, sizeof(cardUuid));
    keepOpen = keepConnection;
    accepted = false;
    justAccepted = false;
    rejected = false;
    writePending = false;
    locationsWritten = 0;
    locationsRefused = 0;
    knownByLocationNotify = false;
    knownByTimeNotify = false;
    memset(handles, 0, sizeof(handles));
    failureText[0] = '\0';
    if (!camera_link::connect(camera)) {
        snprintf(failureText, sizeof(failureText), "connect: refused by the SoftDevice");
        return false;
    }
    enter(Step::Connecting, CONNECT_MS);
    return true;
}

Result poll()
{
    if (step == Step::Idle)
        return Result::Failed;
    camera_link::Event event;
    while (step != Step::Idle && camera_link::nextEvent(&event))
        handleEvent(event);
    if (justAccepted) {
        justAccepted = false;
        return Result::Accepted;
    }
    if (step == Step::Done)
        return Result::Accepted; // pairing: accepted, the connection stays until the caller cancels
    if (step == Step::Idle)
        return rejected ? Result::Rejected : Result::Failed;
    if (stepLimit != NO_LIMIT && millis() - stepStart >= stepLimit) {
        handleTimeout();
        if (step == Step::Idle)
            return rejected ? Result::Rejected : Result::Failed;
    } else if (step == Step::Sending && !writePending && millis() - lastLocationMs >= LOCATION_PERIOD_MS) {
        sendLocation(millis());
    }
    return Result::Running;
}

const char *failure()
{
    return failureText;
}

void cancel()
{
    if (step == Step::Idle)
        return;
    if (step == Step::Connecting)
        camera_link::cancelConnect();
    if (camera_link::connected())
        camera_link::disconnect();
    step = Step::Idle;
}

bool active()
{
    return step != Step::Idle;
}

void logState()
{
    if (step == Step::Idle)
        logger::transient("older: idle (last session: %lu locations written, %lu refused)",
                          (unsigned long)locationsWritten, (unsigned long)locationsRefused);
    else
        logger::transient("older: %s, %lu locations written, %lu refused", stepName(step),
                          (unsigned long)locationsWritten, (unsigned long)locationsRefused);
}

} // namespace older_session
