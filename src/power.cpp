#include "power.h"

#include <Arduino.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>

#include "battery.h"
#include "battery_log.h"
#include "battery_rules.h"
#include "bluetooth.h"
#include "board.h"
#include "gps.h"
#include "link_control.h"
#include "log.h"
#include "pairing.h"
#include "sound.h"

// Sources: nRF52840 Product Specification v1.5 (PS):
//   5.3.3     System OFF: wake-up by GPIO DETECT, LPCOMP, NFC, VBUS (VBUS,DETECT) or reset;
//             the system is reset on wake-up.
//   5.3.3.1   Emulated System OFF (debugger attached): the CPU keeps running.
//   5.3.6.8   Reset table: a wake-up from System OFF does not reset GPIO or retained registers.
//   5.3.7.16  GPREGRET is a retained register.
//   6.9       PIN_CNF registers are retained; 6.9.1 LATCH: a bit cannot be cleared while
//             its pin's SENSE condition is met.
//   6.10.2    Entering System OFF while DETECT is high wakes the system at once.
// S140 SoftDevice Specification, "Peripheral runtime protection": while the SoftDevice runs,
// POWER registers can be read, but a direct write causes a HardFault; writes then go
// through its API (nrf_soc.h).

namespace {

// Adafruit nRF52 Bootloader (src/main.c, DFU_MAGIC_SKIP): with this value in GPREGRET the
// bootloader skips all DFU checks on the next start and clears GPREGRET. Meshtastic sets it
// before System OFF for the same reason (src/platform/nrf52/main-nrf52.cpp, cpuDeepSleep()).
constexpr uint32_t DFU_MAGIC_SKIP = 0x6D;

// sd_power_gpregret_set()/clr() (nrf_soc.h): register 0 is GPREGRET (8 bits); set only
// sets bits, so the register is cleared first.
constexpr uint32_t SD_GPREGRET_ID = 0;
constexpr uint32_t GPREGRET_ALL_BITS = 0xFF;

constexpr uint32_t DEBOUNCE_MS = 50;            // as button.cpp (OneButton default)
constexpr uint32_t RELEASE_TIMEOUT_MS = 5000;   // give up powering off if the button stays pressed
constexpr uint32_t MELODY_TIMEOUT_MS = 2000;    // the shutdown melody takes about 0.45 s
constexpr uint32_t SYSTEM_OFF_GRACE_MS = 1000;  // reached only in emulated System OFF
constexpr uint32_t COUNTDOWN_LOG_MS = 5000;

// LATCH clearing is retried this long before powering off is refused (diagnostics, M2 step 4).
constexpr uint32_t LATCH_RETRY_MS = 100;
constexpr uint32_t LATCH_RETRY_STEP_MS = 10;

// GPREGRET2 (8 bits, retained through System OFF and its wake-up: PS 5.3.7.17, 5.3.6.8;
// not used by the bootloader 488711a nor the core) keeps the program time at which a short
// wake-up press was released, in units of 10 ms, so it can be logged at the next start.
// 0 = nothing recorded. Written only in handleWakeUp(), before the SoftDevice starts, so directly.
constexpr uint32_t RELEASE_UNIT_MS = 10;
constexpr uint32_t RELEASE_MAX_UNITS = 254;

uint8_t lowBatteryReadings = 0;

bool countdown = false;
uint32_t countdownStart = 0;
uint32_t nextCountdownLog = 0;

// After a wake-up held long enough: log when the button is finally released.
bool trackWakeRelease = false;
bool wakeReleased = false;
uint32_t wakeReleasedAt = 0;

bool buttonPressed()
{
    return digitalRead(BUTTON_PIN) == HIGH; // active high, see docs/SPRZET.md
}

// True when the button has been released continuously for DEBOUNCE_MS within timeoutMs.
// Keeps a playing sound going while waiting.
bool waitForRelease(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    bool released = false;
    uint32_t releasedAt = 0;
    while (millis() - start < timeoutMs) {
        if (buttonPressed()) {
            released = false;
        } else if (!released) {
            released = true;
            releasedAt = millis();
        } else if (millis() - releasedAt >= DEBOUNCE_MS) {
            return true;
        }
        sound::poll();
        delay(1);
    }
    return false;
}

void waitForSoundEnd()
{
    const uint32_t start = millis();
    while (sound::playing() && millis() - start < MELODY_TIMEOUT_MS) {
        sound::poll();
        delay(1);
    }
    sound::stop(); // in case the timeout ended the wait
}

// Makes the button the wake-up source: pull-down (no resistor on the board) and SENSE
// high (active high), as Meshtastic's INPUT_PULLDOWN_SENSE. Returns false if a SENSE
// condition is met right now, because System OFF would then end at once (PS 6.10.2).
bool armWakeUp()
{
    NRF_P0->DETECTMODE = GPIO_DETECTMODE_DETECTMODE_Default; // DETECT follows the pins, not LATCH
    NRF_P1->DETECTMODE = GPIO_DETECTMODE_DETECTMODE_Default;
    pinMode(BUTTON_PIN, INPUT_PULLDOWN_SENSE);

    // Writing 1 clears a LATCH bit, unless that pin still meets its SENSE condition (PS 6.9.1).
    // Retried for LATCH_RETRY_MS: on one wake-up the button bit would not clear (open question
    // in docs/SPRZET.md); the values below go to the log to find out why.
    const uint32_t latchBefore = NRF_P0->LATCH;
    const uint32_t start = millis();
    uint32_t latchP0 = 0;
    uint32_t latchP1 = 0;
    for (;;) {
        NRF_P0->LATCH = 0xFFFFFFFF;
        NRF_P1->LATCH = 0xFFFFFFFF;
        latchP0 = NRF_P0->LATCH;
        latchP1 = NRF_P1->LATCH;
        if ((latchP0 == 0 && latchP1 == 0) || millis() - start >= LATCH_RETRY_MS)
            break;
        delay(LATCH_RETRY_STEP_MS);
    }
    if (latchP0 != 0 || latchP1 != 0) {
        const uint32_t pinConfig = NRF_P0->PIN_CNF[g_ADigitalPinMap[BUTTON_PIN]];
        const int level = digitalRead(BUTTON_PIN);
        pinMode(BUTTON_PIN, INPUT_PULLDOWN); // sensing off again
        logger::line("power off cancelled: LATCH P0 0x%08lx->0x%08lx P1 0x%08lx, button %d, PIN_CNF 0x%08lx",
                     (unsigned long)latchBefore, (unsigned long)latchP0, (unsigned long)latchP1, level,
                     (unsigned long)pinConfig);
        return false;
    }
    if (latchBefore != 0)
        logger::line("power: LATCH P0 0x%08lx cleared after %lu ms", (unsigned long)latchBefore,
                     (unsigned long)(millis() - start));
    return true;
}

bool softDeviceEnabled()
{
    uint8_t enabled = 0;
    sd_softdevice_is_enabled(&enabled); // also valid while it is disabled (as in the core's flash_nrf5x.c)
    return enabled != 0;
}

// Tells the bootloader to skip its DFU checks on the next start and enters System OFF.
// Returns only in emulated System OFF (PS 5.3.3.1).
void powerDown()
{
    if (softDeviceEnabled()) {
        sd_power_gpregret_clr(SD_GPREGRET_ID, GPREGRET_ALL_BITS);
        sd_power_gpregret_set(SD_GPREGRET_ID, DFU_MAGIC_SKIP);
        sd_power_system_off();
    } else {
        NRF_POWER->GPREGRET = DFU_MAGIC_SKIP;
        NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
    }
    __DSB();
}

bool refuseWithUsb()
{
    if (!battery::usbPowered())
        return false;
    logger::line("power off refused: USB connected - unplug the cable first");
    return true;
}

// The only place that enters System OFF. The checks run right before entering. A press
// after them wakes the chip at once (PS 6.10.2); a cable plugged in within those few
// microseconds does not (VBUS wakes only when it rises during System OFF, PS 5.3.2), so
// the cable would have to be plugged in again - the button still works.
void enterSystemOff()
{
    if (refuseWithUsb())
        return;
    pairing::stop();             // closes a camera connection
    link_control::stop(false);   // closes a camera connection, GPS off
    bluetooth::scanOff();        // the radio must be idle
    gps::off();           // a running GPS would draw milliamps in System OFF
    if (!armWakeUp())
        return;

    // Nothing else runs at this point: the sound has ended (PWM stopped), SAADC and SPI
    // are idle, so no EasyDMA transfer is in progress (PS 5.3.3). The radio is idle too:
    // pairing, link and scanning were stopped above.
    powerDown();

    // Reached only in emulated System OFF (PS 5.3.3.1). Instead of an endless loop,
    // restart after a moment; GPREGRET makes the bootloader start the program directly.
    delay(SYSTEM_OFF_GRACE_MS);
    NVIC_SystemReset();
}

void logPreviousShortPress()
{
    const uint32_t units = NRF_POWER->GPREGRET2 & POWER_GPREGRET2_GPREGRET_Msk;
    if (units == 0)
        return;
    NRF_POWER->GPREGRET2 = 0;
    logger::line("power: earlier wake-up was a short press, released at %lu ms of program time",
                 (unsigned long)(units * RELEASE_UNIT_MS));
}

void logWakeRelease()
{
    if (buttonPressed()) {
        wakeReleased = false;
        return;
    }
    if (!wakeReleased) {
        wakeReleased = true;
        wakeReleasedAt = millis();
    } else if (millis() - wakeReleasedAt >= DEBOUNCE_MS) {
        trackWakeRelease = false;
        logger::line("power: wake-up press released at %lu ms of program time", (unsigned long)wakeReleasedAt);
    }
}

} // namespace

