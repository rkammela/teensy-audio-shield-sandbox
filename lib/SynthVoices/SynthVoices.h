/*
 * SynthVoices - the AURA audio engine: every AudioStream object, the
 * patch-cord graph, the SGTL5000 codec, and the one-shot panic helper.
 *
 * The library owns the *definitions* of these globals in SynthVoices.cpp;
 * this header exposes them with `extern` so the rest of the project keeps
 * poking them by name (e.g. stringEnvelope.noteOn(), mixer1.gain(...))
 * without any rewiring of the mode code.
 *
 *   SynthVoices::begin(masterVolume);   // AudioMemory + codec + mixer gains
 *                                       // + envelope/filter/drum defaults
 *   SynthVoices::panic();               // release every sustaining envelope
 */

#ifndef AURA_SYNTH_VOICES_H
#define AURA_SYNTH_VOICES_H

#include <Audio.h>

// ----------------------------------------------------------------------------
// Audio object handles (defined in SynthVoices.cpp; re-declared here so every
// mode can keep using the short names it already uses).
// ----------------------------------------------------------------------------

// String voice (Karplus-Strong -> envelope -> state-variable filter)
extern AudioSynthKarplusStrong  stringVoice;
extern AudioEffectEnvelope      stringEnvelope;
extern AudioFilterStateVariable stringFilter;

// Drum synthesizers
extern AudioSynthSimpleDrum     kickDrum;
extern AudioSynthSimpleDrum     snareDrum;

// Hi-hat (white noise gated by a fast envelope)
extern AudioSynthNoiseWhite     noiseWhite;
extern AudioEffectEnvelope      hatEnvelope;

// Main mixer + I2S output
extern AudioMixer4              mixer1;
extern AudioOutputI2S           i2s1;

// SGTL5000 audio shield codec
extern AudioControlSGTL5000     sgtl5000_1;

namespace SynthVoices {

  // Bring the audio engine up:
  //   - reserves AudioMemory blocks
  //   - enables the SGTL5000 and sets master volume
  //   - applies per-channel mixer gains and per-voice parameter defaults
  //     (string envelope, string filter, kick/snare drum, hi-hat envelope)
  // Call this once, before any mode logic starts triggering notes.
  void begin(float masterVolume);

  // Release every sustaining voice immediately. Used when switching modes
  // and on the encoder "reset" press so we never leave a note ringing.
  void panic();

} // namespace SynthVoices

#endif // AURA_SYNTH_VOICES_H
