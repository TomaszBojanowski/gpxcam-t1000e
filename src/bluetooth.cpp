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

// Scanning: active scan, the radio listens 50 ms of every 100 ms, until stopped; slower in the
// link's search modes (setScanTiming()).
constexpr uint16_t SCAN_INTERVAL_MS = 100;
constexpr uint16_t SCAN_WINDOW_MS = 50;
// The SoftDevice accepts a legacy scan interval up to 10.24 s (ble_gap.h, BLE_GAP_SCAN_INTERVAL_MAX).
constexpr uint16_t SCAN_INTERVAL_MAX_MS = 10240;

uint16_t scanIntervalMs = SCAN_INTERVAL_MS;
uint16_t scanWindowMs = SCAN_WINDOW_MS;
constexpr uint16_t SCAN_UNTIL_STOPPED = 0; // BLEScanner::start() timeout: 0 = none

constexpr uint32_t STATUS_PERIOD_MS = 10000;
constexpr uint32_t MS_PER_DECISECOND = 100;

// Scan watchdog. After each report the SoftDevice pauses the scan until it is resumed (ble_gap.h,
// sd_ble_gap_scan_start()); a tester's card got 1-3 reports in 30 s with a phone and a camera next
// to it.
//  - At the normal timing (pairing, 'diag scan', the link's normal search) a scan that is on but has
//    had no report for SCAN_SILENCE_MS is stopped and started again.
//  - At the slower timings of the link's search modes (100 ms every 1, 2 or 10 s) a silence of 5 s
//    is normal (with 10 s between windows a 5 s watchdog would restart the scan before each of
//    them), so only a failed resume restarts it: it leaves the scan paused, and the card would not
//    see the camera again in that mode.
constexpr uint32_t SCAN_SILENCE_MS = 5000;

// Transmit power +8 dBm, the most of the nRF52840 (ble_gap.h), for a steadier connection a few metres away.
// Set for the scanner and initiator role, whose power a connection inherits (ble_gap.h); Bluefruit.setTxPower()
// sets only the advertiser's (BLEAdvertising.cpp). Cost and EU limits: docs/HARDWARE.md, "Transmit power".
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
// A Panasonic advert's bytes go in a second line, indented under its header. Both fit in a log line
// (logger::TEXT_MAX), so nothing is cut.
constexpr char DUMP_PREFIX[] = "diag scan: ";
constexpr char DUMP_BYTES_PREFIX[] = "diag scan:   ";
static_assert(sizeof(DUMP_BYTES_PREFIX) - 1 + scan_dump::BODY_MAX <= logger::TEXT_MAX,
              "a 'diag scan' line would be cut");
uint8_t dumpSeen[DUMP_SEEN_MAX][scan_dump::ADDRESS_LENGTH + 1]; // address + the scan-response flag
size_t dumpSeenCount = 0;                                       // loop task only
volatile uint32_t dumpReports = 0; // every report of a run, weak ones and scan responses too (BLE task)
// A run listens at the normal timing; a timing the link asks for meanwhile is used when it ends.
uint16_t afterDumpIntervalMs = SCAN_INTERVAL_MS; // loop task only
uint16_t afterDumpWindowMs = SCAN_WINDOW_MS;
bool dumpStartedScan = false; // the run switched the scan on, so it switches it off at its end
uint32_t dumpResumeFailuresAtStart = 0; // loop task only: the totals when the run started
uint32_t dumpRestartsAtStart = 0;
// Reports of a run per advertising channel 37..39: all, and those of LUMIX cameras (BLE task).
volatile uint32_t dumpByChannel[scan_dump::ADV_CHANNELS] = {};
volatile uint32_t dumpCameraByChannel[scan_dump::ADV_CHANNELS] = {};

bool started = false;

// Set by the loop task; the BLE task resumes scanning after a report only while it is set.
volatile bool scanActive = false;
// Set by the loop task while it stops and starts the scan again (restartScan()): a resume() of the BLE
// task that meets the scan stopped then is not a failed resume.
volatile bool scanRestarting = false;

// Written only by the BLE task (aligned 32-bit accesses are atomic on the Cortex-M4).
// The loop task keeps the values from the start of a scan and reports the difference.
volatile uint32_t reportsTotal = 0;
volatile uint32_t otherPanasonicTotal = 0;
volatile uint32_t resumeFailuresTotal = 0; // Scanner.resume() failed while the scan was on
#ifdef GPXCAM_TEST_COMMANDS
volatile bool stallNextResume = false; // 'diag scan stall': the next resume is left out, as if it failed
#endif
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
uint32_t resumeFailuresSeen = 0; // the watchdog's last look at resumeFailuresTotal
bool resumeRestartLogged = false; // a restart after a failed resume: one line per scan, then counted only
bool startFailureLogged = false;  // "could not start" once until a scan starts again

