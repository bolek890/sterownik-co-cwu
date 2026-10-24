// Konfiguracja sprzetu i wartosci domyslne sekretow.
// Sekrety ustawia sie w pliku .env (patrz .env.example), nie tutaj.
// Piny ekranu i termometrow mozna tez zmienic na stronie /config (zapis w sterowniku).
#pragma once
#include <Arduino.h>

#if __has_include("env.h")
#include "env.h"   // generowany z .env przez scripts/load_env.py
#endif

#define FW_VERSION "1.3.4"

#if CONFIG_IDF_TARGET_ESP32C3
#define BOARD_NAME "ESP32-C3"
#define FIRMWARE_FILE ".pio/build/c3/firmware.bin"
#elif CONFIG_IDF_TARGET_ESP32S3
#define BOARD_NAME "ESP32-S3"
#define FIRMWARE_FILE ".pio/build/sterownik/firmware.bin"
#else
#error "Obslugiwane uklady: ESP32-S3 (env:sterownik) i ESP32-C3 (env:c3)"
#endif

// Taktowanie procesora. S3: 80 MHz wystarcza (dwa rdzenie), a grzeje sie wyraznie mniej
// niz przy 240 MHz. C3 ma jeden rdzen - 160 MHz, zeby TLS Firebase nie spowalnial ekranu.
#if CONFIG_IDF_TARGET_ESP32C3
#define CPU_MHZ 160
#else
#define CPU_MHZ 80
#endif

// ---------------------------------------------------------------------------
// Zmienne z .env - wartosci domyslne, gdy w .env sa puste
// ---------------------------------------------------------------------------
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef FIREBASE_API_KEY
#define FIREBASE_API_KEY ""
#endif
#ifndef FIREBASE_DATABASE_URL
#define FIREBASE_DATABASE_URL ""
#endif
#ifndef FIREBASE_USER_EMAIL
#define FIREBASE_USER_EMAIL ""
#endif
#ifndef FIREBASE_USER_PASSWORD
#define FIREBASE_USER_PASSWORD ""
#endif
#ifndef WEB_USER
#define WEB_USER "admin"
#endif
#ifndef WEB_PASSWORD
#define WEB_PASSWORD ""
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "sterownik-co"
#endif
#ifndef NTP_SERVER
#define NTP_SERVER "pool.ntp.org"
#endif
#ifndef TZ_INFO
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD "sterownik"   // haslo sieci konfiguracyjnej (min. 8 znakow)
#endif

// ---------------------------------------------------------------------------
// Piny domyslne
// Ekran GMG12864-06D: 1 CS  2 RSE  3 RS  4 SCL  5 SI  6 VDD(3V3)  7 VSS  8 A  9 K
// DS18B20: rezystor 4.7k miedzy DATA a 3V3. Enkoder KY-040/EC11 zasilany z 3V3, nie z 5 V!
// ---------------------------------------------------------------------------
#if CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3 (np. SuperMini): wolne sa tylko GPIO 0-10, 20, 21.
// GPIO 11-17 = pamiec flash, 18/19 = USB, 9 = przycisk BOOT.
#define PIN_LCD_SCK   4
#define PIN_LCD_MOSI  6
#define PIN_LCD_CS    7
#define PIN_LCD_DC    5
#define PIN_LCD_RST   10
#define PIN_BACKLIGHT 3     // pin A ekranu przez 47-100 ohm, PWM
#define PIN_ONEWIRE   2     // pin startowy - rezystor 4.7k do 3V3 trzyma go w stanie wysokim, jak trzeba
#define PIN_ENC_CLK   0
#define PIN_ENC_DT    1
#define PIN_ENC_SW    21
#define PIN_BUTTON    9     // BOOT na plytkach C3
#define PIN_BUZZER    20    // brzeczyk alarmu
#define PIN_RESET_BTN 8     // przycisk reset do GND (na SuperMini swieci wtedy niebieska dioda)
#else
// ESP32-S3 Zero - piny sprawdzone na sprzecie
#define PIN_LCD_SCK   4
#define PIN_LCD_MOSI  5
#define PIN_LCD_CS    6
#define PIN_LCD_DC    7
#define PIN_LCD_RST   8
#define PIN_BACKLIGHT 9     // pin A ekranu przez 47-100 ohm, PWM
#define PIN_ONEWIRE   10    // stary sterownik mial GPIO4, ale tu GPIO4 to zegar ekranu
#define PIN_ENC_CLK   11
#define PIN_ENC_DT    13
#define PIN_ENC_SW    12
#define PIN_BUTTON    0     // BOOT na S3 Zero
#define PIN_BUZZER    2     // brzeczyk alarmu
#define PIN_RESET_BTN 1     // przycisk reset do GND
#endif
#define BACKLIGHT_CHANNEL 0

#define DS18B20_RESOLUTION 9   // jak w starym sterowniku: krok 0.5 Â°C

// Adresy przeniesione 1:1 ze starego sterownika (inne czujniki przypisuje sie w menu)
static const uint8_t SENSOR_ADDR_PIEC[8]   = {0x28, 0x71, 0xD0, 0x58, 0x00, 0x00, 0x00, 0x2B};
static const uint8_t SENSOR_ADDR_BOILER[8] = {0x28, 0xF7, 0xF4, 0x54, 0x00, 0x00, 0x00, 0x86};

#define ENCODER_DIRECTION 1     // -1, jesli obrot w prawo przewija w zla strone
#define ENCODER_HALF_STEP 0     // 1, jesli jeden krok wymaga dwoch zapadek
#define LONG_PRESS_MS     800

// Brzeczyk: typ (aktywny/pasywny) i poziom zalaczenia ustawia sie w menu Alarmy albo w panelu.
// Przycisk reset: przytrzymanie RESET_HOLD_MS = ponowne uruchomienie.
// Oba piny mozna zmienic na stronie /config (-1 = niepodlaczony).
#define BUZZER_CHANNEL 2        // kanal LEDC dla pasywnego (inny zegar niz podswietlenie)
#define BUZZER_FREQ    2700     // Hz - typowa czestotliwosc rezonansowa brzeczykow
#define RESET_HOLD_MS  300

// ---------------------------------------------------------------------------
// Pompy. -1 = nieuzywane (nic nie jest sterowane).
// Wolne po brzeczyku i resecie: S3 Zero - TX (GP43), RX (GP44); C3 - brak.
// Przycisk BOOT: krotko = nastepny ekran, przytrzymanie = wyciszenie alarmu.
// ---------------------------------------------------------------------------
#define PIN_PUMP_CO    -1   // przekaznik pompy obiegu CO
#define PIN_PUMP_CWU   -1   // przekaznik pompy ladowania bojlera
#define RELAY_ACTIVE_LEVEL  HIGH   // LOW dla modulow przekaznikow sterowanych zerem

// ---------------------------------------------------------------------------
// Czasy
// ---------------------------------------------------------------------------
#define SENSOR_READ_INTERVAL_MS 2000    // odczyt czujnikow
#define HISTORY_INTERVAL_MS     60000   // probka historii / wykresu
#define HISTORY_LEN             1440    // 24 h po 1 min
#define SENSOR_FAULT_COUNT      3       // tyle bledow z rzedu = alarm czujnika
#define HIST_INVALID            INT16_MIN

// Rdzenie: S3 ma dwa (sterowanie na 1, siec na 0), C3 jeden - wszystko na 0
#if CONFIG_FREERTOS_UNICORE
#define CORE_CONTROL 0
#define CORE_NET     0
#else
#define CORE_CONTROL 1
#define CORE_NET     0
#endif
