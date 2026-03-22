#pragma once

#include "audio/audio_format.h"
#include <cstdint>
#include <vector>
#include <utility>

namespace snora {

// Multi-layer mixer.
// Sums N input buffers weighted by per-layer gains, applies master volume,
// and clamps the result to int16 range.
class Mixer {
 public:
  // Mix multiple layers into output.
  // layers: vector of (buffer pointer, gain) pairs.
  //   Each buffer has num_samples int16_t values.
  // output: destination buffer (num_samples int16_t values, written not added).
  // master_volume: overall output scale [0, 1].
  void mix(const std::vector<std::pair<const int16_t*, float>>& layers,
           int16_t* output, int num_samples, float master_volume);
};

}  // namespace snora
