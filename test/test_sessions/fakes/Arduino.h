// Fake of the Arduino core header for the host tests of the state machines (test/test_sessions):
// only what camera_session, older_session, pairing and link_control use. millis() is the test clock
// (fake.h).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>  // as the real Arduino.h, which the card's sources rely on for snprintf
#include <stdlib.h>
#include <string.h>

uint32_t millis();

// As in the Adafruit nRF52 core (cores/nRF5/Arduino.h): the smaller of two values of any types.
template <class T, class L> auto min(const T &a, const L &b) -> decltype((b < a) ? b : a)
{
    return (b < a) ? b : a;
}
