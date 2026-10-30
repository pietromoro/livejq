/* livejq - the application core: events in, a rendered grid out. */
#ifndef LIVEJQ_APP_H
#define LIVEJQ_APP_H

#include <jv.h>

#include "defines.h"
#include "grid.h"

/* keys that are not text sit above the unicode range. */
enum {
  APP_KEY_BACKSPACE = 0x110000,
  APP_KEY_BACKTAB,
  APP_KEY_UP,
  APP_KEY_DOWN,
  APP_KEY_LEFT,
  APP_KEY_RIGHT,
  APP_KEY_PAGE_UP,
  APP_KEY_PAGE_DOWN,
  APP_KEY_HOME,
  APP_KEY_END,
  APP_KEY_ENTER,
};

typedef enum {
  EV_KEY = 0,
  EV_TICK,
  EV_FILE_CHANGED, /* reads the file again and reruns the query */
} app_event_kind;

typedef enum {
  FOCUS_QUERY = 0,
  FOCUS_TREE,
} app_focus;

typedef struct {
  app_event_kind kind;
  u32 key;
  i64 now_ms;
} app_event;

typedef struct {
  const char* path;
  const char* query; /* default "." */
  b8 raw;
  b8 slurp;
  jv args; /* object of $name bindings, owned by the app */
  usz cap; /* default 10000 */
} app_opts;

typedef struct app app;

/* NULL when the file cannot be read, with the reason in err. */
app* app_new(const app_opts* opts, char* err, usz errlen);
void app_free(app* a);
void app_feed(app* a, app_event ev);
/* Runs a pending edit without waiting for the debounce, then waits for its
 * result. False if it is still running after timeout_ms. */
b8 app_settle(app* a, i64 timeout_ms);
void app_render(app* a, grid* g);
app_focus app_focused(const app* a);

#endif /* LIVEJQ_APP_H */
