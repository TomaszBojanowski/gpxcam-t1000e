#include "bluetooth.h"

#include <bluefruit.h>

namespace {

// Bluefruit.begin(prph_count, central_count) (Bluefruit52Lib/src/bluefruit.cpp) sizes the
// SoftDevice RAM for these links and fails if it does not fit below the program's RAM
// (0x20006000, variants/t1000-e/nrf52840_s140_v7.ld).
constexpr uint8_t PERIPHERAL_LINKS = 0; // the card never accepts connections
constexpr uint8_t CENTRAL_LINKS = 1;    // the camera

} // namespace

namespace bluetooth {

bool begin()
{
    // Bluefruit blinks LED_BLUE while scanning and lights it while connected. The variant
    // has no such LED (LED_BLUE is -1), so keep the library away from the pins.
    Bluefruit.autoConnLed(false);
    return Bluefruit.begin(PERIPHERAL_LINKS, CENTRAL_LINKS);
}

} // namespace bluetooth
