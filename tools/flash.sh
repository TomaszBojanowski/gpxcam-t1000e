#!/usr/bin/env bash
# Flashes the firmware to the T1000-E, only through the UF2 drive of its bootloader.
# Usage: tools/flash.sh [-y] [file.uf2]
#   -y  do not ask before flashing (only when the owner has already agreed)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/common.sh"

assume_yes=0
if [[ ${1:-} == -y ]]; then assume_yes=1; shift; fi
uf2=${1:-$(dirname "$here")/.pio/build/t1000e/firmware.uf2}

# 1. The file: family and addresses. Without a passed check nothing happens.
python3 "$here/uf2check.py" "$uf2"

# 2. What will happen - and the consent, before the card even enters DFU
#    (after the 'dfu' command it stays in the bootloader until it gets a file).
cat <<EOF

What will happen:
  1. The card goes into the UF2 bootloader (the 'dfu' command, or by hand: button + cable twice).
  2. The script checks the drive: Board-ID $BOARD_ID and SoftDevice $SOFTDEVICE.
  3. $(basename "$uf2") is copied to the drive; the bootloader writes only the blocks in the file
     (the application area checked above) and starts the new firmware.
  The bootloader, the SoftDevice and the application data area (0xEA000-0xF4000) are not changed.
EOF
if ((!assume_yes)); then
    if [[ ! -t 0 ]]; then
        echo "No terminal to ask for consent - nothing done."
        echo "Run it in a normal terminal, or with -y (only when the owner has already agreed)."
        exit 1
    fi
    read -r -p "Flash? [y/N] " answer || answer=""
    [[ $answer == [yY] ]] || { echo "Stopped, the card was not touched."; exit 1; }
fi

# 3. The bootloader drive: already there, or the gpxcam firmware is asked to enter DFU.
if ! drive=$(find_drive); then
    (($(count_drives) <= 1)) || { echo "More than one T1000-E in DFU is connected - stopping."; exit 1; }
    if send_dfu; then
        echo "Sent 'dfu', waiting for the T1000-E drive..."
    else
        echo "No gpxcam firmware on USB. Enter DFU by hand:"
        echo "hold the button and plug the cable in twice quickly (the LED lights green). Waiting 60 s..."
    fi
    drive=$(wait_for find_drive 60) || { echo "No T1000-E drive, nothing flashed."; exit 1; }
fi
info="$drive/INFO_UF2.TXT"
echo "Bootloader drive: $drive"
sed 's/^/  /' "$info"

# 4. Second check: the right card and the SoftDevice the firmware is built for.
grep -q "^Board-ID: $BOARD_ID" "$info" || { echo "Wrong Board-ID - nothing flashed."; exit 1; }
grep -q "^SoftDevice: $SOFTDEVICE" "$info" || { echo "SoftDevice is not $SOFTDEVICE - nothing flashed."; exit 1; }

# 5. Copying. The bootloader resets the card right after the last block,
#    so an error reported at the very end of the copy can be normal.
if ! { cp "$uf2" "$drive/" && sync; }; then
    echo "(cp reported an error when the drive went away - checking whether the card started)"
fi
wait_for no_drive 20 || { echo "The drive did not go away - flashing may have failed."; exit 1; }
if port=$(wait_for find_app_port 20); then
    echo "Done. The firmware runs, port: $port"
else
    echo "The drive went away, but the gpxcam firmware did not show up on USB within 20 s."
    exit 1
fi
