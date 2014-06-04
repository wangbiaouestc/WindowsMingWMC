/*
 * Copyright (c) 2006 Guillaume Poirier <gpoirier@mplayerhq.hu>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef TYPES_SPU_H
#define TYPES_SPU_H

/***********************************************************************
 * Scalar types
 **********************************************************************/
    typedef signed char  int8_t;
    typedef signed short int16_t;
    typedef signed int   int32_t;
    typedef unsigned char  uint8_t;
    typedef unsigned short uint16_t;
    typedef unsigned int   uint32_t;
    typedef unsigned long long uint64_t;

//     typedef short DCTELEM;		// transform coeficients of dct

/***********************************************************************
 * Vector types
 **********************************************************************/
    typedef	vector	signed int	vsint32_t;
    typedef	vector	unsigned int	vuint32_t;
    typedef	vector	signed short	vsint16_t;
    typedef	vector	unsigned short	vuint16_t;
    typedef	vector	signed char	vsint8_t;
    typedef	vector	unsigned char	vuint8_t;

/***********************************************************************
 * Functions
 **********************************************************************/
    typedef void (*qpel_mc_func)(uint8_t *dst, uint8_t *src, int dst_stride, int h);
    typedef void (*h264_chroma_mc_func)(uint8_t *dst, uint8_t *src, int dst_stride, int h, int x, int y);
    typedef void (*h264_idct_func)(uint8_t *dst, short *block, int stride);
    typedef void (*h264_weight_func)(uint8_t *block, int stride, int log2_denom, int weight, int offset);
    typedef void (*h264_biweight_func)(uint8_t *dst, uint8_t *src, int dst_stride, int src_stride, int log2_denom, int weightd,
                  int weights, int offset);
    typedef void(* intra_pred4x4)(uint8_t *src, uint8_t *topright, int stride);
    typedef void(* intra_pred16x16)(uint8_t *src, int stride);
    typedef void(* intra_pred8x8)(uint8_t *src, int stride);
    typedef void(* intra_pred8x8l)(uint8_t *src, int topleft, int topright, int stride);


#define AVV(x...) {x}
	
	
#endif // AVCODEC_TYPES_SPU_H




