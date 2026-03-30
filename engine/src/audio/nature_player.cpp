#include "audio/nature_player.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sndfile.h>

namespace snora {

bool NaturePlayer::load(const std::string &assets_path,
                        const std::string &soundscape) {
  // Read manifest
  std::string manifest_path = assets_path + "/manifest.json";
  std::ifstream f(manifest_path);
  if (!f.is_open()) {
    error_ = "Cannot open manifest: " + manifest_path;
    return false;
  }

  nlohmann::json manifest;
  try {
    manifest = nlohmann::json::parse(f);
  } catch (const std::exception &e) {
    error_ = "Malformed manifest: " + std::string(e.what());
    return false;
  }

  if (!manifest.contains("soundscapes") ||
      !manifest["soundscapes"].contains(soundscape)) {
    error_ = "Soundscape not found in manifest: " + soundscape;
    return false;
  }

  auto &sc = manifest["soundscapes"][soundscape];
  for (auto &layer_json : sc["layers"]) {
    SoundLayer layer;
    layer.file = layer_json["file"].get<std::string>();
    layer.default_gain = layer_json["default_gain"].get<float>();
    layer.loop = layer_json.value("loop", true);

    std::string wav_path = assets_path + "/" + layer.file;
    if (!loadWav(wav_path, layer)) {
      return false;
    }
    layers_.push_back(std::move(layer));
  }

  return true;
}

bool NaturePlayer::loadWav(const std::string &path, SoundLayer &layer) {
  SF_INFO info{};
  SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
  if (!file) {
    error_ = "Cannot open WAV: " + path;
    return false;
  }

  if (info.samplerate != SAMPLE_RATE) {
    error_ = "WAV sample rate must be 48000: " + path;
    sf_close(file);
    return false;
  }

  // Read all samples
  std::vector<int16_t> raw(info.frames * info.channels);
  sf_readf_short(file, raw.data(), info.frames);
  sf_close(file);

  // Upmix mono to stereo if needed
  if (info.channels == 1) {
    layer.pcm_data.resize(info.frames * 2);
    for (sf_count_t i = 0; i < info.frames; ++i) {
      layer.pcm_data[i * 2] = raw[i];
      layer.pcm_data[i * 2 + 1] = raw[i];
    }
  } else {
    layer.pcm_data = std::move(raw);
  }

  return true;
}

void NaturePlayer::render(int16_t *output, float gain) {
  // Zero output first
  std::fill(output, output + FRAME_SAMPLES, static_cast<int16_t>(0));

  for (auto &layer : layers_) {
    if (layer.pcm_data.empty())
      continue;

    float layer_gain = layer.default_gain * gain;
    size_t total_stereo_samples = layer.pcm_data.size();

    for (int i = 0; i < FRAME_SAMPLES; ++i) {
      float sample =
          static_cast<float>(layer.pcm_data[layer.position]) * layer_gain;
      float mixed = static_cast<float>(output[i]) + sample;
      output[i] = static_cast<int16_t>(std::clamp(mixed, -32767.0f, 32767.0f));

      layer.position++;
      if (layer.position >= total_stereo_samples) {
        if (layer.loop) {
          layer.position = 0;
        } else {
          break;
        }
      }
    }
  }
}

} // namespace snora
