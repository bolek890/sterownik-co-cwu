#include "controller.h"
#include "shared.h"
#include "devcfg.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <time.h>

static OneWire oneWire(PIN_ONEWIRE);   // pin z konfiguracji ustawia controlTask
static DallasTemperature sensors(&oneWire);

static CtrlState st;
static int16_t hist[2][HISTORY_LEN];
static int histHead = 0, histCount = 0;
static int currentYday = -1;
static bool lowArmed = false;
static volatile bool addrChanged = false;   // przypisanie z menu - przeskanuj magistrale
static const char *NVS_SENSORS = "czujniki";
static const char *NVS_KEYS[2] = {"piec", "bojler"};

// Powody stanu pomp (wyswietlane na ekranie i w WWW)
static const char *const R_START    = "start";
static const char *const R_DISABLED = "wyłączona";
static const char *const R_SENSOR   = "awaria czujnika";
static const char *const R_OVERHEAT = "przegrzanie pieca";
static const char *const R_FREEZE   = "ochrona mrozowa";
static const char *const R_HEATING  = "piec grzeje";
static const char *const R_COLD     = "piec zimny";
static const char *const R_NODATA   = "brak odczytu";
static const char *const R_CWU_HEAT = "grzanie CWU";
static const char *const R_CWU_DONE = "bojler nagrzany";
static const char *const R_CWU_COLD = "piec za zimny";
static const char *const R_DUMP     = "zrzut ciepła";

// ---------------------------------------------------------------------------
// Trend - przepisane 1:1 ze starego sterownika (checkTrend, updateTrendInfo)
// ---------------------------------------------------------------------------
static uint8_t checkTrend(const float temps[3]) {
  if (temps[0] > temps[1] && temps[1] > temps[2]) return TREND_UP;
  if (temps[0] < temps[1] && temps[1] < temps[2]) return TREND_DOWN;
  return TREND_FLAT;
}

static void trendSample(SensorState &s, float temp) {
  s.last[2] = s.last[1];
  s.last[1] = s.last[0];
  s.last[0] = temp;
  uint8_t trend = checkTrend(s.last);
  if (trend != s.lastTrend) {
    s.trendRef = s.last[1];     // temperatura poprzedzajaca zmiane trendu
    s.trendDiff = 0;
    s.trendStartMs = millis();
  }
  if (trend == TREND_UP)   s.trendDiff = temp - s.trendRef;
  if (trend == TREND_DOWN) s.trendDiff = s.trendRef - temp;
  s.trend = trend;
  s.lastTrend = trend;
}

String trendTimeLabel(uint32_t startMs) {
  uint32_t minutes = (millis() - startMs) / 60000;
  uint32_t hours = minutes / 60;
  minutes %= 60;
  if (hours > 99) return ">100h";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes);
  return buf;
}

// ---------------------------------------------------------------------------
// Statystyki i historia
// ---------------------------------------------------------------------------
static void resetDay(Stats &d) {
  memset(&d, 0, sizeof(d));
  for (auto &s : d.s) { s.min = NAN; s.max = NAN; }
}

static void checkDayRollover() {
  time_t now = time(nullptr);
  if (now < 1700000000) return;   // czas jeszcze nie zsynchronizowany
  struct tm lt;
  localtime_r(&now, &lt);
  if (currentYday < 0) { currentYday = lt.tm_yday; return; }
  if (lt.tm_yday != currentYday) {
    st.yesterday = st.today;
    st.haveYesterday = true;
    resetDay(st.today);
    currentYday = lt.tm_yday;
  }
}

static void pushHistory() {
  for (int i = 0; i < 2; i++)
    hist[i][histHead] = st.s[i].ok ? (int16_t)lroundf(st.s[i].temp * 10) : HIST_INVALID;
  histHead = (histHead + 1) % HISTORY_LEN;
  if (histCount < HISTORY_LEN) histCount++;
}

