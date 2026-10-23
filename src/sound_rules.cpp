#include "sound_rules.h"

#include <string.h>

namespace sound_rules {

namespace {

constexpr Setting SETTINGS[] = {Setting::Loud, Setting::Quiet, Setting::Off};

} // namespace

uint8_t dutyPercent(Setting setting, Kind kind)
{
    switch (setting) {
    case Setting::Loud:
        return LOUD_DUTY_PERCENT;
    case Setting::Quiet:
        return QUIET_DUTY_PERCENT;
    case Setting::Off:
        return kind == Kind::HoldAnswer ? QUIET_DUTY_PERCENT : 0;
    }
    return LOUD_DUTY_PERCENT;
}

const char *settingName(Setting setting)
{
    switch (setting) {
    case Setting::Loud:
        return "loud";
    case Setting::Quiet:
        return "quiet";
    case Setting::Off:
        return "off";
    }
    return "?";
}

bool settingFromName(const char *name, Setting *setting)
{
    for (Setting candidate : SETTINGS) {
        if (strcmp(name, settingName(candidate)) == 0) {
            *setting = candidate;
            return true;
        }
    }
    return false;
}

} // namespace sound_rules
