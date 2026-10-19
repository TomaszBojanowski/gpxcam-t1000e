#include "diag.h"

#include <Arduino.h>
#include <stdio.h>

#include "board.h"
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

// ---- ADC --------------------------------------------------------------------

// Settings as in Seeed's SDK (smtc_hal/src/smtc_hal_adc.c: gain 1/5, internal 0.6 V
// reference = 0..3.0 V, 12 bit, 40 us acquisition) and Meshtastic (variant.h:
// AREF_VOLTAGE 3.0, ADC_MULTIPLIER 2.0). The core's default acquisition time is 3 us.
constexpr uint8_t ADC_BITS = 12;
constexpr uint32_t ADC_CODES = 1u << ADC_BITS;
constexpr uint8_t ADC_SAMPLE_US = 40;
constexpr uint32_t RANGE_3V0_MV = 3000; // AR_INTERNAL_3_0: 0.6 V * 5
constexpr uint32_t RANGE_3V6_MV = 3600; // AR_INTERNAL: 0.6 V * 6, for VDD, VDDH/5 and P0.04
constexpr uint32_t BATTERY_DIVIDER = 2; // Meshtastic ADC_MULTIPLIER, Seeed "bat_v * 2"
constexpr uint32_t VDDH_DIVIDER = 5;    // SAADC input VDDHDIV5
constexpr uint32_t ADC_AVERAGE = 8;     // samples per reading

constexpr uint32_t SUPPLY_SETTLE_MS = 10; // after switching P1.06 (Seeed samples right away)
constexpr uint32_t PULL_SETTLE_MS = 5;    // after changing a pin's pull resistor

constexpr uint32_t PIN_P0_04 = 0 + 4; // role unclear, see board.h

// Note: analogRead() waits for SAADC events without a timeout (core,
// wiring_analog_nRF52.c); a conversion completes in microseconds.
template <typename Read> uint32_t average(Read read)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ADC_AVERAGE; i++)
        sum += read();
    return sum / ADC_AVERAGE;
}

uint32_t toMillivolts(uint32_t raw, uint32_t rangeMv)
{
    return raw * rangeMv / ADC_CODES;
}

void prepareAdc(eAnalogReference reference)
{
    analogReadResolution(ADC_BITS);
    analogSampleTime(ADC_SAMPLE_US);
    analogReference(reference);
}

void logBatteryInput(const char *label, bool sensorSupplyOn)
{
    board::sensorPower(sensorSupplyOn);
    delay(SUPPLY_SETTLE_MS);
    const uint32_t raw = average([] { return analogRead(BATTERY_PIN); });
    const uint32_t pinMv = toMillivolts(raw, RANGE_3V0_MV);
    logger::line("bat: %s: AIN0 raw %lu = %lu mV, x%lu = %lu mV", label, (unsigned long)raw,
                 (unsigned long)pinMv, (unsigned long)BATTERY_DIVIDER,
                 (unsigned long)(pinMv * BATTERY_DIVIDER));
}

int readWithMode(uint32_t pin, uint32_t mode)
{
    pinMode(pin, mode);
    delay(PULL_SETTLE_MS);
    return digitalRead(pin);
}

void logDigital(const char *label, uint32_t pin)
{
    const int noPull = readWithMode(pin, INPUT);
    const int pullUp = readWithMode(pin, INPUT_PULLUP);
    board::releasePin(pin);
    logger::line("bat: %s: no-pull %d, pull-up %d", label, noPull, pullUp);
}

// ---- Button -----------------------------------------------------------------

constexpr uint32_t BUTTON_DIAG_MS = 15000;
constexpr uint32_t BUTTON_SAMPLE_MS = 200;

bool buttonActive = false;
uint32_t buttonStart = 0;
uint32_t buttonNext = 0;

// Each pull setting is followed by a no-pull reading: if the no-pull value just
// follows the previous pull, the pin floats; if it stays put, something on the
// board pulls or drives it.
void sampleButton(uint32_t elapsedMs)
{
    const int pullDown = readWithMode(BUTTON_PIN, INPUT_PULLDOWN);
    const int afterDown = readWithMode(BUTTON_PIN, INPUT);
    const int pullUp = readWithMode(BUTTON_PIN, INPUT_PULLUP); // the bootloader's setting
    const int afterUp = readWithMode(BUTTON_PIN, INPUT);
    logger::line("btn %5lu ms: pull-down %d, then none %d | pull-up %d, then none %d",
                 (unsigned long)elapsedMs, pullDown, afterDown, pullUp, afterUp);
}

