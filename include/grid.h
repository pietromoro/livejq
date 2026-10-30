/* livejq - the cell grid the core renders into. */
#ifndef LIVEJQ_GRID_H
#define LIVEJQ_GRID_H

#include "defines.h"

typedef enum {
  STYLE_NORMAL = 0,
  STYLE_DIM,
  STYLE_LABEL,
  STYLE_STATUS,
  STYLE_ERROR,
  STYLE_EMPTY,
  STYLE_NULL,
  STYLE_FALSE,
  STYLE_TRUE,
  STYLE_NUMBER,
  STYLE_STRING,
  STYLE_KEY,
  STYLE_PUNCT,
  STYLE_COUNT,
  STYLE_SELECTED = 0x80, /* or'ed onto a style on the cursor's line */
} style;

typedef struct {
  u32 ch;
  u8 style;
} cell;

typedef struct {
  i32 w;
  i32 h;
  cell* cells;
} grid;

grid* grid_new(i32 w, i32 h);
void grid_free(grid* g);
void grid_clear(grid* g);
void grid_fill(grid* g, i32 y, u8 st);
/* Writes UTF-8 text from column x, clipped at max_x. Returns the column after
 * the last one written. */
i32 grid_put(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz len, u8 st);
/* Like grid_put, but ends with an ellipsis when s does not fit. */
i32 grid_put_fit(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz len, u8 st);
i32 grid_puts(grid* g, i32 x, i32 y, const char* s, u8 st);
cell grid_at(const grid* g, i32 x, i32 y);
/* Row y as UTF-8 without trailing blanks. */
void grid_line(const grid* g, i32 y, char* buf, usz cap);

#endif /* LIVEJQ_GRID_H */
