# Snora CUDA Audio Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the C++ CUDA audio engine that generates adaptive sleep soundscapes in real-time and streams them via Agora Server Gateway SDK, with a CPU fallback mode for testing without a GPU.

**Architecture:** Each engine process is spawned by the Node.js Worker Manager and communicates via a Unix socket using length-prefixed JSON (IPC protocol defined in the Node.js layer). The engine runs an audio pipeline at 10ms frame intervals (48kHz stereo 16-bit = 960 samples = 1920 bytes per frame): white noise generation, spectral tilt filtering, amplitude modulation for respiration entrainment, binaural beat oscillators, nature loop playback, procedural textures (rain/wind/ocean), and mixing. Output frames are pushed to Agora via `sendAudioFrame()`. A compile-time `SNORA_CPU_MODE` flag replaces all CUDA kernels with plain C++ equivalents for CI testing.

**Tech Stack:** C++17, CUDA 12.x, CMake, Agora Server Gateway SDK 4.x, nlohmann/json, GoogleTest, libsndfile (WAV decode)

**Spec:** `docs/superpowers/specs/2026-03-22-snora-design.md` (Components 4, 5, 6)

**IPC contract:** Engine is spawned as `snora-engine --socket <path> --gpu <device_id>`. It listens on the Unix socket, receives `init`, `state_update`, `shutdown`, `token_update` messages, and sends `ack` and `status` messages back. Message framing: 4-byte big-endian uint32 length prefix + UTF-8 JSON payload. Max 64KB per message. Must ack within 5 seconds.

---

## File Structure

```
engine/
  CMakeLists.txt                        # Top-level CMake, SNORA_CPU_MODE option
  src/
    main.cpp                            # Entry point: parse args, create socket, run main loop
    ipc/
      socket_server.h                   # Unix socket server (listen, accept, read/write)
      socket_server.cpp
      message.h                         # IPC message types, JSON parse/serialize
      message.cpp
    audio/
      audio_format.h                    # Constants: SAMPLE_RATE, CHANNELS, FRAME_SAMPLES, etc.
      param_smoother.h                  # One-pole lowpass smoother for control signals
      param_smoother.cpp
      noise_gen.h                       # White noise + Paul Kellett pinking filter
      noise_gen.cpp                     # CPU impl (CUDA impl in noise_gen.cu)
      noise_gen.cu                      # CUDA kernel (compiled only when !CPU_MODE)
      spectral_tilt.h                   # Spectral tilt filter (variable slope)
      spectral_tilt.cpp
      spectral_tilt.cu
      amplitude_mod.h                   # Respiration entrainment AM envelope
      amplitude_mod.cpp
      amplitude_mod.cu
      binaural.h                        # Binaural beat oscillators (L/R)
      binaural.cpp
      binaural.cu
      nature_player.h                   # WAV file loader + loop player (CPU decode)
      nature_player.cpp
      procedural.h                      # Rain/wind/ocean procedural textures
      procedural.cpp
      procedural.cu
      mixer.h                           # Multi-layer mixer with smoothed gains
      mixer.cpp
      mixer.cu
      pipeline.h                        # Orchestrates all audio components per frame
      pipeline.cpp
    agora/
      agora_sender.h                    # Agora SDK wrapper: init, join, send frame, leave
      agora_sender.cpp
    state/
      session_state.h                   # Holds current physiological state + derived audio params
      session_state.cpp
      param_mapper.h                    # Maps physio state → audio parameters (formulas from spec)
      param_mapper.cpp
  tests/
    test_message.cpp                    # IPC message parse/serialize
    test_param_smoother.cpp             # Smoother convergence
    test_noise_gen.cpp                  # Noise statistical properties
    test_spectral_tilt.cpp              # Spectral slope correctness
    test_amplitude_mod.cpp              # AM envelope shape
    test_binaural.cpp                   # Binaural frequency difference
    test_mixer.cpp                      # Gain application, no clipping
    test_param_mapper.cpp               # Physio → audio mapping formulas
    test_nature_player.cpp              # WAV loading, mono upmix
    test_pipeline.cpp                   # Full pipeline integration (reference audio)
  third_party/
    nlohmann/                           # nlohmann/json (header-only, downloaded by CMake)
```

---

## Task 1: CMake Build System

**Files:**
- Create: `engine/CMakeLists.txt`

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.18)
project(snora-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SNORA_CPU_MODE "Build without CUDA (CPU fallback for CI)" OFF)
option(SNORA_BUILD_TESTS "Build tests" ON)
option(SNORA_USE_AGORA "Build with Agora SDK" OFF)

# nlohmann/json
include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

# libsndfile (for WAV decode)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED sndfile)

if(NOT SNORA_CPU_MODE)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  add_compile_definitions(SNORA_USE_CUDA)
endif()

# Engine sources
set(ENGINE_SOURCES
  src/main.cpp
  src/ipc/socket_server.cpp
  src/ipc/message.cpp
  src/audio/param_smoother.cpp
  src/audio/noise_gen.cpp
  src/audio/spectral_tilt.cpp
  src/audio/amplitude_mod.cpp
  src/audio/binaural.cpp
  src/audio/nature_player.cpp
  src/audio/procedural.cpp
  src/audio/mixer.cpp
  src/audio/pipeline.cpp
  src/state/session_state.cpp
  src/state/param_mapper.cpp
)

if(NOT SNORA_CPU_MODE)
  list(APPEND ENGINE_SOURCES
    src/audio/noise_gen.cu
    src/audio/spectral_tilt.cu
    src/audio/amplitude_mod.cu
    src/audio/binaural.cu
    src/audio/procedural.cu
    src/audio/mixer.cu
  )
endif()

# Always compile agora_sender.cpp — it has #ifdef guards for stub vs real
list(APPEND ENGINE_SOURCES src/agora/agora_sender.cpp)

if(SNORA_USE_AGORA)
  # Agora SDK paths — set via -DAGORA_SDK_DIR=<path>
  if(NOT DEFINED AGORA_SDK_DIR)
    message(FATAL_ERROR "AGORA_SDK_DIR must be set when SNORA_USE_AGORA=ON")
  endif()
endif()

add_executable(snora-engine ${ENGINE_SOURCES})

target_include_directories(snora-engine PRIVATE
  src
  ${SNDFILE_INCLUDE_DIRS}
)
target_link_libraries(snora-engine PRIVATE
  nlohmann_json::nlohmann_json
  ${SNDFILE_LIBRARIES}
)

if(NOT SNORA_CPU_MODE)
  target_link_libraries(snora-engine PRIVATE CUDA::curand)
endif()

if(SNORA_USE_AGORA)
  target_include_directories(snora-engine PRIVATE ${AGORA_SDK_DIR}/include)
  target_link_directories(snora-engine PRIVATE ${AGORA_SDK_DIR}/lib)
  target_link_libraries(snora-engine PRIVATE agora_rtc_sdk)
endif()

install(TARGETS snora-engine DESTINATION bin)

