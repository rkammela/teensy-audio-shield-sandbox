/*
 * AuraState.h - shared project-wide state.
 *
 * Anything that lives in src/main.cpp but is also touched by a mode file
 * gets `extern`-declared here. main.cpp owns the definitions; mode files
 * include this header to see the same names.
 *
 * Per-mode private state (step counters, touch latches, etc.) does NOT
 * belong here - it lives at file scope inside the matching src/modes/*.cpp.
 */

#ifndef AURA_STATE_H
#define AURA_STATE_H

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"

// Currently active play mode + the master volume (applied to mixer gains
// every time a voice triggers).
extern PlayMode currentMode;
extern float    masterVolume;

// True while a Karplus-Strong string voice is sustaining. panicMute()
// reads this to know whether it needs to release the envelope.
extern bool noteActive;

// VL53L5CX initialization flags + per-frame distance grids. The library
// (lib/ToFGrid) owns the sensors; main.cpp owns these buffers.
extern bool     sensor_ch0_initialized;
extern bool     sensor_ch1_initialized;
extern uint16_t distanceGrid_ch0[8][8];
extern uint16_t distanceGrid_ch1[8][8];
extern unsigned long lastSensorRead;

// LED matrix frame buffers. Serpentine pixel mapping lives in lib/LedMatrix.
extern CRGB leds[NUM_LEDS];
extern CRGB leds_r[NUM_LEDS];

// Global "are the matrices allowed to light up?" gate. Each mode honours
// this so we can blank the panels for stage demos.
extern bool ledVisualizationEnabled;

// Helpers defined in main.cpp that mode files need to call.
void panicMute();
void clearAllLEDs();
bool initDistanceSensor(uint8_t channel);
void readDistanceGrid(uint8_t channel);

#endif // AURA_STATE_H
