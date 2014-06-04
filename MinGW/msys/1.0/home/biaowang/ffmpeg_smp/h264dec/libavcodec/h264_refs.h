#ifndef H264_REFS_H
#define H264_REFS_H

#include "avcodec.h"
#include "h264_types.h"

int ff_h264_fill_default_ref_list(NalContext *n, H264Slice *s);
int ff_h264_decode_ref_pic_list_reordering(NalContext *n, H264Slice *s, GetBitContext *gb);
void ff_h264_remove_all_refs(NalContext *n, H264Slice *s);
int ff_h264_ref_pic_marking(NalContext *n, H264Slice *s, GetBitContext *gb);
void ff_h264_direct_ref_list_init(H264Slice *s);
void ff_h264_direct_dist_scale_factor(H264Slice *s);

#endif
