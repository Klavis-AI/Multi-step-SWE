/* build.c -- AST -> FileDescriptorProto.
 *
 * Every rule here reproduces something measured against protoc 32.1 rather
 * than inferred from the documentation. The ones that cost bytes when wrong:
 *
 *   - json_name is emitted for EVERY field, in proto2 too.
 *   - A group emits a field whose name is the group name fully lower-cased,
 *     plus a nested message under the original spelling.
 *   - A map field emits a synthesised nested message named by CamelCasing the
 *     field and appending "Entry", carrying options.map_entry = true.
 *   - A proto3 `optional` field emits proto3_optional and a synthetic oneof
 *     named "_<field>", and synthetic oneofs sort AFTER every real one.
 *   - public_dependency / weak_dependency are INDICES into dependency.
 *   - proto2 emits no `syntax`; proto3 emits "proto3"; editions emit
 *     "editions" plus an `edition` enum value.
 *
 * Field numbers come from descnum.h, generated from the bundled
 * descriptor.proto so they track the pinned protoc release.
 */
#include "build.h"
#include "descnum.h"
#include "names.h"
#include "options.h"
#include "wkt.h"
#include "desc.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

struct Sym {
  const char *fqn; /* fully qualified, no leading dot */
  int kind;        /* T_MESSAGE | T_ENUM */
  Sym *next;
};

BuildCtx *build_ctx_new(Arena *a, Diags *d) {
  BuildCtx *c = arena_calloc(a, sizeof(BuildCtx));
  c->a = a;
  c->d = d;
  c->pool = pool_new(a);
  return c;
}

/* Options are resolved against real descriptors, so the compiler has to know
 * descriptor.proto before it can interpret a single `option` statement. It
 * learns it the self-hosting way: parse the bundled copy, build it with option
 * interpretation switched off (nothing to resolve against yet), and pool the
 * result. From then on FileOptions, FieldOptions and every extension of them
 * are ordinary descriptors. */
static void bootstrap(BuildCtx *c) {
  if (c->bootstrapped) return;
  c->bootstrapped = true;
  /* Save and restore: this runs from inside the first build_file, and the
   * caller may be midway through a two-pass build that owns this flag. */
  bool saved_no_options = c->no_options;
  const WktFile *w = wkt_lookup("google/protobuf/descriptor.proto");
  if (!w) return;
  Diags quiet;
  diags_init(&quiet, c->a);
  AstFile *f = ast_parse(c->a, &quiet, "google/protobuf/descriptor.proto",
                         str_of(w->text, w->len));
  if (!f) return;
  c->no_options = true;
  Dyn *fd = build_file(c, f, "google/protobuf/descriptor.proto");
  c->no_options = saved_no_options;
  if (fd) {
    const char *err = NULL;
    pool_add_serialized(c->pool, dyn_serialize(fd, c->a), &err);
  }
}

/* Interprets `opts` and attaches the result at `field` only when it produced
 * something. protoc emits an options submessage exactly when an option was
 * written -- including one written at its default value -- and omits it
 * entirely otherwise. */
static void attach_options(BuildCtx *c, Dyn *out, uint32_t field,
                           const char *options_msg, const char *scope,
                           const AstOption *opts) {
  if (c->no_options || !opts) return;
  const Desc *od = pool_find_message(c->pool, options_msg);
  if (!od) return;
  Dyn *tmp = dyn_new(c->a);
  /* Pass one interprets so that builtin options (notably FieldOptions.retention)
   * land in the pooled descriptor, but says nothing about custom options whose
   * extensions this file has not registered yet. */
  Diags scratch;
  diags_init(&scratch, c->a);
  options_interpret(c->a, c->quiet ? &scratch : c->d, c->pool, od, scope, opts,
                    tmp, c->retain_options, c->cur_file);
  uint32_t probe[4];
  if (dyn_fields(tmp, probe, 4) == 0) return;
  dyn_put_bytes(out, field, dyn_serialize(tmp, c->a));
}

static void sym_add(BuildCtx *c, const char *fqn, int kind) {
  Sym *s = arena_calloc(c->a, sizeof(Sym));
  s->fqn = fqn;
  s->kind = kind;
  s->next = c->syms;
  c->syms = s;
}

int build_lookup(const BuildCtx *c, const char *fqn) {
  for (Sym *s = c->syms; s; s = s->next)
    if (strcmp(s->fqn, fqn) == 0) return s->kind;
  return 0;
}

static const char *join(Arena *a, const char *scope, Str leaf) {
  if (!scope || !*scope) return arena_strndup(a, leaf.p, leaf.n);
  size_t n = strlen(scope);
  char *out = arena_alloc(a, n + 1 + leaf.n + 1);
  memcpy(out, scope, n);
  out[n] = '.';
  memcpy(out + n + 1, leaf.p, leaf.n);
  out[n + 1 + leaf.n] = '\0';
  return out;
}

/* ---------------------------------------------------------------- pass 1 */
/* Collect every declared symbol so resolution can see forward references and
 * siblings, exactly as protoc's two-pass build does. */

static void collect_enum(BuildCtx *c, const AstEnum *e, const char *scope) {
  for (; e; e = e->next) sym_add(c, join(c->a, scope, e->name), T_ENUM);
}

static void collect_msg(BuildCtx *c, const AstMessage *m, const char *scope) {
  for (; m; m = m->next) {
    const char *fq = join(c->a, scope, m->name);
    sym_add(c, fq, T_MESSAGE);
    collect_msg(c, m->messages, fq);
    collect_enum(c, m->enums, fq);
    /* A map field's synthesised entry message is a real nested symbol. */
    for (const AstField *f = m->fields; f; f = f->next)
      if (f->kind == AST_FIELD_MAP) {
        char *nm = map_entry_name_of(c->a, arena_strndup(c->a, f->name.p, f->name.n));
        sym_add(c, join(c->a, fq, str_cstr(nm)), T_MESSAGE);
      }
  }
}

/* ------------------------------------------------------------- resolution */

/* C++ scoping: for a relative name, try the innermost scope outward. A name
 * written with a leading dot is already absolute. */
static const char *resolve(BuildCtx *c, const char *scope, Str written, int *kind) {
  if (written.n && written.p[0] == '.') {
    const char *fq = arena_strndup(c->a, written.p + 1, written.n - 1);
    *kind = build_lookup(c, fq);
    return fq;
  }
  char *cur = arena_strdup(c->a, scope ? scope : "");
  for (;;) {
    const char *cand = join(c->a, cur, written);
    int k = build_lookup(c, cand);
    if (k) { *kind = k; return cand; }
    if (!*cur) break;
    char *dot = strrchr(cur, '.');
    if (!dot) { cur[0] = '\0'; } else { *dot = '\0'; }
  }
  *kind = 0;
  return arena_strndup(c->a, written.p, written.n);
}

/* ------------------------------------------------------- default_value fmt */
/* protoc stores default_value as a STRING for every type: "inf" for double
 * infinity, "-1.5" for a float, "NEG" for an enum, and the C-escaped form for
 * bytes. */
