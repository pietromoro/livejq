/* tui - curses in, core events out, the core's grid drawn back. */
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include <curses.h>

#include "app.h"
#include "thread.h"
#include "tui.h"

#define TICK_MS    30
#define KEY_CTRL_C 3

static void init_colors(void);
static b8 translate(i32 kind, wint_t ch, u32* key);
static void blit(const grid* g);

static attr_t style_attrs[STYLE_COUNT];

/* PDCursesMod's vt port switches screens with ?47, which some terminals
 * ignore, and on endwin moves the cursor to the bottom row and forces a
 * block cursor. ?1049 around it saves and restores the cursor and screen. */
void tui_enter(void) {
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
}

i32 tui_run(const char* path) {
  app_opts o;
  char err[256];
  app* a;
  grid* g;

  memset(&o, 0, sizeof(o));
  o.path = path;
  o.args = jv_invalid();
  a = app_new(&o, err, sizeof(err));
  if (!a) {
    fprintf(stderr, "livejq: %s\n", err);
    return 1;
  }

  setlocale(LC_ALL, "");
  tui_enter();
  raw();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(TICK_MS);
  init_colors();
  g = grid_new(COLS, LINES);

  for (;;) {
    app_event ev;
    wint_t ch;
    i32 kind;

    app_render(a, g);
    blit(g);

    kind = get_wch(&ch);
    ev.kind = EV_TICK;
    ev.key = 0;
    ev.now_ms = clock_us() / 1000;
    if (kind == KEY_CODE_YES && ch == KEY_RESIZE) {
      resize_term(0, 0);
      grid_free(g);
      g = grid_new(COLS, LINES);
      clear();
    } else if (kind != ERR) {
      if (kind == OK && ch == KEY_CTRL_C) break;
      if (kind == OK && ch == 'q' && app_focused(a) == FOCUS_TREE) break;
      if (translate(kind, ch, &ev.key)) ev.kind = EV_KEY;
    }

    app_feed(a, ev);
  }

  tui_leave();
  grid_free(g);
  app_free(a);
  return 0;
}

/* jq's palette: gray null, green strings, bold punctuation and keys. */
static void init_colors(void) {
  static const struct {
    u8 style;
    i16 fg;
    attr_t attrs;
  } spec[] = {
      { STYLE_LABEL, COLOR_CYAN, A_BOLD },
      { STYLE_ERROR, COLOR_RED, A_BOLD | A_REVERSE },
      { STYLE_STRING, COLOR_GREEN, 0 },
      { STYLE_KEY, COLOR_BLUE, A_BOLD },
  };

  style_attrs[STYLE_DIM] = A_DIM;
  style_attrs[STYLE_EMPTY] = A_DIM;
  style_attrs[STYLE_STATUS] = A_REVERSE;
  style_attrs[STYLE_ERROR] = A_BOLD | A_REVERSE;
  style_attrs[STYLE_PUNCT] = A_BOLD;
  style_attrs[STYLE_NULL] = A_DIM;
  if (!has_colors()) return;

  start_color();
  use_default_colors();
  for (usz i = 0; i < ARRAY_LEN(spec); i++) {
    init_pair((i16)(i + 1), spec[i].fg, -1);
    style_attrs[spec[i].style] = spec[i].attrs | COLOR_PAIR(i + 1);
  }

  if (COLORS < 16) return;

  init_pair((i16)(ARRAY_LEN(spec) + 1), 8, -1);
  style_attrs[STYLE_NULL] = COLOR_PAIR(ARRAY_LEN(spec) + 1);
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
  };

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

static void blit(const grid* g) {
  for (i32 y = 0; y < g->h && y < LINES; y++) {
    for (i32 x = 0; x < g->w && x < COLS; x++) {
      cell c = grid_at(g, x, y);
      u8 st = c.style & ~STYLE_SELECTED;
      attr_t at = st < STYLE_COUNT ? style_attrs[st] : 0;
      wchar_t wc[2];
      cchar_t cc;

      if (c.style & STYLE_SELECTED) at |= A_REVERSE;

      wc[0] = sizeof(wchar_t) < 4 && c.ch > 0xffff ? 0xfffd : (wchar_t)c.ch;
      wc[1] = 0;
      setcchar(&cc, wc, at & ~A_COLOR, (i16)PAIR_NUMBER(at), NULL);
      mvadd_wch(y, x, &cc);
    }
  }

  refresh();
}
