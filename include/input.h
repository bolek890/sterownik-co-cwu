// Wejscia: enkoder obrotowy (KY-040/EC11) i przyciski (przycisk enkodera, BOOT).
#pragma once
#include <Arduino.h>

enum ButtonEvent : uint8_t { BTN_NONE, BTN_CLICK, BTN_LONG };

void inputBegin();
int  encoderTake();          // zapadki od ostatniego wywolania (+ = w prawo)
ButtonEvent encoderButton(); // przycisk na osi enkodera
ButtonEvent bootButton();    // przycisk BOOT na plytce
bool resetButtonFired();     // przycisk reset przytrzymany RESET_HOLD_MS (raz na nacisniecie)
