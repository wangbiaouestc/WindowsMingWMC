#include "config.h"

#include "h264_types.h"
#include "h264_parser.h"
#include "h264_nal.h"
#include "h264_entropy.h"
#include "h264_rec.h"
#include "h264_misc.h"
#include "h264_pred_mode.h"

#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "opencl/h264_mc_opencl.h"
#include "opencl/h264_idct_opencl.h"
#include "cuda/h264_cuda.h"
#include "cuda/h264_mc_cuda.h"
#include "cuda/h264_idct_cuda.h"

int gpumode=0;
double idct_gpu_time=0.0;
double mc_total_time[MC_PROFILE_PHASES]=  {0.0,};

static inline double getmillisecond(struct timespec end, struct timespec start){
    return (double) (1.e3*(end.tv_sec - start.tv_sec) + 1.e-6*(end.tv_nsec - start.tv_nsec)); 
}

static int frames=0;
static pthread_cond_t task_ed_cond, task_mbd_cond;
static pthread_mutex_t task_ed_lock, task_mbd_lock;

typedef int FUN_GPU_ENV_BUILD(H264Context *h);
typedef int FUN_GPU_ENV_DESTROY(void);
typedef int FUN_GPU_IDCT(H264Context *h);
typedef void FUN_GPU_MC(H264Context *h, MBRecContext *d, H264Slice *s, SliceBufferEntry *sbe);

typedef struct {
    FUN_GPU_ENV_BUILD *gpu_construct_environment;
    FUN_GPU_ENV_DESTROY *gpu_destroy_environment;
    FUN_GPU_IDCT *gpu_idct;
    FUN_GPU_MC *gpu_mc;
} GPU_PROGRAMMING_ENV_T;

static const GPU_PROGRAMMING_ENV_T myGPUEnv[2] = {
    {
    &gpu_opencl_construct_environment,
    &gpu_opencl_destroy_environment,
    &gpu_opencl_idct,
    &gpu_opencl_mc,        
    },    
    {
#if ARCH_NVIDIA_CUDA
    &gpu_cuda_construct_environment,
    &gpu_cuda_destroy_environment,
    &gpu_cuda_idct,
    &gpu_cuda_mc,
#endif    
    }
};

static void notify_one_worker(H264Context *h, int is_rec){
    if(is_rec){
        pthread_mutex_lock(&task_mbd_lock);
        pthread_cond_signal(&task_mbd_cond);
        pthread_mutex_unlock(&task_mbd_lock);
    }else{
        pthread_mutex_lock(&task_ed_lock);
        pthread_cond_signal(&task_ed_cond);
        pthread_mutex_unlock(&task_ed_lock);
    }
}

static void push_sbe (SliceBufferQueue *sbq, SliceBufferEntry *sbe, int notify ){
    pthread_mutex_lock(&sbq->lock);
    while (sbq->cnt >= sbq->size)
        pthread_cond_wait(&sbq->cond, &sbq->lock);
    sbq->queue[sbq->fi] = sbe;
    sbq->cnt++;
    sbq->fi++; sbq->fi %= sbq->size;
    if (notify)
        pthread_cond_signal(&sbq->cond);
    pthread_mutex_unlock(&sbq->lock);
}

static SliceBufferEntry* pop_sbe (SliceBufferQueue *sbq, int block){
    SliceBufferEntry *sbe=NULL;

    pthread_mutex_lock(&sbq->lock);
    
    if (block){
        while (sbq->cnt <= 0)
            pthread_cond_wait(&sbq->cond, &sbq->lock);
    }else {
        if (sbq->cnt <= 0)
            goto nonblock;
    }
    sbe = sbq->queue[sbq->fo];
    sbq->cnt--;
    sbq->fo++; sbq->fo %= sbq->size;
    pthread_cond_signal(&sbq->cond);
nonblock:
    pthread_mutex_unlock(&sbq->lock);

    return sbe;
}

static void pop_next_ed(H264Context *h, SliceBufferEntry **psbe){

    pthread_mutex_lock(&task_ed_lock);
    for(;;){
        if ( (*psbe = pop_sbe(&h->sb_q[ENTROPY], 0 ) ) )
            break;
        pthread_cond_wait(&task_ed_cond, &task_ed_lock);
    }
    pthread_mutex_unlock(&task_ed_lock);
}

static void pop_next_mbd(H264Context *h, SliceBufferEntry **psbe){
    pthread_mutex_lock(&task_mbd_lock);
    for(;;){
        if( (*psbe=pop_sbe(&h->sb_q[MBDEC],0 ) ) )
            break;
        pthread_cond_wait(&task_mbd_cond,&task_mbd_lock);
    }
    pthread_mutex_unlock(&task_mbd_lock);    
}

