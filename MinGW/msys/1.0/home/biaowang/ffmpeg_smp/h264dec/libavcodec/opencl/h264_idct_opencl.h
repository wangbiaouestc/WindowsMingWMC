#ifndef H264_IDCT_OPENCL_H
#define H264_IDCT_OPENCL_H

#include "h264_opencl.h"
#include "gpu_common.h"
#include <pthread.h>

#define KERNEL_DIMENSION 2
#define INPUT_MEM_NUM 	1
#define OUTPUT_MEM_NUM	1
#define MB_RESIDUAL_SIZE ((16)*(24))
#define IDCT_KERNEL_COUNT   2u

#define KERNEL_SOURCE_IDCT "../h264dec/libavcodec/opencl/kernelIDCT.cl"


#if HAVE_FRAME_PROFILING
extern int64_t Profile_MBD_Frame[MAX_NUM_FRAME];
#endif

#if HAVE_SPLIT_IDCT
#if HAVE_FRAME_IDCT_PROFILING
extern int64_t Profile_IDCTDecoding_Frame[MAX_NUM_FRAME];
extern int64_t Profile_IDCTExclusiveDecoding_Frame[MAX_NUM_FRAME];
#endif
#endif


#if HAVE_MBD_COMPACT
#if HAVE_COMPACT_PROFILING
extern int64_t Profile_IDCT_Frame_Inputmem_Remapping[MAX_NUM_FRAME][2];
extern int64_t Profile_IDCT_Frame_Processing[MAX_NUM_FRAME];
extern int64_t Profile_Cabac_Frame[MAX_NUM_FRAME];
#endif
#endif

#if HAVE_ENTROPY_IDCT
#if HAVE_ENTROPY_PROFILING
extern int64_t Profile_IDCT_Frame_Inputmem_Remapping[MAX_NUM_FRAME][2];
extern int64_t Profile_IDCT_Frame_Processing[MAX_NUM_FRAME];
extern int64_t Profile_Cabac_Frame[MAX_NUM_FRAME];
#endif
#endif

#if HAVE_COARSEFRAME_PROFILING
extern int64_t Profile_Cabac_Frame[MAX_NUM_FRAME];
#endif

#if HAVE_KERNEL_PROFILING
extern int64_t Profile_IDCT_Kernel_Frame[MAX_NUM_FRAME];
extern int64_t Profile_IDCT_MB_Num_Effective[MAX_NUM_FRAME];
extern cl_ulong Profile_HOST_TO_DEVICE_COPY[MAX_NUM_FRAME];
extern cl_ulong Profile_DEVICE_TO_HOST_COPY[MAX_NUM_FRAME];

extern int64_t Profile_IDCT_Kernel_Frame_CPU[MAX_NUM_FRAME];
extern int64_t Profile_PRE_COMPUTE[MAX_NUM_FRAME];
extern cl_ulong Profile_HOST_TO_DEVICE_COPY_CPU[MAX_NUM_FRAME];
extern cl_ulong Profile_DEVICE_TO_HOST_COPY_CPU[MAX_NUM_FRAME];
#endif


enum {IDCT_KERNEL_4x4, IDCT_KERNEL_8x8, IDCT_KERNEL_PROFILE_PHASES};

extern int64_t global_comp_static[2];
extern int64_t global_comp_time_static[2];
extern int mb_4x4_effective;
extern int mb_4x4_effective_I;
extern int mb_4x4_effective_P;
extern int mb_4x4_effective_B;

extern int mb_4x4_dc_effective;
extern int mb_4x4_dc_effective_I;
extern int mb_4x4_dc_effective_P;
extern int mb_4x4_dc_effective_B;

extern int mb_8x8_effective;
extern int mb_8x8_effective_I ;
extern int mb_8x8_effective_P ;
extern int mb_8x8_effective_B ;

extern int mb_8x8_dc_effective;
extern int mb_8x8_dc_effective_I ;
extern int mb_8x8_dc_effective_P ;
extern int mb_8x8_dc_effective_B ;

extern int mb_4x4_all;
extern int mb_8x8_all;

extern int64_t time_4x4_idct;
extern int64_t time_8x8_idct;
extern int64_t time_4x4_dc_idct;
extern int64_t time_8x8_dc_idct;

extern int mb_4x4_I;
extern int mb_4x4_B;
extern int mb_4x4_P;

extern int mb_4x4_I_effective;
extern int mb_4x4_B_effective;
extern int mb_4x4_P_effective;

extern int mb_8x8_I;
extern int mb_8x8_B;
extern int mb_8x8_P;

extern int mb_8x8_I_effective;
extern int mb_8x8_B_effective;
extern int mb_8x8_P_effective;

extern int num_frame_I;
extern int num_frame_P;
extern int num_frame_B;

extern char SLICE_COLLECTOR[1000];

extern unsigned int idct_opencl_exec_kernel;
extern const char *idct_opencl_kernel_names[];

typedef struct {
	int mb_type;
    uint32_t dequant4_coeff_y;
    uint32_t dequant4_coeff_cb;
    uint32_t dequant4_coeff_cr;
}idct_need_info_t;

typedef struct {
	int compact_count;
	int idct8_compact_count;
	short *idct_non_zero_blocks;
	short *idct8_non_zero_blocks;
} IDCTCompactedBuf;


typedef struct {
	pthread_mutex_t lock;
	pthread_cond_t  cond;	
	int size;
	int cnt;
	int fi;
	int fo;
} IDCTCompactedBufQueue;

extern int16_t *idct_input;
extern int *idct_out_reference;

extern int *idct_out;

extern IDCTCompactedBuf *compact_mem;
extern IDCTCompactedBufQueue IDCT_Mem_queue;

extern H264Mb *input_idct_buffer;
extern double idct_gpu_time_fine[];
extern int gpu_opencl_idct(H264Context *h);
extern void decode_slice_idct_seq(MBRecContext *d, H264Slice *s);
extern void squareAdd8x8(uint8_t *dst,short *src,int stride);
extern void squareAdd4x4(uint8_t *dst,short *src,int stride);
extern void idct_cpu_mem_construct(H264Context *h);
extern void idct_cpu_mem_destruct(void);
extern int idct_opencl_construct_env(H264Context *d);
extern int idct_opencl_destruct_env(void);
extern void cpu_slice_idct(H264Context *h);
#if HAVE_MBD_COMPACT
extern void decode_slice_idct_compact(MBRecContext *d, H264Slice *s);
#endif


extern void h264_decode_mb_without_idct(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m);
extern void h264_decode_mb_idct(MBRecContext *d,H264Mb *m);
extern void h264_decode_mb_idct_ssse3(MBRecContext *d,H264Mb *m);

#define oclCheckErrorEX(a, b, c) __oclCheckErrorEX(a, b, c, __FILE__ , __LINE__)

#endif
