/* options.c -- `option` statements -> the options submessage.
 *
 * The rule, measured against protoc 32.1: an option is resolved to a real
 * field (or extension field) of the options message and encoded per that
 * field's type, at that field's number. Two consequences that cost bytes:
 *
 *   - `option (study.my_opt) = "tagged"` on a field emits
 *     `options { 50001: "tagged" }` -- the extension's own number.
 *   - `[deprecated = false]` emits `options { deprecated: false }` even though
 *     false is the default, because it was written.
 */
#include "options.h"
#include "descnum.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Finds a field of `d` by name, searching extensions declared anywhere in the
 * pool when the name was written in parentheses. */
static const FieldD *find_opt_field(const Pool *pool, const Desc *d,
                                    const char *name, bool is_ext,
                                    const char *scope) {
  if (!is_ext) {
    const FieldD *f = desc_field_by_name(d, name);
    if (f) return f;
    /* A group is written under its message name in text format, and an
     * aggregate option value IS text format. */
    for (const FieldD *g = d->fields; g; g = g->next)
      if (g->type == T_GROUP && g->msg_type && strcmp(g->msg_type->name, name) == 0)
        return g;
    return NULL;
  }

  /* An extension name may be written fully qualified or relative to the
   * package. Try the fully-qualified spelling first, then scope-qualified. */
  const FieldD *hit = pool_find_extension(pool, d->full_name, name);
  if (hit) return hit;
  if (scope && *scope) {
    size_t n = strlen(scope) + 1 + strlen(name) + 1;
    char *q = malloc(n);
    if (q) {
      snprintf(q, n, "%s.%s", scope, name);
      hit = pool_find_extension(pool, d->full_name, q);
      free(q);
    }
  }
  return hit;
}

/* Does this literal fit the option field's declared type? protoc refuses a
 * string assigned to a bool option, and so on. */
static bool value_fits(const FieldD *f, const AstValue *v) {
  switch (f->type) {
    case T_BOOL:
      return v->kind == AV_IDENT &&
             (str_eq_c(v->sval, "true") || str_eq_c(v->sval, "false"));
    case T_STRING: case T_BYTES:
      return v->kind == AV_STR;
    case T_DOUBLE: case T_FLOAT:
      return v->kind == AV_INT || v->kind == AV_FLOAT || v->kind == AV_IDENT;
    case T_ENUM:
      return v->kind == AV_IDENT || v->kind == AV_INT;
    case T_MESSAGE: case T_GROUP:
      return v->kind == AV_AGG;
    default:
      return v->kind == AV_INT;
  }
}

static void put_typed(Arena *a, Dyn *out, const FieldD *f, const AstValue *v) {
  int64_t iv = 0;
  double dv = 0;
  switch (v->kind) {
    case AV_INT:   iv = v->negative ? -(int64_t)v->ival : (int64_t)v->ival;
                   dv = (double)iv; break;
    case AV_FLOAT: dv = v->negative ? -v->fval : v->fval; iv = (int64_t)dv; break;
    case AV_IDENT:
      /* `inf` and `nan` are written as bare identifiers, and protoc stores the
       * IEEE bit patterns: inf -> 0x7ff0000000000000, -inf -> 0xff800000 as a
       * float, nan -> 0x7ff8000000000000. Falling through as 0 was why those
       * option values came out zeroed. */
      if (str_eq_c(v->sval, "inf") || str_eq_c(v->sval, "infinity"))
        dv = v->negative ? -INFINITY : INFINITY;
      else if (str_eq_c(v->sval, "nan"))
        dv = NAN;
      break;
    default: break;
  }
  switch (f->type) {
    case T_BOOL:
      dyn_put_bool(out, f->number,
                   v->kind == AV_IDENT ? str_eq_c(v->sval, "true") : iv != 0);
      break;
    case T_STRING: case T_BYTES:
      dyn_put_bytes(out, f->number, v->sval);
      break;
    case T_ENUM: {
      if (v->kind == AV_IDENT && f->enum_type) {
        char *nm = arena_strndup(a, v->sval.p, v->sval.n);
        for (const EnumValD *x = f->enum_type->values; x; x = x->next)
          if (strcmp(x->name, nm) == 0) { dyn_put_i32(out, f->number, x->number); return; }
      }
      dyn_put_i32(out, f->number, (int32_t)iv);
      break;
    }
    case T_DOUBLE: dyn_put_double(out, f->number, dv); break;
    case T_FLOAT:  dyn_put_float(out, f->number, (float)dv); break;
    case T_INT32:  case T_INT64:   dyn_put_i64(out, f->number, iv); break;
    case T_UINT32: case T_UINT64:  dyn_put_u64(out, f->number, (uint64_t)iv); break;
    case T_SINT32: dyn_put_sint32(out, f->number, (int32_t)iv); break;
    case T_SINT64: dyn_put_sint64(out, f->number, iv); break;
    case T_FIXED32: case T_SFIXED32: dyn_put_fixed32(out, f->number, (uint32_t)iv); break;
    case T_FIXED64: case T_SFIXED64: dyn_put_fixed64(out, f->number, (uint64_t)iv); break;
    default: break;
  }
}

