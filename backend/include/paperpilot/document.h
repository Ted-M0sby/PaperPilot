#ifndef PAPERPILOT_DOCUMENT_H
#define PAPERPILOT_DOCUMENT_H

#include "paperpilot/common.h"

PP_Status pp_document_import(const char* file_path, const char* index_path);
PP_Status pp_document_import_dir(const char* dir_path, const char* index_path);
int pp_document_is_supported(const char* file_path);

#endif
