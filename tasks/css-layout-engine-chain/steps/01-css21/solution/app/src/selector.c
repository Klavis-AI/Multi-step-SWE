/* selector.c -- CSS 2.1 selector parsing and matching.  C11, libc only.
 *
 * Scope is exactly CSS 2.1 (https://www.w3.org/TR/CSS21/selector.html):
 *
 *   universal  *
 *   type       div                      (ASCII case-insensitive; HTML)
 *   class      .name
 *   id         #name
 *   attribute  [att] [att=v] [att~=v] [att|=v]
 *   pseudo-cls :first-child :link :visited :hover :active :focus :lang(C)
 *   pseudo-elt :before :after :first-line :first-letter (and the :: forms)
 *   combinator descendant (whitespace), child '>', adjacent sibling '+'
 *
 * Everything newer -- ':nth-child()' and the rest of the Selectors-3
 * pseudo-classes, ':not()', ':is()', ':where()', ':has()', the general
 * sibling combinator '~', '[att^=]', '[att$=]', '[att*=]', '::selection',
 * namespace prefixes -- is REJECTED (sel_parse returns NULL).
 *
 * Per CSS 2.1 4.1.7 an unparsable member invalidates the whole comma group,
 * so sel_parse() is all-or-nothing.
 *
 * Every allocation comes from the caller's arena; nothing is freed here.
 */
#include "selector.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ chars */

static int s_lc(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static int s_ieq(const char *x, const char *y) {
  if (!x || !y) return 0;
  while (*x && *y) {
    if (s_lc((unsigned char)*x) != s_lc((unsigned char)*y)) return 0;
    x++;
    y++;
  }
  return *x == '\0' && *y == '\0';
}

static int s_is_space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int s_is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int s_hexval(unsigned char c) {
  if (c >= '0' && c <= '9') return (int)c - '0';
  if (c >= 'a' && c <= 'f') return (int)c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return (int)c - 'A' + 10;
  return -1;
}

/* CSS 2.1 4.1.1: nmstart [_a-zA-Z]|nonascii|escape, nmchar adds [0-9-]. */
static int s_is_nmstart(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}
static int s_is_nmchar(unsigned char c) {
  return s_is_nmstart(c) || s_is_digit(c) || c == '-';
}

/* ------------------------------------------------------------------ model */

enum { COMB_NONE = 0, COMB_DESC, COMB_CHILD, COMB_ADJ };

enum {
  C_CLASS = 1,
  C_ID,
  C_ATTR,
  C_FIRST_CHILD,
  C_LINK,
  C_NEVER,          /* :visited, :hover, :active, :focus -- static document */
  C_LANG
};

enum { A_EXISTS = 0, A_EQ, A_INCLUDES, A_DASHMATCH };

typedef struct sel_cond {
  int kind;
  int op;                 /* attribute operator, A_* */
  char *name;             /* class / id / attribute name / language range */
  char *value;            /* attribute value (NULL for A_EXISTS) */
  struct sel_cond *next;
} sel_cond;

/* One "simple selector sequence" (compound selector). */
typedef struct sel_seq {
  char *tag;              /* NULL for '*' and for a bare condition sequence */
  sel_cond *conds;
  int comb;               /* how this sequence attaches to `prev` */
  struct sel_seq *prev;   /* the sequence to our left, NULL at the head */
} sel_seq;

typedef struct sel_complex {
  sel_seq *key;           /* rightmost sequence: the subject */
  int a, b, c;            /* CSS 2.1 6.4.3 specificity */
  int pseudo_elem;        /* carries a pseudo-element: never matches an element */
  struct sel_complex *next;
} sel_complex;

struct sel_list {
  sel_complex *first;
};

/* -------------------------------------------------------------- tokenizer */

static void put_utf8(strbuf *sb, unsigned long cp) {
  if (cp == 0 || cp > 0x10FFFFul || (cp >= 0xD800ul && cp <= 0xDFFFul)) cp = 0xFFFDul;
  if (cp < 0x80ul) {
    sb_putc(sb, (char)cp);
  } else if (cp < 0x800ul) {
    sb_putc(sb, (char)(0xC0ul | (cp >> 6)));
    sb_putc(sb, (char)(0x80ul | (cp & 0x3Ful)));
  } else if (cp < 0x10000ul) {
    sb_putc(sb, (char)(0xE0ul | (cp >> 12)));
    sb_putc(sb, (char)(0x80ul | ((cp >> 6) & 0x3Ful)));
    sb_putc(sb, (char)(0x80ul | (cp & 0x3Ful)));
  } else {
    sb_putc(sb, (char)(0xF0ul | (cp >> 18)));
    sb_putc(sb, (char)(0x80ul | ((cp >> 12) & 0x3Ful)));
    sb_putc(sb, (char)(0x80ul | ((cp >> 6) & 0x3Ful)));
    sb_putc(sb, (char)(0x80ul | (cp & 0x3Ful)));
  }
}

/* Copy one (possibly multi-byte UTF-8) character.  *pp is not at the NUL. */
static void copy_char(const char **pp, strbuf *sb) {
  const char *p = *pp;
  unsigned char c = (unsigned char)*p;
  sb_putc(sb, *p++);
  if (c >= 0xC0) {
    while ((unsigned char)*p >= 0x80 && (unsigned char)*p < 0xC0) sb_putc(sb, *p++);
  }
  *pp = p;
}

/* Decode one escape at *pp (which points at the backslash).  Returns 0 for an
 * escaped newline / end of input, which is never valid inside an identifier. */
static int read_escape(const char **pp, strbuf *sb) {
  const char *p = *pp + 1;
  if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '\f') return 0;
  if (s_hexval((unsigned char)*p) >= 0) {
    unsigned long cp = 0;
    int i = 0;
    while (i < 6 && s_hexval((unsigned char)*p) >= 0) {
      cp = cp * 16ul + (unsigned long)s_hexval((unsigned char)*p);
      p++;
      i++;
    }
    /* one optional whitespace terminator, CRLF counting as one */
    if (p[0] == '\r' && p[1] == '\n') p += 2;
    else if (s_is_space((unsigned char)*p)) p++;
    put_utf8(sb, cp);
  } else {
    copy_char(&p, sb);
  }
  *pp = p;
  return 1;
}

