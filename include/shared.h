// Wspolna blokada danych miedzy zadaniami (sterowanie, siec, ekran, WWW).
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t gLock;

void sharedBegin();

extern volatile float gChipTemp;   // temperatura ukladu ESP32-S3, odswiezana co 5 s

// Diagnostyka restartow: etap pracy zapisany w pamieci RTC (przetrwa restart programowy
// i awarie, nie przetrwa odlaczenia zasilania). Po starcie wypisywany razem z przyczyna.
enum CrashStage : uint32_t {
  STAGE_NONE = 0, STAGE_FB_READY = 1, STAGE_FB_SEND = 2, STAGE_FB_INIT = 3, STAGE_FB_IDLE = 4,
};
void setStage(CrashStage s);
const char *resetReasonText();   // przyczyna ostatniego restartu (po polsku)
uint32_t lastStageBeforeReset();

// Ponowne uruchomienie z opoznieniem (zeby odpowiedz HTTP zdazyla dojsc)
void requestRestart(uint32_t delayMs = 1500);
bool restartPending();

struct LockGuard {
  LockGuard() { xSemaphoreTakeRecursive(gLock, portMAX_DELAY); }
  ~LockGuard() { xSemaphoreGiveRecursive(gLock); }
  LockGuard(const LockGuard &) = delete;
  LockGuard &operator=(const LockGuard &) = delete;
};
