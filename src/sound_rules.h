// How loud each sound plays with each sound setting ('sound loud|quiet|off'). No hardware
// dependencies (tested on the PC).
#pragma once

#include <stdint.h>

namespace sound_rules {

// Stored as a byte in the settings file: keep the values.
enum class Setting : uint8_t { Loud = 0, Quiet = 1, Off = 2 };

enum class Kind : uint8_t {
    HoldAnswer, // answers a hold of the button: switching on, switching off, starting pairing
    Event,      // everything else: what the card plays by itself, and the click of a short press
};

// Volume = PWM duty, the share of each period the buzzer is driven (sound.cpp). Meshtastic's
// tone() always uses 50 %; shorter pulses are quieter. Both chosen by ear on the card with
// 'diag snd N': 10 %, and 2 % on 2026-10-23 (heard against 10 and 1 %; 5 % sounded hardly
// quieter than 10 %).
constexpr uint8_t LOUD_DUTY_PERCENT = 10;
constexpr uint8_t QUIET_DUTY_PERCENT = 2;

// The duty to play a sound with; 0 = silent. Off keeps the answers to a held button, quietly:
// without them it is not clear when to let go.
uint8_t dutyPercent(Setting setting, Kind kind);

const char *settingName(Setting setting); // "loud", "quiet", "off"

// A setting from its name (lower case); false if there is none.
bool settingFromName(const char *name, Setting *setting);

} // namespace sound_rules