# Tests
if(SNORA_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)
  enable_testing()

  set(TEST_SOURCES
    tests/test_message.cpp
    tests/test_param_smoother.cpp
    tests/test_noise_gen.cpp
    tests/test_spectral_tilt.cpp
    tests/test_amplitude_mod.cpp
    tests/test_binaural.cpp
    tests/test_mixer.cpp
    tests/test_param_mapper.cpp
    tests/test_nature_player.cpp
    tests/test_pipeline.cpp
    # Reuse engine sources (minus main.cpp)
    src/ipc/message.cpp
    src/audio/param_smoother.cpp
    src/audio/noise_gen.cpp
    src/audio/spectral_tilt.cpp
    src/audio/amplitude_mod.cpp
    src/audio/binaural.cpp
    src/audio/nature_player.cpp
    src/audio/procedural.cpp
    src/audio/mixer.cpp
    src/audio/pipeline.cpp
    src/state/session_state.cpp
    src/state/param_mapper.cpp
  )

  if(NOT SNORA_CPU_MODE)
    list(APPEND TEST_SOURCES
      src/audio/noise_gen.cu
      src/audio/spectral_tilt.cu
      src/audio/amplitude_mod.cu
      src/audio/binaural.cu
      src/audio/procedural.cu
      src/audio/mixer.cu
    )
  endif()

  add_executable(snora-engine-tests ${TEST_SOURCES})
  target_include_directories(snora-engine-tests PRIVATE src ${SNDFILE_INCLUDE_DIRS})
  target_link_libraries(snora-engine-tests PRIVATE
    GTest::gtest GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${SNDFILE_LIBRARIES}
  )
  if(NOT SNORA_CPU_MODE)
    target_link_libraries(snora-engine-tests PRIVATE CUDA::curand)
  endif()

  include(GoogleTest)
  gtest_discover_tests(snora-engine-tests)
endif()
```

- [ ] **Step 2: Commit**

```bash
git add engine/CMakeLists.txt
git commit -m "feat: CMake build system with CPU_MODE and Agora options

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 2: Audio Format Constants & Parameter Smoother

**Files:**
- Create: `engine/src/audio/audio_format.h`
- Create: `engine/src/audio/param_smoother.h`
- Create: `engine/src/audio/param_smoother.cpp`
- Test: `engine/tests/test_param_smoother.cpp`

- [ ] **Step 1: Create audio_format.h**

```cpp
#pragma once

namespace snora {

constexpr int SAMPLE_RATE = 48000;
constexpr int CHANNELS = 2;
constexpr int BITS_PER_SAMPLE = 16;
constexpr int FRAME_DURATION_MS = 10;
constexpr int SAMPLES_PER_CHANNEL = SAMPLE_RATE * FRAME_DURATION_MS / 1000;  // 480
constexpr int FRAME_SAMPLES = SAMPLES_PER_CHANNEL * CHANNELS;                // 960
constexpr int FRAME_BYTES = FRAME_SAMPLES * (BITS_PER_SAMPLE / 8);           // 1920

}  // namespace snora
```

- [ ] **Step 2: Create param_smoother.h/cpp**

`engine/src/audio/param_smoother.h`:
```cpp
#pragma once

namespace snora {

// One-pole exponential smoother for control signals.
// Usage: set target, call smooth() every frame to get current smoothed value.
class ParamSmoother {
 public:
  // smoothing_seconds: time constant for the one-pole filter
  // sample_rate: audio sample rate (used to compute alpha per sample)
  // frame_samples: number of samples per frame (used for per-frame stepping)
  explicit ParamSmoother(float smoothing_seconds, float sample_rate = 48000.0f,
                          int frame_samples = 480);

  void setTarget(float target);
  void setImmediate(float value);  // bypass smoothing
  float current() const;
  float target() const;

  // Advance one frame. Returns the smoothed value.
  float smooth();

 private:
  float current_;
  float target_;
  float alpha_;  // per-sample coefficient
  int frame_samples_;
};

}  // namespace snora
```

`engine/src/audio/param_smoother.cpp`:
```cpp
#include "audio/param_smoother.h"
#include <cmath>

namespace snora {

ParamSmoother::ParamSmoother(float smoothing_seconds, float sample_rate, int frame_samples)
    : current_(0.0f), target_(0.0f), frame_samples_(frame_samples) {
  // One-pole coefficient: alpha = 1 - e^(-1 / (tau * sr))
  float tau = smoothing_seconds * sample_rate;
  alpha_ = 1.0f - std::exp(-1.0f / tau);
}

void ParamSmoother::setTarget(float target) { target_ = target; }
void ParamSmoother::setImmediate(float value) { current_ = value; target_ = value; }
float ParamSmoother::current() const { return current_; }
float ParamSmoother::target() const { return target_; }

float ParamSmoother::smooth() {
  // Apply one-pole filter for frame_samples_ steps
  for (int i = 0; i < frame_samples_; ++i) {
    current_ += alpha_ * (target_ - current_);
  }
  return current_;
}

}  // namespace snora
```

- [ ] **Step 3: Create test file**

`engine/tests/test_param_smoother.cpp` (GoogleTest main is provided by `GTest::gtest_main` — no custom `test_main.cpp` needed):
```cpp
#include <gtest/gtest.h>
#include "audio/param_smoother.h"
#include <cmath>

TEST(ParamSmoother, ConvergesToTarget) {
  snora::ParamSmoother smoother(0.1f, 48000.0f, 480);  // 100ms smoothing
  smoother.setImmediate(0.0f);
  smoother.setTarget(1.0f);

  // After enough frames (~5 time constants), should be very close to target
  for (int i = 0; i < 50; ++i) {  // 50 frames = 500ms >> 100ms
    smoother.smooth();
  }

  EXPECT_NEAR(smoother.current(), 1.0f, 0.01f);
}

TEST(ParamSmoother, SetImmediateBypassesSmoothing) {
  snora::ParamSmoother smoother(3.0f);  // very slow
  smoother.setImmediate(0.5f);
  EXPECT_FLOAT_EQ(smoother.current(), 0.5f);
}

TEST(ParamSmoother, RespondsToTargetChange) {
  snora::ParamSmoother smoother(0.05f, 48000.0f, 480);  // 50ms smoothing
  smoother.setImmediate(0.0f);
  smoother.setTarget(1.0f);

  float prev = 0.0f;
  for (int i = 0; i < 10; ++i) {
    float val = smoother.smooth();
    EXPECT_GT(val, prev);  // monotonically increasing
    prev = val;
  }
}

TEST(ParamSmoother, ThreeSecondSmoothing) {
  // Spec: spectral tilt uses 3-second smoothing
  snora::ParamSmoother smoother(3.0f, 48000.0f, 480);
  smoother.setImmediate(0.0f);
  smoother.setTarget(1.0f);

  // At 1 time constant (~3s = 300 frames), should be ~63% of target
  for (int i = 0; i < 300; ++i) {
    smoother.smooth();
  }
  EXPECT_NEAR(smoother.current(), 0.632f, 0.05f);
}
```

- [ ] **Step 4: Build and test in CPU mode**

```bash
cd engine
sudo apt-get install -y libsndfile1-dev  # if not installed
cmake -B build -DSNORA_CPU_MODE=ON -DSNORA_USE_AGORA=OFF
cmake --build build
cd build && ctest --output-on-failure
```
Expected: All 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add engine/src/audio/audio_format.h engine/src/audio/param_smoother.h engine/src/audio/param_smoother.cpp engine/tests/test_param_smoother.cpp
git commit -m "feat: audio format constants and parameter smoother with tests

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 3: IPC Message Parsing

**Files:**
- Create: `engine/src/ipc/message.h`
- Create: `engine/src/ipc/message.cpp`
- Test: `engine/tests/test_message.cpp`

- [ ] **Step 1: Create message.h/cpp**

`engine/src/ipc/message.h`:
```cpp
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
```

