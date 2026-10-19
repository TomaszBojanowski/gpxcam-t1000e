# Shared helpers for flash.sh and dfu.sh (sourced, not executed).

APP_USB_ID="2886:8057"           # gpxcam firmware (boards/t1000-e.json)
BOARD_ID="nRF52840-T1000-E-v1"   # INFO_UF2.TXT of this card
SOFTDEVICE="S140 7.3.0"          # the firmware is linked for this SoftDevice (app at 0x27000)

# Prints the /dev/ttyACM* port of the gpxcam firmware.
# Fails if there is none or more than one (we never guess which card to use).
find_app_port() {
    local tty dev found=()
    for tty in /sys/class/tty/ttyACM*; do
        [[ -e $tty/device ]] || continue
        dev=$(readlink -f "$tty/device/..")
        if [[ "$(cat "$dev/idVendor" 2>/dev/null):$(cat "$dev/idProduct" 2>/dev/null)" == "$APP_USB_ID" ]]; then
            found+=("/dev/$(basename "$tty")")
        fi
    done
    ((${#found[@]} == 1)) || return 1
    echo "${found[0]}"
}

# Prints the mount point of the T1000-E bootloader drive (matched by Board-ID).
# Fails if there is none or more than one.
find_drive() {
    local info found=()
    for info in /run/media/"$USER"/*/INFO_UF2.TXT; do
        [[ -f $info ]] && grep -q "^Board-ID: $BOARD_ID" "$info" && found+=("$(dirname "$info")")
    done
    ((${#found[@]} == 1)) || return 1
    echo "${found[0]}"
}

no_drive() { ! find_drive >/dev/null; }

# Number of T1000-E bootloader drives (to explain a find_drive failure).
count_drives() {
    grep -l "^Board-ID: $BOARD_ID" /run/media/"$USER"/*/INFO_UF2.TXT 2>/dev/null | wc -l
}

# Asks the running gpxcam firmware to reboot into the UF2 bootloader.
# The port is set to 115200 first: closing it at 1200 baud would start
# the bootloader in serial-only mode (no drive).
send_dfu() {
    local port
    port=$(find_app_port) || return 1
    stty -F "$port" 115200 raw -echo
    exec 3<>"$port"
    printf 'dfu\r\n' >&3
    sleep 0.5
    exec 3>&-
}

# Runs command $1 every 0.25 s until it succeeds or $2 seconds pass; prints its output.
wait_for() {
    local cmd=$1 timeout=$2 i
    for ((i = 0; i < timeout * 4; i++)); do
        if $cmd; then return 0; fi
        sleep 0.25
    done
    return 1
}
