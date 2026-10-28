// Normal work with the paired camera, 'link on' / 'link off': looks for it in the search modes of
// link_policy.h, connects once the GPS has UTC time, introduces the card in the camera's dialect
// (camera_session, older_session) and sends the locations by the timing of 'updates' (link_policy.h);
// pauses after a failure and stops when the camera refuses the card, until 'link on' or a new pairing.
// In detail: docs/HARDWARE.md, "Normal work". (Not named "link": that is a C library function.)
// Loop task only.
#pragma once

#include <stdint.h>

#include "link_policy.h"

namespace link_control {

// Starts the link when a camera is paired. Call once at the end of setup().
void begin();

// 'link on'; logs why not if no camera is paired or pairing runs.
void start();

// Disconnects and stops looking. keepGps: leave the GPS on (pairing follows).
void stop(bool keepGps);

bool active();

// What the link is doing, for 'status'. Stopped: the camera refused the card (see above).
enum class State : uint8_t { Off, Searching, Connecting, Connected, Pausing, Stopped };
State state();

// The location timing in effect, for 'status': in a connection the one in use (normal while
// the guard of link_policy slows it down), otherwise the chosen one ('updates').
link_policy::Updates updatesNow();

// What the LED shows.
enum class Indication : uint8_t { Off, Searching, Connected };
Indication indication();

// What the link is doing, for the battery log (stored as a byte: keep the values).
enum class Activity : uint8_t { Off = 0, Connected = 1, Normal = 2, Rare = 3, Standby = 4, LongStandby = 5 };
Activity activity();
const char *activityName(uint8_t activity); // "off", "connected", ...; "?" for others

// Logs the link state, the time since the last location and the counters, for 'p'.
void logState();

// Call from loop().
void poll();

} // namespace link_control
