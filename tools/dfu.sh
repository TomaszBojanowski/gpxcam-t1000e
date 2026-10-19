#!/usr/bin/env bash
# Przełącza kartę z działającym programem gpxcam w tryb wgrywania UF2 (pojawia się dysk T1000-E).
# Flash nie jest zmieniany. Karta zostaje w bootloaderze, dopóki nie dostanie pliku .uf2.
set -euo pipefail
source "$(dirname "$0")/common.sh"

if drive=$(find_drive); then
    echo "Karta już jest w trybie DFU: $drive"
    exit 0
fi
if ! send_dfu; then
    echo "Nie widzę programu gpxcam na USB (USB $APP_USB_ID, dokładnie jedna karta)."
    exit 1
fi
echo "Wysłano 'dfu', czekam na dysk T1000-E..."
if drive=$(wait_for find_drive 20); then
    echo "Karta w trybie DFU: $drive"
    sed 's/^/  /' "$drive/INFO_UF2.TXT"
else
    echo "Dysk się nie pojawił w ciągu 20 s."
    exit 1
fi
