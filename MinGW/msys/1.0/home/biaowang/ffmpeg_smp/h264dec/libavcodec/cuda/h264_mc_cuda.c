#include "h264_mc_cuda.h"
#include "../opencl/h264_mc_opencl.h"
#include "../opencl/gpu_common.h"

#if ARCH_NVIDIA_CUDA
static int block_num=16;
// static cl_event mc_cuda_event_luma;
// static cl_event mc_cuda_event_chroma;
cudaStream_t mc_streams[2];
// cl_event mc_copy_event_luma;
// cl_event mc_copy_event_chroma[2];
// cl_event mc_copy_back_event_luma;
// cl_event mc_copy_back_event_chroma[2];

int mc_cuda_reference_size         = 0;
static unsigned int mc_cuda_frame_size      = 0;
static unsigned int mc_cuda_reference_width = 0;
static int mc_cuda_frame_width     = 0;
static int mc_cuda_frame_height    = 0;
static unsigned int mc_cuda_reference_height= 0;

/************************************************************************************************************************
*************************GPU memory objects: shared, luma specific and chroma specific **********************************
*************************************************************************************************************************/

//Shared memory objects for luma and chroma part
//Motion vector contain two kinds of information: reference source, interpolation mode

uint8_t *mc_gpu_cuda_luma_references=NULL;
uint8_t *mc_gpu_cuda_chroma_references[2]={NULL, NULL};

typedef struct {
    int luma_weight;
    int chroma_weight;
    int implicit_weight;
    int sub_mb_type;
    int chroma_output[2];
    int luma_output;
    int shared_vectors[2];
    int shared_ref[2];
    int luma_references;
    int chroma_references[2];    
} GPU_MEM_SIZE_T;

static cuda_mc_env_t mc_env;

GPU_MEM_SIZE_T d_size;
/************************************************************************************************************************
*************************CPU memory objects: shared, luma specific and chroma specific **********************************
*************************************************************************************************************************/
//for 4x4 blocks, there might be 16 blocks in each macroblock
extern MVVector  *mc_cpu_shared_vectors[2][16];
extern MCREFER   *mc_cpu_shared_ref[2][16];

static uint8_t  *mc_cpu_luma_output;
static uint8_t  *mc_cpu_chroma_output[2];

extern uint16_t (*mc_cpu_sub_mb_type)[4];
extern uint8_t *mc_cpu_luma_input;
extern uint8_t *mc_cpu_chroma_input[2];

static inline void shared_memory_transfer(H264Slice *s,cuda_mc_env_t *p){
    cudaError_t ciErrNum =0;
    int num_list =s->slice_type==FF_B_TYPE?2:1;

    ciErrNum|=cudaMemcpyAsync(p->luma_weight,(void*)&s->luma_weight[0][0][0],d_size.luma_weight,cudaMemcpyHostToDevice,mc_streams[0]);
    ciErrNum|=cudaMemcpyAsync(p->chroma_weight,(void*)&s->chroma_weight[0][0][0][0],d_size.chroma_weight,cudaMemcpyHostToDevice,mc_streams[0]);
    ciErrNum|=cudaMemcpyAsync(p->implicit_weight,(void*)&s->implicit_weight[0][0][0],d_size.implicit_weight,cudaMemcpyHostToDevice,mc_streams[0]);
    ciErrNum|=cudaMemcpyAsync(p->sub_mb_type,mc_cpu_sub_mb_type,d_size.sub_mb_type,cudaMemcpyHostToDevice,mc_streams[0]);
    int vecblocksize = d_size.shared_vectors[0]/block_num;
    int refblocksize = d_size.shared_ref[0]/block_num;
    for(int list=0;list<num_list;list++){
        for(int b=0;b<block_num;b++){
            ciErrNum|=cudaMemcpyAsync(p->shared_vectors[list]+b*vecblocksize/4,mc_cpu_shared_vectors[list][b],vecblocksize,cudaMemcpyHostToDevice,mc_streams[0]); 
            ciErrNum|=cudaMemcpyAsync(p->shared_ref[list]+b*refblocksize,mc_cpu_shared_ref[list][b],refblocksize,cudaMemcpyHostToDevice,mc_streams[1]);
        }
    }
    cudaStreamSynchronize(mc_streams[0]);
    cudaStreamSynchronize(mc_streams[1]);
}



