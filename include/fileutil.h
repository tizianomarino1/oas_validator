#ifndef FILEUTIL_H
#define FILEUTIL_H
#include <stddef.h>
// Legge completamente il file in `path`, restituisce buffer terminato da NUL.
// Scrive in `out_len` la dimensione (se non NULL). Restituisce NULL su errore.
char *read_entire_file(const char *path, size_t *out_len);
#endif
