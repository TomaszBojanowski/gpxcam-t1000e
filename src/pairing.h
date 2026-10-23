// Pairing with a LUMIX camera (M4), started only on request ('pair' or click + hold).
// Needs UTC time from the GPS (switched on for the session if it was off) and connects
// only to a camera in pairing state 03 - never in standby 06. A failed attempt is
// followed by a pause; after 3 attempts pairing gives up. Loop task only.
#pragma once

#include <stdint.h>

namespace pairing {

enum class Phase : uint8_t { Idle, WaitingForFix, Searching, Introducing, Pausing };

// How the last session ended, for 'status'.
enum class Result : uint8_t {
    None,             // no session since start, or one runs
    Paired,
    NotSaved,         // the camera accepted the card, but the pairing file could not be written
    NotPossible,      // BLE or the pairing file is not available
    NoGpsFix,         // no GPS time in time
    NoCamera,         // no camera in pairing mode in time, and no LUMIX camera seen at all
    NotInPairingMode, // as NoCamera, but a LUMIX camera was seen in another state
    Failed,           // every attempt failed
    Cancelled,
};

struct Progress {
    Phase phase;
    int attempt; // of the running session: 1..3 from its first connection attempt on, else 0
    Result lastResult;
};

// Starts a session; logs and refuses if one is running or pairing is not possible.
void start();

// Cancels a running session: disconnects and puts GPS and scanning back as they were, or,
// with resumeLink, starts the link again if it was on (System OFF passes false). Does
// nothing when idle.
void stop(bool resumeLink = true);

bool active();

Progress progress();

// Logs the session state, for the 'p' command.
void logState();

// Call from loop().
void poll();

} // namespace pairing
