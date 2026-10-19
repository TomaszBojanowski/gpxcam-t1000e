#include "utc.h"

namespace utc {

namespace {

constexpr int64_t SECONDS_PER_DAY = 86400;
constexpr int64_t SECONDS_PER_HOUR = 3600;
constexpr int SUNDAY = 0;
constexpr int THURSDAY = 4; // 1970-01-01
constexpr int DAYS_PER_WEEK = 7;
constexpr uint8_t MARCH = 3;
constexpr uint8_t OCTOBER = 10;
constexpr int64_t SUMMER_TIME_CHANGE_HOUR = 1; // 01:00 UTC

bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month)
{
    static const uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (month == 2 && isLeapYear(year)) ? 29 : DAYS[month - 1];
}

// Days since 1970-01-01 for a proleptic Gregorian date; the days_from_civil algorithm
// from H. Hinnant, "chrono-Compatible Low-Level Date Algorithms".
int64_t daysFromCivil(int year, int month, int day)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

// The inverse, civil_from_days, from the same source.
void civilFromDays(int64_t days, int *year, int *month, int *day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t monthIndex = (5 * dayOfYear + 2) / 153; // 0 = March
    *day = (int)(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
    *month = (int)(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
    *year = (int)(yearOfEra + era * 400 + (*month <= 2));
}

int weekday(int64_t days) // 0 = Sunday
{
    return (int)(((days % DAYS_PER_WEEK) + DAYS_PER_WEEK + THURSDAY) % DAYS_PER_WEEK);
}

int64_t lastSunday(int year, int month) // in days since 1970-01-01
{
    const int64_t lastDay = daysFromCivil(year, month, daysInMonth(year, month));
    return lastDay - (weekday(lastDay) - SUNDAY);
}

} // namespace

bool isValid(const DateTime &t)
{
    return t.month >= 1 && t.month <= 12 && t.day >= 1 && t.day <= daysInMonth(t.year, t.month) && t.hour < 24 &&
           t.minute < 60 && t.second < 60;
}

int64_t toUnixSeconds(const DateTime &t)
{
    return daysFromCivil(t.year, t.month, t.day) * SECONDS_PER_DAY + t.hour * SECONDS_PER_HOUR + t.minute * 60 +
           t.second;
}

DateTime fromUnixSeconds(int64_t seconds)
{
    int64_t days = seconds / SECONDS_PER_DAY;
    int64_t rest = seconds % SECONDS_PER_DAY;
    if (rest < 0) {
        rest += SECONDS_PER_DAY;
        days--;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    civilFromDays(days, &year, &month, &day);
    DateTime t;
    t.year = (uint16_t)year;
    t.month = (uint8_t)month;
    t.day = (uint8_t)day;
    t.hour = (uint8_t)(rest / SECONDS_PER_HOUR);
    t.minute = (uint8_t)(rest % SECONDS_PER_HOUR / 60);
    t.second = (uint8_t)(rest % 60);
    return t;
}

bool euSummerTime(int64_t unixSeconds)
{
    const int year = fromUnixSeconds(unixSeconds).year;
    const int64_t start = lastSunday(year, MARCH) * SECONDS_PER_DAY + SUMMER_TIME_CHANGE_HOUR * SECONDS_PER_HOUR;
    const int64_t end = lastSunday(year, OCTOBER) * SECONDS_PER_DAY + SUMMER_TIME_CHANGE_HOUR * SECONDS_PER_HOUR;
    return unixSeconds >= start && unixSeconds < end;
}

} // namespace utc
