// Read-only hardware diagnostics for milestone M1 (button polarity, battery
// measurement, reset reasons). Nothing here changes flash or enters a sleep mode.
#pragma once

namespace diag {

// Reset reason (RESETREAS), VBUS, regulator mode and the RESET pin setting.
void logResetInfo();

// Starts sampling the button (P0.06) with no pull, pull-down and pull-up
// for BUTTON_DIAG_MS; results are logged from poll().
void startButton();

// Runs the button sampling started by startButton(). Call from loop().
void poll();

// Battery input (AIN0) with the sensor supply P1.06 off, on and off again,
// plus VDD, VDDH and the charger status pins.
void battery();

// P0.04 as an analog input only (never driven), with P1.06 off and on.
void p004();

} // namespace diag
