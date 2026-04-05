#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define pp_mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <dirent.h>
#define pp_mkdir(path) mkdir(path, 0755)
#endif

#include "paperpilot/document.h"
#include "paperpilot/index.h"
#include "paperpilot/logging.h"

#define PP_MAX_SEEN_CHUNKS 4096
#define PP_CHUNK_BUFFER 1024

static PP_Status pp_ensure_index_parent_dir(const char* index_path) {
  char parent[260];
  const char* slash;
  size_t len;

  if (!index_path) {
    return PP_ERR_INVALID_ARG;
  }

  slash = strrchr(index_path, '/');
  if (!slash) {
    slash = strrchr(index_path, '\\');
  }

  if (!slash) {
    return PP_OK;
  }

  len = (size_t)(slash - index_path);
  if (len == 0 || len >= sizeof(parent)) {
    return PP_ERR_IO;
  }

  memcpy(parent, index_path, len);
  parent[len] = '\0';

  if (pp_mkdir(parent) != 0 && errno != EEXIST) {
    return PP_ERR_IO;
  }

  return PP_OK;
}

#if !defined(_WIN32)
static int pp_is_txt_file(const char* name) {
  const char* ext = strrchr(name, '.');
  if (!ext) {
    return 0;
  }
  return (strcmp(ext, ".txt") == 0);
}
#endif

static int pp_chunk_exists(char seen[][PP_CHUNK_BUFFER], int seen_count, const char* chunk) {
  int i;
  for (i = 0; i < seen_count; ++i) {
    if (strcmp(seen[i], chunk) == 0) {
      return 1;
    }
  }
  return 0;
}

static void pp_trim_chunk(char* chunk) {
  size_t len;
  size_t start = 0;
  size_t end;

  if (!chunk) {
    return;
  }

  len = strlen(chunk);
  while (start < len && isspace((unsigned char)chunk[start])) {
    ++start;
  }

  if (start > 0) {
    memmove(chunk, chunk + start, len - start + 1);
  }

  len = strlen(chunk);
  end = len;
  while (end > 0 && isspace((unsigned char)chunk[end - 1])) {
    --end;
  }
  chunk[end] = '\0';
}

static PP_Status pp_write_chunk(FILE* out, char* chunk, int* chunk_id, int* wrote_chunks,
                                char seen[][PP_CHUNK_BUFFER], int* seen_count) {
  pp_trim_chunk(chunk);
  if (chunk[0] == '\0') {
    return PP_OK;
  }

  if (pp_chunk_exists(seen, *seen_count, chunk)) {
    return PP_OK;
  }

  if (*seen_count >= PP_MAX_SEEN_CHUNKS) {
    return PP_ERR_LIMIT_EXCEEDED;
  }

  strncpy(seen[*seen_count], chunk, PP_CHUNK_BUFFER - 1);
  seen[*seen_count][PP_CHUNK_BUFFER - 1] = '\0';
  *seen_count += 1;

  fprintf(out, "%d\t%s\n", (*chunk_id)++, chunk);
  *wrote_chunks = 1;
  return PP_OK;
}

static PP_Status pp_import_single_file_internal(const char* file_path, FILE* out, int* chunk_id,
                                                int* wrote_chunks,
                                                char seen[][PP_CHUNK_BUFFER], int* seen_count) {
  FILE* fp;
  char in_buf[PP_CHUNK_BUFFER];
  char chunk[PP_CHUNK_BUFFER];
  size_t chunk_len = 0;
  int chunk_has_word = 0;
  PP_Status status = PP_OK;

  fp = fopen(file_path, "r");
  if (!fp) {
    pp_log_error("Cannot open input file");
    return PP_ERR_IO;
  }

  while (fgets(in_buf, sizeof(in_buf), fp)) {
    size_t i;
    size_t in_len = strlen(in_buf);
    for (i = 0; i < in_len; ++i) {
      char c = in_buf[i];
      if (c == '\n' || c == '\r' || c == '\t') {
        c = ' ';
      }

      if (c != ' ') {
        chunk_has_word = 1;
      }

      if (chunk_len < sizeof(chunk) - 1) {
        chunk[chunk_len++] = c;
      }

      if (chunk_len >= 240) {
        if (chunk_has_word) {
          chunk[chunk_len] = '\0';
          status = pp_write_chunk(out, chunk, chunk_id, wrote_chunks, seen, seen_count);
          if (status != PP_OK) {
            fclose(fp);
            return status;
          }
        }
        chunk_len = 0;
        chunk_has_word = 0;
      }
    }
  }

  if (chunk_len > 0 && chunk_has_word) {
    chunk[chunk_len] = '\0';
    status = pp_write_chunk(out, chunk, chunk_id, wrote_chunks, seen, seen_count);
    if (status != PP_OK) {
      fclose(fp);
      return status;
    }
  }

  fclose(fp);
  return PP_OK;
}

