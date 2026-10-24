// Serwer WWW:
//   /              - strona IDENTYCZNA jak w starym sterowniku (bez logowania)
//   /panel         - panel: odczyty, wykres 24 h, statystyki, ustawienia
//   /api/status    - stan w JSON
//   /api/history   - historia 24 h (co 1 min)
//   /api/settings  - GET ustawienia + opis pol, POST zmiana (formularz)
//   /api/ack       - POST wyciszenie alarmu
//   /api/stats/reset - POST zerowanie statystyk
#pragma once

void webBegin();
