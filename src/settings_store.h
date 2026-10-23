// Keeps the card's own settings (settings_record.h) in the internal file system as
// /gpxcam/settings.bin. Without the file the defaults apply (sound loud), and nothing is
// written until a setting is changed. Loop task only.
#pragma once

#include "sound_rules.h"

namespace settings_store {

// Loads the settings; a missing file means the defaults, and so (logged) does a damaged one.
// Call once in setup() after bluetooth::begin(), with its result: Bluefruit mounts the file
// system (see tz_store.h).
void begin(bool fileSystemMounted);

// The sound setting in effect; valid after begin().
sound_rules::Setting sound();

enum class SetResult {
    Saved,     // written and in effect
    Unchanged, // the same setting is already saved
    NotSaved,  // writing failed; what is in effect stays as it was
};

SetResult setSound(sound_rules::Setting setting);

// Logs the settings, for 'p' and 'sound'.
void logState();

} // namespace settings_store
