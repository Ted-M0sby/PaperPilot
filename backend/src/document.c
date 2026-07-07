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
#define PP_EXTRACT_BUFFER 8192

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

static const char* pp_file_ext(const char* name) {
  const char* ext = strrchr(name, '.');
  if (!ext) {
    return "";
  }
  return ext;
}

static int pp_ext_equals(const char* ext, const char* expected) {
  if (!ext || !expected) {
    return 0;
  }
  while (*ext && *expected) {
    if (tolower((unsigned char)*ext) != tolower((unsigned char)*expected)) {
      return 0;
    }
    ++ext;
    ++expected;
  }
  return *ext == '\0' && *expected == '\0';
}

int pp_document_is_supported(const char* file_path) {
  const char* ext = pp_file_ext(file_path);
  return pp_ext_equals(ext, ".txt") || pp_ext_equals(ext, ".docx") ||
         pp_ext_equals(ext, ".ppt") || pp_ext_equals(ext, ".pdf");
}

static int pp_is_importable_file(const char* name) {
  return pp_document_is_supported(name);
}

#if defined(_WIN32)
static PP_Status pp_extract_docx_text_windows(const char* file_path, char* out_path,
                                              size_t out_path_size) {
  char temp_dir[MAX_PATH];
  char temp_path[MAX_PATH];
  char command[4600];
  int n;
  int rc;
  DWORD len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);

  if (!file_path || !out_path || out_path_size == 0) {
    return PP_ERR_INVALID_ARG;
  }

  if (len == 0 || len >= sizeof(temp_dir)) {
    return PP_ERR_IO;
  }
  if (!GetTempFileNameA(temp_dir, "ppx", 0, temp_path)) {
    return PP_ERR_IO;
  }

  strncpy(out_path, temp_path, out_path_size - 1);
  out_path[out_path_size - 1] = '\0';

  n = snprintf(command, sizeof(command),
               "powershell -NoProfile -ExecutionPolicy Bypass -Command "
               "\"Add-Type -AssemblyName System.IO.Compression.FileSystem;"
               "$z=[IO.Compression.ZipFile]::OpenRead('%s');"
               "$e=$z.GetEntry('word/document.xml');"
               "if($null -eq $e){$z.Dispose(); exit 2};"
               "$r=New-Object IO.StreamReader($e.Open());"
               "$s=$r.ReadToEnd();$r.Dispose();$z.Dispose();"
               "$s=$s -replace '<w:tab[^>]*/>',' ';"
               "$s=$s -replace '</w:p>','`n';"
               "$s=$s -replace '<[^>]+>',' ';"
               "$s=[Net.WebUtility]::HtmlDecode($s);"
               "[IO.File]::WriteAllText('%s',$s,[Text.Encoding]::UTF8)\"",
               file_path, out_path);
  if (n <= 0 || (size_t)n >= sizeof(command)) {
    DeleteFileA(out_path);
    out_path[0] = '\0';
    return PP_ERR_LIMIT_EXCEEDED;
  }

  rc = system(command);
  if (rc != 0) {
    DeleteFileA(out_path);
    out_path[0] = '\0';
    pp_log_error("Cannot extract docx text");
    return PP_ERR_IO;
  }

  return PP_OK;
}

static PP_Status pp_extract_pdf_text_windows(const char* file_path, char* out_path,
                                             size_t out_path_size) {
  char temp_dir[MAX_PATH];
  char temp_path[MAX_PATH];
  char command[4600];
  int n;
  int rc;
  DWORD len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);

  if (!file_path || !out_path || out_path_size == 0) {
    return PP_ERR_INVALID_ARG;
  }

  if (len == 0 || len >= sizeof(temp_dir)) {
    return PP_ERR_IO;
  }
  if (!GetTempFileNameA(temp_dir, "ppp", 0, temp_path)) {
    return PP_ERR_IO;
  }

  strncpy(out_path, temp_path, out_path_size - 1);
  out_path[out_path_size - 1] = '\0';

  n = snprintf(command, sizeof(command),
               "pwsh -NoProfile -ExecutionPolicy Bypass -File "
               "\".\\scripts\\Extract-PdfText.ps1\" -InputPath \"%s\" -OutputPath \"%s\"",
               file_path, out_path);
  if (n <= 0 || (size_t)n >= sizeof(command)) {
    DeleteFileA(out_path);
    out_path[0] = '\0';
    return PP_ERR_LIMIT_EXCEEDED;
  }

  rc = system(command);
  if (rc != 0) {
    DeleteFileA(out_path);
    out_path[0] = '\0';
    pp_log_error("Cannot extract pdf text");
    return PP_ERR_IO;
  }

  return PP_OK;
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

