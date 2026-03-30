#pragma once

#include <chrono>
#include <string>

namespace snora {

struct PhysioState {
  std::string mood = "neutral";
  float heart_rate = 70.0f;
  float hrv = 50.0f;
  float respiration_rate = 14.0f;
  float stress_level = 0.3f;
};

// Derived audio parameters computed from physio state
struct AudioParams {
  float spectral_slope = -4.0f;    // -6 to -2 dB/oct
  float am_frequency = 0.23f;      // Hz (respiration entrainment)
  float binaural_hz = 4.0f;        // Hz (frequency difference L/R)
  float binaural_carrier = 200.0f; // Hz
  float nature_gain = 0.4f;        // 0-1
  float master_volume = 0.7f;      // 0-1
  bool binaural_enabled = true;
};

struct SessionConfig {
  std::string soundscape = "rain";
  // sleep, focus, exercise, meditation, power_nap
  std::string scenario = "sleep";
  bool binaural_beats = true;
  float volume = 0.7f;
  std::string assets_path = "/assets/sounds";
};

class SessionState {
public:
  void updatePhysio(const PhysioState &state);
  void setConfig(const SessionConfig &config);
  void setStartTime(std::chrono::steady_clock::time_point t);

  const PhysioState &physio() const { return physio_; }
  const SessionConfig &config() const { return config_; }
  float elapsedMinutes() const;

private:
  PhysioState physio_;
  SessionConfig config_;
  std::chrono::steady_clock::time_point start_time_;
};

} // namespace snora
