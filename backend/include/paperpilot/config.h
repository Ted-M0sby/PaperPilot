#ifndef PAPERPILOT_CONFIG_H
#define PAPERPILOT_CONFIG_H

#include "paperpilot/common.h"

#define PP_INDEX_PATH_MAX 260
#define PP_AI_URL_MAX 260
#define PP_AI_MODEL_MAX 64
#define PP_AI_KEY_MAX 260

typedef struct PP_Config {
  char index_path[PP_INDEX_PATH_MAX];
  int top_k;
  int ai_enabled;
  char ai_api_url[PP_AI_URL_MAX];
  char ai_model[PP_AI_MODEL_MAX];
  char ai_api_key[PP_AI_KEY_MAX];
} PP_Config;

PP_Status pp_config_load(PP_Config* config);

#endif
