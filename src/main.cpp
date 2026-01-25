/*
 * Teensy 4.0/4.1 Audio Shield Instrument Sandbox
 *
 * Hardware: Teensy 4.0 or 4.1 with SGTL5000 Audio Shield
 * Output: I2S to headphone/line out
 *
 * Features:
 * - Kick, snare, hi-hat drums
 * - Karplus-Strong plucked string
 * - Synth lead and bass sounds
 * - Serial command interface
 */

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// ============================================================================
// AUDIO OBJECT DECLARATIONS
// ============================================================================

// Drum synthesizers
AudioSynthSimpleDrum     drum1;          // Kick drum
AudioSynthSimpleDrum     drum2;          // Snare drum

// Hi-hat: noise + envelope
AudioSynthNoiseWhite     noise1;
AudioEffectEnvelope      envelope1;      // Hi-hat envelope

// Plucked string (Karplus-Strong)
AudioSynthKarplusStrong  string1;

// Synth lead: oscillator + envelope + filter
AudioSynthWaveform       waveform1;      // Lead oscillator
AudioEffectEnvelope      envelope2;      // Lead envelope
AudioFilterStateVariable filter1;        // Lead filter

// Bass: oscillator + envelope + filter
AudioSynthWaveform       waveform2;      // Bass oscillator
AudioEffectEnvelope      envelope3;      // Bass envelope
AudioFilterStateVariable filter2;        // Bass filter

// Mixer and output
AudioMixer4              mixer1;         // Main mixer
AudioOutputI2S           i2s1;           // I2S output (DAC)

// Audio shield control
AudioControlSGTL5000     sgtl5000_1;

// ============================================================================
// AUDIO CONNECTIONS (Patch Cords)
// ============================================================================

// Kick drum -> mixer channel 0
AudioConnection          patchCord1(drum1, 0, mixer1, 0);

// Snare drum -> mixer channel 1
AudioConnection          patchCord2(drum2, 0, mixer1, 1);

// Hi-hat: noise -> envelope -> mixer channel 2
AudioConnection          patchCord3(noise1, envelope1);
AudioConnection          patchCord4(envelope1, 0, mixer1, 2);

// Plucked string -> mixer channel 3
AudioConnection          patchCord5(string1, 0, mixer1, 3);

// Lead synth: waveform -> envelope -> filter -> mixer (reuse channel 0 with lower gain)
AudioConnection          patchCord6(waveform1, envelope2);
AudioConnection          patchCord7(envelope2, 0, filter1, 0);
AudioConnection          patchCord8(filter1, 0, mixer1, 0);  // Lowpass output

// Bass synth: waveform -> envelope -> filter -> mixer (reuse channel 1 with lower gain)
AudioConnection          patchCord9(waveform2, envelope3);
AudioConnection          patchCord10(envelope3, 0, filter2, 0);
AudioConnection          patchCord11(filter2, 0, mixer1, 1);  // Lowpass output

// Mixer -> I2S output (both left and right channels)
AudioConnection          patchCord12(mixer1, 0, i2s1, 0);    // Left
AudioConnection          patchCord13(mixer1, 0, i2s1, 1);    // Right

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

