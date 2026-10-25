// A connection in the older LUMIX dialect (docs/PROTOCOL.md): the card introduces itself with its
// identity (device_identity) over the older characteristics (camera_link OLDER_*), waits for the
// camera to accept it, writes the time and, in a lasting connection, sends a location every 5 s.
// A sub-session like camera_session: the caller (pairing, link_control) finds the camera, keeps the
// GPS on and owns the link; this does not search, touch the GPS or suspend the link. Pairing
// (keepOpen false) waits up to 300 s for the one-time Wi-Fi step a computer does, and ends once the
// camera accepts; a lasting connection (keepOpen true) keeps sending. Loop task only.
#pragma once

#include <stdint.h>

#include "camera_list.h"
#include "lumix.h"

namespace older_session {

enum class Result { Running, Accepted, Rejected, Failed };

// Starts on a camera the caller chose; scanning must be off and the GPS must have time. keepOpen:
// the connection sends locations after the camera accepts (the lasting link); false ends once the
// camera accepts (pairing). False if it could not start; failure() says why.
bool start(const camera_list::Report &camera, const uint8_t cardUuid[lumix::UUID_LENGTH], bool keepOpen);

// Advances the session: Running while it introduces and then (keepOpen) sends; Accepted once when
// the camera takes the card; Rejected when the camera does not; Failed on a connection problem.
Result poll();

// Why the last session failed or was rejected (for the caller's log).
const char *failure();

// Disconnects and stops; does nothing when idle.
void cancel();

bool active();

// 'p': the step and the locations written.
void logState();

} // namespace older_session
