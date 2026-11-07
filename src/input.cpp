#include "input.h"
#include "config.h"
#include "devcfg.h"

// ---------------------------------------------------------------------------
// Enkoder: dekoder kwadraturowy w przerwaniu. Nieprawidlowe przejscia (drgania
// stykow) daja 0, a zapadka liczy sie dopiero w pozycji spoczynkowej - bez
// kondensatorow i bez gubienia krokow.
// ---------------------------------------------------------------------------
static const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
static volatile int32_t detents = 0;
static volatile uint8_t qstate = 0;
static volatile int8_t acc = 0;
static int8_t encClk = -1, encDt = -1;   // from devcfg (page /config), set in inputBegin()

static inline uint8_t readAB() {
  return (digitalRead(encClk) << 1) | digitalRead(encDt);
}

static void IRAM_ATTR encoderIsr() {
  uint8_t s = readAB();
  portENTER_CRITICAL_ISR(&encMux);
  qstate = ((qstate << 2) | s) & 0x0F;
  acc += QDEC[qstate];
  bool rest = s == 3 || (ENCODER_HALF_STEP && s == 0);
  if (rest) {
    if (acc >= 2) detents += ENCODER_DIRECTION;
    else if (acc <= -2) detents -= ENCODER_DIRECTION;
    acc = 0;
  }
  portEXIT_CRITICAL_ISR(&encMux);
}

int encoderTake() {
  portENTER_CRITICAL(&encMux);
  int d = detents;
  detents = 0;
  portEXIT_CRITICAL(&encMux);
  return d;
}

// ---------------------------------------------------------------------------
// Przyciski: odpytywane z petli ekranu (co ~50 ms, to wystarcza jako filtr drgan)
// ---------------------------------------------------------------------------
struct Button {
  int pin;
  bool prev;
  bool longFired;
  uint32_t downMs;
};

static Button encBtn = {-1, false, false, 0};   // pin set in inputBegin()
static Button bootBtn = {PIN_BUTTON, false, false, 0};

static ButtonEvent poll(Button &b) {
  if (b.pin < 0) return BTN_NONE;
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  ButtonEvent ev = BTN_NONE;
  if (pressed && !b.prev) {
    b.downMs = now;
    b.longFired = false;
  }
  if (pressed && !b.longFired && now - b.downMs >= LONG_PRESS_MS) {
    b.longFired = true;          // dlugie nacisniecie zglaszane od razu, bez puszczania
    ev = BTN_LONG;
  }
  if (!pressed && b.prev && !b.longFired && now - b.downMs >= 20) ev = BTN_CLICK;
  b.prev = pressed;
  return ev;
}

ButtonEvent encoderButton() { return poll(encBtn); }
ButtonEvent bootButton() { return poll(bootBtn); }

// Reset: musi byc wcisniety bez przerwy przez RESET_HOLD_MS - pojedyncze zaklocenie
// (np. od przekaznika pompy na dlugim przewodzie) nie zrestartuje sterownika.
static int resetPin = -1;
static uint32_t resetDownMs = 0;
static bool resetFired = false;

bool resetButtonFired() {
  if (resetPin < 0) return false;
  if (digitalRead(resetPin) != LOW) {
    resetDownMs = 0;
    resetFired = false;
    return false;
  }
  uint32_t now = millis();
  if (!resetDownMs) resetDownMs = now;
  if (!resetFired && now - resetDownMs >= RESET_HOLD_MS) {
    resetFired = true;
    return true;
  }
  return false;
}

void inputBegin() {
  resetPin = devcfg().pinResetBtn;
  if (resetPin >= 0) pinMode(resetPin, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  encBtn.pin = devcfg().pinEncSw;
  if (encBtn.pin >= 0) pinMode(encBtn.pin, INPUT_PULLUP);
  encClk = devcfg().pinEncClk;
  encDt = devcfg().pinEncDt;
  if (encClk >= 0 && encDt >= 0) {
    pinMode(encClk, INPUT_PULLUP);
    pinMode(encDt, INPUT_PULLUP);
    qstate = readAB();
    attachInterrupt(digitalPinToInterrupt(encClk), encoderIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(encDt), encoderIsr, CHANGE);
  }
}
