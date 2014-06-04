//This two kernel finish the 4x4 and 8x8 IDCT of H.264, respectively
extern "C" {
#include "../opencl/gpu_common.h"
#include <stdio.h>
    //FIXME:We redefine the structure here to prevent including the header files, which cause compilation problems
    typedef struct {
        cudaStream_t idct_streams[2];
        int16_t *idct_dev_mem[2];
        cudaEvent_t idct_event[2];
        int isprofile;
    } cuda_idct_env_t;
    
#define IDCT_BANK_BOUND_IN_SHORT (32*2)
#define WARP_SIZE                                (32)
#define IDCT_4x4_STRIDE                  (4)
#define IDCT_4x4_WARP_SIZE               (IDCT_4x4_STRIDE*WARP_SIZE)
#define IDCT_4x4_BLOCK_SIZE              (16)
#define IDCT_4x4_BLOCK_IN_WARP   (IDCT_4x4_WARP_SIZE/IDCT_4x4_BLOCK_SIZE)
#define IDCT_4x4_THREAD_BANK_BOUND      (IDCT_BANK_BOUND_IN_SHORT/IDCT_4x4_STRIDE)
#define IDCT_4x4_PADDING_BANK                   (2)
#define IDCT_4x4_PADDING_BANK_IN_SHORT  (IDCT_4x4_PADDING_BANK*2)

#define IDCT_8x8_STRIDE                         (8)
#define IDCT_8x8_WARP_SIZE                      (IDCT_8x8_STRIDE*WARP_SIZE)
#define IDCT_8x8_BLOCK_SIZE                     (IDCT_8x8_STRIDE*IDCT_8x8_STRIDE)
#define IDCT_8x8_BLOCK_IN_WARP          (IDCT_8x8_WARP_SIZE/IDCT_8x8_BLOCK_SIZE)
#define IDCT_8x8_THREAD_BANK_BOUND          (IDCT_BANK_BOUND_IN_SHORT/IDCT_8x8_STRIDE)
#define IDCT_8x8_PADDING_BANK                   (4)
#define IDCT_8x8_PADDING_BANK_IN_SHORT  (IDCT_8x8_PADDING_BANK*2)


#define MACRO4x4 { \
        const int z0=  block[0 ]     +  block[2 ]; \
        const int z1=  block[0 ]     -  block[2 ]; \
        const int z2= (block[1 ]>>1) -  block[3 ]; \
        const int z3=  block[1 ]     + (block[3 ]>>1);\
        block[0 ]= z0 + z3; \
        block[1 ]= z1 + z2; \
        block[2 ]= z1 - z2; \
        block[3 ]= z0 - z3; \
}

#define MACRO8x8  { \
        const int a0 =  block[0] + block[4];    \
        const int a2 =  block[0] - block[4]; \
        const int a4 = (block[2]>>1) - block[6]; \
        const int a6 = (block[6]>>1) + block[2]; \
        const int b0 = a0 + a6; \
        const int b2 = a2 + a4; \
        const int b4 = a2 - a4; \
        const int b6 = a0 - a6; \
        const int a1 = -block[3] + block[5] - block[7] - (block[7]>>1); \
        const int a3 =  block[1] + block[7] - block[3] - (block[3]>>1); \
        const int a5 = -block[1] + block[7] + block[5] + (block[5]>>1); \
        const int a7 =  block[3] + block[5] + block[1] + (block[1]>>1); \
        const int b1 = (a7>>2) + a1; \
        const int b3 =  a3 + (a5>>2); \
        const int b5 = (a3>>2) - a5; \
        const int b7 =  a7 - (a1>>2); \
        block[0] = b0 + b7; \
        block[7] = b0 - b7; \
        block[1] = b2 + b5; \
        block[6] = b2 - b5; \
        block[2] = b4 + b3; \
        block[5] = b4 - b3; \
        block[3] = b6 + b1; \
        block[4] = b6 - b1;  \
}

__device__ inline static void IDCT4(short *block){
    MACRO4x4;
}
__device__ inline static void IDCT8(short *block){
    MACRO8x8;
}



