// The startup scan (1.5.0-beta.5), for a test without the app: a card that is not paired listens
// for 30 s, 10 s after it starts, at the timing of 'diag scan' (50 ms of every 100 ms), and keeps in
// the log history what it heard - a start line, each LUMIX camera (state, RSSI, advertising channel,
// kind of advertisement; bluetooth.cpp logs them) and the scan's summary with the reports per
// channel - to be read later with 'log' or Copy log. For a card left in a wall charger in another
// room, away from a computer's radio. Once per start; loop task.
#pragma once

#include <stdint.h>

namespace startup_scan {

// Call from loop(): starts the scan when it is due and ends it after its 30 s.
void poll(uint32_t nowMs);

bool running();

// Ends the scan before its time (pairing takes the radio over); its summary goes to the log.
void stop();

} // namespace startup_scan
