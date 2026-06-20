/*
 * AURA - Touchless Musical Instrument
 *
 * Hardware:
 *   - Teensy 4.0 + SGTL5000 audio shield (I2S out to headphones / line out)
 *   - 2x SparkFun VL53L5CX time-of-flight sensors (8x8 zones each)
 *   - 2x WS2812B 8x8 NeoPixel matrices (one per hand, serpentine wiring)
 *   - 1x SSD1306 128x64 OLED status display
 *   - 1x rotary encoder + push button for mode select
 *
 * Modes (cycled with the encoder):
 *   BACKSTAGE     - diagnostic: live sensor grid on both LED matrices
 *   BACKSTAGE 2   - LED-only animation: pulsing heart + scrolling marquee
 *   CHORD JAM     - left hand picks chord, right hand strums
 *   DUAL LOOP     - 8-step looper, left = melody, right = drums
 *   BASS MACHINE  - 8-step bass sequencer with right-hand filter sweep
 *   BATTLE MODE   - two-player duel (P1 = CH0, P2 = CH1)
 */

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Encoder.h>

#include "Config.h"
#include "MusicNotes.h"

// ============================================================================
// AUDIO OBJECT DECLARATIONS
// ============================================================================

// String voice (Karplus-Strong)
AudioSynthKarplusStrong  stringVoice;
AudioEffectEnvelope      stringEnvelope;
AudioFilterStateVariable stringFilter;

// Drum synthesizers
AudioSynthSimpleDrum     kickDrum;
AudioSynthSimpleDrum     snareDrum;

// Hi-hat: noise + envelope
AudioSynthNoiseWhite     noiseWhite;
AudioEffectEnvelope      hatEnvelope;

// Mixer and output
AudioMixer4              mixer1;         // Main mixer
AudioOutputI2S           i2s1;           // I2S output (DAC)

// Audio shield control
AudioControlSGTL5000     sgtl5000_1;

// ============================================================================
// AUDIO CONNECTIONS (Patch Cords)
// ============================================================================

// String voice: stringVoice -> envelope -> filter -> mixer channel 0
AudioConnection          patchCord1(stringVoice, stringEnvelope);
AudioConnection          patchCord2(stringEnvelope, 0, stringFilter, 0);
AudioConnection          patchCord3(stringFilter, 0, mixer1, 0);

// Kick drum -> mixer channel 1
AudioConnection          patchCord4(kickDrum, 0, mixer1, 1);

// Snare drum -> mixer channel 2
AudioConnection          patchCord5(snareDrum, 0, mixer1, 2);

// Hi-hat: noise -> envelope -> mixer channel 3
AudioConnection          patchCord6(noiseWhite, hatEnvelope);
AudioConnection          patchCord7(hatEnvelope, 0, mixer1, 3);

// Mixer -> I2S output (both left and right channels)
AudioConnection          patchCord8(mixer1, 0, i2s1, 0);    // Left
AudioConnection          patchCord9(mixer1, 0, i2s1, 1);    // Right

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Play mode + master volume
PlayMode currentMode = MODE_BACKSTAGE;
float masterVolume = 1.0;

// DUAL LOOP: 8-step melody/drum sequencer driven by both hands.
bool dualLoopMelody[8][8] = {false};
bool dualLoopDrums[8][8]  = {false};
bool dualLoopTouchL[8][8] = {false};
bool dualLoopTouchR[8][8] = {false};
unsigned long dualLoopTouchTimeL[8][8] = {0};
unsigned long dualLoopTouchTimeR[8][8] = {0};
int  dualLoopStep = 0;
unsigned long dualLoopLastStep = 0;
float dualLoopTempo = 180.0;          // ms per step

// CHORD JAM: left hand picks the chord, right hand strums it.
int  chordJamIndex = 0;               // 0=I, 1=IV, 2=V, 3=vi
unsigned long chordJamLastStrum = 0;
bool chordJamTouchState[8][8] = {false};
unsigned long chordJamTouchTime[8][8] = {0};
float chordJamStrumVelocity = 0.0;

// BASS MACHINE: 8-step bass sequencer with a filter sweep on the right hand.
bool bassGrid[8][8]       = {false};
bool bassTouchState[8][8] = {false};
unsigned long bassTouchTime[8][8] = {0};
int  bassStep = 0;
unsigned long bassLastStep = 0;
float bassTempo      = 200.0;         // ms per step
float bassFilterFreq = 400.0;         // Hz, swept by hand distance

// BATTLE MODE: two players, two scores, two voices.
float battleFreq[2]  = {440.0, 440.0};
float battleVol[2]   = {0.0,   0.0};
int   battleScore[2] = {0, 0};

