/* livejq - runs `.` over a JSON file and prints the result. */
#include <stdio.h>
#include <string.h>

#include <curses.h>
#include <jq.h>

#include "defines.h"

static i32 curses_smoke(void);
static i32 run_identity(const char* path);

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "--curses-smoke") == 0)
    return curses_smoke();
  if (argc != 2) {
    fprintf(stderr, "usage: livejq <file>\n");
    return 2;
  }

  return run_identity(argv[1]);
}

static i32 curses_smoke(void) {
  initscr();
  printw("livejq: curses is up, press a key");
  refresh();
  getch();
  endwin();
  return 0;
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
