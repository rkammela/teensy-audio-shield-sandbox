# Understanding AURA's `main.cpp`

### A Beginner's Guide to the C++ Code Behind AURA

Hey! This guide is for anyone who wants to understand the `main.cpp` file in our AURA project but has never written C++ before. Don't worry if you've never coded before either, we'll start from the very beginning.

The guide is in two parts:

- **Part 1** teaches you the C++ tools we use in `main.cpp` (comments, variables, functions, if statements, and so on).
- **Part 2** walks through the real `main.cpp` file section by section and explains what each block of code actually does.

---

# Part 1: C++ Basics

## 1. Comments

A comment is a note for humans. The computer ignores it completely when it runs the code.

There are two kinds of comments in C++:

- `// like this` is a single-line comment. Anything after the `//` on that line is ignored.
- `/* like this */` is a multi-line comment. Everything between `/*` and `*/` is ignored, even if it spans many lines.

Example:

```cpp
// This is a single line comment
int x = 5;  // Comments can also go after some code

/* This is a comment
   that goes over
   multiple lines */
```

In AURA, comments help us remember what each piece of the code is for, so future-us (or someone reading our code for the first time) doesn't get lost.

## 2. `#include` — Bringing in Code from Other Files

Big programs are built from lots of small files. The `#include` line tells the compiler "go grab the code from another file and use it right here."

There are two styles:

- `#include <Arduino.h>` — the **angle brackets** are for libraries that come with the system (like the Arduino library, or FastLED, which is a library we installed).
- `#include "Config.h"` — the **quotes** are for files we wrote ourselves and put inside our own project.

In AURA we include all the audio, sensor, LED, and screen libraries, plus all our own header files. Each include is basically us saying "I'm going to use stuff from this file in here."

## 3. Variables and Data Types

A variable is like a labeled box that stores one value. You give it a **type** (what kind of value it holds), a **name**, and usually a starting value:

```cpp
int score = 0;        // a whole number
float volume = 1.0;   // a number with a decimal
bool isOn = true;     // either true or false
```

The most common types you'll see in `main.cpp`:

| Type | What it holds | Example |
|---|---|---|
| `int` | a whole number | `int count = 5;` |
| `float` | a decimal number | `float vol = 0.5;` |
| `bool` | true or false | `bool ready = true;` |
| `uint8_t` | a tiny whole number (0 to 255) | `uint8_t channel = 0;` |
| `uint16_t` | a small whole number (0 to 65,535) | `uint16_t distance = 200;` |
| `unsigned long` | a very big whole number | `unsigned long t = millis();` |
| `const char*` | a string of text | `const char* name = "AURA";` |

Why are there so many number types? Because on a tiny computer like the Teensy, using a smaller type saves memory. A `uint8_t` only uses 1 byte while an `int` uses 4 bytes, so if you only need numbers 0 to 255 it's nicer to pick the smaller one.

### Arrays

An array is a list of values, all the same type. You say how big the list is in square brackets:

```cpp
int scores[8];  // a list of 8 whole numbers
```

A 2D array is a grid (kind of like a chess board with rows and columns):

```cpp
uint16_t distanceGrid_ch0[8][8];  // 8 rows by 8 columns = 64 cells
```

In AURA we use 2D arrays to store the data from our 8x8 distance sensors. Each cell in the grid holds the distance to whatever is above that part of the sensor.

## 4. Functions

A function is a chunk of reusable code that does one specific job. It has a name, optional inputs (called **parameters**), and an output (called the **return value**).

The basic shape of a function is:

```cpp
returnType functionName(parameters) {
  // code that does something
  return something;  // (only if returnType isn't 'void')
}
```

A simple example:

```cpp
int addTwo(int a, int b) {
  return a + b;
}
```


A function that doesn't return anything uses the keyword `void`:

```cpp
void sayHi() {
  Serial.println("Hi!");
}
```

You **call** a function by writing its name with parentheses: `sayHi();` or `int total = addTwo(3, 5);`.

### Forward declarations

C++ reads code top to bottom, so if you call a function before it's been written in the file, you have to **announce** it first. That announcement is called a forward declaration:

```cpp
void setupLEDs();  // forward declaration (just the signature, no body)
```

Later in the file you fill in the actual code. In `main.cpp` we forward-declare a bunch of helper functions near the top so the compiler is okay with `setup()` and `loop()` using them.

## 5. Operators (Math and Comparisons)

Operators are little symbols that do something to values.

**Math operators:**

