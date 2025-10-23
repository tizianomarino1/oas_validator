#include "oas_extract.h"
#include <string.h>

// Cerca il primo schema JSON all'interno della sezione "content" di un requestBody.
// Restituisce il nodo schema o NULL se assente/non valido.
static cJSON *first_schema_in_content(cJSON *content)
{
  cJSON *appjson = cJSON_GetObjectItemCaseSensitive(content, "application/json");
  if (!cJSON_IsObject(appjson))
    return NULL;
  cJSON *media = cJSON_GetObjectItemCaseSensitive(appjson, "schema");
  return cJSON_IsObject(media) ? media : NULL;
}

// Ispeziona il documento OpenAPI e restituisce il primo schema JSON associato
// a un requestBody application/json. Restituisce NULL se non viene trovato.
cJSON *oas_first_request_body_schema(cJSON *oas_root)
{
  if (!cJSON_IsObject(oas_root))
    return NULL;
  cJSON *paths = cJSON_GetObjectItemCaseSensitive(oas_root, "paths");
  if (!cJSON_IsObject(paths))
    return NULL;

  // paths -> { "/x": { "get": {...}, "post": {...}, ... }, ...}
  cJSON *path_it = NULL;
  cJSON_ArrayForEach(path_it, paths)
  {
    if (!cJSON_IsObject(path_it))
      continue;

    // per ciascuna operation
    const char *ops[] = {"post", "put", "patch", "get", "delete", "options", "head", "trace"};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i)
    {
      cJSON *op = cJSON_GetObjectItemCaseSensitive(path_it, ops[i]);
      if (!cJSON_IsObject(op))
        continue;

      cJSON *rb = cJSON_GetObjectItemCaseSensitive(op, "requestBody");
      if (!cJSON_IsObject(rb))
        continue;

      // requestBody -> (direct) content / o $ref (non gestito ora)
      cJSON *content = cJSON_GetObjectItemCaseSensitive(rb, "content");
      if (cJSON_IsObject(content))
      {
        cJSON *schema = first_schema_in_content(content);
        if (schema)
          return schema;
      }
    }
  }
  return NULL;
}
