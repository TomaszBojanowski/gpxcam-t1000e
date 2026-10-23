// Keeps the time zone (tz_record.h) in the internal file system as /gpxcam/timezone.bin.
// Without the file the zone is tz_record::DEFAULT_NAME, and nothing is written until the
// zone is set. Loop task only.
#pragma once

#include "posix_tz.h"
#include "tz_record.h"

namespace tz_store {

// Loads the zone; a missing file means the default zone, and so (logged) does a damaged
// one or one whose rule cannot be used. Call once in setup() after bluetooth::begin(), with
// its result: Bluefruit mounts the file system. Without it the default zone is used and the
// file is not touched (InternalFS.begin() of the core would erase the data area when it
// cannot mount it).
void begin(bool fileSystemMounted);

// The rule of the zone in effect, valid after begin().
const posix_tz::Rule &rule();

// Whether the card sets the camera's clock and time zone when it connects ('clock'): on
// unless set off. Valid after begin().
bool clockOn();

enum class SetResult {
    Saved,     // written and in effect
    Unchanged, // the same setting is already saved
    Unknown,   // neither a name from the tz table nor a usable POSIX TZ rule
    NotSaved,  // writing failed; what is in effect stays as it was
};

// Sets the zone from a name from the tz table (any case) or a POSIX TZ rule, and saves it;
// the clock setting stays.
SetResult set(const char *text);

// Sets whether the card sets the camera's clock, and saves it with the zone (never Unknown).
SetResult setClock(bool on);

// Logs the zone and its rule and, with GPS time, the offset now and what the camera gets, or
// that the clock is off; for 'p' and 'tz'.
void logState();

} // namespace tz_store
