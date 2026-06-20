/*
 * AURA - Air universal rythm aparatus
 *
 * stuff hooked up:
 *   teensy 4.0 + audio shield
 *   2 distance sensors (the 8x8 ones)
 *   2 led matrixes one per hand
 *   little oled screen
 *   a knob with a button on it
 *
 * modes (turn the knob to switch):
 *   WELCOME - the heart screen at the start
 *   BACKSTAGE - shows sensor data to see if its working
 *   CHORD JAM - left hand picks the chord right hand strums it
 *   DUAL LOOP - loop pedal thing, left is melody right is drums
 *   BASS MACHINE - bass beat with a wah on the right hand
 *   BATTLE MODE - 2 player game
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

#include "modes/Welcome.h"
#include "modes/Backstage.h"
#include "modes/DualLoop.h"
#include "modes/ChordJam.h"
#include "modes/BassMachine.h"
#include "modes/Battle.h"

// ---- globals ----

PlayMode currentMode = MODE_WELCOME;
float masterVolume = 1.0;

// the 2 distance sensors. ch0 is the left hand ch1 is the right hand
bool sensor_ch0_initialized = false;
bool sensor_ch1_initialized = false;
uint16_t distanceGrid_ch0[8][8];
uint16_t distanceGrid_ch1[8][8];
unsigned long lastSensorRead = 0;

// the 2 led matrixes
CRGB leds[NUM_LEDS];     // left one
CRGB leds_r[NUM_LEDS];   // right one

// flip this off if u want the panels dark
bool ledVisualizationEnabled = true;

// true if a string note is still ringing so we know to shut it off later
bool noteActive = false;

unsigned long lastUpdateTime = 0;

// forward decls
void setupLEDs();
void clearAllLEDs();
void setupOLED();
void showLoadingScreen(const char* label, int step, int total);
void updateOLEDDisplay();
void handleEncoder();
void handleEncoderButton();
void switchToMode(PlayMode newMode);
const char* getModeString(PlayMode mode);

// ---- setup ----

void setup() {
  Serial.begin(115200);

  delay(1000);  // give serial a sec

  Serial.println("\n=== Teensy Touchless Instrument (Simulated ToF) ===");

  // boot everything one thing at a time and show a little loading bar on the oled so u know its alive
  SynthVoices::begin(masterVolume);
  Serial.println("Audio system initialized!");

  setupOLED();
  Serial.println("OLED display initialized!");
  showLoadingScreen("Audio", 1, 6);
  delay(150);
  showLoadingScreen("OLED", 2, 6);
  delay(150);

  showLoadingScreen("LEDs", 3, 6);
  setupLEDs();
  Serial.println("LED system initialized!");
  delay(150);

  showLoadingScreen("Encoder", 4, 6);
  RotaryUI::begin(ENCODER_PIN_A, ENCODER_PIN_B, ENCODER_BUTTON, DEBOUNCE_DELAY);
  Serial.println("Rotary encoder initialized!");
  delay(150);

  // sensors go last becuase they take the longest to wake up
  showLoadingScreen("Sensor CH0", 5, 6);
  Serial.println("Initializing VL53L5CX CH0 (I2C0)...");
  sensor_ch0_initialized = initDistanceSensor(0);
  Serial.println(sensor_ch0_initialized ? "   CH0 initialization SUCCESS" : "   CH0 initialization FAILED");

  showLoadingScreen("Sensor CH1", 6, 6);
  Serial.println("Initializing VL53L5CX CH1 (I2C1)...");
  sensor_ch1_initialized = initDistanceSensor(1);
  Serial.println(sensor_ch1_initialized ? "   CH1 initialization SUCCESS" : "   CH1 initialization FAILED");

  // quick Ready! flash then the regular screen shows up
  OledStatus::showSplash("Ready!");
  delay(500);

  lastUpdateTime = millis();

  updateOLEDDisplay();
}


// ---- main loop ----

void loop() {
  // check the knob first
  handleEncoder();
  handleEncoderButton();

  // run the active mode every so often
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL_MS) {
    lastUpdateTime = currentTime;

    // just call whichever mode is on rn
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
    } else if (currentMode == MODE_WELCOME) {
      Welcome::process(currentTime);
    }
  }
}

// ---- helpers ----

// shut up any noise thats still playing
void panicMute() {
  SynthVoices::panic();
  noteActive = false;
}

// turn on one of the distance sensors. ch0 uses Wire and ch1 uses Wire1
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

// grab a frame of distances from one sensor
void readDistanceGrid(uint8_t channel) {
  uint16_t (*grid)[8] = (channel == 0) ? distanceGrid_ch0 : distanceGrid_ch1;
  ToFGrid::readFrame(channel, grid);
}

// ---- leds ----

void setupLEDs() {
  // 2 matrixes one on each pin
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_R, COLOR_ORDER>(leds_r, NUM_LEDS);
  FastLED.setBrightness(50);  // not too bright
  clearAllLEDs();
  Serial.println("FastLED initialized: Left matrix on pin 0, Right matrix on pin 1");
  Serial.print("Each matrix: ");
  Serial.print(NUM_LEDS);
  Serial.println(" LEDs (8x8, serpentine layout)");
}

// blank out both matrixes
void clearAllLEDs() {
  LedMatrix::clearMatrix(leds,   NUM_LEDS);
  LedMatrix::clearMatrix(leds_r, NUM_LEDS);
  FastLED.show();
}

// ---- oled screen ----

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

// gives a name for whatever mode is on right now
const char* getModeString(PlayMode mode) {
  switch (mode) {
    case MODE_WELCOME:      return "WELCOME";
    case MODE_BACKSTAGE:    return "BACKSTAGE";
    case MODE_CHORD_JAM:    return "CHORD JAM";
    case MODE_DUAL_LOOP:    return "DUAL LOOP";
    case MODE_BASS_MACHINE: return "BASS";
    case MODE_BATTLE_MODE:  return "BATTLE";
    default:                return "UNKNOWN";
  }
}

// ---- mode switching ----

// stops a really fast double click from running 2 switches on top of each other
static bool isSwitchingMode = false;

void switchToMode(PlayMode newMode) {
  if (isSwitchingMode) {
    Serial.println(">> Mode switch already in progress, ignoring...");
    return;
  }

  isSwitchingMode = true;
  currentMode = newMode;

  Serial.print(">> switchToMode() called - New mode: ");
  Serial.println(getModeString(currentMode));

  // put the snare back to a drum sound incase another mode changed it
  snareDrum.length(150);
  snareDrum.secondMix(0.5);
  snareDrum.pitchMod(0.3);

  // set up whatever the new mode needs
  if (currentMode == MODE_BACKSTAGE) {
    Serial.println(">> Mode: BACKSTAGE  (CH0->LEFT, CH1->RIGHT)");
    if (!sensor_ch0_initialized) sensor_ch0_initialized = initDistanceSensor(0);
    if (!sensor_ch1_initialized) sensor_ch1_initialized = initDistanceSensor(1);
    panicMute();
    clearAllLEDs();
    FastLED.show();
  } else if (currentMode == MODE_WELCOME) {
    Serial.println(">> Mode: WELCOME  (LED-only boot/idle screen)");
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

  panicMute();   // make sure nothing is still playing
  updateOLEDDisplay();

  isSwitchingMode = false;
  Serial.println(">> switchToMode() complete");
}

// ---- knob stuff ----

// turning the knob switches modes and it wraps around at the ends
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

// pressing the knob restarts the current mode (like clear the pattern)
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
    // welcome and backstage dont have anything to clear so just refresh the oled
    updateOLEDDisplay();
    return;
  }

  panicMute();
  clearAllLEDs();
  FastLED.show();
  updateOLEDDisplay();
}
