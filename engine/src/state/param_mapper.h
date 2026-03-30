#pragma once

#include "state/session_state.h"

namespace snora {

// Maps physiological state to audio parameters using the spec formulas
AudioParams mapPhysioToAudio(const PhysioState &physio,
                             const SessionConfig &config,
                             float elapsed_minutes);

} // namespace snora
