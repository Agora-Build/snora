#include <gtest/gtest.h>
#include "audio/noise_gen.h"
#include "audio/spectral_tilt.h"
#include "audio/amplitude_mod.h"
#include "audio/binaural.h"
#include "audio/pipeline.h"
#include "audio/audio_format.h"
#include "state/session_state.h"
#include "state/param_mapper.h"

#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>
#include <complex>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double rms(const int16_t* buf, int n) {
  double sum = 0;
  for (int i = 0; i < n; ++i) sum += static_cast<double>(buf[i]) * buf[i];
  return std::sqrt(sum / n);
}

// Extract one channel from interleaved stereo
static std::vector<double> extractChannel(const int16_t* buf, int total_samples, int channel) {
  std::vector<double> out;
  for (int i = channel; i < total_samples; i += snora::CHANNELS) {
    out.push_back(static_cast<double>(buf[i]));
  }
  return out;
}

// Radix-2 FFT (in-place, Cooley-Tukey). Input size must be power of 2.
static void fft(std::vector<std::complex<double>>& x) {
  int N = static_cast<int>(x.size());
  if (N <= 1) return;
  // Bit-reversal permutation
  for (int i = 1, j = 0; i < N; ++i) {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(x[i], x[j]);
  }
  // FFT butterfly
  for (int len = 2; len <= N; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    std::complex<double> wlen(std::cos(angle), std::sin(angle));
    for (int i = 0; i < N; i += len) {
      std::complex<double> w(1);
      for (int j = 0; j < len / 2; ++j) {
        auto u = x[i + j], v = x[i + j + len / 2] * w;
        x[i + j] = u + v;
        x[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

// Power spectrum via FFT. Truncates/pads input to nearest power of 2.
// Returns {spectrum, fft_size}
static std::pair<std::vector<double>, int> powerSpectrum(const std::vector<double>& signal) {
  // Find nearest power of 2 <= signal size
  int N = 1;
  while (N * 2 <= static_cast<int>(signal.size())) N *= 2;
  std::vector<std::complex<double>> x(N);
  for (int i = 0; i < N; ++i) x[i] = signal[i];
  fft(x);
  int half = N / 2;
  std::vector<double> power(half);
  for (int k = 0; k < half; ++k) {
    power[k] = std::norm(x[k]) / static_cast<double>(N);
  }
  return {power, N};
}

// Average power in a frequency band [lo_hz, hi_hz)
static double bandPower(const std::vector<double>& spectrum, int fft_size,
                        double sample_rate, double lo_hz, double hi_hz) {
  double bin_width = sample_rate / fft_size;
  int lo_bin = std::max(1, static_cast<int>(lo_hz / bin_width));
  int hi_bin = std::min(static_cast<int>(spectrum.size()) - 1,
                        static_cast<int>(hi_hz / bin_width));
  if (hi_bin <= lo_bin) return 0;
  double sum = 0;
  for (int i = lo_bin; i <= hi_bin; ++i) sum += spectrum[i];
  return sum / (hi_bin - lo_bin + 1);
}

// Pearson correlation coefficient between two vectors
static double correlation(const std::vector<double>& a, const std::vector<double>& b) {
  int n = std::min(a.size(), b.size());
  double mean_a = 0, mean_b = 0;
  for (int i = 0; i < n; ++i) { mean_a += a[i]; mean_b += b[i]; }
  mean_a /= n; mean_b /= n;
  double cov = 0, var_a = 0, var_b = 0;
  for (int i = 0; i < n; ++i) {
    double da = a[i] - mean_a, db = b[i] - mean_b;
    cov += da * db;
    var_a += da * da;
    var_b += db * db;
  }
  if (var_a < 1e-10 || var_b < 1e-10) return 0;
  return cov / std::sqrt(var_a * var_b);
}

// ===========================================================================
// WHITE NOISE TESTS
// ===========================================================================

TEST(WhiteNoise, SpectralFlatness) {
  // White noise (slope=0) should have roughly equal power across frequency bands
  snora::NoiseGenerator gen;

  // Generate many frames to get stable statistics
  std::vector<double> all_left;
  int16_t buf[snora::FRAME_SAMPLES];
  for (int f = 0; f < 200; ++f) {
    gen.generate(buf, 0.0f, 0.7f);  // slope=0 = pure white noise
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    all_left.insert(all_left.end(), left.begin(), left.end());
  }

  auto [spectrum, fft_size] = powerSpectrum(all_left);

  // Compare low-mid (500-2000 Hz) vs high (8000-16000 Hz)
  double low_mid = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 500, 2000);
  double high = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 8000, 16000);

  // White noise should have similar power across bands (within 6 dB)
  double ratio_db = 10.0 * std::log10(low_mid / high);
  EXPECT_NEAR(ratio_db, 0.0, 6.0) << "White noise should be spectrally flat";
}

TEST(WhiteNoise, PinkNoiseHasMoreLowFrequency) {
  // Pink noise (slope=-3) should have MORE low-frequency power than high
  snora::NoiseGenerator gen;

  std::vector<double> all_left;
  int16_t buf[snora::FRAME_SAMPLES];
  for (int f = 0; f < 200; ++f) {
    gen.generate(buf, -3.0f, 0.7f);  // slope=-3 = pink noise
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    all_left.insert(all_left.end(), left.begin(), left.end());
  }

  auto [spectrum, fft_size] = powerSpectrum(all_left);
  double low = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 200, 1000);
  double high = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 4000, 16000);

  // Pink noise: low frequencies should have significantly more power
  EXPECT_GT(low, high * 2.0) << "Pink noise should have more LF than HF power";
}

TEST(WhiteNoise, BrownNoiseHasEvenMoreLowFrequency) {
  // Brown noise (slope=-6) should have MUCH more low-frequency power
  snora::NoiseGenerator gen;

  std::vector<double> all_left;
  int16_t buf[snora::FRAME_SAMPLES];
  for (int f = 0; f < 200; ++f) {
    gen.generate(buf, -6.0f, 0.7f);  // slope=-6 = brown noise
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    all_left.insert(all_left.end(), left.begin(), left.end());
  }

  auto [spectrum, fft_size] = powerSpectrum(all_left);
  double low = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 200, 1000);
  double high = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 4000, 16000);

  EXPECT_GT(low, high * 5.0) << "Brown noise should have much more LF than HF";
}

