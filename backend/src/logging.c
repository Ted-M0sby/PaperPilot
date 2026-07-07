#include <stdio.h>

#include "paperpilot/logging.h"

void pp_log_info(const char* message) {
  fprintf(stdout, "[INFO] %s\n", message ? message : "(null)");
}

void pp_log_error(const char* message) {
  fprintf(stderr, "[ERROR] %s\n", message ? message : "(null)");
}
