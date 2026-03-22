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
  src/ipc/message.cpp
  src/ipc/socket_server.cpp
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

# Tests
if(SNORA_BUILD_TESTS)
  FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.15.2 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()

  add_executable(snora-engine-tests
    tests/test_param_smoother.cpp
    tests/test_message.cpp
  )
  target_link_libraries(snora-engine-tests PRIVATE snora-engine-lib GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(snora-engine-tests)
endif()