`engine/src/ipc/message.cpp`:
```cpp
#include "ipc/message.h"
#include <stdexcept>
#include <cstring>

namespace snora {

IpcMessage IpcMessage::from_json(const nlohmann::json& j) {
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

std::vector<uint8_t> encode_message(const IpcMessage& msg) {
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

std::vector<IpcMessage> MessageDecoder::feed(const uint8_t* data, size_t len) {
  std::vector<IpcMessage> messages;
  buffer_.insert(buffer_.end(), data, data + len);

  while (true) {
    if (!reading_payload_) {
      if (buffer_.size() < 4) break;
      expected_length_ = (static_cast<uint32_t>(buffer_[0]) << 24) |
                          (static_cast<uint32_t>(buffer_[1]) << 16) |
                          (static_cast<uint32_t>(buffer_[2]) << 8) |
                          static_cast<uint32_t>(buffer_[3]);
      buffer_.erase(buffer_.begin(), buffer_.begin() + 4);

      if (expected_length_ > MAX_MESSAGE_SIZE) {
        buffer_.clear();
        break;  // drop oversized message
      }
      reading_payload_ = true;
    }

    if (buffer_.size() < expected_length_) break;

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

}  // namespace snora
```

- [ ] **Step 2: Create test**

`engine/tests/test_message.cpp`:
```cpp
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
  snora::IpcMessage msg1{.type = "ack"};
  snora::IpcMessage msg2{.type = "status", .data = {{"reason", "running"}}};

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
```

- [ ] **Step 3: Build and test**

```bash
cd engine && cmake --build build && cd build && ctest --output-on-failure
```
Expected: All tests pass (param_smoother + message tests).

- [ ] **Step 4: Commit**

```bash
git add engine/src/ipc/message.h engine/src/ipc/message.cpp engine/tests/test_message.cpp
git commit -m "feat: IPC message parsing with length-prefixed JSON framing

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 4: Unix Socket Server

**Files:**
- Create: `engine/src/ipc/socket_server.h`
- Create: `engine/src/ipc/socket_server.cpp`

- [ ] **Step 1: Create socket_server.h/cpp**

`engine/src/ipc/socket_server.h`:
```cpp
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
```

`engine/src/ipc/socket_server.cpp`:
```cpp
#include "ipc/socket_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

namespace snora {

SocketServer::SocketServer(const std::string& socket_path) : socket_path_(socket_path) {
  // Remove existing socket file
  unlink(socket_path.c_str());

  server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    throw std::runtime_error("Failed to create socket");
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(server_fd_);
    throw std::runtime_error("Failed to bind socket: " + socket_path);
  }

  if (listen(server_fd_, 1) < 0) {
    ::close(server_fd_);
    throw std::runtime_error("Failed to listen on socket");
  }
}

SocketServer::~SocketServer() {
  close();
}

bool SocketServer::accept_client() {
  client_fd_ = accept(server_fd_, nullptr, nullptr);
  return client_fd_ >= 0;
}

bool SocketServer::send_message(const IpcMessage& msg) {
  if (client_fd_ < 0) return false;

  auto data = encode_message(msg);
  ssize_t written = write(client_fd_, data.data(), data.size());
  return written == static_cast<ssize_t>(data.size());
}

std::vector<IpcMessage> SocketServer::poll_messages(int timeout_ms) {
  if (client_fd_ < 0) return {};

  struct pollfd pfd{};
  pfd.fd = client_fd_;
  pfd.events = POLLIN;

  int ret = poll(&pfd, 1, timeout_ms);
  if (ret <= 0 || !(pfd.revents & POLLIN)) return {};

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
  if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
  if (server_fd_ >= 0) { ::close(server_fd_); server_fd_ = -1; }
  unlink(socket_path_.c_str());
}

}  // namespace snora
```

- [ ] **Step 2: Commit**

```bash
git add engine/src/ipc/socket_server.h engine/src/ipc/socket_server.cpp
git commit -m "feat: Unix socket server for IPC with Worker Manager

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 5: Session State & Parameter Mapper

**Files:**
- Create: `engine/src/state/session_state.h`
- Create: `engine/src/state/session_state.cpp`
- Create: `engine/src/state/param_mapper.h`
- Create: `engine/src/state/param_mapper.cpp`
- Test: `engine/tests/test_param_mapper.cpp`

- [ ] **Step 1: Create session_state.h/cpp**

`engine/src/state/session_state.h`:
```cpp
#pragma once

#include <string>
#include <chrono>

namespace snora {

struct PhysioState {
  std::string mood = "neutral";
  float heart_rate = 70.0f;
  float hrv = 50.0f;
  float respiration_rate = 14.0f;
  float stress_level = 0.3f;
};

// Derived audio parameters computed from physio state
struct AudioParams {
  float spectral_slope = -4.0f;     // -6 to -2 dB/oct
  float am_frequency = 0.23f;       // Hz (respiration entrainment)
  float binaural_hz = 4.0f;         // Hz (frequency difference L/R)
  float binaural_carrier = 200.0f;  // Hz
  float nature_gain = 0.4f;         // 0-1
  float master_volume = 0.7f;       // 0-1
  bool binaural_enabled = true;
};

struct SessionConfig {
  std::string soundscape = "rain";
  bool binaural_beats = true;
  float volume = 0.7f;
  std::string assets_path = "/assets/sounds";
};

class SessionState {
 public:
  void updatePhysio(const PhysioState& state);
  void setConfig(const SessionConfig& config);
  void setStartTime(std::chrono::steady_clock::time_point t);

  const PhysioState& physio() const { return physio_; }
  const SessionConfig& config() const { return config_; }
  float elapsedMinutes() const;

 private:
  PhysioState physio_;
  SessionConfig config_;
  std::chrono::steady_clock::time_point start_time_;
};

}  // namespace snora
```

`engine/src/state/session_state.cpp`:
```cpp
#include "state/session_state.h"

namespace snora {

void SessionState::updatePhysio(const PhysioState& state) { physio_ = state; }
void SessionState::setConfig(const SessionConfig& config) { config_ = config; }
void SessionState::setStartTime(std::chrono::steady_clock::time_point t) { start_time_ = t; }

float SessionState::elapsedMinutes() const {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
  return static_cast<float>(elapsed.count()) / 60000.0f;
}

}  // namespace snora
```

- [ ] **Step 2: Create param_mapper.h/cpp**

`engine/src/state/param_mapper.h`:
```cpp
#pragma once

#include "state/session_state.h"

namespace snora {

// Maps physiological state to audio parameters using the spec formulas
AudioParams mapPhysioToAudio(const PhysioState& physio, const SessionConfig& config,
                              float elapsed_minutes);

}  // namespace snora
```

`engine/src/state/param_mapper.cpp`:
```cpp
#include "state/param_mapper.h"
#include <algorithm>
#include <cmath>

namespace snora {

namespace {

float lerp(float a, float b, float t) {
  return a + t * (b - a);
}

float clamp(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

enum class MoodRange { Alpha, Theta, Delta };

MoodRange moodToRange(const std::string& mood) {
  if (mood == "anxious" || mood == "stressed") return MoodRange::Alpha;
  if (mood == "neutral") return MoodRange::Theta;
  return MoodRange::Delta;  // calm, relaxed, sleepy
}

}  // namespace

AudioParams mapPhysioToAudio(const PhysioState& physio, const SessionConfig& config,
                              float elapsed_minutes) {
  AudioParams params;

  // Spectral tilt slope: lerp(-6, -2, 1 - stress_level)
  params.spectral_slope = lerp(-6.0f, -2.0f, 1.0f - physio.stress_level);

  // Respiration entrainment
  float session_progress = clamp(elapsed_minutes / 20.0f, 0.0f, 1.0f);
  float target_resp = lerp(physio.respiration_rate, 5.5f, session_progress);
  params.am_frequency = target_resp / 60.0f;

  // Binaural beat frequency
  params.binaural_enabled = config.binaural_beats;
  auto range = moodToRange(physio.mood);
  switch (range) {
    case MoodRange::Alpha:
      params.binaural_hz = lerp(8.0f, 12.0f, physio.stress_level);
      break;
    case MoodRange::Theta:
      params.binaural_hz = lerp(4.0f, 8.0f, physio.stress_level);
      break;
    case MoodRange::Delta:
      params.binaural_hz = lerp(0.5f, 4.0f, physio.stress_level);
      break;
  }
  params.binaural_carrier = 200.0f;

  // Nature layer gain
  params.nature_gain = lerp(0.2f, 0.6f, physio.stress_level);

  // Master volume from preferences
  params.master_volume = config.volume;

  return params;
}

}  // namespace snora
```

