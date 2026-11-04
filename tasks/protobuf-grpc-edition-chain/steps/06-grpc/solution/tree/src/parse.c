/* parse.c -- recursive-descent .proto parser.
 *
 * Semantic-free by contract (see ast.h): it accepts the grammar of proto2,
 * proto3, edition 2023 and edition 2024 exactly, and resolves nothing. Type
 * names stay as written, options stay as name-parts plus a value, and neither
 * map-entry messages nor proto3 synthetic oneofs are invented here -- build.c
 * owns all of that.
 *
 * Errors are reported through Diags and the parser recovers to the next `;`
 * or `}` so one mistake does not cascade, which is what lets a corpus file
 * with a single deliberate fault produce one diagnostic instead of twenty.
 */
#include "ast.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdarg.h>

typedef struct {
  Tokens t;
  size_t i;
  Arena *a;
  Diags *d;
  const char *file;
  AstFile *f;
} P;

static Token *cur(P *p) { return &p->t.v[p->i]; }
static Token *ahead(P *p, size_t k) {
  size_t j = p->i + k;
  return &p->t.v[j < p->t.n ? j : p->t.n - 1];
}
static void adv(P *p) { if (p->i + 1 < p->t.n) p->i++; }

static void perr(P *p, const char *fmt, ...) {
  Token *t = cur(p);
  va_list ap;
  va_start(ap, fmt);
  char msg[512];
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  diag_add(p->d, p->file, t->line, t->col, "%s", msg);
}

static bool is_sym(P *p, char c) {
  Token *t = cur(p);
  return t->kind == TK_SYM && t->sym == c;
}
static bool accept_sym(P *p, char c) { if (is_sym(p, c)) { adv(p); return true; } return false; }
static bool is_kw(P *p, const char *kw) {
  Token *t = cur(p);
  return t->kind == TK_IDENT && str_eq_c(t->text, kw);
}
static bool accept_kw(P *p, const char *kw) { if (is_kw(p, kw)) { adv(p); return true; } return false; }

static bool expect_sym(P *p, char c) {
  if (accept_sym(p, c)) return true;
  perr(p, "Expected \"%c\".", c);
  return false;
}

static bool expect_ident(P *p, Str *out) {
  Token *t = cur(p);
  if (t->kind != TK_IDENT) { perr(p, "Expected identifier, got: %.*s", (int)t->text.n, t->text.p); return false; }
  *out = t->text;
  adv(p);
  return true;
}

/* Skip to just past the next `;` at this nesting level, or to the closing
 * `}`. Keeps one syntax error from cascading. */
static void recover(P *p) {
  int depth = 0;
  while (cur(p)->kind != TK_EOF) {
    if (is_sym(p, '{')) depth++;
    else if (is_sym(p, '}')) {
      if (depth == 0) return;
      depth--;
    } else if (is_sym(p, ';') && depth == 0) { adv(p); return; }
    adv(p);
  }
}

/* A dotted type reference as written: `Foo`, `foo.Bar`, `.foo.Bar`. */
static bool parse_type_name(P *p, Str *out) {
  const char *start = cur(p)->text.p;
  const char *end = start;
  if (is_sym(p, '.')) { end = cur(p)->text.p + 1; adv(p); }
  Token *t = cur(p);
  if (t->kind != TK_IDENT) { perr(p, "Expected type name."); return false; }
  end = t->text.p + t->text.n;
  adv(p);
  while (is_sym(p, '.') && ahead(p, 1)->kind == TK_IDENT) {
    adv(p);
    end = cur(p)->text.p + cur(p)->text.n;
    adv(p);
  }
  *out = str_of(start, (size_t)(end - start));
  return true;
}

/* ------------------------------------------------------------- values */

static AstValue *parse_value(P *p);

