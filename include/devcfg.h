// Konfiguracja urzadzenia zmieniana ze strony /config: Wi-Fi, Firebase, dostep, piny.
// Wartosci z .env i config.h sa domyslne - po zapisie na stronie obowiazuja te z NVS.
// Zmiany dzialaja po ponownym uruchomieniu (strona robi to sama).
#pragma once
#include <Arduino.h>

struct DeviceConfig {
  String wifiSsid, wifiPass;
  String fbApiKey, fbUrl, fbEmail, fbPass;
  String hostname, webUser, webPass, otaPass;
  int8_t pinLcdSck, pinLcdMosi, pinLcdCs, pinLcdDc, pinLcdRst, pinBacklight, pinOneWire;
  int8_t pinBuzzer, pinResetBtn;
};

struct PinField {
  const char *key;          // nazwa w NVS i w formularzu
  const char *label;
  int8_t DeviceConfig::*ptr;
  int8_t def;               // z config.h
  bool optional;            // -1 = niepodlaczony
};

extern const PinField PIN_FIELDS[];
extern const size_t PIN_FIELD_COUNT;
extern const char *PIN_HINT;   // opis dozwolonych pinow dla tego ukladu (strona /config)

void devcfgLoad();
const DeviceConfig &devcfg();   // tylko do odczytu; zmiana = devcfgSave + restart
bool devcfgValidate(const DeviceConfig &c, String &err);
bool devcfgSave(const DeviceConfig &c, String &err);
String apSsid();                // nazwa sieci konfiguracyjnej, np. "Sterownik-CO-1A2B"
