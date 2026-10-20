#include "camera_link.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "lumix.h"

// Sources: S140 7.3.0 API (ble.h, ble_gap.h, ble_gattc.h), Bluefruit52Lib 0.21.0
// (BLECentral.cpp: connect() uses the scanner's parameters and the central link
// configuration set in Bluefruit.begin()).

namespace {

constexpr UBaseType_t QUEUE_LENGTH = 6; // one request at a time: its answer, notifications, a disconnect
StaticQueue_t queueControl;
uint8_t queueStorage[QUEUE_LENGTH * sizeof(camera_link::Event)];
QueueHandle_t eventQueue = nullptr;

volatile uint16_t connHandle = BLE_CONN_HANDLE_INVALID; // written by the BLE task
volatile uint32_t lost = 0;

// Connection details travel from the BLE task to poll() through their own queue, so the
// GATT steps of camera_session and link_control never see them.
enum class InfoType : uint8_t {
    Connected,        // the parameters the connection started with
    CameraRequest,    // the camera asked for other parameters (Bluefruit accepts them)
    ParametersChanged,
    PhyChanged,
};

struct Info {
    InfoType type;
    ble_gap_conn_params_t params;
    uint8_t txPhy;
    uint8_t rxPhy;
};

constexpr UBaseType_t INFO_QUEUE_LENGTH = 4; // a connection, a request, its result, a PHY change
StaticQueue_t infoQueueControl;
uint8_t infoQueueStorage[INFO_QUEUE_LENGTH * sizeof(Info)];
QueueHandle_t infoQueue = nullptr;

// The current details, kept by poll() in the loop task.
ble_gap_conn_params_t currentParams = {};
uint8_t currentTxPhy = BLE_GAP_PHY_1MBPS; // a connection starts on 1M (ble_gap.h, BLE_GAP_PHYS)
uint8_t currentRxPhy = BLE_GAP_PHY_1MBPS;

// Units of ble_gap_conn_params_t (ble_gap.h): the interval in 1.25 ms, the timeout in 10 ms.
constexpr uint32_t INTERVAL_UNIT_US = 1250;
constexpr uint32_t TIMEOUT_UNIT_MS = 10;

constexpr uint16_t FIRST_HANDLE = 0x0001;

// Vendor-specific UUIDs: the SoftDevice keeps the 128-bit base and gives it a type; the
// UUID is then (type, bytes 12-13 of the little-endian form) (ble.h, sd_ble_uuid_vs_add()).
ble_uuid_t serviceUuid = {};
ble_uuid_t characteristicUuids[camera_link::CHARACTERISTIC_COUNT] = {};

const uint8_t *const CHARACTERISTIC_UUIDS[camera_link::CHARACTERISTIC_COUNT] = {
    lumix::KEY_UUID,  lumix::DEVICE_ID_UUID,  lumix::APP_NAME_UUID,  lumix::DEVICE_NAME_UUID,
    lumix::TIME_UUID, lumix::GPS_MODE_UUID, lumix::GPS_NOTIFY_UUID, lumix::LOCATION_UUID};

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

void queueInfo(const Info &info)
{
    if (xQueueSend(infoQueue, &info, 0) != pdTRUE)
        lost++;
}

const char *phyName(uint8_t phy)
{
    switch (phy) {
    case BLE_GAP_PHY_1MBPS:
        return "1M";
    case BLE_GAP_PHY_2MBPS:
        return "2M";
    case BLE_GAP_PHY_CODED:
        return "coded";
    default:
        return "?";
    }
}

constexpr size_t PARAMS_TEXT_MAX = 80;

// "interval 20.00 ms, latency 0, supervision timeout 2000 ms"; an interval range when the
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
    infoQueue = xQueueCreateStatic(INFO_QUEUE_LENGTH, sizeof(Info), infoQueueStorage, &infoQueueControl);
    if (!registerUuid(lumix::SERVICE_UUID, &serviceUuid))
        return false;
    for (size_t i = 0; i < CHARACTERISTIC_COUNT; i++) {
        if (!registerUuid(CHARACTERISTIC_UUIDS[i], &characteristicUuids[i]))
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
        Info info = {};
        info.type = InfoType::Connected;
        info.params = event->evt.gap_evt.params.connected.conn_params;
        queueInfo(info);
        // Signal strength on every received packet, read with sd_ble_gap_rssi_get(); no events.
        sd_ble_gap_rssi_start(eventConn, BLE_GAP_RSSI_THRESHOLD_INVALID, 0);
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
    case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST: {
        Info info = {};
        info.type = InfoType::CameraRequest;
        info.params = event->evt.gap_evt.params.conn_param_update_request.conn_params;
        queueInfo(info);
        return;
    }
    case BLE_GAP_EVT_CONN_PARAM_UPDATE: {
        Info info = {};
        info.type = InfoType::ParametersChanged;
        info.params = event->evt.gap_evt.params.conn_param_update.conn_params;
        queueInfo(info);
        return;
    }
    case BLE_GAP_EVT_PHY_UPDATE: {
        Info info = {};
        info.type = InfoType::PhyChanged;
        info.txPhy = event->evt.gap_evt.params.phy_update.tx_phy;
        info.rxPhy = event->evt.gap_evt.params.phy_update.rx_phy;
        queueInfo(info);
        return;
    }
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
    if (infoQueue == nullptr)
        return;
    Info info;
    char text[PARAMS_TEXT_MAX];
    while (xQueueReceive(infoQueue, &info, 0) == pdTRUE) {
        switch (info.type) {
        case InfoType::Connected:
            currentParams = info.params;
            currentTxPhy = currentRxPhy = BLE_GAP_PHY_1MBPS;
            formatParams(info.params, text, sizeof(text));
            logger::transient("camera: connection %s", text);
            break;
        case InfoType::CameraRequest:
            formatParams(info.params, text, sizeof(text));
            logger::line("camera: asks for %s", text);
            break;
        case InfoType::ParametersChanged:
            currentParams = info.params;
            formatParams(info.params, text, sizeof(text));
            logger::line("camera: connection changed to %s", text);
            break;
        case InfoType::PhyChanged:
            currentTxPhy = info.txPhy;
            currentRxPhy = info.rxPhy;
            logger::line("camera: PHY changed to tx %s, rx %s", phyName(info.txPhy), phyName(info.rxPhy));
            break;
        }
    }
}

void logState()
{
    if (!connected())
        return;
    char text[PARAMS_TEXT_MAX];
    formatParams(currentParams, text, sizeof(text));
    int8_t dbm = 0;
    if (rssi(&dbm))
        logger::transient("camera: %s, PHY %s/%s, %d dBm", text, phyName(currentTxPhy), phyName(currentRxPhy), dbm);
    else
        logger::transient("camera: %s, PHY %s/%s", text, phyName(currentTxPhy), phyName(currentRxPhy));
}

bool rssi(int8_t *dbm)
{
    const uint16_t handle = connHandle;
    uint8_t channel = 0;
    return handle != BLE_CONN_HANDLE_INVALID && sd_ble_gap_rssi_get(handle, dbm, &channel) == NRF_SUCCESS;
}

} // namespace camera_link
