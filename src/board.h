// Board-level helpers for the Seeed SenseCAP T1000-E.
//
// Pin numbers come from variants/t1000-e/variant.h (Meshtastic). Pins that
// Meshtastic does not define, or describes differently, are taken from Seeed's
// own SDK for this card:
//   Seeed-Studio/Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board
//   smtc_hal/inc/smtc_hal_config.h @ 03147a91c5fa
//
// Pins deliberately left in their reset state (input, buffer disconnected):
//   P0.04  Meshtastic: T1000X_SENSOR_EN_PIN (output); Seeed: SENSE_ADC_VCC (analog
//          input AIN2). Measured on the card (M1): about VDD/2 with P1.06 on, so it
//          is an analog input as Seeed says. Never driven.
//   P1.13  FLASH_POWER_EN of the external QSPI flash (Seeed SDK only). Neither
//          Seeed's firmware nor Meshtastic drives it.
#pragma once

#include <Arduino.h>

// Second LED (red), USER_LED_R in the Seeed SDK. Not used by Meshtastic.
#define PIN_LED_RED (0 + 3) // P0.03

// Charger "charge done" status, CHARGER_DONE in the Seeed SDK (Meshtastic has it commented out).
#define PIN_CHARGE_DONE (32 + 4) // P1.04

namespace board {

// Puts every peripheral except the LR1110 in its off state and prepares the green LED;
// holds the LR1110 control lines (NSS, NRESET) high. Takes microseconds. Must be
// called first in setup(): the bootloader leaves all pins in their reset state.
void init();

// Resets the LR1110 and puts it to sleep (it has no power switch). Takes about 0.27 s.
// Needed after every start, including a wake-up from System OFF: while the pins
// are in their reset state the radio may have been woken. Returns true when the
// radio confirmed sleep (BUSY high).
bool sleepRadio();

// Green LED (P0.24).
void led(bool on);

// Supply of the temperature/light sensors, P1.06 (SENSE_POWER_EN in the Seeed SDK).
// Seeed also switches it on for battery measurements.
void sensorPower(bool on);

// Back to the nRF52 reset state: input with the buffer disconnected, no pull.
void releasePin(uint32_t pin);

// Logs the result of sleepRadio() (the other peripherals stay as init() set them).
void logPowerState();

} // namespace board
