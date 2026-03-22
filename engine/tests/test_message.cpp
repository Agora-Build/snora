#include <gtest/gtest.h>
#include "ipc/message.h"

TEST(IpcMessage, EncodeAndDecode) {
  snora::IpcMessage msg;
  msg.type = "state_update";
  msg.data = {{"heart_rate", 68}, {"mood", "calm"}};

  auto encoded = snora::encode_message(msg);

  snora::MessageDecoder decoder;
  auto messages = decoder.feed(encoded.data(), encoded.size());

  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0].type, "state_update");
  EXPECT_EQ(messages[0].data["heart_rate"], 68);
  EXPECT_EQ(messages[0].data["mood"], "calm");
}

TEST(IpcMessage, FragmentedInput) {
  snora::IpcMessage msg;
  msg.type = "ack";

  auto encoded = snora::encode_message(msg);

  snora::MessageDecoder decoder;
  // Feed byte by byte
  std::vector<snora::IpcMessage> all_messages;
  for (size_t i = 0; i < encoded.size(); ++i) {
    auto msgs = decoder.feed(&encoded[i], 1);
    all_messages.insert(all_messages.end(), msgs.begin(), msgs.end());
  }

  ASSERT_EQ(all_messages.size(), 1u);
  EXPECT_EQ(all_messages[0].type, "ack");
}

TEST(IpcMessage, MultipleMessagesInOneBuffer) {
  snora::IpcMessage msg1;
  msg1.type = "ack";
  snora::IpcMessage msg2;
  msg2.type = "status";
  msg2.data = {{"reason", "running"}};

  auto enc1 = snora::encode_message(msg1);
  auto enc2 = snora::encode_message(msg2);
  enc1.insert(enc1.end(), enc2.begin(), enc2.end());

  snora::MessageDecoder decoder;
  auto messages = decoder.feed(enc1.data(), enc1.size());

  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(messages[0].type, "ack");
  EXPECT_EQ(messages[1].type, "status");
  EXPECT_EQ(messages[1].data["reason"], "running");
}

TEST(IpcMessage, AckMessage) {
  snora::IpcMessage ack;
  ack.type = "ack";
  auto j = ack.to_json();
  EXPECT_EQ(j["type"], "ack");
  EXPECT_FALSE(j.contains("data"));
}
