/* livejq - runs `.` over a JSON file and prints the result. */
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <curses.h>
#include <jq.h>

#include "defines.h"

static i32 probe(void);
static void probe_palette(void);
static void probe_log(i32 row, const char* fmt, ...);
static i32 run_identity(const char* path);

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "--probe") == 0)
    return probe();
  if (argc != 2) {
    fprintf(stderr, "usage: livejq <file>\n");
    return 2;
  }

  return run_identity(argv[1]);
}

static i32 probe(void) {
  i32 row = 0;
  wint_t ch;

  setlocale(LC_ALL, "");
  initscr();
  raw();
  noecho();
  keypad(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  mouseinterval(0);
  start_color();
  use_default_colors();
  probe_palette();

  for (;;) {
    i32 kind = get_wch(&ch);
    i32 top = 7;
    i32 lines = LINES - top;

    if (lines < 1) lines = 1;

    if (kind == ERR) continue;

    if (kind == KEY_CODE_YES && ch == KEY_RESIZE) {
      resize_term(0, 0);
      erase();
      probe_palette();
      row = 0;
      probe_log(top, "resize -> %dx%d", COLS, LINES);
    } else if (kind == KEY_CODE_YES && ch == KEY_MOUSE) {
      MEVENT ev;

      if (getmouse(&ev) == OK) {
        probe_log(
          top + row,
          "mouse x=%d y=%d bstate=%#lx",
          ev.x, ev.y,
          (unsigned long)ev.bstate
        );
      } else
        probe_log(top + row, "mouse (getmouse failed)");
    } else if (kind == KEY_CODE_YES) {
      probe_log(top + row, "key %#x %s", (u32)ch, keyname((i32)ch));
    } else {
      probe_log(top + row, "char U+%04X %s", (u32)ch, key_name((wchar_t)ch));
      if (ch == 'q')
        break;
    }

    row = (row + 1) % lines;
    refresh();
  }

  endwin();
  return 0;
}

static void probe_palette(void) {
  static const char* wide = "wide: \u00e8 \u4e2d\u6587 \U0001F600 |";

  mvprintw(
    0, 0,
    "PDCursesMod probe, q quits. COLORS=%d PAIRS=%d %dx%d",
    COLORS,
    COLOR_PAIRS,
    COLS, LINES
  );
  mvprintw(1, 0, "%s", "16 colors:");
  for (i32 i = 0; i < 16 && i < COLORS; i++) {
    init_extended_pair(i + 1, COLOR_BLACK, i);
    attr_set(A_NORMAL, (i16)(i + 1), NULL);
    printw("  ");
  }

  attr_set(A_NORMAL, 0, NULL);
  mvprintw(2, 0, "%s", "256 cube:  ");
  for (i32 i = 0; i < 48 && COLORS >= 256; i++) {
    init_extended_pair(i + 17, COLOR_BLACK, 16 + i * 4);
    attr_set(A_NORMAL, (i16)(i + 17), NULL);
    printw(" ");
  }

  attr_set(A_NORMAL, 0, NULL);
  mvprintw(3, 0, "%s", "rgb ramp:  ");
  for (i32 i = 0; i < 48 && COLORS > 256; i++) {
    i32 color = 256 + i;

    init_extended_color(color, i * 1000 / 47, 0, 1000 - i * 1000 / 47);
    init_extended_pair(i + 65, COLOR_BLACK, color);
    attr_set(A_NORMAL, (i16)(i + 65), NULL);
    printw(" ");
  }

  attr_set(A_NORMAL, 0, NULL);
  mvaddstr(4, 0, wide);
  mvaddstr(
    5, 0,
    "try: arrows, shift/ctrl/alt+arrows, home/end, pgup/pgdn, "
    "tab, shift-tab, ctrl-space, ctrl-b/s, esc, click, wheel, resize"
  );
  refresh();
}

static void probe_log(i32 row, const char* fmt, ...) {
  va_list ap;

  move(row, 0);
  clrtoeol();
  va_start(ap, fmt);
  vw_printw(stdscr, fmt, ap);
  va_end(ap);
}

static i32 run_identity(const char* path) {
  jq_state* jq = jq_init();
  jv data;
  i32 rc = 0;

  if (!jq) {
    fprintf(stderr, "livejq: cannot initialize jq\n");
    return 1;
  }

  if (!jq_compile(jq, ".")) {
    jq_teardown(&jq);
    return 1;
  }

  /* jv_load_file yields an array of every JSON value in the file. */
  data = jv_load_file(path, 0);
  if (!jv_is_valid(data)) {
    jv msg = jv_invalid_get_msg(data);
    fprintf(stderr, "livejq: %s\n", jv_string_value(msg));
    jv_free(msg);
    jq_teardown(&jq);
    return 1;
  }

  for (i32 i = 0; i < jv_array_length(jv_copy(data)); i++) {
    jv result;

    jq_start(jq, jv_array_get(jv_copy(data), i), 0);
    while (jv_is_valid(result = jq_next(jq))) {
      jv_dumpf(result, stdout, JV_PRINT_PRETTY | JV_PRINT_SPACE1);
      putchar('\n');
    }

    if (jv_invalid_has_msg(jv_copy(result))) {
      jv msg = jv_invalid_get_msg(result);
      fprintf(stderr, "livejq: %s\n", jv_string_value(msg));
      jv_free(msg);
      rc = 5;
    } else {
      jv_free(result);
    }
  }

  jv_free(data);
  jq_teardown(&jq);
  return rc;
}
