#include "h264_mc_opencl.h"
#include "h264_idct_opencl.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "../h264.h"

/********************************************Add By Biao Wang to support OpenCL***************************************/

#define OPENCL_SET_ARG(arg,argindex,kernelindex) \
	ciErrNum =clSetKernelArg(mc_opencl_kernels[kernelindex],argindex,sizeof(cl_mem),&(arg.mem_obj));\
	oclCheckError(ciErrNum,CL_SUCCESS);

static cl_kernel mc_opencl_kernels[MC_KERNEL_COUNT];

int block_index=0;

static const int partition_sizes[3] = {16,8,4};
static int block_num=1;

const char *mc_opencl_kernel_names[] = {
		"mc_h264_luma_biweighted",
		"mc_h264_chroma_biweighted",
};

static char * mc_opencl_source_file;
static cl_program mc_opencl_program;
static cl_event mc_opencl_event_luma;
static cl_event mc_opencl_event_chroma;
cl_command_queue mc_commandQueue[2];
cl_event mc_copy_event_luma;
cl_event mc_copy_event_chroma[2];
cl_event mc_copy_back_event_luma;
cl_event mc_copy_back_event_chroma[2];

double mc_gpu_time[MC_KERNEL_PROFILE_PHASES] = {0.0,};

unsigned int mc_opencl_reference_size         = 0;
static unsigned int mc_opencl_frame_size      = 0;
static unsigned int mc_opencl_reference_width = 0;
static int mc_opencl_frame_width     = 0;
static int mc_opencl_frame_height    = 0;
static unsigned int mc_opencl_reference_height= 0;
/************************************************************************************************************************
*************************GPU memory objects: shared, luma specific and chroma specific **********************************
*************************************************************************************************************************/

//Shared memory objects for luma and chroma part
//Motion vector contain two kinds of information: reference source, interpolation mode
static opencl_mem_type mc_gpu_shared_vectors[2];
static opencl_mem_type mc_gpu_shared_ref[2];
static opencl_mem_type mc_gpu_luma_weight;
static opencl_mem_type mc_gpu_chroma_weight;
static opencl_mem_type mc_gpu_luma_output;
static opencl_mem_type mc_gpu_implicit_weight;
static opencl_mem_type mc_gpu_sub_mb_type;
opencl_mem_type mc_gpu_luma_references;

static opencl_mem_type mc_gpu_chroma_output[2];
opencl_mem_type mc_gpu_chroma_references[2];

cl_ulong kernel_time_luma = 0;
cl_ulong kernel_time_chroma = 0;
int64_t CPU_MC_16x16 = 0;

/************************************************************************************************************************
*************************CPU memory objects: shared, luma specific and chroma specific **********************************
*************************************************************************************************************************/

//for 4x4 blocks, there might be 16 blocks in each macroblock
MVVector  *mc_cpu_shared_vectors[2][16];
MCREFER   *mc_cpu_shared_ref[2][16];

static gout_t  *mc_cpu_luma_output;
static gout_t  *mc_cpu_chroma_output[2];

uint16_t (*mc_cpu_sub_mb_type)[4];
uint8_t *mc_cpu_luma_input;
uint8_t *mc_cpu_chroma_input[2];
int mc_transferred_reference[16];
int mc_references_FIFO_top=0;

static void backup_mb_border(MBRecContext *d, H264Mb *m, uint8_t *src_y, uint8_t *src_cb, uint8_t *src_cr, int linesize, int uvlinesize){
    int i;
    uint8_t* top_border_y = d->top[m->mb_x].unfiltered_y;
    uint8_t* top_border_cb = d->top[m->mb_x].unfiltered_cb;
    uint8_t* top_border_cr = d->top[m->mb_x].unfiltered_cr;

    uint8_t* left_border_y = d->left.unfiltered_y;
    uint8_t* left_border_cb = d->left.unfiltered_cb;
    uint8_t* left_border_cr = d->left.unfiltered_cr;

    src_y  -=   linesize;
    src_cb -= uvlinesize;
    src_cr -= uvlinesize;

    // There are two lines saved, the line above the top macroblock of a pair,
    // and the line above the bottom macroblock
    left_border_y[0] = top_border_y[15];
    for(i=1; i<17; i++){
        left_border_y[i] = src_y[15+i*  linesize];
    }
    *(uint64_t*)(top_border_y   )   = *(uint64_t*)(src_y +  16*linesize);
    *(uint64_t*)(top_border_y +8)   = *(uint64_t*)(src_y +8+16*linesize);

    left_border_cb[0] = top_border_cb[7];
    left_border_cr[0] = top_border_cr[7];
    for(i=1; i<9; i++){
        left_border_cb[i] = src_cb[7+i*uvlinesize];
        left_border_cr[i] = src_cr[7+i*uvlinesize];
    }
    *(uint64_t*)(top_border_cb)= *(uint64_t*)(src_cb+8*uvlinesize);
    *(uint64_t*)(top_border_cr)= *(uint64_t*)(src_cr+8*uvlinesize);
}

