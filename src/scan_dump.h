// 'diag scan': a short dump of the advertisements the card hears, so an unknown camera's dialect
// can be told from its advertisement (newer, older, or neither) before anyone pairs it. Only close
// reports are shown (a threshold), the full bytes only for Panasonic manufacturer data and a short
// summary for anything else, with the address masked so a tester can send the output as is. The
// formatting has no hardware dependencies (tested on the PC); bluetooth.cpp runs the scan and calls
// formatAdvert from the loop task.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "advert_kind.h"

namespace scan_dump {

constexpr int8_t RSSI_THRESHOLD_DBM = -65; // only devices this close or closer are shown
constexpr uint32_t DURATION_MS = 30000;    // one run lasts this long
constexpr size_t MAX_ADVERT_BYTES = 31;    // a legacy advertising payload
constexpr size_t ADDRESS_LENGTH = 6;

// One received advertisement or scan response.
struct Advert {
    uint8_t address[ADDRESS_LENGTH]; // as the SoftDevice gives it: least significant byte first
    uint8_t addressType;
    int8_t rssi; // dBm
    advert_kind::Kind kind;
    uint8_t length; // bytes of data in use, at most MAX_ADVERT_BYTES
    uint8_t data[MAX_ADVERT_BYTES];
};

// The body of one "diag scan:" log line for the advert: the address with all but its last byte
// hidden (**:**:**:**:**:xx, as the logs are masked), the address type, RSSI, "rsp" for a scan
// response or "adv" and the advertisement's kind (connectable, scan-only, non-connectable), and the
// length. Panasonic manufacturer data ("camera" for a
// LUMIX camera, "Panasonic" otherwise) is then printed in full; anything else has no bytes.
// Returns the length written (< size), or 0 if it does not fit.
size_t formatAdvert(const Advert &advert, char *out, size_t size);

} // namespace scan_dump
