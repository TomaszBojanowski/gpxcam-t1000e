// The fake of camera_link (fake.h): the requests of the state machines go to a test camera that
// answers at once through the event queue, as the real camera_link turns the SoftDevice's events
// into Events. As there: connect() drops the events of an earlier connection, connected() is true
// from the Connected event until the Disconnected one, and requests without a connection are refused.
#include <stdio.h>
#include <string.h>

#include <deque>

#include <ble_hci.h>

#include "camera_link.h"
#include "fake.h"

namespace fake {

namespace {

struct Queued {
    uint32_t readableAtMs;
    camera_link::Event event;
};
std::deque<Queued> events;
bool linkOpen = false;
bool dropNext = false;       // loseNextEvent()
bool dieOnFirstRequest = false; // Answer::NotEstablished: the camera sends nothing after connecting
bool cccdAsked = false;      // Descriptors::CccdInSecondAnswer: the first answer had none
uint32_t lost = 0;

void ask(const char *what, uint16_t handle = 0)
{
    advance(requestMs);
    char text[40];
    if (handle != 0)
        snprintf(text, sizeof(text), "%s %04x", what, handle);
    else
        snprintf(text, sizeof(text), "%s", what);
    requests.push_back(text);
}

void queue(const camera_link::Event &event)
{
    if (dropNext) {
        dropNext = false;
        lost++;
        return;
    }
    events.push_back({now() + answerMs, event});
}

camera_link::Event made(camera_link::EventType type, uint16_t status = BLE_GATT_STATUS_SUCCESS)
{
    camera_link::Event event = {};
    event.type = type;
    event.status = status;
    return event;
}

bool matches(const Trigger &trigger, Request request, uint16_t handle)
{
    return trigger.request == request && (trigger.handle == 0 || trigger.handle == handle);
}

// What every request does first: refused without a connection; the camera may end the
// connection or stay silent. True if the request gets its answer.
bool answers(Request request, uint16_t handle, bool *accepted)
{
    *accepted = linkOpen;
    if (!linkOpen)
        return false;
    if (dieOnFirstRequest) {
        dieOnFirstRequest = false;
        endConnection(BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED);
        return false;
    }
    if (matches(camera.disconnectAt, request, handle)) {
        endConnection(camera.disconnectReason);
        return false;
    }
    return !matches(camera.silent, request, handle);
}

uint16_t status(Request request, uint16_t handle)
{
    return matches(camera.refuse, request, handle) ? camera.refuseStatus : BLE_GATT_STATUS_SUCCESS;
}

void addNewer(camera_link::Event *event, camera_link::Characteristic which, uint16_t handle, uint8_t properties)
{
    event->characteristics.list[event->characteristics.count++] = {which, properties, handle};
}

void addOlder(camera_link::Event *event, camera_link::OlderCharacteristic which, uint16_t handle, uint8_t properties)
{
    event->characteristics.olderList[event->characteristics.olderCount++] = {which, properties, handle};
}

} // namespace

const uint8_t SERIAL[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, 0x9a};
Camera camera;
uint32_t requestMs = 0;
uint32_t answerMs = 0;
std::vector<std::string> requests;

void resetCameraLink()
{
    events.clear();
    linkOpen = false;
    dropNext = false;
    dieOnFirstRequest = false;
    cccdAsked = false;
    camera = Camera();
    requestMs = 0;
    answerMs = 0;
    requests.clear();
}

int askedCount(const char *request)
{
    int count = 0;
    for (const std::string &asked : requests)
        count += asked == request;
    return count;
}

bool linkUp()
{
    return linkOpen;
}

void notify(uint16_t handle, uint8_t value)
{
    camera_link::Event event = made(camera_link::EventType::Notified);
    event.read.handle = handle;
    event.read.length = 1;
    event.read.data[0] = value;
    queue(event);
}

void endConnection(uint8_t reason)
{
    if (!linkOpen)
        return;
    linkOpen = false; // camera_link clears its handle before it queues the event
    queue(made(camera_link::EventType::Disconnected, reason));
}

void loseNextEvent()
{
    dropNext = true;
}

} // namespace fake

