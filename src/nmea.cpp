#include "nmea.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr int32_t E7 = 10000000;
constexpr int MINUTE_FRACTION_DIGITS = 6; // minutes kept in units of 10^-6 minute
constexpr int64_t MINUTE_UNIT = 1000000;
constexpr int64_t MINUTE_UNITS_PER_E7 = 6; // 10^-6 minute = 1/6 * 10^-7 degree
constexpr int64_t MAX_ALTITUDE_M = 100000;  // far beyond any real altitude; keeps the numbers small

uint8_t xorOf(const char *begin, const char *end)
{
    uint8_t sum = 0;
    for (const char *p = begin; p < end; p++)
        sum ^= (uint8_t)*p;
    return sum;
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// Reads `count` digits as a number; false if any is not a digit.
bool digits(const char *p, int count, int32_t *value)
{
    int32_t v = 0;
    for (int i = 0; i < count; i++) {
        if (!isDigit(p[i]))
            return false;
        v = v * 10 + (p[i] - '0');
    }
    *value = v;
    return true;
}

bool fieldEnd(char c)
{
    return c == ',' || c == '*' || c == '\0';
}

// "hhmmss" with an optional fraction ".s", ".ss" or ".sss".
bool parseTime(const char *f, nmea::Time *t)
{
    int32_t h = 0, m = 0, s = 0;
    if (f == nullptr || !digits(f, 2, &h) || !digits(f + 2, 2, &m) || !digits(f + 4, 2, &s))
        return false;
    // second 60 only during a leap second
    if (h > 23 || m > 59 || s > 60)
        return false;
    const char *p = f + 6;
    int32_t ms = 0;
    if (*p == '.') {
        p++;
        int32_t scale = 100;
        for (; isDigit(*p); p++) {
            ms += (*p - '0') * scale; // digits after the third are dropped
            scale /= 10;
        }
    }
    if (!fieldEnd(*p))
        return false;
    *t = {(uint8_t)h, (uint8_t)m, (uint8_t)s, (uint16_t)ms};
    return true;
}

// "ddmmyy"
bool parseDate(const char *f, nmea::Date *d)
{
    int32_t day = 0, month = 0, yy = 0;
    if (f == nullptr || !digits(f, 2, &day) || !digits(f + 2, 2, &month) || !digits(f + 4, 2, &yy) ||
        !fieldEnd(f[6]))
        return false;
    if (day < 1 || day > 31 || month < 1 || month > 12)
        return false;
    *d = {(uint16_t)(yy >= 80 ? 1900 + yy : 2000 + yy), (uint8_t)month, (uint8_t)day};
    return true;
}

// "ddmm.mmmm" (degreeDigits 2) or "dddmm.mmmm" (3) plus hemisphere, to degrees * 10^7.
bool parseCoordinate(const char *f, const char *hemisphere, int degreeDigits, int32_t maxDegrees,
                     char positive, char negative, int32_t *valueE7)
{
    int32_t degrees = 0, wholeMinutes = 0;
    if (f == nullptr || hemisphere == nullptr || !digits(f, degreeDigits, &degrees) ||
        !digits(f + degreeDigits, 2, &wholeMinutes))
        return false;
    const char *p = f + degreeDigits + 2;
    int64_t minuteUnits = (int64_t)wholeMinutes * MINUTE_UNIT;
    if (*p == '.') {
        p++;
        int64_t scale = MINUTE_UNIT / 10;
        for (int i = 0; isDigit(*p); p++, i++) {
            if (i < MINUTE_FRACTION_DIGITS)
                minuteUnits += (*p - '0') * scale;
            scale /= 10;
        }
    }
    if (!fieldEnd(*p) || wholeMinutes > 59 || degrees > maxDegrees)
        return false;
    const int64_t e7 = (int64_t)degrees * E7 + (minuteUnits + MINUTE_UNITS_PER_E7 / 2) / MINUTE_UNITS_PER_E7;
    if (e7 > (int64_t)maxDegrees * E7)
        return false;
    if (*hemisphere == positive && fieldEnd(hemisphere[1]))
        *valueE7 = (int32_t)e7;
    else if (*hemisphere == negative && fieldEnd(hemisphere[1]))
        *valueE7 = (int32_t)-e7;
    else
        return false;
    return true;
}

// "[-]m[.f]" metres to decimetres, rounded half away from zero; digits after the
// second decimal are ignored.
bool parseDecimetres(const char *f, int32_t *dm)
{
    if (f == nullptr || fieldEnd(*f))
        return false;
    const bool negative = *f == '-';
    const char *p = negative ? f + 1 : f;
    if (!isDigit(*p))
        return false;
    int64_t metres = 0;
    for (; isDigit(*p); p++) {
        metres = metres * 10 + (*p - '0');
        if (metres > MAX_ALTITUDE_M)
            return false;
    }
    int64_t centimetres = metres * 100;
    if (*p == '.') {
        p++;
        int64_t scale = 10;
        for (; isDigit(*p); p++) {
            centimetres += (*p - '0') * scale;
            scale /= 10;
        }
    }
    if (!fieldEnd(*p))
        return false;
    const int32_t rounded = (int32_t)((centimetres + 5) / 10);
    *dm = negative ? -rounded : rounded;
    return true;
}

bool emptyField(const char *f)
{
    return f != nullptr && fieldEnd(*f);
}

} // namespace

