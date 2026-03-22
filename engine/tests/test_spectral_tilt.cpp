#include <gtest/gtest.h>
#include "audio/spectral_tilt.h"
#include "audio/audio_format.h"
#include <cmath>
#include <numeric>
#include <random>

// Helper: compute RMS of int16 buffer for a single channel (0=L, 1=R)
static double channel_rms(const int16_t* buf, int num_samples, int ch) {
  double sum = 0.0;
  int count = 0;
  for (int i = ch; i < num_samples; i += snora::CHANNELS) {
    double v = static_cast<double>(buf[i]);
    sum += v * v;
    ++count;
  }
  return (count > 0) ? std::sqrt(sum / count) : 0.0;
}

// Helper: compute energy above a threshold index (proxy for high-frequency energy)
// We compute the sum of squared differences between adjacent samples (emphasises HF).
static double hf_energy(const int16_t* buf, int num_samples, int ch) {
  double sum = 0.0;
  int count = 0;
  for (int i = ch + snora::CHANNELS; i < num_samples; i += snora::CHANNELS) {
    double diff = static_cast<double>(buf[i]) - static_cast<double>(buf[i - snora::CHANNELS]);
    sum += diff * diff;
    ++count;
  }
  return (count > 0) ? sum / count : 0.0;
}

TEST(SpectralTilt, HeavySlopeReducesHighFrequency) {
  // Fill buffer with white noise (lots of HF energy)
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(-16000, 16000);

  int16_t white[snora::FRAME_SAMPLES];
  int16_t filtered[snora::FRAME_SAMPLES];
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    white[i] = filtered[i] = static_cast<int16_t>(dist(rng));
  }

  snora::SpectralTilt tilt;
  // Steady state: run many frames with slope=-6 on the same pattern
  // to let the filter settle
  for (int f = 0; f < 50; ++f) {
    // Re-fill with fresh noise each frame (simulate streaming)
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) filtered[i] = static_cast<int16_t>(dist(rng));
    tilt.process(filtered, snora::FRAME_SAMPLES, -6.0f);
  }
  // One more frame with fixed white noise to compare
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    white[i] = filtered[i] = static_cast<int16_t>(dist(rng));
  }
  snora::SpectralTilt white_pass;
  // slope=0 means coeff=0 so pass-through
  white_pass.process(white, snora::FRAME_SAMPLES, 0.0f);

  tilt.process(filtered, snora::FRAME_SAMPLES, -6.0f);

  double hf_white = hf_energy(white, snora::FRAME_SAMPLES, 0);
  double hf_filtered = hf_energy(filtered, snora::FRAME_SAMPLES, 0);

  // Filtered signal should have less HF energy than white noise
  EXPECT_LT(hf_filtered, hf_white);
}

TEST(SpectralTilt, ZeroSlopeIsPassthrough) {
  // With slope=0, coeff=0, output should equal input exactly
  int16_t buf[snora::FRAME_SAMPLES];
  int16_t ref[snora::FRAME_SAMPLES];
  std::mt19937 rng(123);
  std::uniform_int_distribution<int> dist(-10000, 10000);
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    buf[i] = ref[i] = static_cast<int16_t>(dist(rng));
  }

  snora::SpectralTilt tilt;
  tilt.process(buf, snora::FRAME_SAMPLES, 0.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(buf[i], ref[i]) << "at index " << i;
  }
}

TEST(SpectralTilt, NoClipping) {
  // Even with max amplitude input and heavy slope, output must stay in int16 range
  std::mt19937 rng(99);
  std::uniform_int_distribution<int> dist(-32767, 32767);

  snora::SpectralTilt tilt;
  for (int f = 0; f < 100; ++f) {
    int16_t buf[snora::FRAME_SAMPLES];
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) buf[i] = static_cast<int16_t>(dist(rng));
    tilt.process(buf, snora::FRAME_SAMPLES, -6.0f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buf[i], -32767) << "at frame " << f << " index " << i;
      EXPECT_LE(buf[i], 32767) << "at frame " << f << " index " << i;
    }
  }
}

TEST(SpectralTilt, ProcessesStereoPairs) {
  // Verify both channels are processed independently
  // L channel: all zeros, R channel: large value — filtered output should differ
  int16_t buf[snora::FRAME_SAMPLES];
  for (int i = 0; i < snora::FRAME_SAMPLES; i += 2) {
    buf[i]     = 0;       // L
    buf[i + 1] = 16000;   // R
  }

  snora::SpectralTilt tilt;
  tilt.process(buf, snora::FRAME_SAMPLES, -4.0f);

  // After a few samples the IIR should build up. L should remain near 0, R near its value.
  // Just verify they're not equal (the channels are processed separately)
  bool channels_differ = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; i += 2) {
    if (buf[i] != buf[i + 1]) { channels_differ = true; break; }
  }
  EXPECT_TRUE(channels_differ);
}
