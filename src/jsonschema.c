#include "jsonschema.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>

// Restituisce un risultato di validazione positivo senza messaggio di errore.
static jsval_result ok(void) { return (jsval_result){true, NULL}; }

// Helper per creare un jsval_result di errore formattando il messaggio.
static jsval_result errf(const char *fmt, ...)
{
  jsval_result r = {false, NULL};
  va_list ap;
  va_start(ap, fmt);
  char buf[512];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  r.error_msg = (char *)malloc(strlen(buf) + 1);
  strcpy(r.error_msg, buf);
  return r;
}

// Libera l'eventuale messaggio di errore contenuto in un jsval_result.
void jsval_result_free(jsval_result *r)
{
  if (r && r->error_msg)
  {
    free(r->error_msg);
    r->error_msg = NULL;
  }
}

// Crea un contesto di validazione per future risoluzioni di $ref.
// Attualmente si limita a memorizzare il nodo radice dell'OAS.
jsval_ctx jsval_ctx_make(cJSON *oas_root)
{
  jsval_ctx c = {oas_root};
  return c;
}

// Estrae la stringa "type" da uno schema JSON (case insensitive).
static const char *get_type(cJSON *schema)
{
  cJSON *t = cJSON_GetObjectItemCaseSensitive(schema, "type");
  return cJSON_IsString(t) ? t->valuestring : NULL;
}

// Verifica che l'istanza `inst` corrisponda al tipo JSON Schema `t`.
// Se `t` è NULL la funzione accetta qualsiasi tipo.
static bool is_type(cJSON *inst, const char *t)
{
  if (!t)
    return true; // se assente, non imponiamo tipo
  if (strcmp(t, "object") == 0)
    return cJSON_IsObject(inst);
  if (strcmp(t, "array") == 0)
    return cJSON_IsArray(inst);
  if (strcmp(t, "string") == 0)
    return cJSON_IsString(inst);
  if (strcmp(t, "number") == 0)
    return cJSON_IsNumber(inst);
  if (strcmp(t, "integer") == 0)
    return cJSON_IsNumber(inst) && (inst->valuedouble == (double)inst->valueint);
  if (strcmp(t, "boolean") == 0)
    return cJSON_IsBool(inst);
  if (strcmp(t, "null") == 0)
    return cJSON_IsNull(inst);
  return true; // tipi non standard: ignora
}

// Valida la presenza (opzionale) di un vincolo "enum" nello schema.
// Restituisce errore se il valore non è presente nella lista enumerata.
static jsval_result validate_enum(cJSON *inst, cJSON *schema)
{
  cJSON *enm = cJSON_GetObjectItemCaseSensitive(schema, "enum");
  if (!cJSON_IsArray(enm))
    return ok();

  cJSON *it = NULL;
  cJSON_ArrayForEach(it, enm)
  {
    if ((cJSON_IsString(inst) && cJSON_IsString(it) && strcmp(inst->valuestring, it->valuestring) == 0) ||
        (cJSON_IsNumber(inst) && cJSON_IsNumber(it) && inst->valuedouble == it->valuedouble) ||
        (cJSON_IsBool(inst) && cJSON_IsBool(it) && !!inst->valueint == !!it->valueint))
    {
      return ok();
    }
  }
  return errf("Valore non incluso in 'enum'.");
}

// Applica i limiti minLength/maxLength per stringhe se definiti.
static jsval_result validate_string_bounds(cJSON *inst, cJSON *schema)
{
  if (!cJSON_IsString(inst))
    return ok();
  const char *s = inst->valuestring;
  cJSON *minL = cJSON_GetObjectItemCaseSensitive(schema, "minLength");
  cJSON *maxL = cJSON_GetObjectItemCaseSensitive(schema, "maxLength");
  if (cJSON_IsNumber(minL) && (int)strlen(s) < minL->valueint)
    return errf("Stringa più corta di minLength");
  if (cJSON_IsNumber(maxL) && (int)strlen(s) > maxL->valueint)
    return errf("Stringa più lunga di maxLength");
  return ok();
}

