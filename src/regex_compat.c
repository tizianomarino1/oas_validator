#ifdef _MSC_VER

#include "regex_compat.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum atom_type
{
  ATOM_LITERAL,
  ATOM_DOT,
  ATOM_CHAR_CLASS,
  ATOM_GROUP,
  ATOM_ANCHOR_BEGIN,
  ATOM_ANCHOR_END
} atom_type;

typedef struct char_range
{
  unsigned char start;
  unsigned char end;
} char_range;

typedef struct atom
{
  atom_type type;
  union
  {
    unsigned char literal;
    struct
    {
      bool negate;
      size_t count;
      char_range *ranges;
    } cls;
    struct
    {
      char *pattern;
    } group;
  } data;
} atom;

typedef struct quantifier
{
  size_t min;
  size_t max;
} quantifier;

static void free_atom(atom *a)
{
  if (!a)
    return;
  if (a->type == ATOM_CHAR_CLASS)
  {
    free(a->data.cls.ranges);
    a->data.cls.ranges = NULL;
    a->data.cls.count = 0;
  }
  else if (a->type == ATOM_GROUP)
  {
    free(a->data.group.pattern);
    a->data.group.pattern = NULL;
  }
}

static bool append_range(atom *a, unsigned char start, unsigned char end)
{
  size_t new_count = a->data.cls.count + 1;
  char_range *nr = (char_range *)realloc(a->data.cls.ranges, new_count * sizeof(char_range));
  if (!nr)
    return false;
  nr[new_count - 1].start = start;
  nr[new_count - 1].end = end;
  a->data.cls.ranges = nr;
  a->data.cls.count = new_count;
  return true;
}

static char *duplicate_segment(const char *start, size_t len)
{
  char *copy = (char *)malloc(len + 1);
  if (!copy)
    return NULL;
  memcpy(copy, start, len);
  copy[len] = '\0';
  return copy;
}

static bool parse_char_class(const char **pattern, atom *out, bool *error)
{
  const char *p = *pattern;
  if (*p != '[')
  {
    *error = true;
    return false;
  }
  ++p;
  out->type = ATOM_CHAR_CLASS;
  out->data.cls.negate = false;
  out->data.cls.count = 0;
  out->data.cls.ranges = NULL;

  if (*p == '^')
  {
    out->data.cls.negate = true;
    ++p;
  }

  bool first_token = true;

  while (*p)
  {
    if (*p == ']' && !first_token)
    {
      ++p;
      *pattern = p;
      return true;
    }

    unsigned char c;
    if (*p == '\\')
    {
      ++p;
      if (*p == '\0')
      {
        *error = true;
        return false;
      }
      c = (unsigned char)*p;
      ++p;
    }
    else
    {
      c = (unsigned char)*p;
      ++p;
    }

    first_token = false;

    if (*p == '-' && p[1] != '\0' && p[1] != ']')
    {
      ++p;
      unsigned char end_char;
      if (*p == '\\')
      {
        ++p;
        if (*p == '\0')
        {
          *error = true;
          return false;
        }
        end_char = (unsigned char)*p;
        ++p;
      }
      else
      {
        end_char = (unsigned char)*p;
        ++p;
      }
      unsigned char start_char = c;
      if (start_char > end_char)
      {
        unsigned char tmp = start_char;
        start_char = end_char;
        end_char = tmp;
      }
      if (!append_range(out, start_char, end_char))
      {
        *error = true;
        return false;
      }
    }
    else
    {
      if (!append_range(out, c, c))
      {
        *error = true;
        return false;
      }
    }
  }

  *error = true;
  return false;
}

static bool parse_group(const char **pattern, atom *out, bool *error)
{
  const char *p = *pattern;
  if (*p != '(')
  {
    *error = true;
    return false;
  }
  ++p;
  int depth = 0;
  bool in_class = false;
  const char *start = p;
  while (*p)
  {
    char ch = *p;
    if (ch == '\\')
    {
      ++p;
      if (*p)
        ++p;
      else
        break;
      continue;
    }
    if (in_class)
    {
      if (ch == ']')
        in_class = false;
      ++p;
      continue;
    }
    if (ch == '[')
    {
      in_class = true;
      ++p;
      continue;
    }
    if (ch == '(')
    {
      ++depth;
      ++p;
      continue;
    }
    if (ch == ')')
    {
      if (depth == 0)
      {
        size_t len = (size_t)(p - start);
        char *segment = duplicate_segment(start, len);
        if (!segment)
        {
          *error = true;
          return false;
        }
        out->type = ATOM_GROUP;
        out->data.group.pattern = segment;
        ++p;
        *pattern = p;
        return true;
      }
      --depth;
      ++p;
      continue;
    }
    ++p;
  }
  *error = true;
  return false;
}

