// LUMIX camera protocol (docs/PROTOKOL.md), without hardware dependencies (tested on the PC):
// the camera's advertisement and the data the card writes when it introduces itself.
// Reference: the gpxcam app, android/.../LumixProtocol.kt.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "utc.h"

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

// ---- introducing the card (docs/PROTOKOL.md, "Formaty bajtów") ----------------------

constexpr size_t KEY_LENGTH = 4;   // read from the camera, different in every connection
constexpr size_t UUID_LENGTH = 16; // the card's identifier, big-endian as in its text form
constexpr size_t DEVICE_ID_LENGTH = 20;
constexpr size_t DEVICE_NAME_TEXT_MAX = 23; // UTF-8 bytes
constexpr size_t DEVICE_NAME_MAX_LENGTH = 28;
constexpr size_t APP_NAME_LENGTH = 32;
constexpr size_t TIME_LENGTH = 10;

// Pads the data with zeros to a multiple of 4 and reverses the byte order in every group
// of 4. Returns the output length, or 0 if it does not fit in outSize.
size_t wordSwap(const uint8_t *in, size_t length, uint8_t *out, size_t outSize);

// XOR with the 4-byte key repeated every 4 bytes, in place.
void applyKey(uint8_t *data, size_t length, const uint8_t key[KEY_LENGTH]);

// Identifier before applyKey(): wordSwap(uuid) + ff ff ff cd.
void deviceIdPlain(const uint8_t uuid[UUID_LENGTH], uint8_t out[DEVICE_ID_LENGTH]);

// Device name before applyKey(): wordSwap([length][UTF-8 text]) + ff ff ff 00. The text is
// cut to at most DEVICE_NAME_TEXT_MAX bytes between characters. Returns the length (8..28).
size_t deviceNamePlain(const char *name, uint8_t out[DEVICE_NAME_MAX_LENGTH]);

// App name in plain UTF-8, cut between characters and zero-padded to APP_NAME_LENGTH.
void appName(const char *name, uint8_t out[APP_NAME_LENGTH]);

// Date and time, little-endian: year (u16), month, day, hour, minute, second in UTC, the
// standard time-zone offset in minutes (i16) and the summer-time flag.
void timeData(const utc::DateTime &utcNow, int16_t standardOffsetMinutes, bool summerTime,
              uint8_t out[TIME_LENGTH]);

} // namespace lumix