static void xchg_mb_border(MBRecContext *d, H264Mb *m, uint8_t *src_y, uint8_t *src_cb, uint8_t *src_cr, int linesize, int uvlinesize, int xchg){

    int temp8, i;
    uint64_t temp64;
    int deblock_left;
    int deblock_top;

    uint8_t* top_border_y = d->top[m->mb_x].unfiltered_y;
    uint8_t* top_border_cb = d->top[m->mb_x].unfiltered_cb;
    uint8_t* top_border_cr = d->top[m->mb_x].unfiltered_cr;
    uint8_t* top_border_y_next = d->top[m->mb_x +1].unfiltered_y;

    uint8_t* left_border_y = d->left.unfiltered_y;
    uint8_t* left_border_cb = d->left.unfiltered_cb;
    uint8_t* left_border_cr = d->left.unfiltered_cr;

    deblock_left = (m->mb_x > 0);
    deblock_top =  (m->mb_y > 0);

    src_y  -= (  linesize + 1);
    src_cb -= (uvlinesize + 1);
    src_cr -= (uvlinesize + 1);

    #define XCHG(a,b,t,xchg)\
    t= a;\
    if(xchg)\
        a= b;\
    b= t;

    if(deblock_left){
        for(i = !deblock_top; i<16; i++){
            XCHG(left_border_y[i], src_y [i*  linesize], temp8, xchg);
        }
        XCHG(left_border_y[i], src_y [i*  linesize], temp8, 1);

        for(i = !deblock_top; i<8; i++){
            XCHG(left_border_cb[i], src_cb[i*uvlinesize], temp8, xchg);
            XCHG(left_border_cr[i], src_cr[i*uvlinesize], temp8, xchg);
        }
        XCHG(left_border_cb[i], src_cb[i*uvlinesize], temp8, 1);
        XCHG(left_border_cr[i], src_cr[i*uvlinesize], temp8, 1);
    }

    if(deblock_top){
        XCHG(*(uint64_t*)(top_border_y+0), *(uint64_t*)(src_y +1), temp64, xchg);
        XCHG(*(uint64_t*)(top_border_y+8), *(uint64_t*)(src_y +9), temp64, 1);
        if(m->mb_x+1 < d->mb_width){
            XCHG(*(uint64_t*)(top_border_y_next), *(uint64_t*)(src_y +17), temp64, 1);
        }
        XCHG(*(uint64_t*)(top_border_cb), *(uint64_t*)(src_cb+1), temp64, 1);
        XCHG(*(uint64_t*)(top_border_cr), *(uint64_t*)(src_cr+1), temp64, 1);
    }
}

static void kernel_arg_setup(H264Slice *s, int kernel_index, int weighted_prediction){
	cl_int ciErrNum;
	int index=0;
	if(kernel_index%2){
		for(int i=0;i<2;i++){
			OPENCL_SET_ARG(mc_gpu_chroma_references[i],index++,kernel_index);
		}
	}else{
		OPENCL_SET_ARG(mc_gpu_luma_references,index++,kernel_index);
	}

	if(kernel_index%2){
		for(int i=0;i<2;i++){
			OPENCL_SET_ARG(mc_gpu_chroma_output[i],index++,kernel_index);
		}
	}else{
		OPENCL_SET_ARG(mc_gpu_luma_output,index++,kernel_index);
	}
	
    for(int list=0;list<2;list++){
        OPENCL_SET_ARG(mc_gpu_shared_ref[list],index++, kernel_index);
    }
    for(int list=0;list<2;list++){
        OPENCL_SET_ARG(mc_gpu_shared_vectors[list],index++,kernel_index);
    }
    OPENCL_SET_ARG(mc_gpu_sub_mb_type,index++,kernel_index);
    if(kernel_index%2){
        OPENCL_SET_ARG(mc_gpu_chroma_weight, index++, kernel_index);
        OPENCL_SET_ARG(mc_gpu_implicit_weight, index++, kernel_index);
        clSetKernelArg(mc_opencl_kernels[kernel_index],index++, sizeof(int), (void*)&s->chroma_log2_weight_denom);
        clSetKernelArg(mc_opencl_kernels[kernel_index],index++, sizeof(int), (void*)&s->use_weight);
        clSetKernelArg(mc_opencl_kernels[kernel_index],index++, sizeof(int), (void*)&s->use_weight_chroma);
    }else{
        OPENCL_SET_ARG(mc_gpu_luma_weight, index++, kernel_index);
        OPENCL_SET_ARG(mc_gpu_implicit_weight, index++, kernel_index);
        clSetKernelArg(mc_opencl_kernels[kernel_index],index++,sizeof(int),(void*)&s->luma_log2_weight_denom);
        clSetKernelArg(mc_opencl_kernels[kernel_index],index++,sizeof(int),(void*)&s->use_weight);
    }

	const int shift_bit = kernel_index&1;
	int picture_width = mc_opencl_reference_width>>shift_bit;
	int picture_height= (mc_opencl_reference_size/mc_opencl_reference_width)>>shift_bit;
	clSetKernelArg(mc_opencl_kernels[kernel_index],index++,sizeof(int),(void*)&picture_width);
	clSetKernelArg(mc_opencl_kernels[kernel_index],index++,sizeof(int),(void*)&picture_height);
    clSetKernelArg(mc_opencl_kernels[kernel_index],index++,sizeof(int),(void*)&weighted_prediction);
}
#undef OPENCL_SET_ARG



