/*
 * LedMatrix - small helpers for square NeoPixel matrices wired in serpentine
 * order. Assumes the actual FastLED bus setup is done by the host project;
 * this library only provides pure pixel-index math + small drawing utilities.
 *
 *   - xyToLEDIndex(row, col)        coordinate -> linear index
 *   - distanceToColor(dist_mm)      ToF distance -> colour ramp
 *   - clearMatrix(buf, count)       wipe a single CRGB buffer to black
 */

#ifndef AURA_LED_MATRIX_H
#define AURA_LED_MATRIX_H

#include <FastLED.h>

namespace LedMatrix {

  // Map a (row, col) cell on a serpentine-wired matrix to a flat LED index.
  // Row 0 runs left-to-right, row 1 runs right-to-left, and so on.
  // matrixWidth defaults to 8 (the AURA panels) but is a parameter so the
  // helper works for any square serpentine layout.
  int xyToLEDIndex(int row, int col, int matrixWidth = 8);

  // Map a ToF distance (millimetres) to a colour for live grid visualisation.
  //   <200 mm  bright red       very close / target detected
  //   <400 mm  orange           close
  //   <600 mm  green            medium
  //   <800 mm  blue             far
  //   <1000 mm dim purple       very far
  //   else    black            out of range / nothing detected
  CRGB distanceToColor(uint16_t distance_mm);

  // Set every pixel of one CRGB buffer to black. Does NOT call FastLED.show()
  // - the caller decides when to flush.
  void clearMatrix(CRGB* leds, int count);

} // namespace LedMatrix

#endif // AURA_LED_MATRIX_H
