#include "bluetooth.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

#include "camera_link.h"
#include "camera_list.h"
#include "log.h"
#include "lumix.h"
#include "scan_dump.h"

// Sources: Bluefruit52Lib 0.21.0 in the core (bluefruit.cpp, BLEScanner.cpp) and the
// S140 7.3.0 API (ble_gap.h).

namespace {

// Bluefruit.begin(prph_count, central_count) (bluefruit.cpp) sizes the SoftDevice RAM for
// these links and fails if it does not fit below the program's RAM (0x20006000,
// variants/t1000-e/nrf52840_s140_v7.ld).
constexpr uint8_t PERIPHERAL_LINKS = 0; // the card never accepts connections
constexpr uint8_t CENTRAL_LINKS = 1;    // the camera
// Both dialects' 128-bit vendor UUIDs (18), with two spare. The SoftDevice default is 10
// (BLE_UUID_VS_COUNT_DEFAULT); raising it uses a little more SoftDevice RAM (checked at start).
constexpr uint8_t VENDOR_UUID_COUNT = 20;

// Scanning (M3 decision): active scan, the radio listens 50 ms of every 100 ms, until
// stopped; slower in the M6 search modes (setScanTiming()).
constexpr uint16_t SCAN_INTERVAL_MS = 100;
constexpr uint16_t SCAN_WINDOW_MS = 50;
// The SoftDevice accepts a legacy scan interval up to 10.24 s (ble_gap.h, BLE_GAP_SCAN_INTERVAL_MAX).
constexpr uint16_t SCAN_INTERVAL_MAX_MS = 10240;

uint16_t scanIntervalMs = SCAN_INTERVAL_MS;
uint16_t scanWindowMs = SCAN_WINDOW_MS;
constexpr uint16_t SCAN_UNTIL_STOPPED = 0; // BLEScanner::start() timeout: 0 = none

constexpr uint32_t STATUS_PERIOD_MS = 10000;
constexpr uint32_t MS_PER_DECISECOND = 100;

// Transmit power +8 dBm, the most the nRF52840 has (ble_gap.h, sd_ble_gap_tx_power_set(): the
// accepted values go up to +8 dBm), instead of the SoftDevice's default 0 dBm: on the walk of
// 2026-10-24 the connection to the camera was lost when the card was only a few metres away,
// carried by another person. It helps the card -> camera direction only.
//  - Set once for the scanner and initiator role: "the initiator will have the same transmit
//    power as the scanner" and "when a connection is created it will inherit the transmit power
//    from the initiator" (ble_gap.h), so the connection gets it too. Bluefruit.setTxPower() sets
//    only the advertiser's power, which the card does not use (BLEAdvertising.cpp).
//  - Cost (estimate, not measured): without DC/DC, as on this card (docs/HARDWARE.md), the radio
//    draws 32.7 mA instead of 10.6 mA while it transmits (nRF52840 PS v1.1 6.20.15.2), for about
//    0.1-0.2 ms of every 48.75 ms connection event: about 0.05-0.1 mA more, about 1 % of the
//    7-8 mA of a connection.
//  - EU: 2400-2483.5 MHz wideband data transmission allows 100 mW (20 dBm) e.i.r.p., and 10 mW/MHz
//    (10 dBm/MHz) for modulations other than frequency hopping (ERC/REC 70-03 annex 3, ETSI
//    EN 300 328). +8 dBm from the chip stays within both while the antenna gain is at most
//    2 dBi; Seeed does not state the gain of the card's antenna.
constexpr int8_t TX_POWER_DBM = 8;
bool txPowerSet = false;

static_assert(camera_list::ADDRESS_LENGTH == BLE_GAP_ADDR_LEN, "address length");

// Camera reports go from the BLE task to the loop task through this queue (static, no
// heap). The BLE task never waits: a report that does not fit is counted as dropped.
constexpr UBaseType_t QUEUE_LENGTH = 8;
StaticQueue_t queueControl;
uint8_t queueStorage[QUEUE_LENGTH * sizeof(camera_list::Report)];
QueueHandle_t reportQueue = nullptr;

// 'diag scan' (startScanDump): the raw adverts to dump go through their own queue; the loop task
// shows each device once. dumpActive gates the BLE task, so set it last when starting.
constexpr UBaseType_t DUMP_QUEUE_LENGTH = 16;
StaticQueue_t dumpQueueControl;
uint8_t dumpQueueStorage[DUMP_QUEUE_LENGTH * sizeof(scan_dump::Advert)];
QueueHandle_t dumpQueue = nullptr;
volatile bool dumpActive = false; // BLE task reads it; loop task writes it
uint32_t dumpDeadlineMs = 0;      // loop task only
constexpr size_t DUMP_SEEN_MAX = 16;
uint8_t dumpSeen[DUMP_SEEN_MAX][scan_dump::ADDRESS_LENGTH + 1]; // address + the scan-response flag
size_t dumpSeenCount = 0;                                       // loop task only

bool started = false;

// Set by the loop task; the BLE task resumes scanning after a report only while it is set.
volatile bool scanActive = false;

// Written only by the BLE task (aligned 32-bit accesses are atomic on the Cortex-M4).
// The loop task keeps the values from the start of a scan and reports the difference.
volatile uint32_t reportsTotal = 0;
volatile uint32_t otherPanasonicTotal = 0;
volatile uint32_t droppedTotal = 0;

struct Counts {
    uint32_t reports;
    uint32_t otherPanasonic;
    uint32_t dropped;
};
Counts atScanStart = {};

// Loop task only.
camera_list::CameraList cameras;
uint32_t cameraReports = 0;
uint32_t untracked = 0; // reports of cameras beyond MAX_CAMERAS
uint32_t scanStartMs = 0;
uint32_t lastStatusMs = 0;

Counts sinceScanStart()
{
    return Counts{reportsTotal - atScanStart.reports, otherPanasonicTotal - atScanStart.otherPanasonic,
                  droppedTotal - atScanStart.dropped};
}

// Runs in Bluefruit's BLE task for every SoftDevice event, after the library's own
// handlers (bluefruit.cpp, _ble_handler()). It must not log: logger is for the loop task.
void onBleEvent(ble_evt_t *event)
{
    if (event->header.evt_id != BLE_GAP_EVT_ADV_REPORT) {
        camera_link::onBleEvent(event);
        return;
    }
    const ble_gap_evt_adv_report_t &advert = event->evt.gap_evt.params.adv_report;
    reportsTotal++;

    camera_list::Report report;
    switch (lumix::parseAdvert(advert.data.p_data, advert.data.len, &report.camera)) {
    case lumix::Advert::Camera:
        memcpy(report.address, advert.peer_addr.addr, camera_list::ADDRESS_LENGTH);
        report.addressType = advert.peer_addr.addr_type;
        report.rssi = advert.rssi;
        report.scanResponse = advert.type.scan_response;
        if (xQueueSend(reportQueue, &report, 0) != pdTRUE)
            droppedTotal++;
        break;
    case lumix::Advert::OtherPanasonic:
        otherPanasonicTotal++;
        break;
    case lumix::Advert::None:
        break;
    }

    // 'diag scan': keep every close-enough advert, not only cameras, for the loop task to show.
    if (dumpActive && advert.rssi >= scan_dump::RSSI_THRESHOLD_DBM) {
        scan_dump::Advert raw;
        memcpy(raw.address, advert.peer_addr.addr, scan_dump::ADDRESS_LENGTH);
        raw.addressType = advert.peer_addr.addr_type;
        raw.rssi = advert.rssi;
        raw.scanResponse = advert.type.scan_response;
        raw.length = advert.data.len <= scan_dump::MAX_ADVERT_BYTES ? (uint8_t)advert.data.len
                                                                    : (uint8_t)scan_dump::MAX_ADVERT_BYTES;
        memcpy(raw.data, advert.data.p_data, raw.length);
        xQueueSend(dumpQueue, &raw, 0); // full: dropped, the device is likely already shown
    }

    // The SoftDevice pauses scanning after each complete report until it is told to go on
    // (ble_gap.h, sd_ble_gap_scan_start()). BLEScanner only does that itself for reports
    // its filters reject or when a report callback is set (BLEScanner.cpp,
    // _eventHandler()); we use neither, so we resume here.
    if (scanActive && advert.type.status != BLE_GAP_ADV_DATA_STATUS_INCOMPLETE_MORE_DATA)
        Bluefruit.Scanner.resume();
}

void logCamera(const char *what, const camera_list::Report &report)
{
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    char address[3 * camera_list::ADDRESS_LENGTH];
    lumix::formatSerial(report.camera.serial, serial, sizeof(serial));
    camera_list::formatAddress(report.address, address, sizeof(address));
    logger::line("scan: camera %s %s: %s (%02x), %s %s, %d dBm, %s", serial, what,
                 lumix::stateName(report.camera.state), report.camera.state, address,
                 camera_list::addressTypeName(report.addressType), report.rssi,
                 report.scanResponse ? "scan response" : "advert");
}

void logLost(const camera_list::Entry &entry)
{
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    lumix::formatSerial(entry.last.camera.serial, serial, sizeof(serial));
    logger::line("scan: camera %s not seen for %lu s", serial, (unsigned long)(camera_list::LOST_AFTER_MS / 1000));
}

void logStatus(uint32_t nowMs)
{
    const Counts counts = sinceScanStart();
    logger::transient("scan: on %lu s, %lu reports, %lu camera, %lu other Panasonic, %lu dropped, %lu untracked",
                      (unsigned long)((nowMs - scanStartMs) / 1000), (unsigned long)counts.reports,
                      (unsigned long)cameraReports, (unsigned long)counts.otherPanasonic,
                      (unsigned long)counts.dropped, (unsigned long)untracked);
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *entry = cameras.at(i);
        if (entry == nullptr)
            continue;
        char serial[2 * lumix::SERIAL_LENGTH + 1];
        lumix::formatSerial(entry->last.camera.serial, serial, sizeof(serial));
        const uint32_t agoDs = (nowMs - entry->lastSeenMs) / MS_PER_DECISECOND;
        logger::transient("scan: camera %s %s (%02x), %d dBm, %lu advert + %lu scan response, last %lu.%lu s ago",
                          serial, lumix::stateName(entry->last.camera.state), entry->last.camera.state,
                          entry->last.rssi, (unsigned long)entry->advertCount,
                          (unsigned long)entry->scanResponseCount, (unsigned long)(agoDs / 10),
                          (unsigned long)(agoDs % 10));
    }
}

} // namespace

