/* pool.c -- FileDescriptorProto bytes -> resolved descriptor graph.
 *
 * This is the other half of self-hosting. build.c goes source -> descriptor;
 * this goes descriptor -> a graph the codecs can drive. Because it reads the
 * serialized form rather than the AST, it also handles descriptor sets that
 * came from somewhere else entirely -- which is exactly what
 * --descriptor_set_in needs, and what lets sets recorded from older protoc
 * majors be consumed faithfully.
 */
#include "desc.h"
#include "descnum.h"
#include <string.h>
#include <stdlib.h>

struct Pool {
  Arena *a;
  FileD *files;
  /* Flat index of every message and enum, by fully-qualified name. */
  struct PoolSym { const char *fqn; const Desc *d; const EnumD *e; struct PoolSym *next; } *syms;
};

Pool *pool_new(Arena *a) {
  Pool *p = arena_calloc(a, sizeof(Pool));
  p->a = a;
  return p;
}

static void pool_index(Pool *p, const char *fqn, const Desc *d, const EnumD *e) {
  struct PoolSym *s = arena_calloc(p->a, sizeof *s);
  s->fqn = fqn; s->d = d; s->e = e; s->next = p->syms;
  p->syms = s;
}

const Desc *pool_find_message(const Pool *p, const char *full_name) {
  for (struct PoolSym *s = p->syms; s; s = s->next)
    if (s->d && strcmp(s->fqn, full_name) == 0) return s->d;
  return NULL;
}

const EnumD *pool_find_enum(const Pool *p, const char *full_name) {
  for (struct PoolSym *s = p->syms; s; s = s->next)
    if (s->e && strcmp(s->fqn, full_name) == 0) return s->e;
  return NULL;
}

static const FieldD *scan_ext(const FieldD *list, const char *extendee,
                              const char *ext_full_name) {
  for (const FieldD *f = list; f; f = f->next) {
    if (!f->extendee || !f->full_name) continue;
    const char *e = f->extendee[0] == '.' ? f->extendee + 1 : f->extendee;
    if (strcmp(e, extendee) == 0 && strcmp(f->full_name, ext_full_name) == 0)
      return f;
  }
  return NULL;
}

static const FieldD *scan_ext_desc(const Desc *d, const char *extendee,
                                   const char *ext_full_name) {
  const FieldD *hit = scan_ext(d->extensions, extendee, ext_full_name);
  if (hit) return hit;
  for (const Desc *n = d->nested; n; n = n->next) {
    hit = scan_ext_desc(n, extendee, ext_full_name);
    if (hit) return hit;
  }
  return NULL;
}

const FieldD *pool_find_extension(const Pool *p, const char *extendee_full_name,
                                  const char *ext_full_name) {
  for (const FileD *f = p->files; f; f = f->next) {
    const FieldD *hit = scan_ext(f->extensions, extendee_full_name, ext_full_name);
    if (hit) return hit;
    for (const Desc *d = f->messages; d; d = d->next) {
      hit = scan_ext_desc(d, extendee_full_name, ext_full_name);
      if (hit) return hit;
    }
  }
  return NULL;
}

const FieldD *desc_field_by_number(const Desc *d, uint32_t number) {
  for (FieldD *f = d->fields; f; f = f->next)
    if (f->number == number) return f;
  return NULL;
}

const FieldD *desc_field_by_name(const Desc *d, const char *name) {
  for (FieldD *f = d->fields; f; f = f->next)
    if (strcmp(f->name, name) == 0) return f;
  return NULL;
}

static const char *qualify(Arena *a, const char *scope, const char *leaf) {
  if (!scope || !*scope) return arena_strdup(a, leaf);
  return arena_printf(a, "%s.%s", scope, leaf);
}

/* ---- small helpers over a parsed Dyn --------------------------------- */

static const char *dyn_cstr(Arena *a, const Dyn *m, uint32_t field, int idx) {
  Str s;
  if (!dyn_get_bytes(m, field, idx, &s)) return NULL;
  return arena_strndup(a, s.p, s.n);
}

static int64_t dyn_int(const Dyn *m, uint32_t field, int idx, int64_t dflt) {
  uint64_t v;
  if (!dyn_get_varint(m, field, idx, &v)) return dflt;
  return (int64_t)v;
}

/* ---- construction ---------------------------------------------------- */

