// Automatic GPS start-time test for runs without USB (M2 step 4). After a start delay it
// runs cycles: GPS on until the first fix (+ 60 s), GPS off in Backup mode, 5 min pause.
// The time to first fix of every cycle goes to the log history ('log').
#pragma once

namespace gps_test {

constexpr int MAX_CYCLES = 5;

// Starts the test; false if cycles is outside 1..MAX_CYCLES.
bool start(int cycles);

// Cancels a running test and switches the GPS off.
void stop();

bool active();

// Call from loop().
void poll();

} // namespace gps_test
