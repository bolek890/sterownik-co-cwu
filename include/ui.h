// Ekran GMG12864-06D: ekrany przelaczane automatycznie, alarmy, podswietlenie,
// brzeczyk i przycisk BOOT (krotko = nastepny ekran, 1 s = wyciszenie alarmu).
#pragma once

void uiBegin();   // ekran startowy
void uiLoop();    // wolane w kazdym obiegu loop()
void uiBuzzerTest();   // krotki sygnal testowy (menu, panel WWW)
