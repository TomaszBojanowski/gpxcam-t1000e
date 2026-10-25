#include "camera_link.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "lumix.h"

// Sources: S140 7.3.0 API (ble.h, ble_gap.h, ble_gattc.h), Bluefruit52Lib 0.21.0
// (BLECentral.cpp: connect() uses the scanner's parameters and the central link
// configuration set in Bluefruit.begin(); it cannot set the supervision timeout, so the
// card connects with sd_ble_gap_connect() itself, with the same scan parameters).

namespace {

constexpr UBaseType_t QUEUE_LENGTH = 6; // one request at a time: its answer, notifications, a disconnect
StaticQueue_t queueControl;
uint8_t queueStorage[QUEUE_LENGTH * sizeof(camera_link::Event)];
QueueHandle_t eventQueue = nullptr;

volatile uint16_t connHandle = BLE_CONN_HANDLE_INVALID; // written by the BLE task
volatile uint32_t lost = 0;

// The connection as Android 15 (gpxcam app) sets it up with the S5II, read from the phone's
// HCI log (docs/PROTOCOL.md): interval 48.75 ms, latency 0, supervision timeout 5 s, and
// packets of 61 bytes (the camera's MTU 57 plus the 4-byte L2CAP header). With Bluefruit's
// defaults (20 ms, 2 s, 27-byte packets) the camera's Bluetooth dropped or hung after hours.
constexpr uint16_t CONN_INTERVAL = 39;        // in 1.25 ms: 48.75 ms
constexpr uint16_t SLAVE_LATENCY = 0;
constexpr uint16_t SUPERVISION_TIMEOUT = 500; // in 10 ms: 5 s
constexpr uint16_t DATA_LENGTH = 61;          // bytes per packet, both ways

// Connection parameters travel from the BLE task to poll() through their own queue, so the
// GATT steps of camera_session and link_control never see them.
enum class ParamsEvent : uint8_t {
    Connected,     // the parameters the connection started with
    CameraRequest, // the camera asked for others (Bluefruit accepts them, BLECentral.cpp)
    Changed,
};

struct ParamsInfo {
    ParamsEvent type;
    ble_gap_conn_params_t params;
};

constexpr UBaseType_t PARAMS_QUEUE_LENGTH = 4;
StaticQueue_t paramsQueueControl;
uint8_t paramsQueueStorage[PARAMS_QUEUE_LENGTH * sizeof(ParamsInfo)];
QueueHandle_t paramsQueue = nullptr;

// Units of ble_gap_conn_params_t (ble_gap.h): the interval in 1.25 ms, the timeout in 10 ms.
constexpr uint32_t INTERVAL_UNIT_US = 1250;
constexpr uint32_t TIMEOUT_UNIT_MS = 10;
constexpr size_t PARAMS_TEXT_MAX = 80;

constexpr uint16_t FIRST_HANDLE = 0x0001;

// Vendor-specific UUIDs: the SoftDevice keeps the 128-bit base and gives it a type; the
// UUID is then (type, bytes 12-13 of the little-endian form) (ble.h, sd_ble_uuid_vs_add()).
ble_uuid_t serviceUuid = {};
ble_uuid_t characteristicUuids[camera_link::CHARACTERISTIC_COUNT] = {};

const uint8_t *const CHARACTERISTIC_UUIDS[camera_link::CHARACTERISTIC_COUNT] = {
    lumix::KEY_UUID,  lumix::DEVICE_ID_UUID,  lumix::APP_NAME_UUID,  lumix::DEVICE_NAME_UUID,
    lumix::TIME_UUID, lumix::GPS_MODE_UUID, lumix::GPS_NOTIFY_UUID, lumix::LOCATION_UUID};

// The older dialect's characteristics, registered so the card can speak it too (no service UUID:
// found by reading across the handle range). Not used until the older-dialect session is wired in.
ble_uuid_t olderCharacteristicUuids[lumix::OLDER_CHARACTERISTIC_COUNT] = {};
const uint8_t *const OLDER_CHARACTERISTIC_UUIDS[lumix::OLDER_CHARACTERISTIC_COUNT] = {
    lumix::OLDER_KEY_UUID,         lumix::OLDER_DEVICE_ID_UUID,   lumix::OLDER_DEVICE_NAME_UUID,
    lumix::OLDER_CAMERA_NAME_UUID, lumix::OLDER_CAMERA_TEXT_UUID, lumix::OLDER_TIME_UUID,
    lumix::OLDER_TIME_NOTIFY_UUID, lumix::OLDER_LOCATION_UUID,    lumix::OLDER_LOCATION_NOTIFY_UUID};

uint8_t registeredCount = 0; // vendor UUIDs begin() registered

bool registerUuid(const uint8_t bigEndian[lumix::UUID_LENGTH], ble_uuid_t *out)
{
    ble_uuid128_t base;
    for (size_t i = 0; i < lumix::UUID_LENGTH; i++)
        base.uuid128[i] = bigEndian[lumix::UUID_LENGTH - 1 - i];
    uint8_t type = 0;
    if (sd_ble_uuid_vs_add(&base, &type) != NRF_SUCCESS)
        return false;
    out->type = type;
    out->uuid = (uint16_t)(base.uuid128[13] << 8 | base.uuid128[12]);
    return true;
}

void queueEvent(const camera_link::Event &event)
{
    if (xQueueSend(eventQueue, &event, 0) != pdTRUE)
        lost++;
}

void addCharacteristics(const ble_gattc_evt_char_disc_rsp_t &response, camera_link::Event *out)
{
    for (uint16_t i = 0; i < response.count; i++) {
        const ble_gattc_char_t &found = response.chars[i];
        out->characteristics.lastHandle = found.handle_value;
        for (uint8_t which = 0; which < camera_link::CHARACTERISTIC_COUNT; which++) {
            const ble_uuid_t &wanted = characteristicUuids[which];
            if (found.uuid.type != wanted.type || found.uuid.uuid != wanted.uuid ||
                out->characteristics.count >= camera_link::CHARACTERISTIC_COUNT)
                continue;
            camera_link::FoundCharacteristic &entry = out->characteristics.list[out->characteristics.count++];
            entry.which = (camera_link::Characteristic)which;
            entry.properties = (found.char_props.read ? camera_link::PROPERTY_READ : 0) |
                               (found.char_props.write ? camera_link::PROPERTY_WRITE : 0) |
                               (found.char_props.notify ? camera_link::PROPERTY_NOTIFY : 0) |
                               (found.char_props.indicate ? camera_link::PROPERTY_INDICATE : 0);
            entry.valueHandle = found.handle_value;
        }
    }
}

// Descriptors follow the value of their characteristic until the next characteristic
// declaration (Bluetooth Core Vol 3, Part G, 3.3).
void addDescriptors(const ble_gattc_evt_desc_disc_rsp_t &response, camera_link::Event *out)
{
    for (uint16_t i = 0; i < response.count; i++) {
        const ble_gattc_desc_t &found = response.descs[i];
        out->descriptors.lastHandle = found.handle;
        if (found.uuid.type != BLE_UUID_TYPE_BLE)
            continue;
        if (found.uuid.uuid == BLE_UUID_CHARACTERISTIC) {
            out->descriptors.nextCharacteristic = true;
            return;
        }
        if (found.uuid.uuid == BLE_UUID_DESCRIPTOR_CLIENT_CHAR_CONFIG && out->descriptors.cccdHandle == 0)
            out->descriptors.cccdHandle = found.handle;
    }
}

void queueParams(ParamsEvent type, const ble_gap_conn_params_t &params)
{
    const ParamsInfo info = {type, params};
    if (xQueueSend(paramsQueue, &info, 0) != pdTRUE)
        lost++;
}

// "interval 48.75 ms, latency 0, supervision timeout 5000 ms"; an interval range when the
// minimum and maximum differ (a camera request).
void formatParams(const ble_gap_conn_params_t &params, char *out, size_t size)
{
    const uint32_t minUs = params.min_conn_interval * INTERVAL_UNIT_US;
    const uint32_t maxUs = params.max_conn_interval * INTERVAL_UNIT_US;
    int length = snprintf(out, size, "interval %lu.%02lu", (unsigned long)(minUs / 1000),
                          (unsigned long)(minUs % 1000 / 10));
    if (maxUs != minUs && length > 0 && (size_t)length < size)
        length += snprintf(out + length, size - length, "-%lu.%02lu", (unsigned long)(maxUs / 1000),
                           (unsigned long)(maxUs % 1000 / 10));
    if (length > 0 && (size_t)length < size)
        snprintf(out + length, size - length, " ms, latency %u, supervision timeout %lu ms", params.slave_latency,
                 (unsigned long)(params.conn_sup_timeout * TIMEOUT_UNIT_MS));
}

} // namespace

