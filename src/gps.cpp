#include "gps.h"

#include <Arduino.h>

#include "board.h"
#include "log.h"

namespace {

// Power-up sequence from Seeed's SDK for this card (Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board
// @ 03147a91c5fa, t1000_e/peripherals/src/ag3335.c: gnss_init() and gnss_scan_start()):
// supply on, 10 ms; reset (active high) for 10 ms; UART; 50 ms; RTC_INT high for 3 ms
// (leaves RTC mode); 50 ms. The commands Seeed sends afterwards come in a later step.
constexpr uint32_t SUPPLY_SETTLE_MS = 10;
constexpr uint32_t RESET_PULSE_MS = 10;
constexpr uint32_t UART_SETTLE_MS = 50;
constexpr uint32_t RTC_WAKE_PULSE_MS = 3;
constexpr uint32_t WAKE_SETTLE_MS = 50;

// Meshtastic variant.h GPS_BAUDRATE; Seeed smtc_hal_uart.c NRF_UART_BAUDRATE_115200.
constexpr uint32_t BAUD_RATE = 115200;

// NMEA 0183 sentences are at most 82 characters including "$" and CR LF; some margin
// for proprietary $PAIR replies.
constexpr size_t NMEA_MAX = 100;
constexpr uint32_t SUMMARY_PERIOD_MS = 10000;
constexpr uint32_t NOT_YET = UINT32_MAX;

bool powered = false;
uint32_t poweredAt = 0;
uint32_t firstByteMs = NOT_YET; // since power on
uint32_t bytes = 0;
uint32_t lines = 0;
uint32_t nonAsciiBytes = 0; // many of them would mean a wrong baud rate
uint32_t longLines = 0;
uint32_t nextSummary = 0;

char sentence[NMEA_MAX + 1];
size_t sentenceLength = 0;
bool sentenceTooLong = false;

void resetCounters()
{
    firstByteMs = NOT_YET;
    bytes = lines = nonAsciiBytes = longLines = 0;
    sentenceLength = 0;
    sentenceTooLong = false;
}

void finishSentence()
{
    if (sentenceLength > 0) {
        sentence[sentenceLength] = '\0';
        lines++;
        if (sentenceTooLong)
            longLines++;
        // Raw lines are sent only, not kept in the log history (they would push the events out).
        logger::transient("nmea: %s%s", sentence, sentenceTooLong ? " (cut)" : "");
    }
    sentenceLength = 0;
    sentenceTooLong = false;
}

void handleByte(int c)
{
    if (c == '\n') {
        finishSentence();
        return;
    }
    if (c == '\r')
        return;
    if (c < ' ' || c > '~') {
        nonAsciiBytes++;
        c = '.'; // keep the terminal readable
    }
    if (sentenceLength < NMEA_MAX)
        sentence[sentenceLength++] = (char)c;
    else
        sentenceTooLong = true;
}

} // namespace

namespace gps {

void on()
{
    if (powered)
        return;
    resetCounters();

    digitalWrite(PIN_GPS_EN, HIGH); // main supply; the backup supply P0.08 is always on
    poweredAt = millis();
    delay(SUPPLY_SETTLE_MS);
    digitalWrite(PIN_GPS_RESET, HIGH);
    delay(RESET_PULSE_MS);
    digitalWrite(PIN_GPS_RESET, LOW);

    // GPIO setup before enabling the UARTE: TXD output high, RXD input (nRF52840 PS v1.5,
    // 6.34.8, table 135). Only now, with the GPS powered, so TXD never feeds an unpowered chip.
    pinMode(GPS_TX_PIN, OUTPUT);
    digitalWrite(GPS_TX_PIN, HIGH);
    pinMode(GPS_RX_PIN, INPUT);
    Serial1.begin(BAUD_RATE);
    delay(UART_SETTLE_MS);

    digitalWrite(GPS_RTC_INT, HIGH);
    delay(RTC_WAKE_PULSE_MS);
    digitalWrite(GPS_RTC_INT, LOW);
    delay(WAKE_SETTLE_MS);

    powered = true;
    nextSummary = millis() + SUMMARY_PERIOD_MS;
    logger::line("gps: on (RESETB %s)", digitalRead(GPS_RESETB_OUT) == HIGH ? "high" : "low");
}

void off()
{
    if (!powered)
        return; // Serial1.end() would wait forever for events of a UART that never ran
    // The core's Uart::end() waits for the TXSTOPPED and RXTO events without a timeout;
    // they follow the STOP tasks within microseconds.
    Serial1.end();
    board::releasePin(GPS_TX_PIN);
    board::releasePin(GPS_RX_PIN);
    digitalWrite(PIN_GPS_EN, LOW);
    powered = false;
    logger::line("gps: off after %lu s, %lu lines, %lu bytes", (unsigned long)((millis() - poweredAt) / 1000),
                 (unsigned long)lines, (unsigned long)bytes);
}

bool isOn()
{
    return powered;
}

void poll()
{
    if (!powered)
        return;
    while (Serial1.available() > 0) {
        const int c = Serial1.read();
        bytes++;
        if (firstByteMs == NOT_YET) {
            firstByteMs = millis() - poweredAt;
            logger::line("gps: first byte %lu ms after power on", (unsigned long)firstByteMs);
        }
        handleByte(c);
    }
    if ((int32_t)(millis() - nextSummary) >= 0) {
        nextSummary += SUMMARY_PERIOD_MS;
        logger::transient("gps: %lu lines, %lu bytes, %lu non-ASCII bytes since power on", (unsigned long)lines,
                          (unsigned long)bytes, (unsigned long)nonAsciiBytes);
    }
}

void logState()
{
    if (!powered) {
        logger::line("gps: off, backup supply on (P0.08 high)");
        return;
    }
    logger::line("gps: on for %lu s, %lu lines (%lu cut), %lu bytes (%lu non-ASCII), RESETB %s",
                 (unsigned long)((millis() - poweredAt) / 1000), (unsigned long)lines, (unsigned long)longLines,
                 (unsigned long)bytes, (unsigned long)nonAsciiBytes,
                 digitalRead(GPS_RESETB_OUT) == HIGH ? "high" : "low");
}

} // namespace gps