static void decode_slice_gpu(H264Context *h, MBRecContext *d, H264Slice *s2, H264Mb *mbs, SliceBufferEntry *sbe){
    //motion compensation only works for P and B slice_type
    struct timespec timestamps[MC_PROFILE_PHASES+1];
    int stamp_idx=0;
    if(h->profile==3)
        clock_gettime(CLOCK_REALTIME, &timestamps[stamp_idx++]);
    for (int i=0; i<2; i++){
        for(int j=0; j< s2->ref_count[i]; j++){
            if (s2->ref_list_cpn[i][j] ==-1)
                continue;
            int k;
            for (k=0; k<h->max_dpb_cnt; k++){
                if(h->dpb[k].reference >= 2 && h->dpb[k].cpn == s2->ref_list_cpn[i][j]){
                    s2->dp_ref_list[i][j] = &h->dpb[k];
                    break;
                }
            }
        }
    }
    sbe->dp = get_dpb_entry(h, s2);
    if (!h->no_mbd){
        if (gpumode==CPU_BASELINE){
            for(int j=0; j<d->mb_height; j++){
                init_mbrec_context(d, d->mrs, s2, j);
                for(int i=0; i<d->mb_width; i++){
                    H264Mb *m = &mbs[i + j*d->mb_width];
                    h264_decode_mb_internal(d, d->mrs, s2, m);
                }
                draw_edges(d, s2, j);
            }
        }
        if (gpumode==GPU_IDCT||gpumode==CPU_BASELINE_IDCT){
            for(int j=0; j<d->mb_height; j++){
                init_mbrec_context(d, d->mrs, s2, j);
                for(int i=0; i<d->mb_width; i++){
                    H264Mb *m = &mbs[i + j*d->mb_width];
                    h264_decode_mb_without_idct(d, d->mrs, s2, m);
                }
                draw_edges(d, s2, j);
            }                       
        }else if (gpumode==CPU_BASELINE_TOTAL||gpumode==GPU_TOTAL||gpumode==CPU_BASELINE_MC||gpumode==GPU_MC||gpumode==GPU_MC_NOVERLAP){
            if(s2->slice_type==FF_I_TYPE){
                if(gpumode==CPU_BASELINE_TOTAL||gpumode==GPU_TOTAL){
                    for(int j=0; j<d->mb_height; j++){
                        init_mbrec_context(d, d->mrs, s2, j);
                        for(int i=0; i<d->mb_width; i++){
                            H264Mb *m = &mbs[i + j*d->mb_width];
                            h264_decode_mb_without_idct(d, d->mrs, s2, m);
                        }
                        draw_edges(d, s2, j);
                    }                    
                }else{
                    for(int j=0; j<d->mb_height; j++){
                        init_mbrec_context(d, d->mrs, s2, j);
                        for(int i=0; i<d->mb_width; i++){
                            H264Mb *m = &mbs[i + j*d->mb_width];
                            h264_decode_mb_internal(d, d->mrs, s2, m);
                        }
                        draw_edges(d, s2, j);
                    }                    
                }
            }else{
                if(gpumode==GPU_TOTAL||gpumode==GPU_MC||gpumode==GPU_MC_NOVERLAP){
                    for(int j=0;j<d->mb_height;j++){
                        init_mbrec_context(d, d->mrs, s2, j);
                        for(int i=0; i<d->mb_width; i++){
                            H264Mb *m = &mbs[i + j*d->mb_width];
                            pred_motion_mb_rec_without_intra_and_collect(d, d->mrs, s2, m, 1);
                        }
                    }
                    if(h->profile==3){
                       //FIXME: Note the MC_OVERHEAD time is included in the MC time
                       clock_gettime(CLOCK_REALTIME, &timestamps[stamp_idx]);
                       mc_total_time[MC_OVERHEAD] += getmillisecond(timestamps[stamp_idx],timestamps[stamp_idx-1]);
                    }
                    //launch motion compensation kernel
                    myGPUEnv[ARCH_NVIDIA_CUDA].gpu_mc(h, d,s2,sbe);
                }
                else if(gpumode==CPU_BASELINE_TOTAL||gpumode==CPU_BASELINE_MC){ //CPU SIMD MC 16x16 kernel
                    for(int j=0; j<d->mb_height; j++){
                        init_mbrec_context(d, d->mrs, s2, j);
                        for(int i=0; i<d->mb_width; i++){
                            H264Mb *m = &mbs[i + j*d->mb_width];
                            h264_decode_mb_mc_16x16(d, d->mrs, s2, m);
                        }
                    }
                }
                if(h->profile==3){
                    clock_gettime(CLOCK_REALTIME, &timestamps[stamp_idx++]);
                    mc_total_time[MC_TIME] += getmillisecond(timestamps[stamp_idx-1],timestamps[stamp_idx-2]);
                }
                if(gpumode==CPU_BASELINE_TOTAL||gpumode==GPU_TOTAL){
                    for(int j=0; j<d->mb_height; j++){
                        init_mbrec_context(d, d->mrs, s2, j);
                        for(int i=0; i<d->mb_width; i++){
                            H264Mb *m = &mbs[i + j*d->mb_width];
                            h264_decode_mb_without_mc_idct(d, d->mrs, s2, m);
                        }
                        draw_edges(d, s2, j);
                    }
                }else{
                    for(int j=0; j<d->mb_height; j++){
                        init_mbrec_context(d, d->mrs, s2, j);
                        for(int i=0; i<d->mb_width; i++){
                            H264Mb *m = &mbs[i + j*d->mb_width];
                            h264_decode_mb_without_mc(d, d->mrs, s2, m);//FIXME
                        }
                        draw_edges(d, s2, j);
                    }
                }
            }
            if(h->profile==3){
               clock_gettime(CLOCK_REALTIME, &timestamps[stamp_idx++]);
                mc_total_time[INTRADF] += getmillisecond(timestamps[stamp_idx-1],timestamps[stamp_idx-2]);
            }
        }

        if((gpumode>=CPU_BASELINE_IDCT)&&(gpumode!=CPU_BASELINE_MC)&&(gpumode!=GPU_MC)&&(gpumode!=GPU_MC_NOVERLAP)){
            compact_mem[IDCT_Mem_queue.fo].compact_count =0;
            compact_mem[IDCT_Mem_queue.fo].idct8_compact_count =0;
            //Release one IDCT Compressed Buffer
            pthread_mutex_lock(&IDCT_Mem_queue.lock);
            if(IDCT_Mem_queue.cnt<=0)
                printf("there is error, why you use a buffer already but count is zero\n");
            if(IDCT_Mem_queue.cnt==IDCT_Mem_queue.size)
                pthread_cond_signal(&IDCT_Mem_queue.cond);      
            IDCT_Mem_queue.cnt--;
            IDCT_Mem_queue.fo++;
            IDCT_Mem_queue.fo %= IDCT_Mem_queue.size;
            pthread_mutex_unlock(&IDCT_Mem_queue.lock);
        }
    }
    for (int i=0; i<s2->release_cnt; i++){
        for(int j=0; j<h->max_dpb_cnt; j++){
            if(h->dpb[j].cpn== s2->release_ref_cpn[i]){
                release_dpb_entry(h, &h->dpb[j], 2);
                break;
            }
        }
    }
    s2->release_cnt=0;

    if((gpumode==GPU_TOTAL||gpumode==GPU_MC||gpumode==GPU_MC_NOVERLAP)&&sbe->dp->reference>=2){
        int cpn = sbe->dp->cpn;
        int ref_slot = is_transferred(mc_transferred_reference,cpn);
        if(ref_slot == MAX_REFERENCE_COUNT){
            mc_cpu_luma_input = sbe->dp->base[0];
            int offset = mc_references_FIFO_top;
        #if !ARCH_NVIDIA_CUDA
            cl_bool blocking = gpumode==GPU_MC_NOVERLAP?CL_TRUE:CL_FALSE;
            clEnqueueWriteBuffer(mc_commandQueue[1],mc_gpu_luma_references.mem_obj,blocking,offset*mc_opencl_reference_size,mc_opencl_reference_size,mc_cpu_luma_input,0, NULL, &mc_copy_event_luma);
            for(int c=0;c<2;c++){
                    mc_cpu_chroma_input[c] = sbe->dp->base[c+1];//base 0 is luma
                    clEnqueueWriteBuffer(mc_commandQueue[0],mc_gpu_chroma_references[c].mem_obj,blocking,offset*(mc_opencl_reference_size>>2),(mc_opencl_reference_size>>2),mc_cpu_chroma_input[c],0, NULL,&mc_copy_event_chroma[c]);
            }
        #else
	    cudaMemcpyAsync(mc_gpu_cuda_luma_references+offset*mc_cuda_reference_size, mc_cpu_luma_input,mc_cuda_reference_size,cudaMemcpyHostToDevice,mc_streams[1]); 
            for(int c=0;c<2;c++){
                    mc_cpu_chroma_input[c] = sbe->dp->base[c+1];//base 0 is luma
          	     cudaMemcpyAsync( mc_gpu_cuda_chroma_references[c]+offset*(mc_cuda_reference_size>>2), mc_cpu_chroma_input[c],(mc_cuda_reference_size>>2),cudaMemcpyHostToDevice,mc_streams[0]); 
            }
        #endif
            mc_transferred_reference[mc_references_FIFO_top++]=cpn;
            mc_references_FIFO_top = mc_references_FIFO_top==MAX_REFERENCE_COUNT?0:mc_references_FIFO_top;
        }        
    }
    if(h->profile==3){
        clock_gettime(CLOCK_REALTIME,&timestamps[stamp_idx++]);
        mc_total_time[MC_TAIL]+=getmillisecond(timestamps[stamp_idx-1],timestamps[stamp_idx-2]);
    }
    curslice++;
}