int controllerHistory(uint8_t sensor, int16_t *out, int maxN) {
  LockGuard g;
  int n = min(histCount, maxN);
  int start = (histHead - n + HISTORY_LEN) % HISTORY_LEN;
  for (int i = 0; i < n; i++) out[i] = hist[sensor][(start + i) % HISTORY_LEN];
  return n;
}

// ---------------------------------------------------------------------------
// Alarmy
// ---------------------------------------------------------------------------
static void evaluateAlarms(const Settings &cfg) {
  const SensorState &p = st.s[PIEC], &b = st.s[BOILER];
  uint8_t a = st.alarms;
  auto hyst = [&](uint8_t bit, bool setCond, bool clearCond) {
    if (setCond) a |= bit;
    else if (clearCond) a &= ~bit;
  };
  if (p.ok) hyst(AL_PIEC_HIGH, p.temp >= cfg.alarmPiec, p.temp < cfg.alarmPiec - cfg.alarmHyst);
  if (b.ok) hyst(AL_BOILER_HIGH, b.temp >= cfg.alarmBoiler, b.temp < cfg.alarmBoiler - cfg.alarmHyst);

  // Wygasanie: uzbraja sie, gdy piec byl wyraznie cieply, i wyzwala przy spadku do progu
  if (cfg.lowPiec <= 0) {
    a &= ~AL_PIEC_LOW;
    lowArmed = false;
  } else if (p.ok) {
    if (p.temp >= cfg.lowPiec + 5) { lowArmed = true; a &= ~AL_PIEC_LOW; }
    else if (lowArmed && p.temp <= cfg.lowPiec) { a |= AL_PIEC_LOW; lowArmed = false; }
  }

  hyst(AL_PIEC_SENSOR, p.errors >= SENSOR_FAULT_COUNT, p.ok);
  hyst(AL_BOILER_SENSOR, b.errors >= SENSOR_FAULT_COUNT, b.ok);

  uint8_t fresh = a & ~st.alarms;
  if (fresh) {
    st.today.alarms += __builtin_popcount(fresh);
    st.acked &= ~fresh;
    Serial.printf("[alarm] nowe: 0x%02X\n", fresh);
  }
  st.acked &= a;   // skasowane alarmy nie sa juz potwierdzone
  st.alarms = a;
}

void alarmText(uint8_t bit, const CtrlState &s, const Settings &cfg,
               char *line1, char *line2, size_t n) {
  switch (bit) {
    case AL_PIEC_HIGH:
      snprintf(line1, n, "Piec: %.1f°C", s.s[PIEC].temp);
      snprintf(line2, n, "limit %.0f°C", cfg.alarmPiec);
      break;
    case AL_BOILER_HIGH:
      snprintf(line1, n, "Bojler: %.1f°C", s.s[BOILER].temp);
      snprintf(line2, n, "limit %.0f°C", cfg.alarmBoiler);
      break;
    case AL_PIEC_LOW:
      snprintf(line1, n, "Piec wygasa: %.1f°C", s.s[PIEC].temp);
      snprintf(line2, n, "próg %.0f°C", cfg.lowPiec);
      break;
    case AL_PIEC_SENSOR:
      snprintf(line1, n, "Czujnik pieca");
      snprintf(line2, n, s.s[PIEC].found ? "brak odczytu" : "menu > Czujniki");
      break;
    case AL_BOILER_SENSOR:
      snprintf(line1, n, "Czujnik bojlera");
      snprintf(line2, n, s.s[BOILER].found ? "brak odczytu" : "menu > Czujniki");
      break;
    default:
      snprintf(line1, n, "Alarm");
      line2[0] = 0;
  }
}

