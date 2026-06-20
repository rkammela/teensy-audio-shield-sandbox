/*
 * Dual Loop mode - 8-step melody + drum sequencer.
 *
 * Left hand toggles melody notes on the left grid (col = step, row = note
 * in C major from C4..C5). Right hand toggles drum hits on the right grid
 * (col = step, row = drum: kick / snare / hat / extra perc). A playhead
 * walks both grids at a tempo defined by the mode. LEDs visualize the
 * pattern: bright on active cells, dim playhead on inactive cells.
 *
 *   DualLoop::process();   // call once per loop tick when the mode is active
 */

#ifndef AURA_MODES_DUAL_LOOP_H
#define AURA_MODES_DUAL_LOOP_H

namespace DualLoop {
  void process();
  void enter();   // full reset (called when switching into this mode)
  void clear();   // soft reset: clear pattern, keep touch debounce state
                  // (called from the encoder "clear pattern" button)
}

#endif // AURA_MODES_DUAL_LOOP_H
