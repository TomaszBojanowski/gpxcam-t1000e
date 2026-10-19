// One connection in which the card introduces itself to the camera, in the order of
// docs/PROTOKOL.md ("Kolejność po każdym połączeniu"): connect, MTU, service and
// characteristics, key, identifier, app name, device name, time. Every step has a time
// limit and starts only after the previous one succeeded; a refused time means the camera
// does not know the card. The connection is closed at the end either way.
// Loop task only.
#pragma once

#include "camera_list.h"
#include "lumix.h"

namespace camera_session {

enum class Result { Running, Accepted, Failed };

// Starts with a camera seen in the scan; scanning must be off. Never connects to a camera
// in standby (state 06). False if it could not start; failure() says why.
bool start(const camera_list::Report &camera, const uint8_t cardUuid[lumix::UUID_LENGTH]);

// Advances the session. Call from loop() while it returns Running.
Result poll();

// Why the last session failed, e.g. "time: refused (GATT 0x0080)".
const char *failure();

// Ends a running session at once: stops connecting or disconnects without waiting.
void cancel();

bool active();

} // namespace camera_session