namespace nmea {

bool checksumOk(const char *s)
{
    if (s[0] != '$')
        return false;
    const char *star = strchr(s, '*');
    if (star == nullptr || !isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2]) ||
        star[3] != '\0')
        return false;
    return strtoul(star + 1, nullptr, 16) == xorOf(s + 1, star);
}

bool isType(const char *s, const char *type)
{
    return strlen(s) > 6 && s[0] == '$' && strncmp(s + 3, type, 3) == 0 && s[6] == ',';
}

bool startsWith(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

const char *field(const char *s, unsigned index)
{
    for (; index > 0; index--) {
        s = strpbrk(s, ",*");
        if (s == nullptr || *s == '*')
            return nullptr;
        s++;
    }
    return s;
}

bool fieldInt(const char *s, unsigned index, long minValue, long maxValue, long *value)
{
    const char *f = field(s, index);
    if (f == nullptr || *f == ',' || *f == '*' || *f == '\0')
        return false;
    char *end = nullptr;
    const long v = strtol(f, &end, 10);
    if (end == f || (*end != ',' && *end != '*' && *end != '\0') || v < minValue || v > maxValue)
        return false;
    *value = v;
    return true;
}

size_t buildSentence(const char *body, char *out, size_t size)
{
    const int len = snprintf(out, size, "$%s*%02X\r\n", body, xorOf(body, body + strlen(body)));
    return (len > 0 && (size_t)len < size) ? (size_t)len : 0;
}

bool parseRmc(const char *s, Rmc *out)
{
    if (!isType(s, "RMC") || !checksumOk(s))
        return false;
    const char *status = field(s, 2);
    if (status == nullptr || !(status[0] == 'A' || status[0] == 'V') || !fieldEnd(status[1]))
        return false;
    Rmc r = {};
    r.valid = status[0] == 'A';
    r.hasTime = parseTime(field(s, 1), &r.time);
    r.hasDate = parseDate(field(s, 9), &r.date);
    if (r.valid) {
        r.hasPosition = parseCoordinate(field(s, 3), field(s, 4), 2, 90, 'N', 'S', &r.latitudeE7) &&
                        parseCoordinate(field(s, 5), field(s, 6), 3, 180, 'E', 'W', &r.longitudeE7);
        if (!r.hasPosition) {
            r.latitudeE7 = 0;
            r.longitudeE7 = 0;
        }
    }
    // A time field that is present but malformed makes the sentence malformed.
    if (!r.hasTime && !emptyField(field(s, 1)))
        return false;
    *out = r;
    return true;
}

bool parseGga(const char *s, Gga *out)
{
    if (!isType(s, "GGA") || !checksumOk(s))
        return false;
    Gga g = {};
    long quality = 0;
    if (!fieldInt(s, 6, 0, 9, &quality))
        return false;
    g.quality = (uint8_t)quality;
    long satellites = 0;
    g.satellites = fieldInt(s, 7, 0, 99, &satellites) ? (int8_t)satellites : (int8_t)-1;
    g.hasTime = parseTime(field(s, 1), &g.time);
    const char *unit = field(s, 10);
    g.hasAltitude = unit != nullptr && unit[0] == 'M' && parseDecimetres(field(s, 9), &g.altitudeDm);
    if (!g.hasTime && !emptyField(field(s, 1)))
        return false;
    *out = g;
    return true;
}

} // namespace nmea
