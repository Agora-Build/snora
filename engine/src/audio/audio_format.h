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
