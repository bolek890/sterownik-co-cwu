// Sterownik temperatury pieca CO i bojlera CWU
// ESP32-S3 Zero + LCD GMG12864-06D + 2x DS18B20 + enkoder
//
// Zgodny ze starym sterownikiem (LCD 20x4):
//  - te same adresy czujnikow i rozdzielczosc 9 bit,
//  - trend, delta i czas od zmiany trendu liczone tak samo (probka co 5 min),
//  - Firebase: te same sciezki /readings/<epoch>/{temperaturaPiec,temperaturaBoiler,timestamp},
//    te same wartosci tekstowe i odstep 5 min, konto anonimowe,
//  - strona http://<IP>/ zwraca ten sam HTML,
//  - alarm 80 °C miga podswietleniem co 1 s.
// Nowe: panel /panel, konfiguracja /config (Wi-Fi, Firebase, piny, aktualizacja),
// ustawienia w NVS, wykresy 24 h, statystyki, pompy, menu enkodera, OTA.
#include <Arduino.h>
#include "config.h"
#include "controller.h"
#include "devcfg.h"
#include "net.h"
#include "settings.h"
#include "shared.h"
#include "ui.h"
#include "web.h"

SemaphoreHandle_t gLock = nullptr;
static volatile uint32_t restartAt = 0;

RTC_NOINIT_ATTR static uint32_t rtcStage;
RTC_NOINIT_ATTR static uint32_t rtcStageMagic;
static uint32_t stageBeforeReset = STAGE_NONE;
const uint32_t STAGE_MAGIC = 0x5354474Bu;

void setStage(CrashStage s) {
  rtcStage = s;
  rtcStageMagic = STAGE_MAGIC;
}
uint32_t lastStageBeforeReset() { return stageBeforeReset; }

const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "wlaczenie zasilania";
    case ESP_RST_SW:        return "restart programowy";
    case ESP_RST_PANIC:     return "AWARIA (wyjatek/panic)";
    case ESP_RST_INT_WDT:   return "AWARIA (watchdog przerwan)";
    case ESP_RST_TASK_WDT:  return "AWARIA (watchdog zadan)";
    case ESP_RST_WDT:       return "AWARIA (watchdog)";
    case ESP_RST_BROWNOUT:  return "spadek napiecia (brownout)";
    case ESP_RST_DEEPSLEEP: return "wybudzenie";
    case ESP_RST_EXT:       return "przycisk RST";
    default:                return "inna";
  }
}

void sharedBegin() { gLock = xSemaphoreCreateRecursiveMutex(); }

void requestRestart(uint32_t delayMs) { restartAt = max<uint32_t>(1, millis() + delayMs); }
bool restartPending() { return restartAt != 0; }

void setup() {
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  sharedBegin();
  devcfgLoad();
  settingsLoad();
  stageBeforeReset = rtcStageMagic == STAGE_MAGIC ? rtcStage : STAGE_NONE;
  setStage(STAGE_NONE);
  uiBegin();
  Serial.printf("\n=== Sterownik CO/CWU v%s ===\n", FW_VERSION);

  controllerBegin();
  netBegin();
  webBegin();
  delay(1500);   // ekran startowy
}

volatile float gChipTemp = NAN;

// Temperatura ukladu ESP32-S3 (czujnik wbudowany) - na ekran, do WWW i do logu
static void updateDiagnostics() {
  static uint32_t lastRead = 0, lastLog = 0;
  if (lastRead && millis() - lastRead < 5000) return;
  lastRead = millis();
  gChipTemp = temperatureRead();
  if (millis() - lastLog >= 30000 || !lastLog) {
    lastLog = millis();
    NetStatus n = netStatus();
    // przyczyna startu w kazdym wpisie - port USB pojawia sie dopiero po starcie
    Serial.printf("[system] uklad %.1f C, CPU %u MHz, wolne %u kB (min %u kB), Wi-Fi %s%s | start: %s, etap %u\n",
                  gChipTemp, getCpuFrequencyMhz(), ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024,
                  n.wifi ? "polaczone" : "brak", n.apActive ? ", siec konfiguracyjna" : "",
                  resetReasonText(), (unsigned)stageBeforeReset);
  }
}

void loop() {
  uiLoop();
  settingsSaveIfDirty();
  updateDiagnostics();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
    Serial.println("[system] ponowne uruchamianie");
    Serial.flush();
    ESP.restart();
  }
  delay(20);   // tylko odpytanie przyciskow - ekran rysuje sie rzadziej (uiLoop)
}
