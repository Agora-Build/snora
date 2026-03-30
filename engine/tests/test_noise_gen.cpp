#include <gtest/gtest.h>
#include "audio/noise_gen.h"
#include <cmath>
#include <numeric>

TEST(NoiseGen, OutputNonZero) {
  snora::NoiseGenerator gen;
  int16_t buffer[snora::FRAME_SAMPLES] = {};
  gen.generate(buffer, -3.0f, 0.5f);

  // Check that output is not all zeros
  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buffer[i] != 0) { any_nonzero = true; break; }
  }
  EXPECT_TRUE(any_nonzero);
}

TEST(NoiseGen, MeanNearZero) {
  snora::NoiseGenerator gen;
  int16_t buffer[snora::FRAME_SAMPLES];

  // Generate many frames and compute mean
  double sum = 0;
  int total_samples = 0;
  for (int f = 0; f < 500; ++f) {
    gen.generate(buffer, -3.0f, 0.5f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      sum += buffer[i];
      total_samples++;
    }
  }
  double mean = sum / total_samples;
  // Mean should be near 0 (pink noise can drift, allow generous tolerance)
  EXPECT_NEAR(mean, 0.0, 800.0);
}

TEST(NoiseGen, AmplitudeScaling) {
  snora::NoiseGenerator gen;
  int16_t loud[snora::FRAME_SAMPLES];
  int16_t quiet[snora::FRAME_SAMPLES];

  gen.generate(loud, -3.0f, 1.0f);
  gen.generate(quiet, -3.0f, 0.1f);

  // RMS of loud should be > RMS of quiet
  auto rms = [](const int16_t* buf, int n) -> double {
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += static_cast<double>(buf[i]) * buf[i];
    return std::sqrt(sum / n);
  };

  EXPECT_GT(rms(loud, snora::FRAME_SAMPLES), rms(quiet, snora::FRAME_SAMPLES));
}

TEST(NoiseGen, NoClipping) {
  snora::NoiseGenerator gen;
  int16_t buffer[snora::FRAME_SAMPLES];

  for (int f = 0; f < 100; ++f) {
    gen.generate(buffer, -3.0f, 1.0f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buffer[i], -32767);
      EXPECT_LE(buffer[i], 32767);
    }
  }
}
