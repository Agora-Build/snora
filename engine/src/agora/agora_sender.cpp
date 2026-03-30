#include "agora/agora_sender.h"
#include "audio/audio_format.h"

#include <chrono>
#include <cstdio>

namespace snora {

AgoraSender::AgoraSender() = default;

AgoraSender::~AgoraSender() { leave(); }

void AgoraSender::emitEvent(const std::string &event,
                            const std::string &detail) {
  if (callback_) {
    callback_(event, detail);
  }
}

// ---------------------------------------------------------------------------
// ConnectionObserver
// ---------------------------------------------------------------------------

void AgoraSender::ConnectionObserver::onConnected(
    const agora::rtc::TConnectionInfo &,
    agora::rtc::CONNECTION_CHANGED_REASON_TYPE) {
  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Agora: connected\"}\n");
  connected_ = true;
  connect_cv_.notify_all();
}

void AgoraSender::ConnectionObserver::onDisconnected(
    const agora::rtc::TConnectionInfo &,
    agora::rtc::CONNECTION_CHANGED_REASON_TYPE) {
  fprintf(stderr, "{\"level\":\"warn\",\"msg\":\"Agora: disconnected\"}\n");
  connected_ = false;
  sender_->connected_ = false;
  sender_->emitEvent("agora_disconnected");
}

void AgoraSender::ConnectionObserver::onConnecting(
    const agora::rtc::TConnectionInfo &,
    agora::rtc::CONNECTION_CHANGED_REASON_TYPE) {
  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Agora: connecting\"}\n");
}

void AgoraSender::ConnectionObserver::onReconnecting(
    const agora::rtc::TConnectionInfo &,
    agora::rtc::CONNECTION_CHANGED_REASON_TYPE) {
  fprintf(stderr, "{\"level\":\"warn\",\"msg\":\"Agora: reconnecting\"}\n");
}

void AgoraSender::ConnectionObserver::onReconnected(
    const agora::rtc::TConnectionInfo &,
    agora::rtc::CONNECTION_CHANGED_REASON_TYPE) {
  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Agora: reconnected\"}\n");
  connected_ = true;
  sender_->connected_ = true;
}

void AgoraSender::ConnectionObserver::onConnectionLost(
    const agora::rtc::TConnectionInfo &) {
  fprintf(stderr, "{\"level\":\"error\",\"msg\":\"Agora: connection lost\"}\n");
  connected_ = false;
  sender_->connected_ = false;
  sender_->emitEvent("agora_disconnected");
}

void AgoraSender::ConnectionObserver::onConnectionFailure(
    const agora::rtc::TConnectionInfo &,
    agora::rtc::CONNECTION_CHANGED_REASON_TYPE) {
  fprintf(stderr,
          "{\"level\":\"error\",\"msg\":\"Agora: connection failure\"}\n");
  sender_->connected_ = false;
  sender_->emitEvent("agora_disconnected");
}

void AgoraSender::ConnectionObserver::onTokenPrivilegeWillExpire(const char *) {
  fprintf(stderr, "{\"level\":\"warn\",\"msg\":\"Agora: token expiring\"}\n");
  sender_->emitEvent("token_expiring");
}

void AgoraSender::ConnectionObserver::onTokenPrivilegeDidExpire() {
  fprintf(stderr, "{\"level\":\"error\",\"msg\":\"Agora: token expired\"}\n");
  sender_->emitEvent("token_expiring");
}

void AgoraSender::ConnectionObserver::onUserJoined(agora::user_id_t userId) {
  fprintf(
      stderr,
      "{\"level\":\"info\",\"msg\":\"Agora: user joined\",\"user\":\"%s\"}\n",
      userId ? userId : "unknown");
  {
    std::lock_guard<std::mutex> lock(sender_->users_mutex_);
    sender_->remote_users_.insert(userId ? userId : "");
  }
  sender_->emitEvent("subscriber_joined", userId ? userId : "");
}

void AgoraSender::ConnectionObserver::onUserLeft(
    agora::user_id_t userId, agora::rtc::USER_OFFLINE_REASON_TYPE) {
  fprintf(stderr,
          "{\"level\":\"info\",\"msg\":\"Agora: user left\",\"user\":\"%s\"}\n",
          userId ? userId : "unknown");
  bool no_subscribers = false;
  {
    std::lock_guard<std::mutex> lock(sender_->users_mutex_);
    sender_->remote_users_.erase(userId ? userId : "");
    no_subscribers = sender_->remote_users_.empty();
  }
  if (no_subscribers) {
    sender_->emitEvent("no_subscribers");
  }
}

void AgoraSender::ConnectionObserver::waitUntilConnected(int timeout_ms) {
  std::unique_lock<std::mutex> lock(connect_mutex_);
  if (connected_)
    return;
  connect_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                       [this]() { return connected_.load(); });
}

// ---------------------------------------------------------------------------
// AgoraSender
// ---------------------------------------------------------------------------

