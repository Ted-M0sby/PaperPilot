#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paperpilot/config.h"

static void pp_trim(char* s) {
  char* p;
  char* end;

  if (!s) {
    return;
  }

  p = s;
  while (*p != '\0' && isspace((unsigned char)*p)) {
    ++p;
  }
  if (p != s) {
    memmove(s, p, strlen(p) + 1);
  }

  end = s + strlen(s);
  while (end > s && isspace((unsigned char)*(end - 1))) {
    --end;
  }
  *end = '\0';
}

PP_Status pp_config_load(PP_Config* config) {
  FILE* fp;
  char line[512];

  if (!config) {
    return PP_ERR_INVALID_ARG;
  }

  strncpy(config->index_path, "./data/index.dat", sizeof(config->index_path) - 1);
  config->index_path[sizeof(config->index_path) - 1] = '\0';
  config->top_k = 3;
    config->ai_enabled = 0;
    strncpy(config->ai_api_url, "http://127.0.0.1:11434/v1/chat/completions",
      sizeof(config->ai_api_url) - 1);
    config->ai_api_url[sizeof(config->ai_api_url) - 1] = '\0';
    strncpy(config->ai_model, "qwen2.5:7b", sizeof(config->ai_model) - 1);
    config->ai_model[sizeof(config->ai_model) - 1] = '\0';
    config->ai_api_key[0] = '\0';

  fp = fopen("paperpilot.conf", "r");
  if (!fp) {
    return PP_OK;
  }

  while (fgets(line, sizeof(line), fp)) {
    char* eq = strchr(line, '=');
    char* key;
    char* value;

    if (!eq) {
      continue;
    }

    *eq = '\0';
    key = line;
    value = eq + 1;

    pp_trim(key);
    pp_trim(value);

    if (strcmp(key, "index_path") == 0) {
      strncpy(config->index_path, value, sizeof(config->index_path) - 1);
      config->index_path[sizeof(config->index_path) - 1] = '\0';
    } else if (strcmp(key, "top_k") == 0) {
      int parsed = atoi(value);
      if (parsed >= 1 && parsed <= 10) {
        config->top_k = parsed;
      }
    } else if (strcmp(key, "ai_enabled") == 0) {
      config->ai_enabled = atoi(value) ? 1 : 0;
    } else if (strcmp(key, "ai_api_url") == 0) {
      strncpy(config->ai_api_url, value, sizeof(config->ai_api_url) - 1);
      config->ai_api_url[sizeof(config->ai_api_url) - 1] = '\0';
    } else if (strcmp(key, "ai_model") == 0) {
      strncpy(config->ai_model, value, sizeof(config->ai_model) - 1);
      config->ai_model[sizeof(config->ai_model) - 1] = '\0';
    } else if (strcmp(key, "ai_api_key") == 0) {
      if (strncmp(value, "env:", 4) == 0) {
        const char* env_name = value + 4;
        const char* env_val = getenv(env_name);
        if (env_val) {
          strncpy(config->ai_api_key, env_val, sizeof(config->ai_api_key) - 1);
          config->ai_api_key[sizeof(config->ai_api_key) - 1] = '\0';
        }
      } else {
        strncpy(config->ai_api_key, value, sizeof(config->ai_api_key) - 1);
        config->ai_api_key[sizeof(config->ai_api_key) - 1] = '\0';
      }
    }
  }

  fclose(fp);
  return PP_OK;
}
