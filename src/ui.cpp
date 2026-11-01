#include "ui.h"
#include "config.h"
#include "controller.h"
#include "net.h"
#include "settings.h"
#include "devcfg.h"
#include "input.h"
#include "shared.h"
#include <SPI.h>
#include <U8g2lib.h>
#include <time.h>

// Sprzetowe SPI (dowolne piny przez matryce GPIO) - programowe SPI zajmowalo procesor
// przy kazdym bicie. Piny z config.h sa tylko domyslne - uiBegin() ustawia te z /config.
static U8G2_ST7565_ERC12864_ALT_F_4W_HW_SPI u8g2(U8G2_R0, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

// Czcionki: _te maja polskie znaki, _tf maja znak stopnia
#define F_TITLE u8g2_font_helvB08_te
#define F_LABEL u8g2_font_helvR08_te
#define F_TEXT  u8g2_font_6x12_te
#define F_MED   u8g2_font_6x10_tf
#define F_TINY  u8g2_font_5x7_tf
#define F_SMALL u8g2_font_4x6_tr
#define F_HUGE  u8g2_font_logisoso16_tr

const uint32_t SLIDE_MS = 400;
const uint32_t MANUAL_HOLD_MS = 60000;   // po nacisnieciu BOOT ekrany stoja 1 min
const uint32_t RETURN_MAIN_MS = 120000;  // other screens return to the main one after 2 min

struct Ctx {
  CtrlState st;
  Settings cfg;
  NetStatus net;
  uint32_t now;
  uint8_t unacked;
};

// ---------------------------------------------------------------------------
// Ikony ('#' = piksel), szerokosc = dlugosc wiersza
// ---------------------------------------------------------------------------
typedef const char *const Icon[8];

Icon ICON_STOVE = {   // jak znak "piec" ze starego LCD
  "#######", "#.....#", "#.....#", "#######", "#######", "#.....#", "#######", "#.....#",
};
Icon ICON_BOILER = {
  "..###..", ".#...#.", "#.....#", "#..#..#", "#.###.#", "#.###.#", "#.....#", ".#####.",
};
Icon ICON_UP = {
  "...####", ".....##", "....#.#", "...#..#", "..#....", ".#.....", "#......", ".......",
};
Icon ICON_DOWN = {
  "#......", ".#.....", "..#....", "...#..#", "....#.#", ".....##", "...####", ".......",
};
Icon ICON_RIGHT = {
  ".......", "....#..", ".....#.", "#######", ".....#.", "....#..", ".......", ".......",
};
Icon ICON_CLOCK = {
  "..###..", ".#.#.#.", "#..#..#", "#..##.#", "#.....#", ".#...#.", "..###..", ".......",
};
Icon ICON_PUMP = {
  "..###..", ".#...#.", "#.#...#", "#.##..#", "#.#...#", ".#...#.", "..###..", ".......",
};
Icon ICON_BELL = {
  "...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", ".......", "...#...",
};
Icon ICON_CHECK = {   // "ptaszek" zamiast napisu OK
  "......#", ".....##", "#...##.", "##.##..", ".###...", "..#....", ".......", ".......",
};

static void drawIcon(int x, int y, const char *const *icon) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; icon[r][c]; c++)
      if (icon[r][c] == '#') u8g2.drawPixel(x + c, y + r);
}

// ---------------------------------------------------------------------------
// Pomocnicze
// ---------------------------------------------------------------------------
static float easeInOut(float p) { return p < 0.5f ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p); }

static void textRight(int xRight, int y, const char *s) {
  u8g2.drawUTF8(xRight - u8g2.getUTF8Width(s), y, s);
}
static void textCenter(int xCenter, int y, const char *s) {
  u8g2.drawUTF8(xCenter - u8g2.getUTF8Width(s) / 2, y, s);
}

static bool blink(uint32_t periodMs) { return (millis() / periodMs) % 2 == 0; }

static void fmtMinutes(char *buf, size_t n, uint32_t minutes) {
  if (minutes < 60) snprintf(buf, n, "%lu min", (unsigned long)minutes);
  else snprintf(buf, n, "%luh%02lu", (unsigned long)(minutes / 60), (unsigned long)(minutes % 60));
}

static void fmtTemp(char *buf, size_t n, float v) {
  if (isnan(v)) snprintf(buf, n, "--");
  else snprintf(buf, n, "%.1f°", v);
}

static bool hasPumps() { return (PIN_PUMP_CO >= 0 || PIN_PUMP_CWU >= 0) && settingsGet().pumps; }
// "Pompa CO" / "Pompa CWU" settings groups - hidden while the pump section is off
static bool isPumpGroup(const char *g) { return !strncmp(g, "Pompa ", 6); }

// ---------------------------------------------------------------------------
// Naglowek: tytul, dzwonek alarmu, Wi-Fi, zegar
// ---------------------------------------------------------------------------
static void drawWifi(int x, const NetStatus &n) {
  if (!n.wifiConfigured) return;
  if (!n.wifi) {   // krzyzyk
    u8g2.drawLine(x + 1, 3, x + 6, 8);
    u8g2.drawLine(x + 6, 3, x + 1, 8);
    return;
  }
  int bars = n.rssi > -55 ? 4 : n.rssi > -65 ? 3 : n.rssi > -75 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    if (i < bars) u8g2.drawBox(x + i * 2, 10 - h - 1, 1, h);
    else u8g2.drawPixel(x + i * 2, 8);
  }
}

static void drawHeader(const Ctx &c, const char *title) {
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 11);
  u8g2.setDrawColor(0);
  u8g2.setFont(F_TITLE);
  u8g2.drawUTF8(2, 9, title);

  int x = 127;
  if (c.net.timeValid) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", &lt);
    u8g2.setFont(F_TINY);
    x -= u8g2.getStrWidth(buf);
    u8g2.drawStr(x, 8, buf);
    x -= 4;
  }
  if (c.net.wifiConfigured) {
    x -= 7;
    drawWifi(x, c.net);
    x -= 3;
  }
  if (c.st.alarms && (c.unacked == 0 || blink(500))) {
    x -= 7;
    drawIcon(x, 2, ICON_BELL);
  }
  u8g2.setDrawColor(1);
}

