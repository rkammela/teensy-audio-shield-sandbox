# DISCO — Source Code (Abridged)

> **Touchless Musical Instrument** · Teensy 4.0 + Dual VL53L5CX 8×8 ToF Sensors + SGTL5000 Audio Shield + WS2812B LED Matrix
>
> Full source: `src/main.cpp` (~3,900 lines). Abridged below to highlight architecture and key interaction logic.

---

## 1. Libraries

```cpp
#include <Audio.h>                      // Teensy Audio (Karplus-Strong, drums, filters)
#include <Wire.h>                       // I2C for dual ToF sensors
#include <SparkFun_VL53L5CX_Library.h>  // 8×8 Time-of-Flight distance sensors
#include <FastLED.h>                    // WS2812B addressable LED matrix
#include <Adafruit_SSD1306.h>           // 128×64 OLED display
#include <Encoder.h>                    // Rotary encoder for mode selection
```

---

## 2. Twenty Play Modes

```cpp
enum PlayMode {
  MODE_STRING_THEREMIN,   // Karplus-Strong string, pitch mapped to distance
  MODE_GESTURE_DRUM,      // Jab/tap gestures trigger kick/snare/hat
  MODE_THEREMIN,          // Classic continuous-pitch theremin
  MODE_DISTANCE_SENSOR,   // Raw 8×8 heatmap visualizer
  MODE_DUAL_THEREMIN,     // Left hand = pitch, Right hand = volume
  MODE_DUAL_DRUMS,        // Spatial drum kit across two sensors
  MODE_DUAL_PIANO,        // Left = note select, Right = velocity
  MODE_GRID_SEQUENCER,    // 64-zone launchpad: each zone = unique note
  MODE_RHYTHM_TAPPER,     // 8-row drum machine, instant response
  MODE_ARPEGGIATOR,       // Auto-arpeggiating chords, speed by distance
  MODE_DJ_SCRATCH,        // Vinyl scratch effects from hand speed
  MODE_STEP_SEQ,          // Toggle-grid 8-step beat sequencer
  MODE_DUAL_LOOP,         // Left = melody layer, Right = drums (synced)
  MODE_CHORD_JAM,         // Left picks chord (I/IV/V/vi), Right strums
  MODE_DRONE_SOLO,        // Left holds drone, Right plays melody on top
  MODE_RAIN_MODE,         // Falling notes: catch them with your hands
  MODE_BASS_MACHINE,      // Looping bass builder + filter wah
  MODE_ECHO_DELAY,        // Notes echo back with diminishing repeats
  MODE_BATTLE_MODE,       // Two-player dueling melodies
  MODE_LED_TEST           // LED diagnostic mode
};

enum ScaleType { SCALE_PENTATONIC, SCALE_MAJOR, SCALE_MINOR };
enum Zone      { ZONE_A_NEAR, ZONE_B_MID, ZONE_C_FAR };
```

---

## 3. Audio Signal Chain

```cpp
// === AUDIO OBJECTS ===
AudioSynthKarplusStrong  stringVoice;     // Physical-modeled plucked string
AudioEffectEnvelope      stringEnvelope;  // ADSR envelope shaping
AudioFilterStateVariable stringFilter;    // Resonant low-pass filter
AudioSynthSimpleDrum     kickDrum;        // Synthesized kick drum
AudioSynthSimpleDrum     snareDrum;       // Synthesized snare drum
AudioSynthNoiseWhite     noiseWhite;      // White noise for hi-hat
AudioEffectEnvelope      hatEnvelope;     // Hi-hat envelope (noise → short burst)
AudioMixer4              mixer1;          // 4-channel mixer
AudioOutputI2S           i2s1;            // I2S DAC output to headphones/line out
AudioControlSGTL5000     sgtl5000_1;      // Audio shield hardware controller

// === PATCH CORDS (Signal Routing) ===
// String: stringVoice → envelope → filter → mixer[0]
AudioConnection patchCord1(stringVoice, stringEnvelope);
AudioConnection patchCord2(stringEnvelope, 0, stringFilter, 0);
AudioConnection patchCord3(stringFilter, 0, mixer1, 0);
// Kick drum → mixer[1]
AudioConnection patchCord4(kickDrum, 0, mixer1, 1);
// Snare drum → mixer[2]
AudioConnection patchCord5(snareDrum, 0, mixer1, 2);
// Hi-hat: noise → envelope → mixer[3]
AudioConnection patchCord6(noiseWhite, hatEnvelope);
AudioConnection patchCord7(hatEnvelope, 0, mixer1, 3);
// Mixer → I2S stereo output (both channels)
AudioConnection patchCord8(mixer1, 0, i2s1, 0);  // Left
AudioConnection patchCord9(mixer1, 0, i2s1, 1);  // Right
```

