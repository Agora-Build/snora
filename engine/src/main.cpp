#include "ipc/socket_server.h"
#include "ipc/message.h"
#include "audio/pipeline.h"
#include "audio/audio_format.h"
#include "agora/agora_sender.h"
#include "state/session_state.h"
#include "state/param_mapper.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static volatile bool g_running = true;

static void signal_handler(int /*sig*/) { g_running = false; }

int main(int argc, char* argv[]) {
  // ---------------------------------------------------------------------- //
  // Parse CLI arguments: --socket <path>  --gpu <device_id>
  // ---------------------------------------------------------------------- //
  std::string socket_path = "/tmp/snora-engine.sock";
  int gpu_id = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
      socket_path = argv[++i];
    } else if (std::strcmp(argv[i], "--gpu") == 0 && i + 1 < argc) {
      gpu_id = std::atoi(argv[++i]);
    }
  }

  std::signal(SIGTERM, signal_handler);
  std::signal(SIGINT,  signal_handler);

  fprintf(stderr,
          "{\"level\":\"info\",\"msg\":\"Engine starting\","
          "\"socket\":\"%s\",\"gpu\":%d}\n",
          socket_path.c_str(), gpu_id);

  // ---------------------------------------------------------------------- //
  // Create Unix socket server and wait for Worker Manager to connect
  // ---------------------------------------------------------------------- //
  snora::SocketServer ipc(socket_path);

  if (!ipc.accept_client()) {
    fprintf(stderr,
            "{\"level\":\"error\",\"msg\":\"Failed to accept client connection\"}\n");
    return 1;
  }

  fprintf(stderr,
          "{\"level\":\"info\",\"msg\":\"Client connected, waiting for init\"}\n");

  // ---------------------------------------------------------------------- //
  // Wait for the init message (5-second window per spec)
  // ---------------------------------------------------------------------- //
  snora::IpcMessage init_msg;
  bool got_init = false;

  while (!got_init && g_running) {
    auto msgs = ipc.poll_messages(5000 /*ms*/);
    for (auto& m : msgs) {
      if (m.type == "init") {
        init_msg  = m;
        got_init  = true;
        break;
      }
    }
  }

  if (!got_init) {
    fprintf(stderr,
            "{\"level\":\"error\",\"msg\":\"Did not receive init message within timeout\"}\n");
    return 1;
  }

  // Acknowledge the init message
  snora::IpcMessage ack;
  ack.type = "ack";
  ipc.send_message(ack);

  // ---------------------------------------------------------------------- //
  // Parse config from init data
  // ---------------------------------------------------------------------- //
  snora::SessionConfig config;

  if (init_msg.data.contains("preferences")) {
    const auto& prefs = init_msg.data["preferences"];
    config.soundscape     = prefs.value("soundscape", config.soundscape);
    config.binaural_beats = prefs.value("binaural_beats", config.binaural_beats);
    config.volume         = prefs.value("volume", config.volume);
  }
  if (init_msg.data.contains("assets_path")) {
    config.assets_path = init_msg.data["assets_path"].get<std::string>();
  }

  snora::SessionState state;
  state.setConfig(config);
  state.setStartTime(std::chrono::steady_clock::now());

  // ---------------------------------------------------------------------- //
  // Initialize Agora sender
  // ---------------------------------------------------------------------- //
  snora::AgoraSender agora;

  const std::string app_id  = init_msg.data.value("app_id",  std::string(""));
  const std::string token   = init_msg.data.value("token",   std::string(""));
  const std::string channel = init_msg.data.value("channel", std::string(""));

  agora.init(app_id, token, channel,
    [&](const std::string& event, const std::string& /*detail*/) {
      snora::IpcMessage status;
      status.type = "status";
      status.data = {{"reason", event}};
      ipc.send_message(status);
    });

  // ---------------------------------------------------------------------- //
  // Initialize audio pipeline
  // ---------------------------------------------------------------------- //
  snora::AudioPipeline pipeline;
  pipeline.init(config);

  // Signal that the engine is running
  snora::IpcMessage running_status;
  running_status.type = "status";
  running_status.data = {{"reason", "running"}};
  ipc.send_message(running_status);

  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Engine running\"}\n");

  // ---------------------------------------------------------------------- //
  // Main audio loop — 10 ms frame intervals
  // ---------------------------------------------------------------------- //
  int16_t frame_buffer[snora::FRAME_SAMPLES];
  const auto frame_interval = std::chrono::milliseconds(snora::FRAME_DURATION_MS);

  while (g_running) {
    const auto frame_start = std::chrono::steady_clock::now();

    // ---- Poll IPC messages (non-blocking) --------------------------------
    const auto messages = ipc.poll_messages(0);
    for (const auto& msg : messages) {
      if (msg.type == "state_update") {
        snora::PhysioState physio = state.physio();  // copy current as base
        physio.mood              = msg.data.value("mood",              physio.mood);
        physio.heart_rate        = msg.data.value("heart_rate",        physio.heart_rate);
        physio.hrv               = msg.data.value("hrv",               physio.hrv);
        physio.respiration_rate  = msg.data.value("respiration_rate",  physio.respiration_rate);
        physio.stress_level      = msg.data.value("stress_level",      physio.stress_level);
        state.updatePhysio(physio);

        snora::IpcMessage a;
        a.type = "ack";
        ipc.send_message(a);

      } else if (msg.type == "shutdown") {
        snora::IpcMessage a;
        a.type = "ack";
        ipc.send_message(a);
        g_running = false;

      } else if (msg.type == "token_update") {
        const std::string new_token = msg.data.value("token", std::string(""));
        agora.renewToken(new_token);

        snora::IpcMessage a;
        a.type = "ack";
        ipc.send_message(a);
      }
    }

    if (!g_running) break;

    // ---- Compute audio parameters from current physio state --------------
    const auto audio_params = snora::mapPhysioToAudio(
        state.physio(), state.config(), state.elapsedMinutes());

    // ---- Generate one audio frame ----------------------------------------
    pipeline.processFrame(frame_buffer, audio_params);

    // ---- Send frame to Agora (stub discards, real SDK streams) ----------
    agora.sendFrame(frame_buffer, snora::FRAME_SAMPLES);

    // ---- Sleep for remainder of 10ms frame interval ----------------------
    const auto elapsed = std::chrono::steady_clock::now() - frame_start;
    if (elapsed < frame_interval) {
      std::this_thread::sleep_for(frame_interval - elapsed);
    }
  }

  // ---------------------------------------------------------------------- //
  // Clean shutdown
  // ---------------------------------------------------------------------- //
  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Engine shutting down\"}\n");
  agora.leave();
  ipc.close();

  return 0;
}
