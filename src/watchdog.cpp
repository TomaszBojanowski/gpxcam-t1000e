#include "watchdog.h"

#include <Arduino.h>

#include "log.h"

namespace {

// The watchdog counts the 32.768 kHz low-frequency clock; timeout = (CRV + 1) / 32768 s (PS 6.36).
constexpr uint32_t LFCLK_HZ = 32768;
constexpr uint32_t RELOAD_VALUE = watchdog::TIMEOUT_S * LFCLK_HZ - 1;
constexpr uint32_t RELOAD_REGISTERS = 8; // RR[0..7]

bool keptRunning = false; // started before the last (soft) reset

bool running()
{
    return (NRF_WDT->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUS_Msk) != 0;
}

} // namespace

namespace watchdog {

void begin()
{
    if (running()) { // kept as it is: started before a soft reset, maybe by another firmware
        keptRunning = true;
        feed();
        return;
    }
    // Running while the CPU sleeps between loop passes (delay() in loop(): FreeRTOS tickless
    // idle), so a loop blocked for good with every task waiting is caught too. Paused only
    // while a debugger halts the CPU.
    NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) | (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
    NRF_WDT->CRV = RELOAD_VALUE;
    NRF_WDT->RREN = WDT_RREN_RR0_Msk; // one reload register, written by feed()
    NRF_WDT->TASKS_START = 1;
}

void feed()
{
    // All enabled reload registers must be written (PS 6.36); after a soft reset they may be
    // those of the firmware that started it.
    const uint32_t enabled = NRF_WDT->RREN;
    for (uint32_t i = 0; i < RELOAD_REGISTERS; i++) {
        if (enabled & (1UL << i))
            NRF_WDT->RR[i] = WDT_RR_RR_Reload;
    }
}

bool restartedLastTime()
{
    return (readResetReason() & POWER_RESETREAS_DOG_Msk) != 0; // RESETREAS saved by the core
}

void logState()
{
    if (!running()) {
        logger::transient("watchdog: off");
        return;
    }
    logger::transient("watchdog: on, %lu s, %s", (unsigned long)((NRF_WDT->CRV + 1) / LFCLK_HZ),
                      keptRunning ? "kept running through the reset" : "started now");
}

} // namespace watchdog