---

## 4. Hardware Pin Map

```cpp
// --- Dual I2C for ToF Sensors ---
// CH0: Wire  (SDA=18, SCL=19) — Left sensor
// CH1: Wire1 (SDA=17, SCL=16) — Right sensor
SparkFun_VL53L5CX sensor_ch0, sensor_ch1;
uint16_t distanceGrid_ch0[8][8];  // 64-zone distance arrays (mm)
uint16_t distanceGrid_ch1[8][8];

// --- LED Matrix ---
#define LED_PIN     0         // GPIO 0
#define NUM_LEDS    64        // 8×8 matrix
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

---

## 5. Core Constants & Key State Variables

```cpp
float masterVolume = 1.0;              // 100% default
const int GRID_ZONE_THRESHOLD = 400;   // mm — hand within 40 cm to register a "touch"
const int GRID_DEBOUNCE_MS    = 50;    // Prevents rapid re-triggers per zone
const int DRUM_HIT_COOLDOWN   = 80;    // ms between drum hits

// Distance zones for String Theremin mode
const int DIST_MIN = 80, DIST_MAX = 800;
const int ZONE_A_MAX = 250;  // 80–250 mm  = Zone A (bass)
const int ZONE_B_MAX = 500;  // 250–500 mm = Zone B (melody)
                              // 500–800 mm = Zone C (harmonics)

// Musical scales (semitone offsets from root note)
const int pentatonicScale[] = {0, 2, 4, 7, 9};        // 5-note
const int majorScale[]      = {0, 2, 4, 5, 7, 9, 11}; // 7-note
const int minorScale[]      = {0, 2, 3, 5, 7, 8, 10}; // 7-note

// Dual Loop — layered melody + drum pattern builder
bool dualLoopMelody[8][8] = {false};   // Left hand: 8 steps × 8 notes
bool dualLoopDrums[8][8]  = {false};   // Right hand: 8 steps × 8 drums
int  dualLoopStep = 0;
float dualLoopTempo = 180.0;           // ms per step

// Rain Mode — falling-note game
float rainDropRow[8]    = {0};         // Y position (smooth float for animation)
int   rainDropNote[8]   = {0};         // Column/note assignment per drop
bool  rainDropActive[8] = {false};     // Active slot flags

// Bass Machine — looping bass with wah filter
bool  bassGrid[8][8] = {false};        // 8 steps × 8 bass notes
float bassFilterFreq = 400.0;          // Right hand controls this (wah)

// Echo Delay — note + diminishing repeats
float echoNotes[6]   = {0};            // Circular buffer of frequencies
float echoVolumes[6] = {0};            // Volume per echo (decaying)
float echoDelayMs    = 300.0;          // Right hand controls delay time

// Battle Mode — two-player competition
float battleFreq[2]  = {440.0, 440.0};
int   battleScore[2] = {0, 0};         // "Dominance" score for LED display
```

---

## 6. Setup & Main Loop

```cpp
void setup() {
  Serial.begin(115200);
  delay(1000);

  AudioMemory(20);                        // Allocate audio processing blocks
  sgtl5000_1.enable();                    // Power on audio shield
  sgtl5000_1.volume(masterVolume);        // Set headphone/line-out volume

  // Initial mixer gains (each channel × master volume)
  mixer1.gain(0, 0.8 * masterVolume);     // Strings
  mixer1.gain(1, 0.8 * masterVolume);     // Kick
  mixer1.gain(2, 0.7 * masterVolume);     // Snare
  mixer1.gain(3, 0.5 * masterVolume);     // Hi-hat

  setupAudio();                           // Configure envelopes, filters, drums
  setupLEDs();                            // Initialize FastLED (WS2812B on pin 0)
  setupOLED();                            // Initialize 128×64 OLED display

  pinMode(ENCODER_BUTTON, INPUT_PULLUP);

  // Initialize both ToF sensors (8×8 mode, 10 Hz ranging)
  sensor_ch0_initialized = initDistanceSensor(0);  // Left  (Wire,  18/19)
  sensor_ch1_initialized = initDistanceSensor(1);  // Right (Wire1, 17/16)

  updateOLEDDisplay();
}