/* An aggregate value `{ a: 1 b { c: 2 } }` is a text-format message against
 * the field's own message type. */
static void put_aggregate(Arena *a, Diags *d, const Pool *pool, const Desc *md,
                          const char *scope, const AstAggField *agg, Dyn *out);

static bool g_retain_source = false;
static const char *g_file = "";

static void put_one(Arena *a, Diags *d, const Pool *pool, const Desc *od,
                    const char *scope, const AstOptName *name,
                    const AstValue *v, Dyn *out) {
  char *nm = arena_strndup(a, name->name.p, name->name.n);
  const FieldD *f = find_opt_field(pool, od, nm, name->is_extension, scope);
  if (!f) {
    diag_add(d, g_file, name->line, name->col,
             "Option \"%s\" unknown. Ensure that your proto%s definition file "
             "imports the proto which defines the option.", nm,
             name->is_extension ? " extension" : "");
    return;
  }
  /* Source-retention options exist only for the compiler; protoc drops them
   * from the descriptor unless --retain_options is given. */
  if (f->retention == 2 && !g_retain_source) return;
  if (!name->next && v->kind != AV_LIST && !value_fits(f, v)) {
    diag_add(d, g_file, name->line, name->col,
             "Value must be of type %s for option \"%s\".",
             f->type == T_BOOL ? "bool" :
             (f->type == T_STRING || f->type == T_BYTES) ? "string" : "number",
             nm);
    return;
  }
  if (name->next) {
    /* A dotted path navigates into a submessage option. */
    if (f->type != T_MESSAGE || !f->msg_type) {
      diag_add(d, g_file, name->line, name->col,
               "Option \"%s\" is not a message and cannot be sub-scripted.", nm);
      return;
    }
    /* Measured: protoc MERGES repeated dotted-path statements addressing the
     * same extension into one field in the plain descriptor, and leaves them
     * as separate field instances under --retain_options. Same source, two
     * command lines, different block counts from protoc itself:
     *
     *   plain            options { 51001 { 1 { 1: 5   2 { 1: 6 } } } }
     *   --retain_options options { 51001 { 1 { 1: 5 } }
     *                              51001 { 1 { 2 { 1: 6 } } } }
     *
     * Semantically identical -- a parser merges repeated submessages of one
     * field -- but different bytes, and byte-identical is the bar. */
    Dyn *sub = g_retain_source ? NULL : dyn_get_msg(out, f->number, 0);
    if (!sub) sub = dyn_put_msg(out, f->number);
    put_one(a, d, pool, f->msg_type, scope, name->next, v, sub);
    return;
  }
  if (v->kind == AV_AGG) {
    if (f->type != T_MESSAGE && f->type != T_GROUP) {
      diag_add(d, g_file, name->line, name->col,
               "Option \"%s\" does not take an aggregate value.", nm);
      return;
    }
    Dyn *sub = (f->type == T_GROUP) ? dyn_put_group(out, f->number)
                                    : dyn_put_msg(out, f->number);
    if (f->msg_type) put_aggregate(a, d, pool, f->msg_type, scope, v->agg, sub);
    return;
  }
  put_typed(a, out, f, v);
}

static void put_aggregate(Arena *a, Diags *d, const Pool *pool, const Desc *md,
                          const char *scope, const AstAggField *agg, Dyn *out) {
  for (; agg; agg = agg->next) {
    char *nm = arena_strndup(a, agg->name.p, agg->name.n);
    const FieldD *f = find_opt_field(pool, md, nm, agg->is_extension, scope);
    if (!f) {
      diag_add(d, g_file, agg->line, agg->col,
               "Message type \"%s\" has no field named \"%s\".", md->full_name, nm);
      continue;
    }
    const AstValue *v = agg->value;
    if (!v) continue;
    if (v->kind == AV_AGG) {
      Dyn *sub = (f->type == T_GROUP) ? dyn_put_group(out, f->number)
                                      : dyn_put_msg(out, f->number);
      if (f->msg_type) put_aggregate(a, d, pool, f->msg_type, scope, v->agg, sub);
    } else if (v->kind == AV_LIST) {
      for (const AstValue *e = v->list; e; e = e->next) {
        if (e->kind == AV_AGG) {
          Dyn *sub = dyn_put_msg(out, f->number);
          if (f->msg_type) put_aggregate(a, d, pool, f->msg_type, scope, e->agg, sub);
        } else {
          put_typed(a, out, f, e);
        }
      }
    } else {
      put_typed(a, out, f, v);
    }
  }
}

void options_interpret(Arena *a, Diags *d, const Pool *pool, const Desc *opt_desc,
                       const char *scope, const AstOption *opts, Dyn *out,
                       bool retain_source, const char *file) {
  g_retain_source = retain_source;
  g_file = file ? file : "";
  if (!opt_desc) return;
  for (const AstOption *o = opts; o; o = o->next)
    if (o->name && o->value)
      put_one(a, d, pool, opt_desc, scope, o->name, o->value, out);
}
