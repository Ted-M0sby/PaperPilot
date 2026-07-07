#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "paperpilot/ai_client.h"
#include "paperpilot/logging.h"

#define PP_AI_CONTEXT_MAX 8000
#define PP_AI_RESPONSE_MAX 32768
#define PP_AI_MAX_CHUNKS 512
#define PP_AI_MAX_TOP_RESULTS 20

typedef struct PP_AiChunk {
  int id;
  char text[1200];
  int matched;
} PP_AiChunk;

typedef struct PP_AiResult {
  int chunk_idx;
  int score;
} PP_AiResult;

static int pp_hex_val(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static int pp_extract_json_string(const char* json, const char* marker, char* out, size_t out_size) {
  const char* p;
  size_t di = 0;

  if (!json || !marker || !out || out_size == 0) {
    return 0;
  }

  p = strstr(json, marker);
  if (!p) {
    return 0;
  }
  p += strlen(marker);

  while (*p != '\0' && di + 1 < out_size) {
    if (*p == '"') {
      break;
    }

    if (*p == '\\') {
      ++p;
      if (*p == '\0') {
        break;
      }
      if (*p == 'n') {
        out[di++] = '\n';
      } else if (*p == 'r') {
        out[di++] = '\r';
      } else if (*p == 't') {
        out[di++] = '\t';
      } else if (*p == '\\' || *p == '"' || *p == '/') {
        out[di++] = *p;
      } else if (*p == 'u') {
        int h1 = pp_hex_val(*(p + 1));
        int h2 = pp_hex_val(*(p + 2));
        int h3 = pp_hex_val(*(p + 3));
        int h4 = pp_hex_val(*(p + 4));
        if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
          unsigned int cp = (unsigned int)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
          if (cp < 0x80) {
            out[di++] = (char)cp;
          } else if (cp < 0x800 && di + 2 < out_size) {
            out[di++] = (char)(0xC0 | (cp >> 6));
            out[di++] = (char)(0x80 | (cp & 0x3F));
          } else if (di + 3 < out_size) {
            out[di++] = (char)(0xE0 | (cp >> 12));
            out[di++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[di++] = (char)(0x80 | (cp & 0x3F));
          }
          p += 4;
        } else {
          out[di++] = '?';
        }
      } else {
        out[di++] = *p;
      }
    } else {
      out[di++] = *p;
    }
    ++p;
  }

  out[di] = '\0';
  return di > 0;
}

static char* pp_dup_utf8(const char* src) {
  size_t len;
  char* out;
  if (!src) {
    return NULL;
  }
  len = strlen(src);
  out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, src, len + 1);
  return out;
}

static void pp_sanitize_text(char* text) {
  unsigned char* p = (unsigned char*)text;
  unsigned char* w = (unsigned char*)text;

  if (!text) {
    return;
  }

  while (*p != '\0') {
    if (*p == '\n' || *p == '\r' || *p == '\t') {
      *w++ = *p++;
    } else if (*p >= 0x20 && *p < 0x80) {
      *w++ = *p++;
    } else if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
      *w++ = *p++;
      *w++ = *p++;
    } else if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
      *w++ = *p++;
      *w++ = *p++;
      *w++ = *p++;
    } else if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
               (p[3] & 0xc0) == 0x80) {
      *w++ = *p++;
      *w++ = *p++;
      *w++ = *p++;
      *w++ = *p++;
    } else {
      *w++ = ' ';
      ++p;
    }
  }

  *w = '\0';
}

static void pp_json_escape(char* dst, size_t dst_size, const char* src) {
  size_t di = 0;
  size_t i;
  static const char hex[] = "0123456789abcdef";

  if (!dst || !src || dst_size == 0) {
    return;
  }

  for (i = 0; src[i] != '\0' && di + 2 < dst_size; ++i) {
    char c = src[i];
    unsigned char uc = (unsigned char)c;
    if (c == '\\' || c == '"') {
      dst[di++] = '\\';
      dst[di++] = c;
    } else if (c == '\n') {
      dst[di++] = '\\';
      dst[di++] = 'n';
    } else if (c == '\r') {
      dst[di++] = '\\';
      dst[di++] = 'r';
    } else if (c == '\t') {
      dst[di++] = '\\';
      dst[di++] = 't';
    } else if (uc < 0x20) {
      if (di + 6 >= dst_size) {
        break;
      }
      dst[di++] = '\\';
      dst[di++] = 'u';
      dst[di++] = '0';
      dst[di++] = '0';
      dst[di++] = hex[uc >> 4];
      dst[di++] = hex[uc & 0x0f];
    } else {
      dst[di++] = c;
    }
  }
  dst[di] = '\0';
}

static int pp_contains_case_insensitive(const char* text, const char* needle) {
  size_t needle_len;
  const char* p;

  if (!text || !needle) {
    return 0;
  }

  needle_len = strlen(needle);
  if (needle_len == 0) {
    return 0;
  }

  for (p = text; *p != '\0'; ++p) {
    size_t i = 0;
    while (i < needle_len && p[i] != '\0' &&
           tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
      ++i;
    }
    if (i == needle_len) {
      return 1;
    }
  }

  return 0;
}

