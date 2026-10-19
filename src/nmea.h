// NMEA 0183 helpers with no hardware dependencies (to be tested on the PC).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nmea {

struct Time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
};

struct Date {
    uint16_t year; // four digits; NMEA has two: 80..99 -> 1980..1999, 00..79 -> 2000..2079
    uint8_t month;
    uint8_t day;
};

// $xxRMC: recommended minimum data. NMEA 0183 fields: 1 time, 2 status (A valid, V not),
// 3-4 latitude ddmm.mmmm + N/S, 5-6 longitude dddmm.mmmm + E/W, 9 date ddmmyy.
struct Rmc {
    bool hasTime;
    Time time;
    bool hasDate;
    Date date;
    bool valid;         // status A
    bool hasPosition;   // only with status A and both coordinates present
    int32_t latitudeE7; // degrees * 10^7, north positive (format of the LUMIX location)
    int32_t longitudeE7; // degrees * 10^7, east positive
};

// $xxGGA: fix data. Fields: 1 time, 6 fix quality (0 = no fix), 7 satellites in use,
// 9-10 altitude above mean sea level + "M".
struct Gga {
    bool hasTime;
    Time time;
    uint8_t quality;
    int8_t satellites; // -1 when the field is empty
    bool hasAltitude;
    int32_t altitudeDm; // decimetres
};

// Parse a sentence (checksum checked first). False if it is not this type or malformed.
bool parseRmc(const char *sentence, Rmc *out);
bool parseGga(const char *sentence, Gga *out);

// "$...*hh": XOR of all characters between '$' and '*', as two hex digits at the end.
bool checksumOk(const char *sentence);

// True for "$xxTTT," where TTT is the sentence type and xx any talker (GP, GN, ...).
bool isType(const char *sentence, const char *type);

// True when the sentence starts with prefix (e.g. "$PAIR001,").
bool startsWith(const char *sentence, const char *prefix);

// Start of comma-separated field `index` (0 is the address field "$xxTTT"), or nullptr.
// A field ends at ',' or '*'.
const char *field(const char *sentence, unsigned index);

// Reads field `index` as a decimal number (optional leading '-'); false if it is
// missing, empty, not a number or outside [minValue, maxValue].
bool fieldInt(const char *sentence, unsigned index, long minValue, long maxValue, long *value);

// Writes "$<body>*hh\r\n" to out. Returns its length, or 0 if it does not fit.
size_t buildSentence(const char *body, char *out, size_t size);

} // namespace nmea
