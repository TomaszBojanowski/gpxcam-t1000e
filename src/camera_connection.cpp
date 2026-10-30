#include "camera_connection.h"

#include <Arduino.h>
#include <ble_gatt.h>
#include <ble_hci.h>

#include "gps.h"
#include "log.h"
#include "tz_store.h"
#include "utc.h"

namespace {

bool owned = false; // a session has the connection
uint32_t stepStart = 0;
uint32_t stepLimit = camera_connection::NO_LIMIT;

} // namespace

namespace camera_connection {

const char *start(const camera_list::Report &camera)
{
    if (owned)
        return "a session is running";
    if (camera.camera.state == lumix::STATE_STANDBY)
        return "the camera is in standby";
    if (camera_link::connected()) {
        camera_link::close();
        return "an earlier connection was still open - closed now";
    }
    if (!camera_link::connect(camera))
        return "connect: refused by the SoftDevice";
    owned = true;
    enter(camera_link::CONNECT_TIMEOUT_MS);
    return nullptr;
}

void release()
{
    owned = false;
}

void cancel()
{
    camera_link::close();
    owned = false;
}

void enter(uint32_t limitMs)
{
    stepStart = millis();
    stepLimit = limitMs;
}

bool overLimit()
{
    return stepLimit != NO_LIMIT && millis() - stepStart >= stepLimit;
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
