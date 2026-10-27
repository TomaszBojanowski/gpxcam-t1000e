// LUMIX cameras seen while scanning (M3): which reports are worth a log line, which
// cameras have gone. No hardware dependencies (tested on the PC).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "advert_kind.h"
#include "lumix.h"

namespace camera_list {

constexpr size_t MAX_CAMERAS = 4;      // M3 decision
constexpr uint32_t LOST_AFTER_MS = 30000; // M3 decision: "not seen" after 30 s

constexpr size_t ADDRESS_LENGTH = 6;

// Address types as the SoftDevice reports them (ble_gap.h, BLE_GAP_ADDR_TYPES).
constexpr uint8_t ADDRESS_PUBLIC = 0x00;
constexpr uint8_t ADDRESS_RANDOM_STATIC = 0x01;
constexpr uint8_t ADDRESS_RANDOM_RESOLVABLE = 0x02; // RPA
constexpr uint8_t ADDRESS_RANDOM_NON_RESOLVABLE = 0x03;
constexpr uint8_t ADDRESS_ANONYMOUS = 0x7F;

// One received advertisement or scan response of a camera.
struct Report {
    lumix::Camera camera;
    uint8_t address[ADDRESS_LENGTH]; // as the SoftDevice gives it: least significant byte first
    uint8_t addressType;
    int8_t rssi; // dBm
    advert_kind::Kind kind;
};

struct Entry {
    bool used;
    Report last;
    uint32_t lastSeenMs;
    uint32_t advertCount;      // advertisements of any kind
    uint32_t connectableCount; // ... of them connectable
    uint32_t scanResponseCount;
};

enum class Change {
    None,      // a known camera with the same state and address
    New,       // first report of this serial number
    Changed,   // state, address or address type differ from the last report
    TableFull, // a new camera, but MAX_CAMERAS are tracked already; not stored
};

class CameraList {
public:
    // Stores the report (unless TableFull) and says what is new about it.
    Change add(const Report &report, uint32_t nowMs);

    // Removes one camera not seen for LOST_AFTER_MS and copies it to *lost; false if none.
    // Call until false. Works across the 49-day wrap of millis().
    bool removeLost(uint32_t nowMs, Entry *lost);

    // Camera in slot i (0..MAX_CAMERAS-1), or nullptr if the slot is empty.
    const Entry *at(size_t i) const;

    void clear();

private:
    Entry entries_[MAX_CAMERAS] = {};
};

// "public", "random static", "RPA", "random", "anonymous" or "unknown".
const char *addressTypeName(uint8_t type);

// Writes the address as "aa:bb:cc:dd:ee:ff", most significant byte first. Returns its
// length, or 0 if it does not fit in out together with the terminator.
size_t formatAddress(const uint8_t address[ADDRESS_LENGTH], char *out, size_t size);

} // namespace camera_list
