#include <gtest/gtest.h>
#include "audio/amplitude_mod.h"
#include "audio/audio_format.h"
#include <cmath>

// Fill buffer with a constant value
static void fill_constant(int16_t* buf, int n, int16_t val) {
  for (int i = 0; i < n; ++i) buf[i] = val;
}

// Compute RMS of buffer
static double rms(const int16_t* buf, int n) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    double v = static_cast<double>(buf[i]);
    sum += v * v;
  }
  return (n > 0) ? std::sqrt(sum / n) : 0.0;
}

TEST(AmplitudeMod, ReducesAveragePower) {
  // The envelope varies between 0 and 1, so average power should be < input power.
  // Use a very low frequency (0.1 Hz) so the envelope cycles slowly and we can
  // measure many envelope states.
  constexpr int MANY_FRAMES = 500;  // 5 seconds worth
  constexpr float AM_FREQ = 0.1f;  // 0.1 Hz — one full cycle every 10 seconds

  snora::AmplitudeMod mod;

  double input_rms_total = 0.0;
  double output_rms_total = 0.0;

  for (int f = 0; f < MANY_FRAMES; ++f) {
    int16_t buf[snora::FRAME_SAMPLES];
    fill_constant(buf, snora::FRAME_SAMPLES, 10000);
    input_rms_total += rms(buf, snora::FRAME_SAMPLES);
    mod.process(buf, snora::FRAME_SAMPLES, AM_FREQ);
    output_rms_total += rms(buf, snora::FRAME_SAMPLES);
  }

  // Output RMS should be less than input RMS because envelope < 1 on average
  EXPECT_LT(output_rms_total, input_rms_total);
}

TEST(AmplitudeMod, EnvelopeVariesOverTime) {
  // With a very low AM frequency, the envelope should produce varying output
  // levels over many frames (some frames louder, some quieter).
  constexpr float AM_FREQ = 0.5f;  // 0.5 Hz
  snora::AmplitudeMod mod;

  double min_frame_rms = 1e9;
  double max_frame_rms = 0.0;

  for (int f = 0; f < 200; ++f) {
    int16_t buf[snora::FRAME_SAMPLES];
    fill_constant(buf, snora::FRAME_SAMPLES, 10000);
    mod.process(buf, snora::FRAME_SAMPLES, AM_FREQ);
    double r = rms(buf, snora::FRAME_SAMPLES);
    if (r < min_frame_rms) min_frame_rms = r;
    if (r > max_frame_rms) max_frame_rms = r;
  }

  // Envelope depth maps [0,1] to [0.4,1.0], so max/min ratio ~2.5
  EXPECT_GT(max_frame_rms, min_frame_rms * 1.5)
      << "max=" << max_frame_rms << " min=" << min_frame_rms;
}

TEST(AmplitudeMod, NoClipping) {
  // Even at max amplitude input, output must stay within int16 range
  snora::AmplitudeMod mod;
  for (int f = 0; f < 100; ++f) {
    int16_t buf[snora::FRAME_SAMPLES];
    fill_constant(buf, snora::FRAME_SAMPLES, 32767);
    mod.process(buf, snora::FRAME_SAMPLES, 0.25f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buf[i], -32767) << "at frame " << f << " index " << i;
      EXPECT_LE(buf[i], 32767) << "at frame " << f << " index " << i;
    }
  }
}

TEST(AmplitudeMod, ZeroFrequencyDoesNotCrash) {
  // Edge case: 0 Hz frequency (phase never advances)
  snora::AmplitudeMod mod;
  int16_t buf[snora::FRAME_SAMPLES];
  fill_constant(buf, snora::FRAME_SAMPLES, 1000);
  // Should not crash or produce NaN
  mod.process(buf, snora::FRAME_SAMPLES, 0.0f);
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_GE(buf[i], -32767);
    EXPECT_LE(buf[i], 32767);
  }
}
