# Sprzęt: Seeed SenseCAP T1000-E

Ustalenia o karcie, na których opiera się program. Każda wartość ma źródło; „sprawdzone na karcie”
oznacza odczyt z naszego egzemplarza.

## Układy

- MCU: Nordic nRF52840.
- GPS: Airoha AG3335 (UART 115200).
- Radio LoRa: Semtech LR1110 – nieużywane, usypiane na starcie.
- Akumulator 700 mAh, przycisk, dwie diody (zielona, czerwona), brzęczyk.
- Czujniki temperatury (NTC) i światła, akcelerometr QMA6100P – nieużywane, wyłączone.

## Oprogramowanie fabryczne (sprawdzone na karcie 2026-10-19, `INFO_UF2.TXT`)

- Bootloader: `UF2 Bootloader 0.9.1-5-g488711a`, model „Seeed T1000-E for Meshtastic”,
  Board-ID `nRF52840-T1000-E-v1`, data Jul 15 2024.
- SoftDevice: **S140 7.3.0**.
- Program fabryczny: Meshtastic 2.7.15.567b8ea.

Bootloader to Adafruit nRF52 Bootloader; konfiguracja tej płytki jest w repozytorium Adafruit
(`src/boards/t1000_e/`). Zachowanie bootloadera opisane niżej pochodzi z kodu upstream
(`src/main.c`, `src/usb/uf2/ghostfat.c`, `Makefile`) i zgadza się z tym, co widać na karcie.

## Mapa flasha

| Adres | Zawartość | Uwagi |
|---|---|---|
| 0x00000–0x01000 | MBR | nietykalne |
| 0x01000–0x27000 | SoftDevice S140 7.3.0 | nietykalne |
| 0x27000–0xEA000 | program | `variants/t1000-e/nrf52840_s140_v7.ld` |
| 0xEA000–0xF4000 | dane programu (40 KB) | bootloader ich nie nadpisuje; w tym InternalFS 0xED000–0xF4000 |
| 0xF4000–0x100000 | bootloader, jego konfiguracja i ustawienia (0xFF000) | nietykalne |

Źródła:
- Program od 0x27000 i RAM od 0x20006000: linker Meshtastic dla S140 7.x
  (`src/platform/nrf52/nrf52840_s140_v7.ld`).
- Koniec obszaru programu 0xEA000: bootloader rezerwuje dla nRF52840 `DFU_APP_DATA_RESERVED = 10*4096`
  (Makefile), więc `USER_FLASH_END = 0xF4000 − 0xA000`. Zgadza się z końcem `CURRENT.UF2` na karcie.
  Linker Meshtastic pozwala na kod do 0xED000; u nas limit jest zawężony do 0xEA000.
- InternalFS: 0xED000, 7 stron po 4 KB (`libraries/InternalFileSytem/src/InternalFileSystem.cpp` w rdzeniu).
- Bootloader od 0xF4000, ustawienia 0xFF000: `linker/nrf52840.ld` bootloadera.

## UF2

- Program wgrywamy z rodziną **0xADA52840**. Bootloader przyjmuje też rodzinę płytki 0x28860057.
- **Bootloader zapisze każdy blok z zakresu 0x1000–0xEA000, również w obszarze SoftDevice.**
  Bloki poniżej 0x1000 (MBR) pomija, powyżej 0xEA000 odrzuca (`write_block()` w `ghostfat.c`).
  Dlatego `tools/uf2check.py` przepuszcza wyłącznie bloki z 0x27000–0xEA000 i wymaga, żeby pierwszy był pod 0x27000.
- `CURRENT.UF2` z dysku bootloadera ma rodzinę 0x28860057 i obejmuje 0x1000–0xEA000, czyli **zawiera SoftDevice**.
  Kopia Meshtastic w tym formacie – tylko awaryjnie. Normalny powrót: https://flasher.meshtastic.org.

## USB

| Stan karty | VID:PID | Nazwa |
|---|---|---|
| program gpxcam | 2886:8057 | T1000-E gpxcam (PID jak w `boards.txt` Seeed dla T1000-E) |
| bootloader UF2 | 2886:0057 | T1000-E |
| Meshtastic 2.7.15 | 239a:8029 | T1000-E-BOOT |

## Wejście w tryb wgrywania (DFU)

- **Ręcznie:** przytrzymać przycisk i dwa razy szybko podłączyć kabel. Dioda świeci na zielono, pojawia się dysk T1000-E.
  Działa zawsze, niezależnie od programu na karcie.
- **Komenda `dfu`** (program gpxcam, `tools/dfu.sh`): `enterUf2Dfu()` z rdzenia wyłącza SoftDevice, ustawia
  GPREGRET = 0x57 i resetuje. Bootloader startuje z dyskiem.
- **„1200 bps touch”** (otwarcie i zamknięcie portu z prędkością 1200): rdzeń wywołuje `enterSerialDfu()`
  (GPREGRET = 0x4E). Bootloader startuje **bez dysku**, tylko z portem szeregowym.
  Wyjście: ręczny DFU i wgranie pliku UF2. Dlatego monitora portu nie uruchamiamy z prędkością 1200.
- Po wejściu przez GPREGRET (komenda lub 1200 bps) bootloader czeka, dopóki USB jest podłączone,
  aż dostanie plik (`main.c`: limit 3 s działa tylko wtedy, gdy USB się nie połączy).