// ---------------------------------------------------------------------------
// Ekran 1: glowny - piec i bojler (jak stary LCD: temp, trend, delta, czas)
// ---------------------------------------------------------------------------
static void drawSensorRow(const Ctx &c, uint8_t id, int y0) {
  const SensorState &s = c.st.s[id];
  const float limit = id == PIEC ? c.cfg.alarmPiec : c.cfg.alarmBoiler;
  const uint8_t highBit = id == PIEC ? AL_PIEC_HIGH : AL_BOILER_HIGH;

  // lewa kolumna: ikona, nazwa, pompa
  drawIcon(7, y0 + 2, id == PIEC ? ICON_STOVE : ICON_BOILER);
  u8g2.setFont(F_SMALL);
  const char *name = id == PIEC ? "PIEC" : "BOJLER";
  u8g2.drawStr(max(0, 11 - (int)u8g2.getStrWidth(name) / 2), y0 + 16, name);
  const PumpState &p = c.st.pump[id == PIEC ? PUMP_CO : PUMP_CWU];
  if (p.hasOutput && p.on) {
    const char *tag = id == PIEC ? "CO" : "CWU";
    int w = u8g2.getStrWidth(tag) + 4;
    int x = 11 - w / 2;
    u8g2.drawBox(x, y0 + 18, w, 7);
    u8g2.setDrawColor(0);
    u8g2.drawStr(x + 2, y0 + 24, tag);
    u8g2.setDrawColor(1);
  }

  // temperatura
  char buf[16];
  if (s.ok) {
    snprintf(buf, sizeof(buf), "%.1f", s.temp);
    u8g2.setFont(F_HUGE);
    u8g2.drawStr(25, y0 + 19, buf);
    int w = u8g2.getStrWidth(buf);
    u8g2.setFont(F_TINY);
    u8g2.drawUTF8(25 + w + 2, y0 + 9, "°C");
    // pasek: jak blisko temperatury granicznej
    float frac = constrain(s.temp / limit, 0.0f, 1.0f);
    u8g2.drawHLine(25, y0 + 23, 50);
    u8g2.drawBox(25, y0 + 22, (int)(50 * frac), 2);
  } else {
    u8g2.setFont(F_TITLE);
    u8g2.drawUTF8(25, y0 + 12, "BŁĄD");
    u8g2.setFont(F_SMALL);
    u8g2.drawStr(25, y0 + 20, s.found ? "BRAK ODCZYTU" : "BRAK CZUJNIKA");
  }
  if ((c.st.alarms & highBit) && blink(500)) {
    u8g2.setDrawColor(2);
    u8g2.drawBox(23, y0 + 1, 55, 20);
    u8g2.setDrawColor(1);
  }

  // prawa kolumna: trend jak w starym sterowniku
  if (s.trend == TREND_NONE) {
    u8g2.setFont(F_SMALL);
    u8g2.drawStr(82, y0 + 9, "TREND:");
    u8g2.drawStr(82, y0 + 17, "ZBIERAM");
    return;
  }
  drawIcon(80, y0 + 2, s.trend == TREND_UP ? ICON_UP : s.trend == TREND_DOWN ? ICON_DOWN : ICON_RIGHT);
  u8g2.setFont(F_MED);
  snprintf(buf, sizeof(buf), "%.1f°", s.trendDiff);
  u8g2.drawUTF8(90, y0 + 9, buf);
  drawIcon(80, y0 + 13, ICON_CLOCK);
  u8g2.drawStr(90, y0 + 20, trendTimeLabel(s.trendStartMs).c_str());
}

static void scrMain(const Ctx &c) {
  drawHeader(c, "Piec i bojler");
  drawSensorRow(c, PIEC, 12);
  for (int x = 0; x < 128; x += 2) u8g2.drawPixel(x, 37);
  drawSensorRow(c, BOILER, 38);
}

// ---------------------------------------------------------------------------
// Ekrany 2-3: wykresy z historii (os czasu: "teraz" po prawej)
// ---------------------------------------------------------------------------
static int16_t histBuf[HISTORY_LEN];

static void drawChart(const Ctx &c, uint8_t id) {
  char title[24];
  snprintf(title, sizeof(title), "%s  %u h", id == PIEC ? "Piec" : "Bojler", c.cfg.chartHours);
  drawHeader(c, title);

  // bar chart: BAR_W px wide bars with a 1 px gap, filling the inside of the frame exactly
  const int X0 = 15, Y0 = 12, W = 113, H = 51, PW = W - 2;
  const int BAR_W = 3, STEP = BAR_W + 1, NB = (PW + 1) / STEP;
  const float limit = id == PIEC ? c.cfg.alarmPiec : c.cfg.alarmBoiler;
  int n = controllerHistory(id, histBuf, HISTORY_LEN);
  int span = c.cfg.chartHours * 60;

  // average the samples into bars
  static float val[NB];
  bool any = false;
  float lo = 1e9, hi = -1e9;
  for (int px = 0; px < NB; px++) {
    int from = n - span + px * span / NB;
    int to = n - span + (px + 1) * span / NB;
    if (to <= from) to = from + 1;
    float sum = 0;
    int cnt = 0;
    for (int i = max(from, 0); i < min(to, n); i++)
      if (histBuf[i] != HIST_INVALID) { sum += histBuf[i] / 10.0f; cnt++; }
    val[px] = cnt ? sum / cnt : NAN;
    if (cnt) { any = true; lo = min(lo, val[px]); hi = max(hi, val[px]); }
  }

  if (!any) {
    // no scale on the left yet - centre the frame on the screen
    const int WE = 114, XE = (128 - WE) / 2;   // even width: 7 px margin on both sides
    u8g2.drawFrame(XE, Y0, WE, H);
    u8g2.setFont(F_LABEL);
    textCenter(64, Y0 + 24, "Zbieranie danych");
    u8g2.setFont(F_SMALL);
    textCenter(64, Y0 + 34, "PROBKA CO 1 MIN");
    return;
  }
  u8g2.drawFrame(X0, Y0, W, H);
  lo = min(lo, limit);
  hi = max(hi, limit);
  lo = floorf((lo - 2) / 5) * 5;
  hi = ceilf((hi + 2) / 5) * 5;
  if (hi - lo < 10) hi = lo + 10;
  auto yOf = [&](float v) { return Y0 + H - 2 - (int)((v - lo) / (hi - lo) * (H - 3) + 0.5f); };

  // skala
  u8g2.setFont(F_SMALL);
  const float marks[3] = {hi, (hi + lo) / 2, lo};
  for (float m : marks) {
    char b[6];
    snprintf(b, sizeof(b), "%.0f", m);
    int y = yOf(m);
    textRight(X0 - 2, y + 3, b);
    if (m != hi && m != lo)
      for (int x = X0 + 2; x < X0 + W - 1; x += 4) u8g2.drawPixel(x, y);
  }
  // bars from the bottom of the frame up to the value
  const int yMax = Y0 + H - 2;
  for (int b = 0; b < NB; b++) {
    if (isnan(val[b])) continue;
    int y = constrain(yOf(val[b]), Y0 + 1, yMax);
    u8g2.drawBox(X0 + 1 + b * STEP, y, BAR_W, yMax - y + 1);
  }

  // alarm limit - dashed line, XOR so it stays visible across the bars
  int yl = yOf(limit);
  u8g2.setDrawColor(2);
  for (int x = X0 + 1; x < X0 + W - 1; x++)
    if ((x / 3) % 2 == 0) u8g2.drawPixel(x, yl);
  u8g2.setDrawColor(1);

  // biezaca wartosc w ramce
  const SensorState &s = c.st.s[id];
  char b[12];
  if (s.ok) snprintf(b, sizeof(b), "%.1f°C", s.temp);
  else snprintf(b, sizeof(b), "--.-");
  u8g2.setFont(F_TINY);
  int w = u8g2.getUTF8Width(b) + 4;
  u8g2.setDrawColor(0);
  u8g2.drawBox(X0 + 2, Y0 + 2, w + 2, 11);
  u8g2.setDrawColor(1);
  u8g2.drawRBox(X0 + 3, Y0 + 3, w, 9, 2);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(X0 + 5, Y0 + 10, b);
  u8g2.setDrawColor(1);

  // time span label, on a cleared background so the bars do not run through it
  u8g2.setFont(F_SMALL);
  snprintf(b, sizeof(b), "-%uh", c.cfg.chartHours);
  u8g2.setDrawColor(0);
  u8g2.drawBox(X0 + 1, Y0 + H - 10, u8g2.getStrWidth(b) + 3, 9);
  u8g2.setDrawColor(1);
  u8g2.drawStr(X0 + 3, Y0 + H - 3, b);
}