/* Read a CSS name.  With need_ident, the strict IDENT production
 * ( [-]? nmstart nmchar* ); without it, the looser {nmchar}+ used by HASH.
 * Returns NULL (leaving *pp alone) when nothing valid is there. */
static char *read_name(arena *ar, const char **pp, int need_ident) {
  strbuf sb;
  const char *p = *pp;
  int got = 0;

  sb_init(&sb, ar);
  if (need_ident) {
    if (*p == '-') {
      sb_putc(&sb, '-');
      p++;
    }
    if (*p == '\\') {
      if (!read_escape(&p, &sb)) return NULL;
    } else if (s_is_nmstart((unsigned char)*p)) {
      copy_char(&p, &sb);
    } else {
      return NULL;
    }
    got = 1;
  }
  for (;;) {
    if (*p == '\\') {
      const char *save = p;
      if (!read_escape(&p, &sb)) { p = save; break; }
      got = 1;
    } else if (s_is_nmchar((unsigned char)*p)) {
      copy_char(&p, &sb);
      got = 1;
    } else {
      break;
    }
  }
  if (!got) return NULL;
  *pp = p;
  return sb_done(&sb);
}

/* Read a quoted string; *pp points at the opening quote. */
static char *read_string(arena *ar, const char **pp) {
  strbuf sb;
  const char *p = *pp;
  char q = *p++;

  sb_init(&sb, ar);
  for (;;) {
    if (*p == '\0') return NULL;                      /* unterminated */
    if (*p == '\n' || *p == '\r' || *p == '\f') return NULL;
    if (*p == q) { p++; break; }
    if (*p == '\\') {
      if (p[1] == '\n' || p[1] == '\f') { p += 2; continue; }   /* continuation */
      if (p[1] == '\r') { p += (p[2] == '\n') ? 3 : 2; continue; }
      if (!read_escape(&p, &sb)) return NULL;
      continue;
    }
    copy_char(&p, &sb);
  }
  *pp = p;
  return sb_done(&sb);
}

/* Skip whitespace and comments.  Returns 1 only when real whitespace was
 * skipped: the caller needs that to tell a descendant combinator apart from
 * mere padding around '>' / '+'.  A comment is removed from the token stream
 * without leaving whitespace behind, so it never forms a combinator. */
static int skip_ws(const char **pp) {
  const char *p = *pp;
  int any = 0;
  for (;;) {
    if (s_is_space((unsigned char)*p)) { p++; any = 1; continue; }
    if (p[0] == '/' && p[1] == '*') {
      p += 2;
      while (*p && !(p[0] == '*' && p[1] == '/')) p++;
      if (*p) p += 2;
      continue;
    }
    break;
  }
  *pp = p;
  return any;
}

/* ----------------------------------------------------------------- parser */