// VL53L5CX Distance Sensors (8x8 mode)
// CH0: I2C0 (Wire) - SDA=18, SCL=19
// CH1: I2C1 (Wire1) - SDA=17, SCL=16
SparkFun_VL53L5CX sensor_ch0;
SparkFun_VL53L5CX sensor_ch1;
bool sensor_ch0_initialized = false;
bool sensor_ch1_initialized = false;
uint16_t distanceGrid_ch0[8][8];  // 8x8 distance array for CH0
uint16_t distanceGrid_ch1[8][8];  // 8x8 distance array for CH1
unsigned long lastSensorRead = 0;
uint8_t currentSensorChannel = 0;  // 0 = CH0, 1 = CH1, 2 = BOTH

// LED matrices (pin assignments / dimensions live in Config.h).
// Serpentine layout: row 0 is left-to-right, row 1 is right-to-left, etc.
CRGB leds[NUM_LEDS];        // Left LED matrix array
CRGB leds_r[NUM_LEDS];      // Right LED matrix array

// Global LED visualization gate. Each mode checks this before lighting
// up its matrices so we can blank the panels for "stage" demos.
bool ledVisualizationEnabled = true;

// OLED display (geometry / I2C address live in Config.h).
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Rotary encoder (pin assignments live in Config.h).
Encoder knob(ENCODER_PIN_A, ENCODER_PIN_B);
long lastEncoderPosition = 0;
bool lastButtonState = HIGH;
unsigned long lastButtonPress = 0;

// True when a Karplus-Strong string voice is currently sustaining. Lets
// panicMute() know whether it actually needs to release the envelope.
bool noteActive = false;

// Main-loop timer for the fixed-rate sensor + mode update.
unsigned long lastUpdateTime = 0;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void setupAudio();
void processDualLoop();
void processChordJam();
void processBassMachine();
void processBattleMode();
void calculateHandMetrics(uint16_t grid[8][8], float& avgDistance, int& centroidX, int& centroidY, int& activeZones);
void panicMute();
bool initDistanceSensor(uint8_t channel);
void readDistanceGrid(uint8_t channel);
void setupLEDs();
void clearAllLEDs();
CRGB distanceToColor(uint16_t distance_mm);
int xyToLEDIndex(int row, int col);
void setupOLED();
void showLoadingScreen(const char* label, int step, int total);
void updateOLEDDisplay();
void handleEncoder();
void handleEncoderButton();
void switchToMode(PlayMode newMode);
const char* getModeString(PlayMode mode);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);

  // Wait a moment for serial to initialize
  delay(1000);

  Serial.println("\n=== Teensy Touchless Instrument (Simulated ToF) ===");

  // ---- Step 1/6: Audio engine ----
  // AudioMemory must be allocated before any AudioStream objects run.
  AudioMemory(20);
  sgtl5000_1.enable();
  sgtl5000_1.volume(masterVolume);
  mixer1.gain(0, 0.8 * masterVolume);  // String voice
  mixer1.gain(1, 0.8 * masterVolume);  // Kick drum
  mixer1.gain(2, 0.7 * masterVolume);  // Snare drum
  mixer1.gain(3, 0.5 * masterVolume);  // Hi-hat
  setupAudio();
  Serial.println("Audio system initialized!");

  // ---- Step 2/6: OLED (bring up early so we can show progress) ----
  setupOLED();
  Serial.println("OLED display initialized!");
  showLoadingScreen("Audio", 1, 6);
  delay(150);
  showLoadingScreen("OLED", 2, 6);
  delay(150);

  // ---- Step 3/6: LED matrices ----
  showLoadingScreen("LEDs", 3, 6);
  setupLEDs();
  Serial.println("LED system initialized!");
  delay(150);

  // ---- Step 4/6: Rotary encoder ----
  showLoadingScreen("Encoder", 4, 6);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  Serial.println("Rotary encoder initialized!");
  delay(150);

  // ---- Step 5/6: VL53L5CX CH0 (left hand sensor on Wire) ----
  showLoadingScreen("Sensor CH0", 5, 6);
  Serial.println("Initializing VL53L5CX CH0 (I2C0)...");
  sensor_ch0_initialized = initDistanceSensor(0);
  Serial.println(sensor_ch0_initialized ? "   CH0 initialization SUCCESS" : "   CH0 initialization FAILED");

  // ---- Step 6/6: VL53L5CX CH1 (right hand sensor on Wire1) ----
  showLoadingScreen("Sensor CH1", 6, 6);
  Serial.println("Initializing VL53L5CX CH1 (I2C1)...");
  sensor_ch1_initialized = initDistanceSensor(1);
  Serial.println(sensor_ch1_initialized ? "   CH1 initialization SUCCESS" : "   CH1 initialization FAILED");

  // Brief "Ready!" flash before normal UI takes over
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 0);
  display.println("AURA");
  display.setTextSize(2);
  const char* ready = "Ready!";
  int readyW = strlen(ready) * 12;  // size-2 font ~ 12 px per char
  display.setCursor((128 - readyW) / 2, 28);
  display.println(ready);
  display.display();
  delay(500);

  lastUpdateTime = millis();

  // Normal display takes over
  updateOLEDDisplay();
}


// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // Handle rotary encoder input
  handleEncoder();
  handleEncoderButton();

  // Update sensor simulation and audio at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL_MS) {
    lastUpdateTime = currentTime;

    // Dispatch to the active mode. Each branch is responsible for reading
    // whatever sensor data it needs and driving its own audio + LEDs.
    if (currentMode == MODE_DUAL_LOOP) {
      processDualLoop();
    } else if (currentMode == MODE_CHORD_JAM) {
      processChordJam();
    } else if (currentMode == MODE_BASS_MACHINE) {
      processBassMachine();
    } else if (currentMode == MODE_BATTLE_MODE) {
      processBattleMode();
    } else if (currentMode == MODE_BACKSTAGE) {
      // Diagnostic: render BOTH sensor grids simultaneously. CH0 -> LEFT
      // matrix, CH1 -> RIGHT matrix. Rotation is handled inside
      // readDistanceGrid(), so grid[row][col] already matches the user-facing
      // orientation of each LED matrix.
      if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
        lastSensorRead = currentTime;
        if (sensor_ch0_initialized) readDistanceGrid(0);
        if (sensor_ch1_initialized) readDistanceGrid(1);
        for (int row = 0; row < MATRIX_HEIGHT; row++) {
          for (int col = 0; col < MATRIX_WIDTH; col++) {
            int ledIndex = xyToLEDIndex(row, col);
            leds[ledIndex]   = sensor_ch0_initialized
                                 ? distanceToColor(distanceGrid_ch0[row][col])
                                 : CRGB::Black;
            leds_r[ledIndex] = sensor_ch1_initialized
                                 ? distanceToColor(distanceGrid_ch1[row][col])
                                 : CRGB::Black;
          }
        }
        FastLED.show();
      }
    } else if (currentMode == MODE_BACKSTAGE_2) {
      // LED-only animation. No sensors used. Left matrix = small pulsing
      // heart with an EKG trace scrolling through its middle row. Right
      // matrix = rainbow "AURA TSA 2026" text scrolling right-to-left.
      // Both animations share an 80 ms tick so the EKG and text scroll
      // together. Right matrix is physically column-mirrored, so its
      // column index is flipped at render time.
      static uint32_t lastFrame = 0;
      static uint16_t scrollPos = 0;
      if (currentTime - lastFrame >= 80) {
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
            leds[xyToLEDIndex(row, col)] =
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
            leds[xyToLEDIndex(r, c)] = color;
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
            leds_r[xyToLEDIndex(row, c)] = lit ? on : CRGB::Black;
          }
        }

        FastLED.show();
      }
    }
    // Drum mode is event-driven (no continuous update needed)
  }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================


void setupAudio() {
  // String voice setup
  stringEnvelope.attack(10.0);
  stringEnvelope.hold(20.0);
  stringEnvelope.decay(500.0);
  stringEnvelope.sustain(0.4);
  stringEnvelope.release(300.0);

  // String filter: lowpass with moderate resonance
  stringFilter.frequency(2000);
  stringFilter.resonance(1.2);
  stringFilter.octaveControl(2.0);

  // Kick drum configuration
  kickDrum.frequency(60);
  kickDrum.length(300);
  kickDrum.secondMix(0.0);
  kickDrum.pitchMod(0.55);

  // Snare drum configuration
  snareDrum.frequency(200);
  snareDrum.length(150);
  snareDrum.secondMix(0.5);
  snareDrum.pitchMod(0.3);

  // Hi-hat envelope: short, sharp attack and decay
  hatEnvelope.attack(1.0);
  hatEnvelope.hold(0.0);
  hatEnvelope.decay(50.0);
  hatEnvelope.sustain(0.0);
  hatEnvelope.release(50.0);

  // Start noise generator at low amplitude
  noiseWhite.amplitude(0.3);
}

// Kill any audio still playing. Called when switching modes and when the
// player presses the encoder button to "reset" the current mode.
void panicMute() {
  stringEnvelope.noteOff();
  hatEnvelope.noteOff();
  noteActive = false;
}

