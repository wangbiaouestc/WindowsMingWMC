/*
* H264 decoder main
*/

#include "config.h"
#include "libavcodec/h264.h"
#include "libavcodec/opencl/h264_mc_opencl.h"
#include "libavcodec/opencl/h264_idct_opencl.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include <assert.h>

static const char program_name[] = "h264dec";
static const int program_birth_year = 2010;

static const char *file_name;
static int ifile, ofile;
static int no_arch =0;
static int parallel = 1;
static int frame_width  = 0;
static int frame_height = 0;

static void av_exit(int ret)
{
    //do some free calls
#undef exit
    exit(ret);
}

static void opt_input_file(const char *filename)
{
    /* open the input file */
    ifile = open(filename, O_RDONLY, 0666);
    if (ifile < 0){
        fprintf(stderr, "Failed to open %s\n", filename);
        av_exit(-1);
    }

    //parse first frame to get resolution (other information available but not used)
    H264Slice slice;
    PictureInfo pi;
    GetBitContext gb = {0,};
    ParserContext *pc;
    NalContext *nc;

    pc = get_parse_context(ifile);
    nc = get_nal_context(0, 0);

    memset(&slice, 0, sizeof(H264Slice));
    slice.current_picture_info=&pi;

    av_read_frame_internal(pc, &gb);
    decode_nal_units(nc, &slice, &gb);

    frame_width = nc->width;
    frame_height= nc->height;

    //clean up
    av_freep(&gb.raw);
    if (gb.rbsp)
        av_freep(&gb.rbsp);
    free_parse_context(pc);
    free_nal_context(nc);

    //rewind file
    int offset;
    if ( (offset=lseek(ifile, 0, SEEK_SET)) ){
        fprintf(stderr, "Rewind input file %s failed at offset %d\n", filename, offset);
    }

}

static void opt_output_file(const char *filename)
{
    if (filename){
        if (!strcmp(filename, "-"))
            filename = "pipe:";

        ofile = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    }else{
        ofile =0;
    }
}

static void show_usage(void)
{
    printf("usage: ffmpeg [options] -i infile }...\n");
    printf("\n");
}

static struct option long_options[] = {
    {"static-sched", 0, 0, 0},
    {"static-mbd", 0, 0, 0},
    {"numamap", 0, 0, 0},
    {"no-mbd", 0, 0, 0},
    {"static-3d", 0, 0, 0},
    {"slice-bufs", 1, 0, 0},
    {"smt", 0, 0, 0},
    {"noarch", 0, 0, 'a'},
    {"display", 0, 0, 'd'},
    {"fullscreen", 0, 0, 'f'},
    {"numframes", 1, 0, 'n'},
    {"use-ppe-ed", 1, 0, 'p'},
    {"sequential", 0, 0, 's'},
    {"threads", 1, 0, 't'},
    {"verbose", 1, 0, 'v'},
    {"wave-order", 1, 0, 'w'},
    {"smb-size", 1, 0, 'z'},
    {"pipe-bufs", 1, 0, 'e'},
    {"gpu-mode",1,0,0},
    {"opencl-online",0,0,0},
    {"opencl-partition",1,0,0},
    {0, 0, 0, 0}
};