static void scrChartPiec(const Ctx &c) { drawChart(c, PIEC); }
static void scrChartBoiler(const Ctx &c) { drawChart(c, BOILER); }

// ---------------------------------------------------------------------------
// Ekran 4: statystyki dnia
// ---------------------------------------------------------------------------
static void scrStats(const Ctx &c) {
  drawHeader(c, "Statystyki dziś");
  const int COL1 = 88, COL2 = 126;
  u8g2.setFont(F_TITLE);
  textRight(COL1, 21, "Piec");
  textRight(COL2, 21, "Bojler");
  u8g2.drawHLine(0, 23, 128);

  const char *labels[4] = {"minimum", "maksimum", "średnia", "> limitu"};
  for (int r = 0; r < 4; r++) {
    int y = 32 + r * 10;
    u8g2.setFont(F_LABEL);
    u8g2.drawUTF8(1, y, labels[r]);
    u8g2.setFont(F_TINY);
    for (int i = 0; i < 2; i++) {
      const DayStats &d = c.st.today.s[i];
      char b[12];
      switch (r) {
        case 0: fmtTemp(b, sizeof(b), d.min); break;
        case 1: fmtTemp(b, sizeof(b), d.max); break;
        case 2: fmtTemp(b, sizeof(b), d.samples ? d.sum / d.samples : NAN); break;
        default: fmtMinutes(b, sizeof(b), d.minutesAbove); break;
      }
      textRight(i == 0 ? COL1 : COL2, y, b);
    }
  }
}

// ---------------------------------------------------------------------------
// Ekran 5: pompy (tylko gdy skonfigurowano wyjscie)
// ---------------------------------------------------------------------------
static void drawPump(const Ctx &c, uint8_t id, int y0) {
  const PumpState &p = c.st.pump[id];
  drawIcon(1, y0 + 2, ICON_PUMP);
  u8g2.setFont(F_TITLE);
  u8g2.drawUTF8(11, y0 + 9, id == PUMP_CO ? "Pompa CO" : "Pompa CWU");

  const char *state = !p.hasOutput ? "---" : p.on ? "ZAŁ" : "WYŁ";
  int w = u8g2.getUTF8Width(state) + 8;
  int x = 127 - w;
  if (p.hasOutput && p.on) {
    u8g2.drawRBox(x, y0, w, 11, 2);
    u8g2.setDrawColor(0);
  } else {
    u8g2.drawRFrame(x, y0, w, 11, 2);
  }
  u8g2.drawUTF8(x + 4, y0 + 9, state);
  u8g2.setDrawColor(1);

  u8g2.setFont(F_LABEL);
  u8g2.drawUTF8(11, y0 + 20, p.hasOutput ? p.reason : "brak wyjścia");
  char b[16];
  fmtMinutes(b, sizeof(b), (uint32_t)(c.st.today.pumpOnSec[id] / 60));
  u8g2.setFont(F_TINY);
  textRight(127, y0 + 20, b);
}

static void scrPumps(const Ctx &c) {
  drawHeader(c, "Pompy");
  drawPump(c, PUMP_CO, 13);
  for (int x = 0; x < 128; x += 2) u8g2.drawPixel(x, 37);
  drawPump(c, PUMP_CWU, 39);
}

// ---------------------------------------------------------------------------
// Wiersz "etykieta ... wartosc" (ekran Siec i system)
// polish = wartosc z polskimi znakami (F_TINY ich nie ma)
static void settingsRow(int y, const char *label, const char *value, bool polish = false) {
  u8g2.setFont(F_LABEL);
  u8g2.drawUTF8(1, y, label);
  u8g2.setFont(polish ? F_LABEL : F_TINY);
  textRight(127, y, value);
}

// ---------------------------------------------------------------------------
// Ekran 7: siec i system
// ---------------------------------------------------------------------------
static void scrSystem(const Ctx &c) {
  drawHeader(c, "Sieć i system");
  char b[32];
  const NetStatus &n = c.net;

  if (!n.wifiConfigured) snprintf(b, sizeof(b), "brak w .env");
  else if (n.wifi) snprintf(b, sizeof(b), "%d dBm", n.rssi);
  else snprintf(b, sizeof(b), "rozłączone");
  settingsRow(21, "Wi-Fi", b, true);

  snprintf(b, sizeof(b), "%s", n.wifi ? n.ip.toString().c_str() : "--");
  settingsRow(31, "IP", b);

  if (!n.fbConfigured) snprintf(b, sizeof(b), "wyłączony");
  else if (n.fbLastMs == 0 && n.fbError[0]) snprintf(b, sizeof(b), "błąd logow.");   // opis na /config
  else if (n.fbLastMs == 0) snprintf(b, sizeof(b), n.fbReady ? "czeka" : "łączenie");
  else if (n.fbLastOk) snprintf(b, sizeof(b), "%lu min", (unsigned long)((millis() - n.fbLastMs) / 60000));
  else snprintf(b, sizeof(b), "błąd %lu min", (unsigned long)((millis() - n.fbLastMs) / 60000));
  settingsRow(41, "Firebase", b, true);
  if (n.fbConfigured && n.fbLastMs && n.fbLastOk) {   // ptaszek przed czasem ostatniej wysylki
    u8g2.setFont(F_LABEL);
    drawIcon(127 - u8g2.getUTF8Width(b) - 10, 41 - 7, ICON_CHECK);
  }

  snprintf(b, sizeof(b), "%.0f°C", gChipTemp);   // liczba czujnikow jest w menu Czujniki
  settingsRow(51, "Temp. układu", b);

  uint32_t s = millis() / 1000;
  snprintf(b, sizeof(b), "%lud %02lu:%02lu", (unsigned long)(s / 86400),
           (unsigned long)(s % 86400 / 3600), (unsigned long)(s % 3600 / 60));
  settingsRow(61, "Czas pracy", b);
}

// ---------------------------------------------------------------------------
// Nakladki: alarm, komunikat, OTA
// ---------------------------------------------------------------------------
static char toastMsg[32];
static uint32_t toastUntil = 0;

static void toast(const char *msg) {
  strlcpy(toastMsg, msg, sizeof(toastMsg));
  toastUntil = millis() + 1800;
}

static void drawToast() {
  if ((int32_t)(millis() - toastUntil) >= 0) return;
  u8g2.setFont(F_LABEL);
  int w = u8g2.getUTF8Width(toastMsg) + 10;
  int x = (128 - w) / 2;
  u8g2.setDrawColor(0);
  u8g2.drawBox(x - 1, 47, w + 2, 16);
  u8g2.setDrawColor(1);
  u8g2.drawRBox(x, 48, w, 14, 3);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(x + 5, 58, toastMsg);
  u8g2.setDrawColor(1);
}