static void *rec_thread(void *arg){
    H264Context *h =  (H264Context*) arg;
    MBRecContext *mrc=NULL;
    SliceBufferEntry *sbe=NULL;
    H264Slice *s;
    if (!h->no_mbd){
        mrc = get_mbrec_context(h);
        mrc ->top_next = mrc->top = av_malloc(h->mb_width *sizeof(TopBorder));
    }

    for(;;){
        pop_next_mbd(h, &sbe);
        if (sbe){
            if (sbe->state<0){
                push_sbe(&h->sb_q[OUTPUT], sbe, 1);
                break;
            }
            s = &sbe->slice;
            H264Mb *mbs=sbe->mbs;
            if(h->profile==3) 
                start_timer(h,REC);
            decode_slice_gpu(h, mrc, s, mbs, sbe);
            if(h->profile==3) 
                stop_timer(h,REC);
            push_sbe(&h->sb_q[OUTPUT], sbe, 1);            
        }
    }

    if (!h->no_mbd){
        free(mrc ->top_next);
        free_mbrec_context(mrc);
    }

    //make sure threads quit in order of rle assignment
    pthread_exit(NULL);
    return NULL;    
}

static void *ed_thread(void *arg){
    H264Context *h =  (H264Context*) arg;
    EntropyContext *ec=NULL;
    SliceBufferEntry *sbe=NULL;    
    ec = get_entropy_context(h);
    for(;;){
        pop_next_ed(h, &sbe);
        if (sbe){
            if(h->profile==3)
                start_timer(h, ED);
            if (sbe->state<0){
                //notify mbdec thread
                push_sbe(&h->sb_q[MBDEC], sbe, 0);              
                notify_one_worker(h,1);
                break;
            }
            if (!sbe->initialized){
                init_sb_entry(h, sbe);
            }
            //wait until there is another IDCT Compressed Buffer available 
            decode_slice_entropy(ec, sbe);
            if ((gpumode>=CPU_BASELINE_IDCT)&&(gpumode!=CPU_BASELINE_MC)&&(gpumode!=GPU_MC)&&(gpumode!=GPU_MC_NOVERLAP)){
                struct timespec timestamps[2];
                if(h->profile==3){
                    clock_gettime(CLOCK_REALTIME, &timestamps[0]);
                }
                if((gpumode==GPU_IDCT)||(gpumode==GPU_TOTAL))
                    myGPUEnv[ARCH_NVIDIA_CUDA].gpu_idct(h);
                if((gpumode==CPU_BASELINE_IDCT)||(gpumode==CPU_BASELINE_TOTAL))
                    cpu_slice_idct(h);
                if(h->profile==3){
                    clock_gettime(CLOCK_REALTIME, &timestamps[1]);
                    idct_gpu_time += (double)(1.e3*(timestamps[1].tv_sec-timestamps[0].tv_sec)+1.e-6*(timestamps[1].tv_nsec-timestamps[0].tv_nsec));
                }
                pthread_mutex_lock(&IDCT_Mem_queue.lock);
                while(IDCT_Mem_queue.cnt>=IDCT_Mem_queue.size)
                    pthread_cond_wait(&IDCT_Mem_queue.cond, &IDCT_Mem_queue.lock);
                IDCT_Mem_queue.cnt++;
                IDCT_Mem_queue.fi++; 
                IDCT_Mem_queue.fi%=IDCT_Mem_queue.size;
                pthread_mutex_unlock(&IDCT_Mem_queue.lock);
            }
            if (h->no_mbd){
                release_sb_entry(h, sbe);
                sbe=NULL;
            } else {
                push_sbe(&h->sb_q[MBDEC], sbe, 0);
                notify_one_worker(h,1);
            }
            if(h->profile==3)
                stop_timer(h,ED);
        }
    }        
    free_entropy_context(ec);
    pthread_exit(NULL);
    return NULL;
}

