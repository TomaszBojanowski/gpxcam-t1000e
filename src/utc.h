// Calendar arithmetic in UTC and the EU summer-time rule. No hardware dependencies
// (tested on the PC).
#pragma once

#include <stdint.h>

namespace utc {

struct DateTime {
    uint16_t year;
    uint8_t month; // 1..12
    uint8_t day;   // 1..31
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

// True for a real date (leap years included) and a time 00:00:00..23:59:59.
bool isValid(const DateTime &t);

// Seconds since 1970-01-01 00:00:00 UTC without leap seconds (Unix time), for a valid t.
int64_t toUnixSeconds(const DateTime &t);

DateTime fromUnixSeconds(int64_t seconds);

// EU rule (Directive 2000/84/EC): summer time from 01:00 UTC on the last Sunday of
// March until 01:00 UTC on the last Sunday of October.
bool euSummerTime(int64_t unixSeconds);

} // namespace utc
