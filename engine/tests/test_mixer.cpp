#include <gtest/gtest.h>
#include "audio/mixer.h"
#include "audio/audio_format.h"
#include <algorithm>
#include <cstring>

TEST(Mixer, SingleLayerGainOneEqualsInput) {
  int16_t input[snora::FRAME_SAMPLES];
  int16_t output[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) input[i] = static_cast<int16_t>(i % 1000 - 500);

  snora::Mixer mixer;
  mixer.mix({{input, 1.0f}}, output, snora::FRAME_SAMPLES, 1.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(output[i], input[i]) << "at index " << i;
  }
}

TEST(Mixer, ZeroMasterVolumeProducesSilence) {
  int16_t input[snora::FRAME_SAMPLES];
  int16_t output[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) input[i] = 10000;

  snora::Mixer mixer;
  mixer.mix({{input, 1.0f}}, output, snora::FRAME_SAMPLES, 0.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(output[i], 0) << "at index " << i;
  }
}

TEST(Mixer, ZeroLayerGainProducesSilence) {
  int16_t input[snora::FRAME_SAMPLES];
  int16_t output[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) input[i] = 10000;

  snora::Mixer mixer;
  mixer.mix({{input, 0.0f}}, output, snora::FRAME_SAMPLES, 1.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(output[i], 0) << "at index " << i;
  }
}

TEST(Mixer, TwoLayersAreSummed) {
  // Two identical buffers of 1000, gain=0.5 each -> output = 1000
  int16_t a[snora::FRAME_SAMPLES];
  int16_t b[snora::FRAME_SAMPLES];
  int16_t out[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) a[i] = b[i] = 1000;

  snora::Mixer mixer;
  mixer.mix({{a, 0.5f}, {b, 0.5f}}, out, snora::FRAME_SAMPLES, 1.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(out[i], 1000) << "at index " << i;
  }
}

TEST(Mixer, NoClippingBeyondInt16Range) {
  // Two buffers at max int16, gain=1 each — sum would be 65534, should be clamped
  int16_t a[snora::FRAME_SAMPLES];
  int16_t b[snora::FRAME_SAMPLES];
  int16_t out[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) a[i] = b[i] = 32767;

  snora::Mixer mixer;
  mixer.mix({{a, 1.0f}, {b, 1.0f}}, out, snora::FRAME_SAMPLES, 1.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_GE(out[i], -32767) << "at index " << i;
    EXPECT_LE(out[i], 32767) << "at index " << i;
  }
}

TEST(Mixer, MasterVolumeScalesOutput) {
  int16_t input[snora::FRAME_SAMPLES];
  int16_t out_full[snora::FRAME_SAMPLES] = {};
  int16_t out_half[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) input[i] = 10000;

  snora::Mixer mixer;
  mixer.mix({{input, 1.0f}}, out_full, snora::FRAME_SAMPLES, 1.0f);
  mixer.mix({{input, 1.0f}}, out_half, snora::FRAME_SAMPLES, 0.5f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_NEAR(out_half[i], out_full[i] / 2, 1) << "at index " << i;
  }
}

TEST(Mixer, EmptyLayerListProducesSilence) {
  int16_t out[snora::FRAME_SAMPLES] = {};
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) out[i] = 999;  // pre-fill with garbage

  snora::Mixer mixer;
  mixer.mix({}, out, snora::FRAME_SAMPLES, 1.0f);

  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    EXPECT_EQ(out[i], 0) << "at index " << i;
  }
}
