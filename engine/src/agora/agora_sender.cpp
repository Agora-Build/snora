#include "agora/agora_sender.h"

#include <cstdio>

namespace snora {

AgoraSender::AgoraSender() = default;

AgoraSender::~AgoraSender() {
  leave();
}

#ifndef SNORA_USE_AGORA

// ---------------------------------------------------------------------------
// Stub implementation — built when Agora SDK is not available.
// Logs to stderr (JSON lines) and discards all audio frames.
// ---------------------------------------------------------------------------

bool AgoraSender::init(const std::string& app_id, const std::string& token,
                        const std::string& channel,
                        AgoraEventCallback callback) {
  (void)token;
  callback_  = callback;
  connected_ = true;
  fprintf(stderr,
          "{\"level\":\"info\",\"msg\":\"Agora stub: init\","
          "\"app_id\":\"%s\",\"channel\":\"%s\"}\n",
          app_id.c_str(), channel.c_str());
  return true;
}

bool AgoraSender::sendFrame(const int16_t* /*data*/, int /*num_samples*/) {
  return connected_;
}

bool AgoraSender::renewToken(const std::string& /*new_token*/) {
  return connected_;
}

void AgoraSender::leave() {
  if (connected_) {
    fprintf(stderr,
            "{\"level\":\"info\",\"msg\":\"Agora stub: left channel\"}\n");
    connected_ = false;
  }
}

#else  // SNORA_USE_AGORA

// ---------------------------------------------------------------------------
// Real Agora SDK implementation — requires AGORA_SDK_DIR at build time.
// TODO: implement using Agora Server Gateway SDK 4.x.
// ---------------------------------------------------------------------------

bool AgoraSender::init(const std::string&, const std::string&,
                        const std::string&, AgoraEventCallback) {
  return false;
}

bool AgoraSender::sendFrame(const int16_t*, int) {
  return false;
}

bool AgoraSender::renewToken(const std::string&) {
  return false;
}

void AgoraSender::leave() {}

#endif  // SNORA_USE_AGORA

}  // namespace snora
