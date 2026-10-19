# gpxcam-t1000e

Program na kartę Seeed SenseCAP **T1000-E**, który ma przekazywać pozycję z jej GPS-u
do aparatu Panasonic LUMIX S5II przez Bluetooth LE – bez telefonu.
Protokół jak w aplikacji gpxcam na Androida.

Stan: **M1 w toku** – wszystkie układy wyłączone, dioda mruga, wersja w logu po USB, wejście w tryb
wgrywania komendą `dfu`, melodia przy starcie, przycisk: krótkie naciśnięcie = cichy sygnał,
przytrzymanie 3 s = melodia wyłączenia (samo wyłączanie dojdzie w kolejnym kroku), napięcie baterii
i stan ładowania w logu co 60 s.

## Wymagania

- PlatformIO: `uv tool install platformio`
- Użytkownik w grupie `dialout` (dostęp do `/dev/ttyACM0`)

## Budowanie

```
pio run
```

Wynik: `.pio/build/t1000e/firmware.uf2`. Po każdej kompilacji `tools/uf2check.py` sprawdza plik
(rodzina 0xADA52840, pierwszy blok 0x27000, wszystkie bloki w 0x27000–0xED000).
Plik, który nie przejdzie sprawdzenia, jest usuwany.

## Wgrywanie (tylko UF2)

```
tools/flash.sh          # albo: pio run -t upload
```

Skrypt sprawdza plik, opisuje, co zrobi, i pyta o zgodę. Dopiero potem przełącza kartę w tryb DFU
(komenda `dfu`), sprawdza na dysku T1000-E Board-ID i wersję SoftDevice (S140 7.3.0) i kopiuje plik.
Jeśli na karcie nie ma programu gpxcam: przytrzymać przycisk i dwa razy szybko podłączyć kabel
(dioda świeci na zielono, pojawia się dysk T1000-E).

Nigdy nie ruszamy bootloadera ani SoftDevice S140 7.3.0 (żadnego SWD, mass erase, nrfjprog).

Tylko DFU, bez wgrywania: `tools/dfu.sh`. Karta zostaje wtedy w bootloaderze, dopóki nie dostanie pliku `.uf2`.

## Log po USB

```
pio device monitor
```

Komendy: `v` – wersja i przyczyna ostatniego resetu, `p` – stan zasilania układów i bateria,
`dfu` – tryb wgrywania UF2.
Diagnostyka (tylko odczyty): `diag bat` – napięcie baterii przy różnych czasach od włączenia dzielnika (ok. 4 s).
`diag snd N` – wszystkie dźwięki z wypełnieniem PWM N% (1–50), do dobrania głośności na słuch.
Portu nie otwierać z prędkością 1200 bps – to uruchamia bootloader bez dysku.

## Powrót do Meshtastic

https://flasher.meshtastic.org (urządzenie: Seeed SenseCAP T1000-E).

## Dokumentacja

- [docs/SPRZET.md](docs/SPRZET.md) – karta: bootloader, SoftDevice, mapa flasha, DFU, piny, odstępstwa od Meshtastic.
- [docs/PROTOKOL.md](docs/PROTOKOL.md) – protokół Bluetooth LE aparatów LUMIX i wektory testowe.

## Źródła

- Piny, plik płytki i linker: [Meshtastic](https://github.com/meshtastic/firmware) (GPL-3.0),
  uzupełnione z SDK Seeed dla T1000-E – szczegóły w `docs/SPRZET.md` i w komentarzach w kodzie.
- Rdzeń Arduino: fork Meshtastic rdzenia Adafruit nRF52, przypięty w `platformio.ini`.

## Licencja

GNU GPL v3.0 – plik [LICENSE](LICENSE).
