// 'diag scan': a short dump of the advertisements the card hears, so an unknown camera's dialect
// can be told from its advertisement (newer, older, or neither) before anyone pairs it. Only close
// reports are shown (a threshold), the full bytes only for Panasonic manufacturer data and a short
// summary for anything else, with the address masked so a tester can send the output as is. The
// formatting has no hardware dependencies (tested on the PC); bluetooth.cpp runs the scan and calls
// formatAdvert and formatBytes from the loop task.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "advert_kind.h"

namespace scan_dump {

constexpr int8_t RSSI_THRESHOLD_DBM = -65; // only devices this close or closer are shown
constexpr uint32_t DURATION_MS = 30000;    // one run lasts this long
constexpr size_t MAX_ADVERT_BYTES = 31;    // a legacy advertising payload
constexpr size_t ADDRESS_LENGTH = 6;

// The longest text formatAdvert or formatBytes writes: the bytes, "bytes:" and 3 characters per byte (99);
// a header is at most 83 ("random static", -128 dBm, a 3-digit channel, "non-connectable", ", Panasonic").
// bluetooth.cpp checks that it fits in a log line after its prefix, so nothing is cut.
constexpr size_t BODY_MAX = 6 + 3 * MAX_ADVERT_BYTES;

// The primary advertising channels a legacy report comes on (ble_gap.h, ble_gap_evt_adv_report_t::ch_index:
// 37, 38, 39 - 2402, 2426 and 2480 MHz), counted one by one in a run: a card that hears mostly one of them is
// jammed on the others where it lies (Wi-Fi), a camera heard on one only advertises there.
constexpr uint8_t FIRST_ADV_CHANNEL = 37;
constexpr size_t ADV_CHANNELS = 3;

// 0..2 for channel 37..39, or ADV_CHANNELS for any other index.
inline size_t channelSlot(uint8_t channel)
{
    return channel >= FIRST_ADV_CHANNEL && channel < FIRST_ADV_CHANNEL + ADV_CHANNELS ? channel - FIRST_ADV_CHANNEL
                                                                                       : ADV_CHANNELS;
}

// One received advertisement or scan response.
struct Advert {
    uint8_t address[ADDRESS_LENGTH]; // as the SoftDevice gives it: least significant byte first
    uint8_t addressType;
    int8_t rssi; // dBm
    advert_kind::Kind kind;
    uint8_t channel; // ch_index: 37, 38 or 39
    uint8_t length;  // bytes of data in use, at most MAX_ADVERT_BYTES
    uint8_t data[MAX_ADVERT_BYTES];
};

// The header line of the advert in 'diag scan': the address with all but its last byte hidden
// (**:**:**:**:**:xx, as the logs are masked), the address type, RSSI, the advertising channel ("ch 37"..
// "ch 39"), "rsp" for a scan response or "adv" and the advertisement's kind (connectable, scan-only,
// non-connectable), the length, and "camera" for a LUMIX camera or "Panasonic" for other Panasonic
// manufacturer data. Returns the length written (< size), or 0 if it does not fit.
size_t formatAdvert(const Advert &advert, char *out, size_t size);

// The line under the header for Panasonic manufacturer data: "bytes: 02 01 06 ..." with every byte of the
// advert, in one line, as the masking of the camera's serial number in it works line by line (the card
// page, local tools). Returns its length (< size), or 0 for other data or when it does not fit.
size_t formatBytes(const Advert &advert, char *out, size_t size);

} // namespace scan_dump
