#include "audio/pipeline.h"

#include <algorithm>
#include <cstring>

namespace snora {

AudioPipeline::AudioPipeline()
    : rain_(ProceduralTexture::Type::Rain),
      wind_(ProceduralTexture::Type::Wind),
      ocean_(ProceduralTexture::Type::Ocean), slope_smoother_(3.0f),
      am_smoother_(0.05f), binaural_smoother_(10.0f), nature_smoother_(15.0f),
      volume_smoother_(0.05f) {}

bool AudioPipeline::init(const SessionConfig &config) {
  soundscape_ = config.soundscape;

  // Set initial smoother values to sensible defaults so there is no
  // large ramp from 0 on the very first frame.
  slope_smoother_.setImmediate(-4.0f);
  am_smoother_.setImmediate(0.23f);
  binaural_smoother_.setImmediate(4.0f);
  nature_smoother_.setImmediate(0.4f);
  volume_smoother_.setImmediate(config.volume);

  // Load nature sounds (non-fatal if assets unavailable)
  if (!config.assets_path.empty()) {
    nature_player_.load(config.assets_path, config.soundscape);
  }

  return true;
}

void AudioPipeline::processFrame(int16_t *output, const AudioParams &params) {
  // ------------------------------------------------------------------ //
  // Step 1: update smoother targets
  // ------------------------------------------------------------------ //
  slope_smoother_.setTarget(params.spectral_slope);
  am_smoother_.setTarget(params.am_frequency);
  binaural_smoother_.setTarget(params.binaural_hz);
  nature_smoother_.setTarget(params.nature_gain);
  volume_smoother_.setTarget(params.master_volume);

  // Step 2: advance smoothers to get values for this frame
  float slope = slope_smoother_.smooth();
  float am_freq = am_smoother_.smooth();
  float bin_hz = binaural_smoother_.smooth();
  float nature_gain = nature_smoother_.smooth();
  float master_vol = volume_smoother_.smooth();

  // ------------------------------------------------------------------ //
  // Step 3: generate noise buffer
  // ------------------------------------------------------------------ //
  static int16_t noise_buf[FRAME_SAMPLES];
  noise_gen_.generate(noise_buf, slope, 0.7f);

  // ------------------------------------------------------------------ //
  // Step 4: apply spectral tilt in-place
  // ------------------------------------------------------------------ //
  spectral_tilt_.process(noise_buf, FRAME_SAMPLES, slope);

  // ------------------------------------------------------------------ //
  // Step 5: apply amplitude modulation in-place
  // ------------------------------------------------------------------ //
  amplitude_mod_.process(noise_buf, FRAME_SAMPLES, am_freq);

  // ------------------------------------------------------------------ //
  // Step 6: generate binaural tones into a separate buffer and mix later
  // ------------------------------------------------------------------ //
  static int16_t binaural_buf[FRAME_SAMPLES];
  std::memset(binaural_buf, 0, sizeof(binaural_buf));
  if (params.binaural_enabled) {
    binaural_.process(binaural_buf, FRAME_SAMPLES, params.binaural_carrier,
                      bin_hz, 0.25f);
  }

  // ------------------------------------------------------------------ //
  // Step 7: nature player (WAV loops from asset files)
  // ------------------------------------------------------------------ //
  static int16_t nature_buf[FRAME_SAMPLES];
  std::memset(nature_buf, 0, sizeof(nature_buf));
  if (nature_player_.isLoaded()) {
    nature_player_.render(nature_buf, nature_gain);
  }

  // ------------------------------------------------------------------ //
  // Step 8: procedural texture based on soundscape selection
  // ------------------------------------------------------------------ //
  static int16_t proc_buf[FRAME_SAMPLES];
  std::memset(proc_buf, 0, sizeof(proc_buf));

  if (soundscape_ == "rain") {
    rain_.process(proc_buf, FRAME_SAMPLES, 0.6f);
  } else if (soundscape_ == "wind") {
    wind_.process(proc_buf, FRAME_SAMPLES, 0.6f);
  } else if (soundscape_ == "ocean") {
    ocean_.process(proc_buf, FRAME_SAMPLES, 0.6f);
  }

  // ------------------------------------------------------------------ //
  // Step 9: mix all layers with Mixer, apply master volume
  // ------------------------------------------------------------------ //
  mixer_.mix(
      {
          {noise_buf, 0.6f},
          {binaural_buf, 0.4f},
          {proc_buf, 0.4f},
          {nature_buf, 0.5f},
      },
      output, FRAME_SAMPLES, master_vol);
}

} // namespace snora
