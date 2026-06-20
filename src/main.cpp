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
#include <FastLED.h>

#include "Config.h"
#include "AuraState.h"
#include "MusicNotes.h"
#include "LedMatrix.h"
#include "OledStatus.h"
#include "RotaryUI.h"
#include "ToFGrid.h"
#include "SynthVoices.h"

#include "modes/Backstage.h"
#include "modes/DualLoop.h"
#include "modes/ChordJam.h"
#include "modes/BassMachine.h"
#include "modes/Battle.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
// All audio objects (oscillators, envelopes, filters, mixer, codec) and the
// patch-cord graph live in lib/SynthVoices. SynthVoices.h re-declares them
// with `extern` so this file - and every mode - can keep poking them by
// their short names.

// Play mode + master volume. Per-mode private state (sequencer grids, touch
// latches, step counters, etc.) lives at file scope inside the matching
// src/modes/*.cpp; this file only owns the truly shared globals declared in
// include/AuraState.h.
PlayMode currentMode = MODE_BACKSTAGE;
float masterVolume = 1.0;

// VL53L5CX Distance Sensors (8x8 mode)
// CH0: I2C0 (Wire) - SDA=18, SCL=19
// CH1: I2C1 (Wire1) - SDA=17, SCL=16
// The sensor instances now live in lib/ToFGrid; main.cpp just keeps the
// per-frame buffers (every mode reads grid[row][col] from them directly).
bool sensor_ch0_initialized = false;
bool sensor_ch1_initialized = false;
uint16_t distanceGrid_ch0[8][8];  // 8x8 distance array for CH0
uint16_t distanceGrid_ch1[8][8];  // 8x8 distance array for CH1
unsigned long lastSensorRead = 0;

// LED matrices (pin assignments / dimensions live in Config.h).
// Serpentine layout: row 0 is left-to-right, row 1 is right-to-left, etc.
CRGB leds[NUM_LEDS];        // Left LED matrix array
CRGB leds_r[NUM_LEDS];      // Right LED matrix array

// Global LED visualization gate. Each mode checks this before lighting
// up its matrices so we can blank the panels for "stage" demos.
bool ledVisualizationEnabled = true;

// OLED display instance now lives inside lib/OledStatus.

// Rotary encoder state and the Encoder instance now live inside lib/RotaryUI.

// True when a Karplus-Strong string voice is currently sustaining. Lets
// panicMute() know whether it actually needs to release the envelope.
bool noteActive = false;

// Main-loop timer for the fixed-rate sensor + mode update.
unsigned long lastUpdateTime = 0;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Most helper declarations live in include/AuraState.h. Only the bits that
// are still defined in this file - the mode-switching glue and a couple of
// boot-time wrappers - need forward decls here.
void setupLEDs();
void clearAllLEDs();
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
  // lib/SynthVoices brings up AudioMemory, the SGTL5000, mixer gains, and
  // every voice's parameter defaults in one call.
  SynthVoices::begin(masterVolume);
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
  RotaryUI::begin(ENCODER_PIN_A, ENCODER_PIN_B, ENCODER_BUTTON, DEBOUNCE_DELAY);
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
  OledStatus::showSplash("Ready!");
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

    // Dispatch to the active mode. Each mode owns its own state and is
    // responsible for reading sensors + driving audio + LEDs.
    if (currentMode == MODE_DUAL_LOOP) {
      DualLoop::process();
    } else if (currentMode == MODE_CHORD_JAM) {
      ChordJam::process();
    } else if (currentMode == MODE_BASS_MACHINE) {
      BassMachine::process();
    } else if (currentMode == MODE_BATTLE_MODE) {
      Battle::process();
    } else if (currentMode == MODE_BACKSTAGE) {
      Backstage::process(currentTime);
    } else if (currentMode == MODE_BACKSTAGE_2) {
      Backstage::processAnimation(currentTime);
    }
  }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================


// Kill any audio still playing. Called when switching modes and when the
// player presses the encoder button to "reset" the current mode. The
// envelope releases live in lib/SynthVoices; the noteActive flag is a
// project-side state shadow tied to the Karplus-Strong voice, so we clear
// it here.
void panicMute() {
  SynthVoices::panic();
  noteActive = false;
}

// Thin wrappers around lib/ToFGrid: route CH0 to Wire and CH1 to Wire1, and
// hand each channel its own buffer. The library owns the sensor instances,
// ranging configuration, and the 180-degree flip applied to every frame.
bool initDistanceSensor(uint8_t channel) {
  TwoWire& bus = (channel == 0) ? Wire : Wire1;
  Serial.print("Initializing VL53L5CX CH"); Serial.print(channel); Serial.println(" ...");
  bool ok = ToFGrid::begin(channel, bus);
  if (!ok) {
    Serial.print("ERROR: VL53L5CX CH"); Serial.print(channel); Serial.println(" not detected!");
  } else {
    Serial.print("VL53L5CX CH"); Serial.print(channel); Serial.println(" ranging started.");
  }
  return ok;
}

void readDistanceGrid(uint8_t channel) {
  uint16_t (*grid)[8] = (channel == 0) ? distanceGrid_ch0 : distanceGrid_ch1;
  ToFGrid::readFrame(channel, grid);
}

// ============================================================================
// LED FUNCTIONS
// ============================================================================

