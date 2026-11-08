/* width - the columns of each code point, from Unicode 15.0. */
#include "grid.h"

/* the zero and wide tables, made by tools/width.c. */
#include "width.inc"

static b8 in(const u32 (*t)[2], usz n, u32 c);

i32 utf8_width(u32 c) {
  if (c < 0x300) return 1;
  if (in(zero, ARRAY_LEN(zero), c)) return 0;

  return in(wide, ARRAY_LEN(wide), c) ? 2 : 1;
}

static b8 in(const u32 (*t)[2], usz n, u32 c) {
  usz lo = 0;

  while (lo < n) {
    usz mid = lo + (n - lo) / 2;

    if (c < t[mid][0]) n = mid;
    else if (c > t[mid][1]) lo = mid + 1;
    else return 1;
  }

  return 0;
}
