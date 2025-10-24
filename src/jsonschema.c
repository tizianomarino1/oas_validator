#include "jsonschema.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#ifndef _MSC_VER
#include <regex.h>
#else
#include "regex_compat.h"
#endif

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
jsval_ctx jsval_ctx_make(cJSON *oas_root, jsval_mode mode)
{
  jsval_ctx c = {oas_root, mode};
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

// Applica il vincolo pattern per le stringhe se definito.
static jsval_result validate_string_pattern(cJSON *inst, cJSON *schema)
{
  if (!cJSON_IsString(inst))
    return ok();

  cJSON *pattern = cJSON_GetObjectItemCaseSensitive(schema, "pattern");
  if (!cJSON_IsString(pattern))
    return ok();

#ifdef _MSC_VER
  regex_compat_result re = regex_compat_match(pattern->valuestring, inst->valuestring);
  if (!re.valid)
    return errf("Pattern non valido nello schema.");
  if (re.matched)
    return ok();
  return errf("Stringa non conforme al pattern.");
#else
  regex_t re;
  int rc = regcomp(&re, pattern->valuestring, REG_EXTENDED | REG_NOSUB);
  if (rc != 0)
    return errf("Pattern non valido nello schema.");

  rc = regexec(&re, inst->valuestring, 0, NULL, 0);
  regfree(&re);
  if (rc == 0)
    return ok();
  return errf("Stringa non conforme al pattern.");
#endif
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

// Decodifica un token JSON Pointer sostituendo le sequenze ~0 e ~1.
static bool decode_pointer_token(const char *start, size_t len, char *out, size_t out_sz)
{
  if (!out || out_sz == 0)
    return false;
  size_t j = 0;
  for (size_t i = 0; i < len; ++i)
  {
    char c = start[i];
    if (c == '~' && i + 1 < len)
    {
      char next = start[i + 1];
      if (next == '0')
      {
        c = '~';
        ++i;
      }
      else if (next == '1')
      {
        c = '/';
        ++i;
      }
    }
    if (j + 1 >= out_sz)
      return false;
    out[j++] = c;
  }
  if (j >= out_sz)
    return false;
  out[j] = '\0';
  return true;
}

// Risolve un riferimento JSON Pointer limitato a riferimenti interni (#/...) dell'OAS.
static cJSON *resolve_ref(const char *ref, const jsval_ctx *ctx)
{
  if (!ref || !ctx || !ctx->oas_root)
    return NULL;
  if (strncmp(ref, "#/", 2) != 0)
    return NULL; // supportiamo solo riferimenti interni

  cJSON *node = ctx->oas_root;
  const char *p = ref + 2;
  while (*p)
  {
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (len == 0)
      return NULL;

    char token[256];
    if (!decode_pointer_token(p, len, token, sizeof(token)))
      return NULL;

    if (cJSON_IsArray(node))
    {
      char *endptr = NULL;
      long idx = strtol(token, &endptr, 10);
      if (!endptr || *endptr != '\0' || idx < 0)
        return NULL;
      node = cJSON_GetArrayItem(node, (int)idx);
    }
    else
    {
      node = cJSON_GetObjectItemCaseSensitive(node, token);
    }

    if (!node)
      return NULL;

    if (!slash)
      break;
    p = slash + 1;
  }
  return node;
}

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

// Applica i sotto-schemi di patternProperties alle chiavi che combaciano.
static jsval_result apply_pattern_properties_to_child(
    cJSON *child, cJSON *pattern_props, const jsval_ctx *ctx, bool *matched_out)
{
  if (matched_out)
    *matched_out = false;

  if (!cJSON_IsObject(pattern_props) || !child)
    return ok();

#ifndef _MSC_VER
  cJSON *pp = NULL;
  const char *prop_name = child->string ? child->string : "";
  cJSON_ArrayForEach(pp, pattern_props)
  {
    const char *pattern = pp->string;
    if (!pattern)
      continue;

    regex_t re;
    int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);
    if (rc != 0)
      return errf("Pattern non valido nello schema: '%s'.", pattern);

    rc = regexec(&re, prop_name, 0, NULL, 0);
    regfree(&re);
    if (rc == 0)
    {
      if (matched_out)
        *matched_out = true;

      if (cJSON_IsObject(pp) || cJSON_IsArray(pp))
      {
        jsval_result sub = js_validate_impl(child, pp, ctx);
        if (!sub.ok)
          return sub;
      }
      else if (cJSON_IsBool(pp))
      {
        if (cJSON_IsFalse(pp))
          return errf("Chiave '%s' non ammessa da patternProperties.", prop_name);
      }
    }
  }
#else
  (void)pattern_props;
  (void)ctx;
  (void)child;
#endif

  return ok();
}

// Valida un oggetto JSON confrontando proprietà richieste e sotto-schemi.
static jsval_result validate_object(cJSON *inst, cJSON *schema, const jsval_ctx *ctx)
{
  if (!cJSON_IsObject(inst))
    return errf("Atteso object.");

  // required
  if (!ctx || ctx->mode == JSVAL_MODE_STRICT)
  {
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

  cJSON *pattern_props = cJSON_GetObjectItemCaseSensitive(schema, "patternProperties");
  if (cJSON_IsObject(pattern_props) || (ctx && ctx->mode == JSVAL_MODE_LEXICAL))
  {
    cJSON *child = NULL;
    cJSON_ArrayForEach(child, inst)
    {
      bool matched_pattern = false;
      if (cJSON_IsObject(pattern_props))
      {
        jsval_result r = apply_pattern_properties_to_child(child, pattern_props, ctx, &matched_pattern);
        if (!r.ok)
          return r;
      }

      if (ctx && ctx->mode == JSVAL_MODE_LEXICAL)
      {
        bool in_props = cJSON_IsObject(props) &&
                        child->string &&
                        cJSON_GetObjectItemCaseSensitive(props, child->string) != NULL;
        if (!in_props && !matched_pattern)
        {
          return errf("Chiave non prevista: '%s'", child->string ? child->string : "(null)");
        }
      }
    }
  }
  return ok();
}

// Implementazione ricorsiva del validatore per un sottoalbero JSON.
static jsval_result js_validate_impl(cJSON *inst, cJSON *schema, const jsval_ctx *ctx)
{
  // Risolvi $ref se presente
  cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "$ref");
  if (cJSON_IsString(ref))
  {
    cJSON *resolved = resolve_ref(ref->valuestring, ctx);
    if (!resolved)
      return errf("Impossibile risolvere $ref '%s'.", ref->valuestring);
    return js_validate_impl(inst, resolved, ctx);
  }

  // type
  const char *t = get_type(schema);
  if (t && !is_type(inst, t))
    return errf("Tipo non valido: atteso '%s'.", t);

  // enum / bounds
  jsval_result r;
  r = validate_enum(inst, schema);
  if (!r.ok)
    return r;
  r = validate_string_pattern(inst, schema);
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
