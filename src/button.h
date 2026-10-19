// User button (P0.06): debounced short press and long press.
#pragma once

#include <stdint.h>

namespace button {

enum class Type { None, ShortPress, LongPress };

struct Event {
    Type type;
    uint32_t heldMs; // how long the button was (or has been) held
};

// Long press threshold (milestone M1 decision: hold 3 s to power off).
constexpr uint32_t LONG_PRESS_MS = 3000;

// Configures the pin: active high, no resistor on the board, so the internal
// pull-down is used (as Meshtastic's INPUT_PULLDOWN_SENSE). See docs/SPRZET.md.
void begin();

// Call from loop(). Returns at most one event per call: ShortPress on release
// before LONG_PRESS_MS, LongPress once when the hold reaches LONG_PRESS_MS
// (the release that follows is not reported).
Event poll();

} // namespace button
