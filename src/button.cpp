#include "button.h"

#include <Arduino.h>

namespace {

bool rawPressed = false;     // last raw reading
bool stablePressed = false;  // debounced state
bool longReported = false;   // LongPress already sent for the current hold
uint32_t rawChangedAt = 0;
uint32_t pressedAt = 0;
bool afterClick = false;      // the current press began soon after a short press
bool clickSeen = false;       // a short press has happened since start
uint32_t clickReleasedAt = 0;

bool readPressed()
{
    return digitalRead(BUTTON_PIN) == HIGH; // active high
}

} // namespace

namespace button {

void begin()
{
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    rawPressed = stablePressed = readPressed();
    rawChangedAt = pressedAt = millis();
    longReported = stablePressed; // a hold that began before start is not a long press
}

Event poll()
{
    const uint32_t now = millis();
    const bool raw = readPressed();
    if (raw != rawPressed) {
        rawPressed = raw;
        rawChangedAt = now;
    }

    if (rawPressed != stablePressed && now - rawChangedAt >= DEBOUNCE_MS) {
        stablePressed = rawPressed;
        if (stablePressed) {
            pressedAt = rawChangedAt; // the press began at the first raw change
            longReported = false;
            afterClick = clickSeen && pressedAt - clickReleasedAt <= CLICK_HOLD_GAP_MS;
        } else if (!longReported) {
            clickSeen = true;
            clickReleasedAt = rawChangedAt;
            return {Type::ShortPress, rawChangedAt - pressedAt};
        }
    }

    if (stablePressed && !longReported && now - pressedAt >= LONG_PRESS_MS) {
        longReported = true;
        return {afterClick ? Type::ClickHold : Type::LongPress, now - pressedAt};
    }
    return {Type::None, 0};
}

} // namespace button
