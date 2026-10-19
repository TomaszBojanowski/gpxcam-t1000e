// Powering off (nRF52840 System OFF) and the start after a wake-up.
//
// Safety rules (milestone M1 step 4):
//  - System OFF is entered in one place only, enterSystemOff(), and never with USB
//    connected: with a cable the card always stays on and reachable over USB.
//  - Wake-up sources in System OFF: the button (GPIO SENSE) and VBUS (plugging the
//    cable in), which the nRF52840 always provides.
//  - Before System OFF the bootloader is told to skip its DFU checks on the next start.
#pragma once

#include <stdint.h>

namespace power {

// Hold time needed to switch the card on with the button (M1 decision).
constexpr uint32_t WAKE_HOLD_MS = 1000;

// Countdown of the 'off' command, long enough to unplug the cable (M1 step 4 tests).
constexpr uint32_t OFF_COUNTDOWN_MS = 15000;

// Call in setup() right after board::init() and before board::sleepRadio(). After a
// wake-up by the button without USB, the button must still be held until WAKE_HOLD_MS
// after the start; a shorter press sends the card straight back to System OFF (the
// radio is put to sleep first). Returns when the start continues.
void handleWakeUp();

// Starts the 'off' countdown; powerOff() runs when it ends.
void startCountdown();
void cancelCountdown();
bool countdownActive();

// Runs the countdown. Call from loop().
void poll();

// Plays the shutdown melody, waits for the button to be released and enters
// System OFF. Returns, with a log line and the refusal sound (two clicks), only
// when powering off is refused: USB connected, button still pressed, or a
// wake-up condition already active.
void powerOff();

} // namespace power
