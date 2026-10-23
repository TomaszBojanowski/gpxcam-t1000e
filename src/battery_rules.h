// Battery rules of M6, without hardware dependencies (tested on the PC): when to power off
// to protect the battery, the state of charge, and the 8-byte entry of the battery log.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace battery_rules {

// M6 decision: below this the card powers off (System OFF) to protect the battery.
constexpr uint32_t LOW_MV = 3400;

// While running, readings come every 60 s; this many low ones in a row power off, so a
// short dip under load (radio, GPS) does not.
constexpr uint8_t LOW_READINGS_TO_POWER_OFF = 2;

// Counts the readings below LOW_MV in *lowReadings and says whether to power off now:
// at start after one low reading (nothing runs yet, so it is not a dip under load),
// otherwise after LOW_READINGS_TO_POWER_OFF in a row. Never with USB connected: then the
// card starts normally and the charger charges the battery.
bool powerOffForLowBattery(uint32_t millivolts, bool usb, bool atStart, uint8_t *lowReadings);

// The battery left until the card powers off, 0..100 %, estimated from the voltage. First the
// state of charge as Meshtastic estimates it for the T1000-E: its discharge curve (OCV_ARRAY in
// variants/nrf52840/tracker-t1000-e/variant.h, a voltage for every 10 %) with a straight line
// between the points, cut to a whole percent (getBatteryPercent() in src/Power.cpp, both at
// 6d41e279f1f5). Then scaled so that LOW_MV, where the card powers off (5 % on that curve),
// gives 0 % and a full battery still 100 %: at most 5 points below Meshtastic's figure. Rough:
// under load and while charging the voltage differs from the resting voltage of the curve.
uint8_t percentFromMillivolts(uint32_t millivolts);

// Battery log entry, little-endian: uptime in minutes (u32), battery voltage in mV (u16),
// what the link was doing (link_control::Activity), flags.
constexpr size_t ENTRY_SIZE = 8;
constexpr uint8_t FLAG_LOW_BATTERY_OFF = 0x01; // the card powered off after this entry

struct Entry {
    uint32_t uptimeMinutes;
    uint16_t millivolts;
    uint8_t activity;
    uint8_t flags;
};

void encode(const Entry &entry, uint8_t out[ENTRY_SIZE]);
Entry decode(const uint8_t in[ENTRY_SIZE]);

} // namespace battery_rules
