# gpxcam-t1000e

Program na kartę Seeed SenseCAP **T1000-E**, który ma przekazywać pozycję z jej GPS-u
do aparatu Panasonic LUMIX S5II przez Bluetooth LE – bez telefonu.
Protokół jak w aplikacji gpxcam na Androida.

Stan: **M4 zakończony, M5 w toku.** Wszystkie układy wyłączone, dioda mruga, wersja w logu po USB, wejście w tryb
wgrywania komendą `dfu`, melodia przy starcie, przycisk: krótkie naciśnięcie = cichy sygnał,
przytrzymanie 3 s = wyłączenie z melodią (System OFF; z podłączonym USB odrzucane – dwa kliknięcia), napięcie baterii
i stan ładowania w logu co 60 s. GPS (M2): konfiguracja tylko w RAM, fix, satelity, wysokość i czas UTC w logu,
między użyciami tryb Backup (gorący start po 2–3 s). Bluetooth (M3): SoftDevice S140 startuje przy włączeniu,
tylko rola central; skan szuka aparatów LUMIX i loguje ich numer, stan, adres i RSSI.
Parowanie (M4): `pair` albo klik + przytrzymanie przycisku 3 s. Praca normalna (M5): po starcie karta sama łączy się
ze sparowanym aparatem i wysyła mu pozycję.

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

## Testy na komputerze

```
pio test -e native
```

Moduły bez zależności od sprzętu (parser NMEA, protokół LUMIX: ogłoszenie i formaty zapisu, lista aparatów,
kalendarz UTC z czasem letnim UE, plik parowania, reguły łączenia i wysyłania lokalizacji) – testy na zmyślonych danych i wektorach z `docs/PROTOKOL.md`.

## Log po USB

```
pio device monitor
```

Komendy: `v` – wersja i przyczyna ostatniego resetu, `p` – stan zasilania układów i bateria,
`dfu` – tryb wgrywania UF2, `off` – wyłączenie (System OFF) po 15 s odliczania; z podłączonym USB
wyłączenie jest odrzucane, krótkie naciśnięcie przycisku przerywa odliczanie.
`log` – ostatnie 32 linie logu (zdarzenia i pomiary baterii), także z czasu bez podłączonego USB.
GPS i skan BLE włącza `link` (i `pair`); gdy GPS działa, co 10 s linia stanu: fix, satelity, wysokość, czas UTC
(bez współrzędnych). W czasie skanu w logu tylko aparaty: numer seryjny, stan, adres z typem (public/RPA), RSSI –
przy pierwszym zobaczeniu, przy zmianie stanu lub adresu i gdy aparat zniknie na 30 s. Log zawiera prawdziwy numer
i adres aparatu – nie publikować.
`pair` – parowanie z aparatem w trybie parowania (stan `03`; aparat: menu Bluetooth, parowanie). Potrzebny czas UTC
z GPS: GPS jest włączany na czas sesji, karta czeka na fix do 120 s, potem szuka aparatu do 60 s; najwyżej 3 próby
z przerwami 10 s i 30 s. Z aparatem w stanie `00` lub `06` karta się nie łączy. `pair off` albo krótkie naciśnięcie
przycisku przerywa. Przycisk: klik i w ciągu 0,5 s przytrzymanie 3 s = parowanie (dwa krótkie sygnały), udane
parowanie – rosnąca melodia, rezygnacja – dwa kliknięcia.
`link on` / `link off` – praca ze sparowanym aparatem; włącza się sama po starcie, gdy aparat jest sparowany.
Włącza GPS i skan, łączy się tylko ze sparowanym numerem w stanie `00` i dopiero, gdy GPS ma czas UTC; potem tryb GPS
w aparacie i lokalizacja od razu, dalej co 25 s (wcześniej po przesunięciu o ponad 20 m albo na prośbę aparatu, nie
częściej niż co 5 s). Bez fixa połączenie trwa, ale lokalizacje czekają. Po rozłączeniu przerwa 5 s, 15 s, potem 60 s;
gdy aparat odrzuci kartę – koniec łączenia do `link on` albo `pair`. `link off` rozłącza i wyłącza GPS. Gdy działa
`link`, steruje GPS-em i skanem. Oszczędzanie (M6): łączy się tylko przy sygnale co najmniej
−85 dBm; gdy aparatu nie widać od 5 min – skan rzadki i GPS w Backup; gdy aparat jest w czuwaniu (`06`) – karta czeka na
jego włączenie: przez pierwsze 10 min skan co 5 s i GPS gotowy (pozycja od razu po włączeniu aparatu), potem skan
co 10 s i GPS w Backup. Powrót aparatu przywraca pełny skan i GPS. Dioda: szukanie – dwa krótkie mignięcia co 2 s, połączony –
jedno co 4 s. Dźwięki: połączenie – dwa rosnące tony, koniec połączenia – dwa opadające.
`unpair` – zapomina sparowany aparat (identyfikator karty zostaje). `p` pokazuje też identyfikator karty (UUID)
i sparowany aparat – także tych wartości nie publikować.
Włączanie: przytrzymanie przycisku ok. 1 s (krótsze naciśnięcie – karta wraca do uśpienia; dioda może mignąć
od bootloadera) albo podłączenie USB.
`bat log on` / `bat log off` – dziennik baterii we flashu: na baterii wpis od razu i co 10 min (czas pracy, napięcie,
stan `link`), do 1024 wpisów; `bat log` – wypisanie, `bat log clear` – skasowanie. Procedura pomiaru prądu:
`docs/SPRZET.md`. Poniżej 3,40 V na baterii karta sama się wyłącza (dwa kliknięcia); z kablem nigdy.
Diagnostyka (tylko odczyty): `diag bat` – napięcie baterii przy różnych czasach od włączenia dzielnika (ok. 4 s).
`diag snd N` – wszystkie dźwięki (klik, start, wyłączenie, parowanie, sparowano, połączenie, rozłączenie)
z wypełnieniem PWM N% (1–50),
do dobrania głośności na słuch.
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