namespace camera_link {

bool begin()
{
    eventQueue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(Event), queueStorage, &queueControl);
    paramsQueue = xQueueCreateStatic(PARAMS_QUEUE_LENGTH, sizeof(ParamsInfo), paramsQueueStorage, &paramsQueueControl);
    registeredCount = 0;
    if (!registerUuid(lumix::SERVICE_UUID, &serviceUuid))
        return false;
    registeredCount++;
    for (size_t i = 0; i < CHARACTERISTIC_COUNT; i++) {
        if (!registerUuid(CHARACTERISTIC_UUIDS[i], &characteristicUuids[i]))
            return false;
        registeredCount++;
    }
    for (size_t i = 0; i < lumix::OLDER_CHARACTERISTIC_COUNT; i++) {
        if (!registerUuid(OLDER_CHARACTERISTIC_UUIDS[i], &olderCharacteristicUuids[i]))
            return false;
        registeredCount++;
    }
    return true;
}

uint8_t registeredUuidCount()
{
    return registeredCount;
}

void onBleEvent(const ble_evt_t *event)
{
    if (eventQueue == nullptr)
        return;
    const uint16_t id = event->header.evt_id;
    const uint16_t eventConn = event->evt.common_evt.conn_handle; // same offset for all events
    Event out = {};
    if (id == BLE_GAP_EVT_CONNECTED) {
        if (event->evt.gap_evt.params.connected.role != BLE_GAP_ROLE_CENTRAL)
            return;
        connHandle = eventConn;
        out.type = EventType::Connected;
        queueEvent(out);
        queueParams(ParamsEvent::Connected, event->evt.gap_evt.params.connected.conn_params);
        // Longer packets, as the phone asks for them; the times are left to the SoftDevice
        // (ble_gap.h, BLE_GAP_DATA_LENGTH_AUTO). If it refuses, the link works with 27 bytes.
        const ble_gap_data_length_params_t dataLength = {DATA_LENGTH, DATA_LENGTH, BLE_GAP_DATA_LENGTH_AUTO,
                                                         BLE_GAP_DATA_LENGTH_AUTO};
        sd_ble_gap_data_length_update(eventConn, &dataLength, nullptr);
        return;
    }
    if (connHandle == BLE_CONN_HANDLE_INVALID || eventConn != connHandle)
        return;
    const ble_gattc_evt_t &gattc = event->evt.gattc_evt;
    switch (id) {
    case BLE_GAP_EVT_DISCONNECTED:
        connHandle = BLE_CONN_HANDLE_INVALID;
        out.type = EventType::Disconnected;
        out.status = event->evt.gap_evt.params.disconnected.reason;
        queueEvent(out);
        return;
    case BLE_GAP_EVT_SEC_REQUEST:
        out.type = EventType::SecurityRequest;
        queueEvent(out);
        return;
    case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
        queueParams(ParamsEvent::CameraRequest, event->evt.gap_evt.params.conn_param_update_request.conn_params);
        return;
    case BLE_GAP_EVT_CONN_PARAM_UPDATE:
        queueParams(ParamsEvent::Changed, event->evt.gap_evt.params.conn_param_update.conn_params);
        return;
    default:
        break;
    }
    out.status = gattc.gatt_status; // the GATT client events below
    switch (id) {
    case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:
        out.type = EventType::MtuExchanged;
        out.mtu = gattc.params.exchange_mtu_rsp.server_rx_mtu;
        break;
    case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:
        out.type = EventType::ServiceFound;
        if (gattc.gatt_status == BLE_GATT_STATUS_SUCCESS && gattc.params.prim_srvc_disc_rsp.count > 0) {
            out.service.found = true;
            out.service.startHandle = gattc.params.prim_srvc_disc_rsp.services[0].handle_range.start_handle;
            out.service.endHandle = gattc.params.prim_srvc_disc_rsp.services[0].handle_range.end_handle;
        }
        break;
    case BLE_GATTC_EVT_CHAR_DISC_RSP:
        out.type = EventType::CharacteristicsFound;
        if (gattc.gatt_status == BLE_GATT_STATUS_SUCCESS)
            addCharacteristics(gattc.params.char_disc_rsp, &out);
        break;
    case BLE_GATTC_EVT_DESC_DISC_RSP:
        out.type = EventType::DescriptorsFound;
        if (gattc.gatt_status == BLE_GATT_STATUS_SUCCESS)
            addDescriptors(gattc.params.desc_disc_rsp, &out);
        break;
    case BLE_GATTC_EVT_HVX:
        // An indication must be confirmed before the camera can send the next one.
        if (gattc.params.hvx.type == BLE_GATT_HVX_INDICATION)
            sd_ble_gattc_hv_confirm(connHandle, gattc.params.hvx.handle);
        out.type = EventType::Notified;
        out.read.handle = gattc.params.hvx.handle;
        out.read.length = (uint8_t)min((uint16_t)MAX_READ_LENGTH, gattc.params.hvx.len);
        memcpy(out.read.data, gattc.params.hvx.data, out.read.length);
        break;
    case BLE_GATTC_EVT_READ_RSP:
        out.type = EventType::ReadDone;
        out.read.handle = gattc.params.read_rsp.handle;
        out.read.length = (uint8_t)min((uint16_t)MAX_READ_LENGTH, gattc.params.read_rsp.len);
        memcpy(out.read.data, gattc.params.read_rsp.data, out.read.length);
        break;
    case BLE_GATTC_EVT_WRITE_RSP:
        out.type = EventType::WriteDone;
        break;
    case BLE_GATTC_EVT_TIMEOUT:
        out.type = EventType::GattTimeout;
        break;
    default:
        return;
    }
    queueEvent(out);
}

