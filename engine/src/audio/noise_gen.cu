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
