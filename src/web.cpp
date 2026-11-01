#include "web.h"
#include "config.h"
#include "config_page.h"
#include "controller.h"
#include "devcfg.h"
#include "net.h"
#include "settings.h"
#include "shared.h"
#include "ui.h"
#include "web_page.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>
#include <time.h>

static AsyncWebServer server(80);

// Polaczenie przez siec konfiguracyjna sterownika (192.168.4.x). Taka siec ma wlasne
// haslo, wiec nie pytamy dodatkowo o haslo panelu - to tez droga odzyskania dostepu.
static bool viaAp(AsyncWebServerRequest *request) {
  return WiFi.getMode() & WIFI_AP && request->client()->localIP() == WiFi.softAPIP();
}

static bool authorized(AsyncWebServerRequest *request) {
  const DeviceConfig &d = devcfg();
  if (d.webPass.isEmpty() || viaAp(request)) return true;
  return request->authenticate(d.webUser.c_str(), d.webPass.c_str());
}

// Logowanie tylko gdy ustawiono haslo panelu (.env albo strona /config)
static bool authOk(AsyncWebServerRequest *request) {
  if (authorized(request)) return true;
  request->requestAuthentication();
  return false;
}

static void sendJson(AsyncWebServerRequest *request, JsonDocument &doc) {
  AsyncResponseStream *res = request->beginResponseStream("application/json");
  res->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *res);
  request->send(res);
}

static const char *trendName(uint8_t t) {
  switch (t) {
    case TREND_UP: return "up";
    case TREND_DOWN: return "down";
    case TREND_FLAT: return "flat";
  }
  return "none";
}

static void numOrNull(JsonObject o, const char *key, float v, int decimals = 1) {
  if (isnan(v)) o[key] = nullptr;
  else o[key] = serialized(String(v, decimals));
}

static void sensorJson(JsonObject o, const SensorState &s, float limit) {
  numOrNull(o, "temp", s.ok ? s.temp : NAN);
  o["raw"] = serialized(String(s.raw));   // jak w starym sterowniku (2 miejsca, -127.00)
  o["ok"] = s.ok;
  o["found"] = s.found;
  o["errors"] = s.errors;
  o["trend"] = trendName(s.trend);
  numOrNull(o, "trendDiff", s.trend == TREND_NONE ? NAN : s.trendDiff);
  o["trendTime"] = s.trend == TREND_NONE ? String("--:--") : trendTimeLabel(s.trendStartMs);
  o["limit"] = limit;
}

static void statsJson(JsonObject o, const Stats &st) {
  const char *names[2] = {"piec", "boiler"};
  for (int i = 0; i < 2; i++) {
    JsonObject s = o[names[i]].to<JsonObject>();
    const DayStats &d = st.s[i];
    numOrNull(s, "min", d.min);
    numOrNull(s, "max", d.max);
    numOrNull(s, "avg", d.samples ? d.sum / d.samples : NAN);
    s["minutesAbove"] = d.minutesAbove;
  }
  o["alarms"] = st.alarms;
  JsonObject p = o["pumps"].to<JsonObject>();
  p["coMinutes"] = (uint32_t)(st.pumpOnSec[PUMP_CO] / 60);
  p["coStarts"] = st.pumpStarts[PUMP_CO];
  p["cwuMinutes"] = (uint32_t)(st.pumpOnSec[PUMP_CWU] / 60);
  p["cwuStarts"] = st.pumpStarts[PUMP_CWU];
}

