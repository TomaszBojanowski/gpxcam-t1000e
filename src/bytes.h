// Little-endian 32-bit values in the records of the file system (tz_record, settings_record).
#pragma once

#include <stdint.h>

namespace bytes {

inline void putU32(uint8_t *out, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)(value >> (8 * i));
}

inline uint32_t getU32(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

} // namespace bytes
