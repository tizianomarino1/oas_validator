#include "oas_extract.h"
#include <string.h>
#include <stdlib.h>

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

static void json_pointer_unescape(char *token)
{
  char *src = token;
  char *dst = token;
  while (*src)
  {
    if (*src == '~' && (src[1] == '0' || src[1] == '1'))
    {
      *dst = (src[1] == '0') ? '~' : '/';
      src += 2;
    }
    else
    {
      *dst = *src;
      ++src;
    }
    ++dst;
  }
  *dst = '\0';
}

static cJSON *resolve_ref(cJSON *oas_root, const char *ref)
{
  if (!cJSON_IsObject(oas_root) || !ref)
    return NULL;
  if (strncmp(ref, "#/", 2) != 0)
    return NULL;

  const char *path = ref + 2;
  size_t len = strlen(path);
  char *buffer = (char *)malloc(len + 1);
  if (!buffer)
    return NULL;
  memcpy(buffer, path, len + 1);

  cJSON *current = oas_root;
  char *token = buffer;
  while (token)
  {
    char *slash = strchr(token, '/');
    if (slash)
      *slash = '\0';

    json_pointer_unescape(token);

    if (!cJSON_IsObject(current))
    {
      free(buffer);
      return NULL;
    }

    cJSON *next = cJSON_GetObjectItemCaseSensitive(current, token);
    if (!next)
    {
      free(buffer);
      return NULL;
    }

    current = next;

    if (!slash)
      break;
    token = slash + 1;
  }

  free(buffer);
  return current;
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

cJSON *oas_request_body_schema(cJSON *oas_root, const char *http_method, const char *endpoint_path)
{
  if (!cJSON_IsObject(oas_root) || !http_method || !endpoint_path)
    return NULL;

  cJSON *paths = cJSON_GetObjectItemCaseSensitive(oas_root, "paths");
  if (!cJSON_IsObject(paths))
    return NULL;

  cJSON *path_item = cJSON_GetObjectItemCaseSensitive(paths, endpoint_path);
  if (!cJSON_IsObject(path_item))
    return NULL;

  cJSON *operation = cJSON_GetObjectItemCaseSensitive(path_item, http_method);
  if (!cJSON_IsObject(operation))
    return NULL;

  cJSON *request_body = cJSON_GetObjectItemCaseSensitive(operation, "requestBody");
  if (!cJSON_IsObject(request_body))
    return NULL;

  cJSON *content = cJSON_GetObjectItemCaseSensitive(request_body, "content");
  if (cJSON_IsObject(content))
  {
    return first_schema_in_content(content);
  }

  cJSON *ref = cJSON_GetObjectItemCaseSensitive(request_body, "$ref");
  if (cJSON_IsString(ref))
  {
    cJSON *resolved = resolve_ref(oas_root, ref->valuestring);
    if (cJSON_IsObject(resolved))
    {
      cJSON *resolved_content = cJSON_GetObjectItemCaseSensitive(resolved, "content");
      if (cJSON_IsObject(resolved_content))
        return first_schema_in_content(resolved_content);
    }
  }

  return NULL;
}