static AstAggField *parse_agg_body(P *p, char close) {
  AstAggField *head = NULL, **tail = &head;
  while (!is_sym(p, close) && cur(p)->kind != TK_EOF) {
    AstAggField *fld = arena_calloc(p->a, sizeof(AstAggField));
    fld->line = cur(p)->line;
    fld->col = cur(p)->col;
    if (accept_sym(p, '[')) {
      /* `[type.googleapis.com/foo.Bar]` (Any) or `[pkg.ext]` (extension) */
      const char *s = cur(p)->text.p;
      const char *e = s;
      while (!is_sym(p, ']') && cur(p)->kind != TK_EOF) {
        e = cur(p)->text.p + cur(p)->text.n;
        adv(p);
      }
      Str whole = str_of(s, (size_t)(e - s));
      const char *slash = memchr(whole.p, '/', whole.n);
      if (slash) {
        fld->any_prefix = str_of(whole.p, (size_t)(slash - whole.p));
        fld->name = str_of(slash + 1, whole.n - (size_t)(slash + 1 - whole.p));
      } else {
        fld->name = whole;
      }
      fld->is_extension = true;
      expect_sym(p, ']');
    } else {
      Str nm;
      if (!expect_ident(p, &nm)) { recover(p); continue; }
      fld->name = nm;
    }
    if (accept_sym(p, ':')) fld->has_colon = true;
    if (is_sym(p, '[')) {
      /* a list value */
      adv(p);
      AstValue *lv = arena_calloc(p->a, sizeof(AstValue));
      lv->kind = AV_LIST;
      AstValue **lt = &lv->list;
      while (!is_sym(p, ']') && cur(p)->kind != TK_EOF) {
        AstValue *e = parse_value(p);
        if (!e) break;
        *lt = e; lt = &e->next;
        if (!accept_sym(p, ',')) break;
      }
      expect_sym(p, ']');
      fld->value = lv;
    } else {
      fld->value = parse_value(p);
    }
    accept_sym(p, ',');
    accept_sym(p, ';');
    *tail = fld; tail = &fld->next;
  }
  return head;
}

static AstValue *parse_value(P *p) {
  AstValue *v = arena_calloc(p->a, sizeof(AstValue));
  v->line = cur(p)->line;
  v->col = cur(p)->col;

  if (accept_sym(p, '-')) v->negative = true;
  else if (is_sym(p, '+')) adv(p);

  Token *t = cur(p);
  switch (t->kind) {
    case TK_INT:
      v->kind = AV_INT; v->ival = t->ival; v->ovf = t->ovf; adv(p); return v;
    case TK_FLOAT:
      v->kind = AV_FLOAT; v->fval = t->fval; adv(p); return v;
    case TK_IDENT:
      v->kind = AV_IDENT; v->sval = t->text; adv(p); return v;
    case TK_STR: {
      /* Adjacent string literals concatenate. */
      Buf b; buf_init(&b, p->a);
      while (cur(p)->kind == TK_STR) {
        buf_put(&b, cur(p)->sval.p, cur(p)->sval.n);
        adv(p);
      }
      v->kind = AV_STR;
      v->sval = buf_str(&b);
      return v;
    }
    case TK_SYM:
      if (t->sym == '{' || t->sym == '<') {
        char close = t->sym == '{' ? '}' : '>';
        const char *body_start = t->text.p + 1;
        adv(p);
        v->kind = AV_AGG;
        v->agg = parse_agg_body(p, close);
        const char *body_end = cur(p)->text.p;
        v->raw = str_of(body_start, (size_t)(body_end > body_start ? body_end - body_start : 0));
        expect_sym(p, close);
        return v;
      }
      break;
    default: break;
  }
  perr(p, "Expected option value.");
  return NULL;
}

/* --------------------------------------------------------------- options */

/* `option name = value;` where name is `a.b`, `(pkg.ext)`, or a mix. */
static AstOption *parse_option_name_and_value(P *p) {
  AstOption *o = arena_calloc(p->a, sizeof(AstOption));
  o->line = cur(p)->line;
  o->col = cur(p)->col;
  AstOptName **tail = &o->name;
  for (;;) {
    AstOptName *n = arena_calloc(p->a, sizeof(AstOptName));
    n->line = cur(p)->line;
    n->col = cur(p)->col;
    if (accept_sym(p, '(')) {
      Str tn;
      if (!parse_type_name(p, &tn)) return NULL;
      n->name = tn;
      n->is_extension = true;
      if (!expect_sym(p, ')')) return NULL;
    } else {
      Str nm;
      if (!expect_ident(p, &nm)) return NULL;
      n->name = nm;
    }
    *tail = n; tail = &n->next;
    if (!accept_sym(p, '.')) break;
  }
  if (!expect_sym(p, '=')) return NULL;
  o->value = parse_value(p);
  return o;
}

