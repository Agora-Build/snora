#include <gtest/gtest.h>
#include "ipc/message.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// E2E test: spawn the real snora-engine binary, connect via IPC, exercise
// the init → state_update → shutdown flow, and verify responses.
// ---------------------------------------------------------------------------

class EngineE2ETest : public ::testing::Test {
 protected:
  std::string socket_path_;
  std::string engine_path_;
  pid_t engine_pid_ = -1;
  int client_fd_ = -1;
  snora::MessageDecoder decoder_;

  void SetUp() override {
    socket_path_ = "/tmp/snora_e2e_" + std::to_string(getpid()) + ".sock";

    // Engine binary is in the build directory next to the test binary
    engine_path_ = "./snora-engine";
    if (!fs::exists(engine_path_)) {
      GTEST_SKIP() << "snora-engine binary not found at " << engine_path_;
    }
  }

  void TearDown() override {
    if (client_fd_ >= 0) ::close(client_fd_);
    if (engine_pid_ > 0) {
      kill(engine_pid_, SIGTERM);
      int status;
      waitpid(engine_pid_, &status, 0);
    }
    fs::remove(socket_path_);
  }

  // Spawn engine process with --socket argument
  void spawnEngine() {
    engine_pid_ = fork();
    ASSERT_GE(engine_pid_, 0) << "fork() failed";

    if (engine_pid_ == 0) {
      // Child: exec the engine
      // Redirect stderr to /dev/null to keep test output clean
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) { dup2(devnull, STDERR_FILENO); ::close(devnull); }

      execl(engine_path_.c_str(), "snora-engine",
            "--socket", socket_path_.c_str(),
            "--gpu", "0", nullptr);
      _exit(127);  // exec failed
    }

    // Parent: wait for socket file to appear
    for (int i = 0; i < 100; ++i) {
      if (fs::exists(socket_path_)) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(fs::exists(socket_path_)) << "Engine didn't create socket file";
  }

  // Connect to the engine's Unix socket
  void connectToEngine() {
    client_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(client_fd_, 0);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    int ret = connect(client_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(ret, 0) << "Failed to connect to engine socket";
  }

  // Send an IPC message
  void sendMessage(const snora::IpcMessage& msg) {
    auto data = snora::encode_message(msg);
    ssize_t written = write(client_fd_, data.data(), data.size());
    ASSERT_EQ(written, static_cast<ssize_t>(data.size()));
  }

  // Receive messages with timeout
  std::vector<snora::IpcMessage> receiveMessages(int timeout_ms = 2000) {
    struct pollfd pfd{};
    pfd.fd = client_fd_;
    pfd.events = POLLIN;

    std::vector<snora::IpcMessage> result;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
      int remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0) break;

      int ret = poll(&pfd, 1, std::min(remaining, 100));
      if (ret > 0 && (pfd.revents & POLLIN)) {
        uint8_t buf[4096];
        ssize_t n = read(client_fd_, buf, sizeof(buf));
        if (n <= 0) break;
        auto msgs = decoder_.feed(buf, static_cast<size_t>(n));
        result.insert(result.end(), msgs.begin(), msgs.end());
        if (!result.empty()) break;  // got at least one message
      }
    }
    return result;
  }

  // Wait for a specific message type
  snora::IpcMessage waitForMessage(const std::string& type, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
      int remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()).count();
      auto msgs = receiveMessages(std::min(remaining, 500));
      for (auto& m : msgs) {
        if (m.type == type) return m;
      }
    }
    ADD_FAILURE() << "Timed out waiting for message type: " << type;
    return {};
  }
};

TEST_F(EngineE2ETest, InitAckAndRunningStatus) {
  spawnEngine();
  connectToEngine();

  // Send init message
  snora::IpcMessage init;
  init.type = "init";
  init.data = {
    {"app_id", "test-app"},
    {"token", "test-token"},
    {"channel", "test-channel"},
    {"preferences", {
      {"soundscape", "rain"},
      {"binaural_beats", true},
      {"volume", 0.7}
    }}
  };
  sendMessage(init);

  // Should receive ack
  auto ack = waitForMessage("ack");
  EXPECT_EQ(ack.type, "ack");

  // Should receive "running" status
  auto status = waitForMessage("status");
  EXPECT_EQ(status.type, "status");
  EXPECT_EQ(status.data["reason"], "running");
}

TEST_F(EngineE2ETest, StateUpdateGetsAcked) {
  spawnEngine();
  connectToEngine();

  // Init first
  snora::IpcMessage init;
  init.type = "init";
  init.data = {
    {"app_id", "test"},
    {"token", "tok"},
    {"channel", "ch"},
    {"preferences", {{"soundscape", "ocean"}, {"volume", 0.5}}}
  };
  sendMessage(init);
  waitForMessage("ack");
  waitForMessage("status");

  // Send state update
  snora::IpcMessage update;
  update.type = "state_update";
  update.data = {
    {"mood", "calm"},
    {"heart_rate", 65},
    {"hrv", 55},
    {"respiration_rate", 12.0},
    {"stress_level", 0.2}
  };
  sendMessage(update);

  auto ack = waitForMessage("ack");
  EXPECT_EQ(ack.type, "ack");
}