#define FINISH(commandQueue)	\
	ciErrNum =  clFinish(commandQueue);\
	oclCheckError(ciErrNum,CL_SUCCESS);

static inline void shared_memory_transfer(H264Slice *s){
	cl_int ciErrNum =0;
	int num_list =s->slice_type==FF_B_TYPE?2:1;

    ciErrNum|=clEnqueueWriteBuffer(mc_commandQueue[0],mc_gpu_luma_weight.mem_obj,CL_FALSE,0,mc_gpu_luma_weight.size,(void*)&s->luma_weight[0][0][0],0,NULL,NULL);
    ciErrNum|=clEnqueueWriteBuffer(mc_commandQueue[0],mc_gpu_chroma_weight.mem_obj,CL_FALSE,0,mc_gpu_chroma_weight.size,(void*)&s->chroma_weight[0][0][0][0],0,NULL,NULL);           
    ciErrNum|=clEnqueueWriteBuffer(mc_commandQueue[0],mc_gpu_implicit_weight.mem_obj,CL_FALSE,0,mc_gpu_implicit_weight.size,(void*)&s->implicit_weight[0][0][0],0,NULL,NULL);
    ciErrNum|=clEnqueueWriteBuffer(mc_commandQueue[0],mc_gpu_sub_mb_type.mem_obj,CL_FALSE,0,mc_gpu_sub_mb_type.size,mc_cpu_sub_mb_type,0,NULL,NULL);
    int vecblocksize = mc_gpu_shared_vectors[0].size/block_num;
    int refblocksize = mc_gpu_shared_ref[0].size/block_num;
	for(int list=0;list<num_list;list++){
        for(int b=0;b<block_num;b++){
            ciErrNum|=clEnqueueWriteBuffer(mc_commandQueue[0],mc_gpu_shared_vectors[list].mem_obj,CL_FALSE,b*vecblocksize,vecblocksize, mc_cpu_shared_vectors[list][b],0,NULL,NULL); 
            ciErrNum|=clEnqueueWriteBuffer(mc_commandQueue[1],mc_gpu_shared_ref[list].mem_obj, CL_FALSE, b*refblocksize, refblocksize, mc_cpu_shared_ref[list][b],0, NULL,NULL);
            oclCheckError(ciErrNum,CL_SUCCESS);
        }
	}
    clFinish(mc_commandQueue[0]);
    clFinish(mc_commandQueue[1]);
}



