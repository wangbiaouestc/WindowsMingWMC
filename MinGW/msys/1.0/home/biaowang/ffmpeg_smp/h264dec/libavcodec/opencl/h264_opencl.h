#ifndef H264_OPENCL_H
#define H264_OPENCL_H

#include "../cl.h"
#include "../h264_types.h"


#include <time.h>

#define MAX_NUM_FRAME    1000u
#define DETECT_FRAME 0
#define TRUE 	1

#define HDASHLINE "-----------------------------------------------------------\n"
#define FAILURE -1

extern int64_t diff(struct timespec start,struct timespec end);
extern int decode_slice_entropy(EntropyContext *ec, SliceBufferEntry *sbe);

extern cl_platform_id cpPlatform;
extern cl_uint uiNumDevices;
extern cl_device_id* cdDevices;
extern cl_context cxGPUContext;
extern cl_event ceEvent;

extern int curslice   ;
extern int widthindex ;
extern int heightindex;
extern int mb_height  ;
extern int mb_width   ;
extern int openclonline;
extern double idct_gpu_time;
extern double mc_total_time[];
extern double mc_gpu_time[];
extern int gpumode;
enum {
	MC_OVERHEAD,
	MC_TIME,
	INTRADF,
	MC_TAIL,
	MC_PROFILE_PHASES
};

enum {MC_KERNEL_COPY_CHROMA, MC_KERNEL_COPY_LUMA, MC_KERNEL_CHROMA, MC_KERNEL_LUMA, MC_KERNEL_COPY_DATA_STRUCTURE,MC_KERNEL_PROFILE_PHASES};
typedef struct {
	cl_mem mem_obj;
	uint32_t size;
} opencl_mem_type;

typedef uint8_t gout_t;
//CPU_BASELINE     : ED and REC only
//CPU_BASELINE_IDCT: decouple the IDCT in ED with CPU kernel, all the other in REC
//CPU_BASELINE_MC:decouple the MC in REC, IDCT integrate in the loop
//CPU_BASELINE_TOTAL: decouple the IDCT in the ED and MC 16x16 in REC with CPU kernels
//GPU_IDCT: offload IDCT to GPU in ED
//GPU_MC: offload MC to GPU in MC
//GPU_TOTAL: offload IDCT to GPU in ED, and MC 16x16 in REC
enum {CPU_BASELINE=1,CPU_BASELINE_IDCT, CPU_BASELINE_MC, CPU_BASELINE_TOTAL, GPU_IDCT, GPU_MC, GPU_TOTAL, GPU_MC_NOVERLAP};

extern void printmatrixshort(short* dst,int size,int stride);
extern void printmatrixint(int* dst,int size,int stride);
extern void printmatrixuint8_t(uint8_t *dst, int size, int stride);
extern int64_t diff(struct timespec start,struct timespec end);
extern const char* oclErrorString(cl_int error);
extern void __oclCheckErrorEX(cl_int iSample, cl_int iReference, void (*pCleanup)(int), const char* cFile, const int iLine);
extern cl_int oclGetPlatformID(cl_platform_id* clSelectedPlatformID);
extern char* readSource(const char *sourceFilename, size_t *psize);
extern void oclLogBuildInfo(cl_program opencl_program, cl_device_id cdDevice);
extern void oclLogPtx(cl_program opencl_program, cl_device_id cdDevice, const char* cPtxFileName);
extern int getcurrentdir(char *dir);

extern int gpu_opencl_construct_environment(H264Context *d);
extern int gpu_opencl_destroy_environment(void);
extern void BuildProgram(const char *KernelFilename, cl_program *CreatedProgram);

#define oclCheckErrorEX(a, b, c) __oclCheckErrorEX(a, b, c, __FILE__ , __LINE__)
#define oclCheckError(a, b) oclCheckErrorEX(a, b, 0)

#endif