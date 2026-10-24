#include "devcfg.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>

const PinField PIN_FIELDS[] = {
  {"sck",  "Ekran SCL (zegar)",        &DeviceConfig::pinLcdSck,    PIN_LCD_SCK,   false},
  {"mosi", "Ekran SI (dane)",          &DeviceConfig::pinLcdMosi,   PIN_LCD_MOSI,  false},
  {"cs",   "Ekran CS",                 &DeviceConfig::pinLcdCs,     PIN_LCD_CS,    false},
  {"dc",   "Ekran RS (dane/komenda)",  &DeviceConfig::pinLcdDc,     PIN_LCD_DC,    false},
  {"rst",  "Ekran RSE (reset)",        &DeviceConfig::pinLcdRst,    PIN_LCD_RST,   true},
  {"bl",   "Podświetlenie (pin A)",    &DeviceConfig::pinBacklight, PIN_BACKLIGHT, true},
  {"ow",   "Termometry DS18B20",       &DeviceConfig::pinOneWire,   PIN_ONEWIRE,   false},
  {"buzz", "Brzęczyk alarmu",          &DeviceConfig::pinBuzzer,    PIN_BUZZER,    true},
  {"rbtn", "Przycisk reset (do GND)",  &DeviceConfig::pinResetBtn,  PIN_RESET_BTN, true},
};
const size_t PIN_FIELD_COUNT = sizeof(PIN_FIELDS) / sizeof(PIN_FIELDS[0]);

static const char *NS_NET = "siec";
static const char *NS_PINS = "piny";
static DeviceConfig cfg;

const DeviceConfig &devcfg() { return cfg; }

String apSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[24];
  snprintf(b, sizeof(b), "Sterownik-CO-%02X%02X", mac[4], mac[5]);
  return b;
}

#if CONFIG_IDF_TARGET_ESP32C3
// C3: GPIO 11-17 = pamiec flash, 18/19 = USB, 9 = BOOT (zajety jako przycisk)
static bool pinAllowed(int p) { return (p >= 0 && p <= 10) || p == 20 || p == 21; }
const char *PIN_HINT = "Dozwolone GPIO: 0–10, 20, 21 (11–17 to pamięć flash, 18–19 USB, 9 BOOT).";
#else
// S3: BOOT(0), startowe (3, 45, 46), USB (19, 20), pamiec flash (26-32), brak wyprowadzenia (22-25)
static bool pinAllowed(int p) {
  return (p >= 1 && p <= 18 && p != 3) || p == 21 || (p >= 33 && p <= 44) || p == 47 || p == 48;
}
const char *PIN_HINT = "Niedozwolone GPIO: 0, 3, 19, 20, 22–32, 45, 46.";
#endif

bool devcfgValidate(const DeviceConfig &c, String &err) {
  if (c.wifiSsid.length() > 32) { err = "Nazwa sieci Wi-Fi jest za długa (max 32 znaki)"; return false; }
  if (c.wifiPass.length() && (c.wifiPass.length() < 8 || c.wifiPass.length() > 63)) {
    err = "Hasło Wi-Fi musi mieć 8–63 znaki";
    return false;
  }
  if (c.hostname.isEmpty() || c.hostname.length() > 32) { err = "Nazwa urządzenia: 1–32 znaki"; return false; }
  for (char ch : c.hostname)
    if (!isalnum(ch) && ch != '-') { err = "Nazwa urządzenia: tylko litery, cyfry i '-'"; return false; }
  if (c.webPass.length() && c.webUser.isEmpty()) { err = "Podaj użytkownika panelu"; return false; }
  if (c.fbApiKey.isEmpty() != c.fbUrl.isEmpty()) {
    err = "Firebase wymaga obu pól: klucza API i adresu bazy";
    return false;
  }
  if (c.fbUrl.length() && !c.fbUrl.startsWith("https://")) {
    err = "Adres bazy Firebase musi zaczynać się od https://";
    return false;
  }
  // E-mail jest opcjonalny (puste = konto anonimowe jak w starym sterowniku). Zly e-mail
  // blokowal wysylke - przegladarka potrafi wpisac tu cos sama (autouzupelnianie).
  if (c.fbEmail.length()) {
    int at = c.fbEmail.indexOf('@');
    if (at < 1 || c.fbEmail.indexOf('.', at) < at + 2 || c.fbEmail.indexOf(' ') >= 0) {
      err = "Nieprawidłowy e-mail Firebase. Zostaw pole puste, aby logować się anonimowo";
      return false;
    }
    if (c.fbPass.length() < 6) {
      err = "Logowanie e-mailem do Firebase wymaga hasła (min. 6 znaków)";
      return false;
    }
  }

  // piny: dozwolone, bez powtorzen, bez kolizji z enkoderem i wyjsciami z config.h
  const int fixed[] = {PIN_BUTTON, PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW, PIN_PUMP_CO, PIN_PUMP_CWU};
  const char *fixedNames[] = {"przycisk BOOT", "enkoder CLK", "enkoder DT", "enkoder SW",
                              "pompa CO", "pompa CWU"};
  for (size_t i = 0; i < PIN_FIELD_COUNT; i++) {
    const PinField &f = PIN_FIELDS[i];
    int p = c.*f.ptr;
    if (p < 0) {
      if (f.optional) continue;
      err = String(f.label) + ": pin jest wymagany";
      return false;
    }
    if (!pinAllowed(p)) {
      err = String(f.label) + ": GPIO" + p + " nie może być użyty";
      return false;
    }
    for (size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); k++)
      if (fixed[k] == p) {
        err = String(f.label) + ": GPIO" + p + " zajmuje " + fixedNames[k];
        return false;
      }
    for (size_t j = 0; j < i; j++)
      if (c.*PIN_FIELDS[j].ptr == p) {
        err = String(f.label) + " i " + PIN_FIELDS[j].label + " mają ten sam GPIO" + p;
        return false;
      }
  }
  return true;
}

