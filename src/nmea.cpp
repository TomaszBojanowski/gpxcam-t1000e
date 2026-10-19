#include "nmea.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

uint8_t xorOf(const char *begin, const char *end)
{
    uint8_t sum = 0;
    for (const char *p = begin; p < end; p++)
        sum ^= (uint8_t)*p;
    return sum;
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

} // namespace nmea
