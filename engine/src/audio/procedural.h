#pragma once

#include "audio/audio_format.h"
#include <cstdint>
#include <random>

namespace snora {

// Procedural audio texture generator.
// Three types:
//   Rain:  Stochastic grain triggers — filtered noise bursts (2-8 ms).
//          Grain density is proportional to intensity.
//   Wind:  Swept bandpass filter on white noise with sine-modulated center
//   freq. Ocean: Amplitude-modulated brown noise at wave rate (0.05-0.15 Hz).
//
// Adds generated texture into the output buffer (does not zero it first).
class ProceduralTexture {
public:
  enum class Type { Rain, Wind, Ocean };

  explicit ProceduralTexture(Type type);

  // Generate one frame of texture and ADD to buffer.
  // intensity: 0.0 (silent) to 1.0 (full volume)
  void process(int16_t *buffer, int num_samples, float intensity);

private:
  Type type_;
  std::mt19937 rng_;

  // Shared oscillator phase for slow LFO (wind sweep, ocean AM)
  double phase_ = 0.0;

  // Per-channel filter states
  float filter_state_[CHANNELS] = {0.0f, 0.0f};

  // Brown noise integrator state per channel
  float brown_state_[CHANNELS] = {0.0f, 0.0f};

  // Rain grain state
  struct RainGrain {
    int samples_remaining = 0; // samples left in this grain
    int grain_length = 0;      // total length of grain in samples
    float filter_state = 0.0f;
  };
  RainGrain rain_grains_[CHANNELS];

  void process_rain(int16_t *buffer, int num_samples, float intensity);
  void process_wind(int16_t *buffer, int num_samples, float intensity);
  void process_ocean(int16_t *buffer, int num_samples, float intensity);
};

} // namespace snora
