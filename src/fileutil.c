#include "fileutil.h"

#if !defined(_MSC_VER)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _MSC_VER
#define FSEEK _fseeki64
#define FTELL _ftelli64
#else
#define FSEEK fseeko
#define FTELL ftello
#endif

// Legge l'intero contenuto di un file binario e restituisce un buffer
// terminato da NUL. In caso di successo popola `out_len` (se non NULL)
// con il numero di byte letti. In caso di errore stampa un messaggio
// su stderr e restituisce NULL.
char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Errore aprendo '%s': %s\n", path, strerror(errno)); return NULL; }
    if (FSEEK(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long long size = FTELL(f);
    if (size < 0) { fclose(f); return NULL; }
    if (FSEEK(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) { free(buf); return NULL; }
    buf[size] = '\0';
    if (out_len) *out_len = (size_t)size;
    return buf;
}