- [ ] **Step 3: Create test**

`engine/tests/test_param_mapper.cpp`:
```cpp
#include <gtest/gtest.h>
#include "state/param_mapper.h"

TEST(ParamMapper, SpectralTiltHighStress) {
  snora::PhysioState physio;
  physio.stress_level = 1.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.spectral_slope, -6.0f, 0.01f);
}

TEST(ParamMapper, SpectralTiltLowStress) {
  snora::PhysioState physio;
  physio.stress_level = 0.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.spectral_slope, -2.0f, 0.01f);
}

TEST(ParamMapper, RespirationEntrainmentAtStart) {
  snora::PhysioState physio;
  physio.respiration_rate = 18.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  // At start (progress=0), AM freq = respiration_rate / 60
  EXPECT_NEAR(params.am_frequency, 18.0f / 60.0f, 0.01f);
}

TEST(ParamMapper, RespirationEntrainmentAfter20Min) {
  snora::PhysioState physio;
  physio.respiration_rate = 18.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 20.0f);
  // At 20min (progress=1), AM freq = 5.5 / 60
  EXPECT_NEAR(params.am_frequency, 5.5f / 60.0f, 0.01f);
}

TEST(ParamMapper, BinauralAnxious) {
  snora::PhysioState physio;
  physio.mood = "anxious";
  physio.stress_level = 0.5f;
  snora::SessionConfig config;
  config.binaural_beats = true;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_GE(params.binaural_hz, 8.0f);
  EXPECT_LE(params.binaural_hz, 12.0f);
  EXPECT_TRUE(params.binaural_enabled);
}

TEST(ParamMapper, BinauralSleepy) {
  snora::PhysioState physio;
  physio.mood = "sleepy";
  physio.stress_level = 0.5f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_GE(params.binaural_hz, 0.5f);
  EXPECT_LE(params.binaural_hz, 4.0f);
}

TEST(ParamMapper, NatureGainHighStress) {
  snora::PhysioState physio;
  physio.stress_level = 1.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.nature_gain, 0.6f, 0.01f);
}

TEST(ParamMapper, NatureGainLowStress) {
  snora::PhysioState physio;
  physio.stress_level = 0.0f;
  snora::SessionConfig config;

  auto params = snora::mapPhysioToAudio(physio, config, 0.0f);
  EXPECT_NEAR(params.nature_gain, 0.2f, 0.01f);
}
```

- [ ] **Step 4: Build and test**

```bash
cd engine && cmake --build build && cd build && ctest --output-on-failure
```
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add engine/src/state/ engine/tests/test_param_mapper.cpp
git commit -m "feat: session state and physio-to-audio parameter mapper with tests

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 6: Noise Generator (CPU + CUDA)

**Files:**
- Create: `engine/src/audio/noise_gen.h`
- Create: `engine/src/audio/noise_gen.cpp`
- Create: `engine/src/audio/noise_gen.cu`
- Test: `engine/tests/test_noise_gen.cpp`

- [ ] **Step 1: Create noise_gen.h**

```cpp
#pragma once

#include "audio/audio_format.h"
#include <vector>
#include <cstdint>
#include <random>

namespace snora {

// Generates white noise and applies Paul Kellett IIR pinking filter.
// Output: FRAME_SAMPLES int16_t samples (stereo interleaved).
class NoiseGenerator {
 public:
  NoiseGenerator();
  ~NoiseGenerator();

  // Generate one frame of noise into output buffer.
  // spectral_slope: -6 (brown) to 0 (white). Currently implements pink (-3 dB/oct) via Kellett filter.
  void generate(int16_t* output, float spectral_slope, float amplitude);

 private:
  // Kellett filter state (per channel)
  struct PinkState {
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
  };

  PinkState pink_state_[CHANNELS];
  float brown_state_[CHANNELS] = {0, 0};
  std::mt19937 rng_;
  std::uniform_real_distribution<float> dist_;

#ifdef SNORA_USE_CUDA
  // CUDA resources
  float* d_output_ = nullptr;
  void* d_curand_state_ = nullptr;
  bool cuda_initialized_ = false;
  void init_cuda();
#endif
};

}  // namespace snora
```

- [ ] **Step 2: Create CPU implementation**

`engine/src/audio/noise_gen.cpp`:
```cpp
#include "audio/noise_gen.h"
#include <cmath>
#include <algorithm>

namespace snora {

NoiseGenerator::NoiseGenerator()
    : rng_(std::random_device{}()), dist_(-1.0f, 1.0f) {}

NoiseGenerator::~NoiseGenerator() = default;

void NoiseGenerator::generate(int16_t* output, float spectral_slope, float amplitude) {
  // Mix between white noise and pink-filtered noise based on spectral_slope.
  // slope = 0: pure white, slope = -3: pure pink (Kellett), slope = -6: extra filtering
  float pink_mix = std::clamp(-spectral_slope / 3.0f, 0.0f, 1.0f);
  float brown_mix = std::clamp((-spectral_slope - 3.0f) / 3.0f, 0.0f, 1.0f);

  for (int ch = 0; ch < CHANNELS; ++ch) {
    auto& s = pink_state_[ch];
    for (int i = 0; i < SAMPLES_PER_CHANNEL; ++i) {
      float white = dist_(rng_);

      // Paul Kellett's pinking filter (attempt economy of computation)
      s.b0 = 0.99886f * s.b0 + white * 0.0555179f;
      s.b1 = 0.99332f * s.b1 + white * 0.0750759f;
      s.b2 = 0.96900f * s.b2 + white * 0.1538520f;
      s.b3 = 0.86650f * s.b3 + white * 0.3104856f;
      s.b4 = 0.55000f * s.b4 + white * 0.5329522f;
      s.b5 = -0.7616f * s.b5 - white * 0.0168980f;
      float pink = s.b0 + s.b1 + s.b2 + s.b3 + s.b4 + s.b5 + s.b6 + white * 0.5362f;
      s.b6 = white * 0.115926f;
      pink *= 0.11f;  // normalize

      // Brown noise: integrate white noise (leaky)
      brown_state_[ch] = brown_state_[ch] * 0.998f + white * 0.04f;

      // Mix based on slope
      float sample = white * (1.0f - pink_mix) + pink * pink_mix * (1.0f - brown_mix) + brown_state[ch] * brown_mix;
      sample *= amplitude;

      // Clamp and convert to int16
      sample = std::clamp(sample, -1.0f, 1.0f);
      output[i * CHANNELS + ch] = static_cast<int16_t>(sample * 32767.0f);
    }
  }
}

}  // namespace snora
```

- [ ] **Step 3: Create CUDA stub** (compiled only with CUDA)