static int pp_score_chunk_for_question(const char* text, const char* question) {
  static const char* keywords[] = {
    "项目经历", "项目", "经历", "教育经历", "教育", "技能", "实习", "工作", "求职",
    "学校", "专业", "课程", "证书", "能力", "优势", "联系方式", "电话", "邮箱",
    "github", "GitHub", "AI", "LLM", "C++", "Python"
  };
  int score = 0;
  char token[64];
  size_t qi = 0;
  size_t ki;

  if (!text || !question) {
    return 0;
  }

  while (question[qi] != '\0') {
    size_t ti = 0;
    while (question[qi] != '\0' && isspace((unsigned char)question[qi])) {
      ++qi;
    }
    while (question[qi] != '\0' && !isspace((unsigned char)question[qi]) && ti < sizeof(token) - 1) {
      unsigned char c = (unsigned char)question[qi++];
      if (c < 0x80 && ispunct(c)) {
        continue;
      }
      token[ti++] = (char)c;
    }
    token[ti] = '\0';
    while (question[qi] != '\0' && !isspace((unsigned char)question[qi])) {
      ++qi;
    }

    if (ti >= 2 && pp_contains_case_insensitive(text, token)) {
      score += (int)ti;
    }
  }

  for (ki = 0; ki < sizeof(keywords) / sizeof(keywords[0]); ++ki) {
    if (pp_contains_case_insensitive(question, keywords[ki]) &&
        pp_contains_case_insensitive(text, keywords[ki])) {
      score += (int)strlen(keywords[ki]) * 3;
    }
  }

  return score;
}

static void pp_insert_ai_result(PP_AiResult* top, int top_k, int chunk_idx, int score) {
  int i;
  for (i = 0; i < top_k; ++i) {
    if (score > top[i].score) {
      int j;
      for (j = top_k - 1; j > i; --j) {
        top[j] = top[j - 1];
      }
      top[i].chunk_idx = chunk_idx;
      top[i].score = score;
      break;
    }
  }
}

static PP_Status pp_append_context_chunk(char* out_context, size_t out_size, const char* text) {
  if (strlen(out_context) + strlen(text) + 32 >= out_size) {
    return PP_ERR_LIMIT_EXCEEDED;
  }

  strcat(out_context, "[chunk] ");
  strcat(out_context, text);
  if (out_context[strlen(out_context) - 1] != '\n') {
    strcat(out_context, "\n");
  }

  return PP_OK;
}

static PP_Status pp_load_context_snippets(const char* index_path, const char* question, int top_k,
                                          char* out_context, size_t out_size) {
  FILE* fp;
  char line[1400];
  PP_AiChunk chunks[PP_AI_MAX_CHUNKS];
  PP_AiResult top[PP_AI_MAX_TOP_RESULTS];
  int chunk_count = 0;
  int picked = 0;
  int i;

  if (!index_path || !out_context || out_size == 0) {
    return PP_ERR_INVALID_ARG;
  }

  fp = fopen(index_path, "r");
  if (!fp) {
    return PP_ERR_IO;
  }

  out_context[0] = '\0';
  while (fgets(line, sizeof(line), fp) && chunk_count < PP_AI_MAX_CHUNKS) {
    char* tab = strchr(line, '\t');
    if (!tab) {
      continue;
    }
    ++tab;

    *tab = '\0';
    ++tab;
    chunks[chunk_count].id = atoi(line);
    strncpy(chunks[chunk_count].text, tab, sizeof(chunks[chunk_count].text) - 1);
    chunks[chunk_count].text[sizeof(chunks[chunk_count].text) - 1] = '\0';
    chunks[chunk_count].matched = 0;
    ++chunk_count;
  }

  fclose(fp);

  if (chunk_count == 0) {
    return PP_ERR_EMPTY_INPUT;
  }

  if (top_k > PP_AI_MAX_TOP_RESULTS) {
    top_k = PP_AI_MAX_TOP_RESULTS;
  }

  for (i = 0; i < top_k; ++i) {
    top[i].chunk_idx = -1;
    top[i].score = 0;
  }

  for (i = 0; i < chunk_count; ++i) {
    int score = pp_score_chunk_for_question(chunks[i].text, question);
    if (score > 0) {
      pp_insert_ai_result(top, top_k, i, score);
    }
  }

  for (i = 0; i < top_k; ++i) {
    if (top[i].chunk_idx >= 0 && top[i].score > 0) {
      PP_Status st = pp_append_context_chunk(out_context, out_size, chunks[top[i].chunk_idx].text);
      if (st != PP_OK) {
        break;
      }
      chunks[top[i].chunk_idx].matched = 1;
      ++picked;
    }
  }

  for (i = 0; i < chunk_count && picked < top_k; ++i) {
    if (!chunks[i].matched) {
      PP_Status st = pp_append_context_chunk(out_context, out_size, chunks[i].text);
      if (st != PP_OK) {
        break;
      }
      ++picked;
    }
  }

  if (picked == 0) {
    return PP_ERR_EMPTY_INPUT;
  }

  return PP_OK;
}