// Walk an 8x8 distance grid and report where the player's hand is and how
// much of the grid it covers. A "cell" counts as hand only when its distance
// is between 50 mm (too close = noise/sensor artifact) and 800 mm.
//   avgDistance  -> mean distance of the hand cells, in mm
//   centroidX/Y  -> mean (col, row) of the hand cells (0..7)
//   activeZones  -> how many cells passed the threshold
void calculateHandMetrics(uint16_t grid[8][8], float& avgDistance,
                          int& centroidX, int& centroidY, int& activeZones) {
  const int HAND_THRESHOLD = 800;  // mm

  long sumX = 0, sumY = 0, sumDist = 0;
  int count = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = grid[row][col];
      if (dist < HAND_THRESHOLD && dist > 50) {
        sumX    += col;
        sumY    += row;
        sumDist += dist;
        count++;
      }
    }
  }
  if (count > 0) {
    centroidX   = sumX / count;
    centroidY   = sumY / count;
    avgDistance = (float)sumDist / count;
    activeZones = count;
  } else {
    centroidX   = 4;
    centroidY   = 4;
    avgDistance = 2000.0;
    activeZones = 0;
  }
}

void processDualLoop() {
  // DUAL LOOP: Left hand = melody grid, Right hand = drum grid
  // Both auto-loop in sync on same 8-step timeline
  // Build a complete song layer by layer!

  unsigned long currentTime = millis();

  // Read both sensors
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
    // 8 rows = 8 notes of the C major scale (C4 to C5)
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
        bool drumActive = dualLoopDrums[col][row];

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

void processChordJam() {
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
  calculateHandMetrics(distanceGrid_ch1, rightAvgDist, rightCentroidX, rightCentroidY, rightZones);

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
      // No history: scan from the appropriate end so first strum picks the
      // bottom-most (or top-most) currently-fretted string.
      int start = (strumDir > 0) ? 0 : 3;
      for (int i = 0; i < 4; i++) {
        int s = (strumDir > 0) ? (start + i) : (start - i);
        if (stringFretted[s]) { playString = s; break; }
      }
    } else {
      // Walk to the next fretted string in the strum direction, wrapping around.
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

    // LEFT matrix: 4 strings as 2-row strips. Whole strip lit if fretted;
    // briefly flash the string that was just strummed.
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
      int row1 = (3 - s) * 2, row2 = (3 - s) * 2 + 1;  // flipped to match player POV
      for (int col = 0; col < 8; col++) {
        leds[row1 * 8 + col] = color;
        leds[row2 * 8 + col] = color;
      }
    }

    // RIGHT matrix: yellow horizontal BAR tracks the strum hand's height
    // (closer to sensor = lower row, farther = higher row). Whole matrix
    // flashes amber when a strum just fired.
    int strumFlash = 0;
    if (currentTime - chordJamLastStrum < 200) {
      strumFlash = 255 - ((currentTime - chordJamLastStrum) * 255 / 200);
    }
    // Map current right-hand distance to a row index 0..7
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
          leds_r[ledIndex] = CRGB(220, 220, 80);  // bright yellow strum bar
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

void processBassMachine() {
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
    // Map distance to filter frequency: closer = higher cutoff (bright), farther = low (dark)
    bassFilterFreq = constrain(2000.0 - (rightAvg * 4.0), 100.0, 2000.0);
  } else {
    bassFilterFreq = 400.0;  // Default
  }

  // --- AUTO-ADVANCE STEP ---
  if (currentTime - bassLastStep >= (unsigned long)bassTempo) {
    bassLastStep = currentTime;

    for (int note = 0; note < 8; note++) {
      if (bassGrid[bassStep][note]) {
        float freq = MusicNotes::midiToFreq(MusicNotes::MAJOR_SCALE_C2[note]);
        stringFilter.frequency(bassFilterFreq);
        stringFilter.resonance(3.0);  // Resonant for funky tone
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
    // Left matrix: bass pattern grid
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (col == bassStep) {
          if (bassGrid[col][row]) {
            leds[ledIndex] = CRGB::White;
          } else {
            leds[ledIndex] = CRGB(20, 5, 0);  // Dim amber playhead
          }
        } else if (bassGrid[col][row]) {
          uint8_t brightness = 100 + (int)(bassFilterFreq / 20.0);
          leds[ledIndex] = CRGB(brightness, brightness / 4, 0);
        }
      }
    }
    // Right matrix: chord-jam-style row-pair scan with anti-flicker.
    //   1) Require >=2 cells per pair before counting it (kills single-cell
    //      sensor noise that caused phantom "1-row" flickers).
    //   2) Require 3 consecutive frames at the same raw pair before
    //      committing it as the target (kills boundary chatter).
    //   3) Step the displayed bar by AT MOST 1 pair per 50 ms so a hand
    //      that lands far away grows the bar smoothly 2 -> 4 -> 6 -> 8
    //      rather than skipping.
    // 4 levels: 6-7 -> 4-7 -> 2-7 -> 0-7. Red-to-orange gradient preserved.
    static int displayedPair  = 0;
    static int targetPair     = 0;
    static int candidatePair  = 0;
    static int candidateFrames = 0;
    static uint32_t lastPairStep = 0;
    const int STABILITY_FRAMES = 3;
    const uint32_t STEP_INTERVAL_MS = 50;

    int rawPair = 0;
    for (int p = 0; p < 4; p++) {
      int row1 = 6 - p * 2;  // p=0 -> rows 6,7 ; p=3 -> rows 0,1
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

    int rowsLit = (displayedPair + 1) * 2;  // 2, 4, 6, or 8 rows
    for (int row = 8 - rowsLit; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        uint8_t hue = map(row, 0, 7, 0, 40);  // Red to orange
        leds_r[ledIndex] = CHSV(hue, 255, 180);
      }
    }
    FastLED.show();
  }
}