static int pseudo_class_kind(const char *name) {
  if (s_ieq(name, "first-child")) return C_FIRST_CHILD;
  if (s_ieq(name, "link")) return C_LINK;
  if (s_ieq(name, "visited")) return C_NEVER;
  if (s_ieq(name, "hover")) return C_NEVER;
  if (s_ieq(name, "active")) return C_NEVER;
  if (s_ieq(name, "focus")) return C_NEVER;
  return 0;
}

static int is_pseudo_element(const char *name) {
  return s_ieq(name, "before") || s_ieq(name, "after") ||
         s_ieq(name, "first-line") || s_ieq(name, "first-letter");
}

static sel_cond *new_cond(arena *ar, int kind) {
  sel_cond *c = (sel_cond *)arena_alloc(ar, sizeof *c);
  c->kind = kind;
  c->op = A_EXISTS;
  c->name = NULL;
  c->value = NULL;
  c->next = NULL;
  return c;
}

/* One simple selector sequence.  Bumps cx's specificity counters. */
static sel_seq *parse_compound(arena *ar, const char **pp, sel_complex *cx) {
  const char *p = *pp;
  sel_seq *s = (sel_seq *)arena_alloc(ar, sizeof *s);
  sel_cond **tail;
  int got = 0;

  s->tag = NULL;
  s->conds = NULL;
  s->comb = COMB_NONE;
  s->prev = NULL;
  tail = &s->conds;

  if (*p == '|') return NULL;                  /* |E -- namespaces are not 2.1 */
  if (*p == '*') {
    p++;
    got = 1;                                   /* universal: adds no specificity */
  } else if (*p == '\\' || *p == '-' || s_is_nmstart((unsigned char)*p)) {
    char *t = read_name(ar, &p, 1);
    if (!t) return NULL;
    s->tag = t;
    cx->c++;
    got = 1;
  }
  if (*p == '|') return NULL;                  /* ns|E, *|E */

  for (;;) {
    if (*p == '#') {
      sel_cond *c;
      char *nm;
      p++;
      nm = read_name(ar, &p, 0);               /* HASH is {nmchar}+ */
      if (!nm) return NULL;
      c = new_cond(ar, C_ID);
      c->name = nm;
      *tail = c;
      tail = &c->next;
      cx->a++;
      got = 1;
      continue;
    }
    if (*p == '.') {
      sel_cond *c;
      char *nm;
      p++;
      nm = read_name(ar, &p, 1);               /* class is '.' IDENT */
      if (!nm) return NULL;
      c = new_cond(ar, C_CLASS);
      c->name = nm;
      *tail = c;
      tail = &c->next;
      cx->b++;
      got = 1;
      continue;
    }
    if (*p == '[') {
      sel_cond *c;
      char *nm, *val = NULL;
      int op;
      p++;
      skip_ws(&p);
      nm = read_name(ar, &p, 1);
      if (!nm) return NULL;
      skip_ws(&p);
      if (*p == ']') {
        op = A_EXISTS;
        p++;
      } else {
        if (p[0] == '=') { op = A_EQ; p += 1; }
        else if (p[0] == '~' && p[1] == '=') { op = A_INCLUDES; p += 2; }
        else if (p[0] == '|' && p[1] == '=') { op = A_DASHMATCH; p += 2; }
        else return NULL;                      /* ^= $= *= and friends: not 2.1 */
        skip_ws(&p);
        if (*p == '"' || *p == '\'') val = read_string(ar, &p);
        else val = read_name(ar, &p, 0);
        if (!val) return NULL;
        skip_ws(&p);
        if (*p != ']') return NULL;
        p++;
      }
      c = new_cond(ar, C_ATTR);
      c->name = nm;
      c->value = val;
      c->op = op;
      *tail = c;
      tail = &c->next;
      cx->b++;
      got = 1;
      continue;
    }
    if (*p == ':') {
      char *nm;
      int dbl = 0, kind;
      p++;
      if (*p == ':') { dbl = 1; p++; }
      nm = read_name(ar, &p, 1);
      if (!nm) return NULL;
      if (*p == '(') {
        sel_cond *c;
        char *arg;
        /* :lang() is the only CSS 2.1 functional pseudo-class.  Everything
         * else here -- :not(), :nth-child(), :is(), :where(), :has() -- is
         * deliberately rejected. */
        if (dbl || !s_ieq(nm, "lang")) return NULL;
        p++;
        skip_ws(&p);
        if (*p == '"' || *p == '\'') arg = read_string(ar, &p);
        else arg = read_name(ar, &p, 1);
        if (!arg || !*arg) return NULL;
        skip_ws(&p);
        if (*p != ')') return NULL;
        p++;
        c = new_cond(ar, C_LANG);
        c->name = arg;
        *tail = c;
        tail = &c->next;
        cx->b++;
        got = 1;
        continue;
      }
      if (is_pseudo_element(nm)) {
        cx->c++;                               /* 6.4.3: counts as an element */
        cx->pseudo_elem = 1;
        got = 1;
        continue;
      }
      if (dbl) return NULL;                    /* ::selection, ::first-child, ... */
      kind = pseudo_class_kind(nm);
      if (!kind) return NULL;                  /* :nth-child, :last-child, ... */
      {
        sel_cond *c = new_cond(ar, kind);
        *tail = c;
        tail = &c->next;
      }
      cx->b++;
      got = 1;
      continue;
    }
    break;
  }

  if (!got) return NULL;
  *pp = p;
  return s;
}

