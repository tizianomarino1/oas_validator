#ifndef FILEUTIL_H
#define FILEUTIL_H
#include <stddef.h>
char *read_entire_file(const char *path, size_t *out_len);
#endif
