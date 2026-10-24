#include "net.h"
#include "config.h"
#include "controller.h"
#include "devcfg.h"
#include "settings.h"
#include "shared.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Firebase_ESP_Client.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Firebase - sciezki i format danych IDENTYCZNE jak w starym sterowniku:
//   /readings/<epoch>/temperaturaPiec   = "23.50"   (String(float), -127.00 = blad)
//   /readings/<epoch>/temperaturaBoiler = "45.00"
//   /readings/<epoch>/timestamp         = "1759000000"
// ---------------------------------------------------------------------------
static FirebaseData fbdo;
static FirebaseAuth auth;
static FirebaseConfig config;
static FirebaseJson json;
static String tempPiecPath = "/temperaturaPiec";
static String tempBoilerPath = "/temperaturaBoiler";
static String timePath = "/timestamp";

static NetStatus ns;
static unsigned long lastSendedDataMiliseconds = 0;

static DNSServer dns;
static volatile int8_t apRequest = -1;   // 1 = wlacz, 0 = wylacz, -1 = brak
static bool apActive = false;
static bool apManual = false;            // wlaczony recznie - nie wylaczaj sam

const uint32_t AP_AFTER_BOOT_MS = 60000;    // brak polaczenia po starcie
const uint32_t AP_AFTER_LOSS_MS = 120000;   // utrata polaczenia w trakcie pracy
const uint32_t STA_RETRY_IN_AP_MS = 300000; // proba powrotu do sieci z trybu AP
const uint32_t AP_IDLE_OFF_MS = 600000;     // siec konf. bez nikogo przez 10 min -> wylacz

bool timeValid() { return time(nullptr) > 1700000000; }

void netSetOtaProgress(int p) {
  LockGuard g;
  ns.otaProgress = p;
}

void netRequestAp(bool on) { apRequest = on ? 1 : 0; }

static void setFbError(const char *msg) {
  LockGuard g;
  strlcpy(ns.fbError, msg, sizeof(ns.fbError));
}

// Bledne dane logowania (zly e-mail, haslo, klucz API) nie naprawia sie same - biblioteka
// ponawialaby je kilka razy na sekunde. Takie bledy wstrzymuja Firebase na FB_AUTH_PAUSE_MS.
const uint32_t FB_AUTH_PAUSE_MS = 600000;
static volatile bool fbAuthFailed = false;

static bool permanentAuthError(const String &msg) {
  const char *codes[] = {"INVALID_EMAIL", "INVALID_PASSWORD", "EMAIL_NOT_FOUND", "INVALID_LOGIN_CREDENTIALS",
                         "USER_DISABLED", "API_KEY_INVALID", "API key not valid", "OPERATION_NOT_ALLOWED",
                         "ADMIN_ONLY_OPERATION", "MISSING_PASSWORD"};
  for (const char *c : codes)
    if (msg.indexOf(c) >= 0) return true;
  return false;
}

// Kody bledow Google po polsku (ekran, /config, panel - pole ma 48 znakow)
static String friendlyAuthError(const String &msg) {
  if (msg.indexOf("ADMIN_ONLY_OPERATION") >= 0) return "włącz logowanie anonimowe w Firebase";
  if (msg.indexOf("OPERATION_NOT_ALLOWED") >= 0) return "włącz logowanie e-mailem w Firebase";
  if (msg.indexOf("INVALID_EMAIL") >= 0) return "nieprawidłowy e-mail Firebase";
  if (msg.indexOf("EMAIL_NOT_FOUND") >= 0 || msg.indexOf("INVALID_PASSWORD") >= 0 ||
      msg.indexOf("INVALID_LOGIN_CREDENTIALS") >= 0)
    return "zły e-mail lub hasło Firebase";
  if (msg.indexOf("USER_DISABLED") >= 0) return "konto Firebase zablokowane";
  if (msg.indexOf("API key not valid") >= 0 || msg.indexOf("API_KEY_INVALID") >= 0)
    return "zły klucz API Firebase";
  return "Firebase: " + msg;
}

static void tokenCallback(TokenInfo info) {
  static String lastError;
  if (info.status == token_status_ready) {
    if (lastError.length()) Serial.println("[firebase] zalogowano");
    lastError = "";
    setFbError("");
    return;
  }
  if (info.status != token_status_error) return;
  String msg = info.error.message.c_str();
  if (msg != lastError) {   // ten sam blad nie zasmieca logu
    Serial.printf("[firebase] blad logowania %d: %s\n", info.error.code, msg.c_str());
    lastError = msg;
  }
  setFbError(friendlyAuthError(msg).c_str());
  if (permanentAuthError(msg)) fbAuthFailed = true;
}

