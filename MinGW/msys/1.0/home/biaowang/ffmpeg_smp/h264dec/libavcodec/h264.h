/*
* H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
* Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

/**
* @file
* H.264 / AVC / MPEG4 part10 codec.
* @author Michael Niedermayer <michaelni@gmx.at>
*/

#ifndef H264_H
#define H264_H

#include "h264_entropy.h"
#include "h264_data.h"
#include "h264_mc.h"
#include "h264_misc.h"
#include "h264_dsp.h"
#include "h264_pred.h"
#include "h264_parser.h"
#include "h264_nal.h"
#include "h264_rec.h"
#include "h264_deblock.h"
#include "h264_types.h"

typedef struct h264_options{
    int statsched;
    int statmbd;
    int numamap;
    int no_mbd;
    int numframes;
    int display;
    int fullscreen;
    int verbose;
    int ppe_ed;         // only useful for Cell
    int profile;
    int threads;
    int smb_size[2];    // only useful for OmpSs
    int wave_order;
    int static_3d;
    int pipe_bufs;
    int slice_bufs;
    int smt;
}h264_options;

int h264_decode_cell(H264Context *h);
int h264_decode_cell_seq(H264Context *h);

int h264_decode_ompss(H264Context *h);

int h264_decode_pthread(H264Context *h);
int h264_decode_seq(H264Context *h);
int h264_decode_gpu(H264Context *h);
extern int gpumode;
H264Context *get_h264dec_context(const char *file_name, int ifile, int ofile, int frame_width, int frame_height, h264_options *opts);
void free_h264dec_context(H264Context *h);


#endif /* AVCODEC_H264_H */
