// Normal work with the paired camera (M5), 'link on' / 'link off'. Looks for the camera,
// connects only to the paired serial number in state 00 (link_policy.h), introduces the
// card (only once the GPS has UTC time: the introduction ends with it), switches the
// camera's GPS mode on, enables its notifications and sends the location at once and then
// every 25 s (earlier after moving or when the camera asks) - only with a GPS fix; without
// one the connection stays. After a disconnect it pauses (5 s, 15 s,
// 60 s) and looks again; if the camera refuses the card, it stops until 'link on' or a new
// pairing. GPS and scanning stay on while the link is on. (Not named "link": that is a
// C library function.) Loop task only.
#pragma once

namespace link_control {

// Starts the link when a camera is paired. Call once at the end of setup().
void begin();

// 'link on'; logs why not if no camera is paired or pairing runs.
void start();

// Disconnects and stops looking. keepGps: leave the GPS on (pairing follows).
void stop(bool keepGps);

bool active();

// What the LED shows (M5 decision).
enum class Indication { Off, Searching, Connected };
Indication indication();

// Logs the link state, the time since the last location and the counters, for 'p'.
void logState();

// Call from loop().
void poll();

} // namespace link_control
