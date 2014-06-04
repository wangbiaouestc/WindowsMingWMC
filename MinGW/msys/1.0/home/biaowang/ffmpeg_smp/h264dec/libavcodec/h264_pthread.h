#ifndef H264_PTHREAD_H
#define H264_PTHREAD_H

#include "h264_types.h"

int decode_B_slice_entropy(EntropyContext *ec, EDSlice *s, EDThreadContext *eb, EDThreadContext *eb_prev);
int decode_slice_entropy(EntropyContext *hc, EDSlice *s);

void *read_thread(void *arg);
void *parsenal_thread(void *arg);
void *mbrec_thread(void *arg);
void *write_thread(void *arg);

#endif