__global__ static void idct_asyn_4x4_baseline_TB2( int16_t * mbs_idct4){
    __shared__ short buf1[2*(((ATOMIC_DATA_GRANULARITY_4x4)>>1)+3*IDCT_4x4_PADDING_BANK_IN_SHORT)];/*size of short is 2*/
    const unsigned int mbindex = blockIdx.y*gridDim.x+blockIdx.x;
    const int warp_data_size =  threadIdx.y*IDCT_4x4_WARP_SIZE*ITERATE_NUM_4x4;
    const int atomic_warp_data_size = threadIdx.y*(IDCT_4x4_WARP_SIZE+IDCT_4x4_PADDING_BANK_IN_SHORT);
    const int indexinwarp = threadIdx.x;
    int   *l_V_32  = (int   *)&buf1[atomic_warp_data_size+(indexinwarp<<1)];
    long  *residual_y_64 = ( long  *)&mbs_idct4[mbindex*(TBSIZE_2_MAPPED_DATA_4x4>>1)+warp_data_size];
    int   *residual_y_32 = ( int   *)&mbs_idct4[mbindex*(TBSIZE_2_MAPPED_DATA_4x4>>1)+warp_data_size];
    short D[4];
    long *l_V_private64 =(long *) D;
    int *tmp32 = (int *)D;

    const int indexwithinblock = indexinwarp%IDCT_4x4_BLOCK_IN_WARP;
    const int inneroffset = indexinwarp/IDCT_4x4_BLOCK_IN_WARP;
    const int target = (indexwithinblock*IDCT_4x4_BLOCK_SIZE)+inneroffset;
    const int bcfreebanknum   = target/IDCT_BANK_BOUND_IN_SHORT;
    const int bcfreeidxinbank = target%IDCT_BANK_BOUND_IN_SHORT;

    for(int iter=0;iter<ITERATE_NUM_4x4;iter++){
        const int offset_in_4p = (IDCT_4x4_WARP_SIZE>>2)*iter;
        const int offset_in_2p = (IDCT_4x4_WARP_SIZE>>1)*iter;
        l_V_private64[0] = residual_y_64[indexinwarp+offset_in_4p];
        IDCT4(D);
        buf1[indexinwarp+0*WARP_SIZE+atomic_warp_data_size] = D[0];
        buf1[indexinwarp+1*WARP_SIZE+atomic_warp_data_size] = D[1];
        buf1[indexinwarp+2*WARP_SIZE+atomic_warp_data_size] = D[2];
        buf1[indexinwarp+3*WARP_SIZE+atomic_warp_data_size] = D[3];
        int *l_H = (int *)&buf1[atomic_warp_data_size+(indexinwarp<<2)];
        tmp32[0] = l_H[0];
        tmp32[1] = l_H[1];
        IDCT4(D);

        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_4x4_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_4x4_STRIDE*0] = D[0]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_4x4_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_4x4_STRIDE*1] = D[1]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_4x4_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_4x4_STRIDE*2] = D[2]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_4x4_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_4x4_STRIDE*3] = D[3]>>6;

        residual_y_32[indexinwarp+offset_in_2p] = l_V_32[0];
        residual_y_32[indexinwarp+offset_in_2p+32] = l_V_32[WARP_SIZE+IDCT_4x4_PADDING_BANK];


    }
}

typedef struct {
    long low; 
    long high;
} long128;

