#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "../h264.h"
#include "../h264_pred_mode.h"
#include "h264_idct_opencl.h"
/********************************************Add By Biao Wang to support OpenCL***************************************/

//Global OpenCL objects
static cl_kernel idct_opencl_kernels[IDCT_KERNEL_COUNT];

static opencl_mem_type inputmem;
static opencl_mem_type idct8inputmem;
static cl_program idct_opencl_program;
cl_command_queue idct_commandQueue[2];
static char *idct_opencl_source_file;

const char *idct_opencl_kernel_names[] = {
		"idct_asyn_4x4_baseline_TB2",
		"idct_asyn_8x8_baseline_TB2",
};

static const uint8_t non_zero_map[24]={
	 0, 1, 4, 5,
	 2, 3, 6, 7,
	 8, 9, 12, 13,
	 10, 11, 14, 15,
	 16, 17, 18, 19,
	 20, 21, 22, 23
};


int *idct_out;
int *idct_out_reference;

IDCTCompactedBufQueue IDCT_Mem_queue;
IDCTCompactedBuf *compact_mem;

double idct_gpu_time_fine[IDCT_KERNEL_PROFILE_PHASES];

unsigned int exec_kernel=2; //default is kernel 2

int64_t global_comp_static[2]={0,0};
int64_t global_comp_time_static[2] = {0,0};

int mb_4x4_effective=0;
int mb_4x4_effective_I=0;
int mb_4x4_effective_P=0;
int mb_4x4_effective_B=0;

int mb_4x4_dc_effective=0;
int mb_4x4_dc_effective_I=0;
int mb_4x4_dc_effective_P=0;
int mb_4x4_dc_effective_B=0;

int mb_8x8_effective =0;
int mb_8x8_effective_I =0;
int mb_8x8_effective_P =0;
int mb_8x8_effective_B =0;

int mb_8x8_dc_effective =0;
int mb_8x8_dc_effective_I =0;
int mb_8x8_dc_effective_P =0;
int mb_8x8_dc_effective_B =0;


char SLICE_COLLECTOR[1000];

int mb_8x8_all= 0;
int mb_4x4_all= 0;

int mb_4x4_I=0;
int mb_4x4_B=0;
int mb_4x4_P=0;

int mb_4x4_I_effective=0;
int mb_4x4_B_effective=0;
int mb_4x4_P_effective=0;

int mb_8x8_I=0;
int mb_8x8_B=0;
int mb_8x8_P=0;

int mb_8x8_I_effective=0;
int mb_8x8_B_effective=0;
int mb_8x8_P_effective=0;

int num_frame_I=0;
int num_frame_P=0;
int num_frame_B=0;


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

void squareAdd4x4(uint8_t *dst,short *src,int stride)
{

#define ADD_IDCT_4(p,t,z) \
	"movd      (%0),"#t" \n\t" 	\
	"punpcklbw "#z","#t" \n\t"  \
	"paddsw	   "#t","#p" \n\t"  \
	"packuswb  "#z","#p" \n\t"  \
	"movd	   "#p",(%0) \n\t"

#if HAVE_SSSE3
	__asm__ volatile (
	"pxor %%mm7,%%mm7  \n\t"
	"movq (%0),%%mm0   \n\t"
	"movq 8(%0),%%mm2  \n\t"
	"movq 16(%0),%%mm3 \n\t"
	"movq 24(%0),%%mm4 \n\t"
	::"r"(src));

	__asm__ volatile (
	ADD_IDCT_4(%%mm0,%%mm1,%%mm7)
	"add %1,%0		\n\t"
	ADD_IDCT_4(%%mm2,%%mm5,%%mm7)
	"add %1,%0		\n\t"
	ADD_IDCT_4(%%mm3,%%mm6,%%mm7)
	"add %1,%0		\n\t"
	ADD_IDCT_4(%%mm4,%%mm0,%%mm7)
	:"+r"(dst)
	:"r"((x86_reg)stride)
	);
#else
	uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

	for(int i=0;i<4;i++)
	{
        dst[i + 0*stride]= cm[ dst[i + 0*stride] + src[i + 0*4] ];
        dst[i + 1*stride]= cm[ dst[i + 1*stride] + src[i + 1*4] ];
        dst[i + 2*stride]= cm[ dst[i + 2*stride] + src[i + 2*4] ];
        dst[i + 3*stride]= cm[ dst[i + 3*stride] + src[i + 3*4] ];
	}
#endif
	compact_mem[IDCT_Mem_queue.fo].compact_count++;
}