static AstOption *parse_option_stmt(P *p) {
  AstOption *o = parse_option_name_and_value(p);
  if (!o) { recover(p); return NULL; }
  expect_sym(p, ';');
  return o;
}

/* `[a = 1, (b).c = 2]` after a field or enum value. */
static AstOption *parse_field_options(P *p, AstValue **default_out,
                                      Str *json_name_out, bool *has_json) {
  AstOption *head = NULL, **tail = &head;
  if (!accept_sym(p, '[')) return NULL;
  for (;;) {
    if (is_kw(p, "default") && ahead(p, 1)->kind == TK_SYM && ahead(p, 1)->sym == '=') {
      adv(p); adv(p);
      *default_out = parse_value(p);
    } else if (is_kw(p, "json_name") && ahead(p, 1)->kind == TK_SYM && ahead(p, 1)->sym == '=') {
      adv(p); adv(p);
      Token *t = cur(p);
      if (t->kind != TK_STR) { perr(p, "Expected string for json_name."); }
      else { *json_name_out = t->sval; *has_json = true; adv(p); }
    } else {
      AstOption *o = parse_option_name_and_value(p);
      if (!o) break;
      *tail = o; tail = &o->next;
    }
    if (!accept_sym(p, ',')) break;
  }
  expect_sym(p, ']');
  return head;
}

/* ---------------------------------------------------------------- ranges */

static AstRange *parse_ranges(P *p) {
  AstRange *head = NULL, **tail = &head;
  for (;;) {
    AstRange *r = arena_calloc(p->a, sizeof(AstRange));
    r->line = cur(p)->line;
    r->col = cur(p)->col;
    bool neg = accept_sym(p, '-');
    Token *t = cur(p);
    if (t->kind != TK_INT) { perr(p, "Expected field number range."); return head; }
    r->start = neg ? -(int64_t)t->ival : (int64_t)t->ival;
    adv(p);
    if (accept_kw(p, "to")) {
      r->has_end = true;
      if (accept_kw(p, "max")) r->end_is_max = true;
      else {
        bool n2 = accept_sym(p, '-');
        Token *e = cur(p);
        if (e->kind != TK_INT) { perr(p, "Expected integer or \"max\"."); return head; }
        r->end = n2 ? -(int64_t)e->ival : (int64_t)e->ival;
        adv(p);
      }
    }
    *tail = r; tail = &r->next;
    if (!accept_sym(p, ',')) break;
  }
  return head;
}

/* protoc refuses to mix numbers and names in one `reserved` statement, so the
 * form is decided by the first token and a mismatch is an error. */
static AstReserved *parse_reserved(P *p) {
  AstReserved *r = arena_calloc(p->a, sizeof(AstReserved));
  r->line = cur(p)->line;
  r->col = cur(p)->col;
  if (cur(p)->kind == TK_STR) {
    AstRName **tail = &r->names;
    for (;;) {
      Token *t = cur(p);
      if (t->kind != TK_STR) { perr(p, "Expected string."); break; }
      AstRName *n = arena_calloc(p->a, sizeof(AstRName));
      n->name = t->sval;
      n->quoted = true;
      n->line = t->line;
      n->col = t->col;
      *tail = n; tail = &n->next;
      adv(p);
      if (!accept_sym(p, ',')) break;
    }
  } else if (cur(p)->kind == TK_IDENT && !is_kw(p, "max")) {
    /* editions allow bare identifiers as reserved names */
    AstRName **tail = &r->names;
    for (;;) {
      Str nm;
      if (!expect_ident(p, &nm)) break;
      AstRName *n = arena_calloc(p->a, sizeof(AstRName));
      n->name = nm;
      *tail = n; tail = &n->next;
      if (!accept_sym(p, ',')) break;
    }
  } else {
    r->ranges = parse_ranges(p);
  }
  expect_sym(p, ';');
  return r;
}

/* ---------------------------------------------------------------- fields */

static AstMessage *parse_message_body(P *p, Str name, int visibility, bool is_group);

