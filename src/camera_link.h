// The BLE connection to the camera: GATT client requests made directly with the
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

// The characteristics the card uses (lumix.h, docs/PROTOCOL.md). The first five are needed
// to introduce the card (pairing), the others for the normal work.
enum Characteristic : uint8_t {
    KEY,
    DEVICE_ID,
    APP_NAME,
    DEVICE_NAME,
    TIME,
    GPS_MODE,
    GPS_NOTIFY,
    LOCATION,
    CHARACTERISTIC_COUNT
};
constexpr uint8_t INTRODUCTION_CHARACTERISTICS = TIME + 1;

// The older dialect's characteristics, in the order camera_link registers them (lumix.h). Found
// only on demand (findOlderCharacteristics), so a newer-dialect connection never looks for them.
// The older dialect has no service UUID: these are found across the whole handle range.
enum OlderCharacteristic : uint8_t {
    OLDER_KEY,
    OLDER_DEVICE_ID,
    OLDER_DEVICE_NAME,
    OLDER_CAMERA_NAME,
    OLDER_CAMERA_TEXT,
    OLDER_TIME,
    OLDER_TIME_NOTIFY,
    OLDER_LOCATION,
    OLDER_LOCATION_NOTIFY,
    OLDER_CHARACTERISTIC_COUNT
};

enum class EventType : uint8_t {
    Connected,
    Disconnected,
    MtuExchanged,
    ServiceFound,         // service.found false: not on the camera
    CharacteristicsFound, // a part of the service; more may follow
    ReadDone,
    WriteDone,
    DescriptorsFound,     // a part of the descriptors after a characteristic; more may follow
    Notified,             // a notification or (confirmed) indication from the camera
    GattTimeout,          // no ATT response for 30 s: the link is unusable
    SecurityRequest,      // the camera asked for encryption; not answered (as the gpxcam app)
};

constexpr size_t MAX_READ_LENGTH = 8;

// ATT MTU of the camera link (as the gpxcam app asks for): the SoftDevice is
// configured for it in bluetooth::begin(), the session asks the camera for it.
constexpr uint16_t MAX_MTU = 185;

struct FoundCharacteristic {
    Characteristic which;
    uint8_t properties; // PROPERTY_* bits
    uint16_t valueHandle;
};

struct FoundOlderCharacteristic {
    OlderCharacteristic which;
    uint8_t properties; // PROPERTY_* bits
    uint16_t valueHandle;
};

constexpr uint8_t PROPERTY_READ = 0x01;
constexpr uint8_t PROPERTY_WRITE = 0x02; // write with response
constexpr uint8_t PROPERTY_NOTIFY = 0x04;
constexpr uint8_t PROPERTY_INDICATE = 0x08;

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
        uint8_t olderCount; // older-dialect ones in this response (only after findOlderCharacteristics)
        FoundOlderCharacteristic olderList[OLDER_CHARACTERISTIC_COUNT];
        uint16_t lastHandle; // the last value handle in this response
    } characteristics;
    struct {
        uint16_t cccdHandle;  // the Client Characteristic Configuration descriptor, 0 if none
        uint16_t lastHandle;  // the last handle in this response
        bool nextCharacteristic; // the next characteristic begins: no more descriptors
    } descriptors;
    struct {
        uint16_t handle;
        uint8_t length;
        uint8_t data[MAX_READ_LENGTH];
    } read; // also the data of Notified
};

// Registers the service and characteristic UUIDs of both dialects with the SoftDevice. Call once
// after Bluefruit.begin() (which must raise the vendor-UUID count first).
bool begin();

// How many 128-bit vendor UUIDs begin() registered: the newer dialect's service and 8
// characteristics plus the older dialect's 9 characteristics, so 18 when all succeed.
uint8_t registeredUuidCount();

// For bluetooth.cpp, in Bluefruit's BLE task: turns the events of the camera connection
// into Events. Never blocks; an event that does not fit in the queue is counted as lost.
void onBleEvent(const ble_evt_t *event);

// Next event, or false if none. Events left over from an earlier connection are dropped
// by connect().
bool nextEvent(Event *event);

// How long the sessions wait for a connection to come up: with the
// radio listening all the time (connect()) a camera that advertises is reached well within it,
// and pairing tries again at once on the camera's next advertisement.
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;

// How long the sessions and the link wait for an answer: to the MTU exchange, and to every other
// request of a connection (a discovery, a read, a write).
constexpr uint32_t MTU_TIMEOUT_MS = 3000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;

// Why a session got no link at all, for pairing's quick retries: no answer within
// CONNECT_TIMEOUT_MS, or the connection not established (HCI 0x3e: the camera sent nothing in
// its first connection events). None: a link was made, or the session ended some other way.
enum class LinkFailure : uint8_t { None, NoAnswer, NotEstablished };

// Starts connecting to the camera of the report (its address and type), with the phone's
// connection parameters (camera_link.cpp), the radio listening all the time until it connects.
// Scanning must be off. False if the SoftDevice refused.
bool connect(const camera_list::Report &camera);
bool connected();
// Gives the connection up, whatever its state: one being made is cancelled (if it comes up all the same
// it is closed at once, camera_link.cpp), an open one is closed. True if a Disconnected event follows.
bool close();

// The requests below need a connection; false if the SoftDevice refused.
bool exchangeMtu(uint16_t mtu);
bool findService();
bool findCharacteristics(uint16_t startHandle, uint16_t endHandle);
// Like findCharacteristics, but the CharacteristicsFound events match the older dialect into
// Event::characteristics::olderList instead. For the older-dialect session, which has no service
// to search within and so reads across the whole handle range. Reset by connect().
bool findOlderCharacteristics(uint16_t startHandle, uint16_t endHandle);
bool findDescriptors(uint16_t startHandle, uint16_t endHandle);
bool read(uint16_t handle);
// Write with response. The data must stay unchanged until WriteDone.
bool write(uint16_t handle, const uint8_t *data, uint16_t length);

// Events lost because the queue was full, since start.
uint32_t lostEvents();

// Call from loop(): logs the parameters of each connection (interval, latency, supervision
// timeout) and every change of them, and a connection that came up after it was given up.
void poll();

} // namespace camera_link
