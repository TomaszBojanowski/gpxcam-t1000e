#include "lumix.h"

#include <string.h>

namespace lumix {

namespace {

// Bluetooth Core Specification Supplement, Part A, 1.4: manufacturer specific data starts
// with the 2-byte company identifier.
constexpr uint8_t AD_TYPE_MANUFACTURER_DATA = 0xFF;
constexpr size_t COMPANY_ID_LENGTH = 2;
constexpr size_t CAMERA_DATA_LENGTH = 1 + SERIAL_LENGTH; // state + serial number

// payload: the Panasonic manufacturer data after the company identifier.
Advert readCamera(const uint8_t *payload, size_t length, Camera *camera)
{
    if (length != CAMERA_DATA_LENGTH)
        return Advert::OtherPanasonic;
    camera->state = payload[0];
    memcpy(camera->serial, &payload[1], SERIAL_LENGTH);
    return Advert::Camera;
}

} // namespace

Advert parseAdvert(const uint8_t *data, size_t length, Camera *camera)
{
    if (data == nullptr)
        return Advert::None;
    size_t pos = 0;
    while (pos < length) {
        const size_t fieldLength = data[pos]; // type + data
        if (fieldLength == 0)
            break; // early end of the data, the rest is padding (Core Vol 3, Part C, 11)
        if (fieldLength > length - pos - 1)
            break; // runs past the end: malformed
        const uint8_t type = data[pos + 1];
        const uint8_t *value = &data[pos + 2];
        const size_t valueLength = fieldLength - 1;
        if (type == AD_TYPE_MANUFACTURER_DATA && valueLength >= COMPANY_ID_LENGTH &&
            (value[0] | value[1] << 8) == PANASONIC_COMPANY_ID)
            return readCamera(value + COMPANY_ID_LENGTH, valueLength - COMPANY_ID_LENGTH, camera);
        pos += 1 + fieldLength;
    }
    return Advert::None;
}

const char *stateName(uint8_t state)
{
    switch (state) {
    case STATE_ON:
        return "on";
    case STATE_PAIRING:
        return "pairing";
    case STATE_STANDBY:
        return "standby";
    default:
        return "unknown";
    }
}

size_t formatSerial(const uint8_t serial[SERIAL_LENGTH], char *out, size_t size)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    constexpr size_t TEXT_LENGTH = 2 * SERIAL_LENGTH;
    if (out == nullptr || size < TEXT_LENGTH + 1)
        return 0;
    for (size_t i = 0; i < SERIAL_LENGTH; i++) {
        out[2 * i] = HEX_DIGITS[serial[i] >> 4];
        out[2 * i + 1] = HEX_DIGITS[serial[i] & 0x0F];
    }
    out[TEXT_LENGTH] = '\0';
    return TEXT_LENGTH;
}

} // namespace lumix