static AstField *parse_field(P *p, int label, AstMessage *owner, int oneof_index) {
  AstField *f = arena_calloc(p->a, sizeof(AstField));
  f->line = cur(p)->line;
  f->col = cur(p)->col;
  f->label = label;
  f->oneof_index = oneof_index;

  if (is_kw(p, "group")) {
    adv(p);
    f->kind = AST_FIELD_GROUP;
    Str gname;
    if (!expect_ident(p, &gname)) { recover(p); return NULL; }
    f->group_name = gname;
    f->type_name = gname;
    f->builtin = T_GROUP;
    /* the field name is the group name lower-cased; build.c applies the rule */
    f->name = gname;
    f->name_line = cur(p)->line;
    f->name_col = cur(p)->col;
    if (!expect_sym(p, '=')) { recover(p); return NULL; }
    Token *num = cur(p);
    if (num->kind != TK_INT) { perr(p, "Expected field number."); recover(p); return NULL; }
    f->number = num->ival;
    f->has_number = true;
    f->number_ovf = num->ovf;
    adv(p);
    if (is_sym(p, '[')) f->options = parse_field_options(p, &f->default_value, &f->json_name, &f->has_json_name);
    if (!expect_sym(p, '{')) { recover(p); return NULL; }
    f->group_body = parse_message_body(p, gname, AST_VIS_UNSET, true);
    if (owner && f->group_body) {
      AstMessage **mt = &owner->messages;
      while (*mt) mt = &(*mt)->next;
      *mt = f->group_body;
    }
    return f;
  }

  if (is_kw(p, "map") && ahead(p, 1)->kind == TK_SYM && ahead(p, 1)->sym == '<') {
    adv(p); adv(p);
    f->kind = AST_FIELD_MAP;
    Str kt;
    if (!parse_type_name(p, &kt)) { recover(p); return NULL; }
    f->key_type = kt;
    f->key_builtin = ast_builtin_type(kt);
    if (!expect_sym(p, ',')) { recover(p); return NULL; }
    Str vt;
    if (!parse_type_name(p, &vt)) { recover(p); return NULL; }
    f->value_type = vt;
    f->value_builtin = ast_builtin_type(vt);
    if (!expect_sym(p, '>')) { recover(p); return NULL; }
  } else {
    f->kind = AST_FIELD_NORMAL;
    Str tn;
    if (!parse_type_name(p, &tn)) { recover(p); return NULL; }
    f->type_name = tn;
    f->builtin = ast_builtin_type(tn);
  }

  Str nm;
  f->name_line = cur(p)->line;
  f->name_col = cur(p)->col;
  if (!expect_ident(p, &nm)) { recover(p); return NULL; }
  f->name = nm;
  if (!expect_sym(p, '=')) { recover(p); return NULL; }
  Token *num = cur(p);
  if (num->kind != TK_INT) { perr(p, "Expected field number."); recover(p); return NULL; }
  f->number = num->ival;
  f->has_number = true;
  f->number_ovf = num->ovf;
  adv(p);
  if (is_sym(p, '[')) f->options = parse_field_options(p, &f->default_value, &f->json_name, &f->has_json_name);
  expect_sym(p, ';');
  return f;
}

/* ----------------------------------------------------------------- enums */

static AstEnum *parse_enum(P *p, int visibility) {
  AstEnum *e = arena_calloc(p->a, sizeof(AstEnum));
  e->line = cur(p)->line;
  e->col = cur(p)->col;
  e->visibility = visibility;
  Str nm;
  if (!expect_ident(p, &nm)) { recover(p); return NULL; }
  e->name = nm;
  if (!expect_sym(p, '{')) { recover(p); return NULL; }

  AstEnumValue **vt = &e->values;
  AstOption **ot = &e->options;
  AstReserved **rt = &e->reserved;
  while (!is_sym(p, '}') && cur(p)->kind != TK_EOF) {
    if (accept_sym(p, ';')) continue;
    if (accept_kw(p, "option")) {
      AstOption *o = parse_option_stmt(p);
      if (o) { *ot = o; ot = &o->next; }
      continue;
    }
    if (accept_kw(p, "reserved")) {
      AstReserved *r = parse_reserved(p);
      if (r) { *rt = r; rt = &r->next; }
      continue;
    }
    AstEnumValue *v = arena_calloc(p->a, sizeof(AstEnumValue));
    v->line = cur(p)->line;
    v->col = cur(p)->col;
    Str vn;
    if (!expect_ident(p, &vn)) { recover(p); continue; }
    v->name = vn;
    if (!expect_sym(p, '=')) { recover(p); continue; }
    bool neg = accept_sym(p, '-');
    Token *t = cur(p);
    if (t->kind != TK_INT) { perr(p, "Expected numeric value for enum constant."); recover(p); continue; }
    v->number = neg ? -(int64_t)t->ival : (int64_t)t->ival;
    v->has_number = true;
    v->number_ovf = t->ovf;
    adv(p);
    if (is_sym(p, '[')) {
      AstValue *dummy = NULL; Str js = str_of("", 0); bool hj = false;
      v->options = parse_field_options(p, &dummy, &js, &hj);
    }
    expect_sym(p, ';');
    *vt = v; vt = &v->next;
  }
  expect_sym(p, '}');
  return e;
}