void loop() {
  handleEncoder();         // Rotary encoder → cycle through 20 modes
  handleEncoderButton();   // Button press → reset/clear current pattern

  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL_MS) {  // 50 Hz update
    lastUpdateTime = currentTime;
    smoothSensorValues();  // Low-pass filter on raw distance readings

    // Dispatch to active mode's process function
    if      (currentMode == MODE_STRING_THEREMIN) updateStringVoice();
    else if (currentMode == MODE_DUAL_THEREMIN)   processDualTheremin();
    else if (currentMode == MODE_DUAL_DRUMS)      processDualDrums();
    else if (currentMode == MODE_GRID_SEQUENCER)  processGridSequencer();
    else if (currentMode == MODE_STEP_SEQ)        processStepSequencer();
    else if (currentMode == MODE_DUAL_LOOP)       processDualLoop();
    else if (currentMode == MODE_CHORD_JAM)       processChordJam();
    else if (currentMode == MODE_DRONE_SOLO)      processDroneSolo();
    else if (currentMode == MODE_RAIN_MODE)       processRainMode();
    else if (currentMode == MODE_BASS_MACHINE)    processBassMachine();
    else if (currentMode == MODE_ECHO_DELAY)      processEchoDelay();
    else if (currentMode == MODE_BATTLE_MODE)     processBattleMode();
    // ... (remaining modes follow the same dispatch pattern)
  }
}
```

---

## 7. ToF Sensor Functions

```cpp
bool initDistanceSensor(uint8_t channel) {
  // Initialize VL53L5CX sensor in 8×8 mode (64 independent distance zones)
  // channel 0 = I2C0 (Wire, SDA=18, SCL=19) — Left sensor
  // channel 1 = I2C1 (Wire1, SDA=17, SCL=16) — Right sensor

  if (channel == 0) {
    Wire.begin();
    Wire.setClock(400000);
    if (!sensor_ch0.begin(0x29, Wire)) return false;
    sensor_ch0.setResolution(8 * 8);     // 64-zone mode
    sensor_ch0.setRangingFrequency(10);  // 10 Hz
    sensor_ch0.startRanging();
  } else {
    Wire1.begin();
    Wire1.setClock(400000);
    if (!sensor_ch1.begin(0x29, Wire1)) return false;
    sensor_ch1.setResolution(8 * 8);
    sensor_ch1.setRangingFrequency(10);
    sensor_ch1.startRanging();
  }
  return true;
}

void readDistanceGrid(uint8_t channel) {
  // Reads 64 distance values into distanceGrid_ch0[][] or ch1[][]
  SparkFun_VL53L5CX* sensor = (channel == 0) ? &sensor_ch0 : &sensor_ch1;
  uint16_t (*grid)[8] = (channel == 0) ? distanceGrid_ch0 : distanceGrid_ch1;

  if (sensor->isDataReady()) {
    VL53L5CX_ResultsData results;
    sensor->getRangingData(&results);
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++)
        grid[row][col] = results.distance_mm[row * 8 + col];
  }
}