void gpu_cuda_mc(H264Context *h, MBRecContext *d, H264Slice *s, SliceBufferEntry *sbe){
    cudaError_t ciErrNum;
    struct timespec copy[2];
    cuda_mc_env_t *p= &mc_env;
    //transfer memories shared by chroma and luma components:mb_type,reference_index, as well as luma and chroma reference pictures
    if(h->profile==3)
        clock_gettime(CLOCK_REALTIME, &copy[0]);
    shared_memory_transfer(s,p);
    if(h->profile){
        clock_gettime(CLOCK_REALTIME, &copy[1]);
        mc_gpu_time[MC_KERNEL_COPY_DATA_STRUCTURE]+= (double) (1.e3*(copy[1].tv_sec - copy[0].tv_sec) + 1.e-6*(copy[1].tv_nsec - copy[0].tv_nsec));
    }
    int weighted_prediction = s->use_weight;
    mc_cpu_luma_output = s->curr_pic->data[0];
    mc_cpu_chroma_output[0] = s->curr_pic->data[1];
    mc_cpu_chroma_output[1] = s->curr_pic->data[2];

     if(s->slice_type==FF_B_TYPE){
         weighted_prediction = 2;
     }
     else if(s->slice_type==FF_P_TYPE&&s->use_weight){
         weighted_prediction = 1;
     }else{
         weighted_prediction = 0;
     }
    p->use_weight = s->use_weight;
    p->use_weight_chroma = s->use_weight_chroma;
    p->chroma_log2_weight_denom = s->chroma_log2_weight_denom;
    p->luma_log2_weight_denom   = s->luma_log2_weight_denom;
 
// /*  printf("****************argument for chroma is over****************\n");*/
     //launch kernel of chroma components
     int chroma_globalsize[2] = {(d->mb_width),(d->mb_height)};
     int chroma_localsize[2]  = {8,8};
 
     int luma_globalsize[MC_KERNEL_DIMENSION] = {(d->mb_width),(d->mb_height)};
     int luma_localsize[MC_KERNEL_DIMENSION]  = {16,4};
    //NOTE initilize some field for mc_env which is required as parameters for kernels
     p->luma_references = mc_gpu_cuda_luma_references;
     for(int i=0;i<2;i++){
        p->chroma_references[i] = mc_gpu_cuda_chroma_references[i];
        p->streams[i] = &mc_streams[i];
     }

     launch_kernel_mc(luma_globalsize,luma_localsize,chroma_globalsize,chroma_localsize,weighted_prediction,p);

     cudaMemcpyAsync(mc_cpu_luma_output,p->luma_output,d_size.luma_output,cudaMemcpyDeviceToHost,*p->streams[1]);
     cudaMemcpyAsync(mc_cpu_chroma_output[0],p->chroma_output[0],d_size.chroma_output[0],cudaMemcpyDeviceToHost,*p->streams[0]);
     cudaMemcpyAsync(mc_cpu_chroma_output[1],p->chroma_output[1],d_size.chroma_output[1],cudaMemcpyDeviceToHost,*p->streams[0]);     

     cudaStreamSynchronize(*p->streams[0]);
     cudaStreamSynchronize(*p->streams[1]);
//     if(h->profile==3){
//         mc_gpu_time[MC_KERNEL_CHROMA]+= get_event_time(mc_cuda_event_chroma);
//         mc_gpu_time[MC_KERNEL_LUMA] += get_event_time(mc_cuda_event_luma);
//         mc_gpu_time[MC_KERNEL_COPY_LUMA] += get_event_time(mc_copy_event_luma)+get_event_time(mc_copy_back_event_luma);
//         mc_gpu_time[MC_KERNEL_COPY_CHROMA] += get_event_time(mc_copy_event_chroma[0])+get_event_time(mc_copy_back_event_chroma[0]);
//         mc_gpu_time[MC_KERNEL_COPY_CHROMA] += get_event_time(mc_copy_event_chroma[1])+get_event_time(mc_copy_back_event_chroma[1]);
//     }
}

