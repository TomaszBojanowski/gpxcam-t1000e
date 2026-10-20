#!/usr/bin/env bash
# Switches a card running the gpxcam firmware into UF2 flashing mode (the T1000-E drive appears).
# Flash is not changed. The card stays in the bootloader until it gets a .uf2 file.
set -euo pipefail
source "$(dirname "$0")/common.sh"

if drive=$(find_drive); then
    echo "The card is already in DFU mode: $drive"
    exit 0
fi
if ! send_dfu; then
    echo "No gpxcam firmware on USB (USB $APP_USB_ID, exactly one card)."
    exit 1
fi
echo "Sent 'dfu', waiting for the T1000-E drive..."
if drive=$(wait_for find_drive 20); then
    echo "The card is in DFU mode: $drive"
    sed 's/^/  /' "$drive/INFO_UF2.TXT"
else
    echo "The drive did not appear within 20 s."
    exit 1
fi
