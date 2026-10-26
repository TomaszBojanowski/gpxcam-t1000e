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
constexpr uint32_t RECENT_SECONDS = 10;    // recentReports(): the reports of the last 10 s, logged at a connect

// The primary advertising channels a legacy report comes on (ble_gap.h, ble_gap_evt_adv_report_t::ch_index:
// 37, 38, 39 - 2402, 2426 and 2480 MHz), counted one by one: a card that hears mostly one of them is jammed
// on the others where it lies (Wi-Fi), a camera heard on one only advertises there.
constexpr uint8_t FIRST_ADV_CHANNEL = 37;
constexpr size_t ADV_CHANNELS = 3;

// 0..2 for channel 37..39, or ADV_CHANNELS for any other index.
inline size_t channelSlot(uint8_t channel)
{
    return channel >= FIRST_ADV_CHANNEL && channel < FIRST_ADV_CHANNEL + ADV_CHANNELS ? channel - FIRST_ADV_CHANNEL
                                                                                       : ADV_CHANNELS;
}

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
    uint8_t channel; // ch_index: 37, 38 or 39
};

struct Entry {
    bool used;
    Report last;
    uint32_t lastSeenMs;
    uint32_t advertCount;      // advertisements of any kind
    uint32_t connectableCount; // ... of them connectable
    uint32_t scanResponseCount;
    uint32_t channelCount[ADV_CHANNELS]; // reports (advertisements and scan responses) per channel 37..39
    // Reports per second of the last RECENT_SECONDS seconds: the count of the second recentSecond[i]
    // (millis() / 1000), in slot second % RECENT_SECONDS.
    uint16_t recentCount[RECENT_SECONDS];
    uint32_t recentSecond[RECENT_SECONDS];
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

// Reports of the camera (advertisements and scan responses) in the last RECENT_SECONDS seconds,
// the current one included; works across the 49-day wrap of millis() but for that one moment.
uint32_t recentReports(const Entry &entry, uint32_t nowMs);

// "public", "random static", "RPA", "random", "anonymous" or "unknown".
const char *addressTypeName(uint8_t type);

// Writes the address as "aa:bb:cc:dd:ee:ff", most significant byte first. Returns its
// length, or 0 if it does not fit in out together with the terminator.
size_t formatAddress(const uint8_t address[ADDRESS_LENGTH], char *out, size_t size);

} // namespace camera_list
