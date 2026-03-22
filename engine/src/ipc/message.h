#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace snora {

struct IpcMessage {
  std::string type;
  nlohmann::json data;

  static IpcMessage from_json(const nlohmann::json& j);
  nlohmann::json to_json() const;
};

// Encode a message with 4-byte big-endian length prefix
std::vector<uint8_t> encode_message(const IpcMessage& msg);

// Stateful decoder for length-prefixed messages from a byte stream
class MessageDecoder {
 public:
  static constexpr uint32_t MAX_MESSAGE_SIZE = 64 * 1024;

  // Feed raw bytes. Returns decoded messages.
  std::vector<IpcMessage> feed(const uint8_t* data, size_t len);

 private:
  std::vector<uint8_t> buffer_;
  uint32_t expected_length_ = 0;
  bool reading_payload_ = false;
};

}  // namespace snora
