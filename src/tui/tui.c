/* tui - curses in, core events out, the core's grid drawn back. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

#include "app.h"
#include "guard.h"
#include "thread.h"
#include "tui.h"

#define TICK_MS    30
#define CURSOR_BAR 4 /* PDCursesMod's caret, where 1 is an underline */

static void init_colors(void);
static b8 translate(i32 kind, wint_t ch, u32* key);
static b8 translate_mouse(app_event* ev);
static void blit(const grid* g);

static attr_t style_attrs[STYLE_COUNT];
static attr_t selected_attrs[STYLE_COUNT]; /* the cursor's line */
static attr_t idle_attrs[STYLE_COUNT];     /* the same, while the tree has no focus */
static i32 cursor_shown = -1; /* curs_set rewrites the cursor shape on each call */

/* PDCursesMod's vt port switches screens with ?47, which some terminals
 * ignore, and on endwin moves the cursor to the bottom row and forces a
 * block cursor. ?1049 around it saves and restores the cursor and screen. */
void tui_enter(void) {
  guard_arm();
#ifndef _WIN32
  fputs("\033[?1049h", stdout);
  fflush(stdout);
#endif
  initscr();
}

void tui_leave(void) {
  endwin();
#ifndef _WIN32
  fputs("\033[?1049l\033[0 q", stdout);
  fflush(stdout);
#endif
  guard_disarm();
}

i32 tui_run(const app_opts* opts) {
  char err[256];
  app* a;
  grid* g;
  b8 dirty = 1;

  a = app_new(opts, err, sizeof(err));
  if (!a) {
    fprintf(stderr, "livejq: %s\n", err);
    return 1;
  }

  setlocale(LC_ALL, "");
  tui_enter();
  raw();
  noecho();
  keypad(stdscr, TRUE);
  timeout(TICK_MS);
  mousemask(BUTTON1_PRESSED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL); /* no motion: ?1003 floods hover events */
  mouseinterval(0);
  PDC_set_bold(TRUE); /* real bold: a brightened color would not be one of the terminal's */
  init_colors();
  g = grid_new(COLS, LINES);

  for (;;) {
    app_event ev;
    wint_t ch;
    i32 kind;

    if (dirty) {
      app_render(a, g);
      blit(g);
    }

    kind = get_wch(&ch);
    ev.kind = EV_TICK;
    ev.key = 0;
    ev.now_ms = clock_us() / 1000;
    if (kind == KEY_CODE_YES && ch == KEY_RESIZE) {
      resize_term(0, 0);
      grid_free(g);
      g = grid_new(COLS, LINES);
      clear();
    } else if (kind == KEY_CODE_YES && ch == KEY_MOUSE) {
      if (translate_mouse(&ev)) ev.kind = EV_MOUSE;
    } else if (kind != ERR) {
      if (translate(kind, ch, &ev.key)) ev.kind = EV_KEY;
    }

    dirty = app_feed(a, ev) || (kind == KEY_CODE_YES && ch == KEY_RESIZE);
    if (app_quitting(a)) break;
  }

  tui_leave();
  grid_free(g);
  app_free(a);
  return 0;
}

/* the values keep jq's colors from the terminal's own palette: gray null,
 * green strings, bold punctuation and keys, which the query shares. the
 * chrome is gruvbox: a yellow accent, aqua and red, and its grays, or
 * their nearest basic colors below 256. the cursor's line gets a dark
 * background while the tree has focus, reverse without colors. a color
 * stands in for dim and reverse, and NO_COLOR keeps the attributes only. */
