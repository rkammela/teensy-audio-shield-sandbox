/*
 * Backstage modes - diagnostic + LED-art views (no audio).
 *
 *   Backstage::process(currentTime)          - dual-sensor distance heatmap
 *   Backstage::processAnimation(currentTime) - LED-only pulsing heart + EKG
 *                                              on the left matrix, rainbow
 *                                              "AURA TSA 2026" text scroll
 *                                              on the right matrix
 *
 * Both are called from main.cpp's loop() when the matching mode is active.
 */

#ifndef AURA_MODES_BACKSTAGE_H
#define AURA_MODES_BACKSTAGE_H

#include <Arduino.h>

namespace Backstage {
  void process(unsigned long currentTime);
  void processAnimation(unsigned long currentTime);
}

#endif // AURA_MODES_BACKSTAGE_H
