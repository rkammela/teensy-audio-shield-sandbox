#include "BassMachine.h"

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"
#include "AuraState.h"
#include "MusicNotes.h"
#include "SynthVoices.h"

namespace {
  // Per-mode private state.
  bool          bassGrid[8][8]       = {false};
  bool          bassTouchState[8][8] = {false};
  unsigned long bassTouchTime[8][8]  = {0};
  int           bassStep             = 0;
  unsigned long bassLastStep         = 0;
  float         bassTempo            = 200.0f;  // ms per step
  float         bassFilterFreq       = 400.0f;  // Hz, swept by hand distance
}

namespace BassMachine {

void enter() {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      bassGrid[r][c]       = false;
      bassTouchState[r][c] = false;
      bassTouchTime[r][c]  = 0;
    }
  }
  bassStep       = 0;
  bassLastStep   = 0;
  bassFilterFreq = 400.0f;
}

void clear() {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) bassGrid[r][c] = false;
  }
  bassStep = 0;
}

void process() {
  // BASS MACHINE: Left hand = toggle bass notes in loop, Right hand = filter sweep
  // Bass notes come from MusicNotes::MAJOR_SCALE_C2 (C2..C3).

  unsigned long currentTime = millis();

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- LEFT HAND: Toggle bass notes in 8-step loop ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = bassTouchState[row][col];
      if (isTouched && !wasTouched && (currentTime - bassTouchTime[row][col]) > 150) {
        bassGrid[col][row] = !bassGrid[col][row];
        bassTouchTime[row][col] = currentTime;
      }
      bassTouchState[row][col] = isTouched;
    }
  }

  // --- RIGHT HAND: Filter sweep (wah) ---
  float rightAvg = 0;
  int rightCount = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t d = distanceGrid_ch1[row][col];
      if (d < GRID_ZONE_THRESHOLD && d > 50) {
        rightAvg += d;
        rightCount++;
      }
    }
  }
  if (rightCount > 3) {
    rightAvg /= rightCount;
    bassFilterFreq = constrain(2000.0 - (rightAvg * 4.0), 100.0, 2000.0);
  } else {
    bassFilterFreq = 400.0;
  }

  // --- AUTO-ADVANCE STEP ---
  if (currentTime - bassLastStep >= (unsigned long)bassTempo) {
    bassLastStep = currentTime;

    for (int note = 0; note < 8; note++) {
      if (bassGrid[bassStep][note]) {
        float freq = MusicNotes::midiToFreq(MusicNotes::MAJOR_SCALE_C2[note]);
        stringFilter.frequency(bassFilterFreq);
        stringFilter.resonance(3.0);
        stringVoice.noteOn(freq, 0.9);
        stringEnvelope.noteOn();
        noteActive = true;
        mixer1.gain(0, 0.8 * masterVolume);
      }
    }
    bassStep = (bassStep + 1) % 8;
  }

  // --- LED VISUALIZATION ---
  if (ledVisualizationEnabled) {
    clearAllLEDs();
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (col == bassStep) {
          if (bassGrid[col][row]) {
            leds[ledIndex] = CRGB::White;
          } else {
            leds[ledIndex] = CRGB(20, 5, 0);
          }
        } else if (bassGrid[col][row]) {
          uint8_t brightness = 100 + (int)(bassFilterFreq / 20.0);
          leds[ledIndex] = CRGB(brightness, brightness / 4, 0);
        }
      }
    }
    // Right matrix: stable 4-level "reach" bar with anti-flicker.
    static int displayedPair  = 0;
    static int targetPair     = 0;
    static int candidatePair  = 0;
    static int candidateFrames = 0;
    static uint32_t lastPairStep = 0;
    const int STABILITY_FRAMES = 3;
    const uint32_t STEP_INTERVAL_MS = 50;

    int rawPair = 0;
    for (int p = 0; p < 4; p++) {
      int row1 = 6 - p * 2;
      int row2 = row1 + 1;
      int cellsInPair = 0;
      for (int col = 0; col < 8; col++) {
        uint16_t d1 = distanceGrid_ch1[row1][col];
        uint16_t d2 = distanceGrid_ch1[row2][col];
        if (d1 > 50 && d1 < GRID_ZONE_THRESHOLD) cellsInPair++;
        if (d2 > 50 && d2 < GRID_ZONE_THRESHOLD) cellsInPair++;
      }
      if (cellsInPair >= 2) rawPair = p;
    }

    if (rawPair == candidatePair) {
      if (candidateFrames < STABILITY_FRAMES) candidateFrames++;
      if (candidateFrames >= STABILITY_FRAMES) targetPair = candidatePair;
    } else {
      candidatePair = rawPair;
      candidateFrames = 1;
    }

    if (currentTime - lastPairStep >= STEP_INTERVAL_MS) {
      if (displayedPair < targetPair) { displayedPair++; lastPairStep = currentTime; }
      else if (displayedPair > targetPair) { displayedPair--; lastPairStep = currentTime; }
    }

    int rowsLit = (displayedPair + 1) * 2;
    for (int row = 8 - rowsLit; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        uint8_t hue = map(row, 0, 7, 0, 40);
        leds_r[ledIndex] = CHSV(hue, 255, 180);
      }
    }
    FastLED.show();
  }
}

} // namespace BassMachine
