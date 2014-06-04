#ifndef H264_SEI_H
#define H264_SEI_H

int ff_h264_decode_sei(NalContext *n, GetBitContext *gb);
void ff_h264_reset_sei(NalContext *n);

#endif
