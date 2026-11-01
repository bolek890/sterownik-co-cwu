# Sterownik CO / CWU

Sterownik temperatury pieca CO i bojlera CWU na ESP32-S3 albo ESP32-C3 z wyświetlaczem 128×64,
dwoma czujnikami DS18B20 i enkoderem. Odczyty trafiają do Firebase Realtime Database, a sterownik
ma własny panel WWW, wykresy z 24 h, statystyki, alarmy i (opcjonalnie) sterowanie pompami.

Powstał jako następca prostszego sterownika z wyświetlaczem LCD 20×4 i jest z nim zgodny:
te same ścieżki i format danych w Firebase, ten sam sposób liczenia trendu i ta sama strona `http://<IP>/`.

## Możliwości

- **Ekran** 128×64 (ST7565R): temperatury z trendem jak w starym sterowniku (strzałka, Δ, czas od zmiany trendu),
  wykresy pieca i bojlera, statystyki dnia, stan pomp. Domyślnie stoi ekran z temperaturami; inne ekrany
  wybiera się enkoderem albo przyciskiem BOOT, a po 2 minutach ekran sam wraca do temperatur.
  „Sieć i system” pokazuje się tylko przy problemie z siecią, Firebase albo przegrzaniem układu.
- **Menu na enkoderze** (kliknięcie): wszystkie ustawienia z podglądem na żywo, przypisanie czujników
  (piec / bojler) z bieżącą temperaturą, sieć konfiguracyjna, informacje, zerowanie statystyk.
- **Alarmy**: temperatura graniczna pieca i bojlera z histerezą, wygasanie pieca, awaria czujnika.
  Miganie podświetlenia, brzęczyk (aktywny albo pasywny), wyciszanie enkoderem, BOOT albo w panelu.
- **Pompy CO i CWU** (opcjonalne przekaźniki): progi z histerezą, ochrona przed przegrzaniem i zamarzaniem,
  pompa bojlera nigdy nie pracuje, gdy piec jest zimniejszy od bojlera.
- **Panel WWW** `/panel`: odczyty, wykres 1–24 h, statystyki dziś/wczoraj, wszystkie ustawienia.
- **Konfiguracja** `/config`: Wi-Fi (z wyszukiwaniem sieci), Firebase, hasła, piny ekranu, termometrów,
  brzęczyka i przycisku reset, aktualizacja firmware przez przeglądarkę.
- **Sieć konfiguracyjna** `Sterownik-CO-xxxx` (192.168.4.1): włącza się sama, gdy nie ma Wi-Fi, albo z menu.
- **Firebase**: wysyłka co 5 min do `/readings/<epoch>` (format starego sterownika), logowanie anonimowe
  albo e-mailem. Błędne dane logowania nie blokują Wi-Fi ani ekranu.
- **Oszczędność energii**: procesor 80 MHz (S3), sprzętowe SPI, ekran odświeżany tylko przy zmianie.

## Sprzęt

| Element | ESP32-S3 Zero (`-e sterownik`) | ESP32-C3 SuperMini (`-e c3`) |
|---|---|---|
| Ekran SCL (pin 4) | GPIO4 | GPIO4 |
| Ekran SI (pin 5) | GPIO5 | GPIO6 |
| Ekran CS (pin 1) | GPIO6 | GPIO7 |
| Ekran RS (pin 3) | GPIO7 | GPIO5 |
| Ekran RSE (pin 2) | GPIO8 | GPIO10 |
| Podświetlenie A (pin 8), przez 47–100 Ω | GPIO9 | GPIO3 |
| DS18B20 (DATA + 4,7 kΩ do 3V3) | GPIO10 | GPIO2 |
| Enkoder CLK / DT / SW | GPIO11 / 13 / 12 | GPIO0 / 1 / 21 |
| Brzęczyk | GPIO2 | GPIO20 |
| Przycisk reset (do GND) | GPIO1 | GPIO8 |
| Przycisk BOOT (na płytce) | GPIO0 | GPIO9 |

Wyświetlacz: GMG12864-06D (ST7565R), zasilanie 3,3 V. Enkoder KY-040 / EC11 zasilany z 3,3 V.
Piny ekranu, termometrów, brzęczyka i przycisku reset można zmienić na stronie `/config`.
Przekaźniki pomp ustawia się w `include/config.h` (domyślnie wyłączone).

