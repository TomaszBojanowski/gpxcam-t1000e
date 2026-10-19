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
| 0xEA000–0xF4000 | dane programu (40 KB) | bootloader ich nie nadpisuje; w tym InternalFS 0xED000–0xF4000 (od M3 – Bluefruit) |
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
- **BLE:** tylko rola central (`Bluefruit.begin(0, 1)`), karta się nie ogłasza; Meshtastic jest peryferium.

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
- Krok 4 (tryb Backup, `gps test 2` przy oknie bez USB): zimny start – fix po 61 s (10 satelitów); po 5 min
  w trybie Backup gorący start – fix po 2 s (8 satelitów). Moduł domyślnie usypia swój procesor między
  pomiarami i gubi wtedy komendy: pojedyncze `$PAIR650,0` bywało ignorowane. Działa sekwencja Seeed:
  `$PAIR382,1` (blokada usypiania) po każdym starcie, przy wyłączaniu `$PAIR382,0`, a potem `$PAIR650,0`
  powtarzane co 40 ms do potwierdzenia `$PAIR001,650,0` (przy oknie dopiero 23.–24. kopia, przy biurku 11.–14.)
  i 1,5 s ciszy. Po wybudzeniu z trybu Backup pierwszy bajt przychodzi po 100–120 ms zamiast 190 ms.
- Podtrzymanie przeżywa wyłączenie karty: GPS w trybie Backup, karta 5 min w System OFF, wybudzenie przyciskiem
  (bootloader na kilka ms nie steruje P0.08), potem `gps test 1` – fix po 3 s (12 satelitów). Zgaśnięcie
  podtrzymania powoduje dopiero wgrywanie (tryb DFU trwa sekundy).

## Czas startu a przytrzymanie przy włączaniu

Część czasu od naciśnięcia przycisku nie jest liczona przez `millis()`. Bootloader wychodzi od razu
(`DFU_MAGIC_SKIP` w GPREGRET pomija opóźnienie 500 ms, `src/main.c` bootloadera), ale rdzeń Arduino tylko
uruchamia kwarc 32 kHz (`cores/nRF5/wiring.c`, bez czekania), a licznik systemowy rusza dopiero, gdy kwarc
wystartuje (zwykle kilkaset ms). Pomiar (czas puszczenia zapisany w GPREGRET2, M2; trzymanie liczone na wyczucie):
„ok. 1 s” skończyło się po 500 ms czasu programu, „ok. 3 s” po 1553 ms. Próg włączenia: 400 ms czasu programu.
Sprawdzone z progiem 400 ms: szybkie stuknięcie – puszczone po 170 ms, karta wraca do uśpienia; przytrzymanie
ok. 1 s – puszczone po 836 ms, karta startuje.

## SoftDevice i Bluefruit (M3)

- **Start:** `Bluefruit.begin(0, 1)` w `setup()` po obsłudze wybudzenia – krótkie stuknięcie przy wybudzeniu
  wraca do System OFF, zanim SoftDevice ruszy. Skan domyślnie wyłączony.
- **Rejestry POWER:** przy działającym SoftDevice odczyt jest dozwolony, zapis kończy się HardFaultem
  (specyfikacja S140, „Peripheral runtime protection”). System OFF idzie więc przez `sd_power_gpregret_clr/set`
  i `sd_power_system_off()`; GPREGRET2 zapisujemy tylko przed startem SoftDevice. Odczyt VBUS (`USBREGSTATUS`)
  zostaje bezpośredni.
- **DFU:** `enterUf2Dfu()` w rdzeniu (`wiring.c`, `reset_mcu()`) wyłącza SoftDevice przed zapisem GPREGRET.
- **Przerwania:** SoftDevice zastrzega priorytety 0, 1 i 4 oraz przerwania POWER_CLOCK, RADIO, RTC0, TIMER0, RNG,
  ECB, CCM_AAR, TEMP, NVMC i SWI5 (`nrf_nvic.h` S140 7.3.0). UART GPS to UARTE0 z priorytetem 3 (`Uart.cpp`),
  USB – 2, PWM3 brzęczyka działa bez przerwań – bez kolizji. Rdzeń odbiera z UART po 1 bajcie (`RXD.MAXCNT = 1`),
  a sprzęt buforuje 4 bajty: przy 115200 to ok. 0,35 ms tolerancji na opóźnione przerwanie. Sprawdzone (krok 3):
  GPS i skan razem przez 6 min – 754 linie NMEA, 0 złych sum; 3 ucięte linie i 11 bajtów spoza ASCII to start GPS
  (tyle samo w 40 s bez skanu).