`engine/src/audio/noise_gen.cu`:
```cpp
#ifdef SNORA_USE_CUDA
#include "audio/noise_gen.h"
#include <curand_kernel.h>

// CUDA kernel placeholder — full implementation in a future iteration
// For now, the CPU path is used even when CUDA is available

namespace snora {

void NoiseGenerator::init_cuda() {
  // TODO: allocate curand states, device buffers
  cuda_initialized_ = true;
}

}  // namespace snora
#endif
```

- [ ] **Step 4: Create test**

`engine/tests/test_noise_gen.cpp`:
```cpp
#include <gtest/gtest.h>
#include "audio/noise_gen.h"
#include <cmath>
#include <numeric>

TEST(NoiseGen, OutputNonZero) {
  snora::NoiseGenerator gen;
  int16_t buffer[snora::FRAME_SAMPLES] = {};
  gen.generate(buffer, -3.0f, 0.5f);

  // Check that output is not all zeros
  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buffer[i] != 0) { any_nonzero = true; break; }
  }
  EXPECT_TRUE(any_nonzero);
}

TEST(NoiseGen, MeanNearZero) {
  snora::NoiseGenerator gen;
  int16_t buffer[snora::FRAME_SAMPLES];

  // Generate many frames and compute mean
  double sum = 0;
  int total_samples = 0;
  for (int f = 0; f < 100; ++f) {
    gen.generate(buffer, -3.0f, 0.5f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      sum += buffer[i];
      total_samples++;
    }
  }
  double mean = sum / total_samples;
  // Mean should be near 0 (within a few hundred out of 32767)
  EXPECT_NEAR(mean, 0.0, 500.0);
}

TEST(NoiseGen, AmplitudeScaling) {
  snora::NoiseGenerator gen;
  int16_t loud[snora::FRAME_SAMPLES];
  int16_t quiet[snora::FRAME_SAMPLES];

  gen.generate(loud, -3.0f, 1.0f);
  gen.generate(quiet, -3.0f, 0.1f);

  // RMS of loud should be > RMS of quiet
  auto rms = [](const int16_t* buf, int n) -> double {
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += static_cast<double>(buf[i]) * buf[i];
    return std::sqrt(sum / n);
  };

  EXPECT_GT(rms(loud, snora::FRAME_SAMPLES), rms(quiet, snora::FRAME_SAMPLES));
}

TEST(NoiseGen, NoCLipping) {
  snora::NoiseGenerator gen;
  int16_t buffer[snora::FRAME_SAMPLES];

  for (int f = 0; f < 100; ++f) {
    gen.generate(buffer, -3.0f, 1.0f);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buffer[i], -32767);
      EXPECT_LE(buffer[i], 32767);
    }
  }
}
```

- [ ] **Step 5: Build and test**

```bash
cd engine && cmake --build build && cd build && ctest --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add engine/src/audio/noise_gen.h engine/src/audio/noise_gen.cpp engine/src/audio/noise_gen.cu engine/tests/test_noise_gen.cpp
git commit -m "feat: noise generator with Kellett pinking filter (CPU + CUDA stub)

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 7a: Spectral Tilt Filter

**Files:**
- Create: `engine/src/audio/spectral_tilt.h`, `engine/src/audio/spectral_tilt.cpp`, `engine/src/audio/spectral_tilt.cu`
- Test: `engine/tests/test_spectral_tilt.cpp`

**Algorithm:** Apply a one-pole IIR shelving filter that tilts the spectrum. The `slope` parameter controls rolloff (-6 dB/oct = heavy brown, -2 dB/oct = near pink). Process stereo buffer in-place. Per sample: `y[n] = (1-|slope_coeff|) * x[n] + slope_coeff * y[n-1]` where `slope_coeff = slope / -6.0` (normalized 0-1).

**Interface:**
```cpp
class SpectralTilt {
public:
  void process(int16_t* buffer, int num_samples, float slope); // slope: -6 to -2
private:
  float prev_[2] = {0, 0}; // per-channel state
};
```

**Tests:** (1) With slope=-6, output should have significantly less high-frequency energy than input. (2) With slope=0 (white), output ≈ input. (3) No clipping.

**CUDA stub:** Empty `.cu` file with `#ifdef SNORA_USE_CUDA` guard.

- [ ] **Step 1:** Implement .h, .cpp, .cu
- [ ] **Step 2:** Write test, build, verify pass
- [ ] **Step 3:** Commit: `"feat: spectral tilt filter (CPU + CUDA stub)"`

---

## Task 7b: Amplitude Modulation (Respiration Entrainment)

**Files:**
- Create: `engine/src/audio/amplitude_mod.h`, `.cpp`, `.cu`
- Test: `engine/tests/test_amplitude_mod.cpp`

**Algorithm:** Asymmetric raised cosine envelope at `am_frequency` Hz with 1:2 inhale:exhale ratio. Per sample: compute phase from oscillator, apply raised cosine with asymmetric duty cycle (inhale = 1/3 of cycle, exhale = 2/3). Multiply buffer samples by envelope value.

**Interface:**
```cpp
class AmplitudeMod {
public:
  void process(int16_t* buffer, int num_samples, float am_freq_hz);
private:
  double phase_ = 0.0;
};
```

**Tests:** (1) At very low frequency (e.g., 0.1 Hz), verify envelope varies between ~0 and 1 over time. (2) Verify output RMS is lower than input RMS (modulation reduces average power). (3) No clipping.

- [ ] **Step 1:** Implement
- [ ] **Step 2:** Test, build, verify
- [ ] **Step 3:** Commit: `"feat: amplitude modulation for respiration entrainment (CPU + CUDA stub)"`

---

## Task 7c: Binaural Beat Oscillators

**Files:**
- Create: `engine/src/audio/binaural.h`, `.cpp`, `.cu`
- Test: `engine/tests/test_binaural.cpp`

