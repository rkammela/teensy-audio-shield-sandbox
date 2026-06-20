/*
 * Chord Jam mode - guitar simulation.
 *
 *   Left sensor  = FRETBOARD. The hand selects which "string" (one of 4
 *                  row-pairs) is fretted by reaching across it.
 *   Right sensor = SOUNDHOLE. Vertical hand motion velocity counts as a
 *                  strum; consecutive strums walk through fretted strings
 *                  so holding two strings + strumming repeatedly plays an
 *                  arpeggiated chord.
 *
 * String pitches come from MusicNotes::GUITAR_OPEN_TUNING_TOP4.
 *
 *   ChordJam::process();  // call once per loop tick when the mode is active
 */

#ifndef AURA_MODES_CHORD_JAM_H
#define AURA_MODES_CHORD_JAM_H

namespace ChordJam {
  void process();
  void enter();   // full reset (called when switching into this mode)
  void clear();   // soft reset (called from the encoder "clear" button)
}

#endif // AURA_MODES_CHORD_JAM_H
