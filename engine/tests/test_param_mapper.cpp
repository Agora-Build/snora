#include <gtest/gtest.h>
#include "state/param_mapper.h"

TEST(ParamMapper, SpectralTiltHighStress) {
  snora::PhysioState physio;
  physio.stress_level = 1.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.spectral_slope, -6.0f, 0.01f);
}

TEST(ParamMapper, SpectralTiltLowStress) {
  snora::PhysioState physio;
  physio.stress_level = 0.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.spectral_slope, -2.0f, 0.01f);
}

TEST(ParamMapper, RespirationEntrainmentAtStart) {
  snora::PhysioState physio;
  physio.respiration_rate = 18.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  // At start (progress=0), AM freq = respiration_rate / 60
  EXPECT_NEAR(params.am_frequency, 18.0f / 60.0f, 0.01f);
}

TEST(ParamMapper, RespirationEntrainmentAfter20Min) {
  snora::PhysioState physio;
  physio.respiration_rate = 18.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 20.0f);
  // At 20min (progress=1), AM freq = 5.5 / 60
  EXPECT_NEAR(params.am_frequency, 5.5f / 60.0f, 0.01f);
}

TEST(ParamMapper, BinauralAnxious) {
  snora::PhysioState physio;
  physio.mood = "anxious";
  physio.stress_level = 0.5f;
  snora::SessionConfig config;
  config.binaural_beats = true;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_GE(params.binaural_hz, 8.0f);
  EXPECT_LE(params.binaural_hz, 12.0f);
  EXPECT_TRUE(params.binaural_enabled);
}

TEST(ParamMapper, BinauralSleepy) {
  snora::PhysioState physio;
  physio.mood = "sleepy";
  physio.stress_level = 0.5f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_GE(params.binaural_hz, 0.5f);
  EXPECT_LE(params.binaural_hz, 4.0f);
}

TEST(ParamMapper, NatureGainHighStress) {
  snora::PhysioState physio;
  physio.stress_level = 1.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.nature_gain, 0.6f, 0.01f);
}

TEST(ParamMapper, NatureGainLowStress) {
  snora::PhysioState physio;
  physio.stress_level = 0.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.nature_gain, 0.2f, 0.01f);
}
