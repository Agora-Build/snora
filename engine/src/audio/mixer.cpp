#include "audio/mixer.h"
#include <algorithm>

namespace snora {

void Mixer::mix(const std::vector<std::pair<const int16_t*, float>>& layers,
                int16_t* output, int num_samples, float master_volume) {
  for (int i = 0; i < num_samples; ++i) {
    float sum = 0.0f;
    for (const auto& [buf, gain] : layers) {
      if (buf != nullptr) {
        sum += static_cast<float>(buf[i]) * gain;
      }
    }

    sum *= master_volume;

    // Clamp to int16 range
    float clamped = std::max(-32767.0f, std::min(32767.0f, sum));
    output[i] = static_cast<int16_t>(clamped);
  }
}

}  // namespace snora
