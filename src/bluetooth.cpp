#include "bluetooth.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

#include "camera_list.h"
#include "log.h"
#include "lumix.h"

// Sources: Bluefruit52Lib 0.21.0 in the core (bluefruit.cpp, BLEScanner.cpp) and the
// S140 7.3.0 API (ble_gap.h).

namespace {

// Bluefruit.begin(prph_count, central_count) (bluefruit.cpp) sizes the SoftDevice RAM for
// these links and fails if it does not fit below the program's RAM (0x20006000,
// variants/t1000-e/nrf52840_s140_v7.ld).
constexpr uint8_t PERIPHERAL_LINKS = 0; // the card never accepts connections
constexpr uint8_t CENTRAL_LINKS = 1;    // the camera

// Scanning (M3 decision; the current is to be measured in M6): active scan, the radio
// listens 50 ms of every 100 ms, until stopped.
constexpr uint16_t SCAN_INTERVAL_MS = 100;
constexpr uint16_t SCAN_WINDOW_MS = 50;
constexpr uint16_t SCAN_UNTIL_STOPPED = 0; // BLEScanner::start() timeout: 0 = none

constexpr uint32_t STATUS_PERIOD_MS = 10000;
constexpr uint32_t MS_PER_DECISECOND = 100;

static_assert(camera_list::ADDRESS_LENGTH == BLE_GAP_ADDR_LEN, "address length");

// Camera reports go from the BLE task to the loop task through this queue (static, no
// heap). The BLE task never waits: a report that does not fit is counted as dropped.
constexpr UBaseType_t QUEUE_LENGTH = 8;
StaticQueue_t queueControl;
uint8_t queueStorage[QUEUE_LENGTH * sizeof(camera_list::Report)];
QueueHandle_t reportQueue = nullptr;

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
    if (event->header.evt_id != BLE_GAP_EVT_ADV_REPORT)
        return;
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
    if (!Bluefruit.begin(PERIPHERAL_LINKS, CENTRAL_LINKS))
        return false;
    reportQueue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(camera_list::Report), queueStorage, &queueControl);
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
    logger::line("scan: on - active, radio %u ms of every %u ms, logging LUMIX cameras only", SCAN_WINDOW_MS,
                 SCAN_INTERVAL_MS);
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

void logState()
{
    if (!started) {
        logger::line("ble: off, the SoftDevice did not start");
        return;
    }
    logger::line("ble: SoftDevice on, %s", scanActive ? "scanning" : "not scanning");
    if (scanActive)
        logStatus(millis());
}

void poll()
{
    if (!started)
        return;
    const uint32_t now = millis();
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
