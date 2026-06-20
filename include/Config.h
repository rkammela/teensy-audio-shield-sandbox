/*
 * Config.h - AURA project-wide constants
 *
 * Shared definitions used by main.cpp and by the extracted libraries
 * (ToFGrid, LedMatrix, OledStatus, RotaryUI, MusicNotes, SynthVoices).
 *
 * Keep this header lightweight: enums, pin numbers, sizes, and timing
 * constants only. Mode-specific runtime state stays in its owning module.
 */

#ifndef AURA_CONFIG_H
#define AURA_CONFIG_H

// ============================================================================
// PLAY MODE
// ============================================================================

// The 6 performance modes the encoder cycles through. The enum order is
// also the menu order, and the wrap-around is defined by MODE_FIRST and
// MODE_LAST below.
enum PlayMode {
  MODE_WELCOME,        // Default boot screen: AURA + pulsing heart, TSA scroll
  MODE_BACKSTAGE,      // Diagnostic: live 8x8 sensor grids on both LED matrices
  MODE_CHORD_JAM,      // Guitar-style: left hand picks chord, right hand strums
  MODE_DUAL_LOOP,      // Layered looper: left = melody grid, right = drum grid
  MODE_BASS_MACHINE,   // 8-step bass line + right-hand filter sweep
  MODE_BATTLE_MODE     // Two-player duel: P1 = CH0, P2 = CH1
};

#define MODE_FIRST  MODE_WELCOME
#define MODE_LAST   MODE_BATTLE_MODE

// ============================================================================
// TIMING
// ============================================================================

// Main loop tick rate. Sensors + modes update at this cadence.
const unsigned long UPDATE_INTERVAL_MS  = 20;   // 50 Hz

// VL53L5CX sensor frame poll interval.
const unsigned long SENSOR_READ_INTERVAL = 100; // ms

// Rotary encoder button debounce window.
const unsigned long DEBOUNCE_DELAY = 50;        // ms

// ============================================================================
// SENSOR GRID
// ============================================================================

// Shared touch threshold: a grid cell is considered "touched" when its
// distance is between 50 mm and this value. Used by every grid-based mode.
const int GRID_ZONE_THRESHOLD = 400;  // mm

// ============================================================================
// LED MATRIX
// ============================================================================

#define LED_PIN       0     // Data pin for LEFT LED matrix
#define LED_PIN_R     1     // Data pin for RIGHT LED matrix
#define NUM_LEDS      64    // Number of LEDs per matrix (64 for 8x8)
#define MATRIX_WIDTH  8     // Matrix width (columns)
#define MATRIX_HEIGHT 8     // Matrix height (rows)
#define LED_TYPE      WS2812B   // LED type (WS2812/NeoPixel)
#define COLOR_ORDER   GRB       // Color order for WS2812B

// ============================================================================
// OLED DISPLAY
// ============================================================================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1     // No reset pin (shares I2C bus with VL53L5CX CH0)
#define OLED_ADDR     0x3C   // I2C address

// ============================================================================
// ROTARY ENCODER
// ============================================================================

#define ENCODER_PIN_A   2    // CLK
#define ENCODER_PIN_B   3    // DT
#define ENCODER_BUTTON  4    // SW (push button)

#endif // AURA_CONFIG_H
