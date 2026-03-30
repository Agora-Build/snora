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

// ── Sleep ──────────────────────────────────────────────────────────────
// Guide to deep sleep. Dark tone when stressed, bright when calm.
// Delta binaural (0.5-4 Hz). Breathing guided toward 5.5 bpm over 20 min.
AudioParams mapSleep(const PhysioState &physio, const SessionConfig &config,
                     float elapsed_minutes) {
  AudioParams params;

  params.spectral_slope = lerp(-6.0f, -2.0f, 1.0f - physio.stress_level);

  float session_progress = clamp(elapsed_minutes / 20.0f, 0.0f, 1.0f);
  float target_resp = lerp(physio.respiration_rate, 5.5f, session_progress);
  params.am_frequency = target_resp / 60.0f;

  params.binaural_enabled = config.binaural_beats;
  auto range = moodToRange(physio.mood);
  switch (range) {
  case MoodRange::Alpha:
    params.binaural_hz = lerp(8.0f, 12.0f, physio.stress_level);
    break;
  case MoodRange::Theta:
    params.binaural_hz = lerp(4.0f, 8.0f, physio.stress_level);
    break;
  case MoodRange::Delta:
    params.binaural_hz = lerp(0.5f, 4.0f, physio.stress_level);
    break;
  }
  params.binaural_carrier = 200.0f;

  params.nature_gain = lerp(0.2f, 0.6f, physio.stress_level);
  params.master_volume = config.volume;

  return params;
}

// ── Focus ──────────────────────────────────────────────────────────────
// Sustained concentration. Bright/clear tone. Beta binaural (14-20 Hz).
// No breathing guidance — keep user's natural rhythm. Minimal AM.
AudioParams mapFocus(const PhysioState &physio, const SessionConfig &config,
                     float /*elapsed_minutes*/) {
  AudioParams params;

  // Consistently bright — slight darkening only at very high stress
  params.spectral_slope = lerp(-3.0f, -1.0f, 1.0f - physio.stress_level);

  // No breathing guidance — just gentle AM at user's current rate
  params.am_frequency = physio.respiration_rate / 60.0f;

  // Beta range for focus/alertness
  params.binaural_enabled = config.binaural_beats;
  params.binaural_hz = lerp(14.0f, 20.0f, 1.0f - physio.stress_level);
  params.binaural_carrier = 250.0f;

  params.nature_gain = 0.3f; // steady background
  params.master_volume = config.volume;

  return params;
}

// ── Exercise ───────────────────────────────────────────────────────────
// Energize and motivate. Bright, punchy tone. High-beta binaural (20-30 Hz).
// No breathing slowdown — match user's elevated pace.
AudioParams mapExercise(const PhysioState &physio, const SessionConfig &config,
                        float /*elapsed_minutes*/) {
  AudioParams params;

  // Very bright and energetic — minimal filtering
  params.spectral_slope = lerp(-2.0f, -0.5f, 1.0f - physio.stress_level);

  // Match user's breathing — no slowdown
  params.am_frequency = physio.respiration_rate / 60.0f;

  // High-beta for energy and motivation
  params.binaural_enabled = config.binaural_beats;
  params.binaural_hz = lerp(20.0f, 30.0f, physio.stress_level);
  params.binaural_carrier = 300.0f;

  params.nature_gain = 0.2f; // low — exercise doesn't need nature sounds
  params.master_volume = config.volume;

  return params;
}

// ── Meditation ─────────────────────────────────────────────────────────
// Mindfulness. Smooth, warm tone. Theta binaural (4-8 Hz).
// Guide toward 4 bpm over 15 minutes.
AudioParams mapMeditation(const PhysioState &physio,
                          const SessionConfig &config, float elapsed_minutes) {
  AudioParams params;

  // Warm and smooth — more filtering than sleep
  params.spectral_slope = lerp(-5.0f, -3.0f, 1.0f - physio.stress_level);

  // Guide toward very slow breathing (4 bpm) over 15 minutes
  float session_progress = clamp(elapsed_minutes / 15.0f, 0.0f, 1.0f);
  float target_resp = lerp(physio.respiration_rate, 4.0f, session_progress);
  params.am_frequency = target_resp / 60.0f;

  // Theta range for meditative state
  params.binaural_enabled = config.binaural_beats;
  params.binaural_hz = lerp(4.0f, 8.0f, 1.0f - physio.stress_level);
  params.binaural_carrier = 180.0f; // lower carrier for warmer tone

  params.nature_gain = lerp(0.3f, 0.5f, physio.stress_level);
  params.master_volume = config.volume;

  return params;
}

// ── Power Nap ──────────────────────────────────────────────────────────
// Quick 20-min rest. Theta→delta binaural transition. Moderate breathing
// slowdown toward 8 bpm. Slightly brighter than deep sleep.
AudioParams mapPowerNap(const PhysioState &physio, const SessionConfig &config,
                        float elapsed_minutes) {
  AudioParams params;

  // Similar to sleep but slightly brighter
  params.spectral_slope = lerp(-5.0f, -2.0f, 1.0f - physio.stress_level);

  // Moderate breathing slowdown toward 8 bpm over 10 minutes
  float session_progress = clamp(elapsed_minutes / 10.0f, 0.0f, 1.0f);
  float target_resp = lerp(physio.respiration_rate, 8.0f, session_progress);
  params.am_frequency = target_resp / 60.0f;

  // Theta→delta transition over 20 minutes
  float nap_progress = clamp(elapsed_minutes / 20.0f, 0.0f, 1.0f);
  params.binaural_enabled = config.binaural_beats;
  // Start at theta (6 Hz), transition to delta (2 Hz)
  float base_hz = lerp(6.0f, 2.0f, nap_progress);
  params.binaural_hz = base_hz + physio.stress_level * 2.0f;
  params.binaural_carrier = 200.0f;

  params.nature_gain = lerp(0.2f, 0.5f, physio.stress_level);
  params.master_volume = config.volume;

  return params;
}

} // namespace

AudioParams mapPhysioToAudio(const PhysioState &physio,
                             const SessionConfig &config,
                             float elapsed_minutes) {
  if (config.scenario == "focus")
    return mapFocus(physio, config, elapsed_minutes);
  if (config.scenario == "exercise")
    return mapExercise(physio, config, elapsed_minutes);
  if (config.scenario == "meditation")
    return mapMeditation(physio, config, elapsed_minutes);
  if (config.scenario == "power_nap")
    return mapPowerNap(physio, config, elapsed_minutes);

  // Default: sleep
  return mapSleep(physio, config, elapsed_minutes);
}

} // namespace snora
