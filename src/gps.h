// GPS Airoha AG3335 on UART Serial1 (115200 8N1): power, raw NMEA lines in the log,
// configuration in the GPS RAM after every power-on (gps_setup.cpp).
#pragma once

#include <stdint.h>

namespace gps {

// Switches the GPS on (sequence from Seeed's SDK) and opens the UART. Blocks ~0.1 s.
void on();

// Puts the GPS in Backup (RTC) mode with $PAIR650,0, closes the UART, releases its pins
// and switches the main supply off; the backup supply (P0.08) stays on, so the next
// start is a hot start. Blocks for about 2 s (up to 4 s). Safe when already off.
void off();

bool isOn();

// Seconds from power-on to the first fix of this power-on; false while there is none.
bool firstFix(uint32_t *secondsAfterPowerOn);

// Sends "$<body>*hh\r\n" to the GPS (body e.g. "PAIR063,-1") and, with echo, shows it in
// the log (not in the history). Does nothing when off.
void sendCommand(const char *body, bool echo = true);

// Every NMEA line in the log (live only, never in the history); off by default
// because after a fix the lines contain the position.
void setRawLog(bool on);

// Reads the UART, parses RMC/GGA and logs a status line every 10 s. Call from loop().
void poll();

// One log line with the GPS state and counters.
void logState();

} // namespace gps
