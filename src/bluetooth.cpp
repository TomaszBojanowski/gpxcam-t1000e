#include "bluetooth.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

#include "advert_kind.h"
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

// Scan watchdog: a scan that is on but has had no report for this long is stopped and started
// again, in every mode (pairing, 'diag scan', the link's search). After each report the SoftDevice
// pauses the scan until it is resumed (ble_gap.h, sd_ble_gap_scan_start()); a tester's card got
// 1-3 reports in 30 s with a phone and a camera next to it (1.5.0-beta.2).
constexpr uint32_t SCAN_SILENCE_MS = 5000;

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
volatile uint32_t dumpReports = 0; // every report of a run, weak ones and scan responses too (BLE task)
// A run listens at the normal timing; a timing the link asks for meanwhile is used when it ends.
uint16_t afterDumpIntervalMs = SCAN_INTERVAL_MS; // loop task only
uint16_t afterDumpWindowMs = SCAN_WINDOW_MS;
bool dumpStartedScan = false; // the run switched the scan on, so it switches it off at its end
uint32_t dumpResumeFailuresAtStart = 0; // loop task only: the totals when the run started
uint32_t dumpRestartsAtStart = 0;

bool started = false;

// Set by the loop task; the BLE task resumes scanning after a report only while it is set.
volatile bool scanActive = false;

// Written only by the BLE task (aligned 32-bit accesses are atomic on the Cortex-M4).
// The loop task keeps the values from the start of a scan and reports the difference.
volatile uint32_t reportsTotal = 0;
volatile uint32_t otherPanasonicTotal = 0;
volatile uint32_t resumeFailuresTotal = 0; // Scanner.resume() failed while the scan was on
volatile uint32_t lastReportMs = 0;
volatile uint32_t droppedTotal = 0;

struct Counts {
    uint32_t reports;
    uint32_t otherPanasonic;
    uint32_t dropped;
    uint32_t resumeFailures;
};
Counts atScanStart = {};

// Loop task only.
camera_list::CameraList cameras;
uint32_t cameraReports = 0;
uint32_t untracked = 0; // reports of cameras beyond MAX_CAMERAS
uint32_t scanStartMs = 0;
uint32_t lastStatusMs = 0;
uint32_t scanWatchStartMs = 0; // the scan watchdog: when the scan was last (re)started
uint32_t restarts = 0;         // by the watchdog, in the current scan
uint32_t restartsTotal = 0;
bool restartLogged = false; // one line per silence: where no device advertises at all, it restarts every 5 s

Counts sinceScanStart()
{
    return Counts{reportsTotal - atScanStart.reports, otherPanasonicTotal - atScanStart.otherPanasonic,
                  droppedTotal - atScanStart.dropped, resumeFailuresTotal - atScanStart.resumeFailures};
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
    lastReportMs = millis();
    const advert_kind::Kind kind =
        advert_kind::fromReport(advert.type.connectable, advert.type.scannable, advert.type.scan_response);

    camera_list::Report report;
    switch (lumix::parseAdvert(advert.data.p_data, advert.data.len, &report.camera)) {
    case lumix::Advert::Camera:
        memcpy(report.address, advert.peer_addr.addr, camera_list::ADDRESS_LENGTH);
        report.addressType = advert.peer_addr.addr_type;
        report.rssi = advert.rssi;
        report.kind = kind;
        if (xQueueSend(reportQueue, &report, 0) != pdTRUE)
            droppedTotal++;
        break;
    case lumix::Advert::OtherPanasonic:
        otherPanasonicTotal++;
        break;
    case lumix::Advert::None:
        break;
    }

    // 'diag scan': count every report, and keep every close-enough one, not only cameras, for the
    // loop task to show.
    if (dumpActive) {
        dumpReports++;
        if (advert.rssi >= scan_dump::RSSI_THRESHOLD_DBM) {
            scan_dump::Advert raw;
            memcpy(raw.address, advert.peer_addr.addr, scan_dump::ADDRESS_LENGTH);
            raw.addressType = advert.peer_addr.addr_type;
            raw.rssi = advert.rssi;
            raw.kind = kind;
            raw.length = advert.data.len <= scan_dump::MAX_ADVERT_BYTES ? (uint8_t)advert.data.len
                                                                        : (uint8_t)scan_dump::MAX_ADVERT_BYTES;
            memcpy(raw.data, advert.data.p_data, raw.length);
            xQueueSend(dumpQueue, &raw, 0); // full: dropped, the device is likely already shown
        }
    }

    // The SoftDevice pauses scanning after each complete report until it is told to go on
    // (ble_gap.h, sd_ble_gap_scan_start()). BLEScanner only does that itself for reports
    // its filters reject or when a report callback is set (BLEScanner.cpp,
    // _eventHandler()); we use neither, so we resume here. A failure while the scan is still on
    // (not stopped meanwhile by the loop task) leaves it paused: counted, and the watchdog restarts it.
    if (scanActive && advert.type.status != BLE_GAP_ADV_DATA_STATUS_INCOMPLETE_MORE_DATA &&
        !Bluefruit.Scanner.resume() && scanActive)
        resumeFailuresTotal++;
}

