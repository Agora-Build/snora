#include "ipc/message.h"
#include <cstring>
#include <stdexcept>

namespace snora {

IpcMessage IpcMessage::from_json(const nlohmann::json &j) {
  IpcMessage msg;
  msg.type = j.at("type").get<std::string>();
  if (j.contains("data")) {
    msg.data = j["data"];
  }
  return msg;
}

nlohmann::json IpcMessage::to_json() const {
  nlohmann::json j;
  j["type"] = type;
  if (!data.is_null()) {
    j["data"] = data;
  }
  return j;
}

std::vector<uint8_t> encode_message(const IpcMessage &msg) {
  std::string payload = msg.to_json().dump();
  if (payload.size() > MessageDecoder::MAX_MESSAGE_SIZE) {
    throw std::runtime_error("Message exceeds max size");
  }
  uint32_t len = static_cast<uint32_t>(payload.size());
  std::vector<uint8_t> result(4 + len);
  // Big-endian length prefix
  result[0] = (len >> 24) & 0xFF;
  result[1] = (len >> 16) & 0xFF;
  result[2] = (len >> 8) & 0xFF;
  result[3] = len & 0xFF;
  std::memcpy(result.data() + 4, payload.data(), len);
  return result;
}

std::vector<IpcMessage> MessageDecoder::feed(const uint8_t *data, size_t len) {
  std::vector<IpcMessage> messages;
  buffer_.insert(buffer_.end(), data, data + len);

  while (true) {
    if (!reading_payload_) {
      if (buffer_.size() < 4)
        break;
      expected_length_ = (static_cast<uint32_t>(buffer_[0]) << 24) |
                         (static_cast<uint32_t>(buffer_[1]) << 16) |
                         (static_cast<uint32_t>(buffer_[2]) << 8) |
                         static_cast<uint32_t>(buffer_[3]);
      buffer_.erase(buffer_.begin(), buffer_.begin() + 4);

      if (expected_length_ > MAX_MESSAGE_SIZE) {
        buffer_.clear();
        break; // drop oversized message
      }
      reading_payload_ = true;
    }

    if (buffer_.size() < expected_length_)
      break;

    std::string payload(buffer_.begin(), buffer_.begin() + expected_length_);
    buffer_.erase(buffer_.begin(), buffer_.begin() + expected_length_);
    reading_payload_ = false;

    try {
      auto j = nlohmann::json::parse(payload);
      messages.push_back(IpcMessage::from_json(j));
    } catch (...) {
      // malformed JSON — drop silently
    }
  }

  return messages;
}

} // namespace snora
