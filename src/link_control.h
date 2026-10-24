// Normal work with the paired camera (M5, M6), 'link on' / 'link off'. Looks for the camera
// in one of the search modes of link_policy.h (normal, rare, standby, long standby: scan
// timing and GPS); connects only to the paired serial number in state 00 and only once the
// GPS has UTC time (the introduction ends with it). Then it switches the camera's GPS mode
// on, listens for its notifications and sends the location at once and then by the timing of
// 'updates' (link_policy.h: every 25 s or 5 s, earlier after moving or when the camera asks) -
// only with a GPS fix; without one the connection stays. No scanning while connected. After
// a failure it pauses (5 s, 15 s, 60 s) and looks again; a camera that closes the connection
// itself (switched off) gives a 5 s pause; a connection that was not established is tried
// again after 1 s, once. If the camera refuses the card, it stops until 'link on' or a new
// pairing.
// (Not named "link": that is a C library function.) Loop task only.
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

// What the LED shows (M5 decision).
enum class Indication { Off, Searching, Connected };
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
