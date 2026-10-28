// GPS Airoha AG3335 on UART Serial1 (115200 8N1): power on and off (Backup mode between
// uses, for a hot start), configuration in the GPS RAM after every power-on (gps_setup.cpp),
// RMC/GGA parsing, UTC time and position for the camera, a status line every 10 s.
#pragma once

#include <stdint.h>

namespace gps {

// Switches the GPS on (sequence from Seeed's SDK) and opens the UART. Blocks ~0.1 s.
void on();

// Puts the GPS in Backup (RTC) mode with $PAIR650,0, closes the UART, releases its pins
// and switches the main supply off; the backup supply (P0.08) stays on, so the next
// start is a hot start. Blocks for about 2 s (at most about 9.5 s with its sound). Safe when
// already off.
void off();

bool isOn();

// UTC now, in Unix seconds: the time of the last RMC with a fix (at most 2 s old) plus the
// time since it arrived. False without a recent fix. The RMC reaches the card a fraction
// of a second after its time, so the result may lag by that much.
bool utcNow(int64_t *unixSeconds);

// UTC now from the last RMC with a fix if it came at most maxAgeMs ago, with millis() counting the time
// since (the 32 kHz crystal): for a camera that wants the time long after the card introduced itself
// (older_session, the Wi-Fi step), when the GPS may have lost its fix meanwhile. *ageMs: how old the
// fix is. False without a fix in that time; a GPS switched on again forgets the last one.
bool utcSinceFix(uint32_t maxAgeMs, int64_t *unixSeconds, uint32_t *ageMs);

// The position of the last RMC with a fix (at most 2 s old): degrees * 10^7, and the
// altitude of the last GGA in decimetres (0 if the GPS gave none). False without a recent
// fix. Stays in RAM: never logged.
bool position(int32_t *latitudeE7, int32_t *longitudeE7, int32_t *altitudeDm);

// A fix in the last RMC, at most 2 s old: when position() gives one.
bool hasFix();

// Satellites in use from the last GGA; -1 when off or not known yet.
int satellites();

#ifdef GPXCAM_TEST_COMMANDS
// Test builds only (platformio.ini, env t1000e_test): 'diag gps stale' makes the time and position count
// as not fresh (utcNow(), position(), hasFix() false) while the last fix stays for utcSinceFix(), as when
// the card was carried indoors; 'diag gps fresh' ends it.
void setStaleForTest(bool stale);
#endif

// Sends "$<body>*hh\r\n" to the GPS (body e.g. "PAIR063,-1") and, with echo, shows it in
// the log (not in the history). Does nothing when off.
void sendCommand(const char *body, bool echo = true);

// Reads the UART, parses RMC/GGA and logs a status line every 10 s. Call from loop().
void poll();

// One log line with the GPS state and counters.
void logState();

} // namespace gps
