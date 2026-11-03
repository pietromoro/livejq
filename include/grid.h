/* livejq - the cell grid the core renders into. */
#ifndef LIVEJQ_GRID_H
#define LIVEJQ_GRID_H

#include "defines.h"

typedef enum {
  STYLE_NORMAL = 0,
  STYLE_DIM,       /* the tree while it shows an older query's outputs */
  STYLE_EMPTY,     /* placeholders: no output, an empty query */
  STYLE_LABEL,
  STYLE_ACCENT,
  STYLE_BORDER,
  STYLE_BORDER_FOCUS,
  STYLE_BORDER_ERROR,
  STYLE_GUTTER,    /* the mark left of the cursor's line */
  STYLE_PILL,
  STYLE_PILL_ERROR,
  STYLE_STATUS,
  STYLE_STATUS_FAINT,
  STYLE_STATUS_INFO,
  STYLE_STATUS_TIME,
  STYLE_STATUS_ERROR,
  STYLE_ERROR,     /* a mark on the query where it cannot parse */
  STYLE_HELP_KEY,
  STYLE_HELP,
  STYLE_HELP_SEP,
  STYLE_NULL,
  STYLE_FALSE,
  STYLE_TRUE,
  STYLE_NUMBER,
  STYLE_STRING,
  STYLE_KEY,
  STYLE_PUNCT,
  STYLE_VARIABLE,
  STYLE_KEYWORD,
  STYLE_BUILTIN,
  STYLE_OPERATOR,
  STYLE_FORMAT,
  STYLE_COMMENT,
  STYLE_MATCH,     /* text the tree's search found */
  STYLE_COUNT,
  STYLE_SELECTED = 0x80,      /* or'ed onto a style on the cursor's line */
  STYLE_SELECTED_IDLE = 0x40, /* the same, while the tree has no focus */
} style;

typedef struct {
  u32 ch;
  u8 style;
} cell;

typedef struct {
  i32 w;
  i32 h;
  cell* cells;
  i32 cursor_x; /* -1 when the terminal cursor is hidden */
  i32 cursor_y;
} grid;

grid* grid_new(i32 w, i32 h);
void grid_free(grid* g);
void grid_clear(grid* g);
void grid_fill(grid* g, i32 y, u8 st);
/* Fills row y from column x up to max_x with blanks in style st. */
void grid_fill_span(grid* g, i32 x, i32 max_x, i32 y, u8 st);
/* Sets the style of row y from column x up to max_x to st, keeping the
 * text. */
void grid_restyle(grid* g, i32 x, i32 max_x, i32 y, u8 st);
/* Draws a box with rounded corners, its inside left untouched. */
void grid_box(grid* g, i32 x, i32 y, i32 w, i32 h, u8 st);
/* Writes UTF-8 text from column x, clipped at max_x. Returns the column after
 * the last one written. */
i32 grid_put(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz len, u8 st);
/* Like grid_put, but ends with an ellipsis when s does not fit. */
i32 grid_put_fit(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz len, u8 st);
i32 grid_puts(grid* g, i32 x, i32 y, const char* s, u8 st);
/* The columns s takes, one per code point. */
i32 utf8_columns(const char* s, usz len);
cell grid_at(const grid* g, i32 x, i32 y);
/* Row y as UTF-8 without trailing blanks. */
void grid_line(const grid* g, i32 y, char* buf, usz cap);
/* Writes c as UTF-8 into out, which has room for 4 bytes. Returns the byte
 * count. */
usz utf8_put(u32 c, char* out);

#endif /* LIVEJQ_GRID_H */
