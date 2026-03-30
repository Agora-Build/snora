#include "audio/spectral_tilt.h"
#include <algorithm>
#include <cmath>

namespace snora {

void SpectralTilt::process(int16_t *buffer, int num_samples, float slope) {
  // Normalize slope to [0, 1]: slope=-6 -> coeff~0.85, slope=0 -> coeff=0
  // Use a gentler per-stage coefficient since we cascade 3 stages
  float norm = std::clamp(slope / -6.0f, 0.0f, 1.0f);
  float coeff = norm * 0.85f; // per-stage feedback
  float pass = 1.0f - coeff;

  int frames = num_samples / CHANNELS;

  for (int i = 0; i < frames; ++i) {
    for (int ch = 0; ch < CHANNELS; ++ch) {
      int idx = i * CHANNELS + ch;
      float x = static_cast<float>(buffer[idx]);

      // Cascade 3 one-pole lowpass stages
      for (int s = 0; s < STAGES; ++s) {
        x = pass * x + coeff * prev_[s][ch];
        prev_[s][ch] = x;
      }

      buffer[idx] = static_cast<int16_t>(std::clamp(x, -32767.0f, 32767.0f));
    }
  }
}

} // namespace snora