PP_Status pp_ai_answer(const char* question, const PP_Config* config) {
  char context[PP_AI_CONTEXT_MAX];
  char escaped_q[1024];
  char escaped_ctx[PP_AI_CONTEXT_MAX * 2];
  char payload[PP_AI_CONTEXT_MAX * 2 + 2048];
  FILE* fp;
  char cmd[2048];
  char response[PP_AI_RESPONSE_MAX];
  char answer[PP_AI_RESPONSE_MAX];
  char err_msg[1024];
  size_t n;
  int written;
  char* question_utf8;
  char* context_utf8;

  if (!question || !config) {
    return PP_ERR_INVALID_ARG;
  }

  if (!config->ai_enabled) {
    pp_log_error("AI 未启用，请在 paperpilot.conf 中设置 ai_enabled=1");
    return PP_ERR_NOT_IMPLEMENTED;
  }

  if (config->ai_api_url[0] == '\0' || config->ai_api_key[0] == '\0' || config->ai_model[0] == '\0') {
    pp_log_error("AI 配置不完整，请检查接口地址、密钥和模型名称");
    return PP_ERR_INVALID_ARG;
  }

  if (pp_load_context_snippets(config->index_path, question, config->top_k, context,
                               sizeof(context)) != PP_OK) {
    pp_log_error("无法读取文档索引，请先上传或导入文档");
    return PP_ERR_IO;
  }

  question_utf8 = pp_dup_utf8(question);
  context_utf8 = pp_dup_utf8(context);
  if (!question_utf8 || !context_utf8) {
    free(question_utf8);
    free(context_utf8);
    pp_log_error("准备 AI 请求内容失败，请检查文档编码");
    return PP_ERR_IO;
  }
  pp_sanitize_text(question_utf8);
  pp_sanitize_text(context_utf8);

  pp_json_escape(escaped_q, sizeof(escaped_q), question_utf8);
  pp_json_escape(escaped_ctx, sizeof(escaped_ctx), context_utf8);
  free(question_utf8);
  free(context_utf8);

  written = snprintf(payload, sizeof(payload),
                     "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"You are a "
                     "document assistant. Answer in Chinese using only the provided context. If the context "
                     "contains partial resume or project details, summarize the available details instead of "
                     "saying they are missing.\"},{\"role\":\"user\",\"content\":"
                     "\"Context:%s\\nQuestion:%s\"}],\"temperature\":0.2}",
                     config->ai_model, escaped_ctx, escaped_q);
  if (written < 0 || (size_t)written >= sizeof(payload)) {
    pp_log_error("发送给 AI 的内容过长，请缩短文档或问题");
    return PP_ERR_LIMIT_EXCEEDED;
  }

  fp = fopen(".pp_ai_payload.json", "wb");
  if (!fp) {
    pp_log_error("创建 AI 请求文件失败");
    return PP_ERR_IO;
  }
  fputs(payload, fp);
  fclose(fp);

  snprintf(cmd, sizeof(cmd),
           "curl -s -X POST \"%s\" -H \"Content-Type: application/json\" -H \"Authorization: Bearer %s\" "
           "--data-binary \"@.pp_ai_payload.json\" -o \".pp_ai_response.json\"",
           config->ai_api_url, config->ai_api_key);

  if (system(cmd) != 0) {
    pp_log_error("调用 AI 服务失败，请检查网络、密钥或模型配置");
    remove(".pp_ai_payload.json");
    return PP_ERR_IO;
  }

  fp = fopen(".pp_ai_response.json", "rb");
  if (!fp) {
    pp_log_error("读取 AI 响应失败");
    remove(".pp_ai_payload.json");
    return PP_ERR_IO;
  }

  n = fread(response, 1, sizeof(response) - 1, fp);
  response[n] = '\0';
  fclose(fp);

  if (strstr(response, "\"error\"")) {
    if (pp_extract_json_string(response, "\"message\":\"", err_msg, sizeof(err_msg))) {
      char full_msg[1200];
      snprintf(full_msg, sizeof(full_msg), "AI 服务返回错误: %s", err_msg);
      pp_log_error(full_msg);
    } else {
      pp_log_error("AI 服务返回错误，请稍后重试");
    }
    remove(".pp_ai_payload.json");
    remove(".pp_ai_response.json");
    return PP_ERR_IO;
  }

  if (pp_extract_json_string(response, "\"content\":\"", answer, sizeof(answer))) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    printf("AI 回答:\n%s\n", answer);
  } else {
    pp_log_error("解析 AI 回答失败，请稍后重试");
    remove(".pp_ai_payload.json");
    remove(".pp_ai_response.json");
    return PP_ERR_IO;
  }

  remove(".pp_ai_payload.json");
  remove(".pp_ai_response.json");
  return PP_OK;
}
