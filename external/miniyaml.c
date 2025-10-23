#include "miniyaml.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIYAML_MAX_STACK 128

typedef enum {
    LINE_MAP,
    LINE_SEQ
} line_type;

typedef struct {
    int line_no;
    int indent;
    line_type type;
    char *key;      /* for LINE_MAP */
    char *value;    /* trimmed value */
    int has_value;  /* for LINE_MAP */
} YamlLine;

typedef struct {
    int indent;
    char *text;
} BlockPiece;

static void free_block_pieces(BlockPiece *pieces, size_t count) {
    if (!pieces) return;
    for (size_t i = 0; i < count; ++i) {
        free(pieces[i].text);
    }
    free(pieces);
}

typedef enum {
    CT_OBJECT,
    CT_ARRAY
} ContainerType;

typedef struct {
    int indent;
    ContainerType type;
    cJSON *node;
} Container;

static void free_lines(YamlLine *lines, size_t count) {
    if (!lines) return;
    for (size_t i = 0; i < count; ++i) {
        free(lines[i].key);
        free(lines[i].value);
    }
    free(lines);
}

static char *mini_strdup_range(const char *start, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *mini_strdup(const char *s) {
    if (!s) return NULL;
    return mini_strdup_range(s, strlen(s));
}

static char *mini_strdup_trim(const char *start) {
    const char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    while (start < end && isspace((unsigned char)*start)) {
        ++start;
    }
    return mini_strdup_range(start, (size_t)(end - start));
}

static void rtrim_inplace(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static char *make_error(int line_no, const char *msg) {
    char buf[256];
    if (line_no > 0)
        snprintf(buf, sizeof(buf), "Linea %d: %s", line_no, msg);
    else
        snprintf(buf, sizeof(buf), "%s", msg);
    return mini_strdup(buf);
}

static const char *skip_spaces(const char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static const char *find_unquoted_colon(const char *s) {
    int in_single = 0, in_double = 0;
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (c == '\'' && !in_double) {
            in_single = !in_single;
        } else if (c == '"' && !in_single) {
            if (p == s || *(p - 1) != '\\')
                in_double = !in_double;
        } else if (c == ':' && !in_single && !in_double) {
            char next = *(p + 1);
            if (next == '\0' || isspace((unsigned char)next))
                return p;
        } else if (c == '#') {
            break;
        }
    }
    return NULL;
}

static char *collect_block_scalar(int parent_indent, int *line_no, const char **cursor, const char *indicator, char **error_msg) {
    if (!indicator || indicator[0] == '\0') {
        return mini_strdup("");
    }

    int strip_mode = 0;
    const char *opts = indicator + 1;
    while (*opts == ' ' || *opts == '\t') ++opts;
    if (*opts == '-' || *opts == '+') {
        strip_mode = (*opts == '-') ? 1 : 2;
        ++opts;
    }
    while (*opts == ' ' || *opts == '\t') ++opts;
    if (isdigit((unsigned char)*opts)) {
        if (error_msg) *error_msg = make_error(*line_no, "Indicatori di indentazione per blocchi YAML non supportati");
        return NULL;
    }

    BlockPiece *pieces = NULL;
    size_t count = 0, cap = 0;
    const char *p = *cursor;

    while (*p) {
        const char *line_start = p;
        const char *line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);
        size_t len = (size_t)(line_end - line_start);
        const char *next = (*line_end == '\n') ? line_end + 1 : line_end;

        while (len > 0 && (line_start[len - 1] == '\r' || line_start[len - 1] == '\n')) {
            --len;
        }

        size_t idx = 0;
        int indent = 0;
        while (idx < len && line_start[idx] == ' ') { ++idx; ++indent; }
        size_t content_len = len - idx;
        const char *content = line_start + idx;
        int is_blank = content_len == 0;

        if (!is_blank && indent <= parent_indent) {
            break;
        }

        if (count == cap) {
            size_t newcap = cap ? cap * 2 : 8;
            BlockPiece *tmp = (BlockPiece *)realloc(pieces, newcap * sizeof(BlockPiece));
            if (!tmp) {
                free_block_pieces(pieces, count);
                if (error_msg) *error_msg = make_error(*line_no, "Memoria insufficiente");
                return NULL;
            }
            pieces = tmp;
            cap = newcap;
        }

        pieces[count].indent = is_blank ? parent_indent + 1 : indent;
        if (is_blank) {
            pieces[count].text = mini_strdup("");
        } else {
            pieces[count].text = mini_strdup_range(content, content_len);
            if (!pieces[count].text) {
                free_block_pieces(pieces, count);
                if (error_msg) *error_msg = make_error(*line_no, "Memoria insufficiente");
                return NULL;
            }
            rtrim_inplace(pieces[count].text);
        }
        ++count;

        p = next;
        (*line_no)++;
    }

    if (count == 0) {
        *cursor = p;
        return mini_strdup("");
    }

    int min_indent = INT_MAX;
    for (size_t i = 0; i < count; ++i) {
        if (pieces[i].indent < min_indent) {
            min_indent = pieces[i].indent;
        }
    }
    if (min_indent == INT_MAX) min_indent = parent_indent + 1;

    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        int rel = pieces[i].indent - min_indent;
        if (rel < 0) rel = 0;
        total += (size_t)rel + strlen(pieces[i].text) + 1;
    }

    char *result = (char *)malloc(total + 1);
    if (!result) {
        free_block_pieces(pieces, count);
        if (error_msg) *error_msg = make_error(*line_no, "Memoria insufficiente");
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
        int rel = pieces[i].indent - min_indent;
        if (rel < 0) rel = 0;
        for (int r = 0; r < rel; ++r) result[pos++] = ' ';
        size_t tlen = strlen(pieces[i].text);
        if (tlen > 0) {
            memcpy(result + pos, pieces[i].text, tlen);
            pos += tlen;
        }
        result[pos++] = '\n';
    }

    if (pos == 0) {
        result[pos] = '\0';
    } else {
        if (strip_mode == 1) {
            while (pos > 0 && result[pos - 1] == '\n') {
                --pos;
            }
        }
        result[pos] = '\0';
    }

    free_block_pieces(pieces, count);
    *cursor = p;
    return result;
}