static String getStr(Preferences &p, bool opened, const char *key, const char *def) {
  return opened && p.isKey(key) ? p.getString(key, def) : String(def);
}

void devcfgLoad() {
  Preferences p;
  bool ok = p.begin(NS_NET, true);
  cfg.wifiSsid = getStr(p, ok, "ssid", WIFI_SSID);
  cfg.wifiPass = getStr(p, ok, "pass", WIFI_PASSWORD);
  cfg.fbApiKey = getStr(p, ok, "fbKey", FIREBASE_API_KEY);
  cfg.fbUrl    = getStr(p, ok, "fbUrl", FIREBASE_DATABASE_URL);
  cfg.fbEmail  = getStr(p, ok, "fbEmail", FIREBASE_USER_EMAIL);
  cfg.fbPass   = getStr(p, ok, "fbPass", FIREBASE_USER_PASSWORD);
  cfg.hostname = getStr(p, ok, "host", DEVICE_HOSTNAME);
  cfg.webUser  = getStr(p, ok, "webUser", WEB_USER);
  cfg.webPass  = getStr(p, ok, "webPass", WEB_PASSWORD);
  cfg.otaPass  = getStr(p, ok, "otaPass", OTA_PASSWORD);
  if (ok) p.end();

  ok = p.begin(NS_PINS, true);
  for (size_t i = 0; i < PIN_FIELD_COUNT; i++) {
    const PinField &f = PIN_FIELDS[i];
    cfg.*f.ptr = ok && p.isKey(f.key) ? p.getChar(f.key, f.def) : f.def;
  }
  if (ok) p.end();

  String err;
  if (!devcfgValidate(cfg, err)) {   // np. zmiana config.h po zapisie - wroc do domyslnych pinow
    Serial.printf("[konfiguracja] %s - przywracam domyslne piny\n", err.c_str());
    for (size_t i = 0; i < PIN_FIELD_COUNT; i++) cfg.*PIN_FIELDS[i].ptr = PIN_FIELDS[i].def;
  }
}

bool devcfgSave(const DeviceConfig &c, String &err) {
  if (!devcfgValidate(c, err)) return false;
  Preferences p;
  if (!p.begin(NS_NET, false)) { err = "Błąd zapisu (NVS)"; return false; }
  p.putString("ssid", c.wifiSsid);
  p.putString("pass", c.wifiPass);
  p.putString("fbKey", c.fbApiKey);
  p.putString("fbUrl", c.fbUrl);
  p.putString("fbEmail", c.fbEmail);
  p.putString("fbPass", c.fbPass);
  p.putString("host", c.hostname);
  p.putString("webUser", c.webUser);
  p.putString("webPass", c.webPass);
  p.putString("otaPass", c.otaPass);
  p.end();

  if (!p.begin(NS_PINS, false)) { err = "Błąd zapisu (NVS)"; return false; }
  for (size_t i = 0; i < PIN_FIELD_COUNT; i++) p.putChar(PIN_FIELDS[i].key, c.*PIN_FIELDS[i].ptr);
  p.end();
  cfg = c;
  Serial.println("[konfiguracja] zapisano");
  return true;
}
