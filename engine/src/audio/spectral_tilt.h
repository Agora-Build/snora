#pragma once

#include "audio/audio_format.h"
#include <cstdint>

namespace snora {

// Multi-pole IIR shelving filter that tilts the spectrum.
// slope controls rolloff: -6 dB/oct (brown/heavy) to 0 (no filtering).
// Uses 3 cascaded one-pole stages for a more audible effect.
// Processes stereo interleaved buffer in-place.
class SpectralTilt {
public:
  void process(int16_t *buffer, int num_samples, float slope);

private:
  static constexpr int STAGES = 3;
  float prev_[STAGES][CHANNELS] = {};
};

} // namespace snora