static int parse_lines(const char *input, YamlLine **out_lines, size_t *out_count, char **error_msg) {
    size_t cap = 32;
    size_t count = 0;
    YamlLine *lines = (YamlLine *)calloc(cap, sizeof(YamlLine));
    if (!lines) {
        *error_msg = make_error(0, "Memoria insufficiente");
        return 0;
    }

    const char *cursor = input;
    int line_no = 0;
    while (*cursor) {
        const char *line_start = cursor;
        const char *line_end = strchr(cursor, '\n');
        if (!line_end) line_end = cursor + strlen(cursor);
        cursor = (*line_end == '\n') ? line_end + 1 : line_end;
        ++line_no;

        size_t len = (size_t)(line_end - line_start);
        while (len > 0 && (line_start[len - 1] == '\r' || line_start[len - 1] == '\n')) {
            --len;
        }
        if (len == 0) continue;

        char *line_buf = mini_strdup_range(line_start, len);
        if (!line_buf) {
            free_lines(lines, count);
            *error_msg = make_error(line_no, "Memoria insufficiente");
            return 0;
        }

        int indent = 0;
        while (line_buf[indent] == ' ') ++indent;
        if (line_buf[indent] == '\t') {
            free(line_buf);
            free_lines(lines, count);
            *error_msg = make_error(line_no, "Tabulazioni non supportate");
            return 0;
        }

        char *content = line_buf + indent;
        int in_single = 0, in_double = 0;
        for (size_t i = 0; content[i]; ++i) {
            char c = content[i];
            if (c == '\'' && !in_double) {
                in_single = !in_single;
            } else if (c == '"' && !in_single) {
                if (i == 0 || content[i - 1] != '\\')
                    in_double = !in_double;
            } else if (c == '#' && !in_single && !in_double) {
                content[i] = '\0';
                break;
            }
        }

        char *trimmed = mini_strdup_trim(content);
        free(line_buf);
        if (!trimmed) {
            free_lines(lines, count);
            *error_msg = make_error(line_no, "Memoria insufficiente");
            return 0;
        }
        if (trimmed[0] == '\0') {
            free(trimmed);
            continue;
        }

        if (count == cap) {
            cap *= 2;
            YamlLine *tmp = (YamlLine *)realloc(lines, cap * sizeof(YamlLine));
            if (!tmp) {
                free(trimmed);
                free_lines(lines, count);
                *error_msg = make_error(line_no, "Memoria insufficiente");
                return 0;
            }
            lines = tmp;
        }

        YamlLine *dst = &lines[count++];
        memset(dst, 0, sizeof(*dst));
        dst->line_no = line_no;
        dst->indent = indent;

        if (trimmed[0] == '-' && (trimmed[1] == '\0' || isspace((unsigned char)trimmed[1]))) {
            dst->type = LINE_SEQ;
            const char *rest = trimmed + 1;
            rest = skip_spaces(rest);
            dst->value = mini_strdup_trim(rest);
            if (!dst->value) {
                free(trimmed);
                free_lines(lines, count - 1);
                *error_msg = make_error(line_no, "Memoria insufficiente");
                return 0;
            }
            if (dst->value[0] == '|' || dst->value[0] == '>') {
                char *block = collect_block_scalar(indent, &line_no, &cursor, dst->value, error_msg);
                if (!block) {
                    free(trimmed);
                    free_lines(lines, count - 1);
                    free(dst->value);
                    return 0;
                }
                free(dst->value);
                dst->value = block;
            }
            free(trimmed);
        } else {
            const char *colon = find_unquoted_colon(trimmed);
            if (!colon) {
                free(trimmed);
                free_lines(lines, count - 1);
                *error_msg = make_error(line_no, "Atteso ':' in riga YAML");
                return 0;
            }
            dst->type = LINE_MAP;
            dst->key = mini_strdup_range(trimmed, (size_t)(colon - trimmed));
            if (!dst->key) {
                free(trimmed);
                free_lines(lines, count - 1);
                *error_msg = make_error(line_no, "Memoria insufficiente");
                return 0;
            }
            char *key_trim = mini_strdup_trim(dst->key);
            free(dst->key);
            dst->key = key_trim;
            if (!dst->key) {
                free(trimmed);
                free_lines(lines, count - 1);
                *error_msg = make_error(line_no, "Memoria insufficiente");
                return 0;
            }
            const char *valstart = colon + 1;
            while (*valstart && isspace((unsigned char)*valstart)) ++valstart;
            dst->value = mini_strdup_trim(valstart);
            if (!dst->value) {
                free(trimmed);
                free_lines(lines, count - 1);
                *error_msg = make_error(line_no, "Memoria insufficiente");
                return 0;
            }
            dst->has_value = dst->value[0] != '\0';
            free(trimmed);
            if (dst->has_value && (dst->value[0] == '|' || dst->value[0] == '>')) {
                char *block = collect_block_scalar(indent, &line_no, &cursor, dst->value, error_msg);
                if (!block) {
                    free_lines(lines, count);
                    return 0;
                }
                free(dst->value);
                dst->value = block;
            }
        }
    }

    *out_lines = lines;
    *out_count = count;
    return 1;
}

