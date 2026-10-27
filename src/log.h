// Line-based log over USB serial that never blocks for long, with a small
// history of recent lines that survives periods without a terminal (e.g. on battery).
#pragma once

#include <stdint.h>

namespace logger {

// Longest line including the trailing "\r\n"; longer lines are truncated.
// Must stay below the 256-byte USB CDC TX FIFO (CFG_TUD_CDC_TX_BUFSIZE in
// Adafruit_TinyUSB_Arduino src/arduino/ports/nrf/tusb_config_nrf.h).
constexpr uint32_t LINE_MAX = 128;
// The longest text of a line: LINE_MAX less "\r\n" and the terminator (log.cpp, format()).
constexpr uint32_t TEXT_MAX = LINE_MAX - 3;

// Lines kept in RAM (about 8 KB, static) and shown by dumpHistory(): enough for the events
// of a walk with the card without USB (link, pairing, power), read later with 'log', and for
// a whole older-camera pairing from its start (32 up to 1.5.0-beta.4: the pairing's first lines,
// the attempt and the quick retries, were gone by the time it ended). 'p' shows the RAM left.
constexpr uint32_t HISTORY_LINES = 64;

// Formats one line (printf syntax, no newline needed), keeps it in the history
// and sends it. If no terminal is open, or the TX buffer stays full for
// LOG_WAIT_MS, sending is skipped and counted instead of waiting; after that the next lines do
// not wait at all until the buffer has room again (log.cpp).
// Not reentrant: call only from the Arduino loop task.
void line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// As line(), but not kept in the history: for frequent status lines that would
// push the events out.
void transient(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Sends the history, oldest line first, each with its uptime.
void dumpHistory();

// Number of lines not sent since start.
uint32_t dropped();

} // namespace logger