static void drawAlarmOverlay(const Ctx &c) {
  uint8_t bits[8];
  int count = 0;
  for (uint8_t bit = 1; bit; bit <<= 1)
    if (c.unacked & bit) bits[count++] = bit;
  if (!count) return;
  uint8_t bit = bits[(millis() / 2500) % count];
  char l1[40], l2[40];
  alarmText(bit, c.st, c.cfg, l1, l2, sizeof(l1));

  const int X = 4, Y = 14, W = 120, H = 48;
  u8g2.drawBox(X + 2, Y + H, W, 2);   // cien
  u8g2.drawBox(X + W, Y + 2, 2, H);
  u8g2.setDrawColor(0);
  u8g2.drawBox(X, Y, W, H);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(X, Y, W, H);
  u8g2.drawFrame(X + 1, Y + 1, W - 2, H - 2);

  bool inv = blink(500);
  if (inv) u8g2.drawBox(X + 2, Y + 2, W - 4, 12);
  u8g2.setDrawColor(inv ? 0 : 1);
  u8g2.setFont(F_TITLE);
  textCenter(64, Y + 11, count > 1 ? "ALARM!  (więcej)" : "ALARM!");
  u8g2.setDrawColor(1);

  u8g2.setFont(F_LABEL);
  textCenter(64, Y + 25, l1);
  textCenter(64, Y + 35, l2);
  u8g2.setFont(F_SMALL);
  textCenter(64, Y + 44, "PRZYTRZYMAJ BOOT = CISZA");
}

static void drawOta(int progress) {
  u8g2.clearBuffer();
  u8g2.setFont(F_TITLE);
  textCenter(64, 20, "Aktualizacja");
  u8g2.setFont(F_LABEL);
  textCenter(64, 32, "nie wyłączaj zasilania");
  u8g2.drawFrame(8, 40, 112, 10);
  u8g2.drawBox(10, 42, 108 * progress / 100, 6);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", progress);
  u8g2.setFont(F_TINY);
  textCenter(64, 60, b);
  u8g2.sendBuffer();
}

static void drawRestart() {
  u8g2.clearBuffer();
  drawIcon(60, 14, ICON_CLOCK);
  u8g2.setFont(F_TITLE);
  textCenter(64, 38, "Ponowne");
  textCenter(64, 50, "uruchamianie…");
  u8g2.sendBuffer();
}

// ---------------------------------------------------------------------------
// Ekran sieci konfiguracyjnej (punkt dostepu)
// ---------------------------------------------------------------------------
static bool uiApActive = false;

static void scrAp(const Ctx &c) {
  drawHeader(c, "Konfiguracja Wi-Fi");
  u8g2.setFont(F_LABEL);
  u8g2.drawUTF8(1, 21, "1. Połącz telefon z siecią:");
  u8g2.setFont(F_TITLE);
  u8g2.drawUTF8(9, 31, c.net.apSsid);
  u8g2.setFont(F_LABEL);
  u8g2.drawUTF8(9, 41, "hasło: " AP_PASSWORD);
  u8g2.drawUTF8(1, 51, "2. Otwórz 192.168.4.1");
  char b[24];
  if (c.net.apClients) snprintf(b, sizeof(b), "połączonych: %u", c.net.apClients);
  else snprintf(b, sizeof(b), "czekam na telefon…");
  u8g2.drawUTF8(9, 61, b);
}

// ---------------------------------------------------------------------------
// Lista ekranow i przelaczanie
// ---------------------------------------------------------------------------
struct Screen {
  void (*draw)(const Ctx &c);
  bool (*visible)();
};

// "Siec i system" jest w petli tylko przy problemie (uiNetProblem) albo po wybraniu
// Ustawienia -> Informacje (uiSystemForcedUntil). Ustawienia sa pod kliknieciem enkodera.
static bool uiNetProblem = false;
static uint32_t uiSystemForcedUntil = 0;

static bool always() { return true; }
static bool apVisible() { return uiApActive; }
static bool systemVisible() {
  return uiNetProblem || (int32_t)(uiSystemForcedUntil - millis()) > 0;
}

static const Screen SCREENS[] = {
  {scrMain, always},
  {scrChartPiec, always},
  {scrChartBoiler, always},
  {scrStats, always},
  {scrPumps, hasPumps},
  {scrSystem, systemVisible},
  {scrAp, apVisible},
};
static const int SCREEN_SYSTEM = 5;
static const int SCREEN_AP = 6;

// Problem z siecia lub systemem - wtedy "Siec i system" dolacza do petli ekranow
static bool netProblem(const Ctx &c) {
  bool wifi = !c.net.wifiConfigured || (!c.net.wifi && millis() > 90000);   // 1.5 min na polaczenie po starcie
  bool firebase = c.net.fbConfigured && (c.net.fbError[0] || (c.net.fbLastMs && !c.net.fbLastOk));
  bool hot = gChipTemp > 75;
  return wifi || firebase || hot;
}
static const int SCREEN_COUNT = sizeof(SCREENS) / sizeof(SCREENS[0]);

static int current = 0;
static uint32_t screenStart = 0;
static uint32_t holdUntil = 0;
static uint8_t imgOld[1024], imgNew[1024];

static bool overlayVisible(const Ctx &c) {
  return c.unacked && (millis() % 6000) < 4000;   // 4 s alarm, 2 s podglad danych
}

static void render(int idx, const Ctx &c) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  SCREENS[idx].draw(c);
  if (overlayVisible(c)) drawAlarmOverlay(c);
  drawToast();
}

static int nextVisible(int idx) {
  for (int i = 1; i <= SCREEN_COUNT; i++) {
    int n = (idx + i) % SCREEN_COUNT;
    if (SCREENS[n].visible()) return n;
  }
  return 0;
}

// Przesuwanie gotowych obrazow w buforze (8 stron po 128 bajtow).
// dir = 1: nowy obraz wjezdza z prawej, -1: z lewej.
static void slideBuffers(int dir) {
  uint8_t *buf = u8g2.getBufferPtr();
  const int pageW = u8g2.getBufferTileWidth() * 8;
  const int pages = u8g2.getBufferTileHeight();
  uint32_t s = millis();
  for (;;) {
    float p = min(1.0f, (millis() - s) / (float)SLIDE_MS);
    int off = (int)(easeInOut(p) * pageW + 0.5f);
    for (int pg = 0; pg < pages; pg++) {
      uint8_t *d = buf + pg * pageW;
      const uint8_t *o = imgOld + pg * pageW, *n = imgNew + pg * pageW;
      if (dir > 0) {
        memcpy(d, o + off, pageW - off);
        memcpy(d + pageW - off, n, off);
      } else {
        memcpy(d, n + pageW - off, off);
        memcpy(d + off, o, pageW - off);
      }
    }
    u8g2.sendBuffer();
    if (p >= 1) break;
  }
}

static size_t bufferSize() { return u8g2.getBufferTileWidth() * 8 * u8g2.getBufferTileHeight(); }