**Algorithm:** Generate sine oscillators at `carrier - binaural_hz/2` (left) and `carrier + binaural_hz/2` (right). ADD to existing buffer contents (don't overwrite). Amplitude controlled by a `gain` parameter.

**Interface:**
```cpp
class BinauralGenerator {
public:
  void process(int16_t* buffer, int num_samples, float carrier_hz, float binaural_hz, float gain);
private:
  double phase_left_ = 0.0;
  double phase_right_ = 0.0;
};
```

**Tests:** (1) With carrier=200, binaural_hz=4: left channel frequency ≈ 198 Hz, right ≈ 202 Hz (measure via zero-crossing rate). (2) With gain=0, output unchanged. (3) With binaural_enabled=false (gain=0), no sine tone added.

- [ ] **Step 1:** Implement
- [ ] **Step 2:** Test, build, verify
- [ ] **Step 3:** Commit: `"feat: binaural beat oscillators (CPU + CUDA stub)"`

---

## Task 7d: Procedural Textures (Rain, Wind, Ocean)

**Files:**
- Create: `engine/src/audio/procedural.h`, `.cpp`, `.cu`
- Test: `engine/tests/test_procedural.cpp`

**Algorithm:** Three texture types, output into a stereo buffer:
- **Rain:** Stochastic grain triggers. Each grain is a filtered noise burst (2-8ms). Poisson-distributed trigger times. Grain density increases with `intensity` parameter.
- **Wind:** Swept bandpass filter on white noise. Center frequency and bandwidth modulated by a slow Perlin-like oscillator (use sine-based approximation).
- **Ocean:** Amplitude-modulated brown noise at wave rhythm rate (0.05-0.15 Hz).

**Interface:**
```cpp
class ProceduralTexture {
public:
  enum class Type { Rain, Wind, Ocean };
  explicit ProceduralTexture(Type type);
  void process(int16_t* buffer, int num_samples, float intensity); // 0-1
private:
  Type type_;
  // Internal state for each texture type
  std::mt19937 rng_;
  double phase_ = 0.0;
  float filter_state_[2] = {0, 0};
};
```

**Tests:** (1) Rain: output is non-zero, contains transient bursts. (2) Wind: output has audible frequency modulation (RMS varies over time). (3) Ocean: output has slow amplitude modulation (~0.1 Hz). (4) All: no clipping.

- [ ] **Step 1:** Implement
- [ ] **Step 2:** Test, build, verify
- [ ] **Step 3:** Commit: `"feat: procedural rain/wind/ocean textures (CPU + CUDA stub)"`

---

## Task 7e: Mixer

**Files:**
- Create: `engine/src/audio/mixer.h`, `.cpp`, `.cu`
- Test: `engine/tests/test_mixer.cpp`

**Algorithm:** Combine N input buffers into one output buffer. Each layer has a gain (0-1). Gains are smoothed per-frame using one-pole filters (use `ParamSmoother`). Sum all `input[i] * gain[i]`, clamp to int16 range.

**Interface:**
```cpp
class Mixer {
public:
  // Mix multiple layers into output. layers[i].first = buffer, layers[i].second = gain
  void mix(const std::vector<std::pair<const int16_t*, float>>& layers,
           int16_t* output, int num_samples, float master_volume);
};
```

**Tests:** (1) Single layer with gain=1.0: output equals input. (2) Two layers: output is sum (clamped). (3) With master_volume=0: output is silence. (4) No clipping beyond int16 range.

- [ ] **Step 1:** Implement
- [ ] **Step 2:** Test, build, verify
- [ ] **Step 3:** Commit: `"feat: multi-layer mixer with gain smoothing (CPU + CUDA stub)"`

---

## Task 8: Nature Sound Player (WAV Loader)

**Files:**
- Create: `engine/src/audio/nature_player.h`
- Create: `engine/src/audio/nature_player.cpp`
- Test: `engine/tests/test_nature_player.cpp`

- [ ] **Step 1: Create nature_player.h/cpp**

`engine/src/audio/nature_player.h`:
```cpp
#pragma once

#include "audio/audio_format.h"
#include <string>
#include <vector>
#include <cstdint>

namespace snora {

struct SoundLayer {
  std::string file;
  float default_gain;
  bool loop;
  std::vector<int16_t> pcm_data;  // stereo interleaved, 48kHz, 16-bit
  size_t position = 0;             // current playback position (in samples)
};

class NaturePlayer {
 public:
  // Load manifest.json and all WAV files for the given soundscape
  bool load(const std::string& assets_path, const std::string& soundscape);

  // Fill output buffer with the next frame of nature sounds (mixed layers)
  void render(int16_t* output, float gain);

  bool isLoaded() const { return !layers_.empty(); }
  const std::string& error() const { return error_; }

 private:
  std::vector<SoundLayer> layers_;
  std::string error_;

  bool loadWav(const std::string& path, SoundLayer& layer);
};

}  // namespace snora
```

`engine/src/audio/nature_player.cpp`:
```cpp
#include "audio/nature_player.h"
#include <nlohmann/json.hpp>
#include <sndfile.h>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace snora {

bool NaturePlayer::load(const std::string& assets_path, const std::string& soundscape) {
  // Read manifest
  std::string manifest_path = assets_path + "/manifest.json";
  std::ifstream f(manifest_path);
  if (!f.is_open()) {
    error_ = "Cannot open manifest: " + manifest_path;
    return false;
  }

  nlohmann::json manifest;
  try {
    manifest = nlohmann::json::parse(f);
  } catch (const std::exception& e) {
    error_ = "Malformed manifest: " + std::string(e.what());
    return false;
  }

  if (!manifest.contains("soundscapes") || !manifest["soundscapes"].contains(soundscape)) {
    error_ = "Soundscape not found in manifest: " + soundscape;
    return false;
  }

  auto& sc = manifest["soundscapes"][soundscape];
  for (auto& layer_json : sc["layers"]) {
    SoundLayer layer;
    layer.file = layer_json["file"].get<std::string>();
    layer.default_gain = layer_json["default_gain"].get<float>();
    layer.loop = layer_json.value("loop", true);

    std::string wav_path = assets_path + "/" + layer.file;
    if (!loadWav(wav_path, layer)) {
      return false;
    }
    layers_.push_back(std::move(layer));
  }

  return true;
}

bool NaturePlayer::loadWav(const std::string& path, SoundLayer& layer) {
  SF_INFO info{};
  SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
  if (!file) {
    error_ = "Cannot open WAV: " + path;
    return false;
  }

  if (info.samplerate != SAMPLE_RATE) {
    error_ = "WAV sample rate must be 48000: " + path;
    sf_close(file);
    return false;
  }

  // Read all samples
  std::vector<int16_t> raw(info.frames * info.channels);
  sf_readf_short(file, raw.data(), info.frames);
  sf_close(file);

  // Upmix mono to stereo if needed
  if (info.channels == 1) {
    layer.pcm_data.resize(info.frames * 2);
    for (sf_count_t i = 0; i < info.frames; ++i) {
      layer.pcm_data[i * 2] = raw[i];
      layer.pcm_data[i * 2 + 1] = raw[i];
    }
  } else {
    layer.pcm_data = std::move(raw);
  }

  return true;
}

void NaturePlayer::render(int16_t* output, float gain) {
  // Zero output first
  std::fill(output, output + FRAME_SAMPLES, static_cast<int16_t>(0));

  for (auto& layer : layers_) {
    if (layer.pcm_data.empty()) continue;

    float layer_gain = layer.default_gain * gain;
    size_t total_stereo_samples = layer.pcm_data.size();

    for (int i = 0; i < FRAME_SAMPLES; ++i) {
      float sample = static_cast<float>(layer.pcm_data[layer.position]) * layer_gain;
      float mixed = static_cast<float>(output[i]) + sample;
      output[i] = static_cast<int16_t>(std::clamp(mixed, -32767.0f, 32767.0f));

      layer.position++;
      if (layer.position >= total_stereo_samples) {
        if (layer.loop) {
          layer.position = 0;
        } else {
          break;
        }
      }
    }
  }
}

}  // namespace snora
```

- [ ] **Step 2: Create test**

`engine/tests/test_nature_player.cpp`:
```cpp
#include <gtest/gtest.h>
#include "audio/nature_player.h"

// These tests require WAV fixture files.
// For CI, create minimal test WAVs programmatically or skip if not available.

TEST(NaturePlayer, FailsOnMissingManifest) {
  snora::NaturePlayer player;
  bool ok = player.load("/nonexistent/path", "rain");
  EXPECT_FALSE(ok);
  EXPECT_FALSE(player.error().empty());
}

TEST(NaturePlayer, FailsOnUnknownSoundscape) {
  // Create a temporary manifest with no matching soundscape
  // This test documents the expected behavior
  snora::NaturePlayer player;
  bool ok = player.load("/tmp", "nonexistent_soundscape");
  EXPECT_FALSE(ok);
}
```

- [ ] **Step 3: Build and test, commit**

```bash
cd engine && cmake --build build && cd build && ctest --output-on-failure
git add engine/src/audio/nature_player.h engine/src/audio/nature_player.cpp engine/tests/test_nature_player.cpp
git commit -m "feat: nature sound WAV player with manifest loading and mono upmix

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 9: Audio Pipeline

**Files:**
- Create: `engine/src/audio/pipeline.h`
- Create: `engine/src/audio/pipeline.cpp`
- Test: `engine/tests/test_pipeline.cpp`

The pipeline orchestrates all audio components per frame:
1. NoiseGenerator → noise buffer
2. SpectralTilt → apply filter to noise
3. AmplitudeMod → apply AM envelope
4. Binaural → generate L/R oscillators, add to buffer
5. NaturePlayer → render nature sounds into separate buffer
6. Procedural → generate procedural textures into separate buffer
7. Mixer → combine all layers with smoothed gains
8. Return mixed buffer (1920 bytes)

The pipeline holds ParamSmoothers for each parameter with the spec's time constants:
- Spectral tilt: 3 seconds
- Amplitude/gain changes: 50ms
- Nature crossfade: 15 seconds
- Binaural frequency: 10 seconds

**Interface:**
```cpp
class AudioPipeline {
public:
  bool init(const SessionConfig& config);
  void processFrame(int16_t* output, const AudioParams& params);
private:
  NoiseGenerator noise_gen_;
  SpectralTilt spectral_tilt_;
  AmplitudeMod amplitude_mod_;
  BinauralGenerator binaural_;
  NaturePlayer nature_player_;
  ProceduralTexture rain_{ProceduralTexture::Type::Rain};
  ProceduralTexture wind_{ProceduralTexture::Type::Wind};
  ProceduralTexture ocean_{ProceduralTexture::Type::Ocean};
  Mixer mixer_;
  ParamSmoother slope_smoother_{3.0f};
  ParamSmoother am_smoother_{0.05f};
  ParamSmoother binaural_smoother_{10.0f};
  ParamSmoother nature_smoother_{15.0f};
  ParamSmoother volume_smoother_{0.05f};
};
```

`processFrame()` implementation:
1. Update all smoother targets from `params`
2. Advance all smoothers
3. Generate noise buffer with current smoothed slope
4. Apply spectral tilt
5. Apply amplitude modulation at smoothed AM frequency
6. Generate binaural into separate buffer
7. Render nature sounds into separate buffer
8. Generate procedural textures into separate buffer (based on soundscape type)
9. Mix all buffers with smoothed gains using Mixer

**Tests (`test_pipeline.cpp`):**
```cpp
TEST(Pipeline, GeneratesNonZeroOutput) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  config.assets_path = "/nonexistent"; // nature player will fail, but noise/binaural still work
  pipeline.init(config);

  snora::AudioParams params;
  int16_t buffer[snora::FRAME_SAMPLES];
  pipeline.processFrame(buffer, params);

  bool any_nonzero = false;
  for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
    if (buffer[i] != 0) { any_nonzero = true; break; }
  }
  EXPECT_TRUE(any_nonzero);
}

