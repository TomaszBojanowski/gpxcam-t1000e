// GPS configuration after every power-on, in the GPS RAM only: check first, change
// only what differs. $PAIR513 (save to the GPS flash) is never sent (M2 decision).
#pragma once

namespace gps_setup {

// Starts the sequence; call right after the GPS is powered on.
void begin();

// Stops the sequence; call when the GPS is powered off.
void stop();

// Every received sentence with a valid checksum.
void handleSentence(const char *sentence);

// Timeouts and retries. Call from loop() while the GPS is on.
void poll();

const char *stateText();

} // namespace gps_setup