- **System plików:** `Bluefruit.begin()` zawsze wywołuje `bond_init()` – montuje InternalFS (0xED000–0xF4000),
  a gdy się nie da, formatuje go; zakłada katalogi `/adafruit/bond_prph` i `/adafruit/bond_cntr`.
  Przy zapisie flasha biblioteka zapala `LED_BUILTIN` – u nas `-DLED_BUILTIN=-1`, jak w Meshtastic.
- **Diody:** `Bluefruit.autoConnLed(false)` – w wariancie `LED_BLUE` to -1.
- **Sprawdzone na karcie (krok 1):** log `ble: SoftDevice on`; `off` z kablem – odmowa po 15 s, karta działa dalej;
  `dfu` (przez `tools/flash.sh`) – dysk bootloadera, program wraca; GPS – fix po 58 s (zimny start po wgraniu),
  wyłączenie w tryb Backup potwierdzone; bez kabla: wyłączenie przyciskiem → System OFF (start z `OFF-GPIO`),
  stuknięcie (puszczone po 180 ms) → z powrotem do uśpienia, przytrzymanie (1061 ms) → start z SoftDevice.
- **Skan (krok 3):** raporty z SoftDevice odbiera własna funkcja w zadaniu BLE (`Bluefruit.setEventCallback`),
  sprawdza je `lumix::parseAdvert()`, aparaty trafiają do statycznej kolejki (8 miejsc), a skan jest wznawiany
  ręcznie – `BLEScanner` robi to sam tylko z własnymi filtrami albo callbackiem, który dla każdego raportu
  przydziela pamięć (`ada_callback`). Zatrzymanie działa także przy skanie wstrzymanym po raporcie (`ble_gap.h`).
- **Sprawdzone na karcie (krok 3):** przy biurku ok. 37 raportów/s od wszystkich urządzeń, w logu tylko aparat;
  kolejka bez strat (0 z ok. 15 600 raportów); dźwięki w trakcie skanu; wyłączenie przyciskiem w trakcie skanu →
  System OFF i start z `OFF-GPIO`.

## Plik parowania (M4)

`/gpxcam/pairing.bin` w systemie plików InternalFS (0xED000–0xF4000), 32 bajty (`src/pairing_record.h`):

| Bajty | Zawartość |
|---|---|
| 0–3 | znacznik `GXPR` |
| 4 | wersja formatu = 1 |
| 5 | 1 = aparat sparowany, 0 = brak |
| 6–21 | UUID karty (16 B, kolejność jak w zapisie tekstowym), wersja 4 (losowy) |
| 22–26 | numer seryjny aparatu (zera, gdy bajt 5 = 0) |
| 27 | 0 (zapas) |
| 28–31 | CRC-32 (IEEE 802.3, jak zlib) bajtów 0–27, little-endian |

- UUID powstaje raz, przy pierwszym starcie bez pliku, ze sprzętowego generatora przez SoftDevice
  (`sd_rand_application_vector_get`). Uszkodzony plik oznacza nowy UUID i wpis w logu – aparat trzeba sparować ponownie.
- Zapis: plik tymczasowy `/gpxcam/pairing.tmp`, potem `rename` (atomowy w littlefs 1.7, także przy zaniku zasilania)
  i odczyt kontrolny, bo `File::close()` nie zgłasza błędów. Otwarcie pliku przydziela pamięć na stercie (biblioteka) –
  tylko przy starcie i zmianie parowania.
- Sprawdzone na karcie: pierwszy start tworzy plik (ok. 0,9 s), UUID ma znaczniki wersji 4; ten sam UUID po ponownym
  wgraniu przez `dfu` i po wyłączeniu i włączeniu przyciskiem.

## Parowanie (M4)

- **GATT bez blokujących funkcji Bluefruit:** `BLEClientCharacteristic::read()/write_resp()` czekają tylko 100 ms
  (`BLE_GENERIC_TIMEOUT`, bez możliwości zmiany), tworzą semafor na stercie przy każdym wywołaniu, a odpowiedź po czasie
  trafiłaby do nieaktualnego bufora (`AdaMsg`). Połączenie, MTU, wyszukanie usługi i charakterystyk, odczyt i zapis
  z odpowiedzią idą wprost przez API SoftDevice (`src/camera_link.cpp`), z własnymi limitami czasu
  (`src/camera_session.cpp`). Nie rejestrujemy obiektów klienta Bluefruit, więc jego procedury ignorują nasze odpowiedzi.
  Bluefruit odpowiada za nas na prośby aparatu: PHY, długość pakietu, parametry połączenia, MTU od aparatu.
