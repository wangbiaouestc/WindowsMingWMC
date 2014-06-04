#ifndef H264_REC_H
#define H264_REC_H

#include "h264_types.h"

MBRecContext *get_mbrec_context(H264Context *h);
void free_mbrec_context( MBRecContext *d);
void h264_decode_mb_internal(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m);

void init_mbrec_context(MBRecContext *mrc, MBRecState *mrs, H264Slice *s, int line);

#endif
