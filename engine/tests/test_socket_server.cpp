#include <gtest/gtest.h>
#include "ipc/socket_server.h"
#include "ipc/message.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

// Helper: connect a raw Unix socket client to the server
static int connect_client(const std::string& path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Helper: send a raw IPC message over a file descriptor
static void send_raw_message(int fd, const snora::IpcMessage& msg) {
  auto data = snora::encode_message(msg);
  write(fd, data.data(), data.size());
}

// Helper: read raw bytes from fd
static std::vector<uint8_t> read_raw(int fd, size_t max_bytes = 4096) {
  std::vector<uint8_t> buf(max_bytes);
  ssize_t n = read(fd, buf.data(), buf.size());
  if (n <= 0) return {};
  buf.resize(n);
  return buf;
}

class SocketServerTest : public ::testing::Test {
 protected:
  std::string socket_path_;

  void SetUp() override {
    socket_path_ = "/tmp/snora_test_sock_" + std::to_string(getpid()) + ".sock";
  }

  void TearDown() override {
    fs::remove(socket_path_);
  }
};

TEST_F(SocketServerTest, AcceptsClientConnection) {
  snora::SocketServer server(socket_path_);

  // Connect in a thread since accept_client blocks
  std::thread client_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int fd = connect_client(socket_path_);
    ASSERT_GE(fd, 0);
    ::close(fd);
  });

  bool accepted = server.accept_client();
  EXPECT_TRUE(accepted);
  EXPECT_TRUE(server.is_connected());

  client_thread.join();
  server.close();
}

TEST_F(SocketServerTest, SendAndReceiveMessage) {
  snora::SocketServer server(socket_path_);

  int client_fd = -1;
  std::thread client_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client_fd = connect_client(socket_path_);
  });

  server.accept_client();
  client_thread.join();
  ASSERT_GE(client_fd, 0);

  // Client sends a message to server
  snora::IpcMessage msg;
  msg.type = "init";
  msg.data = {{"app_id", "test-app"}, {"channel", "ch1"}};
  send_raw_message(client_fd, msg);

  // Give the kernel a moment to deliver the data
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Server polls and receives
  auto received = server.poll_messages(100);
  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received[0].type, "init");
  EXPECT_EQ(received[0].data["app_id"], "test-app");
  EXPECT_EQ(received[0].data["channel"], "ch1");

  ::close(client_fd);
  server.close();
}

TEST_F(SocketServerTest, ServerSendsMessageToClient) {
  snora::SocketServer server(socket_path_);

  int client_fd = -1;
  std::thread client_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client_fd = connect_client(socket_path_);
  });

  server.accept_client();
  client_thread.join();
  ASSERT_GE(client_fd, 0);

  // Server sends ack
  snora::IpcMessage ack;
  ack.type = "ack";
  bool sent = server.send_message(ack);
  EXPECT_TRUE(sent);

  // Client reads
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto raw = read_raw(client_fd);
  ASSERT_FALSE(raw.empty());

  // Decode
  snora::MessageDecoder decoder;
  auto msgs = decoder.feed(raw.data(), raw.size());
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0].type, "ack");

  ::close(client_fd);
  server.close();
}

TEST_F(SocketServerTest, MultipleMessagesInSequence) {
  snora::SocketServer server(socket_path_);

  int client_fd = -1;
  std::thread client_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client_fd = connect_client(socket_path_);
  });

  server.accept_client();
  client_thread.join();
  ASSERT_GE(client_fd, 0);

  // Client sends 3 messages rapidly
  for (int i = 0; i < 3; ++i) {
    snora::IpcMessage msg;
    msg.type = "state_update";
    msg.data = {{"heart_rate", 70 + i}, {"stress_level", 0.5f}};
    send_raw_message(client_fd, msg);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Server should receive all 3
  auto received = server.poll_messages(100);
  EXPECT_EQ(received.size(), 3u);
  for (size_t i = 0; i < received.size(); ++i) {
    EXPECT_EQ(received[i].type, "state_update");
    EXPECT_EQ(received[i].data["heart_rate"], static_cast<int>(70 + i));
  }

  ::close(client_fd);
  server.close();
}

TEST_F(SocketServerTest, DetectsClientDisconnect) {
  snora::SocketServer server(socket_path_);

  int client_fd = -1;
  std::thread client_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client_fd = connect_client(socket_path_);
  });

  server.accept_client();
  client_thread.join();
  ASSERT_GE(client_fd, 0);

  EXPECT_TRUE(server.is_connected());

  // Client disconnects
  ::close(client_fd);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Server should detect disconnection on next poll
  server.poll_messages(100);
  EXPECT_FALSE(server.is_connected());

  server.close();
}

TEST_F(SocketServerTest, PollReturnsEmptyOnTimeout) {
  snora::SocketServer server(socket_path_);

  int client_fd = -1;
  std::thread client_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client_fd = connect_client(socket_path_);
  });

  server.accept_client();
  client_thread.join();
  ASSERT_GE(client_fd, 0);

  // No data sent — poll should return empty
  auto received = server.poll_messages(10);
  EXPECT_TRUE(received.empty());

  ::close(client_fd);
  server.close();
}

TEST_F(SocketServerTest, CleanupRemovesSocketFile) {
  {
    snora::SocketServer server(socket_path_);
    EXPECT_TRUE(fs::exists(socket_path_));
    server.close();
  }
  EXPECT_FALSE(fs::exists(socket_path_));
}
