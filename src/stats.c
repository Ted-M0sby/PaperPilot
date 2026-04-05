#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "paperpilot/logging.h"
#include "paperpilot/stats.h"

static int pp_is_word_char(char c) {
  return isalnum((unsigned char)c) || c == '_';
}

static int pp_count_tokens(const char* text) {
  int count = 0;
  int in_token = 0;
  size_t i;

  if (!text) {
    return 0;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    if (pp_is_word_char(text[i])) {
      if (!in_token) {
        ++count;
        in_token = 1;
      }
    } else {
      in_token = 0;
    }
  }

  return count;
}

PP_Status pp_index_stats_collect(const char* index_path, PP_IndexStats* out_stats) {
  FILE* fp;
  char line[1400];

  if (!index_path || !out_stats) {
    return PP_ERR_INVALID_ARG;
  }

  out_stats->chunk_count = 0;
  out_stats->token_count = 0;
  out_stats->avg_tokens_per_chunk = 0.0f;

  fp = fopen(index_path, "r");
  if (!fp) {
    return PP_ERR_IO;
  }

  while (fgets(line, sizeof(line), fp)) {
    char* tab = strchr(line, '\t');
    if (!tab) {
      continue;
    }
    ++tab;

    out_stats->chunk_count += 1;
    out_stats->token_count += pp_count_tokens(tab);
  }

  fclose(fp);

  if (out_stats->chunk_count > 0) {
    out_stats->avg_tokens_per_chunk =
        (float)out_stats->token_count / (float)out_stats->chunk_count;
  }

  return PP_OK;
}

PP_Status pp_index_stats_print(const char* index_path) {
  PP_IndexStats stats;
  PP_Status status = pp_index_stats_collect(index_path, &stats);

  if (status != PP_OK) {
    pp_log_error("Cannot collect index stats. Run import first");
    return status;
  }

  printf("Index stats:\n");
  printf("- chunks: %d\n", stats.chunk_count);
  printf("- tokens: %d\n", stats.token_count);
  printf("- avg_tokens_per_chunk: %.2f\n", stats.avg_tokens_per_chunk);

  return PP_OK;
}
