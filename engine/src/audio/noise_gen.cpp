#include "audio/noise_gen.h"
#include <cmath>
#include <algorithm>

namespace snora {

NoiseGenerator::NoiseGenerator()
    : rng_(std::random_device{}()), dist_(-1.0f, 1.0f) {}

NoiseGenerator::~NoiseGenerator() = default;

void NoiseGenerator::generate(int16_t* output, float spectral_slope, float amplitude) {
  // Mix between white noise and pink-filtered noise based on spectral_slope.
  // slope = 0: pure white, slope = -3: pure pink (Kellett), slope = -6: extra filtering
  float pink_mix = std::clamp(-spectral_slope / 3.0f, 0.0f, 1.0f);
  float brown_mix = std::clamp((-spectral_slope - 3.0f) / 3.0f, 0.0f, 1.0f);

  for (int ch = 0; ch < CHANNELS; ++ch) {
    auto& s = pink_state_[ch];
    for (int i = 0; i < SAMPLES_PER_CHANNEL; ++i) {
      float white = dist_(rng_);

      // Paul Kellett's pinking filter (attempt economy of computation)
      s.b0 = 0.99886f * s.b0 + white * 0.0555179f;
      s.b1 = 0.99332f * s.b1 + white * 0.0750759f;
      s.b2 = 0.96900f * s.b2 + white * 0.1538520f;
      s.b3 = 0.86650f * s.b3 + white * 0.3104856f;
      s.b4 = 0.55000f * s.b4 + white * 0.5329522f;
      s.b5 = -0.7616f * s.b5 - white * 0.0168980f;
      float pink = s.b0 + s.b1 + s.b2 + s.b3 + s.b4 + s.b5 + s.b6 + white * 0.5362f;
      s.b6 = white * 0.115926f;
      pink *= 0.11f;  // normalize

      // Brown noise: integrate white noise (leaky)
      brown_state_[ch] = brown_state_[ch] * 0.998f + white * 0.04f;

      // Mix based on slope
      float sample = white * (1.0f - pink_mix) + pink * pink_mix * (1.0f - brown_mix) + brown_state_[ch] * brown_mix;
      sample *= amplitude;

      // Clamp and convert to int16
      sample = std::clamp(sample, -1.0f, 1.0f);
      output[i * CHANNELS + ch] = static_cast<int16_t>(sample * 32767.0f);
    }
  }
}

}  // namespace snora