namespace camera_link {

using fake::Request;

bool nextEvent(Event *event)
{
    if (fake::events.empty() || (int32_t)(fake::now() - fake::events.front().readableAtMs) < 0)
        return false;
    *event = fake::events.front().event;
    fake::events.pop_front();
    return true;
}

bool connect(const camera_list::Report &)
{
    fake::ask("connect");
    if (fake::linkOpen)
        return false;
    fake::events.clear(); // answers from an earlier connection
    switch (fake::camera.answer) {
    case fake::Answer::Connects:
    case fake::Answer::NotEstablished:
        fake::linkOpen = true;
        fake::dieOnFirstRequest = fake::camera.answer == fake::Answer::NotEstablished;
        fake::queue(fake::made(EventType::Connected));
        break;
    case fake::Answer::NoAnswer:
        break;
    }
    return true;
}

void cancelConnect()
{
    fake::ask("cancel connect");
}

bool connected()
{
    return fake::linkOpen;
}

bool disconnect()
{
    fake::ask("disconnect");
    if (!fake::linkOpen)
        return false;
    fake::linkOpen = false;
    fake::queue(fake::made(EventType::Disconnected, BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION));
    return true;
}

bool exchangeMtu(uint16_t)
{
    fake::ask("mtu");
    bool accepted = false;
    if (fake::answers(Request::Mtu, 0, &accepted)) {
        Event event = fake::made(EventType::MtuExchanged);
        event.mtu = 57; // what the S5II grants
        fake::queue(event);
    }
    return accepted;
}

bool findService()
{
    fake::ask("service");
    bool accepted = false;
    if (fake::answers(Request::Service, 0, &accepted)) {
        if (fake::matches(fake::camera.refuse, Request::Service, 0)) {
            fake::queue(fake::made(EventType::ServiceFound, fake::camera.refuseStatus));
        } else if (fake::camera.dialect == fake::Dialect::Older) {
            fake::queue(fake::made(EventType::ServiceFound, BLE_GATT_STATUS_ATTERR_ATTRIBUTE_NOT_FOUND));
        } else {
            Event event = fake::made(EventType::ServiceFound);
            event.service = {true, fake::SERVICE_START, fake::SERVICE_END};
            fake::queue(event);
        }
    }
    return accepted;
}

bool findCharacteristics(uint16_t, uint16_t)
{
    fake::ask("characteristics");
    bool accepted = false;
    if (fake::answers(Request::Characteristics, 0, &accepted)) {
        Event event = fake::made(EventType::CharacteristicsFound);
        fake::addNewer(&event, KEY, fake::KEY, PROPERTY_READ);
        fake::addNewer(&event, DEVICE_ID, fake::DEVICE_ID, PROPERTY_WRITE);
        fake::addNewer(&event, APP_NAME, fake::APP_NAME, PROPERTY_WRITE);
        fake::addNewer(&event, DEVICE_NAME, fake::DEVICE_NAME, PROPERTY_WRITE);
        fake::addNewer(&event, TIME, fake::TIME, PROPERTY_WRITE);
        fake::addNewer(&event, GPS_MODE, fake::GPS_MODE, PROPERTY_WRITE);
        fake::addNewer(&event, GPS_NOTIFY, fake::GPS_NOTIFY, PROPERTY_NOTIFY);
        fake::addNewer(&event, LOCATION, fake::LOCATION, PROPERTY_WRITE);
        event.characteristics.lastHandle = fake::SERVICE_END; // the end of the service ends the discovery
        fake::queue(event);
    }
    return accepted;
}

bool findOlderCharacteristics(uint16_t, uint16_t)
{
    fake::ask("older characteristics");
    bool accepted = false;
    if (fake::answers(Request::Characteristics, 0, &accepted)) {
        Event event = fake::made(EventType::CharacteristicsFound);
        fake::addOlder(&event, OLDER_KEY, fake::OLDER_KEY, PROPERTY_READ);
        fake::addOlder(&event, OLDER_DEVICE_ID, fake::OLDER_DEVICE_ID, PROPERTY_WRITE);
        fake::addOlder(&event, OLDER_DEVICE_NAME, fake::OLDER_DEVICE_NAME, PROPERTY_WRITE);
        fake::addOlder(&event, OLDER_CAMERA_NAME, fake::OLDER_CAMERA_NAME, PROPERTY_READ);
        fake::addOlder(&event, OLDER_CAMERA_TEXT, fake::OLDER_CAMERA_TEXT, PROPERTY_READ);
        fake::addOlder(&event, OLDER_TIME, fake::OLDER_TIME, PROPERTY_WRITE);
        fake::addOlder(&event, OLDER_TIME_NOTIFY, fake::OLDER_TIME_NOTIFY, PROPERTY_NOTIFY);
        fake::addOlder(&event, OLDER_LOCATION, fake::OLDER_LOCATION, PROPERTY_WRITE);
        fake::addOlder(&event, OLDER_LOCATION_NOTIFY, fake::OLDER_LOCATION_NOTIFY, PROPERTY_NOTIFY);
        event.characteristics.lastHandle = 0xFFFF; // the last handle ends the discovery
        fake::queue(event);
    }
    return accepted;
}

bool findDescriptors(uint16_t startHandle, uint16_t)
{
    fake::ask("descriptors", startHandle);
    bool accepted = false;
    if (fake::answers(Request::Descriptors, 0, &accepted)) {
        Event event = fake::made(EventType::DescriptorsFound);
        event.descriptors.lastHandle = startHandle;
        switch (fake::camera.descriptors) {
        case fake::Descriptors::NoCccd: // the S5II: the next characteristic follows the value at once
            event.descriptors.nextCharacteristic = true;
            break;
        case fake::Descriptors::Cccd:
            event.descriptors.cccdHandle = startHandle;
            break;
        case fake::Descriptors::CccdInSecondAnswer:
            if (fake::cccdAsked)
                event.descriptors.cccdHandle = startHandle;
            fake::cccdAsked = true;
            break;
        }
        fake::queue(event);
    }
    return accepted;
}

bool read(uint16_t handle)
{
    fake::ask("read", handle);
    bool accepted = false;
    if (fake::answers(Request::Read, handle, &accepted)) {
        Event event = fake::made(EventType::ReadDone, fake::status(Request::Read, handle));
        event.read.handle = handle;
        event.read.length = lumix::KEY_LENGTH;
        const uint8_t key[lumix::KEY_LENGTH] = {0x01, 0x02, 0x03, 0x04}; // docs/PROTOCOL.md's example key
        memcpy(event.read.data, key, sizeof(key));
        fake::queue(event);
    }
    return accepted;
}

bool write(uint16_t handle, const uint8_t *, uint16_t)
{
    fake::ask("write", handle);
    bool accepted = false;
    if (fake::answers(Request::Write, handle, &accepted)) {
        const uint16_t status = fake::status(Request::Write, handle);
        fake::queue(fake::made(EventType::WriteDone, status));
        if (handle == fake::OLDER_DEVICE_NAME && status == BLE_GATT_STATUS_SUCCESS && fake::camera.knowsCard)
            fake::notify(fake::OLDER_LOCATION_NOTIFY, lumix::OLDER_KNOWN);
    }
    return accepted;
}

uint32_t lostEvents()
{
    return fake::lost;
}

} // namespace camera_link
