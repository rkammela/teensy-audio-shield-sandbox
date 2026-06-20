#include "SynthVoices.h"

// ----------------------------------------------------------------------------
// Audio object definitions
// ----------------------------------------------------------------------------

AudioSynthKarplusStrong  stringVoice;
AudioEffectEnvelope      stringEnvelope;
AudioFilterStateVariable stringFilter;

AudioSynthSimpleDrum     kickDrum;
AudioSynthSimpleDrum     snareDrum;

AudioSynthNoiseWhite     noiseWhite;
AudioEffectEnvelope      hatEnvelope;

AudioMixer4              mixer1;
AudioOutputI2S           i2s1;

AudioControlSGTL5000     sgtl5000_1;

// ----------------------------------------------------------------------------
// Patch cords (anonymous to the rest of the program - only the audio engine
// cares about these once they've been constructed)
// ----------------------------------------------------------------------------

// String voice: stringVoice -> envelope -> filter -> mixer channel 0
static AudioConnection patchCord1(stringVoice, stringEnvelope);
static AudioConnection patchCord2(stringEnvelope, 0, stringFilter, 0);
static AudioConnection patchCord3(stringFilter, 0, mixer1, 0);

// Kick drum -> mixer channel 1
static AudioConnection patchCord4(kickDrum, 0, mixer1, 1);

// Snare drum -> mixer channel 2
static AudioConnection patchCord5(snareDrum, 0, mixer1, 2);

// Hi-hat: noise -> envelope -> mixer channel 3
static AudioConnection patchCord6(noiseWhite, hatEnvelope);
static AudioConnection patchCord7(hatEnvelope, 0, mixer1, 3);

// Mixer -> I2S output (mono signal duplicated to both DAC channels)
static AudioConnection patchCord8(mixer1, 0, i2s1, 0);
static AudioConnection patchCord9(mixer1, 0, i2s1, 1);

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

namespace SynthVoices {

void begin(float masterVolume) {
  // AudioMemory must be allocated before any AudioStream objects run.
  AudioMemory(20);

  sgtl5000_1.enable();
  sgtl5000_1.volume(masterVolume);

  // Per-channel mixer gains (matched to the original setup() block).
  mixer1.gain(0, 0.8f * masterVolume);  // String voice
  mixer1.gain(1, 0.8f * masterVolume);  // Kick drum
  mixer1.gain(2, 0.7f * masterVolume);  // Snare drum
  mixer1.gain(3, 0.5f * masterVolume);  // Hi-hat

  // String envelope (slow plucked decay)
  stringEnvelope.attack(10.0f);
  stringEnvelope.hold(20.0f);
  stringEnvelope.decay(500.0f);
  stringEnvelope.sustain(0.4f);
  stringEnvelope.release(300.0f);

  // String filter: lowpass with moderate resonance
  stringFilter.frequency(2000);
  stringFilter.resonance(1.2f);
  stringFilter.octaveControl(2.0f);

  // Kick drum defaults
  kickDrum.frequency(60);
  kickDrum.length(300);
  kickDrum.secondMix(0.0f);
  kickDrum.pitchMod(0.55f);

  // Snare drum defaults
  snareDrum.frequency(200);
  snareDrum.length(150);
  snareDrum.secondMix(0.5f);
  snareDrum.pitchMod(0.3f);

  // Hi-hat envelope: short, sharp attack and decay
  hatEnvelope.attack(1.0f);
  hatEnvelope.hold(0.0f);
  hatEnvelope.decay(50.0f);
  hatEnvelope.sustain(0.0f);
  hatEnvelope.release(50.0f);

  // White noise generator at low amplitude (gated by hatEnvelope)
  noiseWhite.amplitude(0.3f);
}

void panic() {
  stringEnvelope.noteOff();
  hatEnvelope.noteOff();
}

} // namespace SynthVoices