bool nextEvent(Event *event)
{
    return eventQueue != nullptr && xQueueReceive(eventQueue, event, 0) == pdTRUE;
}

bool connect(const camera_list::Report &camera)
{
    if (connHandle != BLE_CONN_HANDLE_INVALID)
        return false;
    xQueueReset(eventQueue); // answers from an earlier connection
    ble_gap_addr_t address = {};
    address.addr_type = camera.addressType;
    memcpy(address.addr, camera.address, camera_list::ADDRESS_LENGTH);
    const ble_gap_conn_params_t params = {CONN_INTERVAL, CONN_INTERVAL, SLAVE_LATENCY, SUPERVISION_TIMEOUT};
    return sd_ble_gap_connect(&address, Bluefruit.Scanner.getParams(), &params, CONN_CFG_CENTRAL) == NRF_SUCCESS;
}

void cancelConnect()
{
    sd_ble_gap_connect_cancel(); // NRF_ERROR_INVALID_STATE when no connection is being made
}

bool connected()
{
    return connHandle != BLE_CONN_HANDLE_INVALID;
}

bool exchangeMtu(uint16_t mtu)
{
    return sd_ble_gattc_exchange_mtu_request(connHandle, mtu) == NRF_SUCCESS;
}

bool findService()
{
    return sd_ble_gattc_primary_services_discover(connHandle, FIRST_HANDLE, &serviceUuid) == NRF_SUCCESS;
}

