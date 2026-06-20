/*
 * Bass Machine mode - 8-step bass sequencer with hand-controlled filter sweep.
 *
 *   Left hand  = toggles bass notes (rows = pitch in MAJOR_SCALE_C2, col = step).
 *   Right hand = "wah" - hand distance maps to lowpass filter cutoff.
 *
 * The right LED matrix shows a stable, smoothed 4-level "ladder" of how
 * far the player's right hand reaches across the sensor (red -> orange).
 *
 *   BassMachine::process();
 */

#ifndef AURA_MODES_BASS_MACHINE_H
#define AURA_MODES_BASS_MACHINE_H

namespace BassMachine {
  void process();
  void enter();   // full reset (called when switching into this mode)
  void clear();   // soft reset: clear pattern, keep filter sweep state
}

#endif // AURA_MODES_BASS_MACHINE_H
