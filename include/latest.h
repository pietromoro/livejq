/* livejq - the newest file of a folder whose name matches a pattern. */
#ifndef LIVEJQ_LATEST_H
#define LIVEJQ_LATEST_H

#include "defines.h"
#include "loader.h"

/* True when name matches pattern, where * stands for any run of characters
 * and ? for any one. Case-insensitive on Windows. */
b8 latest_match(const char* pattern, const char* name);

/* The path of the regular file in dir that matches pattern and was modified
 * last, the name sorting last on a tie, to free. NULL when the folder cannot
 * be listed or nothing matches, with the reason in err. */
char* latest_find(const char* dir, const char* pattern, file_stamp* stamp, char* err, usz errlen);

#endif /* LIVEJQ_LATEST_H */
