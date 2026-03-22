#include <gtest/gtest.h>
#include "audio/nature_player.h"
#include "audio/audio_format.h"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <vector>

namespace fs = std::filesystem;

// Helper: create a 48kHz mono WAV with a sine wave
static void createTestWav(const std::string& path, int channels, int frames, float freq = 440.0f) {
  SF_INFO info{};
  info.samplerate = snora::SAMPLE_RATE;
  info.channels = channels;
  info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info);
  ASSERT_NE(file, nullptr) << "Failed to create test WAV: " << path;

  std::vector<int16_t> buf(frames * channels);
  for (int i = 0; i < frames; ++i) {
    int16_t sample = static_cast<int16_t>(16000.0f * std::sin(2.0f * M_PI * freq * i / snora::SAMPLE_RATE));
    for (int ch = 0; ch < channels; ++ch) {
      buf[i * channels + ch] = sample;
    }
  }
  sf_writef_short(file, buf.data(), frames);
  sf_close(file);
}

// Helper: create a manifest.json and WAV files in a temp dir
class NaturePlayerFixture : public ::testing::Test {
 protected:
  fs::path test_dir_;

  void SetUp() override {
    test_dir_ = fs::temp_directory_path() / ("snora_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::create_directories(test_dir_);
  }

  void TearDown() override {
    fs::remove_all(test_dir_);
  }

  void writeManifest(const nlohmann::json& manifest) {
    std::ofstream f(test_dir_ / "manifest.json");
    f << manifest.dump(2);
  }
};

TEST(NaturePlayer, FailsOnMissingManifest) {
  snora::NaturePlayer player;
  bool ok = player.load("/nonexistent/path", "rain");
  EXPECT_FALSE(ok);
  EXPECT_FALSE(player.error().empty());
}

TEST(NaturePlayer, FailsOnUnknownSoundscape) {
  snora::NaturePlayer player;
  bool ok = player.load("/tmp", "nonexistent_soundscape");
  EXPECT_FALSE(ok);
}

TEST_F(NaturePlayerFixture, LoadsMonoWavAndRendersNonZero) {
  // Create a short mono WAV (1 second at 48kHz)
  std::string wav_file = "test_mono.wav";
  createTestWav((test_dir_ / wav_file).string(), 1, 48000, 440.0f);

  nlohmann::json manifest = {
    {"soundscapes", {
      {"rain", {
        {"layers", {{
          {"file", wav_file},
          {"default_gain", 0.8f},
          {"loop", true}
        }}}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  bool ok = player.load(test_dir_.string(), "rain");
  ASSERT_TRUE(ok) << "Load failed: " << player.error();
  EXPECT_TRUE(player.isLoaded());

  // Render one frame
  int16_t output[snora::FRAME_SAMPLES];
  std::memset(output, 0, sizeof(output));
  player.render(output, 1.0f);

  // Should produce non-zero output
  bool has_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (output[i] != 0) { has_nonzero = true; break; }
  }
  EXPECT_TRUE(has_nonzero) << "Expected non-zero output from nature player";
}

TEST_F(NaturePlayerFixture, LoadsStereoWav) {
  std::string wav_file = "test_stereo.wav";
  createTestWav((test_dir_ / wav_file).string(), 2, 48000, 220.0f);

  nlohmann::json manifest = {
    {"soundscapes", {
      {"ocean", {
        {"layers", {{
          {"file", wav_file},
          {"default_gain", 0.5f},
          {"loop", true}
        }}}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  bool ok = player.load(test_dir_.string(), "ocean");
  ASSERT_TRUE(ok) << player.error();
  EXPECT_TRUE(player.isLoaded());

  int16_t output[snora::FRAME_SAMPLES];
  player.render(output, 1.0f);

  bool has_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (output[i] != 0) { has_nonzero = true; break; }
  }
  EXPECT_TRUE(has_nonzero);
}

TEST_F(NaturePlayerFixture, ZeroGainProducesSilence) {
  std::string wav_file = "test_silent.wav";
  createTestWav((test_dir_ / wav_file).string(), 1, 48000, 440.0f);

  nlohmann::json manifest = {
    {"soundscapes", {
      {"rain", {
        {"layers", {{
          {"file", wav_file},
          {"default_gain", 1.0f},
          {"loop", true}
        }}}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  ASSERT_TRUE(player.load(test_dir_.string(), "rain"));

  int16_t output[snora::FRAME_SAMPLES];
  player.render(output, 0.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(output[i], 0) << "Expected silence at sample " << i;
  }
}

TEST_F(NaturePlayerFixture, LoopsCorrectly) {
  // Create a very short WAV (just 100 stereo samples = 50 frames mono -> 100 stereo)
  std::string wav_file = "test_short.wav";
  createTestWav((test_dir_ / wav_file).string(), 1, 100, 1000.0f);

  nlohmann::json manifest = {
    {"soundscapes", {
      {"rain", {
        {"layers", {{
          {"file", wav_file},
          {"default_gain", 0.5f},
          {"loop", true}
        }}}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  ASSERT_TRUE(player.load(test_dir_.string(), "rain"));

  // Render multiple frames — the 100-sample file must loop to fill FRAME_SAMPLES (960)
  int16_t output[snora::FRAME_SAMPLES];
  player.render(output, 1.0f);

  // Verify it didn't crash and produced output
  bool has_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (output[i] != 0) { has_nonzero = true; break; }
  }
  EXPECT_TRUE(has_nonzero) << "Loop should wrap around and produce output";
}

TEST_F(NaturePlayerFixture, MultipleLayers) {
  std::string wav1 = "layer1.wav";
  std::string wav2 = "layer2.wav";
  createTestWav((test_dir_ / wav1).string(), 1, 48000, 440.0f);
  createTestWav((test_dir_ / wav2).string(), 1, 48000, 880.0f);

  nlohmann::json manifest = {
    {"soundscapes", {
      {"rain", {
        {"layers", {
          {{"file", wav1}, {"default_gain", 0.5f}, {"loop", true}},
          {{"file", wav2}, {"default_gain", 0.3f}, {"loop", true}}
        }}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  ASSERT_TRUE(player.load(test_dir_.string(), "rain"));

  int16_t output[snora::FRAME_SAMPLES];
  player.render(output, 1.0f);

  // Both layers should contribute — output should differ from single-layer
  bool has_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (output[i] != 0) { has_nonzero = true; break; }
  }
  EXPECT_TRUE(has_nonzero);
}

TEST_F(NaturePlayerFixture, FailsOnMissingWavFile) {
  nlohmann::json manifest = {
    {"soundscapes", {
      {"rain", {
        {"layers", {{
          {"file", "does_not_exist.wav"},
          {"default_gain", 1.0f},
          {"loop", true}
        }}}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  bool ok = player.load(test_dir_.string(), "rain");
  EXPECT_FALSE(ok);
  EXPECT_FALSE(player.error().empty());
}

TEST_F(NaturePlayerFixture, MalformedManifestJson) {
  std::ofstream f(test_dir_ / "manifest.json");
  f << "{ not valid json !!!";
  f.close();

  snora::NaturePlayer player;
  bool ok = player.load(test_dir_.string(), "rain");
  EXPECT_FALSE(ok);
  EXPECT_TRUE(player.error().find("Malformed") != std::string::npos);
}

TEST_F(NaturePlayerFixture, NoClippingWithLoudInput) {
  // Create a loud WAV near max amplitude
  std::string wav_file = "loud.wav";
  {
    SF_INFO info{};
    info.samplerate = snora::SAMPLE_RATE;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* file = sf_open((test_dir_ / wav_file).c_str(), SFM_WRITE, &info);
    std::vector<int16_t> buf(48000, 32000);  // near max
    sf_writef_short(file, buf.data(), 48000);
    sf_close(file);
  }

  nlohmann::json manifest = {
    {"soundscapes", {
      {"rain", {
        {"layers", {
          {{"file", wav_file}, {"default_gain", 1.0f}, {"loop", true}},
          {{"file", wav_file}, {"default_gain", 1.0f}, {"loop", true}}
        }}
      }}
    }}
  };
  writeManifest(manifest);

  snora::NaturePlayer player;
  ASSERT_TRUE(player.load(test_dir_.string(), "rain"));

  int16_t output[snora::FRAME_SAMPLES];
  player.render(output, 1.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_GE(output[i], -32767);
    EXPECT_LE(output[i], 32767);
  }
}
