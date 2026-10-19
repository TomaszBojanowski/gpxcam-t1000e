// Pairing with a LUMIX camera (M4), started only on request ('pair' or click + hold).
// Needs UTC time from the GPS (switched on for the session if it was off) and connects
// only to a camera in pairing state 03 - never in standby 06. A failed attempt is
// followed by a pause; after 3 attempts pairing gives up. Loop task only.
#pragma once

namespace pairing {

// Starts a session; logs and refuses if one is running or pairing is not possible.
void start();

// Cancels a running session: disconnects and puts GPS and scanning back as they were, or,
// with resumeLink, starts the link again if it was on (System OFF passes false). Does
// nothing when idle.
void stop(bool resumeLink = true);

bool active();

// Logs the session state, for the 'p' command.
void logState();

// Call from loop().
void poll();

} // namespace pairing
