#ifndef H264_MC_H
#define H264_MC_H

#include "dsputil.h"
#include "h264_types.h"

void hl_motion(MBRecContext *d, MBRecState *mrs, H264Slice *s, H264Mb *m, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
					qpel_mc_func (*qpix_put)[16], h264_chroma_mc_func (*chroma_put),
					qpel_mc_func (*qpix_avg)[16], h264_chroma_mc_func (*chroma_avg),
					h264_weight_func *weight_op, h264_biweight_func *weight_avg);

#endif
