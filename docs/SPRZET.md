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

Bootloader to dokładnie upstream Adafruit nRF52 Bootloader, commit `488711a` (lipiec 2024, PR #336,
który dodał płytkę `src/boards/t1000_e/`) – wersja `0.9.1-5-g488711a` z `INFO_UF2.TXT` to 5 commitów po tagu 0.9.1.
Zachowanie bootloadera opisane niżej pochodzi z kodu tej wersji (`src/main.c`, `src/boards/boards.c`,
`src/usb/uf2/ghostfat.c`, `Makefile`) i zgadza się z tym, co widać na karcie.

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
  Działa zawsze, niezależnie od programu na karcie. Sprawdzone na karcie (M0).
- **Komenda `dfu`** (program gpxcam, `tools/dfu.sh`): `enterUf2Dfu()` z rdzenia wyłącza SoftDevice, ustawia
  GPREGRET = 0x57 i resetuje. Bootloader startuje z dyskiem. Sprawdzone na karcie (M0);
  `INFO_UF2.TXT` po tej operacji jest identyczny jak przed pierwszym wgraniem.
- **„1200 bps touch”** (otwarcie i zamknięcie portu z prędkością 1200): rdzeń wywołuje `enterSerialDfu()`
  (GPREGRET = 0x4E). Bootloader startuje **bez dysku**, tylko z portem szeregowym.
  Wyjście: ręczny DFU i wgranie pliku UF2. Dlatego monitora portu nie uruchamiamy z prędkością 1200.
- Po wejściu przez GPREGRET (komenda lub 1200 bps) bootloader czeka, dopóki USB jest podłączone,
  aż dostanie plik (`main.c`: limit 3 s działa tylko wtedy, gdy USB się nie połączy).

## Wybudzanie z System OFF (nRF52840)

Według opisów bitów `RESETREAS` w nagłówku Nordic MDK (`nrf52840_bitfields.h`) procesor budzi się z System OFF przez:
GPIO DETECT, np. przycisk (bit 16 `OFF`), **VBUS – podłączenie kabla USB** (bit 20 `VBUS`), LPCOMP (bit 17),
pole NFC (bit 19) i reset pinem (bit 0). Wybudzenie zawsze kończy się resetem, więc bootloader (w tym ręczny DFU)
działa po każdym wybudzeniu. Wybudzenia przez VBUS nie da się wyłączyć (Nordic DevZone). Jeśli warunek GPIO DETECT
jest spełniony w chwili wejścia w System OFF, procesor od razu się budzi – przed wyłączeniem trzeba poczekać
na puszczenie przycisku. VBUS budzi tylko wtedy, gdy napięcie **wzrośnie** w czasie System OFF (PS 5.3.2), dlatego
program nie wchodzi w System OFF z podłączonym USB.

Sprawdzone na karcie (M1, krok 4): wyłączenie na baterii działa (po odłączeniu kabla nRF przestaje widzieć VBUS);
podłączenie kabla budzi procesor (`RESETREAS` = 0x00100000, `VBUS`); przycisk budzi procesor
(`RESETREAS` = 0x00010000, `OFF`); krótkie naciśnięcie odsyła kartę z powrotem do uśpienia bez dźwięku;
przytrzymanie 3 s wyłącza kartę, a z podłączonym USB wyłączenie jest odrzucane. Konfiguracja pinów i GPREGRET przetrwają System OFF (PS 6.9, 5.3.6.8, 5.3.7.16),
ale bootloader przed uruchomieniem programu przywraca wszystkie piny do stanu po resecie (`board_teardown()`
w `src/boards/boards.c`) – po każdym starcie trzeba ponownie ustawić piny i uśpić LR1110.

## Piny

Numeracja nRF: P0.x = x, P1.x = 32 + x. Podstawa: `variant.h` Meshtastic. Uzupełnienia z SDK Seeed
(`Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board`, `smtc_hal/inc/smtc_hal_config.h`).

