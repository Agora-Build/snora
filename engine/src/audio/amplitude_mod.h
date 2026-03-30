#pragma once

#include "audio/audio_format.h"
#include <cstdint>

namespace snora {

// Asymmetric raised cosine amplitude modulation for respiration entrainment.
// Inhale:exhale ratio = 1:2 (inhale occupies 1/3 of cycle, exhale 2/3).
// Each sample in the buffer is multiplied by the envelope value.
class AmplitudeMod {
public:
  // am_freq_hz: modulation frequency in Hz
  void process(int16_t *buffer, int num_samples, float am_freq_hz);

private:
  double phase_ = 0.0; // oscillator phase [0, 1)
};

} // namespace snora
