#include "RotaryUI.h"

#include <Encoder.h>

namespace {
  // Quadrature encoders typically emit 4 pulses per physical detent;
  // dividing the raw position by this keeps loop logic on "clicks".
  constexpr int PULSES_PER_DETENT = 4;

  // Heap-allocated singleton. Constructed once in begin() because the
  // Encoder class registers ISRs based on the pins it is given.
  Encoder* knob = nullptr;

  long lastPosition = 0;

  int btnPin = -1;
  bool lastBtnState = HIGH;
  unsigned long lastBtnPress = 0;
  unsigned long debounceWindow = 50;
}

namespace RotaryUI {

void begin(int pinA, int pinB, int pinBtn, unsigned long debounceMs) {
  if (knob == nullptr) {
    knob = new Encoder(pinA, pinB);
  }
  lastPosition = 0;

  btnPin          = pinBtn;
  pinMode(btnPin, INPUT_PULLUP);
  lastBtnState    = HIGH;
  lastBtnPress    = 0;
  debounceWindow  = debounceMs;
}

int pollRotation() {
  if (knob == nullptr) return 0;

  long newPosition = knob->read();
  long delta = (newPosition - lastPosition) / PULSES_PER_DETENT;
  if (delta != 0) {
    // Snap our reference back to the most-recent detent boundary so the
    // next call only sees brand-new movement.
    lastPosition = newPosition - (newPosition % PULSES_PER_DETENT);
  }
  return (int)delta;
}

bool pollButtonPressed() {
  if (btnPin < 0) return false;

  bool state = digitalRead(btnPin);
  unsigned long now = millis();
  bool pressed = false;

  // Falling edge (released -> pressed) with a debounce window so a single
  // mechanical click only fires once.
  if (state == LOW && lastBtnState == HIGH
                   && (now - lastBtnPress) > debounceWindow) {
    lastBtnPress = now;
    pressed = true;
  }
  lastBtnState = state;
  return pressed;
}

} // namespace RotaryUI