static void handleStatus(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  CtrlState st = controllerSnapshot();
  Settings cfg = settingsGet();
  NetStatus net = netStatus();

  JsonDocument doc;
  doc["version"] = FW_VERSION " (" BOARD_NAME ")";
  doc["uptime"] = millis() / 1000;
  doc["time"] = net.timeValid ? (uint32_t)time(nullptr) : 0;
  sensorJson(doc["piec"].to<JsonObject>(), st.s[PIEC], cfg.alarmPiec);
  sensorJson(doc["boiler"].to<JsonObject>(), st.s[BOILER], cfg.alarmBoiler);

  JsonArray alarms = doc["alarms"].to<JsonArray>();
  for (uint8_t bit = 1; bit; bit <<= 1) {
    if (!(st.alarms & bit)) continue;
    char l1[40], l2[40];
    alarmText(bit, st, cfg, l1, l2, sizeof(l1));
    JsonObject a = alarms.add<JsonObject>();
    a["code"] = bit;
    a["text"] = String(l1) + (l2[0] ? String(" – ") + l2 : String());
    a["acked"] = (st.acked & bit) != 0;
  }

  doc["pumpsEnabled"] = cfg.pumps;
  const char *pumpNames[2] = {"co", "cwu"};
  JsonObject pumps = doc["pumps"].to<JsonObject>();
  for (int i = 0; i < 2; i++) {
    JsonObject p = pumps[pumpNames[i]].to<JsonObject>();
    p["on"] = st.pump[i].on;
    p["output"] = st.pump[i].hasOutput;
    p["reason"] = st.pump[i].reason;
  }

  statsJson(doc["today"].to<JsonObject>(), st.today);
  if (st.haveYesterday) statsJson(doc["yesterday"].to<JsonObject>(), st.yesterday);
  else doc["yesterday"] = nullptr;

  JsonObject n = doc["net"].to<JsonObject>();
  n["ssid"] = devcfg().wifiSsid;
  n["wifi"] = net.wifi;
  n["rssi"] = net.rssi;
  n["ip"] = net.ip.toString();
  n["host"] = devcfg().hostname;
  n["firebase"] = !net.fbConfigured ? "wyłączony" : net.fbLastMs == 0 ? "czeka" : net.fbLastOk ? "OK" : "błąd";
  n["fbAgo"] = net.fbLastMs ? (millis() - net.fbLastMs) / 1000 : -1;
  n["fbSent"] = net.fbSent;
  n["fbError"] = net.fbError;
  n["sensorsOnBus"] = st.devicesOnBus;
  numOrNull(n, "chipTemp", gChipTemp);
  n["resetReason"] = resetReasonText();
  n["stageBeforeReset"] = lastStageBeforeReset();
  n["minHeap"] = ESP.getMinFreeHeap();
  n["cpuMhz"] = getCpuFrequencyMhz();
  n["heap"] = ESP.getFreeHeap();
  sendJson(request, doc);
}

static void handleHistory(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  static int16_t buf[HISTORY_LEN];   // chronione blokada wewnatrz controllerHistory
  AsyncResponseStream *res = request->beginResponseStream("application/json");
  res->addHeader("Cache-Control", "no-store");
  res->printf("{\"interval\":%d", HISTORY_INTERVAL_MS / 1000);
  const char *names[2] = {"piec", "boiler"};
  for (int s = 0; s < 2; s++) {
    int n = controllerHistory(s, buf, HISTORY_LEN);
    res->printf(",\"%s\":[", names[s]);
    for (int i = 0; i < n; i++) {
      if (i) res->print(',');
      if (buf[i] == HIST_INVALID) res->print("null");
      else res->printf("%.1f", buf[i] / 10.0f);
    }
    res->print(']');
  }
  res->print('}');
  request->send(res);
}

static void settingsJson(JsonDocument &doc, const Settings &cfg) {
  JsonObject values = doc["values"].to<JsonObject>();
  JsonArray fields = doc["fields"].to<JsonArray>();
  for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++) {
    const SettingField &f = SETTINGS_FIELDS[i];
    float v = settingsGetField(cfg, f);
    if (f.type == ST_BOOL) values[f.key] = v >= 0.5f;
    else if (f.type == ST_INT) values[f.key] = (int)v;
    else values[f.key] = v;
    JsonObject o = fields.add<JsonObject>();
    o["key"] = f.key;
    o["type"] = f.type == ST_BOOL ? "bool" : f.type == ST_INT ? "int" : "float";
    o["min"] = f.min;
    o["max"] = f.max;
    o["step"] = f.step;
    o["group"] = f.group;
    o["label"] = f.label;
    o["unit"] = f.unit;
  }
  doc["pumpOutputs"]["co"] = PIN_PUMP_CO >= 0;
  doc["pumpOutputs"]["cwu"] = PIN_PUMP_CWU >= 0;
}

static void handleSettingsGet(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  JsonDocument doc;
  settingsJson(doc, settingsGet());
  sendJson(request, doc);
}

