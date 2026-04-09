# Touchless Instrument - Simulated ToF Version

## Overview
This firmware simulates a touchless instrument using two virtual Time-of-Flight (ToF) sensors controlled via serial commands. It demonstrates two play modes:
1. **STRING THEREMIN** - Melodic mode with pitch quantization and expression
2. **GESTURE DRUM** - Rhythmic mode with zone-based drum triggering

## Hardware Setup
- **Teensy 4.0 or 4.1** with SGTL5000 Audio Shield
- **Headphones/speakers** connected to Audio Shield
- **USB cable** for programming and serial control

## Quick Start

### 1. Upload Firmware
```bash
# Build and upload using PlatformIO
pio run --target upload

# Or use VS Code PlatformIO buttons
# Click ✓ to build, → to upload
```

### 2. Open Serial Monitor
```bash
# Using PlatformIO CLI
pio device monitor

# Or use VS Code PlatformIO button (🔌)
# Baud rate: 115200
```

### 3. Test the Instrument

**First, see the help menu:**
```
?
```

**Try STRING THEREMIN mode (default):**
```
r          # Move pitch hand to Zone B (melody)
f          # Fast jab to pluck the string
q          # Move pitch hand closer (lower pitch)
a          # Move pitch hand farther (higher pitch)
w          # Move expr hand closer (louder)
s          # Move expr hand farther (quieter)
v          # Toggle vibrato on/off
```

**Switch to DRUM mode:**
```
M          # Toggle to GESTURE DRUM mode
e          # Move to Zone A (near)
f          # Fast jab -> triggers KICK
r          # Move to Zone B (mid)
f          # Fast jab -> triggers SNARE
t          # Move to Zone C (far)
f          # Fast jab -> triggers HI-HAT
g          # Gentle tap (lower velocity)
```

**Try different scales (STRING mode):**
```
M          # Switch back to STRING THEREMIN
1          # Pentatonic scale
2          # Major scale
3          # Minor scale
```

## Command Reference

### Mode Control
- `M` - Toggle between STRING THEREMIN and GESTURE DRUM modes
- `x` - Panic mute (stop all sounds)
- `?` - Show help menu with current state

### Simulated ToF Controls

**Pitch Hand (determines note/zone):**
- `q` - Move closer (-20mm)
- `a` - Move farther (+20mm)
- `e` - Jump to Zone A (150mm, near/bass)
- `r` - Jump to Zone B (350mm, mid/melody)
- `t` - Jump to Zone C (650mm, far/harmonics)

**Expression Hand (volume/brightness):**
- `w` - Move closer (-20mm, louder)
- `s` - Move farther (+20mm, quieter)

### Gesture Events
- `f` - Fast jab (high velocity trigger/pluck)
- `g` - Gentle tap (low velocity trigger/pluck)
- `v` - Toggle vibrato (STRING mode only)

### Scale Selection (STRING mode)
- `1` - Pentatonic scale (5 notes)
- `2` - Major scale (7 notes)
- `3` - Minor scale (7 notes)

### Settings
- `m` - Toggle mute-if-idle mode

## How It Works

### STRING THEREMIN Mode

**Pitch Mapping:**
- Distance is mapped to discrete notes in the selected scale
- Three zones with different octave ranges:
  - **Zone A (80-250mm)**: Bass register (A2-A3)
  - **Zone B (250-500mm)**: Melody register (A3-A4)
  - **Zone C (500-800mm)**: High register (A4-A5)

**Expression:**
- Expression hand controls volume
- Closer = louder, farther = quieter

**Timbre:**
- Each zone has different filter settings:
  - Zone A: Dark, warm (1200 Hz cutoff)
  - Zone B: Balanced (2000 Hz cutoff)
  - Zone C: Bright, resonant (3500 Hz cutoff)

**Vibrato:**
- Toggle with `v` key
- Adds ±2% pitch modulation at 5 Hz

### GESTURE DRUM Mode

**Zone-Based Triggering:**
- Zone A jab → **Kick drum**
- Zone B jab → **Snare drum**
- Zone C jab → **Hi-hat**

**Velocity:**
- `f` (fast jab) = high velocity
- `g` (gentle tap) = lower velocity

## Technical Details

### Audio Architecture
```
String Voice: KarplusStrong → Envelope → Filter → Mixer Ch0
Kick Drum:    SimpleDrum → Mixer Ch1
Snare Drum:   SimpleDrum → Mixer Ch2
Hi-hat:       Noise → Envelope → Mixer Ch3
Mixer → I2S L/R Output
```

### Parameters
- **Sample Rate**: 44.1 kHz
- **Audio Memory**: 20 blocks
- **Update Rate**: 50 Hz (20ms intervals)
- **Smoothing**: Exponential moving average (α=0.3)
- **Distance Range**: 80-800mm
- **Presence Threshold**: 780mm (above = no presence)

### Scales
- **Pentatonic**: 0, 2, 4, 7, 9 semitones
- **Major**: 0, 2, 4, 5, 7, 9, 11 semitones
- **Minor**: 0, 2, 3, 5, 7, 8, 10 semitones

## Code Structure

The code is organized for easy integration of real ToF sensors:

```cpp
// Simulated sensor values (replace with real sensor readings)
int pitchHandRaw = 350;  // mm
int exprHandRaw = 400;   // mm

// Smoothed values (already implemented)
float pitchHandSmooth = 350.0;
float exprHandSmooth = 400.0;
```

**To add real ToF sensors later:**
1. Include VL53L0X/VL53L1X library
2. Replace `pitchHandRaw` and `exprHandRaw` with sensor readings
3. Keep all the smoothing and processing logic unchanged

## Troubleshooting

**No sound:**
- Check Audio Shield is properly connected
- Verify headphones are plugged into Audio Shield
- Try panic mute (`x`) then trigger a sound

**Choppy/glitchy audio:**
- Increase AudioMemory if needed
- Check CPU usage with `AudioProcessorUsageMax()`

**Notes not changing:**
- Make sure you're in STRING THEREMIN mode (`M` to toggle)
- Try moving pitch hand with `q`/`a` keys
- Check current state with `?`

## Next Steps

This firmware is ready for real ToF sensor integration:
- Add VL53L0X or VL53L1X libraries
- Connect sensors via I2C
- Replace simulated values with sensor readings
- Add gesture detection algorithms (swipe, tap, etc.)

Enjoy experimenting with your touchless instrument! 🎵

