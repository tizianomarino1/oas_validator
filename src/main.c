// Uso: openapi_validator <request.json> <openapi.json>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fileutil.h"
#include "jsonschema.h"
#include "oas_extract.h"
#include "cJSON.h"
#include "miniyaml.h"

// Stampa su stderr la sintassi corretta del programma.
static void print_usage(const char *prog) {
    fprintf(stderr, "Uso: %s <request.json> <openapi.json>\n", prog);
}

// Restituisce un puntatore al primo carattere non spazio/tab/newline della stringa.
static const char* ltrim(const char *s) {
    while (*s==' '||*s=='\t'||*s=='\r'||*s=='\n') ++s;
    return s;
}

// Punto di ingresso del validatore: carica i file, gestisce JSON/YAML e
// avvia la validazione restituendo 0 se il payload è conforme allo schema.
int main(int argc, char **argv) {
    if (argc != 3) { print_usage(argv[0]); return 2; }

    size_t json_len=0, oas_len=0;
    char *json_body = read_entire_file(argv[1], &json_len);
    if (!json_body) return 1;
    char *oas_spec  = read_entire_file(argv[2], &oas_len);
    if (!oas_spec) { free(json_body); return 1; }

    cJSON *inst = cJSON_ParseWithLength(json_body, (int)json_len);
    if (!inst) {
        fprintf(stderr, "Errore: JSON body non valido.\n");
        free(json_body); free(oas_spec);
        return 4;
    }
    const char *oas_trim = ltrim(oas_spec);
    cJSON *oas = NULL;
    if (oas_trim[0] == '{' || oas_trim[0] == '[') {
        oas = cJSON_ParseWithLength(oas_spec, (int)oas_len);
        if (!oas) {
            fprintf(stderr, "Errore: OpenAPI JSON non valido.\n");
            cJSON_Delete(inst); free(json_body); free(oas_spec);
            return 5;
        }
    } else {
        char *yaml_error = NULL;
        oas = miniyaml_parse(oas_spec, &yaml_error);
        if (!oas) {
            fprintf(stderr, "Errore: OpenAPI YAML non valido%s%s\n",
                    yaml_error ? ": " : "",
                    yaml_error ? yaml_error : "");
            free(yaml_error);
            cJSON_Delete(inst); free(json_body); free(oas_spec);
            return 5;
        }
        free(yaml_error);
    }

    // check openapi 3.x minimale
    cJSON *openapi = cJSON_GetObjectItemCaseSensitive(oas, "openapi");
    if (!cJSON_IsString(openapi) || strncmp(openapi->valuestring, "3.", 2)!=0) {
        fprintf(stderr, "Errore: 'openapi' non è 3.x.\n");
        cJSON_Delete(inst); cJSON_Delete(oas);
        free(json_body); free(oas_spec);
        return 6;
    }

    // estrai il primo schema di requestBody application/json
    cJSON *schema = oas_first_request_body_schema(oas);
    if (!schema) {
        fprintf(stderr, "Errore: impossibile trovare requestBody application/json->schema.\n");
        cJSON_Delete(inst); cJSON_Delete(oas);
        free(json_body); free(oas_spec);
        return 7;
    }

    // valida
    jsval_ctx ctx = jsval_ctx_make(oas); // per futuro: $ref/components
    jsval_result res = js_validate(inst, schema, &ctx);

    if (res.ok) {
        printf("OK");
    } else {
        printf("NON VALIDO - Motivo: %s\n", res.error_msg ? res.error_msg : "(sconosciuto)");
    }

    jsval_result_free(&res);
    cJSON_Delete(inst);
    cJSON_Delete(oas);
    free(json_body);
    free(oas_spec);
    return res.ok ? 0 : 1;
}
