// User button (P0.06): debounced short press and long press.
#pragma once

#include <stdint.h>

namespace button {

enum class Type { None, ShortPress, LongPress, ClickHold };

struct Event {
    Type type;
    uint32_t heldMs; // how long the button was (or has been) held
};

// Long press threshold (milestone M1 decision: hold 3 s to power off).
constexpr uint32_t LONG_PRESS_MS = 3000;

// Click + hold (M4 decision: pairing): a press that begins at most this long after a
// short press ended and is held for LONG_PRESS_MS.
constexpr uint32_t CLICK_HOLD_GAP_MS = 500;

// Configures the pin: active high, no resistor on the board, so the internal
// pull-down is used (as Meshtastic's INPUT_PULLDOWN_SENSE). See docs/SPRZET.md.
void begin();

// Call from loop(). Returns at most one event per call: ShortPress on release
// before LONG_PRESS_MS; once the hold reaches LONG_PRESS_MS, ClickHold if the press began
// within CLICK_HOLD_GAP_MS after a short press, otherwise LongPress (the release that
// follows is not reported).
Event poll();

} // namespace button
