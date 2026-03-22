#pragma once

#include "ipc/message.h"
#include <string>
#include <functional>

namespace snora {

class SocketServer {
 public:
  explicit SocketServer(const std::string& socket_path);
  ~SocketServer();

  // Block until a client connects
  bool accept_client();

  // Send a message to the connected client
  bool send_message(const IpcMessage& msg);

  // Read available data and return decoded messages
  std::vector<IpcMessage> poll_messages(int timeout_ms = 0);

  // Close the connection and clean up
  void close();

  bool is_connected() const { return client_fd_ >= 0; }

 private:
  std::string socket_path_;
  int server_fd_ = -1;
  int client_fd_ = -1;
  MessageDecoder decoder_;
};

}  // namespace snora
