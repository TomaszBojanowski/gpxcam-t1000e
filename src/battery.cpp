#include "battery.h"

#include <Arduino.h>

#include "board.h"

namespace {

// ADC settings as in Seeed's SDK (smtc_hal/src/smtc_hal_adc.c: gain 1/5, internal 0.6 V
// reference = 0..3.0 V, 12 bit, 40 us acquisition; t1000_e/peripherals/src/sensor.c:
// sensor_bat_sample() switches P1.06 on, samples AIN0 and doubles the result) and
// Meshtastic (variant.h: AREF_VOLTAGE 3.0, ADC_MULTIPLIER 2.0). The core's default
// acquisition time is 3 us, too short for a high-impedance divider.
constexpr uint8_t ADC_BITS = 12;
constexpr uint32_t ADC_CODES = 1u << ADC_BITS;
constexpr uint8_t ADC_SAMPLE_US = 40;
constexpr uint32_t ADC_RANGE_MV = 3000; // AR_INTERNAL_3_0: 0.6 V * 5
constexpr uint32_t DIVIDER = 2;
constexpr uint32_t SAMPLES = 8; // averaged per measurement

// Charger status outputs, both active low (Meshtastic variant.h: EXT_CHRG_DETECT_VALUE LOW
// for P1.03, EXT_IS_CHRGD_VALUE LOW for P1.04). Read without pull like Seeed's SDK
// (hal_gpio_init_in(..., HAL_GPIO_PULL_MODE_NONE)), and only with VBUS present: without
// USB the charger is unpowered and its outputs are not meaningful.
constexpr uint32_t PIN_CHARGING = EXT_CHRG_DETECT; // P1.03
constexpr uint32_t PIN_CHARGED = PIN_CHARGE_DONE;  // P1.04

bool readActiveLow(uint32_t pin)
{
    pinMode(pin, INPUT);
    const bool active = digitalRead(pin) == LOW;
    board::releasePin(pin); // input buffer disconnected between reads
    return active;
}

} // namespace

namespace battery {

uint32_t measureMillivolts(uint32_t settleMs)
{
    analogReadResolution(ADC_BITS);
    analogSampleTime(ADC_SAMPLE_US);
    analogReference(AR_INTERNAL_3_0);

    board::sensorPower(true);
    delay(settleMs);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < SAMPLES; i++)
        sum += analogRead(BATTERY_PIN); // completes in microseconds (core waits for SAADC events)
    board::sensorPower(false);

    // Max 8 * 4095 * 3000 * 2 = 196 560 000, fits in 32 bits.
    return sum * ADC_RANGE_MV * DIVIDER / (ADC_CODES * SAMPLES);
}

bool usbPowered()
{
    // A plain read also works while the SoftDevice runs: it protects POWER against writes
    // only (S140 SoftDevice Specification, "Peripheral runtime protection").
    return NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk;
}

Status read()
{
    Charger charger = Charger::NoUsb;
    if (usbPowered()) {
        if (readActiveLow(PIN_CHARGING))
            charger = Charger::Charging;
        else if (readActiveLow(PIN_CHARGED))
            charger = Charger::Charged;
        else
            charger = Charger::NotCharging;
    }
    return {measureMillivolts(), charger};
}

const char *chargerText(Charger charger)
{
    switch (charger) {
    case Charger::NoUsb:
        return "on battery";
    case Charger::Charging:
        return "USB, charging";
    case Charger::Charged:
        return "USB, charged";
    case Charger::NotCharging:
        return "USB, not charging";
    }
    return "unknown";
}

} // namespace battery
