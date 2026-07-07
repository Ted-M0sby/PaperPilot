#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#define PP_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define PP_CLOSESOCK close
#endif

#include "paperpilot/ai_client.h"
#include "paperpilot/document.h"
#include "paperpilot/logging.h"
#include "paperpilot/query.h"
#include "paperpilot/server.h"
#include "paperpilot/stats.h"

#define PP_REQ_MAX 33554432
#define PP_RES_MAX 262144
#define PP_SMALL_BUF 1024

typedef struct PP_ExecContext {
  const PP_Config* config;
  const char* text;
  int op;
} PP_ExecContext;

enum {
  PP_OP_IMPORT = 1,
  PP_OP_IMPORT_DIR = 2,
  PP_OP_ASK = 3,
  PP_OP_ANSWER = 4,
  PP_OP_STATS = 5
};

static void pp_json_escape(const char* src, char* dst, size_t dst_size) {
  size_t si = 0;
  size_t di = 0;

  if (!src || !dst || dst_size == 0) {
    return;
  }

  while (src[si] != '\0' && di + 2 < dst_size) {
    unsigned char c = (unsigned char)src[si++];
    if (c == '"' || c == '\\') {
      dst[di++] = '\\';
      dst[di++] = (char)c;
    } else if (c == '\n') {
      dst[di++] = '\\';
      dst[di++] = 'n';
    } else if (c == '\r') {
      dst[di++] = '\\';
      dst[di++] = 'r';
    } else if (c == '\t') {
      dst[di++] = '\\';
      dst[di++] = 't';
    } else if (c < 0x20) {
      dst[di++] = ' ';
    } else {
      dst[di++] = (char)c;
    }
  }

  dst[di] = '\0';
}

static int pp_send_all(SOCKET sock, const char* data, int len) {
  int sent = 0;
  while (sent < len) {
    int n = send(sock, data + sent, len - sent, 0);
    if (n <= 0) {
      return 0;
    }
    sent += n;
  }
  return 1;
}

static void pp_send_json(SOCKET sock, int status_code, const char* body) {
  char head[PP_SMALL_BUF];
  const char* status_text = "OK";
  int body_len = (int)strlen(body);
  int n;

  if (status_code == 400) {
    status_text = "Bad Request";
  } else if (status_code == 404) {
    status_text = "Not Found";
  } else if (status_code == 500) {
    status_text = "Internal Server Error";
  }

  n = snprintf(head, sizeof(head),
               "HTTP/1.1 %d %s\r\n"
               "Content-Type: application/json; charset=utf-8\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Access-Control-Allow-Headers: Content-Type\r\n"
               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n\r\n",
               status_code, status_text, body_len);

  if (n > 0) {
    pp_send_all(sock, head, n);
    pp_send_all(sock, body, body_len);
  }
}

static void pp_send_text(SOCKET sock, int status_code, const char* content_type, const char* body) {
  char head[PP_SMALL_BUF];
  const char* status_text = "OK";
  int body_len = (int)strlen(body);
  int n;

  if (status_code == 404) {
    status_text = "Not Found";
  }

  n = snprintf(head, sizeof(head),
               "HTTP/1.1 %d %s\r\n"
               "Content-Type: %s\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n\r\n",
               status_code, status_text, content_type, body_len);

  if (n > 0) {
    pp_send_all(sock, head, n);
    pp_send_all(sock, body, body_len);
  }
}