- **MTU:** SoftDevice skonfigurowany na 185 (`configCentralConn` przed startem; RAM wystarcza). S5II przyznaje 57 –
  zapisy do 54 bajtów, potrzeba 32.
- **Gest:** klik i w ciągu 0,5 s przytrzymanie 3 s (`CLICK_HOLD_GAP_MS`); samo przytrzymanie 3 s nadal wyłącza kartę.
- **Sprawdzone na karcie (krok 3):** bez fixa przez 120 s – rezygnacja z jasnym komunikatem, GPS z powrotem w tryb
  Backup; aparat w `00` i w `06` – komunikat „not in pairing mode”, bez łączenia, rezygnacja po 60 s; aparat w menu
  parowania – sparowany za pierwszą próbą w ok. 1,8 s, aparat pokazał „Pairing completed”, na liście urządzeń
  „T1000-E”; gest uruchamia sesję, krótkie naciśnięcie ją przerywa; numer aparatu zostaje po wyłączeniu i włączeniu.
- **Nie sprawdzone na karcie:** ścieżka porażki z przerwami 10 s i 30 s – żadna próba nie zakończyła się porażką.

## Praca normalna (M5)

- **Kolejność:** skan → raport sparowanego aparatu w stanie `00` (nie starszy niż 1 s) i czas UTC z GPS → połączenie
  i przedstawienie jak przy parowaniu, ale połączenie zostaje → `01` do trybu GPS → CCCD → lokalizacje.
  Bez czasu z GPS karta nie łączy się wcale: pierwsza wersja łączyła się wcześniej i każda próba kończyła się na kroku
  godziny (aparat niczego nie odrzucał, ale to zbędne łączenie).
- **Sprawdzone na karcie (krok 2):** połączenie w ok. 1,5 s od raportu, biała ikona Bluetooth i GPS w aparacie;
  lokalizacja od razu i co 25 s; zdjęcie: pozycja w EXIF 8,4 m od pozycji karty (w pomieszczeniu), czas GPS w EXIF
  19 s przed zdjęciem (ostatnia wysyłka), status `A`; `gps off` w trakcie – połączenie trwa, lokalizacje czekają,
  `gps on` – fix po 8 s i wysyłki wracają; wyłączenie aparatu – aparat zamyka połączenie (`HCI 0x13`), karta widzi
  go w czuwaniu `06` i nie łączy się przez 5 min; włączenie – połączenie w 1,6 s.
- **Powiadomienia bez CCCD:** w S5II charakterystyka powiadomień (uchwyt `0x008e`, tylko „notify”) nie ma deskryptora
  CCCD – zaraz za wartością jest deklaracja następnej charakterystyki (`0x2803`). gpxcam też nic nie zapisuje
  (`getDescriptor` zwraca null). Karta słucha powiadomień bez CCCD; aparat wysłał jedno ok. 2 s przed wyłączeniem.
- **Wysokość:** S5II nie zapisuje jej w EXIF ani z kartą, ani z aplikacją Lumix Lab (ten sam zestaw znaczników GPS,
  `GPSMeasureMode` 0); karta wysyła wysokość zgodnie z protokołem.
- **Sygnalizacja (krok 3):** dioda – szukanie: dwa mignięcia 50 ms co 2 s, połączony: jedno co 4 s, bez `link`: co 1 s;
  dźwięki – połączenie: dwa rosnące tony (CS4, FS4), koniec połączenia: dwa opadające.
- **Zasięg (krok 3):** utrata zasięgu po 134 s – `HCI 0x08` (przekroczenie czasu nadzoru), przerwa 5 s; próba przy
  −90 dBm nieudana (`HCI 0x3e`), przerwa 15 s; po powrocie połączenie w 1,5 s.

## Energia (M6)

- **Tryby szukania** (`link_policy::searchMode`), tylko gdy karta nie jest połączona:
  - normalny – skan 50 ms co 100 ms, GPS włączony;
  - rzadki – aparatu nie widać od 5 min: skan 100 ms co 2 s, GPS w Backup;
  - czuwanie – ostatni raport aparatu to `06`, karta tylko czeka na `00`: przez pierwsze 10 min (przerwa między
    zdjęciami) skan 100 ms co 5 s i GPS zostaje włączony z fixem; potem skan 100 ms co 10 s, GPS w Backup.

  Raport w stanie `00` od razu przywraca tryb normalny (GPS włączany tylko w nim). Próba połączenia dopiero przy
  co najmniej −85 dBm.