static FieldD *make_field(Pool *p, const Dyn *fp, const Desc *parent,
                          const char *scope, int file_edition) {
  Arena *a = p->a;
  FieldD *f = arena_calloc(a, sizeof(FieldD));
  f->name = dyn_cstr(a, fp, F_FIELDDESCRIPTORPROTO_NAME, 0);
  f->json_name = dyn_cstr(a, fp, F_FIELDDESCRIPTORPROTO_JSON_NAME, 0);
  f->number = (uint32_t)dyn_int(fp, F_FIELDDESCRIPTORPROTO_NUMBER, 0, 0);
  f->type = (int)dyn_int(fp, F_FIELDDESCRIPTORPROTO_TYPE, 0, 0);
  f->label = (int)dyn_int(fp, F_FIELDDESCRIPTORPROTO_LABEL, 0, L_OPTIONAL);
  f->oneof_index = (int)dyn_int(fp, F_FIELDDESCRIPTORPROTO_ONEOF_INDEX, 0, -1);
  f->proto3_optional = dyn_int(fp, F_FIELDDESCRIPTORPROTO_PROTO3_OPTIONAL, 0, 0) != 0;
  f->default_value = dyn_cstr(a, fp, F_FIELDDESCRIPTORPROTO_DEFAULT_VALUE, 0);
  f->extendee = dyn_cstr(a, fp, F_FIELDDESCRIPTORPROTO_EXTENDEE, 0);
  f->is_extension = f->extendee != NULL;
  const char *tn = dyn_cstr(a, fp, F_FIELDDESCRIPTORPROTO_TYPE_NAME, 0);
  if (tn) f->type_name = (tn[0] == '.') ? tn + 1 : tn;
  f->parent = parent;
  f->full_name = f->name ? qualify(a, scope, f->name) : NULL;

  /* Packedness decides the wire shape of a repeated numeric field, so it has
   * to survive into the graph. proto2 packs only when asked; proto3 packs by
   * default unless [packed=false]. Editions express the same thing through
   * features.repeated_field_encoding, resolved by the caller. */
  Dyn *fopts = dyn_get_msg(fp, F_FIELDDESCRIPTORPROTO_OPTIONS, 0);
  bool numeric = !(f->type == T_STRING || f->type == T_BYTES ||
                   f->type == T_MESSAGE || f->type == T_GROUP);
  f->packed = (file_edition == ED_PROTO3) && numeric;
  if (fopts) {
    uint64_t pv;
    if (dyn_get_varint(fopts, F_FIELDOPTIONS_PACKED, 0, &pv)) f->packed = pv != 0;
    uint64_t rv;
    if (dyn_get_varint(fopts, F_FIELDOPTIONS_RETENTION, 0, &rv)) f->retention = (int)rv;
  }
  return f;
}

static EnumD *make_enum(Pool *p, const Dyn *ep, const char *scope, const FileD *file) {
  Arena *a = p->a;
  EnumD *e = arena_calloc(a, sizeof(EnumD));
  e->name = dyn_cstr(a, ep, F_ENUMDESCRIPTORPROTO_NAME, 0);
  e->full_name = qualify(a, scope, e->name ? e->name : "");
  e->file = file;
  EnumValD **tail = &e->values;
  for (int i = 0; i < dyn_count(ep, F_ENUMDESCRIPTORPROTO_VALUE); i++) {
    Dyn *vp = dyn_get_msg(ep, F_ENUMDESCRIPTORPROTO_VALUE, i);
    if (!vp) continue;
    EnumValD *v = arena_calloc(a, sizeof(EnumValD));
    v->name = dyn_cstr(a, vp, F_ENUMVALUEDESCRIPTORPROTO_NAME, 0);
    v->number = (int32_t)dyn_int(vp, F_ENUMVALUEDESCRIPTORPROTO_NUMBER, 0, 0);
    *tail = v; tail = &v->next;
  }
  pool_index(p, e->full_name, NULL, e);
  return e;
}

static Desc *make_desc(Pool *p, const Dyn *dp, const char *scope,
                       const FileD *file, const Desc *parent) {
  Arena *a = p->a;
  Desc *d = arena_calloc(a, sizeof(Desc));
  d->name = dyn_cstr(a, dp, F_DESCRIPTORPROTO_NAME, 0);
  d->full_name = qualify(a, scope, d->name ? d->name : "");
  d->file = file;
  d->parent = parent;
  pool_index(p, d->full_name, d, NULL);

  Dyn *opts = dyn_get_msg(dp, F_DESCRIPTORPROTO_OPTIONS, 0);
  if (opts) d->is_map_entry = dyn_int(opts, F_MESSAGEOPTIONS_MAP_ENTRY, 0, 0) != 0;

  FieldD **ft = &d->fields;
  for (int i = 0; i < dyn_count(dp, F_DESCRIPTORPROTO_FIELD); i++) {
    Dyn *fp = dyn_get_msg(dp, F_DESCRIPTORPROTO_FIELD, i);
    if (!fp) continue;
    FieldD *f = make_field(p, fp, d, d->full_name, file->edition);
    *ft = f; ft = &f->next;
  }
  FieldD **xt = &d->extensions;
  for (int i = 0; i < dyn_count(dp, F_DESCRIPTORPROTO_EXTENSION); i++) {
    Dyn *fp = dyn_get_msg(dp, F_DESCRIPTORPROTO_EXTENSION, i);
    if (!fp) continue;
    FieldD *f = make_field(p, fp, d, d->full_name, file->edition);
    *xt = f; xt = &f->next;
  }
  Desc **nt = &d->nested;
  for (int i = 0; i < dyn_count(dp, F_DESCRIPTORPROTO_NESTED_TYPE); i++) {
    Dyn *np = dyn_get_msg(dp, F_DESCRIPTORPROTO_NESTED_TYPE, i);
    if (!np) continue;
    Desc *n = make_desc(p, np, d->full_name, file, d);
    *nt = n; nt = &n->next;
  }
  EnumD **et = &d->enums;
  for (int i = 0; i < dyn_count(dp, F_DESCRIPTORPROTO_ENUM_TYPE); i++) {
    Dyn *ep = dyn_get_msg(dp, F_DESCRIPTORPROTO_ENUM_TYPE, i);
    if (!ep) continue;
    EnumD *e = make_enum(p, ep, d->full_name, file);
    *et = e; et = &e->next;
  }
  return d;
}

