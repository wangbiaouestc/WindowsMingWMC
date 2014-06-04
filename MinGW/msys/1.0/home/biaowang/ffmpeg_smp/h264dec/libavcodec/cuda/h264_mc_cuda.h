#ifndef H264_MC_CUDA_H
#define H264_MC_CUDA_H


#include "h264_cuda.h"
#include "../opencl/h264_mc_opencl.h"
extern void gpu_cuda_mc(H264Context *h, MBRecContext *d, H264Slice *s, SliceBufferEntry *sbe);
extern int mc_cuda_construct_env(H264Context *h);
#if ARCH_NVIDIA_CUDA
    typedef struct{
        int16_t  *luma_weight;
        int16_t  *chroma_weight;
        int16_t  *implicit_weight;
        uint16_t (*sub_mb_type)[4];
        uint8_t  *chroma_output[2];
        uint8_t   *luma_output;
        MVVector *shared_vectors[2];
        MCREFER  *shared_ref[2];
        MC_AUX_T *uni_pred_auxiliary;
        uint8_t *luma_references;
        uint8_t *chroma_references[2];
        cudaStream_t *streams[2];
        int pic_width;
        int pic_height;
        int use_weight;
        int use_weight_chroma;
        int luma_log2_weight_denom;
        int chroma_log2_weight_denom;
    } cuda_mc_env_t;
    extern void launch_kernel_mc(int *luma_globalsize,int *luma_localsize,int *chroma_globalsize,int *chroma_localsize, int weighted_prediction, cuda_mc_env_t *p);
    extern uint8_t *mc_gpu_cuda_luma_references;
    extern uint8_t *mc_gpu_cuda_chroma_references[2];
    extern int mc_cuda_reference_size;
    extern cudaStream_t mc_streams[];
    
#endif 

#endif