bool AgoraSender::init(const std::string &app_id, const std::string &token,
                       const std::string &channel,
                       AgoraEventCallback callback) {
  callback_ = callback;

  // 1. Create and initialize Agora service (global singleton)
  service_ = createAgoraService();
  if (!service_) {
    fprintf(
        stderr,
        "{\"level\":\"error\",\"msg\":\"Failed to create Agora service\"}\n");
    return false;
  }

  agora::base::AgoraServiceConfiguration scfg;
  scfg.appId = app_id.c_str();
  scfg.enableAudioProcessor = true;
  scfg.enableAudioDevice = false;
  scfg.enableVideo = false;

  int init_result = service_->initialize(scfg);
  if (init_result != agora::ERR_OK) {
    fprintf(stderr,
            "{\"level\":\"error\",\"msg\":\"Failed to initialize Agora "
            "service\",\"code\":%d}\n",
            init_result);
    service_ = nullptr;
    return false;
  }

  fprintf(stderr,
          "{\"level\":\"info\",\"msg\":\"Agora service initialized\","
          "\"app_id\":\"%s\"}\n",
          app_id.c_str());

  // 2. Create media node factory and audio PCM sender
  auto factory = service_->createMediaNodeFactory();
  if (!factory) {
    fprintf(stderr, "{\"level\":\"error\",\"msg\":\"Failed to create media "
                    "node factory\"}\n");
    leave();
    return false;
  }

  pcm_sender_ = factory->createAudioPcmDataSender();
  if (!pcm_sender_) {
    fprintf(
        stderr,
        "{\"level\":\"error\",\"msg\":\"Failed to create PCM data sender\"}\n");
    leave();
    return false;
  }

  // 3. Create custom audio track
  audio_track_ = service_->createCustomAudioTrack(pcm_sender_);
  if (!audio_track_) {
    fprintf(stderr,
            "{\"level\":\"error\",\"msg\":\"Failed to create audio track\"}\n");
    leave();
    return false;
  }
  audio_track_->setEnabled(true);

  // 4. Create RTC connection
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;

  connection_ = service_->createRtcConnection(ccfg);
  if (!connection_) {
    fprintf(
        stderr,
        "{\"level\":\"error\",\"msg\":\"Failed to create RTC connection\"}\n");
    leave();
    return false;
  }

  // 5. Register connection observer
  observer_ = std::make_unique<ConnectionObserver>(this);
  connection_->registerObserver(observer_.get());

  // 6. Connect to channel
  int conn_result =
      connection_->connect(token.c_str(), channel.c_str(), "0");
  if (conn_result) {
    fprintf(stderr,
            "{\"level\":\"error\",\"msg\":\"Failed to connect to Agora "
            "channel\",\"code\":%d,\"channel\":\"%s\",\"token_len\":%zu}\n",
            conn_result, channel.c_str(), token.size());
    leave();
    return false;
  }

  // 7. Wait for connection (up to 10 seconds)
  observer_->waitUntilConnected(10000);

  // 8. Publish audio track
  connection_->getLocalUser()->publishAudio(audio_track_);

  connected_ = true;

  fprintf(stderr,
          "{\"level\":\"info\",\"msg\":\"Agora: init complete\","
          "\"app_id\":\"%s\",\"channel\":\"%s\"}\n",
          app_id.c_str(), channel.c_str());
  return true;
}

bool AgoraSender::sendFrame(const int16_t *data, int num_samples) {
  if (!connected_ || !pcm_sender_)
    return false;

  // num_samples is total interleaved (960 for stereo 10ms at 48kHz)
  // sendAudioPcmData wants samples_per_channel (480)
  int samples_per_channel = num_samples / CHANNELS;

  if (pcm_sender_->sendAudioPcmData(
          data,
          0, // capture_timestamp (0 = SDK uses internal clock)
          0, // capture_timestamp_extra
          samples_per_channel, agora::rtc::TWO_BYTES_PER_SAMPLE, CHANNELS,
          SAMPLE_RATE) < 0) {
    return false;
  }
  return true;
}

bool AgoraSender::renewToken(const std::string &new_token) {
  if (!connection_)
    return false;
  return connection_->renewToken(new_token.c_str()) == agora::ERR_OK;
}

void AgoraSender::leave() {
  if (connection_) {
    if (audio_track_) {
      connection_->getLocalUser()->unpublishAudio(audio_track_);
    }
    if (observer_) {
      connection_->unregisterObserver(observer_.get());
    }
    connection_->disconnect();
  }

  if (audio_track_) {
    audio_track_->setEnabled(false);
    audio_track_ = nullptr;
  }

  pcm_sender_ = nullptr;
  connection_ = nullptr;
  observer_.reset();

  if (service_) {
    service_->release();
    service_ = nullptr;
  }

  connected_ = false;

  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Agora: left channel\"}\n");
}

} // namespace snora