void gpu_opencl_mc(H264Context *h, MBRecContext *d, H264Slice *s, SliceBufferEntry *sbe){
	cl_int ciErrNum;
    struct timespec copy[2];
	//transfer memories shared by chroma and luma components:mb_type,reference_index, as well as luma and chroma reference pictures
    if(h->profile==3)
        clock_gettime(CLOCK_REALTIME, &copy[0]);
    shared_memory_transfer(s);
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

	const int kernel_chroma =1;
	const int kernel_luma = 0;

	kernel_arg_setup(s,kernel_chroma,weighted_prediction);
	kernel_arg_setup(s,kernel_luma,weighted_prediction);

/*	printf("****************argument for chroma is over****************\n");*/
	//launch kernel of chroma components
	size_t chroma_globalsize[2] = {8*(d->mb_width),8*(d->mb_height)};
	size_t chroma_localsize[2]  = {8,8};

	size_t luma_globalsize[MC_KERNEL_DIMENSION] = {(16/PER_THREAD_PIXEL)*(d->mb_width) ,4*(d->mb_height)};
	size_t luma_localsize[MC_KERNEL_DIMENSION]  = {16/PER_THREAD_PIXEL,4};

/*	printf("size of output chroma is %d\n",mc_gpu_chroma_output.size/sizeof(gout_t));*/
    if(gpumode==GPU_MC_NOVERLAP){
        ciErrNum = clEnqueueNDRangeKernel(mc_commandQueue[1],mc_opencl_kernels[kernel_luma],2,NULL,luma_globalsize,luma_localsize, 1, &mc_copy_event_luma, &mc_opencl_event_luma);
        ciErrNum =  clEnqueueReadBuffer(mc_commandQueue[1], mc_gpu_luma_output.mem_obj, CL_TRUE, 0, mc_gpu_luma_output.size,mc_cpu_luma_output, 1, &mc_opencl_event_luma,&mc_copy_back_event_luma);
        ciErrNum = clEnqueueNDRangeKernel(mc_commandQueue[0], mc_opencl_kernels[kernel_chroma], 2, NULL, chroma_globalsize, chroma_localsize, 2, mc_copy_event_chroma, &mc_opencl_event_chroma);
        for(int i=0;i<2;i++){
            clEnqueueReadBuffer(mc_commandQueue[0], mc_gpu_chroma_output[i].mem_obj, CL_TRUE,0,mc_gpu_chroma_output[i].size,mc_cpu_chroma_output[i],1,&mc_opencl_event_chroma,&mc_copy_back_event_chroma[i]);
        }
    }else{
        ciErrNum = clEnqueueNDRangeKernel(mc_commandQueue[1],mc_opencl_kernels[kernel_luma],2,NULL,luma_globalsize,luma_localsize, 1, &mc_copy_event_luma, &mc_opencl_event_luma);

        ciErrNum = clEnqueueNDRangeKernel(mc_commandQueue[0], mc_opencl_kernels[kernel_chroma], 2, NULL, chroma_globalsize, chroma_localsize, 2, mc_copy_event_chroma, &mc_opencl_event_chroma);

        ciErrNum =  clEnqueueReadBuffer(mc_commandQueue[1], mc_gpu_luma_output.mem_obj, CL_FALSE, 0, mc_gpu_luma_output.size,mc_cpu_luma_output, 1, &mc_opencl_event_luma,&mc_copy_back_event_luma);

        for(int i=0;i<2;i++){
            clEnqueueReadBuffer(mc_commandQueue[0], mc_gpu_chroma_output[i].mem_obj, CL_FALSE,0,mc_gpu_chroma_output[i].size,mc_cpu_chroma_output[i],1,&mc_opencl_event_chroma,&mc_copy_back_event_chroma[i]);
        }
    //  printf("launched kernel is %s\n",mc_opencl_kernel_names[kernel_luma]);
        oclCheckError(ciErrNum,CL_SUCCESS);
        clFinish(mc_commandQueue[0]);
        clFinish(mc_commandQueue[1]);
    }


	if(h->profile==3){
		mc_gpu_time[MC_KERNEL_CHROMA]+= get_event_time(mc_opencl_event_chroma);
		mc_gpu_time[MC_KERNEL_LUMA] += get_event_time(mc_opencl_event_luma);
		mc_gpu_time[MC_KERNEL_COPY_LUMA] += get_event_time(mc_copy_event_luma)+get_event_time(mc_copy_back_event_luma);
		mc_gpu_time[MC_KERNEL_COPY_CHROMA] += get_event_time(mc_copy_event_chroma[0])+get_event_time(mc_copy_back_event_chroma[0]);
		mc_gpu_time[MC_KERNEL_COPY_CHROMA] += get_event_time(mc_copy_event_chroma[1])+get_event_time(mc_copy_back_event_chroma[1]);
	}
}


#define OPENCL_MEM_SETUP(name,mem_size) \
		name.size = mem_size;			\
		name.mem_obj = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY,name.size, NULL, &ciErrNum);\
		oclCheckError(ciErrNum, CL_SUCCESS);

