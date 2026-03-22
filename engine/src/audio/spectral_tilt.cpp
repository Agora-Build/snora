#include "audio/spectral_tilt.h"
#include <cmath>
#include <algorithm>

namespace snora {

void SpectralTilt::process(int16_t* buffer, int num_samples, float slope) {
  // Normalize slope to [0, 1]: slope=-6 -> coeff=1, slope=0 -> coeff=0
  float coeff = slope / -6.0f;
  coeff = std::max(0.0f, std::min(1.0f, coeff));

  float pass = 1.0f - coeff;  // feedthrough coefficient

  // num_samples is the total count of interleaved samples (L+R pairs).
  // Iterate over frames: each frame has CHANNELS samples.
  int frames = num_samples / CHANNELS;

  for (int i = 0; i < frames; ++i) {
    for (int ch = 0; ch < CHANNELS; ++ch) {
      int idx = i * CHANNELS + ch;
      float x = static_cast<float>(buffer[idx]);
      float y = pass * x + coeff * prev_[ch];
      prev_[ch] = y;

      // Clamp and write back
      float clamped = std::max(-32767.0f, std::min(32767.0f, y));
      buffer[idx] = static_cast<int16_t>(clamped);
    }
  }
}

}  // namespace snora
