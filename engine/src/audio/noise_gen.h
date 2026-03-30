#pragma once

#include "audio/audio_format.h"
#include <cstdint>
#include <random>
#include <vector>

namespace snora {

// Generates white noise and applies Paul Kellett IIR pinking filter.
// Output: FRAME_SAMPLES int16_t samples (stereo interleaved).
class NoiseGenerator {
public:
  NoiseGenerator();
  ~NoiseGenerator();

  // Generate one frame of noise into output buffer.
  // spectral_slope: -6 (brown) to 0 (white). Currently implements pink (-3
  // dB/oct) via Kellett filter.
  void generate(int16_t *output, float spectral_slope, float amplitude);

private:
  // Kellett filter state (per channel)
  struct PinkState {
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
  };

  PinkState pink_state_[CHANNELS];
  float brown_state_[CHANNELS] = {0, 0};
  std::mt19937 rng_;
  std::uniform_real_distribution<float> dist_;

#ifdef SNORA_USE_CUDA
  // CUDA resources
  float *d_output_ = nullptr;
  void *d_curand_state_ = nullptr;
  bool cuda_initialized_ = false;
  void init_cuda();
#endif
};

} // namespace snora