void logScanLine(bluetooth::Lines lines, const char *text)
{
    if (lines == bluetooth::Lines::Kept)
        logger::line("%s", text);
    else
        logger::transient("%s", text);
}

// "<first>, <rest>" in one line when it fits the card's line (logger::TEXT_MAX), else <first> and then
// "<restPrefix><rest>" in a line of its own, so that nothing is cut.
void logInParts(bluetooth::Lines lines, const char *first, const char *restPrefix, const char *rest)
{
    char text[logger::LINE_MAX];
    if (strlen(first) + 2 + strlen(rest) <= logger::TEXT_MAX) {
        snprintf(text, sizeof(text), "%s, %s", first, rest);
        logScanLine(lines, text);
        return;
    }
    logScanLine(lines, first);
    snprintf(text, sizeof(text), "%s%s", restPrefix, rest);
    logScanLine(lines, text);
}

Counts countsNow()
{
    return Counts{reportsTotal, otherPanasonicTotal, droppedTotal, resumeFailuresTotal};
}

Counts sinceScanStart()
{
    return Counts{reportsTotal - atScanStart.reports, otherPanasonicTotal - atScanStart.otherPanasonic,
                  droppedTotal - atScanStart.dropped, resumeFailuresTotal - atScanStart.resumeFailures};
}

// Loop task: stops the running scan and starts it again with the current timing; the camera list and
// the counters stay. False if it did not start again (the scan is then off).
// A report that came before the stop is handled while scanActive is false: BLEScanner::stop() ends
// with xTimerStop() (Bluefruit's _stopConnLed()), which lets the higher-priority BLE task run first
// (BLEScanner.cpp, bluefruit.cpp). Should a resume() still meet the stopped scan, scanRestarting keeps
// it from counting as a failed resume.
bool restartScan()
{
    scanRestarting = true;
    scanActive = false; // the BLE task stops resuming
    Bluefruit.Scanner.stop();
    scanActive = true; // before starting: the first report must already resume the scan
    const bool restarted = Bluefruit.Scanner.start(SCAN_UNTIL_STOPPED);
    if (!restarted)
        scanActive = false;
    scanRestarting = false;
    scanWatchStartMs = millis();
    return restarted;
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
    const uint32_t receivedMs = millis();
    lastReportMs = receivedMs;
    const advert_kind::Kind kind =
        advert_kind::fromReport(advert.type.connectable, advert.type.scannable, advert.type.scan_response);

    camera_list::Report report;
    const lumix::Advert parsed = lumix::parseAdvert(advert.data.p_data, advert.data.len, &report.camera);
    switch (parsed) {
    case lumix::Advert::Camera:
        memcpy(report.address, advert.peer_addr.addr, camera_list::ADDRESS_LENGTH);
        report.addressType = advert.peer_addr.addr_type;
        report.rssi = advert.rssi;
        report.kind = kind;
        report.receivedMs = receivedMs;
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
        const size_t channelSlot = scan_dump::channelSlot(advert.ch_index);
        if (channelSlot < scan_dump::ADV_CHANNELS) {
            dumpByChannel[channelSlot]++;
            if (parsed == lumix::Advert::Camera)
                dumpCameraByChannel[channelSlot]++;
        }
        if (advert.rssi >= scan_dump::RSSI_THRESHOLD_DBM) {
            scan_dump::Advert raw;
            memcpy(raw.address, advert.peer_addr.addr, scan_dump::ADDRESS_LENGTH);
            raw.addressType = advert.peer_addr.addr_type;
            raw.rssi = advert.rssi;
            raw.kind = kind;
            raw.channel = advert.ch_index;
            raw.length = advert.data.len <= scan_dump::MAX_ADVERT_BYTES ? (uint8_t)advert.data.len
                                                                        : (uint8_t)scan_dump::MAX_ADVERT_BYTES;
            memcpy(raw.data, advert.data.p_data, raw.length);
            xQueueSend(dumpQueue, &raw, 0); // full: dropped, the device is likely already shown
        }
    }

    // The SoftDevice pauses scanning after each complete report until it is told to go on
    // (ble_gap.h, sd_ble_gap_scan_start()). BLEScanner only does that itself for reports
    // its filters reject or when a report callback is set (BLEScanner.cpp,
    // _eventHandler()); we use neither, so we resume here. A failure while the scan should run (not
    // stopped or being restarted meanwhile by the loop task) leaves it paused: counted, and the
    // watchdog restarts it.
    if (!scanActive || advert.type.status == BLE_GAP_ADV_DATA_STATUS_INCOMPLETE_MORE_DATA)
        return;
#ifdef GPXCAM_TEST_COMMANDS
    if (stallNextResume) {
        stallNextResume = false;
        resumeFailuresTotal++;
        return; // the scan stays paused, as after a failed resume
    }
#endif
    if (!Bluefruit.Scanner.resume() && scanActive && !scanRestarting)
        resumeFailuresTotal++;
}

