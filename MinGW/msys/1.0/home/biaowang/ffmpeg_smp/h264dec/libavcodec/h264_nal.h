#ifndef H264_NAL_H
#define H264_NAL_H

#include "avcodec.h"
#include "h264_types.h"

int decode_nal_units(NalContext *n, H264Slice *s, GetBitContext *gb);
NalContext *get_nal_context(int width, int height);
void free_nal_context(NalContext *nc);

#endif
