#ifndef DSPUTIL_CELL_H
#define DSPUTIL_CELL_H

#include "types_spu.h"

typedef struct DSPContext_spu {
	
	void (*h264_v_loop_filter_luma)(uint8_t *pix/*align 16*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_luma)(uint8_t *pix/*align 4 */, int stride, int alpha, int beta, int8_t *tc0);
    /* v/h_loop_filter_luma_intra: align 16 */
    void (*h264_v_loop_filter_luma_intra)(uint8_t *pix, int stride, int alpha, int beta);
    void (*h264_h_loop_filter_luma_intra)(uint8_t *pix, int stride, int alpha, int beta);
    void (*h264_v_loop_filter_chroma)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_chroma)(uint8_t *pix/*align 4*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_v_loop_filter_chroma_intra)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta);
    void (*h264_h_loop_filter_chroma_intra)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta);
	
	qpel_mc_func put_h264_qpel_pixels_tab[3][16];
	qpel_mc_func avg_h264_qpel_pixels_tab[3][16];

	h264_chroma_mc_func put_h264_chroma_pixels_tab[3];
	h264_chroma_mc_func avg_h264_chroma_pixels_tab[3];

	h264_idct_func h264_idct_add[2];

	h264_weight_func weight_h264_pixels_tab[10];
	h264_biweight_func biweight_h264_pixels_tab[10];

} DSPContext_spu;


void dsputil_h264_init_cell(DSPContext_spu* c);
 
#endif
