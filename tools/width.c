/* width - writes the tables of code points taking no columns or two, from
 * the Unicode data files, as C initializers for src/core/width.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"

#define CODE_POINTS 0x110000

static b8 read_widths(const char* path, u8* wide);
static b8 read_categories(const char* path, u8* zero);
static void table(FILE* out, const char* name, const u8* in);

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: width <EastAsianWidth.txt> <UnicodeData.txt> <output>\n");
    return 2;
  }

  u8* wide = calloc(CODE_POINTS, 1);
  u8* zero = calloc(CODE_POINTS, 1);
  if (!wide || !zero) return 1;
  if (!read_widths(argv[1], wide) || !read_categories(argv[2], zero)) return 1;

  zero[0xad] = 0;
  for (u32 c = 0x1160; c <= 0x11ff; c++) zero[c] = 1;
  zero[0x200b] = 1;

  FILE* out = fopen(argv[3], "wb");
  if (!out) {
    perror(argv[3]);
    return 1;
  }

  table(out, "zero", zero);
  table(out, "wide", wide);
  if (fclose(out) != 0) {
    perror(argv[3]);
    return 1;
  }

  return 0;
}

/* marks the code points whose east asian width is W or F. */
static b8 read_widths(const char* path, u8* wide) {
  FILE* in = fopen(path, "rb");

  if (!in) {
    perror(path);
    return 0;
  }

  char line[512];
  while (fgets(line, sizeof(line), in)) {
    if (line[0] == '#' || line[0] == '\n') continue;

    u32 from;
    u32 to;
    char w[3];
    i32 n = sscanf(line, "%x..%x;%2[A-Za-z]", &from, &to, w);
    if (n != 3) {
      n = sscanf(line, "%x;%2[A-Za-z]", &from, w);
      to = from;
      if (n != 2) continue;
    }

    if (strcmp(w, "W") != 0 && strcmp(w, "F") != 0) continue;

    for (u32 c = from; c <= to && c < CODE_POINTS; c++) wide[c] = 1;
  }

  fclose(in);
  return 1;
}

/* marks the combining marks and format characters: categories Mn, Me
 * and Cf. a <..., First> line and its <..., Last> give a range. */
static b8 read_categories(const char* path, u8* zero) {
  FILE* in = fopen(path, "rb");
  u32 first = 0;

  if (!in) {
    perror(path);
    return 0;
  }

  char line[512];
  while (fgets(line, sizeof(line), in)) {
    char* name = strchr(line, ';');
    char* cat = name ? strchr(name + 1, ';') : NULL;
    if (!cat) continue;

    u32 c = (u32)strtoul(line, NULL, 16);
    b8 marks = strncmp(cat + 1, "Mn;", 3) == 0 || strncmp(cat + 1, "Me;", 3) == 0
      || strncmp(cat + 1, "Cf;", 3) == 0;

    if (strstr(name, ", First>")) {
      first = c;
      continue;
    }

    u32 from = strstr(name, ", Last>") ? first : c;
    for (u32 k = from; marks && k <= c && k < CODE_POINTS; k++) zero[k] = 1;
  }

  fclose(in);
  return 1;
}

static void table(FILE* out, const char* name, const u8* in) {
  i32 col = 0;

  fprintf(out, "static const u32 %s[][2] = {\n", name);
  for (u32 c = 0; c < CODE_POINTS; c++) {
    if (!in[c]) continue;

    u32 end = c;
    while (end + 1 < CODE_POINTS && in[end + 1]) end++;

    fprintf(out, "%s{ 0x%05x, 0x%05x },", col ? " " : "    ", c, end);
    if (++col == 4) {
      fputc('\n', out);
      col = 0;
    }

    c = end;
  }

  if (col) fputc('\n', out);

  fprintf(out, "};\n");
}