void squareAdd8x8(uint8_t *dst,short *src,int stride)
{

#define ADD_IDCT_8( p, d, t, z )\
        "movq       "#d", "#t" \n"\
        "punpcklbw  "#z", "#t" \n"\
        "paddsw     "#t", "#p" \n"\
        "packuswb   "#p", "#p" \n"\
        "movq       "#p", "#d" \n"

#if HAVE_SSSE3

	__asm__ volatile (
	"pxor %%xmm8,%%xmm8		\n\t"
	"movdqa (%0),%%xmm0		\n"
	"movdqa 0x10(%0),%%xmm1 \n"
	"movdqa 0x20(%0),%%xmm2	\n"
	"movdqa 0x30(%0),%%xmm3	\n"
	"movdqa 0x40(%0),%%xmm4	\n"
	"movdqa 0x50(%0),%%xmm5	\n"
	"movdqa 0x60(%0),%%xmm6	\n"
	"movdqa 0x70(%0),%%xmm7	\n"
	::"r"(src));

	__asm__ volatile (
        ADD_IDCT_8(%%xmm0, (%0),      %%xmm9, %%xmm8)
        ADD_IDCT_8(%%xmm1, (%0,%2),   %%xmm9, %%xmm8)
        ADD_IDCT_8(%%xmm2, (%0,%2,2), %%xmm9, %%xmm8)
        ADD_IDCT_8(%%xmm3, (%0,%3),   %%xmm9, %%xmm8)
        "lea     (%0,%2,4), %0 \n"
        ADD_IDCT_8(%%xmm4, (%0),      %%xmm9, %%xmm8)
        ADD_IDCT_8(%%xmm5, (%0,%2),   %%xmm9, %%xmm8)
        ADD_IDCT_8(%%xmm6, (%0,%2,2), %%xmm9, %%xmm8)
        ADD_IDCT_8(%%xmm7, (%0,%3),   %%xmm9, %%xmm8)
        :"+r"(dst)
        :"r"(src), "r"((x86_reg)stride), "r"((x86_reg)3L*stride));
#else

	uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
	for(int i=0;i<8;i++)
	{
        dst[i + 0*stride] = cm[ dst[i + 0*stride] + src[i + 0*8]];
        dst[i + 1*stride] = cm[ dst[i + 1*stride] + src[i + 1*8]];
        dst[i + 2*stride] = cm[ dst[i + 2*stride] + src[i + 2*8]];
        dst[i + 3*stride] = cm[ dst[i + 3*stride] + src[i + 3*8]];
        dst[i + 4*stride] = cm[ dst[i + 4*stride] + src[i + 4*8]];
        dst[i + 5*stride] = cm[ dst[i + 5*stride] + src[i + 5*8]];
        dst[i + 6*stride] = cm[ dst[i + 6*stride] + src[i + 6*8]];
        dst[i + 7*stride] = cm[ dst[i + 7*stride] + src[i + 7*8]];
	}

#endif

	compact_mem[IDCT_Mem_queue.fo].idct8_compact_count +=4;
}

void idct_cpu_mem_construct(H264Context *h){
    const uint32_t mb_num = (h->height/16)*(h->width/16);    
    compact_mem = av_malloc(sizeof(IDCTCompactedBuf)*h->threads);
    for(int i=0;i<h->threads;i++){
        compact_mem[i].compact_count=0;
        compact_mem[i].idct8_compact_count =0;
        compact_mem[i].idct_non_zero_blocks  = (short*)av_malloc(MB_RESIDUAL_SIZE*mb_num*sizeof(short));
        compact_mem[i].idct8_non_zero_blocks = (short*)av_malloc(MB_RESIDUAL_SIZE*mb_num*sizeof(short));
    }
    IDCT_Mem_queue.cnt = IDCT_Mem_queue.fi= IDCT_Mem_queue.fo= 0;
    IDCT_Mem_queue.size = h->threads;
}
void idct_cpu_mem_destruct(){
    int queue_size = IDCT_Mem_queue.size;
    for(int i=0;i<queue_size;i++){
        compact_mem[i].compact_count=0;
        compact_mem[i].idct8_compact_count =0;
        av_free(compact_mem[i].idct_non_zero_blocks);
        av_free(compact_mem[i].idct8_non_zero_blocks);
    }
    av_free(compact_mem);
}
static int idct_opencl_construct_mem(H264Context *h){
	cl_int ciErrNum;
      //construct the memory objects
	
	const uint32_t mb_num = (h->height/16)*(h->width/16);

	inputmem.mem_obj = clCreateBuffer(cxGPUContext,CL_MEM_READ_WRITE,MB_RESIDUAL_SIZE*mb_num*sizeof(short), NULL, &ciErrNum);
	idct8inputmem.mem_obj = clCreateBuffer(cxGPUContext,CL_MEM_READ_WRITE,MB_RESIDUAL_SIZE*mb_num*sizeof(short), NULL, &ciErrNum);

	//construct the host memory objects
	idct_out = (int *)av_malloc(MB_RESIDUAL_SIZE*mb_num*sizeof(int));
	idct_out_reference = (int *)av_malloc(MB_RESIDUAL_SIZE*mb_num*sizeof(int));
	idct_cpu_mem_construct(h);

	return 0;
}

