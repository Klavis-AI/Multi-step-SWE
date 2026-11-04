/* cascade.c -- CSS 2.1 6.4 cascade order over a parsed document.
 *
 * Author-origin only, which is all this milestone needs.  Declarations are
 * sorted highest-first by:
 *
 *   1. weight     -- author !important beats author normal (6.4.1, 6.4.2)
 *   2. origin of the declaration -- a style="" declaration outranks any
 *      selector at the same weight (6.4.3's note on the style attribute)
 *   3. specificity a/b/c, lexicographically (6.4.3)
 *   4. document order -- later wins (6.4.1)
 *
 * Because weight is compared before the style-attribute rank, the full order
 * is: important inline > important selector > normal inline > normal selector.
 *
 * Sheets are gathered in document order (every <style> element's text) and the
 * caller's `extra` sheets are appended after them, so an extra sheet wins ties
 * against the document's own.  At-rules are skipped outright, along with the
 * rules nested inside them.
 * Shorthand expansion is not done here either.
 */
#include "cascade.h"
#include "selector.h"

#include <stddef.h>
#include <string.h>

static int c_lc(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static int c_ieq(const char *x, const char *y) {
  if (!x || !y) return 0;
  while (*x && *y) {
    if (c_lc((unsigned char)*x) != c_lc((unsigned char)*y)) return 0;
    x++;
    y++;
  }
  return *x == '\0' && *y == '\0';
}

/* ------------------------------------------------------------------ model */

typedef struct {
  int important;      /* 1 = !important */
  int inlin;          /* 1 = came from a style="" attribute */
  int a, b, c;        /* specificity of the selector that matched */
  long order;         /* position in the concatenated author declarations */
} cas_key;

typedef struct cas_prop {
  char *prop;
  char *value;
  cas_key key;
  struct cas_prop *next;
} cas_prop;

typedef struct cas_node {
  const dom_node *n;
  cas_prop *props;
  size_t nprops;
  const char **names;              /* lazily built, invalidated on insert */
  struct cas_node *hnext;
} cas_node;

struct cascade {
  arena *a;
  cas_node **buckets;
  size_t nb, count;
  const char **empty;
};

static const char *cas_static_empty[1] = { NULL };

/* Higher wins.  Orders are unique, so the final comparison is a total order. */
static int key_wins(const cas_key *k, const cas_key *old) {
  if (k->important != old->important) return k->important > old->important;
  if (k->inlin != old->inlin) return k->inlin > old->inlin;
  if (k->a != old->a) return k->a > old->a;
  if (k->b != old->b) return k->b > old->b;
  if (k->c != old->c) return k->c > old->c;
  return k->order >= old->order;
}

/* -------------------------------------------------- node -> winners table */

static size_t ptr_hash(const void *p) {
  unsigned char buf[sizeof(void *)];
  size_t h = (size_t)1469598103934665603ULL;
  size_t i;
  memcpy(buf, &p, sizeof buf);
  for (i = 0; i < sizeof buf; i++) {
    h ^= (size_t)buf[i];
    h *= (size_t)1099511628211ULL;
  }
  return h;
}

static void cas_grow(cascade *c) {
  size_t nnb = c->nb * 2, i;
  cas_node **nb = (cas_node **)arena_alloc(c->a, nnb * sizeof *nb);
  memset(nb, 0, nnb * sizeof *nb);
  for (i = 0; i < c->nb; i++) {
    cas_node *e = c->buckets[i];
    while (e) {
      cas_node *nx = e->hnext;
      size_t j = ptr_hash(e->n) & (nnb - 1);
      e->hnext = nb[j];
      nb[j] = e;
      e = nx;
    }
  }
  c->buckets = nb;
  c->nb = nnb;
}

static cas_node *cas_find(cascade *c, const dom_node *n, int create) {
  size_t h = ptr_hash(n), i = h & (c->nb - 1);
  cas_node *e;

  for (e = c->buckets[i]; e; e = e->hnext)
    if (e->n == n) return e;
  if (!create) return NULL;

  if (c->count + 1 > c->nb - c->nb / 4) {
    cas_grow(c);
    i = h & (c->nb - 1);
  }
  e = (cas_node *)arena_alloc(c->a, sizeof *e);
  e->n = n;
  e->props = NULL;
  e->nprops = 0;
  e->names = NULL;
  e->hnext = c->buckets[i];
  c->buckets[i] = e;
  c->count++;
  return e;
}

static void cas_apply(cascade *c, const dom_node *n, const css_decl *d, const cas_key *k) {
  cas_node *e;
  cas_prop *p, **tail;

  if (!d->prop || !d->value) return;
  e = cas_find(c, n, 1);
  tail = &e->props;
  for (p = e->props; p; p = p->next) {
    if (strcmp(p->prop, d->prop) == 0) {
      if (key_wins(k, &p->key)) {
        p->value = arena_strdup(c->a, d->value);
        p->key = *k;
      }
      return;
    }
    tail = &p->next;
  }
  p = (cas_prop *)arena_alloc(c->a, sizeof *p);
  p->prop = arena_strdup(c->a, d->prop);
  p->value = arena_strdup(c->a, d->value);
  p->key = *k;
  p->next = NULL;
  *tail = p;                       /* append: cascade_props keeps first-seen order */
  e->nprops++;
  e->names = NULL;
}

/* ------------------------------------------------------------- collection */

static void collect_styles(arena *a, dom_node *n, vec *out) {
  dom_node *ch;

  if (!n) return;
  if (n->type == DOM_ELEMENT && c_ieq(n->tag, "style")) {
    strbuf sb;
    sb_init(&sb, a);
    for (ch = n->first_child; ch; ch = ch->next_sibling)
      if (ch->type == DOM_TEXT && ch->text) sb_puts(&sb, ch->text);
    vec_push(out, css_parse_sheet(a, sb_done(&sb)));
    return;                        /* a <style> holds text, never elements */
  }
  for (ch = n->first_child; ch; ch = ch->next_sibling) collect_styles(a, ch, out);
}

/* The document node itself is not an element, and the front end hands us
 * <html> as the root, so test the type rather than assume a shape. */
static void collect_elems(dom_node *n, vec *out) {
  dom_node *ch;
  if (!n) return;
  if (n->type == DOM_ELEMENT) vec_push(out, n);
  for (ch = n->first_child; ch; ch = ch->next_sibling) collect_elems(ch, out);
}

/* -------------------------------------------------------------------- api */

cascade *cascade_build(arena *a, dom_doc *doc, css_sheet **extra, int n_extra) {
  cascade *c;
  vec sheets, elems;
  long order = 0;
  size_t si, ei;
  int i;

  if (!a) return NULL;

  c = (cascade *)arena_alloc(a, sizeof *c);
  c->a = a;
  c->nb = 64;
  c->count = 0;
  c->buckets = (cas_node **)arena_alloc(a, c->nb * sizeof *c->buckets);
  memset(c->buckets, 0, c->nb * sizeof *c->buckets);
  {
    const char **e = (const char **)arena_alloc(a, sizeof(const char *));
    e[0] = NULL;
    c->empty = e;
  }

  vec_init(&sheets, a);
  vec_init(&elems, a);
  if (doc && doc->root) {
    collect_styles(a, doc->root, &sheets);
    collect_elems(doc->root, &elems);
  }
  for (i = 0; i < n_extra; i++)
    if (extra && extra[i]) vec_push(&sheets, extra[i]);

  for (si = 0; si < sheets.len; si++) {
    css_sheet *sh = (css_sheet *)sheets.v[si];
    css_rule *r;
    if (!sh) continue;
    for (r = sh->rules; r; r = r->next) {
      const css_decl *d;
      sel_list *sl;
      long after = order;

      /* At-rules (and their `children`) are out of scope for this milestone;
       * stepping over them must be harmless, never fatal. */
      if (r->kind != CSS_STYLE_RULE) continue;
      if (!r->prelude || !*r->prelude) continue;
      for (d = r->decls; d; d = d->next) after++;
      if (after == order) continue;                /* empty declaration block */

      /* Parse the whole prelude, not r->selectors[] one by one: CSS 2.1 4.1.7
       * says one unparsable member invalidates the entire group, so the rule
       * is dropped wholesale. */
      sl = sel_parse(a, r->prelude);
      if (sl) {
        for (ei = 0; ei < elems.len; ei++) {
          dom_node *n = (dom_node *)elems.v[ei];
          cas_key k;
          long o = order;
          if (!sel_specificity_for(sl, n, &k.a, &k.b, &k.c)) continue;
          k.inlin = 0;
          for (d = r->decls; d; d = d->next) {
            k.important = d->important;
            k.order = o++;
            cas_apply(c, n, d, &k);
          }
        }
      }
      order = after;
    }
  }

  /* style="" last: it only ever competes with itself on order. */
  for (ei = 0; ei < elems.len; ei++) {
    dom_node *n = (dom_node *)elems.v[ei];
    const char *st = dom_attr_get(n, "style");
    css_decl *d;
    if (!st || !*st) continue;
    for (d = css_parse_decls(a, st); d; d = d->next) {
      cas_key k;
      k.important = d->important;
      k.inlin = 1;
      k.a = 0;
      k.b = 0;
      k.c = 0;
      k.order = order++;
      cas_apply(c, n, d, &k);
    }
  }

  return c;
}

const char *cascade_value(cascade *c, dom_node *n, const char *prop, int *important) {
  cas_node *e;
  cas_prop *p;

  if (important) *important = 0;
  if (!c || !n || !prop) return NULL;
  e = cas_find(c, n, 0);
  if (!e) return NULL;

  for (p = e->props; p; p = p->next)
    if (strcmp(p->prop, prop) == 0) {
      if (important) *important = p->key.important;
      return p->value;
    }
  /* CSS 2.1 property names are case-insensitive; the exact pass runs first so
   * that case-sensitive custom properties, if the parser ever emits any, keep
   * their identity. */
  for (p = e->props; p; p = p->next)
    if (c_ieq(p->prop, prop)) {
      if (important) *important = p->key.important;
      return p->value;
    }
  return NULL;
}

const char **cascade_props(cascade *c, dom_node *n) {
  cas_node *e;
  cas_prop *p;
  const char **out;
  size_t i;

  if (!c) return cas_static_empty;
  if (!n) return c->empty;
  e = cas_find(c, n, 0);
  if (!e || !e->props) return c->empty;
  if (e->names) return e->names;

  out = (const char **)arena_alloc(c->a, (e->nprops + 1) * sizeof(const char *));
  i = 0;
  for (p = e->props; p; p = p->next) out[i++] = p->prop;
  out[i] = NULL;
  e->names = out;
  return out;
}
