#include <stdio.h>
#include "h264_cuda.h"
#include "h264_idct_cuda.h"
#include "h264_mc_cuda.h"
#include "../h264.h"
#include "../opencl/h264_opencl.h"

#if ARCH_NVIDIA_CUDA
int gpu_cuda_construct_environment(H264Context *h)
{
    //using the default device (device 0) to run the cuda
    // Program Setup
    if(gpumode==GPU_IDCT||gpumode==GPU_TOTAL)
        idct_cuda_construct_env(h);
    if(gpumode==GPU_MC_NOVERLAP||gpumode==GPU_MC||gpumode==GPU_TOTAL)
        mc_cuda_construct_env(h);
    return 0;
}

int gpu_cuda_destroy_environment(){
    if(gpumode==GPU_IDCT||gpumode==GPU_TOTAL)
        idct_cuda_destruct_env();
    if(gpumode==GPU_MC_NOVERLAP||gpumode==GPU_MC||gpumode==GPU_TOTAL)
        mc_cuda_destruct_env();
    return 0;
}
#endif

