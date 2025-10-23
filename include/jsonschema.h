#ifndef JSONSCHEMA_H
#define JSONSCHEMA_H
#include <stdbool.h>
#include "cJSON.h"

// Risultato della validazione: `ok` indica successo, `error_msg` contiene
// il motivo del fallimento (heap-allocated) quando `ok` è false.
typedef struct
{
  bool ok;
  char *error_msg;
} jsval_result;

// Modalità di validazione disponibili.
typedef enum
{
  JSVAL_MODE_STRICT,
  JSVAL_MODE_LEXICAL
} jsval_mode;

// Contesto di validazione: consente l'accesso alla radice del documento OAS
// per future estensioni (es. risoluzione di $ref/components) e conserva la
// modalità richiesta.
typedef struct
{
  cJSON *oas_root; // per step futuri ($ref/components)
  jsval_mode mode;
} jsval_ctx;

// Inizializza un contesto di validazione partendo dal nodo radice OAS.
jsval_ctx jsval_ctx_make(cJSON *oas_root, jsval_mode mode);
// Libera le risorse allocate all'interno di un jsval_result (se presenti).
void jsval_result_free(jsval_result *r);

// Validatore ricorsivo base per subset OAS Schema Object.
jsval_result js_validate(cJSON *instance, cJSON *schema, const jsval_ctx *ctx);

#endif