static void *output_thread(void *arg){
    H264Context *h = (H264Context *) arg;

    OutputContext *oc = get_output_context( h );

    SliceBufferEntry *sbe = NULL;
    H264Slice *s=NULL;
    for(;;) {
        DecodedPicture *out, *dp;
        sbe = pop_sbe(&h->sb_q[OUTPUT], 1);
        if (sbe->state <0)
            break;
        
        s = &sbe->slice;
        for (int i=0; i<s->release_cnt; i++){
            for(int j=0; j<h->max_dpb_cnt; j++){
                if(h->dpb[j].cpn== s->release_ref_cpn[i]){
                    release_dpb_entry(h, &h->dpb[j], 2);
                    break;
                }
            }
        }
        
        dp=s->curr_pic;
        release_sb_entry(h, sbe);
        out =output_frame(h, oc, dp, h->ofile, h->frame_width, h->frame_height);
        if (out){
            release_dpb_entry(h, out, 1);
        }
        print_report(oc->frame_number, oc->video_size, 0, h->verbose);

    }
    /* at the end of stream, we must flush the decoder buffers */
    while (output_frame(h, oc, NULL, h->ofile, h->frame_width, h->frame_height));
    print_report(oc->frame_number, oc->video_size, 1, h->verbose);

    free_output_context(oc);
    pthread_exit(NULL);
    return NULL;
}

