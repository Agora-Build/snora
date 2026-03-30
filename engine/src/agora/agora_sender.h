#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <atomic>
#include <set>
#include <mutex>
#include <condition_variable>

#include "AgoraBase.h"
#include "AgoraRefPtr.h"
#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"
#include "NGIAgoraLocalUser.h"

namespace snora {

// Callback invoked on Agora events (e.g., disconnects, errors).
using AgoraEventCallback = std::function<void(const std::string& event,
                                               const std::string& detail)>;

// AgoraSender wraps the Agora Server Gateway SDK.
// Initializes the service, joins a channel, and pushes PCM audio frames.
class AgoraSender {
 public:
  AgoraSender();
  ~AgoraSender();

  // Initialize and join the Agora channel.
  bool init(const std::string& app_id, const std::string& token,
            const std::string& channel, AgoraEventCallback callback);

  // Send one audio frame to the channel.
  // num_samples: total interleaved samples (FRAME_SAMPLES = 960 for stereo 10ms).
  bool sendFrame(const int16_t* data, int num_samples);

  // Renew the RTC token (for expiry rotation).
  bool renewToken(const std::string& new_token);

  // Leave the channel and release resources.
  void leave();

 private:
  class ConnectionObserver : public agora::rtc::IRtcConnectionObserver {
   public:
    explicit ConnectionObserver(AgoraSender* sender) : sender_(sender) {}

    void onConnected(const agora::rtc::TConnectionInfo& info,
                     agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason) override;
    void onDisconnected(const agora::rtc::TConnectionInfo& info,
                        agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason) override;
    void onConnecting(const agora::rtc::TConnectionInfo& info,
                      agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason) override;
    void onReconnecting(const agora::rtc::TConnectionInfo& info,
                        agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason) override;
    void onReconnected(const agora::rtc::TConnectionInfo& info,
                       agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason) override;
    void onConnectionLost(const agora::rtc::TConnectionInfo& info) override;
    void onConnectionFailure(const agora::rtc::TConnectionInfo& info,
                             agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason) override;
    void onTokenPrivilegeWillExpire(const char* token) override;
    void onTokenPrivilegeDidExpire() override;
    void onUserJoined(agora::user_id_t userId) override;
    void onUserLeft(agora::user_id_t userId,
                    agora::rtc::USER_OFFLINE_REASON_TYPE reason) override;
    void onLastmileQuality(const agora::rtc::QUALITY_TYPE quality) override {}
    void onTransportStats(const agora::rtc::RtcStats& stats) override {}
    void onLastmileProbeResult(const agora::rtc::LastmileProbeResult& result) override {}
    void onChannelMediaRelayStateChanged(int state, int code) override {}

    void waitUntilConnected(int timeout_ms);

   private:
    AgoraSender* sender_;
    std::mutex connect_mutex_;
    std::condition_variable connect_cv_;
    std::atomic<bool> connected_{false};
  };

  void emitEvent(const std::string& event, const std::string& detail = "");

  agora::base::IAgoraService* service_ = nullptr;
  agora::agora_refptr<agora::rtc::IRtcConnection> connection_;
  agora::agora_refptr<agora::rtc::ILocalAudioTrack> audio_track_;
  agora::agora_refptr<agora::rtc::IAudioPcmDataSender> pcm_sender_;
  std::unique_ptr<ConnectionObserver> observer_;

  AgoraEventCallback callback_;
  std::atomic<bool> connected_{false};
  std::mutex users_mutex_;
  std::set<std::string> remote_users_;
};

}  // namespace snora