static PP_Status pp_open_index_writer(const char* index_path, FILE** out_fp) {
  PP_Status status;

  status = pp_ensure_index_parent_dir(index_path);
  if (status != PP_OK) {
    pp_log_error("Cannot prepare index directory");
    return status;
  }

  *out_fp = fopen(index_path, "w");
  if (!*out_fp) {
    pp_log_error("Cannot open index output file");
    return PP_ERR_IO;
  }

  status = pp_index_open(index_path);
  if (status != PP_OK) {
    fclose(*out_fp);
    *out_fp = NULL;
    return PP_ERR_IO;
  }

  return PP_OK;
}

static PP_Status pp_finish_index_writer(FILE* out, int wrote_chunks) {
  fclose(out);
  if (pp_index_close() != PP_OK) {
    return PP_ERR_IO;
  }

  if (!wrote_chunks) {
    pp_log_error("Input is empty after normalization");
    return PP_ERR_EMPTY_INPUT;
  }

  pp_log_info("Import completed and index file updated");
  return PP_OK;
}

PP_Status pp_document_import(const char* file_path, const char* index_path) {
  FILE* out;
  int chunk_id = 1;
  int wrote_chunks = 0;
  char(*seen)[PP_CHUNK_BUFFER];
  int seen_count = 0;
  PP_Status status;

  if (!file_path || !index_path) {
    return PP_ERR_INVALID_ARG;
  }

  seen = (char(*)[PP_CHUNK_BUFFER])malloc(PP_MAX_SEEN_CHUNKS * PP_CHUNK_BUFFER);
  if (!seen) {
    return PP_ERR_IO;
  }

  status = pp_open_index_writer(index_path, &out);
  if (status != PP_OK) {
    free(seen);
    return status;
  }

  status = pp_import_single_file_internal(file_path, out, &chunk_id, &wrote_chunks, seen, &seen_count);
  if (status != PP_OK) {
    fclose(out);
    pp_index_close();
    free(seen);
    return status;
  }

  status = pp_finish_index_writer(out, wrote_chunks);
  free(seen);
  return status;
}

PP_Status pp_document_import_dir(const char* dir_path, const char* index_path) {
  FILE* out;
  int chunk_id = 1;
  int wrote_chunks = 0;
  char(*seen)[PP_CHUNK_BUFFER];
  int seen_count = 0;
  PP_Status status;

  if (!dir_path || !index_path) {
    return PP_ERR_INVALID_ARG;
  }

  seen = (char(*)[PP_CHUNK_BUFFER])malloc(PP_MAX_SEEN_CHUNKS * PP_CHUNK_BUFFER);
  if (!seen) {
    return PP_ERR_IO;
  }

  status = pp_open_index_writer(index_path, &out);
  if (status != PP_OK) {
    free(seen);
    return status;
  }

#if defined(_WIN32)
  {
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[300];

    snprintf(pattern, sizeof(pattern), "%s\\*.txt", dir_path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
      fclose(out);
      pp_index_close();
      free(seen);
      pp_log_error("No .txt files found in directory");
      return PP_ERR_EMPTY_INPUT;
    }

    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        char file_path[400];
        snprintf(file_path, sizeof(file_path), "%s\\%s", dir_path, fd.cFileName);
        status = pp_import_single_file_internal(file_path, out, &chunk_id, &wrote_chunks, seen,
                                                &seen_count);
        if (status != PP_OK) {
          FindClose(h);
          fclose(out);
          pp_index_close();
          free(seen);
          return status;
        }
      }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
  }
#else
  {
    DIR* dir = opendir(dir_path);
    struct dirent* entry;
    if (!dir) {
      fclose(out);
      pp_index_close();
      free(seen);
      return PP_ERR_IO;
    }

    while ((entry = readdir(dir)) != NULL) {
      if (entry->d_type != DT_DIR && pp_is_txt_file(entry->d_name)) {
        char file_path[400];
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        status = pp_import_single_file_internal(file_path, out, &chunk_id, &wrote_chunks, seen,
                                                &seen_count);
        if (status != PP_OK) {
          closedir(dir);
          fclose(out);
          pp_index_close();
          free(seen);
          return status;
        }
      }
    }

    closedir(dir);
  }
#endif
  status = pp_finish_index_writer(out, wrote_chunks);
  free(seen);
  return status;
}
