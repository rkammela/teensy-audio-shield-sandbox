/*
 * Teensy 4.0/4.1 Touchless Instrument - Simulated ToF Version
 *
 * Hardware: Teensy 4.0 or 4.1 with SGTL5000 Audio Shield
 * Output: I2S to headphone/line out
 *
 * Features:
 * - STRING THEREMIN mode: pitch quantization, zones, expression
 * - GESTURE DRUM mode: zone-based drum triggering
 * - Simulated ToF sensors via serial commands
 * - Multiple scales (pentatonic, major, minor)
 * - Gesture simulation (jab, tap, vibrato)
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

enum PlayMode {
  // ---- Diagnostic surfaced at the top of the menu ----
  MODE_BACKSTAGE,      // Both sensors rendered on both LED matrices at once
  MODE_BACKSTAGE_2,    // LED-only animation: pulsing heart (L) + treble clef (R)
  // ---- Headline performance modes (no prefix on OLED) ----
  MODE_CHORD_JAM,      // Guitar-style: fretboard + strumming
  MODE_DUAL_LOOP,      // Dual-hand layered looper (melody + drums)
  MODE_BASS_MACHINE,   // Looping bass line with filter sweep
  MODE_BATTLE_MODE,    // Two player dueling melodies
  // ---- Other working performance modes ----
  MODE_STRING_THEREMIN,
  MODE_DUAL_PIANO,     // Dual-hand piano/string instrument
  MODE_GRID_SEQUENCER, // 64-zone MIDI pad style
  MODE_DRONE_SOLO,     // Left hand drone, right hand melody
  // ---- Broken, fix in progress ('!' prefix on OLED) ----
  MODE_DUAL_THEREMIN,  // Right voice audio dead
  MODE_RHYTHM_TAPPER,  // Input lag
  MODE_ECHO_DELAY,     // Right side frozen, half LEDs stuck pink
  // ---- Calibration / diagnostic tools ----
  MODE_LED_TEST,
  MODE_CALIB_SERIAL_CH0,  // Calibration: print CH0 8x8 grid to serial monitor
  MODE_CALIB_SERIAL_CH1,  // Calibration: print CH1 8x8 grid to serial monitor
  // ---- Deprecated (pending removal; '*' prefix on OLED) ----
  MODE_RAIN_MODE,         // Falling notes caught by hands
  MODE_STEP_SEQ,          // Auto-looping step sequencer
  MODE_ARPEGGIATOR,       // Auto-arpeggiating chords
  MODE_DISTANCE_SENSOR    // Raw 8x8 grid view (now covered by UNDER THE HOOD)
};

// Cycle bounds (kept in sync with the enum order). When the enum is
// reordered, update these two so the encoder and 'M' command wrap correctly.
// NOTE: MODE_LAST currently stops the menu at BATTLE so only the 6 headline
// modes are reachable from the encoder. All later enum values still exist
// and still work if selected programmatically -- they are just hidden from
// the menu cycle. Move MODE_LAST back to MODE_DISTANCE_SENSOR to re-expose.
#define MODE_FIRST  MODE_BACKSTAGE
#define MODE_LAST   MODE_BATTLE_MODE

enum ScaleType {
  SCALE_PENTATONIC,
  SCALE_MAJOR,
  SCALE_MINOR
};

enum Zone {
  ZONE_A_NEAR,   // Bass/low register
  ZONE_B_MID,    // Melody register
  ZONE_C_FAR     // Harmonics/bright
};

// Distance thresholds (mm)
const int DIST_MIN = 80;
const int DIST_MAX = 800;
const int ZONE_A_MAX = 250;   // 80-250mm = Zone A
const int ZONE_B_MAX = 500;   // 250-500mm = Zone B
                              // 500-800mm = Zone C
const int PRESENCE_THRESHOLD = 780;  // Above this = no presence

// Distance change increments
const int DIST_STEP = 20;

// Zone default positions
const int ZONE_A_DEFAULT = 150;
const int ZONE_B_DEFAULT = 350;
const int ZONE_C_DEFAULT = 650;

// Smoothing
const float SMOOTH_ALPHA = 0.3;  // Low-pass filter coefficient (0-1, lower = smoother)

// Update rate
const unsigned long UPDATE_INTERVAL_MS = 20;  // 50 Hz

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

// Play mode and settings
PlayMode currentMode = MODE_BACKSTAGE;
ScaleType currentScale = SCALE_PENTATONIC;
bool muteIfIdle = false;
bool vibratoEnabled = false;
bool continuousSound = true;  // Auto re-pluck for sustained sound
float masterVolume = 1.0;  // Master volume (0.0 to 1.0) - default 100%

// Theremin mode variables
float thereminPitch = 440.0;  // Hz (A4)
float thereminVolume = 0.5;   // 0.0 to 1.0
const float THEREMIN_PITCH_STEP = 10.0;  // Hz per key press
const float THEREMIN_VOLUME_STEP = 0.05;  // Volume change per key press

// Dual Theremin mode variables (using ToF sensors)
float dualThereminPitch = 440.0;  // Current pitch in Hz
float dualThereminVolume = 0.5;   // Current volume
float dualThereminFilter = 1000.0;  // Filter cutoff frequency
int leftHandCentroidX = 0;   // Left hand X position (0-7)
int leftHandCentroidY = 0;   // Left hand Y position (0-7)
int rightHandCentroidX = 0;  // Right hand X position (0-7)
int rightHandCentroidY = 0;  // Right hand Y position (0-7)
int leftHandZones = 0;       // Number of active zones (hand coverage)
int rightHandZones = 0;      // Number of active zones
float leftHandAvgDist = 2000.0;   // Average distance for left hand
float rightHandAvgDist = 2000.0;  // Average distance for right hand
unsigned long lastDualThereminUpdate = 0;
const int DUAL_THEREMIN_UPDATE_INTERVAL = 50;  // Update every 50ms (20Hz)

// Pitch mapping for dual theremin
const float DUAL_PITCH_MIN = 110.0;   // A2 - Low note
const float DUAL_PITCH_MAX = 880.0;   // A5 - High note
const float DUAL_DIST_MIN = 100.0;    // Minimum distance (mm) for pitch
const float DUAL_DIST_MAX = 1000.0;   // Maximum distance (mm) for pitch

// Dual Drums mode variables
float lastLeftHandDist = 2000.0;     // For velocity detection
float lastRightHandDist = 2000.0;
unsigned long lastDrumHitTime = 0;
const int DRUM_HIT_COOLDOWN = 80;    // ms between hits (was 200, now much faster!)
const float DRUM_VELOCITY_THRESHOLD = 30.0;  // mm/update for hit detection (was 100, now more sensitive!)

// Dual Piano mode variables
int currentPianoNote = -1;           // -1 = no note playing
float pianoNoteVelocity = 0.5;
unsigned long lastPianoNoteTime = 0;
const int PIANO_NOTE_MIN = 0;        // Lowest note index in scale
const int PIANO_NOTE_MAX = 12;       // Highest note index (one octave)

// Grid Sequencer mode variables
bool gridZoneActive[8][8] = {false}; // Track which zones are currently touched
unsigned long gridZoneLastTrigger[8][8] = {0};  // Debounce timing
bool gridZoneActiveR[8][8] = {false};           // Right sensor zones
unsigned long gridZoneLastTriggerR[8][8] = {0};
const int GRID_ZONE_THRESHOLD = 400;  // mm - closer than this = touched (lowered for intentional touches)
const int GRID_DEBOUNCE_MS = 50;      // Minimum time between re-triggers

// Rhythm Tapper mode variables
bool rhythmRowActive[8] = {false};    // Track which drum rows are active
unsigned long rhythmRowLastHit[8] = {0};
bool rhythmRowActiveR[8] = {false};   // Right sensor drum rows
unsigned long rhythmRowLastHitR[8] = {0};
const int RHYTHM_DEBOUNCE_MS = 80;    // Fast response!

// Arpeggiator mode variables
unsigned long lastArpNoteTime = 0;
int arpNoteIndex = 0;                 // Current note in arpeggio pattern
int arpChordType = 0;                 // 0=major, 1=minor, 2=7th, 3=dim
float arpSpeed = 200.0;               // ms between arp notes
const int ARP_CHORD_NOTES = 4;        // Notes per chord

// DJ Scratch mode variables
float djPitch = 440.0;
float djLastDist = 2000.0;
float djScratchSpeed = 0.0;           // Current scratch speed
bool djIsScratching = false;

// Step Sequencer mode variables
bool stepGrid[8][8] = {false};        // 8 steps x 8 instruments
int stepPosition = 0;                 // Current step (0-7)
unsigned long lastStepTime = 0;
float stepTempo = 200.0;              // ms per step (300 BPM at 200ms)

// Dual Loop mode variables
bool dualLoopMelody[8][8] = {false};  // Left hand: 8 steps x 8 notes (piano/strings)
bool dualLoopDrums[8][8] = {false};   // Right hand: 8 steps x 8 drums
bool dualLoopTouchL[8][8] = {false};  // Touch state tracking (left)
bool dualLoopTouchR[8][8] = {false};  // Touch state tracking (right)
unsigned long dualLoopTouchTimeL[8][8] = {0};
unsigned long dualLoopTouchTimeR[8][8] = {0};
int dualLoopStep = 0;
unsigned long dualLoopLastStep = 0;
float dualLoopTempo = 180.0;          // ms per step

// Chord Jam mode variables
int chordJamIndex = 0;                // Current chord (0=I, 1=IV, 2=V, 3=vi)
unsigned long chordJamLastStrum = 0;
bool chordJamTouchState[8][8] = {false};
unsigned long chordJamTouchTime[8][8] = {0};
float chordJamStrumVelocity = 0.0;

// Drone + Solo mode variables
float droneFreq = 130.81;             // C3 default drone
bool droneActive = false;
int soloLastNote = -1;
unsigned long soloLastTrigger = 0;

// Rain Mode variables
float rainDropRow[8] = {0};           // Y position of each raindrop (float for smooth animation)
int rainDropNote[8] = {0};            // MIDI note for each drop
bool rainDropActive[8] = {false};     // Whether each drop slot is active
float rainDropRowR[8] = {0};          // Right sensor rain drops
int rainDropNoteR[8] = {0};
bool rainDropActiveR[8] = {false};
unsigned long rainLastFall = 0;
unsigned long rainLastSpawn = 0;
float rainSpeed = 0.3;                // Rows per update cycle

// Bass Machine mode variables
bool bassGrid[8][8] = {false};        // 8 steps x 8 bass notes
bool bassTouchState[8][8] = {false};
unsigned long bassTouchTime[8][8] = {0};
int bassStep = 0;
unsigned long bassLastStep = 0;
float bassTempo = 200.0;              // ms per step
float bassFilterFreq = 400.0;         // Filter sweep frequency

// Echo Delay mode variables
float echoNotes[6] = {0};             // Circular buffer of note frequencies
float echoVolumes[6] = {0};           // Volume for each echo
int echoWriteIndex = 0;
unsigned long echoLastPlay = 0;
int echoPlayIndex = 0;
float echoDelayMs = 300.0;            // Delay between echoes
bool echoPlaying = false;
unsigned long echoStartTime = 0;

// Battle Mode variables
float battleFreq[2] = {440.0, 440.0}; // Current frequency for each player
float battleVol[2] = {0.0, 0.0};      // Current volume for each player
int battleScore[2] = {0, 0};          // "Dominance" score
unsigned long battleLastUpdate = 0;

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
const unsigned long SENSOR_READ_INTERVAL = 100;  // ms
uint8_t currentSensorChannel = 0;  // 0 = CH0, 1 = CH1, 2 = BOTH

// LED Matrix Configuration
#define LED_PIN 0           // Data pin for LEFT LED matrix
#define LED_PIN_R 1         // Data pin for RIGHT LED matrix
#define NUM_LEDS 64         // Number of LEDs per matrix (64 for 8x8)
#define MATRIX_WIDTH 8      // Matrix width (columns)
#define MATRIX_HEIGHT 8     // Matrix height (rows)
#define LED_TYPE WS2812B    // LED type (WS2812/NeoPixel)
#define COLOR_ORDER GRB     // Color order for WS2812B
CRGB leds[NUM_LEDS];        // Left LED matrix array
CRGB leds_r[NUM_LEDS];      // Right LED matrix array

// LED Matrix Layout: SERPENTINE (zigzag pattern)
// Row 0: LED 0  →  1  →  2  →  3  →  4  →  5  →  6  →  7
// Row 1: LED 15 ← 14 ← 13 ← 12 ← 11 ← 10 ← 9  ← 8
// Row 2: LED 16 → 17 → 18 → 19 → 20 → 21 → 22 → 23
// ... and so on

// LED Test Mode Variables
enum LEDColor {
  LED_RED,
  LED_GREEN,
  LED_BLUE,
  LED_YELLOW,
  LED_WHITE,
  LED_OFF
};
LEDColor currentLEDColor = LED_RED;
int selectedLED = 0;  // Currently selected LED (0-7)

// LED Visualization Settings
bool ledVisualizationEnabled = true;  // Enable/disable LED visualization in DISTANCE mode

// ============================================================================
// OLED DISPLAY CONFIGURATION
// ============================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // No reset pin (shares I2C bus with VL53L5CX CH0)
#define OLED_ADDR 0x3C // I2C address

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// ROTARY ENCODER CONFIGURATION
// ============================================================================
#define ENCODER_PIN_A 2    // CLK
#define ENCODER_PIN_B 3    // DT
#define ENCODER_BUTTON 4   // SW (push button)

Encoder knob(ENCODER_PIN_A, ENCODER_PIN_B);

// Encoder state variables
long lastEncoderPosition = 0;
bool lastButtonState = HIGH;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 50;  // ms

// Simulated ToF sensor values (raw)
int pitchHandRaw = 350;  // mm
int exprHandRaw = 400;   // mm

// Smoothed sensor values
float pitchHandSmooth = 350.0;
float exprHandSmooth = 400.0;

// Current zone
Zone currentZone = ZONE_B_MID;

// Current note state
int currentNoteIndex = 0;
int currentMidiNote = 60;  // Middle C
float currentFrequency = 261.63;  // Hz
bool noteActive = false;

// Timing
unsigned long lastUpdateTime = 0;
unsigned long lastPluckTime = 0;
const unsigned long REPLUCK_INTERVAL_MS = 800;  // Re-pluck every 800ms to sustain

// Vibrato
float vibratoPhase = 0.0;
const float VIBRATO_RATE = 5.0;  // Hz
const float VIBRATO_DEPTH = 0.02;  // ±2% pitch variation

// ============================================================================
// SCALE DEFINITIONS
// ============================================================================

// Pentatonic scale intervals (semitones from root)
const int pentatonicScale[] = {0, 2, 4, 7, 9};
const int pentatonicSize = 5;

// Major scale intervals
const int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
const int majorSize = 7;

// Minor scale intervals (natural minor)
const int minorScale[] = {0, 2, 3, 5, 7, 8, 10};
const int minorSize = 7;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void printHelp();
void handleCommand(char c);
void setupAudio();
void updateStringVoice();
void updateThereminVoice();
void processDualTheremin();
void processDualDrums();
void processDualPiano();
void processGridSequencer();
void processRhythmTapper();
void processArpeggiator();
void processDjScratch();
void processStepSequencer();
void processDualLoop();
void processChordJam();
void processDroneSolo();
void processRainMode();
void processBassMachine();
void processEchoDelay();
void processBattleMode();
void calculateHandMetrics(uint16_t grid[8][8], float& avgDistance, int& centroidX, int& centroidY, int& activeZones);
void triggerDrumHit(Zone zone, float velocity);
void applyZoneBehavior();
void playScaleNote(int noteIndex);
Zone calculateZone(float distance);
int distanceToNoteIndex(float distance, Zone zone);
float noteIndexToFrequency(int noteIndex, ScaleType scale, Zone zone);
void panicMute();
bool initDistanceSensor(uint8_t channel);
void readDistanceGrid(uint8_t channel);
void printDistanceGrid(uint8_t channel);
void printBothGrids();
void smoothSensorValues();
void panicMute();
void setupLEDs();
void setLED(int index, CRGB color);
void clearAllLEDs();
CRGB getLEDColor(LEDColor colorEnum);
CRGB distanceToColor(uint16_t distance_mm);
void updateLEDsFromSensorRow(uint8_t channel, uint8_t row);
void updateLEDsFromFullGrid(uint8_t channel);
int xyToLEDIndex(int row, int col);
void setupOLED();
void showLoadingScreen(const char* label, int step, int total);
void updateOLEDDisplay();
void handleEncoder();
void handleEncoderButton();
void switchToMode(PlayMode newMode);
const char* getModeString(PlayMode mode);
const char* getScaleString(ScaleType scale);

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

  Serial.println("Mode: STRING THEREMIN");
  Serial.println();
  printHelp();

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

  // Check for serial commands
  if (Serial.available() > 0) {
    char c = Serial.read();
    handleCommand(c);
  }

  // Update sensor simulation and audio at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL_MS) {
    lastUpdateTime = currentTime;

    // Smooth sensor values
    smoothSensorValues();

    // Update based on current mode
    if (currentMode == MODE_STRING_THEREMIN) {
      updateStringVoice();
    } else if (currentMode == MODE_DUAL_THEREMIN) {
      if (currentTime - lastDualThereminUpdate >= DUAL_THEREMIN_UPDATE_INTERVAL) {
        lastDualThereminUpdate = currentTime;
        processDualTheremin();
      }
    } else if (currentMode == MODE_DUAL_PIANO) {
      if (currentTime - lastDualThereminUpdate >= DUAL_THEREMIN_UPDATE_INTERVAL) {
        lastDualThereminUpdate = currentTime;
        processDualPiano();
      }
    } else if (currentMode == MODE_GRID_SEQUENCER) {
      if (currentTime - lastDualThereminUpdate >= DUAL_THEREMIN_UPDATE_INTERVAL) {
        lastDualThereminUpdate = currentTime;
        processGridSequencer();
      }
    } else if (currentMode == MODE_RHYTHM_TAPPER) {
      if (currentTime - lastDualThereminUpdate >= DUAL_THEREMIN_UPDATE_INTERVAL) {
        lastDualThereminUpdate = currentTime;
        processRhythmTapper();
      }
    } else if (currentMode == MODE_ARPEGGIATOR) {
      processArpeggiator();
    } else if (currentMode == MODE_STEP_SEQ) {
      // Step sequencer - auto-looping beat machine
      processStepSequencer();
    } else if (currentMode == MODE_DUAL_LOOP) {
      // Dual loop - left hand melody, right hand drums
      processDualLoop();
    } else if (currentMode == MODE_CHORD_JAM) {
      processChordJam();
    } else if (currentMode == MODE_DRONE_SOLO) {
      processDroneSolo();
    } else if (currentMode == MODE_RAIN_MODE) {
      processRainMode();
    } else if (currentMode == MODE_BASS_MACHINE) {
      processBassMachine();
    } else if (currentMode == MODE_ECHO_DELAY) {
      processEchoDelay();
    } else if (currentMode == MODE_BATTLE_MODE) {
      processBattleMode();
    } else if (currentMode == MODE_DISTANCE_SENSOR) {
      // Read and display distance sensor grid(s)
      if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
        lastSensorRead = currentTime;

        if (currentSensorChannel == 0 && sensor_ch0_initialized) {
          // Show CH0 only
          readDistanceGrid(0);
          printDistanceGrid(0);
          // Update full 8x8 LED matrix from CH0
          if (ledVisualizationEnabled) {
            updateLEDsFromFullGrid(0);
          }
        } else if (currentSensorChannel == 1 && sensor_ch1_initialized) {
          // Show CH1 only
          readDistanceGrid(1);
          printDistanceGrid(1);
          // Update full 8x8 LED matrix from CH1
          if (ledVisualizationEnabled) {
            updateLEDsFromFullGrid(1);
          }
        } else if (currentSensorChannel == 2) {
          // Show BOTH side by side
          if (sensor_ch0_initialized) readDistanceGrid(0);
          if (sensor_ch1_initialized) readDistanceGrid(1);
          if (sensor_ch0_initialized || sensor_ch1_initialized) {
            printBothGrids();
          }
          // Update full 8x8 LED matrix from CH0 (when viewing both)
          if (ledVisualizationEnabled && sensor_ch0_initialized) {
            updateLEDsFromFullGrid(0);
          }
        }
      }
    } else if (currentMode == MODE_CALIB_SERIAL_CH0) {
      // Calibration: stream CH0 grid to serial monitor only
      if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
        lastSensorRead = currentTime;
        if (sensor_ch0_initialized) {
          readDistanceGrid(0);
          printDistanceGrid(0);
        }
      }
    } else if (currentMode == MODE_CALIB_SERIAL_CH1) {
      // Calibration: stream CH1 grid to serial monitor only
      if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
        lastSensorRead = currentTime;
        if (sensor_ch1_initialized) {
          readDistanceGrid(1);
          printDistanceGrid(1);
        }
      }
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

void printHelp() {
  Serial.println("\n=== TOUCHLESS INSTRUMENT HELP ===");

  // Current state
  Serial.print("Mode: ");
  Serial.println(currentMode == MODE_STRING_THEREMIN ? "STRING THEREMIN" : "GESTURE DRUM");
  Serial.print("Pitch Hand: ");
  Serial.print((int)pitchHandSmooth);
  Serial.print(" mm, Expr Hand: ");
  Serial.print((int)exprHandSmooth);
  Serial.println(" mm");
  Serial.print("Zone: ");
  if (currentZone == ZONE_A_NEAR) Serial.println("A (NEAR/BASS)");
  else if (currentZone == ZONE_B_MID) Serial.println("B (MID/MELODY)");
  else Serial.println("C (FAR/HARMONICS)");
  Serial.print("Scale: ");
  if (currentScale == SCALE_PENTATONIC) Serial.println("Pentatonic");
  else if (currentScale == SCALE_MAJOR) Serial.println("Major");
  else Serial.println("Minor");

  Serial.println("\n--- COMMANDS ---");
  Serial.println("Mode:");
  Serial.println("  M = Cycle modes (STRING -> DRUM -> THEREMIN -> DISTANCE -> LED TEST)");
  Serial.println("  x = Panic mute (all notes off)");
  Serial.println();
  Serial.println("Piano Keys (STRING mode - play scale notes):");
  Serial.println("  a s d f g h j k l ; = Play notes 1-10 in current scale");
  Serial.println();
  Serial.println("Drum Triggers (DRUM mode):");
  Serial.println("  [ = LOUD hit, ] = quiet hit");
  Serial.println("  e/r/t = Select drum (kick/snare/hi-hat)");
  Serial.println();
  Serial.println("Theremin Controls (THEREMIN mode):");
  Serial.println("  Pitch: q/w = down/up (10 Hz steps)");
  Serial.println("  Pitch: e/r/t = Low/Mid/High register");
  Serial.println("  Volume: a/s = quieter/louder");
  Serial.println("  Sound is CONTINUOUS - simulates hands at fixed distance");
  Serial.println();
  Serial.println("Distance Sensor (DISTANCE mode):");
  Serial.println("  Displays live 8x8 distance grid from VL53L5CX");
  Serial.println("  C = Cycle through CH0, CH1, and BOTH (side by side)");
  Serial.println("  L = Toggle LED visualization (full 8x8 grid -> 64 LEDs)");
  Serial.print("  LED Visualization: ");
  Serial.println(ledVisualizationEnabled ? "ON" : "OFF");
  Serial.println();
  Serial.println("LED Test (LED TEST mode):");
  Serial.println("  0-63 = Type LED number to select (e.g., type '15' then Enter)");
  Serial.println("  R/G/B/Y/W = Set color (Red/Green/Blue/Yellow/White)");
  Serial.println("  O = Turn off all LEDs");
  Serial.println("  F = Fill all LEDs with current color");
  Serial.println();
  Serial.println("Other:");
  Serial.println("  v = Toggle vibrato (STRING/THEREMIN modes)");
  Serial.println();
  Serial.println("Scale Selection (STRING mode):");
  Serial.println("  1 = Pentatonic");
  Serial.println("  2 = Major");
  Serial.println("  3 = Minor");
  Serial.println();
  Serial.println("Settings:");
  Serial.println("  m = Toggle mute-if-idle");
  Serial.println("  c = Toggle continuous sound (auto re-pluck)");
  Serial.println("  + = Increase volume");
  Serial.println("  - = Decrease volume");
  Serial.println("  ? = Show this help");
  Serial.println("====================\n");
}

void handleCommand(char c) {
  switch (c) {
    // Mode toggle - cycle through all modes (enum order, wraps at MODE_LAST)
    case 'M':
      {
        int nextMode = (int)currentMode + 1;
        if (nextMode > MODE_LAST) nextMode = MODE_FIRST;
        switchToMode((PlayMode)nextMode);
      }
      break;

    // Toggle sensor channel (CH0 / CH1 / BOTH) in DISTANCE mode
    case 'C':
      if (currentMode == MODE_DISTANCE_SENSOR) {
        currentSensorChannel = (currentSensorChannel + 1) % 3;  // Cycle 0 -> 1 -> 2 -> 0

        if (currentSensorChannel == 0) {
          Serial.println(">> Switched to CH0 (I2C0)");
        } else if (currentSensorChannel == 1) {
          Serial.println(">> Switched to CH1 (I2C1)");
        } else {
          Serial.println(">> Switched to BOTH (side by side)");
        }
      }
      break;

    // Toggle LED visualization in DISTANCE mode
    case 'L':
      if (currentMode == MODE_DISTANCE_SENSOR) {
        ledVisualizationEnabled = !ledVisualizationEnabled;
        Serial.print(">> LED Visualization: ");
        Serial.println(ledVisualizationEnabled ? "ON" : "OFF");
        if (!ledVisualizationEnabled) {
          clearAllLEDs();  // Turn off LEDs when disabled
        }
      }
      break;

    // Panic mute
    case 'x':
      Serial.println(">> PANIC MUTE");
      panicMute();
      break;

    // Pitch controls
    case 'q':  // Closer distance
      pitchHandRaw -= DIST_STEP;
      if (pitchHandRaw < DIST_MIN) pitchHandRaw = DIST_MIN;
      Serial.print(">> Pitch hand: ");
      Serial.print(pitchHandRaw);
      Serial.println(" mm");
      break;

    case 'w':  // Farther distance
      pitchHandRaw += DIST_STEP;
      if (pitchHandRaw > DIST_MAX) pitchHandRaw = DIST_MAX;
      Serial.print(">> Pitch hand: ");
      Serial.print(pitchHandRaw);
      Serial.println(" mm");
      break;

    // Zone presets
    case 'e':  // Zone A
      pitchHandRaw = ZONE_A_DEFAULT;
      currentZone = ZONE_A_NEAR;
      Serial.println(">> Pitch hand -> Zone A (150mm)");
      break;

    case 'r':  // Zone B
      pitchHandRaw = ZONE_B_DEFAULT;
      currentZone = ZONE_B_MID;
      Serial.println(">> Pitch hand -> Zone B (350mm)");
      break;

    case 't':  // Zone C
      pitchHandRaw = ZONE_C_DEFAULT;
      currentZone = ZONE_C_FAR;
      Serial.println(">> Pitch hand -> Zone C (650mm)");
      break;

    case 'v':  // Vibrato toggle
      if (currentMode == MODE_STRING_THEREMIN) {
        vibratoEnabled = !vibratoEnabled;
        Serial.print(">> Vibrato: ");
        Serial.println(vibratoEnabled ? "ON" : "OFF");
      }
      break;

    // Piano keys (STRING mode)
    case 'a':  // Note 1
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(0);
      }
      break;

    case 's':  // Note 2
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(1);
      }
      break;

    case 'd':  // Note 3
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(2);
      }
      break;

    case 'f':  // Note 4 (or drum gesture)
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(3);
      } else {
        // DRUM mode - fast jab
        Serial.println(">> Fast jab!");
        triggerDrumHit(currentZone, 1.0);  // Maximum velocity (LOUD)
      }
      break;

    case 'g':  // Note 5 (or drum gesture)
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(4);
      } else {
        // DRUM mode - gentle tap
        Serial.println(">> Gentle tap");
        triggerDrumHit(currentZone, 0.2);  // Very low velocity (QUIET)
      }
      break;

    case 'h':  // Note 6
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(5);
      }
      break;

    case 'j':  // Note 7
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(6);
      }
      break;

    case 'k':  // Note 8
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(7);
      }
      break;

    case 'l':  // Note 9
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(8);
      }
      break;

    case ';':  // Note 10
      if (currentMode == MODE_STRING_THEREMIN) {
        playScaleNote(9);
      }
      break;

    // Scale selection (not in LED TEST mode) / LED selection (in LED TEST mode)
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      if (currentMode == MODE_LED_TEST) {
        // LED Test Mode - LED selection (0-7)
        selectedLED = c - '0';
        Serial.print(">> Selected LED: ");
        Serial.println(selectedLED);
        // Light up the selected LED with current color
        clearAllLEDs();
        setLED(selectedLED, getLEDColor(currentLEDColor));
      } else {
        // Scale selection (only 1-3 are valid)
        if (c == '1') {
          currentScale = SCALE_PENTATONIC;
          Serial.println(">> Scale: Pentatonic");
        } else if (c == '2') {
          currentScale = SCALE_MAJOR;
          Serial.println(">> Scale: Major");
        } else if (c == '3') {
          currentScale = SCALE_MINOR;
          Serial.println(">> Scale: Minor");
        }
      }
      break;

    // Settings
    case 'm':
      muteIfIdle = !muteIfIdle;
      Serial.print(">> Mute-if-idle: ");
      Serial.println(muteIfIdle ? "ON" : "OFF");
      break;

    case 'c':
      continuousSound = !continuousSound;
      Serial.print(">> Continuous sound: ");
      Serial.println(continuousSound ? "ON (auto re-pluck)" : "OFF (single pluck)");
      break;

    case '+':
    case '=':
      masterVolume += 0.05;
      if (masterVolume > 1.0) masterVolume = 1.0;
      sgtl5000_1.volume(masterVolume);
      Serial.print(">> Volume: ");
      Serial.print(masterVolume * 100.0, 0);
      Serial.println("%");
      break;

    case '-':
    case '_':
      masterVolume -= 0.05;
      if (masterVolume < 0.0) masterVolume = 0.0;
      sgtl5000_1.volume(masterVolume);
      Serial.print(">> Volume: ");
      Serial.print(masterVolume * 100.0, 0);
      Serial.println("%");
      break;

    // LED Test Mode - Color selection
    case 'R':
      if (currentMode == MODE_LED_TEST) {
        currentLEDColor = LED_RED;
        Serial.println(">> Color: RED");
        setLED(selectedLED, getLEDColor(currentLEDColor));
      }
      break;

    case 'G':
      if (currentMode == MODE_LED_TEST) {
        currentLEDColor = LED_GREEN;
        Serial.println(">> Color: GREEN");
        setLED(selectedLED, getLEDColor(currentLEDColor));
      }
      break;

    case 'B':
      if (currentMode == MODE_LED_TEST) {
        currentLEDColor = LED_BLUE;
        Serial.println(">> Color: BLUE");
        setLED(selectedLED, getLEDColor(currentLEDColor));
      }
      break;

    case 'Y':
      if (currentMode == MODE_LED_TEST) {
        currentLEDColor = LED_YELLOW;
        Serial.println(">> Color: YELLOW");
        setLED(selectedLED, getLEDColor(currentLEDColor));
      }
      break;

    case 'W':
      if (currentMode == MODE_LED_TEST) {
        currentLEDColor = LED_WHITE;
        Serial.println(">> Color: WHITE");
        setLED(selectedLED, getLEDColor(currentLEDColor));
      }
      break;

    case 'O':
      if (currentMode == MODE_LED_TEST) {
        currentLEDColor = LED_OFF;
        Serial.println(">> Color: OFF");
        clearAllLEDs();
      }
      break;

    case '?':
      printHelp();
      break;

    case '\n':
    case '\r':
      // Ignore newlines
      break;

    default:
      // Ignore unknown commands silently
      break;
  }
}

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

void smoothSensorValues() {
  // Simple exponential moving average (low-pass filter)
  pitchHandSmooth = pitchHandSmooth * (1.0 - SMOOTH_ALPHA) + pitchHandRaw * SMOOTH_ALPHA;
  exprHandSmooth = exprHandSmooth * (1.0 - SMOOTH_ALPHA) + exprHandRaw * SMOOTH_ALPHA;
}

Zone calculateZone(float distance) {
  if (distance <= ZONE_A_MAX) {
    return ZONE_A_NEAR;
  } else if (distance <= ZONE_B_MAX) {
    return ZONE_B_MID;
  } else {
    return ZONE_C_FAR;
  }
}

int distanceToNoteIndex(float distance, Zone zone) {
  // Map distance to note index within zone
  // Each zone spans about 250mm, map to ~12 semitones (1 octave)

  int zoneStart = 0;
  int zoneRange = 0;

  if (zone == ZONE_A_NEAR) {
    zoneStart = DIST_MIN;
    zoneRange = ZONE_A_MAX - DIST_MIN;  // 170mm
  } else if (zone == ZONE_B_MID) {
    zoneStart = ZONE_A_MAX;
    zoneRange = ZONE_B_MAX - ZONE_A_MAX;  // 250mm
  } else {  // ZONE_C_FAR
    zoneStart = ZONE_B_MAX;
    zoneRange = DIST_MAX - ZONE_B_MAX;  // 300mm
  }

  // Normalize distance within zone (0.0 to 1.0)
  float normalized = (distance - zoneStart) / (float)zoneRange;
  normalized = constrain(normalized, 0.0, 1.0);

  // Map to note index (0-11 for 12 semitones)
  int noteIndex = (int)(normalized * 11.0);

  return noteIndex;
}

float noteIndexToFrequency(int noteIndex, ScaleType scale, Zone zone) {
  // Determine base MIDI note based on zone
  int baseMidi = 0;

  if (zone == ZONE_A_NEAR) {
    baseMidi = 45;  // A2 (bass register)
  } else if (zone == ZONE_B_MID) {
    baseMidi = 57;  // A3 (melody register)
  } else {  // ZONE_C_FAR
    baseMidi = 69;  // A4 (high register)
  }

  // Get scale intervals
  const int* scaleIntervals;
  int scaleSize;

  if (scale == SCALE_PENTATONIC) {
    scaleIntervals = pentatonicScale;
    scaleSize = pentatonicSize;
  } else if (scale == SCALE_MAJOR) {
    scaleIntervals = majorScale;
    scaleSize = majorSize;
  } else {  // SCALE_MINOR
    scaleIntervals = minorScale;
    scaleSize = minorSize;
  }

  // Map note index to scale degree
  int octave = noteIndex / scaleSize;
  int degree = noteIndex % scaleSize;

  // Calculate MIDI note
  int midiNote = baseMidi + (octave * 12) + scaleIntervals[degree];

  // Convert MIDI to frequency: f = 440 * 2^((n-69)/12)
  float frequency = 440.0 * pow(2.0, (midiNote - 69) / 12.0);

  return frequency;
}

void updateStringVoice() {
  // Check for presence
  if (pitchHandSmooth > PRESENCE_THRESHOLD) {
    // No presence - mute if mute-if-idle is enabled
    if (muteIfIdle && noteActive) {
      stringEnvelope.noteOff();
      noteActive = false;
    }
    return;
  }

  // Calculate current zone
  Zone newZone = calculateZone(pitchHandSmooth);

  // Get note index from distance
  int newNoteIndex = distanceToNoteIndex(pitchHandSmooth, newZone);

  // Calculate frequency
  float baseFreq = noteIndexToFrequency(newNoteIndex, currentScale, newZone);

  // Apply vibrato if enabled
  float finalFreq = baseFreq;
  if (vibratoEnabled) {
    vibratoPhase += VIBRATO_RATE * (UPDATE_INTERVAL_MS / 1000.0) * 2.0 * PI;
    if (vibratoPhase > 2.0 * PI) vibratoPhase -= 2.0 * PI;

    float vibrato = sin(vibratoPhase) * VIBRATO_DEPTH;
    finalFreq = baseFreq * (1.0 + vibrato);
  }

  // Check if note changed
  bool noteChanged = (newNoteIndex != currentNoteIndex) || (newZone != currentZone);

  // Check if we need to re-pluck for sustain (Karplus-Strong decays naturally)
  unsigned long currentTime = millis();
  bool needRepluck = continuousSound && (currentTime - lastPluckTime) > REPLUCK_INTERVAL_MS;

  if (noteChanged || (noteActive && needRepluck)) {
    // Trigger new note with Karplus-Strong
    float velocity = noteChanged ? 0.7 : 0.4;  // Softer for sustain replucks
    stringVoice.noteOn(finalFreq, velocity);
    stringEnvelope.noteOn();  // TRIGGER THE ENVELOPE!

    // Update state
    currentNoteIndex = newNoteIndex;
    currentZone = newZone;
    currentFrequency = finalFreq;
    noteActive = true;
    lastPluckTime = currentTime;

    // Apply zone-specific filter settings (only on note change)
    if (noteChanged) {
      applyZoneBehavior();
    }
  }

  // Map expression hand to volume (closer = louder)
  // Invert: closer (lower value) = higher volume
  float exprNorm = 1.0 - ((exprHandSmooth - DIST_MIN) / (float)(DIST_MAX - DIST_MIN));
  exprNorm = constrain(exprNorm, 0.0, 1.0);

  // Apply to mixer gain (use masterVolume)
  mixer1.gain(0, exprNorm * 0.8 * masterVolume);
}

void applyZoneBehavior() {
  // Adjust filter based on zone for different timbres
  if (currentZone == ZONE_A_NEAR) {
    // Bass zone: darker, warmer sound
    stringFilter.frequency(1200);
    stringFilter.resonance(0.9);
  } else if (currentZone == ZONE_B_MID) {
    // Melody zone: balanced
    stringFilter.frequency(2000);
    stringFilter.resonance(1.2);
  } else {  // ZONE_C_FAR
    // Harmonics zone: brighter, more resonant
    stringFilter.frequency(3500);
    stringFilter.resonance(1.8);
  }
}

void triggerDrumHit(Zone zone, float velocity) {
  // Trigger different drums based on zone with velocity control
  if (zone == ZONE_A_NEAR) {
    // Kick drum
    Serial.print("   -> KICK (vel: ");
    Serial.print(velocity, 2);
    Serial.print(", gain: ");
    Serial.print(velocity * 0.8, 2);
    Serial.println(")");

    // Adjust kick gain based on velocity - MORE DRAMATIC RANGE
    mixer1.gain(1, velocity * 0.8);  // 0.2 → 0.16 (quiet), 1.0 → 0.8 (LOUD)
    kickDrum.noteOn();

  } else if (zone == ZONE_B_MID) {
    // Snare drum
    Serial.print("   -> SNARE (vel: ");
    Serial.print(velocity, 2);
    Serial.print(", gain: ");
    Serial.print(velocity * 0.7, 2);
    Serial.println(")");

    // Adjust snare gain based on velocity - MORE DRAMATIC RANGE
    mixer1.gain(2, velocity * 0.7);  // 0.2 → 0.14 (quiet), 1.0 → 0.7 (LOUD)
    snareDrum.noteOn();

  } else {  // ZONE_C_FAR
    // Hi-hat
    Serial.print("   -> HI-HAT (vel: ");
    Serial.print(velocity, 2);
    Serial.print(", gain: ");
    Serial.print(velocity * 0.6, 2);
    Serial.println(")");

    // Adjust hi-hat gain and decay based on velocity - MORE DRAMATIC RANGE
    mixer1.gain(3, velocity * 0.6);  // 0.2 → 0.12 (quiet), 1.0 → 0.6 (LOUD)

    // Adjust decay time based on velocity (faster = shorter)
    float decayTime = 20.0 + (velocity * 80.0);  // 0.2 → 36ms, 1.0 → 100ms
    hatEnvelope.decay(decayTime);

    hatEnvelope.noteOn();
    delay(1);
    hatEnvelope.noteOff();
  }
}

void panicMute() {
  // Stop all audio
  stringEnvelope.noteOff();
  hatEnvelope.noteOff();
  noteActive = false;
  vibratoPhase = 0.0;
}

void playScaleNote(int noteIndex) {
  // Play a specific note from the current scale
  // Use Zone B (middle register) for piano keys
  Zone zone = ZONE_B_MID;

  // Calculate frequency for this note in the current scale
  float freq = noteIndexToFrequency(noteIndex, currentScale, zone);

  // Get note name for display
  const char* scaleNames[] = {"Pentatonic", "Major", "Minor"};
  const char* scaleName = scaleNames[currentScale];

  Serial.print(">> Play note ");
  Serial.print(noteIndex + 1);
  Serial.print(" (");
  Serial.print(scaleName);
  Serial.print(") @ ");
  Serial.print(freq, 1);
  Serial.println(" Hz");

  // Pluck the string
  stringVoice.noteOn(freq, 0.8);
  stringEnvelope.noteOn();

  // Update state
  noteActive = true;
  currentZone = zone;
  currentNoteIndex = noteIndex;
  currentFrequency = freq;
  lastPluckTime = millis();

  // Apply zone behavior
  applyZoneBehavior();
}

void updateThereminVoice() {
  // Continuous theremin sound - updates pitch and volume in real-time
  // This simulates hands held at fixed distances from two antennas

  float actualPitch = thereminPitch;

  // Apply vibrato if enabled
  if (vibratoEnabled) {
    vibratoPhase += 0.1;
    if (vibratoPhase > TWO_PI) vibratoPhase -= TWO_PI;
    float vibrato = sin(vibratoPhase) * 8.0;  // ±8 Hz vibrato
    actualPitch += vibrato;
  }

  // Update the string voice frequency continuously
  // For theremin, we use the string voice but keep it continuously triggered
  if (!noteActive) {
    // Start the sound if not already playing
    stringVoice.noteOn(actualPitch, 0.7);
    stringEnvelope.noteOn();
    noteActive = true;
  }

  // Update frequency (Karplus-Strong doesn't support real-time pitch changes well,
  // so we re-trigger periodically for pitch changes)
  static float lastPitch = 0.0;
  if (abs(actualPitch - lastPitch) > 1.0) {  // If pitch changed significantly
    stringVoice.noteOn(actualPitch, 0.7);
    lastPitch = actualPitch;
  }

  // Update volume via mixer
  mixer1.gain(0, thereminVolume * masterVolume);

  // Set filter for theremin tone (smooth, sine-like)
  stringFilter.frequency(actualPitch * 2.0);  // Filter at 2x fundamental
  stringFilter.resonance(0.7);  // Moderate resonance for smooth tone
}

// ============================================================================
// DUAL THEREMIN FUNCTIONS (Using ToF Sensors)
// ============================================================================

void calculateHandMetrics(uint16_t grid[8][8], float& avgDistance, int& centroidX, int& centroidY, int& activeZones) {
  // Calculate hand position and metrics from 8x8 distance grid
  // Only considers zones closer than 800mm as "active" (hand present)

  const int HAND_THRESHOLD = 800;  // mm - zones closer than this are considered "hand"

  long sumX = 0, sumY = 0;
  long sumDist = 0;
  int count = 0;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = grid[row][col];

      if (dist < HAND_THRESHOLD && dist > 50) {  // Valid hand detection
        sumX += col;
        sumY += row;
        sumDist += dist;
        count++;
      }
    }
  }

  if (count > 0) {
    centroidX = sumX / count;
    centroidY = sumY / count;
    avgDistance = (float)sumDist / count;
    activeZones = count;
  } else {
    // No hand detected
    centroidX = 4;  // Center
    centroidY = 4;
    avgDistance = 2000.0;  // Far away
    activeZones = 0;
  }
}

void processDualTheremin() {
  // Process dual-hand theremin using both ToF sensors
  // Left hand (CH0) controls pitch
  // Right hand (CH1) controls volume and expression

  // Read both sensors
  if (sensor_ch0_initialized) {
    readDistanceGrid(0);
  }
  if (sensor_ch1_initialized) {
    readDistanceGrid(1);
  }

  // Calculate hand metrics for both hands
  calculateHandMetrics(distanceGrid_ch0, leftHandAvgDist, leftHandCentroidX, leftHandCentroidY, leftHandZones);
  calculateHandMetrics(distanceGrid_ch1, rightHandAvgDist, rightHandCentroidX, rightHandCentroidY, rightHandZones);

  // LEFT HAND (CH0) - Pitch Control
  // Map average distance to pitch (closer = higher pitch)
  if (leftHandZones > 0) {
    // Hand detected - map distance to pitch
    float pitchRange = DUAL_PITCH_MAX - DUAL_PITCH_MIN;
    float distRange = DUAL_DIST_MAX - DUAL_DIST_MIN;

    // Invert: closer = higher pitch
    float normalizedDist = constrain(leftHandAvgDist, DUAL_DIST_MIN, DUAL_DIST_MAX);
    float pitchFactor = 1.0 - ((normalizedDist - DUAL_DIST_MIN) / distRange);

    dualThereminPitch = DUAL_PITCH_MIN + (pitchRange * pitchFactor);

    // Add pitch bend based on X position (left/right)
    // Center = no bend, left = bend down, right = bend up
    float bendAmount = (leftHandCentroidX - 3.5) * 20.0;  // ±70Hz max bend
    dualThereminPitch += bendAmount;

    // Constrain to valid range
    dualThereminPitch = constrain(dualThereminPitch, DUAL_PITCH_MIN, DUAL_PITCH_MAX);
  }

  // RIGHT HAND (CH1) - Volume and Expression Control
  if (rightHandZones > 0) {
    // Hand detected - map distance to volume
    float normalizedDist = constrain(rightHandAvgDist, DUAL_DIST_MIN, DUAL_DIST_MAX);
    dualThereminVolume = 1.0 - ((normalizedDist - DUAL_DIST_MIN) / (DUAL_DIST_MAX - DUAL_DIST_MIN));
    dualThereminVolume = constrain(dualThereminVolume, 0.0, 1.0);

    // Hand coverage (number of zones) controls filter brightness
    // More zones = brighter sound (higher filter cutoff)
    float coverageFactor = (float)rightHandZones / 64.0;  // 0.0 to 1.0
    dualThereminFilter = 500.0 + (coverageFactor * 3000.0);  // 500Hz to 3500Hz
  } else {
    // No hand detected - fade out volume
    dualThereminVolume *= 0.95;  // Smooth fade
    if (dualThereminVolume < 0.01) dualThereminVolume = 0.0;
  }

  // Update audio synthesis
  // Update pitch
  static float lastDualPitch = 0.0;
  if (abs(dualThereminPitch - lastDualPitch) > 1.0 || !noteActive) {
    stringVoice.noteOn(dualThereminPitch, 0.7);
    if (!noteActive) {
      stringEnvelope.noteOn();
      noteActive = true;
    }
    lastDualPitch = dualThereminPitch;
  }

  // Update volume
  mixer1.gain(0, dualThereminVolume * masterVolume);

  // Update filter for expression
  stringFilter.frequency(dualThereminFilter);
  stringFilter.resonance(0.7);

  // Visual feedback — left matrix = pitch hand, right matrix = volume hand
  if (ledVisualizationEnabled) {
    clearAllLEDs();

    // Left matrix: draw left hand position (blue - pitch control)
    if (leftHandZones > 0) {
      int ledIndex = leftHandCentroidY * 8 + leftHandCentroidX;
      if (ledIndex >= 0 && ledIndex < 64) {
        leds[ledIndex] = CRGB::Blue;
      }
    }

    // Right matrix: draw right hand position (red - volume control)
    if (rightHandZones > 0) {
      int ledIndex = rightHandCentroidY * 8 + rightHandCentroidX;
      if (ledIndex >= 0 && ledIndex < 64) {
        leds_r[ledIndex] = CRGB::Red;
      }
    }

    FastLED.show();
  }
}

void processDualDrums() {
  // Dual-hand spatial drum kit
  // Left hand: Trigger drums based on position (quadrants) and velocity (speed of movement)
  // Right hand: Control effects/volume

  // Read both sensors
  if (sensor_ch0_initialized) {
    readDistanceGrid(0);
  }
  if (sensor_ch1_initialized) {
    readDistanceGrid(1);
  }

  // Calculate hand metrics
  calculateHandMetrics(distanceGrid_ch0, leftHandAvgDist, leftHandCentroidX, leftHandCentroidY, leftHandZones);
  calculateHandMetrics(distanceGrid_ch1, rightHandAvgDist, rightHandCentroidX, rightHandCentroidY, rightHandZones);

  // LEFT HAND - Drum Triggering
  // Detect quick hand movements (velocity) to trigger drums
  if (leftHandZones > 0) {
    float distChange = abs(leftHandAvgDist - lastLeftHandDist);
    unsigned long currentTime = millis();

    // Check if hand moved quickly enough to trigger a hit
    if (distChange > DRUM_VELOCITY_THRESHOLD && (currentTime - lastDrumHitTime) > DRUM_HIT_COOLDOWN) {
      // Determine which drum based on hand position (quadrants)
      Zone drumZone;

      // Divide 8x8 grid into quadrants
      if (leftHandCentroidX < 4 && leftHandCentroidY < 4) {
        drumZone = ZONE_A_NEAR;  // Top-left = KICK
      } else if (leftHandCentroidX >= 4 && leftHandCentroidY < 4) {
        drumZone = ZONE_B_MID;   // Top-right = SNARE
      } else if (leftHandCentroidX < 4 && leftHandCentroidY >= 4) {
        drumZone = ZONE_C_FAR;   // Bottom-left = HI-HAT
      } else {
        drumZone = ZONE_B_MID;   // Bottom-right = TOM (use snare voice)
      }

      // Calculate velocity from movement speed
      float velocity = constrain(distChange / 200.0, 0.3, 1.0);

      // Trigger the drum
      triggerDrumHit(drumZone, velocity);
      lastDrumHitTime = currentTime;

      // LED flash feedback
      if (ledVisualizationEnabled) {
        clearAllLEDs();
        // Flash the quadrant that was hit
        for (int y = (leftHandCentroidY < 4 ? 0 : 4); y < (leftHandCentroidY < 4 ? 4 : 8); y++) {
          for (int x = (leftHandCentroidX < 4 ? 0 : 4); x < (leftHandCentroidX < 4 ? 4 : 8); x++) {
            int ledIndex = y * 8 + x;
            leds[ledIndex] = CRGB::Yellow;
          }
        }
        FastLED.show();
      }
    }
  }

  lastLeftHandDist = leftHandAvgDist;

  // RIGHT HAND - Volume/Effects Control
  if (rightHandZones > 0) {
    // Control master volume with right hand distance
    float normalizedDist = constrain(rightHandAvgDist, DUAL_DIST_MIN, DUAL_DIST_MAX);
    masterVolume = 1.0 - ((normalizedDist - DUAL_DIST_MIN) / (DUAL_DIST_MAX - DUAL_DIST_MIN));
    masterVolume = constrain(masterVolume, 0.2, 1.0);  // Min 0.2 to always hear something
  }

  // Visual feedback - show hand positions
  if (ledVisualizationEnabled && millis() - lastDrumHitTime > 100) {
    clearAllLEDs();

    // Left matrix: show left hand (drum trigger)
    if (leftHandZones > 0) {
      int ledIndex = leftHandCentroidY * 8 + leftHandCentroidX;
      if (ledIndex >= 0 && ledIndex < 64) {
        leds[ledIndex] = CRGB::Cyan;
      }
    }

    // Right matrix: show right hand (volume control)
    if (rightHandZones > 0) {
      int ledIndex = rightHandCentroidY * 8 + rightHandCentroidX;
      if (ledIndex >= 0 && ledIndex < 64) {
        leds_r[ledIndex] = CRGB::Magenta;
      }
    }

    FastLED.show();
  }
}

void processDualPiano() {
  // Dual-hand piano/string instrument
  // Left hand: Select note from scale based on height
  // Right hand: Control expression (velocity, volume)

  // Read both sensors
  if (sensor_ch0_initialized) {
    readDistanceGrid(0);
  }
  if (sensor_ch1_initialized) {
    readDistanceGrid(1);
  }

  // Calculate hand metrics
  calculateHandMetrics(distanceGrid_ch0, leftHandAvgDist, leftHandCentroidX, leftHandCentroidY, leftHandZones);
  calculateHandMetrics(distanceGrid_ch1, rightHandAvgDist, rightHandCentroidX, rightHandCentroidY, rightHandZones);

  // LEFT HAND - Note Selection
  int selectedNote = -1;
  if (leftHandZones > 0) {
    // Map distance to note index in current scale
    float normalizedDist = constrain(leftHandAvgDist, DUAL_DIST_MIN, DUAL_DIST_MAX);
    float noteFactor = (normalizedDist - DUAL_DIST_MIN) / (DUAL_DIST_MAX - DUAL_DIST_MIN);

    selectedNote = (int)(noteFactor * PIANO_NOTE_MAX);
    selectedNote = constrain(selectedNote, PIANO_NOTE_MIN, PIANO_NOTE_MAX);
  }

  // RIGHT HAND - Expression Control
  if (rightHandZones > 0) {
    // Map distance and coverage to velocity
    float normalizedDist = constrain(rightHandAvgDist, DUAL_DIST_MIN, DUAL_DIST_MAX);
    float distFactor = 1.0 - ((normalizedDist - DUAL_DIST_MIN) / (DUAL_DIST_MAX - DUAL_DIST_MIN));

    // Hand coverage affects velocity too
    float coverageFactor = (float)rightHandZones / 64.0;

    pianoNoteVelocity = (distFactor * 0.7) + (coverageFactor * 0.3);
    pianoNoteVelocity = constrain(pianoNoteVelocity, 0.1, 1.0);
  } else {
    pianoNoteVelocity = 0.0;
  }

  // Play note if both hands are present and note changed
  if (selectedNote != currentPianoNote && selectedNote >= 0 && pianoNoteVelocity > 0.1) {
    // Play the new note
    playScaleNote(selectedNote);
    currentPianoNote = selectedNote;
    lastPianoNoteTime = millis();
  } else if (leftHandZones == 0) {
    // No left hand detected - stop note
    currentPianoNote = -1;
  }

  // Update volume based on right hand
  mixer1.gain(0, pianoNoteVelocity * masterVolume);

  // Visual feedback - show notes as vertical bars
  if (ledVisualizationEnabled) {
    clearAllLEDs();

    // Left matrix: show selected note as a vertical bar
    if (selectedNote >= 0) {
      int barHeight = map(selectedNote, 0, PIANO_NOTE_MAX, 0, 7);
      for (int y = 7; y >= (7 - barHeight); y--) {
        for (int x = 0; x < 8; x++) {
          int ledIndex = y * 8 + x;
          leds[ledIndex] = CRGB::Blue;
        }
      }
    }

    // Right matrix: show velocity as a vertical bar
    if (pianoNoteVelocity > 0.0) {
      int velHeight = (int)(pianoNoteVelocity * 7.0);
      for (int y = 7; y >= (7 - velHeight); y--) {
        for (int x = 0; x < 8; x++) {
          int ledIndex = y * 8 + x;
          leds_r[ledIndex] = CRGB::Red;
        }
      }
    }

    FastLED.show();
  }
}

void processGridSequencer() {
  // 64-zone MIDI pad — uses BOTH sensors and BOTH LED matrices
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  unsigned long currentTime = millis();

  // --- LEFT sensor → left LED matrix (lower octaves) ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      int zoneIndex = row * 8 + col;
      bool wasTouched = gridZoneActive[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);

      if (isTouched && !wasTouched && (currentTime - gridZoneLastTrigger[row][col]) > GRID_DEBOUNCE_MS) {
        int noteIndex = zoneIndex % 13;
        playScaleNote(noteIndex);
        gridZoneLastTrigger[row][col] = currentTime;
      }
      gridZoneActive[row][col] = isTouched;

      if (ledVisualizationEnabled) {
        if (isTouched) {
          uint8_t hue = (zoneIndex * 256 / 64);
          leds[zoneIndex] = CHSV(hue, 255, 255);
        } else {
          leds[zoneIndex] = CRGB::Black;
        }
      }
    }
  }

  // --- RIGHT sensor → right LED matrix (higher octaves) ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch1[row][col];
      int zoneIndex = row * 8 + col;
      bool wasTouched = gridZoneActiveR[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);

      if (isTouched && !wasTouched && (currentTime - gridZoneLastTriggerR[row][col]) > GRID_DEBOUNCE_MS) {
        int noteIndex = (zoneIndex % 13) + 12;  // One octave higher
        playScaleNote(noteIndex);
        gridZoneLastTriggerR[row][col] = currentTime;
      }
      gridZoneActiveR[row][col] = isTouched;

      if (ledVisualizationEnabled) {
        if (isTouched) {
          uint8_t hue = (zoneIndex * 256 / 64);
          leds_r[zoneIndex] = CHSV(hue, 255, 255);
        } else {
          leds_r[zoneIndex] = CRGB::Black;
        }
      }
    }
  }

  if (ledVisualizationEnabled) {
    FastLED.show();
  }
}

void processRhythmTapper() {
  // 8-drum rhythm tapper — uses BOTH sensors + BOTH LED matrices
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  unsigned long currentTime = millis();

  Zone drumMap[8] = {
    ZONE_C_FAR, ZONE_C_FAR, ZONE_B_MID, ZONE_B_MID,
    ZONE_A_NEAR, ZONE_A_NEAR, ZONE_B_MID, ZONE_C_FAR
  };
  CRGB rowColors[8] = {
    CRGB::Yellow, CRGB::Cyan, CRGB::Red, CRGB::Orange,
    CRGB::Purple, CRGB::Blue, CRGB::Green, CRGB::White
  };

  // --- LEFT sensor → left LED matrix ---
  for (int row = 0; row < 8; row++) {
    bool rowTouched = false;
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      if (dist < GRID_ZONE_THRESHOLD && dist > 50) { rowTouched = true; break; }
    }
    bool wasTouched = rhythmRowActive[row];
    if (rowTouched && !wasTouched && (currentTime - rhythmRowLastHit[row]) > RHYTHM_DEBOUNCE_MS) {
      triggerDrumHit(drumMap[row], 0.8);
      rhythmRowLastHit[row] = currentTime;
    }
    rhythmRowActive[row] = rowTouched;
    if (ledVisualizationEnabled) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        leds[ledIndex] = rowTouched ? rowColors[row] : CRGB(10, 10, 10);
      }
    }
  }

  // --- RIGHT sensor → right LED matrix ---
  for (int row = 0; row < 8; row++) {
    bool rowTouched = false;
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch1[row][col];
      if (dist < GRID_ZONE_THRESHOLD && dist > 50) { rowTouched = true; break; }
    }
    bool wasTouched = rhythmRowActiveR[row];
    if (rowTouched && !wasTouched && (currentTime - rhythmRowLastHitR[row]) > RHYTHM_DEBOUNCE_MS) {
      triggerDrumHit(drumMap[row], 0.8);
      rhythmRowLastHitR[row] = currentTime;
    }
    rhythmRowActiveR[row] = rowTouched;
    if (ledVisualizationEnabled) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        leds_r[ledIndex] = rowTouched ? rowColors[row] : CRGB(10, 10, 10);
      }
    }
  }

  if (ledVisualizationEnabled) FastLED.show();
}

void processArpeggiator() {
  // Auto-arpeggiating chords!
  // Left hand = chord selection, Right hand = speed control

  unsigned long currentTime = millis();

  // Read both sensors
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // Left hand (CH0): determine chord type based on hand position
  float leftAvg = 0; int leftCount = 0;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (distanceGrid_ch0[r][c] < 800 && distanceGrid_ch0[r][c] > 50) {
        leftAvg += distanceGrid_ch0[r][c]; leftCount++;
      }
    }
  }

  if (leftCount > 0) {
    leftAvg /= leftCount;
    // Map distance to chord type (4 chords across range)
    if (leftAvg < 200) arpChordType = 0;       // Major
    else if (leftAvg < 350) arpChordType = 1;   // Minor
    else if (leftAvg < 500) arpChordType = 2;   // 7th
    else arpChordType = 3;                       // Diminished
  }

  // Right hand (CH1): control arpeggio speed
  float rightAvg = 0; int rightCount = 0;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (distanceGrid_ch1[r][c] < 800 && distanceGrid_ch1[r][c] > 50) {
        rightAvg += distanceGrid_ch1[r][c]; rightCount++;
      }
    }
  }

  if (rightCount > 0) {
    rightAvg /= rightCount;
    // Closer = faster! Range: 50ms (blazing) to 400ms (slow)
    arpSpeed = map(constrain((int)rightAvg, 100, 700), 100, 700, 50, 400);
  }

  // Auto-play arpeggio notes at the set speed.
  // Gate on hand presence so the mode is silent until the player engages.
  bool handPresent = (leftCount > 0 || rightCount > 0);
  if (!handPresent) {
    // No hands -> kill any sustaining note and skip the auto-play tick.
    if (noteActive) {
      stringEnvelope.noteOff();
      noteActive = false;
    }
  } else if (currentTime - lastArpNoteTime >= (unsigned long)arpSpeed) {
    lastArpNoteTime = currentTime;

    // Define chord intervals (semitones from root)
    int chords[4][4] = {
      {0, 4, 7, 12},   // Major (C E G C)
      {0, 3, 7, 12},   // Minor (C Eb G C)
      {0, 4, 7, 10},   // 7th (C E G Bb)
      {0, 3, 6, 9}     // Diminished (C Eb Gb A)
    };

    // Root note based on left hand distance (if hand present)
    int rootMidi = 60;  // Middle C default
    if (leftCount > 0) {
      rootMidi = map(constrain((int)leftAvg, 100, 700), 700, 100, 48, 72);
    }

    int semitone = chords[arpChordType][arpNoteIndex % ARP_CHORD_NOTES];
    int midiNote = rootMidi + semitone;
    float freq = 440.0 * pow(2.0, (midiNote - 69) / 12.0);

    // Play the note with punch!
    stringFilter.frequency(freq * 2.0);  // Filter at 2x fundamental
    stringFilter.resonance(1.5);
    stringVoice.noteOn(freq, 0.9);
    stringEnvelope.noteOn();  // Must trigger envelope for audio to pass!
    noteActive = true;
    mixer1.gain(0, 0.8 * masterVolume);

    arpNoteIndex = (arpNoteIndex + 1) % ARP_CHORD_NOTES;

    // LED visualization - chase pattern on BOTH matrices
    if (ledVisualizationEnabled) {
      clearAllLEDs();
      int col = arpNoteIndex * 2;
      uint8_t hue = (arpChordType * 64) + (arpNoteIndex * 30);
      for (int r = 0; r < 8; r++) {
        // Left matrix: chord note chase
        leds[r * 8 + col] = CHSV(hue, 255, 255);
        leds[r * 8 + col + 1] = CHSV(hue, 255, 180);
        // Right matrix: mirror with complementary color
        leds_r[r * 8 + (6 - col)] = CHSV(hue + 128, 255, 255);
        leds_r[r * 8 + (7 - col)] = CHSV(hue + 128, 255, 180);
      }
      // Left bottom row: speed indicator
      int speedBar = map(constrain((int)arpSpeed, 50, 400), 400, 50, 0, 7);
      for (int c = 0; c <= speedBar; c++) {
        leds[56 + c] = CRGB::White;
        leds_r[56 + c] = CRGB::White;
      }
      FastLED.show();
    }
  }
}

void processDjScratch() {
  // DJ Scratch mode - hand speed creates pitch bending effects

  // Read left sensor
  if (sensor_ch0_initialized) readDistanceGrid(0);

  // Find closest zone (fastest response)
  float closestDist = 2000.0;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (distanceGrid_ch0[r][c] > 50 && distanceGrid_ch0[r][c] < closestDist) {
        closestDist = distanceGrid_ch0[r][c];
      }
    }
  }

  if (closestDist < 800) {
    // Calculate hand speed (change in distance)
    djScratchSpeed = closestDist - djLastDist;
    djLastDist = closestDist;

    // Map scratch speed to pitch bend
    // Fast movement = big pitch change (like vinyl scratching)
    float pitchBend = djScratchSpeed * 3.0;  // Amplify the effect
    djPitch = constrain(djPitch + pitchBend, 80.0, 2000.0);

    // Base pitch from hand distance
    float basePitch = map(constrain((int)closestDist, 100, 700), 700, 100, 110, 880);

    // Blend base pitch with scratch effect
    float finalPitch = basePitch + (djScratchSpeed * 5.0);
    finalPitch = constrain(finalPitch, 60.0, 2500.0);

    // Continuous tone with pitch changes
    stringFilter.frequency(finalPitch * 2.0);  // Filter tracks pitch
    stringFilter.resonance(1.0);
    stringVoice.noteOn(finalPitch, 0.85);
    stringEnvelope.noteOn();  // Must trigger envelope for audio to pass!
    noteActive = true;
    mixer1.gain(0, 0.9 * masterVolume);
    djIsScratching = true;

    // LED visualization - scratch effect
    if (ledVisualizationEnabled) {
      clearAllLEDs();
      // Pitch bar on left
      int pitchBar = map(constrain((int)finalPitch, 80, 2000), 80, 2000, 0, 7);
      for (int r = 7; r >= (7 - pitchBar); r--) {
        for (int c = 0; c < 4; c++) {
          leds[r * 8 + c] = CRGB::Blue;
        }
      }
      // Scratch speed indicator on right (intensity shows speed)
      int speedVis = constrain(abs((int)djScratchSpeed) / 5, 0, 7);
      for (int r = 7; r >= (7 - speedVis); r--) {
        for (int c = 4; c < 8; c++) {
          leds[r * 8 + c] = (djScratchSpeed > 0) ? CRGB::Red : CRGB::Green;
        }
      }
      FastLED.show();
    }
  } else {
    djLastDist = 2000.0;
    djIsScratching = false;
  }
}

void processStepSequencer() {
  // Auto-looping step sequencer
  // Left sensor: toggle notes on/off in the pattern
  // Pattern auto-loops, playing active steps

  unsigned long currentTime = millis();

  // Read left sensor for pattern editing
  if (sensor_ch0_initialized) readDistanceGrid(0);

  // Check for zone touches to toggle pattern.
  // A flat hand covers many cells at once, so we toggle ONLY the single
  // closest newly-touched cell per frame -- prevents 'sweeping' a dozen
  // toggles in one gesture. Per-cell debounce still applies.
  int toggleRow = -1, toggleCol = -1;
  uint16_t minDist = 0xFFFF;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = gridZoneActive[row][col];

      if (isTouched && !wasTouched && (currentTime - gridZoneLastTrigger[row][col]) > 150) {
        if (dist < minDist) {
          minDist = dist;
          toggleRow = row;
          toggleCol = col;
        }
      }
      gridZoneActive[row][col] = isTouched;
    }
  }
  if (toggleRow >= 0) {
    stepGrid[toggleCol][toggleRow] = !stepGrid[toggleCol][toggleRow];  // col=step, row=instrument
    gridZoneLastTrigger[toggleRow][toggleCol] = currentTime;
  }

  // Read right sensor for tempo control
  if (sensor_ch1_initialized) readDistanceGrid(1);
  float rightAvg = 0; int rightCount = 0;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (distanceGrid_ch1[r][c] < 800 && distanceGrid_ch1[r][c] > 50) {
        rightAvg += distanceGrid_ch1[r][c]; rightCount++;
      }
    }
  }
  if (rightCount > 0) {
    rightAvg /= rightCount;
    stepTempo = map(constrain((int)rightAvg, 100, 700), 100, 700, 80, 500);
  }

  // Auto-advance step at tempo
  if (currentTime - lastStepTime >= (unsigned long)stepTempo) {
    lastStepTime = currentTime;

    // Play all active instruments at current step
    for (int inst = 0; inst < 8; inst++) {
      if (stepGrid[stepPosition][inst]) {
        // Map instrument rows to sounds
        if (inst < 2) {
          // Rows 0-1: Kick variations
          kickDrum.frequency(inst == 0 ? 80 : 60);
          kickDrum.noteOn();
          mixer1.gain(1, 0.8 * masterVolume);
        } else if (inst < 4) {
          // Rows 2-3: Snare variations
          snareDrum.frequency(inst == 2 ? 200 : 300);
          snareDrum.noteOn();
          mixer1.gain(2, 0.7 * masterVolume);
        } else if (inst < 6) {
          // Rows 4-5: Hi-hat (short noise bursts)
          hatEnvelope.noteOn();
          mixer1.gain(3, (inst == 4 ? 0.4 : 0.3) * masterVolume);
        } else {
          // Rows 6-7: Melodic notes
          int midiNote = (inst == 6) ? 60 : 67;  // C4 or G4
          float freq = 440.0 * pow(2.0, (midiNote - 69) / 12.0);
          stringFilter.frequency(freq * 2.0);
          stringVoice.noteOn(freq, 0.7);
          stringEnvelope.noteOn();
          noteActive = true;
          mixer1.gain(0, 0.6 * masterVolume);
        }
      }
    }

    stepPosition = (stepPosition + 1) % 8;
  }

  // LED visualization - BOTH matrices show grid pattern with playhead.
  // stepPosition was already advanced past the just-played step, so the
  // visible playhead must point to the step that is currently sounding.
  if (ledVisualizationEnabled) {
    int playheadCol = (stepPosition + 7) % 8;
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        CRGB color;
        if (col == playheadCol) {
          color = stepGrid[col][row] ? CRGB::White : CRGB(30, 30, 0);
        } else if (stepGrid[col][row]) {
          uint8_t hue = row * 32;
          color = CHSV(hue, 255, 150);
        } else {
          color = CRGB::Black;
        }
        leds[ledIndex] = color;
        leds_r[ledIndex] = color;  // Mirror on right matrix
      }
    }
    // Right matrix bottom row: tempo indicator
    int tempoBar = map(constrain((int)stepTempo, 80, 500), 500, 80, 0, 7);
    for (int c = 0; c <= tempoBar; c++) {
      leds_r[56 + c] = CRGB::Cyan;
    }
    FastLED.show();
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
    // 8 rows = 8 notes in scale (C4 to C5)
    int melodyMidi[] = {60, 62, 64, 65, 67, 69, 71, 72};  // C major scale
    for (int note = 0; note < 8; note++) {
      if (dualLoopMelody[dualLoopStep][note]) {
        float freq = 440.0 * pow(2.0, (melodyMidi[note] - 69) / 12.0);
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
  static const int stringNotes[4] = {52, 57, 62, 67};  // E3, A3, D4, G4
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
      float freq = 440.0 * pow(2.0, (stringNotes[playString] - 69) / 12.0);
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

void processDroneSolo() {
  // DRONE + SOLO (hybrid control model):
  //   Left hand  : height (avg distance) -> drone note; sustains while present.
  //   Right hand : height -> pitch, lateral swipe -> trigger, coverage -> velocity.
  static const int droneNotes[8] = {36, 38, 40, 41, 43, 45, 47, 48};
  static const int soloNotes[8]  = {60, 62, 64, 65, 67, 69, 71, 72};

  // Narrower distance window than DUAL_DIST_* so small hand motions
  // produce audible / visible changes. Max capped at 250 mm so a high
  // hand on one sensor doesn't spill into the other sensor's field.
  const float DRONE_DIST_MIN = 60.0;
  const float DRONE_DIST_MAX = 250.0;

  // Swipe detection state
  static int prevSoloCentroidX = -1;
  static unsigned long lastSwipeFire = 0;
  // Smoothed continuous "level" (0..8) for buttery LED motion between note quanta
  static float leftLevelSmooth = 0;
  static float rightLevelSmooth = 0;

  unsigned long currentTime = millis();

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  float leftAvgDist = 0, rightAvgDist = 0;
  int leftCentroidX = 0, leftCentroidY = 0, leftZones = 0;
  int rightCentroidX = 0, rightCentroidY = 0, rightZones = 0;
  calculateHandMetrics(distanceGrid_ch0, leftAvgDist, leftCentroidX, leftCentroidY, leftZones);
  calculateHandMetrics(distanceGrid_ch1, rightAvgDist, rightCentroidX, rightCentroidY, rightZones);

  // --- LEFT HAND: Drone (height -> pitch, sustains while hand present) ---
  int droneIdx = -1;
  float leftLevel = 0;  // continuous 0..8 for LED bar
  if (leftZones > 0) {
    float normDist = constrain(leftAvgDist, DRONE_DIST_MIN, DRONE_DIST_MAX);
    float factor = (normDist - DRONE_DIST_MIN) / (DRONE_DIST_MAX - DRONE_DIST_MIN);
    leftLevel = factor * 8.0;
    droneIdx = constrain((int)(factor * 7.99), 0, 7);

    float newDroneFreq = 440.0 * pow(2.0, (droneNotes[droneIdx] - 69) / 12.0);
    // Snappier response (was 0.9/0.1 -> sluggish)
    droneFreq = droneFreq * 0.7 + newDroneFreq * 0.3;
    if (!droneActive) {
      stringVoice.noteOn(droneFreq, 0.5);
      stringEnvelope.noteOn();
      stringFilter.frequency(droneFreq * 3.0);
      stringFilter.resonance(2.0);
      noteActive = true;
      droneActive = true;
    } else {
      stringVoice.noteOn(droneFreq, 0.5);
      stringFilter.frequency(droneFreq * 3.0);
    }
    mixer1.gain(0, 0.5 * masterVolume);
  } else {
    if (droneActive) {
      stringEnvelope.noteOff();
      droneActive = false;
    }
  }
  leftLevelSmooth = leftLevelSmooth * 0.6 + leftLevel * 0.4;

  // --- RIGHT HAND: Solo (height -> pitch, swipe -> trigger, coverage -> velocity) ---
  int soloIdx = -1;
  float rightLevel = 0;
  if (rightZones > 0) {
    float normDist = constrain(rightAvgDist, DRONE_DIST_MIN, DRONE_DIST_MAX);
    float factor = (normDist - DRONE_DIST_MIN) / (DRONE_DIST_MAX - DRONE_DIST_MIN);
    rightLevel = factor * 8.0;
    soloIdx = constrain((int)(factor * 7.99), 0, 7);

    // Trigger: lateral swipe (centroid X moved >= 2 cells) OR fresh hand arrival.
    bool fire = false;
    if (prevSoloCentroidX < 0) {
      fire = true;  // hand just appeared
    } else {
      int dx = rightCentroidX - prevSoloCentroidX;
      if (dx < 0) dx = -dx;
      if (dx >= 2 && (currentTime - lastSwipeFire) > 80) fire = true;
    }

    if (fire) {
      // Coverage -> velocity (bigger swipe = louder note)
      float coverage = (float)rightZones / 64.0;
      float velocity = constrain(0.35 + coverage * 0.65, 0.3, 1.0);

      float soloFreq = 440.0 * pow(2.0, (soloNotes[soloIdx] - 69) / 12.0);
      // Melodic snareDrum config (safety net - also set in mode-enter)
      snareDrum.length(500);
      snareDrum.secondMix(0.0);
      snareDrum.pitchMod(0.0);
      snareDrum.frequency(soloFreq);
      snareDrum.noteOn();
      mixer1.gain(2, velocity * masterVolume);
      lastSwipeFire = currentTime;
      soloLastNote = soloIdx;
      soloLastTrigger = currentTime;
    }
    prevSoloCentroidX = rightCentroidX;
  } else {
    prevSoloCentroidX = -1;
    soloLastNote = -1;
  }
  rightLevelSmooth = rightLevelSmooth * 0.6 + rightLevel * 0.4;

  // --- LED VISUALIZATION (continuous height bar + centroid indicator) ---
  if (ledVisualizationEnabled) {
    clearAllLEDs();
    // Helper: continuous bar fill for both matrices. Each row fades from base
    // dim color up to a bright top "tip" that tracks hand height smoothly.
    uint8_t pulse = 128 + 127 * sin(currentTime / 200.0);

    // LEFT matrix - drone bar (teal/cyan, pulses)
    for (int row = 0; row < 8; row++) {
      // How "filled" is this row? 1.0 if fully below the level, fractional at the top
      float fill = constrain(leftLevelSmooth - row, 0.0f, 1.0f);
      uint8_t brightness = (uint8_t)(fill * pulse);
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        if (leftZones > 0 && brightness > 0) {
          leds[ledIndex] = CRGB(0, brightness, brightness);
        } else {
          leds[ledIndex] = CRGB(0, 6, 6);  // dim baseline
        }
      }
    }

    // RIGHT matrix - solo bar (magenta) with centroid column highlight
    for (int row = 0; row < 8; row++) {
      float fill = constrain(rightLevelSmooth - row, 0.0f, 1.0f);
      uint8_t r = (uint8_t)(fill * 255);
      uint8_t b = (uint8_t)(fill * 200);
      for (int col = 0; col < 8; col++) {
        int ledIndex = row * 8 + col;
        bool onCentroidCol = (rightZones > 0 && col == rightCentroidX);
        if (rightZones > 0 && fill > 0) {
          // Boost the centroid column so you SEE the X position too
          if (onCentroidCol) {
            leds_r[ledIndex] = CRGB(255, 255, 255);
          } else {
            leds_r[ledIndex] = CRGB(r, 30, b);
          }
        } else if (onCentroidCol) {
          leds_r[ledIndex] = CRGB(60, 0, 60);  // faint column when below level
        } else {
          leds_r[ledIndex] = CRGB(6, 0, 6);
        }
      }
    }
    FastLED.show();
  }
}

void processRainMode() {
  // RAIN MODE: Notes fall on BOTH matrices, catch with either hand!
  static const int rainScaleNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};

  unsigned long currentTime = millis();
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- Spawn new raindrops on both sides ---
  if (currentTime - rainLastSpawn > 600) {
    rainLastSpawn = currentTime;
    // Spawn on left
    for (int i = 0; i < 8; i++) {
      if (!rainDropActive[i]) {
        rainDropActive[i] = true;
        rainDropRow[i] = 0.0;
        rainDropNote[i] = random(0, 8);
        break;
      }
    }
    // Spawn on right
    for (int i = 0; i < 8; i++) {
      if (!rainDropActiveR[i]) {
        rainDropActiveR[i] = true;
        rainDropRowR[i] = 0.0;
        rainDropNoteR[i] = random(0, 8);
        break;
      }
    }
  }

  // --- Move & catch: independent passes for LEFT and RIGHT ---
  // Split so a catch on one side cannot 'continue' past the other side's tick.
  if (currentTime - rainLastFall > 50) {
    rainLastFall = currentTime;

    // LEFT sensor
    for (int i = 0; i < 8; i++) {
      if (!rainDropActive[i]) continue;
      rainDropRow[i] += rainSpeed;
      int dropGridRow = (int)rainDropRow[i];
      int dropCol = rainDropNote[i];
      if (dropGridRow >= 0 && dropGridRow < 8) {
        uint16_t d = distanceGrid_ch0[dropGridRow][dropCol];
        if (d < GRID_ZONE_THRESHOLD && d > 50) {
          float freq = 440.0 * pow(2.0, (rainScaleNotes[dropCol] - 69) / 12.0);
          float velocity = constrain(1.0 - (d / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
          stringFilter.frequency(freq * 2.5);
          stringFilter.resonance(1.2);
          stringEnvelope.noteOff();  // release any sustaining envelope first
          stringVoice.noteOn(freq, velocity);
          stringEnvelope.noteOn();
          noteActive = true;
          mixer1.gain(0, velocity * masterVolume);
          rainDropActive[i] = false;
          continue;
        }
      }
      if (rainDropRow[i] >= 8.0) rainDropActive[i] = false;
    }

    // RIGHT sensor
    for (int i = 0; i < 8; i++) {
      if (!rainDropActiveR[i]) continue;
      rainDropRowR[i] += rainSpeed;
      int dropGridRow = (int)rainDropRowR[i];
      int dropCol = rainDropNoteR[i];
      if (dropGridRow >= 0 && dropGridRow < 8) {
        uint16_t d = distanceGrid_ch1[dropGridRow][dropCol];
        if (d < GRID_ZONE_THRESHOLD && d > 50) {
          float freq = 440.0 * pow(2.0, (rainScaleNotes[dropCol] - 69) / 12.0);
          float velocity = constrain(1.0 - (d / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
          stringFilter.frequency(freq * 2.5);
          stringFilter.resonance(1.2);
          stringEnvelope.noteOff();  // release any sustaining envelope first
          stringVoice.noteOn(freq, velocity);
          stringEnvelope.noteOn();
          noteActive = true;
          mixer1.gain(0, velocity * masterVolume);
          rainDropActiveR[i] = false;
          continue;
        }
      }
      if (rainDropRowR[i] >= 8.0) rainDropActiveR[i] = false;
    }
  }

  // --- LED VISUALIZATION ---
  if (ledVisualizationEnabled) {
    clearAllLEDs();
    // Left matrix: drops + hand
    for (int i = 0; i < 8; i++) {
      if (rainDropActive[i]) {
        int row = (int)rainDropRow[i];
        int col = rainDropNote[i];
        if (row >= 0 && row < 8) {
          uint8_t hue = col * 32;
          leds[row * 8 + col] = CHSV(hue, 255, 200);
          if (row > 0) leds[(row - 1) * 8 + col] = CHSV(hue, 255, 60);
        }
      }
    }
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++) {
        uint16_t d = distanceGrid_ch0[row][col];
        if (d < GRID_ZONE_THRESHOLD && d > 50) leds[row * 8 + col] += CRGB(0, 80, 0);
      }
    // Right matrix: drops + hand
    for (int i = 0; i < 8; i++) {
      if (rainDropActiveR[i]) {
        int row = (int)rainDropRowR[i];
        int col = rainDropNoteR[i];
        if (row >= 0 && row < 8) {
          uint8_t hue = col * 32;
          leds_r[row * 8 + col] = CHSV(hue, 255, 200);
          if (row > 0) leds_r[(row - 1) * 8 + col] = CHSV(hue, 255, 60);
        }
      }
    }
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++) {
        uint16_t d = distanceGrid_ch1[row][col];
        if (d < GRID_ZONE_THRESHOLD && d > 50) leds_r[row * 8 + col] += CRGB(0, 80, 0);
      }
    FastLED.show();
  }
}

void processBassMachine() {
  // BASS MACHINE: Left hand = toggle bass notes in loop, Right hand = filter sweep
  // Low bass notes: C2, D2, E2, F2, G2, A2, B2, C3
  static const int bassNotes[8] = {36, 38, 40, 41, 43, 45, 47, 48};

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
        float freq = 440.0 * pow(2.0, (bassNotes[note] - 69) / 12.0);
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

void processEchoDelay() {
  // ECHO DELAY: Left hand = pitch, echoes automatically repeat
  // Right hand = controls delay time
  static const int echoScaleNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};

  unsigned long currentTime = millis();

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- RIGHT HAND: Control delay time ---
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
    // Closer = faster echoes (100ms), farther = slower (600ms)
    echoDelayMs = constrain(rightAvg * 1.5, 100.0, 600.0);
  }

  // --- LEFT HAND: Play notes ---
  int playRow = -1;
  float playDist = 9999;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t d = distanceGrid_ch0[row][col];
      if (d < GRID_ZONE_THRESHOLD && d > 50 && d < playDist) {
        playDist = d;
        playRow = row;
      }
    }
  }

  if (playRow >= 0 && !echoPlaying) {
    // New note! Start echo chain
    float freq = 440.0 * pow(2.0, (echoScaleNotes[playRow] - 69) / 12.0);
    echoWriteIndex = 0;
    for (int i = 0; i < 6; i++) {
      echoNotes[i] = freq;
      echoVolumes[i] = 0.9 * pow(0.6, i);  // Each echo 60% of previous
    }
    echoPlaying = true;
    echoPlayIndex = 0;
    echoStartTime = currentTime;
    echoLastPlay = currentTime;

    // Play first note immediately
    stringFilter.frequency(freq * 2.5);
    stringFilter.resonance(1.5);
    stringVoice.noteOn(freq, 0.9);
    stringEnvelope.noteOn();
    noteActive = true;
    mixer1.gain(0, 0.9 * masterVolume);
    echoPlayIndex = 1;
  }

  // --- Play echo repeats ---
  if (echoPlaying && echoPlayIndex < 6) {
    if (currentTime - echoLastPlay >= (unsigned long)echoDelayMs) {
      echoLastPlay = currentTime;
      float freq = echoNotes[echoPlayIndex];
      float vol = echoVolumes[echoPlayIndex];

      stringFilter.frequency(freq * 2.0);  // Slightly darker each echo
      stringVoice.noteOn(freq, vol);
      stringEnvelope.noteOn();
      noteActive = true;
      mixer1.gain(0, vol * masterVolume);

      echoPlayIndex++;
      if (echoPlayIndex >= 6) echoPlaying = false;
    }
  }

  // --- LED VISUALIZATION ---
  if (ledVisualizationEnabled) {
    clearAllLEDs();
    // Left matrix: echo ripple effect + hand position
    if (echoPlaying) {
      for (int i = 0; i < echoPlayIndex; i++) {
        int radius = i + 1;
        uint8_t brightness = 255 - (i * 40);
        uint8_t hue = 160 + (i * 15);  // Blue to purple
        for (int row = 0; row < 8; row++) {
          for (int col = 0; col < 8; col++) {
            int dist = abs(row - 4) + abs(col - 4);
            if (dist == radius) {
              leds[row * 8 + col] = CHSV(hue, 200, brightness);
            }
          }
        }
      }
    }
    if (playRow >= 0) {
      for (int col = 0; col < 8; col++) {
        leds[playRow * 8 + col] = CRGB(255, 255, 255);
      }
    }
    // Right matrix: delay time visualization (bar height = delay length)
    int delayBar = constrain((int)((echoDelayMs / 600.0) * 8.0), 0, 7);
    for (int row = 7; row >= (7 - delayBar); row--) {
      for (int col = 0; col < 8; col++) {
        uint8_t hue = map(row, 0, 7, 180, 220);
        leds_r[row * 8 + col] = CHSV(hue, 200, 150);
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

  static const int battleNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};

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
    float freq = 440.0 * pow(2.0, (battleNotes[p1Row] - 69) / 12.0);
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
    float freq = 440.0 * pow(2.0, (battleNotes[p2Row] - 69) / 12.0);
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

  // Copy distance data to our 8x8 grid, rotating 180° so that grid[row][col]
  // matches the user's perceived orientation (top-left as you face the device =
  // grid[0][0]). The raw sensor frame is mounted rotated 180° relative to the
  // LED matrices, so we flip both axes here once, at the source. Every mode
  // downstream then sees correctly-oriented data without needing its own flip.
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int rawIndex = (7 - row) * 8 + (7 - col);
      grid[row][col] = results.distance_mm[rawIndex];
    }
  }
}

void printDistanceGrid(uint8_t channel) {
  // Print the 8x8 distance grid in a readable format
  // channel: 0 = CH0 (I2C0), 1 = CH1 (I2C1)
  // Each cell shows distance in millimeters
  // Cells with distance < 200mm are marked with *** to track target shape
  // This represents a low-resolution depth map

  uint16_t (*grid)[8];

  if (channel == 0) {
    grid = distanceGrid_ch0;
  } else {
    grid = distanceGrid_ch1;
  }

  Serial.println("\n========== VL53L5CX 8x8 DISTANCE GRID (mm) ==========");
  Serial.print("Channel: CH");
  Serial.print(channel);
  Serial.print(" (I2C");
  Serial.print(channel);
  Serial.println(")");
  Serial.println("(Each cell = one zone, 64 zones total)");
  Serial.println("(*** = distance < 200mm - TARGET DETECTED!)");
  Serial.println();

  // Print column headers
  Serial.print("     ");
  for (int col = 0; col < 8; col++) {
    Serial.print("  C");
    Serial.print(col);
    Serial.print("  ");
  }
  Serial.println();
  Serial.println("   +-----+-----+-----+-----+-----+-----+-----+-----+");

  // Print each row
  for (int row = 0; row < 8; row++) {
    Serial.print("R");
    Serial.print(row);
    Serial.print(" |");

    for (int col = 0; col < 8; col++) {
      uint16_t dist = grid[row][col];

      // Mark targets (distance < 200mm) with ***
      bool isTarget = (dist < 200);

      if (isTarget) {
        // Show *** for close targets
        Serial.print(" *** ");
      } else {
        // Format: 5 characters wide, right-aligned for far objects
        if (dist >= 10000) {
          Serial.print(" ----");  // Out of range
        } else if (dist >= 1000) {
          Serial.print(dist);
          Serial.print(" ");
        } else if (dist >= 100) {
          Serial.print(" ");
          Serial.print(dist);
          Serial.print(" ");
        } else if (dist >= 10) {
          Serial.print("  ");
          Serial.print(dist);
          Serial.print(" ");
        } else {
          Serial.print("   ");
          Serial.print(dist);
          Serial.print(" ");
        }
      }

      Serial.print("|");
    }
    Serial.println();
    Serial.println("   +-----+-----+-----+-----+-----+-----+-----+-----+");
  }

  Serial.println();
}

void printBothGrids() {
  // Print both 8x8 distance grids side by side
  // Shows CH0 (left) and CH1 (right) for two-hand tracking

  Serial.println("\n============ VL53L5CX DUAL SENSOR VIEW (mm) ============");
  Serial.println("        CH0 (I2C0)                    CH1 (I2C1)");
  Serial.println("(*** = distance < 200mm - TARGET DETECTED!)");
  Serial.println();

  // Print column headers for both grids
  Serial.print("     ");
  for (int col = 0; col < 8; col++) {
    Serial.print("  C");
    Serial.print(col);
    Serial.print("  ");
  }
  Serial.print("       ");  // Space between grids
  for (int col = 0; col < 8; col++) {
    Serial.print("  C");
    Serial.print(col);
    Serial.print("  ");
  }
  Serial.println();

  // Print separator line for both grids
  Serial.print("   +-----+-----+-----+-----+-----+-----+-----+-----+");
  Serial.print("     ");
  Serial.println("+-----+-----+-----+-----+-----+-----+-----+-----+");

  // Print each row for both grids side by side
  for (int row = 0; row < 8; row++) {
    // Print CH0 row
    Serial.print("R");
    Serial.print(row);
    Serial.print(" |");

    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      bool isTarget = (dist < 200);

      if (isTarget) {
        Serial.print(" *** ");
      } else {
        if (dist >= 10000) {
          Serial.print(" ----");
        } else if (dist >= 1000) {
          Serial.print(dist);
          Serial.print(" ");
        } else if (dist >= 100) {
          Serial.print(" ");
          Serial.print(dist);
          Serial.print(" ");
        } else if (dist >= 10) {
          Serial.print("  ");
          Serial.print(dist);
          Serial.print(" ");
        } else {
          Serial.print("   ");
          Serial.print(dist);
          Serial.print(" ");
        }
      }
      Serial.print("|");
    }

    // Space between grids
    Serial.print("   ");

    // Print CH1 row
    Serial.print("R");
    Serial.print(row);
    Serial.print(" |");

    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch1[row][col];
      bool isTarget = (dist < 200);

      if (isTarget) {
        Serial.print(" *** ");
      } else {
        if (dist >= 10000) {
          Serial.print(" ----");
        } else if (dist >= 1000) {
          Serial.print(dist);
          Serial.print(" ");
        } else if (dist >= 100) {
          Serial.print(" ");
          Serial.print(dist);
          Serial.print(" ");
        } else if (dist >= 10) {
          Serial.print("  ");
          Serial.print(dist);
          Serial.print(" ");
        } else {
          Serial.print("   ");
          Serial.print(dist);
          Serial.print(" ");
        }
      }
      Serial.print("|");
    }

    Serial.println();

    // Print separator line for both grids
    Serial.print("   +-----+-----+-----+-----+-----+-----+-----+-----+");
    Serial.print("     ");
    Serial.println("+-----+-----+-----+-----+-----+-----+-----+-----+");
  }

  Serial.println();
}


// ============================================================================
// LED FUNCTIONS
// ============================================================================

void setupLEDs() {
  // Initialize FastLED library — two 8x8 matrices
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);    // Left matrix (pin 0)
  FastLED.addLeds<LED_TYPE, LED_PIN_R, COLOR_ORDER>(leds_r, NUM_LEDS); // Right matrix (pin 1)
  FastLED.setBrightness(50);  // Set brightness (0-255), 50 is moderate
  clearAllLEDs();
  Serial.println("FastLED initialized: Left matrix on pin 0, Right matrix on pin 1");
  Serial.print("Each matrix: ");
  Serial.print(NUM_LEDS);
  Serial.println(" LEDs (8x8, serpentine layout)");
}

void setLED(int index, CRGB color) {
  // Set a specific LED on the left matrix
  if (index >= 0 && index < NUM_LEDS) {
    leds[index] = color;
    FastLED.show();
  }
}

void setLED_R(int index, CRGB color) {
  // Set a specific LED on the right matrix
  if (index >= 0 && index < NUM_LEDS) {
    leds_r[index] = color;
    FastLED.show();
  }
}

void clearAllLEDs() {
  // Turn off all LEDs on both matrices
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
    leds_r[i] = CRGB::Black;
  }
  FastLED.show();
}

CRGB getLEDColor(LEDColor colorEnum) {
  // Convert our color enum to FastLED CRGB color
  switch (colorEnum) {
    case LED_RED:
      return CRGB::Red;
    case LED_GREEN:
      return CRGB::Green;
    case LED_BLUE:
      return CRGB::Blue;
    case LED_YELLOW:
      return CRGB::Yellow;
    case LED_WHITE:
      return CRGB::White;
    case LED_OFF:
    default:
      return CRGB::Black;
  }
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

void updateLEDsFromSensorRow(uint8_t channel, uint8_t row) {
  // Update the 8 LEDs based on a specific row from the sensor grid
  // channel: 0 = CH0, 1 = CH1
  // row: 0-7 (which row of the 8x8 grid to visualize)

  uint16_t (*grid)[8];

  if (channel == 0) {
    grid = distanceGrid_ch0;
  } else {
    grid = distanceGrid_ch1;
  }

  // Map each column in the specified row to an LED using serpentine layout
  for (int col = 0; col < MATRIX_WIDTH; col++) {
    uint16_t distance = grid[row][col];
    CRGB color = distanceToColor(distance);
    int ledIndex = xyToLEDIndex(row, col);
    leds[ledIndex] = color;
  }

  // Update the LED strip
  FastLED.show();
}

void updateLEDsFromFullGrid(uint8_t channel) {
  // Update ALL 64 LEDs based on the full 8x8 sensor grid
  // channel: 0 = CH0, 1 = CH1

  uint16_t (*grid)[8];

  if (channel == 0) {
    grid = distanceGrid_ch0;
  } else {
    grid = distanceGrid_ch1;
  }

  // Map entire 8x8 grid to 8x8 LED matrix using serpentine layout
  for (int row = 0; row < MATRIX_HEIGHT; row++) {
    for (int col = 0; col < MATRIX_WIDTH; col++) {
      uint16_t distance = grid[row][col];
      CRGB color = distanceToColor(distance);
      int ledIndex = xyToLEDIndex(row, col);
      leds[ledIndex] = color;
    }
  }

  // Update the LED strip
  FastLED.show();
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
  // sensor/LED is up — only requires setupOLED() to have run.
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
  int loadingWidth = strlen(loading) * 6;  // size-1 font ≈ 6 px per char
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
    // Active performance modes
    case MODE_BACKSTAGE:        return "BACKSTAGE";
    case MODE_BACKSTAGE_2:      return "BACKSTAGE 2";
    case MODE_STRING_THEREMIN:  return "STRING";
    case MODE_DUAL_THEREMIN:    return "!DUAL THR";   // ! = broken, fix in progress
    case MODE_DUAL_PIANO:       return "DUAL PIANO";
    case MODE_GRID_SEQUENCER:   return "GRID SEQ";
    case MODE_RHYTHM_TAPPER:    return "!RHYTHM";     // ! = broken (input lag)
    case MODE_DUAL_LOOP:        return "DUAL LOOP";
    case MODE_CHORD_JAM:        return "CHORD JAM";
    case MODE_DRONE_SOLO:       return "DRONE+SOLO";
    case MODE_BASS_MACHINE:     return "BASS";
    case MODE_ECHO_DELAY:       return "!ECHO";       // ! = broken (right side frozen)
    case MODE_BATTLE_MODE:      return "BATTLE";
    // Calibration / diagnostic tools
    case MODE_LED_TEST:         return "LED TEST";
    case MODE_CALIB_SERIAL_CH0: return "CAL S CH0";
    case MODE_CALIB_SERIAL_CH1: return "CAL S CH1";
    // Deprecated (leading '*' marks them as pending removal)
    case MODE_RAIN_MODE:        return "*RAIN";
    case MODE_STEP_SEQ:         return "*STEP SEQ";
    case MODE_ARPEGGIATOR:      return "*ARPEG";
    case MODE_DISTANCE_SENSOR:  return "*DISTANCE";
    default: return "UNKNOWN";
  }
}

const char* getScaleString(ScaleType scale) {
  switch (scale) {
    case SCALE_PENTATONIC: return "PENTATONIC";
    case SCALE_MAJOR: return "MAJOR";
    case SCALE_MINOR: return "MINOR";
    default: return "UNKNOWN";
  }
}

// ============================================================================
// MODE SWITCHING FUNCTION (used by both serial commands and encoder)
// ============================================================================

// Flag to prevent mode switching during initialization
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

  // Mode-specific initialization
  if (currentMode == MODE_DISTANCE_SENSOR) {
    Serial.println(">> Mode: DISTANCE SENSOR (8x8 Grid)");
    // Initialize both sensors if not already done
    if (!sensor_ch0_initialized) {
      Serial.println("   Initializing VL53L5CX CH0 (I2C0)...");
      sensor_ch0_initialized = initDistanceSensor(0);
      if (sensor_ch0_initialized) {
        Serial.println("   CH0 initialization SUCCESS");
      } else {
        Serial.println("   CH0 initialization FAILED");
      }
    } else {
      Serial.println("   CH0 already initialized");
    }
    if (!sensor_ch1_initialized) {
      Serial.println("   Initializing VL53L5CX CH1 (I2C1)...");
      sensor_ch1_initialized = initDistanceSensor(1);
      if (sensor_ch1_initialized) {
        Serial.println("   CH1 initialization SUCCESS");
      } else {
        Serial.println("   CH1 initialization FAILED");
      }
    } else {
      Serial.println("   CH1 already initialized");
    }
    Serial.print("   Viewing: CH");
    Serial.println(currentSensorChannel);

    // Small delay to ensure sensors are ready
    delay(100);
  } else if (currentMode == MODE_CALIB_SERIAL_CH0 || currentMode == MODE_CALIB_SERIAL_CH1) {
    // Serial-monitor calibration: isolate a single sensor channel
    uint8_t calibChannel = (currentMode == MODE_CALIB_SERIAL_CH0) ? 0 : 1;

    Serial.print(">> Mode: CALIBRATION (serial monitor) - CH");
    Serial.println(calibChannel);

    bool& initFlag = (calibChannel == 0) ? sensor_ch0_initialized : sensor_ch1_initialized;
    if (!initFlag) {
      Serial.print("   Initializing VL53L5CX CH");
      Serial.print(calibChannel);
      Serial.println("...");
      initFlag = initDistanceSensor(calibChannel);
      Serial.print(initFlag ? "   CH init SUCCESS" : "   CH init FAILED");
      Serial.println();
    } else {
      Serial.print("   CH");
      Serial.print(calibChannel);
      Serial.println(" already initialized");
    }

    panicMute();
    clearAllLEDs();
    FastLED.show();
    delay(100);
  } else if (currentMode == MODE_BACKSTAGE) {
    // Diagnostic: dual-hand view of both sensor grids on both LED matrices
    Serial.println(">> Mode: BACKSTAGE");
    Serial.println("   CH0 -> LEFT matrix | CH1 -> RIGHT matrix");
    if (!sensor_ch0_initialized) {
      Serial.println("   Initializing VL53L5CX CH0 (I2C0)...");
      sensor_ch0_initialized = initDistanceSensor(0);
      Serial.println(sensor_ch0_initialized ? "   CH0 init SUCCESS" : "   CH0 init FAILED");
    } else {
      Serial.println("   CH0 already initialized");
    }
    if (!sensor_ch1_initialized) {
      Serial.println("   Initializing VL53L5CX CH1 (I2C1)...");
      sensor_ch1_initialized = initDistanceSensor(1);
      Serial.println(sensor_ch1_initialized ? "   CH1 init SUCCESS" : "   CH1 init FAILED");
    } else {
      Serial.println("   CH1 already initialized");
    }
    panicMute();
    clearAllLEDs();
    FastLED.show();
    delay(100);
  } else if (currentMode == MODE_BACKSTAGE_2) {
    Serial.println(">> Mode: BACKSTAGE 2");
    Serial.println("   LED-only: pulsing heart (L) + eighth note (R), no sensors");
    panicMute();
    clearAllLEDs();
    FastLED.show();
    delay(100);
  } else if (currentMode == MODE_DUAL_THEREMIN) {
    Serial.println(">> Mode: DUAL THEREMIN");
    Serial.println("   Left hand (CH0) = Pitch | Right hand (CH1) = Volume");
    // Initialize both sensors if not already done
    if (!sensor_ch0_initialized) {
      Serial.println("   Initializing VL53L5CX CH0 (I2C0)...");
      sensor_ch0_initialized = initDistanceSensor(0);
      if (sensor_ch0_initialized) {
        Serial.println("   CH0 initialization SUCCESS");
      } else {
        Serial.println("   CH0 initialization FAILED");
      }
    } else {
      Serial.println("   CH0 already initialized");
    }
    if (!sensor_ch1_initialized) {
      Serial.println("   Initializing VL53L5CX CH1 (I2C1)...");
      sensor_ch1_initialized = initDistanceSensor(1);
      if (sensor_ch1_initialized) {
        Serial.println("   CH1 initialization SUCCESS");
      } else {
        Serial.println("   CH1 initialization FAILED");
      }
    } else {
      Serial.println("   CH1 already initialized");
    }
    // Enable LED visualization by default
    ledVisualizationEnabled = true;
    clearAllLEDs();
    delay(100);
  } else if (currentMode == MODE_DUAL_PIANO) {
    Serial.println(">> Mode: DUAL PIANO");
    Serial.println("   Left hand = Note selection | Right hand = Expression");
    // Initialize both sensors if not already done
    if (!sensor_ch0_initialized) {
      Serial.println("   Initializing VL53L5CX CH0 (I2C0)...");
      sensor_ch0_initialized = initDistanceSensor(0);
      if (sensor_ch0_initialized) {
        Serial.println("   CH0 initialization SUCCESS");
      } else {
        Serial.println("   CH0 initialization FAILED");
      }
    } else {
      Serial.println("   CH0 already initialized");
    }
    if (!sensor_ch1_initialized) {
      Serial.println("   Initializing VL53L5CX CH1 (I2C1)...");
      sensor_ch1_initialized = initDistanceSensor(1);
      if (sensor_ch1_initialized) {
        Serial.println("   CH1 initialization SUCCESS");
      } else {
        Serial.println("   CH1 initialization FAILED");
      }
    } else {
      Serial.println("   CH1 already initialized");
    }
    ledVisualizationEnabled = true;
    clearAllLEDs();
    currentPianoNote = -1;  // Reset piano state
    delay(100);
  } else if (currentMode == MODE_GRID_SEQUENCER) {
    Serial.println(">> Mode: GRID SEQUENCER");
    Serial.println("   Touch any of 64 zones to play notes!");
    // Initialize left sensor only
    if (!sensor_ch0_initialized) {
      Serial.println("   Initializing VL53L5CX CH0 (I2C0)...");
      sensor_ch0_initialized = initDistanceSensor(0);
      if (sensor_ch0_initialized) {
        Serial.println("   CH0 initialization SUCCESS");
      } else {
        Serial.println("   CH0 initialization FAILED");
      }
    } else {
      Serial.println("   CH0 already initialized");
    }
    ledVisualizationEnabled = true;
    clearAllLEDs();
    // Reset grid state
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        gridZoneActive[r][c] = false;
        gridZoneLastTrigger[r][c] = 0;
      }
    }
    delay(100);
  } else if (currentMode == MODE_RHYTHM_TAPPER) {
    Serial.println(">> Mode: RHYTHM TAPPER");
    Serial.println("   8 rows = 8 drums. Touch any row to play!");
    // Initialize left sensor only
    if (!sensor_ch0_initialized) {
      Serial.println("   Initializing VL53L5CX CH0 (I2C0)...");
      sensor_ch0_initialized = initDistanceSensor(0);
      if (sensor_ch0_initialized) {
        Serial.println("   CH0 initialization SUCCESS");
      } else {
        Serial.println("   CH0 initialization FAILED");
      }
    } else {
      Serial.println("   CH0 already initialized");
    }
    ledVisualizationEnabled = true;
    clearAllLEDs();
    // Reset rhythm state
    for (int r = 0; r < 8; r++) {
      rhythmRowActive[r] = false;
      rhythmRowLastHit[r] = 0;
    }
    delay(100);
  } else if (currentMode == MODE_ARPEGGIATOR) {
    Serial.println(">> Mode: ARPEGGIATOR");
    Serial.println("   Left hand=chord, Right hand=speed!");
    // Initialize both sensors
    if (!sensor_ch0_initialized) {
      sensor_ch0_initialized = initDistanceSensor(0);
    }
    if (!sensor_ch1_initialized) {
      sensor_ch1_initialized = initDistanceSensor(1);
    }
    ledVisualizationEnabled = true;
    clearAllLEDs();
    arpNoteIndex = 0;
    lastArpNoteTime = 0;
    delay(100);
  } else if (currentMode == MODE_STEP_SEQ) {
    Serial.println(">> Mode: STEP SEQUENCER");
    Serial.println("   Touch zones to toggle notes, pattern auto-loops!");
    if (!sensor_ch0_initialized) {
      sensor_ch0_initialized = initDistanceSensor(0);
    }
    if (!sensor_ch1_initialized) {
      sensor_ch1_initialized = initDistanceSensor(1);
    }
    ledVisualizationEnabled = true;
    clearAllLEDs();
    // Reset pattern
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        stepGrid[r][c] = false;
        gridZoneActive[r][c] = false;
        gridZoneLastTrigger[r][c] = 0;
      }
    }
    stepPosition = 0;
    lastStepTime = 0;
    delay(100);
  } else if (currentMode == MODE_DUAL_LOOP) {
    Serial.println(">> Mode: DUAL LOOP");
    Serial.println("   Left hand=melody notes, Right hand=drum hits");
    Serial.println("   Both auto-loop together! Build layers!");
    if (!sensor_ch0_initialized) {
      sensor_ch0_initialized = initDistanceSensor(0);
    }
    if (!sensor_ch1_initialized) {
      sensor_ch1_initialized = initDistanceSensor(1);
    }
    ledVisualizationEnabled = true;
    clearAllLEDs();
    // Reset both grids
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        dualLoopMelody[r][c] = false;
        dualLoopDrums[r][c] = false;
        dualLoopTouchL[r][c] = false;
        dualLoopTouchR[r][c] = false;
        dualLoopTouchTimeL[r][c] = 0;
        dualLoopTouchTimeR[r][c] = 0;
      }
    }
    dualLoopStep = 0;
    dualLoopLastStep = 0;
    delay(100);
  } else if (currentMode == MODE_CHORD_JAM) {
    Serial.println(">> Mode: CHORD JAM");
    Serial.println("   Left hand=select chord (I/IV/V/vi), Right hand=strum");
    chordJamIndex = 0;
    chordJamLastStrum = 0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_DRONE_SOLO) {
    Serial.println(">> Mode: DRONE + SOLO");
    Serial.println("   Left hand=drone pitch, Right hand=melody solo");
    droneActive = false;
    droneFreq = 130.81;
    soloLastNote = -1;
    // Re-tune snareDrum into a melodic pitched voice: no noise, no drum
    // pitch-drop, long ring-out so successive notes blend instead of "chop".
    snareDrum.length(500);
    snareDrum.secondMix(0.0);
    snareDrum.pitchMod(0.0);
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_RAIN_MODE) {
    Serial.println(">> Mode: RAIN MODE");
    Serial.println("   Catch falling notes with BOTH hands!");
    for (int i = 0; i < 8; i++) {
      rainDropActive[i] = false; rainDropRow[i] = 0;
      rainDropActiveR[i] = false; rainDropRowR[i] = 0;
    }
    rainLastFall = 0;
    rainLastSpawn = 0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_BASS_MACHINE) {
    Serial.println(">> Mode: BASS MACHINE");
    Serial.println("   Left hand=toggle bass notes, Right hand=filter wah");
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        bassGrid[r][c] = false;
        bassTouchState[r][c] = false;
        bassTouchTime[r][c] = 0;
      }
    }
    bassStep = 0;
    bassLastStep = 0;
    bassFilterFreq = 400.0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_ECHO_DELAY) {
    Serial.println(">> Mode: ECHO DELAY");
    Serial.println("   Left hand=play note, Right hand=delay time");
    echoPlaying = false;
    echoPlayIndex = 0;
    echoDelayMs = 300.0;
    for (int i = 0; i < 6; i++) { echoNotes[i] = 0; echoVolumes[i] = 0; }
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_BATTLE_MODE) {
    Serial.println(">> Mode: BATTLE MODE");
    Serial.println("   Two players! P1=CH0 (blue), P2=CH1 (red)");
    battleScore[0] = 0;
    battleScore[1] = 0;
    battleVol[0] = 0;
    battleVol[1] = 0;
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
    delay(100);
  } else if (currentMode == MODE_LED_TEST) {
    Serial.println(">> Mode: LED TEST");
    Serial.println("   Use 0-7 to select LED, R/G/B/Y/W/O to set color");
    clearAllLEDs();  // Clear all LEDs when entering mode
  } else if (currentMode == MODE_STRING_THEREMIN) {
    Serial.println(">> Mode: STRING THEREMIN");
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

    // Button action depends on current mode
    if (currentMode == MODE_STRING_THEREMIN) {
      // Cycle through scales
      int nextScale = (int)currentScale + 1;
      if (nextScale > SCALE_MINOR) nextScale = SCALE_PENTATONIC;
      currentScale = (ScaleType)nextScale;

      Serial.print(">> Scale changed to: ");
      Serial.println(getScaleString(currentScale));
    } else if (currentMode == MODE_DISTANCE_SENSOR) {
      // Cycle through sensor channels
      currentSensorChannel = (currentSensorChannel + 1) % 3;
      Serial.print(">> Sensor channel: ");
      if (currentSensorChannel == 0) Serial.println("CH0");
      else if (currentSensorChannel == 1) Serial.println("CH1");
      else Serial.println("BOTH");
    } else if (currentMode == MODE_DUAL_LOOP) {
      // Reset both melody and drum layers
      Serial.println(">> DUAL LOOP: Clearing all layers!");
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          dualLoopMelody[r][c] = false;
          dualLoopDrums[r][c] = false;
        }
      }
      dualLoopStep = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_STEP_SEQ) {
      // Reset step sequencer pattern
      Serial.println(">> STEP SEQ: Clearing pattern!");
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          stepGrid[r][c] = false;
        }
      }
      stepPosition = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_CHORD_JAM) {
      Serial.println(">> CHORD JAM: Reset!");
      chordJamIndex = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_DRONE_SOLO) {
      Serial.println(">> DRONE+SOLO: Reset!");
      droneActive = false;
      soloLastNote = -1;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_RAIN_MODE) {
      Serial.println(">> RAIN: Reset!");
      for (int i = 0; i < 8; i++) { rainDropActive[i] = false; rainDropActiveR[i] = false; }
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
    } else if (currentMode == MODE_ECHO_DELAY) {
      Serial.println(">> ECHO: Reset!");
      echoPlaying = false;
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
    } else if (currentMode == MODE_LED_TEST) {
      // Cycle LED color
      int nextColor = (int)currentLEDColor + 1;
      if (nextColor > LED_OFF) nextColor = LED_RED;
      currentLEDColor = (LEDColor)nextColor;
      setLED(selectedLED, getLEDColor(currentLEDColor));
      Serial.print(">> LED color: ");
      Serial.println(getScaleString(currentScale));  // Reuse function for color names
    }

    updateOLEDDisplay();
  }

  lastButtonState = buttonState;
}