void processBattleMode() {
  // BATTLE MODE: Two players, each controls one sensor
  // Player 1 = CH0, Player 2 = CH1
  // Each plays notes, LEDs show who's louder/more active

  unsigned long currentTime = millis();

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
    // Use snare for P2 timbre
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
          leds[ledIndex] = CRGB(0, 100, 255);  // Bright blue
        } else if (row <= p1Bar) {
          leds[ledIndex] = CRGB(0, 20, 60);  // Dim score bar
        }
      }
      // Right matrix = Player 2 (red)
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (p2Row >= 0 && row == p2Row) {
          leds_r[ledIndex] = CRGB(255, 50, 0);  // Bright orange-red
        } else if (row <= p2Bar) {
          leds_r[ledIndex] = CRGB(60, 10, 0);  // Dim score bar
        }
      }
    }
    FastLED.show();
  }
}

bool initDistanceSensor(uint8_t channel) {
  // Initialize VL53L5CX Time-of-Flight sensor in 8x8 mode
  // channel: 0 = I2C0 (Wire, SDA=18, SCL=19)
  //          1 = I2C1 (Wire1, SDA=17, SCL=16)

  if (channel == 0) {
    Serial.println("Initializing VL53L5CX CH0 (I2C0)...");

    // Initialize I2C at 400 kHz
    Wire.begin();
    Wire.setClock(400000);

    // Initialize sensor on Wire (I2C0)
    // VL53L5CX default I2C address is 0x29 (8-bit: 0x52)
    // Try with 0x29 directly and explicit Wire reference
    if (!sensor_ch0.begin(0x29, Wire)) {
      Serial.println("ERROR: VL53L5CX CH0 not detected!");
      Serial.println("Check wiring:");
      Serial.println("  SDA -> Pin 18");
      Serial.println("  SCL -> Pin 19");
      Serial.println("  VDD -> 3.3V, GND -> GND");
      return false;
    }

    Serial.println("VL53L5CX CH0 detected and initialized!");

    // Set resolution to 8x8 (64 zones)
    sensor_ch0.setResolution(8*8);

    Serial.println("VL53L5CX CH0 configured for 8x8 mode (64 zones)");

    // Set ranging frequency (Hz)
    sensor_ch0.setRangingFrequency(10);

    // Start ranging
    sensor_ch0.startRanging();

    Serial.println("VL53L5CX CH0 ranging started!");
    Serial.println("Ready to read 8x8 distance grid\n");

  } else {
    Serial.println("Initializing VL53L5CX CH1 (I2C1)...");

    // Initialize I2C1 at 400 kHz
    Wire1.begin();
    Wire1.setClock(400000);

    // Initialize sensor on Wire1 (I2C1)
    // CRITICAL: Must pass BOTH address AND Wire1 reference
    // VL53L5CX default I2C address is 0x29
    if (!sensor_ch1.begin(0x29, Wire1)) {
      Serial.println("ERROR: VL53L5CX CH1 not detected!");
      Serial.println("Check wiring:");
      Serial.println("  SDA -> Pin 17");
      Serial.println("  SCL -> Pin 16");
      Serial.println("  VDD -> 3.3V, GND -> GND");
      return false;
    }

    Serial.println("VL53L5CX CH1 detected and initialized!");

    // Set resolution to 8x8 (64 zones)
    sensor_ch1.setResolution(8*8);

    Serial.println("VL53L5CX CH1 configured for 8x8 mode (64 zones)");

    // Set ranging frequency (Hz)
    sensor_ch1.setRangingFrequency(10);

    // Start ranging
    sensor_ch1.startRanging();

    Serial.println("VL53L5CX CH1 ranging started!");
    Serial.println("Ready to read 8x8 distance grid\n");
  }

  return true;
}