void setupLEDs() {
  // Initialize FastLED library -- two 8x8 matrices
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);    // Left matrix (pin 0)
  FastLED.addLeds<LED_TYPE, LED_PIN_R, COLOR_ORDER>(leds_r, NUM_LEDS); // Right matrix (pin 1)
  FastLED.setBrightness(50);  // Set brightness (0-255), 50 is moderate
  clearAllLEDs();
  Serial.println("FastLED initialized: Left matrix on pin 0, Right matrix on pin 1");
  Serial.print("Each matrix: ");
  Serial.print(NUM_LEDS);
  Serial.println(" LEDs (8x8, serpentine layout)");
}

// Blank both LED matrices and push the result to the panels. Pixel-level
// math lives in lib/LedMatrix; only the project knows it has two buffers.
void clearAllLEDs() {
  LedMatrix::clearMatrix(leds,   NUM_LEDS);
  LedMatrix::clearMatrix(leds_r, NUM_LEDS);
  FastLED.show();
}

// ============================================================================
// OLED DISPLAY FUNCTIONS
// ============================================================================

// Thin wrappers around lib/OledStatus so existing call sites stay unchanged.
// The library owns the Adafruit_SSD1306 instance and all rendering math.
void setupOLED() {
  if (!OledStatus::begin(OLED_ADDR)) {
    Serial.println("ERROR: SSD1306 OLED allocation failed!");
  } else {
    Serial.println("OLED display found at 0x3C");
  }
}

void showLoadingScreen(const char* label, int step, int total) {
  OledStatus::showLoading(label, step, total);
}

void updateOLEDDisplay() {
  OledStatus::showStatus(getModeString(currentMode), "Turn to switch modes");
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
// two mode-enter blocks on top of each other. File-local: only switchToMode
// touches it.
static bool isSwitchingMode = false;

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

  // Mode-specific initialization. Each mode's enter() resets the state it
  // owns; the orchestrator just handles the shared scaffolding (boot the
  // sensors lazily if BACKSTAGE needs them, re-read the grids so the new
  // mode starts with fresh data, blank the LEDs).
  if (currentMode == MODE_BACKSTAGE) {
    Serial.println(">> Mode: BACKSTAGE  (CH0->LEFT, CH1->RIGHT)");
    if (!sensor_ch0_initialized) sensor_ch0_initialized = initDistanceSensor(0);
    if (!sensor_ch1_initialized) sensor_ch1_initialized = initDistanceSensor(1);
    panicMute();
    clearAllLEDs();
    FastLED.show();
  } else if (currentMode == MODE_BACKSTAGE_2) {
    Serial.println(">> Mode: BACKSTAGE 2  (LED-only animation)");
    panicMute();
    clearAllLEDs();
    FastLED.show();
  } else if (currentMode == MODE_CHORD_JAM) {
    Serial.println(">> Mode: CHORD JAM  (left=chord, right=strum)");
    ChordJam::enter();
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
  } else if (currentMode == MODE_DUAL_LOOP) {
    Serial.println(">> Mode: DUAL LOOP  (left=melody, right=drums)");
    DualLoop::enter();
    clearAllLEDs();
  } else if (currentMode == MODE_BASS_MACHINE) {
    Serial.println(">> Mode: BASS  (left=pattern, right=filter wah)");
    BassMachine::enter();
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
  } else if (currentMode == MODE_BATTLE_MODE) {
    Serial.println(">> Mode: BATTLE  (P1=CH0, P2=CH1)");
    Battle::enter();
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
  }
  delay(100);

  panicMute();  // Mute when switching modes
  updateOLEDDisplay();  // Update display

  isSwitchingMode = false;  // Allow mode switches again
  Serial.println(">> switchToMode() complete");
}

// ============================================================================
// ROTARY ENCODER FUNCTIONS
// ============================================================================

// Map rotation events from lib/RotaryUI to mode transitions. On this build
// the encoder reports CW as a NEGATIVE detent count, so a right-turn (CW)
// advances to the next enum value with wraparound at MODE_LAST/MODE_FIRST.
void handleEncoder() {
  int detents = RotaryUI::pollRotation();
  if (detents == 0) return;

  if (detents < 0) {
    int nextMode = (int)currentMode + 1;
    if (nextMode > MODE_LAST) nextMode = MODE_FIRST;
    switchToMode((PlayMode)nextMode);
  } else {
    int prevMode = (int)currentMode - 1;
    if (prevMode < MODE_FIRST) prevMode = MODE_LAST;
    switchToMode((PlayMode)prevMode);
  }
}

// Map button-press events from lib/RotaryUI to mode-specific "clear" actions
// so the player can restart a pattern without leaving the current mode.
void handleEncoderButton() {
  if (!RotaryUI::pollButtonPressed()) return;

  if (currentMode == MODE_DUAL_LOOP) {
    Serial.println(">> DUAL LOOP: Clearing all layers!");
    DualLoop::clear();
  } else if (currentMode == MODE_CHORD_JAM) {
    Serial.println(">> CHORD JAM: Reset!");
    ChordJam::clear();
  } else if (currentMode == MODE_BASS_MACHINE) {
    Serial.println(">> BASS: Clearing pattern!");
    BassMachine::clear();
  } else if (currentMode == MODE_BATTLE_MODE) {
    Serial.println(">> BATTLE: Score reset!");
    Battle::clear();
  } else {
    // BACKSTAGE / BACKSTAGE_2 have no per-mode state to clear; just blank.
    updateOLEDDisplay();
    return;
  }

  panicMute();
  clearAllLEDs();
  FastLED.show();
  updateOLEDDisplay();
}
