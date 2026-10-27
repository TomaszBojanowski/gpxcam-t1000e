// Fake of the SoftDevice header ble_hci.h for the host tests: the values the card's sources use, as in
// S140 7.3.0 (framework-arduinoadafruitnrf52 cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API/include/
// ble_hci.h).
#pragma once

#define BLE_HCI_CONNECTION_TIMEOUT 0x08
#define BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION 0x13
#define BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION 0x16
#define BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED 0x3E