bool findCharacteristics(uint16_t startHandle, uint16_t endHandle)
{
    const ble_gattc_handle_range_t range = {startHandle, endHandle};
    return sd_ble_gattc_characteristics_discover(connHandle, &range) == NRF_SUCCESS;
}

bool findDescriptors(uint16_t startHandle, uint16_t endHandle)
{
    const ble_gattc_handle_range_t range = {startHandle, endHandle};
    return sd_ble_gattc_descriptors_discover(connHandle, &range) == NRF_SUCCESS;
}

bool read(uint16_t handle)
{
    return sd_ble_gattc_read(connHandle, handle, 0) == NRF_SUCCESS;
}

bool write(uint16_t handle, const uint8_t *data, uint16_t length)
{
    ble_gattc_write_params_t params = {};
    params.write_op = BLE_GATT_OP_WRITE_REQ;
    params.flags = 0;
    params.handle = handle;
    params.offset = 0;
    params.len = length;
    params.p_value = data;
    return sd_ble_gattc_write(connHandle, &params) == NRF_SUCCESS;
}

bool disconnect()
{
    return sd_ble_gap_disconnect(connHandle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION) == NRF_SUCCESS;
}

uint32_t lostEvents()
{
    return lost;
}

void poll()
{
    if (paramsQueue == nullptr)
        return;
    ParamsInfo info;
    char text[PARAMS_TEXT_MAX];
    while (xQueueReceive(paramsQueue, &info, 0) == pdTRUE) {
        formatParams(info.params, text, sizeof(text));
        switch (info.type) {
        case ParamsEvent::Connected:
            logger::line("camera: connection %s", text);
            break;
        case ParamsEvent::CameraRequest:
            logger::line("camera: asks for %s", text);
            break;
        case ParamsEvent::Changed:
            logger::line("camera: connection changed to %s", text);
            break;
        }
    }
}

} // namespace camera_link
