#include <stdio.h>
#include <string.h>

#include "paperpilot/cli.h"

PP_Command pp_cli_parse(int argc, char** argv) {
  PP_Command cmd = {PP_CMD_HELP, NULL, NULL};

  if (argc < 2) {
    return cmd;
  }

  if (strcmp(argv[1], "help") == 0) {
    cmd.type = PP_CMD_HELP;
  } else if (strcmp(argv[1], "import") == 0) {
    cmd.type = PP_CMD_IMPORT;
    cmd.arg1 = (argc >= 3) ? argv[2] : NULL;
  } else if (strcmp(argv[1], "importdir") == 0) {
    cmd.type = PP_CMD_IMPORT_DIR;
    cmd.arg1 = (argc >= 3) ? argv[2] : NULL;
  } else if (strcmp(argv[1], "ask") == 0) {
    cmd.type = PP_CMD_ASK;
    cmd.arg1 = (argc >= 3) ? argv[2] : NULL;
  } else if (strcmp(argv[1], "stats") == 0) {
    cmd.type = PP_CMD_STATS;
  } else if (strcmp(argv[1], "answer") == 0) {
    cmd.type = PP_CMD_ANSWER;
    cmd.arg1 = (argc >= 3) ? argv[2] : NULL;
  } else if (strcmp(argv[1], "serve") == 0) {
    cmd.type = PP_CMD_SERVE;
    cmd.arg1 = (argc >= 3) ? argv[2] : NULL;
  } else {
    cmd.type = PP_CMD_UNKNOWN;
  }

  return cmd;
}

void pp_cli_print_help(void) {
  printf("paperpilot - C literature companion (MVP)\n");
  printf("Usage:\n");
  printf("  paperpilot help\n");
  printf("  paperpilot import <text_file>\n");
  printf("  paperpilot importdir <folder>\n");
  printf("  paperpilot ask <question>\n");
  printf("  paperpilot answer <question>\n");
  printf("  paperpilot stats\n");
  printf("  paperpilot serve [port]\n");
}
