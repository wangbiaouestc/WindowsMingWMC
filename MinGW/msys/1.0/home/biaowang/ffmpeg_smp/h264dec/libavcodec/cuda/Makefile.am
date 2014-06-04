CUDA_CFLAGS=-I/usr/include -I../../../build -I../../../h264dec -I../
.cu.o:
	nvcc -keep $(CUDA_CFLAGS) -gencode=arch=compute_20,code=sm_20 -o $@ -c $<
	
AM_CFLAGS = -std=gnu99
lib_LIBRARIES = libavcodec_cuda.a
libavcodec_cuda_a_SOURCES =  h264_cuda.c 	\
			h264_idct_cuda.c	\
			h264_mc_cuda.c
			
libavcodec_cuda_a_LIBADD = kernelIDCT.o kernelMC.o
libavcodec_cuda_a_CPPFLAGS = -I$(top_srcdir) -I/usr/local/cuda/include
