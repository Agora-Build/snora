#include "audio/param_smoother.h"
#include <cmath>

namespace snora {

ParamSmoother::ParamSmoother(float smoothing_seconds, float sample_rate,
                             int frame_samples)
    : current_(0.0f), target_(0.0f), frame_samples_(frame_samples) {
  float tau = smoothing_seconds * sample_rate;
  alpha_ = 1.0f - std::exp(-1.0f / tau);
}

void ParamSmoother::setTarget(float target) { target_ = target; }
void ParamSmoother::setImmediate(float value) {
  current_ = value;
  target_ = value;
}
float ParamSmoother::current() const { return current_; }
float ParamSmoother::target() const { return target_; }

float ParamSmoother::smooth() {
  for (int i = 0; i < frame_samples_; ++i) {
    current_ += alpha_ * (target_ - current_);
  }
  return current_;
}

} // namespace snora
