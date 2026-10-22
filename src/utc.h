// Calendar arithmetic in UTC. No hardware dependencies (tested on the PC).
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

// Days since 1970-01-01 for a valid date (proleptic Gregorian calendar).
int64_t daysFromCivil(int year, int month, int day);

// Days in a month (1..12) of a year.
int daysInMonth(int year, int month);

// Day of the week of a day counted from 1970-01-01: 0 = Sunday .. 6 = Saturday.
int weekday(int64_t days);

} // namespace utc
