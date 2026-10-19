#include "camera_link.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

#include "lumix.h"

// Sources: S140 7.3.0 API (ble.h, ble_gap.h, ble_gattc.h), Bluefruit52Lib 0.21.0
// (BLECentral.cpp: connect() uses the scanner's parameters and the central link
// configuration set in Bluefruit.begin()).

namespace {

constexpr UBaseType_t QUEUE_LENGTH = 4; // one request at a time: one answer, maybe a disconnect
StaticQueue_t queueControl;
uint8_t queueStorage[QUEUE_LENGTH * sizeof(camera_link::Event)];
QueueHandle_t eventQueue = nullptr;

volatile uint16_t connHandle = BLE_CONN_HANDLE_INVALID; // written by the BLE task
volatile uint32_t lost = 0;

constexpr uint16_t FIRST_HANDLE = 0x0001;

// Vendor-specific UUIDs: the SoftDevice keeps the 128-bit base and gives it a type; the
// UUID is then (type, bytes 12-13 of the little-endian form) (ble.h, sd_ble_uuid_vs_add()).
ble_uuid_t serviceUuid = {};
ble_uuid_t characteristicUuids[camera_link::CHARACTERISTIC_COUNT] = {};

const uint8_t *const CHARACTERISTIC_TEXT_ORDER[camera_link::CHARACTERISTIC_COUNT] = {
    lumix::KEY_UUID, lumix::DEVICE_ID_UUID, lumix::APP_NAME_UUID, lumix::DEVICE_NAME_UUID, lumix::TIME_UUID};

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
                               (found.char_props.write ? camera_link::PROPERTY_WRITE : 0);
            entry.valueHandle = found.handle_value;
        }
    }
}

} // namespace

namespace camera_link {

bool begin()
{
    eventQueue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(Event), queueStorage, &queueControl);
    if (!registerUuid(lumix::SERVICE_UUID, &serviceUuid))
        return false;
    for (size_t i = 0; i < CHARACTERISTIC_COUNT; i++) {
        if (!registerUuid(CHARACTERISTIC_TEXT_ORDER[i], &characteristicUuids[i]))
            return false;
    }
    return true;
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
    return Bluefruit.Central.connect(&address);
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

} // namespace camera_link