namespace power {

void handleWakeUp()
{
    logPreviousShortPress();
    if ((readResetReason() & POWER_RESETREAS_OFF_Msk) == 0)
        return; // not woken by the button: normal start
    if (battery::usbPowered())
        return; // cable connected: stay on

    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    // millis() counts from the start of the program. The press began earlier: the time the
    // bootloader and the core need before setup() is not counted (being measured with the
    // log lines below). A release before WAKE_HOLD_MS means a short press: back to System
    // OFF, silently. If that is refused, the start simply continues.
    const uint32_t now = millis();
    if (now < WAKE_HOLD_MS && waitForRelease(WAKE_HOLD_MS - now)) {
        const uint32_t releasedAt = millis() - DEBOUNCE_MS;
        const uint32_t units = min(max(releasedAt / RELEASE_UNIT_MS, 1ul), RELEASE_MAX_UNITS);
        // The radio may have woken while the pins were floating. If it does not confirm
        // sleep, start normally instead (the error shows in the log) rather than leave
        // it awake in System OFF.
        if (board::sleepRadio()) {
            NRF_POWER->GPREGRET2 = units; // read back at the next start
            enterSystemOff();
            NRF_POWER->GPREGRET2 = 0; // refused: the card starts, log it now
        }
        logger::line("power: short wake-up press released at %lu ms of program time, card starts",
                     (unsigned long)releasedAt);
        return;
    }
    trackWakeRelease = true; // held long enough: log the release time from poll()
}

void checkBattery(uint32_t millivolts, bool usb, bool atStart)
{
    if (!battery_rules::powerOffForLowBattery(millivolts, usb, atStart, &lowBatteryReadings))
        return;
    logger::line("power: battery %lu mV, below %lu mV - powering off to protect it", (unsigned long)millivolts,
                 (unsigned long)battery_rules::LOW_MV);
    battery_log::addPowerOffEntry(millivolts);
    sound::playRefuse();
    waitForSoundEnd();
    enterSystemOff(); // refuses with USB, then the card simply goes on
}

void startCountdown()
{
    countdown = true;
    countdownStart = millis();
    nextCountdownLog = countdownStart + COUNTDOWN_LOG_MS;
    logger::line("power off in %lu s - unplug the USB cable now (with USB it will be refused);"
                 " a short press cancels", (unsigned long)(OFF_COUNTDOWN_MS / 1000));
}

void cancelCountdown()
{
    if (!countdown)
        return;
    countdown = false;
    logger::line("power off cancelled");
}

bool countdownActive()
{
    return countdown;
}

void poll()
{
    if (trackWakeRelease)
        logWakeRelease();
    if (!countdown)
        return;
    const uint32_t elapsed = millis() - countdownStart;
    if (elapsed >= OFF_COUNTDOWN_MS) {
        countdown = false;
        if (buttonPressed()) // a press meant to cancel, still held
            logger::line("power off cancelled: button pressed");
        else
            powerOff();
        return;
    }
    if ((int32_t)(millis() - nextCountdownLog) >= 0) {
        nextCountdownLog += COUNTDOWN_LOG_MS;
        logger::line("power off in %lu s", (unsigned long)((OFF_COUNTDOWN_MS - elapsed + 999) / 1000));
    }
}

void powerOff()
{
    countdown = false;
    if (refuseWithUsb()) {
        sound::playRefuse();
        return;
    }
    sound::playShutdown();
    if (!waitForRelease(RELEASE_TIMEOUT_MS)) {
        sound::stop();
        logger::line("power off cancelled: button still pressed after %lu s",
                     (unsigned long)(RELEASE_TIMEOUT_MS / 1000));
        sound::playRefuse();
        return;
    }
    waitForSoundEnd();
    board::led(false);
    enterSystemOff();
    sound::playRefuse(); // reached only when enterSystemOff() refused
}

} // namespace power