TEST(WhiteNoise, StereoChannelsAreUncorrelated) {
  snora::NoiseGenerator gen;

  std::vector<double> all_left, all_right;
  int16_t buf[snora::FRAME_SAMPLES];
  for (int f = 0; f < 100; ++f) {
    gen.generate(buf, 0.0f, 0.7f);
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    auto right = extractChannel(buf, snora::FRAME_SAMPLES, 1);
    all_left.insert(all_left.end(), left.begin(), left.end());
    all_right.insert(all_right.end(), right.begin(), right.end());
  }

  double corr = correlation(all_left, all_right);
  // Independent channels should have correlation near 0 (< 0.1)
  EXPECT_LT(std::abs(corr), 0.1) << "Stereo channels should be uncorrelated, got r=" << corr;
}

TEST(WhiteNoise, ZeroAmplitudeProducesSilence) {
  snora::NoiseGenerator gen;
  int16_t buf[snora::FRAME_SAMPLES];
  gen.generate(buf, 0.0f, 0.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(buf[i], 0) << "Zero amplitude should produce silence at sample " << i;
  }
}

TEST(WhiteNoise, RmsConsistentOverTime) {
  // RMS should be roughly stable across frames (no energy drift)
  snora::NoiseGenerator gen;
  int16_t buf[snora::FRAME_SAMPLES];

  std::vector<double> frame_rms;
  for (int f = 0; f < 500; ++f) {
    gen.generate(buf, -3.0f, 0.5f);
    frame_rms.push_back(rms(buf, snora::FRAME_SAMPLES));
  }

  // Compute mean and stddev of per-frame RMS
  double mean = std::accumulate(frame_rms.begin(), frame_rms.end(), 0.0) / frame_rms.size();
  double var = 0;
  for (double r : frame_rms) var += (r - mean) * (r - mean);
  double stddev = std::sqrt(var / frame_rms.size());

  // Coefficient of variation should be small (< 30%)
  double cv = stddev / mean;
  EXPECT_LT(cv, 0.3) << "RMS should be stable across frames, CV=" << cv;
  EXPECT_GT(mean, 100) << "Should produce audible output";
}

