#include "audio/binaural.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace snora {

void BinauralGenerator::process(int16_t* buffer, int num_samples,
                                float carrier_hz, float binaural_hz, float gain) {
  if (gain <= 0.0f) return;

  double freq_left  = static_cast<double>(carrier_hz - binaural_hz * 0.5f);
  double freq_right = static_cast<double>(carrier_hz + binaural_hz * 0.5f);

  double phase_inc_left  = freq_left  / static_cast<double>(SAMPLE_RATE);
  double phase_inc_right = freq_right / static_cast<double>(SAMPLE_RATE);

  // Scale to int16 range; 32767 * gain gives amplitude headroom
  float amplitude = 32767.0f * gain;

  // Iterate over stereo frames
  int frames = num_samples / CHANNELS;
  for (int i = 0; i < frames; ++i) {
    float left_sine  = static_cast<float>(std::sin(2.0 * M_PI * phase_left_));
    float right_sine = static_cast<float>(std::sin(2.0 * M_PI * phase_right_));

    int idx_l = i * CHANNELS;      // left channel index
    int idx_r = i * CHANNELS + 1;  // right channel index

    // ADD to existing buffer, then clamp
    float l = static_cast<float>(buffer[idx_l]) + left_sine  * amplitude;
    float r = static_cast<float>(buffer[idx_r]) + right_sine * amplitude;

    buffer[idx_l] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, l)));
    buffer[idx_r] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, r)));

    // Advance phases
    phase_left_  += phase_inc_left;
    phase_right_ += phase_inc_right;

    // Wrap phases to [0, 1) to avoid floating-point precision loss over time
    if (phase_left_  >= 1.0) phase_left_  -= 1.0;
    if (phase_right_ >= 1.0) phase_right_ -= 1.0;
  }
}

}  // namespace snora
