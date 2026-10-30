#include "older_session.h"

#include <ble_gatt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "camera_connection.h"
#include "camera_link.h"
#include "device_identity.h"
#include "log.h"
#include "lumix.h"
#include "tz_store.h"

// The order and byte formats come from a capture of the older Panasonic app with the S5II (lumix.h).

namespace {

// Waiting for the camera's 01 (lumix::OLDER_KNOWN). Pairing: the camera waits for the one-time
// Wi-Fi step, so 300 s leaves time for a computer to join its network and send it. A lasting
// connection: the camera that already knows the card sends 01 right after the device name.
constexpr uint32_t KNOWN_WAIT_PAIR_MS = 300000;
constexpr uint32_t KNOWN_WAIT_CONNECT_MS = 15000;
// A camera that waits for the Wi-Fi step takes its time up to KNOWN_WAIT_PAIR_MS after the card introduced
// itself, when the user may have carried the card indoors to the computer and the GPS lost its fix. The
// time of the last fix then counts (gps::utcSinceFix(): millis() on the 32 kHz crystal, a few ms in 10 min)
// for at most this long.
constexpr uint32_t TIME_FROM_FIX_MAX_MS = 600000;

constexpr uint16_t FIRST_HANDLE = 0x0001;
constexpr uint16_t LAST_HANDLE = 0xFFFF;

// The name the camera shows and the computer registers over Wi-Fi; the same on both channels.
constexpr const char *DEVICE_NAME = "T1000-E";

enum class Step { Idle, Connecting, Mtu, Characteristics, Key, DeviceId, DeviceName, CameraName, CameraText,
                  WaitKnown, Time, TimeAgain, Done };

Step step = Step::Idle; // the time limit of each step: camera_connection
bool keepOpen = false; // true: the connection goes to the caller once the camera accepts (the link)
session_end::Reason ending = session_end::Reason::Failed; // endReason(); Accepted once the camera took the card
uint8_t cardUuid[lumix::UUID_LENGTH];
uint16_t handles[camera_link::OLDER_CHARACTERISTIC_COUNT];
uint8_t key[lumix::KEY_LENGTH];
bool knownByLocationNotify = false;
bool knownByTimeNotify = false;
bool timeFromFixLogged = false; // "the time of the fix ... ago" said once per session
char failureText[80] = "";

// The data of the write in progress; the SoftDevice may read it until WriteDone.
uint8_t txData[device_identity::FIELD_LENGTH];
static_assert(lumix::TIME_LENGTH <= sizeof(txData), "every write fits txData");

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
    case Step::Done:
        return "accepted";
    }
    return "?";
}

void enter(Step next, uint32_t limitMs)
{
    step = next;
    camera_connection::enter(limitMs);
}

// Ends the session for this reason, with the failure text. Disconnects; the pending event is ignored.
void end(session_end::Reason reason, const char *format, va_list args)
{
    vsnprintf(failureText, sizeof(failureText), format, args);
    ending = reason;
    camera_link::close();
    step = Step::Idle;
    camera_connection::release();
}

void endWith(session_end::Reason reason, const char *format, ...) __attribute__((format(printf, 2, 3)));
void endWith(session_end::Reason reason, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    end(reason, format, args);
    va_end(args);
}

// Ends the session with a connection problem (Failed).
void fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
void fail(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    end(session_end::Reason::Failed, format, args);
    va_end(args);
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
    enter(next, camera_link::REQUEST_TIMEOUT_MS);
}

void startRead(camera_link::OlderCharacteristic which, Step next)
{
    if (!camera_link::read(handles[which])) {
        fail("%s: read refused by the SoftDevice", CHARACTERISTIC_NAMES[which]);
        return;
    }
    enter(next, camera_link::REQUEST_TIMEOUT_MS);
}

// The time of this moment with the zone of 'tz', into txData; without a fresh GPS time the time of the
// last fix (TIME_FROM_FIX_MAX_MS). False without either.
bool makeTimePacket()
{
    camera_connection::TimeSource source;
    if (!camera_connection::timePacket(TIME_FROM_FIX_MAX_MS, txData, &source))
        return false;
    if (source.fromLastFix && !timeFromFixLogged) {
        logger::line("older: no fresh GPS time - the time of the fix %lu s ago",
                     (unsigned long)(source.fixAgeMs / 1000));
        timeFromFixLogged = true;
    }
    return true;
}

