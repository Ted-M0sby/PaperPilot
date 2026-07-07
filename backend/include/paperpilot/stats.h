#ifndef PAPERPILOT_STATS_H
#define PAPERPILOT_STATS_H

#include "paperpilot/common.h"

typedef struct PP_IndexStats {
  int chunk_count;
  int token_count;
  float avg_tokens_per_chunk;
} PP_IndexStats;

PP_Status pp_index_stats_collect(const char* index_path, PP_IndexStats* out_stats);
PP_Status pp_index_stats_print(const char* index_path);

#endif
