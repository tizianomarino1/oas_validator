#ifndef OAS_EXTRACT_H
#define OAS_EXTRACT_H
#include "cJSON.h"

// Ritorna lo schema del primo requestBody application/json trovato
// navigando l'albero `paths` della specifica OpenAPI.
// (Borrowed pointer: non fare cJSON_Delete su questo.)
cJSON *oas_first_request_body_schema(cJSON *oas_root);

// Restituisce lo schema JSON associato al requestBody dell'endpoint indicato
// (method/path). L'oggetto ritornato è un puntatore preso in prestito dal DOM cJSON.
cJSON *oas_request_body_schema(cJSON *oas_root, const char *http_method, const char *endpoint_path);

#endif
