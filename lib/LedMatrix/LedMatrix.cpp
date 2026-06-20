#include "LedMatrix.h"

namespace LedMatrix {

  int xyToLEDIndex(int row, int col, int matrixWidth) {
    // Serpentine pattern: even rows go left-to-right, odd rows go right-to-left.
    if (row % 2 == 0) {
      return row * matrixWidth + col;
    } else {
      return row * matrixWidth + (matrixWidth - 1 - col);
    }
  }

  CRGB distanceToColor(uint16_t distance_mm) {
    if (distance_mm < 200) {
      return CRGB::Red;
    } else if (distance_mm < 400) {
      return CRGB(255, 165, 0);   // orange
    } else if (distance_mm < 600) {
      return CRGB::Green;
    } else if (distance_mm < 800) {
      return CRGB::Blue;
    } else if (distance_mm < 1000) {
      return CRGB(64, 0, 64);     // dim purple
    } else {
      return CRGB::Black;
    }
  }

  void clearMatrix(CRGB* leds, int count) {
    for (int i = 0; i < count; i++) {
      leds[i] = CRGB::Black;
    }
  }

} // namespace LedMatrix
