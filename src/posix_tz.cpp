#include "posix_tz.h"

#include <ctype.h>

#include "utc.h"

namespace posix_tz {

namespace {

constexpr int32_t SECONDS_PER_MINUTE = 60;
constexpr int32_t SECONDS_PER_HOUR = 3600;
constexpr int64_t SECONDS_PER_DAY = 86400;
constexpr int DAYS_PER_WEEK = 7;
constexpr int MIN_NAME_LENGTH = 3;                                   // POSIX
constexpr int MAX_OFFSET_HOURS = 24;                                 // POSIX
constexpr int32_t MAX_UTC_OFFSET_SECONDS = 15 * SECONDS_PER_HOUR;    // real zones: -12 h .. +14 h
constexpr int MAX_TRANSITION_HOURS = 167;                            // RFC 8536, 3.3.1
constexpr int MAX_NUMBER_DIGITS = 3;                                 // enough for 167
constexpr int LAST_WEEK = 5;                                         // "the last" in Mm.w.d
constexpr int32_t DEFAULT_TRANSITION_SECONDS = 2 * SECONDS_PER_HOUR; // POSIX: 02:00:00
constexpr int32_t DEFAULT_SUMMER_SHIFT_SECONDS = SECONDS_PER_HOUR;   // POSIX: one hour ahead
constexpr int32_t CAMERA_SUMMER_SHIFT_SECONDS = SECONDS_PER_HOUR;    // the camera's flag adds 1 h

bool skip(const char **p, char c)
{
    if (**p != c)
        return false;
    ++*p;
    return true;
}

// An unsigned decimal number of 1..3 digits within min..max.
bool parseNumber(const char **p, int min, int max, int *value)
{
    int number = 0;
    int digits = 0;
    while (isdigit((unsigned char)**p)) {
        if (++digits > MAX_NUMBER_DIGITS)
            return false;
        number = number * 10 + (**p - '0');
        ++*p;
    }
    if (digits == 0 || number < min || number > max)
        return false;
    *value = number;
    return true;
}

// A zone abbreviation: at least 3 letters, or at least 3 letters, digits, '+' or '-' in <>.
bool parseName(const char **p)
{
    const bool quoted = skip(p, '<');
    const char *start = *p;
    while (isalpha((unsigned char)**p) || (quoted && (isdigit((unsigned char)**p) || **p == '+' || **p == '-')))
        ++*p;
    return *p - start >= MIN_NAME_LENGTH && (!quoted || skip(p, '>'));
}

// [+|-]hh[:mm[:ss]] with hh at most maxHours, in seconds.
bool parseTime(const char **p, int maxHours, int32_t *seconds)
{
    const bool negative = skip(p, '-');
    if (!negative)
        skip(p, '+');
    int hours = 0;
    int minutes = 0;
    int secs = 0;
    if (!parseNumber(p, 0, maxHours, &hours))
        return false;
    if (skip(p, ':') && (!parseNumber(p, 0, 59, &minutes) || (skip(p, ':') && !parseNumber(p, 0, 59, &secs))))
        return false;
    const int32_t total = hours * SECONDS_PER_HOUR + minutes * SECONDS_PER_MINUTE + secs;
    *seconds = negative ? -total : total;
    return true;
}

// POSIX counts offsets west of UTC; the rule keeps them east, as the camera does.
bool parseOffset(const char **p, int32_t *eastSeconds)
{
    int32_t westSeconds = 0;
    if (!parseTime(p, MAX_OFFSET_HOURS, &westSeconds) || westSeconds % SECONDS_PER_MINUTE != 0)
        return false;
    *eastSeconds = -westSeconds;
    return true;
}

// Mm.w.d[/time]
bool parseTransition(const char **p, Transition *transition)
{
    int month = 0;
    int week = 0;
    int weekday = 0;
    if (!skip(p, 'M') || !parseNumber(p, 1, 12, &month) || !skip(p, '.') || !parseNumber(p, 1, LAST_WEEK, &week) ||
        !skip(p, '.') || !parseNumber(p, 0, DAYS_PER_WEEK - 1, &weekday))
        return false;
    int32_t timeSeconds = DEFAULT_TRANSITION_SECONDS;
    if (skip(p, '/') && !parseTime(p, MAX_TRANSITION_HOURS, &timeSeconds))
        return false;
    *transition = {(uint8_t)month, (uint8_t)week, (uint8_t)weekday, timeSeconds};
    return true;
}

// The moment of a change in a year; offsetBefore is the UTC offset in effect before it.
int64_t transitionMoment(const Transition &transition, int year, int32_t offsetBefore)
{
    const int64_t firstDay = utc::daysFromCivil(year, transition.month, 1);
    const int64_t lastDay = firstDay + utc::daysInMonth(year, transition.month) - 1;
    int64_t day = firstDay + (transition.weekday - utc::weekday(firstDay) + DAYS_PER_WEEK) % DAYS_PER_WEEK +
                  (int64_t)(transition.week - 1) * DAYS_PER_WEEK;
    if (day > lastDay) // week 5 in a month with only four such weekdays
        day -= DAYS_PER_WEEK;
    return day * SECONDS_PER_DAY + transition.timeSeconds - offsetBefore;
}

// The latest change at or before the moment decides. The changes of the years before and
// after count too: a transition time of up to 167 h can move a change into another year.
bool inSummerTime(const Rule &rule, int64_t unixSeconds)
{
    if (!rule.hasSummerTime)
        return false;
    const int year = utc::fromUnixSeconds(unixSeconds).year;
    bool summer = false;
    int64_t latest = INT64_MIN;
    for (int y = year - 1; y <= year + 1; y++) {
        const int64_t start = transitionMoment(rule.summerStart, y, rule.standardOffsetSeconds);
        const int64_t end = transitionMoment(rule.summerEnd, y, rule.summerOffsetSeconds);
        if (start <= unixSeconds && start > latest) {
            latest = start;
            summer = true;
        }
        if (end <= unixSeconds && end > latest) {
            latest = end;
            summer = false;
        }
    }
    return summer;
}

bool withinUtcOffsetLimit(int32_t seconds)
{
    return seconds >= -MAX_UTC_OFFSET_SECONDS && seconds <= MAX_UTC_OFFSET_SECONDS;
}

int16_t toMinutes(int32_t seconds)
{
    return (int16_t)(seconds / SECONDS_PER_MINUTE);
}

} // namespace

bool parse(const char *text, Rule *rule)
{
    const char *p = text;
    Rule parsed = {};
    if (!parseName(&p) || !parseOffset(&p, &parsed.standardOffsetSeconds))
        return false;
    if (*p != '\0') {
        if (!parseName(&p))
            return false;
        parsed.hasSummerTime = true;
        parsed.summerOffsetSeconds = parsed.standardOffsetSeconds + DEFAULT_SUMMER_SHIFT_SECONDS;
        if (*p != ',' && !parseOffset(&p, &parsed.summerOffsetSeconds))
            return false;
        if (!skip(&p, ',') || !parseTransition(&p, &parsed.summerStart) || !skip(&p, ',') ||
            !parseTransition(&p, &parsed.summerEnd) || *p != '\0')
            return false;
    }
    if (!withinUtcOffsetLimit(parsed.standardOffsetSeconds) ||
        (parsed.hasSummerTime && !withinUtcOffsetLimit(parsed.summerOffsetSeconds)))
        return false;
    *rule = parsed;
    return true;
}

int32_t utcOffsetSeconds(const Rule &rule, int64_t unixSeconds)
{
    return inSummerTime(rule, unixSeconds) ? rule.summerOffsetSeconds : rule.standardOffsetSeconds;
}

CameraZone cameraZone(const Rule &rule, int64_t unixSeconds)
{
    if (!rule.hasSummerTime)
        return {toMinutes(rule.standardOffsetSeconds), false};
    const bool summer = inSummerTime(rule, unixSeconds);
    const int32_t shift = rule.summerOffsetSeconds - rule.standardOffsetSeconds;
    if (shift == CAMERA_SUMMER_SHIFT_SECONDS)
        return {toMinutes(rule.standardOffsetSeconds), summer};
    if (shift == -CAMERA_SUMMER_SHIFT_SECONDS)
        return {toMinutes(rule.summerOffsetSeconds), !summer};
    return {toMinutes(summer ? rule.summerOffsetSeconds : rule.standardOffsetSeconds), false};
}

} // namespace posix_tz