// Wysyla klatke tylko, gdy rozni sie od poprzedniej
static uint8_t lastFrame[1024];
static bool lastFrameValid = false;

static void present() {
  const uint8_t *buf = u8g2.getBufferPtr();
  if (lastFrameValid && !memcmp(buf, lastFrame, bufferSize())) return;
  memcpy(lastFrame, buf, bufferSize());
  lastFrameValid = true;
  u8g2.sendBuffer();
}

// Czy rysowac teraz klatke: plynnie (40 ms) przy obsludze enkodera, w spoczynku co 250 ms,
// przy alarmie co 100 ms (miganie). Przyciski i enkoder sa odpytywane czesciej niz rysowanie.
static uint32_t lastFrameMs = 0;
static uint32_t lastInputMsUi = 0;

static bool frameDue(const Ctx &c, bool input) {
  if (input) lastInputMsUi = c.now;
  bool lively = c.now - lastInputMsUi < 3000 || (int32_t)(toastUntil - c.now) > 0;
  uint32_t interval = lively ? 40 : (c.st.alarms ? 100 : 250);
  if (!input && c.now - lastFrameMs < interval) return false;
  lastFrameMs = c.now;
  return true;
}

static void slideTo(int next, const Ctx &c, int dir = 1) {
  render(current, c);
  memcpy(imgOld, u8g2.getBufferPtr(), bufferSize());
  render(next, c);
  memcpy(imgNew, u8g2.getBufferPtr(), bufferSize());
  slideBuffers(dir);
  current = next;
  screenStart = millis();
}

static int prevVisible(int idx) {
  for (int i = 1; i <= SCREEN_COUNT; i++) {
    int n = (idx - i + SCREEN_COUNT) % SCREEN_COUNT;
    if (SCREENS[n].visible()) return n;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Podswietlenie, kontrast, brzeczyk
// ---------------------------------------------------------------------------
static int lastBacklight = -1, lastContrast = -1;

// Brzeczyk: aktywny = zwykle wl/wyl, pasywny = ton PWM. Poziom zalaczenia z ustawien,
// bo moduly z tranzystorem PNP wlaczaja sie stanem niskim.
static volatile uint32_t buzzerTestUntil = 0;
static int8_t buzzerOn = -1, buzzerPassive = -1, buzzerLow = -1;

void uiBuzzerTest() { buzzerTestUntil = millis() + 1600; }

static void setBuzzer(bool on, const Settings &cfg) {
  const int pin = devcfg().pinBuzzer;
  if (pin < 0) return;
  if (on == buzzerOn && cfg.buzzerPassive == buzzerPassive && cfg.buzzerLow == buzzerLow) return;
  if (cfg.buzzerPassive != buzzerPassive) {   // zmiana typu - przepnij pin
    if (cfg.buzzerPassive) {
      ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, 8);
      ledcAttachPin(pin, BUZZER_CHANNEL);
    } else {
      if (buzzerPassive == 1) ledcDetachPin(pin);
      pinMode(pin, OUTPUT);
    }
  }
  buzzerOn = on;
  buzzerPassive = cfg.buzzerPassive;
  buzzerLow = cfg.buzzerLow;
  if (cfg.buzzerPassive) ledcWrite(BUZZER_CHANNEL, on ? 128 : (cfg.buzzerLow ? 255 : 0));
  else digitalWrite(pin, on != cfg.buzzerLow ? HIGH : LOW);
}

static void applyOutputs(const Ctx &c) {
  // miganie podswietleniem co 1 s przy alarmie - jak w starym sterowniku
  int level = c.cfg.backlight;
  if (c.unacked && c.cfg.alarmBlink && (millis() / 1000) % 2) level = 0;
  if (level != lastBacklight) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    lastBacklight = level;
  }
  if (c.cfg.contrast != lastContrast) {
    u8g2.setContrast(c.cfg.contrast);
    lastContrast = c.cfg.contrast;
  }
  // alarm: dwa krotkie sygnaly co sekunde, az do wyciszenia; test: 4 sygnaly
  uint32_t m = millis();
  bool on = c.unacked && c.cfg.buzzer && ((m % 1000) < 120 || ((m % 1000) >= 240 && (m % 1000) < 360));
  if ((int32_t)(buzzerTestUntil - m) > 0) on = (m % 400) < 200;
  setBuzzer(on, c.cfg);
}

static void ackAlarm(const Ctx &c) {
  if (c.unacked) {
    controllerAck();
    toast("Alarm wyciszony");
  } else {
    toast(c.st.alarms ? "Już wyciszony" : "Brak alarmów");
  }
}

// ===========================================================================
// Menu "Ustawienia" (klikniecie enkodera): obrot = wybor, klik = wejscie / zmiana,
// przytrzymanie = wstecz. Jedna lista: grupy ustawien, czujniki, siec, informacje.
// ===========================================================================
enum MenuPage : uint8_t {
  MP_NONE, MP_ROOT, MP_FIELDS, MP_SENSORS, MP_SENSOR, MP_RESET, MP_NET, MP_COUNT
};

enum MenuAction : uint8_t {
  A_NONE, A_BACK, A_SENSORS, A_ACK, A_RESET, A_RESET_YES,
  A_GROUP, A_FIELD, A_BUS, A_ASSIGN_PIEC, A_ASSIGN_BOILER, A_RESCAN,
  A_NET, A_AP, A_RESTART, A_BUZZ_TEST, A_INFO,
};

struct ListState {
  int sel, prev, first;
  uint32_t movedMs;
};

struct MenuRow {
  char label[24];
  char value[20];
  bool sub;          // strzalka: wejscie do podmenu
  bool polish;       // wartosc z polskimi znakami (inna czcionka)
  int8_t toggle;     // -1 brak, 0/1 przelacznik
  uint8_t action;
  int8_t param;
};

const uint32_t MENU_TIMEOUT_MS = 60000;
const int MAX_ROWS = 24;

static MenuPage page = MP_NONE;
static ListState lists[MP_COUNT];
static MenuRow rows[MAX_ROWS];
static int rowCount = 0;
static const char *groups[8];
static int groupCount = 0;
static int curGroup = 0, curBus = 0;
static bool editing = false;
static int editField = -1;
static Settings editCfg;
static uint32_t lastInputMs = 0, lastTurnMs = 0;

static void initGroups() {
  groupCount = 0;
  for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++) {
    const char *g = SETTINGS_FIELDS[i].group;
    bool known = false;
    for (int k = 0; k < groupCount; k++) known |= !strcmp(groups[k], g);
    if (!known && groupCount < 8) groups[groupCount++] = g;
  }
}

static void addRow(const char *label, uint8_t action, int8_t param = 0, const char *value = "",
                   bool sub = false, bool polish = false, int8_t toggle = -1) {
  if (rowCount >= MAX_ROWS) return;
  MenuRow &r = rows[rowCount++];
  strlcpy(r.label, label, sizeof(r.label));
  strlcpy(r.value, value, sizeof(r.value));
  r.sub = sub;
  r.polish = polish;
  r.toggle = toggle;
  r.action = action;
  r.param = param;
}