void logCamera(const char *what, const camera_list::Report &report)
{
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    char address[3 * camera_list::ADDRESS_LENGTH];
    lumix::formatSerial(report.camera.serial, serial, sizeof(serial));
    camera_list::formatAddress(report.address, address, sizeof(address));
    const bool response = report.kind == advert_kind::Kind::ScanResponse;
    logger::line("scan: camera %s %s: %s (%02x), %s %s, %d dBm, %s%s", serial, what,
                 lumix::stateName(report.camera.state), report.camera.state, address,
                 camera_list::addressTypeName(report.addressType), report.rssi, advert_kind::name(report.kind),
                 response ? "" : " advert");
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
    logger::transient("scan: on %lu s, %lu reports, %lu camera, %lu other Panasonic, %lu dropped, %lu untracked, "
                      "%lu resume failed, %lu restarts",
                      (unsigned long)((nowMs - scanStartMs) / 1000), (unsigned long)counts.reports,
                      (unsigned long)cameraReports, (unsigned long)counts.otherPanasonic,
                      (unsigned long)counts.dropped, (unsigned long)untracked, (unsigned long)counts.resumeFailures,
                      (unsigned long)restarts);
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *entry = cameras.at(i);
        if (entry == nullptr)
            continue;
        char serial[2 * lumix::SERIAL_LENGTH + 1];
        lumix::formatSerial(entry->last.camera.serial, serial, sizeof(serial));
        const uint32_t agoDs = (nowMs - entry->lastSeenMs) / MS_PER_DECISECOND;
        logger::transient("scan: camera %s %s (%02x), %d dBm, %lu advert (%lu connectable) + %lu scan response, "
                          "last %lu.%lu s ago",
                          serial, lumix::stateName(entry->last.camera.state), entry->last.camera.state,
                          entry->last.rssi, (unsigned long)entry->advertCount, (unsigned long)entry->connectableCount,
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
    atScanStart = Counts{reportsTotal, otherPanasonicTotal, droppedTotal, resumeFailuresTotal};
    restarts = 0;
    restartLogged = false;
    scanActive = true; // before starting: the first report must already resume the scan
    if (!Bluefruit.Scanner.start(SCAN_UNTIL_STOPPED)) {
        scanActive = false;
        logger::line("scan: could not start");
        return;
    }
    scanStartMs = millis();
    lastStatusMs = scanStartMs;
    scanWatchStartMs = scanStartMs;
    logger::line("scan: on - active, radio %u ms of every %u ms, logging LUMIX cameras only", scanWindowMs,
                 scanIntervalMs);
}

bool setScanTiming(uint16_t intervalMs, uint16_t windowMs)
{
    if (!started || intervalMs > SCAN_INTERVAL_MAX_MS || windowMs == 0 || windowMs > intervalMs)
        return false;
    if (dumpActive) { // 'diag scan' keeps the normal timing; this one is used when the run ends
        afterDumpIntervalMs = intervalMs;
        afterDumpWindowMs = windowMs;
        return true;
    }
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
    scanWatchStartMs = millis();
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
    key[scan_dump::ADDRESS_LENGTH] = raw.kind == advert_kind::Kind::ScanResponse ? 1 : 0;
    for (size_t i = 0; i < dumpSeenCount; i++)
        if (memcmp(dumpSeen[i], key, sizeof(key)) == 0)
            return true;
    if (dumpSeenCount >= DUMP_SEEN_MAX)
        return true; // the list is full: cap the output
    memcpy(dumpSeen[dumpSeenCount++], key, sizeof(key));
    return false;
}

// Loop task: the scan watchdog. A scan with no report for SCAN_SILENCE_MS since it was last (re)started
// is stopped and started again with the same timing; the camera list and the counters stay.
void watchScan()
{
    const uint32_t lastReport = lastReportMs; // first: a report after this read is not older than now
    const uint32_t now = millis();
    const bool reportSinceStart = (int32_t)(lastReport - scanWatchStartMs) >= 0;
    if (reportSinceStart)
        restartLogged = false; // the next silence gets its line again
    if ((int32_t)(now - (reportSinceStart ? lastReport : scanWatchStartMs)) < (int32_t)SCAN_SILENCE_MS)
        return;
    scanActive = false; // the BLE task stops resuming
    Bluefruit.Scanner.stop();
    scanActive = true;
    scanWatchStartMs = now;
    restarts++;
    restartsTotal++;
    if (!Bluefruit.Scanner.start(SCAN_UNTIL_STOPPED)) {
        scanActive = false; // the link and pairing switch it on again
        logger::line("scan: could not restart after %lu s without reports", (unsigned long)(SCAN_SILENCE_MS / 1000));
        return;
    }
    if (!restartLogged) {
        restartLogged = true;
        logger::line("scan: restarted (no reports for %lu s)", (unsigned long)(SCAN_SILENCE_MS / 1000));
    }
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
        logger::line("diag scan: done, %u device(s) shown, %lu reports received, %lu resume failed, %lu restarts",
                     (unsigned)dumpSeenCount, (unsigned long)dumpReports,
                     (unsigned long)(resumeFailuresTotal - dumpResumeFailuresAtStart),
                     (unsigned long)(restartsTotal - dumpRestartsAtStart));
        if (dumpStartedScan)
            scanOff(); // as before the run; the link and pairing switch it on again when they need it
        setScanTiming(afterDumpIntervalMs, afterDumpWindowMs);
    }
}

} // namespace