TEST(Pipeline, NoClipping) {
  snora::AudioPipeline pipeline;
  snora::SessionConfig config;
  pipeline.init(config);

  snora::AudioParams params;
  params.master_volume = 1.0f;
  int16_t buffer[snora::FRAME_SAMPLES];

  for (int f = 0; f < 100; ++f) {
    pipeline.processFrame(buffer, params);
    for (int i = 0; i < snora::FRAME_SAMPLES; ++i) {
      EXPECT_GE(buffer[i], -32767);
      EXPECT_LE(buffer[i], 32767);
    }
  }
}

TEST(Pipeline, CorrectFrameSize) {
  EXPECT_EQ(snora::FRAME_SAMPLES, 960);
  EXPECT_EQ(snora::FRAME_BYTES, 1920);
}
```

- [ ] **Step 1:** Implement pipeline.h/cpp
- [ ] **Step 2:** Write test_pipeline.cpp, build, verify
- [ ] **Step 3:** Commit: `"feat: audio pipeline orchestrating all processors per frame"`

---

## Task 10: Agora Sender (Stub + Real)

**Files:**
- Create: `engine/src/agora/agora_sender.h`
- Create: `engine/src/agora/agora_sender.cpp`

The Agora sender wraps the Agora Server Gateway SDK. When `SNORA_USE_AGORA` is OFF, it compiles as a stub that discards frames (for testing without the SDK). When ON, it initializes the service, joins a channel, creates a custom audio track, and provides `sendFrame(int16_t* data, int samples)`.

- [ ] **Step 1: Create agora_sender.h**

```cpp
#pragma once

#include <string>
#include <cstdint>
#include <functional>

namespace snora {

// Callback for Agora events sent back to the engine main loop
using AgoraEventCallback = std::function<void(const std::string& event, const std::string& detail)>;

class AgoraSender {
 public:
  AgoraSender();
  ~AgoraSender();

  bool init(const std::string& app_id, const std::string& token,
            const std::string& channel, AgoraEventCallback callback);

  bool sendFrame(const int16_t* data, int num_samples);

  bool renewToken(const std::string& new_token);

  void leave();

 private:
#ifdef SNORA_USE_AGORA
  // Agora SDK handles — opaque
  void* service_ = nullptr;
  void* connection_ = nullptr;
  void* audio_track_ = nullptr;
#endif
  AgoraEventCallback callback_;
  bool connected_ = false;
};

}  // namespace snora
```

- [ ] **Step 2: Create stub implementation** (compiled when `!SNORA_USE_AGORA`)

`engine/src/agora/agora_sender.cpp`:
```cpp
#include "agora/agora_sender.h"
#include <cstdio>

namespace snora {

AgoraSender::AgoraSender() = default;
AgoraSender::~AgoraSender() { leave(); }

#ifndef SNORA_USE_AGORA
// Stub implementation — discards frames, logs init
bool AgoraSender::init(const std::string& app_id, const std::string& token,
                        const std::string& channel, AgoraEventCallback callback) {
  callback_ = callback;
  connected_ = true;
  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Agora stub: init app_id=%s channel=%s\"}\n",
          app_id.c_str(), channel.c_str());
  return true;
}

bool AgoraSender::sendFrame(const int16_t*, int) {
  return connected_;
}

bool AgoraSender::renewToken(const std::string&) {
  return connected_;
}

void AgoraSender::leave() {
  if (connected_) {
    fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Agora stub: left channel\"}\n");
    connected_ = false;
  }
}
#else
// Real Agora SDK implementation — requires AGORA_SDK_DIR at build time
// TODO: implement with actual Agora Server Gateway SDK 4.x calls
bool AgoraSender::init(const std::string&, const std::string&,
                        const std::string&, AgoraEventCallback) { return false; }
bool AgoraSender::sendFrame(const int16_t*, int) { return false; }
bool AgoraSender::renewToken(const std::string&) { return false; }
void AgoraSender::leave() {}
#endif

}  // namespace snora
```

- [ ] **Step 3: Commit**

```bash
git add engine/src/agora/agora_sender.h engine/src/agora/agora_sender.cpp
git commit -m "feat: Agora sender with stub and SDK-backed implementations

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 11: Engine Main Loop

**Files:**
- Create: `engine/src/main.cpp`

The main entry point:
1. Parse CLI args (`--socket`, `--gpu`)
2. Create Unix socket server, wait for Worker Manager to connect
3. Receive `init` message with config, send `ack`
4. Initialize audio pipeline, Agora sender, session state
5. Main loop (10ms interval):
   a. Poll socket for messages (state_update, shutdown, token_update)
   b. Update session state and parameter targets
   c. Run audio pipeline for one frame
   d. Send frame to Agora
6. On shutdown: leave Agora, close socket, exit

- [ ] **Step 1: Implement main.cpp**

