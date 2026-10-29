// How a session of either dialect (camera_session, older_session) ends: pairing and link_control
// take both dialects in one place. No hardware dependencies.
#pragma once

#include <stdint.h>

namespace session_end {

// What a session's poll() returns.
enum class Result : uint8_t { Running, Accepted, Rejected, Failed };

// Why the last session ended.
enum class Reason : uint8_t {
    Accepted, // the camera took the card
    // The camera refused a write that shows it does not know the card. Newer dialect: the time, or
    // with the clock off any write of the introduction (link_policy::refusalMeansUnknownCard());
    // older dialect: any write of the introduction or the time.
    Refused,
    NotTaken,       // older dialect: the camera sent no 01 in time (it may only be slow)
    NoAnswer,       // no connection within camera_link::CONNECT_TIMEOUT_MS
    NotEstablished, // HCI 0x3e: the camera sent nothing in the first connection events
    EndedEarly,     // the camera ended the connection after it was made, before the session ended
    NoNewerService, // newer dialect: the camera has no newer-dialect service (1FE71B85)
    Failed,         // anything else: a request refused or unanswered, something missing, no GPS time
};

constexpr Result resultOf(Reason reason)
{
    return reason == Reason::Accepted ? Result::Accepted
           : reason == Reason::Refused ? Result::Rejected
                                       : Result::Failed;
}

} // namespace session_end