static PP_Status pp_import_binary_strings_internal(const char* file_path, FILE* out, int* chunk_id,
                                                   int* wrote_chunks,
                                                   char seen[][PP_CHUNK_BUFFER], int* seen_count) {
  FILE* fp;
  unsigned char buf[PP_EXTRACT_BUFFER];
  char chunk[PP_CHUNK_BUFFER];
  size_t chunk_len = 0;
  int chunk_has_word = 0;
  PP_Status status = PP_OK;

  fp = fopen(file_path, "rb");
  if (!fp) {
    pp_log_error("Cannot open binary input file");
    return PP_ERR_IO;
  }

  while (!feof(fp)) {
    size_t n = fread(buf, 1, sizeof(buf), fp);
    size_t i;
    for (i = 0; i < n; ++i) {
      unsigned char c = buf[i];
      if ((c >= 0x20 && c <= 0x7e) || c >= 0x80) {
        if (chunk_len < sizeof(chunk) - 1) {
          chunk[chunk_len++] = (char)c;
        }
        if (!isspace(c)) {
          chunk_has_word = 1;
        }
      } else if (chunk_len > 0) {
        if (chunk_has_word && chunk_len >= 4) {
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

      if (chunk_len >= 240) {
        chunk[chunk_len] = '\0';
        status = pp_write_chunk(out, chunk, chunk_id, wrote_chunks, seen, seen_count);
        if (status != PP_OK) {
          fclose(fp);
          return status;
        }
        chunk_len = 0;
        chunk_has_word = 0;
      }
    }
  }

  if (chunk_len > 0 && chunk_has_word && chunk_len >= 4) {
    chunk[chunk_len] = '\0';
    status = pp_write_chunk(out, chunk, chunk_id, wrote_chunks, seen, seen_count);
  }

  fclose(fp);
  return status;
}

static PP_Status pp_import_supported_file_internal(const char* file_path, FILE* out, int* chunk_id,
                                                  int* wrote_chunks,
                                                  char seen[][PP_CHUNK_BUFFER], int* seen_count) {
  const char* ext = pp_file_ext(file_path);

  if (pp_ext_equals(ext, ".txt")) {
    return pp_import_single_file_internal(file_path, out, chunk_id, wrote_chunks, seen, seen_count);
  }

#if defined(_WIN32)
  if (pp_ext_equals(ext, ".docx")) {
    char extracted[MAX_PATH] = {0};
    PP_Status status = pp_extract_docx_text_windows(file_path, extracted, sizeof(extracted));
    if (status != PP_OK) {
      return status;
    }
    status = pp_import_single_file_internal(extracted, out, chunk_id, wrote_chunks, seen, seen_count);
    DeleteFileA(extracted);
    return status;
  }

  if (pp_ext_equals(ext, ".pdf")) {
    char extracted[MAX_PATH] = {0};
    PP_Status status = pp_extract_pdf_text_windows(file_path, extracted, sizeof(extracted));
    if (status != PP_OK) {
      return status;
    }
    status = pp_import_single_file_internal(extracted, out, chunk_id, wrote_chunks, seen, seen_count);
    DeleteFileA(extracted);
    return status;
  }
#endif

  if (pp_ext_equals(ext, ".ppt")) {
    return pp_import_binary_strings_internal(file_path, out, chunk_id, wrote_chunks, seen, seen_count);
  }

  pp_log_error("Unsupported document type. Supported: .txt, .docx, .ppt, .pdf");
  return PP_ERR_NOT_IMPLEMENTED;
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

  status = pp_import_supported_file_internal(file_path, out, &chunk_id, &wrote_chunks, seen,
                                             &seen_count);
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

    snprintf(pattern, sizeof(pattern), "%s\\*.*", dir_path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
      fclose(out);
      pp_index_close();
      free(seen);
      pp_log_error("No .txt files found in directory");
      return PP_ERR_EMPTY_INPUT;
    }

    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          pp_is_importable_file(fd.cFileName)) {
        char file_path[400];
        snprintf(file_path, sizeof(file_path), "%s\\%s", dir_path, fd.cFileName);
        status = pp_import_supported_file_internal(file_path, out, &chunk_id, &wrote_chunks, seen,
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
      if (entry->d_type != DT_DIR && pp_is_importable_file(entry->d_name)) {
        char file_path[400];
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        status = pp_import_supported_file_internal(file_path, out, &chunk_id, &wrote_chunks, seen,
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
