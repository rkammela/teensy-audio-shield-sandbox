# Teensy Audio Shield Instrument Sandbox

A PlatformIO-based firmware for Teensy 4.0/4.1 with the SGTL5000 Audio Shield that generates multiple instrument-like sounds with serial command control.

## 🎵 Features

- **Drum Sounds**: Kick, snare, and hi-hat
- **Plucked String**: Karplus-Strong algorithm with adjustable pitch
- **Synthesizers**: Lead (sawtooth) and bass (square wave) with filters and envelopes
- **Real-time Volume Control**: Adjust output volume on-the-fly
- **Serial Command Interface**: Control all sounds via USB serial (115200 baud)
- **Clean Audio**: Proper gain staging and envelopes prevent clipping and pops

## 🔧 Hardware Requirements

- **Teensy 4.0 or 4.1** microcontroller
- **Teensy Audio Shield** (Rev D with SGTL5000 codec)
- **USB cable** for programming and power
- **Headphones or powered speakers** connected to Audio Shield's 3.5mm jack

## 🚀 Quick Start

1. **Clone this repository**
   ```bash
   git clone <your-repo-url>
   cd "Teensy Test"
   ```

2. **Open in VS Code with PlatformIO**
   - Install PlatformIO extension if not already installed
   - Open this folder in VS Code

3. **Build and Upload**
   - Click the checkmark (✓) to build
   - Click the arrow (→) to upload
   - Click the plug (🔌) to open serial monitor

4. **Test the sounds**
   - Type `?` in serial monitor to see all commands
   - Try `k` for kick, `s` for snare, `p` for pluck, etc.

See [QUICKSTART.md](QUICKSTART.md) for detailed instructions.

## 🎹 Command Reference

### Drums
- `k` - Kick drum
- `s` - Snare drum
- `h` - Hi-hat

### Plucked String
- `p` - Trigger pluck at current pitch
- `1` - Set pluck to A3 (220 Hz)
- `2` - Set pluck to E4 (329.63 Hz)
- `3` - Set pluck to A4 (440 Hz)

### Synthesizers
- `l` - Synth lead note (sawtooth + filter)
- `b` - Bass note (square wave + filter)

### Settings
- `+` - Increase volume by 5%
- `-` - Decrease volume by 5%
- `m` - Toggle mute-if-idle mode
- `?` - Show help menu

## 📁 Project Structure

```
.
├── src/
│   └── main.cpp              # Main firmware (423 lines)
├── include/                  # Header files (if needed)
├── lib/                      # Custom libraries (if needed)
├── platformio.ini            # PlatformIO configuration
├── BUILD_INSTRUCTIONS.md     # Detailed build guide
├── QUICKSTART.md            # Quick start guide
└── README.md                # This file
```

## 🎛️ Audio Architecture

```
Kick Drum ──────────────────────────┐
Snare Drum ─────────────────────────┤
Noise → Envelope (Hi-hat) ──────────┤
Pluck String ───────────────────────┼──→ Mixer ──→ I2S L/R Output
Lead: Waveform → Envelope → Filter ─┤
Bass: Waveform → Envelope → Filter ─┘
```

## 🔊 Technical Details

- **Platform**: Teensy 4.0/4.1 (ARM Cortex-M7)
- **Framework**: Arduino
- **Audio Library**: Teensy Audio Library by Paul Stoffregen
- **Sample Rate**: 44.1 kHz
- **Bit Depth**: 16-bit
- **Audio Memory**: 20 blocks (128 samples each)
- **Default Volume**: 50% (adjustable with +/-)

## 📚 Documentation

- [QUICKSTART.md](QUICKSTART.md) - Get started in 3 minutes
- [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) - Comprehensive build and troubleshooting guide

## 🛠️ Dependencies

All dependencies are automatically managed by PlatformIO:
- Teensy Audio Library
- Wire (I2C)
- SPI
- SD
- SerialFlash

## 🚧 Future Enhancements

This firmware is a foundation for:
- ToF sensor integration for gesture control
- MIDI input/output
- Sequencer/arpeggiator
- More synthesis types (FM, wavetable)
- Audio effects (reverb, delay, chorus)
- SD card sample playback

## 📝 License

This project is open source. Feel free to use and modify for your own projects.

## 🙏 Acknowledgments

- Built with [PlatformIO](https://platformio.org/)
- Uses [Teensy Audio Library](https://github.com/PaulStoffregen/Audio) by Paul Stoffregen
- Designed for [Teensy 4.0/4.1](https://www.pjrc.com/teensy/) by PJRC

## 📧 Contact

For questions or issues, please open an issue on GitHub.

---

**Enjoy making music with your Teensy! 🎶**

