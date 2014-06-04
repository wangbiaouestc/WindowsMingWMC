#ifndef H264_PARSER_H
#define H264_PARSER_H

#include "h264_types.h"

void av_read_frame_internal(ParserContext *pc, GetBitContext *gb);
ParserContext *get_parse_context(int ifile);
void free_parse_context(ParserContext *pc);

#endif