static int pp_json_get_string(const char* body, const char* key, char* out, size_t out_size) {
  char marker[128];
  const char* p;
  size_t di = 0;

  if (!body || !key || !out || out_size == 0) {
    return 0;
  }

  snprintf(marker, sizeof(marker), "\"%s\"", key);
  p = strstr(body, marker);
  if (!p) {
    return 0;
  }

  p = strchr(p, ':');
  if (!p) {
    return 0;
  }
  ++p;

  while (*p && isspace((unsigned char)*p)) {
    ++p;
  }
  if (*p != '"') {
    return 0;
  }
  ++p;

  while (*p && di + 1 < out_size) {
    if (*p == '"') {
      out[di] = '\0';
      return 1;
    }

    if (*p == '\\') {
      ++p;
      if (*p == '\0') {
        break;
      }
      if (*p == 'n') {
        out[di++] = '\n';
      } else if (*p == 'r') {
        out[di++] = '\r';
      } else if (*p == 't') {
        out[di++] = '\t';
      } else {
        out[di++] = *p;
      }
      ++p;
      continue;
    }

    out[di++] = *p++;
  }

  out[di] = '\0';
  return 0;
}

static int pp_base64_value(int c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+' || c == '-') {
    return 62;
  }
  if (c == '/' || c == '_') {
    return 63;
  }
  if (c == '=') {
    return -2;
  }
  return -1;
}

static int pp_write_base64_file(const char* src, const char* path) {
  FILE* fp;
  int values[4];
  int count = 0;
  size_t i;
  const char* data;

  if (!src || !path) {
    return 0;
  }

  data = strchr(src, ',');
  if (data && strstr(src, "base64") && data > src) {
    src = data + 1;
  }

  fp = fopen(path, "wb");
  if (!fp) {
    return 0;
  }

  for (i = 0; src[i] != '\0'; ++i) {
    int v = pp_base64_value((unsigned char)src[i]);
    if (v == -1) {
      if (isspace((unsigned char)src[i])) {
        continue;
      }
      fclose(fp);
      return 0;
    }

    values[count++] = v;
    if (count == 4) {
      unsigned char out[3];
      if (values[0] < 0 || values[1] < 0) {
        fclose(fp);
        return 0;
      }
      out[0] = (unsigned char)((values[0] << 2) | (values[1] >> 4));
      fwrite(out, 1, 1, fp);
      if (values[2] != -2) {
        out[1] = (unsigned char)(((values[1] & 15) << 4) | (values[2] >> 2));
        fwrite(out + 1, 1, 1, fp);
      }
      if (values[3] != -2) {
        out[2] = (unsigned char)(((values[2] & 3) << 6) | values[3]);
        fwrite(out + 2, 1, 1, fp);
      }
      if (values[2] == -2 || values[3] == -2) {
        count = 0;
        break;
      }
      count = 0;
    }
  }

  if (count == 2) {
    unsigned char out = (unsigned char)((values[0] << 2) | (values[1] >> 4));
    fwrite(&out, 1, 1, fp);
    count = 0;
  } else if (count == 3) {
    unsigned char out[2];
    out[0] = (unsigned char)((values[0] << 2) | (values[1] >> 4));
    out[1] = (unsigned char)(((values[1] & 15) << 4) | (values[2] >> 2));
    fwrite(out, 1, 2, fp);
    count = 0;
  }

  fclose(fp);
  return count == 0;
}

static const char* pp_safe_upload_extension(const char* filename) {
  const char* ext;

  if (!filename) {
    return ".txt";
  }

  ext = strrchr(filename, '.');
  if (!ext) {
    return ".txt";
  }

  if (_stricmp(ext, ".txt") == 0) {
    return ".txt";
  }
  if (_stricmp(ext, ".docx") == 0) {
    return ".docx";
  }
  if (_stricmp(ext, ".ppt") == 0) {
    return ".ppt";
  }
  if (_stricmp(ext, ".pdf") == 0) {
    return ".pdf";
  }

  return ".txt";
}

static const char* pp_strcasestr_local(const char* haystack, const char* needle) {
  size_t needle_len;
  const char* p;

  if (!haystack || !needle) {
    return NULL;
  }

  needle_len = strlen(needle);
  if (needle_len == 0) {
    return haystack;
  }

  for (p = haystack; *p != '\0'; ++p) {
    size_t i = 0;
    while (i < needle_len && p[i] != '\0' &&
           tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
      ++i;
    }
    if (i == needle_len) {
      return p;
    }
  }

  return NULL;
}