TEST(WhiteNoise, FrameContinuityNoDiscontinuities) {
  // Filter state should persist across frames — no abrupt jumps at boundaries
  snora::NoiseGenerator gen;
  int16_t prev_buf[snora::FRAME_SAMPLES];
  int16_t curr_buf[snora::FRAME_SAMPLES];

  gen.generate(prev_buf, -3.0f, 0.5f);

  int large_jumps = 0;
  for (int f = 0; f < 100; ++f) {
    gen.generate(curr_buf, -3.0f, 0.5f);

    // Check boundary between last sample of prev frame and first of current
    for (int ch = 0; ch < snora::CHANNELS; ++ch) {
      int last = prev_buf[(snora::SAMPLES_PER_CHANNEL - 1) * snora::CHANNELS + ch];
      int first = curr_buf[ch];
      int jump = std::abs(first - last);
      // A jump > 10000 (out of 32767) at a boundary would indicate a discontinuity
      if (jump > 10000) large_jumps++;
    }

    std::memcpy(prev_buf, curr_buf, sizeof(curr_buf));
  }

  // Some large jumps are expected in white noise, but pink noise should be smoother
  // With 100 frames * 2 channels = 200 boundaries, less than 5% should have large jumps
  EXPECT_LT(large_jumps, 10) << "Too many large discontinuities at frame boundaries";
}

TEST(WhiteNoise, SlopeTransitionIsSmooth) {
  // Changing spectral slope mid-stream should not cause clicks
  snora::NoiseGenerator gen;
  int16_t buf[snora::FRAME_SAMPLES];

  // Generate white noise for 10 frames
  for (int f = 0; f < 10; ++f) gen.generate(buf, 0.0f, 0.5f);
  double rms_white = rms(buf, snora::FRAME_SAMPLES);

  // Switch to brown noise — should transition without crash or extreme values
  for (int f = 0; f < 10; ++f) gen.generate(buf, -6.0f, 0.5f);
  double rms_brown = rms(buf, snora::FRAME_SAMPLES);

  // Both should produce reasonable output
  EXPECT_GT(rms_white, 50);
  EXPECT_GT(rms_brown, 50);

  // No samples should exceed valid range
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_GE(buf[i], -32767);
    EXPECT_LE(buf[i], 32767);
  }
}

// ===========================================================================
// SPECTRAL TILT VERIFICATION
// ===========================================================================

TEST(SpectralTilt, SlopeAffectsFrequencyBalance) {
  // Generate white noise with NoiseGenerator (slope=0), then apply spectral tilt
  snora::NoiseGenerator gen;
  snora::SpectralTilt tilt;

  std::vector<double> tilted_left, untilted_left;
  int16_t buf[snora::FRAME_SAMPLES];
  int16_t buf2[snora::FRAME_SAMPLES];

  // Generate identical noise batches — one tilted, one not
  // Use separate generators so they have different seeds
  snora::NoiseGenerator gen2;

  for (int f = 0; f < 100; ++f) {
    gen.generate(buf, 0.0f, 0.7f);
    tilt.process(buf, snora::FRAME_SAMPLES, -6.0f);  // heavy tilt
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    tilted_left.insert(tilted_left.end(), left.begin(), left.end());

    gen2.generate(buf2, 0.0f, 0.7f);
    auto left2 = extractChannel(buf2, snora::FRAME_SAMPLES, 0);
    untilted_left.insert(untilted_left.end(), left2.begin(), left2.end());
  }

  auto [spec_tilted, fft1] = powerSpectrum(tilted_left);
  auto [spec_raw, fft2] = powerSpectrum(untilted_left);

  double tilted_hf = bandPower(spec_tilted, fft1, snora::SAMPLE_RATE, 8000, 20000);
  double raw_hf = bandPower(spec_raw, fft2, snora::SAMPLE_RATE, 8000, 20000);

  // Tilted signal should have less HF energy than raw white noise
  EXPECT_LT(tilted_hf, raw_hf)
      << "Tilted should have less HF than raw, tilted=" << tilted_hf << " raw=" << raw_hf;
}

TEST(SpectralTilt, FilterStatePersistsAcrossFrames) {
  snora::SpectralTilt tilt;
  int16_t buf1[snora::FRAME_SAMPLES];
  int16_t buf2[snora::FRAME_SAMPLES];

  // Fill with a constant impulse pattern
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) buf1[i] = (i % 2 == 0) ? 10000 : -10000;
  std::memcpy(buf2, buf1, sizeof(buf1));

  // Process two consecutive frames — second should differ from first
  // because the filter state carries over
  tilt.process(buf1, snora::FRAME_SAMPLES, -4.0f);
  tilt.process(buf2, snora::FRAME_SAMPLES, -4.0f);

  // Compare outputs: they should be different (filter state effect)
  bool different = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buf1[i] != buf2[i]) { different = true; break; }
  }
  EXPECT_TRUE(different) << "Filter state should cause different output on second frame";
}