static int mc_cuda_construct_mem(H264Context *h){
    cl_int ciErrNum;
    cuda_mc_env_t *p = &mc_env;
    const uint32_t mb_num = (h->height/16)*(h->width/16);
    mc_cuda_frame_width     = h->width ;
    mc_cuda_frame_height    = h->height;
    mc_cuda_reference_width = mc_cuda_frame_width +2*EDGE_WIDTH;
    mc_cuda_reference_height= mc_cuda_frame_height + 2*EDGE_WIDTH;
    mc_cuda_reference_size  = mc_cuda_reference_height*mc_cuda_reference_width;
    mc_cuda_frame_size      = mc_cuda_frame_height*mc_cuda_frame_width;
    
    p->pic_width   =mc_cuda_reference_width;
    p->pic_height  =mc_cuda_reference_height;
    block_num = 16;
    //create GPU memory objects
    d_size.luma_weight      = 16*2*2*sizeof(int16_t);           cudaHostAlloc((void**)&p->luma_weight,d_size.luma_weight,cudaHostAllocDefault); 
    d_size.chroma_weight    = 16*2*2*2*sizeof(int16_t);         cudaHostAlloc((void**)&p->chroma_weight,d_size.chroma_weight,cudaHostAllocDefault);
    d_size.implicit_weight  = 16*16*2*sizeof(int16_t);          cudaHostAlloc((void**)&p->implicit_weight,d_size.implicit_weight,cudaHostAllocDefault);
    d_size.sub_mb_type      = mb_num*sizeof(uint16_t)*4;        cudaHostAlloc((void**)&p->sub_mb_type, d_size.sub_mb_type,cudaHostAllocDefault);
    for(int i=0;i<2;i++){
        d_size.chroma_output[i] = (mc_cuda_frame_height>>1)*(mc_cuda_reference_width>>1)-(EDGE_WIDTH>>1);
        cudaHostAlloc((void**)&p->chroma_output[i],d_size.chroma_output[i],cudaHostAllocDefault);
    }
    d_size.luma_output = mc_cuda_frame_height*mc_cuda_reference_width-EDGE_WIDTH;
    cudaHostAlloc((void**)&p->luma_output,d_size.luma_output,cudaHostAllocDefault);

    for(int list=0;list<2;list++){
        d_size.shared_vectors[list] = block_num*mb_num*sizeof(MVVector);
        d_size.shared_ref[list] = block_num*mb_num*sizeof(MCREFER); 
        cudaHostAlloc((void**)&p->shared_vectors[list],block_num*mb_num*sizeof(MVVector),cudaHostAllocDefault);
        cudaHostAlloc((void**)&p->shared_ref[list],block_num*mb_num*sizeof(MCREFER),cudaHostAllocDefault);
    }

    //create decoded frames for luma
    cudaHostAlloc((void**)&mc_gpu_cuda_luma_references,  MAX_REFERENCE_COUNT*mc_cuda_reference_size*sizeof(uint8_t),cudaHostAllocDefault);
    d_size.luma_references   = MAX_REFERENCE_COUNT*mc_cuda_reference_size*sizeof(uint8_t);
    //create decoded  frames for chroma
    for(int i=0;i<2;i++){
        cudaHostAlloc((void**)&mc_gpu_cuda_chroma_references[i],MAX_REFERENCE_COUNT*(mc_cuda_reference_size>>2)*sizeof(uint8_t),cudaHostAllocDefault);
        d_size.chroma_references[i] = MAX_REFERENCE_COUNT*(mc_cuda_reference_size>>2)*sizeof(uint8_t);
    }
    //allocate CPU memories
    for(int list=0;list<2;list++){
        for(int b=0;b<block_num;b++){
            mc_cpu_shared_vectors[list][b] = (MVVector*)av_malloc(mb_num*sizeof(MVVector));
            mc_cpu_shared_ref[list][b]     = (MCREFER *)av_malloc(mb_num*sizeof(MCREFER));
        }
    }
    mc_cpu_sub_mb_type        = (sub_mb_type_T *) av_malloc(mb_num*sizeof(uint16_t)*4);
    //notation for transferred reference number
    for(int i=0;i<16;i++)
        mc_transferred_reference[i] = -1;
    return 0;
}

static int mc_cuda_destruct_mem(){
    cuda_mc_env_t *p =&mc_env;
    cl_int ciErrNum=0;
    av_free(mc_cpu_sub_mb_type);
    for(int list=1;list>=0;list--){
        for(int b=block_num-1;b>=0;b--){
            av_free(mc_cpu_shared_vectors[list][b]);
            av_free(mc_cpu_shared_ref[list][b]);
        }
    }
    //destroy frames in GPU
    for(int i=1;i>=0;i--){
        ciErrNum |= cudaFreeHost(mc_gpu_cuda_chroma_references[i]);
    }
    ciErrNum |= cudaFreeHost(mc_gpu_cuda_luma_references);

    //free GPU memeories , FIXME is there any difference between global memory and constant memory destruction

    for(int list=1;list>=0;list--){
        cudaFreeHost(p->shared_vectors[list]);
        cudaFreeHost(p->shared_ref[list]);
    }
    cudaFreeHost(p->luma_output);
    for(int i=1;i>=0;i--)
        cudaFreeHost(p->chroma_output[i]);
    cudaFreeHost(p->sub_mb_type);
    cudaFreeHost(p->implicit_weight);
    cudaFreeHost(p->chroma_weight);
    cudaFree(p->luma_weight);
    return 0;
}

int mc_cuda_construct_env(H264Context *h)
{
    mc_cuda_construct_mem(h);
    for(int i=0;i<2;i++){
        cudaStreamCreate(&mc_streams[i]);
    }
    return 0;
}

int mc_cuda_destruct_env(void)
{
    for(int i=0;i<2;i++){
        cudaStreamDestroy(mc_streams[i]);
    }
    mc_cuda_destruct_mem();
    return 0;
}

#endif
