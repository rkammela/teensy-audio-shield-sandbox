#include "Welcome.h"

#include <FastLED.h>
#include "Config.h"
#include "AuraState.h"
#include "LedMatrix.h"
#include "SynthVoices.h"

namespace Welcome {

void process(unsigned long currentTime) {
  // LED + audio welcome screen. No sensors.
  //   AUDIO: heartbeat "lub-dub" (kick drum, synced to the visual heart
  //          pulse) layered over a slow ambient arpeggio on the string
  //          voice cycling through Am - F - C - G.
  //   LEFT : pulsing red heart, centered vertically (drawn at rows 2..6,
  //          cols 1..6) on an otherwise dark matrix.
  //   RIGHT: rainbow "TSA NATIONALS 2026" scrolling right-to-left.
  // The visual loop is gated to 80 ms; audio triggers run every call so
  // beat timing stays tight.

  // ---- AUDIO: heartbeat (kick) ----
  // Two kick thumps per 1400 ms cycle. "lub" fires at phase ~200..280 ms
  // (brightness rising toward peak); "dub" at phase ~400..480 ms (just
  // past peak). cycle-number tracking guarantees each thump only fires
  // once per cycle even when this function is called many times per ms.
  const uint32_t HEART_PERIOD = 1400;
  uint32_t cycle   = currentTime / HEART_PERIOD;
  uint32_t phaseMs = currentTime % HEART_PERIOD;
  static uint32_t lastLubCycle = UINT32_MAX;
  static uint32_t lastDubCycle = UINT32_MAX;
  if (cycle != lastLubCycle && phaseMs >= 200 && phaseMs < 280) {
    kickDrum.noteOn();
    lastLubCycle = cycle;
  }
  if (cycle != lastDubCycle && phaseMs >= 400 && phaseMs < 480) {
    kickDrum.noteOn();
    lastDubCycle = cycle;
  }

  // ---- AUDIO: ambient arpeggio (string voice) ----
  // Four chords (Am, F, C, G) x four ascending notes each, one note every
  // 400 ms. Notes overlap because the string envelope's decay/release
  // tails are longer than 400 ms, producing an evolving pad texture
  // underneath the heartbeat. Full loop = 6.4 s.
  static const float arpNotes[16] = {
    220.00f, 261.63f, 329.63f, 440.00f,  // Am: A3 C4 E4 A4
    174.61f, 220.00f, 261.63f, 349.23f,  // F : F3 A3 C4 F4
    130.81f, 164.81f, 196.00f, 261.63f,  // C : C3 E3 G3 C4
    196.00f, 246.94f, 293.66f, 392.00f   // G : G3 B3 D4 G4
  };
  static uint32_t lastArpTime = 0;
  static uint8_t  arpStep     = 0;
  if (currentTime - lastArpTime >= 400) {
    lastArpTime = currentTime;
    stringVoice.noteOn(arpNotes[arpStep], 0.35f);
    stringEnvelope.noteOn();
    arpStep = (arpStep + 1) % 16;
  }

  // ---- VISUAL (gated to 80 ms) ----
  static uint32_t lastFrame = 0;
  static uint16_t scrollPos = 0;
  if (currentTime - lastFrame < 80) return;
  lastFrame = currentTime;
  scrollPos++;

  // ---- LEFT MATRIX: pulsing heart ----

  // Heart pulse phase (~1.4 s period). Brightness ramps 35..105 dim red.
  float phase = (currentTime % 1400) / 1400.0f;
  float s = (sinf(phase * 2.0f * PI) + 1.0f) * 0.5f;
  uint8_t heartBright = 35 + (uint8_t)(70.0f * s);

  // 6-wide x 5-tall heart shape. Centered vertically by drawing at row
  // offset +2 (rows 2..6) -- that leaves 2 dark rows above and 1 below,
  // which looks visually centered because the heart tapers downward.
  static const uint8_t heart[5][6] = {
    {1,1,0,0,1,1},
    {1,1,1,1,1,1},
    {1,1,1,1,1,1},
    {0,1,1,1,1,0},
    {0,0,1,1,0,0}
  };

  // Blank the left matrix, then draw the heart.
  LedMatrix::clearMatrix(leds, NUM_LEDS);
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 6; col++) {
      if (heart[row][col]) {
        leds[LedMatrix::xyToLEDIndex(row + 2, col + 1)] = CRGB(heartBright, 0, 0);
      }
    }
  }

  // ---- RIGHT MATRIX: rainbow "TSA NATIONALS 2026" scroll ----
  // 5-wide x 7-tall column-major font. Bit 0 = top row of glyph.
  // Indices:  0=A  1=N  2=T  3=S  4=I  5=O  6=L  7=space  8=2  9=0  10=6
  static const uint8_t font[][5] = {
    {0x7E, 0x09, 0x09, 0x09, 0x7E},  // 0:  A
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // 1:  N
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // 2:  T
    {0x46, 0x49, 0x49, 0x49, 0x31},  // 3:  S
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // 4:  I
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // 5:  O
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // 6:  L
    {0x00, 0x00, 0x00, 0x00, 0x00},  // 7:  space
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 8:  2
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 9:  0
    {0x3E, 0x49, 0x49, 0x49, 0x30}   // 10: 6
  };
  // "TSA NATIONALS 2026" -> font indices
  static const uint8_t msgIdx[] = {2,3,0,7,1,0,2,4,5,1,0,6,3,7,8,9,8,10};
  const int MSG_CHARS_R = sizeof(msgIdx);       // 18
  const int CHAR_W_R    = 6;                    // 5 glyph cols + 1 spacer
  const int TEXT_LEN_R  = MSG_CHARS_R * CHAR_W_R;  // 108
  const int LOOP_LEN_R  = TEXT_LEN_R + 16;         // gap between loops
  int textScrollR = scrollPos % LOOP_LEN_R;

  for (int c = 0; c < 8; c++) {
    int physCol = 7 - c;  // right matrix is physically mirrored
    int textCol = (textScrollR + physCol) % LOOP_LEN_R;
    uint8_t colBits = 0;
    if (textCol < TEXT_LEN_R) {
      int charIdx   = textCol / CHAR_W_R;
      int colInChar = textCol % CHAR_W_R;
      if (colInChar < 5) colBits = font[msgIdx[charIdx]][colInChar];
    }
    uint8_t hue = (uint8_t)(textCol * 5);
    CRGB on = CHSV(hue, 255, 200);
    for (int row = 0; row < 8; row++) {
      bool lit = (row < 7) && ((colBits >> row) & 1);
      leds_r[LedMatrix::xyToLEDIndex(row, c)] = lit ? on : CRGB::Black;
    }
  }

  FastLED.show();
}

} // namespace Welcome
