#include "board.h"

#include <SPI.h>
#include <nrf_gpio.h>

#include "log.h"

namespace {

// LR1110 timing and SPI settings, all from Seeed's SDK for this card
// (Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board @ 03147a91c5fa):
//   t1000_e/LR11XX/radio_drivers_hal/lr11xx_hal.c: lr11xx_hal_reset(),
//     lr11xx_hal_wait_on_busy(), lr11xx_hal_write()
//   smtc_hal/src/smtc_hal_spi.c: 4 MHz, SPI mode 0, MSB first
constexpr uint32_t RADIO_RESET_PULSE_MS = 5;    // NRESET low
constexpr uint32_t RADIO_BOOT_MS = 5 + 250;     // after NRESET high, until the radio firmware runs
constexpr uint32_t RADIO_BUSY_TIMEOUT_MS = 3000; // longest wait for BUSY low
constexpr uint32_t RADIO_SLEEP_SETTLE_US = 500;  // after SetSleep, before checking BUSY
constexpr uint32_t RADIO_SPI_HZ = 4000000;

// SetSleep (opcode 0x011B), sleep config 0x00 = cold start without RTC wake-up,
// sleep time 0. Semtech lr11xx driver, lr11xx_system_set_sleep() in the same SDK
// (lora_basics_modem/smtc_modem_core/radio_drivers/lr11xx_driver/src/lr11xx_system.c).
constexpr uint8_t RADIO_CMD_SET_SLEEP[] = {0x01, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr uint32_t RADIO_BUSY_PIN = LORA_DIO2; // variant.h: LORA_DIO2 is the LR1110 BUSY line

enum class RadioState { NotInitialised, Sleeping, NotReady, NoSleepConfirm };
RadioState radioState = RadioState::NotInitialised;

void driveLow(uint32_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

void driveHigh(uint32_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
}

bool waitRadioNotBusy()
{
    const uint32_t start = millis();
    while (digitalRead(RADIO_BUSY_PIN) == HIGH) {
        if (millis() - start >= RADIO_BUSY_TIMEOUT_MS)
            return false;
        delay(1);
    }
    return true;
}

void resetRadio()
{
    driveHigh(LORA_CS);
    pinMode(RADIO_BUSY_PIN, INPUT); // no pull: BUSY must not be held low while leaving reset
    driveLow(LORA_RESET);
    delay(RADIO_RESET_PULSE_MS);
    digitalWrite(LORA_RESET, HIGH); // stays high: NRESET low would reset the radio out of sleep
    delay(RADIO_BOOT_MS);
}

void sendRadioCommand(const uint8_t *cmd, size_t len)
{
    SPI.begin();
    SPI.beginTransaction(SPISettings(RADIO_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_CS, LOW);
    for (size_t i = 0; i < len; i++)
        SPI.transfer(cmd[i]);
    digitalWrite(LORA_CS, HIGH); // stays high: a falling edge on NSS wakes the radio
    SPI.endTransaction();
    // nrfx_spim_uninit() releases MISO; SCK and MOSI stay driven low (SPI mode 0),
    // so the radio inputs do not float while it sleeps.
    SPI.end();
}

// The LR1110 has no power switch on this board, so it is reset and put to sleep.
RadioState resetAndSleepRadio()
{
    resetRadio();
    if (!waitRadioNotBusy())
        return RadioState::NotReady;

    sendRadioCommand(RADIO_CMD_SET_SLEEP, sizeof(RADIO_CMD_SET_SLEEP));
    delayMicroseconds(RADIO_SLEEP_SETTLE_US);

    // In sleep the LR1110 holds BUSY high (comment in lr11xx_hal_write()).
    return digitalRead(RADIO_BUSY_PIN) == HIGH ? RadioState::Sleeping : RadioState::NoSleepConfirm;
}

const char *radioStateText(RadioState state)
{
    switch (state) {
    case RadioState::Sleeping:
        return "sleeping (BUSY high after SetSleep)";
    case RadioState::NotReady:
        return "ERROR: BUSY stayed high after reset, SetSleep not sent";
    case RadioState::NoSleepConfirm:
        return "ERROR: SetSleep sent but BUSY is low (not asleep)";
    case RadioState::NotInitialised:
        break;
    }
    return "not initialised";
}

} // namespace

namespace board {

void init()
{
    // LEDs off (LED_STATE_ON = 1).
    driveLow(PIN_LED1);    // P0.24 green
    driveLow(PIN_LED_RED); // P0.03 red

    // Buzzer: supply off, PWM line low.
    driveLow(BUZZER_EN_PIN); // P1.05
    driveLow(PIN_BUZZER);    // P0.25

    // GPS AG3335: main supply off, backup (RTC) supply on permanently, so the GPS keeps
    // its data between uses (M2 decision, hot start). Other lines as in Seeed's SDK with
    // the GPS off and the backup on (t1000_e/peripherals/src/ag3335.c: gnss_init(),
    // gnss_scan_stop()). UART pins released until the GPS is switched on (gps.cpp).
    driveLow(PIN_GPS_EN);                  // P1.11 main supply off
    driveHigh(GPS_VRTC_EN);                // P0.08 backup supply on
    driveLow(PIN_GPS_RESET);               // P1.15 reset is active high
    driveHigh(GPS_SLEEP_INT);              // P1.12 low would request sleep
    driveLow(GPS_RTC_INT);                 // P0.15 a high pulse wakes the GPS from RTC mode
    pinMode(GPS_RESETB_OUT, INPUT_PULLUP); // P1.14 GPS output "reset done"
    releasePin(GPS_RX_PIN);                // P0.14 UART
    releasePin(GPS_TX_PIN);                // P0.13 UART

    // Accelerometer QMA6100P: supply off, interrupt and I2C released.
    driveLow(PIN_3V3_ACC_EN); // P1.07
    releasePin(QMA_6100P_INT_PIN); // P1.02
    releasePin(PIN_WIRE_SDA);      // P0.26
    releasePin(PIN_WIRE_SCL);      // P0.27

    // Temperature and light sensors: supply off.
    driveLow(PIN_3V3_EN); // P1.06, SENSE_POWER_EN in the Seeed SDK

    // LR1110: NSS and NRESET high until sleepRadio(); a falling edge on NSS would wake it.
    driveHigh(LORA_CS);    // P0.12
    driveHigh(LORA_RESET); // P1.10
}

bool sleepRadio()
{
    radioState = resetAndSleepRadio();
    return radioState == RadioState::Sleeping;
}

void led(bool on)
{
    digitalWrite(PIN_LED1, on ? HIGH : LOW);
}

void sensorPower(bool on)
{
    digitalWrite(PIN_3V3_EN, on ? HIGH : LOW);
}

// Lowest leakage, and never feeds current into a chip whose supply is off.
void releasePin(uint32_t pin)
{
    nrf_gpio_cfg_default(g_ADigitalPinMap[pin]);
}

void logPowerState()
{
    logger::line("power: accelerometer off (P1.07 low), sensors off (P1.06 low)");
    logger::line("power: buzzer off (P1.05, P0.25 low), red LED off (P0.03 low)");
    logger::line("power: LR1110 %s", radioStateText(radioState));
    logger::line("power: not touched: P0.04 (analog input), P1.13 (QSPI flash power)");
}

} // namespace board