bool muteIfIdle = false;
float currentPluckFreq = 440.0;  // Default A4
float currentVolume = 0.5;       // Default volume (0.0 to 1.0)

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void printHelp();
void handleCommand(char c);
void setupDrums();
void setupSynths();
void triggerKick();
void triggerSnare();
void triggerHiHat();
void triggerPluck(float frequency);
void triggerLead();
void triggerBass();
void increaseVolume();
void decreaseVolume();

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);

  // Wait a moment for serial to initialize
  delay(1000);

  Serial.println("\n=== Teensy Audio Shield Instrument Sandbox ===");

  // IMPORTANT: AudioMemory allocates memory for audio processing
  // Each block is 128 samples. More complex patches need more memory.
  // Teensy 4.0/4.1 can handle 200+ blocks easily.
  AudioMemory(20);

  // Initialize the audio shield
  // IMPORTANT: enable() must be called before any other SGTL5000 functions
  sgtl5000_1.enable();

  // Set volume (0.0 to 1.0)
  // IMPORTANT: Start at moderate volume to avoid speaker damage
  sgtl5000_1.volume(0.5);

  // Configure mixer gains to avoid clipping
  // Each channel: 0.0 to 1.0+ (values > 1.0 can cause clipping)
  // We'll use lower gains since multiple sources can sum
  mixer1.gain(0, 0.4);  // Kick/Lead channel
  mixer1.gain(1, 0.4);  // Snare/Bass channel
  mixer1.gain(2, 0.3);  // Hi-hat channel
  mixer1.gain(3, 0.5);  // Pluck channel

  // Setup instruments
  setupDrums();
  setupSynths();

  Serial.println("Audio system initialized!");
  Serial.println();
  printHelp();
}


// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // Check for serial commands
  if (Serial.available() > 0) {
    char c = Serial.read();
    handleCommand(c);
  }

  // Small delay to prevent overwhelming the serial buffer
  delay(10);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void printHelp() {
  Serial.println("=== COMMAND MENU ===");
  Serial.println("Drums:");
  Serial.println("  k = Kick drum");
  Serial.println("  s = Snare drum");
  Serial.println("  h = Hi-hat");
  Serial.println();
  Serial.println("Plucked String:");
  Serial.println("  p = Trigger pluck at current pitch");
  Serial.println("  1 = Set pluck to A3 (220 Hz)");
  Serial.println("  2 = Set pluck to E4 (329.63 Hz)");
  Serial.println("  3 = Set pluck to A4 (440 Hz)");
  Serial.println();
  Serial.println("Synths:");
  Serial.println("  l = Synth lead note");
  Serial.println("  b = Bass note");
  Serial.println();
  Serial.println("Settings:");
  Serial.println("  + = Increase volume");
  Serial.println("  - = Decrease volume");
  Serial.println("  m = Toggle mute-if-idle mode");
  Serial.println("  ? = Show this help");
  Serial.println("====================");
  Serial.println();
}

void handleCommand(char c) {
  switch (c) {
    case 'k':
      Serial.println(">> Kick");
      triggerKick();
      break;

    case 's':
      Serial.println(">> Snare");
      triggerSnare();
      break;

    case 'h':
      Serial.println(">> Hi-hat");
      triggerHiHat();
      break;

    case 'p':
      Serial.print(">> Pluck @ ");
      Serial.print(currentPluckFreq);
      Serial.println(" Hz");
      triggerPluck(currentPluckFreq);
      break;

    case '1':
      currentPluckFreq = 220.0;  // A3
      Serial.println(">> Pluck pitch set to A3 (220 Hz)");
      break;

    case '2':
      currentPluckFreq = 329.63;  // E4
      Serial.println(">> Pluck pitch set to E4 (329.63 Hz)");
      break;

    case '3':
      currentPluckFreq = 440.0;  // A4
      Serial.println(">> Pluck pitch set to A4 (440 Hz)");
      break;

    case 'l':
      Serial.println(">> Lead synth");
      triggerLead();
      break;

    case 'b':
      Serial.println(">> Bass synth");
      triggerBass();
      break;

    case '+':
    case '=':  // Allow both + and = (same key without shift)
      increaseVolume();
      break;

    case '-':
    case '_':  // Allow both - and _ (same key with shift)
      decreaseVolume();
      break;

    case 'm':
      muteIfIdle = !muteIfIdle;
      Serial.print(">> Mute-if-idle: ");
      Serial.println(muteIfIdle ? "ON" : "OFF");
      // Note: Actual mute implementation would require additional logic
      // For now, this is just a placeholder toggle
      break;

    case '?':
      printHelp();
      break;

    case '\n':
    case '\r':
      // Ignore newlines
      break;

    default:
      Serial.print(">> Unknown command: ");
      Serial.println(c);
      break;
  }
}

