/* term - escape sequences sent past curses, straight to the terminal. */
#include <string.h>

#include "term.h"

/* curufmt variant */
#ifdef _WIN32

#include <windows.h>

#define VT_OUTPUT (ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)

/* CONOUT$ is the screen buffer shown, the one curses draws in. the console
 * passes a sequence on only while it processes them, which curses keeps
 * off between its own writes. */
void term_write(const char* s) {
  HANDLE out = CreateFileA(
    "CONOUT$",
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL,
    OPEN_EXISTING,
    0,
    NULL
  );
  DWORD mode;

  if (out == INVALID_HANDLE_VALUE) return;

  if (GetConsoleMode(out, &mode) && SetConsoleMode(out, mode | VT_OUTPUT)) {
    WriteConsoleA(out, s, (DWORD)strlen(s), NULL, NULL);
    SetConsoleMode(out, mode);
  }

  CloseHandle(out);
}

#else

#include <stdio.h>

void term_write(const char* s) {
  fputs(s, stdout);
  fflush(stdout);
}

#endif
