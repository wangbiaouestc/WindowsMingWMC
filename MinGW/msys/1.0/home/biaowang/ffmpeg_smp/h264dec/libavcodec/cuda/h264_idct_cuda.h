#ifndef H264_IDCT_CUDA_H
#define H264_IDCT_CUDA_H


#include "h264_cuda.h"
extern int gpu_cuda_idct(H264Context *h);
extern int idct_cuda_construct_env(H264Context *h);
    #if ARCH_NVIDIA_CUDA
    typedef struct {
        cudaStream_t idct_streams[2];
        int16_t *idct_dev_mem[2];
        cudaEvent_t idct_event[2];
        int isprofile;
    } cuda_idct_env_t;
    extern void launch_kernel_idct (int *globalsize, int *localsize, int *idct8_globalsize, int *idct8_localsize, double *idct_gpu_time_fine, cuda_idct_env_t *p);
    #endif
#endif 