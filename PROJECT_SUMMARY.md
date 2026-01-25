# Teensy Audio Shield Instrument Sandbox - Project Summary

## Overview
This is a complete PlatformIO project for Teensy 4.0/4.1 with the SGTL5000 Audio Shield that implements a multi-instrument sound generator with serial command control.

## Files Created/Modified

### 1. `platformio.ini`
Complete PlatformIO configuration with:
- Teensy 4.0 platform and board settings
- Arduino framework
- Serial monitor at 115200 baud
- Teensy Audio Library and dependencies (Audio, Wire, SPI, SD, SerialFlash)
- Optimized build flags

### 2. `src/main.cpp` (374 lines)
Full-featured instrument sandbox firmware with:

**Audio Objects:**
- 2x AudioSynthSimpleDrum (kick, snare)
- AudioSynthNoiseWhite + AudioEffectEnvelope (hi-hat)
- AudioSynthKarplusStrong (plucked string)
- 2x AudioSynthWaveform + AudioEffectEnvelope + AudioFilterStateVariable (lead & bass synths)
- AudioMixer4 (4-channel mixer)
- AudioOutputI2S (stereo output)
- AudioControlSGTL5000 (codec control)

**Features:**
- Serial command interface (115200 baud)
- Help menu with '?' command
- Drum triggers: k, s, h
- Pluck triggers: p, 1, 2, 3 (with pitch selection)
- Synth triggers: l (lead), b (bass)
- Mute toggle: m (placeholder)
- Proper audio routing with anti-clipping mixer gains
- Well-commented code with setup functions for each instrument type

**Audio Graph:**
```
Kick Drum ──────────────────────────┐
Snare Drum ─────────────────────────┤
Noise → Envelope (Hi-hat) ──────────┤
Pluck String ───────────────────────┼──→ Mixer ──→ I2S L/R Output
Lead: Waveform → Envelope → Filter ─┤
Bass: Waveform → Envelope → Filter ─┘
```

### 3. `BUILD_INSTRUCTIONS.md`
Comprehensive guide covering:
- Hardware requirements and setup
- Building with VS Code GUI or CLI
- Uploading to Teensy
- Serial monitor usage
- Command reference
- Troubleshooting tips
- Performance notes

## Key Technical Details

### Audio Configuration
- **AudioMemory**: 20 blocks (128 samples each)
- **Sample Rate**: 44.1 kHz (Teensy Audio Library default)
- **Bit Depth**: 16-bit
- **Output**: I2S to SGTL5000 DAC
- **Volume**: 0.5 (50% of max)

### Mixer Gains (Anti-clipping)
- Channel 0 (Kick/Lead): 0.4
- Channel 1 (Snare/Bass): 0.4
- Channel 2 (Hi-hat): 0.3
- Channel 3 (Pluck): 0.5

### Instrument Parameters

**Kick Drum:**
- Frequency: 60 Hz
- Length: 300 ms
- Pitch modulation: 0.55

**Snare Drum:**
- Frequency: 200 Hz
- Length: 150 ms
- Second mix: 0.5 (adds noise character)

**Hi-hat:**
- White noise with fast envelope
- Attack: 1 ms, Decay: 50 ms

**Pluck (Karplus-Strong):**
- Selectable frequencies: 220 Hz (A3), 329.63 Hz (E4), 440 Hz (A4)
- Velocity: 0.8

**Lead Synth:**
- Sawtooth wave at 440 Hz (A4)
- Attack: 5 ms, Decay: 200 ms, Sustain: 0.3, Release: 300 ms
- Lowpass filter: 2000 Hz cutoff, 1.2 resonance

**Bass Synth:**
- Square wave at 110 Hz (A2)
- Attack: 10 ms, Decay: 300 ms, Sustain: 0.5, Release: 400 ms
- Lowpass filter: 800 Hz cutoff, 1.5 resonance

## Build & Upload Quick Reference

```bash
# Build
pio run

# Upload
pio run --target upload

# Monitor
pio device monitor
```

Or use VS Code PlatformIO toolbar icons.

## Testing the Firmware

1. Upload to Teensy
2. Open serial monitor (115200 baud)
3. Press '?' to see help menu
4. Try commands: k, s, h, p, l, b
5. Adjust pluck pitch with 1, 2, 3
6. Listen to audio output on headphones/speakers

## Future Enhancements

This firmware is ready for:
- ToF sensor integration for gesture control
- MIDI input/output
- Sequencer/arpeggiator
- More synthesis types (FM, wavetable)
- Audio effects (reverb, delay, chorus, distortion)
- Parameter control via analog inputs
- SD card sample playback
- Real-time parameter adjustment via serial

## Dependencies

All dependencies are automatically managed by PlatformIO:
- Teensy Audio Library (Paul Stoffregen)
- Wire (I2C)
- SPI
- SD
- SerialFlash

No manual Arduino IDE library installation required!

