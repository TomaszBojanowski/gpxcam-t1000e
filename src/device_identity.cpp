#include "device_identity.h"

#include <string.h>

namespace device_identity {

namespace {

constexpr size_t PREFIX_LENGTH = IDENTITY_LENGTH - SEED_LENGTH;
constexpr uint8_t PREFIX[PREFIX_LENGTH] = {0x4d, 0x45, 0x49, 0x30, 0x01, 0x00, 0x10, 0x00, 0x80, 0x01};
constexpr uint8_t FIELD_END[] = {0xff, 0xff, 0xff};
static_assert(1 + IDENTITY_LENGTH + sizeof(FIELD_END) == FIELD_LENGTH, "the identity field is full");

// Hyphens of the text form go before these bytes: 8-4-4-4-12 hex digits.
bool hyphenBefore(size_t byte)
{
    return byte == 4 || byte == 6 || byte == 8 || byte == 10;
}

bool printableAscii(char c)
{
    return c >= 0x20 && c <= 0x7e;
}

// The plain field in the camera's byte order and XORed with the key.
void encodeField(const uint8_t plain[FIELD_LENGTH], const uint8_t key[lumix::KEY_LENGTH], uint8_t out[FIELD_LENGTH])
{
    lumix::wordSwap(plain, FIELD_LENGTH, out, FIELD_LENGTH);
    lumix::applyKey(out, FIELD_LENGTH, key);
}

} // namespace

void cardSeed(const uint8_t cardUuid[lumix::UUID_LENGTH], uint8_t seed[SEED_LENGTH])
{
    memcpy(seed, &cardUuid[lumix::UUID_LENGTH - SEED_LENGTH], SEED_LENGTH);
}

void identityBytes(const uint8_t seed[SEED_LENGTH], uint8_t identity[IDENTITY_LENGTH])
{
    memcpy(identity, PREFIX, PREFIX_LENGTH);
    memcpy(&identity[PREFIX_LENGTH], seed, SEED_LENGTH);
}

size_t identityText(const uint8_t seed[SEED_LENGTH], char *out, size_t size)
{
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    if (out == nullptr || size < TEXT_LENGTH + 1)
        return 0;
    uint8_t identity[IDENTITY_LENGTH];
    identityBytes(seed, identity);
    size_t pos = 0;
    for (size_t i = 0; i < IDENTITY_LENGTH; i++) {
        if (hyphenBefore(i))
            out[pos++] = '-';
        out[pos++] = HEX_DIGITS[identity[i] >> 4];
        out[pos++] = HEX_DIGITS[identity[i] & 0x0F];
    }
    out[pos] = '\0';
    return pos;
}

void identityField(const uint8_t identity[IDENTITY_LENGTH], const uint8_t key[lumix::KEY_LENGTH],
                   uint8_t out[FIELD_LENGTH])
{
    uint8_t plain[FIELD_LENGTH];
    plain[0] = IDENTITY_LENGTH;
    memcpy(&plain[1], identity, IDENTITY_LENGTH);
    memcpy(&plain[1 + IDENTITY_LENGTH], FIELD_END, sizeof(FIELD_END));
    encodeField(plain, key, out);
}

bool nameField(const char *name, const uint8_t key[lumix::KEY_LENGTH], uint8_t out[FIELD_LENGTH])
{
    if (name == nullptr)
        return false;
    const size_t length = strnlen(name, NAME_MAX_LENGTH + 1);
    if (length == 0 || length > NAME_MAX_LENGTH)
        return false;
    for (size_t i = 0; i < length; i++)
        if (!printableAscii(name[i]))
            return false;
    uint8_t plain[FIELD_LENGTH] = {};
    plain[0] = (uint8_t)length;
    memcpy(&plain[1], name, length);
    memcpy(&plain[FIELD_LENGTH - sizeof(FIELD_END)], FIELD_END, sizeof(FIELD_END));
    encodeField(plain, key, out);
    return true;
}

} // namespace device_identity
