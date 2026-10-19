#include "link_policy.h"

#include <math.h>
#include <string.h>

namespace link_policy {

namespace {

constexpr uint32_t PAUSES_MS[] = {5000, 15000, 60000};
constexpr size_t PAUSE_STEPS = sizeof(PAUSES_MS) / sizeof(PAUSES_MS[0]);

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
           camera.last.camera.state == lumix::STATE_ON && nowMs - camera.lastSeenMs <= REPORT_FRESH_MS;
}

uint32_t pauseMs(uint32_t failuresInARow)
{
    if (failuresInARow == 0)
        return 0;
    return PAUSES_MS[failuresInARow < PAUSE_STEPS ? failuresInARow - 1 : PAUSE_STEPS - 1];
}

Send locationDue(bool sentBefore, uint32_t msSinceLast, uint32_t metresMoved, bool cameraAsked)
{
    if (!sentBefore)
        return Send::First;
    if (msSinceLast >= PERIOD_MS)
        return Send::Periodic;
    if (msSinceLast < MIN_GAP_MS)
        return Send::No;
    if (metresMoved > MOVE_METRES)
        return Send::Moved;
    if (cameraAsked)
        return Send::Asked;
    return Send::No;
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

} // namespace link_policy