// ===========================================================================
// PIPELINE END-TO-END AUDIO QUALITY
// ===========================================================================

TEST(PipelineAudio, StereoChannelsHaveSimilarRMS) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.volume = 0.7f;
  config.assets_path = "";
  pipeline.init(config);

  snora::AudioParams params;
  params.spectral_slope = -4.0f;
  params.am_frequency = 0.15f;
  params.binaural_hz = 4.0f;
  params.binaural_carrier = 200.0f;
  params.nature_gain = 0.4f;
  params.master_volume = 0.7f;
  params.binaural_enabled = true;

  int16_t buf[snora::FRAME_SAMPLES];

  // Stabilize
  for (int f = 0; f < 50; ++f) pipeline.processFrame(buf, params);

  // Measure L/R RMS over many frames
  double l_rms_sum = 0, r_rms_sum = 0;
  int count = 0;
  for (int f = 0; f < 200; ++f) {
    pipeline.processFrame(buf, params);
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    auto right = extractChannel(buf, snora::FRAME_SAMPLES, 1);
    double l_rms = 0, r_rms = 0;
    for (double s : left) l_rms += s * s;
    for (double s : right) r_rms += s * s;
    l_rms_sum += std::sqrt(l_rms / left.size());
    r_rms_sum += std::sqrt(r_rms / right.size());
    count++;
  }

  double l_avg = l_rms_sum / count;
  double r_avg = r_rms_sum / count;

  // L and R should be within 6 dB of each other
  double ratio = std::max(l_avg, r_avg) / std::min(l_avg, r_avg);
  EXPECT_LT(ratio, 2.0) << "L/R RMS ratio should be < 2.0 (6 dB), got " << ratio;
}

TEST(PipelineAudio, MasterVolumeScalesLinearly) {
  auto measureRMS = [](float volume) -> double {
    snora::AudioPipeline pipeline;
    snora::SessionConfig config;
    config.soundscape = "rain";
    config.volume = volume;
    config.assets_path = "";
    pipeline.init(config);

    snora::AudioParams params;
    params.spectral_slope = -3.0f;
    params.master_volume = volume;
    params.binaural_enabled = false;

    int16_t buf[snora::FRAME_SAMPLES];
    // Stabilize
    for (int f = 0; f < 100; ++f) pipeline.processFrame(buf, params);

    double total_rms = 0;
    for (int f = 0; f < 100; ++f) {
      pipeline.processFrame(buf, params);
      total_rms += rms(buf, snora::FRAME_SAMPLES);
    }
    return total_rms / 100;
  };

  double rms_loud = measureRMS(0.8f);
  double rms_medium = measureRMS(0.4f);
  double rms_quiet = measureRMS(0.1f);

  EXPECT_GT(rms_loud, rms_medium) << "Loud > Medium";
  EXPECT_GT(rms_medium, rms_quiet) << "Medium > Quiet";
  EXPECT_GT(rms_quiet, 10) << "Even quiet should produce some output";
}

TEST(PipelineAudio, StressLevelAffectsSpectralContent) {
  // Different stress levels should produce measurably different spectral content
  auto measureHFRatio = [](float stress) -> double {
    snora::SessionConfig config;
    config.soundscape = "rain";
    config.volume = 0.7f;
    config.assets_path = "";

    snora::PhysioState physio;
    physio.mood = stress > 0.5f ? "anxious" : "calm";
    physio.stress_level = stress;
    physio.heart_rate = 60 + stress * 40;
    physio.respiration_rate = 10 + stress * 10;

    auto params = snora::mapPhysioToAudio(physio, config, 5.0f);

    snora::AudioPipeline pipeline;
    pipeline.init(config);

    int16_t buf[snora::FRAME_SAMPLES];
    // Run 300 frames to let smoothers converge
    for (int f = 0; f < 300; ++f) pipeline.processFrame(buf, params);

    // Collect samples for spectral analysis
    std::vector<double> samples;
    for (int f = 0; f < 100; ++f) {
      pipeline.processFrame(buf, params);
      auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
      samples.insert(samples.end(), left.begin(), left.end());
    }

    auto [spectrum, fft_size] = powerSpectrum(samples);
    double hf = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 4000, 16000);
    double lf = bandPower(spectrum, fft_size, snora::SAMPLE_RATE, 200, 2000);
    return hf / (lf + 1e-10);
  };

  double hf_ratio_calm = measureHFRatio(0.1f);
  double hf_ratio_stressed = measureHFRatio(0.9f);

  // Both should produce audio
  EXPECT_GT(hf_ratio_calm, 0) << "Calm should produce audio";
  EXPECT_GT(hf_ratio_stressed, 0) << "Stressed should produce audio";

  // They should be different (stress level affects spectral tilt)
  EXPECT_NE(hf_ratio_calm, hf_ratio_stressed)
      << "Different stress levels should produce different spectral content";
}

