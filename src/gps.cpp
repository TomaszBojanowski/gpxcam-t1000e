#include "gps.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

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
constexpr int NO_SATELLITES = -1;

// Fields used before the real parser exists (NMEA 0183): RMC field 2 is the status
// (A = valid position, V = not valid); GGA field 7 is the number of satellites in use.
constexpr unsigned RMC_STATUS_FIELD = 2;
constexpr unsigned GGA_SATELLITES_FIELD = 7;
constexpr unsigned long MAX_SATELLITES = 99; // two digits in GGA

bool powered = false;
uint32_t poweredAt = 0;
uint32_t firstByteMs = NOT_YET; // since power on
uint32_t bytes = 0;
uint32_t lines = 0;
uint32_t nonAsciiBytes = 0; // many of them would mean a wrong baud rate
uint32_t longLines = 0;
uint32_t badChecksums = 0;
uint32_t nextSummary = 0;
uint32_t firstFixMs = NOT_YET; // since power on
int lastSatellites = NO_SATELLITES;

char sentence[NMEA_MAX + 1];
size_t sentenceLength = 0;
bool sentenceTooLong = false;

void resetCounters()
{
    firstByteMs = firstFixMs = NOT_YET;
    bytes = lines = nonAsciiBytes = longLines = badChecksums = 0;
    lastSatellites = NO_SATELLITES;
    sentenceLength = 0;
    sentenceTooLong = false;
}

// NMEA 0183 checksum: XOR of all characters between '$' and '*', written as two hex
// digits after '*' at the end of the sentence.
bool checksumOk(const char *s)
{
    if (s[0] != '$')
        return false;
    uint8_t sum = 0;
    const char *p = s + 1;
    for (; *p != '\0' && *p != '*'; p++)
        sum ^= (uint8_t)*p;
    if (*p != '*' || !isxdigit((unsigned char)p[1]) || !isxdigit((unsigned char)p[2]) || p[3] != '\0')
        return false;
    return strtoul(p + 1, nullptr, 16) == sum;
}

// True for "$xxTTT," where TTT is the sentence type and xx any talker (GP, GN, ...).
bool isType(const char *s, const char *type)
{
    return strlen(s) > 6 && strncmp(s + 3, type, 3) == 0 && s[6] == ',';
}

// Start of comma-separated field `index` (0 is "$xxTTT"), or nullptr.
const char *field(const char *s, unsigned index)
{
    for (; index > 0; index--) {
        s = strchr(s, ',');
        if (s == nullptr)
            return nullptr;
        s++;
    }
    return s;
}

void checkFix()
{
    if (!checksumOk(sentence)) {
        if (sentence[0] == '$')
            badChecksums++;
        return;
    }
    if (isType(sentence, "GGA")) {
        const char *f = field(sentence, GGA_SATELLITES_FIELD);
        char *end = nullptr;
        const unsigned long count = f ? strtoul(f, &end, 10) : 0;
        lastSatellites = (f && end != f && *end == ',' && count <= MAX_SATELLITES) ? (int)count : NO_SATELLITES;
    } else if (isType(sentence, "RMC") && firstFixMs == NOT_YET) {
        const char *status = field(sentence, RMC_STATUS_FIELD);
        if (status != nullptr && *status == 'A') {
            firstFixMs = millis() - poweredAt;
            // Kept in the history, without coordinates.
            logger::line("gps: first fix %lu s after power on, %d satellites", (unsigned long)(firstFixMs / 1000),
                         lastSatellites);
        }
    }
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
        if (!sentenceTooLong)
            checkFix();
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
    char fix[32];
    formatFix(fix, sizeof(fix));
    logger::line("gps: off after %lu s, %lu lines, %lu bytes, %s", (unsigned long)((millis() - poweredAt) / 1000),
                 (unsigned long)lines, (unsigned long)bytes, fix);
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
    char fix[32];
    formatFix(fix, sizeof(fix));
    logger::line("gps: on %lu s, %s, %d sat, %lu lines (%lu cut, %lu bad sum), %lu B, RESETB %s",
                 (unsigned long)((millis() - poweredAt) / 1000), fix, lastSatellites, (unsigned long)lines,
                 (unsigned long)longLines, (unsigned long)badChecksums, (unsigned long)bytes,
                 digitalRead(GPS_RESETB_OUT) == HIGH ? "high" : "low");
}

} // namespace gps
