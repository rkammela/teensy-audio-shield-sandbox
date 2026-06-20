/*
 * Battle mode - two-player duel.
 *
 * Player 1 plays the left sensor (CH0) on the string (Karplus-Strong) voice;
 * Player 2 plays the right sensor (CH1) on the snare-drum voice. Whichever
 * player has more active sensor zones earns a point per frame; the score
 * is rendered as a vertical bar on the matching LED matrix.
 *
 *   Battle::process();   // call once per loop tick when the mode is active
 */

#ifndef AURA_MODES_BATTLE_H
#define AURA_MODES_BATTLE_H

namespace Battle {
  void process();
  void enter();   // full reset (called when switching into this mode)
  void clear();   // soft reset: zero the scores, keep nothing else
}

#endif // AURA_MODES_BATTLE_H
