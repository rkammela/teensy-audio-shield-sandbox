#include "Battle.h"

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"
#include "AuraState.h"
#include "MusicNotes.h"
#include "SynthVoices.h"

namespace {
  // Per-mode private state.
  float battleFreq[2]  = {440.0f, 440.0f};
  float battleVol[2]   = {0.0f,   0.0f};
  int   battleScore[2] = {0, 0};
}

namespace Battle {

void enter() {
  battleScore[0] = 0;
  battleScore[1] = 0;
  battleVol[0]   = 0;
  battleVol[1]   = 0;
}

void clear() {
  battleScore[0] = 0;
  battleScore[1] = 0;
}

void process() {
  // BATTLE MODE: Two players, each controls one sensor
  // Player 1 = CH0, Player 2 = CH1
  // Each plays notes, LEDs show who's louder/more active

  unsigned long currentTime = millis();
  (void)currentTime;

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // Both players use the same C major scale (C4..C5) from MusicNotes.

  // --- PLAYER 1 (CH0): Play on string voice ---
  int p1Row = -1;
  float p1Dist = 9999;
  int p1Zones = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t d = distanceGrid_ch0[row][col];
      if (d < GRID_ZONE_THRESHOLD && d > 50) {
        p1Zones++;
        if (d < p1Dist) { p1Dist = d; p1Row = row; }
      }
    }
  }

  if (p1Row >= 0) {
    float freq = MusicNotes::midiToFreq(MusicNotes::MAJOR_SCALE_C4[p1Row]);
    float vel = constrain(1.0 - (p1Dist / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
    battleFreq[0] = freq;
    battleVol[0] = vel;
    stringFilter.frequency(freq * 2.5);
    stringVoice.noteOn(freq, vel);
    stringEnvelope.noteOn();
    noteActive = true;
    mixer1.gain(0, vel * masterVolume);
  } else {
    battleVol[0] *= 0.9;
  }

  // --- PLAYER 2 (CH1): Play on drum voice (different timbre) ---
  int p2Row = -1;
  float p2Dist = 9999;
  int p2Zones = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t d = distanceGrid_ch1[row][col];
      if (d < GRID_ZONE_THRESHOLD && d > 50) {
        p2Zones++;
        if (d < p2Dist) { p2Dist = d; p2Row = row; }
      }
    }
  }

  if (p2Row >= 0) {
    float freq = MusicNotes::midiToFreq(MusicNotes::MAJOR_SCALE_C4[p2Row]);
    float vel = constrain(1.0 - (p2Dist / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
    battleFreq[1] = freq;
    battleVol[1] = vel;
    snareDrum.frequency(freq);
    snareDrum.noteOn();
    mixer1.gain(2, vel * masterVolume);
  } else {
    battleVol[1] *= 0.9;
  }

  // --- Score: who's more active ---
  if (p1Zones > p2Zones + 3) battleScore[0] = min(battleScore[0] + 1, 64);
  else if (p2Zones > p1Zones + 3) battleScore[1] = min(battleScore[1] + 1, 64);

  // --- LED VISUALIZATION ---
  if (ledVisualizationEnabled) {
    clearAllLEDs();
    int p1Bar = constrain(battleScore[0] / 8, 0, 7);
    int p2Bar = constrain(battleScore[1] / 8, 0, 7);

    for (int row = 0; row < 8; row++) {
      // Left matrix = Player 1 (blue)
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (p1Row >= 0 && row == p1Row) {
          leds[ledIndex] = CRGB(0, 100, 255);
        } else if (row <= p1Bar) {
          leds[ledIndex] = CRGB(0, 20, 60);
        }
      }
      // Right matrix = Player 2 (red)
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (p2Row >= 0 && row == p2Row) {
          leds_r[ledIndex] = CRGB(255, 50, 0);
        } else if (row <= p2Bar) {
          leds_r[ledIndex] = CRGB(60, 10, 0);
        }
      }
    }
    FastLED.show();
  }
}

} // namespace Battle