| Funkcja | Pin | Stan po starcie programu |
|---|---|---|
| Dioda zielona (świeci przy 1 – sprawdzone na karcie w M0) | P0.24 | mruga |
| Dioda czerwona (tylko Seeed) | P0.03 | 0 |
| Przycisk (aktywny stanem wysokim, bez rezystora na płytce) | P0.06 | odłączony (M1: podciąganie w dół) |
| Brzęczyk: zasilanie / PWM | P1.05 / P0.25 | 0 / 0 |
| GPS: zasilanie | P1.11 | 0 (`gps on`: 1) |
| GPS: podtrzymanie RTC (VRTC) | P0.08 | 1 na stałe (od M2) |
| GPS: reset (aktywny 1) / RESETB / SLEEP_INT / RTC_INT | P1.15 / P1.14 / P1.12 / P0.15 | 0 / wejście z podciąganiem w górę / 1 / 0 (jak Seeed) |
| GPS: UART 115200 (nRF TX / nRF RX) | P0.13 / P0.14 | odłączone; przy włączonym GPS UARTE |
| Akcelerometr: zasilanie / INT | P1.07 / P1.02 | 0 / odłączone |
| I2C SDA / SCL | P0.26 / P0.27 | odłączone |
| Zasilanie czujników temperatury i światła oraz dzielników baterii i P0.04 | P1.06 | 0 |
| LR1110: NRESET / NSS / BUSY / IRQ | P1.10 / P0.12 / P0.07 / P1.01 | 1 / 1 / wejście / odłączone |
| LR1110: SCK / MOSI / MISO | P0.11 / P1.09 / P1.08 | 0 / 0 / odłączone |
| Pomiar baterii (dzielnik ×2, zasilany z P1.06) | P0.02 (AIN0) | odłączony |
| Wykrycie zasilania zewn. / ładowanie (aktywny 0) / naładowane | P0.05 (AIN3) / P1.03 / P1.04 | odłączone |
| Zasilanie flasha QSPI (tylko Seeed) | P1.13 | nieruszany |
| Pomiar napięcia linii P1.06 (dzielnik ≈ 1:2) | P0.04 (AIN2) | nieruszany |

„Odłączone” = stan po resecie nRF52: wejście z odłączonym buforem, bez podciągania – najmniejszy
upływ i brak zasilania wyłączonego układu przez jego wyprowadzenia.

LR1110 nie ma wyłącznika zasilania. Program resetuje go i wysyła komendę SetSleep
`01 1B 00 00 00 00 00` (zimny start, bez budzenia z RTC) – według sterownika Semtech lr11xx z SDK Seeed.
W uśpieniu LR1110 trzyma BUSY w stanie wysokim; program sprawdza to i zapisuje wynik w logu.

## Odstępstwa od Meshtastic

- **Brak włączania układów na starcie.** Meshtastic w `initVariant()` włącza czujniki, akcelerometr,
  brzęczyk i podtrzymanie GPS. U nas `initVariant()` jest pusty, a `board::init()` wszystko wyłącza.
- **P0.08 (podtrzymanie RTC GPS):** od M2 zawsze 1, także w System OFF – GPS zachowuje dane między użyciami
  (gorący start). W M0–M1 było 0. Prąd podtrzymania – do zmierzenia w M6.
- **Linie sterujące GPS** przy wyłączonym GPS i włączonym podtrzymaniu jak w SDK Seeed (`ag3335.c`: `gnss_init()`,
  `gnss_scan_stop()`): RESET 0, SLEEP_INT 1, RTC_INT 0, RESETB wejście z podciąganiem w górę. Piny UART są
  odłączone, dopóki GPS jest wyłączony (TX w stanie 1 zasilałby wyłączony układ), wbrew tabeli 135 specyfikacji
  nRF52840, która zakłada zasilone urządzenie po drugiej stronie.
- **Bez `$PAIR513`:** Meshtastic zapisuje konfigurację w pamięci flash GPS; my konfigurujemy GPS tylko w RAM.
- **P0.04:** Meshtastic – `T1000X_SENSOR_EN_PIN` (wyjście, ustawia 1). Wariant Arduino Seeed – `A1`
  „VCC voltage detect”, SDK Seeed – definicja `SENSE_ADC_VCC` (AIN2), ale nieużywana: „VCC” SDK mierzy
  wewnętrznym kanałem VDD przetwornika (`smtc_hal_adc.c`: `VCC_ADC_CHANNEL = NRF_SAADC_INPUT_VDD`).
  Pomiar na karcie (M1) potwierdza wersję Seeed: P0.04 to wejście pomiarowe (≈ VDD/2 przy włączonym P1.06),
  a nie wyjście. Nie sterujemy nim.
