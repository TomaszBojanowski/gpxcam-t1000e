#include "battery_rules.h"

namespace battery_rules {

bool powerOffForLowBattery(uint32_t millivolts, bool usb, bool atStart, uint8_t *lowReadings)
{
    if (usb || millivolts >= LOW_MV) {
        *lowReadings = 0;
        return false;
    }
    if (*lowReadings < UINT8_MAX)
        (*lowReadings)++;
    return atStart || *lowReadings >= LOW_READINGS_TO_POWER_OFF;
}

void encode(const Entry &entry, uint8_t out[ENTRY_SIZE])
{
    for (size_t i = 0; i < 4; i++)
        out[i] = (uint8_t)(entry.uptimeMinutes >> (8 * i));
    out[4] = (uint8_t)entry.millivolts;
    out[5] = (uint8_t)(entry.millivolts >> 8);
    out[6] = entry.activity;
    out[7] = entry.flags;
}

Entry decode(const uint8_t in[ENTRY_SIZE])
{
    Entry entry;
    entry.uptimeMinutes = (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
    entry.millivolts = (uint16_t)(in[4] | in[5] << 8);
    entry.activity = in[6];
    entry.flags = in[7];
    return entry;
}

} // namespace battery_rules
