// The BLE connection to the camera (M4): GATT client requests made directly with the
// SoftDevice API. Bluefruit's blocking helpers are not used: they wait only 100 ms
// (BLE_GENERIC_TIMEOUT) and create a semaphore on the heap for every call.
// Requests return at once; their results come back as events through a static queue.
// One connection at a time, one GATT request at a time. Loop task only, except
// onBleEvent().
#pragma once

#include <ble.h>
#include <stddef.h>
#include <stdint.h>

#include "camera_list.h"

namespace camera_link {

// The characteristics the card uses (lumix.h, docs/PROTOKOL.md).
enum Characteristic : uint8_t { KEY, DEVICE_ID, APP_NAME, DEVICE_NAME, TIME, CHARACTERISTIC_COUNT };

enum class EventType : uint8_t {
    Connected,
    Disconnected,
    MtuExchanged,
    ServiceFound,         // service.found false: not on the camera
    CharacteristicsFound, // a part of the service; more may follow
    ReadDone,
    WriteDone,
    GattTimeout,          // no ATT response for 30 s: the link is unusable
    SecurityRequest,      // the camera asked for encryption; not answered (as the gpxcam app)
};

constexpr size_t MAX_READ_LENGTH = 8;

// ATT MTU of the camera link (M4 decision, as the gpxcam app asks for): the SoftDevice is
// configured for it in bluetooth::begin(), the session asks the camera for it.
constexpr uint16_t MAX_MTU = 185;

struct FoundCharacteristic {
    Characteristic which;
    uint8_t properties; // PROPERTY_* bits
    uint16_t valueHandle;
};

constexpr uint8_t PROPERTY_READ = 0x01;
constexpr uint8_t PROPERTY_WRITE = 0x02; // write with response

struct Event {
    EventType type;
    uint16_t status; // GATT status (0 = success); for Disconnected the HCI reason
    uint16_t mtu;    // MtuExchanged: the camera's receive MTU
    struct {
        bool found;
        uint16_t startHandle;
        uint16_t endHandle;
    } service;
    struct {
        uint8_t count; // ours among those in this response
        FoundCharacteristic list[CHARACTERISTIC_COUNT];
        uint16_t lastHandle; // the last value handle in this response
    } characteristics;
    struct {
        uint16_t handle;
        uint8_t length;
        uint8_t data[MAX_READ_LENGTH];
    } read;
};

// Registers the service and characteristic UUIDs with the SoftDevice. Call once after
// Bluefruit.begin().
bool begin();

// For bluetooth.cpp, in Bluefruit's BLE task: turns the events of the camera connection
// into Events. Never blocks; an event that does not fit in the queue is counted as lost.
void onBleEvent(const ble_evt_t *event);

// Next event, or false if none. Events left over from an earlier connection are dropped
// by connect().
bool nextEvent(Event *event);

// Starts connecting to the camera of the report (its address and type). Scanning must be
// off. False if the SoftDevice refused.
bool connect(const camera_list::Report &camera);
void cancelConnect();
bool connected();

// The requests below need a connection; false if the SoftDevice refused.
bool exchangeMtu(uint16_t mtu);
bool findService();
bool findCharacteristics(uint16_t startHandle, uint16_t endHandle);
bool read(uint16_t handle);
// Write with response. The data must stay unchanged until WriteDone.
bool write(uint16_t handle, const uint8_t *data, uint16_t length);
bool disconnect();

// Events lost because the queue was full, since start.
uint32_t lostEvents();

} // namespace camera_link
