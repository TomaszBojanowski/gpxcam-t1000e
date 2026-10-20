// LUMIX camera protocol (docs/PROTOCOL.md), without hardware dependencies (tested on the PC):
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

// ---- introducing the card (docs/PROTOCOL.md, "Byte formats") ----------------------

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

// ---- location (docs/PROTOCOL.md, "Byte formats", location) ----------------------------------------

constexpr size_t LOCATION_LENGTH = 16;
constexpr int64_t GPS_EPOCH_UNIX_SECONDS = 315964800; // 1980-01-06 00:00:00 UTC

// Location, little-endian: GPS time u32 (Unix seconds - GPS_EPOCH_UNIX_SECONDS, no leap
// seconds; 0 before the GPS epoch), latitude and longitude i32 in degrees * 10^7, altitude
// i16 in metres (rounded half away from zero, limited to the i16 range), 0x41 ('A',
// valid position), 0x00. The time must be the moment of sending, not of the fix.
void locationData(int64_t unixSeconds, int32_t latitudeE7, int32_t longitudeE7, int32_t altitudeDm,
                  uint8_t out[LOCATION_LENGTH]);

// ---- GATT service and characteristics (docs/PROTOCOL.md), big-endian as written ------

// 1FE71B85-8344-4E6A-BF1A-AC7ACDB3F8AF
constexpr uint8_t SERVICE_UUID[UUID_LENGTH] = {0x1f, 0xe7, 0x1b, 0x85, 0x83, 0x44, 0x4e, 0x6a,
                                               0xbf, 0x1a, 0xac, 0x7a, 0xcd, 0xb3, 0xf8, 0xaf};
// 239C7203-922F-43E9-BDF1-6D1FEDF6F43F: key, read, 4 bytes
constexpr uint8_t KEY_UUID[UUID_LENGTH] = {0x23, 0x9c, 0x72, 0x03, 0x92, 0x2f, 0x43, 0xe9,
                                           0xbd, 0xf1, 0x6d, 0x1f, 0xed, 0xf6, 0xf4, 0x3f};
// BFF12E7F-5944-4EB6-A90F-48D8786157EA: device identifier, write
constexpr uint8_t DEVICE_ID_UUID[UUID_LENGTH] = {0xbf, 0xf1, 0x2e, 0x7f, 0x59, 0x44, 0x4e, 0xb6,
                                                 0xa9, 0x0f, 0x48, 0xd8, 0x78, 0x61, 0x57, 0xea};
// ED8B1352-5AA7-4155-B7C9-521292DD1932: app name, write
constexpr uint8_t APP_NAME_UUID[UUID_LENGTH] = {0xed, 0x8b, 0x13, 0x52, 0x5a, 0xa7, 0x41, 0x55,
                                                0xb7, 0xc9, 0x52, 0x12, 0x92, 0xdd, 0x19, 0x32};
// 05A797B6-9C2F-4D18-99A7-FA61A3880BFE: device name, write
constexpr uint8_t DEVICE_NAME_UUID[UUID_LENGTH] = {0x05, 0xa7, 0x97, 0xb6, 0x9c, 0x2f, 0x4d, 0x18,
                                                   0x99, 0xa7, 0xfa, 0x61, 0xa3, 0x88, 0x0b, 0xfe};
// 39A02C41-C6E3-4B73-A3EA-537F23F0085A: date and time, write
constexpr uint8_t TIME_UUID[UUID_LENGTH] = {0x39, 0xa0, 0x2c, 0x41, 0xc6, 0xe3, 0x4b, 0x73,
                                            0xa3, 0xea, 0x53, 0x7f, 0x23, 0xf0, 0x08, 0x5a};
// C289B6A4-B9EE-49AB-AB06-7099BB5EDAA8: GPS mode of the camera, write 01 on / 02 off
constexpr uint8_t GPS_MODE_UUID[UUID_LENGTH] = {0xc2, 0x89, 0xb6, 0xa4, 0xb9, 0xee, 0x49, 0xab,
                                                0xab, 0x06, 0x70, 0x99, 0xbb, 0x5e, 0xda, 0xa8};
// 437E6512-321A-4A94-9281-FAB1BFF49A91: notifications (the camera asks for a location)
constexpr uint8_t GPS_NOTIFY_UUID[UUID_LENGTH] = {0x43, 0x7e, 0x65, 0x12, 0x32, 0x1a, 0x4a, 0x94,
                                                  0x92, 0x81, 0xfa, 0xb1, 0xbf, 0xf4, 0x9a, 0x91};
// 565B4253-8BAC-46AE-8C5D-EC6F94FC8714: location, write 16 bytes
constexpr uint8_t LOCATION_UUID[UUID_LENGTH] = {0x56, 0x5b, 0x42, 0x53, 0x8b, 0xac, 0x46, 0xae,
                                                0x8c, 0x5d, 0xec, 0x6f, 0x94, 0xfc, 0x87, 0x14};

constexpr uint8_t GPS_MODE_ON = 0x01;

} // namespace lumix
