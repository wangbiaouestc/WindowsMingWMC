#ifndef H264_CABAC_H
#define H264_CABAC_H

#define CELL_SPE
#include "libavcodec/avcodec.h"
#include "h264_types_spu.h"
#include "cabac_spu.h"


/**
 * decodes a CABAC coded macroblock
 * @return 0 if OK, AC_ERROR / DC_ERROR / MV_ERROR if an error is noticed
 */
int ff_h264_decode_mb_cabac(H264Cabac_spu *hc, EDSlice_spu *s, CABACContext *c);
void ff_h264_init_cabac_states(EDSlice_spu *s, CABACContext *c);

#endif