// ---------------------------------------------------------------------------
// Pompy
// ---------------------------------------------------------------------------
static void evaluatePumps(const Settings &cfg, float dt) {
  const SensorState &p = st.s[PIEC], &b = st.s[BOILER];
  bool pFault = p.errors >= SENSOR_FAULT_COUNT;
  bool bFault = b.errors >= SENSOR_FAULT_COUNT;

  // --- Pompa CO ---
  PumpState &co = st.pump[PUMP_CO];
  bool on = co.on;
  const char *why = co.reason;
  if (!cfg.coEnabled)                   { on = false; why = R_DISABLED; }
  else if (pFault)                      { on = true;  why = R_SENSOR; }   // bezpieczniej krecic woda
  else if (!p.ok)                       { /* pojedynczy blad - bez zmian */ }
  else if (st.alarms & AL_PIEC_HIGH)    { on = true;  why = R_OVERHEAT; }
  else if (cfg.antiFreeze > 0 &&
           (p.temp <= cfg.antiFreeze ||
            (co.on && co.reason == R_FREEZE && p.temp < cfg.antiFreeze + 2)))
                                        { on = true;  why = R_FREEZE; }
  else if (p.temp >= cfg.coOn)          { on = true;  why = R_HEATING; }
  else if (p.temp < cfg.coOn - cfg.coHyst) { on = false; why = R_COLD; }
  if (on && !co.on) st.today.pumpStarts[PUMP_CO]++;
  co.on = on;
  co.reason = why;

  // --- Pompa CWU: nigdy nie laduj bojlera zimniejszym piecem ---
  PumpState &cwu = st.pump[PUMP_CWU];
  on = cwu.on;
  why = cwu.reason;
  if (!cfg.cwuEnabled)                  { on = false; why = R_DISABLED; }
  else if (pFault || bFault)            { on = false; why = R_NODATA; }
  else if (p.ok && b.ok) {
    float diff = p.temp - b.temp;
    if (diff < 2)                       { on = false; why = R_CWU_COLD; }
    else if ((st.alarms & AL_PIEC_HIGH) && b.temp < cfg.alarmBoiler - cfg.alarmHyst)
                                        { on = true;  why = R_DUMP; }
    else if (b.temp >= cfg.cwuTarget)   { on = false; why = R_CWU_DONE; }
    else if (diff >= cfg.cwuDelta && b.temp <= cfg.cwuTarget - cfg.cwuHyst)
                                        { on = true;  why = R_CWU_HEAT; }
    else if (!on)                       { why = diff < cfg.cwuDelta ? R_CWU_COLD : R_CWU_DONE; }
  }
  if (on && !cwu.on) st.today.pumpStarts[PUMP_CWU]++;
  cwu.on = on;
  cwu.reason = why;

  for (int i = 0; i < 2; i++)
    if (st.pump[i].on) st.today.pumpOnSec[i] += dt;
}

