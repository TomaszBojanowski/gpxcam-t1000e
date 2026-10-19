// Rules of the normal work with the paired camera (M5): when to connect, how long to wait
// after a disconnect and when to send a location. No hardware dependencies (tested on the PC).
#pragma once

#include <stdint.h>

#include "camera_list.h"
#include "pairing_record.h"

namespace link_policy {

// Connect only on a report this recent, so its state is current.
constexpr uint32_t REPORT_FRESH_MS = 1000;

// True only for the paired camera (the same serial number), switched on (state 00), with
// a report at most REPORT_FRESH_MS old. Never in standby (06) or pairing (03), never to
// another camera.
bool shouldConnect(const pairing_record::Record &pairing, const camera_list::Entry &camera, uint32_t nowMs);

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

// Distance in metres between two positions in degrees * 10^7 (equirectangular
// approximation; good to well under 1 % for the short distances it is used for).
uint32_t distanceMetres(int32_t latitude1E7, int32_t longitude1E7, int32_t latitude2E7, int32_t longitude2E7);

} // namespace link_policy