static int next_child_index(const YamlLine *lines, size_t count, size_t idx) {
    int indent = lines[idx].indent;
    for (size_t j = idx + 1; j < count; ++j) {
        if (lines[j].indent < indent) {
            return -1;
        }
        if (lines[j].indent == indent) {
            return lines[j].type == LINE_SEQ ? (int)j : -1;
        }
        if (lines[j].indent > indent) {
            return (int)j;
        }
    }
    return -1;
}

static cJSON *parse_double_quoted(const char *value, char **error_msg, int line_no) {
    size_t len = strlen(value);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        *error_msg = make_error(line_no, "Memoria insufficiente");
        return NULL;
    }
    size_t j = 0;
    int escape = 0;
    for (size_t i = 1; i < len; ++i) {
        char c = value[i];
        if (escape) {
            switch (c) {
                case '\\': out[j++] = '\\'; break;
                case '"': out[j++] = '"'; break;
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                case 'b': out[j++] = '\b'; break;
                case 'f': out[j++] = '\f'; break;
                default:
                    free(out);
                    *error_msg = make_error(line_no, "Sequenza di escape non supportata in stringa");
                    return NULL;
            }
            escape = 0;
        } else if (c == '\\') {
            escape = 1;
        } else if (c == '"') {
            if (i + 1 < len) {
                const char *rest = value + i + 1;
                while (*rest) {
                    if (!isspace((unsigned char)*rest)) {
                        free(out);
                        *error_msg = make_error(line_no, "Contenuto non atteso dopo stringa");
                        return NULL;
                    }
                    ++rest;
                }
            }
            out[j] = '\0';
            cJSON *node = cJSON_CreateString(out);
            free(out);
            return node;
        } else {
            out[j++] = c;
        }
    }
    free(out);
    *error_msg = make_error(line_no, "Stringa senza chiusura");
    return NULL;
}

