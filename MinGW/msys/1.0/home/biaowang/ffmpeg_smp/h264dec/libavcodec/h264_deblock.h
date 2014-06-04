#ifndef H264_LOOPFILTER_H
#define H264_LOOPFILTER_H

#include "h264_types.h"

void ff_h264_filter_mb(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m, uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr);

#endif
