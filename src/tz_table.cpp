#include "tz_table.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

#include "tz_table_data.inc"

// Number of NUL-ended strings in a blob of string literals (size includes the literal's own
// final NUL, which ends no string).
constexpr size_t stringCount(const char *blob, size_t size)
{
    size_t count = 0;
    for (size_t i = 0; i + 1 < size; i++)
        if (blob[i] == '\0')
            count++;
    return count;
}

constexpr bool ruleIndexesValid()
{
    for (size_t i = 0; i < ZONE_COUNT; i++)
        if (ZONE_RULES[i] >= RULE_COUNT)
            return false;
    return true;
}

// The generated file checked at compile time, so a broken one cannot be built.
static_assert(stringCount(ZONE_NAMES, sizeof(ZONE_NAMES)) == ZONE_COUNT, "ZONE_NAMES does not match ZONE_COUNT");
static_assert(stringCount(RULES, sizeof(RULES)) == RULE_COUNT, "RULES does not match RULE_COUNT");
static_assert(ruleIndexesValid(), "ZONE_RULES points outside RULES");

bool sameIgnoringCase(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

const char *nthString(const char *blob, size_t index)
{
    for (size_t i = 0; i < index; i++)
        blob += strlen(blob) + 1;
    return blob;
}

} // namespace

namespace tz_table {

const char *tzdataVersion()
{
    return TZDATA_VERSION;
}

bool find(const char *name, const char **tableName, const char **rule)
{
    const char *entry = ZONE_NAMES;
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (sameIgnoringCase(name, entry)) {
            *tableName = entry;
            *rule = nthString(RULES, ZONE_RULES[i]);
            return true;
        }
        entry += strlen(entry) + 1;
    }
    return false;
}

} // namespace tz_table