- **P0.03:** druga (czerwona) dioda według Seeed; Meshtastic jej nie definiuje.
- **P1.13:** zasilanie flasha QSPI według Seeed; nikt go nie steruje, my też nie.
- **USB:** program przedstawia się jako 2886:8057 (Seeed), nie 239a:8029 (Adafruit Feather) jak Meshtastic.
- **Wgrywanie:** bez „1200 bps touch” i nrfutil – tylko UF2 (`tools/flash.sh`).
- **Limit programu:** do 0xEA000 zamiast 0xED000 (zob. mapa flasha).

## Sprawdzone na karcie (M1, krok 1 – komendy `diag`)

- **Przycisk P0.06:** aktywny stanem wysokim, stan wysoki trwa przez cały czas wciśnięcia (przytrzymanie 4 s
  = 19 kolejnych próbek co 0,2 s). Puszczony pin „wisi” (bez podciągania odczyt zależy od poprzedniego
  ustawienia), więc na płytce nie ma rezystora – potrzebne wewnętrzne podciąganie w dół, jak w Meshtastic
  (`INPUT_PULLDOWN_SENSE`). Bootloader `488711a` ustawia podciąganie w górę i uznaje stan niski za wciśnięty,
  a przy podciąganiu w górę pin zawsze ma stan wysoki – **bootloader nigdy nie uzna przycisku za żądanie DFU**.
  Krótkie naciśnięcie ani przytrzymanie 4 s nie resetuje procesora.
- **Bateria:** dzielnik ×2 na AIN0 jest zasilany z P1.06. P1.06 = 0: 0 mV; P1.06 = 1: 2088 mV na pinie,
  czyli 4176 mV baterii (z podłączonym kablem, w trakcie ładowania); znowu 0: 1 mV. P1.06 włączamy tylko
  na czas pomiaru, jak SDK Seeed (`sensor_bat_sample()`: AIN0, wzmocnienie 1/5, odniesienie wewnętrzne,
  12 bit, 40 µs, wynik ×2). Meshtastic trzyma P1.06 włączone cały czas. Dzielnik ustala się praktycznie
  od razu (krok 3, `diag bat` z kablem: 4179 mV bez czekania, 4181 mV po 10 ms, 4185 mV po 50 ms);
  program czeka 10 ms z zapasem.
- **P0.04:** 0 mV przy P1.06 = 0, ok. 1502 mV (≈ VDD/2) przy P1.06 = 1 – wejście pomiarowe, zob. odstępstwa.
- **Zasilanie nRF:** VDD = 3,0 V, regulator w trybie normalnym (zasilanie przez VDD, nie VDDH).
- **Pin RESET:** P0.18 włączony jako reset (`UICR.PSELRESET` = 0x00000012).
- **Kabel USB:** podłączenie **nie resetuje** procesora (działał dalej na baterii, przyczyna resetu bez zmian);
  nRF od razu widzi VBUS (`USBREGSTATUS.VBUSDETECT`).
- **Ładowarka** (oba sygnały aktywne stanem niskim, wg Meshtastic): w trakcie ładowania P1.03 = 0, P1.04 = 1;
  po naładowaniu (4,18 V) P1.04 = 0 – program pokazuje wtedy `USB, charged`. Bez kabla ładowarka nie działa,
  więc program czyta te linie tylko przy obecnym VBUS.
- **Bateria bez kabla** (M1, bufor logu): pełny akumulator, 10 minut na baterii – 4174–4184 mV, rozrzut
  pojedynczych pomiarów ok. ±5 mV; spadku w tym czasie nie widać. Zużycie prądu – do zmierzenia w M6.

## Bootloader a dioda

