cmake_minimum_required(VERSION 3.18)
project(snora-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SNORA_CPU_MODE "Build without CUDA (CPU fallback for CI)" ON)
option(SNORA_BUILD_TESTS "Build tests" ON)

include(FetchContent)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json)

# libsndfile (optional — needed for nature_player)
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(SNDFILE sndfile)
endif()

if(NOT SNORA_CPU_MODE)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  add_compile_definitions(SNORA_USE_CUDA)
endif()

# ---------------------------------------------------------------------------
# Agora Server Gateway SDK (auto-download)
# ---------------------------------------------------------------------------
set(AGORA_SDK_URL "https://download.agora.io/rtsasdk/release/Agora-RTC-x86_64-linux-gnu-v4.4.32-20250425_144419-675648.tgz")
set(AGORA_SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/agora_rtc_sdk/agora_sdk")

if(NOT EXISTS "${AGORA_SDK_DIR}/include/IAgoraService.h")
  message(STATUS "Downloading Agora Server Gateway SDK...")
  FetchContent_Declare(agora_sdk
    URL ${AGORA_SDK_URL}
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/agora_rtc_sdk
  )
  FetchContent_MakeAvailable(agora_sdk)
endif()

set(AGORA_INCLUDE_DIR "${AGORA_SDK_DIR}/include")
set(AGORA_LIB_DIR "${AGORA_SDK_DIR}")

# Collect sources
set(ENGINE_SOURCES
  src/audio/param_smoother.cpp
  src/audio/noise_gen.cpp
  src/audio/spectral_tilt.cpp
  src/audio/amplitude_mod.cpp
  src/audio/binaural.cpp
  src/audio/procedural.cpp
  src/audio/mixer.cpp
  src/audio/nature_player.cpp
  src/audio/pipeline.cpp
  src/agora/agora_sender.cpp
  src/ipc/message.cpp
  src/ipc/socket_server.cpp
  src/state/session_state.cpp
  src/state/param_mapper.cpp
)

add_library(snora-engine-lib STATIC ${ENGINE_SOURCES})
target_include_directories(snora-engine-lib PUBLIC src ${AGORA_INCLUDE_DIR})
target_link_libraries(snora-engine-lib PUBLIC nlohmann_json::nlohmann_json)
target_link_directories(snora-engine-lib PUBLIC ${AGORA_LIB_DIR})
target_link_libraries(snora-engine-lib PUBLIC agora_rtc_sdk)

if(SNDFILE_FOUND)
  target_include_directories(snora-engine-lib PUBLIC ${SNDFILE_INCLUDE_DIRS})
  target_link_libraries(snora-engine-lib PUBLIC ${SNDFILE_LIBRARIES})
endif()

if(NOT SNORA_CPU_MODE)
  target_link_libraries(snora-engine-lib PUBLIC CUDA::curand)
endif()

# Main engine executable
add_executable(snora-engine src/main.cpp)
target_link_libraries(snora-engine PRIVATE snora-engine-lib)

# Set RPATH so the binary can find libagora_rtc_sdk.so at runtime
set_target_properties(snora-engine PROPERTIES
  INSTALL_RPATH "${AGORA_LIB_DIR}"
  BUILD_RPATH "${AGORA_LIB_DIR}"
)

# Tests
if(SNORA_BUILD_TESTS)
  FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.15.2 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()

  add_executable(snora-engine-tests
    tests/test_param_smoother.cpp
    tests/test_message.cpp
    tests/test_param_mapper.cpp
    tests/test_noise_gen.cpp
    tests/test_spectral_tilt.cpp
    tests/test_amplitude_mod.cpp
    tests/test_binaural.cpp
    tests/test_procedural.cpp
    tests/test_mixer.cpp
    tests/test_nature_player.cpp
    tests/test_pipeline.cpp
    tests/test_socket_server.cpp
    tests/test_session_lifecycle.cpp
    tests/test_audio_quality.cpp
    tests/test_e2e_engine.cpp
  )
  target_link_libraries(snora-engine-tests PRIVATE snora-engine-lib GTest::gtest_main)
  set_target_properties(snora-engine-tests PROPERTIES
    BUILD_RPATH "${AGORA_LIB_DIR}"
  )
  include(GoogleTest)
  gtest_discover_tests(snora-engine-tests
    DISCOVERY_MODE PRE_TEST
  )
endif()
