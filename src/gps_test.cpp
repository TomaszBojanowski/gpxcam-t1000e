#include "gps_test.h"

#include <Arduino.h>

#include "gps.h"
#include "log.h"

namespace {

constexpr uint32_t MINUTE_MS = 60000;
constexpr uint32_t START_DELAY_MS = MINUTE_MS;     // time to unplug USB and put the card at a window
constexpr uint32_t FIX_TIMEOUT_MS = 15 * MINUTE_MS; // give up a cycle without a fix
constexpr uint32_t AFTER_FIX_MS = MINUTE_MS;       // keep receiving so the GPS has data for a hot start
constexpr uint32_t PAUSE_MS = 5 * MINUTE_MS;       // GPS off (Backup mode) between cycles

enum class Phase { Idle, Waiting, Searching, AfterFix, Paused };

Phase phase = Phase::Idle;
int cycle = 0;
int cycles = 0;
uint32_t phaseStart = 0;

void enter(Phase next)
{
    phase = next;
    phaseStart = millis();
}

void startCycle()
{
    cycle++;
    logger::line("gps test %d/%d: GPS on", cycle, cycles);
    gps::on();
    enter(Phase::Searching);
}

void endCycle()
{
    gps::off(); // Backup mode, logs its own line
    if (cycle < cycles) {
        logger::line("gps test %d/%d: next GPS start in %lu min", cycle, cycles, (unsigned long)(PAUSE_MS / MINUTE_MS));
        enter(Phase::Paused);
    } else {
        logger::line("gps test: done");
        phase = Phase::Idle;
    }
}

} // namespace

namespace gps_test {

bool start(int count)
{
    if (count < 1 || count > MAX_CYCLES)
        return false;
    stop();
    cycles = count;
    cycle = 0;
    enter(Phase::Waiting);
    logger::line("gps test: %d cycles, first GPS start in %lu s - unplug USB, put the card at a window", cycles,
                 (unsigned long)(START_DELAY_MS / 1000));
    return true;
}

void stop()
{
    if (phase == Phase::Idle)
        return;
    phase = Phase::Idle;
    gps::off();
    logger::line("gps test: stopped");
}

bool active()
{
    return phase != Phase::Idle;
}

void poll()
{
    const uint32_t elapsed = millis() - phaseStart;
    uint32_t fixSeconds = 0;
    switch (phase) {
    case Phase::Idle:
        break;
    case Phase::Waiting:
        if (elapsed >= START_DELAY_MS)
            startCycle();
        break;
    case Phase::Searching:
        if (gps::firstFix(&fixSeconds)) {
            logger::line("gps test %d/%d: fix after %lu s", cycle, cycles, (unsigned long)fixSeconds);
            enter(Phase::AfterFix);
        } else if (elapsed >= FIX_TIMEOUT_MS) {
            logger::line("gps test %d/%d: no fix in %lu min", cycle, cycles, (unsigned long)(FIX_TIMEOUT_MS / MINUTE_MS));
            endCycle();
        }
        break;
    case Phase::AfterFix:
        if (elapsed >= AFTER_FIX_MS)
            endCycle();
        break;
    case Phase::Paused:
        if (elapsed >= PAUSE_MS)
            startCycle();
        break;
    }
}

} // namespace gps_test