void *parse_thread_opencl(void *arg){
    H264Context *h = (H264Context *) arg;
    ParserContext *pc = get_parse_context(h->ifile);
    NalContext *nc = get_nal_context(h->width, h->height);
    H264Slice *s;
    SliceBufferEntry *sbe = NULL;
    while(!pc->final_frame && frames++ <h->num_frames && !h->quit){
        sbe = get_sb_entry(h);
        if (h->profile==3) start_timer(h, FRONT);
        av_read_frame_internal(pc, &sbe->gb);
        s = &sbe->slice;
        decode_nal_units(nc, s, &sbe->gb);
        if (h->profile==3) stop_timer(h, FRONT);
        push_sbe(&h->sb_q[ENTROPY], sbe, 0);
        notify_one_worker(h,0);
    }


    sbe = get_sb_entry(h);
    sbe->state=-1;
    sbe->slice.coded_pic_num=nc->coded_pic_num;
    push_sbe(&h->sb_q[ENTROPY], sbe, 0);    
    notify_one_worker(h,0);

    free_nal_context(nc);
    free_parse_context(pc);

    pthread_exit(NULL);
    return NULL;
}

int h264_decode_gpu(H264Context *h) {
//     put the simplified version of h264_decode_pthread    
    pthread_t parse_thr,entropy_thr, rec_thr ,output_thr;
    struct timespec gpuEnv[2];
    double env_time;
    if(gpumode>CPU_BASELINE_TOTAL){
        clock_gettime(CLOCK_REALTIME, &gpuEnv[0]);
        myGPUEnv[ARCH_NVIDIA_CUDA].gpu_construct_environment(h);
    }
    if(gpumode==CPU_BASELINE_IDCT||gpumode==CPU_BASELINE_TOTAL)
        idct_cpu_mem_construct(h);
    if(gpumode>CPU_BASELINE_TOTAL){
        clock_gettime(CLOCK_REALTIME, &gpuEnv[1]);
        env_time = getmillisecond(gpuEnv[1],gpuEnv[0]);
        printf("gpu Env %.3f\n", env_time);
    }
    av_start_timer();
    pthread_create(&parse_thr, NULL, parse_thread_opencl, h);
    
    if(!h->no_mbd){
        pthread_create(&output_thr,NULL,output_thread,h);
    }
       
    pthread_create(&entropy_thr, NULL, ed_thread, h);
    pthread_create(&rec_thr, NULL, rec_thread, h);
    pthread_join(entropy_thr,NULL);
    pthread_join(rec_thr,NULL);
    pthread_join(parse_thr,NULL);
    if(!h->no_mbd){
        pthread_join(output_thr,NULL);
    }
    if(gpumode==CPU_BASELINE_IDCT||gpumode==CPU_BASELINE_TOTAL)
        idct_cpu_mem_destruct();
    if(gpumode>CPU_BASELINE_TOTAL)
        myGPUEnv[ARCH_NVIDIA_CUDA].gpu_destroy_environment();
    return 0;
}