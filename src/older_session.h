// The introduction in the older LUMIX dialect (docs/PROTOCOL.md): the card introduces itself with its
// identity (device_identity) over the older characteristics (camera_link OLDER_*), waits for the
// camera to accept it and writes the time. The caller (pairing, link_control) finds the camera,
// keeps the GPS on and owns the link. Pairing (keepOpen false) waits up to 300 s for the one-time
// Wi-Fi step a computer does; a lasting connection (keepOpen true) hands the open connection to
// link_control, which sends the locations as in the newer dialect. Loop task only.
#pragma once

#include <stdint.h>

#include "camera_list.h"
#include "lumix.h"
#include "session_end.h"

namespace older_session {

using Result = session_end::Result;

// Starts on a camera the caller chose; scanning must be off and the GPS must have time. keepOpen:
// the connection stays open for the caller once the camera accepts (the lasting link); false: the
// pairing, which cancels after Accepted. False if it could not start; failure() says why.
bool start(const camera_list::Report &camera, const uint8_t cardUuid[lumix::UUID_LENGTH], bool keepOpen);

// Advances the session: Running while it introduces the card; Accepted when the camera takes it
// (pairing: until cancel(); keepOpen: once, and then the session is idle and the connection the
// caller's); Rejected when the camera refuses a write of the introduction or the time; Failed on a
// connection problem, and when the camera sends no 01 in time (it may be slow).
Result poll();

// Why the last session failed or was rejected (for the caller's log).
const char *failure();

// Why the last session ended, once poll() returned something other than Running. The camera did not
// take the card when it refused a write of the introduction or the time (Refused) or sent no 01 in time
// (NotTaken): the link stops after a few in a row.
session_end::Reason endReason();

// The handles of the last accepted connection: the location characteristic and its notifications
// (01: the camera knows the card, 02: it is switching off).
uint16_t locationHandle();
uint16_t locationNotifyHandle();

// Disconnects and stops; does nothing when idle.
void cancel();

// 'p': the step.
void logState();

} // namespace older_session
