// The camera's characteristic handles kept between connections, without hardware
// dependencies (tested on the PC). RAM only, so after a restart the card always looks the
// handles up once. camera_session keeps them after an introduction the camera accepted and
// skips the service and characteristic discovery in the next session with the same camera
// (about 2.6 s of the 3.7 s introduction at the 48.75 ms interval).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lumix.h"

namespace handle_cache {

// The characteristics the card uses (camera_link::CHARACTERISTIC_COUNT, checked in camera_session.cpp).
constexpr size_t CHARACTERISTIC_COUNT = 8;

struct Handles {
    uint16_t serviceEnd;
    uint16_t valueHandle[CHARACTERISTIC_COUNT];
    uint8_t properties[CHARACTERISTIC_COUNT];
};

// Keeps the handles of the camera with this serial number, replacing any kept before.
void keep(const uint8_t serial[lumix::SERIAL_LENGTH], const Handles &handles);

enum class Lookup : uint8_t {
    Found,                // *handles are the camera's
    Empty,                // nothing kept
    OtherCameraForgotten, // the kept handles were another camera's; now forgotten
};

// For a session with the camera of this serial number.
Lookup find(const uint8_t serial[lumix::SERIAL_LENGTH], Handles *handles);

// Forgets the kept handles; true if there were any.
bool forget();

// Whether a failed session forgets the handles: an error once connected may come from a
// wrong handle. Without a connection (no answer, or HCI 0x3e: not established) no handle
// was used, so they stay.
bool forgetAfterFailure(bool connected, bool notEstablished);

} // namespace handle_cache
