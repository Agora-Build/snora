#include <gtest/gtest.h>
#include "audio/binaural.h"
#include "audio/audio_format.h"
#include <cmath>
#include <vector>

// Measure zero-crossing rate for a single channel from a stereo buffer.
// Returns crossings per second.
static double zero_crossing_rate(const int16_t* buf, int num_samples, int ch) {
  int crossings = 0;
  int frames = num_samples / snora::CHANNELS;
  for (int i = 1; i < frames; ++i) {
    int16_t prev = buf[(i - 1) * snora::CHANNELS + ch];
    int16_t curr = buf[i * snora::CHANNELS + ch];
    if ((prev < 0 && curr >= 0) || (prev >= 0 && curr < 0)) {
      ++crossings;
    }
  }
  // Each sine cycle produces 2 zero crossings.
  // Duration = frames / SAMPLE_RATE seconds.
  double duration_s = static_cast<double>(frames) / static_cast<double>(snora::SAMPLE_RATE);
  return static_cast<double>(crossings) / (2.0 * duration_s);
}

TEST(BinauralGenerator, CorrectFrequenciesViaZeroCrossings) {
  // carrier=200 Hz, binaural_hz=4 -> L=198 Hz, R=202 Hz
  // Use enough samples for a reliable measurement.
  // 4096 frames = ~85ms — enough for ~17 cycles at 198/202 Hz.
  constexpr int N_FRAMES = 4096;
  constexpr int N_SAMPLES = N_FRAMES * snora::CHANNELS;

  int16_t buf[N_SAMPLES] = {};  // start with silence (zero buffer)

  snora::BinauralGenerator gen;
  gen.process(buf, N_SAMPLES, 200.0f, 4.0f, 0.9f);

  double freq_l = zero_crossing_rate(buf, N_SAMPLES, 0);
  double freq_r = zero_crossing_rate(buf, N_SAMPLES, 1);

  // Allow ±5% tolerance
  EXPECT_NEAR(freq_l, 198.0, 198.0 * 0.05) << "Left channel frequency";
  EXPECT_NEAR(freq_r, 202.0, 202.0 * 0.05) << "Right channel frequency";
}

TEST(BinauralGenerator, ZeroGainLeavesBufferUnchanged) {
  int16_t buf[snora::FRAME_SAMPLES];
  int16_t ref[snora::FRAME_SAMPLES];
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) buf[i] = ref[i] = static_cast<int16_t>(i % 1000);

  snora::BinauralGenerator gen;
  gen.process(buf, snora::FRAME_SAMPLES, 200.0f, 4.0f, 0.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(buf[i], ref[i]) << "at index " << i;
  }
}

TEST(BinauralGenerator, AddsToExistingBuffer) {
  // Start with a known constant buffer; after processing the tone should be additive
  constexpr int16_t INITIAL = 1000;
  int16_t buf[snora::FRAME_SAMPLES];
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) buf[i] = INITIAL;

  snora::BinauralGenerator gen;
  gen.process(buf, snora::FRAME_SAMPLES, 200.0f, 4.0f, 0.1f);

  // At least some samples should differ from the initial value (the sine was added)
  bool any_changed = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buf[i] != INITIAL) { any_changed = true; break; }
  }
  EXPECT_TRUE(any_changed);
}

TEST(BinauralGenerator, NoClipping) {
  // Max amplitude input + max gain should still stay in int16 range
  int16_t buf[snora::FRAME_SAMPLES];
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) buf[i] = 0;

  snora::BinauralGenerator gen;
  for (int f = 0; f < 100; ++f) {
    gen.process(buf, snora::FRAME_SAMPLES, 200.0f, 4.0f, 1.0f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buf[i], -32767) << "at frame " << f << " index " << i;
      EXPECT_LE(buf[i], 32767) << "at frame " << f << " index " << i;
    }
    // Reset buffer each frame to avoid accumulation
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) buf[i] = 0;
  }
}

TEST(BinauralGenerator, LeftAndRightDifferAfterProcessing) {
  // With non-zero binaural_hz, L and R channels should differ
  int16_t buf[snora::FRAME_SAMPLES] = {};
  snora::BinauralGenerator gen;
  gen.process(buf, snora::FRAME_SAMPLES, 200.0f, 4.0f, 0.5f);

  bool differ = false;
  int frames = snora::FRAME_SAMPLES / snora::CHANNELS;
  for (int i = 0; i < frames; ++i) {
    if (buf[i * 2] != buf[i * 2 + 1]) { differ = true; break; }
  }
  EXPECT_TRUE(differ);
}
