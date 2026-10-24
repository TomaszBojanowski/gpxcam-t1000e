// The internal file system (littlefs, 0xED000-0xF4000, InternalFileSystem.cpp in the core) that
// holds the pairing, time zone, settings and battery log: mounted here, and never formatted
// without a command when it holds data.
//
// Bluefruit.begin() calls InternalFS.begin() (bond_init(), bonding.cpp), which erases and formats
// the whole area when it cannot mount it (InternalFileSystem.cpp). On 2026-10-24 a flash write cut
// off by a hang left it unmountable, and that format erased the pairing, the time zone and the
// settings at the next start. So begin() runs before Bluefruit.begin():
//  - the file system mounts: Bluefruit finds it mounted and leaves it alone;
//  - the area is empty (all bytes 0xFF, a new card): it is formatted, nothing is lost;
//  - anything else: it is left as it is, and a small file system in RAM is mounted in its place
//    for Bluefruit's bond directories (the card does not bond). The card then works with the
//    default settings and the pairing from its copy (pairing_copy.h), says so in the log, in 'p'
//    and in 'status', and only 'storage format yes' erases the area (the card restarts with an
//    empty one, and the pairing comes back from the copy).
// Loop task only, except begin() in setup().
#pragma once

namespace storage {

// Call in setup() before bluetooth::begin(). Mounting only reads (lfs_mount, littlefs 1.7).
void begin();

// True when the file system in flash is mounted: settings can be read and written.
bool ready();

// False only if not even the RAM stand-in mounted: Bluefruit.begin() would then format the area,
// so BLE must not start.
bool safeForBluefruit();

// At start, after the version line: a line kept in the history when the area was formatted or
// the file system is damaged.
void logStart();

// 'storage format yes': erases the whole area and restarts the card. Refused (logged) while the
// file system mounts. Does not return when it erases.
void formatDamaged();

// 'p': "storage: ok, ..." or the damaged state and what to do.
void logState();

// For 'status': "ok" or "damaged".
const char *stateName();

} // namespace storage
