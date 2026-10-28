// Fake of the SoftDevice header ble_gatt.h for the host tests: the values the card's sources use, as
// in S140 7.3.0 (framework-arduinoadafruitnrf52 cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API/
// include/ble_gatt.h).
#pragma once

#define BLE_GATT_ATT_MTU_DEFAULT 23
#define BLE_GATT_STATUS_SUCCESS 0x0000
#define BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED 0x0103
#define BLE_GATT_STATUS_ATTERR_ATTRIBUTE_NOT_FOUND 0x010A
