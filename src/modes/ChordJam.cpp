#include "ChordJam.h"

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"
#include "AuraState.h"
#include "MusicNotes.h"
#include "SynthVoices.h"
#include "ToFGrid.h"

namespace {
  // Per-mode private state.
  int           chordJamIndex     = 0;  // 0=I, 1=IV, 2=V, 3=vi
  unsigned long chordJamLastStrum = 0;
}

namespace ChordJam {

void enter() {
  chordJamIndex     = 0;
  chordJamLastStrum = 0;
}

void clear() {
  chordJamIndex = 0;
}

void process() {
  // CHORD JAM (guitar model):
  //   Left sensor  = FRETBOARD. 4 "strings", each = a pair of LED rows
  //     (rows 0-1, 2-3, 4-5, 6-7). A string is FRETTED when any cell in its
  //     2-row strip is touched; the whole strip lights up in that string's color.
  //   Right sensor = SOUNDHOLE. A lateral swipe ("strum") plays the next
  //     fretted string in the swipe direction. Holding multiple strings and
  //     strumming several times in a row = arpeggiated chord (Karplus-Strong
  //     decay tails overlap so it sounds like a chord, not a sequence).
  //   No fretted strings = muted, just like palming the strings on a real guitar.
  // String pitches come from MusicNotes::GUITAR_OPEN_TUNING_TOP4 (E3, A3, D4, G4).
  static const CRGB stringColors[4] = {
    CRGB(255,  60,  40),  // string 0 - red    (E)
    CRGB(255, 180,   0),  // string 1 - amber  (A)
    CRGB( 40, 255,  80),  // string 2 - green  (D)
    CRGB( 60, 140, 255),  // string 3 - blue   (G)
  };

  // Strum is now a VERTICAL motion: how fast the hand moves up/down over the
  // soundhole sensor. Moving DOWN toward the sensor (distance shrinking) is a
  // downstroke; moving UP away (distance growing) is an upstroke.
  const float STRUM_VELOCITY_THRESHOLD = 25.0;  // mm change per frame to count

  static float prevStrumDist = -1.0f;
  static int   lastStrummedString = -1;

  unsigned long currentTime = millis();
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- LEFT HAND: pick a single string = the row-pair the hand has
  // REACHED FURTHEST across the fretboard. Strings are laid out from the
  // player's POV: string 0 (low E) at the far side (rows 6-7), string 3
  // (high G) at the near side (rows 0-1). Hovering over the near rows alone
  // selects string 0; reaching further selects strings 1, 2, 3 in turn.
  bool stringFretted[4] = {false, false, false, false};
  int highestFretted = -1;
  for (int s = 0; s < 4; s++) {
    int row1 = (3 - s) * 2;       // string 0 -> rows 6,7 ; string 3 -> rows 0,1
    int row2 = (3 - s) * 2 + 1;
    for (int col = 0; col < 8; col++) {
      uint16_t d1 = distanceGrid_ch0[row1][col];
      uint16_t d2 = distanceGrid_ch0[row2][col];
      if ((d1 > 50 && d1 < GRID_ZONE_THRESHOLD) ||
          (d2 > 50 && d2 < GRID_ZONE_THRESHOLD)) {
        highestFretted = s;  // keep overwriting; final value = string reached furthest
        break;
      }
    }
  }
  if (highestFretted >= 0) stringFretted[highestFretted] = true;

  // --- RIGHT HAND: detect strum (vertical motion velocity) ---
  float rightAvgDist = 0;
  int rightCentroidX = 0, rightCentroidY = 0, rightZones = 0;
  ToFGrid::calculateHandMetrics(distanceGrid_ch1, rightAvgDist, rightCentroidX, rightCentroidY, rightZones);

  bool strum = false;
  int strumDir = +1;  // +1 = downstroke (hand moving toward sensor)
  if (rightZones > 0) {
    if (prevStrumDist < 0) {
      strum = true;          // hand just arrived = automatic single pluck
      strumDir = +1;
    } else {
      float dd = rightAvgDist - prevStrumDist;  // +ve = hand rising, -ve = falling
      if (dd <= -STRUM_VELOCITY_THRESHOLD && (currentTime - chordJamLastStrum) > 100) {
        strum = true;  strumDir = +1;   // downstroke (hand swinging down)
      } else if (dd >= STRUM_VELOCITY_THRESHOLD && (currentTime - chordJamLastStrum) > 100) {
        strum = true;  strumDir = -1;   // upstroke (hand swinging up)
      }
    }
    prevStrumDist = rightAvgDist;
  } else {
    prevStrumDist = -1.0f;
  }

  // --- Fire next fretted string in the strum direction ---
  if (strum) {
    int playString = -1;
    if (lastStrummedString < 0) {
      int start = (strumDir > 0) ? 0 : 3;
      for (int i = 0; i < 4; i++) {
        int s = (strumDir > 0) ? (start + i) : (start - i);
        if (stringFretted[s]) { playString = s; break; }
      }
    } else {
      for (int i = 1; i <= 4; i++) {
        int s = ((lastStrummedString + strumDir * i) % 4 + 4) % 4;
        if (stringFretted[s]) { playString = s; break; }
      }
    }

    if (playString >= 0) {
      float freq = MusicNotes::midiToFreq(MusicNotes::GUITAR_OPEN_TUNING_TOP4[playString]);
      float coverage = (float)rightZones / 64.0;
      float velocity = constrain(0.4 + coverage * 0.6, 0.3, 1.0);
      stringFilter.frequency(freq * 3.0);
      stringFilter.resonance(2.0);
      stringVoice.noteOn(freq, velocity);
      stringEnvelope.noteOn();
      mixer1.gain(0, 0.7 * masterVolume);
      noteActive = true;
      lastStrummedString = playString;
      chordJamIndex = playString;
      chordJamLastStrum = currentTime;
    }
  }

  // --- LED VISUALIZATION ---
  if (ledVisualizationEnabled) {
    clearAllLEDs();

    // LEFT matrix: 4 strings as 2-row strips.
    for (int s = 0; s < 4; s++) {
      CRGB color = stringColors[s];
      if (!stringFretted[s]) color.nscale8(25);  // dim baseline
      if (lastStrummedString == s && (currentTime - chordJamLastStrum) < 250) {
        uint8_t flash = 255 - ((currentTime - chordJamLastStrum) * 255 / 250);
        color = stringColors[s];
        color.r = qadd8(color.r, flash);
        color.g = qadd8(color.g, flash / 2);
        color.b = qadd8(color.b, flash / 2);
      }
      int row1 = (3 - s) * 2, row2 = (3 - s) * 2 + 1;
      for (int col = 0; col < 8; col++) {
        leds[row1 * 8 + col] = color;
        leds[row2 * 8 + col] = color;
      }
    }

    // RIGHT matrix: yellow horizontal BAR tracks the strum hand's height.
    int strumFlash = 0;
    if (currentTime - chordJamLastStrum < 200) {
      strumFlash = 255 - ((currentTime - chordJamLastStrum) * 255 / 200);
    }
    int strumRow = -1;
    if (rightZones > 0) {
      float clamped = constrain(rightAvgDist, 60.0f, 400.0f);
      float t = (clamped - 60.0f) / (400.0f - 60.0f);
      strumRow = constrain((int)(t * 7.99f), 0, 7);
    }
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (row == strumRow) {
          leds_r[ledIndex] = CRGB(220, 220, 80);
        } else if (strumFlash > 0) {
          leds_r[ledIndex] = CRGB(strumFlash / 4, strumFlash / 6, 0);
        } else {
          leds_r[ledIndex] = CRGB(8, 6, 0);
        }
      }
    }
    FastLED.show();
  }
}

} // namespace ChordJam