static h264_options cli_opts;
static void parse_cmd(int argc, char **argv)
{
    int c;
    int digit_optind = 0;
    int option_index = 0;
    char ofile_name[1024];
    extern char *optarg;
    extern int optind, optopt;

    cli_opts.statsched =0;
    cli_opts.numamap =0;
    cli_opts.statmbd =0;
    cli_opts.no_mbd= 0;
    cli_opts.numframes = INT_MAX;
    cli_opts.display=0;
    cli_opts.fullscreen=0;
    cli_opts.verbose=0;
    cli_opts.ppe_ed=0;
    cli_opts.profile=0;
    cli_opts.threads = 1;
    cli_opts.smb_size[0] = cli_opts.smb_size[1] = 1;
    cli_opts.wave_order=0;
    cli_opts.static_3d=0;
    cli_opts.pipe_bufs=8;
    cli_opts.slice_bufs=1;
    cli_opts.smt= 0;
    while ((c = getopt_long(argc, argv, "ade:fi:n:o:p:st:vwz:", long_options, &option_index)) != -1 ){
        int this_option_optind = optind ? optind : 1;

        switch (c){
            case 0:
                if (option_index==0){
                    cli_opts.statsched=1;
                }else if (option_index==1){
                    cli_opts.statmbd= 1;
                }else if (option_index==2){
                    cli_opts.numamap= 1;
                }else if (option_index==3){
                    cli_opts.no_mbd= 1;
                }else if (option_index==4){
                    cli_opts.static_3d= 1;
                }else if (option_index==5){
                    cli_opts.slice_bufs= (unsigned) atoi(optarg);
                }else if (option_index==6){
                    cli_opts.smt= 1;
                }else if (option_index==18){
                    gpumode = (unsigned) atoi(optarg);					
                }
                else if (option_index==19){
                    openclonline=1;
                }
                break;
            case '0':
            case '1':
            case '2':
                if (digit_optind != 0 && digit_optind != this_option_optind)
                    printf("digits occur in two different argv-elements.\n");
                digit_optind = this_option_optind;
                printf("option %c\n", c);
                break;
            case 'a':
                no_arch=1;
                break;
            case 'd':
                cli_opts.display=1;
                break;
            case 'f':
                cli_opts.fullscreen=1;
                break;
            case 'i':
                file_name = (const char *)optarg;
                opt_input_file(file_name);
                break;
            case 'n':
                cli_opts.numframes = (unsigned) atoi(optarg);
                break;
            case 'o':
                strcpy(ofile_name, optarg);
                opt_output_file(ofile_name);
                break;
            case 'p':
                cli_opts.profile = (unsigned) atoi(optarg);
                break;
            case 's':
                cli_opts.threads = 0;
                parallel = 0;
                break;
            case 't':
                cli_opts.threads = atoi(optarg);
                if (cli_opts.threads<=0){
                    fprintf(stderr, "Option -%c requires thread numbers > 0\n", c);
                    av_exit(-1);
                }
                break;
            case 'v':
                cli_opts.verbose = 1;
                break;
            case 'w':
                cli_opts.wave_order = 1;
                break;
            case 'z': // only useful in ompss
                if (argc < optind +1){
                    fprintf(stderr, "Option -%c (--smb-size) requires 2 arguments\n", c);
                    av_exit(-1);
                }
                optind--;
                for (int i=0; i<2; i++){
                    cli_opts.smb_size[i] = atoi(argv[optind++]);
                    if (!(cli_opts.smb_size > 0)){
                        fprintf(stderr, "Option -%c (--smb-size) requires dimensions > 0\n", c);
                        av_exit(-1);
                    }
                }
                break;
            case 'e':
                cli_opts.pipe_bufs = atoi(optarg);
                break;
            case ':':
                fprintf(stderr, "Option -%c requires an operand\n", optopt);
                av_exit(-1);
                break;
            case '?':
                fprintf(stderr, "Unrecognized option: -%c\n", optopt);
                av_exit(-1);
                break;
        }
    }

}

