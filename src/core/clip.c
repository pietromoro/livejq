/* clip - the clipboard escape sequence. */
#include <stdlib.h>
#include <string.h>

#include "clip.h"

#define HEAD "\033]52;c;"
#define TAIL "\a"

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* clip_osc52(const char* s, usz len) {
  usz head = strlen(HEAD);
  char* out = malloc(head + (len + 2) / 3 * 4 + strlen(TAIL) + 1);
  const u8* in = (const u8*)s;
  usz n = head;

  if (!out) abort();

  memcpy(out, HEAD, head);
  for (usz i = 0; i < len; i += 3) {
    u32 bits = (u32)in[i] << 16;

    if (i + 1 < len) bits |= (u32)in[i + 1] << 8;
    if (i + 2 < len) bits |= in[i + 2];

    out[n++] = alphabet[bits >> 18 & 63];
    out[n++] = alphabet[bits >> 12 & 63];
    out[n++] = i + 1 < len ? alphabet[bits >> 6 & 63] : '=';
    out[n++] = i + 2 < len ? alphabet[bits & 63] : '=';
  }

  strcpy(out + n, TAIL);
  return out;
}