static cJSON *parse_single_quoted(const char *value, char **error_msg, int line_no) {
    size_t len = strlen(value);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        *error_msg = make_error(line_no, "Memoria insufficiente");
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 1; i < len; ++i) {
        char c = value[i];
        if (c == '\'') {
            if (i + 1 < len && value[i + 1] == '\'') {
                out[j++] = '\'';
                ++i;
            } else {
                if (i + 1 < len) {
                    const char *rest = value + i + 1;
                    while (*rest) {
                        if (!isspace((unsigned char)*rest)) {
                            free(out);
                            *error_msg = make_error(line_no, "Contenuto non atteso dopo stringa");
                            return NULL;
                        }
                        ++rest;
                    }
                }
                out[j] = '\0';
                cJSON *node = cJSON_CreateString(out);
                free(out);
                return node;
            }
        } else {
            out[j++] = c;
        }
    }
    free(out);
    *error_msg = make_error(line_no, "Stringa senza chiusura");
    return NULL;
}

static int is_number(const char *value, double *out) {
    if (!value || !*value) return 0;
    char *endptr = NULL;
    double v = strtod(value, &endptr);
    if (endptr == value) return 0;
    while (*endptr) {
        if (!isspace((unsigned char)*endptr)) return 0;
        ++endptr;
    }
    *out = v;
    return 1;
}

static cJSON *parse_scalar_value(const char *value, char **error_msg, int line_no) {
    if (!value) return cJSON_CreateNull();
    if (value[0] == '\0') return cJSON_CreateString("");
    if (value[0] == '"') {
        return parse_double_quoted(value, error_msg, line_no);
    }
    if (value[0] == '\'') {
        return parse_single_quoted(value, error_msg, line_no);
    }
    if (value[0] == '[' || value[0] == '{') {
        cJSON *node = cJSON_Parse(value);
        if (node) return node;
        if (error_msg)
            *error_msg = make_error(line_no, "Impossibile interpretare struttura inline");
        return NULL;
    }
    if (value[0] == '|' || value[0] == '>') {
        if (error_msg)
            *error_msg = make_error(line_no, "Blocchi letterali YAML non supportati");
        return NULL;
    }
    if (strcmp(value, "null") == 0 || strcmp(value, "~") == 0) {
        return cJSON_CreateNull();
    }
    if (strcmp(value, "true") == 0) {
        return cJSON_CreateBool(1);
    }
    if (strcmp(value, "false") == 0) {
        return cJSON_CreateBool(0);
    }
    double num;
    if (is_number(value, &num)) {
        return cJSON_CreateNumber(num);
    }
    return cJSON_CreateString(value);
}