static void fmtFieldValue(const Settings &s, const SettingField &f, char *out, size_t n, bool &polish) {
  float v = settingsGetField(s, f);
  polish = false;
  if ((f.flags & SF_ZERO_OFF) && v == 0) {
    snprintf(out, n, "wył.");
    polish = true;
  } else if (f.type == ST_INT) {
    snprintf(out, n, "%d%s%s", (int)v, *f.unit ? " " : "", f.unit);
  } else {
    snprintf(out, n, f.step < 1 ? "%.1f%s" : "%.0f%s", v, f.unit);
  }
}

static void fmtBusTemp(float t, char *out, size_t n) {
  if (t == -127.0f) snprintf(out, n, "--");
  else snprintf(out, n, "%.1f°", t);
}

static const char *busRole(const Ctx &c, int i) {
  if (!memcmp(c.st.bus[i].addr, c.st.addr[PIEC], 8)) return "piec";
  if (!memcmp(c.st.bus[i].addr, c.st.addr[BOILER], 8)) return "bojler";
  return "wolny";
}

static void buildRows(const Ctx &c) {
  rowCount = 0;
  char b[24];
  switch (page) {
    case MP_ROOT:
      addRow("< Zamknij", A_BACK);
      if (c.unacked) addRow("Wycisz alarm", A_ACK);
      for (int g = 0; g < groupCount; g++)
        if (c.cfg.pumps || !isPumpGroup(groups[g])) addRow(groups[g], A_GROUP, g, "", true);
      addRow("Czujniki", A_SENSORS, 0, "", true);
      addRow("Sieć Wi-Fi", A_NET, 0, "", true);
      addRow("Informacje", A_INFO);      // ekran Siec i system (poza petla, gdy brak bledow)
      addRow("Zeruj statystyki", A_RESET, 0, "", true);
      break;
    case MP_NET:
      addRow("< Wstecz", A_BACK);
      addRow("Tryb konfiguracji", A_AP, 0, "", false, false, c.net.apActive);
      if (!c.net.wifiConfigured) addRow("Sieć", A_NONE, 0, "brak", false, false);
      else addRow("Sieć", A_NONE, 0, c.net.wifi ? devcfg().wifiSsid.c_str() : "rozłączona", false, !c.net.wifi);
      addRow("IP", A_NONE, 0, c.net.wifi ? c.net.ip.toString().c_str() : "--");
      addRow("Uruchom ponownie", A_RESTART);
      break;
    case MP_FIELDS:
      addRow("< Wstecz", A_BACK);
      for (size_t i = 0; i < SETTINGS_FIELD_COUNT; i++) {
        const SettingField &f = SETTINGS_FIELDS[i];
        if (strcmp(f.group, groups[curGroup])) continue;
        if (f.type == ST_BOOL) {
          addRow(f.lcd, A_FIELD, i, "", false, false, settingsGetField(c.cfg, f) >= 0.5f);
        } else {
          bool polish;
          fmtFieldValue(c.cfg, f, b, sizeof(b), polish);
          addRow(f.lcd, A_FIELD, i, b, false, polish);
        }
      }
      if (!strcmp(groups[curGroup], "Alarmy")) addRow("Test brzęczyka", A_BUZZ_TEST);
      break;
    case MP_SENSORS:
      addRow("< Wstecz", A_BACK);
      for (int i = 0; i < c.st.busCount; i++) {
        char label[24];
        snprintf(label, sizeof(label), "%d: %s", i + 1, busRole(c, i));
        fmtBusTemp(c.st.bus[i].temp, b, sizeof(b));
        addRow(label, A_BUS, i, b, true);
      }
      addRow("Szukaj ponownie", A_RESCAN);
      break;
    case MP_SENSOR:
      addRow("< Wstecz", A_BACK);
      addRow("To jest piec", A_ASSIGN_PIEC);
      addRow("To jest bojler", A_ASSIGN_BOILER);
      break;
    case MP_RESET:
      addRow("< Nie", A_BACK);
      addRow("Tak, zeruj", A_RESET_YES);
      break;
    default:
      break;
  }
}

static const char *pageTitle() {
  switch (page) {
    case MP_ROOT: return "Ustawienia";
    case MP_FIELDS: return groups[curGroup];
    case MP_SENSORS: return "Czujniki";
    case MP_RESET: return "Statystyki";
    case MP_NET: return "Sieć Wi-Fi";
    default: break;
  }
  static char t[16];
  snprintf(t, sizeof(t), "Czujnik %d", curBus + 1);
  return t;
}

static void drawChevron(int x, int y) {
  u8g2.drawLine(x, y + 2, x + 2, y + 4);
  u8g2.drawLine(x + 2, y + 4, x, y + 6);
}

static void drawToggle(int x, int y, bool on) {
  if (on) {
    u8g2.drawRBox(x, y, 15, 8, 3);
    u8g2.setDrawColor(0);
    u8g2.drawBox(x + 9, y + 2, 4, 4);
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawRFrame(x, y, 15, 8, 3);
    u8g2.drawBox(x + 2, y + 2, 4, 4);
  }
}

// Lista z odwracanym (XOR) zaznaczeniem, ktore plynnie przesuwa sie miedzy wierszami
static void drawRows(ListState &ls, int y0, int vis) {
  const int ROW = 12;
  ls.sel = constrain(ls.sel, 0, max(0, rowCount - 1));
  if (ls.sel < ls.first) ls.first = ls.sel;
  if (ls.sel >= ls.first + vis) ls.first = ls.sel - vis + 1;
  ls.first = constrain(ls.first, 0, max(0, rowCount - vis));
  bool scroll = rowCount > vis;
  int right = scroll ? 122 : 127;

  for (int r = 0; r < vis && ls.first + r < rowCount; r++) {
    const MenuRow &row = rows[ls.first + r];
    int y = y0 + r * ROW;
    u8g2.setFont(F_LABEL);
    u8g2.drawUTF8(3, y + 9, row.label);
    int vr = right - 3;
    if (row.sub) {
      drawChevron(vr - 2, y + 1);
      vr -= 7;
    }
    if (row.toggle >= 0) {
      drawToggle(vr - 15, y + 2, row.toggle);
    } else if (row.value[0]) {
      u8g2.setFont(row.polish ? F_LABEL : F_TINY);
      textRight(vr, y + 9, row.value);
    }
  }

  int yTo = y0 + (ls.sel - ls.first) * ROW;
  int yHl = yTo;
  uint32_t dt = millis() - ls.movedMs;
  int pr = ls.prev - ls.first;
  if (dt < 150 && pr >= 0 && pr < vis && ls.prev != ls.sel) {
    int yFrom = y0 + pr * ROW;
    yHl = yFrom + (yTo - yFrom) * (int)dt / 150;
  }
  u8g2.setDrawColor(2);
  u8g2.drawBox(0, yHl, right, ROW);
  u8g2.drawPixel(0, yHl);                 // zaokraglone rogi
  u8g2.drawPixel(right - 1, yHl);
  u8g2.drawPixel(0, yHl + ROW - 1);
  u8g2.drawPixel(right - 1, yHl + ROW - 1);
  u8g2.setDrawColor(1);

  if (scroll) {
    const int x = 125, h = vis * ROW;
    for (int y = y0; y < y0 + h; y += 2) u8g2.drawPixel(x + 1, y);
    int th = max(4, h * vis / rowCount);
    int ty = y0 + (h - th) * ls.first / (rowCount - vis);
    u8g2.drawBox(x, ty, 3, th);
  }
}

