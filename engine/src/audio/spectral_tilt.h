#pragma once

#include "audio/audio_format.h"
#include <cstdint>

namespace snora {

// One-pole IIR shelving filter that tilts the spectrum.
// slope controls rolloff: -6 dB/oct (brown/heavy) to -2 dB/oct (near pink).
// Algorithm: slope_coeff = slope / -6.0 (normalized 0..1)
//            y[n] = (1 - |slope_coeff|) * x[n] + slope_coeff * y[n-1]
// Processes stereo interleaved buffer in-place.
class SpectralTilt {
public:
  // slope: -6 to -2 (or 0 for no filtering)
  void process(int16_t *buffer, int num_samples, float slope);

private:
  float prev_[CHANNELS] = {0.0f, 0.0f}; // per-channel filter state
};

} // namespace snora
