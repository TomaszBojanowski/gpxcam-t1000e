// The nRF52840 watchdog (PS v1.1 6.36): restarts the card when the main loop stops running.
// On 2026-10-24 the card hung after a bouncing USB cable: a busy wait in TinyUSB's USB power
// event (dcd_nrf5x.c) kept the Bluefruit SOC task, of a higher priority, running, so the loop
// never ran again (no LED, no button) until a pin reset.
//
// Facts it relies on:
//  - Once started it cannot be stopped; only a watchdog, pin, brownout or power-on reset stops
//    it (PS 5.3.6.8). A soft reset ('dfu', the bootloader starting the program after flashing)
//    leaves it running.
//  - The UF2 bootloader feeds a running watchdog in its DFU loop (Adafruit_nRF52_Bootloader,
//    lib/sdk11/components/libraries/bootloader_dfu/bootloader.c, wait_for_events()).
//  - No watchdog reset in System OFF (PS 5.3.6.8, footnote 9); after the wake-up it is set up
//    again (PS 6.36.3).
#pragma once

#include <stdint.h>

namespace watchdog {

// Far above the longest wait in the loop (gps::off() up to about 9.5 s with its sound and the module's
// answers, gps.h; switching off with the button, the melody and gps::off() about 17 s; 'diag bat' about
// 4 s), short enough that a hung card comes back soon.
constexpr uint32_t TIMEOUT_S = 30;
constexpr uint32_t TIMEOUT_MS = TIMEOUT_S * 1000;

// Starts it; one already running (after a soft reset) is kept and fed. Call in setup() right after
// board::init().
void begin();

// Call on every pass of loop(), and only there: a loop that stops running stops the feeding.
void feed();

// True when the last reset came from the watchdog: the card had hung.
bool restartedLastTime();

// 'p': "watchdog: on, 30 s, started now" (or "kept running through the reset").
void logState();

} // namespace watchdog
