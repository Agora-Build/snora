#pragma once

#include "audio/audio_format.h"
#include <cstdint>

namespace snora {

// Binaural beat oscillators.
// Generates sine tones at (carrier - binaural_hz/2) on the left channel and
// (carrier + binaural_hz/2) on the right channel, then ADDS them to the
// existing buffer contents (does not overwrite).
// The perceived beat frequency equals binaural_hz.
class BinauralGenerator {
 public:
  // process: add binaural sine tones to buffer
  // carrier_hz: center carrier frequency (Hz)
  // binaural_hz: frequency difference between L and R (Hz)
  // gain: amplitude [0, 1]
  void process(int16_t* buffer, int num_samples, float carrier_hz,
               float binaural_hz, float gain);

 private:
  double phase_left_  = 0.0;
  double phase_right_ = 0.0;
};

}  // namespace snora
