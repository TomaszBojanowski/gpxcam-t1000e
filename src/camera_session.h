// One connection in which the card introduces itself to the camera, in the order of
// docs/PROTOCOL.md ("Order after every connection"): connect, MTU, service and
// characteristics, key, identifier, app name, device name, time. Every step has a time
// limit and starts only after the previous one succeeded; a refused time means the camera
// does not know the card. For pairing the connection is closed at the end; for the normal
// work (M5) the time goes once more (the camera takes the time zone from the packet before
// the last one) and the connection stays open. Loop task only.
#pragma once

#include "camera_link.h"
#include "camera_list.h"
#include "lumix.h"

namespace camera_session {

// Rejected: a refusal that means the camera does not know the card: the time, or with the clock
// off any write of the introduction (link_policy::refusalMeansUnknownCard()).
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

// True if the last session failed because the connection was not established (HCI 0x3e,
// seen on the S5II right after it was switched on, and now and then otherwise): a quick
// second attempt usually works.
bool notEstablished();

const Discovered &discovered();

// True if the last session failed because the connection ended after it was made (not HCI
// 0x3e, a connection that was never made).
bool endedAfterConnecting();

// Whether the last session got no link at all (no answer, or HCI 0x3e), for pairing's quick retries.
camera_link::LinkFailure linkFailure();

// True if the last session failed because the camera has no newer-dialect service (1FE71B85): its
// search ended with "attribute not found" or found none. It speaks only the older dialect, so 'pair'
// switches to it (pairing.cpp). Another error of that search is a failed attempt only.
bool lacksNewerService();

// Whether the last session wrote the time: always when pairing; in a connection that stays
// open unless the clock is off ('clock off', tz_store). link_control judges refusals by it.
bool writesTime();

// The handles of the last camera are kept in RAM after an introduction it accepted, and the
// next session with the same camera skips the service and characteristic discovery
// (handle_cache). They are forgotten after an error in a session that had a connection, for
// another camera, and by this function (errors in the link's later steps); after a restart
// the card always looks them up. why: for the log.
void forgetHandles(const char *why);

// Ends a running session at once: stops connecting or disconnects without waiting.
void cancel();

} // namespace camera_session
