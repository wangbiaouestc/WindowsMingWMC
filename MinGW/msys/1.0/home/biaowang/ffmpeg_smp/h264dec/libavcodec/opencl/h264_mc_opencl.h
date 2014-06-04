#ifndef H264_MC_OPENCL_H
#define H264_MC_OPENCL_H

#include "h264_opencl.h"
#include "../h264_pred_mode.h"

#define MC_KERNEL_DIMENSION 2
#define MC_KERNEL_COUNT		(2u)
#define MAX_REFERENCE_COUNT	16
#define KERNEL_SOURCE_MC   "../h264dec/libavcodec/opencl/kernelMC.cl"
#define USED_REFERENCE_COUNT 16
#define MACROBLOCK_SIZE		(16)


#if HAVE_MC_TYPE_PROFILING
//partition size profiling
extern int64_t inter_mb_type_count;
extern int64_t inter_mb_type_16x16;
extern int64_t inter_mb_type_16x8;
extern int64_t inter_mb_type_8x16;
extern int64_t inter_mb_type_8x8;
extern int64_t inter_sub_mb_type_8x8;
extern int64_t inter_sub_mb_type_8x4;
extern int64_t inter_sub_mb_type_4x8;
extern int64_t inter_sub_mb_type_4x4;
extern int64_t inter_pos[2][16];
extern int64_t intra_mb_type_count;

//reference picture profiling
#endif



extern int *mc_opencl_mb_type;

extern int *mc_opencl_use_weight;
extern int *mc_opencl_list[2];
extern int *mc_opencl_weight[2];
extern int *mc_opencl_offset;


extern cl_ulong kernel_time_luma   ;
extern cl_ulong kernel_time_chroma ;
extern int64_t CPU_MC_16x16        ;

extern int mc_transferred_reference[16];
extern uint8_t *mc_cpu_luma_input;
extern int mc_references_FIFO_top;
extern cl_command_queue mc_commandQueue[];
extern cl_event mc_copy_event_luma;
extern cl_event mc_copy_event_chroma[2];
extern uint8_t *mc_cpu_chroma_input[2];
extern opencl_mem_type mc_gpu_luma_references;
extern opencl_mem_type mc_gpu_chroma_references[2];
extern unsigned int mc_opencl_reference_size;

extern int mc_opencl_construct_env(H264Context *h);
extern int mc_opencl_destruct_env(void);
extern void gpu_opencl_mc(H264Context *h, MBRecContext *d, H264Slice *s, SliceBufferEntry *sbe);
extern int pred_motion_mb_rec_without_intra_and_collect (MBRecContext *mrc, MBRecState *mrs, H264Slice *s, H264Mb *m, int iscollect);
extern void h264_decode_mb_without_mc_idct(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m);
extern void h264_decode_mb_mc_16x16(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m);
extern void h264_decode_mb_without_mc(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m);
static inline int is_transferred(int *mc_transferred_reference, int cpn){
	int i;
	for(i=0;i<MAX_REFERENCE_COUNT;i++)
		if(cpn==mc_transferred_reference[i])
			break;
	return i;
}
static inline double get_event_time(cl_event event){
	cl_ulong timestamps[2]={0,0};
	double t;
	clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_START,sizeof(cl_ulong),&timestamps[0],NULL);
	clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_END,sizeof(cl_ulong),&timestamps[1],NULL);
	t =(double)((1.e-6)*(timestamps[1]-timestamps[0]));
	if (t<0.0){
		printf("shit , minor time received\n");
		exit(-1);
	}
	return t;
}
typedef struct {
	int16_t mx;
	int16_t my;
} MVVector;

typedef struct{
    uint8_t ref_slot:4;
    uint8_t refn:4;
} MCREFER;
typedef struct {
	uint8_t mb_type:4;
	uint8_t list:4;
} MC_AUX_T;
typedef uint16_t sub_mb_type_T[4];

extern MC_AUX_T *mc_cpu_uni_pred_auxiliary;

//for 4x4 blocks, there might be 16 blocks in each macroblock
extern MVVector  *mc_cpu_shared_vectors[2][16];
extern MCREFER   *mc_cpu_shared_ref[2][16];
extern int mc_transferred_reference[];
extern uint16_t (*mc_cpu_sub_mb_type)[4];

#endif