static String maskEmail(const String &e) {
  int at = e.indexOf('@');
  if (at < 1) return "\"" + e.substring(0, 2) + "...\" (bez @)";
  return e.substring(0, 1) + "***" + e.substring(at);
}

// Sprawdza e-mail i haslo jednym zapytaniem, ZANIM wywolamy Firebase.begin() - biblioteka
// nie ma limitu prob (max_token_generation_retry nie jest nigdzie uzywane) i przy zlych
// danych ponawia logowanie bez konca. Zwraca true = dane poprawne.
static bool checkEmailLogin(const DeviceConfig &d, String &err, bool &permanent) {
  WiFiClientSecure client;
  client.setInsecure();   // jak biblioteka Firebase: bez weryfikacji certyfikatu
  HTTPClient http;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + d.fbApiKey;
  if (!http.begin(client, url)) {
    err = "brak połączenia z Google";
    permanent = false;
    return false;
  }
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  JsonDocument req;
  req["email"] = d.fbEmail;
  req["password"] = d.fbPass;
  req["returnSecureToken"] = false;
  String body;
  serializeJson(req, body);
  int code = http.POST(body);
  String resp = code > 0 ? http.getString() : String();
  http.end();
  if (code == 200) return true;
  permanent = code >= 400 && code < 500;   // blad danych, nie sieci
  JsonDocument r;
  if (code > 0 && !deserializeJson(r, resp) && r["error"]["message"].is<const char *>())
    err = r["error"]["message"].as<const char *>();
  else
    err = code > 0 ? "HTTP " + String(code) : "brak połączenia z Google";
  return false;
}

// false = nie wywoluj Firebase.begin() (zle dane albo brak sieci) - fbTask sprobuje pozniej
static bool initFirebase() {
  const DeviceConfig &d = devcfg();
  config.api_key = d.fbApiKey;
  config.database_url = d.fbUrl;
  Firebase.reconnectWiFi(false);   // Wi-Fi obsluguje netTask (siec konfiguracyjna!), nie biblioteka
  fbdo.setResponseSize(4096);
  config.token_status_callback = tokenCallback;

  if (d.fbEmail.length()) {
    Serial.printf("[firebase] logowanie e-mailem %s\n", maskEmail(d.fbEmail).c_str());
    String err;
    bool permanent = false;
    if (!checkEmailLogin(d, err, permanent)) {
      Serial.printf("[firebase] logowanie e-mailem nieudane: %s\n", err.c_str());
      setFbError(friendlyAuthError(err).c_str());
      fbAuthFailed = permanent;
      return false;
    }
    auth.user.email = d.fbEmail;
    auth.user.password = d.fbPass;
  } else if (Firebase.signUp(&config, &auth, "", "")) {   // konto anonimowe jak dotad
    Serial.println("[firebase] signUp ok");
  } else {
    String msg = config.signer.signupError.message.c_str();
    Serial.printf("[firebase] rejestracja anonimowa nieudana: %s\n", msg.c_str());
    setFbError(friendlyAuthError(msg).c_str());
    fbAuthFailed = permanentAuthError(msg);
    return false;   // bez tokenu begin() wpadlby w nieskonczone ponawianie
  }
  Firebase.begin(&config, &auth);
  return true;
}

static void sendDataToFirebase() {
  Settings cfg = settingsGet();
  unsigned long interval = cfg.firebaseMin * 60000UL;
  setStage(STAGE_FB_READY);
  bool ready = Firebase.ready();
  setStage(STAGE_FB_IDLE);
  {
    LockGuard g;
    ns.fbReady = ready;
  }
  if (!ready || !timeValid()) return;   // bez czasu klucz /readings/<epoch> bylby bledny
  if (lastSendedDataMiliseconds != 0 && millis() - lastSendedDataMiliseconds <= interval) return;

  lastSendedDataMiliseconds = millis();
  CtrlState st = controllerSnapshot();
  int timeStamp = (int)time(nullptr);
  Serial.print("time: ");
  Serial.println(timeStamp);
  String parentPath = "/readings/" + String(timeStamp);

  json.clear();
  json.set(tempPiecPath.c_str(), String(st.s[PIEC].raw));
  json.set(tempBoilerPath.c_str(), String(st.s[BOILER].raw));
  json.set(timePath, String(timeStamp));

  setStage(STAGE_FB_SEND);
  bool ok = Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json);
  setStage(STAGE_FB_IDLE);
  if (ok) Serial.println("Updated JSON: OK");
  else Serial.println(fbdo.errorReason().c_str());

  LockGuard g;
  ns.fbLastOk = ok;
  ns.fbLastMs = millis();
  if (ok) {
    ns.fbSent++;
    ns.fbError[0] = 0;
  } else {
    strlcpy(ns.fbError, fbdo.errorReason().c_str(), sizeof(ns.fbError));
  }
}