void stopButton()
{
    buttonActive = false;
    board::releasePin(BUTTON_PIN);
    logger::line("btn: done");
}

} // namespace

namespace diag {

void logResetInfo()
{
    char reasons[RESET_TEXT_MAX];
    const uint32_t resetReason = readResetReason(); // RESETREAS saved by the core before clearing
    formatResetReason(resetReason, reasons, sizeof(reasons));
    logger::line("reset: 0x%08lx (%s)", (unsigned long)resetReason, reasons);

    // Direct register reads are fine while the SoftDevice is disabled (M1 has no BLE).
    const bool vbus = NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk;
    const bool highVoltage = (NRF_POWER->MAINREGSTATUS & POWER_MAINREGSTATUS_MAINREGSTATUS_Msk) ==
                             (POWER_MAINREGSTATUS_MAINREGSTATUS_High << POWER_MAINREGSTATUS_MAINREGSTATUS_Pos);
    logger::line("power: VBUS %s, supply via %s", vbus ? "present" : "absent",
                 highVoltage ? "VDDH (high-voltage mode)" : "VDD (normal mode)");

    const uint32_t psel = NRF_UICR->PSELRESET[0];
    const bool connected = (psel & UICR_PSELRESET_CONNECT_Msk) ==
                           (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos);
    logger::line("reset pin: PSELRESET 0x%08lx/0x%08lx -> %s P%lu.%02lu", (unsigned long)psel,
                 (unsigned long)NRF_UICR->PSELRESET[1], connected ? "connected" : "not connected",
                 (unsigned long)((psel & UICR_PSELRESET_PORT_Msk) >> UICR_PSELRESET_PORT_Pos),
                 (unsigned long)((psel & UICR_PSELRESET_PIN_Msk) >> UICR_PSELRESET_PIN_Pos));
}

void startButton()
{
    buttonActive = true;
    buttonStart = millis();
    buttonNext = buttonStart;
    logger::line("btn: sampling P0.06 for %lu s - press and release the button a few times",
                 (unsigned long)(BUTTON_DIAG_MS / 1000));
}

void poll()
{
    if (!buttonActive)
        return;
    const uint32_t now = millis();
    const uint32_t elapsed = now - buttonStart;
    if (elapsed >= BUTTON_DIAG_MS) {
        stopButton();
        return;
    }
    if ((int32_t)(now - buttonNext) >= 0) {
        buttonNext += BUTTON_SAMPLE_MS;
        sampleButton(elapsed);
    }
}

void battery()
{
    prepareAdc(AR_INTERNAL_3_0);
    logBatteryInput("P1.06 off", false);
    logBatteryInput("P1.06 on", true);
    logBatteryInput("P1.06 off again", false);

    prepareAdc(AR_INTERNAL);
    const uint32_t vdd = toMillivolts(average([] { return analogReadVDD(); }), RANGE_3V6_MV);
    const uint32_t vddh =
        toMillivolts(average([] { return analogReadVDDHDIV5(); }), RANGE_3V6_MV) * VDDH_DIVIDER;
    const uint32_t extPower = toMillivolts(average([] { return analogRead(EXT_PWR_DETECT); }), RANGE_3V6_MV);
    logger::line("bat: VDD %lu mV, VDDH %lu mV, P0.05 (external power) %lu mV", (unsigned long)vdd,
                 (unsigned long)vddh, (unsigned long)extPower);

    logDigital("P0.05 external power", EXT_PWR_DETECT);
    logDigital("P1.03 charging", EXT_CHRG_DETECT);
    logDigital("P1.04 charge done", PIN_CHARGE_DONE);
}

void p004()
{
    prepareAdc(AR_INTERNAL);
    constexpr bool SUPPLY_STATES[] = {false, true};
    for (bool sensorSupplyOn : SUPPLY_STATES) {
        board::sensorPower(sensorSupplyOn);
        delay(SUPPLY_SETTLE_MS);
        const uint32_t mv = toMillivolts(average([] { return analogRead(PIN_P0_04); }), RANGE_3V6_MV);
        logger::line("p004: P1.06 %s: AIN2 %lu mV", sensorSupplyOn ? "on" : "off", (unsigned long)mv);
    }
    board::sensorPower(false);
}

} // namespace diag
