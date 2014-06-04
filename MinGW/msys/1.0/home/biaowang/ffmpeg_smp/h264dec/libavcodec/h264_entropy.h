#ifndef H264_CABAC_H
#define H264_CABAC_H

#include "h264_types.h"
#include "cabac.h"

/**
 * decodes a CABAC coded macroblock
 * @return 0 if OK, AC_ERROR / DC_ERROR / MV_ERROR if an error is noticed
 */

int ff_h264_decode_mb_cabac(EntropyContext *ec, H264Slice *s, CABACContext *c);
void ff_h264_init_cabac_states(EntropyContext *ec, H264Slice *s, CABACContext *c);

int init_entropy_buf(EntropyContext *ec, H264Slice *s, int line);
EntropyContext * get_entropy_context(H264Context *h);
void init_dequant_tables(H264Slice *s, EntropyContext *ec);
void free_entropy_context(EntropyContext *ec);

#endif
