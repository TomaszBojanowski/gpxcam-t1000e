// Battery log for measuring the current without a meter (M6): while switched on with
// 'bat log on' and running on the battery, an entry every 10 minutes (uptime, voltage, what
// the link was doing) is appended to /gpxcam/battery.bin in the file system, so it survives
// a power-off. Entries: battery_rules::Entry. Loop task only.
#pragma once

#include <stdint.h>

namespace battery_log {

constexpr uint32_t PERIOD_MS = 10 * 60000;
constexpr uint32_t MAX_ENTRIES = 1024; // 8 KB, about 7 days; then logging stops

// 'bat log on' / 'bat log off'. On by command only, to spare the flash.
void start();
void stop();

// 'bat log': all entries, oldest first. 'bat log clear': removes the file.
void dump();
void clear();

// Call from loop(): an entry when the card goes on the battery and every PERIOD_MS then.
void poll();

// The last entry before a low-battery power-off (also when the log is off: it explains
// the power-off later).
void addPowerOffEntry(uint32_t millivolts);

} // namespace battery_log
