#include "lumix.h"

#include <string.h>

namespace lumix {

namespace {

// Bluetooth Core Specification Supplement, Part A, 1.4: manufacturer specific data starts
// with the 2-byte company identifier.
constexpr uint8_t AD_TYPE_MANUFACTURER_DATA = 0xFF;
constexpr size_t COMPANY_ID_LENGTH = 2;
constexpr size_t CAMERA_DATA_LENGTH = 1 + SERIAL_LENGTH; // state + serial number

constexpr uint8_t DEVICE_ID_END[] = {0xff, 0xff, 0xff, 0xcd};
constexpr uint8_t DEVICE_NAME_END[] = {0xff, 0xff, 0xff, 0x00};
constexpr size_t WORD = 4;

// Number of leading bytes of a UTF-8 text that fit in maxBytes without splitting a
// character (continuation bytes are 10xxxxxx).
size_t utf8Prefix(const char *text, size_t maxBytes)
{
    const size_t length = strlen(text);
    if (length <= maxBytes)
        return length;
    size_t n = maxBytes;
    while (n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80)
        n--;
    return n;
}

void putU16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

void putU32(uint8_t *out, uint32_t value)
{
    for (size_t i = 0; i < 4; i++)
        out[i] = (uint8_t)(value >> (8 * i));
}

constexpr uint8_t LOCATION_VALID = 0x41; // 'A'
constexpr int32_t DM_PER_METRE = 10;

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

// The states of the older Panasonic app's own table (docs/PROTOCOL.md); only 03 means pairing.
const char *stateName(uint8_t state)
{
    switch (state) {
    case STATE_ON:
        return "on";
    case 0x01:
        return "asleep";
    case 0x02:
        return "waking up";
    case STATE_PAIRING:
        return "pairing";
    case 0x04:
        return "off";
    case 0x05:
        return "on, asleep";
    case STATE_STANDBY:
        return "standby";
    case 0x07:
        return "on, asleep (quick wake)";
    case 0x08:
        return "on, asleep (auto transfer)";
    case 0x09:
        return "standby (auto transfer)";
    case 0x0A:
        return "on, asleep (quick wake, auto transfer)";
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

size_t wordSwap(const uint8_t *in, size_t length, uint8_t *out, size_t outSize)
{
    const size_t padded = (length + WORD - 1) / WORD * WORD;
    if (padded > outSize)
        return 0;
    for (size_t i = 0; i < padded; i++) {
        const size_t source = i / WORD * WORD + (WORD - 1 - i % WORD);
        out[i] = source < length ? in[source] : 0;
    }
    return padded;
}

void applyKey(uint8_t *data, size_t length, const uint8_t key[KEY_LENGTH])
{
    for (size_t i = 0; i < length; i++)
        data[i] ^= key[i % KEY_LENGTH];
}

void deviceIdPlain(const uint8_t uuid[UUID_LENGTH], uint8_t out[DEVICE_ID_LENGTH])
{
    wordSwap(uuid, UUID_LENGTH, out, DEVICE_ID_LENGTH);
    memcpy(&out[UUID_LENGTH], DEVICE_ID_END, sizeof(DEVICE_ID_END));
}

size_t deviceNamePlain(const char *name, uint8_t out[DEVICE_NAME_MAX_LENGTH])
{
    uint8_t text[1 + DEVICE_NAME_TEXT_MAX];
    const size_t textLength = utf8Prefix(name, DEVICE_NAME_TEXT_MAX);
    text[0] = (uint8_t)textLength;
    memcpy(&text[1], name, textLength);
    const size_t swapped = wordSwap(text, 1 + textLength, out, DEVICE_NAME_MAX_LENGTH - sizeof(DEVICE_NAME_END));
    memcpy(&out[swapped], DEVICE_NAME_END, sizeof(DEVICE_NAME_END));
    return swapped + sizeof(DEVICE_NAME_END);
}

void appName(const char *name, uint8_t out[APP_NAME_LENGTH])
{
    const size_t length = utf8Prefix(name, APP_NAME_LENGTH);
    memset(out, 0, APP_NAME_LENGTH);
    memcpy(out, name, length);
}

void locationData(int64_t unixSeconds, int32_t latitudeE7, int32_t longitudeE7, int32_t altitudeDm,
                  uint8_t out[LOCATION_LENGTH])
{
    const int64_t gpsSeconds = unixSeconds > GPS_EPOCH_UNIX_SECONDS ? unixSeconds - GPS_EPOCH_UNIX_SECONDS : 0;
    const int32_t half = DM_PER_METRE / 2;
    int32_t metres = (altitudeDm >= 0 ? altitudeDm + half : altitudeDm - half) / DM_PER_METRE;
    if (metres > INT16_MAX)
        metres = INT16_MAX;
    if (metres < INT16_MIN)
        metres = INT16_MIN;
    putU32(&out[0], (uint32_t)gpsSeconds);
    putU32(&out[4], (uint32_t)latitudeE7);
    putU32(&out[8], (uint32_t)longitudeE7);
    putU16(&out[12], (uint16_t)(int16_t)metres);
    out[14] = LOCATION_VALID;
    out[15] = 0x00;
}

void timeData(const utc::DateTime &utcNow, int16_t standardOffsetMinutes, bool summerTime,
              uint8_t out[TIME_LENGTH])
{
    putU16(&out[0], utcNow.year);
    out[2] = utcNow.month;
    out[3] = utcNow.day;
    out[4] = utcNow.hour;
    out[5] = utcNow.minute;
    out[6] = utcNow.second;
    putU16(&out[7], (uint16_t)standardOffsetMinutes);
    out[9] = summerTime ? 1 : 0;
}

} // namespace lumix
