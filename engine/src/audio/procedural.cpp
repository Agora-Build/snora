#include "audio/procedural.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace snora {

ProceduralTexture::ProceduralTexture(Type type)
    : type_(type), rng_(std::random_device{}()) {}

void ProceduralTexture::process(int16_t* buffer, int num_samples, float intensity) {
  switch (type_) {
    case Type::Rain:  process_rain (buffer, num_samples, intensity); break;
    case Type::Wind:  process_wind (buffer, num_samples, intensity); break;
    case Type::Ocean: process_ocean(buffer, num_samples, intensity); break;
  }
}

// ── Rain ──────────────────────────────────────────────────────────────────────
//
// Stochastic grain synthesis. Each "grain" is a short burst of filtered white
// noise (2-8 ms). A Poisson-like trigger decides whether to start a new grain
// each sample. Grain density (triggers per second) scales with intensity.

void ProceduralTexture::process_rain(int16_t* buffer, int num_samples, float intensity) {
  if (intensity <= 0.0f) return;

  std::uniform_real_distribution<float> white_dist(-1.0f, 1.0f);
  std::uniform_int_distribution<int>   len_dist(
      static_cast<int>(0.002f * SAMPLE_RATE),   // 2 ms
      static_cast<int>(0.008f * SAMPLE_RATE));  // 8 ms

  // Trigger probability per sample: scale 0→0.001, 1→0.01 triggers/sample
  // (roughly 48-480 grain starts per second at the two extremes)
  float trigger_prob = 0.001f + intensity * 0.009f;

  std::uniform_real_distribution<float> trigger_dist(0.0f, 1.0f);

  // Low-pass filter coefficient for grain envelope smoothing
  // Cutoff ~4 kHz: coeff ≈ 1 - 2*pi*fc/sr
  constexpr float LP_COEFF = 0.5f;

  int frames = num_samples / CHANNELS;

  for (int i = 0; i < frames; ++i) {
    for (int ch = 0; ch < CHANNELS; ++ch) {
      auto& g = rain_grains_[ch];

      // Trigger a new grain if none is active and the dice roll succeeds
      if (g.samples_remaining <= 0) {
        if (trigger_dist(rng_) < trigger_prob) {
          g.grain_length      = len_dist(rng_);
          g.samples_remaining = g.grain_length;
          g.filter_state      = 0.0f;
        }
      }

      float sample = 0.0f;
      if (g.samples_remaining > 0) {
        float white = white_dist(rng_);
        // One-pole low-pass to smooth the burst (simulates a raindrop's spectral shape)
        g.filter_state = LP_COEFF * g.filter_state + (1.0f - LP_COEFF) * white;

        // Hanning window for grain envelope (avoid clicks)
        float t = 1.0f - static_cast<float>(g.samples_remaining) / static_cast<float>(g.grain_length);
        float window = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));

        sample = g.filter_state * window * intensity;
        --g.samples_remaining;
      }

      int idx = i * CHANNELS + ch;
      float mixed = static_cast<float>(buffer[idx]) + sample * 32767.0f;
      buffer[idx] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, mixed)));
    }
  }
}

// ── Wind ──────────────────────────────────────────────────────────────────────
//
// White noise fed through a swept bandpass filter. Center frequency is
// modulated by a slow sine LFO (0.1-0.3 Hz). We approximate the bandpass with
// two cascaded one-pole filters (highpass + lowpass).

void ProceduralTexture::process_wind(int16_t* buffer, int num_samples, float intensity) {
  if (intensity <= 0.0f) return;

  std::uniform_real_distribution<float> white_dist(-1.0f, 1.0f);

  // LFO sweeps center freq between ~200 Hz and ~2000 Hz
  constexpr double LFO_FREQ = 0.15;  // Hz — slow sweep
  double phase_inc = LFO_FREQ / static_cast<double>(SAMPLE_RATE);

  int frames = num_samples / CHANNELS;

  for (int i = 0; i < frames; ++i) {
    // Modulate center frequency with LFO
    double lfo = std::sin(2.0 * M_PI * phase_);
    // Map [-1,1] to [300, 2000] Hz logarithmically via linear mix
    float center_hz = 300.0f + static_cast<float>((lfo + 1.0) * 0.5) * 1700.0f;

    // Simple one-pole LP at center_hz (acts as a broad lowpass — approximation)
    float omega = 2.0f * static_cast<float>(M_PI) * center_hz / static_cast<float>(SAMPLE_RATE);
    float lp_coeff = 1.0f - omega;  // crude but works for modest frequencies
    lp_coeff = std::max(0.0f, std::min(0.99f, lp_coeff));

    phase_ += phase_inc;
    if (phase_ >= 1.0) phase_ -= 1.0;

    for (int ch = 0; ch < CHANNELS; ++ch) {
      float white = white_dist(rng_);
      // One-pole LP
      filter_state_[ch] = lp_coeff * filter_state_[ch] + (1.0f - lp_coeff) * white;
      float sample = filter_state_[ch] * intensity;

      int idx = i * CHANNELS + ch;
      float mixed = static_cast<float>(buffer[idx]) + sample * 32767.0f;
      buffer[idx] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, mixed)));
    }
  }
}

// ── Ocean ─────────────────────────────────────────────────────────────────────
//
// Brown (red) noise with slow amplitude modulation at wave rhythm rate (0.05-0.15 Hz).
// Brown noise is produced by integrating white noise with a leaky integrator.

void ProceduralTexture::process_ocean(int16_t* buffer, int num_samples, float intensity) {
  if (intensity <= 0.0f) return;

  std::uniform_real_distribution<float> white_dist(-1.0f, 1.0f);

  // Wave AM rate: start at 0.1 Hz (middle of 0.05-0.15 Hz range)
  // We modulate slightly with LFO to add variation
  constexpr double WAVE_RATE_BASE = 0.1;   // Hz
  constexpr double WAVE_LFO_FREQ  = 0.01;  // Hz — very slow modulation of wave rate
  double wave_phase_inc = WAVE_RATE_BASE / static_cast<double>(SAMPLE_RATE);
  double lfo_phase_inc  = WAVE_LFO_FREQ  / static_cast<double>(SAMPLE_RATE);

  int frames = num_samples / CHANNELS;

  for (int i = 0; i < frames; ++i) {
    // Slow LFO modulates wave amplitude envelope rate slightly
    double lfo = std::sin(2.0 * M_PI * phase_);
    // envelope: raised cosine at wave rate -> smooth AM [0.1, 1.0]
    // Use a second phase accumulator tracked within the LFO phase for simplicity:
    // We embed wave phase in phase_ by letting it tick at wave_rate.
    double wave_env = 0.5 * (1.0 + std::sin(2.0 * M_PI * phase_));
    // Make the minimum ~0.1 (ocean never goes fully silent)
    float envelope = 0.1f + 0.9f * static_cast<float>(wave_env);

    phase_ += wave_phase_inc + lfo * lfo_phase_inc * 0.5;
    if (phase_ >= 1.0) phase_ -= 1.0;

    for (int ch = 0; ch < CHANNELS; ++ch) {
      float white = white_dist(rng_);
      // Leaky integrator for brown noise
      brown_state_[ch] = brown_state_[ch] * 0.998f + white * 0.04f;

      float sample = brown_state_[ch] * envelope * intensity;

      int idx = i * CHANNELS + ch;
      float mixed = static_cast<float>(buffer[idx]) + sample * 32767.0f;
      buffer[idx] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, mixed)));
    }
  }
}

}  // namespace snora