void setupDrums() {
  // Kick drum configuration
  // frequency: pitch of the drum (30-100 Hz typical for kick)
  // length: duration in milliseconds
  // secondMix: blend of second pitch (0.0 to 1.0)
  // pitchMod: pitch sweep amount
  drum1.frequency(60);
  drum1.length(300);
  drum1.secondMix(0.0);
  drum1.pitchMod(0.55);

  // Snare drum configuration
  // Higher pitch, shorter decay, more noise-like
  drum2.frequency(200);
  drum2.length(150);
  drum2.secondMix(0.5);
  drum2.pitchMod(0.3);

  // Hi-hat envelope: short, sharp attack and decay
  envelope1.attack(1.0);
  envelope1.hold(0.0);
  envelope1.decay(50.0);
  envelope1.sustain(0.0);
  envelope1.release(50.0);

  // Start noise generator at low amplitude
  noise1.amplitude(0.3);
}

void setupSynths() {
  // Lead synth setup
  waveform1.begin(WAVEFORM_SAWTOOTH);
  waveform1.amplitude(0.6);
  waveform1.frequency(440);  // Default A4

  // Lead envelope: short attack, medium decay
  envelope2.attack(5.0);
  envelope2.hold(10.0);
  envelope2.decay(200.0);
  envelope2.sustain(0.3);
  envelope2.release(300.0);

  // Lead filter: lowpass with moderate resonance
  filter1.frequency(2000);
  filter1.resonance(1.2);
  filter1.octaveControl(2.0);

  // Bass synth setup
  waveform2.begin(WAVEFORM_SQUARE);
  waveform2.amplitude(0.7);
  waveform2.frequency(110);  // Default A2

  // Bass envelope: slightly longer attack, sustained
  envelope3.attack(10.0);
  envelope3.hold(20.0);
  envelope3.decay(300.0);
  envelope3.sustain(0.5);
  envelope3.release(400.0);

  // Bass filter: lower cutoff for deep bass
  filter2.frequency(800);
  filter2.resonance(1.5);
  filter2.octaveControl(1.5);
}

// ============================================================================
// INSTRUMENT TRIGGER FUNCTIONS
// ============================================================================

void triggerKick() {
  drum1.noteOn();
}

void triggerSnare() {
  drum2.noteOn();
}

void triggerHiHat() {
  // Trigger the envelope for the noise generator
  envelope1.noteOn();
  // Automatically release after a short time
  delay(1);
  envelope1.noteOff();
}

void triggerPluck(float frequency) {
  // Karplus-Strong algorithm
  // velocity: 0.0 to 1.0 (pluck strength)
  string1.noteOn(frequency, 0.8);
}

void triggerLead() {
  // Play a lead note at A4 (440 Hz)
  waveform1.frequency(440);
  envelope2.noteOn();

  // Auto-release after a short time
  delay(1);
  envelope2.noteOff();
}

void triggerBass() {
  // Play a bass note at A2 (110 Hz)
  waveform2.frequency(110);
  envelope3.noteOn();

  // Auto-release after a short time
  delay(1);
  envelope3.noteOff();
}

void increaseVolume() {
  currentVolume += 0.05;  // Increase by 5%

  // Clamp to maximum of 1.0
  if (currentVolume > 1.0) {
    currentVolume = 1.0;
  }

  // Update the codec volume
  sgtl5000_1.volume(currentVolume);

  // Print feedback
  Serial.print(">> Volume: ");
  Serial.print(currentVolume * 100.0, 0);  // Show as percentage
  Serial.println("%");
}

void decreaseVolume() {
  currentVolume -= 0.05;  // Decrease by 5%

  // Clamp to minimum of 0.0
  if (currentVolume < 0.0) {
    currentVolume = 0.0;
  }

  // Update the codec volume
  sgtl5000_1.volume(currentVolume);

  // Print feedback
  Serial.print(">> Volume: ");
  Serial.print(currentVolume * 100.0, 0);  // Show as percentage
  Serial.println("%");
}