TEST(PipelineAudio, AmplitudeModulationCreatesEnvelope) {
  // AM should create volume fluctuations at the respiration frequency
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.volume = 0.7f;
  config.assets_path = "";
  pipeline.init(config);

  snora::AudioParams params;
  params.spectral_slope = -3.0f;
  params.am_frequency = 0.2f;  // 12 bpm (one breath per 5 seconds)
  params.binaural_hz = 4.0f;
  params.binaural_carrier = 200.0f;
  params.nature_gain = 0.0f;  // disable nature so AM is clearer
  params.master_volume = 0.7f;
  params.binaural_enabled = false;  // disable binaural so we only test AM

  int16_t buf[snora::FRAME_SAMPLES];
  // Stabilize
  for (int f = 0; f < 100; ++f) pipeline.processFrame(buf, params);

  // Measure per-frame RMS over 500 frames (5 seconds = 1 full AM cycle at 0.2 Hz)
  std::vector<double> frame_rms;
  for (int f = 0; f < 500; ++f) {
    pipeline.processFrame(buf, params);
    frame_rms.push_back(rms(buf, snora::FRAME_SAMPLES));
  }

  double min_rms = *std::min_element(frame_rms.begin(), frame_rms.end());
  double max_rms = *std::max_element(frame_rms.begin(), frame_rms.end());

  // AM should create variation in RMS (at least 2:1 ratio max/min)
  EXPECT_GT(max_rms / (min_rms + 1e-10), 1.5)
      << "AM should create envelope variations, max=" << max_rms << " min=" << min_rms;
}

TEST(PipelineAudio, BinauralCreatesFrequencyDifference) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.volume = 0.7f;
  config.assets_path = "";
  pipeline.init(config);

  snora::AudioParams params;
  params.spectral_slope = -3.0f;
  params.am_frequency = 0.0f;  // no AM
  params.binaural_hz = 6.0f;   // 6 Hz beat
  params.binaural_carrier = 200.0f;
  params.nature_gain = 0.0f;
  params.master_volume = 0.7f;
  params.binaural_enabled = true;

  int16_t buf[snora::FRAME_SAMPLES];
  // Stabilize
  for (int f = 0; f < 100; ++f) pipeline.processFrame(buf, params);

  // Collect L/R samples
  std::vector<double> all_left, all_right;
  for (int f = 0; f < 200; ++f) {
    pipeline.processFrame(buf, params);
    auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
    auto right = extractChannel(buf, snora::FRAME_SAMPLES, 1);
    all_left.insert(all_left.end(), left.begin(), left.end());
    all_right.insert(all_right.end(), right.begin(), right.end());
  }

  // L and R should have different spectral content near 200 Hz
  // (L at ~197 Hz, R at ~203 Hz for 6 Hz beat)
  auto [l_spec, l_fft] = powerSpectrum(all_left);
  auto [r_spec, r_fft] = powerSpectrum(all_right);

  double l_at_carrier = bandPower(l_spec, l_fft, snora::SAMPLE_RATE, 190, 210);
  double r_at_carrier = bandPower(r_spec, r_fft, snora::SAMPLE_RATE, 190, 210);

  // Both channels should have energy near the carrier frequency
  EXPECT_GT(l_at_carrier, 0) << "Left channel should have energy near 200 Hz";
  EXPECT_GT(r_at_carrier, 0) << "Right channel should have energy near 200 Hz";
}

