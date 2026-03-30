#include "ipc/socket_server.h"

#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace snora {

SocketServer::SocketServer(const std::string &socket_path)
    : socket_path_(socket_path) {
  // Remove existing socket file
  unlink(socket_path.c_str());

  server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    throw std::runtime_error("Failed to create socket");
  }

  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(server_fd_, reinterpret_cast<struct sockaddr *>(&addr),
           sizeof(addr)) < 0) {
    ::close(server_fd_);
    throw std::runtime_error("Failed to bind socket: " + socket_path);
  }

  if (listen(server_fd_, 1) < 0) {
    ::close(server_fd_);
    throw std::runtime_error("Failed to listen on socket");
  }
}

SocketServer::~SocketServer() { close(); }

bool SocketServer::accept_client() {
  client_fd_ = accept(server_fd_, nullptr, nullptr);
  return client_fd_ >= 0;
}

bool SocketServer::send_message(const IpcMessage &msg) {
  if (client_fd_ < 0)
    return false;

  auto data = encode_message(msg);
  ssize_t written = write(client_fd_, data.data(), data.size());
  return written == static_cast<ssize_t>(data.size());
}

std::vector<IpcMessage> SocketServer::poll_messages(int timeout_ms) {
  if (client_fd_ < 0)
    return {};

  struct pollfd pfd {};
  pfd.fd = client_fd_;
  pfd.events = POLLIN;

  int ret = poll(&pfd, 1, timeout_ms);
  if (ret <= 0 || !(pfd.revents & POLLIN))
    return {};

  uint8_t buf[4096];
  ssize_t n = read(client_fd_, buf, sizeof(buf));
  if (n <= 0) {
    ::close(client_fd_);
    client_fd_ = -1;
    return {};
  }

  return decoder_.feed(buf, static_cast<size_t>(n));
}

void SocketServer::close() {
  if (client_fd_ >= 0) {
    ::close(client_fd_);
    client_fd_ = -1;
  }
  if (server_fd_ >= 0) {
    ::close(server_fd_);
    server_fd_ = -1;
  }
  unlink(socket_path_.c_str());
}

} // namespace snora
