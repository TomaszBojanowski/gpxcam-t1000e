#include "gps.h"

#include <Arduino.h>

#include "board.h"
#include "gps_setup.h"
#include "log.h"
#include "nmea.h"

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
constexpr uint32_t STATUS_PERIOD_MS = 10000;
constexpr size_t COMMAND_MAX = 48; // "$PAIR066,1,1,1,1,0,0*hh\r\n" is 25 characters
constexpr uint32_t NOT_YET = UINT32_MAX;
constexpr size_t STATUS_TEXT_MAX = 96;

bool powered = false;
uint32_t poweredAt = 0;
uint32_t firstByteMs = NOT_YET; // since power on
uint32_t bytes = 0;
uint32_t lines = 0;
uint32_t nonAsciiBytes = 0; // many of them would mean a wrong baud rate
uint32_t longLines = 0;
uint32_t badChecksums = 0;
uint32_t nextStatus = 0;
uint32_t firstFixMs = NOT_YET; // since power on
bool rawLog = false;           // 'gps raw': every NMEA line in the log (positions included)

// Latest navigation data. The position stays in RAM only: it is never logged.
nmea::Rmc lastRmc = {};
nmea::Gga lastGga = {};
bool haveGga = false;
bool fixValid = false;

char sentence[NMEA_MAX + 1];
size_t sentenceLength = 0;
bool sentenceTooLong = false;

void resetCounters()
{
    firstByteMs = firstFixMs = NOT_YET;
    bytes = lines = nonAsciiBytes = longLines = badChecksums = 0;
    lastRmc = {};
    lastGga = {};
    haveGga = false;
    fixValid = false;
    sentenceLength = 0;
    sentenceTooLong = false;
}

int satellites()
{
    return haveGga ? lastGga.satellites : -1;
}

// GGA comes just before RMC of the same second on this module (M2 step 1 log).
void handleNavigation()
{
    nmea::Gga gga;
    if (nmea::parseGga(sentence, &gga)) {
        lastGga = gga;
        haveGga = true;
        return;
    }
    nmea::Rmc rmc;
    if (!nmea::parseRmc(sentence, &rmc))
        return;
    lastRmc = rmc;
    const bool valid = rmc.valid && rmc.hasPosition;
    // Events kept in the log history, without coordinates.
    if (valid && firstFixMs == NOT_YET) {
        firstFixMs = millis() - poweredAt;
        logger::line("gps: first fix %lu s after power on, %d satellites", (unsigned long)(firstFixMs / 1000),
                     satellites());
    } else if (valid != fixValid && firstFixMs != NOT_YET) {
        logger::line(valid ? "gps: fix again, %d satellites" : "gps: fix lost, %d satellites", satellites());
    }
    fixValid = valid;
}

// "fix yes, 9 sat, alt 157 m, 19:05:12 UTC 2026-10-19" - no coordinates.
void formatStatus(char *out, size_t size)
{
    char altitude[16] = "-";
    if (haveGga && lastGga.hasAltitude) {
        const long dm = lastGga.altitudeDm;
        snprintf(altitude, sizeof(altitude), "%ld m", (dm >= 0 ? dm + 5 : dm - 5) / 10);
    }
    char clock[32] = "no time";
    if (lastRmc.hasTime && lastRmc.hasDate)
        snprintf(clock, sizeof(clock), "%02u:%02u:%02u UTC %04u-%02u-%02u", lastRmc.time.hour, lastRmc.time.minute,
                 lastRmc.time.second, lastRmc.date.year, lastRmc.date.month, lastRmc.date.day);
    snprintf(out, size, "fix %s, %d sat, alt %s, %s", fixValid ? "yes" : "no", satellites(), altitude, clock);
}

void finishSentence()
{
    if (sentenceLength > 0) {
        sentence[sentenceLength] = '\0';
        lines++;
        if (sentenceTooLong)
            longLines++;
        // Raw lines only on request ('gps raw'), never kept in the log history.
        if (rawLog)
            logger::transient("nmea: %s%s", sentence, sentenceTooLong ? " (cut)" : "");
        if (!sentenceTooLong && nmea::checksumOk(sentence)) {
            handleNavigation();
            gps_setup::handleSentence(sentence);
        } else if (!sentenceTooLong && sentence[0] == '$') {
            badChecksums++;
        }
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

void formatFix(char *out, size_t size)
{
    if (firstFixMs == NOT_YET)
        snprintf(out, size, "no fix yet");
    else
        snprintf(out, size, "first fix after %lu s", (unsigned long)(firstFixMs / 1000));
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
    nextStatus = millis() + STATUS_PERIOD_MS;
    logger::line("gps: on (RESETB %s)", digitalRead(GPS_RESETB_OUT) == HIGH ? "high" : "low");
    gps_setup::begin();
}

void off()
{
    if (!powered)
        return; // Serial1.end() would wait forever for events of a UART that never ran
    // The core's Uart::end() waits for the TXSTOPPED and RXTO events without a timeout;
    // they follow the STOP tasks within microseconds.
    gps_setup::stop();
    Serial1.end();
    board::releasePin(GPS_TX_PIN);
    board::releasePin(GPS_RX_PIN);
    digitalWrite(PIN_GPS_EN, LOW);
    powered = false;
    char fix[32];
    formatFix(fix, sizeof(fix));
    logger::line("gps: off after %lu s, %lu lines, %lu bytes, %s", (unsigned long)((millis() - poweredAt) / 1000),
                 (unsigned long)lines, (unsigned long)bytes, fix);
}

bool isOn()
{
    return powered;
}

void setRawLog(bool on)
{
    rawLog = on;
    logger::line("gps: raw NMEA lines %s", on ? "on (they include the position)" : "off");
}

void sendCommand(const char *body)
{
    if (!powered)
        return;
    char command[COMMAND_MAX];
    const size_t len = nmea::buildSentence(body, command, sizeof(command));
    if (len == 0) {
        logger::line("gps: command too long: %s", body);
        return;
    }
    Serial1.write((const uint8_t *)command, len);
    logger::transient(">> %.*s", (int)(len - 2), command); // without CR LF
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
    gps_setup::poll();
    if ((int32_t)(millis() - nextStatus) >= 0) {
        nextStatus += STATUS_PERIOD_MS;
        char status[STATUS_TEXT_MAX];
        formatStatus(status, sizeof(status));
        logger::transient("gps: %s", status);
    }
}

void logState()
{
    if (!powered) {
        logger::line("gps: off, backup supply on (P0.08 high)");
        return;
    }
    char fix[32];
    formatFix(fix, sizeof(fix));
    char status[STATUS_TEXT_MAX];
    formatStatus(status, sizeof(status));
    logger::line("gps: on %lu s, %s, %s", (unsigned long)((millis() - poweredAt) / 1000), fix,
                 gps_setup::stateText());
    logger::line("gps: %s", status);
    logger::line("gps: %lu lines (%lu cut, %lu bad sum, %lu non-ASCII B), %lu B, RESETB %s", (unsigned long)lines,
                 (unsigned long)longLines, (unsigned long)badChecksums, (unsigned long)nonAsciiBytes,
                 (unsigned long)bytes, digitalRead(GPS_RESETB_OUT) == HIGH ? "high" : "low");
}

} // namespace gps
