// Keeps the time zone (tz_record.h) in the internal file system as /gpxcam/timezone.bin.
// Without the file the zone is tz_record::DEFAULT_NAME, and nothing is written until the
// zone is set. Loop task only.
#pragma once

#include "posix_tz.h"
#include "tz_record.h"

namespace tz_store {

// Loads the zone; a missing file means the default zone, and so (logged) does a damaged
// one or one whose rule cannot be used. Call once in setup() after bluetooth::begin(): the
// file system is mounted by Bluefruit.
void begin();

// The zone in effect, valid after begin().
const tz_record::Record &record();
const posix_tz::Rule &rule();

enum class SetResult {
    Saved,     // written and in effect
    Unchanged, // the same zone is already saved
    Unknown,   // neither a name from the tz table nor a usable POSIX TZ rule
    NotSaved,  // writing failed; the zone in effect stays as it was
};

// Sets the zone from a name from the tz table (any case) or a POSIX TZ rule, and saves it.
SetResult set(const char *text);

} // namespace tz_store