/* Second pass: point every field at its resolved message/enum. Done after the
 * whole file is indexed so forward and cyclic references work. */
static void link_fields(Pool *p, Desc *d) {
  for (FieldD *f = d->fields; f; f = f->next) {
    if (!f->type_name) continue;
    if (f->type == T_MESSAGE || f->type == T_GROUP) f->msg_type = pool_find_message(p, f->type_name);
    else if (f->type == T_ENUM) f->enum_type = pool_find_enum(p, f->type_name);
  }
  for (FieldD *f = d->extensions; f; f = f->next) {
    if (!f->type_name) continue;
    if (f->type == T_MESSAGE || f->type == T_GROUP) f->msg_type = pool_find_message(p, f->type_name);
    else if (f->type == T_ENUM) f->enum_type = pool_find_enum(p, f->type_name);
  }
  for (Desc *n = d->nested; n; n = n->next) link_fields(p, n);
}

const FileD *pool_add_serialized(Pool *p, Str bytes, const char **err) {
  Arena *a = p->a;
  Dyn *fp = dyn_parse_bytes(a, bytes, err);
  if (!fp) return NULL;

  FileD *file = arena_calloc(a, sizeof(FileD));
  file->name = dyn_cstr(a, fp, F_FILEDESCRIPTORPROTO_NAME, 0);
  file->package = dyn_cstr(a, fp, F_FILEDESCRIPTORPROTO_PACKAGE, 0);
  const char *syn = dyn_cstr(a, fp, F_FILEDESCRIPTORPROTO_SYNTAX, 0);
  if (syn && strcmp(syn, "editions") == 0)
    file->edition = (int)dyn_int(fp, F_FILEDESCRIPTORPROTO_EDITION, 0, ED_2023);
  else if (syn && strcmp(syn, "proto3") == 0) file->edition = ED_PROTO3;
  else file->edition = ED_PROTO2;

  const char *scope = file->package ? file->package : "";
  Desc **mt = &file->messages;
  for (int i = 0; i < dyn_count(fp, F_FILEDESCRIPTORPROTO_MESSAGE_TYPE); i++) {
    Dyn *dp = dyn_get_msg(fp, F_FILEDESCRIPTORPROTO_MESSAGE_TYPE, i);
    if (!dp) continue;
    Desc *d = make_desc(p, dp, scope, file, NULL);
    *mt = d; mt = &d->next;
  }
  EnumD **et = &file->enums;
  for (int i = 0; i < dyn_count(fp, F_FILEDESCRIPTORPROTO_ENUM_TYPE); i++) {
    Dyn *ep = dyn_get_msg(fp, F_FILEDESCRIPTORPROTO_ENUM_TYPE, i);
    if (!ep) continue;
    EnumD *e = make_enum(p, ep, scope, file);
    *et = e; et = &e->next;
  }
  FieldD **xt = &file->extensions;
  for (int i = 0; i < dyn_count(fp, F_FILEDESCRIPTORPROTO_EXTENSION); i++) {
    Dyn *xp = dyn_get_msg(fp, F_FILEDESCRIPTORPROTO_EXTENSION, i);
    if (!xp) continue;
    FieldD *f = make_field(p, xp, NULL, scope, file->edition);
    *xt = f; xt = &f->next;
  }

  for (Desc *d = file->messages; d; d = d->next) link_fields(p, d);
  /* File-level extensions need the same second pass: a custom option whose
   * type is a message is unusable until its msg_type is resolved. */
  for (FieldD *f = file->extensions; f; f = f->next) {
    if (!f->type_name) continue;
    if (f->type == T_MESSAGE || f->type == T_GROUP) f->msg_type = pool_find_message(p, f->type_name);
    else if (f->type == T_ENUM) f->enum_type = pool_find_enum(p, f->type_name);
  }

  FileD **tail = &p->files;
  while (*tail) tail = &(*tail)->next;
  *tail = file;
  return file;
}

int pool_add_set(Pool *p, Str set_bytes, const char **err) {
  Dyn *set = dyn_parse_bytes(p->a, set_bytes, err);
  if (!set) return -1;
  int n = 0;
  for (int i = 0; i < dyn_count(set, 1); i++) {
    Str fb;
    if (!dyn_get_bytes(set, 1, i, &fb)) continue;
    if (!pool_add_serialized(p, fb, err)) return -1;
    n++;
  }
  return n;
}
