// The card's pairing data (M4): its own identifier for the camera and the paired camera,
// kept as one 32-byte record in the file system. No hardware dependencies (tested on the PC).
//
// Record, multi-byte values little-endian:
//   0..3    magic "GXPR"
//   4       format version, 1
//   5       1 = a camera is paired, 0 = none
//   6..21   card UUID (16 bytes, big-endian as in its text form)
//   22..26  camera serial number (all zero when byte 5 is 0)
//   27      0, reserved
//   28..31  CRC-32 (IEEE 802.3, as zlib) of bytes 0..27
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lumix.h"

namespace pairing_record {

constexpr size_t SIZE = 32;
constexpr size_t UUID_TEXT_LENGTH = 36; // xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx

struct Record {
    uint8_t uuid[lumix::UUID_LENGTH];
    bool paired;
    uint8_t serial[lumix::SERIAL_LENGTH];
};

enum class Status { Ok, WrongSize, WrongMagic, WrongVersion, BadChecksum, BadFlag };

void encode(const Record &record, uint8_t out[SIZE]);

// Checks size, magic, version, CRC and the paired flag; *record is written only for Ok.
Status decode(const uint8_t *data, size_t length, Record *record);

const char *statusText(Status status);

uint32_t crc32(const uint8_t *data, size_t length);

// Turns 16 random bytes into a version 4 (random) UUID by setting its version and variant
// bits (RFC 9562, 5.4).
void makeRandomUuid(uint8_t uuid[lumix::UUID_LENGTH]);

// Writes the UUID in its text form, lower case. Returns UUID_TEXT_LENGTH, or 0 if it does
// not fit in out together with the terminator.
size_t formatUuid(const uint8_t uuid[lumix::UUID_LENGTH], char *out, size_t size);

} // namespace pairing_record
