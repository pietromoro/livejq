/* livejq - the saved queries, one .query file each in a folder. */
#ifndef LIVEJQ_STORE_H
#define LIVEJQ_STORE_H

#include "defines.h"
#include "loader.h"

typedef struct store store;

/* NULL when the folder cannot be listed, with the reason in err. */
store* store_open(const char* dir, char* err, usz errlen);
void store_free(store* s);
/* Lists the folder again. Returns true when a name or a file's stamp
 * changed; on failure the last list is kept. */
b8 store_rescan(store* s);
/* Names are sorted, case-insensitively first. */
usz store_count(const store* s);
const char* store_name(const store* s, usz i);
file_stamp store_stamp(const store* s, usz i);
/* The index of name, or -1. */
i32 store_find(const store* s, const char* name);
/* True for a name that stays inside the folder and is valid on every
 * platform: not empty, no separators or reserved characters, no leading
 * dot, no trailing dot or space, not a device name. */
b8 store_name_ok(const char* name);
/* The query text, trailing newlines dropped, to free; NULL on failure. */
char* store_read(const store* s, const char* name, char* err, usz errlen);
/* Writes text plus a newline, then rescans. */
b8 store_write(store* s, const char* name, const char* text, char* err, usz errlen);
b8 store_delete(store* s, const char* name, char* err, usz errlen);

#endif /* LIVEJQ_STORE_H */
