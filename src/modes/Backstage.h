/*
 * Backstage mode - diagnostic view (no audio).
 *
 *   Backstage::process(currentTime) - dual-sensor distance heatmap; CH0
 *                                     drives the LEFT matrix, CH1 drives
 *                                     the RIGHT matrix. Useful for verifying
 *                                     sensor orientation and detection range.
 *
 * Called from main.cpp's loop() when MODE_BACKSTAGE is active. The
 * LED-only Welcome animation lives in its own file (src/modes/Welcome.cpp).
 */

#ifndef AURA_MODES_BACKSTAGE_H
#define AURA_MODES_BACKSTAGE_H

#include <Arduino.h>

namespace Backstage {
  void process(unsigned long currentTime);
}

#endif // AURA_MODES_BACKSTAGE_H
