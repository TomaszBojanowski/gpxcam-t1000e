#include "status_text.h"

#include <stdio.h>

namespace status_text {

namespace {

const char *chargerValue(battery::Charger charger)
{
    switch (charger) {
    case battery::Charger::NoUsb:
        return "no_usb";
    case battery::Charger::Charging:
        return "charging";
    case battery::Charger::Charged:
        return "charged";
    case battery::Charger::NotCharging:
        return "not_charging";
    }
    return "unknown";
}

const char *linkValue(link_control::State link)
{
    switch (link) {
    case link_control::State::Off:
        return "off";
    case link_control::State::Searching:
        return "searching";
    case link_control::State::Connecting:
        return "connecting";
    case link_control::State::Connected:
        return "connected";
    case link_control::State::Pausing:
        return "pausing";
    case link_control::State::Stopped:
        return "stopped";
    }
    return "unknown";
}

const char *pairPhaseValue(pairing::Phase phase)
{
    switch (phase) {
    case pairing::Phase::Idle:
        return "idle";
    case pairing::Phase::WaitingForFix:
        return "waiting_gps";
    case pairing::Phase::Searching:
        return "searching";
    case pairing::Phase::Introducing:
        return "connecting";
    case pairing::Phase::Pausing:
        return "pausing";
    }
    return "unknown";
}

const char *pairResultValue(pairing::Result result)
{
    switch (result) {
    case pairing::Result::None:
        return "none";
    case pairing::Result::Paired:
        return "paired";
    case pairing::Result::NotSaved:
        return "not_saved";
    case pairing::Result::NotPossible:
        return "not_possible";
    case pairing::Result::NoGpsFix:
        return "no_gps_fix";
    case pairing::Result::NoCamera:
        return "no_camera";
    case pairing::Result::NotInPairingMode:
        return "not_in_pairing_mode";
    case pairing::Result::Failed:
        return "failed";
    case pairing::Result::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

const char *onOffValue(bool on)
{
    return on ? "on" : "off";
}

} // namespace

void format(const State &state, void (*emit)(const char *line))
{
    char line[LINE_SIZE];
    snprintf(line, sizeof(line), "status: format=%u fw=%s git=%s", FORMAT, state.version, state.git);
    emit(line);
    snprintf(line, sizeof(line), "status: battery_mv=%lu battery_pct=%u charger=%s",
             (unsigned long)state.batteryMillivolts, (unsigned)state.batteryPercent, chargerValue(state.charger));
    emit(line);
    snprintf(line, sizeof(line), "status: gps=%s gps_fix=%d gps_sat=%u", onOffValue(state.gpsOn), state.gpsFix ? 1 : 0,
             (unsigned)state.gpsSatellites);
    emit(line);
    snprintf(line, sizeof(line), "status: paired=%d link=%s", state.paired ? 1 : 0, linkValue(state.link));
    emit(line);
    snprintf(line, sizeof(line), "status: pair=%s pair_attempt=%d pair_result=%s", pairPhaseValue(state.pairing.phase),
             state.pairing.attempt, pairResultValue(state.pairing.lastResult));
    emit(line);
    snprintf(line, sizeof(line), "status: clock=%s tz=%s", onOffValue(state.clockOn), state.timeZone);
    emit(line);
    snprintf(line, sizeof(line), "status: sound=%s", sound_rules::settingName(state.sound));
    emit(line);
    snprintf(line, sizeof(line), "status: updates=%s updates_now=%s", link_policy::updatesName(state.updates),
             link_policy::updatesName(state.updatesNow));
    emit(line);
    emit("status: end");
}

} // namespace status_text