void startScanDump()
{
    if (!started) {
        logger::line("diag scan: not possible, the SoftDevice did not start");
        return;
    }
    if (dumpActive) {
        logger::line("diag scan: already running");
        return;
    }
    // Listen at the normal timing whatever the link's search mode; its timing is kept for after the run.
    afterDumpIntervalMs = scanIntervalMs;
    afterDumpWindowMs = scanWindowMs;
    setNormalScanTiming();
    dumpStartedScan = !scanActive;
    if (!scanActive)
        scanOn();
    xQueueReset(dumpQueue);
    dumpSeenCount = 0;
    dumpReports = 0;
    dumpResumeFailuresAtStart = resumeFailuresTotal;
    dumpRestartsAtStart = restartsTotal;
    dumpDeadlineMs = millis() + scan_dump::DURATION_MS;
    dumpActive = true; // last: the BLE task may now queue adverts
    logger::transient("diag scan: on %lu s, radio %u ms of every %u ms, reports stronger than %d dBm; full bytes "
                      "for Panasonic",
                      (unsigned long)(scan_dump::DURATION_MS / 1000), scanWindowMs, scanIntervalMs,
                      (int)scan_dump::RSSI_THRESHOLD_DBM);
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
    watchScan();
    camera_list::Entry lost;
    while (cameras.removeLost(now, &lost))
        logLost(lost);
    if (now - lastStatusMs >= STATUS_PERIOD_MS) {
        lastStatusMs = now;
        logStatus(now);
    }
}

} // namespace bluetooth