static void drawEditPopup() {
  const SettingField &f = SETTINGS_FIELDS[editField];
  const int X = 6, Y = 14, W = 116, H = 48;
  u8g2.drawBox(X + 2, Y + H, W, 2);   // cien
  u8g2.drawBox(X + W, Y + 2, 2, H);
  u8g2.setDrawColor(0);
  u8g2.drawBox(X, Y, W, H);
  u8g2.setDrawColor(1);
  u8g2.drawRFrame(X, Y, W, H, 3);

  u8g2.setFont(F_TITLE);
  textCenter(64, Y + 11, f.lcd);

  float v = settingsGetField(editCfg, f);
  if ((f.flags & SF_ZERO_OFF) && v == 0) {
    u8g2.setFont(F_TITLE);
    textCenter(64, Y + 30, "wyłączone");
  } else {
    char b[12];
    if (f.type == ST_INT) snprintf(b, sizeof(b), "%d", (int)v);
    else snprintf(b, sizeof(b), f.step < 1 ? "%.1f" : "%.0f", v);
    u8g2.setFont(F_HUGE);
    int w = u8g2.getStrWidth(b);
    u8g2.setFont(F_TINY);
    int uw = *f.unit ? u8g2.getUTF8Width(f.unit) + 2 : 0;
    int x = 64 - (w + uw) / 2;
    u8g2.setFont(F_HUGE);
    u8g2.drawStr(x, Y + 34, b);
    if (uw) {
      u8g2.setFont(F_TINY);
      u8g2.drawUTF8(x + w + 2, Y + 24, f.unit);
    }
  }
  // strzalki: ktora strone mozna jeszcze krecic
  const int ay = Y + 26;
  if (v > f.min) u8g2.drawTriangle(X + 5, ay, X + 10, ay - 5, X + 10, ay + 5);
  if (v < f.max) u8g2.drawTriangle(X + W - 6, ay, X + W - 11, ay - 5, X + W - 11, ay + 5);

  char r[24];
  snprintf(r, sizeof(r), "%g - %g", f.min, f.max);
  u8g2.setFont(F_SMALL);
  textCenter(64, Y + 44, r);
}

static void drawMenu(const Ctx &c) {
  drawHeader(c, pageTitle());
  int y0 = 12, vis = 4;
  if (page == MP_SENSOR && curBus < c.st.busCount) {
    char a[32], t[16];
    formatAddress(c.st.bus[curBus].addr, a, sizeof(a));
    u8g2.setFont(F_SMALL);
    u8g2.drawStr(2, 18, a);
    fmtBusTemp(c.st.bus[curBus].temp, t, sizeof(t));
    u8g2.setFont(F_TINY);
    textRight(127, 25, t);
    u8g2.setFont(F_SMALL);
    char role[20];
    snprintf(role, sizeof(role), "TERAZ: %s", busRole(c, curBus));
    for (char *p = role; *p; p++) *p = toupper(*p);
    u8g2.drawStr(2, 25, role);
    y0 = 28;
    vis = 3;
  } else if (page == MP_RESET) {
    u8g2.setFont(F_LABEL);
    u8g2.drawUTF8(3, 22, "Wyzerować dziś i wczoraj?");
    y0 = 28;
    vis = 2;
  }
  drawRows(lists[page], y0, vis);
  if (editing) drawEditPopup();
}

static void goPage(MenuPage p, int sel = 1) {
  page = p;
  lists[p] = {sel, sel, 0, 0};
}

static void openMenu(const Ctx &c) {
  render(current, c);
  memcpy(imgOld, u8g2.getBufferPtr(), bufferSize());
  goPage(MP_ROOT, 1);   // od razu na "Wycisz alarm" albo pierwszej grupie ustawien
  editing = false;
  buildRows(c);
  u8g2.clearBuffer();
  drawMenu(c);
  memcpy(imgNew, u8g2.getBufferPtr(), bufferSize());
  slideBuffers(1);
}

static void closeMenu(const Ctx &c, bool animate) {
  if (animate) {
    u8g2.clearBuffer();
    drawMenu(c);
    memcpy(imgOld, u8g2.getBufferPtr(), bufferSize());
  }
  page = MP_NONE;
  editing = false;
  if (animate) {
    render(current, c);
    memcpy(imgNew, u8g2.getBufferPtr(), bufferSize());
    slideBuffers(-1);
  }
  screenStart = millis();
}

static void goBack(const Ctx &c) {
  if (editing) {
    editing = false;
    toast("Anulowano");
    return;
  }
  switch (page) {
    case MP_ROOT: closeMenu(c, true); break;
    case MP_FIELDS: page = MP_ROOT; break;
    case MP_SENSOR: page = MP_SENSORS; break;
    default: page = MP_ROOT; break;     // wybor w liscie zostaje zapamietany
  }
}

static void activate(const Ctx &c, const MenuRow &row) {
  switch (row.action) {
    case A_BACK: goBack(c); break;
    case A_SENSORS: goPage(MP_SENSORS); break;
    case A_INFO: {   // menu -> ekran Siec i system (wjezdza z prawej), widoczny przez minute
      uiSystemForcedUntil = millis() + MANUAL_HOLD_MS;
      holdUntil = uiSystemForcedUntil;
      memcpy(imgOld, lastFrame, bufferSize());   // ostatnia narysowana klatka menu
      page = MP_NONE;
      editing = false;
      current = SCREEN_SYSTEM;
      render(current, c);
      memcpy(imgNew, u8g2.getBufferPtr(), bufferSize());
      slideBuffers(1);
      lastFrameValid = false;
      screenStart = millis();
      break;
    }
    case A_ACK: ackAlarm(c); break;
    case A_RESET: goPage(MP_RESET, 0); break;   // domyslnie "Nie"
    case A_RESET_YES:
      controllerResetStats();
      toast("Wyzerowano");
      page = MP_ROOT;
      break;
    case A_GROUP:
      curGroup = row.param;
      goPage(MP_FIELDS);
      break;
    case A_FIELD: {
      const SettingField &f = SETTINGS_FIELDS[row.param];
      if (f.type == ST_BOOL) {   // przelacznik zmienia sie od razu
        Settings s = settingsGet();
        settingsSetField(s, f, settingsGetField(s, f) >= 0.5f ? 0 : 1);
        settingsSet(s);
        toast("Zapisano");
      } else {
        editing = true;
        editField = row.param;
        editCfg = settingsGet();
      }
      break;
    }
    case A_BUS:
      curBus = row.param;
      goPage(MP_SENSOR);
      break;
    case A_ASSIGN_PIEC:
    case A_ASSIGN_BOILER:
      if (curBus < c.st.busCount) {
        bool piec = row.action == A_ASSIGN_PIEC;
        controllerAssignSensor(piec ? PIEC : BOILER, c.st.bus[curBus].addr);
        toast(piec ? "Przypisano: piec" : "Przypisano: bojler");
      }
      page = MP_SENSORS;
      break;
    case A_RESCAN:
      controllerRescan();
      toast("Szukam czujników");
      break;
    case A_NET:
      goPage(MP_NET);
      break;
    case A_AP:
      netRequestAp(!c.net.apActive);
      toast(c.net.apActive ? "Wyłączam sieć konf." : "Włączam sieć konf.");
      break;
    case A_RESTART:
      requestRestart(500);
      break;
    case A_BUZZ_TEST:
      if (devcfg().pinBuzzer < 0) toast("Brak pinu brzęczyka");
      else uiBuzzerTest();
      break;
    default:
      break;
  }
}

