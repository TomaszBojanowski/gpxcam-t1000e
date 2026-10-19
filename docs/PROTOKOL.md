# Protokół Bluetooth LE aparatów LUMIX

Opis protokołu, którym karta przekazuje aparatowi Panasonic LUMIX S5II (DC-S5M2) pozycję i czas.
Sprawdzony na S5II w aplikacji gpxcam na Androida (repo `TomaszBojanowski/gpxcam`,
`android/app/src/main/java/me/bojanowski/gpxcam/LumixProtocol.kt` i `PROTOKOL.md`) – tamten kod jest wzorcem.

## Ogłoszenie aparatu
- Dane producenta: `3a 00 <stan> <5 bajtów numeru seryjnego>`.
- `<stan>`: `00` aparat włączony, `03` tryb parowania („Dodaj nowe urządzenie”), `06` wyłączony przełącznikiem
  (czuwanie). **W stanie `06` się nie łączymy – połączenie budzi aparat.**
- Ogłaszana usługa: `054AC620-3214-11E6-AC0D-0002A5D5C51B`.
- Włączony: adres RPA. Czuwanie: adres publiczny. Legacy ADV_IND, connectable.
- Łączymy się tylko, gdy stan i numer seryjny są znane, a numer zgadza się z zapamiętanym.
- Obserwacja z S5II (M3, skan aktywny 50 ms co 100 ms): dane producenta są tylko w ogłoszeniu, nigdy w odpowiedzi
  na skan; ok. 3 raporty/s. Każda zmiana stanu zmienia adres: `00` → `03` i `03` → `00` – nowy RPA, `00` → `06` –
  adres publiczny, `06` → `00` – znów nowy RPA. W czuwaniu (`06`) aparat ogłasza się dalej.
- Karta przyjmuje tylko dokładnie ten układ: dane producenta (typ AD `0xFF`) mają 8 bajtów razem z `3a 00`.
- Praca normalna z S5II (M5): wyłączenie aparatu przełącznikiem zamyka połączenie (`HCI 0x13`); EXIF zdjęcia ma
  pozycję i czas GPS z ostatniej lokalizacji (czas wysyłki), status `A`, bez wysokości – tak samo z aplikacją
  Lumix Lab. Charakterystyka powiadomień nie ma CCCD, a aparat i tak wysyła powiadomienie (np. tuż przed
  wyłączeniem).
- Parowanie karty z S5II (M4): aparat przyznaje MTU 57 (karta prosi o 185 jak gpxcam), nie prosi o szyfrowanie;
  od połączenia do przyjęcia godziny ok. 1,2 s. Bez parowania na poziomie Bluetooth, jak w gpxcam.
  Dane Panasonic w innym układzie (inne urządzenia tej firmy) nie są aparatem. Dane mogą przyjść w ogłoszeniu
  albo w odpowiedzi na skan – czytamy oba (`src/lumix.cpp`).

## Usługa i charakterystyki
Usługa: `1FE71B85-8344-4E6A-BF1A-AC7ACDB3F8AF`

| Charakterystyka | Co |
|---|---|
| `239C7203-922F-43E9-BDF1-6D1FEDF6F43F` | klucz (odczyt, 4 bajty, inny przy każdym połączeniu) |
| `BFF12E7F-5944-4EB6-A90F-48D8786157EA` | identyfikator urządzenia (zapis, zaszyfrowany) |
| `ED8B1352-5AA7-4155-B7C9-521292DD1932` | nazwa aplikacji (zapis, zwykły tekst, 32 bajty) |
| `05A797B6-9C2F-4D18-99A7-FA61A3880BFE` | nazwa urządzenia (zapis, zaszyfrowana) |
| `39A02C41-C6E3-4B73-A3EA-537F23F0085A` | data i godzina (zapis, 10 bajtów) |
| `C289B6A4-B9EE-49AB-AB06-7099BB5EDAA8` | tryb GPS w aparacie: `01` włącz, `02` wyłącz |
| `437E6512-321A-4A94-9281-FAB1BFF49A91` | powiadomienia (włączyć przez CCCD `0x2902`) |
| `565B4253-8BAC-46AE-8C5D-EC6F94FC8714` | lokalizacja (zapis, 16 bajtów) |

Wszystkie zapisy z odpowiedzią (write with response).

