// LUMIX camera protocol (docs/PROTOKOL.md), without hardware dependencies (tested on the PC).
// So far: reading the camera's advertisement.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lumix {

// Panasonic's Bluetooth SIG company identifier, the first two bytes of its
// manufacturer specific data (little-endian: 3a 00).
constexpr uint16_t PANASONIC_COMPANY_ID = 0x003A;

constexpr size_t SERIAL_LENGTH = 5;

// Camera state byte in the advertisement.
constexpr uint8_t STATE_ON = 0x00;
constexpr uint8_t STATE_PAIRING = 0x03; // "Add New Device"
constexpr uint8_t STATE_STANDBY = 0x06; // switched off; connecting would wake it up

struct Camera {
    uint8_t state;
    uint8_t serial[SERIAL_LENGTH];
};

enum class Advert {
    None,           // no Panasonic manufacturer data, or malformed advertising data
    Camera,         // a LUMIX camera: state and serial number read
    OtherPanasonic, // Panasonic manufacturer data in another layout
};

// Reads advertising or scan response data: AD structures of [length][type][data], where
// length covers type and data (Bluetooth Core Specification Vol 3, Part C, 11). The
// Panasonic manufacturer data (type 0xFF) of a camera is exactly
// 3a 00 <state> <5-byte serial number>. *camera is written only for Advert::Camera.
Advert parseAdvert(const uint8_t *data, size_t length, Camera *camera);

// "on", "pairing", "standby" or "unknown".
const char *stateName(uint8_t state);

// Writes the serial number as 10 lower-case hex digits (as the gpxcam app shows it).
// Returns its length, or 0 if it does not fit in out together with the terminator.
size_t formatSerial(const uint8_t serial[SERIAL_LENGTH], char *out, size_t size);

} // namespace lumix
