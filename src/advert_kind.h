// How a received advertisement may be answered, from the SoftDevice's report type (ble_gap.h,
// ble_gap_adv_report_type_t: connectable, scannable, scan_response). The card can connect only on a
// connectable advertisement (ADV_IND); a scan-only one (ADV_SCAN_IND) answers a scan request but
// takes no connection (Bluetooth Core Specification Vol 6, Part B, 2.3.1). For the logs of the
// camera list and of 'diag scan'. No hardware dependencies (tested on the PC).
#pragma once

#include <stdint.h>

namespace advert_kind {

enum class Kind : uint8_t { Connectable, ScanOnly, NonConnectable, ScanResponse };

inline Kind fromReport(bool connectable, bool scannable, bool scanResponse)
{
    if (scanResponse)
        return Kind::ScanResponse;
    if (connectable)
        return Kind::Connectable;
    return scannable ? Kind::ScanOnly : Kind::NonConnectable;
}

// "connectable", "scan-only", "non-connectable" or "scan response".
inline const char *name(Kind kind)
{
    switch (kind) {
    case Kind::Connectable:
        return "connectable";
    case Kind::ScanOnly:
        return "scan-only";
    case Kind::NonConnectable:
        return "non-connectable";
    case Kind::ScanResponse:
        return "scan response";
    }
    return "?";
}

} // namespace advert_kind
