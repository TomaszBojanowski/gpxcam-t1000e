// The time zone chosen with the 'tz' command and whether the card sets the camera's clock at
// all ('clock'), kept as one 124-byte record in the file system (tz_store). No hardware
// dependencies (tested on the PC).
//
// Record, multi-byte values little-endian:
//   0..3     magic "GXTZ"
//   4        format version, 1
//   5        clock: 0 = the card sets the camera's clock (as up to 1.2, whose files have 0
//            here), 1 = it does not ('clock off')
//   6..7     0, reserved
//   8..55    zone name from the tz table, NUL-terminated, zero-padded; empty for a rule
//            given directly
//   56..119  POSIX TZ rule (posix_tz.h), NUL-terminated, zero-padded
//   120..123 CRC-32 (IEEE 802.3, as zlib) of bytes 0..119
// 124 bytes fit in one 128-byte block of the file system (LFS_BLOCK_SIZE in the core's
// InternalFileSystem.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tz_record {

constexpr size_t SIZE = 124;
constexpr size_t NAME_SIZE = 48; // with the NUL; tools/gen_tz_table.py allows names up to 40
constexpr size_t RULE_SIZE = 64; // with the NUL; tools/gen_tz_table.py allows rules up to 63

// Without a record: Central European Time, as up to version 1.1.
constexpr const char *DEFAULT_NAME = "Europe/Warsaw";

struct Record {
    char name[NAME_SIZE];
    char rule[RULE_SIZE];
    bool clockOff; // the card does not set the camera's clock; false in a record made by {}
};

enum class Status : uint8_t { Ok, WrongSize, WrongMagic, WrongVersion, BadChecksum, BadText, BadClockFlag };

// What the user typed: a name from the tz table (any case; kept as the table spells it) or a
// POSIX TZ rule, with the clock on. False, with *record unchanged, if it is neither or too long.
bool fromInput(const char *text, Record *record);

Record defaultRecord();

// A newer firmware may bring a newer tz database: a record with a name from the table takes
// the table's current rule; the clock setting stays. True if the rule changed.
bool updateFromTable(Record *record);

void encode(const Record &record, uint8_t out[SIZE]);

// Checks size, magic, version, CRC, that both texts end within their fields and that the rule
// is not empty; *record is written only for Ok.
Status decode(const uint8_t *data, size_t length, Record *record);

const char *statusText(Status status);

} // namespace tz_record