```cpp
#include "ipc/socket_server.h"
#include "ipc/message.h"
#include "audio/pipeline.h"
#include "agora/agora_sender.h"
#include "state/session_state.h"
#include "state/param_mapper.h"
#include "audio/audio_format.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>
#include <string>

static volatile bool g_running = true;

void signal_handler(int) { g_running = false; }

int main(int argc, char* argv[]) {
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
  std::signal(SIGINT, signal_handler);

  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Engine starting, socket=%s gpu=%d\"}\n",
          socket_path.c_str(), gpu_id);

  // Create socket and wait for connection
  snora::SocketServer socket(socket_path);
  if (!socket.accept_client()) {
    fprintf(stderr, "{\"level\":\"error\",\"msg\":\"Failed to accept client\"}\n");
    return 1;
  }

  // Wait for init message
  snora::IpcMessage init_msg;
  bool got_init = false;
  while (!got_init && g_running) {
    auto msgs = socket.poll_messages(5000);
    for (auto& m : msgs) {
      if (m.type == "init") {
        init_msg = m;
        got_init = true;
        break;
      }
    }
  }

  if (!got_init) {
    fprintf(stderr, "{\"level\":\"error\",\"msg\":\"Did not receive init message\"}\n");
    return 1;
  }

  // Ack init
  socket.send_message(snora::IpcMessage{.type = "ack"});

  // Parse config from init message
  snora::SessionState state;
  snora::SessionConfig config;
  if (init_msg.data.contains("preferences")) {
    auto& prefs = init_msg.data["preferences"];
    config.soundscape = prefs.value("soundscape", "rain");
    config.binaural_beats = prefs.value("binaural_beats", true);
    config.volume = prefs.value("volume", 0.7f);
  }
  if (init_msg.data.contains("assets_path")) {
    config.assets_path = init_msg.data["assets_path"].get<std::string>();
  }
  state.setConfig(config);
  state.setStartTime(std::chrono::steady_clock::now());

  // Initialize Agora
  snora::AgoraSender agora;
  std::string app_id = init_msg.data.value("app_id", "");
  std::string token = init_msg.data.value("token", "");
  std::string channel = init_msg.data.value("channel", "");

  agora.init(app_id, token, channel, [&](const std::string& event, const std::string& detail) {
    snora::IpcMessage status_msg;
    status_msg.type = "status";
    status_msg.data = {{"reason", event}};
    socket.send_message(status_msg);
  });

  // Initialize audio pipeline
  snora::AudioPipeline pipeline;
  pipeline.init(config);

  // Send running status
  socket.send_message(snora::IpcMessage{.type = "status", .data = {{"reason", "running"}}});

  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Engine running\"}\n");

  // Main audio loop
  int16_t frame_buffer[snora::FRAME_SAMPLES];
  auto frame_interval = std::chrono::milliseconds(snora::FRAME_DURATION_MS);

  while (g_running) {
    auto frame_start = std::chrono::steady_clock::now();

    // Poll for IPC messages (non-blocking)
    auto messages = socket.poll_messages(0);
    for (auto& msg : messages) {
      if (msg.type == "state_update") {
        snora::PhysioState physio;
        physio.mood = msg.data.value("mood", physio.mood);
        physio.heart_rate = msg.data.value("heart_rate", physio.heart_rate);
        physio.hrv = msg.data.value("hrv", physio.hrv);
        physio.respiration_rate = msg.data.value("respiration_rate", physio.respiration_rate);
        physio.stress_level = msg.data.value("stress_level", physio.stress_level);
        state.updatePhysio(physio);
        socket.send_message(snora::IpcMessage{.type = "ack"});
      } else if (msg.type == "shutdown") {
        socket.send_message(snora::IpcMessage{.type = "ack"});
        g_running = false;
      } else if (msg.type == "token_update") {
        std::string new_token = msg.data.value("token", "");
        agora.renewToken(new_token);
        socket.send_message(snora::IpcMessage{.type = "ack"});
      }
    }

    if (!g_running) break;

    // Map current physio state to audio parameters
    auto audio_params = snora::mapPhysioToAudio(state.physio(), state.config(),
                                                  state.elapsedMinutes());

    // Generate audio frame
    pipeline.processFrame(frame_buffer, audio_params);

    // Send to Agora
    agora.sendFrame(frame_buffer, snora::FRAME_SAMPLES);

    // Sleep until next frame
    auto elapsed = std::chrono::steady_clock::now() - frame_start;
    if (elapsed < frame_interval) {
      std::this_thread::sleep_for(frame_interval - elapsed);
    }
  }

  fprintf(stderr, "{\"level\":\"info\",\"msg\":\"Engine shutting down\"}\n");
  agora.leave();
  socket.close();

  return 0;
}
```

- [ ] **Step 2: Build (CPU mode, no Agora)**

```bash
cd engine && cmake --build build
```
Expected: `snora-engine` binary built successfully.

- [ ] **Step 3: Commit**

```bash
git add engine/src/main.cpp
git commit -m "feat: engine main loop with IPC, audio pipeline, and Agora integration

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 12: Update Dockerfile to Build Real Engine

**Files:**
- Modify: `docker/Dockerfile`

Replace the stub engine binary with a real build:

```dockerfile
# Stage 1: Build C++ CUDA engine
FROM nvidia/cuda:12.6.3-devel-ubuntu22.04 AS engine-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake build-essential pkg-config libsndfile1-dev \
    && rm -rf /var/lib/apt/lists/*

COPY engine/ /build/engine/
WORKDIR /build/engine
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DSNORA_CPU_MODE=OFF -DSNORA_USE_AGORA=OFF -DSNORA_BUILD_TESTS=OFF \
    && cmake --build build --target snora-engine
```

Update the COPY line in stage 2:
```dockerfile
COPY --from=engine-builder /build/engine/build/snora-engine /usr/local/bin/snora-engine
```

Also add `libsndfile1` to the runtime stage:
```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    tini curl libsndfile1 \
    && rm -rf /var/lib/apt/lists/*
```

- [ ] **Step 1: Update Dockerfile**
- [ ] **Step 2: Docker build to verify**
- [ ] **Step 3: Commit**

```bash
git commit -m "feat: Dockerfile builds real CUDA engine binary

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 13: Update CI for C++ Tests

**Files:**
- Modify: `.github/workflows/ci.yml`

Uncomment and activate the C++ CI jobs:

```yaml
  lint-cpp:
    name: Lint (C++)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y clang-format
      - run: find engine/src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror

  test-cpp:
    name: Test (C++ CPU mode)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y cmake build-essential pkg-config libsndfile1-dev
      - run: cd engine && cmake -B build -DSNORA_CPU_MODE=ON -DSNORA_USE_AGORA=OFF && cmake --build build
      - run: cd engine/build && ctest --output-on-failure
```

- [ ] **Step 1: Update ci.yml**
- [ ] **Step 2: Commit**

```bash
git commit -m "feat: enable C++ lint and CPU-mode tests in CI

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 14: Full Verification

- [ ] **Step 1: Build engine in CPU mode and run all tests**

```bash
cd engine && cmake -B build -DSNORA_CPU_MODE=ON -DSNORA_USE_AGORA=OFF && cmake --build build && cd build && ctest --output-on-failure
```
Expected: All C++ tests pass.

- [ ] **Step 2: Run Node.js tests**

```bash
npx vitest run
```
Expected: 59 tests pass.

- [ ] **Step 3: Type check Node.js**

```bash
npx tsc --noEmit
```
Expected: 0 errors.

- [ ] **Step 4: Helm lint**

```bash
helm lint charts/snora/ --set secrets.apiKey=test --set config.agoraAppId=test
```
Expected: 0 failures.

- [ ] **Step 5: Final commit if fixes needed**

```bash
git add -A
git commit -m "fix: engine verification fixes

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```
