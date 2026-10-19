// GPS Airoha AG3335 on UART Serial1 (115200 8N1): power, raw NMEA lines in the log,
// configuration in the GPS RAM after every power-on (gps_setup.cpp).
#pragma once

namespace gps {

// Switches the GPS on (sequence from Seeed's SDK) and opens the UART. Blocks ~0.13 s.
void on();

// Closes the UART, releases its pins and switches the GPS main supply off; the
// backup supply (P0.08) stays on. Safe to call when the GPS is already off.
void off();

bool isOn();

// Sends "$<body>*hh\r\n" to the GPS (body e.g. "PAIR063,-1"). Does nothing when off.
void sendCommand(const char *body);

// Reads the UART and logs complete NMEA lines. Call from loop().
void poll();

// One log line with the GPS state and counters.
void logState();

} // namespace gps
