// Read-only hardware diagnostics (reset reason, battery divider settling).
// Nothing here changes flash or enters a sleep mode. The one-off checks of M1
// step 1 (button, P0.04, P0.05, VDDH) are documented in docs/SPRZET.md.
#pragma once

namespace diag {

// Reset reason (RESETREAS), VBUS, regulator mode and the RESET pin setting.
void logResetInfo();

// Battery voltage after different waits since switching P1.06 on (0..50 ms, with
// P1.06 off for 0.5 s before each), then the normal reading. Blocks for about 4 s.
void battery();

} // namespace diag
