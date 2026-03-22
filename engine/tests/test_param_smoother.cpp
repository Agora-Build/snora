#include <gtest/gtest.h>
#include "audio/param_smoother.h"
#include <cmath>

TEST(ParamSmoother, ConvergesToTarget) {
  snora::ParamSmoother smoother(0.1f, 48000.0f, 480);
  smoother.setImmediate(0.0f);
  smoother.setTarget(1.0f);

  for (int i = 0; i < 50; ++i) {
    smoother.smooth();
  }

  EXPECT_NEAR(smoother.current(), 1.0f, 0.01f);
}

TEST(ParamSmoother, SetImmediateBypassesSmoothing) {
  snora::ParamSmoother smoother(3.0f);
  smoother.setImmediate(0.5f);
  EXPECT_FLOAT_EQ(smoother.current(), 0.5f);
}

TEST(ParamSmoother, RespondsToTargetChange) {
  snora::ParamSmoother smoother(0.05f, 48000.0f, 480);
  smoother.setImmediate(0.0f);
  smoother.setTarget(1.0f);

  float prev = 0.0f;
  for (int i = 0; i < 10; ++i) {
    float val = smoother.smooth();
    EXPECT_GT(val, prev);
    prev = val;
  }
}

TEST(ParamSmoother, ThreeSecondSmoothing) {
  snora::ParamSmoother smoother(3.0f, 48000.0f, 480);
  smoother.setImmediate(0.0f);
  smoother.setTarget(1.0f);

  for (int i = 0; i < 300; ++i) {
    smoother.smooth();
  }
  EXPECT_NEAR(smoother.current(), 0.632f, 0.05f);
}
