#ifndef PAPERPILOT_QUERY_H
#define PAPERPILOT_QUERY_H

#include "paperpilot/common.h"

PP_Status pp_query_ask(const char* question, const char* index_path, int top_k);

#endif