static bool parse_atom(const char **pattern, atom *out, bool *error)
{
  const char *p = *pattern;
  if (*p == '\0' || *p == '|' || *p == ')')
  {
    *error = true;
    return false;
  }
  if (*p == '[')
  {
    if (!parse_char_class(pattern, out, error))
      return false;
    return true;
  }
  if (*p == '(')
  {
    if (!parse_group(pattern, out, error))
      return false;
    return true;
  }
  if (*p == '.')
  {
    out->type = ATOM_DOT;
    ++(*pattern);
    return true;
  }
  if (*p == '^')
  {
    out->type = ATOM_ANCHOR_BEGIN;
    ++(*pattern);
    return true;
  }
  if (*p == '$')
  {
    out->type = ATOM_ANCHOR_END;
    ++(*pattern);
    return true;
  }
  if (*p == '\\')
  {
    ++p;
    if (*p == '\0')
    {
      *error = true;
      return false;
    }
    out->type = ATOM_LITERAL;
    out->data.literal = (unsigned char)*p;
    ++p;
    *pattern = p;
    return true;
  }

  out->type = ATOM_LITERAL;
  out->data.literal = (unsigned char)*p;
  ++p;
  *pattern = p;
  return true;
}

static quantifier parse_quantifier(const char **pattern)
{
  quantifier q = {1u, 1u};
  const char *p = *pattern;
  if (*p == '*')
  {
    q.min = 0;
    q.max = SIZE_MAX;
    ++p;
  }
  else if (*p == '+')
  {
    q.min = 1;
    q.max = SIZE_MAX;
    ++p;
  }
  else if (*p == '?')
  {
    q.min = 0;
    q.max = 1;
    ++p;
  }
  else if (*p == '{')
  {
    const char *orig = p;
    ++p;
    if (!isdigit((unsigned char)*p))
      return q;
    unsigned long min = 0;
    while (isdigit((unsigned char)*p))
    {
      min = min * 10 + (unsigned long)(*p - '0');
      ++p;
    }
    unsigned long max = min;
    bool has_comma = false;
    if (*p == ',')
    {
      has_comma = true;
      ++p;
      if (*p == '}')
      {
        max = ULONG_MAX;
      }
      else if (isdigit((unsigned char)*p))
      {
        max = 0;
        while (isdigit((unsigned char)*p))
        {
          max = max * 10 + (unsigned long)(*p - '0');
          ++p;
        }
      }
      else
      {
        *pattern = orig;
        return q;
      }
    }
    if (*p != '}')
    {
      *pattern = orig;
      return q;
    }
    ++p;
    q.min = (size_t)min;
    if (!has_comma)
    {
      q.max = (size_t)max;
    }
    else if (max == ULONG_MAX)
    {
      q.max = SIZE_MAX;
    }
    else
    {
      q.max = (size_t)max;
    }
  }
  *pattern = p;
  return q;
}

static bool match_pattern_segment(const char *pattern, const char *text, const char *text_start, const char **match_end, bool *error);

static bool match_atom(const atom *a, const char *text, const char *text_start, const char **next, bool *error)
{
  switch (a->type)
  {
  case ATOM_LITERAL:
    if (*text == '\0' || (unsigned char)*text != a->data.literal)
      return false;
    *next = text + 1;
    return true;
  case ATOM_DOT:
    if (*text == '\0')
      return false;
    *next = text + 1;
    return true;
  case ATOM_CHAR_CLASS:
    if (*text == '\0')
      return false;
    {
      unsigned char ch = (unsigned char)*text;
      bool in_range = false;
      for (size_t i = 0; i < a->data.cls.count; ++i)
      {
        if (ch >= a->data.cls.ranges[i].start && ch <= a->data.cls.ranges[i].end)
        {
          in_range = true;
          break;
        }
      }
      if (a->data.cls.negate)
        in_range = !in_range;
      if (!in_range)
        return false;
      *next = text + 1;
      return true;
    }
  case ATOM_GROUP:
  {
    const char *group_end = NULL;
    if (!a->data.group.pattern)
      return false;
    if (!match_pattern_segment(a->data.group.pattern, text, text_start, &group_end, error))
      return false;
    if (*error)
      return false;
    *next = group_end;
    return true;
  }
  case ATOM_ANCHOR_BEGIN:
    if (text != text_start)
      return false;
    *next = text;
    return true;
  case ATOM_ANCHOR_END:
    if (*text != '\0')
      return false;
    *next = text;
    return true;
  }
  return false;
}