namespace bluetooth {

bool begin()
{
    // Bluefruit blinks LED_BLUE while scanning and lights it while connected. The variant
    // has no such LED (LED_BLUE is -1), so keep the library away from the pins.
    Bluefruit.autoConnLed(false);
    // The camera link needs an MTU above the default 23 for writes of up to 32 bytes; the
    // other values are the SoftDevice defaults Bluefruit uses (bluefruit.cpp).
    Bluefruit.configCentralConn(camera_link::MAX_MTU, BLE_GAP_EVENT_LENGTH_DEFAULT, BLE_GATTS_HVN_TX_QUEUE_SIZE_DEFAULT,
                                BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT);
    // Must come before begin(): it sizes the SoftDevice's vendor-UUID table (camera_link::begin
    // registers 18). Without it the 11th sd_ble_uuid_vs_add() would fail.
    Bluefruit.configUuid128Count(VENDOR_UUID_COUNT);
    if (!Bluefruit.begin(PERIPHERAL_LINKS, CENTRAL_LINKS) || !camera_link::begin())
        return false;
    // The handle is not used for this role. Should it fail, the card works at 0 dBm ('p' says so).
    txPowerSet = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_SCAN_INIT, 0, TX_POWER_DBM) == NRF_SUCCESS;
    reportQueue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(camera_list::Report), queueStorage, &queueControl);
    dumpQueue = xQueueCreateStatic(DUMP_QUEUE_LENGTH, sizeof(scan_dump::Advert), dumpQueueStorage, &dumpQueueControl);
    Bluefruit.setEventCallback(onBleEvent);
    Bluefruit.Scanner.useActiveScan(true);
    Bluefruit.Scanner.setIntervalMS(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
    Bluefruit.Scanner.restartOnDisconnect(false); // we decide when to scan
    started = true;
    return true;
}

