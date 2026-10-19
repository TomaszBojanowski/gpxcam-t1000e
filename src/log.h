// Line-based log over USB serial that never blocks for long.
#pragma once

#include <stdint.h>

namespace logger {

// Longest line including the trailing "\r\n"; longer lines are truncated.
// Must stay below the 256-byte USB CDC TX FIFO (CFG_TUD_CDC_TX_BUFSIZE in
// Adafruit_TinyUSB_Arduino src/arduino/ports/nrf/tusb_config_nrf.h).
constexpr uint32_t LINE_MAX = 128;

// Formats one line (printf syntax, no newline needed) and sends it.
// If no terminal is open, or the TX buffer stays full for LOG_WAIT_MS,
// the line is dropped and counted instead of waiting.
// Not reentrant: call only from the Arduino loop task.
void line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Number of lines dropped since start.
uint32_t dropped();

} // namespace logger