## Budowanie

Wymagany [PlatformIO](https://platformio.org/).

1. Skopiuj `.env.example` jako `.env` i uzupełnij (Wi-Fi, Firebase, hasła). Plik `.env` nie trafia do repozytorium.
   **Zmień `AP_PASSWORD`** — domyślne hasło sieci konfiguracyjnej jest publiczne.
2. Zbuduj i wgraj:

   ```bash
   pio run -e sterownik -t upload    # ESP32-S3 Zero
   pio run -e c3 -t upload           # ESP32-C3
   ```

3. Opcjonalnie skopiuj `platformio_local.ini.example` jako `platformio_local.ini`, aby na stałe wskazać port COM.

Wartości z `.env` są tylko startowe — po zapisaniu strony `/config` obowiązują dane zapisane w sterowniku.

### Aktualizacja przez Wi-Fi

- Strona `http://<IP>/config` → *Aktualizacja oprogramowania* → plik `.pio/build/<env>/firmware.bin`.
  Plik dla innego układu (S3/C3) jest odrzucany.
- Z PlatformIO (po ustawieniu hasła OTA): `pio run -e ota -t upload` (S3) albo `pio run -e c3-ota -t upload` (C3).

## Pierwsze uruchomienie

1. Bez skonfigurowanego Wi-Fi sterownik uruchamia sieć `Sterownik-CO-xxxx` (hasło z `AP_PASSWORD`).
   Połącz się z nią telefonem — strona konfiguracji otworzy się sama (albo wejdź na `http://192.168.4.1`).
2. Wybierz sieć Wi-Fi, wpisz dane Firebase i zapisz. Sterownik uruchomi się ponownie.
3. Kliknij enkoder → *Czujniki* i przypisz czujniki do pieca i bojlera (ogrzej jeden w dłoni, żeby je rozróżnić).

W projekcie Firebase musi być włączone logowanie **anonimowe** (albo podaj e-mail i hasło konta).

## Obsługa

| | Obrót enkodera | Kliknięcie | Przytrzymanie |
|---|---|---|---|
| Ekrany | poprzedni / następny ekran | menu Ustawienia (przy alarmie: wycisza) | menu Ustawienia |
| Menu | wybór | wejście / zmiana | wstecz |
| Edycja wartości | zmiana (szybki obrót = większe kroki) | zapis | anulowanie |

Przycisk BOOT: krótko = następny ekran, przytrzymanie = wyciszenie alarmu.
Przycisk reset: przytrzymanie 0,3 s = ponowne uruchomienie.

## Struktura

```
include/config.h        piny domyślne (S3/C3), czasy, wartości domyślne
src/controller.cpp      czujniki, trend, alarmy, pompy, statystyki, historia 24 h
src/net.cpp             Wi-Fi, sieć konfiguracyjna, NTP, Firebase, OTA
src/web.cpp             serwer WWW i API (/api/status, /api/history, /api/settings, /api/config, /update)
include/web_page.h      panel /panel
include/config_page.h   strona /config
src/ui.cpp              ekran, menu, animacje, brzęczyk
src/input.cpp           enkoder, BOOT, przycisk reset
src/settings.cpp        ustawienia użytkownika (NVS)
src/devcfg.cpp          konfiguracja urządzenia i piny (NVS)
scripts/load_env.py     zamiana .env na nagłówek env.h przy kompilacji
projektant/ekrany.json  ekrany i menu (src/ui.cpp) jako projekt dla projektanta ekranów 128×64
```

`projektant/ekrany.json` otwiera się w przeglądarkowym projektancie ekranów 128×64 (osobna aplikacja,
przycisk **Otwórz…**). Działa tam podgląd i symulacja wszystkich ekranów, menu, alarmów i komunikatów.
To kopia do projektowania i testów. Firmware nadal rysuje ekrany kodem z `src/ui.cpp`.

Biblioteki: U8g2, OneWire, DallasTemperature, Firebase Arduino Client Library, ESPAsyncWebServer,
AsyncTCP, ArduinoJson (pobierane automatycznie przez PlatformIO).