// ---------------------------------------------------------------------------
// OTA z PlatformIO (pio run -e ota -t upload)
// ---------------------------------------------------------------------------
static void initOta() {
  ArduinoOTA.setHostname(devcfg().hostname.c_str());
  ArduinoOTA.setPassword(devcfg().otaPass.c_str());
  ArduinoOTA.onStart([]() { netSetOtaProgress(0); Serial.println("[ota] start"); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    netSetOtaProgress(total ? done * 100 / total : 0);
  });
  ArduinoOTA.onEnd([]() { netSetOtaProgress(100); Serial.println("[ota] koniec"); });
  ArduinoOTA.onError([](ota_error_t e) { netSetOtaProgress(-1); Serial.printf("[ota] blad %u\n", e); });
  ArduinoOTA.begin();
  Serial.printf("[ota] gotowe: %s.local\n", devcfg().hostname.c_str());
}

// ---------------------------------------------------------------------------
// Punkt dostepu do konfiguracji
// ---------------------------------------------------------------------------
static void startAp(bool manual) {
  apManual = manual;
  if (apActive) return;
  WiFi.setAutoReconnect(false);   // proby polaczenia zmienialyby kanal i rozlaczaly telefon
  WiFi.mode(WIFI_AP_STA);         // STA potrzebne do wyszukiwania sieci na stronie
  WiFi.softAP(apSsid().c_str(), AP_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_11dBm);   // telefon jest obok sterownika - pelna moc niepotrzebna
  dns.start(53, "*", WiFi.softAPIP());   // kazda domena -> sterownik (strona logowania sieci)
  apActive = true;
  Serial.printf("[wifi] siec konfiguracyjna %s, http://%s/config\n", apSsid().c_str(),
                WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // zasieg do routera
  WiFi.setAutoReconnect(true);
  apActive = false;
  apManual = false;
  if (devcfg().wifiSsid.length() && WiFi.status() != WL_CONNECTED)
    WiFi.begin(devcfg().wifiSsid.c_str(), devcfg().wifiPass.c_str());
  Serial.println("[wifi] siec konfiguracyjna wylaczona");
}

// ---------------------------------------------------------------------------
// Zadanie Firebase - osobno, bo wywolania biblioteki (TLS, logowanie) potrafia trwac
// sekundami albo utknac. Wczesniej blokowaly obsluge Wi-Fi i sieci konfiguracyjnej.
// ---------------------------------------------------------------------------
static void fbTask(void *) {
  bool initDone = false;
  uint32_t pausedUntil = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (WiFi.status() != WL_CONNECTED) continue;
    if (pausedUntil && (int32_t)(millis() - pausedUntil) < 0) continue;
    pausedUntil = 0;
    if (!initDone) {
      setStage(STAGE_FB_INIT);
      bool ok = initFirebase();
      setStage(STAGE_FB_IDLE);
      if (!ok) {
        // zle dane: ponow za 10 min (moga zostac poprawione w konsoli Firebase), siec: za 1 min
        uint32_t pause = fbAuthFailed ? FB_AUTH_PAUSE_MS : 60000;
        fbAuthFailed = false;
        pausedUntil = millis() + pause;
        Serial.printf("[firebase] kolejna proba za %lu min\n", (unsigned long)(pause / 60000));
        continue;
      }
      initDone = true;
    }
    // Blad trwaly juz po zalogowaniu (np. zmienione haslo) - tez bez ponawiania co sekunde
    if (fbAuthFailed) {
      fbAuthFailed = false;
      pausedUntil = millis() + FB_AUTH_PAUSE_MS;
      Serial.println("[firebase] bledne dane logowania - kolejna proba za 10 min");
      continue;
    }
    sendDataToFirebase();
  }
}

