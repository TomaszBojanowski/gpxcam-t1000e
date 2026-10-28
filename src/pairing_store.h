// Keeps the pairing record (pairing_record.h) in the internal file system as
// /gpxcam/pairing.bin. Loop task only.
#pragma once

#include "pairing_record.h"

namespace pairing_store {

// Loads the record from /gpxcam/pairing.bin and keeps its copy (pairing_copy.h) the same. If the
// file is missing or damaged (logged), the copy brings it back; with no copy either, a record
// with a new random card UUID and no camera is made. Without the file system (storage::ready())
// the copy alone is used and nothing is written. Call once in setup() after bluetooth::begin()
// succeeded: the random numbers come from the SoftDevice. The very first call writes flash.
// Returns false if there is no record.
bool begin();

// False until begin() succeeded.
bool ready();

// The pairing can be changed: ready and the file system works (not a pairing from the copy alone).
bool writable();

const pairing_record::Record &record();

// Stores the camera that accepted the card and its dialect (pairing_record DIALECT_NEWER or
// DIALECT_OLDER), replacing an earlier one, in the file and then in
// its copy. False if not writable or the file was not written.
bool saveCamera(const uint8_t serial[lumix::SERIAL_LENGTH], uint8_t dialect);

// Removes the paired camera, keeps the card UUID; the file, then its copy. False if not writable or
// the file was not written.
bool forgetCamera();

// Logs the card UUID, the paired camera and the copy's pages, for the 'p' command.
void logState();

} // namespace pairing_store
