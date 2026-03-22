#include "audio/amplitude_mod.h"
#include <cmath>
#include <algorithm>

namespace snora {

// Compute the asymmetric raised cosine envelope value for a given phase [0, 1).
// Inhale occupies [0, 1/3) of the cycle, exhale occupies [1/3, 1).
// Both transitions use a raised cosine shape: (1 - cos(pi * t)) / 2
static float asymmetric_envelope(double phase) {
  constexpr double INHALE_FRACTION = 1.0 / 3.0;  // 1:2 ratio

  if (phase < INHALE_FRACTION) {
    // Inhale: rise from 0 to 1 over inhale fraction
    double t = phase / INHALE_FRACTION;
    return static_cast<float>((1.0 - std::cos(M_PI * t)) * 0.5);
  } else {
    // Exhale: fall from 1 to 0 over exhale fraction
    double t = (phase - INHALE_FRACTION) / (1.0 - INHALE_FRACTION);
    return static_cast<float>((1.0 + std::cos(M_PI * t)) * 0.5);
  }
}

void AmplitudeMod::process(int16_t* buffer, int num_samples, float am_freq_hz) {
  // Phase increment per sample
  double phase_inc = static_cast<double>(am_freq_hz) / static_cast<double>(SAMPLE_RATE);

  for (int i = 0; i < num_samples; ++i) {
    float env = asymmetric_envelope(phase_);

    // Multiply sample by envelope
    float sample = static_cast<float>(buffer[i]) * env;
    float clamped = std::max(-32767.0f, std::min(32767.0f, sample));
    buffer[i] = static_cast<int16_t>(clamped);

    // Advance phase (per sample, not per stereo frame — both channels share phase)
    phase_ += phase_inc;
    if (phase_ >= 1.0) phase_ -= 1.0;
  }
}

}  // namespace snora
