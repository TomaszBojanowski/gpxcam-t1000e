// Why a session of either dialect (newer_session, older_session) ended: pairing and link_control
// take both dialects in one place (camera_connection::endReason()). No hardware dependencies.
#pragma once

#include <stdint.h>

namespace session_end {

enum class Reason : uint8_t {
    Accepted, // the camera took the card
    // The camera refused a write that shows it does not know the card. Newer dialect: the time, or
    // with the clock off any write of the introduction (link_policy::refusalMeansUnknownCard());
    // older dialect: any write of the introduction or the time.
    Refused,
    NotTaken,       // older dialect: the camera sent no 01 in time (it may only be slow)
    NoAnswer,       // no connection within camera_link::CONNECT_TIMEOUT_MS
    // HCI 0x3e: the camera sent nothing in the first connection events. The S5II does so right after it
    // was switched on, and now and then otherwise: a quick second attempt usually works.
    NotEstablished,
    EndedEarly, // the camera ended the connection after it was made, before the session ended
    // Newer dialect: the service search ended with "attribute not found" or found no newer-dialect service
    // (1FE71B85); the camera speaks only the older dialect, so 'pair' switches to it. Another error is Failed.
    NoNewerService,
    Failed,         // anything else: a request refused or unanswered, something missing, no GPS time
};

} // namespace session_end