// ---------------------------------------------------------------------------
// Zadanie sieciowe: Wi-Fi, siec konfiguracyjna, OTA, stan
// ---------------------------------------------------------------------------
static void netTask(void *) {
  const DeviceConfig &d = devcfg();
  bool hasSsid = d.wifiSsid.length() > 0;
  bool servicesUp = false, everConnected = false, wasConnected = false;
  bool fbConfigured = d.fbApiKey.length() && d.fbUrl.length();
  bool otaEnabled = d.otaPass.length() > 0;
  uint32_t lastConnected = millis(), lastReconnect = millis(), lastStaTry = millis();
  uint32_t reconnectEvery = 30000;     // rosnie do 5 min - skanowanie kanalow kosztuje prad
  uint32_t lastApClientMs = millis();
  bool apAutoBlocked = false;          // siec konf. wylaczona sama - nie wlaczaj ponownie
  uint32_t lastStatus = 0;

  if (!hasSsid) startAp(false);

  for (;;) {
    uint32_t now = millis();
    if (apRequest >= 0) {
      if (apRequest == 1) {
        startAp(true);
        lastApClientMs = now;
      } else {
        stopAp();
      }
      apRequest = -1;
    }

    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !wasConnected)
      Serial.printf("[wifi] polaczono, IP %s\n", WiFi.localIP().toString().c_str());
    if (!connected && wasConnected) Serial.println("[wifi] rozlaczono");
    wasConnected = connected;
    if (connected) {
      lastConnected = now;
      everConnected = true;
      reconnectEvery = 30000;
      apAutoBlocked = false;
    }

    // Brak sieci zbyt dlugo -> wlacz punkt dostepu do konfiguracji
    if (!apActive && !apAutoBlocked && hasSsid && !connected &&
        now - lastConnected > (everConnected ? AP_AFTER_LOSS_MS : AP_AFTER_BOOT_MS)) {
      startAp(false);
      lastApClientMs = now;
    }

    if (apActive) {
      dns.processNextRequest();
      uint8_t clients = WiFi.softAPgetStationNum();
      if (clients) lastApClientMs = now;
      // co 5 min sprobuj wrocic do sieci, ale nie gdy ktos jest na stronie konfiguracji
      if (hasSsid && !connected && clients == 0 && now - lastStaTry > STA_RETRY_IN_AP_MS) {
        lastStaTry = now;
        WiFi.begin(d.wifiSsid.c_str(), d.wifiPass.c_str());
      }
      if (connected && !apManual && clients == 0) stopAp();
      // Siec konfiguracyjna nadaje bez przerwy (radio bez uspienia) - wylacz ja,
      // gdy nikt z niej nie korzysta. Wlaczenie ponowne: menu albo restart.
      else if (clients == 0 && now - lastApClientMs > AP_IDLE_OFF_MS) {
        Serial.println("[wifi] siec konfiguracyjna nieuzywana - wylaczam");
        stopAp();
        apAutoBlocked = true;
        lastReconnect = now;
      }
    } else if (hasSsid && !connected && now - lastReconnect > reconnectEvery) {
      lastReconnect = now;   // wlasne ponawianie, coraz rzadziej
      reconnectEvery = min<uint32_t>(reconnectEvery * 2, 300000);
      WiFi.disconnect();
      WiFi.begin(d.wifiSsid.c_str(), d.wifiPass.c_str());
    }

    if (connected && !servicesUp) {
      servicesUp = true;
      if (otaEnabled) initOta();      // ArduinoOTA uruchamia tez mDNS
      else MDNS.begin(d.hostname.c_str());
      MDNS.addService("http", "tcp", 80);
    }
    if (otaEnabled && servicesUp) ArduinoOTA.handle();

    if (now - lastStatus >= 500) {   // stan dla ekranu i WWW - 2x na sekunde wystarczy
      lastStatus = now;
      LockGuard g;
      ns.wifi = connected;
      ns.rssi = connected ? WiFi.RSSI() : 0;
      ns.ip = connected ? WiFi.localIP() : IPAddress();
      ns.apActive = apActive;
      ns.apClients = apActive ? WiFi.softAPgetStationNum() : 0;
      ns.timeValid = timeValid();
      ns.fbConfigured = fbConfigured;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void netBegin() {
  const DeviceConfig &d = devcfg();
  {
    LockGuard g;
    memset(&ns, 0, sizeof(ns));
    ns.otaProgress = -1;
    ns.wifiConfigured = d.wifiSsid.length() > 0;
    strlcpy(ns.apSsid, apSsid().c_str(), sizeof(ns.apSsid));
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);   // uspienie modemu miedzy ramkami routera (WIFI_PS_MIN_MODEM)
  WiFi.setHostname(d.hostname.c_str());
  WiFi.setAutoReconnect(true);
  if (d.wifiSsid.length()) WiFi.begin(d.wifiSsid.c_str(), d.wifiPass.c_str());
  else Serial.println("[wifi] brak sieci - uruchamiam siec konfiguracyjna");
  configTzTime(TZ_INFO, NTP_SERVER);
  xTaskCreatePinnedToCore(netTask, "siec", 8192, nullptr, 1, nullptr, CORE_NET);
  if (d.fbApiKey.length() && d.fbUrl.length())
    xTaskCreatePinnedToCore(fbTask, "firebase", 16384, nullptr, 1, nullptr, CORE_NET);   // TLS potrzebuje stosu
}

NetStatus netStatus() {
  LockGuard g;
  return ns;
}
