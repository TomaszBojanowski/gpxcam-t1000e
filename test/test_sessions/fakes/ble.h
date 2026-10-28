// Fake of the SoftDevice header ble.h for the host tests: camera_link.h only names ble_evt_t. As the
// real one, it brings the GATT and HCI values with it (ble.h includes ble_gatt.h, and ble_gap.h ble_hci.h).
#pragma once

#include <stdint.h>

#include "ble_gatt.h"
#include "ble_hci.h"

struct ble_evt_t {
    uint8_t unused;
};
