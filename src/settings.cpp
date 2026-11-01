#include "settings.h"
#include "shared.h"
#include <Preferences.h>
#include <stddef.h>

#define FLD(name, type, mn, mx, st, grp, lbl, lcd, unit, flags) \
  {#name, type, offsetof(Settings, name), mn, mx, st, grp, lbl, lcd, unit, flags}

const SettingField SETTINGS_FIELDS[] = {
  FLD(alarmPiec,   ST_FLOAT, 40, 110, 0.5, "Alarmy", "Temperatura graniczna pieca", "Limit pieca", "°C", SF_NONE),
  FLD(alarmBoiler, ST_FLOAT, 30, 95,  0.5, "Alarmy", "Temperatura graniczna bojlera", "Limit bojlera", "°C", SF_NONE),
  FLD(alarmHyst,   ST_FLOAT, 0.5, 10, 0.5, "Alarmy", "Histereza kasowania alarmu", "Histereza", "°C", SF_NONE),
  FLD(lowPiec,     ST_FLOAT, 0, 60,   1,   "Alarmy", "Alarm wygasania pieca (0 = wył.)", "Wygasanie", "°C", SF_ZERO_OFF),
  FLD(alarmBlink,  ST_BOOL,  0, 1,    1,   "Alarmy", "Miganie podświetlenia przy alarmie", "Miganie", "", SF_NONE),
  FLD(buzzer,      ST_BOOL,  0, 1,    1,   "Alarmy", "Sygnał dźwiękowy przy alarmie", "Dźwięk", "", SF_NONE),
  FLD(buzzerPassive, ST_BOOL, 0, 1,   1,   "Alarmy", "Brzęczyk pasywny (bez generatora)", "Brz. pasywny", "", SF_NONE),
  FLD(buzzerLow,   ST_BOOL,  0, 1,    1,   "Alarmy", "Brzęczyk załączany stanem niskim", "Brz. stan niski", "", SF_NONE),
  FLD(pumps,       ST_BOOL,  0, 1,    1,   "Pompy", "Sekcja pomp (sterowanie, ekran, ustawienia)", "Włączone", "", SF_NONE),
  FLD(coEnabled,   ST_BOOL,  0, 1,    1,   "Pompa CO", "Sterowanie pompą CO", "Sterowanie", "", SF_NONE),
  FLD(coOn,        ST_FLOAT, 25, 80,  0.5, "Pompa CO", "Załączenie od temperatury pieca", "Załącz od", "°C", SF_NONE),
  FLD(coHyst,      ST_FLOAT, 1, 15,   0.5, "Pompa CO", "Histereza", "Histereza", "°C", SF_NONE),
  FLD(antiFreeze,  ST_FLOAT, 0, 15,   0.5, "Pompa CO", "Ochrona przed zamarzaniem (0 = wył.)", "Antyzamarzanie", "°C", SF_ZERO_OFF),
  FLD(cwuEnabled,  ST_BOOL,  0, 1,    1,   "Pompa CWU", "Sterowanie pompą ładującą bojler", "Sterowanie", "", SF_NONE),
  FLD(cwuTarget,   ST_FLOAT, 30, 80,  0.5, "Pompa CWU", "Temperatura zadana bojlera", "Temp. zadana", "°C", SF_NONE),
  FLD(cwuDelta,    ST_FLOAT, 2, 20,   0.5, "Pompa CWU", "Min. przewaga pieca nad bojlerem", "Przewaga pieca", "°C", SF_NONE),
  FLD(cwuHyst,     ST_FLOAT, 1, 15,   0.5, "Pompa CWU", "Histereza", "Histereza", "°C", SF_NONE),
  FLD(backlight,   ST_INT,   0, 255,  1,   "Wyświetlacz", "Podświetlenie (0–255)", "Podświetlenie", "", SF_NONE),
  FLD(contrast,    ST_INT,   0, 255,  1,   "Wyświetlacz", "Kontrast (0–255)", "Kontrast", "", SF_NONE),
  FLD(chartHours,  ST_INT,   1, 24,   1,   "Wyświetlacz", "Zakres wykresu na ekranie", "Zakres wykresu", "h", SF_NONE),
  FLD(firebaseMin, ST_INT,   1, 60,   1,   "Dane", "Wysyłka do Firebase co", "Firebase co", "min", SF_NONE),
  FLD(trendMin,    ST_INT,   1, 60,   1,   "Dane", "Próbkowanie trendu co", "Trend co", "min", SF_NONE),
};
const size_t SETTINGS_FIELD_COUNT = sizeof(SETTINGS_FIELDS) / sizeof(SETTINGS_FIELDS[0]);

static const char *NVS_NAMESPACE = "sterownik";
static Settings current;
static bool dirty = false;

static void *fieldPtr(const Settings &s, const SettingField &f) {
  return (uint8_t *)&s + f.offset;
}

float settingsGetField(const Settings &s, const SettingField &f) {
  void *p = fieldPtr(s, f);
  switch (f.type) {
    case ST_FLOAT: return *(float *)p;
    case ST_BOOL:  return *(bool *)p ? 1 : 0;
    case ST_INT:   return *(uint16_t *)p;
  }
  return 0;
}

void settingsSetField(Settings &s, const SettingField &f, float value) {
  if (isnan(value)) return;
  value = constrain(value, f.min, f.max);
  void *p = fieldPtr(s, f);
  switch (f.type) {
    case ST_FLOAT: *(float *)p = value; break;
    case ST_BOOL:  *(bool *)p = value >= 0.5f; break;
    case ST_INT:   *(uint16_t *)p = (uint16_t)lroundf(value); break;
  }
}

void settingsLoad() {
  Settings s;   // wartosci domyslne
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++) {
      const SettingField &f = SETTINGS_FIELDS[i];
      if (!prefs.isKey(f.key)) continue;
      float v = settingsGetField(s, f);
      switch (f.type) {
        case ST_FLOAT: v = prefs.getFloat(f.key, v); break;
        case ST_BOOL:  v = prefs.getBool(f.key, v >= 0.5f) ? 1 : 0; break;
        case ST_INT:   v = prefs.getUShort(f.key, (uint16_t)v); break;
      }
      settingsSetField(s, f, v);
    }
    prefs.end();
  }
  LockGuard g;
  current = s;
}

Settings settingsGet() {
  LockGuard g;
  return current;
}

void settingsSet(const Settings &s) {
  Settings v = s;
  for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++)
    settingsSetField(v, SETTINGS_FIELDS[i], settingsGetField(v, SETTINGS_FIELDS[i]));
  LockGuard g;
  current = v;
  dirty = true;
}

void settingsSaveIfDirty() {
  Settings s;
  {
    LockGuard g;
    if (!dirty) return;
    dirty = false;
    s = current;
  }
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    Serial.println("[ustawienia] blad otwarcia NVS");
    return;
  }
  for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++) {
    const SettingField &f = SETTINGS_FIELDS[i];
    float v = settingsGetField(s, f);
    switch (f.type) {
      case ST_FLOAT: prefs.putFloat(f.key, v); break;
      case ST_BOOL:  prefs.putBool(f.key, v >= 0.5f); break;
      case ST_INT:   prefs.putUShort(f.key, (uint16_t)v); break;
    }
  }
  prefs.end();
  Serial.println("[ustawienia] zapisano w NVS");
}
