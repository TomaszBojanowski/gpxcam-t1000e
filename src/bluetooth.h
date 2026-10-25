// BLE through the SoftDevice S140 7.3.0, started by the Bluefruit library (M3).
// (Not named ble.h: that name belongs to the SoftDevice header the library includes.)
// Central role only: the card looks for the camera and connects to it (camera_link.h); it
// never advertises and never accepts connections.
#pragma once

#include <stddef.h>

#include "camera_list.h"

namespace bluetooth {

// Starts the SoftDevice and registers the camera's GATT UUIDs; scanning stays off. Call
// once in setup(), after power::handleWakeUp(), so a short wake-up press goes back to
// System OFF without it.
// Bluefruit also mounts its file system in the data area 0xED000-0xF4000 (bond_init() in
// Bluefruit52Lib/src/utility/bonding.cpp) and would erase and format it if it could not mount
// it; storage::begin() runs first, so Bluefruit finds a mounted one (see storage.h).
// Returns false if the SoftDevice did not start; the card then works without BLE.
bool begin();

// Starts an active scan for LUMIX cameras (radio on 50 ms of every 100 ms unless
// setScanTiming() said otherwise) until scanOff(). Only cameras are logged: when first
// seen, when their state or address changes and when not seen for 30 s; a status line
// follows every 10 s.
void scanOn();

// Stops scanning and logs a summary. Does nothing if not scanning (also before begin()).
void scanOff();

// Radio on windowMs of every intervalMs (M6: slower when the camera is away or in
// standby); applied at once if scanning, kept for the next scanOn(). During a 'diag scan'
// run it is kept for when the run ends. False if out of the SoftDevice's range (interval up
// to 10.24 s, window up to the interval).
bool setScanTiming(uint16_t intervalMs, uint16_t windowMs);

// Back to the M3 timing: 50 ms of every 100 ms.
void setNormalScanTiming();

bool scanning();

// True after begin() succeeded.
bool ready();

// Camera i (0..camera_list::MAX_CAMERAS-1) seen in the current or last scan, or nullptr.
const camera_list::Entry *camera(size_t i);

// Logs the BLE state, for the 'p' command.
void logState();

// Call from loop(): turns the camera reports into log lines.
void poll();

// 'diag scan': for about 30 s, logs the advertisements of every close device (not only cameras),
// each shown once, with the full bytes only for Panasonic manufacturer data (scan_dump.h), then
// how many reports came in all. It listens at the normal timing whatever the link's search mode,
// and switches scanning on for the run if it was off (and off again at its end). For telling an
// unknown camera's dialect from its advertisement.
void startScanDump();

} // namespace bluetooth