static Str fmt_default(Arena *a, const AstValue *v, int type) {
  Buf b; buf_init(&b, a);
  if (!v) return buf_str(&b);
  switch (v->kind) {
    case AV_IDENT:
      if (v->negative) buf_byte(&b, '-');
      buf_put(&b, v->sval.p, v->sval.n);
      break;
    case AV_INT:
      if (v->negative) buf_byte(&b, '-');
      buf_printf(&b, "%" PRIu64, v->ival);
      break;
    case AV_FLOAT: {
      if (v->negative) buf_byte(&b, '-');
      char tmp[48];
      /* protoc prints enough digits to round-trip and no more: 15 significant
       * digits for a double, falling back to 17, and 6 then 9 for a float.
       * `%g`'s default of 6 truncates 3.14159265358979 to 3.14159. */
      if (type == T_FLOAT) {
        snprintf(tmp, sizeof tmp, "%.6g", v->fval);
        float back = 0;
        if (sscanf(tmp, "%f", &back) != 1 || (double)back != v->fval)
          snprintf(tmp, sizeof tmp, "%.9g", v->fval);
      } else {
        snprintf(tmp, sizeof tmp, "%.15g", v->fval);
        double back = 0;
        if (sscanf(tmp, "%lf", &back) != 1 || back != v->fval)
          snprintf(tmp, sizeof tmp, "%.17g", v->fval);
      }
      buf_cstr(&b, tmp);
      break;
    }
    case AV_STR:
      if (type == T_BYTES) {
        /* bytes defaults are stored C-escaped */
        for (size_t i = 0; i < v->sval.n; i++) {
          unsigned char ch = (unsigned char)v->sval.p[i];
          if (ch == '\\') buf_cstr(&b, "\\\\");
          else if (ch == '\'') buf_cstr(&b, "\\'");
          else if (ch == '"') buf_cstr(&b, "\\\"");
          else if (ch == '\n') buf_cstr(&b, "\\n");
          else if (ch == '\r') buf_cstr(&b, "\\r");
          else if (ch == '\t') buf_cstr(&b, "\\t");
          else if (ch < 32 || ch >= 127) buf_printf(&b, "\\%03o", ch);
          else buf_byte(&b, (char)ch);
        }
      } else {
        buf_put(&b, v->sval.p, v->sval.n);
      }
      break;
    default:
      break;
  }
  return buf_str(&b);
}

/* ------------------------------------------------------------------ field */

typedef struct {
  const AstMessage *msg;
  const char *scope;
  int n_real_oneofs;
} MsgCtx;

static void emit_field(BuildCtx *c, Dyn *out, const AstField *f,
                       const char *scope, int edition, int oneof_index,
                       bool proto3_optional) {
  char *name = arena_strndup(c->a, f->name.p, f->name.n);
  /* A group's field name is the group's name fully lower-cased, underscores
   * kept: `group My_Group` -> field `my_group`, `group ALLCAPS` -> `allcaps`. */
  if (f->kind == AST_FIELD_GROUP) name = group_field_name_of(c->a, name);
  dyn_put_str(out, F_FIELDDESCRIPTORPROTO_NAME, name);
  if (f->is_extension && f->extendee.n) {
    int k = 0;
    const char *fq = resolve(c, scope, f->extendee, &k);
    char *dotted = arena_printf(c->a, ".%s", fq);
    dyn_put_str(out, F_FIELDDESCRIPTORPROTO_EXTENDEE, dotted);
  }
  dyn_put_i32(out, F_FIELDDESCRIPTORPROTO_NUMBER, (int32_t)f->number);

  int label = f->label ? f->label : L_OPTIONAL;
  if (f->kind == AST_FIELD_MAP) label = L_REPEATED;
  dyn_put_i32(out, F_FIELDDESCRIPTORPROTO_LABEL, label);

  int type = f->builtin;
  const char *type_fq = NULL;
  if (f->kind == AST_FIELD_GROUP) {
    type = T_GROUP;
    int k = 0;
    type_fq = resolve(c, scope, f->group_name.n ? f->group_name : f->type_name, &k);
  } else if (f->kind == AST_FIELD_MAP) {
    type = T_MESSAGE;
    char *nm = map_entry_name_of(c->a, name);
    type_fq = join(c->a, scope, str_cstr(nm));
  } else if (!type) {
    int k = 0;
    type_fq = resolve(c, scope, f->type_name, &k);
    type = (k == T_ENUM) ? T_ENUM : T_MESSAGE;
  }
  dyn_put_i32(out, F_FIELDDESCRIPTORPROTO_TYPE, type);
  if (type_fq) {
    char *dotted = arena_printf(c->a, ".%s", type_fq);
    dyn_put_str(out, F_FIELDDESCRIPTORPROTO_TYPE_NAME, dotted);
  }

  if (f->default_value) {
    Str d = fmt_default(c->a, f->default_value, type);
    dyn_put_bytes(out, F_FIELDDESCRIPTORPROTO_DEFAULT_VALUE, d);
  }
  if (oneof_index >= 0)
    dyn_put_i32(out, F_FIELDDESCRIPTORPROTO_ONEOF_INDEX, oneof_index);

  /* json_name is always emitted; an explicit option wins. */
  if (f->has_json_name)
    dyn_put_bytes(out, F_FIELDDESCRIPTORPROTO_JSON_NAME, f->json_name);
  else
    dyn_put_str(out, F_FIELDDESCRIPTORPROTO_JSON_NAME, json_name_of(c->a, name));

  if (proto3_optional)
    dyn_put_bool(out, F_FIELDDESCRIPTORPROTO_PROTO3_OPTIONAL, true);
  (void)edition;
}

/* ------------------------------------------------------------------ enums */

static void emit_enum(BuildCtx *c, Dyn *out, const AstEnum *e, const char *scope) {
  dyn_put_bytes(out, F_ENUMDESCRIPTORPROTO_NAME, e->name);
  for (const AstEnumValue *v = e->values; v; v = v->next) {
    Dyn *vd = dyn_put_msg(out, F_ENUMDESCRIPTORPROTO_VALUE);
    dyn_put_bytes(vd, F_ENUMVALUEDESCRIPTORPROTO_NAME, v->name);
    dyn_put_i32(vd, F_ENUMVALUEDESCRIPTORPROTO_NUMBER, (int32_t)v->number);
    attach_options(c, vd, F_ENUMVALUEDESCRIPTORPROTO_OPTIONS,
                   "google.protobuf.EnumValueOptions", scope, v->options);
  }
  attach_options(c, out, F_ENUMDESCRIPTORPROTO_OPTIONS,
                 "google.protobuf.EnumOptions", scope, e->options);
  for (const AstReserved *r = e->reserved; r; r = r->next) {
    for (const AstRange *g = r->ranges; g; g = g->next) {
      Dyn *rd = dyn_put_msg(out, F_ENUMDESCRIPTORPROTO_RESERVED_RANGE);
      dyn_put_i32(rd, 1, (int32_t)g->start);
      dyn_put_i32(rd, 2, (int32_t)(g->has_end ? (g->end_is_max ? 2147483647 : g->end) : g->start));
    }
    for (const AstRName *n = r->names; n; n = n->next)
      dyn_put_bytes(out, F_ENUMDESCRIPTORPROTO_RESERVED_NAME, n->name);
  }
  /* SymbolVisibility -- edition 2024's `local` / `export`. Field 6, emitted
   * after reserved_name (5) because protobuf serialises in ascending field
   * number. AST_VIS_UNSET/LOCAL/EXPORT are 0/1/2, matching the wire enum. */
  if (e->visibility != AST_VIS_UNSET)
    dyn_put_i32(out, F_ENUMDESCRIPTORPROTO_VISIBILITY, e->visibility);
}

/* --------------------------------------------------------------- messages */

static void emit_msg(BuildCtx *c, Dyn *out, const AstMessage *m,
                     const char *parent_scope, int edition);

