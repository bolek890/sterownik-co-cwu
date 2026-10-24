// Wi-Fi, punkt dostepu do konfiguracji, czas NTP, Firebase (format zgodny ze starym
// sterownikiem) i OTA. Dziala we wlasnym zadaniu, zeby wysylka TLS nie zatrzymywala ekranu.
//
// Punkt dostepu "Sterownik-CO-xxxx" (haslo AP_PASSWORD, strona http://192.168.4.1/config)
// wlacza sie sam, gdy nie ma skonfigurowanej sieci albo polaczenie nie dziala, lub z menu.
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

struct NetStatus {
  bool wifiConfigured;
  bool wifi;
  int rssi;
  IPAddress ip;
  bool apActive;
  char apSsid[33];
  uint8_t apClients;
  bool timeValid;
  bool fbConfigured;
  bool fbReady;
  bool fbLastOk;
  uint32_t fbLastMs;     // millis() ostatniej proby wysylki, 0 = jeszcze nie
  uint32_t fbSent;       // liczba udanych wysylek
  char fbError[48];
  int otaProgress;       // -1 = brak aktualizacji, 0-100 = trwa
};

void netBegin();
NetStatus netStatus();
bool timeValid();
void netRequestAp(bool on);       // wlacz/wylacz punkt dostepu (z menu)
void netSetOtaProgress(int p);    // postep aktualizacji (OTA lub strona /config)