__global__ void idct_asyn_8x8_baseline_TB2(int16_t* mbs_idct8){
    __shared__ short buf1[(((ATOMIC_DATA_GRANULARITY_8x8)>>1)+3*IDCT_8x8_PADDING_BANK_IN_SHORT*4)*2];/*size of short is 2*/
    const unsigned int mbindex = blockIdx.y*gridDim.x+blockIdx.x;
    const int warp_data_size = threadIdx.y*IDCT_8x8_WARP_SIZE*ITERATE_NUM;
    const int atomic_warp_data_size = threadIdx.y*(IDCT_8x8_WARP_SIZE+IDCT_8x8_PADDING_BANK_IN_SHORT*4);
    const int indexinwarp = threadIdx.x;

    int *l_V_32  = (int *)&buf1[atomic_warp_data_size+(indexinwarp<<1)];

    long128 *residual_y_128 =( long128 *)&mbs_idct8[mbindex*(TBSIZE_2_MAPPED_DATA_8x8>>1)+warp_data_size];
    int     *residual_y_32  =( int       *)&mbs_idct8[mbindex*(TBSIZE_2_MAPPED_DATA_8x8>>1)+warp_data_size];
 
    short D[8];
    long128 *l_V_private128=(long128 *)D;
    int *tmp32 = (int *)D;

    const int indexwithinblock = indexinwarp%IDCT_8x8_BLOCK_IN_WARP;
    const int inneroffset = indexinwarp/IDCT_8x8_BLOCK_IN_WARP;
    const int target = (indexwithinblock*IDCT_8x8_BLOCK_SIZE)+inneroffset;

    const int bcfreebanknum    = target/IDCT_BANK_BOUND_IN_SHORT;
    const int bcfreeidxinbank  = target%IDCT_BANK_BOUND_IN_SHORT;

    for(int iter=0;iter<ITERATE_NUM;iter++){
        const int offset_in_8p = (IDCT_8x8_WARP_SIZE>>3)*iter;
        const int offset_in_2p = (IDCT_8x8_WARP_SIZE>>1)*iter;
        l_V_private128[0] = residual_y_128[indexinwarp+offset_in_8p];
        IDCT8(D);
        buf1[atomic_warp_data_size+indexinwarp+0*WARP_SIZE] = D[0];
        buf1[atomic_warp_data_size+indexinwarp+1*WARP_SIZE] = D[1];
        buf1[atomic_warp_data_size+indexinwarp+2*WARP_SIZE] = D[2];
        buf1[atomic_warp_data_size+indexinwarp+3*WARP_SIZE] = D[3];
        buf1[atomic_warp_data_size+indexinwarp+4*WARP_SIZE] = D[4];
        buf1[atomic_warp_data_size+indexinwarp+5*WARP_SIZE] = D[5];
        buf1[atomic_warp_data_size+indexinwarp+6*WARP_SIZE] = D[6];
        buf1[atomic_warp_data_size+indexinwarp+7*WARP_SIZE] = D[7];
        __syncthreads();
        int *l_H = (int *)&buf1[atomic_warp_data_size+(indexinwarp<<3)];
        tmp32[0] = l_H[0];
        tmp32[1] = l_H[1];
        tmp32[2] = l_H[2];
        tmp32[3] = l_H[3];
        IDCT8(D);
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*0] = D[0]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*1] = D[1]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*2] = D[2]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*3] = D[3]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*4] = D[4]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*5] = D[5]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*6] = D[6]>>6;
        buf1[atomic_warp_data_size + bcfreebanknum*(IDCT_BANK_BOUND_IN_SHORT+IDCT_8x8_PADDING_BANK_IN_SHORT)+ bcfreeidxinbank+IDCT_8x8_STRIDE*7] = D[7]>>6;

        residual_y_32[indexinwarp+WARP_SIZE*0+offset_in_2p] = l_V_32[(WARP_SIZE+IDCT_8x8_PADDING_BANK)*0];
        residual_y_32[indexinwarp+WARP_SIZE*1+offset_in_2p] = l_V_32[(WARP_SIZE+IDCT_8x8_PADDING_BANK)*1];
        residual_y_32[indexinwarp+WARP_SIZE*2+offset_in_2p] = l_V_32[(WARP_SIZE+IDCT_8x8_PADDING_BANK)*2];
        residual_y_32[indexinwarp+WARP_SIZE*3+offset_in_2p] = l_V_32[(WARP_SIZE+IDCT_8x8_PADDING_BANK)*3];

    }
}

     __host__ void launch_kernel_idct (int *globalsize, int *localsize, int *idct8_globalsize, int *idct8_localsize, double *idct_gpu_time_fine, cuda_idct_env_t *p){
        dim3 threads(localsize[0],localsize[1]);
        dim3 grid(globalsize[0],globalsize[1]);
        dim3 threads8(idct8_localsize[0],idct8_localsize[1]);
        dim3 grid8(idct8_globalsize[0],idct8_globalsize[1]);
        if(p->isprofile){
            float idct_kernel_time;
            cudaEventRecord(p->idct_event[0],p->idct_streams[0]);
            idct_asyn_4x4_baseline_TB2<<<grid,threads,0,p->idct_streams[0]>>>(p->idct_dev_mem[0]);
            cudaEventRecord(p->idct_event[1],p->idct_streams[0]);
            cudaEventSynchronize(p->idct_event[1]);
            cudaEventElapsedTime(&idct_kernel_time,p->idct_event[0],p->idct_event[1]);
            idct_gpu_time_fine[0]+= idct_kernel_time;

            cudaEventRecord(p->idct_event[0],p->idct_streams[1]);
            idct_asyn_8x8_baseline_TB2<<<grid8, threads8,0,p->idct_streams[1]>>>(p->idct_dev_mem[1]);
            cudaEventRecord(p->idct_event[1],p->idct_streams[1]);
            cudaEventSynchronize(p->idct_event[1]);
            cudaEventElapsedTime(&idct_kernel_time,p->idct_event[0],p->idct_event[1]);
            idct_gpu_time_fine[1]+= idct_kernel_time;
        }else{
            idct_asyn_4x4_baseline_TB2<<<grid,threads,0,p->idct_streams[0]>>>(p->idct_dev_mem[0]);
            idct_asyn_8x8_baseline_TB2<<<grid8, threads8,0,p->idct_streams[1]>>>(p->idct_dev_mem[1]);
        }
     }
}
