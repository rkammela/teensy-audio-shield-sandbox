#include "Backstage.h"

#include <FastLED.h>
#include "Config.h"
#include "AuraState.h"
#include "LedMatrix.h"

namespace Backstage {

void process(unsigned long currentTime) {
  // Diagnostic: render BOTH sensor grids simultaneously. CH0 -> LEFT
  // matrix, CH1 -> RIGHT matrix. Rotation is handled inside
  // readDistanceGrid(), so grid[row][col] already matches the user-facing
  // orientation of each LED matrix.
  if (currentTime - lastSensorRead < SENSOR_READ_INTERVAL) return;
  lastSensorRead = currentTime;

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  for (int row = 0; row < MATRIX_HEIGHT; row++) {
    for (int col = 0; col < MATRIX_WIDTH; col++) {
      int ledIndex = LedMatrix::xyToLEDIndex(row, col);
      leds[ledIndex]   = sensor_ch0_initialized
                           ? LedMatrix::distanceToColor(distanceGrid_ch0[row][col])
                           : CRGB::Black;
      leds_r[ledIndex] = sensor_ch1_initialized
                           ? LedMatrix::distanceToColor(distanceGrid_ch1[row][col])
                           : CRGB::Black;
    }
  }
  FastLED.show();
}

} // namespace Backstage
