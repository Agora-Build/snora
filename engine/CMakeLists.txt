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

# Collect sources incrementally — add files as they are created
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
target_include_directories(snora-engine-lib PUBLIC src)
target_link_libraries(snora-engine-lib PUBLIC nlohmann_json::nlohmann_json)

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
  )
  target_link_libraries(snora-engine-tests PRIVATE snora-engine-lib GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(snora-engine-tests)
endif()