| Symbol | What it does | Example |
|---|---|---|
| `+` | add | `3 + 2` is `5` |
| `-` | subtract | `5 - 1` is `4` |
| `*` | multiply | `4 * 2` is `8` |
| `/` | divide | `10 / 2` is `5` |
| `%` | remainder (modulo) | `10 % 3` is `1` |

**Comparison operators** (these give back `true` or `false`):

| Symbol | What it asks |
|---|---|
| `==` | are these equal? |
| `!=` | are these *not* equal? |
| `<`  | is the left smaller? |
| `>`  | is the left bigger? |
| `<=` | is the left smaller or equal? |
| `>=` | is the left bigger or equal? |

**Logical operators** (also give true/false):

| Symbol | What it means |
|---|---|
| `&&` | AND (both have to be true) |
| `\|\|` | OR (at least one is true) |
| `!`  | NOT (flips true into false) |

> **Heads up:** `=` is for *assigning* a value (`x = 5;`) and `==` is for *checking equality* (`if (x == 5)`). Mixing them up is one of the most common C++ bugs ever.

## 6. `if` / `else if` / `else` — Making Decisions

This is how the code makes choices:

```cpp
if (currentMode == MODE_CHORD_JAM) {
  // do chord jam stuff
} else if (currentMode == MODE_BATTLE_MODE) {
  // do battle stuff
} else {
  // do something else
}
```

In `main.cpp` we use a big chain of `if / else if` to figure out which mode is currently active and which function to call.

## 7. `switch` Statements — Decisions with Lots of Options

A `switch` is a cleaner way to write a long `if / else if` chain when you're comparing one variable against a bunch of specific values:

```cpp
switch (mode) {
  case MODE_WELCOME:   return "WELCOME";
  case MODE_CHORD_JAM: return "CHORD JAM";
  default:             return "UNKNOWN";
}
```

Each `case` is one of the possible values. `default` is the fallback for anything else. We use this in `getModeString()` to translate the current mode into a piece of text for the OLED screen.

## 8. References (`&`) — Giving Something a Nickname

When you write a type with an `&` after it, you're making a **reference**, which is like a nickname for a variable that already exists. Anything you do to the nickname happens to the original.

```cpp
void readDistanceGrid(uint8_t channel) {
  TwoWire& bus = (channel == 0) ? Wire : Wire1;  // bus is a nickname for Wire or Wire1
  // ...
}
```

We use this so we don't have to copy huge things around in memory; we just pass a reference.

## 9. Namespaces and the `::` Operator

A **namespace** is like a folder for code. It keeps names from bumping into each other when two libraries happen to use the same word. To use something from a namespace, you write `Namespace::name`:

```cpp
SynthVoices::begin(masterVolume);   // call begin() from the SynthVoices namespace
Welcome::process(currentTime);      // call process() from the Welcome namespace
```

In AURA each mode has its own namespace (`Welcome`, `ChordJam`, `DualLoop`, etc.) so we can have a `process()` function in every single one without them clashing.

## 10. Objects and the Dot Operator

Some libraries give you **objects**, which are like little bundles of data and functions glued together. You use the dot `.` to reach inside one:

```cpp
Serial.println("Hello!");      // println() lives inside Serial
FastLED.setBrightness(50);     // setBrightness() lives inside FastLED
snareDrum.length(150);         // length() lives inside the snareDrum object
```

You'll see lots of dots in `main.cpp` because every Arduino library hands you an object.

## 11. Enums — A Named List of Choices

An **enum** (short for enumeration) is a list of named values, useful when something can only be one of a few specific things. In AURA:

```cpp
enum PlayMode {
  MODE_WELCOME,
  MODE_BACKSTAGE,
  MODE_CHORD_JAM,
  MODE_DUAL_LOOP,
  MODE_BASS_MACHINE,
  MODE_BATTLE_MODE
};
```

Now anywhere in the code we can say `MODE_CHORD_JAM` instead of remembering some random number. Under the hood enums actually *are* numbers (0, 1, 2, 3, …) which is why we can do math on them, like `(int)currentMode + 1` to "go to the next mode."


## 12. `static` Variables Inside Functions

A normal variable inside a function gets created fresh every time the function runs and disappears when the function ends. A `static` variable is different. It gets created **once** and keeps its value between calls:

```cpp
static bool isSwitchingMode = false;
```

In `main.cpp` we use `static` so the variable remembers whether a mode switch is already in progress, even after the function returns.

## 13. Type Casts — Forcing One Type Into Another