static PP_Status pp_exec_operation(PP_ExecContext* ctx) {
  if (!ctx || !ctx->config) {
    return PP_ERR_INVALID_ARG;
  }

  switch (ctx->op) {
    case PP_OP_IMPORT:
      return pp_document_import(ctx->text, ctx->config->index_path);
    case PP_OP_IMPORT_DIR:
      return pp_document_import_dir(ctx->text, ctx->config->index_path);
    case PP_OP_ASK:
      return pp_query_ask(ctx->text, ctx->config->index_path, ctx->config->top_k);
    case PP_OP_ANSWER:
      return pp_ai_answer(ctx->text, ctx->config);
    case PP_OP_STATS:
      return pp_index_stats_print(ctx->config->index_path);
    default:
      return PP_ERR_INVALID_ARG;
  }
}

static FILE* pp_open_capture_file(char* path, size_t path_size) {
#if defined(_WIN32)
  char temp_dir[MAX_PATH];
  char temp_path[MAX_PATH];
  DWORD len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);

  if (len == 0 || len >= sizeof(temp_dir)) {
    return NULL;
  }
  if (!GetTempFileNameA(temp_dir, "pp", 0, temp_path)) {
    return NULL;
  }
  if (path && path_size > 0) {
    strncpy(path, temp_path, path_size - 1);
    path[path_size - 1] = '\0';
  }
  return fopen(temp_path, "w+b");
#else
  (void)path;
  (void)path_size;
  return tmpfile();
#endif
}

static PP_Status pp_capture_operation(PP_ExecContext* ctx, char* out, size_t out_size) {
  FILE* tmp_out;
  FILE* tmp_err;
#if defined(_WIN32)
  char tmp_out_path[MAX_PATH] = {0};
  char tmp_err_path[MAX_PATH] = {0};
#endif
  int saved_out;
  int saved_err;
  PP_Status st;
  size_t n;

  if (!ctx || !out || out_size == 0) {
    return PP_ERR_INVALID_ARG;
  }

  out[0] = '\0';
  tmp_out = pp_open_capture_file(tmp_out_path, sizeof(tmp_out_path));
  tmp_err = pp_open_capture_file(tmp_err_path, sizeof(tmp_err_path));
  if (!tmp_out || !tmp_err) {
    if (tmp_out) {
      fclose(tmp_out);
    }
    if (tmp_err) {
      fclose(tmp_err);
    }
    return PP_ERR_IO;
  }

  saved_out = _dup(_fileno(stdout));
  saved_err = _dup(_fileno(stderr));
  if (saved_out < 0 || saved_err < 0) {
    fclose(tmp_out);
    fclose(tmp_err);
    return PP_ERR_IO;
  }

  fflush(stdout);
  fflush(stderr);
  _dup2(_fileno(tmp_out), _fileno(stdout));
  _dup2(_fileno(tmp_err), _fileno(stderr));

  st = pp_exec_operation(ctx);

  fflush(stdout);
  fflush(stderr);
  _dup2(saved_out, _fileno(stdout));
  _dup2(saved_err, _fileno(stderr));
  _close(saved_out);
  _close(saved_err);

  rewind(tmp_out);
  n = fread(out, 1, out_size - 1, tmp_out);
  out[n] = '\0';

  if (n + 2 < out_size) {
    out[n++] = '\n';
    out[n] = '\0';
    rewind(tmp_err);
    n += fread(out + n, 1, out_size - 1 - n, tmp_err);
    out[n] = '\0';
  }

  fclose(tmp_out);
  fclose(tmp_err);
#if defined(_WIN32)
  if (tmp_out_path[0] != '\0') {
    remove(tmp_out_path);
  }
  if (tmp_err_path[0] != '\0') {
    remove(tmp_err_path);
  }
#endif
  return st;
}

