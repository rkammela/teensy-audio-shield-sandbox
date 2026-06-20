/*
 * RotaryUI - thin wrapper around an incremental rotary encoder + push
 * button. The library owns the underlying Encoder instance and tracks
 * the debounce state for the button; callers just poll for events from
 * loop().
 *
 *   RotaryUI::begin(pinA, pinB, pinBtn);
 *   int detents = RotaryUI::pollRotation();      // signed, -N..+N
 *   if (RotaryUI::pollButtonPressed()) { ... }    // true once per click
 *
 * The library does NOT interpret rotation direction (CW vs CCW). Wiring
 * varies between encoders; the caller decides which sign means "next".
 */

#ifndef AURA_ROTARY_UI_H
#define AURA_ROTARY_UI_H

#include <Arduino.h>

namespace RotaryUI {

  // Initialize the encoder + button. pinA/pinB are the encoder's quadrature
  // channels (CLK/DT), pinBtn is the push-button line which is configured
  // as INPUT_PULLUP. debounceMs is how long after a press is registered
  // before the next one will be accepted.
  void begin(int pinA, int pinB, int pinBtn, unsigned long debounceMs = 50);

  // Return the number of full detents the encoder has rotated since the
  // last call (positive or negative). 0 means no movement. Most encoders
  // emit 4 quadrature pulses per detent; the library handles that math.
  int pollRotation();

  // Return true exactly once per debounced button press (HIGH -> LOW edge).
  // Subsequent calls return false until the button is released and pressed
  // again past the debounce window.
  bool pollButtonPressed();

} // namespace RotaryUI

#endif // AURA_ROTARY_UI_H
