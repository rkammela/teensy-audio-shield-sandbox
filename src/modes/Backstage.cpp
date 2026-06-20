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

void processAnimation(unsigned long currentTime) {
  // LED-only animation. No sensors used. Left matrix = small pulsing
  // heart with an EKG trace scrolling through its middle row. Right
  // matrix = rainbow "AURA TSA 2026" text scrolling right-to-left.
  // Both animations share an 80 ms tick so the EKG and text scroll
  // together. Right matrix is physically column-mirrored, so its
  // column index is flipped at render time.
  static uint32_t lastFrame = 0;
  static uint16_t scrollPos = 0;
  if (currentTime - lastFrame < 80) return;
  lastFrame = currentTime;
  scrollPos++;

  // ---- LEFT: small heart (6x5) centered + scrolling EKG ----
  static const uint8_t heart[5][6] = {
    {1,1,0,0,1,1},
    {1,1,1,1,1,1},
    {1,1,1,1,1,1},
    {0,1,1,1,1,0},
    {0,0,1,1,0,0}
  };
  // PQRST waveform sampled every column. Lower row = higher up on
  // the matrix. Baseline = 4, P=3, Q=5, R=1 (spike), S=6, T=3.
  static const int8_t ekgPattern[16] =
    {4,4,3,3,4,5,1,6,4,3,3,4,4,4,4,4};
  const int EKG_LEN = 16;

  float phase = (currentTime % 1400) / 1400.0f;
  float s = (sin(phase * 2.0f * PI) + 1.0f) * 0.5f;
  uint8_t heartBright = 35 + (uint8_t)(55.0f * s);  // 35..90 dim red

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      bool isHeart = (row >= 2 && row <= 6 && col >= 1 && col <= 6)
                       && heart[row - 2][col - 1];
      leds[LedMatrix::xyToLEDIndex(row, col)] =
        isHeart ? CRGB(heartBright, 0, 0) : CRGB::Black;
    }
  }
  // Overlay EKG as connected vertical segments; rightmost col is
  // newest sample (brightest), older cols fade toward the left.
  for (int c = 0; c < 8; c++) {
    int patternCol = (scrollPos + c) % EKG_LEN;
    int prevCol    = (patternCol + EKG_LEN - 1) % EKG_LEN;
    int waveRow = ekgPattern[patternCol];
    int prevRow = ekgPattern[prevCol];
    uint8_t b = 50 + c * 29;  // 50..253
    CRGB color = CRGB(b, b / 6, b / 6);
    int r1 = min(prevRow, waveRow);
    int r2 = max(prevRow, waveRow);
    for (int r = r1; r <= r2; r++) {
      leds[LedMatrix::xyToLEDIndex(r, c)] = color;
    }
  }

  // ---- RIGHT: rainbow scrolling "AURA TSA 2026" ----
  // 5-wide x 7-tall column-major font. Bit 0 = top row of glyph.
  static const uint8_t font[][5] = {
    {0x7E, 0x09, 0x09, 0x09, 0x7E},  // 0: A
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // 1: U
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // 2: R
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // 3: T
    {0x46, 0x49, 0x49, 0x49, 0x31},  // 4: S
    {0x00, 0x00, 0x00, 0x00, 0x00},  // 5: space
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 6: 2
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 7: 0
    {0x3E, 0x49, 0x49, 0x49, 0x30}   // 8: 6
  };
  // Message "AURA TSA 2026" -> font indices
  static const uint8_t msgIdx[] = {0,1,2,0,5,3,4,0,5,6,7,6,8};
  const int MSG_CHARS = 13;
  const int CHAR_W   = 6;                   // 5 glyph cols + 1 spacer
  const int TEXT_LEN = MSG_CHARS * CHAR_W;  // 78
  const int LOOP_LEN = TEXT_LEN + 16;       // blank gap between loops
  int textScroll = scrollPos % LOOP_LEN;

  for (int c = 0; c < 8; c++) {
    int physCol = 7 - c;  // right matrix is physically mirrored
    int textCol = (textScroll + physCol) % LOOP_LEN;
    uint8_t colBits = 0;
    if (textCol < TEXT_LEN) {
      int charIdx   = textCol / CHAR_W;
      int colInChar = textCol % CHAR_W;
      if (colInChar < 5) colBits = font[msgIdx[charIdx]][colInChar];
    }
    uint8_t hue = (uint8_t)(textCol * 8);
    CRGB on = CHSV(hue, 255, 200);
    for (int row = 0; row < 8; row++) {
      bool lit = (row < 7) && ((colBits >> row) & 1);
      leds_r[LedMatrix::xyToLEDIndex(row, c)] = lit ? on : CRGB::Black;
    }
  }

  FastLED.show();
}

} // namespace Backstage
