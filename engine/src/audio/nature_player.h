#pragma once

#include "audio/audio_format.h"
#include <cstdint>
#include <string>
#include <vector>

namespace snora {

struct SoundLayer {
  std::string file;
  float default_gain;
  bool loop;
  std::vector<int16_t> pcm_data; // stereo interleaved, 48kHz, 16-bit
  size_t position = 0;           // current playback position (in samples)
};

class NaturePlayer {
public:
  // Load manifest.json and all WAV files for the given soundscape
  bool load(const std::string &assets_path, const std::string &soundscape);

  // Fill output buffer with the next frame of nature sounds (mixed layers)
  void render(int16_t *output, float gain);

  bool isLoaded() const { return !layers_.empty(); }
  const std::string &error() const { return error_; }

private:
  std::vector<SoundLayer> layers_;
  std::string error_;

  bool loadWav(const std::string &path, SoundLayer &layer);
};

} // namespace snora