static int mc_opencl_construct_mem(H264Context *h){
	cl_int ciErrNum;
	const uint32_t mb_num = (h->height/16)*(h->width/16);
	mc_opencl_frame_width 	  = h->width ;
	mc_opencl_frame_height    = h->height;
	mc_opencl_reference_width = mc_opencl_frame_width +2*EDGE_WIDTH;
	mc_opencl_reference_height= mc_opencl_frame_height + 2*EDGE_WIDTH;
	mc_opencl_reference_size  = mc_opencl_reference_height*mc_opencl_reference_width;
	mc_opencl_frame_size      = mc_opencl_frame_height*mc_opencl_frame_width;

    block_num = 16;
	//create GPU memory objects
	OPENCL_MEM_SETUP(mc_gpu_luma_weight,16*2*2*sizeof(int16_t));
	OPENCL_MEM_SETUP(mc_gpu_chroma_weight,16*2*2*2*sizeof(int16_t));
    OPENCL_MEM_SETUP(mc_gpu_implicit_weight,16*16*2*sizeof(int16_t));
    OPENCL_MEM_SETUP(mc_gpu_sub_mb_type, mb_num*sizeof(uint16_t)*4);

	for(int i=0;i<2;i++){
		mc_gpu_chroma_output[i].size = (mc_opencl_frame_height>>1)*(mc_opencl_reference_width>>1)-(EDGE_WIDTH>>1);
		mc_gpu_chroma_output[i].mem_obj = clCreateBuffer(cxGPUContext, CL_MEM_WRITE_ONLY, mc_gpu_chroma_output[i].size, NULL, &ciErrNum);
		oclCheckError(ciErrNum,CL_SUCCESS);
	}
	mc_gpu_luma_output.size = mc_opencl_frame_height*mc_opencl_reference_width-EDGE_WIDTH;
	mc_gpu_luma_output.mem_obj = clCreateBuffer(cxGPUContext, CL_MEM_WRITE_ONLY,mc_gpu_luma_output.size, NULL, &ciErrNum);
	oclCheckError(ciErrNum,CL_SUCCESS);

	for(int list=0;list<2;list++){
        OPENCL_MEM_SETUP(mc_gpu_shared_vectors[list],block_num*mb_num*sizeof(MVVector));
        OPENCL_MEM_SETUP(mc_gpu_shared_ref[list],block_num*mb_num*sizeof(MCREFER));
	}

	//create decoded frames for luma
	OPENCL_MEM_SETUP(mc_gpu_luma_references,  MAX_REFERENCE_COUNT*mc_opencl_reference_size*sizeof(uint8_t));
	//create decoded  frames for chroma
	for(int i=0;i<2;i++){
		OPENCL_MEM_SETUP(mc_gpu_chroma_references[i],MAX_REFERENCE_COUNT*(mc_opencl_reference_size>>2)*sizeof(uint8_t));
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

static int mc_opencl_destruct_mem(){
	cl_int ciErrNum=0;

	//free CPU memories
    av_free(mc_cpu_sub_mb_type);
    for(int list=1;list>=0;list--){
        for(int b=block_num-1;b>=0;b--){
            av_free(mc_cpu_shared_vectors[list][b]);
            av_free(mc_cpu_shared_ref[list][b]);
        }
    }

	//destroy frames in GPU
	for(int i=1;i>=0;i--){
		ciErrNum |= clReleaseMemObject(mc_gpu_chroma_references[i].mem_obj);
	}
	ciErrNum |= clReleaseMemObject(mc_gpu_luma_references.mem_obj);
	oclCheckError(ciErrNum,CL_SUCCESS);

	//free GPU memeories , FIXME is there any difference between global memory and constant memory destruction

	for(int list=1;list>=0;list--){
        clReleaseMemObject(mc_gpu_shared_vectors[list].mem_obj);
        clReleaseMemObject(mc_gpu_shared_ref[list].mem_obj);
        oclCheckError(ciErrNum, CL_SUCCESS);
	}
	clReleaseMemObject(mc_gpu_luma_output.mem_obj);
	for(int i=1;i>=0;i--)
		clReleaseMemObject(mc_gpu_chroma_output[i].mem_obj);
    clReleaseMemObject(mc_gpu_sub_mb_type.mem_obj);
    clReleaseMemObject(mc_gpu_implicit_weight.mem_obj);
	clReleaseMemObject(mc_gpu_chroma_weight.mem_obj);
	clReleaseMemObject(mc_gpu_luma_weight.mem_obj);
	return 0;
}

static int mc_opencl_construct_program(){
    cl_int ciErrNum;
    BuildProgram(KERNEL_SOURCE_MC, &mc_opencl_program);
    
	//Create kernel for mc_opencl_kernels 
    for(int i=0;i<MC_KERNEL_COUNT;i++){
	   	mc_opencl_kernels[i]=clCreateKernel(mc_opencl_program, mc_opencl_kernel_names[i], &ciErrNum);
		oclCheckError(ciErrNum, CL_SUCCESS);
	}
	for(int i=0;i<2;i++){
		mc_commandQueue[i] = clCreateCommandQueue(cxGPUContext,cdDevices[0],CL_QUEUE_PROFILING_ENABLE,&ciErrNum);
		oclCheckError(ciErrNum, CL_SUCCESS);
	}
	

	return 0;
}

static int mc_opencl_destruct_program(){
	cl_int ciErrNum=0;
	if(mc_opencl_event_luma)
		ciErrNum |=clReleaseEvent(mc_opencl_event_luma);
	if(mc_opencl_event_chroma)
		ciErrNum |=clReleaseEvent(mc_opencl_event_chroma);	
	oclCheckError(ciErrNum,CL_SUCCESS);		
	for(int i=0;i<MC_KERNEL_COUNT;i++)
		ciErrNum|= clReleaseKernel(mc_opencl_kernels[i]);
	oclCheckError(ciErrNum,CL_SUCCESS);
    ciErrNum |= clReleaseProgram(mc_opencl_program);
	oclCheckError(ciErrNum,CL_SUCCESS);
	av_free(mc_opencl_source_file);
	return 0;
}

int mc_opencl_construct_env(H264Context *h)
{
	mc_opencl_construct_mem(h);
	mc_opencl_construct_program();
	return 0;
}

int mc_opencl_destruct_env(void)
{
	mc_opencl_destruct_program();
	mc_opencl_destruct_mem();
	return 0;
}


void h264_decode_mb_without_mc_idct(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m)
{
	int i;
	const int mb_x= m->mb_x;
	const int mb_y= m->mb_y;

	int *block_offset = d->block_offset;

	int linesize   = d->linesize;
	int uvlinesize = d->uvlinesize;
	
	uint8_t *dest_y  = s->curr_pic->data[0] + (mb_x + mb_y * linesize  ) * 16;
	uint8_t *dest_cb = s->curr_pic->data[1] + (mb_x + mb_y * uvlinesize) * 8;
	uint8_t *dest_cr = s->curr_pic->data[2] + (mb_x + mb_y * uvlinesize) * 8;

	pred_motion_mb_rec(d, mrs, s, m);

	const int mb_type= m->mb_type;

	if(IS_INTRA(mb_type)){
		xchg_mb_border(d, m, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 1);

		d->hpc.pred8x8[ m->chroma_pred_mode ](dest_cb, uvlinesize);
		d->hpc.pred8x8[ m->chroma_pred_mode ](dest_cr, uvlinesize);

		if(IS_INTRA4x4(mb_type)){
			if(IS_8x8DCT(mb_type)){

				for(i=0; i<16; i+=4){
					uint8_t * const ptr= dest_y + block_offset[i];
					const int dir= mrs->intra4x4_pred_mode_cache[ scan8[i] ];

					const int nnz = mrs->non_zero_count_cache[ scan8[i] ];

					d->hpc.pred8x8l[ dir ](ptr, (mrs->topleft_samples_available<<i)&0x8000,
												(mrs->topright_samples_available<<i)&0x4000, linesize);
					if(nnz){
						squareAdd8x8(ptr,compact_mem[IDCT_Mem_queue.fo].idct8_non_zero_blocks+16*compact_mem[IDCT_Mem_queue.fo].idct8_compact_count,linesize);
					}
				}
			}else{ //if(IS_8x8DCT(mb_type))
				for(i=0; i<16; i++){
					uint8_t * const ptr= dest_y + block_offset[i];
					const int dir= mrs->intra4x4_pred_mode_cache[ scan8[i] ];
					uint8_t *topright;
					int nnz, tr;
					if(dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED){
						const int topright_avail= (mrs->topright_samples_available<<i)&0x8000;
						assert(mb_y || linesize <= block_offset[i]);
						if(!topright_avail){
							tr= ptr[3 - linesize]*0x01010101;
							topright= (uint8_t*) &tr;
						}else
							topright= ptr + 4 - linesize;
					}else
						topright= NULL;

					d->hpc.pred4x4[ dir ](ptr, topright, linesize);

					nnz = mrs->non_zero_count_cache[scan8[i]];
					if(nnz){
						squareAdd4x4(ptr,compact_mem[IDCT_Mem_queue.fo].idct_non_zero_blocks+16*compact_mem[IDCT_Mem_queue.fo].compact_count,linesize);
					}
				}
			  }//end if(IS_8x8DCT(mb_type))
		}else{ //if(IS_INTRA4x4(mb_type))
			d->hpc.pred16x16[ m->intra16x16_pred_mode ](dest_y , linesize);
			for(i=0; i<16; i++){
				int nnz = mrs->non_zero_count_cache[scan8[i]];
				if(nnz||m->mb[16*i]){
				    squareAdd4x4(dest_y+block_offset[i],compact_mem[IDCT_Mem_queue.fo].idct_non_zero_blocks+16*compact_mem[IDCT_Mem_queue.fo].compact_count,linesize);
				}
			}

		}//end if(IS_INTRA4x4(mb_type))
		xchg_mb_border(d, m, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 0);
	}
	else //if(IS_INTRA(mb_type))
	{
		//so this is the inter prediction motion_compensation
		if(m->cbp&15){
			if(IS_8x8DCT(mb_type)){
				for(i=0; i<16; i+=4){
					int nnz = mrs->non_zero_count_cache[scan8[i]];
					if(nnz){
							squareAdd8x8(dest_y+block_offset[i],compact_mem[IDCT_Mem_queue.fo].idct8_non_zero_blocks+compact_mem[IDCT_Mem_queue.fo].idct8_compact_count*16,linesize);		
					}
				}
			}
			else{
				for(i=0; i<16; i++){
					int nnz = mrs->non_zero_count_cache[scan8[i]];
					if(nnz){
						squareAdd4x4(dest_y+block_offset[i],compact_mem[IDCT_Mem_queue.fo].idct_non_zero_blocks+compact_mem[IDCT_Mem_queue.fo].compact_count*16,linesize);
					}
				}
			}
		}
	} //end if(IS_INTRA(mb_type))
	//chroma add
	if(m->cbp&0x30)
	{
		uint8_t *dst;
		for(int i=16;i<16+8;i++)
		{
			if(i<20)
				dst = dest_cb+block_offset[i];
			else
				dst = dest_cr+block_offset[i];

			int nnz = mrs->non_zero_count_cache[scan8[i]];			
			if(nnz||m->mb[16*i]){
				squareAdd4x4(dst,compact_mem[IDCT_Mem_queue.fo].idct_non_zero_blocks+compact_mem[IDCT_Mem_queue.fo].compact_count*16,uvlinesize);
			}
		}
	}

	backup_mb_border(d, m, dest_y, dest_cb, dest_cr, linesize, uvlinesize);
	if (mb_y +1 <d->mb_height && d->top_next != d->top){
		memcpy(&d->top_next[mb_x],&d->top[mb_x], sizeof(TopBorder));
	}

	ff_h264_filter_mb(d, mrs, s, m, dest_y, dest_cb, dest_cr);
}

void h264_decode_mb_without_mc(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m)
{
    int i;
    const int mb_x= m->mb_x;
    const int mb_y= m->mb_y;

    int *block_offset = d->block_offset;

    void (*idct_add)(uint8_t *dst, DCTELEM *block, int stride);
    void (*idct_dc_add)(uint8_t *dst, DCTELEM *block, int stride);

    int linesize   = d->linesize;
    int uvlinesize = d->uvlinesize;

    uint8_t *dest_y  = s->curr_pic->data[0] + (mb_x + mb_y * linesize  ) * 16;
    uint8_t *dest_cb = s->curr_pic->data[1] + (mb_x + mb_y * uvlinesize) * 8;
    uint8_t *dest_cr = s->curr_pic->data[2] + (mb_x + mb_y * uvlinesize) * 8;

    pred_motion_mb_rec(d, mrs, s, m);

    const int mb_type= m->mb_type;

    d->dsp.prefetch(dest_y + (m->mb_x&3)*4*linesize + 64, d->linesize, 4);
    d->dsp.prefetch(dest_cb + (m->mb_x&7)*uvlinesize + 64, dest_cr - dest_cb, 2);

    if(IS_INTRA(mb_type)){
        xchg_mb_border(d, m, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 1);

        d->hpc.pred8x8[ m->chroma_pred_mode ](dest_cb, uvlinesize);
        d->hpc.pred8x8[ m->chroma_pred_mode ](dest_cr, uvlinesize);

        if(IS_INTRA4x4(mb_type)){
            if(IS_8x8DCT(mb_type)){
                idct_dc_add = d->hdsp.h264_idct8_dc_add;
                idct_add    = d->hdsp.h264_idct8_add;

                for(i=0; i<16; i+=4){
                    uint8_t * const ptr= dest_y + block_offset[i];
                    const int dir= mrs->intra4x4_pred_mode_cache[ scan8[i] ];

                    const int nnz = mrs->non_zero_count_cache[ scan8[i] ];
                    d->hpc.pred8x8l[ dir ](ptr, (mrs->topleft_samples_available<<i)&0x8000,
                                                (mrs->topright_samples_available<<i)&0x4000, linesize);
                    if(nnz){
                        if(nnz == 1 && m->mb[i*16])
                            idct_dc_add(ptr, m->mb + i*16, linesize);
                        else
                            idct_add   (ptr, m->mb + i*16, linesize);
                    }
                }
            }else{
                idct_dc_add = d->hdsp.h264_idct_dc_add;
                idct_add    = d->hdsp.h264_idct_add;

                for(i=0; i<16; i++){
                    uint8_t * const ptr= dest_y + block_offset[i];
                    const int dir= mrs->intra4x4_pred_mode_cache[ scan8[i] ];
                    uint8_t *topright;
                    int nnz, tr;
                    if(dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED){
                        const int topright_avail= (mrs->topright_samples_available<<i)&0x8000;
                        assert(mb_y || linesize <= block_offset[i]);
                        if(!topright_avail){
                            tr= ptr[3 - linesize]*0x01010101;
                            topright= (uint8_t*) &tr;
                        }else
                            topright= ptr + 4 - linesize;
                    }else
                        topright= NULL;

                    d->hpc.pred4x4[ dir ](ptr, topright, linesize);
                    nnz = mrs->non_zero_count_cache[ scan8[i] ];
                    if(nnz){
                        if(nnz == 1 && m->mb[i*16])
                            idct_dc_add(ptr, m->mb + i*16, linesize);
                        else
                            idct_add   (ptr, m->mb + i*16, linesize);
                    }
                }
            }
        }else{
            d->hpc.pred16x16[ m->intra16x16_pred_mode ](dest_y , linesize);
        }
        xchg_mb_border(d, m, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 0);
    }
    else //if(IS_INTRA(mb_type))
    {
      //NOTE:motion compensation offloaded to GPU already
    } //end if(IS_INTRA(mb_type))
    //chroma add
    if(!IS_INTRA4x4(mb_type)){

        if(IS_INTRA16x16(mb_type)){

            d->hdsp.h264_idct_add16intra(dest_y, block_offset, m->mb, linesize, mrs->non_zero_count_cache);

        }else if(m->cbp&15){

            if(IS_8x8DCT(mb_type)){
                d->hdsp.h264_idct8_add4(dest_y, block_offset, m->mb, linesize, mrs->non_zero_count_cache);
            }else{
                d->hdsp.h264_idct_add16(dest_y, block_offset, m->mb, linesize, mrs->non_zero_count_cache);
            }
        }
    }

    if(m->cbp&0x30){
        uint8_t *dest[2] = {dest_cb, dest_cr};

        idct_add = d->hdsp.h264_idct_add;
        idct_dc_add = d->hdsp.h264_idct_dc_add;
        for(i=16; i<16+8; i++){
            if(mrs->non_zero_count_cache[ scan8[i] ])
                idct_add   (dest[(i&4)>>2] + block_offset[i], m->mb + i*16, uvlinesize);
            else if(m->mb[i*16])
                idct_dc_add(dest[(i&4)>>2] + block_offset[i], m->mb + i*16, uvlinesize);
        }
    }

    backup_mb_border(d, m, dest_y, dest_cb, dest_cr, linesize, uvlinesize);
    if (mb_y +1 <d->mb_height && d->top_next != d->top){
        memcpy(&d->top_next[mb_x],&d->top[mb_x], sizeof(TopBorder));
    }

    ff_h264_filter_mb(d, mrs, s, m, dest_y, dest_cb, dest_cr);
}
void h264_decode_mb_mc_16x16(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m){
    int i;
    const int mb_x= m->mb_x;
    const int mb_y= m->mb_y;
    const int mb_type= m->mb_type;
    int *block_offset = d->block_offset;

    int linesize   = d->linesize;
    int uvlinesize = d->uvlinesize;

    uint8_t *dest_y  = s->curr_pic->data[0] + (mb_x + mb_y * linesize  ) * 16;
    uint8_t *dest_cb = s->curr_pic->data[1] + (mb_x + mb_y * uvlinesize) * 8;
    uint8_t *dest_cr = s->curr_pic->data[2] + (mb_x + mb_y * uvlinesize) * 8;

    pred_motion_mb_rec_without_intra_and_collect (d, mrs, s, m, 0);

    d->dsp.prefetch(dest_y + (m->mb_x&3)*4*linesize + 64, d->linesize, 4);
    d->dsp.prefetch(dest_cb + (m->mb_x&7)*uvlinesize + 64, dest_cr - dest_cb, 2);

    if(IS_INTRA(mb_type))
        return;
    if(!IS_INTRA(mb_type)){
        m->mb_type = mrs->mb_type[mb_x];
        hl_motion(d, mrs, s, m, dest_y, dest_cb, dest_cr,
                    d->hdsp.qpel_put, d->dsp.put_h264_chroma_pixels_tab,
                    d->hdsp.qpel_avg, d->dsp.avg_h264_chroma_pixels_tab,
                    d->hdsp.weight_h264_pixels_tab, d->hdsp.biweight_h264_pixels_tab);
        m->mb_type = mb_type;
    }

}