static bool match_sequence_internal(const char *pattern, const char *text, const char *text_start, const char **pattern_end, const char **text_end, bool *error);

static bool match_pattern_segment(const char *pattern, const char *text, const char *text_start, const char **match_end, bool *error)
{
  const char *segment_start = pattern;
  const char *p = pattern;
  int depth = 0;
  bool in_class = false;

  while (1)
  {
    char ch = *p;
    if (ch == '\0' || (ch == '|' && depth == 0 && !in_class))
    {
      size_t len = (size_t)(p - segment_start);
      char *segment = duplicate_segment(segment_start, len);
      if (!segment)
      {
        *error = true;
        return false;
      }
      const char *seq_end = NULL;
      bool ok = match_sequence_internal(segment, text, text_start, NULL, &seq_end, error);
      free(segment);
      if (*error)
        return false;
      if (ok)
      {
        if (match_end)
          *match_end = seq_end;
        return true;
      }
      if (ch == '\0')
        return false;
      segment_start = p + 1;
      ++p;
      continue;
    }

    if (ch == '\\')
    {
      if (p[1] == '\0')
      {
        *error = true;
        return false;
      }
      p += 2;
      continue;
    }
    if (ch == '[')
    {
      in_class = true;
      ++p;
      continue;
    }
    if (ch == ']' && in_class)
    {
      in_class = false;
      ++p;
      continue;
    }
    if (!in_class)
    {
      if (ch == '(')
      {
        ++depth;
      }
      else if (ch == ')')
      {
        if (depth == 0)
        {
          *error = true;
          return false;
        }
        --depth;
      }
    }
    ++p;
  }
  return false;
}

static bool match_sequence_internal(const char *pattern, const char *text, const char *text_start, const char **pattern_end, const char **text_end, bool *error)
{
  const char *p = pattern;
  const char *t = text;

  if (*p == '\0')
  {
    if (pattern_end)
      *pattern_end = p;
    if (text_end)
      *text_end = t;
    return true;
  }

  if (*p == '|' || *p == ')')
  {
    if (error)
      *error = true;
    return false;
  }

  atom a;
  memset(&a, 0, sizeof(a));
  const char *cursor = p;
  if (!parse_atom(&cursor, &a, error))
  {
    free_atom(&a);
    return false;
  }

  quantifier q = parse_quantifier(&cursor);
  size_t min = q.min;
  size_t max = q.max;
  if ((a.type == ATOM_ANCHOR_BEGIN || a.type == ATOM_ANCHOR_END) && max > min)
    max = min;

  const char *curr = t;
  size_t matched = 0;

  for (; matched < min; ++matched)
  {
    const char *next = curr;
    if (!match_atom(&a, curr, text_start, &next, error))
    {
      free_atom(&a);
      return false;
    }
    if (*error)
    {
      free_atom(&a);
      return false;
    }
    if (next == curr && a.type != ATOM_ANCHOR_BEGIN && a.type != ATOM_ANCHOR_END)
    {
      free_atom(&a);
      return false;
    }
    curr = next;
  }

  if (match_sequence_internal(cursor, curr, text_start, pattern_end, text_end, error))
  {
    free_atom(&a);
    return true;
  }
  if (*error)
  {
    free_atom(&a);
    return false;
  }

  while (matched < max)
  {
    const char *next = curr;
    if (!match_atom(&a, curr, text_start, &next, error))
      break;
    if (*error)
    {
      free_atom(&a);
      return false;
    }
    if (next == curr && a.type != ATOM_ANCHOR_BEGIN && a.type != ATOM_ANCHOR_END)
      break;
    curr = next;
    ++matched;
    if (match_sequence_internal(cursor, curr, text_start, pattern_end, text_end, error))
    {
      free_atom(&a);
      return true;
    }
    if (*error)
    {
      free_atom(&a);
      return false;
    }
  }

  free_atom(&a);
  return false;
}

regex_compat_result regex_compat_match(const char *pattern, const char *text)
{
  regex_compat_result result = {true, false};
  if (!pattern || !text)
  {
    result.valid = false;
    return result;
  }

  const char *text_start = text;
  const char *pos = text;
  bool error = false;

  do
  {
    const char *match_end = NULL;
    if (match_pattern_segment(pattern, pos, text_start, &match_end, &error))
    {
      result.matched = true;
      return result;
    }
    if (error)
    {
      result.valid = false;
      return result;
    }
    if (*pos == '\0')
      break;
    ++pos;
  } while (1);

  return result;
}

#endif