static void handleSettingsPost(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  Settings cfg = settingsGet();
  int changed = 0;
  for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++) {
    const SettingField &f = SETTINGS_FIELDS[i];
    if (!request->hasParam(f.key, true)) continue;
    String v = request->getParam(f.key, true)->value();
    v.trim();
    v.replace(',', '.');
    float value;
    if (f.type == ST_BOOL) value = (v == "1" || v == "true" || v == "on") ? 1 : 0;
    else {
      char *end;
      value = strtof(v.c_str(), &end);
      if (end == v.c_str()) continue;   // nie liczba - pomin
    }
    settingsSetField(cfg, f, value);
    changed++;
  }
  settingsSet(cfg);
  Serial.printf("[www] zmieniono ustawienia (%d pol)\n", changed);
  JsonDocument doc;
  settingsJson(doc, settingsGet());
  sendJson(request, doc);
}

// ---------------------------------------------------------------------------
// Konfiguracja urzadzenia (/config)
// ---------------------------------------------------------------------------
static void handleConfigGet(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  const DeviceConfig &d = devcfg();
  NetStatus net = netStatus();
  JsonDocument doc;
  doc["version"] = FW_VERSION;
  doc["board"] = BOARD_NAME;
  doc["firmwareFile"] = FIRMWARE_FILE;
  doc["pinHint"] = PIN_HINT;
  doc["viaAp"] = viaAp(request);

  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = d.wifiSsid;
  w["passSet"] = d.wifiPass.length() > 0;
  w["connected"] = net.wifi;
  w["ip"] = net.wifi ? net.ip.toString() : String();
  w["rssi"] = net.rssi;
  w["apActive"] = net.apActive;
  w["apSsid"] = net.apSsid;

  JsonObject f = doc["firebase"].to<JsonObject>();
  f["apiKey"] = d.fbApiKey;
  f["url"] = d.fbUrl;
  f["email"] = d.fbEmail;
  f["passSet"] = d.fbPass.length() > 0;
  f["status"] = !net.fbConfigured ? "wyłączony" : net.fbLastMs == 0 ? "czeka" : net.fbLastOk ? "OK" : "błąd";
  f["error"] = net.fbError;

  JsonObject a = doc["access"].to<JsonObject>();
  a["host"] = d.hostname;
  a["webUser"] = d.webUser;
  a["webPassSet"] = d.webPass.length() > 0;
  a["otaPassSet"] = d.otaPass.length() > 0;

  JsonArray pins = doc["pins"].to<JsonArray>();
  for (size_t i = 0; i < PIN_FIELD_COUNT; i++) {
    const PinField &p = PIN_FIELDS[i];
    JsonObject o = pins.add<JsonObject>();
    o["key"] = p.key;
    o["label"] = p.label;
    o["value"] = d.*p.ptr;
    o["def"] = p.def;
    o["optional"] = p.optional;
  }
  JsonObject fx = doc["fixedPins"].to<JsonObject>();
  fx["Enkoder CLK"] = PIN_ENC_CLK;
  fx["Enkoder DT"] = PIN_ENC_DT;
  fx["Enkoder SW"] = PIN_ENC_SW;
  fx["Przycisk BOOT"] = PIN_BUTTON;
  sendJson(request, doc);
}

static void handleConfigPost(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  DeviceConfig n = devcfg();
  auto text = [&](const char *key, String &dst) {
    if (!request->hasParam(key, true)) return;
    dst = request->getParam(key, true)->value();
    dst.trim();
  };
  // Hasla: puste pole = bez zmian, <pole>_clear = usun
  auto secret = [&](const char *key, String &dst) {
    if (request->hasParam(String(key) + "_clear", true)) dst = "";
    else if (request->hasParam(key, true)) {
      String v = request->getParam(key, true)->value();
      if (v.length()) dst = v;
    }
  };
  text("ssid", n.wifiSsid);
  secret("wifiPass", n.wifiPass);
  text("fbKey", n.fbApiKey);
  text("fbUrl", n.fbUrl);
  text("fbEmail", n.fbEmail);
  secret("fbPass", n.fbPass);
  text("host", n.hostname);
  text("webUser", n.webUser);
  secret("webPass", n.webPass);
  secret("otaPass", n.otaPass);
  for (size_t i = 0; i < PIN_FIELD_COUNT; i++) {
    const PinField &p = PIN_FIELDS[i];
    if (!request->hasParam(p.key, true)) continue;
    String v = request->getParam(p.key, true)->value();
    v.trim();
    n.*p.ptr = v.length() ? (int8_t)v.toInt() : -1;
  }

  String err;
  if (!devcfgSave(n, err)) {
    JsonDocument doc;
    doc["error"] = err;
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    res->setCode(400);
    serializeJson(doc, *res);
    request->send(res);
    return;
  }
  request->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  requestRestart();
}