static void emit_map_entry(BuildCtx *c, Dyn *parent, const AstField *f,
                           const char *scope) {
  char *name = arena_strndup(c->a, f->name.p, f->name.n);
  Dyn *e = dyn_put_msg(parent, F_DESCRIPTORPROTO_NESTED_TYPE);
  dyn_put_str(e, F_DESCRIPTORPROTO_NAME, map_entry_name_of(c->a, name));

  Dyn *k = dyn_put_msg(e, F_DESCRIPTORPROTO_FIELD);
  dyn_put_str(k, F_FIELDDESCRIPTORPROTO_NAME, "key");
  dyn_put_i32(k, F_FIELDDESCRIPTORPROTO_NUMBER, 1);
  dyn_put_i32(k, F_FIELDDESCRIPTORPROTO_LABEL, L_OPTIONAL);
  dyn_put_i32(k, F_FIELDDESCRIPTORPROTO_TYPE, f->key_builtin);
  dyn_put_str(k, F_FIELDDESCRIPTORPROTO_JSON_NAME, "key");

  Dyn *v = dyn_put_msg(e, F_DESCRIPTORPROTO_FIELD);
  dyn_put_str(v, F_FIELDDESCRIPTORPROTO_NAME, "value");
  dyn_put_i32(v, F_FIELDDESCRIPTORPROTO_NUMBER, 2);
  dyn_put_i32(v, F_FIELDDESCRIPTORPROTO_LABEL, L_OPTIONAL);
  if (f->value_builtin) {
    dyn_put_i32(v, F_FIELDDESCRIPTORPROTO_TYPE, f->value_builtin);
  } else {
    int kind = 0;
    const char *fq = resolve(c, scope, f->value_type, &kind);
    dyn_put_i32(v, F_FIELDDESCRIPTORPROTO_TYPE, kind == T_ENUM ? T_ENUM : T_MESSAGE);
    dyn_put_str(v, F_FIELDDESCRIPTORPROTO_TYPE_NAME, arena_printf(c->a, ".%s", fq));
  }
  dyn_put_str(v, F_FIELDDESCRIPTORPROTO_JSON_NAME, "value");

  Dyn *o = dyn_put_msg(e, F_DESCRIPTORPROTO_OPTIONS);
  dyn_put_bool(o, F_MESSAGEOPTIONS_MAP_ENTRY, true);
}

static void emit_msg(BuildCtx *c, Dyn *out, const AstMessage *m,
                     const char *parent_scope, int edition) {
  const char *scope = join(c->a, parent_scope, m->name);
  dyn_put_bytes(out, F_DESCRIPTORPROTO_NAME, m->name);

  /* Real oneofs first, then synthetic ones for proto3 `optional`. protoc
   * orders them that way and the indices depend on it. */
  int n_real = 0;
  for (const AstOneof *o = m->oneofs; o; o = o->next) n_real++;

  int synth = n_real;
  for (const AstField *f = m->fields; f; f = f->next) {
    bool is_p3opt = (edition == ED_PROTO3 && f->label == L_OPTIONAL &&
                     f->oneof_index < 0 && f->kind != AST_FIELD_MAP);
    Dyn *fd = dyn_put_msg(out, F_DESCRIPTORPROTO_FIELD);
    int oi = f->oneof_index >= 0 ? f->oneof_index : (is_p3opt ? synth++ : -1);
    emit_field(c, fd, f, scope, edition, oi, is_p3opt);
    attach_options(c, fd, F_FIELDDESCRIPTORPROTO_OPTIONS,
                   "google.protobuf.FieldOptions", parent_scope, f->options);
  }

  /* nested_type is emitted in SOURCE DECLARATION ORDER, with synthesised
   * map-entry messages and group bodies interleaved among the explicitly
   * declared nested messages. Measured: `message A; map m1; message B;
   * group G; map m2; message C` emits A, M1Entry, B, G, M2Entry, C.
   * Sorting by source position reproduces that without the parser having to
   * synthesise anything. */
  typedef struct { long key; const AstMessage *msg; const AstField *map_field; } Slot;
  int nslots = 0;
  for (const AstMessage *n = m->messages; n; n = n->next) nslots++;
  for (const AstField *f = m->fields; f; f = f->next)
    if (f->kind == AST_FIELD_MAP) nslots++;
  Slot *slots = arena_calloc(c->a, (size_t)(nslots > 0 ? nslots : 1) * sizeof(Slot));
  int ns = 0;
  for (const AstMessage *n = m->messages; n; n = n->next) {
    slots[ns].key = (long)n->line * 100000L + n->col;
    slots[ns].msg = n;
    ns++;
  }
  for (const AstField *f = m->fields; f; f = f->next)
    if (f->kind == AST_FIELD_MAP) {
      slots[ns].key = (long)f->line * 100000L + f->col;
      slots[ns].map_field = f;
      ns++;
    }
  for (int i = 1; i < ns; i++) {
    Slot tmp = slots[i];
    int j = i - 1;
    while (j >= 0 && slots[j].key > tmp.key) { slots[j + 1] = slots[j]; j--; }
    slots[j + 1] = tmp;
  }
  for (int i = 0; i < ns; i++) {
    if (slots[i].msg) {
      Dyn *nd = dyn_put_msg(out, F_DESCRIPTORPROTO_NESTED_TYPE);
      emit_msg(c, nd, slots[i].msg, scope, edition);
    } else {
      emit_map_entry(c, out, slots[i].map_field, scope);
    }
  }

  for (const AstEnum *e = m->enums; e; e = e->next) {
    Dyn *ed = dyn_put_msg(out, F_DESCRIPTORPROTO_ENUM_TYPE);
    emit_enum(c, ed, e, scope);
  }
  /* `to max` in an extension range is 536870912 exclusive, except in a
   * MessageSet, where protoc widens it to the full int32 range. */
  bool message_set = false;
  for (const AstOption *o = m->options; o; o = o->next)
    if (o->name && !o->name->next && !o->name->is_extension &&
        str_eq_c(o->name->name, "message_set_wire_format") &&
        o->value && o->value->kind == AV_IDENT && str_eq_c(o->value->sval, "true"))
      message_set = true;
  int32_t ext_max = message_set ? 2147483647 : 536870912;
  for (const AstExtRange *r = m->ext_ranges; r; r = r->next)
    for (const AstRange *g = r->ranges; g; g = g->next) {
      Dyn *rd = dyn_put_msg(out, F_DESCRIPTORPROTO_EXTENSION_RANGE);
      dyn_put_i32(rd, 1, (int32_t)g->start);
      dyn_put_i32(rd, 2, (int32_t)(g->end_is_max ? ext_max : (g->has_end ? g->end + 1 : g->start + 1)));
      attach_options(c, rd, 3, "google.protobuf.ExtensionRangeOptions", parent_scope, r->options);
    }
  for (const AstExtend *x = m->extends; x; x = x->next)
    for (const AstField *f = x->fields; f; f = f->next) {
      Dyn *fd = dyn_put_msg(out, F_DESCRIPTORPROTO_EXTENSION);
      emit_field(c, fd, f, scope, edition, -1, false);
      attach_options(c, fd, F_FIELDDESCRIPTORPROTO_OPTIONS,
                     "google.protobuf.FieldOptions", parent_scope, f->options);
    }

  for (const AstOneof *o = m->oneofs; o; o = o->next) {
    Dyn *od = dyn_put_msg(out, F_DESCRIPTORPROTO_ONEOF_DECL);
    dyn_put_bytes(od, F_ONEOFDESCRIPTORPROTO_NAME, o->name);
    attach_options(c, od, F_ONEOFDESCRIPTORPROTO_OPTIONS,
                   "google.protobuf.OneofOptions", parent_scope, o->options);
  }
  if (edition == ED_PROTO3)
    for (const AstField *f = m->fields; f; f = f->next)
      if (f->label == L_OPTIONAL && f->oneof_index < 0 && f->kind != AST_FIELD_MAP) {
        Dyn *od = dyn_put_msg(out, F_DESCRIPTORPROTO_ONEOF_DECL);
        dyn_put_str(od, F_ONEOFDESCRIPTORPROTO_NAME,
                    arena_printf(c->a, "_%.*s", (int)f->name.n, f->name.p));
      }

  attach_options(c, out, F_DESCRIPTORPROTO_OPTIONS,
                 "google.protobuf.MessageOptions", parent_scope, m->options);

  for (const AstReserved *r = m->reserved; r; r = r->next) {
    for (const AstRange *g = r->ranges; g; g = g->next) {
      Dyn *rd = dyn_put_msg(out, F_DESCRIPTORPROTO_RESERVED_RANGE);
      dyn_put_i32(rd, 1, (int32_t)g->start);
      dyn_put_i32(rd, 2, (int32_t)(g->end_is_max ? 536870912 : (g->has_end ? g->end + 1 : g->start + 1)));
    }
    for (const AstRName *n = r->names; n; n = n->next)
      dyn_put_bytes(out, F_DESCRIPTORPROTO_RESERVED_NAME, n->name);
  }
  /* Same, field 11 after reserved_name's 10. */
  if (m->visibility != AST_VIS_UNSET)
    dyn_put_i32(out, F_DESCRIPTORPROTO_VISIBILITY, m->visibility);
}

