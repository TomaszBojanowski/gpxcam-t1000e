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
void playRefuse();   // two clicks: power off or pairing refused
void playPairing();  // two short high beeps: pairing started
void playPaired();   // rising melody: the camera accepted the card
void playConnected();    // two short rising beeps: connected to the camera
void playDisconnected(); // two short falling beeps: the connection ended

// Plays all sounds (click, start, shutdown, pairing, paired, connected, disconnected) at the given PWM duty
// (1..50 %) so the volume can be chosen by ear. Returns false for a duty outside that range.
bool playTest(uint8_t dutyPercent);

// Advances the current sound. Call from loop().
void poll();

// True while a sound is playing.
bool playing();

// Ends the current sound at once: PWM stopped, buzzer pin low, buzzer supply off.
void stop();

} // namespace sound
