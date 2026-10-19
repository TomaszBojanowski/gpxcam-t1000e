#!/usr/bin/env bash
# Wgrywa program na T1000-E wyłącznie przez dysk UF2 bootloadera.
# Użycie: tools/flash.sh [-y] [plik.uf2]
#   -y  bez pytania o zgodę (tylko gdy zgoda padła wcześniej)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/common.sh"

assume_yes=0
if [[ ${1:-} == -y ]]; then assume_yes=1; shift; fi
uf2=${1:-$(dirname "$here")/.pio/build/t1000e/firmware.uf2}

# 1. Plik: rodzina i adresy. Bez pozytywnego wyniku nic nie robimy.
python3 "$here/uf2check.py" "$uf2"

# 2. Co się stanie – i zgoda, zanim karta w ogóle wejdzie w DFU
#    (po komendzie 'dfu' zostaje w bootloaderze, dopóki nie dostanie pliku).
cat <<EOF

Co się stanie:
  1. Karta przejdzie do bootloadera UF2 (komenda 'dfu' albo ręcznie: przycisk + 2x kabel).
  2. Skrypt sprawdzi dysk: Board-ID $BOARD_ID i SoftDevice $SOFTDEVICE.
  3. $(basename "$uf2") zostanie skopiowany na dysk; bootloader zapisze tylko bloki z pliku
     (obszar programu sprawdzony wyżej) i uruchomi nowy program.
  Bootloader, SoftDevice i obszar danych programu (0xEA000-0xF4000) nie są zmieniane.
EOF
if ((!assume_yes)); then
    if [[ ! -t 0 ]]; then
        echo "Brak terminala do pytania o zgodę – nic nie zrobiono."
        echo "Uruchom w zwykłym terminalu albo z -y (tylko gdy zgoda padła wcześniej)."
        exit 1
    fi
    read -r -p "Wgrać? [t/N] " answer || answer=""
    [[ $answer == [tTyY] ]] || { echo "Przerwano, karta nie została ruszona."; exit 1; }
fi

# 3. Dysk bootloadera: już jest, albo prosimy program gpxcam o wejście w DFU.
if ! drive=$(find_drive); then
    (($(count_drives) <= 1)) || { echo "Podłączono więcej niż jedną kartę T1000-E w DFU – przerywam."; exit 1; }
    if send_dfu; then
        echo "Wysłano 'dfu', czekam na dysk T1000-E..."
    else
        echo "Nie widzę programu gpxcam na USB. Wejdź w DFU ręcznie:"
        echo "przytrzymaj przycisk i dwa razy szybko podłącz kabel (dioda świeci na zielono). Czekam 60 s..."
    fi
    drive=$(wait_for find_drive 60) || { echo "Brak dysku T1000-E, nic nie wgrano."; exit 1; }
fi
info="$drive/INFO_UF2.TXT"
echo "Dysk bootloadera: $drive"
sed 's/^/  /' "$info"

# 4. Druga kontrola: właściwa karta i SoftDevice, pod który zbudowano program.
grep -q "^Board-ID: $BOARD_ID" "$info" || { echo "Zły Board-ID – nic nie wgrano."; exit 1; }
grep -q "^SoftDevice: $SOFTDEVICE" "$info" || { echo "SoftDevice inny niż $SOFTDEVICE – nic nie wgrano."; exit 1; }

# 5. Kopiowanie. Bootloader resetuje kartę zaraz po ostatnim bloku,
#    więc błąd zgłoszony na samym końcu kopiowania bywa normalny.
if ! { cp "$uf2" "$drive/" && sync; }; then
    echo "(cp zgłosił błąd przy odłączaniu dysku – sprawdzam, czy karta wystartowała)"
fi
wait_for no_drive 20 || { echo "Dysk nie zniknął – wgranie mogło się nie udać."; exit 1; }
if port=$(wait_for find_app_port 20); then
    echo "Gotowe. Program działa, port: $port"
else
    echo "Dysk zniknął, ale program gpxcam nie zgłosił się po USB w ciągu 20 s."
    exit 1
fi