void readDistanceGrid(uint8_t channel) {
  // Read the 8x8 distance array from VL53L5CX
  // channel: 0 = CH0 (I2C0), 1 = CH1 (I2C1)
  // The sensor provides 64 distance measurements in a grid pattern

  SparkFun_VL53L5CX* sensor;
  uint16_t (*grid)[8];

  if (channel == 0) {
    sensor = &sensor_ch0;
    grid = distanceGrid_ch0;
  } else {
    sensor = &sensor_ch1;
    grid = distanceGrid_ch1;
  }

  VL53L5CX_ResultsData results;

  // Check if new data is ready
  if (!sensor->isDataReady()) {
    // No new data yet
    return;
  }

  // Get ranging data
  if (!sensor->getRangingData(&results)) {
    Serial.print("ERROR: Failed to get ranging data from CH");
    Serial.println(channel);
    return;
  }

  // Copy distance data to our 8x8 grid, rotating 180Â° so that grid[row][col]
  // matches the user's perceived orientation (top-left as you face the device =
  // grid[0][0]). The raw sensor frame is mounted rotated 180Â° relative to the
  // LED matrices, so we flip both axes here once, at the source. Every mode
  // downstream then sees correctly-oriented data without needing its own flip.
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int rawIndex = (7 - row) * 8 + (7 - col);
      grid[row][col] = results.distance_mm[rawIndex];
    }
  }
}

// ============================================================================
// LED FUNCTIONS
// ============================================================================

void setupLEDs() {
  // Initialize FastLED library â€” two 8x8 matrices
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);    // Left matrix (pin 0)
  FastLED.addLeds<LED_TYPE, LED_PIN_R, COLOR_ORDER>(leds_r, NUM_LEDS); // Right matrix (pin 1)
  FastLED.setBrightness(50);  // Set brightness (0-255), 50 is moderate
  clearAllLEDs();
  Serial.println("FastLED initialized: Left matrix on pin 0, Right matrix on pin 1");
  Serial.print("Each matrix: ");
  Serial.print(NUM_LEDS);
  Serial.println(" LEDs (8x8, serpentine layout)");
}

void clearAllLEDs() {
  // Turn off all LEDs on both matrices
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
    leds_r[i] = CRGB::Black;
  }
  FastLED.show();
}

CRGB distanceToColor(uint16_t distance_mm) {
  // Map distance values to colors for LED visualization
  // Color scheme:
  //   < 200mm: Bright RED/YELLOW (very close - target detected)
  //   200-400mm: ORANGE (close)
  //   400-600mm: GREEN (medium distance)
  //   600-800mm: BLUE (far)
  //   > 800mm: DIM PURPLE or OFF (very far / no object)

  if (distance_mm < 200) {
    // Very close - bright red
    return CRGB::Red;
  } else if (distance_mm < 400) {
    // Close - orange
    return CRGB(255, 165, 0);  // Orange (R=255, G=165, B=0)
  } else if (distance_mm < 600) {
    // Medium - green
    return CRGB::Green;
  } else if (distance_mm < 800) {
    // Far - blue
    return CRGB::Blue;
  } else if (distance_mm < 1000) {
    // Very far - dim purple
    return CRGB(64, 0, 64);  // Dim purple (R=64, G=0, B=64)
  } else {
    // Out of range or no object - off
    return CRGB::Black;
  }
}

int xyToLEDIndex(int row, int col) {
  // Convert 2D matrix coordinates to LED index for SERPENTINE layout
  // Serpentine pattern: even rows go left-to-right, odd rows go right-to-left

  if (row % 2 == 0) {
    // Even rows (0, 2, 4, 6): left to right
    return row * MATRIX_WIDTH + col;
  } else {
    // Odd rows (1, 3, 5, 7): right to left
    return row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
  }
}

// ============================================================================
// OLED DISPLAY FUNCTIONS
// ============================================================================

void setupOLED() {
  // Initialize OLED display on I2C bus (Wire - shared with VL53L5CX CH0)
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("ERROR: SSD1306 OLED allocation failed!");
    // Continue anyway - serial interface still works
  } else {
    Serial.println("OLED display found at 0x3C");
  }

  display.clearDisplay();

  // Yellow section (0-15 pixels): "AURA" in big letters, centered
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  // "AURA" is 4 chars * 12 pixels wide (textSize 2) = 48 pixels
  // Center: (128 - 48) / 2 = 40
  display.setCursor(40, 0);
  display.println("AURA");

  display.display();
}