// Calculate hand centroid and coverage from an 8×8 grid
void calculateHandMetrics(uint16_t grid[8][8],
    float& avgDistance, int& centroidX, int& centroidY, int& activeZones) {
  float sumDist = 0, sumX = 0, sumY = 0;
  activeZones = 0;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if (grid[row][col] < GRID_ZONE_THRESHOLD && grid[row][col] > 50) {
        sumDist += grid[row][col];
        sumX += col;  sumY += row;
        activeZones++;
      }
    }
  }
  if (activeZones > 0) {
    avgDistance = sumDist / activeZones;
    centroidX  = (int)(sumX / activeZones);
    centroidY  = (int)(sumY / activeZones);
  } else {
    avgDistance = 2000.0;
    centroidX = centroidY = 4;
  }
}
```

---

## 8. Audio Setup & Music Theory

```cpp
void setupAudio() {
  // String voice: ADSR envelope for natural pluck/decay
  stringEnvelope.attack(10.0);     // 10 ms attack
  stringEnvelope.hold(20.0);
  stringEnvelope.decay(500.0);     // Half-second decay
  stringEnvelope.sustain(0.4);
  stringEnvelope.release(300.0);

  // Low-pass filter for timbre control
  stringFilter.frequency(2000);
  stringFilter.resonance(1.2);
  stringFilter.octaveControl(2.0);

  // Kick drum: low frequency, long sustain
  kickDrum.frequency(60);
  kickDrum.length(300);
  kickDrum.pitchMod(0.55);

  // Snare drum: mid frequency, shorter
  snareDrum.frequency(200);
  snareDrum.length(150);
  snareDrum.secondMix(0.5);

  // Hi-hat: very short noise burst
  hatEnvelope.attack(1.0);
  hatEnvelope.decay(50.0);
  hatEnvelope.sustain(0.0);
  hatEnvelope.release(50.0);
  noiseWhite.amplitude(0.3);
}

