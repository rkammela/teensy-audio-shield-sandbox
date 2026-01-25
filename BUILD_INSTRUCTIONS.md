# Teensy Audio Shield Instrument Sandbox - Build Instructions

## Hardware Requirements
- **Teensy 4.0 or 4.1** microcontroller
- **Teensy Audio Shield** (Rev D with SGTL5000 codec)
- **USB cable** for programming and power
- **Headphones or powered speakers** connected to the Audio Shield's 3.5mm jack

## Hardware Setup
1. Stack the Audio Shield on top of the Teensy 4.0/4.1
2. Ensure all pins are properly aligned and seated
3. Connect headphones/speakers to the Audio Shield's headphone jack
4. Connect Teensy to your computer via USB

## Software Requirements
- **VS Code** with PlatformIO extension installed
- **PlatformIO Core** (installed automatically with the extension)

## Building the Project

### Method 1: Using VS Code GUI
1. Open this project folder in VS Code
2. Wait for PlatformIO to initialize (check bottom status bar)
3. Click the **checkmark icon** (✓) in the bottom status bar to build
4. Or use: `Terminal` → `Run Task` → `PlatformIO: Build`

### Method 2: Using PlatformIO CLI
```bash
# From the project root directory
pio run
```

## Uploading to Teensy

### Method 1: Using VS Code GUI
1. Click the **right arrow icon** (→) in the bottom status bar to upload
2. Or use: `Terminal` → `Run Task` → `PlatformIO: Upload`
3. The Teensy Loader will automatically program the board

### Method 2: Using PlatformIO CLI
```bash
pio run --target upload
```

**Note:** The first upload may require you to press the physical button on the Teensy board to enter bootloader mode.

## Opening Serial Monitor

### Method 1: Using VS Code GUI
1. Click the **plug icon** (🔌) in the bottom status bar
2. Or use: `Terminal` → `Run Task` → `PlatformIO: Monitor`

### Method 2: Using PlatformIO CLI
```bash
pio device monitor
```

### Method 3: Using Arduino Serial Monitor
1. Open Arduino IDE
2. Select `Tools` → `Port` → Select your Teensy's COM port
3. Open `Tools` → `Serial Monitor`
4. Set baud rate to **115200**

## Using the Instrument Sandbox

Once uploaded and the serial monitor is open, you should see:
```
=== Teensy Audio Shield Instrument Sandbox ===
Audio system initialized!

=== COMMAND MENU ===
...
```

### Available Commands
Send single characters via serial monitor:

**Drums:**
- `k` - Kick drum
- `s` - Snare drum
- `h` - Hi-hat

**Plucked String:**
- `p` - Trigger pluck at current pitch
- `1` - Set pluck to A3 (220 Hz)
- `2` - Set pluck to E4 (329.63 Hz)
- `3` - Set pluck to A4 (440 Hz)

**Synths:**
- `l` - Synth lead note
- `b` - Bass note

**Settings:**
- `+` - Increase volume by 5%
- `-` - Decrease volume by 5%
- `m` - Toggle mute-if-idle mode (placeholder)
- `?` - Show help menu

## Troubleshooting

### No audio output
- Check that headphones/speakers are connected to the Audio Shield
- Verify the Audio Shield is properly seated on the Teensy
- Check volume level (default is 0.5, adjust in code if needed)
- Ensure the SGTL5000 codec is being initialized (check serial output)

### Build errors
- Ensure PlatformIO has downloaded all dependencies (Audio library, Wire, SPI, etc.)
- Try cleaning the build: `pio run --target clean` then rebuild
- Check that you're using the correct board (teensy40 or teensy41)

### Upload fails
- Press the physical button on the Teensy to enter bootloader mode
- Check USB cable connection
- Verify correct COM port is selected
- Try a different USB port

### Serial monitor shows garbage
- Verify baud rate is set to 115200
- Reset the Teensy after opening serial monitor

## Audio Performance Notes

- **AudioMemory(20)**: Allocates 20 blocks of audio memory. Increase if you get audio glitches.
- **Volume**: Set to 0.5 (50%) by default. Adjust with `sgtl5000_1.volume(0.0 to 1.0)`.
- **Mixer gains**: Channels set to 0.3-0.5 to prevent clipping when multiple sounds play.
- **CPU usage**: Monitor with `AudioProcessorUsageMax()` and `AudioMemoryUsageMax()` if needed.

## Next Steps

This firmware is a foundation for adding:
- MIDI input
- ToF sensor integration for gesture control
- Sequencer/pattern playback
- More complex synthesis (FM, wavetable, etc.)
- Effects (reverb, delay, chorus)
- Parameter control via potentiometers or encoders

Enjoy experimenting with your Teensy Audio Shield!