// Applica i limiti minimum/maximum per numeri se definiti nello schema.
static jsval_result validate_numeric_bounds(cJSON *inst, cJSON *schema)
{
  if (!cJSON_IsNumber(inst))
    return ok();
  cJSON *min = cJSON_GetObjectItemCaseSensitive(schema, "minimum");
  cJSON *max = cJSON_GetObjectItemCaseSensitive(schema, "maximum");
  if (cJSON_IsNumber(min) && inst->valuedouble < min->valuedouble)
    return errf("Numero < minimum");
  if (cJSON_IsNumber(max) && inst->valuedouble > max->valuedouble)
    return errf("Numero > maximum");
  return ok();
}

static jsval_result js_validate_impl(cJSON *inst, cJSON *schema, const jsval_ctx *ctx);

// Valida gli elementi di un array utilizzando ricorsivamente lo schema `items`.
static jsval_result validate_array(cJSON *inst, cJSON *schema, const jsval_ctx *ctx)
{
  cJSON *items = cJSON_GetObjectItemCaseSensitive(schema, "items");
  if (!items)
    return ok();
  if (!cJSON_IsArray(inst))
    return errf("Atteso array.");
  cJSON *el = NULL;
  cJSON_ArrayForEach(el, inst)
  {
    jsval_result r = js_validate_impl(el, items, ctx);
    if (!r.ok)
      return r;
  }
  return ok();
}

// Valida un oggetto JSON confrontando proprietà richieste e sotto-schemi.
static jsval_result validate_object(cJSON *inst, cJSON *schema, const jsval_ctx *ctx)
{
  if (!cJSON_IsObject(inst))
    return errf("Atteso object.");

  // required
  cJSON *req = cJSON_GetObjectItemCaseSensitive(schema, "required");
  if (cJSON_IsArray(req))
  {
    cJSON *r = NULL;
    cJSON_ArrayForEach(r, req)
    {
      if (cJSON_IsString(r))
      {
        if (!cJSON_HasObjectItem(inst, r->valuestring))
        {
          return errf("Campo richiesto mancante: '%s'", r->valuestring);
        }
      }
    }
  }

  // properties
  cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
  if (cJSON_IsObject(props))
  {
    cJSON *p = NULL;
    cJSON_ArrayForEach(p, props)
    {
      const char *name = p->string;
      cJSON *subschema = p;
      if (!name || !cJSON_IsObject(subschema))
        continue;

      cJSON *child = cJSON_GetObjectItemCaseSensitive(inst, name);
      if (child)
      {
        jsval_result r = js_validate_impl(child, subschema, ctx);
        if (!r.ok)
          return r;
      }
    }
  }
  return ok();
}

// Implementazione ricorsiva del validatore per un sottoalbero JSON.
static jsval_result js_validate_impl(cJSON *inst, cJSON *schema, const jsval_ctx *ctx)
{
  (void)ctx; // step 3: non usiamo ancora components/$ref

  // type
  const char *t = get_type(schema);
  if (t && !is_type(inst, t))
    return errf("Tipo non valido: atteso '%s'.", t);

  // enum / bounds
  jsval_result r;
  r = validate_enum(inst, schema);
  if (!r.ok)
    return r;
  r = validate_string_bounds(inst, schema);
  if (!r.ok)
    return r;
  r = validate_numeric_bounds(inst, schema);
  if (!r.ok)
    return r;

  // ricorsione su object/array
  if (t && strcmp(t, "object") == 0)
    return validate_object(inst, schema, ctx);
  if (t && strcmp(t, "array") == 0)
    return validate_array(inst, schema, ctx);

  // se nessun 'type', proviamo euristica: se schema ha 'properties' → object
  if (!t && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(schema, "properties")))
    return validate_object(inst, schema, ctx);

  return ok();
}

// Punto di ingresso pubblico per validare `instance` rispetto a `schema`.
// Il contesto permette future estensioni per la risoluzione di riferimenti.
jsval_result js_validate(cJSON *instance, cJSON *schema, const jsval_ctx *ctx)
{
  return js_validate_impl(instance, schema, ctx);
}
