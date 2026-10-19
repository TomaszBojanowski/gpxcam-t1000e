// BLE through the SoftDevice S140 7.3.0, started by the Bluefruit library (M3).
// (Not named ble.h: that name belongs to the SoftDevice header the library includes.)
// Central role only: the card looks for the camera and later connects to it; it never
// advertises and never accepts connections.
#pragma once

namespace bluetooth {

// Starts the SoftDevice; scanning stays off. Call once in setup(), after
// power::handleWakeUp(), so a short wake-up press goes back to System OFF without it.
// Bluefruit also mounts its file system in the data area 0xED000-0xF4000 (bond_init() in
// Bluefruit52Lib/src/utility/bonding.cpp); if it cannot be mounted, it is erased and
// formatted, which happens once and takes up to about 1 s.
// Returns false if the SoftDevice did not start; the card then works without BLE.
bool begin();

// Starts an active scan for LUMIX cameras (radio on 50 ms of every 100 ms) until
// scanOff(). Only cameras are logged: when first seen, when their state or address
// changes and when not seen for 30 s; a status line follows every 10 s.
void scanOn();

// Stops scanning and logs a summary. Does nothing if not scanning (also before begin()).
void scanOff();

bool scanning();

// Logs the BLE state, for the 'p' command.
void logState();

// Call from loop(): turns the camera reports into log lines.
void poll();

} // namespace bluetooth