static void pp_reply_operation(SOCKET sock, PP_ExecContext* ctx, const char* success_msg) {
  char* raw = (char*)malloc(PP_RES_MAX);
  char* escaped = (char*)malloc(PP_RES_MAX * 2);
  char* payload = (char*)malloc(PP_RES_MAX * 2 + 256);
  PP_Status st;

  if (!raw || !escaped || !payload) {
    free(raw);
    free(escaped);
    free(payload);
    pp_send_json(sock, 500, "{\"ok\":false,\"message\":\"服务器内存不足\"}");
    return;
  }

  st = pp_capture_operation(ctx, raw, PP_RES_MAX);

  if (raw[0] == '\0' && success_msg) {
    strncpy(raw, success_msg, PP_RES_MAX - 1);
    raw[PP_RES_MAX - 1] = '\0';
  }

  pp_json_escape(raw, escaped, PP_RES_MAX * 2);
  snprintf(payload, PP_RES_MAX * 2 + 256, "{\"ok\":%s,\"status\":%d,\"output\":\"%s\"}",
           st == PP_OK ? "true" : "false", (int)st, escaped);
  pp_send_json(sock, st == PP_OK ? 200 : 500, payload);

  free(raw);
  free(escaped);
  free(payload);
}

static int pp_read_file_text(const char* path, char** out_data, int* out_len) {
  FILE* fp;
  long size;
  char* data;
  size_t readn;

  fp = fopen(path, "rb");
  if (!fp) {
    return 0;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 0;
  }
  size = ftell(fp);
  if (size < 0 || size > PP_REQ_MAX) {
    fclose(fp);
    return 0;
  }
  rewind(fp);

  data = (char*)malloc((size_t)size + 1);
  if (!data) {
    fclose(fp);
    return 0;
  }

  readn = fread(data, 1, (size_t)size, fp);
  fclose(fp);
  data[readn] = '\0';

  *out_data = data;
  *out_len = (int)readn;
  return 1;
}

static const char* pp_guess_content_type(const char* path) {
  const char* ext = strrchr(path, '.');
  if (!ext) {
    return "text/plain; charset=utf-8";
  }
  if (strcmp(ext, ".html") == 0) {
    return "text/html; charset=utf-8";
  }
  if (strcmp(ext, ".css") == 0) {
    return "text/css; charset=utf-8";
  }
  if (strcmp(ext, ".js") == 0) {
    return "application/javascript; charset=utf-8";
  }
  return "text/plain; charset=utf-8";
}

static void pp_handle_static(SOCKET sock, const char* frontend_dir, const char* route) {
  char path[512];
  char* data = NULL;
  int len = 0;
  char header[PP_SMALL_BUF];
  int n;

  if (strstr(route, "..")) {
    pp_send_text(sock, 404, "text/plain; charset=utf-8", "Not found");
    return;
  }

  if (strcmp(route, "/") == 0) {
    route = "/index.html";
  }

  snprintf(path, sizeof(path), "%s%s", frontend_dir, route);
  if (!pp_read_file_text(path, &data, &len)) {
    pp_send_text(sock, 404, "text/plain; charset=utf-8", "Not found");
    return;
  }

  n = snprintf(header, sizeof(header),
               "HTTP/1.1 200 OK\r\n"
               "Content-Type: %s\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n\r\n",
               pp_guess_content_type(path), len);

  if (n > 0) {
    pp_send_all(sock, header, n);
    pp_send_all(sock, data, len);
  }

  free(data);
}

