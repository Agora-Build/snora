#pragma once

namespace snora {

class ParamSmoother {
public:
  explicit ParamSmoother(float smoothing_seconds, float sample_rate = 48000.0f,
                         int frame_samples = 480);

  void setTarget(float target);
  void setImmediate(float value);
  float current() const;
  float target() const;
  float smooth();

private:
  float current_;
  float target_;
  float alpha_;
  int frame_samples_;
};

} // namespace snora
