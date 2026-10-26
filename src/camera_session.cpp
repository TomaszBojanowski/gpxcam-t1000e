#include "camera_session.h"

#include <Arduino.h>
#include <ble_gatt.h>
#include <ble_hci.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "camera_link.h"
#include "gps.h"
#include "handle_cache.h"
#include "link_policy.h"
#include "log.h"
#include "posix_tz.h"
#include "tz_store.h"
#include "utc.h"
#include "version.h"

namespace {

using camera_link::Characteristic;

// Time limits (M4 decisions; connecting: camera_link::CONNECT_TIMEOUT_MS).
constexpr uint32_t MTU_MS = 3000;
constexpr uint32_t STEP_MS = 5000; // service, characteristics, key and every write
constexpr uint32_t DISCONNECT_MS = 5000;

// A write request carries 3 bytes of ATT header; the longest write is the 32-byte app name.
constexpr uint16_t MTU_REQUEST = camera_link::MAX_MTU;
constexpr uint16_t ATT_WRITE_HEADER = 3;
constexpr uint16_t MTU_NEEDED = lumix::APP_NAME_LENGTH + ATT_WRITE_HEADER;

// M4 decisions: names shown by the camera.
constexpr const char *DEVICE_NAME = "T1000-E";
constexpr const char *APP_NAME = "gpxcam T1000-E " FW_VERSION;

enum class Step { Idle, Connecting, Mtu, Service, Characteristics, Key, DeviceId, AppName, DeviceName, Time,
                  TimeAgain, Disconnecting, Done };

Step step = Step::Idle;
uint32_t stepStart = 0;
uint32_t stepLimit = 0;
bool keepOpen = false;
bool timeWritten = true; // set at the start: false only with the clock off in a lasting connection
bool accepted = false;
bool rejected = false; // a refusal that means the camera does not know the card (link_policy)
bool establishFailed = false; // HCI 0x3e: the connection was not established
bool connectTimedOut = false; // no connection within camera_link::CONNECT_TIMEOUT_MS
bool endedEarly = false;      // the connection ended after it was made, during the introduction
bool newerServiceAbsent = false; // the newer-dialect service (1FE71B85) was not on the camera
char failureText[80] = "";

uint8_t cardUuid[lumix::UUID_LENGTH];
camera_session::Discovered info = {}; // MTU, service end, handles and properties

// The handles kept from the last introduction (handle_cache, camera_session::forgetHandles()).
static_assert(handle_cache::CHARACTERISTIC_COUNT == camera_link::CHARACTERISTIC_COUNT, "same characteristics");
uint8_t sessionSerial[lumix::SERIAL_LENGTH]; // the camera of this session
handle_cache::Handles keptHandles = {};       // valid when usingKept
bool usingKept = false;                       // this session skips the discovery
uint8_t key[lumix::KEY_LENGTH];

// The data of the write in progress; the SoftDevice may read it until WriteDone.
uint8_t txData[lumix::APP_NAME_LENGTH];
static_assert(lumix::DEVICE_ID_LENGTH <= sizeof(txData) && lumix::DEVICE_NAME_MAX_LENGTH <= sizeof(txData) &&
                  lumix::TIME_LENGTH <= sizeof(txData),
              "every write fits txData");

const char *stepName(Step s)
{
    switch (s) {
    case Step::Connecting:
        return "connect";
    case Step::Mtu:
        return "MTU";
    case Step::Service:
        return "service";
    case Step::Characteristics:
        return "characteristics";
    case Step::Key:
        return "key";
    case Step::DeviceId:
        return "identifier";
    case Step::AppName:
        return "app name";
    case Step::DeviceName:
        return "device name";
    case Step::Time:
        return "time";
    case Step::TimeAgain:
        return "time again";
    case Step::Disconnecting:
        return "disconnect";
    case Step::Idle:
    case Step::Done:
        break;
    }
    return "-";
}

void enter(Step next, uint32_t limitMs)
{
    step = next;
    stepStart = millis();
    stepLimit = limitMs;
}

void setFailure(const char *format, ...) __attribute__((format(printf, 1, 2)));
void setFailure(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(failureText, sizeof(failureText), format, args);
    va_end(args);
}

// Closes the connection (also after success) and waits for the Disconnected event.
void closeLink()
{
    if (camera_link::connected() && camera_link::disconnect())
        enter(Step::Disconnecting, DISCONNECT_MS);
    else
        step = Step::Done;
}

// After a full discovery the camera accepted: the handles serve the next session.
void keepHandles()
{
    handle_cache::Handles handles = {};
    handles.serviceEnd = info.serviceEnd;
    memcpy(handles.valueHandle, info.valueHandle, sizeof(handles.valueHandle));
    memcpy(handles.properties, info.properties, sizeof(handles.properties));
    handle_cache::keep(sessionSerial, handles);
    logger::transient("camera: handles kept for the next connection");
}

void fail()
{
    if (handle_cache::forgetAfterFailure(step != Step::Connecting, false))
        camera_session::forgetHandles(failureText);
    accepted = false;
    closeLink();
}

void requestCharacteristics(uint16_t fromHandle)
{
    if (!camera_link::findCharacteristics(fromHandle, info.serviceEnd)) {
        setFailure("characteristics: request refused");
        fail();
    }
}

// Starts a write of txData[0..length) to the characteristic and moves to the next step.
void startWrite(Characteristic which, size_t length, Step next)
{
    if (length + ATT_WRITE_HEADER > info.mtu) {
        setFailure("%s: %u bytes do not fit MTU %u", stepName(next), (unsigned)length, info.mtu);
        fail();
    } else if (!camera_link::write(info.valueHandle[which], txData, (uint16_t)length)) {
        setFailure("%s: write refused by the SoftDevice", stepName(next));
        fail();
    } else {
        enter(next, STEP_MS);
    }
}

void writeDeviceId()
{
    lumix::deviceIdPlain(cardUuid, txData);
    lumix::applyKey(txData, lumix::DEVICE_ID_LENGTH, key);
    startWrite(camera_link::DEVICE_ID, lumix::DEVICE_ID_LENGTH, Step::DeviceId);
}

void writeAppName()
{
    lumix::appName(APP_NAME, txData);
    startWrite(camera_link::APP_NAME, lumix::APP_NAME_LENGTH, Step::AppName);
}

void writeDeviceName()
{
    const size_t length = lumix::deviceNamePlain(DEVICE_NAME, txData);
    lumix::applyKey(txData, length, key);
    startWrite(camera_link::DEVICE_NAME, length, Step::DeviceName);
}

// Puts the time of this moment with the time zone set by 'tz' (tz_store) into txData, as the
// camera expects (docs/PROTOCOL.md). False without GPS time.
bool makeTimePacket(posix_tz::CameraZone *zone)
{
    int64_t now = 0;
    if (!gps::utcNow(&now))
        return false;
    *zone = posix_tz::cameraZone(tz_store::rule(), now);
    lumix::timeData(utc::fromUnixSeconds(now), zone->standardOffsetMinutes, zone->summerTime, txData);
    return true;
}

void writeTime()
{
    posix_tz::CameraZone zone;
    if (!makeTimePacket(&zone)) {
        setFailure("time: no GPS time any more");
        fail();
        return;
    }
    logger::transient("camera: time zone %d min%s", zone.standardOffsetMinutes,
                      zone.summerTime ? " + summer time" : "");
    startWrite(camera_link::TIME, lumix::TIME_LENGTH, Step::Time);
}

// With the clock off ('clock off', tz_store) a connection that stays open writes no time: the
// camera keeps its own clock and time zone and still takes the card and its locations (tested
// on the S5II, docs/PROTOCOL.md). Pairing always writes the time.
void introduceWithoutTime()
{
    logger::transient("camera: clock off - no time written");
    accepted = true;
    if (!usingKept)
        keepHandles();
    step = Step::Done; // the link goes on with the open connection
}

// The second time write only makes the zone of the first count (onTimeAccepted()): when it
// cannot be sent or is refused, the link goes on and the zone counts from the next connection.
void endTimeAgain(const char *what)
{
    logger::line("camera: time again %s - the time zone counts from the next connection", what);
    step = Step::Done; // the link goes on with the open connection
}

// No answer in time still ends the session (handleTimeout()): the write stays pending in the
// SoftDevice, which then refuses the link's next write.
void writeTimeAgain()
{
    posix_tz::CameraZone zone;
    if (!makeTimePacket(&zone))
        endTimeAgain("not sent: no GPS time any more");
    else if (!camera_link::write(info.valueHandle[camera_link::TIME], txData, lumix::TIME_LENGTH))
        endTimeAgain("not sent: write refused by the SoftDevice");
    else
        enter(Step::TimeAgain, STEP_MS);
}

void readKey()
{
    for (uint8_t which = 0; which < camera_link::INTRODUCTION_CHARACTERISTICS; which++) {
        if (info.valueHandle[which] == 0) {
            setFailure("characteristics: number %u (0 = key .. 4 = time) not found", (unsigned)which);
            fail();
            return;
        }
        const uint8_t needed = which == camera_link::KEY ? camera_link::PROPERTY_READ : camera_link::PROPERTY_WRITE;
        if ((info.properties[which] & needed) == 0) {
            setFailure("characteristics: no %s on characteristic %u", needed == camera_link::PROPERTY_READ ? "read" : "write",
                       (unsigned)which);
            fail();
            return;
        }
    }
    unsigned count = 0;
    for (uint16_t handle : info.valueHandle)
        count += handle != 0;
    logger::transient(usingKept ? "camera: %u of %u characteristics from the last connection"
                                : "camera: service and %u of %u characteristics found",
                      count, (unsigned)camera_link::CHARACTERISTIC_COUNT);
    if (!camera_link::read(info.valueHandle[camera_link::KEY])) {
        setFailure("key: read refused by the SoftDevice");
        fail();
        return;
    }
    enter(Step::Key, STEP_MS);
}

void onCharacteristics(const camera_link::Event &event)
{
    for (uint8_t i = 0; i < event.characteristics.count; i++) {
        const camera_link::FoundCharacteristic &entry = event.characteristics.list[i];
        info.valueHandle[entry.which] = entry.valueHandle;
        info.properties[entry.which] = entry.properties;
    }
    // ATT "attribute not found" ends the discovery; so does reaching the end of the service.
    const uint16_t last = event.characteristics.lastHandle;
    if (event.status == BLE_GATT_STATUS_SUCCESS && last != 0 && last < info.serviceEnd)
        requestCharacteristics(last + 1);
    else
        readKey();
}

link_policy::IntroductionWrite introductionWrite(Step s)
{
    switch (s) {
    case Step::DeviceId:
        return link_policy::IntroductionWrite::Identifier;
    case Step::AppName:
        return link_policy::IntroductionWrite::AppName;
    case Step::DeviceName:
        return link_policy::IntroductionWrite::DeviceName;
    default:
        return link_policy::IntroductionWrite::Time;
    }
}

// A write answer: next step on success, failure otherwise.
void onWritten(const camera_link::Event &event, void (*next)())
{
    if (event.status != BLE_GATT_STATUS_SUCCESS) {
        setFailure("%s: refused (GATT 0x%04x)", stepName(step), event.status);
        rejected = link_policy::refusalMeansUnknownCard(introductionWrite(step), timeWritten);
        fail();
        return;
    }
    logger::transient("camera: %s accepted", stepName(step));
    next();
}

// The camera knows the card. A connection that stays open writes the time once more: the
// camera takes the time zone offset from the time packet before the last one (only the
// summer-time flag from the last), so the second packet makes the zone of the first count
// (tested on the S5II, docs/PROTOCOL.md). Pairing writes it once, as up to 1.1; the
// connection after it writes it twice.
void onTimeAccepted()
{
    accepted = true;
    if (!usingKept)
        keepHandles();
    if (keepOpen)
        writeTimeAgain();
    else
        closeLink();
}

void onTimeAgainWritten(const camera_link::Event &event)
{
    if (event.status != BLE_GATT_STATUS_SUCCESS) {
        char what[32];
        snprintf(what, sizeof(what), "refused (GATT 0x%04x)", event.status);
        endTimeAgain(what);
        return;
    }
    logger::transient("camera: time again accepted");
    step = Step::Done; // the link goes on with the open connection
}

void onMtu(const camera_link::Event &event)
{
    info.mtu = event.status == BLE_GATT_STATUS_SUCCESS ? min(MTU_REQUEST, event.mtu) : BLE_GATT_ATT_MTU_DEFAULT;
    logger::transient("camera: MTU %u", info.mtu);
    if (info.mtu < MTU_NEEDED) {
        setFailure("MTU: %u, writes of 32 bytes need %u", info.mtu, MTU_NEEDED);
        fail();
    } else if (usingKept) {
        info.serviceEnd = keptHandles.serviceEnd;
        memcpy(info.valueHandle, keptHandles.valueHandle, sizeof(info.valueHandle));
        memcpy(info.properties, keptHandles.properties, sizeof(info.properties));
        readKey();
    } else if (!camera_link::findService()) {
        setFailure("service: request refused");
        fail();
    } else {
        enter(Step::Service, STEP_MS);
    }
}

void onService(const camera_link::Event &event)
{
    if (!event.service.found) {
        newerServiceAbsent = true; // 'pair' uses this to switch to the older dialect (pairing.cpp)
        setFailure("service: the newer dialect is not on this camera (GATT 0x%04x)", event.status);
        fail();
        return;
    }
    info.serviceEnd = event.service.endHandle;
    enter(Step::Characteristics, STEP_MS);
    requestCharacteristics(event.service.startHandle);
}

void onKey(const camera_link::Event &event)
{
    if (event.status != BLE_GATT_STATUS_SUCCESS || event.read.length < lumix::KEY_LENGTH) {
        setFailure("key: not read (GATT 0x%04x, %u bytes)", event.status, event.read.length);
        fail();
        return;
    }
    memcpy(key, event.read.data, lumix::KEY_LENGTH);
    logger::transient("camera: key read");
    writeDeviceId();
}

void onConnected()
{
    logger::transient("camera: connected");
    if (!camera_link::exchangeMtu(MTU_REQUEST)) {
        setFailure("MTU: request refused");
        fail();
        return;
    }
    enter(Step::Mtu, MTU_MS);
}

// Events that may come in any step.
bool handleCommonEvent(const camera_link::Event &event)
{
    switch (event.type) {
    case camera_link::EventType::Disconnected:
        if (step != Step::Disconnecting) {
            setFailure("%s: the camera disconnected (HCI 0x%02x)", stepName(step), event.status);
            establishFailed = event.status == BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED;
            endedEarly = step != Step::Connecting && !establishFailed;
            if (!accepted && handle_cache::forgetAfterFailure(true, establishFailed))
                camera_session::forgetHandles(failureText);
            accepted = false; // the link is gone, also after an accepted time
        }
        step = Step::Done;
        return true;
    case camera_link::EventType::GattTimeout:
        setFailure("%s: no ATT answer for 30 s", stepName(step));
        fail();
        return true;
    case camera_link::EventType::SecurityRequest:
        logger::line("camera: asked for encryption - not answered (as the gpxcam app)");
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
    case Step::Service:
        if (event.type == EventType::ServiceFound)
            onService(event);
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
        if (event.type == EventType::WriteDone)
            onWritten(event, writeAppName);
        break;
    case Step::AppName:
        if (event.type == EventType::WriteDone)
            onWritten(event, writeDeviceName);
        break;
    case Step::DeviceName:
        if (event.type == EventType::WriteDone)
            onWritten(event, timeWritten ? writeTime : introduceWithoutTime);
        break;
    case Step::Time:
        if (event.type == EventType::WriteDone)
            onWritten(event, onTimeAccepted);
        break;
    case Step::TimeAgain:
        if (event.type == EventType::WriteDone)
            onTimeAgainWritten(event);
        break;
    case Step::Disconnecting:
    case Step::Idle:
    case Step::Done:
        break;
    }
}

void handleTimeout()
{
    switch (step) {
    case Step::Disconnecting:
        // The SoftDevice ends the link itself by supervision timeout; the next start
        // closes a link that is still open.
        step = Step::Done;
        break;
    case Step::Connecting:
        camera_link::cancelConnect();
        connectTimedOut = true;
        setFailure("connect: no answer in %lu s", (unsigned long)(stepLimit / 1000));
        fail(); // in case the connection came up just now
        break;
    default:
        setFailure("%s: no answer in %lu s", stepName(step), (unsigned long)(stepLimit / 1000));
        fail();
        break;
    }
}

} // namespace

