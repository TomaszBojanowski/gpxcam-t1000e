// Battery log for measuring the current without a meter (M6): while switched on with
// 'bat log on' and running on the battery, an entry every 10 minutes (uptime, voltage, what
// the link was doing) goes to /gpxcam/battery.bin in the file system, so it survives a
// power-off. Entries: battery_rules::Entry. They wait in RAM and go to the file
// battery_batch::BATCH_ENTRIES at a time (1 h), and at once when the cable is plugged in,
// on 'bat log off' and before a low-battery power-off; at most battery_batch::MAX_ENTRIES.
// Loop task only.
#pragma once

#include <stdint.h>

namespace battery_log {

constexpr uint32_t PERIOD_MS = 10 * 60000;

// Counts the entries in the file. Call once in setup() after bluetooth::begin(), with its
// result (Bluefruit mounts the file system, see tz_store.h), before the battery is checked.
void begin(bool fileSystemMounted);

// 'bat log on' / 'bat log off' (writes the entries in RAM). On by command only, to spare
// the flash.
void start();
void stop();

// 'bat log': all entries, oldest first, those in RAM marked; writes nothing. 'bat log clear':
// removes the file and the entries in RAM.
void dump();
void clear();

// Call from loop(): an entry when the card goes on the battery and every PERIOD_MS then.
void poll();

// The last entry before a low-battery power-off, written at once (also when the log is off:
// it explains the power-off later).
void addPowerOffEntry(uint32_t millivolts);

} // namespace battery_log