static sel_complex *parse_complex(arena *ar, const char **pp) {
  const char *p = *pp;
  sel_complex *cx = (sel_complex *)arena_alloc(ar, sizeof *cx);
  sel_seq *prev = NULL;
  int comb = COMB_NONE;

  cx->key = NULL;
  cx->a = cx->b = cx->c = 0;
  cx->pseudo_elem = 0;
  cx->next = NULL;

  for (;;) {
    sel_seq *s = parse_compound(ar, &p, cx);
    int ws;
    if (!s) return NULL;
    s->prev = prev;
    s->comb = comb;
    prev = s;

    ws = skip_ws(&p);
    if (*p == '>') { comb = COMB_CHILD; p++; skip_ws(&p); continue; }
    if (*p == '+') { comb = COMB_ADJ; p++; skip_ws(&p); continue; }
    if (*p == '~') return NULL;                /* general sibling: Selectors 3 */
    if (*p == '\0' || *p == ',') break;
    if (ws) { comb = COMB_DESC; continue; }
    return NULL;                               /* junk we do not understand */
  }

  cx->key = prev;
  *pp = p;
  return cx;
}

sel_list *sel_parse(arena *a, const char *text) {
  sel_list *sl;
  sel_complex *head = NULL, **tail = &head;
  const char *p;

  if (!a || !text) return NULL;
  p = text;
  skip_ws(&p);
  if (!*p) return NULL;                        /* an empty list is not a list */

  for (;;) {
    sel_complex *cx = parse_complex(a, &p);
    if (!cx) return NULL;                      /* 4.1.7: poisons the whole group */
    *tail = cx;
    tail = &cx->next;
    skip_ws(&p);
    if (*p == ',') {
      p++;
      skip_ws(&p);
      if (!*p) return NULL;                    /* trailing comma */
      continue;
    }
    if (*p == '\0') break;
    return NULL;
  }

  sl = (sel_list *)arena_alloc(a, sizeof *sl);
  sl->first = head;
  return sl;
}

/* --------------------------------------------------------------- matching */

static dom_node *prev_element(dom_node *n) {
  dom_node *p = n->prev_sibling;
  while (p && p->type != DOM_ELEMENT) p = p->prev_sibling;
  return p;
}

/* [att~=v]: v is one of the space-separated words of `list`.  An empty v or
 * one containing whitespace can never match (CSS 2.1 5.8.1). */
static int has_word(const char *list, const char *word) {
  const char *p = list;
  size_t wl;

  if (!list || !word || !*word) return 0;
  wl = strlen(word);
  while (*p) {
    const char *s;
    while (*p && s_is_space((unsigned char)*p)) p++;
    s = p;
    while (*p && !s_is_space((unsigned char)*p)) p++;
    if ((size_t)(p - s) == wl && memcmp(s, word, wl) == 0) return 1;
  }
  return 0;
}

/* [att|=v]: exactly v, or v immediately followed by '-'. */
static int dashmatch(const char *v, const char *want) {
  size_t n;
  if (!v || !want) return 0;
  n = strlen(want);
  if (strncmp(v, want, n) != 0) return 0;
  return v[n] == '\0' || v[n] == '-';
}

/* :lang(C) -- the element's language comes from the nearest `lang` (or
 * `xml:lang`) attribute at or above it; codes compare case-insensitively. */
static const char *node_lang(dom_node *n) {
  dom_node *p;
  for (p = n; p; p = p->parent) {
    const char *v;
    if (p->type != DOM_ELEMENT) continue;
    v = dom_attr_get(p, "lang");
    if (v && *v) return v;
    v = dom_attr_get(p, "xml:lang");
    if (v && *v) return v;
  }
  return NULL;
}