void logCamera(const char *what, const camera_list::Report &report)
{
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    char address[3 * camera_list::ADDRESS_LENGTH];
    lumix::formatSerial(report.camera.serial, serial, sizeof(serial));
    camera_list::formatAddress(report.address, address, sizeof(address));
    char first[logger::LINE_MAX];
    char prefix[32];
    char rest[64];
    snprintf(first, sizeof(first), "scan: camera %s %s: %s (%02x)", serial, what,
             lumix::stateName(report.camera.state), report.camera.state);
    snprintf(prefix, sizeof(prefix), "scan: camera %s: ", serial);
    snprintf(rest, sizeof(rest), "%s %s, %d dBm, %s", address, camera_list::addressTypeName(report.addressType),
             report.rssi, advert_kind::name(report.kind));
    logInParts(bluetooth::Lines::Kept, first, prefix, rest); // two lines only for the longest state names
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
    // Two lines, each within logger::TEXT_MAX with every count at its largest.
    logger::transient("scan: on %lu s, %lu reports, %lu camera, %lu other Panasonic",
                      (unsigned long)((nowMs - scanStartMs) / 1000), (unsigned long)counts.reports,
                      (unsigned long)cameraReports, (unsigned long)counts.otherPanasonic);
    logger::transient("scan: %lu dropped, %lu untracked, %lu resume failed, %lu restarts", (unsigned long)counts.dropped,
                      (unsigned long)untracked, (unsigned long)counts.resumeFailures, (unsigned long)restarts);
    for (size_t i = 0; i < camera_list::MAX_CAMERAS; i++) {
        const camera_list::Entry *entry = cameras.at(i);
        if (entry == nullptr)
            continue;
        char serial[2 * lumix::SERIAL_LENGTH + 1];
        lumix::formatSerial(entry->last.camera.serial, serial, sizeof(serial));
        const uint32_t agoDs = (nowMs - entry->lastSeenMs) / MS_PER_DECISECOND;
        logger::transient("scan: camera %s %s (%02x), %d dBm, last %lu.%lu s ago", serial,
                          lumix::stateName(entry->last.camera.state), entry->last.camera.state, entry->last.rssi,
                          (unsigned long)(agoDs / 10), (unsigned long)(agoDs % 10));
        logger::transient("scan: camera %s: %lu advert (%lu connectable) + %lu scan response", serial,
                          (unsigned long)entry->advertCount, (unsigned long)entry->connectableCount,
                          (unsigned long)entry->scanResponseCount);
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

void scanOn(Lines lines)
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
    atScanStart = countsNow();
    restarts = 0;
    restartLogged = false;
    resumeFailuresSeen = resumeFailuresTotal;
    resumeRestartLogged = false;
    scanActive = true; // before starting: the first report must already resume the scan
    if (!Bluefruit.Scanner.start(SCAN_UNTIL_STOPPED)) {
        scanActive = false;
        if (!startFailureLogged) // pairing tries again every second
            logger::line("scan: could not start (tried again quietly until it starts)");
        startFailureLogged = true;
        return;
    }
    startFailureLogged = false;
    scanStartMs = millis();
    lastStatusMs = scanStartMs;
    scanWatchStartMs = scanStartMs;
    char text[logger::LINE_MAX];
    snprintf(text, sizeof(text), "scan: on - active, radio %u ms of every %u ms, logging LUMIX cameras only",
             scanWindowMs, scanIntervalMs);
    logScanLine(lines, text);
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
    if (!restartScan()) {
        logger::line("scan: could not restart with the new timing");
        return false;
    }
    return true;
}

void setNormalScanTiming()
{
    setScanTiming(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
}

void scanOff(Lines lines)
{
    if (!scanActive)
        return;
    scanActive = false; // first, so the BLE task no longer resumes the scan
    // Stopping also works while the scan is paused after a report (ble_gap.h).
    const bool stopped = Bluefruit.Scanner.stop();
    const Counts counts = sinceScanStart();
    char first[logger::LINE_MAX];
    char rest[logger::LINE_MAX];
    snprintf(first, sizeof(first), "scan: off after %lu s%s, %lu reports, %lu camera",
             (unsigned long)((millis() - scanStartMs) / 1000), stopped ? "" : " (stop error)",
             (unsigned long)counts.reports, (unsigned long)cameraReports);
    snprintf(rest, sizeof(rest), "%lu other Panasonic, %lu dropped, %lu untracked", (unsigned long)counts.otherPanasonic,
             (unsigned long)counts.dropped, (unsigned long)untracked);
    logInParts(lines, first, "scan: ", rest); // one line but with very large counts
}

bool scanning()
{
    return scanActive;
}

bool scanForDumpOnly()
{
    return scanActive && dumpActive && dumpStartedScan;
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

// Loop task: the scan watchdog (SCAN_SILENCE_MS). At the normal timing a scan with no report for
// SCAN_SILENCE_MS since it was last (re)started is stopped and started again; at a slower timing only one
// that has had a failed resume. The timing and the camera list stay. restartScan(), setScanTiming() and
// scanOn() set scanWatchStartMs, so a silence counts from the switch.
void watchScan()
{
    const uint32_t lastReport = lastReportMs; // first: a report after this read is not older than now
    const uint32_t now = millis();
    const uint32_t failures = resumeFailuresTotal;
    const bool resumeFailed = failures != resumeFailuresSeen;
    resumeFailuresSeen = failures;
    if (scanIntervalMs != SCAN_INTERVAL_MS || scanWindowMs != SCAN_WINDOW_MS) {
        if (!resumeFailed)
            return; // a search mode's slower timing: silences are normal there
        restarts++;
        restartsTotal++;
        if (!restartScan()) { // the link switches it on again
            logger::line("scan: could not restart after a failed resume");
            return;
        }
        if (!resumeRestartLogged) {
            resumeRestartLogged = true;
            logger::line("scan: restarted after a failed resume (more only counted)");
        }
        return;
    }
    const bool reportSinceStart = (int32_t)(lastReport - scanWatchStartMs) >= 0;
    if (reportSinceStart)
        restartLogged = false; // the next silence gets its line again
    if ((int32_t)(now - (reportSinceStart ? lastReport : scanWatchStartMs)) < (int32_t)SCAN_SILENCE_MS)
        return;
    restarts++;
    restartsTotal++;
    if (!restartScan()) { // the link and pairing switch it on again
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
    char line[scan_dump::BODY_MAX + 1];
    while (xQueueReceive(dumpQueue, &raw, 0) == pdTRUE) {
        if (!dumpActive || seenBefore(raw))
            continue;
        if (scan_dump::formatAdvert(raw, line, sizeof(line)) > 0)
            logger::line("%s%s", DUMP_PREFIX, line);
        if (scan_dump::formatBytes(raw, line, sizeof(line)) > 0)
            logger::line("%s%s", DUMP_BYTES_PREFIX, line);
    }
    if (dumpActive && (int32_t)(now - dumpDeadlineMs) >= 0) {
        dumpActive = false;
        // Before "done": the card page and the app stop reading the run at that line.
        logger::line("diag scan: by channel 37/38/39: %lu/%lu/%lu reports, %lu/%lu/%lu from LUMIX cameras",
                     (unsigned long)dumpByChannel[0], (unsigned long)dumpByChannel[1], (unsigned long)dumpByChannel[2],
                     (unsigned long)dumpCameraByChannel[0], (unsigned long)dumpCameraByChannel[1],
                     (unsigned long)dumpCameraByChannel[2]);
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

#ifdef GPXCAM_TEST_COMMANDS
void stallScan()
{
    if (!scanActive) {
        logger::line("diag scan stall: the scan is off");
        return;
    }
    stallNextResume = true;
    logger::line("diag scan stall: the next report is not resumed, as after a failed resume");
}
#endif

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
    for (size_t i = 0; i < scan_dump::ADV_CHANNELS; i++) {
        dumpByChannel[i] = 0;
        dumpCameraByChannel[i] = 0;
    }
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
    drainScanDump(millis());
    camera_list::Report report;
    while (xQueueReceive(reportQueue, &report, 0) == pdTRUE) {
        if (!scanActive)
            continue; // left over from before scanOff()
        cameraReports++;
        switch (cameras.add(report, report.receivedMs)) {
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
    // Read after the reports: a report keeps the time it came (Report::receivedMs), which may be later
    // than a time read before them, and the camera list's ages are unsigned.
    const uint32_t now = millis();
    camera_list::Entry lost;
    while (cameras.removeLost(now, &lost))
        logLost(lost);
    if (now - lastStatusMs >= STATUS_PERIOD_MS) {
        lastStatusMs = now;
        logStatus(now);
    }
}

} // namespace bluetooth
