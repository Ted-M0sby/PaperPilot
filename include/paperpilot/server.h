#ifndef PAPERPILOT_SERVER_H
#define PAPERPILOT_SERVER_H

#include "paperpilot/common.h"
#include "paperpilot/config.h"

PP_Status pp_server_run(const PP_Config* config, const char* frontend_dir, int port);

#endif
