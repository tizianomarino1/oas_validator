#ifndef OAS_EXTRACT_H
#define OAS_EXTRACT_H
#include "cJSON.h"

// Ritorna lo schema del primo requestBody application/json trovato.
// (Borrowed pointer: non fare cJSON_Delete su questo.)
cJSON *oas_first_request_body_schema(cJSON *oas_root);

#endif
