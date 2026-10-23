#include "battery_rules.h"

namespace battery_rules {

namespace {

// Meshtastic's OCV_ARRAY for the T1000-E: the resting voltage at 100, 90, ... 0 %.
constexpr uint16_t DISCHARGE_CURVE_MV[] = {4190, 4042, 3957, 3885, 3820, 3776, 3746, 3725, 3696, 3644, 3100};
constexpr uint32_t CURVE_STEPS = sizeof(DISCHARGE_CURVE_MV) / sizeof(DISCHARGE_CURVE_MV[0]) - 1;
constexpr uint32_t PERCENT_PER_STEP = 100 / CURVE_STEPS;
static_assert(PERCENT_PER_STEP * CURVE_STEPS == 100, "a point every 10 %");

// Meshtastic's state of charge, 0..100 %.
constexpr uint32_t stateOfCharge(uint32_t millivolts)
{
    for (uint32_t i = 0; i <= CURVE_STEPS; i++) {
        if (millivolts < DISCHARGE_CURVE_MV[i])
            continue;
        if (i == 0)
            return 100;
        const uint32_t above = millivolts - DISCHARGE_CURVE_MV[i];
        const uint32_t span = DISCHARGE_CURVE_MV[i - 1] - DISCHARGE_CURVE_MV[i];
        return PERCENT_PER_STEP * (CURVE_STEPS - i) + PERCENT_PER_STEP * above / span;
    }
    return 0;
}

constexpr uint32_t STATE_AT_POWER_OFF = stateOfCharge(LOW_MV);
static_assert(STATE_AT_POWER_OFF < 100, "the power-off voltage is below a full battery");

} // namespace

uint8_t percentFromMillivolts(uint32_t millivolts)
{
    const uint32_t state = stateOfCharge(millivolts);
    if (state <= STATE_AT_POWER_OFF)
        return 0;
    return (uint8_t)((state - STATE_AT_POWER_OFF) * 100 / (100 - STATE_AT_POWER_OFF));
}

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
