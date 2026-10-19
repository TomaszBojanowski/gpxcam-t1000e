// Battery voltage and charger state.
#pragma once

#include <stdint.h>

namespace battery {

// Wait after switching on P1.06 (which powers the battery divider) before sampling.
// Checked on the card with 'diag bat' (M1 step 3).
constexpr uint32_t SETTLE_MS = 10;

enum class Charger {
    NoUsb,       // no VBUS: running on the battery
    Charging,    // P1.03 low
    Charged,     // P1.04 low
    NotCharging, // VBUS present, neither status line active
};

struct Status {
    uint32_t batteryMv;
    Charger charger;
};

// Battery voltage in mV. Switches P1.06 on for the measurement only.
uint32_t measureMillivolts(uint32_t settleMs = SETTLE_MS);

// USB supply present (VBUS detected by the nRF52840).
bool usbPowered();

// Battery voltage and charger state.
Status read();

const char *chargerText(Charger charger);

} // namespace battery