- **Sprawdzone na karcie (krok 2):** pierwsza wersja (GPS w Backup po minucie czuwania, skan co 10 s) – od włączenia
  aparatu do połączenia ok. 25 s (skan + gorący start GPS 5 s + połączenie); stąd dwustopniowe czuwanie: przerwa
  2 min – ok. 8 s od włączenia aparatu, lokalizacja 2 s po wykryciu; poza zasięgiem – rozłączenie `HCI 0x08`, po 5 min tryb rzadki;
  po powrocie połączenie ok. 11 s od wykrycia (gorący start GPS 9 s). W czuwaniu aparat bywa niewidoczny przez 30 s
  (rzadkie ogłoszenia w czuwaniu i rzadki skan) – tryb zostaje, bo karta pamięta ostatni stan.
- **DC/DC:** nie włączamy – brak potwierdzenia, że płytka ma cewkę dla REG1, Meshtastic też go nie włącza.
- **Niskie napięcie:** poniżej 3,40 V na baterii karta zapisuje wpis w dzienniku, gra dwa kliknięcia i przechodzi
  w System OFF – przy starcie od razu, w pracy po dwóch kolejnych pomiarach co 60 s (krótki spadek pod obciążeniem
  tego nie wywołuje). Z podłączonym USB nigdy (`battery_rules::powerOffForLowBattery`, test na PC): rozładowana
  karta po podłączeniu kabla startuje normalnie i się ładuje.

### Pomiar prądu

**Bez miernika – test rozładowania z dziennikiem baterii** (średni prąd w pracy, rzędu mA):
1. Naładuj kartę do pełna (`p` pokazuje `charged`).
2. `bat log clear`, potem `bat log on`.
3. Ustaw jeden scenariusz, np. aparat włączony i połączony; aparat w czuwaniu przez noc; aparat daleko (tryb rzadki).
4. Odłącz kabel. Pierwszy wpis powstaje od razu, kolejne co 10 min (czas pracy, napięcie, stan `link`), do 1024 wpisów.
5. Zostaw kartę do wyłączenia przy 3,40 V (ostatni wpis: `low battery, powered off`) albo przerwij podłączeniem
   kabla – zapis wstrzymuje się przy USB.
6. Podłącz kabel, `bat log`. Średni prąd ≈ 700 mAh × zużyta część / liczba godzin. Do 3,40 V zużyta jest prawie
   cała pojemność – to najdokładniejszy wariant; z samego spadku napięcia w krótszym teście wynik jest zgrubny
   (±20–30 %), bo napięcie ogniwa Li-ion nie jest liniowe względem ładunku.

Spoczynku (System OFF, rzędu µA) tak się nie zmierzy: spadek napięcia to kilka mV na tydzień.

**Ze sprzętem:**
- Nordic Power Profiler Kit II (PPK2) – zasila kartę zamiast baterii (3,7–4,2 V) i mierzy 200 nA – 1 A z wykresem:
  spoczynek, skan, GPS i połączenie widać osobno. Wymaga podłączenia w miejsce baterii (otwarcie obudowy).
- Multimetr z zakresem µA/mA szeregowo z baterią – dobry do spoczynku; w pracy odczyt skacze (radio impulsowo).
- Tester USB się nie nada – mierzy ładowanie, nie pobór z baterii.

## Otwarte pytania

- **Wybudzenie a LATCH (M2, krok 4):** raz po wybudzeniu przyciskiem bit P0.06 w `LATCH` nie dał się skasować
  (`LATCH P0 0x00000040`) i program, zgodnie z zasadą, wystartował zamiast wrócić do uśpienia. W kolejnej próbie
  problem nie wystąpił; program ponawia kasowanie przez 100 ms i loguje szczegóły, gdyby wrócił.

- **P0.05:** odczyt analogowy 2,73 V, ale cyfrowo 0, także z podciąganiem w górę – sprzeczne, niewyjaśnione.
  Do wykrywania zasilania z USB używamy rejestru VBUS w nRF, P0.05 nie jest potrzebny.
- **Ręczny DFU (przycisk + dwa razy kabel):** działa, ale ani samo podłączenie kabla, ani samo naciśnięcie
  nie resetuje procesora, więc mechanizm jest inny. Bootloader wchodzi w DFU po podwójnym resecie pinem
  RESET (P0.18), a pin jest włączony – prawdopodobnie coś na płytce wywołuje ten reset. Nie badamy, dopóki działa.

## Ustalenia na kolejne etapy

- **M6:** zmierzyć pobór prądu w spoczynku. LR1110 w uśpieniu ma pobierać poniżej 1 µA.