static void handleWifiScan(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    request->send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    request->send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  JsonDocument doc;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;   // ta sama siec z kilku punktow - zostaw najsilniejszy
    for (JsonObject o : list)
      if (ssid == o["ssid"].as<const char *>()) dup = true;
    if (dup) continue;
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(request, doc);
}

// ---------------------------------------------------------------------------
// Aktualizacja oprogramowania ze strony (plik firmware.bin)
// ---------------------------------------------------------------------------
static bool updateAllowed = false;
static String updateError;

static void handleUpdateUpload(AsyncWebServerRequest *request, const String &filename, size_t index,
                               uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    updateError = "";
    updateAllowed = authorized(request);
    if (!updateAllowed) return;
    // Naglowek obrazu ESP: bajt 0 = 0xE9, bajty 12-13 = ID ukladu (9 = ESP32-S3, 5 = ESP32-C3).
    // Plik dla innego ukladu nie uruchomilby sie, wiec odrzucamy go od razu.
    if (len < 16 || data[0] != 0xE9 || (data[12] | (data[13] << 8)) != CONFIG_IDF_FIRMWARE_CHIP_ID) {
      updateError = "To nie jest plik firmware.bin dla " BOARD_NAME;
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      updateError = Update.errorString();
      return;
    }
    Serial.printf("[aktualizacja] start: %s\n", filename.c_str());
    netSetOtaProgress(0);
  }
  if (!updateAllowed || updateError.length()) return;
  if (Update.write(data, len) != len) {
    updateError = Update.errorString();
    Update.abort();
    netSetOtaProgress(-1);
    return;
  }
  size_t total = request->contentLength();
  if (total) netSetOtaProgress(min<int>(99, (index + len) * 100 / total));
  if (final) {
    if (Update.end(true)) {
      netSetOtaProgress(100);
      Serial.printf("[aktualizacja] zapisano %u B\n", (unsigned)(index + len));
    } else {
      updateError = Update.errorString();
      netSetOtaProgress(-1);
    }
  }
}

static void handleUpdateDone(AsyncWebServerRequest *request) {
  if (!authOk(request)) return;
  JsonDocument doc;
  if (updateError.length() || !Update.isFinished()) {
    doc["error"] = updateError.length() ? updateError : String("Plik nie został odebrany w całości");
    netSetOtaProgress(-1);
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    res->setCode(400);
    serializeJson(doc, *res);
    request->send(res);
    return;
  }
  doc["ok"] = true;
  sendJson(request, doc);
  requestRestart(2000);
}

void webBegin() {
  // Strona jak w starym sterowniku - tekst i format bez zmian
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    CtrlState st = controllerSnapshot();
    String tempStr = "Temperatura pieca: " + String(st.s[PIEC].raw) + " &ordmC<br>";
    tempStr += "Temperatura boilera: " + String(st.s[BOILER].raw) + " &ordmC<br>";
    String html = "<html><body><h1>Temperatura z czujnik&oacutew DS18B20</h1>";
    html += "<p>" + tempStr + "</p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/panel", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authOk(request)) return;
    request->send(200, "text/html; charset=utf-8", (const uint8_t *)PANEL_HTML, strlen(PANEL_HTML));
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/ack", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authOk(request)) return;
    controllerAck();
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/buzzer/test", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authOk(request)) return;
    if (devcfg().pinBuzzer < 0) {
      request->send(400, "application/json", "{\"error\":\"Brak pinu brzęczyka (strona /config)\"}");
      return;
    }
    uiBuzzerTest();
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/stats/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authOk(request)) return;
    controllerResetStats();
    request->send(200, "application/json", "{\"ok\":true}");
  });
  // --- konfiguracja urzadzenia, Wi-Fi, aktualizacja ---
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authOk(request)) return;
    request->send(200, "text/html; charset=utf-8", (const uint8_t *)CONFIG_HTML, strlen(CONFIG_HTML));
  });
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authOk(request)) return;
    request->send(200, "application/json", "{\"ok\":true}");
    requestRestart();
  });
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

  server.onNotFound([](AsyncWebServerRequest *request) {
    // W sieci konfiguracyjnej kazdy adres (np. test telefonu "czy jest internet")
    // prowadzi na strone konfiguracji - telefon sam ja wtedy otworzy.
    if (viaAp(request)) {
      request->redirect("http://" + WiFi.softAPIP().toString() + "/config");
      return;
    }
    request->send(404, "text/plain", "Nie znaleziono");
  });
  server.begin();
}
