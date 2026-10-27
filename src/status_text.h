// The 'status' command: the card's state as key=value lines for the card page
// (docs/index.html), easy to read by a program, unlike the free text of 'p'. No camera serial
// number, card id or position: the page shows what it gets. No hardware dependencies (tested
// on the PC; the enums come from headers without code).
//
//   status: format=1 fw=1.3.0 git=1234567
//   status: battery_mv=4100 battery_pct=89 charger=charging
//   status: gps=on gps_fix=1 gps_sat=11 gps_hold=0
//   status: paired=1 link=connected
//   status: pair=idle pair_attempt=0 pair_result=none
//   status: clock=on tz=Europe/Warsaw
//   status: sound=loud
//   status: updates=normal updates_now=normal
//   status: watchdog_restart=0
//   status: storage=ok
//   status: end
//
// Values hold no spaces; 'end' tells that the answer is complete. A later version may add
// keys, which a reader skips; a changed meaning raises 'format'.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "battery.h"
#include "link_control.h"
#include "link_policy.h"
#include "pairing.h"
#include "sound_rules.h"

namespace status_text {

constexpr unsigned FORMAT = 1;

// With the NUL: logger::LINE_MAX (128) less the "\r\n" that logger adds; main.cpp checks it
// (log.h cannot be included on the PC, where LINE_MAX is a macro of limits.h).
constexpr size_t LINE_SIZE = 126;

struct State {
    const char *version; // "1.3.0"
    const char *git;     // short commit hash
    bool watchdogRestart; // the last start was a watchdog reset: the card had hung
    const char *storage;  // storage::stateName(): "ok", or "damaged" (defaults, the pairing from its copy, if any)
    uint32_t batteryMillivolts;
    uint8_t batteryPercent; // battery_rules::percentFromMillivolts()
    battery::Charger charger;
    bool gpsOn;
    bool gpsFix;
    uint8_t gpsSatellites; // 0 when not known
    uint32_t gpsHoldSeconds; // the GPS kept on for a pairing to come, 0 without (pairing::gpsHoldSeconds())
    bool paired;
    link_control::State link;
    pairing::Progress pairing;
    bool clockOn;
    const char *timeZone; // tz_store::zoneText()
    sound_rules::Setting sound;
    link_policy::Updates updates;    // as chosen
    link_policy::Updates updatesNow; // in effect: normal while the guard slows a connection down
};

// Calls emit for every line, 'status: end' last.
void format(const State &state, void (*emit)(const char *line));

} // namespace status_text