// Convert MIDI note → frequency: f = 440 × 2^((note − 69) / 12)
float noteToFrequency(int midiNote) {
  return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

// Convert note index → frequency using selected musical scale
float noteIndexToFrequency(int noteIndex, ScaleType scale, Zone zone) {
  const int* scaleNotes;
  int scaleSize, baseOctave;

  switch (scale) {
    case SCALE_PENTATONIC: scaleNotes = pentatonicScale; scaleSize = 5; break;
    case SCALE_MAJOR:      scaleNotes = majorScale;      scaleSize = 7; break;
    case SCALE_MINOR:      scaleNotes = minorScale;      scaleSize = 7; break;
  }

  // Zone determines octave: A = C3 (bass), B = C4 (melody), C = C5 (harmonics)
  if      (zone == ZONE_A_NEAR) baseOctave = 48;  // C3
  else if (zone == ZONE_B_MID)  baseOctave = 60;  // C4 (middle C)
  else                          baseOctave = 72;  // C5

  int wrappedIndex = noteIndex % scaleSize;
  int octaveShift  = noteIndex / scaleSize;
  int midiNote     = baseOctave + scaleNotes[wrappedIndex] + (octaveShift * 12);

  return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}
```

---

## 9. Key Mode Implementations

### 9a. String Theremin (Default Mode)

Pitch hand distance maps to musical notes; expression hand controls volume.

```cpp
void updateStringVoice() {
  // No hand detected → silence
  if (pitchHandSmooth > PRESENCE_THRESHOLD) {
    if (noteActive) { stringEnvelope.noteOff(); noteActive = false; }
    return;
  }

  Zone newZone = calculateZone(pitchHandSmooth);
  int newNoteIndex = distanceToNoteIndex(pitchHandSmooth, newZone);
  float baseFreq = noteIndexToFrequency(newNoteIndex, currentScale, newZone);

  // Optional vibrato effect
  float finalFreq = baseFreq;
  if (vibratoEnabled) {
    vibratoPhase += VIBRATO_RATE * (UPDATE_INTERVAL_MS / 1000.0) * 2.0 * PI;
    finalFreq = baseFreq * (1.0 + sin(vibratoPhase) * VIBRATO_DEPTH);
  }

  bool noteChanged = (newNoteIndex != currentNoteIndex) || (newZone != currentZone);
  bool needRepluck = continuousSound && (millis() - lastPluckTime) > REPLUCK_INTERVAL_MS;

  if (noteChanged || needRepluck) {
    float velocity = noteChanged ? 0.7 : 0.4;
    stringVoice.noteOn(finalFreq, velocity);  // Pluck the string
    stringEnvelope.noteOn();                  // Gate the envelope
    noteActive = true;
    lastPluckTime = millis();
  }

  // Expression hand → volume (closer = louder)
  float exprNorm = 1.0 - ((exprHandSmooth - DIST_MIN) / (float)(DIST_MAX - DIST_MIN));
  exprNorm = constrain(exprNorm, 0.0, 1.0);
  mixer1.gain(0, exprNorm * 0.8 * masterVolume);
}
```

### 9b. Grid Sequencer (64-Zone Launchpad)

Each of 64 sensor zones maps to a unique note. Touch any zone for instant playback.

```cpp
void processGridSequencer() {
  static const int gridNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};  // C major
  unsigned long currentTime = millis();
  if (sensor_ch0_initialized) readDistanceGrid(0);

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = gridZoneActive[row][col];

      if (isTouched && !wasTouched &&
          (currentTime - gridZoneLastTrigger[row][col]) > GRID_DEBOUNCE_MS) {
        gridZoneLastTrigger[row][col] = currentTime;

        // Row = octave offset, Col = scale note
        int midiNote = gridNotes[col] + (row * 12);
        float freq = 440.0 * pow(2.0, (midiNote - 69) / 12.0);
        float velocity = constrain(1.0 - (dist / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);

        stringFilter.frequency(freq * 2.5);
        stringVoice.noteOn(freq, velocity);
        stringEnvelope.noteOn();
        noteActive = true;
        mixer1.gain(0, velocity * masterVolume);
      }
      gridZoneActive[row][col] = isTouched;
    }
  }

  // LED: rainbow color per zone, brightness by proximity
  if (ledVisualizationEnabled) {
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int ledIndex = xyToLEDIndex(row, col);
        uint16_t d = distanceGrid_ch0[row][col];
        uint8_t hue = (row * 32 + col * 32) % 256;
        if (d < GRID_ZONE_THRESHOLD && d > 50) {
          uint8_t brightness = map(d, 50, GRID_ZONE_THRESHOLD, 255, 60);
          leds[ledIndex] = CHSV(hue, 255, brightness);
        } else {
          leds[ledIndex] = CHSV(hue, 255, 15);  // Dim idle
        }
      }
    }
    FastLED.show();
  }
}
```

### 9c. Dual Loop (Layered Melody + Drums)

Left hand toggles melody notes, right hand toggles drum hits.
Both layers loop together in sync over an 8-step timeline.

```cpp
void processDualLoop() {
  static const float melodyNotes[8] = {  // C major scale
    261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25
  };
  unsigned long currentTime = millis();

  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- LEFT HAND: Toggle melody notes in the pattern ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch0[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = dualLoopTouchL[row][col];
      if (isTouched && !wasTouched &&
          (currentTime - dualLoopTouchTimeL[row][col]) > 150) {
        dualLoopMelody[col][row] = !dualLoopMelody[col][row];  // Toggle
        dualLoopTouchTimeL[row][col] = currentTime;
      }
      dualLoopTouchL[row][col] = isTouched;
    }
  }

  // --- RIGHT HAND: Toggle drum hits in the pattern ---
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = distanceGrid_ch1[row][col];
      bool isTouched = (dist < GRID_ZONE_THRESHOLD && dist > 50);
      bool wasTouched = dualLoopTouchR[row][col];
      if (isTouched && !wasTouched &&
          (currentTime - dualLoopTouchTimeR[row][col]) > 150) {
        dualLoopDrums[col][row] = !dualLoopDrums[col][row];  // Toggle
        dualLoopTouchTimeR[row][col] = currentTime;
      }
      dualLoopTouchR[row][col] = isTouched;
    }
  }

  // --- AUTO-ADVANCE PLAYHEAD ---
  if (currentTime - dualLoopLastStep >= (unsigned long)dualLoopTempo) {
    dualLoopLastStep = currentTime;

    // Play melody notes at current step
    for (int note = 0; note < 8; note++) {
      if (dualLoopMelody[dualLoopStep][note]) {
        stringFilter.frequency(melodyNotes[note] * 2.5);
        stringVoice.noteOn(melodyNotes[note], 0.7);
        stringEnvelope.noteOn();
        noteActive = true;
        mixer1.gain(0, 0.7 * masterVolume);
      }
    }

    // Play drum hits at current step
    for (int drum = 0; drum < 8; drum++) {
      if (dualLoopDrums[dualLoopStep][drum]) {
        if      (drum < 2) { kickDrum.noteOn();  mixer1.gain(1, 0.8 * masterVolume); }
        else if (drum < 4) { snareDrum.noteOn(); mixer1.gain(2, 0.7 * masterVolume); }
        else if (drum < 6) { hatEnvelope.noteOn(); mixer1.gain(3, 0.4 * masterVolume); }
        else               { snareDrum.noteOn(); mixer1.gain(2, 0.5 * masterVolume); }
      }
    }
    dualLoopStep = (dualLoopStep + 1) % 8;  // Wrap around
  }

  // --- LED: Green=melody, Orange=drums, Yellow=both, White=playhead ---
  // ... (LED visualization code, ~30 lines)
}
```


### 9d. Chord Jam (Gesture Strumming)

Left hand selects chord (I/IV/V/vi), right hand "strums" with a sweeping gesture.

```cpp
void processChordJam() {
  // Chord definitions in C major
  static const int chords[4][3] = {
    {60, 64, 67},  // I  = C E G
    {65, 69, 72},  // IV = F A C5
    {67, 71, 74},  // V  = G B D5
    {69, 72, 76}   // vi = A C5 E5
  };
  unsigned long currentTime = millis();
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- LEFT HAND: Select chord by row position (4 quadrants) ---
  float closestDist = 9999;
  int leftQuadrant = -1;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t d = distanceGrid_ch0[row][col];
      if (d < GRID_ZONE_THRESHOLD && d > 50 && d < closestDist) {
        closestDist = d;
        leftQuadrant = row / 2;  // 0-3 maps to I, IV, V, vi
      }
    }
  }
  if (leftQuadrant >= 0 && leftQuadrant < 4)
    chordJamIndex = leftQuadrant;

  // --- RIGHT HAND: Strum detection (many zones active = hand sweep) ---
  int rightActiveCount = 0;
  float rightAvgDist = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t d = distanceGrid_ch1[row][col];
      if (d < GRID_ZONE_THRESHOLD && d > 50) { rightActiveCount++; rightAvgDist += d; }
    }
  }
  if (rightActiveCount > 0) rightAvgDist /= rightActiveCount;

  // Strum triggers when 6+ zones are touched simultaneously
  if (rightActiveCount > 6 && (currentTime - chordJamLastStrum) > 120) {
    chordJamLastStrum = currentTime;
    float velocity = constrain(1.0 - (rightAvgDist / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);

    // Play all 3 notes of the selected chord
    for (int i = 0; i < 3; i++) {
      float freq = 440.0 * pow(2.0, (chords[chordJamIndex][i] - 69) / 12.0);
      stringFilter.frequency(freq * 2.5);
      stringVoice.noteOn(freq, velocity);
      stringEnvelope.noteOn();
      noteActive = true;
      mixer1.gain(0, velocity * masterVolume);
    }
  }
  // ... (LED visualization: left = chord colors, right = strum flash)
}
```

### 9e. Rain Mode (Musical Game)

Notes automatically "rain down" the LED grid. Catch them with your hand to play the sound.

```cpp
void processRainMode() {
  static const int rainScaleNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};
  unsigned long currentTime = millis();
  if (sensor_ch0_initialized) readDistanceGrid(0);

  // Spawn new raindrops every 600 ms
  if (currentTime - rainLastSpawn > 600) {
    rainLastSpawn = currentTime;
    for (int i = 0; i < 8; i++) {
      if (!rainDropActive[i]) {
        rainDropActive[i] = true;
        rainDropRow[i] = 0.0;          // Start at top
        rainDropNote[i] = random(0, 8); // Random column
        break;
      }
    }
  }

  // Move drops down & check for hand catches (every 50 ms)
  if (currentTime - rainLastFall > 50) {
    rainLastFall = currentTime;
    for (int i = 0; i < 8; i++) {
      if (!rainDropActive[i]) continue;
      rainDropRow[i] += rainSpeed;

      int row = (int)rainDropRow[i];
      int col = rainDropNote[i];
      if (row >= 0 && row < 8) {
        uint16_t d = distanceGrid_ch0[row][col];
        if (d < GRID_ZONE_THRESHOLD && d > 50) {
          // CAUGHT! Play the note
          float freq = 440.0 * pow(2.0, (rainScaleNotes[col] - 69) / 12.0);
          float velocity = constrain(1.0 - (d / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
          stringFilter.frequency(freq * 2.5);
          stringVoice.noteOn(freq, velocity);
          stringEnvelope.noteOn();
          noteActive = true;
          mixer1.gain(0, velocity * masterVolume);
          rainDropActive[i] = false;  // Remove caught drop
          continue;
        }
      }
      if (rainDropRow[i] >= 8.0) rainDropActive[i] = false;  // Missed
    }
  }
  // ... (LED: colored drops fall with trails, hand shown in green)
}
```

### 9f. Battle Mode (Two-Player)

Each player controls one sensor. Both play notes simultaneously with competing LED displays.

```cpp
void processBattleMode() {
  static const int battleNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);

  // --- PLAYER 1 (CH0): plays on string voice ---
  int p1Row = -1;  float p1Dist = 9999;  int p1Zones = 0;
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
      uint16_t d = distanceGrid_ch0[r][c];
      if (d < GRID_ZONE_THRESHOLD && d > 50) {
        p1Zones++;
        if (d < p1Dist) { p1Dist = d; p1Row = r; }
      }
    }
  if (p1Row >= 0) {
    float freq = 440.0 * pow(2.0, (battleNotes[p1Row] - 69) / 12.0);
    float vel = constrain(1.0 - (p1Dist / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
    stringFilter.frequency(freq * 2.5);
    stringVoice.noteOn(freq, vel);
    stringEnvelope.noteOn();
    mixer1.gain(0, vel * masterVolume);
  }

  // --- PLAYER 2 (CH1): plays on snare voice (different timbre) ---
  int p2Row = -1;  float p2Dist = 9999;  int p2Zones = 0;
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
      uint16_t d = distanceGrid_ch1[r][c];
      if (d < GRID_ZONE_THRESHOLD && d > 50) {
        p2Zones++;
        if (d < p2Dist) { p2Dist = d; p2Row = r; }
      }
    }
  if (p2Row >= 0) {
    float freq = 440.0 * pow(2.0, (battleNotes[p2Row] - 69) / 12.0);
    float vel = constrain(1.0 - (p2Dist / (float)GRID_ZONE_THRESHOLD), 0.3, 1.0);
    snareDrum.frequency(freq);
    snareDrum.noteOn();
    mixer1.gain(2, vel * masterVolume);
  }

  // Score: who's more active
  if (p1Zones > p2Zones + 3) battleScore[0] = min(battleScore[0] + 1, 64);
  else if (p2Zones > p1Zones + 3) battleScore[1] = min(battleScore[1] + 1, 64);

  // LED: Left = Player 1 (blue), Right = Player 2 (red), score bars fill up
  // ... (LED visualization, ~25 lines)
}
```

> **Note:** Modes not shown above (Drone+Solo, Bass Machine, Echo Delay, Arpeggiator,
> DJ Scratch, Step Sequencer, Dual Theremin, Dual Drums, Dual Piano, Rhythm Tapper)
> follow the same architectural pattern: read sensor grids → map zones to musical
> parameters → trigger audio objects → update LED visualization.

---

## 10. LED Matrix & Serpentine Mapping

```cpp
int xyToLEDIndex(int row, int col) {
  // Convert 2D matrix coordinates to LED index for SERPENTINE layout
  // Even rows: left-to-right, Odd rows: right-to-left
  if (row % 2 == 0) return row * 8 + col;
  else              return row * 8 + (7 - col);
}

void clearAllLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
}

void setupLEDs() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(60);
  clearAllLEDs();
  FastLED.show();
}
```

---

## 11. Rotary Encoder & Mode Switching

```cpp
void handleEncoder() {
  long newPosition = knob.read();
  // 4 pulses per detent → divide by 4
  if ((newPosition - lastEncoderPosition) >= 4) {
    lastEncoderPosition = newPosition;
    // Cycle through modes: STRING → DRUM → ... → BATTLE → LED_TEST → STRING
    PlayMode nextMode;
    if      (currentMode == MODE_STRING_THEREMIN) nextMode = MODE_GESTURE_DRUM;
    else if (currentMode == MODE_DUAL_LOOP)       nextMode = MODE_CHORD_JAM;
    else if (currentMode == MODE_CHORD_JAM)       nextMode = MODE_DRONE_SOLO;
    // ... (all 20 modes chain to the next)
    else if (currentMode == MODE_BATTLE_MODE)     nextMode = MODE_LED_TEST;
    else                                          nextMode = MODE_STRING_THEREMIN;
    switchToMode(nextMode);
  }
}

void handleEncoderButton() {
  bool buttonState = digitalRead(ENCODER_BUTTON);
  unsigned long currentTime = millis();

  if (buttonState == LOW && lastButtonState == HIGH &&
      (currentTime - lastButtonPress) > DEBOUNCE_DELAY) {
    lastButtonPress = currentTime;

    // Context-sensitive button action
    if (currentMode == MODE_DUAL_LOOP) {
      // Clear all melody and drum patterns
      for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
          dualLoopMelody[r][c] = false;
          dualLoopDrums[r][c] = false;
        }
      dualLoopStep = 0;
      panicMute();
      clearAllLEDs();
      FastLED.show();
    } else if (currentMode == MODE_BASS_MACHINE) {
      // Clear bass pattern
      for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) bassGrid[r][c] = false;
      bassStep = 0;
      panicMute();
    } else if (currentMode == MODE_BATTLE_MODE) {
      // Reset scores
      battleScore[0] = battleScore[1] = 0;
      panicMute();
    }
    // ... (similar reset logic for each pattern-based mode)
    updateOLEDDisplay();
  }
  lastButtonState = buttonState;
}
```

---

## 12. OLED Display

```cpp
void updateOLEDDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Line 1: Mode name (large)
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(getModeString(currentMode));

  // Line 2: Scale (if applicable)
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print("Scale: ");
  display.println(getScaleString(currentScale));

  // Line 3: Volume bar
  display.setCursor(0, 40);
  display.print("Vol: ");
  int barWidth = (int)(masterVolume * 80);
  display.fillRect(30, 40, barWidth, 8, SSD1306_WHITE);

  display.display();
}