/* -------------------------------------------------------------- messages */

static AstExtend *parse_extend(P *p, AstMessage *owner) {
  AstExtend *x = arena_calloc(p->a, sizeof(AstExtend));
  x->line = cur(p)->line;
  x->col = cur(p)->col;
  Str tn;
  if (!parse_type_name(p, &tn)) { recover(p); return NULL; }
  x->extendee = tn;
  if (!expect_sym(p, '{')) { recover(p); return NULL; }
  AstField **ft = &x->fields;
  while (!is_sym(p, '}') && cur(p)->kind != TK_EOF) {
    if (accept_sym(p, ';')) continue;
    int label = 0;
    if (accept_kw(p, "optional")) label = L_OPTIONAL;
    else if (accept_kw(p, "required")) label = L_REQUIRED;
    else if (accept_kw(p, "repeated")) label = L_REPEATED;
    AstField *f = parse_field(p, label, owner, -1);
    if (f) {
      f->is_extension = true;
      f->extendee = x->extendee;
      *ft = f; ft = &f->next;
    }
  }
  expect_sym(p, '}');
  return x;
}

static AstMessage *parse_message_body(P *p, Str name, int visibility, bool is_group) {
  AstMessage *m = arena_calloc(p->a, sizeof(AstMessage));
  m->name = name;
  m->visibility = visibility;
  m->is_group = is_group;
  m->line = cur(p)->line;
  m->col = cur(p)->col;

  AstField **ft = &m->fields;
  AstEnum **et = &m->enums;
  AstExtend **xt = &m->extends;
  AstExtRange **rt = &m->ext_ranges;
  AstReserved **vt = &m->reserved;
  AstOption **ot = &m->options;
  AstOneof **nt = &m->oneofs;
  int oneof_n = 0;

  while (!is_sym(p, '}') && cur(p)->kind != TK_EOF) {
    if (accept_sym(p, ';')) continue;

    int vis = AST_VIS_UNSET;
    if (is_kw(p, "local") && (ahead(p, 1)->kind == TK_IDENT &&
        (str_eq_c(ahead(p, 1)->text, "message") || str_eq_c(ahead(p, 1)->text, "enum")))) {
      vis = AST_VIS_LOCAL; adv(p);
    } else if (is_kw(p, "export") && (ahead(p, 1)->kind == TK_IDENT &&
        (str_eq_c(ahead(p, 1)->text, "message") || str_eq_c(ahead(p, 1)->text, "enum")))) {
      vis = AST_VIS_EXPORT; adv(p);
    }

    if (accept_kw(p, "message")) {
      Str nm;
      if (!expect_ident(p, &nm)) { recover(p); continue; }
      if (!expect_sym(p, '{')) { recover(p); continue; }
      AstMessage *n = parse_message_body(p, nm, vis, false);
      /* Walk to the tail rather than trusting a cached one: parse_field
       * appends group bodies to this same list as it goes. */
      if (n) {
        AstMessage **t = &m->messages;
        while (*t) t = &(*t)->next;
        *t = n;
      }
      continue;
    }
    if (accept_kw(p, "enum")) {
      AstEnum *e = parse_enum(p, vis);
      if (e) { *et = e; et = &e->next; }
      continue;
    }
    if (accept_kw(p, "extend")) {
      AstExtend *x = parse_extend(p, m);
      if (x) { *xt = x; xt = &x->next; }
      continue;
    }
    if (accept_kw(p, "option")) {
      AstOption *o = parse_option_stmt(p);
      if (o) { *ot = o; ot = &o->next; }
      continue;
    }
    if (accept_kw(p, "reserved")) {
      AstReserved *r = parse_reserved(p);
      if (r) { *vt = r; vt = &r->next; }
      continue;
    }
    if (accept_kw(p, "extensions")) {
      AstExtRange *r = arena_calloc(p->a, sizeof(AstExtRange));
      r->line = cur(p)->line;
      r->col = cur(p)->col;
      r->ranges = parse_ranges(p);
      if (is_sym(p, '[')) {
        AstValue *dummy = NULL; Str js = str_of("", 0); bool hj = false;
        r->options = parse_field_options(p, &dummy, &js, &hj);
      }
      expect_sym(p, ';');
      *rt = r; rt = &r->next;
      continue;
    }
    if (accept_kw(p, "oneof")) {
      AstOneof *o = arena_calloc(p->a, sizeof(AstOneof));
      o->line = cur(p)->line;
      o->col = cur(p)->col;
      o->index = oneof_n;
      Str nm;
      if (!expect_ident(p, &nm)) { recover(p); continue; }
      o->name = nm;
      if (!expect_sym(p, '{')) { recover(p); continue; }
      AstOption **oot = &o->options;
      while (!is_sym(p, '}') && cur(p)->kind != TK_EOF) {
        if (accept_sym(p, ';')) continue;
        if (accept_kw(p, "option")) {
          AstOption *oo = parse_option_stmt(p);
          if (oo) { *oot = oo; oot = &oo->next; }
          continue;
        }
        AstField *f = parse_field(p, 0, m, oneof_n);
        if (f) { *ft = f; ft = &f->next; }
      }
      expect_sym(p, '}');
      *nt = o; nt = &o->next;
      oneof_n++;
      continue;
    }

    int label = 0;
    if (accept_kw(p, "optional")) label = L_OPTIONAL;
    else if (accept_kw(p, "required")) label = L_REQUIRED;
    else if (accept_kw(p, "repeated")) label = L_REPEATED;
    AstField *f = parse_field(p, label, m, -1);
    if (f) { *ft = f; ft = &f->next; }
  }
  expect_sym(p, '}');
  return m;
}

