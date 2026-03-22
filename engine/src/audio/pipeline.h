#pragma once

#include "audio/audio_format.h"
#include "audio/param_smoother.h"
#include "audio/noise_gen.h"
#include "audio/spectral_tilt.h"
#include "audio/amplitude_mod.h"
#include "audio/binaural.h"
#include "audio/procedural.h"
#include "audio/mixer.h"
#include "state/session_state.h"

#include <cstdint>

namespace snora {

// AudioPipeline orchestrates all audio components per frame.
// Call init() once with config, then processFrame() at 10ms intervals.
class AudioPipeline {
 public:
  AudioPipeline();

  // Initialize pipeline with session configuration.
  // Returns true on success; nature player failures are non-fatal.
  bool init(const SessionConfig& config);

  // Generate one frame of audio into output buffer (FRAME_SAMPLES int16_t).
  void processFrame(int16_t* output, const AudioParams& params);

 private:
  // Audio processors
  NoiseGenerator   noise_gen_;
  SpectralTilt     spectral_tilt_;
  AmplitudeMod     amplitude_mod_;
  BinauralGenerator binaural_;
  ProceduralTexture rain_;
  ProceduralTexture wind_;
  ProceduralTexture ocean_;
  Mixer            mixer_;

  // Parameter smoothers (time constants from spec)
  ParamSmoother slope_smoother_;    // 3.0s  — spectral tilt
  ParamSmoother am_smoother_;       // 0.05s — AM frequency
  ParamSmoother binaural_smoother_; // 10.0s — binaural beat hz
  ParamSmoother nature_smoother_;   // 15.0s — nature crossfade (reserved)
  ParamSmoother volume_smoother_;   // 0.05s — master volume

  // Session config (soundscape type selects procedural texture)
  std::string soundscape_;
};

}  // namespace snora
