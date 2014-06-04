#ifndef H264_DIRECT_H
#define H264_DIRECT_H

#include "h264_types_spu.h"

void ff_h264_pred_direct_motion(H264Cabac_spu *hc, EDSlice_spu *s, int *mb_type);

#endif