/* ------------------------------------------------------------------- file */


/* ----------------------------------------------------------- validation */
/* STYLE2024 naming, probed against protoc 32.1 rather than inferred from one
 * diagnostic. Every predicate below matches all sixteen observed accepts and
 * rejects, including the three that are easy to get wrong:
 *   field_2      rejected -- an underscore followed by a non-lowercase
 *   HTTPServer   accepted -- TitleCase allows consecutive capitals
 *   Http2Server  accepted -- and embedded digits
 * Enum VALUES have their own rule, UPPER_SNAKE_CASE, which no diagnostic I had
 * seen before probing even mentioned. */
static bool nm_titlecase(Str s) {
  if (!s.n || s.p[0] < 'A' || s.p[0] > 'Z') return false;
  for (size_t i = 0; i < s.n; i++) if (s.p[i] == '_') return false;
  return true;
}
static bool nm_lower_snake(Str s) {
  if (!s.n || s.p[0] < 'a' || s.p[0] > 'z') return false;
  for (size_t i = 0; i < s.n; i++) {
    char ch = s.p[i];
    if (ch >= 'A' && ch <= 'Z') return false;
    if (ch == '_' && (i + 1 >= s.n || !(s.p[i + 1] >= 'a' && s.p[i + 1] <= 'z'))) return false;
  }
  return true;
}
static bool nm_upper_snake(Str s) {
  if (!s.n || s.p[0] < 'A' || s.p[0] > 'Z') return false;
  for (size_t i = 0; i < s.n; i++) if (s.p[i] >= 'a' && s.p[i] <= 'z') return false;
  return true;
}
#define NM_STYLE "(feature.enforce_naming_style = STYLE2024)"
/* protoc refuses these before it emits anything. The corpus's rejection set is
 * the specification: each check below corresponds to inputs protoc exits
 * non-zero on. Nothing here changes the bytes of a valid file. */

static bool range_has(const AstReserved *r, int64_t n) {
  for (; r; r = r->next)
    for (const AstRange *g = r->ranges; g; g = g->next) {
      int64_t hi = g->has_end ? (g->end_is_max ? 536870911 : g->end) : g->start;
      if (n >= g->start && n <= hi) return true;
    }
  return false;
}

static bool names_has(const AstReserved *r, Str name) {
  for (; r; r = r->next)
    for (const AstRName *n = r->names; n; n = n->next)
      if (str_eq(n->name, name)) return true;
  return false;
}

static void validate_msg(BuildCtx *c, const AstMessage *m, int edition,
                         const char *file);

static void validate_field(BuildCtx *c, const AstMessage *owner,
                           const AstField *f, int edition, const char *file) {
  int64_t n = (int64_t)f->number;
  if (f->number_ovf || n <= 0)
    diag_add(c->d, file, f->line, f->col,
             "Field numbers must be positive integers.");
  else if (n > 536870911)
    diag_add(c->d, file, f->line, f->col,
             "Field numbers cannot be greater than 536870911.");
  else if (n >= 19000 && n <= 19999)
    diag_add(c->d, file, f->line, f->col,
             "Field numbers 19000 through 19999 are reserved for the protocol "
             "buffer library implementation.");

  if (edition == ED_PROTO3) {
    if (f->label == L_REQUIRED)
      diag_add(c->d, file, f->line, f->col,
               "Required fields are not allowed in proto3.");
    if (f->kind == AST_FIELD_GROUP)
      diag_add(c->d, file, f->line, f->col,
               "Groups are not supported in proto3 syntax.");
    if (f->default_value)
      diag_add(c->d, file, f->line, f->col,
               "Explicit default values are not allowed in proto3.");
  }
  if (f->default_value) {
    if (f->label == L_REPEATED || f->kind == AST_FIELD_MAP)
      diag_add(c->d, file, f->line, f->col,
               "Repeated fields can't have default values.");
    else if (f->builtin == 0 && f->kind == AST_FIELD_NORMAL) {
      int kind = 0;
      resolve(c, owner ? "" : "", f->type_name, &kind);
    }
  }
  if (owner && !f->is_extension) {
    if (range_has(owner->reserved, n))
      diag_add(c->d, file, f->line, f->col,
               "Field \"%.*s\" uses reserved number %lld.",
               (int)f->name.n, f->name.p, (long long)n);
    if (names_has(owner->reserved, f->name))
      diag_add(c->d, file, f->line, f->col,
               "Field name \"%.*s\" is reserved.", (int)f->name.n, f->name.p);
  }
}

