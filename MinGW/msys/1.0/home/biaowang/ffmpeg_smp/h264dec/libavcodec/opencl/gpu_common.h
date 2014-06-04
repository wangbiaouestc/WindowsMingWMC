#ifndef GPU_COMMON_H
#define GPU_COMMON_H

//define a local work group 16x16 ,otherwise 16x24
#define PURE_DECOMPOSITION
//define a scope to work on 

//define the way handle the 8x8 pixels
//0: full pixel-level
//1: half pixel-level
//2 line-level
#define MODE_8x8_ONLY 7
#define SCALE_GLB     8

//to define the input sequence
#define USE_NON8x8_INPUT 0

#define SIZE_OF_MACRO_BLOCK  (768)	//byte
#define SHARED_MEM_SIZE   (49152) /*for fermi, shared memory size is 48k by default*/
#define BLOCK_DATA_LENGTH (SHARED_MEM_SIZE/8) /*for fermi , one stream multiprocessor can accommodate 8 thread blocks at maximum*/
#define TRANSACTION_PER_THREAD	16		/*for existing kernel, it performs best, warps need to saturate the global memory bus with enough transactions*/
#define THREAD_BLOCK_NUM  ((BLOCK_DATA_LENGTH)/(TRANSACTION_PER_THREAD)/4) /*each thread will access 32 bit words*/

#define ITERATE_NUM	2
#define ITERATE_NUM_4x4	(4)
#define BASELINE_THREAD_BLOCK_MAPPED_DATA_4x4	(384*sizeof(short)*ITERATE_NUM)
#define BASELINE_THREAD_BLOCK_MAPPED_DATA_8x8	(768*sizeof(short)*ITERATE_NUM)

#define ATOMIC_DATA_GRANULARITY_4x4				(384*sizeof(short))
#define ATOMIC_DATA_GRANULARITY_8x8				(768*sizeof(short))


//if TBSIZE_SCALE_IN_WARP is 1 then it becomes baseline
#define TBSIZE_SCALE_IN_WARP			4

#define TBSIZE_WARP_IN_THREAD_BLOCKS	(3*TBSIZE_SCALE_IN_WARP)
#define TBSIZE_MAPPED_DATA_4x4  		(TBSIZE_SCALE_IN_WARP*ATOMIC_DATA_GRANULARITY_4x4)
#define TBSIZE_MAPPED_DATA_8x8			(TBSIZE_SCALE_IN_WARP*ATOMIC_DATA_GRANULARITY_8x8)

#define TBSIZE_2_WARP_IN_THREAD_BLOCKS	(6)
#define TBSIZE_2_MAPPED_DATA_4x4  		(2*ATOMIC_DATA_GRANULARITY_4x4*ITERATE_NUM_4x4)
#define TBSIZE_2_MAPPED_DATA_8x8		(2*ATOMIC_DATA_GRANULARITY_8x8*ITERATE_NUM)

#define UNROLL_NUM						2
#define TBSIZE_2_UNROLL_DATA_4x4		(TBSIZE_2_MAPPED_DATA_4x4*UNROLL_NUM)
#define TBSIZE_2_UNROLL_DATA_8x8		(TBSIZE_2_MAPPED_DATA_8x8*UNROLL_NUM)

#define PER_THREAD_PIXEL 1

#endif

