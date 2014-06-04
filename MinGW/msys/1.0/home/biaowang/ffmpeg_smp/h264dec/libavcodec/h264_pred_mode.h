#ifndef H264_DIRECT_H
#define H264_DIRECT_H

#include "h264_types.h"

void ff_h264_pred_direct_motion_rec(MBRecContext *mrc, MBRecState *mrs, H264Slice *s, H264Mb *m, int *mb_type);
int pred_motion_mb_rec(MBRecContext *mrc, MBRecState *mrs, H264Slice *s, H264Mb *m);


#endif
