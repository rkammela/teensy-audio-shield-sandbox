/*
 * MusicNotes - MIDI <-> frequency conversion and common scale presets.
 *
 * Header-only API; implementation lives in MusicNotes.cpp.
 * No Arduino, Audio, or hardware dependencies - pure math + data tables,
 * so this library can be reused by any project that needs MIDI helpers.
 */

#ifndef AURA_MUSIC_NOTES_H
#define AURA_MUSIC_NOTES_H

namespace MusicNotes {

  // Convert a MIDI note number (0-127) to a frequency in Hz.
  // A4 (MIDI 69) returns 440.0. Equal-tempered, standard tuning.
  float midiToFreq(int midiNote);

  // C major scale, one octave starting at middle C (C4 = MIDI 60).
  //   C4  D4  E4  F4  G4  A4  B4  C5
  extern const int MAJOR_SCALE_C4[8];

  // C major scale, one octave starting at C2 (bass register).
  //   C2  D2  E2  F2  G2  A2  B2  C3
  extern const int MAJOR_SCALE_C2[8];

  // Top 4 strings of a guitar in standard tuning (lowest to highest):
  //   E3  A3  D4  G4
  extern const int GUITAR_OPEN_TUNING_TOP4[4];

} // namespace MusicNotes

#endif // AURA_MUSIC_NOTES_H
