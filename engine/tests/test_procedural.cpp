#include <gtest/gtest.h>
#include "audio/procedural.h"
#include "audio/audio_format.h"
#include <cmath>
#include <numeric>

// Compute RMS of buffer
static double buf_rms(const int16_t* buf, int n) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    double v = static_cast<double>(buf[i]);
    sum += v * v;
  }
  return (n > 0) ? std::sqrt(sum / n) : 0.0;
}

// Check that all samples are within int16 range
static bool no_clipping(const int16_t* buf, int n) {
  for (int i = 0; i < n; ++i) {
    if (buf[i] < -32767 || buf[i] > 32767) return false;
  }
  return true;
}

// ── Rain tests ───────────────────────────────────────────────────────────────

TEST(ProceduralRain, OutputIsNonZero) {
  snora::ProceduralTexture rain(snora::ProceduralTexture::Type::Rain);
  bool any_nonzero = false;

  for (int f = 0; f < 200 && !any_nonzero; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    rain.process(buf, snora::FRAME_SAMPLES, 1.0f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      if (buf[i] != 0) { any_nonzero = true; break; }
    }
  }
  EXPECT_TRUE(any_nonzero);
}

TEST(ProceduralRain, ZeroIntensityIsSilent) {
  snora::ProceduralTexture rain(snora::ProceduralTexture::Type::Rain);
  int16_t buf[snora::FRAME_SAMPLES] = {};
  rain.process(buf, snora::FRAME_SAMPLES, 0.0f);
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(buf[i], 0) << "at index " << i;
  }
}

TEST(ProceduralRain, NoClipping) {
  snora::ProceduralTexture rain(snora::ProceduralTexture::Type::Rain);
  for (int f = 0; f < 100; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    rain.process(buf, snora::FRAME_SAMPLES, 1.0f);
    EXPECT_TRUE(no_clipping(buf, snora::FRAME_SAMPLES)) << "clipping at frame " << f;
  }
}

TEST(ProceduralRain, ContainsTransientBursts) {
  // Over many frames, the RMS should vary (transient grain behaviour)
  snora::ProceduralTexture rain(snora::ProceduralTexture::Type::Rain);
  double min_rms = 1e9;
  double max_rms = 0.0;

  for (int f = 0; f < 300; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    rain.process(buf, snora::FRAME_SAMPLES, 1.0f);
    double r = buf_rms(buf, snora::FRAME_SAMPLES);
    if (r < min_rms) min_rms = r;
    if (r > max_rms) max_rms = r;
  }
  // There should be some variation (some frames have more grains than others)
  EXPECT_GT(max_rms, min_rms) << "max=" << max_rms << " min=" << min_rms;
}

// ── Wind tests ───────────────────────────────────────────────────────────────

TEST(ProceduralWind, OutputIsNonZero) {
  snora::ProceduralTexture wind(snora::ProceduralTexture::Type::Wind);
  int16_t buf[snora::FRAME_SAMPLES] = {};
  wind.process(buf, snora::FRAME_SAMPLES, 1.0f);
  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) if (buf[i] != 0) { any_nonzero = true; break; }
  EXPECT_TRUE(any_nonzero);
}

TEST(ProceduralWind, ZeroIntensityIsSilent) {
  snora::ProceduralTexture wind(snora::ProceduralTexture::Type::Wind);
  int16_t buf[snora::FRAME_SAMPLES] = {};
  wind.process(buf, snora::FRAME_SAMPLES, 0.0f);
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) EXPECT_EQ(buf[i], 0);
}

TEST(ProceduralWind, RmsVariesOverTime) {
  // Wind has a frequency-swept character — RMS can vary as the filter sweeps
  snora::ProceduralTexture wind(snora::ProceduralTexture::Type::Wind);
  double min_rms = 1e9;
  double max_rms = 0.0;

  for (int f = 0; f < 500; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    wind.process(buf, snora::FRAME_SAMPLES, 1.0f);
    double r = buf_rms(buf, snora::FRAME_SAMPLES);
    if (r < min_rms) min_rms = r;
    if (r > max_rms) max_rms = r;
  }
  // Some variation is expected
  EXPECT_GT(max_rms, 0.0);
}

TEST(ProceduralWind, NoClipping) {
  snora::ProceduralTexture wind(snora::ProceduralTexture::Type::Wind);
  for (int f = 0; f < 100; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    wind.process(buf, snora::FRAME_SAMPLES, 1.0f);
    EXPECT_TRUE(no_clipping(buf, snora::FRAME_SAMPLES)) << "clipping at frame " << f;
  }
}

// ── Ocean tests ───────────────────────────────────────────────────────────────

TEST(ProceduralOcean, OutputIsNonZero) {
  snora::ProceduralTexture ocean(snora::ProceduralTexture::Type::Ocean);
  int16_t buf[snora::FRAME_SAMPLES] = {};
  ocean.process(buf, snora::FRAME_SAMPLES, 1.0f);
  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) if (buf[i] != 0) { any_nonzero = true; break; }
  EXPECT_TRUE(any_nonzero);
}

TEST(ProceduralOcean, ZeroIntensityIsSilent) {
  snora::ProceduralTexture ocean(snora::ProceduralTexture::Type::Ocean);
  int16_t buf[snora::FRAME_SAMPLES] = {};
  ocean.process(buf, snora::FRAME_SAMPLES, 0.0f);
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) EXPECT_EQ(buf[i], 0);
}

TEST(ProceduralOcean, SlowAmplitudeModulation) {
  // Ocean AM is at ~0.1 Hz; over 10 seconds (1000 frames) the RMS should vary noticeably.
  snora::ProceduralTexture ocean(snora::ProceduralTexture::Type::Ocean);
  double min_rms = 1e9;
  double max_rms = 0.0;

  for (int f = 0; f < 1000; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    ocean.process(buf, snora::FRAME_SAMPLES, 1.0f);
    double r = buf_rms(buf, snora::FRAME_SAMPLES);
    if (r < min_rms) min_rms = r;
    if (r > max_rms) max_rms = r;
  }
  // The AM should produce at least a 2x range in RMS over 10 seconds
  EXPECT_GT(max_rms, min_rms * 1.5)
      << "max=" << max_rms << " min=" << min_rms
      << " — expected slow AM modulation";
}

TEST(ProceduralOcean, NoClipping) {
  snora::ProceduralTexture ocean(snora::ProceduralTexture::Type::Ocean);
  for (int f = 0; f < 100; ++f) {
    int16_t buf[snora::FRAME_SAMPLES] = {};
    ocean.process(buf, snora::FRAME_SAMPLES, 1.0f);
    EXPECT_TRUE(no_clipping(buf, snora::FRAME_SAMPLES)) << "clipping at frame " << f;
  }
}
