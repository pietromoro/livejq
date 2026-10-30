/* loader - turns the input file into the list of values a query runs over. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "loader.h"

/* jq counts bytes in an int: the parser takes the file in chunks, and a
 * string can hold no more than the largest int. */
#define CHUNK      ((usz)1 << 30)
#define MAX_STRING ((usz)INT32_MAX)

static char* read_all(const char* path, usz* len);
static load_result parse_json(const char* buf, usz len, b8 slurp);
static load_result split_lines(const char* buf, usz len, b8 slurp);

b8 file_stat(const char* path, file_stamp* out) {
  struct stat st;

  if (stat(path, &st) != 0) return 0;

  out->size = (i64)st.st_size;
#if defined(__APPLE__)
  out->mtime_ns = (i64)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
#elif defined(_WIN32)
  out->mtime_ns = (i64)st.st_mtime * 1000000000;
#else
  out->mtime_ns = (i64)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
  return 1;
}

b8 load_file(
  const char* path, b8 raw, b8 slurp, load_result* out, file_stamp* stamp, char* err, usz errlen
) {
  usz len;
  char* buf;

  if (!file_stat(path, stamp) || !(buf = read_all(path, &len))) {
    snprintf(err, errlen, "%s: %s", path, strerror(errno));
    return 0;
  }

  *out = load_buffer(buf, len, raw, slurp);
  free(buf);
  return 1;
}

load_result load_buffer(const char* buf, usz len, b8 raw, b8 slurp) {
  return raw ? split_lines(buf, len, slurp) : parse_json(buf, len, slurp);
}

void load_result_free(load_result* r) {
  jv_free(r->inputs);
  jv_free(r->error);
  r->inputs = jv_array();
  r->error = jv_null();
}

static char* read_all(const char* path, usz* len) {
  FILE* f = fopen(path, "rb");
  char* buf = NULL;
  usz cap = 0;
  usz n = 0;

  if (!f) return NULL;

  for (;;) {
    usz got;

    if (n == cap) {
      char* grown;

      cap = cap ? cap * 2 : 65536;
      grown = realloc(buf, cap);
      if (!grown) {
        free(buf);
        fclose(f);
        errno = ENOMEM;
        return NULL;
      }

      buf = grown;
    }

    got = fread(buf + n, 1, cap - n, f);
    n += got;
    if (got == 0) break;
  }

  if (ferror(f)) {
    i32 saved = errno;

    free(buf);
    fclose(f);
    errno = saved;
    return NULL;
  }

  fclose(f);
  *len = n;
  return buf;
}

static load_result parse_json(const char* buf, usz len, b8 slurp) {
  load_result r = { jv_array(), jv_null() };
  jv_parser* p = jv_parser_new(0);
  jv v = jv_invalid();
  usz at = 0;

  do {
    usz n = len - at < CHUNK ? len - at : CHUNK;

    jv_free(v);
    jv_parser_set_buf(p, buf + at, (i32)n, at + n < len);
    at += n;
    while (jv_is_valid(v = jv_parser_next(p)))
      r.inputs = jv_array_append(r.inputs, v);
  } while (at < len && !jv_invalid_has_msg(jv_copy(v)));

  if (jv_invalid_has_msg(jv_copy(v)))
    r.error = jv_invalid_get_msg(v);
  else jv_free(v);

  jv_parser_free(p);
  if (slurp) r.inputs = jv_array_append(jv_array(), r.inputs);
  return r;
}

static load_result split_lines(const char* buf, usz len, b8 slurp) {
  load_result r = { jv_array(), jv_null() };
  usz start = 0;

  if (slurp && len > MAX_STRING) {
    r.error = jv_string("the file is too long for one string");
    return r;
  }

  if (slurp) {
    r.inputs = jv_array_append(r.inputs, jv_string_sized(buf, (i32)len));
    return r;
  }

  for (usz i = 0; i <= len; i++) {
    if (i < len && buf[i] != '\n') continue;
    if (i == len && start == len) break;

    if (i - start > MAX_STRING) {
      r.error = jv_string("a line is too long for one string");
      return r;
    }

    r.inputs = jv_array_append(r.inputs, jv_string_sized(buf + start, (i32)(i - start)));
    start = i + 1;
  }

  return r;
}