void scanOn()
{
    if (!started) {
        logger::line("scan: not possible, the SoftDevice did not start");
        return;
    }
    if (scanActive) {
        logger::line("scan: already on");
        return;
    }
    xQueueReset(reportQueue);
    cameras.clear();
    cameraReports = 0;
    untracked = 0;
    atScanStart = Counts{reportsTotal, otherPanasonicTotal, droppedTotal};
    scanActive = true; // before starting: the first report must already resume the scan
    if (!Bluefruit.Scanner.start(SCAN_UNTIL_STOPPED)) {
        scanActive = false;
        logger::line("scan: could not start");
        return;
    }
    scanStartMs = millis();
    lastStatusMs = scanStartMs;
    logger::line("scan: on - active, radio %u ms of every %u ms, logging LUMIX cameras only", scanWindowMs,
                 scanIntervalMs);
}

bool setScanTiming(uint16_t intervalMs, uint16_t windowMs)
{
    if (!started || intervalMs > SCAN_INTERVAL_MAX_MS || windowMs == 0 || windowMs > intervalMs)
        return false;
    if (intervalMs == scanIntervalMs && windowMs == scanWindowMs)
        return true;
    scanIntervalMs = intervalMs;
    scanWindowMs = windowMs;
    Bluefruit.Scanner.setIntervalMS(scanIntervalMs, scanWindowMs);
    if (!scanActive)
        return true;
    // Restart with the new timing; the camera list and the counters stay.
    scanActive = false; // the BLE task stops resuming
    Bluefruit.Scanner.stop();
    scanActive = true;
    if (!Bluefruit.Scanner.start(SCAN_UNTIL_STOPPED)) {
        scanActive = false;
        logger::line("scan: could not restart with the new timing");
        return false;
    }
    return true;
}

