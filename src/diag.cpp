#include "diag.h"

#include <Arduino.h>
#include <stdio.h>

#include "battery.h"
#include "log.h"

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
        logger::line("bat: settle %2lu ms: %lu mV", (unsigned long)settleMs,
                     (unsigned long)battery::measureMillivolts(settleMs));
    }
    const battery::Status status = battery::read();
    logger::line("bat: %lu mV, %s", (unsigned long)status.batteryMv, battery::chargerText(status.charger));
}

} // namespace diag
