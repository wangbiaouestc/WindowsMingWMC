#include <stdio.h>
#include "h264_idct_cuda.h"
#include "../opencl/h264_idct_opencl.h"

#if ARCH_NVIDIA_CUDA

typedef struct {
    int idct_4x4;
    int idct_8x8;
} d_size_idct;

static cuda_idct_env_t idct_env;

int gpu_cuda_idct(H264Context *h){
    //collect information shared by all kernels
    cudaError_t ciErrNum;
    d_size_idct idct_size;
    int tmp = compact_mem[IDCT_Mem_queue.fi].compact_count*sizeof(short)*16;
    //make it BASELINE_THREAD_BLCOK_MAPPED_DATA_8x8 alignment
    idct_size.idct_4x4 = tmp%TBSIZE_2_MAPPED_DATA_4x4?(tmp/TBSIZE_2_MAPPED_DATA_4x4+1)*TBSIZE_2_MAPPED_DATA_4x4:tmp;
    tmp = compact_mem[IDCT_Mem_queue.fi].idct8_compact_count*sizeof(short)*16;
    idct_size.idct_8x8 = tmp%TBSIZE_2_MAPPED_DATA_8x8?(tmp/TBSIZE_2_MAPPED_DATA_8x8+1)*TBSIZE_2_MAPPED_DATA_8x8:tmp;
    //FIXME: if input frame or sequence have no 8x8 blocks, zero length memory will result into a error
    if(idct_size.idct_8x8==0)
        idct_size.idct_8x8=16;
    cuda_idct_env_t *p = &idct_env;
    //one for 4x4 and the other for 8x8
    
    ciErrNum = cudaMemcpyAsync(p->idct_dev_mem[0],compact_mem[IDCT_Mem_queue.fi].idct_non_zero_blocks,idct_size.idct_4x4,cudaMemcpyHostToDevice,p->idct_streams[0]);
    ciErrNum = cudaMemcpyAsync(p->idct_dev_mem[1],compact_mem[IDCT_Mem_queue.fi].idct8_non_zero_blocks,idct_size.idct_8x8,cudaMemcpyHostToDevice,p->idct_streams[1]);

    int gridsize[2];
    int localsize[2];
    int idct8_gridsize[2];
    int idct8_localsize[2];

    int global_dim = idct_size.idct_4x4/TBSIZE_2_MAPPED_DATA_4x4;
    int idct8_global_dim = idct_size.idct_8x8/TBSIZE_2_MAPPED_DATA_8x8;

    gridsize[0]=global_dim;
    gridsize[1]=1;
    localsize[0] = 32;
    localsize[1] = TBSIZE_2_WARP_IN_THREAD_BLOCKS;

    idct8_gridsize[0]= idct8_global_dim;
    idct8_gridsize[1]= 1;
    idct8_localsize[0] = 32;
    idct8_localsize[1] = TBSIZE_2_WARP_IN_THREAD_BLOCKS;

    launch_kernel_idct(gridsize,localsize,idct8_gridsize,idct8_localsize,idct_gpu_time_fine,p);

    cudaMemcpyAsync(compact_mem[IDCT_Mem_queue.fi].idct_non_zero_blocks,p->idct_dev_mem[0],idct_size.idct_4x4,cudaMemcpyDeviceToHost,p->idct_streams[0]);
    cudaMemcpyAsync(compact_mem[IDCT_Mem_queue.fi].idct8_non_zero_blocks,p->idct_dev_mem[1],idct_size.idct_8x8,cudaMemcpyDeviceToHost,p->idct_streams[1]);
    cudaStreamSynchronize(p->idct_streams[0]);
    cudaStreamSynchronize(p->idct_streams[1]);

    compact_mem[IDCT_Mem_queue.fi].idct8_compact_count=0;
    compact_mem[IDCT_Mem_queue.fi].compact_count=0;
    return 0;
}

int idct_cuda_construct_env(H264Context *h){
    cuda_idct_env_t *p= &idct_env;
    int mb_num = (h->height/16)*(h->width/16);
    for(int i=0;i<2;i++){
        cudaStreamCreate(&p->idct_streams[i]);
        cudaHostAlloc((void**)&p->idct_dev_mem[i],MB_RESIDUAL_SIZE*mb_num*sizeof(short),cudaHostAllocDefault);
        if(h->profile==3)
            cudaEventCreate(&p->idct_event[i]);
    }
    p->isprofile = h->profile==3?1:0;
    //construct the host memory objects
    idct_cpu_mem_construct(h);
}

int idct_cuda_destruct_env(void)
{
    idct_cpu_mem_destruct();
    cuda_idct_env_t *p= &idct_env;
    for(int i=0;i<2;i++){
        if(p->isprofile)
            cudaEventDestroy(p->idct_event[i]);
        cudaFreeHost(p->idct_dev_mem[i]);
        cudaStreamDestroy(p->idct_streams[i]);
    }
    return 0;
}

#endif