void setNormalScanTiming()
{
    setScanTiming(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
}

void scanOff()
{
    if (!scanActive)
        return;
    scanActive = false; // first, so the BLE task no longer resumes the scan
    // Stopping also works while the scan is paused after a report (ble_gap.h).
    const bool stopped = Bluefruit.Scanner.stop();
    const Counts counts = sinceScanStart();
    logger::line("scan: off after %lu s%s, %lu reports, %lu camera, %lu other Panasonic, %lu dropped, %lu untracked",
                 (unsigned long)((millis() - scanStartMs) / 1000), stopped ? "" : " (stop error)",
                 (unsigned long)counts.reports, (unsigned long)cameraReports, (unsigned long)counts.otherPanasonic,
                 (unsigned long)counts.dropped, (unsigned long)untracked);
}

bool scanning()
{
    return scanActive;
}

bool ready()
{
    return started;
}

const camera_list::Entry *camera(size_t i)
{
    return cameras.at(i);
}

void logState()
{
    if (!started) {
        logger::transient("ble: off, the SoftDevice did not start");
        return;
    }
    logger::transient("ble: SoftDevice on, %s, transmit power %+d dBm, %u vendor UUIDs, %lu camera events lost",
                      scanActive ? "scanning" : "not scanning", txPowerSet ? TX_POWER_DBM : 0,
                      (unsigned)camera_link::registeredUuidCount(), (unsigned long)camera_link::lostEvents());
    if (scanActive)
        logStatus(millis());
}

namespace {

// 'diag scan' shows each device once per advert and once per scan response; this remembers which.
bool seenBefore(const scan_dump::Advert &raw)
{
    uint8_t key[scan_dump::ADDRESS_LENGTH + 1];
    memcpy(key, raw.address, scan_dump::ADDRESS_LENGTH);
    key[scan_dump::ADDRESS_LENGTH] = raw.scanResponse ? 1 : 0;
    for (size_t i = 0; i < dumpSeenCount; i++)
        if (memcmp(dumpSeen[i], key, sizeof(key)) == 0)
            return true;
    if (dumpSeenCount >= DUMP_SEEN_MAX)
        return true; // the list is full: cap the output
    memcpy(dumpSeen[dumpSeenCount++], key, sizeof(key));
    return false;
}

// Loop task: logs the new adverts of a 'diag scan' run and ends the run at its deadline.
void drainScanDump(uint32_t now)
{
    scan_dump::Advert raw;
    char line[200];
    while (xQueueReceive(dumpQueue, &raw, 0) == pdTRUE) {
        if (!dumpActive || seenBefore(raw))
            continue;
        if (scan_dump::formatAdvert(raw, line, sizeof(line)) > 0)
            logger::line("diag scan: %s", line);
    }
    if (dumpActive && (int32_t)(now - dumpDeadlineMs) >= 0) {
        dumpActive = false;
        logger::line("diag scan: done, %u device(s)", (unsigned)dumpSeenCount);
    }
}

} // namespace

void startScanDump()
{
    if (!started) {
        logger::line("diag scan: not possible, the SoftDevice did not start");
        return;
    }
    if (!scanActive)
        scanOn();
    xQueueReset(dumpQueue);
    dumpSeenCount = 0;
    dumpDeadlineMs = millis() + scan_dump::DURATION_MS;
    dumpActive = true; // last: the BLE task may now queue adverts
    logger::transient("diag scan: on %lu s, reports stronger than %d dBm; full bytes for Panasonic",
                      (unsigned long)(scan_dump::DURATION_MS / 1000), (int)scan_dump::RSSI_THRESHOLD_DBM);
}

void poll()
{
    if (!started)
        return;
    const uint32_t now = millis();
    drainScanDump(now);
    camera_list::Report report;
    while (xQueueReceive(reportQueue, &report, 0) == pdTRUE) {
        if (!scanActive)
            continue; // left over from before 'scan off'
        cameraReports++;
        switch (cameras.add(report, now)) {
        case camera_list::Change::New:
            logCamera("seen", report);
            break;
        case camera_list::Change::Changed:
            logCamera("changed", report);
            break;
        case camera_list::Change::TableFull:
            untracked++;
            break;
        case camera_list::Change::None:
            break;
        }
    }
    if (!scanActive)
        return;
    camera_list::Entry lost;
    while (cameras.removeLost(now, &lost))
        logLost(lost);
    if (now - lastStatusMs >= STATUS_PERIOD_MS) {
        lastStatusMs = now;
        logStatus(now);
    }
}

} // namespace bluetooth