TEST_F(EngineE2ETest, MultipleStateUpdatesAllAcked) {
  spawnEngine();
  connectToEngine();

  // Init
  snora::IpcMessage init;
  init.type = "init";
  init.data = {
    {"app_id", "test"},
    {"token", "tok"},
    {"channel", "ch"},
    {"preferences", {{"soundscape", "wind"}, {"volume", 0.6}}}
  };
  sendMessage(init);
  waitForMessage("ack");
  waitForMessage("status");

  // Send 5 state updates in sequence
  for (int i = 0; i < 5; ++i) {
    snora::IpcMessage update;
    update.type = "state_update";
    update.data = {
      {"mood", i < 3 ? "anxious" : "calm"},
      {"heart_rate", 90 - i * 5},
      {"hrv", 25 + i * 5},
      {"respiration_rate", 20.0 - i * 2.0},
      {"stress_level", 0.8 - i * 0.15}
    };
    sendMessage(update);

    auto ack = waitForMessage("ack");
    EXPECT_EQ(ack.type, "ack") << "Missing ack for state_update " << i;
  }
}

TEST_F(EngineE2ETest, ShutdownGracefully) {
  spawnEngine();
  connectToEngine();

  // Init
  snora::IpcMessage init;
  init.type = "init";
  init.data = {
    {"app_id", "test"},
    {"token", "tok"},
    {"channel", "ch"},
    {"preferences", {{"soundscape", "rain"}, {"volume", 0.5}}}
  };
  sendMessage(init);
  waitForMessage("ack");
  waitForMessage("status");

  // Let it run for a bit (generate a few frames)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Send shutdown
  snora::IpcMessage shutdown;
  shutdown.type = "shutdown";
  sendMessage(shutdown);

  auto ack = waitForMessage("ack");
  EXPECT_EQ(ack.type, "ack");

  // Engine should exit cleanly
  int status;
  int wait_result = waitpid(engine_pid_, &status, 0);
  EXPECT_GT(wait_result, 0);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);

  engine_pid_ = -1;  // prevent TearDown from killing again
}

TEST_F(EngineE2ETest, TokenUpdateGetsAcked) {
  spawnEngine();
  connectToEngine();

  // Init
  snora::IpcMessage init;
  init.type = "init";
  init.data = {
    {"app_id", "test"},
    {"token", "old-token"},
    {"channel", "ch"},
    {"preferences", {{"soundscape", "rain"}, {"volume", 0.5}}}
  };
  sendMessage(init);
  waitForMessage("ack");
  waitForMessage("status");

  // Send token update
  snora::IpcMessage token_update;
  token_update.type = "token_update";
  token_update.data = {{"token", "new-token-abc123"}};
  sendMessage(token_update);

  auto ack = waitForMessage("ack");
  EXPECT_EQ(ack.type, "ack");
}

TEST_F(EngineE2ETest, FullSessionLifecycle) {
  spawnEngine();
  connectToEngine();

  // 1. Init with rain soundscape
  snora::IpcMessage init;
  init.type = "init";
  init.data = {
    {"app_id", "production-app"},
    {"token", "agora-token"},
    {"channel", "sleep-session-42"},
    {"preferences", {
      {"soundscape", "rain"},
      {"binaural_beats", true},
      {"volume", 0.8}
    }},
    {"assets_path", ""}
  };
  sendMessage(init);
  EXPECT_EQ(waitForMessage("ack").type, "ack");
  EXPECT_EQ(waitForMessage("status").data["reason"], "running");

  // 2. Simulate user going from anxious to calm over 3 updates
  std::vector<std::pair<std::string, float>> mood_progression = {
    {"anxious", 0.8f},
    {"neutral", 0.5f},
    {"calm", 0.2f},
  };

  for (auto& [mood, stress] : mood_progression) {
    snora::IpcMessage update;
    update.type = "state_update";
    update.data = {
      {"mood", mood},
      {"heart_rate", static_cast<int>(90 - stress * 30)},
      {"hrv", static_cast<int>(25 + (1 - stress) * 35)},
      {"respiration_rate", 20.0 - (1 - stress) * 10.0},
      {"stress_level", stress}
    };
    sendMessage(update);
    EXPECT_EQ(waitForMessage("ack").type, "ack");

    // Let engine process some audio frames between updates
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // 3. Renew token mid-session
  snora::IpcMessage token;
  token.type = "token_update";
  token.data = {{"token", "renewed-token"}};
  sendMessage(token);
  EXPECT_EQ(waitForMessage("ack").type, "ack");

  // 4. Graceful shutdown
  snora::IpcMessage shutdown;
  shutdown.type = "shutdown";
  sendMessage(shutdown);
  EXPECT_EQ(waitForMessage("ack").type, "ack");

  // 5. Verify clean exit
  int status;
  waitpid(engine_pid_, &status, 0);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  engine_pid_ = -1;
}
