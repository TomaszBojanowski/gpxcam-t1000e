// The card's identity in the older dialect of LUMIX cameras (docs/PROTOCOL.md, "Older dialect: the
// card's identity"). At the first pairing the camera takes a device only when the identity it
// presents over Bluetooth is the one registered over the camera's Wi-Fi. The identity is a fixed
// 10-byte prefix and a 6-byte seed; the card's seed is the end of its own UUID (pairing_record.h),
// so the identity stays as long as the card keeps its pairing file. Over Bluetooth the identity
// and the device name are 20-byte fields in the byte order and XOR of the newer dialect
// (lumix::wordSwap, lumix::applyKey). No hardware dependencies (tested on the PC with the made-up
// values of test/device_identity_vectors.txt).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lumix.h"

namespace device_identity {

constexpr size_t SEED_LENGTH = 6;
constexpr size_t IDENTITY_LENGTH = 16; // the prefix and the seed
constexpr size_t TEXT_LENGTH = 36;     // 4D454930-0100-1000-8001-XXXXXXXXXXXX
constexpr size_t FIELD_LENGTH = 20;    // one Bluetooth write of the identity or of the name
constexpr size_t NAME_MAX_LENGTH = FIELD_LENGTH - 4; // without the length byte and ff ff ff

// The card's seed: the last SEED_LENGTH bytes of its UUID.
void cardSeed(const uint8_t cardUuid[lumix::UUID_LENGTH], uint8_t seed[SEED_LENGTH]);

// The identity: 4d 45 49 30 01 00 10 00 80 01 (ASCII "MEI0" and six bytes), then the seed.
void identityBytes(const uint8_t seed[SEED_LENGTH], uint8_t identity[IDENTITY_LENGTH]);

// The identity as text in upper case, the form registered over the camera's Wi-Fi. Returns
// TEXT_LENGTH, or 0 if it does not fit in out together with the terminator.
size_t identityText(const uint8_t seed[SEED_LENGTH], char *out, size_t size);

// The identity for the camera: xor(wordSwap(10 + identity + ff ff ff), key).
void identityField(const uint8_t identity[IDENTITY_LENGTH], const uint8_t key[lumix::KEY_LENGTH],
                   uint8_t out[FIELD_LENGTH]);

// The device name for the camera: xor(wordSwap([length] + name + 00... + ff ff ff), key). The name
// must be the one registered over Wi-Fi: printable ASCII, 1 to NAME_MAX_LENGTH characters. False,
// and out not written, for any other name.
bool nameField(const char *name, const uint8_t key[lumix::KEY_LENGTH], uint8_t out[FIELD_LENGTH]);

} // namespace device_identity
