// What both introductions (newer_session, older_session) do alike with the one connection to the
// camera: starting it, the time limit of each step, the MTU exchange, the events that may come in any
// step, the time packet, and how the last session ended. Each session keeps its own steps, log lines
// and failure texts. One session at a time has the connection. Loop task only.
#pragma once

#include <stdarg.h>
#include <stdint.h>

#include "camera_link.h"
#include "camera_list.h"
#include "lumix.h"
#include "posix_tz.h"
#include "session_end.h"

namespace camera_connection {

// The name the card gives itself in both dialects: the camera lists it.
constexpr const char *DEVICE_NAME = "T1000-E";

// Starts connecting for a session, unless another session has the connection, the camera is in
// standby (connecting would wake it), or an earlier connection is still open (closed now, so that the
// next start can connect). False if not; failure() says why. The session has the connection until
// release() or cancel(); its first step, connecting, has camera_link::CONNECT_TIMEOUT_MS.
bool start(const camera_list::Report &camera);

// The session no longer has the connection: it ended, or gave its lasting connection to the link.
void release();

// Ends the session's connection at once: stops connecting or disconnects without waiting; releases.
void cancel();

// How the last session ended, set by the session: why (Failed until it says otherwise, and after a
// cancel) and the text for the caller's log, e.g. "time: refused (GATT 0x0080)".
session_end::Reason endReason();
const char *failure();
void setEndReason(session_end::Reason reason);
void setFailure(const char *format, ...) __attribute__((format(printf, 1, 2)));
void setFailureV(const char *format, va_list args);

// A step of the session starts, with its time limit.
void enter(uint32_t limitMs);
bool overLimit(); // the step's time limit has passed
uint32_t limitSeconds();

// The step after connecting: the card asks for camera_link::MAX_MTU. False if the SoftDevice refused.
bool requestMtu();
// The MTU of the connection from the camera's answer (MtuExchanged); the default 23 when it failed.
uint16_t mtu(const camera_link::Event &event);

// True if the event is the camera asking for encryption: logged, and the card does not answer it.
bool securityRequest(const camera_link::Event &event);

// Why the camera's disconnect ends the session: HCI 0x3e is a connection never established; another
// end of a connection that was made is an early one.
session_end::Reason disconnectedReason(uint16_t hciReason, bool connecting);

// The time of this moment in the zone of 'tz' (tz_store), as the camera takes it (lumix::timeData),
// into data: from a fresh GPS time, or, unless lastFixMaxMs is FRESH_TIME_ONLY, from the last fix up to
// that old (gps::utcSinceFix()). False without either.
constexpr uint32_t FRESH_TIME_ONLY = 0;
struct TimeSource {
    posix_tz::CameraZone zone;
    bool fromLastFix;
    uint32_t fixAgeMs; // with fromLastFix
};
bool timePacket(uint32_t lastFixMaxMs, uint8_t data[lumix::TIME_LENGTH], TimeSource *source);

} // namespace camera_connection