static int append_key_value(Container *container, const YamlLine *line, const char *key, const char *value, int has_value, int child_index, const YamlLine *lines, size_t count, char **error_msg, Container *stack, size_t *stack_sz) {
    if (container->type != CT_OBJECT) {
        *error_msg = make_error(line->line_no, "Valore mappato fuori da un oggetto");
        return 0;
    }

    if (!has_value) {
        ContainerType child_type = CT_OBJECT;
        cJSON *child_node = NULL;
        if (child_index >= 0 && (size_t)child_index < count && lines[child_index].type == LINE_SEQ) {
            child_type = CT_ARRAY;
            child_node = cJSON_CreateArray();
        } else {
            child_type = CT_OBJECT;
            child_node = cJSON_CreateObject();
        }
        if (!child_node) {
            *error_msg = make_error(line->line_no, "Memoria insufficiente");
            return 0;
        }
        cJSON_AddItemToObject(container->node, key, child_node);
        if (*stack_sz >= MINIYAML_MAX_STACK) {
            *error_msg = make_error(line->line_no, "Nidificazione YAML troppo profonda");
            return 0;
        }
        stack[(*stack_sz)++] = (Container){ .indent = line->indent, .type = child_type, .node = child_node };
        return 1;
    }

    cJSON *val = parse_scalar_value(value, error_msg, line->line_no);
    if (!val) return 0;
    cJSON_AddItemToObject(container->node, key, val);
    return 1;
}

static int append_sequence_item(Container *container, const YamlLine *line, const char *value, int child_index, const YamlLine *lines, size_t count, char **error_msg, Container *stack, size_t *stack_sz) {
    if (container->type != CT_ARRAY) {
        *error_msg = make_error(line->line_no, "Elemento di sequenza fuori da una lista");
        return 0;
    }

    if (!value || value[0] == '\0') {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            *error_msg = make_error(line->line_no, "Memoria insufficiente");
            return 0;
        }
        cJSON_AddItemToArray(container->node, item);
        if (*stack_sz >= MINIYAML_MAX_STACK) {
            *error_msg = make_error(line->line_no, "Nidificazione YAML troppo profonda");
            return 0;
        }
        stack[(*stack_sz)++] = (Container){ .indent = line->indent, .type = CT_OBJECT, .node = item };
        return 1;
    }

    const char *colon = find_unquoted_colon(value);
    if (colon) {
        char *key = mini_strdup_range(value, (size_t)(colon - value));
        if (!key) {
            *error_msg = make_error(line->line_no, "Memoria insufficiente");
            return 0;
        }
        char *key_trim = mini_strdup_trim(key);
        free(key);
        if (!key_trim) {
            *error_msg = make_error(line->line_no, "Memoria insufficiente");
            return 0;
        }
        const char *valstart = colon + 1;
        while (*valstart && isspace((unsigned char)*valstart)) ++valstart;
        char *value_trim = mini_strdup_trim(valstart);
        if (!value_trim) {
            free(key_trim);
            *error_msg = make_error(line->line_no, "Memoria insufficiente");
            return 0;
        }
        cJSON *item_obj = cJSON_CreateObject();
        if (!item_obj) {
            free(key_trim);
            free(value_trim);
            *error_msg = make_error(line->line_no, "Memoria insufficiente");
            return 0;
        }
        cJSON_AddItemToArray(container->node, item_obj);
        if (*stack_sz >= MINIYAML_MAX_STACK) {
            free(key_trim);
            free(value_trim);
            *error_msg = make_error(line->line_no, "Nidificazione YAML troppo profonda");
            return 0;
        }
        stack[(*stack_sz)++] = (Container){ .indent = line->indent, .type = CT_OBJECT, .node = item_obj };

        if (value_trim[0] == '\0') {
            ContainerType child_type = CT_OBJECT;
            cJSON *child = NULL;
            if (child_index >= 0 && (size_t)child_index < count && lines[child_index].indent > line->indent && lines[child_index].type == LINE_SEQ) {
                child_type = CT_ARRAY;
                child = cJSON_CreateArray();
            } else {
                child_type = CT_OBJECT;
                child = cJSON_CreateObject();
            }
            if (!child) {
                free(key_trim);
                free(value_trim);
                *error_msg = make_error(line->line_no, "Memoria insufficiente");
                return 0;
            }
            cJSON_AddItemToObject(item_obj, key_trim, child);
            free(key_trim);
            free(value_trim);
            if (*stack_sz >= MINIYAML_MAX_STACK) {
                *error_msg = make_error(line->line_no, "Nidificazione YAML troppo profonda");
                return 0;
            }
            stack[(*stack_sz)++] = (Container){ .indent = line->indent, .type = child_type, .node = child };
            return 1;
        }

        cJSON *val = parse_scalar_value(value_trim, error_msg, line->line_no);
        free(value_trim);
        if (!val) {
            free(key_trim);
            return 0;
        }
        cJSON_AddItemToObject(item_obj, key_trim, val);
        free(key_trim);
        return 1;
    }

    cJSON *val = parse_scalar_value(value, error_msg, line->line_no);
    if (!val) return 0;
    cJSON_AddItemToArray(container->node, val);
    return 1;
}

