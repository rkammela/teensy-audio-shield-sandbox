#include "MusicNotes.h"
#include <math.h>

namespace MusicNotes {

  float midiToFreq(int midiNote) {
    // 12-TET reference: A4 (MIDI 69) = 440 Hz, each semitone is 2^(1/12).
    return 440.0f * powf(2.0f, (midiNote - 69) / 12.0f);
  }

  const int MAJOR_SCALE_C4[8] = {60, 62, 64, 65, 67, 69, 71, 72};
  const int MAJOR_SCALE_C2[8] = {36, 38, 40, 41, 43, 45, 47, 48};
  const int GUITAR_OPEN_TUNING_TOP4[4] = {52, 57, 62, 67};

} // namespace MusicNotes
