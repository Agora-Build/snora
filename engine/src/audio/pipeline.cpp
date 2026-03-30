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

  slope_smoother_.setImmediate(-4.0f);
  am_smoother_.setImmediate(0.23f);
  binaural_smoother_.setImmediate(4.0f);
  nature_smoother_.setImmediate(0.4f);
  volume_smoother_.setImmediate(config.volume);

  if (!config.assets_path.empty()) {
    nature_player_.load(config.assets_path, config.soundscape);
  }

  return true;
}

void AudioPipeline::processFrame(int16_t *output, const AudioParams &params) {
  // ------------------------------------------------------------------ //
  // Step 1: update and advance smoothers
  // ------------------------------------------------------------------ //
  slope_smoother_.setTarget(params.spectral_slope);
  am_smoother_.setTarget(params.am_frequency);
  binaural_smoother_.setTarget(params.binaural_hz);
  nature_smoother_.setTarget(params.nature_gain);
  volume_smoother_.setTarget(params.master_volume);

  float slope = slope_smoother_.smooth();
  float am_freq = am_smoother_.smooth();
  float bin_hz = binaural_smoother_.smooth();
  float nature_gain = nature_smoother_.smooth();
  float master_vol = volume_smoother_.smooth();

  // ------------------------------------------------------------------ //
  // Step 2: generate noise bed — louder when stressed, softer when calm
  // ------------------------------------------------------------------ //
  static int16_t noise_buf[FRAME_SAMPLES];
  float stress_norm = std::clamp(slope / -6.0f, 0.0f, 1.0f);
  float noise_amp = 0.08f + 0.25f * stress_norm;
  noise_gen_.generate(noise_buf, slope, noise_amp);

  // ------------------------------------------------------------------ //
  // Step 3: generate binaural tones
  // ------------------------------------------------------------------ //
  static int16_t binaural_buf[FRAME_SAMPLES];
  std::memset(binaural_buf, 0, sizeof(binaural_buf));
  if (params.binaural_enabled) {
    binaural_.process(binaural_buf, FRAME_SAMPLES, params.binaural_carrier,
                      bin_hz, 0.25f);
  }

  // ------------------------------------------------------------------ //
  // Step 4: nature player (WAV loops from asset files)
  // ------------------------------------------------------------------ //
  static int16_t nature_buf[FRAME_SAMPLES];
  std::memset(nature_buf, 0, sizeof(nature_buf));
  if (nature_player_.isLoaded()) {
    nature_player_.render(nature_buf, nature_gain);
  }

  // ------------------------------------------------------------------ //
  // Step 5: procedural texture (rain/wind/ocean)
  // ------------------------------------------------------------------ //
  static int16_t proc_buf[FRAME_SAMPLES];
  std::memset(proc_buf, 0, sizeof(proc_buf));

  // Texture intensity varies with slope: calm = louder, stressed = quieter
  float tex_intensity =
      0.5f + 0.5f * (1.0f - std::clamp(slope / -6.0f, 0.0f, 1.0f));

  if (soundscape_ == "rain") {
    rain_.process(proc_buf, FRAME_SAMPLES, tex_intensity);
  } else if (soundscape_ == "wind") {
    wind_.process(proc_buf, FRAME_SAMPLES, tex_intensity);
  } else if (soundscape_ == "ocean") {
    ocean_.process(proc_buf, FRAME_SAMPLES, tex_intensity);
  }

  // ------------------------------------------------------------------ //
  // Step 6: mix all layers
  // ------------------------------------------------------------------ //
  mixer_.mix(
      {
          {noise_buf, 0.15f},
          {binaural_buf, 0.12f},
          {proc_buf, 0.8f},
          {nature_buf, 0.5f},
      },
      output, FRAME_SAMPLES, master_vol);

  // ------------------------------------------------------------------ //
  // Step 7: apply spectral tilt to the FULL mix
  //   Bio data drives this: high stress = steeper slope = darker/warmer
  //   low stress = flatter = brighter. Applied to entire output so the
  //   ocean/rain/wind textures also shift with mood.
  // ------------------------------------------------------------------ //
  spectral_tilt_.process(output, FRAME_SAMPLES, slope);

  // ------------------------------------------------------------------ //
  // Step 8: apply amplitude modulation to the FULL mix
  //   Respiration entrainment: the entire soundscape gently pulses at
  //   the user's breathing rate, gradually slowing toward 5.5 bpm.
  // ------------------------------------------------------------------ //
  amplitude_mod_.process(output, FRAME_SAMPLES, am_freq);
}

} // namespace snora
