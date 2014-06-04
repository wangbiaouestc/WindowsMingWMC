if HAVE_CELL
CELL_SUBDIR=cell
CELL_SOURCE= h264_cell.c
endif

if HAVE_OMPSS
OMPSS_SOURCE= h264_ompss.c
else
PTHREAD_SOURCE= h264_pthread.c
SEQUENTIAL_SOURCE= h264_seq.c
endif


OPENCL_SUBDIR = opencl

if HAVE_CUDA
CUDA_SUBDIR = cuda
else
CUDA_SUBDIR =
endif

AM_CFLAGS = -std=gnu99 -Wall -Werror
SUBDIRS = $(ARCH_SUBDIR) $(CELL_SUBDIR) $(OPENCL_SUBDIR) $(CUDA_SUBDIR)
lib_LIBRARIES = libavcodec.a

libavcodec_a_SOURCES =  dsputil.c			\
			simple_idct.c			\
			utils.c				\
			golomb.c			\
			h264_dsp.c			\
			h264_idct.c			\
			h264_pred.c			\
			h264_mc.c			\
			h264_deblock.c			\
			h264_pred_mode.c			\
			cabac.c				\
			h264_sei.c			\
			h264_ps.c			\
			h264_refs.c			\
			h264_entropy.c			\
			h264_parser.c			\
			h264_nal.c			\
			h264_rec.c			\
			h264_misc.c			\
			h264.c			\
			h264_gpu.c                      \
			$(SEQUENTIAL_SOURCE)		\
			$(PTHREAD_SOURCE)		\
			$(CELL_SOURCE)			\
			$(OMPSS_SOURCE)


libavcodec_a_CPPFLAGS = -I$(top_srcdir) -I/usr/local/cuda/include
