#ifndef H264_CUDA_H
#define H264_CUDA_H

#include "config.h"
#include "../h264_types.h"

    #if ARCH_NVIDIA_CUDA
    #include <cuda_runtime_api.h>
    #include <cuda_runtime.h>
    extern int gpu_mode;
    extern int gpu_cuda_construct_environment(H264Context *h);
    extern int gpu_cuda_destroy_environment();
    #endif
#endif