/* -------------------------------------------------------------- services */

static AstService *parse_service(P *p) {
  AstService *s = arena_calloc(p->a, sizeof(AstService));
  s->line = cur(p)->line;
  s->col = cur(p)->col;
  Str nm;
  if (!expect_ident(p, &nm)) { recover(p); return NULL; }
  s->name = nm;
  if (!expect_sym(p, '{')) { recover(p); return NULL; }
  AstMethod **mt = &s->methods;
  AstOption **ot = &s->options;
  while (!is_sym(p, '}') && cur(p)->kind != TK_EOF) {
    if (accept_sym(p, ';')) continue;
    if (accept_kw(p, "option")) {
      AstOption *o = parse_option_stmt(p);
      if (o) { *ot = o; ot = &o->next; }
      continue;
    }
    if (accept_kw(p, "rpc")) {
      AstMethod *m = arena_calloc(p->a, sizeof(AstMethod));
      m->line = cur(p)->line;
      m->col = cur(p)->col;
      Str mn;
      if (!expect_ident(p, &mn)) { recover(p); continue; }
      m->name = mn;
      if (!expect_sym(p, '(')) { recover(p); continue; }
      if (accept_kw(p, "stream")) m->client_streaming = true;
      if (!parse_type_name(p, &m->input_type)) { recover(p); continue; }
      if (!expect_sym(p, ')')) { recover(p); continue; }
      if (!accept_kw(p, "returns")) { perr(p, "Expected \"returns\"."); recover(p); continue; }
      if (!expect_sym(p, '(')) { recover(p); continue; }
      if (accept_kw(p, "stream")) m->server_streaming = true;
      if (!parse_type_name(p, &m->output_type)) { recover(p); continue; }
      if (!expect_sym(p, ')')) { recover(p); continue; }
      if (accept_sym(p, '{')) {
        m->has_body = true;
        AstOption **mot = &m->options;
        while (!is_sym(p, '}') && cur(p)->kind != TK_EOF) {
          if (accept_sym(p, ';')) continue;
          if (accept_kw(p, "option")) {
            AstOption *o = parse_option_stmt(p);
            if (o) { *mot = o; mot = &o->next; }
            continue;
          }
          perr(p, "Expected \"option\".");
          recover(p);
        }
        expect_sym(p, '}');
      } else {
        expect_sym(p, ';');
      }
      *mt = m; mt = &m->next;
      continue;
    }
    perr(p, "Expected \"rpc\" or \"option\".");
    recover(p);
  }
  expect_sym(p, '}');
  return s;
}

