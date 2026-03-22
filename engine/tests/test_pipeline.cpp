#include <gtest/gtest.h>
#include "audio/pipeline.h"
#include "audio/audio_format.h"
#include "state/session_state.h"

TEST(Pipeline, CorrectFrameSizeConstants) {
  EXPECT_EQ(snora::SAMPLE_RATE, 48000);
  EXPECT_EQ(snora::CHANNELS, 2);
  EXPECT_EQ(snora::FRAME_DURATION_MS, 10);
  EXPECT_EQ(snora::SAMPLES_PER_CHANNEL, 480);
  EXPECT_EQ(snora::FRAME_SAMPLES, 960);
  EXPECT_EQ(snora::FRAME_BYTES, 1920);
}

TEST(Pipeline, GeneratesNonZeroOutput) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  // Nature player not available, but noise/binaural/procedural still work.
  config.assets_path = "/nonexistent";
  config.soundscape  = "rain";
  pipeline.init(config);

  snora::AudioParams params;
  int16_t buffer[snora::FRAME_SAMPLES] = {};
  pipeline.processFrame(buffer, params);

  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buffer[i] != 0) {
      any_nonzero = true;
      break;
    }
  }
  EXPECT_TRUE(any_nonzero);
}

TEST(Pipeline, NoClippingOver100Frames) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "ocean";
  pipeline.init(config);

  snora::AudioParams params;
  params.master_volume = 1.0f;
  int16_t buffer[snora::FRAME_SAMPLES];

  for (int f = 0; f < 100; ++f) {
    pipeline.processFrame(buffer, params);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buffer[i], -32767) << "frame " << f << " sample " << i;
      EXPECT_LE(buffer[i],  32767) << "frame " << f << " sample " << i;
    }
  }
}

TEST(Pipeline, WindSoundscapeRuns) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.soundscape = "wind";
  pipeline.init(config);

  snora::AudioParams params;
  int16_t buffer[snora::FRAME_SAMPLES] = {};
  // Should not crash and should produce output.
  for (int f = 0; f < 10; ++f) {
    pipeline.processFrame(buffer, params);
  }
}

TEST(Pipeline, BinauralDisabledStillProducesOutput) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.binaural_beats = false;
  config.soundscape = "rain";
  pipeline.init(config);

  snora::AudioParams params;
  params.binaural_enabled = false;
  int16_t buffer[snora::FRAME_SAMPLES] = {};
  pipeline.processFrame(buffer, params);

  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buffer[i] != 0) { any_nonzero = true; break; }
  }
  EXPECT_TRUE(any_nonzero);
}
