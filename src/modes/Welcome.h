/*
 * Welcome mode - LED-only boot/idle screen (no audio, no sensors).
 *
 *   LEFT  matrix: pulsing red heart, centered vertically.
 *   RIGHT matrix: rainbow "TSA NATIONALS 2026" scrolling right-to-left
 *                 across all 8 rows.
 *
 * Boot default: AURA powers on into this mode so the player sees branding
 * before any sensors come up. The encoder cycles forward into BACKSTAGE
 * (diagnostic) and the other play modes.
 *
 *   Welcome::process(currentTime);   // call once per loop tick when active
 */

#ifndef AURA_MODES_WELCOME_H
#define AURA_MODES_WELCOME_H

#include <Arduino.h>

namespace Welcome {
  void process(unsigned long currentTime);
}

#endif // AURA_MODES_WELCOME_H
