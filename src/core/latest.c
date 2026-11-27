/* latest - picks the newest file of a folder whose name matches a pattern. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "latest.h"

static b8 same_char(char a, char b);
static char* join(const char* dir, const char* name);

/* backtracks to the last star only: a later star can take what an earlier
 * one would have, so trying the earlier ones again finds nothing more. */
b8 latest_match(const char* pattern, const char* name) {
  const char* star = NULL;
  const char* resume = NULL;

  while (*name) {
    if (*pattern == '*') {
      star = pattern++;
      resume = name;
    } else if (*pattern == '?' || (*pattern && same_char(*pattern, *name))) {
      pattern++;
      name++;
    } else if (star) {
      pattern = star + 1;
      name = ++resume;
    } else {
      return 0;
    }
  }

  while (*pattern == '*')
    pattern++;

  return *pattern == '\0';
}

char* latest_find(const char* dir, const char* pattern, file_stamp* stamp, char* err, usz errlen) {
  DIR* d = opendir(dir);
  char* best = NULL;
  const char* best_name = NULL;
  struct dirent* de;

  if (!d) {
    snprintf(err, errlen, "%s: %s", dir, strerror(errno));
    return NULL;
  }

  while ((de = readdir(d)) != NULL) {
    file_stamp at;
    if (!latest_match(pattern, de->d_name)) continue;

    char* path = join(dir, de->d_name);
    if (!path) break;

    /* a file can go between the listing and the look. */
    if (!file_stat_regular(path, &at)) {
      free(path);
      continue;
    }

    const char* name = path + strlen(path) - strlen(de->d_name);
    b8 newer = !best
      || at.mtime_ns > stamp->mtime_ns
      || (at.mtime_ns == stamp->mtime_ns && strcmp(name, best_name) > 0);
    if (!newer) {
      free(path);
      continue;
    }

    free(best);
    best = path;
    best_name = name;
    *stamp = at;
  }

  closedir(d);
  if (!best) snprintf(err, errlen, "%s: no file matches %s", dir, pattern);

  return best;
}

static b8 same_char(char a, char b) {
#ifdef _WIN32
  return tolower((u8)a) == tolower((u8)b);
#else
  return a == b;
#endif
}

static char* join(const char* dir, const char* name) {
  usz dlen = strlen(dir);
  b8 sep = dlen > 0 && (dir[dlen - 1] == '/' || dir[dlen - 1] == '\\');
  char* path = malloc(dlen + 1 + strlen(name) + 1);

  if (path) sprintf(path, "%s%s%s", dir, sep ? "" : "/", name);

  return path;
}
