#include "paperpilot/index.h"

static int g_index_is_open = 0;

PP_Status pp_index_open(const char* index_path) {
  if (!index_path) {
    return PP_ERR_INVALID_ARG;
  }
  g_index_is_open = 1;
  return PP_OK;
}

PP_Status pp_index_close(void) {
  if (!g_index_is_open) {
    return PP_ERR_INVALID_ARG;
  }
  g_index_is_open = 0;
  return PP_OK;
}
