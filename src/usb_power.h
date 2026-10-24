// A guard around TinyUSB's handler of the USB power events (detected, ready, removed), against
// the hang of 2026-10-24.
//
// tusb_hal_nrf_power_event() (Adafruit_TinyUSB_Arduino 3.1.3, src/portable/nordic/nrf5x/
// dcd_nrf5x.c) waits without a limit, on the READY event, first for the USBD peripheral to be
// ready (EVENTCAUSE.READY), then for the high-frequency clock. When the cable bounces, READY can
// be handled after REMOVED has disabled the USBD and released the clock: neither ever comes,
// and the Bluefruit SOC task (priority 3), which calls the handler, spins for good. The loop
// (priority 1) never runs again, and flash writes, whose completion events the same task
// delivers, never end. The waits are the same in TinyUSB master (2026-10) and in
// Adafruit_TinyUSB_Arduino 3.7.7; meshtastic/firmware#8865 saw the hang at the same line.
//
// The linker sends every call of the handler to __wrap_tusb_hal_nrf_power_event() here
// (platformio.ini: -Wl,--wrap=tusb_hal_nrf_power_event). For READY it checks that the USBD is
// still enabled and VBUS still there, then waits for the USBD and the clock with a limit; only
// then does the original run, and its waits end at once. Otherwise READY is skipped: REMOVED
// follows, and the next plug-in starts again with DETECTED. A READY the original would finish
// runs as before; the other events pass unchanged. The watchdog stays the last resort.
#pragma once

namespace usb_power {

// 'p': "usb: power events guarded, 0 READY skipped" (since start).
void logState();

} // namespace usb_power