static void validate_msg(BuildCtx *c, const AstMessage *m, int edition,
                         const char *file) {
  for (; m; m = m->next) {
    if (edition >= ED_2024) {
      if (!nm_titlecase(m->name))
        diag_add(c->d, file, m->line, m->col,
                 "Message name %.*s should be TitleCase " NM_STYLE ".",
                 (int)m->name.n, m->name.p);
      for (const AstField *nf = m->fields; nf; nf = nf->next)
        if (!nm_lower_snake(nf->name))
          diag_add(c->d, file, nf->line, nf->col,
                   "Field name %.*s should be lower_snake_case " NM_STYLE ".",
                   (int)nf->name.n, nf->name.p);
      for (const AstEnum *ne = m->enums; ne; ne = ne->next) {
        if (!nm_titlecase(ne->name))
          diag_add(c->d, file, ne->line, ne->col,
                   "Enum name %.*s should be TitleCase " NM_STYLE ".",
                   (int)ne->name.n, ne->name.p);
        for (const AstEnumValue *nv = ne->values; nv; nv = nv->next)
          if (!nm_upper_snake(nv->name))
            diag_add(c->d, file, nv->line, nv->col,
                     "Enum value name %.*s should be UPPER_SNAKE_CASE " NM_STYLE ".",
                     (int)nv->name.n, nv->name.p);
      }
    }
    for (const AstField *a = m->fields; a; a = a->next) {
      validate_field(c, m, a, edition, file);
      for (const AstField *b = a->next; b; b = b->next) {
        if (a->has_number && b->has_number && a->number == b->number)
          diag_add(c->d, file, b->line, b->col,
                   "Field number %llu has already been used in \"%.*s\" by field "
                   "\"%.*s\".", (unsigned long long)b->number,
                   (int)m->name.n, m->name.p, (int)a->name.n, a->name.p);
        if (str_eq(a->name, b->name))
          diag_add(c->d, file, b->line, b->col,
                   "\"%.*s\" is already defined in \"%.*s\".",
                   (int)b->name.n, b->name.p, (int)m->name.n, m->name.p);
      }
    }
    /* duplicate nested type names in one scope */
    for (const AstMessage *a = m->messages; a; a = a->next)
      for (const AstMessage *b = a->next; b; b = b->next)
        if (str_eq(a->name, b->name))
          diag_add(c->d, file, b->line, b->col,
                   "\"%.*s\" is already defined in \"%.*s\".",
                   (int)b->name.n, b->name.p, (int)m->name.n, m->name.p);
    for (const AstEnum *a = m->enums; a; a = a->next)
      for (const AstEnum *b = a->next; b; b = b->next)
        if (str_eq(a->name, b->name))
          diag_add(c->d, file, b->line, b->col,
                   "\"%.*s\" is already defined.", (int)b->name.n, b->name.p);
    for (const AstExtend *x = m->extends; x; x = x->next)
      for (const AstField *f = x->fields; f; f = f->next)
        validate_field(c, NULL, f, edition, file);
    validate_msg(c, m->messages, edition, file);
  }
}

static void validate_file(BuildCtx *c, const AstFile *f, const char *name) {
  int edition = f->edition ? f->edition
                           : (f->syntax_kind == AST_SYN_PROTO3 ? ED_PROTO3 : ED_PROTO2);
  validate_msg(c, f->messages, edition, name);
  for (const AstMessage *a = f->messages; a; a = a->next)
    for (const AstMessage *b = a->next; b; b = b->next)
      if (str_eq(a->name, b->name))
        diag_add(c->d, name, b->line, b->col,
                 "\"%.*s\" is already defined in file \"%s\".",
                 (int)b->name.n, b->name.p, name);
  for (const AstEnum *a = f->enums; a; a = a->next) {
    for (const AstEnum *b = a->next; b; b = b->next)
      if (str_eq(a->name, b->name))
        diag_add(c->d, name, b->line, b->col,
                 "\"%.*s\" is already defined in file \"%s\".",
                 (int)b->name.n, b->name.p, name);
    /* proto3 enums must start at zero */
    if (edition == ED_PROTO3 && a->values && a->values->number != 0)
      diag_add(c->d, name, a->line, a->col,
               "The first enum value must be zero in proto3.");
    /* enum value names share the enclosing scope */
    for (const AstEnumValue *v = a->values; v; v = v->next)
      for (const AstEnumValue *w = v->next; w; w = w->next)
        if (str_eq(v->name, w->name))
          diag_add(c->d, name, w->line, w->col,
                   "\"%.*s\" is already defined.", (int)w->name.n, w->name.p);
  }
  for (const AstExtend *x = f->extends; x; x = x->next)
    for (const AstField *fl = x->fields; fl; fl = fl->next)
      validate_field(c, NULL, fl, edition, name);
}


/* Map key types protoc accepts: every integral, bool and string. Never float,
 * double, bytes, enum or message. */
static bool legal_map_key(int t) {
  switch (t) {
    case T_INT32: case T_INT64: case T_UINT32: case T_UINT64:
    case T_SINT32: case T_SINT64: case T_FIXED32: case T_FIXED64:
    case T_SFIXED32: case T_SFIXED64: case T_BOOL: case T_STRING:
      return true;
    default:
      return false;
  }
}

static bool ranges_overlap(const AstRange *a, const AstRange *b) {
  int64_t a1 = a->start, a2 = a->has_end ? (a->end_is_max ? 536870911 : a->end) : a->start;
  int64_t b1 = b->start, b2 = b->has_end ? (b->end_is_max ? 536870911 : b->end) : b->start;
  return a1 <= b2 && b1 <= a2;
}

