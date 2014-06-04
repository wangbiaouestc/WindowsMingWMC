#ifndef H264_INTRA_SPU_H
#define H264_INTRA_SPU_H

#define MAX_NEG_CROP       1024

// For Intra mode
#define MB_TYPE_INTRA4x4   0x0001
#define IS_INTRA(a)       ((a)&7)
#define IS_INTRA4x4(a)    ((a)&MB_TYPE_INTRA4x4)

#define CODEC_FLAG_GRAY   0x2000

#define VERT_PRED             0
#define HOR_PRED              1
#define DC_PRED               2
#define DIAG_DOWN_LEFT_PRED   3
#define DIAG_DOWN_RIGHT_PRED  4
#define VERT_RIGHT_PRED       5
#define HOR_DOWN_PRED         6
#define VERT_LEFT_PRED        7
#define HOR_UP_PRED           8

#define LEFT_DC_PRED          9
#define TOP_DC_PRED           10
#define DC_128_PRED           11


#define DC_PRED8x8            0
#define HOR_PRED8x8           1
#define VERT_PRED8x8          2
#define PLANE_PRED8x8         3

#define LEFT_DC_PRED8x8       4
#define TOP_DC_PRED8x8        5
#define DC_128_PRED8x8        6

typedef struct H264PredContext_spu{

  intra_pred4x4 pred4x4[9+3];
  intra_pred16x16 pred16x16[4+3];
  intra_pred8x8 pred8x8[4+3];
  intra_pred8x8l pred8x8l[9+3];

}H264PredContext_spu;

void init_pred_ptrs(H264PredContext_spu *i);

#endif
