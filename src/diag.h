// Read-only hardware diagnostics (reset reason, battery divider settling) and the watchdog test.
// Nothing here changes flash or enters a sleep mode. The one-off checks of M1
// step 1 (button, P0.04, P0.05, VDDH) are documented in docs/HARDWARE.md.
#pragma once

namespace diag {

// Reset reason (RESETREAS), VBUS, regulator mode and the RESET pin setting.
void logResetInfo();

// Battery voltage after different waits since switching P1.06 on (0..50 ms, with
// P1.06 off for 0.5 s before each), then the normal reading. Blocks for about 4 s.
void battery();

// Test of the watchdog: holds the loop without feeding it, as a hang would (the USB and BLE
// tasks keep running). The watchdog restarts the card after watchdog::TIMEOUT_MS; this returns,
// with a log line, only if it did not, after twice that time.
void hang();

} // namespace diag
