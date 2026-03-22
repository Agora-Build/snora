#pragma once

#include <string>
#include <cstdint>
#include <functional>

namespace snora {

// Callback invoked on Agora events (e.g., disconnects, errors).
using AgoraEventCallback = std::function<void(const std::string& event,
                                               const std::string& detail)>;

// AgoraSender wraps the Agora Server Gateway SDK.
//
// When built without SNORA_USE_AGORA (the default), all methods use the
// stub implementation that logs to stderr and discards audio frames.
//
// When SNORA_USE_AGORA is defined, the real Agora SDK calls are used
// (TODO — requires AGORA_SDK_DIR at build time).
class AgoraSender {
 public:
  AgoraSender();
  ~AgoraSender();

  // Initialize and join the Agora channel.
  // Returns true on success (stub always returns true).
  bool init(const std::string& app_id, const std::string& token,
            const std::string& channel, AgoraEventCallback callback);

  // Send one audio frame to the channel.
  // num_samples: total interleaved samples (FRAME_SAMPLES = 960 for stereo 10ms).
  // Returns false if not connected.
  bool sendFrame(const int16_t* data, int num_samples);

  // Renew the RTC token (for expiry rotation).
  bool renewToken(const std::string& new_token);

  // Leave the channel and release resources.
  void leave();

 private:
#ifdef SNORA_USE_AGORA
  void* service_     = nullptr;
  void* connection_  = nullptr;
  void* audio_track_ = nullptr;
#endif
  AgoraEventCallback callback_;
  bool connected_ = false;
};

}  // namespace snora
