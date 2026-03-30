#include "state/param_mapper.h"
#include <algorithm>
#include <cmath>

namespace snora {

namespace {

float lerp(float a, float b, float t) { return a + t * (b - a); }

float clamp(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

enum class MoodRange { Alpha, Theta, Delta };

MoodRange moodToRange(const std::string &mood) {
  if (mood == "anxious" || mood == "stressed")
    return MoodRange::Alpha;
  if (mood == "neutral")
    return MoodRange::Theta;
  return MoodRange::Delta; // calm, relaxed, sleepy
}

} // namespace

AudioParams mapPhysioToAudio(const PhysioState &physio,
                             const SessionConfig &config,
                             float elapsed_minutes) {
  AudioParams params;

  // Spectral tilt slope: lerp(-6, -2, 1 - stress_level)
  params.spectral_slope = lerp(-6.0f, -2.0f, 1.0f - physio.stress_level);

  // Respiration entrainment: guide breath toward 5.5 bpm over 20 minutes
  float session_progress = clamp(elapsed_minutes / 20.0f, 0.0f, 1.0f);
  float target_resp = lerp(physio.respiration_rate, 5.5f, session_progress);
  params.am_frequency = target_resp / 60.0f;

  // Binaural beat frequency: mood-based range + stress interpolation
  params.binaural_enabled = config.binaural_beats;
  auto range = moodToRange(physio.mood);
  switch (range) {
  case MoodRange::Alpha:
    // Anxious/stressed: alpha range 8-12 Hz
    params.binaural_hz = lerp(8.0f, 12.0f, physio.stress_level);
    break;
  case MoodRange::Theta:
    // Neutral: theta range 4-8 Hz
    params.binaural_hz = lerp(4.0f, 8.0f, physio.stress_level);
    break;
  case MoodRange::Delta:
    // Calm/relaxed/sleepy: delta range 0.5-4 Hz
    params.binaural_hz = lerp(0.5f, 4.0f, physio.stress_level);
    break;
  }
  params.binaural_carrier = 200.0f;

  // Nature layer gain: more nature sound when stressed
  params.nature_gain = lerp(0.2f, 0.6f, physio.stress_level);

  // Master volume from user preferences
  params.master_volume = config.volume;

  return params;
}

} // namespace snora