## Piny

Numeracja nRF: P0.x = x, P1.x = 32 + x. Podstawa: `variant.h` Meshtastic. Uzupełnienia z SDK Seeed
(`Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board`, `smtc_hal/inc/smtc_hal_config.h`).

| Funkcja | Pin | Stan po starcie programu (M0) |
|---|---|---|
| Dioda zielona (świeci przy 1 – sprawdzone na karcie w M0) | P0.24 | mruga |
| Dioda czerwona (tylko Seeed) | P0.03 | 0 |
| Przycisk | P0.06 | nieużywany w M0 |
| Brzęczyk: zasilanie / PWM | P1.05 / P0.25 | 0 / 0 |
| GPS: zasilanie | P1.11 | 0 |
| GPS: podtrzymanie RTC (VRTC) | P0.08 | 0 (zob. odstępstwa i M2) |
| GPS: reset / RESETB / SLEEP_INT / RTC_INT | P1.15 / P1.14 / P1.12 / P0.15 | 0 / odłączone / odłączone / odłączone |
| GPS: UART (nRF TX / nRF RX) | P0.13 / P0.14 | odłączone |
| Akcelerometr: zasilanie / INT | P1.07 / P1.02 | 0 / odłączone |
| I2C SDA / SCL | P0.26 / P0.27 | odłączone |
| Czujniki temperatury i światła: zasilanie | P1.06 | 0 |
| LR1110: NRESET / NSS / BUSY / IRQ | P1.10 / P0.12 / P0.07 / P1.01 | 1 / 1 / wejście / odłączone |
| LR1110: SCK / MOSI / MISO | P0.11 / P1.09 / P1.08 | 0 / 0 / odłączone |
| Pomiar baterii (dzielnik ×2) | P0.02 (AIN0) | nieużywany w M0 |
| Wykrycie ładowarki / ładowanie / naładowane | P0.05 (AIN3) / P1.03 / P1.04 | nieużywane w M0 |
| Zasilanie flasha QSPI (tylko Seeed) | P1.13 | nieruszany |
| P0.04 (rola niepewna) | P0.04 | nieruszany |

„Odłączone” = stan po resecie nRF52: wejście z odłączonym buforem, bez podciągania – najmniejszy
upływ i brak zasilania wyłączonego układu przez jego wyprowadzenia.

LR1110 nie ma wyłącznika zasilania. Program resetuje go i wysyła komendę SetSleep
`01 1B 00 00 00 00 00` (zimny start, bez budzenia z RTC) – według sterownika Semtech lr11xx z SDK Seeed.
W uśpieniu LR1110 trzyma BUSY w stanie wysokim; program sprawdza to i zapisuje wynik w logu.

## Odstępstwa od Meshtastic

- **Brak włączania układów na starcie.** Meshtastic w `initVariant()` włącza czujniki, akcelerometr,
  brzęczyk i podtrzymanie GPS. U nas `initVariant()` jest pusty, a `board::init()` wszystko wyłącza.
- **P0.08 (podtrzymanie RTC GPS):** Meshtastic trzyma 1 cały czas. W M0 jest 0, tak jak w procedurze
  wyłączania karty w SDK Seeed (`app_user_power_off()`).
- **Linie sterujące GPS** przy wyłączonym GPS są odłączone (Meshtastic trzyma SLEEP_INT w stanie 1).
- **P0.04:** Meshtastic – `T1000X_SENSOR_EN_PIN` (wyjście, ustawia 1). SDK i wariant Arduino Seeed –
  `SENSE_ADC_VCC` (wejście analogowe AIN2). Źródła się nie zgadzają, więc pin zostaje nieruszony.
- **P0.03:** druga (czerwona) dioda według Seeed; Meshtastic jej nie definiuje.
- **P1.13:** zasilanie flasha QSPI według Seeed; nikt go nie steruje, my też nie.
- **USB:** program przedstawia się jako 2886:8057 (Seeed), nie 239a:8029 (Adafruit Feather) jak Meshtastic.
- **Wgrywanie:** bez „1200 bps touch” i nrfutil – tylko UF2 (`tools/flash.sh`).
- **Limit programu:** do 0xEA000 zamiast 0xED000 (zob. mapa flasha).

## Niezgodności do sprawdzenia w kolejnych etapach

- **Przycisk P0.06:** Meshtastic – aktywny stanem wysokim, podciąganie w dół. Bootloader upstream
  (`src/boards/t1000_e/board.h`) – `BUTTON_PULL = PULLUP`. Sprawdzić w M1.
- **Pomiar baterii a P1.06:** SDK Seeed opisuje wyłączanie P1.06 jako „power off temp&light/battery”.
  Sprawdzić w M1, czy dzielnik baterii wymaga włączenia P1.06.

## Ustalenia na kolejne etapy

- **M2:** P0.08 (podtrzymanie RTC GPS) trzeba włączyć i trzymać, także gdy główne zasilanie GPS
  jest wyłączone. Bez tego GPS po każdym uśpieniu długo łapie pozycję (zimny start).
- **M6:** zmierzyć pobór prądu w spoczynku. LR1110 w uśpieniu ma pobierać poniżej 1 µA.