namespace camera_session {

bool start(const camera_list::Report &camera, const uint8_t uuid[lumix::UUID_LENGTH], bool keepConnection)
{
    if (step != Step::Idle) {
        setFailure("a session is running");
        return false;
    }
    if (camera.camera.state == lumix::STATE_STANDBY) { // connecting would wake the camera
        setFailure("the camera is in standby");
        return false;
    }
    if (camera_link::connected()) {
        camera_link::disconnect();
        setFailure("an earlier connection was still open - closed now");
        return false;
    }
    memcpy(cardUuid, uuid, sizeof(cardUuid));
    memcpy(sessionSerial, camera.camera.serial, sizeof(sessionSerial));
    const handle_cache::Lookup lookup = handle_cache::find(sessionSerial, &keptHandles);
    if (lookup == handle_cache::Lookup::OtherCameraForgotten)
        logger::transient("camera: handles forgotten - another camera");
    usingKept = lookup == handle_cache::Lookup::Found;
    info = {};
    info.mtu = BLE_GATT_ATT_MTU_DEFAULT;
    keepOpen = keepConnection;
    timeWritten = !keepConnection || tz_store::clockOn();
    accepted = false;
    rejected = false;
    establishFailed = false;
    connectTimedOut = false;
    endedEarly = false;
    newerServiceAbsent = false;
    failureText[0] = '\0';
    if (!camera_link::connect(camera)) {
        setFailure("connect: refused by the SoftDevice");
        return false;
    }
    enter(Step::Connecting, camera_link::CONNECT_TIMEOUT_MS);
    return true;
}

Result poll()
{
    if (step == Step::Idle)
        return Result::Failed;
    camera_link::Event event;
    while (step != Step::Done && camera_link::nextEvent(&event))
        handleEvent(event);
    if (step != Step::Done && millis() - stepStart >= stepLimit)
        handleTimeout();
    if (step != Step::Done)
        return Result::Running;
    step = Step::Idle;
    if (accepted)
        return Result::Accepted;
    return rejected ? Result::Rejected : Result::Failed;
}

const char *failure()
{
    return failureText;
}

bool notEstablished()
{
    return establishFailed;
}

bool endedAfterConnecting()
{
    return endedEarly;
}

camera_link::LinkFailure linkFailure()
{
    if (establishFailed)
        return camera_link::LinkFailure::NotEstablished;
    return connectTimedOut ? camera_link::LinkFailure::NoAnswer : camera_link::LinkFailure::None;
}

bool lacksNewerService()
{
    return newerServiceAbsent;
}

bool writesTime()
{
    return timeWritten;
}

const Discovered &discovered()
{
    return info;
}

void forgetHandles(const char *why)
{
    if (handle_cache::forget())
        logger::transient("camera: handles forgotten - %s", why);
}

void cancel()
{
    if (step == Step::Idle)
        return;
    if (step == Step::Connecting)
        camera_link::cancelConnect();
    if (camera_link::connected())
        camera_link::disconnect();
    accepted = false;
    step = Step::Idle;
}

} // namespace camera_session
