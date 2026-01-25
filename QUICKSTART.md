# Quick Start Guide

## 1. Hardware Setup (30 seconds)
1. Stack Audio Shield on Teensy 4.0/4.1
2. Plug headphones into Audio Shield's 3.5mm jack
3. Connect Teensy to computer via USB

## 2. Build & Upload (2 minutes)

### Using VS Code:
1. Open this folder in VS Code
2. Click **✓** (checkmark) to build
3. Click **→** (arrow) to upload
4. Click **🔌** (plug) to open serial monitor

### Using Terminal:
```bash
pio run --target upload
pio device monitor
```

## 3. Test It! (1 minute)

You should see:
```
=== Teensy Audio Shield Instrument Sandbox ===
Audio system initialized!
```

Try these commands in the serial monitor:
- Type `k` → Hear kick drum
- Type `s` → Hear snare drum
- Type `h` → Hear hi-hat
- Type `p` → Hear plucked string
- Type `l` → Hear synth lead
- Type `b` → Hear bass note
- Type `+` → Increase volume
- Type `-` → Decrease volume
- Type `?` → Show full command menu

## 4. Experiment!

**Change pluck pitch:**
- `1` = A3 (220 Hz) - low
- `2` = E4 (329.63 Hz) - medium
- `3` = A4 (440 Hz) - high
- `p` = Play current pitch

**Make a beat:**
```
k (kick)
s (snare)
h (hi-hat)
h (hi-hat)
```

**Play a melody:**
```
1 p (low pluck)
2 p (mid pluck)
3 p (high pluck)
l (lead note)
b (bass note)
```

## Troubleshooting

**No sound?**
- Check headphones are plugged into Audio Shield (not Teensy)
- Verify Audio Shield is properly seated
- Try adjusting volume in code: `sgtl5000_1.volume(0.8);`

**Build error?**
- Wait for PlatformIO to finish downloading libraries
- Try: `pio run --target clean` then rebuild

**Upload fails?**
- Press the button on Teensy board
- Try a different USB port

## Next Steps

See **BUILD_INSTRUCTIONS.md** for detailed documentation.
See **PROJECT_SUMMARY.md** for technical details and audio parameters.

Ready to add ToF sensors or MIDI? This firmware is your foundation!

