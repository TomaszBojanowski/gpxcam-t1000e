// The card's own settings ('sound', 'updates'), kept as one 16-byte record in the file system
// (settings_store). No hardware dependencies (tested on the PC).
//
// Record, multi-byte values little-endian:
//   0..3    magic "GXST"
//   4       format version, 1
//   5       sound: 0 = loud, 1 = quiet, 2 = off (sound_rules::Setting)
//   6       location updates: 0 = normal, 1 = frequent (link_policy::Updates). Firmware 1.3
//           had it reserved and writes 0 when it saves; any other value (a later firmware's)
//           reads as normal, and the sound setting still applies.
//   7..11   reserved: written 0, not checked when read. A later setting takes one of these
//           bytes, with 0 for its default, so a card with an older firmware still reads the
//           file (and writes that byte 0 again when it saves).
//   12..15  CRC-32 (IEEE 802.3, as zlib) of bytes 0..11
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "link_policy.h"
#include "sound_rules.h"

namespace settings_record {

constexpr size_t SIZE = 16;

struct Record {
    sound_rules::Setting sound;
    link_policy::Updates updates;
};

// What applies without a file.
constexpr Record DEFAULTS = {sound_rules::Setting::Loud, link_policy::Updates::Normal};

enum class Status : uint8_t { Ok, WrongSize, WrongMagic, WrongVersion, BadChecksum, BadSound };

void encode(const Record &record, uint8_t out[SIZE]);

// Checks and reads a record; *record is changed only when the result is Ok.
Status decode(const uint8_t *data, size_t length, Record *record);

const char *statusText(Status status);

} // namespace settings_record