cJSON *miniyaml_parse(const char *input, char **error_msg) {
    if (error_msg) *error_msg = NULL;
    if (!input) {
        if (error_msg) *error_msg = make_error(0, "Input YAML nullo");
        return NULL;
    }

    YamlLine *lines = NULL;
    size_t count = 0;
    if (!parse_lines(input, &lines, &count, error_msg)) {
        return NULL;
    }
    if (count == 0) {
        free_lines(lines, count);
        if (error_msg) *error_msg = make_error(0, "Documento YAML vuoto");
        return NULL;
    }

    Container stack[MINIYAML_MAX_STACK];
    size_t stack_sz = 0;
    cJSON *root = NULL;

    for (size_t i = 0; i < count; ++i) {
        const YamlLine *line = &lines[i];
        if (stack_sz == 0) {
            if (line->type == LINE_SEQ) {
                root = cJSON_CreateArray();
                stack[stack_sz++] = (Container){ .indent = -1, .type = CT_ARRAY, .node = root };
            } else {
                root = cJSON_CreateObject();
                stack[stack_sz++] = (Container){ .indent = -1, .type = CT_OBJECT, .node = root };
            }
        }

        while (stack_sz > 0) {
            Container *top = &stack[stack_sz - 1];
            if (line->indent > top->indent) {
                break;
            }
            if (line->indent == top->indent && top->type == CT_ARRAY && line->type == LINE_SEQ) {
                break;
            }
            --stack_sz;
        }
        if (stack_sz == 0) {
            free_lines(lines, count);
            if (error_msg) *error_msg = make_error(line->line_no, "Struttura YAML non valida");
            if (root) cJSON_Delete(root);
            return NULL;
        }
        Container *parent = &stack[stack_sz - 1];

        if (line->type == LINE_MAP) {
            int child_index = next_child_index(lines, count, i);
            if (!append_key_value(parent, line, line->key, line->value, line->has_value, child_index, lines, count, error_msg, stack, &stack_sz)) {
                if (root) cJSON_Delete(root);
                free_lines(lines, count);
                return NULL;
            }
        } else { // sequence
            int child_index = next_child_index(lines, count, i);
            if (!append_sequence_item(parent, line, line->value, child_index, lines, count, error_msg, stack, &stack_sz)) {
                if (root) cJSON_Delete(root);
                free_lines(lines, count);
                return NULL;
            }
        }
    }

    free_lines(lines, count);
    return root;
}