static int idct_opencl_construct_program(void){
    cl_int ciErrNum;
    // create the program
    BuildProgram(KERNEL_SOURCE_IDCT, &idct_opencl_program);

    //Create kernel for idct_opencl_kernels 
    for(int i=0;i<IDCT_KERNEL_COUNT;i++){
	   	idct_opencl_kernels[i]=clCreateKernel(idct_opencl_program, idct_opencl_kernel_names[i], &ciErrNum);
		oclCheckError(ciErrNum, CL_SUCCESS);
    }

    for(int i=0;i<IDCT_KERNEL_COUNT;i++){
	    idct_commandQueue[i] = clCreateCommandQueue(cxGPUContext,cdDevices[0],CL_QUEUE_PROFILING_ENABLE,&ciErrNum);
	    oclCheckError(ciErrNum, CL_SUCCESS);
    }
    return 0;
}

int idct_opencl_construct_env(H264Context *h)
{
	idct_opencl_construct_program();
	idct_opencl_construct_mem(h);
	return 0;
}

static void idct_opencl_destruct_program(){
	cl_int ciErrNum;
	for(int i=0;i<IDCT_KERNEL_COUNT;i++)
		clReleaseCommandQueue(idct_commandQueue[i]);
	for(int i=0;i<IDCT_KERNEL_COUNT;i++)
		ciErrNum|= clReleaseKernel(idct_opencl_kernels[i]);
    ciErrNum |= clReleaseProgram(idct_opencl_program);
	av_free(idct_opencl_source_file);

}
static void idct_opencl_destruct_mem(){
	cl_int ciErrNum=0;

	idct_cpu_mem_destruct();
	av_free(idct_out_reference);
	av_free(idct_out);
	ciErrNum|= clReleaseMemObject(idct8inputmem.mem_obj);
	ciErrNum|= clReleaseMemObject(inputmem.mem_obj);
    oclCheckError(ciErrNum, CL_SUCCESS);
}

int idct_opencl_destruct_env(void)
{
	idct_opencl_destruct_program();
	idct_opencl_destruct_mem();
	return 0;
}

/********************************************************************
 *  the result is int to avoid loss of overflow or downflow
 ********************************************************************/
static const int x_offset[4]={0, 1*16, 4*16, 5*16};
static const int y_offset[4]={0, 2*16, 8*16, 10*16};


void idct4x4( int *dst,  int16_t *block, int block_stride, int shift){
    int i;

    block[0] += 1<<(shift-1);

    for(i=0; i<4; i++){
        const int z0=  block[0 + block_stride*i]     +  block[2 + block_stride*i];
        const int z1=  block[0 + block_stride*i]     -  block[2 + block_stride*i];
        const int z2= (block[1 + block_stride*i]>>1) -  block[3 + block_stride*i];
        const int z3=  block[1 + block_stride*i]     + (block[3 + block_stride*i]>>1);

        block[0 + block_stride*i]= z0 + z3;
        block[1 + block_stride*i]= z1 + z2;
        block[2 + block_stride*i]= z1 - z2;
        block[3 + block_stride*i]= z0 - z3;
    }

    for(i=0; i<4; i++){
        const int z0=  block[i + block_stride*0]     +  block[i + block_stride*2];
        const int z1=  block[i + block_stride*0]     -  block[i + block_stride*2];
        const int z2= (block[i + block_stride*1]>>1) -  block[i + block_stride*3];
        const int z3=  block[i + block_stride*1]     + (block[i + block_stride*3]>>1);

        block[i + 0*block_stride]= ((z0 + z3) >> shift);
        block[i + 1*block_stride]= ((z1 + z2) >> shift);
        block[i + 2*block_stride]= ((z1 - z2) >> shift);
        block[i + 3*block_stride]= ((z0 - z3) >> shift);
    }
}
/***********************************************************************************************************************/
/***********************************************add support for ssse3***************************************************/
/***********************************************************************************************************************/