static void pp_handle_api(SOCKET sock, const PP_Config* config, const char* method, const char* path,
                          const char* body) {
  PP_ExecContext ctx;
  char* val = (char*)malloc(PP_REQ_MAX);
  char filename[260];
  char temp_path[320];
  FILE* fp;

  if (!val) {
    pp_send_json(sock, 500, "{\"ok\":false,\"message\":\"服务器内存不足\"}");
    return;
  }

  ctx.config = config;
  ctx.text = NULL;
  ctx.op = 0;

  if (strcmp(method, "OPTIONS") == 0) {
    pp_send_json(sock, 200, "{\"ok\":true}");
    free(val);
    return;
  }

  if (strcmp(path, "/api/health") == 0) {
    pp_send_json(sock, 200, "{\"ok\":true,\"message\":\"服务运行中\"}");
    free(val);
    return;
  }

  if (strcmp(path, "/api/stats") == 0 && strcmp(method, "GET") == 0) {
    ctx.op = PP_OP_STATS;
    pp_reply_operation(sock, &ctx, "统计完成");
    free(val);
    return;
  }

  if (strcmp(path, "/api/import") == 0 && strcmp(method, "POST") == 0) {
    if (!pp_json_get_string(body, "path", val, PP_REQ_MAX) || val[0] == '\0') {
      pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"缺少文件路径\"}");
      free(val);
      return;
    }
    ctx.op = PP_OP_IMPORT;
    ctx.text = val;
    pp_reply_operation(sock, &ctx, "导入完成");
    free(val);
    return;
  }

  if (strcmp(path, "/api/importdir") == 0 && strcmp(method, "POST") == 0) {
    if (!pp_json_get_string(body, "path", val, PP_REQ_MAX) || val[0] == '\0') {
      pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"缺少目录路径\"}");
      free(val);
      return;
    }
    ctx.op = PP_OP_IMPORT_DIR;
    ctx.text = val;
    pp_reply_operation(sock, &ctx, "目录导入完成");
    free(val);
    return;
  }

  if (strcmp(path, "/api/import_text") == 0 && strcmp(method, "POST") == 0) {
    if (!pp_json_get_string(body, "filename", filename, sizeof(filename)) || filename[0] == '\0') {
      strcpy(filename, "upload.txt");
    }

    if (!pp_document_is_supported(filename)) {
      pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"仅支持 txt、docx、ppt、pdf 文档\"}");
      free(val);
      return;
    }

    snprintf(temp_path, sizeof(temp_path), "./data/upload_%lu%s", (unsigned long)time(NULL),
             pp_safe_upload_extension(filename));

    if (pp_json_get_string(body, "contentBase64", val, PP_REQ_MAX) && val[0] != '\0') {
      if (!pp_write_base64_file(val, temp_path)) {
        pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"文档编码无效\"}");
        free(val);
        return;
      }
    } else {
      if (!pp_json_get_string(body, "content", val, PP_REQ_MAX) || val[0] == '\0') {
        pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"缺少文档内容\"}");
        free(val);
        return;
      }

      fp = fopen(temp_path, "wb");
      if (!fp) {
        pp_send_json(sock, 500, "{\"ok\":false,\"message\":\"创建临时文档失败\"}");
        free(val);
        return;
      }

      fwrite(val, 1, strlen(val), fp);
      fclose(fp);
    }

    ctx.op = PP_OP_IMPORT;
    ctx.text = temp_path;
    pp_reply_operation(sock, &ctx, "上传文档导入完成");
    free(val);
    return;
  }

  if (strcmp(path, "/api/ask") == 0 && strcmp(method, "POST") == 0) {
    if (!pp_json_get_string(body, "question", val, PP_REQ_MAX) || val[0] == '\0') {
      pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"缺少问题内容\"}");
      free(val);
      return;
    }
    ctx.op = PP_OP_ASK;
    ctx.text = val;
    pp_reply_operation(sock, &ctx, NULL);
    free(val);
    return;
  }

  if (strcmp(path, "/api/answer") == 0 && strcmp(method, "POST") == 0) {
    if (!pp_json_get_string(body, "question", val, PP_REQ_MAX) || val[0] == '\0') {
      pp_send_json(sock, 400, "{\"ok\":false,\"message\":\"缺少问题内容\"}");
      free(val);
      return;
    }
    ctx.op = PP_OP_ANSWER;
    ctx.text = val;
    pp_reply_operation(sock, &ctx, NULL);
    free(val);
    return;
  }

  pp_send_json(sock, 404, "{\"ok\":false,\"message\":\"接口不存在\"}");
  free(val);
}