## Kolejność po każdym połączeniu
Ściśle po kolei, każdy krok dopiero po odpowiedzi na poprzedni:
1. odczyt klucza,
2. zapis identyfikatora,
3. zapis nazwy aplikacji,
4. zapis nazwy urządzenia,
5. zapis godziny – **sukces oznacza, że aparat nas zna** (przy nieznanym identyfikatorze zwraca błąd ATT i rozłącza),
6. zapis `01` do trybu GPS, włączenie powiadomień,
7. lokalizacja od razu, potem co ~25 s.

Parowanie to ta sama sekwencja wykonana, gdy aparat jest w stanie `03`; aparat pokazuje wtedy „Pairing completed”.
Bez parowania nieznany identyfikator jest odrzucany przy zapisie godziny.

## Formaty bajtów

- **wordSwap(d):** dopełnij zerami do wielokrotności 4, w każdej czwórce odwróć kolejność bajtów.
- **XOR:** z 4-bajtowym kluczem powtarzanym co 4 bajty.
- **Identyfikator:** `xor(wordSwap(uuid16) + ff ff ff cd, klucz)`. UUID 16 bajtów w kolejności big-endian
  (jak w zapisie tekstowym), generowany raz przy parowaniu i trzymany we flashu.
- **Nazwa urządzenia:** `xor(wordSwap([dł][UTF-8]) + ff ff ff 00, klucz)`, tekst maks. 23 bajty, cięty na granicy znaku.
  Proponowana nazwa: `T1000-E` (aparat pokazuje ją na liście sparowanych urządzeń).
- **Nazwa aplikacji:** zwykły tekst UTF-8 dopełniony zerami do 32 bajtów, np. `gpxcam T1000-E 0.1`.
- **Godzina (10 B, little-endian):** rok u16, miesiąc, dzień, godzina, minuta, sekunda – **czas UTC**;
  potem przesunięcie strefy standardowej w minutach (i16) i flaga czasu letniego (u8).
  Dla Polski: `60` i `1` latem / `0` zimą (reguła UE: zmiana w ostatnią niedzielę marca i października o 01:00 UTC).
- **Lokalizacja (16 B, little-endian):** czas GPS u32 = `unix_seconds − 315964800` (**bez** sekund przestępnych),
  szerokość i32 ×1e7, długość i32 ×1e7, wysokość i16 w metrach, `0x41` („A” – pozycja ważna), `0x00`.
  **Czas to bieżąca chwila wysłania, nie czas fixa** – inaczej aparat po chwili wyszarza ikonę GPS.
  Bez ważnego fixa nie wysyłamy lokalizacji.

## Wektory testowe

Z testów gpxcam. Wektor lokalizacji ma zmyśloną pozycję (morze); policzono go według formatu
powyżej kodem, który odtwarza pozostałe wektory gpxcam.

Chwila: 2026-10-13 22:30:29 UTC, strefa Europe/Warsaw (czas letni).
- godzina → `ea070a0d161e1d3c0001`
- lokalizacja (54.6000000, 19.0000000, 0 m) → `8573f957804c8b20802b530b00004100`
- `wordSwap(01 02 03 04 05)` → `0403020100000005`
- nazwa „iPhone” przed XOR → `6850690600656e6fffffff00`; „iPhone 17 Pro Max” → 24 bajty
- identyfikator dla UUID `00010203-0405-0607-0809-0a0b0c0d0e0f` przed XOR → `03020100070605040b0a09080f0e0d0cffffffcd`
- `xor(00 00 00 00 0f, 01 02 03 04)` → `010203040e`

Wszystkie wektory, także lokalizacja, są sprawdzane w `test/test_lumix`; kalendarz i reguła czasu letniego UE –
w `test/test_utc`. Nazwa „Zażółć gęślą jaźń aparatu” (34 bajty UTF-8) jest cięta do 22 bajtów, jak w gpxcam.

Ogłoszenie (zmyślony numer seryjny `12 34 56 78 9a`, testy `test/test_lumix`):
- `02 01 06 09 ff 3a 00 00 12 34 56 78 9a` → aparat, stan `00` (włączony), numer `123456789a`
- `09 ff 3a 00 06 12 34 56 78 9a` (sama odpowiedź na skan) → aparat, stan `06` (czuwanie)
- `05 ff 3a 00 01 02` → dane Panasonic w innym układzie, nie aparat

## Zachowania aparatu warte uwagi
- Po kilku odrzuconych połączeniach aparat przestaje się ogłaszać – pomaga wyłączenie i włączenie aparatu.
- Aparat sam rozłącza się po zerwaniu sekwencji; nie wolno wtedy szybko łączyć się w kółko (backoff).
- Zegarki Garmin i inne urządzenia nie mają tu znaczenia – karta jest centralą BLE, aparat peryferium.
