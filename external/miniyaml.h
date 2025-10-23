#ifndef MINIYAML_H
#define MINIYAML_H

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a YAML document into a cJSON structure.
 *
 * The parser is intentionally small and only supports a subset of YAML 1.2
 * that is sufficient for typical OpenAPI documents: mappings, sequences,
 * scalars (strings, numbers, booleans and null), inline JSON objects/arrays
 * and comments.
 *
 * On success a newly allocated cJSON node is returned. On error NULL is
 * returned and error_msg (if non-NULL) is set to a malloc'ed string that must
 * be freed by the caller with free().
 */
cJSON *miniyaml_parse(const char *input, char **error_msg);

#ifdef __cplusplus
}
#endif

#endif /* MINIYAML_H */