static void commitEdit() {
  const SettingField &f = SETTINGS_FIELDS[editField];
  Settings s = settingsGet();   // tylko to pole - reszta mogla zmienic sie w WWW
  settingsSetField(s, f, settingsGetField(editCfg, f));
  settingsSet(s);
  editing = false;
  toast("Zapisano");
}

static void adjustEdit(int d) {
  const SettingField &f = SETTINGS_FIELDS[editField];
  uint32_t now = millis();
  bool fast = now - lastTurnMs < 70;   // szybkie krecenie = wieksze kroki
  lastTurnMs = now;
  float mult = fast ? ((f.max - f.min) / f.step > 100 ? 10 : 4) : 1;
  float v = settingsGetField(editCfg, f) + d * f.step * mult;
  v = roundf(v / f.step) * f.step;
  settingsSetField(editCfg, f, v);
}

// Zwraca true, jesli menu obsluzylo klatke
static bool menuLoop(const Ctx &c, int d, ButtonEvent eb, ButtonEvent bb) {
  if (page == MP_NONE) return false;
  if (millis() - lastInputMs > MENU_TIMEOUT_MS) {
    closeMenu(c, true);
    return true;
  }
  buildRows(c);
  if (bb == BTN_LONG) ackAlarm(c);
  if (eb == BTN_LONG || bb == BTN_CLICK) {
    goBack(c);
  } else if (eb == BTN_CLICK) {
    if (editing) commitEdit();
    else if (rowCount) activate(c, rows[constrain(lists[page].sel, 0, rowCount - 1)]);
  }
  if (page == MP_NONE) return true;   // menu zamkniete przyciskiem

  if (d) {
    if (editing) {
      adjustEdit(d);
    } else {
      ListState &ls = lists[page];
      ls.prev = ls.sel;
      ls.sel = constrain(ls.sel + d, 0, max(0, rowCount - 1));
      ls.movedMs = millis();
    }
  }
  Ctx v = c;
  if (editing) v.cfg = editCfg;   // podglad na zywo (np. kontrast, podswietlenie)
  applyOutputs(v);
  if (!frameDue(c, d || eb || bb)) return true;
  buildRows(v);
  u8g2.clearBuffer();
  drawMenu(v);
  drawToast();
  present();
  return true;
}

// ---------------------------------------------------------------------------
void uiBegin() {
  const DeviceConfig &d = devcfg();
  SPI.begin(d.pinLcdSck, -1, d.pinLcdMosi, -1);   // u8g2 wywola SPI.begin() ponownie - bez efektu
  u8x8_SetPin_4Wire_HW_SPI(u8g2.getU8x8(), d.pinLcdCs, d.pinLcdDc,
                           d.pinLcdRst < 0 ? U8X8_PIN_NONE : d.pinLcdRst);
  u8g2.begin();
  u8g2.setFontMode(1);
  u8g2.enableUTF8Print();
  Settings cfg = settingsGet();
  u8g2.setContrast(cfg.contrast);
  lastContrast = cfg.contrast;

  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  if (d.pinBacklight >= 0) ledcAttachPin(d.pinBacklight, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, cfg.backlight);
  lastBacklight = cfg.backlight;

  inputBegin();
  initGroups();
  setBuzzer(false, cfg);   // ustaw pin w stanie "cisza" od razu po starcie

  u8g2.clearBuffer();
  drawIcon(30, 12, ICON_STOVE);
  drawIcon(92, 12, ICON_BOILER);
  u8g2.setFont(F_TITLE);
  textCenter(64, 20, "Sterownik");
  u8g2.setFont(F_TEXT);
  textCenter(64, 34, "CO / CWU");
  u8g2.setFont(F_TINY);
  textCenter(64, 46, "v" FW_VERSION);
  u8g2.setFont(F_LABEL);
  textCenter(64, 60, d.wifiSsid.length() ? "łączenie z Wi-Fi…" : "konfiguracja Wi-Fi…");
  u8g2.sendBuffer();
  screenStart = millis();
}

void uiLoop() {
  Ctx c;
  c.st = controllerSnapshot();
  c.cfg = settingsGet();
  c.net = netStatus();
  c.now = millis();
  c.unacked = unackedAlarms(c.st);

  int d = encoderTake();
  ButtonEvent eb = encoderButton();
  ButtonEvent bb = bootButton();
  if (d || eb || bb) lastInputMs = c.now;

  if (resetButtonFired()) {
    Serial.println("[system] przycisk reset");
    requestRestart(300);
  }
  if (c.net.otaProgress >= 0) {
    applyOutputs(c);
    drawOta(c.net.otaProgress);
    return;
  }
  if (restartPending()) {
    setBuzzer(false, c.cfg);
    drawRestart();
    return;
  }
  // Wlaczyla sie siec konfiguracyjna - pokaz, jak sie z nia polaczyc
  bool apRising = c.net.apActive && !uiApActive;
  uiApActive = c.net.apActive;
  uiNetProblem = netProblem(c);
  if (menuLoop(c, d, eb, bb)) return;
  applyOutputs(c);

  // --- ekrany ---
  if (eb == BTN_CLICK) {
    if (c.unacked) ackAlarm(c);   // pierwszy klik przy alarmie = cisza
    else { openMenu(c); return; }
  }
  if (eb == BTN_LONG) {
    openMenu(c);
    return;
  }
  if (bb == BTN_LONG) ackAlarm(c);
  if (bb == BTN_CLICK) d = 1;     // BOOT = nastepny ekran

  if (apRising) {
    holdUntil = c.now + MANUAL_HOLD_MS;
    slideTo(SCREEN_AP, c);
    return;
  }
  // Screen left the loop (e.g. the network problem is gone) - back to the main screen
  if (!SCREENS[current].visible()) {
    slideTo(0, c, -1);
    return;
  }
  if (d) {
    holdUntil = c.now + MANUAL_HOLD_MS;
    slideTo(d > 0 ? nextVisible(current) : prevVisible(current), c, d > 0 ? 1 : -1);
    return;
  }
  bool held = (int32_t)(c.now - holdUntil) < 0;
  // Nowy alarm: pokaz ekran glowny (chyba ze uzytkownik wlasnie przegladal ekrany)
  if (c.unacked && current != 0 && !held) {
    slideTo(0, c, -1);
    return;
  }
  // The main screen (temperatures and trend) stays; any other one returns to it after 2 min
  if (current != 0 && c.now - screenStart >= RETURN_MAIN_MS) {
    slideTo(0, c, -1);
    return;
  }
  if (!frameDue(c, eb || bb)) return;
  render(current, c);
  present();
}
