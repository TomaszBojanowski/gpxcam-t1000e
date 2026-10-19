// Keeps the pairing record (pairing_record.h) in the internal file system as
// /gpxcam/pairing.bin (M4). Loop task only.
#pragma once

#include "pairing_record.h"

namespace pairing_store {

// Loads the record. If it is missing or damaged (logged), creates one with a new random
// card UUID and no camera. Call once in setup() after bluetooth::begin() succeeded: the
// file system is mounted by Bluefruit and the random numbers come from the SoftDevice.
// The very first call writes flash (the data area, 0xED000-0xF4000). Returns false if
// the record could not be loaded or created.
bool begin();

// False until begin() succeeded.
bool ready();

const pairing_record::Record &record();

// Removes the paired camera, keeps the card UUID. False if not ready or not written.
bool forgetCamera();

// Logs the card UUID and the paired camera, for the 'p' command.
void logState();

} // namespace pairing_store