void showLoadingScreen(const char* label, int step, int total) {
  // Boot-time progress screen. Yellow band keeps the "AURA" title, blue band
  // shows current step name and a progress bar. Safe to call before any
  // sensor/LED is up â€” only requires setupOLED() to have run.
  display.clearDisplay();

  // Yellow section: AURA title (same position as updateOLEDDisplay)
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 0);
  display.println("AURA");

  // Blue section, size-1 text
  display.setTextSize(1);

  // "Loading..." centered
  const char* loading = "Loading...";
  int loadingWidth = strlen(loading) * 6;  // size-1 font â‰ˆ 6 px per char
  display.setCursor((128 - loadingWidth) / 2, 20);
  display.println(loading);

  // Current step: "[step/total] label" centered
  char stepStr[32];
  snprintf(stepStr, sizeof(stepStr), "[%d/%d] %s", step, total, label);
  int stepWidth = strlen(stepStr) * 6;
  int stepX = (128 - stepWidth) / 2;
  if (stepX < 0) stepX = 0;
  display.setCursor(stepX, 34);
  display.println(stepStr);

  // Progress bar (outlined rectangle with a filled portion)
  const int barX = 14, barY = 50, barW = 100, barH = 8;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  if (total > 0) {
    int fillW = (barW - 2) * step / total;
    if (fillW > 0) display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);
  }

  display.display();
}

void updateOLEDDisplay() {
  display.clearDisplay();

  // ===== YELLOW SECTION (pixels 0-15): "AURA" title =====
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  // "AURA" is 4 chars * 12 pixels wide (textSize 2) = 48 pixels
  // Center: (128 - 48) / 2 = 40
  display.setCursor(40, 0);
  display.println("AURA");

  // ===== BLUE SECTION (pixels 16-63): Current mode, centered, big =====
  const char* modeStr = getModeString(currentMode);

  // Size-2 font is ~12 px per char; OLED is 128 px wide -> max 10 chars/line.
  // Long labels (e.g. "UNDER THE HOOD") get wrapped onto two lines, split at
  // the last space that lets both halves fit. Otherwise render on one line.
  const int charW = 12;
  int len = strlen(modeStr);
  display.setTextSize(2);
  if (len * charW <= 128) {
    int xPos = (128 - len * charW) / 2;
    display.setCursor(xPos, 28);
    display.println(modeStr);
  } else {
    int splitAt = -1;
    for (int i = len - 1; i >= 0; i--) {
      if (modeStr[i] == ' ' && i * charW <= 128 && (len - i - 1) * charW <= 128) {
        splitAt = i;
        break;
      }
    }
    if (splitAt < 0) {
      display.setCursor(0, 28);
      display.println(modeStr);
    } else {
      char line1[20], line2[20];
      int l1 = splitAt, l2 = len - splitAt - 1;
      strncpy(line1, modeStr, l1); line1[l1] = '\0';
      strncpy(line2, modeStr + splitAt + 1, l2); line2[l2] = '\0';
      display.setCursor((128 - l1 * charW) / 2, 20);
      display.println(line1);
      display.setCursor((128 - l2 * charW) / 2, 38);
      display.println(line2);
    }
  }

  // ===== BOTTOM NOTE (small text) =====
  display.setTextSize(1);
  display.setCursor(0, 56);  // Bottom of screen
  display.println("Turn to switch modes");

  display.display();
}

const char* getModeString(PlayMode mode) {
  switch (mode) {
    case MODE_BACKSTAGE:    return "BACKSTAGE";
    case MODE_BACKSTAGE_2:  return "BACKSTAGE 2";
    case MODE_CHORD_JAM:    return "CHORD JAM";
    case MODE_DUAL_LOOP:    return "DUAL LOOP";
    case MODE_BASS_MACHINE: return "BASS";
    case MODE_BATTLE_MODE:  return "BATTLE";
    default:                return "UNKNOWN";
  }
}

// ============================================================================
// MODE SWITCHING
// ============================================================================

// Re-entrancy guard so a fast double-click on the encoder button can't fire
// two mode-enter blocks on top of each other.
bool isSwitchingMode = false;

