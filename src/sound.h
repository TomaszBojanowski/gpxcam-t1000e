// Buzzer sounds, played without blocking: start a sound, then call poll() from loop().
// How loud depends on the sound setting and the kind of sound (sound_rules.h).
#pragma once

#include <stdint.h>

#include "sound_rules.h"

namespace sound {

using sound_rules::Kind;
using sound_rules::Setting;

// Claims the PWM3 peripheral for the buzzer. Returns false (and stays silent)
// if another part of the firmware already owns it.
bool begin();

// The sound setting for the sounds from now on; Loud until set.
void setSetting(Setting setting);

void playStart();    // power-on melody (answers a hold)
void playShutdown(); // power-off melody (answers a hold)
void playClick();    // one short tick for a short button press
// Two clicks: refused or given up. kind: HoldAnswer when it answers a hold of the button
// (powering off refused, pairing not possible), Event otherwise (pairing given up, the camera
// refused the card, low battery).
void playRefuse(Kind kind);
void playPairing();  // two short high beeps: pairing started (answers a hold)
void playPaired();   // rising melody: the camera accepted the card
void playConnected();    // two short rising beeps: connected to the camera
void playDisconnected(); // two short falling beeps: the connection ended

// Plays all sounds (click, start, shutdown, pairing, paired, connected, disconnected) at the given PWM duty
// (1..50 %), whatever the setting, so the volume can be chosen by ear. Returns false for a duty outside
// that range.
bool playTest(uint8_t dutyPercent);

// Advances the current sound. Call from loop().
void poll();

// True while a sound is playing.
bool playing();

// Plays the current sound to its end, at most 2 s, then stops it. For code that is about to
// hold up loop(): poll() would not run meanwhile, and the note playing would last that long.
void playToEnd();

// Ends the current sound at once: PWM stopped, buzzer pin low, buzzer supply off.
void stop();

} // namespace sound
