#include "diag.h"

#include <Arduino.h>
#ifdef GPXCAM_TEST_COMMANDS
#include <flash/flash_nrf5x.h>
#endif
#include <nrf_clock.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>
#include <stdio.h>

#include "battery.h"
#include "log.h"
#include "watchdog.h"

namespace {

// ---- Reset reason ---------------------------------------------------------

// POWER->RESETREAS bits, names from the Nordic MDK (nrf52840_bitfields.h).
// No bit set means power-on or brown-out reset.
struct ResetBit {
    uint32_t mask;
    const char *name;
};
constexpr ResetBit RESET_BITS[] = {
    {POWER_RESETREAS_RESETPIN_Msk, "RESETPIN"}, // reset pin
    {POWER_RESETREAS_DOG_Msk, "DOG"},           // watchdog
    {POWER_RESETREAS_SREQ_Msk, "SREQ"},         // software reset, e.g. after a UF2 copy
    {POWER_RESETREAS_LOCKUP_Msk, "LOCKUP"},     // CPU lock-up
    {POWER_RESETREAS_OFF_Msk, "OFF-GPIO"},      // wake from System OFF by GPIO DETECT
    {POWER_RESETREAS_LPCOMP_Msk, "OFF-LPCOMP"}, // wake from System OFF by LPCOMP
    {POWER_RESETREAS_DIF_Msk, "DIF"},           // debug interface
    {POWER_RESETREAS_NFC_Msk, "OFF-NFC"},       // wake from System OFF by NFC field
    {POWER_RESETREAS_VBUS_Msk, "OFF-VBUS"},     // wake from System OFF by VBUS
};
constexpr size_t RESET_TEXT_MAX = 96; // all names above fit

void formatResetReason(uint32_t reasons, char *out, size_t size)
{
    if (reasons == 0) {
        snprintf(out, size, "power-on or brown-out");
        return;
    }
    size_t used = 0;
    out[0] = '\0';
    for (const ResetBit &bit : RESET_BITS) {
        if ((reasons & bit.mask) == 0)
            continue;
        const int written = snprintf(out + used, size - used, "%s%s", used ? " " : "", bit.name);
        if (written < 0 || (size_t)written >= size - used)
            break; // buffer full: keep what fits
        used += (size_t)written;
    }
}

constexpr uint32_t HANG_LIMIT_S = 2 * watchdog::TIMEOUT_S;
constexpr uint32_t HANG_LIMIT_MS = 2 * watchdog::TIMEOUT_MS;

constexpr uint32_t CLOCK_SAMPLES = 1000;
constexpr uint32_t CLOCK_SAMPLE_MS = 1;
constexpr uint32_t PERCENT = 100;
constexpr uint32_t MS_PER_SECOND = 1000;

bool clockCheckPending = false;
uint32_t clockCheckStartMs = 0;
uint32_t clockCheckDelayMs = 0;

// As TinyUSB's hfclk_running() (dcd_nrf5x.c): through the SoftDevice while it runs.
bool crystalRunning()
{
    uint8_t enabled = 0;
    (void)sd_softdevice_is_enabled(&enabled);
    if (enabled) {
        uint32_t running = 0;
        (void)sd_clock_hfclk_is_running(&running);
        return running != 0;
    }
    return nrf_clock_hf_is_running(NRF_CLOCK, NRF_CLOCK_HFCLK_HIGH_ACCURACY);
}

void measureClock()
{
    uint32_t running = 0;
    for (uint32_t i = 0; i < CLOCK_SAMPLES; i++) {
        if (crystalRunning())
            running++;
        delay(CLOCK_SAMPLE_MS);
    }
    logger::line("diag: 64 MHz crystal running in %lu %% of 1 s (%lu of %lu samples), USB %s",
                 (unsigned long)(running * PERCENT / CLOCK_SAMPLES), (unsigned long)running,
                 (unsigned long)CLOCK_SAMPLES, battery::usbPowered() ? "in" : "out");
}

// ---- Battery settle check ---------------------------------------------------

// Waits after switching P1.06 on, to see when the battery divider has settled.
constexpr uint32_t SETTLE_STEPS_MS[] = {0, 1, 2, 5, 10, 20, 50};
// P1.06 stays off this long between steps so each one starts from a discharged divider.
constexpr uint32_t SETTLE_OFF_MS = 500;

} // namespace

namespace diag {

void logResetInfo()
{
    char reasons[RESET_TEXT_MAX];
    const uint32_t resetReason = readResetReason(); // RESETREAS saved by the core before clearing
    formatResetReason(resetReason, reasons, sizeof(reasons));
    logger::line("reset: 0x%08lx (%s)", (unsigned long)resetReason, reasons);
}

void battery()
{
    for (uint32_t settleMs : SETTLE_STEPS_MS) {
        delay(SETTLE_OFF_MS);
        logger::line("battery: settle %2lu ms: %lu mV", (unsigned long)settleMs,
                     (unsigned long)battery::measureMillivolts(settleMs));
    }
    const battery::Status status = battery::read();
    logger::line("battery: %lu mV, %s", (unsigned long)status.batteryMv, battery::chargerText(status.charger));
}

void hang()
{
    logger::line("diag: loop held - the watchdog should restart the card in %lu s", (unsigned long)watchdog::TIMEOUT_S);
    const uint32_t start = millis();
    while (millis() - start < HANG_LIMIT_MS)
        delay(1); // lets the USB task send the log; the loop does not feed the watchdog
    logger::line("diag: the watchdog did not restart the card in %lu s", (unsigned long)HANG_LIMIT_S);
}

#ifdef GPXCAM_TEST_COMMANDS
void breakFileSystem()
{
    constexpr uint32_t FIRST_PAGE = 0xED000; // LFS_FLASH_ADDR in the core's InternalFileSystem.cpp
    logger::line("diag: TEST - erasing 0x%05lX, the first page of the file system, and restarting",
                 (unsigned long)FIRST_PAGE);
    if (!flash_nrf5x_erase(FIRST_PAGE)) {
        logger::line("diag: erasing failed");
        return;
    }
    delay(100); // lets the log line leave
    NVIC_SystemReset();
}
#endif

void checkClock(uint32_t delayS)
{
    if (delayS == 0) {
        measureClock();
        return;
    }
    clockCheckPending = true;
    clockCheckStartMs = millis();
    clockCheckDelayMs = delayS * MS_PER_SECOND;
    logger::line("diag: crystal check in %lu s", (unsigned long)delayS);
}

void poll()
{
    if (!clockCheckPending || millis() - clockCheckStartMs < clockCheckDelayMs)
        return;
    clockCheckPending = false;
    measureClock();
}

} // namespace diag