static int lang_matches(const char *lang, const char *want) {
  size_t i, n;
  if (!lang || !want) return 0;
  n = strlen(want);
  if (n == 0) return 0;
  for (i = 0; i < n; i++)
    if (s_lc((unsigned char)lang[i]) != s_lc((unsigned char)want[i])) return 0;
  return lang[n] == '\0' || lang[n] == '-';
}

static int match_compound(sel_seq *s, dom_node *n) {
  sel_cond *c;

  if (n->type != DOM_ELEMENT) return 0;
  if (s->tag && !s_ieq(s->tag, n->tag)) return 0;   /* HTML: case-insensitive */

  for (c = s->conds; c; c = c->next) {
    switch (c->kind) {
      case C_CLASS:
        /* class values are case-SENSITIVE */
        if (!has_word(dom_attr_get(n, "class"), c->name)) return 0;
        break;
      case C_ID: {
        const char *v = dom_attr_get(n, "id");
        if (!v || strcmp(v, c->name) != 0) return 0;
        break;
      }
      case C_ATTR: {
        const char *v = dom_attr_get(n, c->name);   /* name: case-insensitive */
        if (!v) return 0;
        switch (c->op) {
          case A_EXISTS: break;
          case A_EQ: if (strcmp(v, c->value) != 0) return 0; break;
          case A_INCLUDES: if (!has_word(v, c->value)) return 0; break;
          case A_DASHMATCH: if (!dashmatch(v, c->value)) return 0; break;
          default: return 0;
        }
        break;
      }
      case C_FIRST_CHILD:
        if (!n->parent || prev_element(n)) return 0;
        break;
      case C_LINK:
        if (!s_ieq(n->tag, "a") || !dom_attr_get(n, "href")) return 0;
        break;
      case C_NEVER:
        return 0;                 /* static document: no visited/hover/active/focus */
      case C_LANG:
        if (!lang_matches(node_lang(n), c->name)) return 0;
        break;
      default:
        return 0;
    }
  }
  return 1;
}

static int match_chain(sel_seq *s, dom_node *n) {
  if (!match_compound(s, n)) return 0;
  if (!s->prev) return 1;
  switch (s->comb) {
    case COMB_CHILD: {
      dom_node *p = n->parent;
      return (p && p->type == DOM_ELEMENT) ? match_chain(s->prev, p) : 0;
    }
    case COMB_ADJ: {
      dom_node *p = prev_element(n);
      return p ? match_chain(s->prev, p) : 0;
    }
    case COMB_DESC:
    default: {
      dom_node *p;
      for (p = n->parent; p && p->type == DOM_ELEMENT; p = p->parent)
        if (match_chain(s->prev, p)) return 1;
      return 0;
    }
  }
}

int sel_matches(sel_list *s, dom_node *n) {
  sel_complex *cx;
  if (!s || !n || n->type != DOM_ELEMENT) return 0;
  for (cx = s->first; cx; cx = cx->next) {
    if (cx->pseudo_elem) continue;             /* 5.10: never matches an element */
    if (match_chain(cx->key, n)) return 1;
  }
  return 0;
}

static int spec_gt(int a1, int b1, int c1, int a2, int b2, int c2) {
  if (a1 != a2) return a1 > a2;
  if (b1 != b2) return b1 > b2;
  return c1 > c2;
}

int sel_specificity_for(sel_list *s, dom_node *n, int *a_, int *b_, int *c_) {
  int found = 0, ba = 0, bb = 0, bc = 0;

  if (s && n && n->type == DOM_ELEMENT) {
    sel_complex *cx;
    for (cx = s->first; cx; cx = cx->next) {
      if (cx->pseudo_elem) continue;
      if (!match_chain(cx->key, n)) continue;
      if (!found || spec_gt(cx->a, cx->b, cx->c, ba, bb, bc)) {
        ba = cx->a;
        bb = cx->b;
        bc = cx->c;
      }
      found = 1;
    }
  }
  if (a_) *a_ = found ? ba : 0;
  if (b_) *b_ = found ? bb : 0;
  if (c_) *c_ = found ? bc : 0;
  return found;
}

int sel_specificity(arena *a, const char *one_complex_selector, int *a_, int *b_, int *c_) {
  sel_list *sl;

  if (a_) *a_ = 0;
  if (b_) *b_ = 0;
  if (c_) *c_ = 0;
  if (!a || !one_complex_selector) return 0;

  sl = sel_parse(a, one_complex_selector);
  if (!sl || !sl->first) return 0;
  if (sl->first->next) return 0;               /* a comma group is not "one" */

  if (a_) *a_ = sl->first->a;
  if (b_) *b_ = sl->first->b;
  if (c_) *c_ = sl->first->c;
  return 1;
}
