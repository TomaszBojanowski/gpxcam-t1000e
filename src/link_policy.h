// Rules of the normal work with the paired camera (M5): when to connect, how long to wait
// after a disconnect and when to send a location. No hardware dependencies (tested on the PC).
#pragma once

#include <stdint.h>

#include "camera_list.h"
#include "pairing_record.h"

namespace link_policy {

constexpr uint32_t MS_PER_MINUTE = 60000;

// Connect only on a report this recent, so its state is current.
constexpr uint32_t REPORT_FRESH_MS = 1000;

// Weaker reports are not worth a connection attempt (M6 decision; at -90 dBm one failed).
constexpr int8_t MIN_RSSI_DBM = -85;

// True only for the paired camera (the same serial number), switched on (state 00), with
// a report at most REPORT_FRESH_MS old and at least MIN_RSSI_DBM strong. Never in standby
// (06) or pairing (03), never to another camera.
bool shouldConnect(const pairing_record::Record &pairing, const camera_list::Entry &camera, uint32_t nowMs);

// How the card looks for the camera while not connected (M6 decisions). In both standby
// modes the camera's last report says standby (06) and the card only waits for state 00.
//   Normal      - scan 50 ms of every 100 ms, GPS on;
//   Rare        - the camera not seen for RARE_AFTER_MS: scan 100 ms every 2 s, GPS in Backup;
//   Standby     - the first LONG_STANDBY_AFTER_MS of standby (a break between photos):
//                 scan 100 ms every 1 s, the GPS stays on, so the location is there at once;
//   LongStandby - after that: scan 100 ms every 10 s, GPS in Backup.
enum class SearchMode { Normal, Rare, Standby, LongStandby };
constexpr uint32_t RARE_AFTER_MS = 5 * MS_PER_MINUTE;
constexpr uint32_t LONG_STANDBY_AFTER_MS = 20 * MS_PER_MINUTE; // a break between photos (10 min in M6)

// cameraSeen: a report of the paired camera since the link started; msSinceSeen and
// lastState: of its last report; msSearching: since the search started; msInStandby:
// since the camera went to standby.
SearchMode searchMode(bool cameraSeen, uint32_t msSinceSeen, uint8_t lastState, uint32_t msSearching,
                      uint32_t msInStandby);

// Whether the GPS may stay on in this mode. (It is switched on only in normal mode.)
bool gpsNeeded(SearchMode mode);

// Pause before looking for the camera again after a disconnect or a failed connection
// (M5 decision): 5 s, 15 s, then 60 s for every further one. failuresInARow starts at 1.
uint32_t pauseMs(uint32_t failuresInARow);

// A connection that lasted this long starts the pause sequence again.
constexpr uint32_t STABLE_CONNECTION_MS = 60000;

// Locations (M5 decisions, as the gpxcam app): every 25 s; earlier after moving more than
// 20 m or when the camera asks, but not more often than every 5 s.
constexpr uint32_t PERIOD_MS = 25000;
constexpr uint32_t MIN_GAP_MS = 5000;
constexpr uint32_t MOVE_METRES = 20;

enum class Send { No, First, Periodic, Moved, Asked };

// What to do now. sentBefore: a location was sent in this connection; msSinceLast: since
// that one; metresMoved: from the position sent then; cameraAsked: a notification came.
Send locationDue(bool sentBefore, uint32_t msSinceLast, uint32_t metresMoved, bool cameraAsked);

// The writes of the introduction (camera_session) and the first ones of the link, in order.
enum class IntroductionWrite { Identifier, AppName, DeviceName, Time, GpsMode, FirstLocation };

// Whether a GATT refusal of this write means that the camera does not know the card (never
// paired, or forgotten), so that the card stops connecting instead of trying again: after a
// few refused connections the S5II stops advertising. The S5II takes the identifier, app name
// and device name of an unknown device and refuses the time (gpxcam notes, 2026-10-14);
// without the time write ('clock off', timeWritten false) nothing shows it so clearly, so any
// refusal in the introduction counts.
bool refusalMeansUnknownCard(IntroductionWrite write, bool timeWritten);

// Distance in metres between two positions in degrees * 10^7 (equirectangular
// approximation; good to well under 1 % for the short distances it is used for).
uint32_t distanceMetres(int32_t latitude1E7, int32_t longitude1E7, int32_t latitude2E7, int32_t longitude2E7);

} // namespace link_policy