A cast is when you tell C++ "treat this value as if it were a different type." You put the new type in parentheses in front:

```cpp
int nextMode = (int)currentMode + 1;     // treat the enum as an int so we can add 1
switchToMode((PlayMode)nextMode);        // turn the int back into a PlayMode
```

We use casts in the encoder code to do math on `PlayMode` (which is an enum) and then change it back when we're done.

## 14. Arduino-Specific Stuff

Arduino (and Teensy, which is Arduino-compatible) programs always have these two special functions:

- `void setup()` runs **once** when the board boots up. Use it to set things up: turn on the screen, start the audio, wake up the sensors.
- `void loop()` runs **over and over forever** after `setup()` finishes. This is where the action happens.

A few handy Arduino tools we use:

- `Serial.println("Hello")` — print a message to your computer over USB (great for debugging).
- `delay(150)` — pause the program for 150 milliseconds (0.15 seconds).
- `millis()` — returns how many milliseconds have passed since the board turned on. We use this to time things without freezing the program with `delay()`.

---

# Part 2: Walking Through `main.cpp`

Now let's go through the actual file, top to bottom.

## Block 1: The Header Comment

```cpp
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
```

This is just a big comment at the top of the file. It tells anyone reading the code:

1. What this project is called (AURA).
2. What hardware is hooked up to the Teensy.
3. What modes exist and what they do.

The computer ignores this whole block when it runs the code, but it's super helpful for humans.

## Block 2: Including the Libraries

```cpp
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
```

The first group (with `< >`) pulls in libraries that don't belong to us:

- `Arduino.h` — basic Arduino stuff like `Serial`, `delay`, `millis`.
- `Audio.h` — the Teensy Audio library that makes sound.
- `Wire.h` and `Wire1` — for talking to the distance sensors over a wire protocol called I²C.
- `SPI.h`, `SD.h`, `SerialFlash.h` — extra communication stuff the audio library needs.
- `FastLED.h` — controls the colorful LED matrices.

The second group (with `" "`) pulls in files **we wrote ourselves**, sitting inside the project:

- `Config.h` — constants and pin numbers (so we don't have magic numbers scattered everywhere).
- `AuraState.h` — global variables shared between files.
- `MusicNotes.h` — note names and frequencies.
- `LedMatrix.h`, `OledStatus.h`, `RotaryUI.h`, `ToFGrid.h`, `SynthVoices.h` — our helper libraries for each piece of hardware.

The third group is the **mode files**. Each mode (Welcome, Backstage, Chord Jam, Dual Loop, Bass, Battle) lives in its own file so the code stays organized.


## Block 3: Global Variables

```cpp
PlayMode currentMode = MODE_WELCOME;
float masterVolume = 1.0;

bool sensor_ch0_initialized = false;
bool sensor_ch1_initialized = false;
uint16_t distanceGrid_ch0[8][8];
uint16_t distanceGrid_ch1[8][8];
unsigned long lastSensorRead = 0;

CRGB leds[NUM_LEDS];     // left one
CRGB leds_r[NUM_LEDS];   // right one

bool ledVisualizationEnabled = true;
bool noteActive = false;

unsigned long lastUpdateTime = 0;
```

These variables live **outside** any function, which makes them **global**. That means every function in the file can see them and change them.

- `currentMode` — which play mode is active right now. Starts as `MODE_WELCOME`.
- `masterVolume` — overall loudness, 1.0 means full volume.
- `sensor_ch0_initialized` / `sensor_ch1_initialized` — `true` once each distance sensor has woken up successfully.
- `distanceGrid_ch0` / `distanceGrid_ch1` — two 8×8 grids that hold the latest distance reading from each sensor. So `distanceGrid_ch0[3][5]` would be the distance reading at row 3, column 5 of the left sensor.
- `leds` / `leds_r` — arrays that hold the color of every LED on the left and right matrices. `CRGB` is a color type from FastLED (red, green, blue).
- `ledVisualizationEnabled` — a toggle. If you set it to `false`, the LED panels go dark.
- `noteActive` — tracks if a sound is still ringing so we know when to shut it off.
- `lastUpdateTime` — remembers the last time we updated, so we can space things out evenly using `millis()`.

## Block 4: Forward Declarations

```cpp
void setupLEDs();
void clearAllLEDs();
void setupOLED();
void showLoadingScreen(const char* label, int step, int total);
void updateOLEDDisplay();
void handleEncoder();
void handleEncoderButton();
void switchToMode(PlayMode newMode);
const char* getModeString(PlayMode mode);
```

These are **promises** to the compiler. The actual code for these functions is written further down in the file, but `setup()` and `loop()` use them earlier. The forward declarations basically say "trust me, these exist, the real code is coming."

Without these, the compiler would freak out the moment it saw `setupLEDs()` being called before it had been defined.

## Block 5: `setup()` — Booting Everything Up

```cpp
void setup() {
  Serial.begin(115200);

  delay(1000);  // give serial a sec

  Serial.println("\n=== Teensy Touchless Instrument (Simulated ToF) ===");

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

  showLoadingScreen("Sensor CH0", 5, 6);
  sensor_ch0_initialized = initDistanceSensor(0);

  showLoadingScreen("Sensor CH1", 6, 6);
  sensor_ch1_initialized = initDistanceSensor(1);

  OledStatus::showSplash("Ready!");
  delay(500);

  lastUpdateTime = millis();
  updateOLEDDisplay();
}
```

`setup()` runs **once** when the Teensy turns on. It walks through each piece of hardware in order and wakes it up, showing a tiny loading bar on the OLED screen the whole time.

Step by step:

1. **Start serial** — `Serial.begin(115200)` opens a connection over USB so we can `Serial.println()` debug messages from our computer.
2. **Wait a second** — gives the serial port time to actually be ready before we print to it.
3. **Print a banner** — just a "hello, I'm awake" message.
4. **Boot the audio** — `SynthVoices::begin(masterVolume)` starts the Teensy Audio system and sets the volume.
5. **Boot the OLED screen** — `setupOLED()` wakes up the tiny screen so we can show progress.
6. **Show loading steps** — `showLoadingScreen("Audio", 1, 6)` draws "step 1 of 6: Audio". We do this for all 6 steps so the user knows AURA is alive and working.
7. **Boot the LEDs** — sets up both LED matrices.
8. **Boot the encoder** — wakes up the knob (and its button) so we can detect turns and presses.
9. **Boot sensor CH0 and CH1** — wakes up each distance sensor. These go last because they take the longest. The result (success or failure) gets stored in the `sensor_chX_initialized` flags.
10. **Show "Ready!"** — a quick splash so the user knows boot is done.
11. **Save the current time** — `lastUpdateTime = millis()` so the loop knows when to next update.
12. **Show the regular display** — switches the OLED over to "WELCOME / Turn to switch modes".


## Block 6: `loop()` — The Heartbeat of the Program

```cpp
void loop() {
  handleEncoder();
  handleEncoderButton();

  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL_MS) {
    lastUpdateTime = currentTime;

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
```

`loop()` runs **over and over forever** after `setup()` finishes. It's the heartbeat of the whole instrument.

Here's what happens on every tick:

1. **Check the knob.** `handleEncoder()` looks at whether the knob was turned. `handleEncoderButton()` looks at whether it was pressed.
2. **Get the current time.** `millis()` returns how many milliseconds since boot.
3. **Have we waited long enough?** `if (currentTime - lastUpdateTime >= UPDATE_INTERVAL_MS)` checks whether enough time has gone by since the last update. If it has, we update. If not, we skip and do nothing this round. This keeps the program from updating too fast.
4. **Remember the new time** so we can compare again next round.
5. **Call the right mode's `process()` function.** This is the big chain of `if / else if`. Whichever mode is active gets its `process()` function called. That function reads the sensors, plays sounds, and updates the LEDs.

Notice how `main.cpp` doesn't actually contain any music-making code itself. It just figures out which mode is active and asks **that mode's file** to do the work. This is what makes the project easy to grow — adding a new mode just means writing a new file and adding one more `else if` here.

## Block 7: `panicMute()` — The Emergency Stop

```cpp
void panicMute() {
  SynthVoices::panic();
  noteActive = false;
}
```

If a note is still ringing when we switch modes, it could keep playing in the background and sound terrible. `panicMute()` is our "kill all sound right now" button. It tells the `SynthVoices` library to stop everything, and it flips `noteActive` back to `false` so we remember nothing is playing.

## Block 8: Distance Sensor Helpers

```cpp
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
```

These are two helper functions for talking to the distance sensors.

**`initDistanceSensor(channel)`** wakes up one sensor:

- It takes a channel number (0 = left, 1 = right).
- The line `TwoWire& bus = (channel == 0) ? Wire : Wire1;` uses a **ternary operator** (`condition ? a : b`). It's a one-line `if/else`: if `channel == 0` then `bus = Wire`, otherwise `bus = Wire1`. Both sensors have the same address from the factory, so we put them on two separate I²C wires so they don't bump into each other.
- `ToFGrid::begin(channel, bus)` does the actual hardware setup and returns `true` if it worked.
- We print a success or error message, then return that `true`/`false` so the rest of the code knows whether the sensor is working.

**`readDistanceGrid(channel)`** grabs a fresh frame of distances from one sensor:

- That weird `uint16_t (*grid)[8]` is a **pointer to a row of 8 `uint16_t` values**. It's basically saying "let `grid` point at one of the 8×8 grids."
- The ternary picks the correct grid based on the channel.
- `ToFGrid::readFrame(channel, grid)` fills the grid up with the latest sensor data.

## Block 9: LED Helpers

```cpp
void setupLEDs() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_R, COLOR_ORDER>(leds_r, NUM_LEDS);
  FastLED.setBrightness(50);
  clearAllLEDs();
}

void clearAllLEDs() {
  LedMatrix::clearMatrix(leds,   NUM_LEDS);
  LedMatrix::clearMatrix(leds_r, NUM_LEDS);
  FastLED.show();
}
```

**`setupLEDs()`** tells FastLED about both matrices. It says "here's the left matrix, it's on pin `LED_PIN`, it has `NUM_LEDS` lights, the data is in the `leds` array." Then the same for the right matrix. We set brightness to 50 (out of 255) so the LEDs aren't blinding, and then blank everything out.

**`clearAllLEDs()`** turns off every LED on both matrices. `LedMatrix::clearMatrix` zeroes out the color array, and `FastLED.show()` actually pushes that update out to the physical LEDs. Until you call `show()`, the changes only live in memory.

## Block 10: OLED Screen Helpers

```cpp
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
```

Three little helpers that wrap our `OledStatus` library so `setup()` and the other functions don't have to repeat the same lines.

- **`setupOLED()`** starts the screen at its I²C address (`OLED_ADDR`). If it fails, it prints an error.
- **`showLoadingScreen()`** draws "Step X of Y: label" during boot.
- **`updateOLEDDisplay()`** shows the regular two-line status: the mode name on top, and "Turn to switch modes" underneath.


## Block 11: `getModeString()` — Turn a Mode Into Text

```cpp
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
```

The `currentMode` variable is a `PlayMode` enum, which is really just a number (0, 1, 2…). But on the OLED screen we want to show a word like "CHORD JAM", not the number `2`. This function does the translation using a `switch` statement.

The return type `const char*` is a string of text. The `const` means "don't change this string," because it's just sitting in the program's memory as a label.

## Block 12: `switchToMode()` — Changing Modes Safely

```cpp
static bool isSwitchingMode = false;

void switchToMode(PlayMode newMode) {
  if (isSwitchingMode) {
    Serial.println(">> Mode switch already in progress, ignoring...");
    return;
  }

  isSwitchingMode = true;
  currentMode = newMode;

  snareDrum.length(150);
  snareDrum.secondMix(0.5);
  snareDrum.pitchMod(0.3);

  if (currentMode == MODE_BACKSTAGE) {
    if (!sensor_ch0_initialized) sensor_ch0_initialized = initDistanceSensor(0);
    if (!sensor_ch1_initialized) sensor_ch1_initialized = initDistanceSensor(1);
    panicMute();
    clearAllLEDs();
    FastLED.show();
  } else if (currentMode == MODE_WELCOME) {
    panicMute();
    clearAllLEDs();
    FastLED.show();
  } else if (currentMode == MODE_CHORD_JAM) {
    ChordJam::enter();
    if (sensor_ch0_initialized) readDistanceGrid(0);
    if (sensor_ch1_initialized) readDistanceGrid(1);
  }
  // ... (similar blocks for DUAL_LOOP, BASS_MACHINE, BATTLE_MODE) ...

  delay(100);

  panicMute();
  updateOLEDDisplay();

  isSwitchingMode = false;
}
```

This function is what runs every time you turn the knob. It handles changing from one mode to another safely. Here's the play-by-play:

1. **Re-entry guard.** `isSwitchingMode` is a `static bool` (so it remembers its value between calls). If someone is already switching modes and you turn the knob again super fast, we bail out so two switches don't happen on top of each other.
2. **Flip the guard.** Set `isSwitchingMode = true` and update `currentMode = newMode` so the rest of the program knows what's active now.
3. **Reset the snare drum.** Some modes change the snare's pitch and length (Battle mode messes with it). We reset it back to defaults so the next mode starts clean.
4. **Run the mode's setup.** Big `if / else if` chain again, this time calling each mode's `enter()` function. `enter()` is each mode's "wake up" routine. Modes that use the sensors also pull in a first frame of data so they have something to work with.
5. **Pause for 100 ms** so the hardware can settle.
6. **Panic mute again.** Just to make sure no audio is leaking from the previous mode.
7. **Refresh the OLED** to show the new mode's name.
8. **Release the guard.** `isSwitchingMode = false` so future switches can happen.

## Block 13: `handleEncoder()` — Turning the Knob to Switch Modes

```cpp
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
```

Every time `loop()` runs, this function asks `RotaryUI` "hey, has the knob moved?"

- `pollRotation()` returns `0` if nothing happened, a negative number if the knob turned one way, or a positive number if it turned the other way. (A "detent" is one click of the knob.)
- If it's `0`, we leave the function with `return`.
- If it's **negative**, go to the next mode. We cast `currentMode` to `int` so we can add 1 to it, then if that goes past `MODE_LAST` we wrap around to `MODE_FIRST` (so it loops back to WELCOME). Then we cast back to `PlayMode` and call `switchToMode()`.
- If it's **positive**, do the same thing but go backward, wrapping around at the other end.

That's why turning the knob just keeps cycling through modes forever in either direction.

## Block 14: `handleEncoderButton()` — Pressing the Knob to Clear

```cpp
void handleEncoderButton() {
  if (!RotaryUI::pollButtonPressed()) return;

  if (currentMode == MODE_DUAL_LOOP) {
    DualLoop::clear();
  } else if (currentMode == MODE_CHORD_JAM) {
    ChordJam::clear();
  } else if (currentMode == MODE_BASS_MACHINE) {
    BassMachine::clear();
  } else if (currentMode == MODE_BATTLE_MODE) {
    Battle::clear();
  } else {
    updateOLEDDisplay();
    return;
  }

  panicMute();
  clearAllLEDs();
  FastLED.show();
  updateOLEDDisplay();
}
```

The knob you turn to switch modes is also a button — you can press it down like a clicky pen. This function handles those presses.

- `pollButtonPressed()` returns `true` if the button was just pressed. If not, we `return`.
- Each mode handles "clear" differently. In **Dual Loop** it wipes the recorded melody and drums. In **Chord Jam** it resets the chord. In **Bass** it clears the pattern. In **Battle** it resets the score.
- For modes that don't have anything to clear (Welcome and Backstage), we just refresh the OLED screen and bail out.
- After clearing, we panic mute the sound, blank out the LEDs, and refresh the display so the user sees a clean slate.

---

# Quick Summary: How It All Fits Together

If you zoom way out, the whole `main.cpp` is doing this:

1. **`setup()` runs once** — wake up audio, screen, LEDs, knob, sensors. Show a loading bar so the user knows it's booting.
2. **`loop()` runs forever** — check the knob, then ask the current mode to do its thing.
3. **Turn the knob** → `handleEncoder()` → `switchToMode()` → the new mode's `enter()` runs.
4. **Press the knob** → `handleEncoderButton()` → the current mode's `clear()` runs.
5. **All actual music + animations** live inside the **mode files** (`Welcome.cpp`, `ChordJam.cpp`, `DualLoop.cpp`, etc.) and the **library files** in `lib/` (`SynthVoices`, `LedMatrix`, `OledStatus`, `RotaryUI`, `ToFGrid`).

`main.cpp` is basically the **conductor** of the orchestra — it doesn't play any instruments itself, it just decides who plays when.

---

# Glossary

- **Compiler** — the program that translates C++ into the 1s and 0s the Teensy actually runs.
- **Library** — a chunk of pre-written code you can use without rewriting it yourself.
- **Header file (`.h`)** — a "table of contents" file that says what's available, used by `#include`.
- **Source file (`.cpp`)** — the actual code that does stuff.
- **I²C** — a way for chips on a circuit board to talk to each other over two wires.
- **Pin** — one of the physical metal legs on the Teensy that connects to a piece of hardware.
- **Detent** — one click of the rotary knob.
- **FastLED** — the library that controls the NeoPixel LED matrices.
- **Teensy Audio Library** — the library that handles all sound generation and routing.

That's it! If you can follow `main.cpp`, you can pretty much understand the whole AURA project, since everything else is just one of those mode files or libraries we've already pointed at.
