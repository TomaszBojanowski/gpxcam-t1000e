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

namespace older_session {

// Starts on a camera the caller chose; scanning must be off and the GPS must have time. keepOpen:
// the connection stays open for the caller once the camera accepts (the lasting link); false: the
// pairing, which cancels after Accepted. False if it could not start; camera_connection::failure() says why.
bool start(const camera_list::Report &camera, const uint8_t cardUuid[lumix::UUID_LENGTH], bool keepOpen);

// Advances the session; true while it introduces the card. Then camera_connection::endReason() says
// why it ended: Accepted when the camera took the card (pairing: the session keeps the connection until
// cancel(); keepOpen: the connection is the caller's), Refused when the camera refused a write of the
// introduction or the time, NotTaken when it sent no 01 in time (it may be slow; the link stops after a
// few in a row), or a connection problem.
bool poll();

// The handles of the last accepted connection: the location characteristic and its notifications
// (01: the camera knows the card, 02: it is switching off).
uint16_t locationHandle();
uint16_t locationNotifyHandle();

// Disconnects and stops; does nothing when idle.
void cancel();

// 'p': the step.
void logState();

} // namespace older_session