const char* getModeString(PlayMode mode) {
  switch (mode) {
    case MODE_STRING_THEREMIN: return "STRING";
    case MODE_DUAL_LOOP:       return "DUAL LOOP";
    case MODE_CHORD_JAM:       return "CHORD JAM";
    case MODE_RAIN_MODE:       return "RAIN";
    case MODE_BASS_MACHINE:    return "BASS";
    case MODE_ECHO_DELAY:      return "ECHO";
    case MODE_BATTLE_MODE:     return "BATTLE";
    // ... (all 20 modes)
    default: return "UNKNOWN";
  }
}
```

---

## 13. Utility Functions

```cpp
void panicMute() {
  // Emergency silence: stop all audio sources
  stringEnvelope.noteOff();
  kickDrum.noteOn();  // SimpleDrum auto-decays
  snareDrum.noteOn();
  hatEnvelope.noteOff();
  noteActive = false;
  mixer1.gain(0, 0);
  mixer1.gain(1, 0);
  mixer1.gain(2, 0);
  mixer1.gain(3, 0);
}

void switchToMode(PlayMode newMode) {
  panicMute();                    // Silence before switching
  currentMode = newMode;
  clearAllLEDs();
  FastLED.show();

  // Mode-specific initialization
  if (newMode == MODE_RAIN_MODE) {
    for (int i = 0; i < 8; i++) rainDropActive[i] = false;
  } else if (newMode == MODE_BASS_MACHINE) {
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 8; c++) bassGrid[r][c] = false;
    bassStep = 0;
  } else if (newMode == MODE_BATTLE_MODE) {
    battleScore[0] = battleScore[1] = 0;
  }
  // ... (initialization for each mode)

  // Re-init sensors needed for this mode
  if (sensor_ch0_initialized) readDistanceGrid(0);
  if (sensor_ch1_initialized) readDistanceGrid(1);
  updateOLEDDisplay();
}
```

---

> *This abridged version covers the essential architecture, signal chain, sensor interface,*
> *music theory mapping, and representative mode implementations. The full source contains*
> *all 20 complete mode implementations with LED visualizations, serial debug interface,*
> *and additional helper functions.*