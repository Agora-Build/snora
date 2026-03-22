#include <gtest/gtest.h>
#include "state/session_state.h"
#include "state/param_mapper.h"
#include "audio/pipeline.h"
#include "audio/audio_format.h"
#include "agora/agora_sender.h"

#include <cmath>
#include <cstring>
#include <chrono>
#include <thread>
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// SessionState lifecycle tests
// ---------------------------------------------------------------------------

TEST(SessionLifecycle, PhysioUpdatesArePersisted) {
  snora::SessionState state;
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.volume = 0.7f;
  state.setConfig(config);

  // Initial defaults
  EXPECT_FLOAT_EQ(state.physio().stress_level, 0.3f);
  EXPECT_EQ(state.physio().mood, "neutral");

  // Update physio
  snora::PhysioState p;
  p.mood = "anxious";
  p.heart_rate = 95.0f;
  p.hrv = 25.0f;
  p.respiration_rate = 22.0f;
  p.stress_level = 0.8f;
  state.updatePhysio(p);

  EXPECT_EQ(state.physio().mood, "anxious");
  EXPECT_FLOAT_EQ(state.physio().heart_rate, 95.0f);
  EXPECT_FLOAT_EQ(state.physio().stress_level, 0.8f);

  // Update again — should overwrite
  p.mood = "calm";
  p.stress_level = 0.2f;
  state.updatePhysio(p);

  EXPECT_EQ(state.physio().mood, "calm");
  EXPECT_FLOAT_EQ(state.physio().stress_level, 0.2f);
}

TEST(SessionLifecycle, ElapsedMinutesAdvances) {
  snora::SessionState state;
  state.setStartTime(std::chrono::steady_clock::now());

  float t0 = state.elapsedMinutes();
  EXPECT_NEAR(t0, 0.0f, 0.01f);

  // Set start time 5 minutes in the past
  auto past = std::chrono::steady_clock::now() - std::chrono::minutes(5);
  state.setStartTime(past);
  float t5 = state.elapsedMinutes();
  EXPECT_NEAR(t5, 5.0f, 0.1f);
}

// ---------------------------------------------------------------------------
// Param mapper adaptation over time
// ---------------------------------------------------------------------------

TEST(SessionLifecycle, ParamMapperAdaptsOverTime) {
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.binaural_beats = true;
  config.volume = 0.7f;

  snora::PhysioState physio;
  physio.mood = "anxious";
  physio.heart_rate = 90.0f;
  physio.respiration_rate = 18.0f;
  physio.stress_level = 0.8f;

  // At t=0 — high stress should produce steep spectral tilt
  auto params_t0 = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_LT(params_t0.spectral_slope, -3.0f);  // steep
  EXPECT_GT(params_t0.binaural_hz, 7.0f);       // alpha range for anxious
  EXPECT_GT(params_t0.nature_gain, 0.4f);        // high nature for stress

  // At t=20min — respiration should have adapted toward 5.5 bpm
  auto params_t20 = snora::mapPhysioToAudio(physio, config, 20.0f);
  float expected_am_freq = 5.5f / 60.0f;  // target resp in Hz
  EXPECT_NEAR(params_t20.am_frequency, expected_am_freq, 0.01f);

  // Different mood should change binaural range
  physio.mood = "sleepy";
  physio.stress_level = 0.1f;
  auto params_sleepy = snora::mapPhysioToAudio(physio, config, 10.0f);
  EXPECT_LT(params_sleepy.binaural_hz, 2.0f);  // delta range
  EXPECT_GT(params_sleepy.spectral_slope, -3.0f);  // flatter with low stress
}

// ---------------------------------------------------------------------------
// IPC → State → Pipeline → Agora: full data flow
// ---------------------------------------------------------------------------

