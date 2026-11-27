/* livejq - reads the input file into jq values. */
#ifndef LIVEJQ_LOADER_H
#define LIVEJQ_LOADER_H

#include <jv.h>

#include "defines.h"

typedef struct {
  i64 mtime_ns;
  i64 size;
} file_stamp;

typedef struct {
  jv inputs; /* array, one per input */
  jv error;  /* string or null */
} load_result;

b8 file_stat(const char* path, file_stamp* out);

/* As file_stat, but false too when path is not a regular file. */
b8 file_stat_regular(const char* path, file_stamp* out);

/* False when the file cannot be read, with the reason in err. */
b8 load_file(
  const char* path, b8 raw, b8 slurp, load_result* out, file_stamp* stamp, char* err, usz errlen
);

/* One input per JSON value, or per line in raw mode; slurp makes a single
 * input of the array of values, or of the whole text in raw mode. Values
 * parsed before an error are kept. */
load_result load_buffer(const char* buf, usz len, b8 raw, b8 slurp);
void load_result_free(load_result* r);

#endif /* LIVEJQ_LOADER_H */