/* ------------------------------------------------------------------ file */

AstFile *ast_parse(Arena *a, Diags *d, const char *filename, Str src) {
  P p;
  p.t = lex_file(a, d, filename, src);
  p.i = 0;
  p.a = a;
  p.d = d;
  p.file = filename;
  p.f = arena_calloc(a, sizeof(AstFile));
  p.f->filename = filename;
  p.f->arena = a;
  p.f->syntax_kind = AST_SYN_ABSENT;
  p.f->edition = ED_PROTO2;

  AstImport **it = &p.f->imports;
  AstOption **ot = &p.f->options;
  AstEnum **et = &p.f->enums;
  AstExtend **xt = &p.f->extends;
  AstService **st = &p.f->services;

  while (cur(&p)->kind != TK_EOF) {
    if (accept_sym(&p, ';')) continue;

    if (is_kw(&p, "syntax") || is_kw(&p, "edition")) {
      bool is_edition = is_kw(&p, "edition");
      p.f->syntax_line = cur(&p)->line;
      p.f->syntax_col = cur(&p)->col;
      adv(&p);
      if (!expect_sym(&p, '=')) { recover(&p); continue; }
      Token *t = cur(&p);
      if (t->kind != TK_STR) { perr(&p, "Expected syntax identifier."); recover(&p); continue; }
      p.f->syntax_text = t->sval;
      adv(&p);
      expect_sym(&p, ';');
      if (is_edition) {
        p.f->syntax_kind = AST_SYN_EDITION;
        if (str_eq_c(p.f->syntax_text, "2023")) p.f->edition = ED_2023;
        else if (str_eq_c(p.f->syntax_text, "2024")) p.f->edition = ED_2024;
        else {
          p.f->edition = 0;
          diag_add(d, filename, p.f->syntax_line, p.f->syntax_col,
                   "Unknown edition \"%.*s\".", (int)p.f->syntax_text.n, p.f->syntax_text.p);
        }
      } else if (str_eq_c(p.f->syntax_text, "proto2")) {
        p.f->syntax_kind = AST_SYN_PROTO2; p.f->edition = ED_PROTO2;
      } else if (str_eq_c(p.f->syntax_text, "proto3")) {
        p.f->syntax_kind = AST_SYN_PROTO3; p.f->edition = ED_PROTO3;
      } else {
        diag_add(d, filename, p.f->syntax_line, p.f->syntax_col,
                 "Expected \"proto2\" or \"proto3\".");
      }
      continue;
    }
    if (accept_kw(&p, "package")) {
      if (p.f->has_package)
        diag_add(d, filename, cur(&p)->line, cur(&p)->col,
                 "Multiple package definitions.");
      p.f->package_line = cur(&p)->line;
      p.f->package_col = cur(&p)->col;
      Str pk;
      if (!parse_type_name(&p, &pk)) { recover(&p); continue; }
      p.f->package = pk;
      p.f->has_package = true;
      expect_sym(&p, ';');
      continue;
    }
    if (accept_kw(&p, "import")) {
      AstImport *im = arena_calloc(a, sizeof(AstImport));
      im->line = cur(&p)->line;
      im->col = cur(&p)->col;
      im->kind = AST_IMP_DEFAULT;
      if (accept_kw(&p, "public")) im->kind = AST_IMP_PUBLIC;
      else if (accept_kw(&p, "weak")) im->kind = AST_IMP_WEAK;
      else if (accept_kw(&p, "option")) im->kind = AST_IMP_OPTION;
      Token *t = cur(&p);
      if (t->kind != TK_STR) { perr(&p, "Expected a string naming the file to import."); recover(&p); continue; }
      im->path = t->sval;
      adv(&p);
      expect_sym(&p, ';');
      *it = im; it = &im->next;
      continue;
    }
    if (accept_kw(&p, "option")) {
      AstOption *o = parse_option_stmt(&p);
      if (o) { *ot = o; ot = &o->next; }
      continue;
    }

    int vis = AST_VIS_UNSET;
    if (is_kw(&p, "local") && ahead(&p, 1)->kind == TK_IDENT &&
        (str_eq_c(ahead(&p, 1)->text, "message") || str_eq_c(ahead(&p, 1)->text, "enum"))) {
      vis = AST_VIS_LOCAL; adv(&p);
    } else if (is_kw(&p, "export") && ahead(&p, 1)->kind == TK_IDENT &&
        (str_eq_c(ahead(&p, 1)->text, "message") || str_eq_c(ahead(&p, 1)->text, "enum"))) {
      vis = AST_VIS_EXPORT; adv(&p);
    }

    if (accept_kw(&p, "message")) {
      Str nm;
      if (!expect_ident(&p, &nm)) { recover(&p); continue; }
      if (!expect_sym(&p, '{')) { recover(&p); continue; }
      AstMessage *m = parse_message_body(&p, nm, vis, false);
      if (m) {
        AstMessage **t = &p.f->messages;
        while (*t) t = &(*t)->next;
        *t = m;
      }
      continue;
    }
    if (accept_kw(&p, "enum")) {
      AstEnum *e = parse_enum(&p, vis);
      if (e) { *et = e; et = &e->next; }
      continue;
    }
    if (accept_kw(&p, "extend")) {
      AstExtend *x = parse_extend(&p, NULL);
      if (x) {
        /* A group inside a file-level extend declares a top-level message.
         * parse_extend had no owner to hang it on, so link it here. */
        for (AstField *fl = x->fields; fl; fl = fl->next)
          if (fl->group_body) {
            AstMessage **t = &p.f->messages;
            while (*t) t = &(*t)->next;
            *t = fl->group_body;
          }
        *xt = x; xt = &x->next;
      }
      continue;
    }
    if (accept_kw(&p, "service")) {
      AstService *s = parse_service(&p);
      if (s) { *st = s; st = &s->next; }
      continue;
    }
    perr(&p, "Expected top-level statement (e.g. \"message\").");
    recover(&p);
  }
  return p.f;
}

