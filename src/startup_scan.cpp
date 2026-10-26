#include "startup_scan.h"

#include "bluetooth.h"
#include "link_control.h"
#include "log.h"
#include "pairing.h"
#include "pairing_store.h"

namespace {

constexpr uint32_t START_AFTER_MS = 10000; // after start-up: USB, GPS and the first log lines settle first
constexpr uint32_t DURATION_MS = 30000;    // as long as one 'diag scan' run

enum class State : uint8_t { Waiting, Scanning, Done };

State state = State::Waiting;
uint32_t startMs = 0;

// Only a card that is not paired, and only when nothing else uses the scan.
bool wanted()
{
    return bluetooth::ready() && pairing_store::ready() && !pairing_store::record().paired && !pairing::active() &&
           !link_control::active() && !bluetooth::scanning();
}

void start(uint32_t nowMs)
{
    state = State::Done; // once per start, whatever happens below
    if (!wanted())
        return;
    logger::line("startup scan: not paired - listening %lu s for LUMIX cameras ('log' or Copy log shows them)",
                 (unsigned long)(DURATION_MS / 1000));
    bluetooth::setNormalScanTiming();
    bluetooth::scanOn();
    if (!bluetooth::scanning())
        return; // scanOn() logged why
    startMs = nowMs;
    state = State::Scanning;
}

} // namespace

namespace startup_scan {

void poll(uint32_t nowMs)
{
    switch (state) {
    case State::Waiting:
        if (nowMs >= START_AFTER_MS) // millis() since start: no wrap this early
            start(nowMs);
        break;
    case State::Scanning:
        if (!bluetooth::scanning()) { // switched off by someone else (it logged its summary)
            state = State::Done;
        } else if (nowMs - startMs >= DURATION_MS) {
            stop();
        }
        break;
    case State::Done:
        break;
    }
}

bool running()
{
    return state == State::Scanning;
}

void stop()
{
    if (state != State::Scanning)
        return;
    state = State::Done;
    logger::line("startup scan: done");
    bluetooth::releaseScan(); // logs the scan's summary with the reports per channel
}

} // namespace startup_scan
