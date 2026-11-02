/* store - lists, reads, writes and deletes the .query files of a folder. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "store.h"

#define EXT          ".query"
#define EXT_LEN      (sizeof(EXT) - 1)
#define NAME_MAX_LEN 200

typedef struct {
  char* name;
  file_stamp stamp;
} entry;

struct store {
  char* dir;
  entry* items;
  usz n;
};

static char* path_of(const store* s, const char* name);
static b8 list(const char* dir, entry** items, usz* n);
static void free_entries(entry* items, usz n);
static i32 by_name(const void* a, const void* b);
static b8 same(const entry* a, usz an, const entry* b, usz bn);
static b8 is_device(const char* name);
static void fail(const char* path, char* err, usz errlen);

store* store_open(const char* dir, char* err, usz errlen) {
  store* s = calloc(1, sizeof(*s));

  if (!s || !(s->dir = strdup(dir))) {
    free(s);
    snprintf(err, errlen, "%s: %s", dir, strerror(ENOMEM));
    return NULL;
  }

  if (!list(dir, &s->items, &s->n)) {
    fail(dir, err, errlen);
    store_free(s);
    return NULL;
  }

  return s;
}

void store_free(store* s) {
  if (!s) return;

  free_entries(s->items, s->n);
  free(s->dir);
  free(s);
}

b8 store_rescan(store* s) {
  entry* items;
  usz n;

  if (!list(s->dir, &items, &n)) return 0;

  if (same(s->items, s->n, items, n)) {
    free_entries(items, n);
    return 0;
  }

  free_entries(s->items, s->n);
  s->items = items;
  s->n = n;
  return 1;
}

usz store_count(const store* s) { return s->n; }

const char* store_name(const store* s, usz i) { return s->items[i].name; }

file_stamp store_stamp(const store* s, usz i) { return s->items[i].stamp; }

i32 store_find(const store* s, const char* name) {
  for (usz i = 0; i < s->n; i++)
    if (strcmp(s->items[i].name, name) == 0) return (i32)i;

  return -1;
}

b8 store_name_ok(const char* name) {
  usz len = strlen(name);

  if (len == 0 || len > NAME_MAX_LEN || name[0] == '.') return 0;
  if (name[len - 1] == '.' || name[len - 1] == ' ') return 0;

  for (const char* p = name; *p; p++) {
    if ((u8)*p < 0x20 || *p == 0x7f) return 0;
    if (strchr("/\\:*?\"<>|", *p)) return 0;
  }

  return !is_device(name);
}

char* store_read(const store* s, const char* name, char* err, usz errlen) {
  char* path = path_of(s, name);

  if (!path) {
    snprintf(err, errlen, "%s: %s", name, strerror(ENOMEM));
    return NULL;
  }

  FILE* f = fopen(path, "rb");
  char* text = NULL;
  usz len = 0;

  if (f) {
    char chunk[4096];
    usz got;

    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
      char* grown = realloc(text, len + got + 1);

      if (!grown) {
        errno = ENOMEM;
        break;
      }

      text = grown;
      memcpy(text + len, chunk, got);
      len += got;
    }

    if (got > 0 || ferror(f)) {
      free(text);
      text = NULL;
    } else if (!text) {
      text = calloc(1, 1);
    }

    fclose(f);
  }

  if (!text) {
    fail(path, err, errlen);
    free(path);
    return NULL;
  }

  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
    len--;

  text[len] = '\0';
  free(path);
  return text;
}

b8 store_write(store* s, const char* name, const char* text, char* err, usz errlen) {
  if (!store_name_ok(name)) {
    snprintf(err, errlen, "not a valid name: %s", name);
    return 0;
  }

  char* path = path_of(s, name);

  if (!path) {
    snprintf(err, errlen, "%s: %s", name, strerror(ENOMEM));
    return 0;
  }

  FILE* f = fopen(path, "wb");
  b8 ok = f != NULL;

  if (ok) {
    ok = fputs(text, f) >= 0 && fputc('\n', f) != EOF;
    ok = fclose(f) == 0 && ok;
  }

  if (!ok) fail(path, err, errlen);

  free(path);
  store_rescan(s);
  return ok;
}

b8 store_delete(store* s, const char* name, char* err, usz errlen) {
  char* path = path_of(s, name);

  if (!path) {
    snprintf(err, errlen, "%s: %s", name, strerror(ENOMEM));
    return 0;
  }

  b8 ok = remove(path) == 0;

  if (!ok) fail(path, err, errlen);

  free(path);
  store_rescan(s);
  return ok;
}

static char* path_of(const store* s, const char* name) {
  usz len = strlen(s->dir) + 1 + strlen(name) + EXT_LEN + 1;
  char* path = malloc(len);

  if (path) snprintf(path, len, "%s/%s" EXT, s->dir, name);

  return path;
}

static b8 list(const char* dir, entry** items, usz* n) {
  DIR* d = opendir(dir);

  *items = NULL;
  *n = 0;
  if (!d) return 0;

  usz cap = 0;
  struct dirent* de;

  while ((de = readdir(d)) != NULL) {
    usz len = strlen(de->d_name);

    if (len <= EXT_LEN || strcmp(de->d_name + len - EXT_LEN, EXT) != 0) continue;

    char* name = malloc(len - EXT_LEN + 1);
    entry e = { name, { 0, 0 } };

    if (!name) break;

    memcpy(name, de->d_name, len - EXT_LEN);
    name[len - EXT_LEN] = '\0';

    char* path = malloc(strlen(dir) + 1 + len + 1);
    b8 keep = path && store_name_ok(name);

    if (keep) {
      sprintf(path, "%s/%s", dir, de->d_name);

      struct stat st;
      keep = stat(path, &st) == 0 && S_ISREG(st.st_mode) && file_stat(path, &e.stamp);
    }

    free(path);
    if (!keep) {
      free(name);
      continue;
    }

    if (*n == cap) {
      cap = cap ? cap * 2 : 16;

      entry* grown = realloc(*items, cap * sizeof(**items));
      if (!grown) {
        free(name);
        break;
      }

      *items = grown;
    }

    (*items)[(*n)++] = e;
  }

  closedir(d);
  if (*n > 1) qsort(*items, *n, sizeof(**items), by_name);

  return 1;
}

static void free_entries(entry* items, usz n) {
  for (usz i = 0; i < n; i++) free(items[i].name);

  free(items);
}

static i32 by_name(const void* a, const void* b) {
  const char* x = ((const entry*)a)->name;
  const char* y = ((const entry*)b)->name;

  for (usz i = 0; x[i] || y[i]; i++) {
    i32 cx = tolower((u8)x[i]);
    i32 cy = tolower((u8)y[i]);

    if (cx != cy) return cx - cy;
  }

  return strcmp(x, y);
}

static b8 same(const entry* a, usz an, const entry* b, usz bn) {
  if (an != bn) return 0;

  for (usz i = 0; i < an; i++) {
    if (strcmp(a[i].name, b[i].name) != 0) return 0;
    if (a[i].stamp.mtime_ns != b[i].stamp.mtime_ns || a[i].stamp.size != b[i].stamp.size)
      return 0;
  }

  return 1;
}

static b8 is_device(const char* name) {
  static const char* const devices[] = { "con", "prn", "aux", "nul" };
  char stem[5] = { 0 };
  usz len = strcspn(name, ".");

  if (len > 4) return 0;

  for (usz i = 0; i < len; i++)
    stem[i] = (char)tolower((u8)name[i]);

  for (usz i = 0; i < ARRAY_LEN(devices); i++)
    if (strcmp(stem, devices[i]) == 0) return 1;

  b8 numbered = len == 4 && stem[3] >= '1' && stem[3] <= '9';

  return numbered && (strncmp(stem, "com", 3) == 0 || strncmp(stem, "lpt", 3) == 0);
}

static void fail(const char* path, char* err, usz errlen) {
  snprintf(err, errlen, "%s: %s", path, strerror(errno));
}
