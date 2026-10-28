// The battery log's entries in RAM until they go to its file together, and where that file
// lives. The internal file system is littlefs 1.7 (Adafruit core 1.10601.0, lfs.h
// LFS_VERSION 0x00010007) with 128-byte blocks; every write to a file copies its last block
// and rewrites a block of its directory, and the core then erases and programs each 4 KB flash
// page touched (flash_cache.c). One write per BATCH_ENTRIES entries instead of one per entry
// spares the flash. No hardware dependencies (tested on the PC).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "battery_rules.h"

namespace battery_batch {

// File and RAM together: 8 KB, about 7 days at one entry every 10 minutes; then logging stops.
constexpr uint32_t MAX_ENTRIES = 1024;

// Written to the file every this many entries: 1 h at one entry every 10 minutes. Entries in
// RAM are lost if the card resets without powering off properly.
constexpr size_t BATCH_ENTRIES = 6;

class Batch {
public:
    // Adds an entry; false when the batch is full or the log, with entriesInFile, has
    // MAX_ENTRIES already.
    bool add(const battery_rules::Entry &entry, uint32_t entriesInFile);

    // Whether to write the batch now: BATCH_ENTRIES entries, or the log is full with them.
    bool due(uint32_t entriesInFile) const;

    size_t size() const { return count; }
    const uint8_t *bytes() const { return data; } // size() * ENTRY_SIZE bytes, oldest first
    size_t byteCount() const { return count * battery_rules::ENTRY_SIZE; }
    battery_rules::Entry entry(size_t index) const; // index < size()

    // After the entries were written, or when they are dropped.
    void clear() { count = 0; }

private:
    uint8_t data[BATCH_ENTRIES * battery_rules::ENTRY_SIZE] = {};
    size_t count = 0;
};

// USB counts as plugged in, ending a run on the battery and writing the batch, only after
// USB_STABLE_MS without a break. A magnetic cable that bounces gives short pulses of USB power;
// on 2026-10-24 a batch write started on one of them was cut off by the hang that followed.
constexpr uint32_t USB_STABLE_MS = 10000;

class StableUsb {
public:
    // Call with every reading; true once USB power has been there for USB_STABLE_MS without a
    // break.
    bool update(bool usbNow, uint32_t nowMs);

private:
    bool present = false;
    uint32_t sinceMs = 0;
};

// What to do at start with the file of firmware before 1.4.0 (/gpxcam/battery.bin). With both
// files (back to 1.3 and up again) both stay, and 'bat log clear' removes both.
enum class OldFile : uint8_t { None, Move, KeepBoth };
OldFile oldFile(bool oldExists, bool newExists);

} // namespace battery_batch
