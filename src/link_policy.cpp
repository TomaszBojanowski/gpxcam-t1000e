#include "link_policy.h"

#include <math.h>
#include <string.h>

namespace link_policy {

namespace {

constexpr uint32_t PAUSES_MS[] = {5000, 15000, 60000};
constexpr size_t PAUSE_STEPS = sizeof(PAUSES_MS) / sizeof(PAUSES_MS[0]);

constexpr Updates UPDATES[] = {Updates::Normal, Updates::Frequent};

constexpr double EARTH_RADIUS_M = 6371000.0; // mean radius
constexpr double E7 = 1e7;
constexpr double RADIANS_PER_DEGREE = 3.14159265358979323846 / 180.0; // not PI: Arduino.h defines it
constexpr int64_t HALF_TURN_E7 = 1800000000; // 180 degrees
constexpr int64_t FULL_TURN_E7 = 3600000000;

double toRadians(double degreesE7)
{
    return degreesE7 / E7 * RADIANS_PER_DEGREE;
}

} // namespace

bool shouldConnect(const pairing_record::Record &pairing, const camera_list::Entry &camera, uint32_t nowMs)
{
    return pairing.paired && camera.used &&
           memcmp(camera.last.camera.serial, pairing.serial, lumix::SERIAL_LENGTH) == 0 &&
           camera.last.camera.state == lumix::STATE_ON && nowMs - camera.lastSeenMs <= REPORT_FRESH_MS &&
           camera.last.rssi >= MIN_RSSI_DBM;
}

SearchMode searchMode(bool cameraSeen, uint32_t msSinceSeen, uint8_t lastState, uint32_t msSearching,
                      uint32_t msInStandby)
{
    if (!cameraSeen)
        return msSearching >= RARE_AFTER_MS ? SearchMode::Rare : SearchMode::Normal;
    if (msSinceSeen >= RARE_AFTER_MS)
        return SearchMode::Rare;
    if (lastState != lumix::STATE_STANDBY)
        return SearchMode::Normal;
    return msInStandby >= LONG_STANDBY_AFTER_MS ? SearchMode::LongStandby : SearchMode::Standby;
}

bool gpsNeeded(SearchMode mode)
{
    return mode == SearchMode::Normal || mode == SearchMode::Standby;
}

uint32_t pauseMs(uint32_t failuresInARow)
{
    if (failuresInARow == 0)
        return 0;
    return PAUSES_MS[failuresInARow < PAUSE_STEPS ? failuresInARow - 1 : PAUSE_STEPS - 1];
}

const char *updatesName(Updates updates)
{
    switch (updates) {
    case Updates::Normal:
        return "normal";
    case Updates::Frequent:
        return "frequent";
    }
    return "?";
}

bool updatesFromName(const char *name, Updates *updates)
{
    for (Updates candidate : UPDATES) {
        if (strcmp(name, updatesName(candidate)) == 0) {
            *updates = candidate;
            return true;
        }
    }
    return false;
}

Send locationDue(const LocationTiming &timing, bool sentBefore, uint32_t msSinceLast, uint32_t metresMoved,
                 bool cameraAsked)
{
    if (!sentBefore)
        return Send::First;
    if (msSinceLast >= timing.periodMs)
        return Send::Periodic;
    if (msSinceLast < timing.minGapMs)
        return Send::No;
    if (metresMoved > timing.moveMetres)
        return Send::Moved;
    if (cameraAsked)
        return Send::Asked;
    return Send::No;
}

void RateGuard::connectionStarted()
{
    slow = nextSlow;
    nextSlow = false;
    late = 0;
}

bool RateGuard::answered(uint32_t answerMs)
{
    if (answerMs <= LATE_ANSWER_MS)
        return false;
    if (late < UINT8_MAX)
        late++;
    if (slow || late < LATE_ANSWERS_TO_SLOW_DOWN)
        return false;
    slow = true;
    return true;
}

void RateGuard::connectionEnded(bool closedByCamera)
{
    nextSlow = !closedByCamera;
}

uint32_t distanceMetres(int32_t latitude1E7, int32_t longitude1E7, int32_t latitude2E7, int32_t longitude2E7)
{
    int64_t dLon = (int64_t)longitude2E7 - longitude1E7;
    if (dLon > HALF_TURN_E7)
        dLon -= FULL_TURN_E7;
    else if (dLon < -HALF_TURN_E7)
        dLon += FULL_TURN_E7;
    const double meanLatitude = toRadians(((double)latitude1E7 + latitude2E7) / 2.0);
    const double x = toRadians((double)dLon) * cos(meanLatitude);
    const double y = toRadians((double)latitude2E7 - latitude1E7);
    return (uint32_t)lround(EARTH_RADIUS_M * sqrt(x * x + y * y));
}

bool refusalMeansUnknownCard(IntroductionWrite write, bool timeWritten)
{
    return write == IntroductionWrite::Time || !timeWritten;
}

bool earlyEndMeansUnknownCard(bool timeWritten, uint8_t *earlyEndsInARow)
{
    if (timeWritten) {
        *earlyEndsInARow = 0;
        return false;
    }
    if (*earlyEndsInARow < UINT8_MAX)
        (*earlyEndsInARow)++;
    return *earlyEndsInARow >= EARLY_ENDS_FOR_UNKNOWN_CARD;
}

bool olderRefusalMeansUnknownCard(uint8_t *refusalsInARow)
{
    if (*refusalsInARow < UINT8_MAX)
        (*refusalsInARow)++;
    return *refusalsInARow >= EARLY_ENDS_FOR_UNKNOWN_CARD;
}

} // namespace link_policy