TEST(SessionLifecycle, FullDataFlowProducesAudio) {
  // Simulate: receive init → configure pipeline → process frames → send to Agora

  // 1. Parse init data (simulating what main.cpp does)
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.binaural_beats = true;
  config.volume = 0.7f;
  config.assets_path = "";  // no WAV files in test

  snora::SessionState state;
  state.setConfig(config);
  state.setStartTime(std::chrono::steady_clock::now());

  // 2. Init pipeline
  snora::AudioPipeline pipeline;
  pipeline.init(config);

  // 3. Init Agora stub
  snora::AgoraSender agora;
  std::vector<std::string> agora_events;
  agora.init("test-app", "test-token", "test-channel",
    [&](const std::string& event, const std::string&) {
      agora_events.push_back(event);
    });

  // 4. Simulate 100 frames (1 second of audio) with state updates
  int16_t frame[snora::FRAME_SAMPLES];
  int nonzero_frames = 0;

  for (int i = 0; i < 100; ++i) {
    // Every 50 frames, update physio state (simulating IPC state_update)
    if (i == 0) {
      snora::PhysioState p;
      p.mood = "anxious";
      p.heart_rate = 90.0f;
      p.hrv = 25.0f;
      p.respiration_rate = 20.0f;
      p.stress_level = 0.8f;
      state.updatePhysio(p);
    } else if (i == 50) {
      snora::PhysioState p;
      p.mood = "calm";
      p.heart_rate = 65.0f;
      p.hrv = 55.0f;
      p.respiration_rate = 12.0f;
      p.stress_level = 0.2f;
      state.updatePhysio(p);
    }

    auto params = snora::mapPhysioToAudio(
        state.physio(), state.config(), state.elapsedMinutes());

    std::memset(frame, 0, sizeof(frame));
    pipeline.processFrame(frame, params);

    // Check frame has audio content
    bool has_audio = false;
    for (int s = 0; s < snora::FRAME_SAMPLES; ++s) {
      if (frame[s] != 0) { has_audio = true; break; }
    }
    if (has_audio) nonzero_frames++;

    // Send to Agora (stub discards but should succeed)
    bool sent = agora.sendFrame(frame, snora::FRAME_SAMPLES);
    EXPECT_TRUE(sent);
  }

  // All frames should have audio
  EXPECT_EQ(nonzero_frames, 100);

  agora.leave();
}

// ---------------------------------------------------------------------------
// Audio quality: state change affects output within N frames
// ---------------------------------------------------------------------------

TEST(SessionLifecycle, StateChangeAffectsAudioOutput) {
  snora::SessionConfig config;
  config.soundscape = "rain";
  config.binaural_beats = true;
  config.volume = 0.7f;
  config.assets_path = "";

  snora::AudioPipeline pipeline;
  pipeline.init(config);

  // Generate frames with high stress
  snora::PhysioState high_stress;
  high_stress.mood = "anxious";
  high_stress.stress_level = 0.9f;
  high_stress.heart_rate = 95.0f;
  high_stress.respiration_rate = 22.0f;

  auto params_high = snora::mapPhysioToAudio(high_stress, config, 0.0f);

  int16_t frame_high[snora::FRAME_SAMPLES];
  // Run a few frames to stabilize smoothers
  for (int i = 0; i < 50; ++i) {
    pipeline.processFrame(frame_high, params_high);
  }
  // Capture RMS of a high-stress frame
  double rms_high = 0;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    rms_high += static_cast<double>(frame_high[i]) * frame_high[i];
  }
  rms_high = std::sqrt(rms_high / snora::FRAME_SAMPLES);

  // Now switch to low stress with quiet volume
  snora::PhysioState low_stress;
  low_stress.mood = "sleepy";
  low_stress.stress_level = 0.1f;
  low_stress.heart_rate = 58.0f;
  low_stress.respiration_rate = 8.0f;

  auto params_low = snora::mapPhysioToAudio(low_stress, config, 0.0f);
  params_low.master_volume = 0.2f;  // much quieter

  int16_t frame_low[snora::FRAME_SAMPLES];
  // Run enough frames for smoothers to converge (~300 frames = 3s)
  for (int i = 0; i < 500; ++i) {
    pipeline.processFrame(frame_low, params_low);
  }
  double rms_low = 0;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    rms_low += static_cast<double>(frame_low[i]) * frame_low[i];
  }
  rms_low = std::sqrt(rms_low / snora::FRAME_SAMPLES);

  // Low-volume calm state should be significantly quieter
  EXPECT_LT(rms_low, rms_high) << "Expected calm+quiet to produce less energy than anxious+loud";
}

// ---------------------------------------------------------------------------
// Agora stub lifecycle
// ---------------------------------------------------------------------------

