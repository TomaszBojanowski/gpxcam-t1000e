// Read-only hardware diagnostics (reset reason, battery divider settling, the 64 MHz crystal)
// and the watchdog test.
// Nothing here changes flash or enters a sleep mode. The one-off checks of M1
// step 1 (button, P0.04, P0.05, VDDH) are documented in docs/HARDWARE.md.
#pragma once

#include <stdint.h>

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

// The 64 MHz crystal (HFXO): in how much of 1 s it runs (1000 samples), as a log line kept in
// the history. With the radio idle ('link off') only a request holds it: USB's while the cable
// is in, none once it is out. After delayS seconds (up to CLOCK_CHECK_MAX_DELAY_S), so that it
// can be measured on the battery and read with 'log' afterwards. Blocks for 1 s when it runs.
constexpr uint32_t CLOCK_CHECK_MAX_DELAY_S = 600;
void checkClock(uint32_t delayS);

// Runs a crystal check that is due. Call from loop().
void poll();

#ifdef GPXCAM_TEST_COMMANDS
// Test builds only (platformio.ini, env t1000e_test): zeroes the start of both copies of
// littlefs's superblock, so that the file system no longer mounts, and restarts. The rest of the
// area keeps its data, so it is not the empty area of a new card (storage.h): the damage of
// 2026-10-24, for testing storage.h and pairing_copy.h on the card.
void breakFileSystem();
#endif

} // namespace diag
