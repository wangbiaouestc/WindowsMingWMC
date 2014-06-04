#ifndef H264_PS_H
#define H264_PS_H

#include "h264_types.h"

int ff_h264_decode_seq_parameter_set(NalContext *n, GetBitContext *gb);
int ff_h264_decode_picture_parameter_set(NalContext *n, GetBitContext *gb, int bit_length);

#endif
