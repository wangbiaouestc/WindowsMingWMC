extern "C" {
#include "../opencl/gpu_common.h"
#include <stdio.h>
#define EDGE_WIDTH 32
#define MC_SIZE     (16)
#define EXTENDED_SIZE       (5)
#define MC_SIZE_EXTENDED    ((MC_SIZE)+(EXTENDED_SIZE))


#define MB_TYPE_8x8DCT     0x01000000
#define MB_TYPE_INTRA4x4   0x0001
#define MB_TYPE_INTRA16x16 0x0002 //FIXME H.264-specific
#define IS_INTRA4x4(a)   ((a)&MB_TYPE_INTRA4x4)
#define IS_8x8DCT(a)       ((a) & MB_TYPE_8x8DCT)
#define IS_INTRA(a)      ((a)&7)
#define IS_INTRA16x16(a) ((a)&MB_TYPE_INTRA16x16)
#define MB_RESIDUAL_SIZE ((16)*(24))

// NOTE MB_TYPE_16x8, MB_TYPE_8x16, MB_TYPE_8x8 are different from the definition in CPU
// where their definitions are as follows, while in GPU all of them have right shift 4 bits
// #define MB_TYPE_16x8            0x0010
// #define MB_TYPE_8x16            0x0020
// #define MB_TYPE_8x8             0x0040
#define MB_TYPE_16x16   0x0008
#define MB_TYPE_16x8    0x0001
#define MB_TYPE_8x16    0x0002
#define MB_TYPE_8x8     0x0004
//only process 16x8 8x16 partitions
#define MB_TYPE_GT8x8               (0x000b)
#define MB_TYPE_WIDTH_16 (0x0009)

#define IS_16X16(a)      ((a)&MB_TYPE_16x16)
#define IS_GT8X8(a)      ((a)&MB_TYPE_GT8x8)
#define IS_16X8(a)       ((a)&MB_TYPE_16x8)
#define IS_8X16(a)       ((a)&MB_TYPE_8x16)
#define IS_8X8(a)        ((a)&MB_TYPE_8x8)

#define IS_WIDTH_16(a)   ((a)&MB_TYPE_WIDTH_16)

#define IS_SUB_8X8(a)    ((a)&MB_TYPE_16x16)
#define IS_SUB_8X4(a)    ((a)&(MB_TYPE_16x8<<4))
#define IS_SUB_4X8(a)    ((a)&(MB_TYPE_8x16<<4))
#define IS_SUB_4X4(a)    ((a)&(MB_TYPE_8x8<<4))

#define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v

typedef short int16_t;
typedef unsigned short uint16_t;

typedef unsigned char uint8_t;
typedef int int32_t;
typedef const unsigned int uint32_t;

typedef struct {
    int16_t mx;
    int16_t my;
} MVVector;

typedef char  MC_AUX_T;

typedef int8_t MCRef;

typedef struct{
    int16_t  *luma_weight;
    int16_t  *chroma_weight;
    int16_t  *implicit_weight;
    uint16_t *sub_mb_type;
    uint8_t  *chroma_output[2];
    uint8_t   *luma_output;
    MVVector *shared_vectors[2];
    MCRef  *shared_ref[2];
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


#define REF_MASK        0xF0
#define REF_SLOT_MASK   0x0F

#define MB_TYPE_MASK    0x0F 
#define LIST0_MASK   0x0030
#define LIST1_MASK   0x00C0

#define MB_TYPE_P0L0       0x1000
#define MB_TYPE_P1L0       0x2000
#define MB_TYPE_P0L1       0x4000
#define MB_TYPE_P1L1       0x8000

#define IS_DIR(a, part, list) ((a) & (MB_TYPE_P0L0<<((part)+2*(list))))

#define UNI_CLIP(out,pel) out = ((pel)&(~0xFF))?((-pel)>>31):(pel);



typedef int16_t LUMA_weight_table;
typedef int16_t CHROMA_weight_table;
typedef int16_t implicit_weight_table;
typedef uint16_t sub_mb_type_table;

/***********************************************************************************************
************************MC kernel starts here***************************************************
************************************************************************************************/
__device__ inline void chromaFilter( uint8_t *reference_cb, uint8_t *reference_cr, MVVector *frame_list_vector, MCRef *frame_ref, uint8_t *out_c, int shifted_gid, int pic_width, int pic_height){
    MVVector vector   = frame_list_vector[shifted_gid];
    int ref_slot = (frame_ref[shifted_gid]&REF_SLOT_MASK);
    int cmx      = vector.mx>>3;
    int cmy      = vector.my>>3;
    int x        = vector.mx&7;
    int y        = vector.my&7;
    int A        =(8-x)*(8-y);
    int B        =(  x)*(8-y);
    int C        =(8-x)*(  y);
    int D        =(  x)*(  y);
    int pic_size = pic_width*pic_height;
    uint8_t *src_cb    = reference_cb+ref_slot*pic_size+(EDGE_WIDTH>>1)*(1+pic_width)+cmx + cmy*pic_width + (threadIdx.x&7) + threadIdx.y*(pic_width);
    uint8_t *src_cr    = reference_cr+ref_slot*pic_size+(EDGE_WIDTH>>1)*(1+pic_width)+cmx + cmy*pic_width + (threadIdx.x&7) + threadIdx.y*(pic_width);
    out_c[0]  = ((A*src_cb[0] + B*src_cb[1] + C*src_cb[pic_width+0] + D*src_cb[pic_width+1])+32)>>6;
    out_c[1]  = ((A*src_cr[0] + B*src_cr[1] + C*src_cr[pic_width+0] + D*src_cr[pic_width+1])+32)>>6;
}

__device__ inline void weightedPrediction(int log2_denom, int weight, int weighted_offset, uint8_t *output){
    weighted_offset<<=log2_denom;
    int pre_weight = *output;
    if(log2_denom)
        weighted_offset +=1<<(log2_denom-1);
    int clip_tmp = (pre_weight*weight+weighted_offset)>>log2_denom;
    UNI_CLIP(*output, clip_tmp)
}

__global__ void mc_h264_chroma_biweighted(uint8_t *reference_cb,
                                          uint8_t *reference_cr,
                                          uint8_t *output_cb, 
                                          uint8_t *output_cr,
                                          MCRef    *frame_list0_ref,
                                          MCRef    *frame_list1_ref,
                                          MVVector *frame_list0_vector,
                                          MVVector *frame_list1_vector,
                                          sub_mb_type_table *frame_sub_mb_type,
                                          CHROMA_weight_table *chroma_weight,
                                          implicit_weight_table *implicit_weight,
                                         int log2_denom,
                                         int use_weight,
                                         int use_weight_chroma,
                                         int pic_width,int pic_height, 
                                         int weighted_prediction){

    uint8_t out_c[2][2];
    int gid= blockIdx.y*gridDim.x+blockIdx.x;
    int block = ((threadIdx.y>>2)<<1)+((threadIdx.x)>>2);
    int sub_block   = (((threadIdx.y&2)>>1)<<1)+(((threadIdx.x)&2)>>1);

    int frame_size  = gridDim.x*gridDim.y;
    int shifted_gid = gid+(block*4+sub_block)*frame_size;

    int mb_type = frame_sub_mb_type[gid*4+block];
    int list[2];
    int part = 0;
    if(IS_16X8(mb_type)){
        part = block>>1;//FIXME when merged, maybe it will not apply as 8x8 smaller blocks part may not be 0 
    }
    if(IS_8X16(mb_type)){
        part = block&1;
    }
    list[0] = IS_DIR(mb_type,part,0);
    list[1] = IS_DIR(mb_type,part,1);
    MVVector *frame_list_vector[2]={frame_list0_vector,frame_list1_vector};
    MCRef    *frame_ref[2]   ={frame_list0_ref,frame_list1_ref};
    for(int l=0;l<2;l++){
        if(list[l]){
            chromaFilter(reference_cb, reference_cr, frame_list_vector[l],frame_ref[l], out_c[l], shifted_gid, pic_width, pic_height);
        }
    }
    int gidout = (blockIdx.y*blockDim.y+threadIdx.y)*pic_width+(blockIdx.x*blockDim.x+threadIdx.x);
    if(list[0]&&list[1]){
        int weight0, weight1, weight_offset,refn0, refn1;
        refn0   = (frame_list0_ref[shifted_gid]&REF_MASK)>>4;
        refn1   = (frame_list1_ref[shifted_gid]&REF_MASK)>>4;
        if(use_weight&0x1){
            weight0 = chroma_weight[refn0*8+0*4+0*2+0];
            weight1 = chroma_weight[refn1*8+1*4+0*2+0];
            weight_offset = chroma_weight[refn0*8+0*4+0*2+1]+chroma_weight[refn1*8+1*4+0*2+1];
            weight_offset = ((weight_offset+1)|1)<<log2_denom;
            out_c[0][0] = (weight0*out_c[0][0]+weight1*out_c[1][0]+weight_offset)>>(log2_denom+1);
            UNI_CLIP(output_cb[gidout],out_c[0][0])

            weight0 = chroma_weight[refn0*8+0*4+1*2+0];
            weight1 = chroma_weight[refn1*8+1*4+1*2+0];
            weight_offset = chroma_weight[refn0*8+0*4+1*2+1]+chroma_weight[refn1*8+1*4+1*2+1];
            weight_offset = ((weight_offset+1)|1)<<log2_denom;
            out_c[0][1] = ((weight0*out_c[0][1]+weight1*out_c[1][1]+weight_offset)>>(log2_denom+1));
            UNI_CLIP(output_cb[gidout],out_c[0][1])

        }else {
            weight0=32;
            if(use_weight) weight0 = implicit_weight[refn0*32+refn1*2];
            weight1= 64-weight0;
            weight_offset = 32;
            out_c[0][0] = ((weight0*out_c[0][0]+weight1*out_c[1][0]+weight_offset)>>6);
            UNI_CLIP(output_cb[gidout],out_c[0][0])

            out_c[0][1] = ((weight0*out_c[0][1]+weight1*out_c[1][1]+weight_offset)>>6);
            UNI_CLIP(output_cr[gidout],out_c[0][1])
        }
    }else{
         int l=list[1]?1:0;
         if(use_weight_chroma==1&&weighted_prediction){
            int refn = (frame_ref[0][shifted_gid]&REF_MASK)>>4;
            int list = 0;
            int weight, weighted_offset;
            weight = chroma_weight[refn*8+list*4+0*2+0];
            weighted_offset = chroma_weight[refn*8+list*4+0*2+1];
            weightedPrediction(log2_denom,weight,weighted_offset, &out_c[l][0]);
            
            weight = chroma_weight[refn*8+list*4+1*2+0];
            weighted_offset = chroma_weight[refn*8+list*4+1*2+1];
            weightedPrediction(log2_denom,weight,weighted_offset, &out_c[l][1]);
        }
         output_cb[gidout]  = out_c[l][0];
         output_cr[gidout]  = out_c[l][1];
    }
}


#define CLIP_0(pel)       (((pel)<0)?0:(pel))
#define CLIP_255(pel)     (((pel)>255)?255:(pel))
#define BI_CLIP(pel)      CLIP_255((CLIP_0(pel)))
#define TAP6_BASIC_H(src)           ((src[0]+src[1])*20-(src[-1]+src[2])*5+(src[-2]+src[3]))
#define TAP6_BASIC_V(src,stride)    ((src[0*stride]+src[1*stride])*20-(src[-1*stride]+src[2*stride])*5+(src[-2*stride]+src[3*stride]))

#define TAP6_FILTER_H(src)           (((TAP6_BASIC_H(src))+16)>>5)
#define TAP6_FILTER_V(src,stride)    (((TAP6_BASIC_V(src,stride))+16)>>5)

#define TAP6_H(src)             CLIP_255(CLIP_0(((TAP6_BASIC_H(src)+16)>>5)))
#define TAP6_V(src,stride)      CLIP_255(CLIP_0(((TAP6_BASIC_V(src,stride)+16)>>5)))
#define TAP6_OP2_H(src)         CLIP_255(CLIP_0(((TAP6_BASIC_H(src)+512)>>10)))
#define TAP6_OP2_V(src,stride)  CLIP_255(CLIP_0(((TAP6_BASIC_V(src,stride)+512)>>10)))

#define SRC_STRIDE (4*MC_SIZE)
#define RND_SHIFT(pel,shift_bits)       BI_CLIP(((pel)+(1<<(shift_bits-1)))>>shift_bits)
#define TAP6(s0,s1,s2,s3,s4,s5,shift_bits) RND_SHIFT((s0+s5)-5*(s1+s4)+20*(s2+s3) ,shift_bits)

__device__ inline void locateSourceAndMode(uint8_t *reference,  MCRef *frame_ref,  MVVector *frame_list_vector, int pic_width, int pic_height, int gid, int block, int sub_block, int *p_mx, int *p_my, uint8_t **p_src_base, int *p_align_offset){
    int frame_size   = gridDim.x*gridDim.y;
    int localidx  = ((threadIdx.x>>2)<<2)+(threadIdx.x&3)*4;
    int localidy  = (threadIdx.y<<2)-2;
    MVVector vector  = frame_list_vector[gid+(4*block+sub_block)*frame_size];
    int ref_slot     = frame_ref[gid+(4*block+sub_block)*frame_size]&REF_SLOT_MASK;
    int mx           = vector.mx;
    int my           = vector.my;
    int ymx          = mx>>2;
    int ymy          = my>>2;
    *p_mx            = mx&3;
    *p_my            = my&3;
    *p_src_base      = reference+ref_slot*pic_width*pic_height+EDGE_WIDTH*(1+pic_width)+ymy*pic_width+ymx+localidx+localidy*pic_width;
    *p_align_offset  = (ymx&3)+4;
}

__device__ inline void loadSourceToShared(uint8_t *dst, uint8_t *src, int dst_stride, int src_stride, int align_offset){
    int *dst_int  = (int *)&dst[(threadIdx.x<<2)+threadIdx.y*9*dst_stride];
    int *src_int  = (int *)(src-align_offset);
    for(int i=0;i<9;i++)
        dst_int[i*(dst_stride>>2)] = src_int[i*(src_stride>>2)];
}

__device__ inline void copyFullAndComputeHorizontal(int16_t *dst_F, int16_t *dst_H, uint8_t *src, int dst_stride, int src_stride, int align_offset, int col, int row){
    int localidx = (threadIdx.x>>2)*16+align_offset+(threadIdx.x&3);
    int localidy = threadIdx.y*9;
    uint8_t *buf_src  = &src[localidx+localidy*64];
    int inner_offset = (col==3);
    for(int i=0;i<9;i++)
        dst_F[threadIdx.x+(localidy+i)*MC_SIZE] = (buf_src+i*SRC_STRIDE)[inner_offset];
    if(col)
        for(int i=0;i<9;i++)
            dst_H[threadIdx.x+(localidy+i)*MC_SIZE] = TAP6_BASIC_H((buf_src+i*SRC_STRIDE));    
}

__device__ inline void computeVertical(int16_t *dst ,int16_t *src, int shift_bits){
    int V0,V1,V2,V3,V4,V5,V6,V7,V8;
    V0 = src[-2*MC_SIZE];
    V1 = src[-1*MC_SIZE];
    V2 = src[0*MC_SIZE];
    V3 = src[1*MC_SIZE];
    V4 = src[2*MC_SIZE];
    V5 = src[3*MC_SIZE];
    V6 = src[4*MC_SIZE];
    V7 = src[5*MC_SIZE];
    V8 = src[6*MC_SIZE];

    dst[0*MC_SIZE]=TAP6(V0,V1,V2,V3,V4,V5,shift_bits);
    dst[1*MC_SIZE]=TAP6(V1,V2,V3,V4,V5,V6,shift_bits);
    dst[2*MC_SIZE]=TAP6(V2,V3,V4,V5,V6,V7,shift_bits);
    dst[3*MC_SIZE]=TAP6(V3,V4,V5,V6,V7,V8,shift_bits);
}

__device__ inline void clipHorizontal(int16_t *buf_H, int col, int row){
    int inner_offset = (row==3);
    int16_t *buf = &buf_H[threadIdx.x+(threadIdx.y*9+2+inner_offset)*MC_SIZE];
    int s[4];
    if(col){
        if(row!=2){
            for(int i=0;i<4;i++)
                s[i] = RND_SHIFT(buf[i*MC_SIZE],5);
            for(int i=0;i<4;i++)
                buf[i*MC_SIZE]=s[i];
        }
    }
}

__device__ inline void adjustPointerForAVG(int16_t **pbufH, int16_t **pbufV, int16_t *buf_F, int16_t *buf_H, int16_t *buf_V, int16_t *buf_HV, int col, int row){

    int inner_offset = (row==3);
    int localV = threadIdx.x+threadIdx.y*4*MC_SIZE;
    int localH = threadIdx.x+(threadIdx.y*9+2+inner_offset)*MC_SIZE;
    int16_t *bufH = &buf_H[localH];
    int16_t *bufV = &buf_V[localV];
    int luma_xy= row+(col<<4);
    switch(luma_xy){
        case 0x00:
            bufH =&buf_F[localH];
            bufV = bufH;
            break;
        case 0x20:
            bufV = bufH;
            break;
        case 0x02:
            bufH=bufV;
            break;
        case 0x22:
            bufV=&buf_HV[localV];
            bufH=bufV;
            break;
        case 0x10:
        case 0x30:
            bufV= &buf_F[localH];
            break;
        case 0x01:
        case 0x03:
            bufH= &buf_F[localH];
            break;
        case 0x21:
        case 0x23:
            bufV=&buf_HV[localV];
            break;
        case 0x12:
        case 0x32:
            bufH=&buf_HV[localV];
            break;
    }
    *pbufH = bufH;
    *pbufV = bufV;
}

__global__ void mc_h264_luma_biweighted(  uint8_t *reference,   
                              uint8_t *output,
                              MCRef *frame_list0_ref,
                              MCRef *frame_list1_ref,
                              MVVector *frame_list0_vector,
                              MVVector *frame_list1_vector,
                              sub_mb_type_table *frame_sub_mb_type,
                              LUMA_weight_table *luma_weight,
                              implicit_weight_table *implicit_weight,
                             int log2_denom,
                             int use_weight,
                             int pic_width,int pic_height,int weighted_prediction){

    __shared__ uint8_t buf_src[MC_SIZE*4*9*4];
    __shared__ int16_t buf_F[4*4*(4+5)*4];
    __shared__ int16_t buf_H[4*4*(4+5)*4];
    __shared__ int16_t buf_V[MC_SIZE*MC_SIZE];
    __shared__ int16_t buf_HV[MC_SIZE*MC_SIZE];
    __shared__ uint8_t buf_weight[2][MC_SIZE*MC_SIZE];

    int gidx     = blockIdx.x;
    int gidy     = blockIdx.y;
    int gid      = gidy*gridDim.x+gidx;
    int col, row, align_offset;
    uint8_t *src_base;
    int block     = (threadIdx.x>>3)+((threadIdx.y>>1)<<1);
    int sub_block = ((threadIdx.x&7)>>2)+((threadIdx.y&1)*2);

    int mb_type   = frame_sub_mb_type[gid*4+block];
    int list[2];
    int part=0;
    if (IS_16X8(mb_type)){
        part = block>>1;
    }
    if (IS_8X16(mb_type)){
        part = block&1;
    }
    list[0] = IS_DIR(mb_type,part,0);
    list[1] = IS_DIR(mb_type,part,1);

    MVVector *frame_list_vector[2]={frame_list0_vector,frame_list1_vector};
    MCRef    *frame_ref[2]        ={frame_list0_ref,frame_list1_ref};
    for(int l=0;l<2;l++){
        if(list[l]){
            locateSourceAndMode(reference, frame_ref[l], frame_list_vector[l], pic_width, pic_height, gid, block, sub_block, &col, &row, &src_base, &align_offset);
            loadSourceToShared(buf_src, src_base, SRC_STRIDE, pic_width, align_offset);
            __syncthreads();
            copyFullAndComputeHorizontal(buf_F, buf_H, buf_src, MC_SIZE, SRC_STRIDE, align_offset, col, row);
            __syncthreads();
            if(row){
                int inner_offset = (col==2);
                int16_t *H = inner_offset?&buf_H[threadIdx.x+(threadIdx.y*9+2)*MC_SIZE]:&buf_F[threadIdx.x+(threadIdx.y*9+2)*MC_SIZE];
                int16_t *V  = inner_offset?&buf_HV[threadIdx.x+threadIdx.y*4*MC_SIZE]:&buf_V[threadIdx.x+threadIdx.y*4*MC_SIZE];
                computeVertical(V, H, 5<<inner_offset);
            }

            if(row==2||(col&1)){
                int16_t *H = &buf_H[threadIdx.x+(threadIdx.y*9+2)*MC_SIZE];
                int16_t *V = &buf_HV[threadIdx.x+threadIdx.y*4*MC_SIZE];
                computeVertical(V, H, 10);
            }
            __syncthreads();

            clipHorizontal(buf_H, col, row);
            __syncthreads();

            int16_t *tmpbufH, *tmpbufV;
            adjustPointerForAVG(&tmpbufH, &tmpbufV, buf_F, buf_H, buf_V, buf_HV, col, row);
            int localout   = threadIdx.x+threadIdx.y*4*MC_SIZE;
            for(int i=0;i<4;i++)
                buf_weight[l][localout+i*MC_SIZE] = (tmpbufH[i*MC_SIZE]+tmpbufV[i*MC_SIZE]+1)>>1;
            __syncthreads();
        }
    }
    int gidout     = gidy*MC_SIZE*pic_width+gidx*MC_SIZE+threadIdx.x+threadIdx.y*4*pic_width;
    int localout   = threadIdx.x+threadIdx.y*4*MC_SIZE;
    int weight0, weight1,offset;
    
    if(list[0]&&list[1]){
        int frame_size = gridDim.x*gridDim.y;
        int refn0   = (frame_list0_ref[gid+(4*block+sub_block)*frame_size]&REF_MASK)>>4;
        int refn1   = (frame_list1_ref[gid+(4*block+sub_block)*frame_size]&REF_MASK)>>4;
        if(use_weight&0x1){
            weight0 = luma_weight[refn0*4+0*2+0];
            weight1 = luma_weight[refn1*4+1*2+0];
            offset  = luma_weight[refn0*4+0*2+1]+luma_weight[refn1*4+1*2+1];
            offset  = ((offset+1)|1)<<log2_denom;
        }else {
            weight0=32;
            if(use_weight) weight0 = implicit_weight[refn0*32+refn1*2+(gidy&1)];
            weight1= 64-weight0;
            offset = 32;
            log2_denom = 5;
        }
        for(int i=0;i<4;i++)
            UNI_CLIP(output[gidout+i*pic_width],((weight0*buf_weight[0][localout+i*MC_SIZE]+weight1*buf_weight[1][localout+i*MC_SIZE]+offset)>>(log2_denom+1)));
    }else{
        if(weighted_prediction==1){
            int refn         = (frame_ref[0][gid+(4*block+sub_block)*gridDim.x*gridDim.y]&REF_MASK)>>4;
            int list         = 0; //NOTE: for p frame only list0 is allowed

            int weighted_offset = luma_weight[refn*4+list*2+1];
            weighted_offset<<=log2_denom;
            if(log2_denom)
                weighted_offset += 1<<(log2_denom-1);
            int weight = luma_weight[refn*4+list*2+0];
            for(int i=0;i<4;i++)
                UNI_CLIP(output[gidout+i*pic_width],(buf_weight[0][localout+i*MC_SIZE]*weight+weighted_offset)>>log2_denom);
            return;
        }
        for(int i=0;i<4;i++)
            output[gidout+i*pic_width] = list[1]?buf_weight[1][localout+i*MC_SIZE]:buf_weight[0][localout+i*MC_SIZE];
    }
}

    __host__ void launch_kernel_mc(int *luma_globalsize,int *luma_localsize,int *chroma_globalsize,int *chroma_localsize, int weighted_prediction, cuda_mc_env_t *p){
    
        dim3 thread_luma(luma_localsize[0],luma_localsize[1]);
        dim3 grid_luma(luma_globalsize[0],luma_globalsize[1]);
        dim3 thread_chroma(chroma_localsize[0],chroma_localsize[1]);
        dim3 grid_chroma(chroma_globalsize[0],chroma_globalsize[1]);
//         printf("local %d %d global %d %d\n",luma_localsize[0],luma_localsize[1],luma_globalsize[0],luma_globalsize[1]);
//         printf("local %d %d global %d %d weight %d \n",chroma_localsize[0],chroma_localsize[1],chroma_globalsize[0],chroma_globalsize[1],weighted_prediction);
//         printf("local %d %d\n",p->pic_width>>1,p->pic_height>>1);

        mc_h264_chroma_biweighted<<<grid_chroma,thread_chroma,0,(*(p->streams[0]))>>>(p->chroma_references[0],p->chroma_references[1],
        p->chroma_output[0],p->chroma_output[1],p->shared_ref[0],p->shared_ref[1],p->shared_vectors[0],p->shared_vectors[1],p->sub_mb_type,
        p->chroma_weight,p->implicit_weight,p->chroma_log2_weight_denom,p->use_weight,p->use_weight_chroma,p->pic_width>>1,p->pic_height>>1,weighted_prediction);
            
        mc_h264_luma_biweighted<<<grid_luma,thread_luma,0,(*(p->streams[1]))>>>(p->luma_references,p->luma_output,
        p->shared_ref[0],p->shared_ref[1],p->shared_vectors[0],p->shared_vectors[1],p->sub_mb_type,
        p->luma_weight,p->implicit_weight,p->luma_log2_weight_denom,p->use_weight,p->pic_width,p->pic_height,weighted_prediction);
    }
}