int main(int argc, char **argv)
{
	struct timespec end ,start;
	clock_gettime(CLOCK_REALTIME, &start);
    /* parse options */
    parse_cmd(argc, argv);

    if(!ifile ) {
        show_usage();
        av_exit(1);
    }

    H264Context *h = get_h264dec_context(file_name, ifile, ofile, frame_width, frame_height, &cli_opts);
#if OMPSS
    if (h264_decode_ompss( h ) < 0)
        av_exit(-1);
#else
    if (parallel){
        if (ARCH_CELL && !no_arch){
            if (h264_decode_cell( h ) < 0)
                av_exit(-1);
        }else if(gpumode){
            if (h264_decode_gpu ( h ) < 0)
		av_exit(-1);	    
	}
	else{
            if (h264_decode_pthread( h ) < 0)
                av_exit(1);
        }
    }else{
        if (ARCH_CELL && !no_arch){
            if (h264_decode_cell_seq( h ) < 0)
                av_exit(1);
        }else{
            if (h264_decode_seq( h ) < 0)
                av_exit(1);
        }
    }
#endif
    double time_ED = h->total_time[ED];
    double time_REC= h->total_time[REC];
    free_h264dec_context(h);
    close(ifile);
    close(ofile);
    clock_gettime(CLOCK_REALTIME, &end);
    double total_time = (double) (1.e3*(end.tv_sec - start.tv_sec) + 1.e-6*(end.tv_nsec - start.tv_nsec));
	if(h->profile==3){
		//Others includes parse time and other sync.
		switch(gpumode){
             case CPU_BASELINE:
                printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT 0.0 MC 0.0 MBREC %.3f \n", curslice,(total_time-(time_ED+time_REC)), time_ED,time_REC);
                break;
			case CPU_BASELINE_IDCT:
				printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT %.3f MC 0.0 MBREC %.3f \n", curslice,(total_time-(time_ED+time_REC)), (time_ED-idct_gpu_time), idct_gpu_time, time_REC);
				break;
			case CPU_BASELINE_MC:
				printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT 0.0  MC %.3f MBREC %.3f \n", curslice,(total_time-(time_ED+time_REC)), time_ED, mc_total_time[MC_TIME], (time_REC-mc_total_time[MC_TIME]));
				break;
			case CPU_BASELINE_TOTAL:
				printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT %.3f MC %.3f MBREC %.3f \n", curslice,(total_time-(time_ED+time_REC)),(time_ED-idct_gpu_time), idct_gpu_time, mc_total_time[MC_TIME], (time_REC-mc_total_time[MC_TIME]));
				break;
			case GPU_IDCT:
				printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT %.3f MC 0.0 MBREC %.3f K4x4 %.3f K8x8 %.3f\n",curslice, (total_time-(time_ED+time_REC)), (time_ED-idct_gpu_time),
					    idct_gpu_time, time_REC, idct_gpu_time_fine[IDCT_KERNEL_4x4], idct_gpu_time_fine[IDCT_KERNEL_8x8]);
				break;
			case GPU_MC:
				printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT 0.0 MC %.3f MBREC %.3f COLLECTION %.3f SIDEINFO_COPY %.3f KERNEL C %.3f L %.3f COPY C %.3f L %.3f\n",curslice,
                       (total_time-(time_ED+time_REC)), (time_ED),time_REC-mc_total_time[INTRADF], mc_total_time[INTRADF],
                       mc_total_time[MC_OVERHEAD],mc_gpu_time[MC_KERNEL_COPY_DATA_STRUCTURE],
	        			  mc_gpu_time[MC_KERNEL_CHROMA], mc_gpu_time[MC_KERNEL_LUMA],mc_gpu_time[MC_KERNEL_COPY_CHROMA], mc_gpu_time[MC_KERNEL_COPY_LUMA]);
				break;
			case GPU_TOTAL:
	        	printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT %.3f MC %.3f MBREC %.3f COLLECTION %.3f SIDEINFO_COPY %.3f KERNEL C %.3f L %.3f COPY C %.3f L %.3f\n",curslice,
	        			(total_time-(time_ED+time_REC)), (time_ED-idct_gpu_time), idct_gpu_time, time_REC-mc_total_time[INTRADF], mc_total_time[INTRADF],
	        			mc_total_time[MC_OVERHEAD],mc_gpu_time[MC_KERNEL_COPY_DATA_STRUCTURE],
	        			mc_gpu_time[MC_KERNEL_CHROMA], mc_gpu_time[MC_KERNEL_LUMA],mc_gpu_time[MC_KERNEL_COPY_CHROMA], mc_gpu_time[MC_KERNEL_COPY_LUMA]);
	        	break;
            case GPU_MC_NOVERLAP:
            printf("Profile Frame %d Others %.3f ENTROPY %.3f IDCT 0.0 MC %.3f MBREC %.3f COLLECTION %.3f SIDEINFO_COPY %.3f KERNEL C %.3f L %.3f COPY C %.3f L %.3f\n",curslice,
                    (total_time-(time_ED+time_REC)), (time_ED),time_REC-mc_total_time[INTRADF], mc_total_time[INTRADF],
                    mc_total_time[MC_OVERHEAD],mc_gpu_time[MC_KERNEL_COPY_DATA_STRUCTURE],
                        mc_gpu_time[MC_KERNEL_CHROMA], mc_gpu_time[MC_KERNEL_LUMA],mc_gpu_time[MC_KERNEL_COPY_CHROMA], mc_gpu_time[MC_KERNEL_COPY_LUMA]);
            break;
		}
	}
    return 0;
}