TEST(AgoraSender, StubInitSendLeaveLifecycle) {
  snora::AgoraSender agora;

  std::vector<std::string> events;
  bool inited = agora.init("app", "token", "ch",
    [&](const std::string& event, const std::string&) {
      events.push_back(event);
    });
  EXPECT_TRUE(inited);

  // Send frames should succeed while connected
  int16_t silence[snora::FRAME_SAMPLES] = {};
  EXPECT_TRUE(agora.sendFrame(silence, snora::FRAME_SAMPLES));

  // Token renewal should succeed
  EXPECT_TRUE(agora.renewToken("new-token"));

  // Leave
  agora.leave();

  // After leave, sendFrame should fail
  EXPECT_FALSE(agora.sendFrame(silence, snora::FRAME_SAMPLES));
}

// ---------------------------------------------------------------------------
// Multiple soundscapes produce different output
// ---------------------------------------------------------------------------

TEST(SessionLifecycle, DifferentSoundscapesProduceDifferentOutput) {
  auto generate_frames = [](const std::string& soundscape, int count) -> std::vector<int64_t> {
    snora::SessionConfig config;
    config.soundscape = soundscape;
    config.binaural_beats = true;
    config.volume = 0.7f;
    config.assets_path = "";

    snora::AudioPipeline pipeline;
    pipeline.init(config);

    snora::PhysioState physio;
    physio.mood = "neutral";
    physio.stress_level = 0.5f;
    auto params = snora::mapPhysioToAudio(physio, config, 5.0f);

    int16_t frame[snora::FRAME_SAMPLES];
    std::vector<int64_t> sums;
    for (int i = 0; i < count; ++i) {
      pipeline.processFrame(frame, params);
      int64_t sum = 0;
      for (int s = 0; s < snora::FRAME_SAMPLES; ++s) {
        sum += std::abs(static_cast<int64_t>(frame[s]));
      }
      sums.push_back(sum);
    }
    return sums;
  };

  auto rain_sums = generate_frames("rain", 50);
  auto wind_sums = generate_frames("wind", 50);
  auto ocean_sums = generate_frames("ocean", 50);

  // Compute average absolute amplitude for each
  auto avg = [](const std::vector<int64_t>& v) {
    return std::accumulate(v.begin(), v.end(), 0LL) / static_cast<int64_t>(v.size());
  };

  int64_t rain_avg = avg(rain_sums);
  int64_t wind_avg = avg(wind_sums);
  int64_t ocean_avg = avg(ocean_sums);

  // All should produce substantial output
  EXPECT_GT(rain_avg, 0);
  EXPECT_GT(wind_avg, 0);
  EXPECT_GT(ocean_avg, 0);

  // At least two soundscapes should differ noticeably (they use different
  // procedural textures with different spectral characteristics)
  bool any_differ = (rain_avg != wind_avg) || (wind_avg != ocean_avg);
  EXPECT_TRUE(any_differ) << "Expected different soundscapes to produce different output levels";
}

// ---------------------------------------------------------------------------
// Binaural disable should change output
// ---------------------------------------------------------------------------

TEST(SessionLifecycle, BinauralDisableChangesOutput) {
  auto generate_with_binaural = [](bool enabled) -> double {
    snora::SessionConfig config;
    config.soundscape = "rain";
    config.binaural_beats = enabled;
    config.volume = 0.7f;
    config.assets_path = "";

    snora::AudioPipeline pipeline;
    pipeline.init(config);

    snora::PhysioState physio;
    physio.mood = "neutral";
    physio.stress_level = 0.5f;
    auto params = snora::mapPhysioToAudio(physio, config, 0.0f);

    int16_t frame[snora::FRAME_SAMPLES];
    // Let it stabilize
    for (int i = 0; i < 50; ++i) {
      pipeline.processFrame(frame, params);
    }

    // Measure RMS
    double rms = 0;
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      rms += static_cast<double>(frame[i]) * frame[i];
    }
    return std::sqrt(rms / snora::FRAME_SAMPLES);
  };

  double rms_with = generate_with_binaural(true);
  double rms_without = generate_with_binaural(false);

  // Both should produce audio
  EXPECT_GT(rms_with, 0);
  EXPECT_GT(rms_without, 0);

  // They should differ (binaural adds an extra tonal layer)
  EXPECT_NE(rms_with, rms_without);
}