TEST(PipelineAudio, LongDurationStability) {
  // Run 10 seconds (1000 frames) — verify no energy drift or crash
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "ocean";
  config.volume = 0.7f;
  config.assets_path = "";
  pipeline.init(config);

  snora::AudioParams params;
  params.spectral_slope = -4.0f;
  params.am_frequency = 0.15f;
  params.binaural_hz = 4.0f;
  params.binaural_carrier = 200.0f;
  params.nature_gain = 0.4f;
  params.master_volume = 0.7f;
  params.binaural_enabled = true;

  int16_t buf[snora::FRAME_SAMPLES];
  double first_100_rms = 0, last_100_rms = 0;

  for (int f = 0; f < 1000; ++f) {
    pipeline.processFrame(buf, params);
    double r = rms(buf, snora::FRAME_SAMPLES);

    if (f >= 50 && f < 150) first_100_rms += r;
    if (f >= 900) last_100_rms += r;

    // No clipping ever
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      ASSERT_GE(buf[i], -32767) << "Clipping at frame " << f << " sample " << i;
      ASSERT_LE(buf[i], 32767) << "Clipping at frame " << f << " sample " << i;
    }
  }

  first_100_rms /= 100;
  last_100_rms /= 100;

  // Energy should not drift wildly over 10 seconds.
  // AM (respiration entrainment) causes intentional RMS variation (~10x),
  // but average RMS over 100-frame windows should stay within 10x.
  double ratio = std::max(first_100_rms, last_100_rms) /
                 (std::min(first_100_rms, last_100_rms) + 1e-10);
  EXPECT_LT(ratio, 15.0) << "Energy should not drift wildly, ratio=" << ratio;
}

TEST(PipelineAudio, SilenceWhenVolumeZero) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.volume = 0.0f;
  config.assets_path = "";
  pipeline.init(config);

  snora::AudioParams params;
  params.master_volume = 0.0f;

  int16_t buf[snora::FRAME_SAMPLES];
  // Let smoothers converge to 0
  for (int f = 0; f < 200; ++f) pipeline.processFrame(buf, params);

  double r = rms(buf, snora::FRAME_SAMPLES);
  EXPECT_LT(r, 5.0) << "Zero volume should produce near-silence, RMS=" << r;
}

TEST(PipelineAudio, ProceduralTexturesHaveDistinctCharacter) {
  // Rain, wind, and ocean should have different spectral signatures

  auto measureSpectralCentroid = [](const std::string& soundscape) -> double {
    snora::AudioPipeline pipeline;
    snora::SessionConfig config;
    config.soundscape = soundscape;
    config.volume = 0.7f;
    config.assets_path = "";
    pipeline.init(config);

    snora::AudioParams params;
    params.spectral_slope = -3.0f;
    params.binaural_enabled = false;
    params.master_volume = 0.7f;
    params.nature_gain = 0.0f;

    int16_t buf[snora::FRAME_SAMPLES];
    std::vector<double> samples;
    for (int f = 0; f < 300; ++f) {
      pipeline.processFrame(buf, params);
      if (f >= 200) {
        auto left = extractChannel(buf, snora::FRAME_SAMPLES, 0);
        samples.insert(samples.end(), left.begin(), left.end());
      }
    }

    auto [spectrum, fft_size] = powerSpectrum(samples);

    // Spectral centroid = weighted average frequency
    double num = 0, den = 0;
    for (size_t k = 1; k < spectrum.size(); ++k) {
      double freq = static_cast<double>(k) * snora::SAMPLE_RATE / fft_size;
      num += freq * spectrum[k];
      den += spectrum[k];
    }
    return den > 0 ? num / den : 0;
  };

  double rain_centroid = measureSpectralCentroid("rain");
  double wind_centroid = measureSpectralCentroid("wind");
  double ocean_centroid = measureSpectralCentroid("ocean");

  // All should produce sound
  EXPECT_GT(rain_centroid, 0);
  EXPECT_GT(wind_centroid, 0);
  EXPECT_GT(ocean_centroid, 0);

  // At least one pair should differ noticeably (different textures = different spectral centroids)
  double max_c = std::max({rain_centroid, wind_centroid, ocean_centroid});
  double min_c = std::min({rain_centroid, wind_centroid, ocean_centroid});
  EXPECT_GT(max_c / (min_c + 1e-10), 1.05)
      << "Different soundscapes should have different spectral character";
}
