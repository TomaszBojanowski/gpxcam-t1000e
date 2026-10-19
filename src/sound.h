// Buzzer sounds, played without blocking: start a sound, then call poll() from loop().
#pragma once

#include <stdint.h>

namespace sound {

// Claims the PWM3 peripheral for the buzzer. Returns false (and stays silent)
// if another part of the firmware already owns it.
bool begin();

void playStart();    // power-on melody
void playShutdown(); // power-off melody
void playClick();    // one short, quiet tick for a short button press

// Plays click, start and shutdown melody at the given PWM duty (1..50 %) so the
// volume can be chosen by ear. Returns false for a duty outside that range.
bool playTest(uint8_t dutyPercent);

// Advances the current sound. Call from loop().
void poll();

} // namespace sound
