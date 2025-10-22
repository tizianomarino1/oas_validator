#ifndef JSONSCHEMA_H
#define JSONSCHEMA_H
#include <stdbool.h>
#include "cJSON.h"

typedef struct
{
  bool ok;
  char *error_msg;
} jsval_result;

typedef struct
{
  cJSON *oas_root; // per step futuri ($ref/components)
} jsval_ctx;

jsval_ctx jsval_ctx_make(cJSON *oas_root);
void jsval_result_free(jsval_result *r);

// Validatore ricorsivo base per subset OAS Schema Object.
jsval_result js_validate(cJSON *instance, cJSON *schema, const jsval_ctx *ctx);

#endif
