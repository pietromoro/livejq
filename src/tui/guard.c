/* guard - restores the terminal from a signal handler or atexit. */
#define _POSIX_C_SOURCE 200809L

#include "guard.h"

/* curufmt variant */
#ifdef _WIN32

void guard_arm(void) {}

void guard_disarm(void) {}

#else

#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "defines.h"

/* mouse modes off, default attributes, cursor shown and reset, both
 * screen switches undone. */
#define RESTORE \
   "\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[0m\033[?25h\033[?47l\033[?1049l\033[0 q"

static void restore(void);
static void on_signal(i32 sig);

static const i32 fatal[] = { SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
static struct termios saved;
static volatile sig_atomic_t armed;
static b8 installed;

void guard_arm(void) {
  struct sigaction sa;

  if (tcgetattr(STDIN_FILENO, &saved) != 0) return;

  armed = 1;
  if (installed) return;

  installed = 1;
  atexit(restore);
  sa.sa_handler = on_signal;
  sa.sa_flags = SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  for (usz i = 0; i < ARRAY_LEN(fatal); i++) sigaction(fatal[i], &sa, NULL);
}

void guard_disarm(void) { armed = 0; }

/* only async-signal-safe calls. */
static void restore(void) {
  if (!armed) return;

  armed = 0;
  if (write(STDOUT_FILENO, RESTORE, sizeof(RESTORE) - 1) < 0) {}

  tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

/* the handler was reset on entry, so the signal raised again is delivered
 * with its default action once this returns. */
static void on_signal(i32 sig) {
  restore();
  raise(sig);
}

#endif
