// One connection in which the card introduces itself to the camera, in the order of
// docs/PROTOKOL.md ("Kolejność po każdym połączeniu"): connect, MTU, service and
// characteristics, key, identifier, app name, device name, time. Every step has a time
// limit and starts only after the previous one succeeded; a refused time means the camera
// does not know the card. For pairing the connection is closed at the end; for the normal
// work (M5) it stays open after an accepted time. Loop task only.
#pragma once

#include "camera_link.h"
#include "camera_list.h"
#include "lumix.h"

namespace camera_session {

// Rejected: the camera refused the time, it does not know the card.
enum class Result { Running, Accepted, Rejected, Failed };

// What the session found on the camera; valid after Accepted.
struct Discovered {
    uint16_t mtu;
    uint16_t serviceEnd;
    uint16_t valueHandle[camera_link::CHARACTERISTIC_COUNT]; // 0 = not found
    uint8_t properties[camera_link::CHARACTERISTIC_COUNT];
};

// Starts with a camera seen in the scan; scanning must be off. Never connects to a camera
// in standby (state 06). keepOpen: the connection stays open after an accepted time.
// False if it could not start; failure() says why.
bool start(const camera_list::Report &camera, const uint8_t cardUuid[lumix::UUID_LENGTH], bool keepOpen);

// Advances the session. Call from loop() while it returns Running.
Result poll();

// Why the last session failed, e.g. "time: refused (GATT 0x0080)".
const char *failure();

const Discovered &discovered();

// Ends a running session at once: stops connecting or disconnects without waiting.
void cancel();

bool active();

} // namespace camera_session