static void validate_msg2(BuildCtx *c, const AstMessage *m, int edition,
                          const char *file, const char *parent_scope) {
  for (; m; m = m->next) {
    const char *scope = join(c->a, parent_scope, m->name);
    /* A feature whose `targets` exclude MESSAGE cannot be set on one:
     * field_presence, repeated_field_encoding, utf8_validation and
     * message_encoding target FIELD and FILE, enum_type ENUM and FILE
     * (descriptor.proto FeatureSet). protoc 32.1 refuses the file. */
    if (edition >= ED_2023)
      for (const AstOption *o = m->options; o; o = o->next) {
        const AstOptName *n0 = o->name, *n1 = n0 ? n0->next : NULL;
        if (!n1 || n0->is_extension || n1->is_extension ||
            !str_eq_c(n0->name, "features"))
          continue;
        if (str_eq_c(n1->name, "field_presence") ||
            str_eq_c(n1->name, "repeated_field_encoding") ||
            str_eq_c(n1->name, "utf8_validation") ||
            str_eq_c(n1->name, "message_encoding") ||
            str_eq_c(n1->name, "enum_type"))
          diag_add(c->d, file, o->line, o->col,
                   "Option google.protobuf.FeatureSet.%.*s cannot be set on an "
                   "entity of type `message`.", (int)n1->name.n, n1->name.p);
      }
    for (const AstField *f = m->fields; f; f = f->next) {
      if (f->kind == AST_FIELD_MAP) {
        if (!legal_map_key(f->key_builtin))
          diag_add(c->d, file, f->line, f->col,
                   "Key in map fields cannot be float/double, bytes or message types.");
        if (f->label)
          diag_add(c->d, file, f->line, f->col,
                   "Field labels (required/optional/repeated) are not allowed on map fields.");
        if (f->oneof_index >= 0)
          diag_add(c->d, file, f->line, f->col,
                   "Map fields are not allowed in oneofs.");
      }
      if (f->kind == AST_FIELD_GROUP && f->group_name.n &&
          !(f->group_name.p[0] >= 'A' && f->group_name.p[0] <= 'Z'))
        diag_add(c->d, file, f->line, f->col,
                 "Group names must start with a capital letter.");
      if (edition == ED_PROTO2 && f->label == AST_LABEL_NONE &&
          f->kind != AST_FIELD_MAP && f->oneof_index < 0)
        diag_add(c->d, file, f->line, f->col,
                 "Expected \"required\", \"optional\", or \"repeated\".");
      /* an unresolvable type reference */
      if (f->kind == AST_FIELD_NORMAL && f->builtin == 0) {
        int kind = 0;
        resolve(c, scope, f->type_name, &kind);
        if (!kind)
          diag_add(c->d, file, f->line, f->col,
                   "\"%.*s\" is not defined.", (int)f->type_name.n, f->type_name.p);
      }
      if (f->default_value && f->kind == AST_FIELD_NORMAL &&
          (f->builtin == T_MESSAGE || f->builtin == T_GROUP))
        diag_add(c->d, file, f->line, f->col,
                 "Messages can't have default values.");
    }
    if (edition == ED_PROTO3 && m->ext_ranges)
      diag_add(c->d, file, m->line, m->col,
               "Extension ranges are not allowed in proto3.");
    /* overlapping reserved ranges, overlapping extension ranges, and the two
     * overlapping each other */
    for (const AstReserved *r = m->reserved; r; r = r->next)
      for (const AstRange *g = r->ranges; g; g = g->next) {
        if (g->start <= 0)
          diag_add(c->d, file, g->line, g->col,
                   "Field numbers must be positive integers.");
        for (const AstReserved *r2 = r; r2; r2 = r2->next)
          for (const AstRange *h = (r2 == r ? g->next : r2->ranges); h; h = h->next)
            if (ranges_overlap(g, h))
              diag_add(c->d, file, h->line, h->col,
                       "Reserved range %lld to %lld overlaps with already-defined range.",
                       (long long)h->start, (long long)(h->has_end ? h->end : h->start));
      }
    for (const AstExtRange *r = m->ext_ranges; r; r = r->next)
      for (const AstRange *g = r->ranges; g; g = g->next) {
        int64_t hi = g->has_end ? (g->end_is_max ? 536870911 : g->end) : g->start;
        if (hi > 536870911)
          diag_add(c->d, file, g->line, g->col,
                   "Extension numbers cannot be greater than 536870911.");
        for (const AstExtRange *r2 = r; r2; r2 = r2->next)
          for (const AstRange *h = (r2 == r ? g->next : r2->ranges); h; h = h->next)
            if (ranges_overlap(g, h))
              diag_add(c->d, file, h->line, h->col,
                       "Extension range %lld to %lld overlaps with already-defined range.",
                       (long long)h->start, (long long)(h->has_end ? h->end : h->start));
        for (const AstReserved *rr = m->reserved; rr; rr = rr->next)
          for (const AstRange *h = rr->ranges; h; h = h->next)
            if (ranges_overlap(g, h))
              diag_add(c->d, file, g->line, g->col,
                       "Extension range %lld to %lld overlaps with reserved range.",
                       (long long)g->start, (long long)hi);
      }
    /* enum: alias without allow_alias, reserved violations */
    for (const AstEnum *e = m->enums; e; e = e->next) {
      bool allow_alias = false;
      for (const AstOption *o = e->options; o; o = o->next)
        if (o->name && !o->name->next && str_eq_c(o->name->name, "allow_alias") &&
            o->value && o->value->kind == AV_IDENT && str_eq_c(o->value->sval, "true"))
          allow_alias = true;
      for (const AstEnumValue *v = e->values; v; v = v->next) {
        if (!allow_alias)
          for (const AstEnumValue *w = v->next; w; w = w->next)
            if (v->number == w->number)
              diag_add(c->d, file, w->line, w->col,
                       "\"%.*s\" uses the same enum value as \"%.*s\". If this is "
                       "intended, set \"option allow_alias = true;\".",
                       (int)w->name.n, w->name.p, (int)v->name.n, v->name.p);
        if (range_has(e->reserved, v->number))
          diag_add(c->d, file, v->line, v->col,
                   "Enum value \"%.*s\" uses reserved number %lld.",
                   (int)v->name.n, v->name.p, (long long)v->number);
        if (names_has(e->reserved, v->name))
          diag_add(c->d, file, v->line, v->col,
                   "Enum value \"%.*s\" is reserved.", (int)v->name.n, v->name.p);
      }
    }
    /* Duplicate JSON names. Measured against protoc 32.1 rather than read
     * from the spec:
     *   proto2  default vs default -> warning, exit 0
     *   proto2  custom  vs custom  -> error
     *   proto3/editions, any pair  -> error
     * The gate is not cosmetic: three VALID proto2 files in step 1's corpus
     * carry default-name collisions, which an ungated check would wrongly
     * reject. */
    for (const AstField *a2 = m->fields; a2; a2 = a2->next) {
      char *an = a2->has_json_name
                   ? arena_strndup(c->a, a2->json_name.p, a2->json_name.n)
                   : json_name_of(c->a, arena_strndup(c->a, a2->name.p, a2->name.n));
      for (const AstField *b2 = a2->next; b2; b2 = b2->next) {
        char *bn = b2->has_json_name
                     ? arena_strndup(c->a, b2->json_name.p, b2->json_name.n)
                     : json_name_of(c->a, arena_strndup(c->a, b2->name.p, b2->name.n));
        if (strcmp(an, bn) != 0) continue;
        bool a_custom = a2->has_json_name, b_custom = b2->has_json_name;
        bool is_error;
        if (a_custom && b_custom) is_error = true;
        else if (a_custom || b_custom) is_error = edition != ED_PROTO2;
        else is_error = (edition != ED_PROTO2);
        if (!is_error) continue;
        diag_add(c->d, file, b2->line, b2->col,
                 "The %s JSON name of field \"%.*s\" (\"%s\") conflicts with "
                 "the %s JSON name of field \"%.*s\".",
                 b_custom ? "custom" : "default",
                 (int)b2->name.n, b2->name.p, bn,
                 a_custom ? "custom" : "default",
                 (int)a2->name.n, a2->name.p);
      }
    }
    validate_msg2(c, m->messages, edition, file, scope);
  }
}

static void validate_file2(BuildCtx *c, const AstFile *f, const char *name) {
  int edition = f->edition ? f->edition
                           : (f->syntax_kind == AST_SYN_PROTO3 ? ED_PROTO3 : ED_PROTO2);
  const char *pkg = f->has_package ? arena_strndup(c->a, f->package.p, f->package.n) : "";
  validate_msg2(c, f->messages, edition, name, pkg);
  if (edition >= ED_2024) {
    for (const AstEnum *te = f->enums; te; te = te->next) {
      if (!nm_titlecase(te->name))
        diag_add(c->d, name, te->line, te->col,
                 "Enum name %.*s should be TitleCase " NM_STYLE ".",
                 (int)te->name.n, te->name.p);
      for (const AstEnumValue *tv = te->values; tv; tv = tv->next)
        if (!nm_upper_snake(tv->name))
          diag_add(c->d, name, tv->line, tv->col,
                   "Enum value name %.*s should be UPPER_SNAKE_CASE " NM_STYLE ".",
                   (int)tv->name.n, tv->name.p);
    }
    for (const AstService *ts = f->services; ts; ts = ts->next) {
      if (!nm_titlecase(ts->name))
        diag_add(c->d, name, ts->line, ts->col,
                 "Service name %.*s should be TitleCase " NM_STYLE ".",
                 (int)ts->name.n, ts->name.p);
      for (const AstMethod *tm = ts->methods; tm; tm = tm->next)
        if (!nm_titlecase(tm->name))
          diag_add(c->d, name, tm->line, tm->col,
                   "Method name %.*s should be TitleCase " NM_STYLE ".",
                   (int)tm->name.n, tm->name.p);
    }
  }
  for (const AstEnum *e = f->enums; e; e = e->next) {
    bool allow_alias = false;
    for (const AstOption *o = e->options; o; o = o->next)
      if (o->name && !o->name->next && str_eq_c(o->name->name, "allow_alias") &&
          o->value && o->value->kind == AV_IDENT && str_eq_c(o->value->sval, "true"))
        allow_alias = true;
    for (const AstEnumValue *v = e->values; v; v = v->next) {
      if (!allow_alias)
        for (const AstEnumValue *w = v->next; w; w = w->next)
          if (v->number == w->number)
            diag_add(c->d, name, w->line, w->col,
                     "\"%.*s\" uses the same enum value as \"%.*s\". If this is "
                     "intended, set \"option allow_alias = true;\".",
                     (int)w->name.n, w->name.p, (int)v->name.n, v->name.p);
      if (range_has(e->reserved, v->number))
        diag_add(c->d, name, v->line, v->col,
                 "Enum value \"%.*s\" uses reserved number %lld.",
                 (int)v->name.n, v->name.p, (long long)v->number);
      if (names_has(e->reserved, v->name))
        diag_add(c->d, name, v->line, v->col,
                 "Enum value \"%.*s\" is reserved.", (int)v->name.n, v->name.p);
    }
  }
  /* rpc input/output must name a message */
  for (const AstService *s2 = f->services; s2; s2 = s2->next)
    for (const AstMethod *me = s2->methods; me; me = me->next) {
      int k1 = 0, k2 = 0;
      resolve(c, pkg, me->input_type, &k1);
      resolve(c, pkg, me->output_type, &k2);
      if (k1 && k1 != T_MESSAGE)
        diag_add(c->d, name, me->line, me->col,
                 "\"%.*s\" is not a message type.", (int)me->input_type.n, me->input_type.p);
      if (k2 && k2 != T_MESSAGE)
        diag_add(c->d, name, me->line, me->col,
                 "\"%.*s\" is not a message type.", (int)me->output_type.n, me->output_type.p);
    }
  /* extend must name a message, and the number must fall in a declared range */
  for (const AstExtend *x = f->extends; x; x = x->next) {
    int k = 0;
    const char *fq = resolve(c, pkg, x->extendee, &k);
    if (k && k != T_MESSAGE)
      diag_add(c->d, name, x->line, x->col,
               "\"%.*s\" is not a message type.", (int)x->extendee.n, x->extendee.p);
    (void)fq;
  }
}


