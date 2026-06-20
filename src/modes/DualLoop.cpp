#include "DualLoop.h"

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"
#include "AuraState.h"
#include "MusicNotes.h"
#include "SynthVoices.h"

namespace {
  // Per-mode private state. Lifted out of main.cpp's global block; only
  // DualLoop::process() touches these now.
  bool dualLoopMelody[8][8] = {false};
  bool dualLoopDrums[8][8]  = {false};
  bool dualLoopTouchL[8][8] = {false};
  bool dualLoopTouchR[8][8] = {false};
  unsigned long dualLoopTouchTimeL[8][8] = {0};
  unsigned long dualLoopTouchTimeR[8][8] = {0};
  int   dualLoopStep      = 0;
  unsigned long dualLoopLastStep = 0;
  float dualLoopTempo     = 180.0f;  // ms per step
}

namespace DualLoop {

void enter() {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      dualLoopMelody[r][c]     = false;
      dualLoopDrums[r][c]      = false;
      dualLoopTouchL[r][c]     = false;
      dualLoopTouchR[r][c]     = false;
      dualLoopTouchTimeL[r][c] = 0;
      dualLoopTouchTimeR[r][c] = 0;
    }
  }
  dualLoopStep     = 0;
  dualLoopLastStep = 0;
}

void clear() {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      dualLoopMelody[r][c] = false;
      dualLoopDrums[r][c]  = false;
    }
  }
  dualLoopStep = 0;
}

void process() {
  // DUAL LOOP: Left hand = melody grid, Right hand = drum grid
  // Both auto-loop in sync on same 8-step timeline
  // Build a complete song layer by layer!

  unsigned long currentTime = millis();

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- LEFT HAND: Toggle melody notes ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = dualLoopTouchL[row][col];

      if (isTouched && !wasTouched && (currentTime - dualLoopTouchTimeL[row][col]) > 150) {
        dualLoopMelody[col][row] = !dualLoopMelody[col][row];  // col=step, row=note
        dualLoopTouchTimeL[row][col] = currentTime;
      }
      dualLoopTouchL[row][col] = isTouched;
    }
  }

  // --- RIGHT HAND: Toggle drum hits ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch1[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = dualLoopTouchR[row][col];

      if (isTouched && !wasTouched && (currentTime - dualLoopTouchTimeR[row][col]) > 150) {
        dualLoopDrums[col][row] = !dualLoopDrums[col][row];  // col=step, row=drum
        dualLoopTouchTimeR[row][col] = currentTime;
      }
      dualLoopTouchR[row][col] = isTouched;
    }
  }

  // --- AUTO-ADVANCE STEP ---
  if (currentTime - dualLoopLastStep >= (unsigned long)dualLoopTempo) {
    dualLoopLastStep = currentTime;

    // Play melody notes at current step (left hand grid)
    for (int note = 0; note < 8; note++) {
      if (dualLoopMelody[dualLoopStep][note]) {
        float freq = MusicNotes::midiToFreq(MusicNotes::MAJOR_SCALE_C4[note]);
        stringFilter.frequency(freq * 2.5);
        stringFilter.resonance(1.2);
        stringVoice.noteOn(freq, 0.8);
        stringEnvelope.noteOn();
        noteActive = true;
        mixer1.gain(0, 0.7 * masterVolume);
      }
    }

    // Play drum hits at current step (right hand grid)
    // Row 0-1: Kick, Row 2-3: Snare, Row 4-5: Hi-hat, Row 6-7: Percussion
    for (int drum = 0; drum < 8; drum++) {
      if (dualLoopDrums[dualLoopStep][drum]) {
        if (drum < 2) {
          kickDrum.frequency(drum == 0 ? 80 : 60);
          kickDrum.noteOn();
          mixer1.gain(1, 0.8 * masterVolume);
        } else if (drum < 4) {
          snareDrum.frequency(drum == 2 ? 200 : 280);
          snareDrum.noteOn();
          mixer1.gain(2, 0.7 * masterVolume);
        } else if (drum < 6) {
          hatEnvelope.noteOn();
          mixer1.gain(3, (drum == 4 ? 0.4 : 0.3) * masterVolume);
        } else {
          // Extra percussion: use snare at higher freq
          snareDrum.frequency(drum == 6 ? 400 : 500);
          snareDrum.noteOn();
          mixer1.gain(2, 0.5 * masterVolume);
        }
      }
    }

    dualLoopStep = (dualLoopStep + 1) % 8;
  }

  // --- LED VISUALIZATION ---
  // Left matrix = melody pattern, Right matrix = drum pattern
  if (ledVisualizationEnabled) {
    clearAllLEDs();

    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        bool melodyActive = dualLoopMelody[col][row];
        bool drumActive   = dualLoopDrums[col][row];

        // Left matrix: melody
        if (col == dualLoopStep) {
          if (melodyActive) {
            leds[ledIndex] = CRGB(0, 255, 100);  // Bright green
          } else {
            leds[ledIndex] = CRGB(20, 20, 20);   // Dim playhead
          }
        } else if (melodyActive) {
          uint8_t hue = 80 + (row * 10);
          leds[ledIndex] = CHSV(hue, 255, 120);
        }

        // Right matrix: drums
        if (col == dualLoopStep) {
          if (drumActive) {
            leds_r[ledIndex] = CRGB(255, 100, 0); // Bright orange
          } else {
            leds_r[ledIndex] = CRGB(20, 20, 20);  // Dim playhead
          }
        } else if (drumActive) {
          uint8_t hue = (row * 32);
          leds_r[ledIndex] = CHSV(hue, 255, 120);
        }
      }
    }

    FastLED.show();
  }
}

} // namespace DualLoop