static void init_colors(void) {
  static const struct {
    u8 style;
    i16 fg;
    i16 bg;
    i16 fg16;
    i16 bg16;
    attr_t attrs;
  } spec[] = {
      { STYLE_DIM, 241, -1, 8, -1, A_DIM },
      { STYLE_EMPTY, 243, -1, 8, -1, A_DIM },
      { STYLE_LABEL, 108, -1, COLOR_CYAN, -1, A_BOLD },
      { STYLE_ACCENT, 214, -1, COLOR_YELLOW, -1, A_BOLD },
      { STYLE_BORDER, 239, -1, 8, -1, A_DIM },
      { STYLE_BORDER_FOCUS, 214, -1, COLOR_YELLOW, -1, A_BOLD },
      { STYLE_BORDER_ERROR, 167, -1, COLOR_RED, -1, A_BOLD },
      { STYLE_GUTTER, 214, -1, COLOR_YELLOW, -1, A_BOLD },
      { STYLE_PILL, 235, 214, COLOR_BLACK, COLOR_YELLOW, A_BOLD | A_REVERSE },
      { STYLE_PILL_ERROR, 235, 167, COLOR_BLACK, COLOR_RED, A_BOLD | A_REVERSE },
      { STYLE_STATUS, 223, 237, -1, 8, A_REVERSE },
      { STYLE_STATUS_FAINT, 245, 237, COLOR_WHITE, 8, A_REVERSE },
      { STYLE_STATUS_INFO, 223, 239, COLOR_BLACK, COLOR_WHITE, A_REVERSE },
      { STYLE_STATUS_TIME, 235, 108, COLOR_BLACK, COLOR_CYAN, A_REVERSE },
      { STYLE_STATUS_ERROR, 167, 237, COLOR_RED, 8, A_BOLD | A_REVERSE },
      { STYLE_ERROR, 235, 167, COLOR_BLACK, COLOR_RED, A_BOLD | A_REVERSE },
      { STYLE_HELP_KEY, 246, -1, COLOR_WHITE, -1, 0 },
      { STYLE_HELP, 241, -1, 8, -1, A_DIM },
      { STYLE_HELP_SEP, 238, -1, 8, -1, A_DIM },
      { STYLE_STRING, COLOR_GREEN, -1, COLOR_GREEN, -1, 0 },
      { STYLE_KEY, COLOR_BLUE, -1, COLOR_BLUE, -1, A_BOLD },
      { STYLE_PUNCT, -1, -1, -1, -1, A_BOLD },
      { STYLE_NULL, 8, -1, 8, -1, A_DIM },
      { STYLE_VARIABLE, COLOR_MAGENTA, -1, COLOR_MAGENTA, -1, 0 },
      { STYLE_KEYWORD, COLOR_YELLOW, -1, COLOR_YELLOW, -1, A_BOLD },
      { STYLE_BUILTIN, COLOR_CYAN, -1, COLOR_CYAN, -1, 0 },
      { STYLE_OPERATOR, COLOR_YELLOW, -1, COLOR_YELLOW, -1, 0 },
      { STYLE_FORMAT, COLOR_MAGENTA, -1, COLOR_MAGENTA, -1, A_BOLD },
      { STYLE_COMMENT, 8, -1, 8, -1, A_DIM },
  };
  const char* no_color = getenv("NO_COLOR");
  b8 rich;
  i16 fg[STYLE_COUNT];
  i16 bg[STYLE_COUNT];
  i16 pair = 1;
  i16 line_bg;

  for (usz i = 0; i < ARRAY_LEN(spec); i++) style_attrs[spec[i].style] = spec[i].attrs;

  for (i32 s = 0; s < STYLE_COUNT; s++) {
    selected_attrs[s] = style_attrs[s] | A_REVERSE;
    idle_attrs[s] = style_attrs[s];
  }

  if (!has_colors() || (no_color && *no_color) || COLORS < 16) return;

  start_color();
  use_default_colors();
  rich = COLORS >= 256;
  line_bg = rich ? 237 : 8;
  for (i32 s = 0; s < STYLE_COUNT; s++) fg[s] = bg[s] = -1;

  for (usz i = 0; i < ARRAY_LEN(spec); i++) {
    u8 st = spec[i].style;

    fg[st] = rich ? spec[i].fg : spec[i].fg16;
    bg[st] = rich ? spec[i].bg : spec[i].bg16;
    if (fg[st] < 0 && bg[st] < 0) continue;

    init_pair(pair, fg[st], bg[st]);
    style_attrs[st] = (style_attrs[st] & A_BOLD) | COLOR_PAIR(pair);
    pair++;
  }

  for (i32 s = 0; s < STYLE_COUNT; s++) {
    idle_attrs[s] = style_attrs[s];
    selected_attrs[s] = style_attrs[s];
    if (bg[s] >= 0 || fg[s] == line_bg) continue;

    init_pair(pair, fg[s], line_bg);
    selected_attrs[s] = (style_attrs[s] & ~A_COLOR) | COLOR_PAIR(pair);
    pair++;
  }

  idle_attrs[STYLE_GUTTER] = style_attrs[STYLE_HELP] & ~A_BOLD;
}

