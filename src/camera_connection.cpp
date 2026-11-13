#include "camera_connection.h"

#include <Arduino.h>
#include <ble_gatt.h>
#include <ble_hci.h>
#include <stdio.h>

#include "gps.h"
#include "log.h"
#include "tz_store.h"
#include "utc.h"

namespace {

bool owned = false; // a session has the connection
uint32_t stepStart = 0;
uint32_t stepLimit = 0; // set by enter()
session_end::Reason ending = session_end::Reason::Failed;
char failureText[80] = "";

} // namespace

namespace camera_connection {

bool start(const camera_list::Report &camera)
{
    ending = session_end::Reason::Failed;
    if (owned) {
        setFailure("a session is running");
        return false;
    }
    if (camera.camera.state == lumix::STATE_STANDBY) {
        setFailure("the camera is in standby");
        return false;
    }
    if (camera_link::connected()) {
        camera_link::close();
        setFailure("an earlier connection was still open - closed now");
        return false;
    }
    if (!camera_link::connect(camera)) {
        setFailure("connect: refused by the SoftDevice");
        return false;
    }
    owned = true;
    failureText[0] = '\0';
    enter(camera_link::CONNECT_TIMEOUT_MS);
    return true;
}

void release()
{
    owned = false;
}

void cancel()
{
    camera_link::close();
    owned = false;
    ending = session_end::Reason::Failed;
}

session_end::Reason endReason()
{
    return ending;
}

const char *failure()
{
    return failureText;
}

void setEndReason(session_end::Reason reason)
{
    ending = reason;
}

void setFailure(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    setFailureV(format, args);
    va_end(args);
}

void setFailureV(const char *format, va_list args)
{
    vsnprintf(failureText, sizeof(failureText), format, args);
}

void enter(uint32_t limitMs)
{
    stepStart = millis();
    stepLimit = limitMs;
}

bool overLimit()
{
    return millis() - stepStart >= stepLimit;
}

uint32_t limitSeconds()
{
    return stepLimit / 1000;
}

bool requestMtu()
{
    return camera_link::exchangeMtu(camera_link::MAX_MTU);
}

uint16_t mtu(const camera_link::Event &event)
{
    return event.status == BLE_GATT_STATUS_SUCCESS ? min(camera_link::MAX_MTU, event.mtu) : BLE_GATT_ATT_MTU_DEFAULT;
}

bool securityRequest(const camera_link::Event &event)
{
    if (event.type != camera_link::EventType::SecurityRequest)
        return false;
    logger::line("%s", camera_link::SECURITY_REQUEST_LINE);
    return true;
}

session_end::Reason disconnectedReason(uint16_t hciReason, bool connecting)
{
    if (hciReason == BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED)
        return session_end::Reason::NotEstablished;
    return connecting ? session_end::Reason::Failed : session_end::Reason::EndedEarly;
}

bool timePacket(uint32_t lastFixMaxMs, uint8_t data[lumix::TIME_LENGTH], TimeSource *source)
{
    int64_t now = 0;
    source->fromLastFix = false;
    source->fixAgeMs = 0;
    if (!gps::utcNow(&now)) {
        if (lastFixMaxMs == FRESH_TIME_ONLY || !gps::utcSinceFix(lastFixMaxMs, &now, &source->fixAgeMs))
            return false;
        source->fromLastFix = true;
    }
    source->zone = posix_tz::cameraZone(tz_store::rule(), now);
    lumix::timeData(utc::fromUnixSeconds(now), source->zone.standardOffsetMinutes, source->zone.summerTime, data);
    return true;
}

} // namespace camera_connection
