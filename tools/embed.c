/* embed - writes a file's bytes as a C initializer list. */
#include <stdio.h>

#include "defines.h"

int main(int argc, char** argv) {
  FILE* in;
  FILE* out;
  i32 c;
  i32 col = 0;

  if (argc != 3) {
    fprintf(stderr, "usage: embed <input> <output>\n");
    return 2;
  }

  in = fopen(argv[1], "rb");
  if (!in) {
    perror(argv[1]);
    return 1;
  }

  out = fopen(argv[2], "wb");
  if (!out) {
    perror(argv[2]);
    fclose(in);
    return 1;
  }

  while ((c = fgetc(in)) != EOF) {
    col += fprintf(out, "%d,", c);
    if (col >= 72) {
      fputc('\n', out);
      col = 0;
    }
  }

  fputc('\n', out);

  fclose(in);
  if (fclose(out) != 0) {
    perror(argv[2]);
    return 1;
  }

  return 0;
}
