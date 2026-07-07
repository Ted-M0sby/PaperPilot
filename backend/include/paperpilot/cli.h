#ifndef PAPERPILOT_CLI_H
#define PAPERPILOT_CLI_H

#include "paperpilot/common.h"

typedef enum PP_CommandType {
  PP_CMD_HELP = 0,
  PP_CMD_IMPORT = 1,
  PP_CMD_IMPORT_DIR = 2,
  PP_CMD_ASK = 3,
  PP_CMD_STATS = 4,
  PP_CMD_ANSWER = 5,
  PP_CMD_SERVE = 6,
  PP_CMD_UNKNOWN = 99
} PP_CommandType;

typedef struct PP_Command {
  PP_CommandType type;
  const char* arg1;
  const char* arg2;
} PP_Command;

PP_Command pp_cli_parse(int argc, char** argv);
void pp_cli_print_help(void);

#endif
