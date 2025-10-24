#ifndef REGEX_COMPAT_H
#define REGEX_COMPAT_H

#include <stdbool.h>

typedef struct regex_compat_result
{
  bool valid;
  bool matched;
} regex_compat_result;

regex_compat_result regex_compat_match(const char *pattern, const char *text);

#endif // REGEX_COMPAT_H