void switchToMode(PlayMode newMode) {
  // Prevent overlapping mode switches
  if (isSwitchingMode) {
    Serial.println(">> Mode switch already in progress, ignoring...");
    return;
  }

  isSwitchingMode = true;
  currentMode = newMode;

  Serial.print(">> switchToMode() called - New mode: ");
  Serial.println(getModeString(currentMode));

  // Reset snareDrum to drum-like defaults. Modes that want a melodic timbre
  // (e.g. DRONE+SOLO) override these in their own mode-enter block below.
  snareDrum.length(150);
  snareDrum.secondMix(0.5);
  snareDrum.pitchMod(0.3);

  // Mode-specific initialization. Each branch resets the state owned by
  // that mode and (if needed) re-reads the sensors so the new mode starts
  // with fresh data instead of whatever was left over.
  if (currentMode == MODE_BACKSTAGE) {
    Serial.println(">> Mode: BACKSTAGE  (CH0->LEFT, CH1->RIGHT)");
    if (!sensor_ch0_initialized) sensor_ch0_initialized = initDistanceSensor(0);
    if (!sensor_ch1_initialized) sensor_ch1_initialized = initDistanceSensor(1);
    panicMute();
    clearAllLEDs();
    FastLED.show();
    delay(100);
  } else if (currentMode == MODE_BACKSTAGE_2) {
    Serial.println(">> Mode: BACKSTAGE 2  (LED-only animation)");
    panicMute();
    clearAllLEDs();
    FastLED.show();
    delay(100);
  } else if (currentMode == MODE_CHORD_JAM) {
    Serial.println(">> Mode: CHORD JAM  (left=chord, right=strum)");
    chordJamIndex = 0;
    chordJamLastStrum = 0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_DUAL_LOOP) {
    Serial.println(">> Mode: DUAL LOOP  (left=melody, right=drums)");
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
    dualLoopStep = 0;
    dualLoopLastStep = 0;
    clearAllLEDs();
    delay(100);
  } else if (currentMode == MODE_BASS_MACHINE) {
    Serial.println(">> Mode: BASS  (left=pattern, right=filter wah)");
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        bassGrid[r][c]       = false;
        bassTouchState[r][c] = false;
        bassTouchTime[r][c]  = 0;
      }
    }
    bassStep = 0;
    bassLastStep = 0;
    bassFilterFreq = 400.0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_BATTLE_MODE) {
    Serial.println(">> Mode: BATTLE  (P1=CH0, P2=CH1)");
    battleScore[0] = 0;
    battleScore[1] = 0;
    battleVol[0]   = 0;
    battleVol[1]   = 0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  }

  panicMute();  // Mute when switching modes
  updateOLEDDisplay();  // Update display

  isSwitchingMode = false;  // Allow mode switches again
  Serial.println(">> switchToMode() complete");
}

// ============================================================================
// ROTARY ENCODER FUNCTIONS
// ============================================================================

void handleEncoder() {
  long newPosition = knob.read();

  // Encoder has 4 pulses per detent, so divide by 4 for smoother control
  long positionChange = (newPosition - lastEncoderPosition) / 4;

  if (positionChange != 0) {
    lastEncoderPosition = newPosition - (newPosition % 4);  // Snap to multiple of 4

    // Encoder hardware reports clockwise as a NEGATIVE positionChange on this
    // build, so a right-turn (CW) should advance to the next enum value.
    if (positionChange < 0) {
      // Clockwise rotation - next mode (wraps at MODE_LAST -> MODE_FIRST)
      int nextMode = (int)currentMode + 1;
      if (nextMode > MODE_LAST) nextMode = MODE_FIRST;
      switchToMode((PlayMode)nextMode);
    } else if (positionChange > 0) {
      // Counter-clockwise rotation - previous mode (wraps at MODE_FIRST -> MODE_LAST)
      int prevMode = (int)currentMode - 1;
      if (prevMode < MODE_FIRST) prevMode = MODE_LAST;
      switchToMode((PlayMode)prevMode);
    }
  }
}

void handleEncoderButton() {
  bool buttonState = digitalRead(ENCODER_BUTTON);
  unsigned long currentTime = millis();

  // Detect button press (HIGH to LOW transition with debounce)
  if (buttonState == LOW && lastButtonState == HIGH &&
      (currentTime - lastButtonPress) > DEBOUNCE_DELAY) {
    lastButtonPress = currentTime;

    // Button action depends on current mode: clear the active pattern/state
    // so the player can start over without losing their place in the menu.
    if (currentMode == MODE_DUAL_LOOP) {
      Serial.println(">> DUAL LOOP: Clearing all layers!");
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          dualLoopMelody[r][c] = false;
          dualLoopDrums[r][c]  = false;
        }
      }
      dualLoopStep = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_CHORD_JAM) {
      Serial.println(">> CHORD JAM: Reset!");
      chordJamIndex = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_BASS_MACHINE) {
      Serial.println(">> BASS: Clearing pattern!");
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) bassGrid[r][c] = false;
      }
      bassStep = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_BATTLE_MODE) {
      Serial.println(">> BATTLE: Score reset!");
      battleScore[0] = 0;
      battleScore[1] = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    }

    updateOLEDDisplay();
  }

  lastButtonState = buttonState;
}