static b8 translate(i32 kind, wint_t ch, u32* key) {
  static const struct {
    i32 curses;
    u32 app;
  } keys[] = {
      { KEY_UP, APP_KEY_UP },
      { KEY_DOWN, APP_KEY_DOWN },
      { KEY_LEFT, APP_KEY_LEFT },
      { KEY_RIGHT, APP_KEY_RIGHT },
      { KEY_PPAGE, APP_KEY_PAGE_UP },
      { KEY_NPAGE, APP_KEY_PAGE_DOWN },
      { KEY_HOME, APP_KEY_HOME },
      { KEY_END, APP_KEY_END },
      { KEY_BTAB, APP_KEY_BACKTAB },
      { KEY_BACKSPACE, APP_KEY_BACKSPACE },
      { KEY_ENTER, APP_KEY_ENTER },
      { KEY_DC, APP_KEY_DELETE },
      { CTL_LEFT, APP_KEY_WORD_LEFT },
      { ALT_LEFT, APP_KEY_WORD_LEFT },
      { ALT_B, APP_KEY_WORD_LEFT },
      { CTL_RIGHT, APP_KEY_WORD_RIGHT },
      { ALT_RIGHT, APP_KEY_WORD_RIGHT },
      { ALT_F, APP_KEY_WORD_RIGHT },
      { ALT_BKSP, APP_KEY_WORD_BACKSPACE },
      { CTL_BKSP, APP_KEY_WORD_BACKSPACE },
      { KEY_F(1), APP_KEY_F1 },
  };

  if (kind == OK && (ch == '\r' || ch == '\n')) {
    *key = APP_KEY_ENTER;
    return 1;
  }

  if (kind == OK) {
    *key = ch == 0x7f || ch == 0x08 ? APP_KEY_BACKSPACE : (u32)ch;
    return 1;
  }

  for (usz i = 0; i < ARRAY_LEN(keys); i++) {
    if ((i32)ch != keys[i].curses) continue;

    *key = keys[i].app;
    return 1;
  }

  return 0;
}

static b8 translate_mouse(app_event* ev) {
  MEVENT m;

  if (getmouse(&m) != OK) return 0;

  if (m.bstate & BUTTON1_PRESSED) ev->mouse = APP_MOUSE_PRESS;
  else if (m.bstate & BUTTON4_PRESSED) ev->mouse = APP_MOUSE_WHEEL_UP;
  else if (m.bstate & BUTTON5_PRESSED) ev->mouse = APP_MOUSE_WHEEL_DOWN;
  else return 0;

  ev->x = m.x;
  ev->y = m.y;
  return 1;
}

static void blit(const grid* g) {
  for (i32 y = 0; y < g->h && y < LINES; y++) {
    for (i32 x = 0; x < g->w && x < COLS; x++) {
      cell c = grid_at(g, x, y);
      u8 st = c.style & ~(STYLE_SELECTED | STYLE_SELECTED_IDLE);
      const attr_t* attrs = style_attrs;
      attr_t at;
      wchar_t wc[2];
      cchar_t cc;

      if (c.style & STYLE_SELECTED) attrs = selected_attrs;
      else if (c.style & STYLE_SELECTED_IDLE) attrs = idle_attrs;

      at = st < STYLE_COUNT ? attrs[st] : 0;

      wc[0] = sizeof(wchar_t) < 4 && c.ch > 0xffff ? 0xfffd : (wchar_t)c.ch;
      wc[1] = 0;
      setcchar(&cc, wc, at & ~A_COLOR, (i16)PAIR_NUMBER(at), NULL);
      mvadd_wch(y, x, &cc);
    }
  }

  if (cursor_shown != (g->cursor_x >= 0)) {
    cursor_shown = g->cursor_x >= 0;
    curs_set(cursor_shown ? CURSOR_BAR : 0);
  }

  if (g->cursor_x >= 0) move(g->cursor_y, g->cursor_x);

  refresh();
}
