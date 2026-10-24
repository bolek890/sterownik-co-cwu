// Logika sterownika: czujniki, trend (zgodny ze starym sterownikiem), alarmy,
// pompy, statystyki dobowe i historia 24 h. Dziala we wlasnym zadaniu FreeRTOS,
// wiec sterowanie nie zalezy od Wi-Fi, Firebase ani odswiezania ekranu.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "settings.h"

enum SensorId : uint8_t { PIEC = 0, BOILER = 1 };
enum PumpId : uint8_t { PUMP_CO = 0, PUMP_CWU = 1 };

// Kody trendu jak w starym sterowniku (numery znakow LCD)
enum Trend : uint8_t { TREND_NONE = 0, TREND_UP = 2, TREND_DOWN = 3, TREND_FLAT = 4 };

enum AlarmBit : uint8_t {
  AL_PIEC_HIGH     = 1 << 0,
  AL_BOILER_HIGH   = 1 << 1,
  AL_PIEC_LOW      = 1 << 2,
  AL_PIEC_SENSOR   = 1 << 3,
  AL_BOILER_SENSOR = 1 << 4,
};

struct SensorState {
  float raw;            // ostatni surowy odczyt (-127 = blad, jak DEVICE_DISCONNECTED_C)
  float temp;           // ostatni poprawny odczyt (NAN = jeszcze brak)
  bool ok;              // ostatni odczyt poprawny
  bool found;           // czujnik odpowiedzial przy starcie
  uint8_t errors;       // bledy z rzedu
  // --- trend 1:1 ze starego sterownika ---
  float last[3];        // lastTempPiec/lastTempBoiler
  uint8_t trend;        // 2 w gore, 3 w dol, 4 bez zmian
  uint8_t lastTrend;
  float trendRef;       // referenceTemp
  float trendDiff;      // temperatureDifference
  uint32_t trendStartMs;  // startNewTrendTime
};

struct DayStats {
  float min, max;       // NAN = brak danych
  float sum;            // suma probek minutowych (do sredniej)
  uint32_t samples;
  uint32_t minutesAbove;  // minuty powyzej temperatury granicznej
};

struct Stats {
  DayStats s[2];
  uint16_t alarms;
  float pumpOnSec[2];
  uint16_t pumpStarts[2];
};

struct PumpState {
  bool on;
  bool hasOutput;       // pin przekaznika skonfigurowany
  const char *reason;
};

#define BUS_MAX 4   // ile czujnikow na magistrali pokazujemy do przypisania

struct BusDevice {
  uint8_t addr[8];
  float temp;           // DEVICE_DISCONNECTED_C = brak odczytu
};

struct CtrlState {
  SensorState s[2];
  uint8_t addr[2][8];   // przypisane adresy (piec, bojler)
  BusDevice bus[BUS_MAX];
  uint8_t busCount;
  uint8_t alarms;       // AlarmBit aktywne
  uint8_t acked;        // AlarmBit potwierdzone (wyciszone)
  PumpState pump[2];
  Stats today, yesterday;
  bool haveYesterday;
  uint8_t devicesOnBus;
  uint32_t readings;
};

void controllerBegin();
CtrlState controllerSnapshot();
// Kopiuje historie (od najstarszej), wartosci = °C * 10 lub HIST_INVALID. Zwraca liczbe probek.
int controllerHistory(uint8_t sensor, int16_t *out, int maxN);
void controllerAck();
// Przypisuje czujnik (adres z magistrali) do pieca lub bojlera i zapisuje w NVS
void controllerAssignSensor(uint8_t sensor, const uint8_t addr[8]);
void formatAddress(const uint8_t addr[8], char *out, size_t n);   // "28:71:D0:..:2B"
void controllerRescan();   // ponowne wyszukanie czujnikow na magistrali
void controllerResetStats();

// Czas od zmiany trendu jak w starym sterowniku: "HH:MM" albo ">100h"
String trendTimeLabel(uint32_t startMs);
// Opis alarmu w dwoch liniach (ekran i WWW)
void alarmText(uint8_t bit, const CtrlState &st, const Settings &cfg,
               char *line1, char *line2, size_t n);
inline uint8_t unackedAlarms(const CtrlState &st) { return st.alarms & ~st.acked; }