/* Does `v` look like a value of type `t`? protoc rejects a string default on a
 * numeric field and an integer default on a bool, among others. */
static bool default_fits(int t, const AstValue *v) {
  switch (t) {
    case T_DOUBLE: case T_FLOAT:
      return v->kind == AV_INT || v->kind == AV_FLOAT ||
             (v->kind == AV_IDENT && (str_eq_c(v->sval, "inf") ||
                                      str_eq_c(v->sval, "nan") ||
                                      str_eq_c(v->sval, "infinity")));
    case T_INT32: case T_INT64: case T_UINT32: case T_UINT64:
    case T_SINT32: case T_SINT64: case T_FIXED32: case T_FIXED64:
    case T_SFIXED32: case T_SFIXED64:
      return v->kind == AV_INT;
    case T_BOOL:
      return v->kind == AV_IDENT &&
             (str_eq_c(v->sval, "true") || str_eq_c(v->sval, "false"));
    case T_STRING: case T_BYTES:
      return v->kind == AV_STR;
    case T_ENUM:
      return v->kind == AV_IDENT;
    default:
      return true;
  }
}

static void validate_msg3(BuildCtx *c, const AstMessage *m, int edition,
                          const char *file, const char *parent_scope,
                          const AstFile *fast, const char *pkg) {
  for (; m; m = m->next) {
    const char *scope = join(c->a, parent_scope, m->name);
    /* enum value names live in the ENCLOSING scope, so two enums in one scope
     * cannot declare the same value name. */
    for (const AstEnum *e1 = m->enums; e1; e1 = e1->next)
      for (const AstEnum *e2 = e1->next; e2; e2 = e2->next)
        for (const AstEnumValue *v1 = e1->values; v1; v1 = v1->next)
          for (const AstEnumValue *v2 = e2->values; v2; v2 = v2->next)
            if (str_eq(v1->name, v2->name))
              diag_add(c->d, file, v2->line, v2->col,
                       "\"%.*s\" is already defined in \"%s\".",
                       (int)v2->name.n, v2->name.p, scope);
    for (const AstField *f = m->fields; f; f = f->next) {
      if (!f->default_value) continue;
      const AstValue *v = f->default_value;
      if (f->builtin) {
        if (!default_fits(f->builtin, v))
          diag_add(c->d, file, f->line, f->col,
                   "Default value for field \"%.*s\" has the wrong type.",
                   (int)f->name.n, f->name.p);
      } else if (f->kind == AST_FIELD_NORMAL) {
        int kind = 0;
        const char *fq = resolve(c, scope, f->type_name, &kind);
        if (kind == T_MESSAGE)
          diag_add(c->d, file, f->line, f->col,
                   "Messages can't have default values.");
        else if (kind == T_ENUM && v->kind == AV_IDENT) {
          /* The named value has to exist in that enum. Locate the enum first,
           * then decide: "enum found" and "value found" are separate checks. */
          const AstEnum *target = NULL;
          for (const AstEnum *e = m->enums; e && !target; e = e->next)
            if (strcmp(join(c->a, scope, e->name), fq) == 0) target = e;
          /* ... and the file's own top-level enums. */
          if (fast)
            for (const AstEnum *e = fast->enums; e && !target; e = e->next)
              if (strcmp(join(c->a, pkg, e->name), fq) == 0) target = e;
          if (target) {
            bool found = false;
            for (const AstEnumValue *ev = target->values; ev; ev = ev->next)
              if (str_eq(ev->name, v->sval)) { found = true; break; }
            if (!found)
              diag_add(c->d, file, f->line, f->col,
                       "Enum type \"%s\" has no value named \"%.*s\".",
                       fq, (int)v->sval.n, v->sval.p);
          }
        }
      }
    }
    validate_msg3(c, m->messages, edition, file, scope, fast, pkg);
  }
}

/* A non-repeated option set twice in one scope. */
static void validate_option_dups(BuildCtx *c, const AstOption *opts,
                                 const char *file) {
  for (const AstOption *a = opts; a; a = a->next) {
    if (!a->name || a->name->next || a->name->is_extension) continue;
    for (const AstOption *b = a->next; b; b = b->next) {
      if (!b->name || b->name->next || b->name->is_extension) continue;
      if (str_eq(a->name->name, b->name->name))
        diag_add(c->d, file, b->line, b->col,
                 "Option \"%.*s\" was already set.",
                 (int)b->name->name.n, b->name->name.p);
    }
  }
}

