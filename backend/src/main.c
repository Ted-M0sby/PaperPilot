#include <stdio.h>
#include <stdlib.h>

#include "paperpilot/ai_client.h"
#include "paperpilot/cli.h"
#include "paperpilot/config.h"
#include "paperpilot/document.h"
#include "paperpilot/logging.h"
#include "paperpilot/query.h"
#include "paperpilot/server.h"
#include "paperpilot/stats.h"

int main(int argc, char** argv) {
  PP_Config config = {0};
  PP_Command cmd = pp_cli_parse(argc, argv);

  if (pp_config_load(&config) != PP_OK) {
    pp_log_error("Failed to load config");
    return PP_ERR_IO;
  }

  switch (cmd.type) {
    case PP_CMD_HELP:
      pp_cli_print_help();
      return PP_OK;
    case PP_CMD_IMPORT:
      if (!cmd.arg1) {
        pp_log_error("Missing file path for import command");
        return PP_ERR_INVALID_ARG;
      }
      return pp_document_import(cmd.arg1, config.index_path);
    case PP_CMD_IMPORT_DIR:
      if (!cmd.arg1) {
        pp_log_error("Missing directory path for importdir command");
        return PP_ERR_INVALID_ARG;
      }
      return pp_document_import_dir(cmd.arg1, config.index_path);
    case PP_CMD_ASK:
      if (!cmd.arg1) {
        pp_log_error("Missing question text for ask command");
        return PP_ERR_INVALID_ARG;
      }
      return pp_query_ask(cmd.arg1, config.index_path, config.top_k);
    case PP_CMD_STATS:
      return pp_index_stats_print(config.index_path);
    case PP_CMD_ANSWER:
      if (!cmd.arg1) {
        pp_log_error("Missing question text for answer command");
        return PP_ERR_INVALID_ARG;
      }
      return pp_ai_answer(cmd.arg1, &config);
    case PP_CMD_SERVE: {
      int port = 8080;
      if (cmd.arg1) {
        port = atoi(cmd.arg1);
      }
      return pp_server_run(&config, "./frontend", port);
    }
    default:
      pp_log_error("Unknown command");
      pp_cli_print_help();
      return PP_ERR_INVALID_ARG;
  }
}
