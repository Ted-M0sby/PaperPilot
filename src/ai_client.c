#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "paperpilot/ai_client.h"
#include "paperpilot/logging.h"

#define PP_AI_CONTEXT_MAX 4000
#define PP_AI_RESPONSE_MAX 32768

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

#if defined(_WIN32)
static char* pp_dup_utf8_from_ansi(const char* src) {
  int wide_len;
  int utf8_len;
  wchar_t* wide_buf;
  char* utf8_buf;

  if (!src) {
    return NULL;
  }

  wide_len = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0);
  if (wide_len <= 0) {
    return NULL;
  }

  wide_buf = (wchar_t*)malloc((size_t)wide_len * sizeof(wchar_t));
  if (!wide_buf) {
    return NULL;
  }

  if (MultiByteToWideChar(CP_ACP, 0, src, -1, wide_buf, wide_len) <= 0) {
    free(wide_buf);
    return NULL;
  }

  utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_buf, -1, NULL, 0, NULL, NULL);
  if (utf8_len <= 0) {
    free(wide_buf);
    return NULL;
  }

  utf8_buf = (char*)malloc((size_t)utf8_len);
  if (!utf8_buf) {
    free(wide_buf);
    return NULL;
  }

  if (WideCharToMultiByte(CP_UTF8, 0, wide_buf, -1, utf8_buf, utf8_len, NULL, NULL) <= 0) {
    free(wide_buf);
    free(utf8_buf);
    return NULL;
  }

  free(wide_buf);
  return utf8_buf;
}
#else
static char* pp_dup_utf8_from_ansi(const char* src) {
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
#endif

static void pp_json_escape(char* dst, size_t dst_size, const char* src) {
  size_t di = 0;
  size_t i;

  if (!dst || !src || dst_size == 0) {
    return;
  }

  for (i = 0; src[i] != '\0' && di + 2 < dst_size; ++i) {
    char c = src[i];
    if (c == '\\' || c == '"') {
      dst[di++] = '\\';
      dst[di++] = c;
    } else if (c == '\n' || c == '\r') {
      dst[di++] = ' ';
    } else if ((unsigned char)c < 0x20) {
      dst[di++] = ' ';
    } else {
      dst[di++] = c;
    }
  }
  dst[di] = '\0';
}

static PP_Status pp_load_context_snippets(const char* index_path, int top_k, char* out_context,
                                          size_t out_size) {
  FILE* fp;
  char line[1400];
  int picked = 0;

  if (!index_path || !out_context || out_size == 0) {
    return PP_ERR_INVALID_ARG;
  }

  fp = fopen(index_path, "r");
  if (!fp) {
    return PP_ERR_IO;
  }

  out_context[0] = '\0';
  while (fgets(line, sizeof(line), fp) && picked < top_k) {
    char* tab = strchr(line, '\t');
    if (!tab) {
      continue;
    }
    ++tab;

    if (strlen(out_context) + strlen(tab) + 32 >= out_size) {
      break;
    }

    strcat(out_context, "[chunk] ");
    strcat(out_context, tab);
    if (out_context[strlen(out_context) - 1] != '\n') {
      strcat(out_context, "\n");
    }
    ++picked;
  }

  fclose(fp);

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
    pp_log_error("AI is disabled. Set ai_enabled=1 in paperpilot.conf");
    return PP_ERR_NOT_IMPLEMENTED;
  }

  if (config->ai_api_url[0] == '\0' || config->ai_api_key[0] == '\0' || config->ai_model[0] == '\0') {
    pp_log_error("Missing ai_api_url/ai_api_key/ai_model in paperpilot.conf");
    return PP_ERR_INVALID_ARG;
  }

  if (pp_load_context_snippets(config->index_path, config->top_k, context, sizeof(context)) != PP_OK) {
    pp_log_error("Cannot load index context. Run import/importdir first");
    return PP_ERR_IO;
  }

  question_utf8 = pp_dup_utf8_from_ansi(question);
  context_utf8 = pp_dup_utf8_from_ansi(context);
  if (!question_utf8 || !context_utf8) {
    free(question_utf8);
    free(context_utf8);
    pp_log_error("Failed to prepare UTF-8 request content");
    return PP_ERR_IO;
  }

  pp_json_escape(escaped_q, sizeof(escaped_q), question_utf8);
  pp_json_escape(escaped_ctx, sizeof(escaped_ctx), context_utf8);
  free(question_utf8);
  free(context_utf8);

  written = snprintf(payload, sizeof(payload),
                     "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"You are a "
                     "paper assistant. Answer only with provided context.\"},{\"role\":\"user\",\"content\":"
                     "\"Context:%s\\nQuestion:%s\"}],\"temperature\":0.2}",
                     config->ai_model, escaped_ctx, escaped_q);
  if (written < 0 || (size_t)written >= sizeof(payload)) {
    pp_log_error("AI payload too large, reduce top_k or document size");
    return PP_ERR_LIMIT_EXCEEDED;
  }

  fp = fopen(".pp_ai_payload.json", "wb");
  if (!fp) {
    return PP_ERR_IO;
  }
  fputs(payload, fp);
  fclose(fp);

  snprintf(cmd, sizeof(cmd),
           "curl -s -X POST \"%s\" -H \"Content-Type: application/json\" -H \"Authorization: Bearer %s\" "
           "--data-binary \"@.pp_ai_payload.json\" -o \".pp_ai_response.json\"",
           config->ai_api_url, config->ai_api_key);

  if (system(cmd) != 0) {
    pp_log_error("Failed to call AI endpoint via curl");
    remove(".pp_ai_payload.json");
    return PP_ERR_IO;
  }

  fp = fopen(".pp_ai_response.json", "rb");
  if (!fp) {
    remove(".pp_ai_payload.json");
    return PP_ERR_IO;
  }

  n = fread(response, 1, sizeof(response) - 1, fp);
  response[n] = '\0';
  fclose(fp);

  if (strstr(response, "\"error\"")) {
    if (pp_extract_json_string(response, "\"message\":\"", err_msg, sizeof(err_msg))) {
      pp_log_error(err_msg);
    } else {
      pp_log_error("AI returned an error response");
    }
    remove(".pp_ai_payload.json");
    remove(".pp_ai_response.json");
    return PP_ERR_IO;
  }

  if (pp_extract_json_string(response, "\"content\":\"", answer, sizeof(answer))) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    printf("AI answer:\n%s\n", answer);
  } else {
    pp_log_error("Failed to parse AI answer content");
    remove(".pp_ai_payload.json");
    remove(".pp_ai_response.json");
    return PP_ERR_IO;
  }

  remove(".pp_ai_payload.json");
  remove(".pp_ai_response.json");
  return PP_OK;
}