static void validate_file3(BuildCtx *c, const AstFile *f, const char *name) {
  int edition = f->edition ? f->edition
                           : (f->syntax_kind == AST_SYN_PROTO3 ? ED_PROTO3 : ED_PROTO2);
  const char *pkg = f->has_package ? arena_strndup(c->a, f->package.p, f->package.n) : "";
  validate_msg3(c, f->messages, edition, name, pkg, f, pkg);
  validate_option_dups(c, f->options, name);
  for (const AstEnum *e1 = f->enums; e1; e1 = e1->next)
    for (const AstEnum *e2 = e1->next; e2; e2 = e2->next)
      for (const AstEnumValue *v1 = e1->values; v1; v1 = v1->next)
        for (const AstEnumValue *v2 = e2->values; v2; v2 = v2->next)
          if (str_eq(v1->name, v2->name))
            diag_add(c->d, name, v2->line, v2->col,
                     "\"%.*s\" is already defined in \"%s\".",
                     (int)v2->name.n, v2->name.p, pkg);
  /* extensions: the host must declare a range that covers the number */
  for (const AstExtend *x = f->extends; x; x = x->next) {
    int k = 0;
    const char *host = resolve(c, pkg, x->extendee, &k);
    const AstMessage *hm = NULL;
    for (const AstMessage *m = f->messages; m && !hm; m = m->next)
      if (strcmp(join(c->a, pkg, m->name), host) == 0) hm = m;
    if (!hm) continue;   /* declared in another file: not checked here */
    for (const AstField *fl = x->fields; fl; fl = fl->next) {
      bool covered = false;
      for (const AstExtRange *r = hm->ext_ranges; r && !covered; r = r->next)
        for (const AstRange *g = r->ranges; g; g = g->next) {
          int64_t hi = g->has_end ? (g->end_is_max ? 536870911 : g->end) : g->start;
          if ((int64_t)fl->number >= g->start && (int64_t)fl->number <= hi) { covered = true; break; }
        }
      if (!covered)
        diag_add(c->d, name, fl->line, fl->col,
                 "\"%.*s\" does not declare %llu as an extension number.",
                 (int)x->extendee.n, x->extendee.p, (unsigned long long)fl->number);
      for (const AstExtend *x2 = f->extends; x2; x2 = x2->next) {
        if (!str_eq(x2->extendee, x->extendee)) continue;
        for (const AstField *o = x2->fields; o; o = o->next) {
          if (o == fl) continue;
          if (o->number == fl->number && (x2 != x || o > fl))
            diag_add(c->d, name, o->line, o->col,
                     "Extension number %llu has already been used in \"%.*s\".",
                     (unsigned long long)o->number, (int)x->extendee.n, x->extendee.p);
        }
      }
    }
  }
}

Dyn *build_file(BuildCtx *c, const AstFile *f, const char *name) {
  bootstrap(c);
  c->cur_file = name;
  if (!c->quiet) { validate_file(c, f, name); validate_file2(c, f, name); validate_file3(c, f, name); }
  const char *pkg = f->has_package ? arena_strndup(c->a, f->package.p, f->package.n) : "";

  collect_msg(c, f->messages, pkg);
  collect_enum(c, f->enums, pkg);

  Dyn *fd = dyn_new(c->a);
  dyn_put_str(fd, F_FILEDESCRIPTORPROTO_NAME, name);
  if (f->has_package) dyn_put_bytes(fd, F_FILEDESCRIPTORPROTO_PACKAGE, f->package);

  /* dependency in declaration order; public/weak are indices into it. */
  int idx = 0;
  for (const AstImport *im = f->imports; im; im = im->next) {
    if (im->kind == AST_IMP_OPTION) continue;
    dyn_put_bytes(fd, F_FILEDESCRIPTORPROTO_DEPENDENCY, im->path);
    idx++;
  }
  idx = 0;
  for (const AstImport *im = f->imports; im; im = im->next) {
    if (im->kind == AST_IMP_OPTION) continue;
    if (im->kind == AST_IMP_PUBLIC)
      dyn_put_i32(fd, F_FILEDESCRIPTORPROTO_PUBLIC_DEPENDENCY, idx);
    idx++;
  }
  idx = 0;
  for (const AstImport *im = f->imports; im; im = im->next) {
    if (im->kind == AST_IMP_OPTION) continue;
    if (im->kind == AST_IMP_WEAK)
      dyn_put_i32(fd, F_FILEDESCRIPTORPROTO_WEAK_DEPENDENCY, idx);
    idx++;
  }
  for (const AstImport *im = f->imports; im; im = im->next)
    if (im->kind == AST_IMP_OPTION)
      dyn_put_bytes(fd, F_FILEDESCRIPTORPROTO_OPTION_DEPENDENCY, im->path);

  int edition = f->edition ? f->edition
                           : (f->syntax_kind == AST_SYN_PROTO3 ? ED_PROTO3 : ED_PROTO2);

  for (const AstMessage *m = f->messages; m; m = m->next) {
    Dyn *md = dyn_put_msg(fd, F_FILEDESCRIPTORPROTO_MESSAGE_TYPE);
    emit_msg(c, md, m, pkg, edition);
  }
  for (const AstEnum *e = f->enums; e; e = e->next) {
    Dyn *ed = dyn_put_msg(fd, F_FILEDESCRIPTORPROTO_ENUM_TYPE);
    emit_enum(c, ed, e, pkg);
  }
  for (const AstService *s = f->services; s; s = s->next) {
    Dyn *sd = dyn_put_msg(fd, F_FILEDESCRIPTORPROTO_SERVICE);
    dyn_put_bytes(sd, F_SERVICEDESCRIPTORPROTO_NAME, s->name);
    attach_options(c, sd, F_SERVICEDESCRIPTORPROTO_OPTIONS,
                   "google.protobuf.ServiceOptions", pkg, s->options);
    for (const AstMethod *me = s->methods; me; me = me->next) {
      Dyn *md = dyn_put_msg(sd, F_SERVICEDESCRIPTORPROTO_METHOD);
      int k = 0;
      dyn_put_bytes(md, F_METHODDESCRIPTORPROTO_NAME, me->name);
      dyn_put_str(md, F_METHODDESCRIPTORPROTO_INPUT_TYPE,
                  arena_printf(c->a, ".%s", resolve(c, pkg, me->input_type, &k)));
      dyn_put_str(md, F_METHODDESCRIPTORPROTO_OUTPUT_TYPE,
                  arena_printf(c->a, ".%s", resolve(c, pkg, me->output_type, &k)));
      if (me->client_streaming)
        dyn_put_bool(md, F_METHODDESCRIPTORPROTO_CLIENT_STREAMING, true);
      if (me->server_streaming)
        dyn_put_bool(md, F_METHODDESCRIPTORPROTO_SERVER_STREAMING, true);
      attach_options(c, md, F_METHODDESCRIPTORPROTO_OPTIONS,
                     "google.protobuf.MethodOptions", pkg, me->options);
      /* `rpc F(M) returns (M) {}` emits an EMPTY MethodOptions; a
       * `;`-terminated method emits none at all. */
      if (me->has_body && dyn_count(md, F_METHODDESCRIPTORPROTO_OPTIONS) == 0)
        dyn_put_bytes(md, F_METHODDESCRIPTORPROTO_OPTIONS, str_of("", 0));
    }
  }
  for (const AstExtend *x = f->extends; x; x = x->next)
    for (const AstField *fl = x->fields; fl; fl = fl->next) {
      Dyn *xd = dyn_put_msg(fd, F_FILEDESCRIPTORPROTO_EXTENSION);
      emit_field(c, xd, fl, pkg, edition, -1, false);
      attach_options(c, xd, F_FIELDDESCRIPTORPROTO_OPTIONS,
                     "google.protobuf.FieldOptions", pkg, fl->options);
    }

  attach_options(c, fd, F_FILEDESCRIPTORPROTO_OPTIONS,
                 "google.protobuf.FileOptions", pkg, f->options);

  /* proto2 emits no syntax at all; proto3 emits "proto3"; editions emit
   * "editions" plus the edition enum. */
  if (f->syntax_kind == AST_SYN_PROTO3)
    dyn_put_str(fd, F_FILEDESCRIPTORPROTO_SYNTAX, "proto3");
  else if (f->syntax_kind == AST_SYN_EDITION) {
    dyn_put_str(fd, F_FILEDESCRIPTORPROTO_SYNTAX, "editions");
    dyn_put_i32(fd, F_FILEDESCRIPTORPROTO_EDITION, f->edition);
  }

  BuiltFile *bf = arena_calloc(c->a, sizeof(BuiltFile));
  bf->name = name;
  bf->ast = f;
  bf->fdp = fd;
  bf->next = c->files;
  c->files = bf;
  c->n_files++;
  return fd;
}