Plik płytki bootloadera (`src/boards/t1000_e/board.h` @ 488711a) ma `LED_STATE_ON 0`, a dioda na karcie świeci
stanem wysokim. Bootloader przy starcie ustawia diodę na „wyłączoną” (`led_pwm_init()`: `1 - LED_STATE_ON`), czyli
w praktyce ją zapala – przy każdym starcie, także po wybudzeniu z System OFF, dioda krótko mignie, zanim program
przejmie piny. Nieszkodliwe; bootloadera nie zmieniamy.

## GPS (M2)

Sprawdzone na karcie (krok 1, `gps on` przy oknie, zimny start, bez wysyłania czegokolwiek do GPS):
- Pierwszy bajt 220 ms po włączeniu zasilania; UART 115200 8N1 poprawny (poza kilkoma bajtami przy starcie).
- Przy starcie układ sam wypisuje: tekst ROM (`F1: …`, `Leaving the BROM`), `$PAIR001,002,1`, `$PAIR001,021,0`,
  wersję `$PAIR021,AG3335M_V2.6.0.AG3335_20221230,…` (dłuższą niż 100 znaków), `$PAIR011`, `$PAIR010`.
- Potem tylko `$GNGGA` i `$GNRMC` co 1 s – konfiguracja zapisana w module wcześniej przez Meshtastic (`$PAIR513`).
- Bez fixa GPS podaje już czas UTC, a datę jeszcze domyślną (`070180`) – czasu nie wolno używać bez statusu `A` w RMC.
- Po `gps off` żadnych linii; RESETB w stanie wysokim (reset zakończony).
- Pierwszy fix przy oknie: 70 s po `gps on`, 8 satelitów (praktycznie zimny start); po 6 min 19 satelitów,
  0 błędnych sum kontrolnych w 769 liniach.
- Konfiguracja w RAM (krok 2, specyfikacja Quectel LC26G/LC76G/LC86G V1.4): po `$PAIR011,001` (ok. 0,7 s po
  włączeniu) `$PAIR063,-1` zwraca `$PAIR001,063,0` i dziesięć `$PAIR063,<typ>,<częstotliwość>`;
  `$PAIR067` zwraca `$PAIR001,067,0` i `$PAIR067,1,1,1,1,0,0` (standardowy format `*hh`). Moduł miał już
  GGA+RMC oraz GPS+GLONASS+Galileo+BeiDou, więc program niczego nie zmieniał. `$PAIR066` restartuje moduł,
  `$PAIR513` zapisuje do flasha – program wysyła je odpowiednio tylko przy różnicy / nigdy.
- Do sprawdzenia w kroku 4: bootloader przy każdym starcie (także po wybudzeniu z System OFF) przywraca piny
  do stanu po resecie, więc P0.08 (podtrzymanie GPS) przez kilka ms nie jest sterowany – czy podtrzymanie
  przetrwa wybudzenie, pokaże porównanie czasu do fixa. Po dwóch wgraniach programu (w trybie DFU piny są
  w stanie po resecie przez sekundy) zegar GPS wrócił do domyślnego 1980-01-06 – podtrzymanie zgasło; wgrywanie
  nie nadaje się więc do oceny gorącego startu.
- Krok 3: parser RMC/GGA działa na karcie; co 10 s linia stanu bez współrzędnych, 0 błędnych sum kontrolnych.

## Otwarte pytania

- **P0.05:** odczyt analogowy 2,73 V, ale cyfrowo 0, także z podciąganiem w górę – sprzeczne, niewyjaśnione.
  Do wykrywania zasilania z USB używamy rejestru VBUS w nRF, P0.05 nie jest potrzebny.
- **Ręczny DFU (przycisk + dwa razy kabel):** działa, ale ani samo podłączenie kabla, ani samo naciśnięcie
  nie resetuje procesora, więc mechanizm jest inny. Bootloader wchodzi w DFU po podwójnym resecie pinem
  RESET (P0.18), a pin jest włączony – prawdopodobnie coś na płytce wywołuje ten reset. Nie badamy, dopóki działa.

## Ustalenia na kolejne etapy

- **M6:** zmierzyć pobór prądu w spoczynku. LR1110 w uśpieniu ma pobierać poniżej 1 µA.