static void writeOutputs() {
  const int pins[2] = {PIN_PUMP_CO, PIN_PUMP_CWU};
  for (int i = 0; i < 2; i++) {
    if (pins[i] < 0) continue;
    bool on = st.pump[i].on;
    digitalWrite(pins[i], on ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
  }
}

// ---------------------------------------------------------------------------
// Zadanie sterujace
// ---------------------------------------------------------------------------
static void updateSensor(SensorState &s, float v) {
  s.raw = v;
  if (v == DEVICE_DISCONNECTED_C || v < -55 || v > 125) {
    s.ok = false;
    if (s.errors < 255) s.errors++;
  } else {
    s.ok = true;
    s.errors = 0;
    s.temp = v;
  }
}

void formatAddress(const uint8_t a[8], char *out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
           a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

// Odswieza liste czujnikow na magistrali i sprawdza, czy przypisane sa obecne
static void scanBus() {
  sensors.begin();
  sensors.setResolution(DS18B20_RESOLUTION);
  sensors.setWaitForConversion(false);
  uint8_t count = sensors.getDeviceCount();

  LockGuard g;
  st.devicesOnBus = count;
  st.busCount = 0;
  DeviceAddress a;
  for (uint8_t i = 0; i < count && st.busCount < BUS_MAX; i++) {
    if (!sensors.getAddress(a, i)) continue;
    BusDevice &d = st.bus[st.busCount++];
    memcpy(d.addr, a, 8);
    d.temp = DEVICE_DISCONNECTED_C;
  }
  for (int s = 0; s < 2; s++) {
    st.s[s].found = false;
    for (int i = 0; i < st.busCount; i++)
      if (!memcmp(st.bus[i].addr, st.addr[s], 8)) st.s[s].found = true;
  }

  char buf[32];
  Serial.printf("[czujniki] magistrala GPIO%d: %u szt.\n", devcfg().pinOneWire, count);
  for (int i = 0; i < st.busCount; i++) {
    formatAddress(st.bus[i].addr, buf, sizeof(buf));
    const char *role = !memcmp(st.bus[i].addr, st.addr[PIEC], 8)     ? " = piec"
                       : !memcmp(st.bus[i].addr, st.addr[BOILER], 8) ? " = bojler"
                                                                     : " (nieprzypisany)";
    Serial.printf("  #%d %s%s\n", i + 1, buf, role);
  }
  Serial.printf("[czujniki] piec: %s, bojler: %s\n", st.s[PIEC].found ? "OK" : "BRAK",
                st.s[BOILER].found ? "OK" : "BRAK");
}

void controllerAssignSensor(uint8_t sensor, const uint8_t addr[8]) {
  {
    LockGuard g;
    // ten sam czujnik nie moze byc jednoczesnie piecem i bojlerem
    uint8_t other = sensor == PIEC ? BOILER : PIEC;
    if (!memcmp(st.addr[other], addr, 8)) memset(st.addr[other], 0, 8);
    memcpy(st.addr[sensor], addr, 8);
    for (auto &s : st.s) { s.errors = 0; s.ok = false; }
  }
  Preferences prefs;
  if (prefs.begin(NVS_SENSORS, false)) {
    CtrlState snap = controllerSnapshot();
    for (int i = 0; i < 2; i++) prefs.putBytes(NVS_KEYS[i], snap.addr[i], 8);
    prefs.end();
  }
  addrChanged = true;
  char buf[32];
  formatAddress(addr, buf, sizeof(buf));
  Serial.printf("[czujniki] %s -> %s\n", buf, sensor == PIEC ? "piec" : "bojler");
}

static void controlTask(void *) {
  // USB CDC pojawia sie ok. 1 s po starcie - poczekaj chwile, zeby wynik skanu byl widoczny
  while (!Serial && millis() < 3000) vTaskDelay(pdMS_TO_TICKS(50));
  oneWire.begin(devcfg().pinOneWire);
  scanBus();
  uint32_t lastScan = millis();

  const uint32_t convMs = sensors.millisToWaitForConversion(DS18B20_RESOLUTION) + 10;
  uint32_t lastMs = millis();
  uint32_t nextHist = millis() + HISTORY_INTERVAL_MS;
  uint32_t nextTrend = 0;
  bool trendStarted = false;

  for (;;) {
    uint32_t cycleStart = millis();
    // brakujacy czujnik: przeskanuj magistrale co 30 s (podlaczenie bez restartu)
    bool missing;
    {
      LockGuard g;
      missing = !st.s[PIEC].found || !st.s[BOILER].found;
    }
    if (addrChanged || (missing && millis() - lastScan > 30000)) {
      addrChanged = false;
      scanBus();
      lastScan = millis();
    }

    uint8_t addr[2][8];
    BusDevice bus[BUS_MAX];
    uint8_t busCount;
    {
      LockGuard g;
      memcpy(addr, st.addr, sizeof(addr));
      memcpy(bus, st.bus, sizeof(bus));
      busCount = st.busCount;
    }
    sensors.requestTemperatures();
    vTaskDelay(pdMS_TO_TICKS(convMs));
    float r[2] = {sensors.getTempC(addr[PIEC]), sensors.getTempC(addr[BOILER])};
    for (int i = 0; i < busCount; i++) bus[i].temp = sensors.getTempC(bus[i].addr);

    Settings cfg = settingsGet();
    uint32_t now = millis();
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;
    {
      LockGuard g;
      for (int i = 0; i < 2; i++) updateSensor(st.s[i], r[i]);
      for (int i = 0; i < busCount && i < st.busCount; i++)
        if (!memcmp(st.bus[i].addr, bus[i].addr, 8)) st.bus[i].temp = bus[i].temp;
      st.readings++;
      checkDayRollover();

      for (int i = 0; i < 2; i++) {
        DayStats &d = st.today.s[i];
        if (!st.s[i].ok) continue;
        float t = st.s[i].temp;
        if (isnan(d.min) || t < d.min) d.min = t;
        if (isnan(d.max) || t > d.max) d.max = t;
      }

      // Trend jak w starym sterowniku: probka co trendMin (domyslnie 5 min),
      // pierwsza zaraz po pierwszym poprawnym odczycie
      if (!trendStarted && (st.s[PIEC].ok || st.s[BOILER].ok)) {
        trendStarted = true;
        nextTrend = now;
      }
      if (trendStarted && (int32_t)(now - nextTrend) >= 0) {
        for (int i = 0; i < 2; i++)
          if (st.s[i].ok) trendSample(st.s[i], st.s[i].temp);
        nextTrend = now + cfg.trendMin * 60000UL;
      }

      if ((int32_t)(now - nextHist) >= 0) {
        nextHist += HISTORY_INTERVAL_MS;
        pushHistory();
        const float limits[2] = {cfg.alarmPiec, cfg.alarmBoiler};
        for (int i = 0; i < 2; i++) {
          if (!st.s[i].ok) continue;
          DayStats &d = st.today.s[i];
          d.sum += st.s[i].temp;
          d.samples++;
          if (st.s[i].temp >= limits[i]) d.minutesAbove++;
        }
      }

      evaluateAlarms(cfg);
      evaluatePumps(cfg, dt);
      writeOutputs();
    }

    int32_t rest = SENSOR_READ_INTERVAL_MS - (int32_t)(millis() - cycleStart);
    if (rest > 0) vTaskDelay(pdMS_TO_TICKS(rest));
  }
}

void controllerBegin() {
  {
    LockGuard g;
    memset(&st, 0, sizeof(st));
    for (auto &s : st.s) {
      s.raw = DEVICE_DISCONNECTED_C;
      s.temp = NAN;
      s.trend = TREND_NONE;
    }
    for (auto &p : st.pump) p.reason = R_START;
    memcpy(st.addr[PIEC], SENSOR_ADDR_PIEC, 8);      // domyslnie adresy ze starego sterownika
    memcpy(st.addr[BOILER], SENSOR_ADDR_BOILER, 8);
    Preferences prefs;
    if (prefs.begin(NVS_SENSORS, true)) {
      for (int i = 0; i < 2; i++)
        if (prefs.getBytesLength(NVS_KEYS[i]) == 8) prefs.getBytes(NVS_KEYS[i], st.addr[i], 8);
      prefs.end();
    }
    st.pump[PUMP_CO].hasOutput = PIN_PUMP_CO >= 0;
    st.pump[PUMP_CWU].hasOutput = PIN_PUMP_CWU >= 0;
    resetDay(st.today);
    resetDay(st.yesterday);
    for (auto &h : hist) for (auto &v : h) v = HIST_INVALID;
  }
  const int pins[2] = {PIN_PUMP_CO, PIN_PUMP_CWU};
  for (int pin : pins) {
    if (pin < 0) continue;
    digitalWrite(pin, !RELAY_ACTIVE_LEVEL);
    pinMode(pin, OUTPUT);
  }
  xTaskCreatePinnedToCore(controlTask, "sterowanie", 6144, nullptr, 3, nullptr, CORE_CONTROL);
}

CtrlState controllerSnapshot() {
  LockGuard g;
  return st;
}

void controllerAck() {
  LockGuard g;
  st.acked = st.alarms;
}

void controllerRescan() { addrChanged = true; }

void controllerResetStats() {
  LockGuard g;
  resetDay(st.today);
  resetDay(st.yesterday);
  st.haveYesterday = false;
}
