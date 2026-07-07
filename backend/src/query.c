#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paperpilot/logging.h"
#include "paperpilot/query.h"

#define PP_MAX_DOCS 512
#define PP_MAX_LINE 1400
#define PP_MAX_TEXT 1200
#define PP_MAX_QUERY_TERMS 16
#define PP_TERM_SIZE 64
#define PP_MAX_TOP_RESULTS 10

typedef struct PP_DocRecord {
  int id;
  char text[PP_MAX_TEXT];
  int length_tokens;
} PP_DocRecord;

typedef struct PP_Result {
  int doc_idx;
  float score;
} PP_Result;

static void pp_to_lower_copy(char* dst, const char* src, size_t dst_size) {
  size_t i = 0;
  if (!dst || !src || dst_size == 0) {
    return;
  }

  while (src[i] != '\0' && i < dst_size - 1) {
    dst[i] = (char)tolower((unsigned char)src[i]);
    ++i;
  }
  dst[i] = '\0';
}

static int pp_is_word_char(char c) {
  return isalnum((unsigned char)c) || c == '_';
}

static int pp_count_tokens_in_text(const char* text) {
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

static int pp_count_term_occurrences(const char* text, const char* term) {
  int count = 0;
  size_t i = 0;
  size_t term_len;

  if (!text || !term) {
    return 0;
  }

  term_len = strlen(term);
  if (term_len == 0) {
    return 0;
  }

  while (text[i] != '\0') {
    if ((i == 0 || !pp_is_word_char(text[i - 1])) && strncmp(&text[i], term, term_len) == 0 &&
        !pp_is_word_char(text[i + term_len])) {
      ++count;
      i += term_len;
      continue;
    }
    ++i;
  }

  return count;
}

static int pp_parse_query_terms(const char* question, char terms[PP_MAX_QUERY_TERMS][PP_TERM_SIZE]) {
  char question_l[512];
  int term_count = 0;
  int i = 0;

  pp_to_lower_copy(question_l, question, sizeof(question_l));

  while (question_l[i] != '\0' && term_count < PP_MAX_QUERY_TERMS) {
    int k = 0;
    int exists = 0;
    int t;
    char term[PP_TERM_SIZE];

    while (question_l[i] != '\0' && !pp_is_word_char(question_l[i])) {
      ++i;
    }

    while (question_l[i] != '\0' && pp_is_word_char(question_l[i]) && k < PP_TERM_SIZE - 1) {
      term[k++] = question_l[i++];
    }
    term[k] = '\0';

    if (k == 0) {
      continue;
    }

    for (t = 0; t < term_count; ++t) {
      if (strcmp(terms[t], term) == 0) {
        exists = 1;
        break;
      }
    }
    if (!exists) {
      strncpy(terms[term_count], term, PP_TERM_SIZE - 1);
      terms[term_count][PP_TERM_SIZE - 1] = '\0';
      ++term_count;
    }
  }

  return term_count;
}

static float pp_bm25_score(const PP_DocRecord* doc, const PP_DocRecord* docs, int doc_count,
                           char terms[PP_MAX_QUERY_TERMS][PP_TERM_SIZE], int term_count,
                           float avg_len) {
  float score = 0.0f;
  int t;
  const float k1 = 1.2f;
  const float b = 0.75f;

  if (!doc || !docs || doc_count <= 0 || term_count <= 0 || avg_len <= 0.0f) {
    return 0.0f;
  }

  for (t = 0; t < term_count; ++t) {
    int tf = pp_count_term_occurrences(doc->text, terms[t]);
    int df = 0;
    int d;
    float idf;
    float denom;

    if (tf <= 0) {
      continue;
    }

    for (d = 0; d < doc_count; ++d) {
      if (pp_count_term_occurrences(docs[d].text, terms[t]) > 0) {
        ++df;
      }
    }

    idf = (float)(doc_count - df + 0.5f) / (float)(df + 0.5f);
    if (idf < 0.0f) {
      idf = 0.0f;
    }

    denom = tf + k1 * (1.0f - b + b * ((float)doc->length_tokens / avg_len));
    if (denom > 0.0f) {
      score += idf * ((tf * (k1 + 1.0f)) / denom);
    }
  }

  return score;
}

static void pp_insert_top_results(PP_Result* top, int top_k, int doc_idx, float score) {
  int i;
  for (i = 0; i < top_k; ++i) {
    if (score > top[i].score) {
      int j;
      for (j = top_k - 1; j > i; --j) {
        top[j] = top[j - 1];
      }
      top[i].doc_idx = doc_idx;
      top[i].score = score;
      break;
    }
  }
}

PP_Status pp_query_ask(const char* question, const char* index_path, int top_k) {
  FILE* fp;
  char line[PP_MAX_LINE];
  PP_DocRecord docs[PP_MAX_DOCS];
  int doc_count = 0;
  char terms[PP_MAX_QUERY_TERMS][PP_TERM_SIZE] = {{0}};
  int term_count;
  float total_len = 0.0f;
  float avg_len;
  PP_Result top[PP_MAX_TOP_RESULTS];
  int i;
  int dropped_docs = 0;
  size_t question_len;

  if (!question || !index_path || top_k <= 0) {
    return PP_ERR_INVALID_ARG;
  }

  question_len = strlen(question);
  if (question_len == 0) {
    pp_log_error("Question cannot be empty");
    return PP_ERR_EMPTY_INPUT;
  }
  if (question_len >= 500) {
    pp_log_error("Question too long (max 499 chars)");
    return PP_ERR_LIMIT_EXCEEDED;
  }

  if (top_k > PP_MAX_TOP_RESULTS) {
    top_k = PP_MAX_TOP_RESULTS;
  }

  for (i = 0; i < top_k; ++i) {
    top[i].doc_idx = -1;
    top[i].score = 0.0f;
  }

  term_count = pp_parse_query_terms(question, terms);
  if (term_count <= 0) {
    pp_log_error("Question has no valid searchable terms");
    return PP_ERR_INVALID_ARG;
  }

  fp = fopen(index_path, "r");
  if (!fp) {
    pp_log_error("Index file not found. Run import first");
    return PP_ERR_IO;
  }

  while (fgets(line, sizeof(line), fp)) {
    char* tab = strchr(line, '\t');
    if (!tab) {
      continue;
    }

    if (doc_count >= PP_MAX_DOCS) {
      dropped_docs = 1;
      continue;
    }

    *tab = '\0';
    ++tab;

    docs[doc_count].id = atoi(line);
    strncpy(docs[doc_count].text, tab, sizeof(docs[doc_count].text) - 1);
    docs[doc_count].text[sizeof(docs[doc_count].text) - 1] = '\0';
    docs[doc_count].length_tokens = pp_count_tokens_in_text(docs[doc_count].text);
    total_len += (float)docs[doc_count].length_tokens;
    ++doc_count;
  }

  fclose(fp);

  if (doc_count == 0) {
    pp_log_error("Index is empty. Run import with a valid text file");
    return PP_ERR_IO;
  }

  if (dropped_docs) {
    pp_log_error("Index has too many chunks; refine import source");
    return PP_ERR_LIMIT_EXCEEDED;
  }

  avg_len = total_len / (float)doc_count;

  for (i = 0; i < doc_count; ++i) {
    float score = pp_bm25_score(&docs[i], docs, doc_count, terms, term_count, avg_len);
    pp_insert_top_results(top, top_k, i, score);
  }

  printf("Question: %s\n", question);
  if (top[0].doc_idx < 0 || top[0].score <= 0.0f) {
    printf("No strong match found in index.\n");
    return PP_OK;
  }

  printf("Top matches:\n");
  for (i = 0; i < top_k; ++i) {
    if (top[i].doc_idx >= 0 && top[i].score > 0.0f) {
      printf("%d) chunk #%d score=%.3f\n%s\n", i + 1, docs[top[i].doc_idx].id, top[i].score,
             docs[top[i].doc_idx].text);
    }
  }

  return PP_OK;
}