/* ------------------------------------------------------------------ dump */

static void dump_msg(FILE *o, const AstMessage *m, int depth) {
  for (const AstMessage *x = m; x; x = x->next) {
    fprintf(o, "%*smessage %.*s%s\n", depth * 2, "", (int)x->name.n, x->name.p,
            x->is_group ? " (group)" : "");
    for (const AstField *f = x->fields; f; f = f->next)
      fprintf(o, "%*sfield %.*s = %" PRIu64 " label=%d kind=%d oneof=%d\n",
              (depth + 1) * 2, "", (int)f->name.n, f->name.p, f->number,
              f->label, (int)f->kind, f->oneof_index);
    for (const AstEnum *e = x->enums; e; e = e->next)
      fprintf(o, "%*senum %.*s\n", (depth + 1) * 2, "", (int)e->name.n, e->name.p);
    dump_msg(o, x->messages, depth + 1);
  }
}

void ast_dump(FILE *out, const AstFile *f) {
  fprintf(out, "file %s syntax=%d edition=%d package=%.*s\n", f->filename,
          (int)f->syntax_kind, f->edition, (int)f->package.n, f->package.p);
  for (const AstImport *i = f->imports; i; i = i->next)
    fprintf(out, "import(%d) %.*s\n", i->kind, (int)i->path.n, i->path.p);
  dump_msg(out, f->messages, 0);
  for (const AstEnum *e = f->enums; e; e = e->next)
    fprintf(out, "enum %.*s\n", (int)e->name.n, e->name.p);
  for (const AstService *s = f->services; s; s = s->next) {
    fprintf(out, "service %.*s\n", (int)s->name.n, s->name.p);
    for (const AstMethod *m = s->methods; m; m = m->next)
      fprintf(out, "  rpc %.*s (%s%.*s) returns (%s%.*s)\n", (int)m->name.n, m->name.p,
              m->client_streaming ? "stream " : "", (int)m->input_type.n, m->input_type.p,
              m->server_streaming ? "stream " : "", (int)m->output_type.n, m->output_type.p);
  }
}
