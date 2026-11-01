// Ustawienia uzytkownika: zapis w NVS, edycja z panelu WWW.
#pragma once
#include <Arduino.h>

struct Settings {
  // Alarmy
  float alarmPiec   = 80;     // temperatura graniczna pieca (jak stare 80 °C)
  float alarmBoiler = 80;     // temperatura graniczna bojlera
  float alarmHyst   = 2;      // histereza kasowania alarmu
  float lowPiec     = 0;      // alarm wygasania pieca, 0 = wylaczony
  bool  alarmBlink  = true;   // miganie podswietlenia (jak w starym sterowniku)
  bool  buzzer      = true;
  bool  buzzerPassive = false; // pasywny (bez generatora) - sterowany tonem PWM
  bool  buzzerLow     = false; // modul zalaczany stanem niskim
  // Pompa CO
  bool  coEnabled   = true;
  float coOn        = 40;     // zalaczenie od tej temperatury pieca
  float coHyst      = 3;
  float antiFreeze  = 5;      // ochrona przed zamarzaniem, 0 = wylaczona
  // Pompa CWU (ladowanie bojlera)
  bool  cwuEnabled  = true;
  float cwuTarget   = 55;     // temperatura zadana bojlera
  float cwuDelta    = 5;      // o ile piec musi byc cieplejszy od bojlera
  float cwuHyst     = 3;
  // Wyswietlacz
  uint16_t backlight  = 60;   // 0-255
  uint16_t contrast   = 60;   // 0-255, 60 sprawdzone na GMG12864-06D
  uint16_t chartHours = 6;    // zakres wykresu na LCD
  // Dane
  uint16_t firebaseMin = 5;   // jak stare timerDelayFirebase = 5 min
  uint16_t trendMin    = 5;   // jak stare temperatureInterval = 5 min
};

enum SettingType : uint8_t { ST_FLOAT, ST_BOOL, ST_INT };
enum SettingFlag : uint8_t { SF_NONE = 0, SF_ZERO_OFF = 1 };   // 0 oznacza "wylaczone"

struct SettingField {
  const char *key;      // nazwa w NVS i w API (max 15 znakow)
  SettingType type;
  size_t offset;
  float min, max, step;
  const char *group;
  const char *label;    // pelna nazwa (panel WWW)
  const char *lcd;      // krotka nazwa na ekran 128 px
  const char *unit;
  uint8_t flags;
};

extern const SettingField SETTINGS_FIELDS[];
extern const size_t SETTINGS_FIELD_COUNT;

float settingsGetField(const Settings &s, const SettingField &f);
void  settingsSetField(Settings &s, const SettingField &f, float value);  // z przycieciem do zakresu

void     settingsLoad();          // NVS -> pamiec (przy starcie)
Settings settingsGet();           // kopia biezacych ustawien (bezpieczna miedzy zadaniami)
void     settingsSet(const Settings &s);   // ustaw i zaplanuj zapis
void     settingsSaveIfDirty();   // zapis do NVS, wolane z loop()
