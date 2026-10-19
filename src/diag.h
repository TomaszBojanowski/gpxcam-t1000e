// Read-only hardware diagnostics for milestone M1 (battery measurement, reset
// reasons). Nothing here changes flash or enters a sleep mode. The button check
// was done in M1 step 1; its results are in docs/SPRZET.md.
#pragma once

namespace diag {

// Reset reason (RESETREAS), VBUS, regulator mode and the RESET pin setting.
void logResetInfo();

// Battery input (AIN0) with the sensor supply P1.06 off, on and off again,
// plus VDD, VDDH and the charger status pins.
void battery();

// P0.04 as an analog input only (never driven), with P1.06 off and on.
void p004();

} // namespace diag