void h264_decode_mb_without_idct(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m)
{
	extern void printmatrix(uint8_t* dst,int size,int stride);
//#define PREDDEBUG
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

	pred_motion_mb_rec(d,mrs,s,m);

    d->dsp.prefetch(dest_y + (m->mb_x&3)*4*linesize + 64, d->linesize, 4);
    d->dsp.prefetch(dest_cb + (m->mb_x&7)*uvlinesize + 64, dest_cr - dest_cb, 2);

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
		//so this is the inter prediction motion_compesation
		hl_motion(d, mrs, s, m, dest_y, dest_cb, dest_cr,
					d->hdsp.qpel_put, d->dsp.put_h264_chroma_pixels_tab,
					d->hdsp.qpel_avg, d->dsp.avg_h264_chroma_pixels_tab,
					d->hdsp.weight_h264_pixels_tab, d->hdsp.biweight_h264_pixels_tab);
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

int gpu_opencl_idct(H264Context *h)
{
	//collect information shared by all kernels
	cl_int ciErrNum;

	int tmp = compact_mem[IDCT_Mem_queue.fi].compact_count*sizeof(short)*16;
	//make it BASELINE_THREAD_BLCOK_MAPPED_DATA_8x8 alignment
	inputmem.size = tmp%TBSIZE_2_MAPPED_DATA_4x4?(tmp/TBSIZE_2_MAPPED_DATA_4x4+1)*TBSIZE_2_MAPPED_DATA_4x4:tmp;
	tmp = compact_mem[IDCT_Mem_queue.fi].idct8_compact_count*sizeof(short)*16;
	idct8inputmem.size = tmp%TBSIZE_2_MAPPED_DATA_8x8?(tmp/TBSIZE_2_MAPPED_DATA_8x8+1)*TBSIZE_2_MAPPED_DATA_8x8:tmp;
	//FIXME: if input frame or sequence have no 8x8 blocks, zero length memory will result into a error
	if(idct8inputmem.size==0)
		idct8inputmem.size=16;

	cl_event idct4_event[3];	
	cl_event idct8_event[3];
	ciErrNum = clEnqueueWriteBuffer(idct_commandQueue[0],inputmem.mem_obj,CL_FALSE,0,inputmem.size,compact_mem[IDCT_Mem_queue.fi].idct_non_zero_blocks,0,NULL,&idct4_event[0]);
	ciErrNum = clEnqueueWriteBuffer(idct_commandQueue[1],idct8inputmem.mem_obj,CL_FALSE,0,idct8inputmem.size,compact_mem[IDCT_Mem_queue.fi].idct8_non_zero_blocks,0,NULL,&idct8_event[0]);

	size_t globalsize[2];
	size_t localsize[2];
	size_t idct8_globalsize[2];
	size_t idct8_localsize[2];

	int global_dim = inputmem.size/TBSIZE_2_MAPPED_DATA_4x4;
	int idct8_global_dim = idct8inputmem.size/TBSIZE_2_MAPPED_DATA_8x8;

	globalsize[0]=32*global_dim;
	globalsize[1]=TBSIZE_2_WARP_IN_THREAD_BLOCKS;
	localsize[0] = 32;
	localsize[1] = TBSIZE_2_WARP_IN_THREAD_BLOCKS;

	idct8_globalsize[0]= 32*idct8_global_dim;
	idct8_globalsize[1]= TBSIZE_2_WARP_IN_THREAD_BLOCKS;
	idct8_localsize[0] = 32;
	idct8_localsize[1] = TBSIZE_2_WARP_IN_THREAD_BLOCKS;

	ciErrNum |= clSetKernelArg(idct_opencl_kernels[0],0, sizeof(cl_mem), &inputmem.mem_obj);
	ciErrNum |= clSetKernelArg(idct_opencl_kernels[1],0, sizeof(cl_mem), &idct8inputmem.mem_obj);

	ciErrNum |= clEnqueueNDRangeKernel(idct_commandQueue[0], idct_opencl_kernels[0], 2, NULL, globalsize,
						   localsize, 1, &idct4_event[0], &idct4_event[1]);
	ciErrNum |= clEnqueueNDRangeKernel(idct_commandQueue[1], idct_opencl_kernels[1], 2, NULL, idct8_globalsize,
						   idct8_localsize,1, &idct8_event[0], &idct8_event[1]);

	oclCheckError(ciErrNum,CL_SUCCESS);
	//read back the output of idct in GPU
	clEnqueueReadBuffer(idct_commandQueue[0], inputmem.mem_obj, CL_FALSE, 0, inputmem.size,compact_mem[IDCT_Mem_queue.fi].idct_non_zero_blocks,
							  1, &idct4_event[1],
#if HAVE_KERNEL_PROFILING
	&idct4_event[2]
#else
	NULL
#endif
	);
	clEnqueueReadBuffer(idct_commandQueue[1], idct8inputmem.mem_obj, CL_FALSE, 0, idct8inputmem.size, compact_mem[IDCT_Mem_queue.fi].idct8_non_zero_blocks, 1, &idct8_event[1],
#if HAVE_KERNEL_PROFILING
	&idct8_event[2]
#else
	NULL
#endif
	);
	clFinish(idct_commandQueue[0]);
	clFinish(idct_commandQueue[1]);
	if(h->profile==3){
	  cl_ulong timestamps[2]={0,0};
	  clGetEventProfilingInfo(idct4_event[1],CL_PROFILING_COMMAND_START,sizeof(cl_ulong),&timestamps[0],NULL);
	  clGetEventProfilingInfo(idct4_event[1],CL_PROFILING_COMMAND_END,sizeof(cl_ulong),&timestamps[1],NULL);
	  idct_gpu_time_fine[IDCT_KERNEL_4x4]+= (double)((1.e-6)*(timestamps[1]-timestamps[0]));
	  clGetEventProfilingInfo(idct8_event[1],CL_PROFILING_COMMAND_START,sizeof(cl_ulong),&timestamps[0],NULL);
	  clGetEventProfilingInfo(idct8_event[1],CL_PROFILING_COMMAND_END,sizeof(cl_ulong),&timestamps[1],NULL);	
	  idct_gpu_time_fine[IDCT_KERNEL_8x8] += (double)((1.e-6)*(timestamps[1]-timestamps[0])); 
	}
	compact_mem[IDCT_Mem_queue.fi].idct8_compact_count=0;
	compact_mem[IDCT_Mem_queue.fi].compact_count=0;
	oclCheckError(ciErrNum, CL_SUCCESS);
	return 0;
}




void cpu_slice_idct(H264Context *h){
    H264DSPContext *c = NULL;
    c = av_malloc(sizeof(H264DSPContext));
#if HAVE_SSSE3
    ff_h264dsp_init_idct(c);
#else
    c->h264_idct_add= ff_h264_idct_add_c;
    c->h264_idct8_add= ff_h264_idct8_add_c;
#endif
    void (*idct_add)(uint8_t *dst, DCTELEM *block, int stride);

    idct_add = c->h264_idct_add;

    int idct_count_tmp = compact_mem[IDCT_Mem_queue.fi].compact_count;
    for(int i=0;i<idct_count_tmp;i++){
        idct_add   ((uint8_t*)0, compact_mem[IDCT_Mem_queue.fi].idct_non_zero_blocks+i*16,4*sizeof(DCTELEM));
    }
    //set count 
    idct_count_tmp = compact_mem[IDCT_Mem_queue.fi].idct8_compact_count;
    if(idct_count_tmp%4){
        fprintf(stderr,"idct8_compact_count is increased by step 4, something is not right\n");
        av_exit(-1);
    }
    //reset function pointer

    idct_add    = c->h264_idct8_add;
    
    for(int i=0;i<idct_count_tmp;i+=4)
        idct_add   ((uint8_t*)0, compact_mem[IDCT_Mem_queue.fi].idct8_non_zero_blocks+i*16, 8*sizeof(DCTELEM));
    
    compact_mem[IDCT_Mem_queue.fi].compact_count=0;
    compact_mem[IDCT_Mem_queue.fi].idct8_compact_count =0;
    av_free(c);
}