static void pp_handle_client(SOCKET sock, const PP_Config* config, const char* frontend_dir) {
  char* req = (char*)malloc(PP_REQ_MAX + 1);
  int total = 0;
  int n;
  char method[16] = {0};
  char path[256] = {0};
  char* header_end;
  char* body = "";
  int content_length = 0;
  int header_len = 0;
  int body_bytes = 0;

  if (!req) {
    pp_send_json(sock, 500, "{\"ok\":false,\"message\":\"服务器内存不足\"}");
    return;
  }

  while ((n = recv(sock, req + total, PP_REQ_MAX - total, 0)) > 0) {
    total += n;
    req[total] = '\0';
    if (strstr(req, "\r\n\r\n")) {
      break;
    }
    if (total >= PP_REQ_MAX) {
      break;
    }
  }

  if (total <= 0) {
    free(req);
    return;
  }

  sscanf(req, "%15s %255s", method, path);
  header_end = strstr(req, "\r\n\r\n");
  if (header_end) {
    const char* cl = pp_strcasestr_local(req, "content-length:");
    body = header_end + 4;
    header_len = (int)(body - req);
    body_bytes = total - header_len;

    if (cl) {
      content_length = atoi(cl + 15);

      while (body_bytes < content_length && total < PP_REQ_MAX) {
        n = recv(sock, req + total, PP_REQ_MAX - total, 0);
        if (n <= 0) {
          break;
        }
        total += n;
        body_bytes += n;
        req[total] = '\0';
      }

      if (content_length >= 0 && header_len + content_length <= total) {
        req[header_len + content_length] = '\0';
      }
    }
  }

  if (strncmp(path, "/api/", 5) == 0) {
    pp_handle_api(sock, config, method, path, body);
  } else {
    pp_handle_static(sock, frontend_dir, path);
  }

  free(req);
}

PP_Status pp_server_run(const PP_Config* config, const char* frontend_dir, int port) {
#if !defined(_WIN32)
  (void)config;
  (void)frontend_dir;
  (void)port;
  pp_log_error("Serve mode is currently supported on Windows only");
  return PP_ERR_NOT_IMPLEMENTED;
#else
  WSADATA wsa;
  SOCKET server_fd;
  struct sockaddr_in addr;

  if (!config || !frontend_dir || port <= 0 || port > 65535) {
    return PP_ERR_INVALID_ARG;
  }

  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    pp_log_error("WSAStartup failed");
    return PP_ERR_IO;
  }

  server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server_fd == INVALID_SOCKET) {
    WSACleanup();
    pp_log_error("socket creation failed");
    return PP_ERR_IO;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
    PP_CLOSESOCK(server_fd);
    WSACleanup();
    pp_log_error("bind failed, maybe port already used");
    return PP_ERR_IO;
  }

  if (listen(server_fd, 8) == SOCKET_ERROR) {
    PP_CLOSESOCK(server_fd);
    WSACleanup();
    pp_log_error("listen failed");
    return PP_ERR_IO;
  }

  printf("PaperPilot Web API running at http://127.0.0.1:%d\n", port);
  printf("Press Ctrl+C to stop.\n");

  while (1) {
    SOCKET client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == INVALID_SOCKET) {
      continue;
    }
    pp_handle_client(client_fd, config, frontend_dir);
    PP_CLOSESOCK(client_fd);
  }

  PP_CLOSESOCK(server_fd);
  WSACleanup();
  return PP_OK;
#endif
}