// The camera took the card. The connection stays open: pairing cancels after Accepted, and a lasting
// connection goes to the caller (poll()).
void accept()
{
    logger::line("older: the camera took the card");
    ending = session_end::Reason::Accepted;
    enter(Step::Done, camera_connection::NO_LIMIT);
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
// network by hand from the camera's screen. Sent in a line the app reads as it comes, and not kept in
// the history ('log', Copy log): the app that resumes a pairing builds the identity from the card's
// UUID in 'p' (docs/index.html). Only for pairing - a lasting connection needs no Wi-Fi step.
void logAppRegistration()
{
    uint8_t seed[device_identity::SEED_LENGTH];
    device_identity::cardSeed(cardUuid, seed);
    char identity[device_identity::TEXT_LENGTH + 1];
    if (device_identity::identityText(seed, identity, sizeof(identity)) == 0) {
        logger::line("older: the identity text for the app could not be built");
        return;
    }
    logger::transient("older: app register identity %s name %s", identity, DEVICE_NAME);
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
    uint8_t seed[device_identity::SEED_LENGTH]; // the same as the computer registers over Wi-Fi
    device_identity::cardSeed(cardUuid, seed);
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
            enter(Step::Characteristics, camera_link::REQUEST_TIMEOUT_MS);
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
    endWith(session_end::Reason::Refused, "%s: refused (GATT 0x%04x)", CHARACTERISTIC_NAMES[which], event.status);
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
    if (!camera_connection::requestMtu()) {
        fail("MTU: request refused");
        return;
    }
    enter(Step::Mtu, camera_link::MTU_TIMEOUT_MS);
}

void onMtu(const camera_link::Event &event)
{
    logger::line("older: MTU %u - looking for the characteristics in all handles", camera_connection::mtu(event));
    if (!camera_link::findOlderCharacteristics(FIRST_HANDLE, LAST_HANDLE))
        fail("characteristics: request refused");
    else
        enter(Step::Characteristics, camera_link::REQUEST_TIMEOUT_MS);
}

// Events that may come in any step of a connection. Returns true if it handled the event.
bool handleCommonEvent(const camera_link::Event &event)
{
    if (camera_connection::securityRequest(event))
        return true;
    using camera_link::EventType;
    switch (event.type) {
    case EventType::Disconnected:
        if (step == Step::Done)
            return true; // already accepted; the caller cancels
        endWith(camera_connection::disconnectedReason(event.status, step == Step::Connecting),
                "the camera disconnected (HCI 0x%02x)", event.status);
        return true;
    case EventType::GattTimeout:
        fail("no ATT answer for 30 s");
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
            // As newer_session: in a lasting connection the camera takes the zone from the packet before.
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
    case Step::Idle:
    case Step::WaitKnown:
    case Step::Done:
        break;
    }
}

void handleTimeout()
{
    if (step == Step::WaitKnown) { // a camera may be slow; the link counts it (link_policy)
        endWith(session_end::Reason::NotTaken, "no 01 from the camera in %lu s",
                (unsigned long)camera_connection::limitSeconds());
        return;
    }
    endWith(step == Step::Connecting ? session_end::Reason::NoAnswer : session_end::Reason::Failed,
            "no answer in step '%s' in %lu s", stepName(step), (unsigned long)camera_connection::limitSeconds());
}

} // namespace

namespace older_session {

bool start(const camera_list::Report &camera, const uint8_t uuid[lumix::UUID_LENGTH], bool keepConnection)
{
    const char *refusal = camera_connection::start(camera);
    if (refusal != nullptr) {
        snprintf(failureText, sizeof(failureText), "%s", refusal);
        return false;
    }
    step = Step::Connecting; // camera_connection::start() began its time limit
    memcpy(cardUuid, uuid, sizeof(cardUuid));
    keepOpen = keepConnection;
    ending = session_end::Reason::Failed;
    timeFromFixLogged = false;
    knownByLocationNotify = false;
    knownByTimeNotify = false;
    memset(handles, 0, sizeof(handles));
    failureText[0] = '\0';
    return true;
}

Result poll()
{
    if (step == Step::Idle)
        return Result::Failed;
    // A connection gone without its Disconnected event (camera_link's queue was full): read before the
    // events, so that a Disconnected queued meanwhile is handled as usual. While waiting for the
    // Wi-Fi step only the 300 s limit would end the session.
    const bool gone = step != Step::Connecting && !camera_link::connected();
    camera_link::Event event;
    while (step != Step::Idle && step != Step::Done && camera_link::nextEvent(&event))
        handleEvent(event);
    if (step == Step::Done) {
        if (keepOpen) { // the connection is the caller's now; its events too
            step = Step::Idle;
            camera_connection::release();
        }
        return Result::Accepted; // pairing: again at every poll until the caller cancels
    }
    if (step == Step::Idle)
        return session_end::resultOf(ending);
    if (gone) {
        fail("the connection ended without its event");
        return Result::Failed;
    }
    if (camera_connection::overLimit()) {
        handleTimeout();
        if (step == Step::Idle)
            return session_end::resultOf(ending);
    }
    return Result::Running;
}

const char *failure()
{
    return failureText;
}

session_end::Reason endReason()
{
    return ending;
}

uint16_t locationHandle()
{
    return handles[camera_link::OLDER_LOCATION];
}

uint16_t locationNotifyHandle()
{
    return handles[camera_link::OLDER_LOCATION_NOTIFY];
}

void cancel()
{
    if (step == Step::Idle)
        return;
    camera_connection::cancel();
    ending = session_end::Reason::Failed;
    step = Step::Idle;
}

void logState()
{
    logger::transient("older: %s", step == Step::Idle ? "idle" : stepName(step));
}

} // namespace older_session
