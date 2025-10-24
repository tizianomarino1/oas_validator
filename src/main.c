// Uso: openapi_validator <request.json> <openapi.json> <http-method> <endpoint> [strict-rule|lexical-rule]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fileutil.h"
#include "jsonschema.h"
#include "oas_extract.h"
#include "cJSON.h"
#include "miniyaml.h"

// Stampa su stderr la sintassi corretta del programma.
static void print_usage(const char *prog) {
    fprintf(stderr, "Uso: %s <request.(json|yaml)> <openapi.(json|yaml)> <http-method> <endpoint> [strict-rule|lexical-rule]\n", prog);
}

// Restituisce un puntatore al primo carattere non spazio/tab/newline della stringa.
static const char* ltrim(const char *s) {
    while (*s==' '||*s=='\t'||*s=='\r'||*s=='\n') ++s;
    return s;
}

static char* lowercase_dup(const char *s) {
    size_t len = strlen(s);
    char *dup = (char*)malloc(len + 1);
    if (!dup) return NULL;
    for (size_t i = 0; i < len; ++i) {
        dup[i] = (char)tolower((unsigned char)s[i]);
    }
    dup[len] = '\0';
    return dup;
}

// Punto di ingresso del validatore: carica i file, gestisce JSON/YAML e
// avvia la validazione restituendo 0 se il payload è conforme allo schema.
int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) { print_usage(argv[0]); return 2; }

    jsval_mode mode = JSVAL_MODE_STRICT;
    if (argc == 6) {
        if (strcmp(argv[5], "strict-rule") == 0) {
            mode = JSVAL_MODE_STRICT;
        } else if (strcmp(argv[5], "lexical-rule") == 0) {
            mode = JSVAL_MODE_LEXICAL;
        } else {
            fprintf(stderr, "Errore: modalità sconosciuta '%s'.\n", argv[5]);
            print_usage(argv[0]);
            return 2;
        }
    }

    size_t json_len=0, oas_len=0;
    char *json_body = read_entire_file(argv[1], &json_len);
    if (!json_body) return 1;
    char *oas_spec  = read_entire_file(argv[2], &oas_len);
    if (!oas_spec) { free(json_body); return 1; }

    const char *http_method_arg = argv[3];
    const char *endpoint_arg = argv[4];

    const char *body_trim = ltrim(json_body);
    cJSON *inst = NULL;
    if (body_trim[0] == '{' || body_trim[0] == '[') {
        inst = cJSON_ParseWithLength(json_body, (int)json_len);
        if (!inst) {
            fprintf(stderr, "Errore: JSON body non valido.\n");
            free(json_body); free(oas_spec);
            return 4;
        }
    } else {
        char *yaml_error = NULL;
        inst = miniyaml_parse(json_body, &yaml_error);
        if (!inst) {
            fprintf(stderr, "Errore: YAML body non valido%s%s\n",
                    yaml_error ? ": " : "",
                    yaml_error ? yaml_error : "");
            free(yaml_error);
            free(json_body); free(oas_spec);
            return 4;
        }
        free(yaml_error);
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

    char *method_lower = lowercase_dup(http_method_arg);
    if (!method_lower) {
        fprintf(stderr, "Errore: memoria insufficiente per elaborare il metodo HTTP.\n");
        cJSON_Delete(inst); cJSON_Delete(oas);
        free(json_body); free(oas_spec);
        return 8;
    }

    cJSON *schema = oas_request_body_schema(oas, method_lower, endpoint_arg);
    free(method_lower);
    if (!schema) {
        fprintf(stderr, "Errore: impossibile trovare requestBody application/json->schema per %s %s.\n", http_method_arg, endpoint_arg);
        cJSON_Delete(inst); cJSON_Delete(oas);
        free(json_body); free(oas_spec);
        return 7;
    }

    // valida
    jsval_ctx ctx = jsval_ctx_make(oas, mode); // per futuro: $ref/components e modalità
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
