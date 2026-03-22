#include "state/session_state.h"

namespace snora {

void SessionState::updatePhysio(const PhysioState& state) { physio_ = state; }
void SessionState::setConfig(const SessionConfig& config) { config_ = config; }
void SessionState::setStartTime(std::chrono::steady_clock::time_point t) { start_time_ = t; }

float SessionState::elapsedMinutes() const {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
  return static_cast<float>(elapsed.count()) / 60000.0f;
}

}  // namespace snora
