/* dyn.c — the dynamic message.
 *
 * Schema-free by construction: a value records the encoding the caller asked
 * for, so a FileDescriptorProto can be built and serialized before any
 * descriptor for descriptor.proto exists. Fields are held in a list kept
 * sorted by field number, and values within a field in insertion order, so
 * dyn_serialize emits ascending field numbers with no sort step. That is what
 * protoc does, and byte-identical descriptor output depends on it.
 */
#include "wire.h"
#include <string.h>

enum { K_VARINT, K_F32, K_F64, K_BYTES, K_MSG, K_GROUP, K_RAW };

typedef struct DynVal {
  struct DynVal *next;
  int kind;
  uint64_t u;
  Str bytes;
  Dyn *msg;
} DynVal;

typedef struct DynField {
  struct DynField *next;
  uint32_t num;
  DynVal *head, **tail;
} DynField;

struct Dyn {
  Arena *a;
  DynField *head;
};

Dyn *dyn_new(Arena *a) {
  Dyn *m = arena_calloc(a, sizeof(Dyn));
  m->a = a;
  return m;
}

void dyn_clear(Dyn *m) { m->head = NULL; }

static DynField *field_for(Dyn *m, uint32_t num) {
  DynField **pp = &m->head;
  while (*pp && (*pp)->num < num) pp = &(*pp)->next;
  if (*pp && (*pp)->num == num) return *pp;
  DynField *f = arena_calloc(m->a, sizeof(DynField));
  f->num = num;
  f->tail = &f->head;
  f->next = *pp;
  *pp = f;
  return f;
}

static DynVal *push(Dyn *m, uint32_t num, int kind) {
  DynField *f = field_for(m, num);
  DynVal *v = arena_calloc(m->a, sizeof(DynVal));
  v->kind = kind;
  *f->tail = v;
  f->tail = &v->next;
  return v;
}

void dyn_put_varint(Dyn *m, uint32_t field, uint64_t v)  { push(m, field, K_VARINT)->u = v; }
void dyn_put_fixed32(Dyn *m, uint32_t field, uint32_t v) { push(m, field, K_F32)->u = v; }
void dyn_put_fixed64(Dyn *m, uint32_t field, uint64_t v) { push(m, field, K_F64)->u = v; }

void dyn_put_bytes(Dyn *m, uint32_t field, Str s) {
  DynVal *v = push(m, field, K_BYTES);
  char *copy = arena_alloc(m->a, s.n ? s.n : 1);
  if (s.n) memcpy(copy, s.p, s.n);
  v->bytes = str_of(copy, s.n);
}

void dyn_put_raw(Dyn *m, uint32_t field, Str tagged) {
  DynVal *v = push(m, field, K_RAW);
  char *copy = arena_alloc(m->a, tagged.n ? tagged.n : 1);
  if (tagged.n) memcpy(copy, tagged.p, tagged.n);
  v->bytes = str_of(copy, tagged.n);
}

Dyn *dyn_put_msg(Dyn *m, uint32_t field) {
  DynVal *v = push(m, field, K_MSG);
  v->msg = dyn_new(m->a);
  return v->msg;
}

Dyn *dyn_put_group(Dyn *m, uint32_t field) {
  DynVal *v = push(m, field, K_GROUP);
  v->msg = dyn_new(m->a);
  return v->msg;
}

void dyn_put_i32(Dyn *m, uint32_t f, int32_t v)  { dyn_put_varint(m, f, (uint64_t)(int64_t)v); }
void dyn_put_i64(Dyn *m, uint32_t f, int64_t v)  { dyn_put_varint(m, f, (uint64_t)v); }
void dyn_put_u32(Dyn *m, uint32_t f, uint32_t v) { dyn_put_varint(m, f, v); }
void dyn_put_u64(Dyn *m, uint32_t f, uint64_t v) { dyn_put_varint(m, f, v); }
void dyn_put_sint32(Dyn *m, uint32_t f, int32_t v) { dyn_put_varint(m, f, zigzag_enc(v)); }
void dyn_put_sint64(Dyn *m, uint32_t f, int64_t v) { dyn_put_varint(m, f, zigzag_enc(v)); }
void dyn_put_bool(Dyn *m, uint32_t f, bool v)    { dyn_put_varint(m, f, v ? 1 : 0); }

void dyn_put_float(Dyn *m, uint32_t f, float v) {
  uint32_t bits; memcpy(&bits, &v, 4); dyn_put_fixed32(m, f, bits);
}
void dyn_put_double(Dyn *m, uint32_t f, double v) {
  uint64_t bits; memcpy(&bits, &v, 8); dyn_put_fixed64(m, f, bits);
}
void dyn_put_str(Dyn *m, uint32_t f, const char *s) {
  dyn_put_bytes(m, f, str_of(s, strlen(s)));
}

static const DynField *find(const Dyn *m, uint32_t num) {
  for (const DynField *f = m->head; f; f = f->next) {
    if (f->num == num) return f;
    if (f->num > num) break;
  }
  return NULL;
}

static const DynVal *nth(const Dyn *m, uint32_t num, int idx) {
  const DynField *f = find(m, num);
  if (!f) return NULL;
  const DynVal *v = f->head;
  while (v && idx--) v = v->next;
  return v;
}

int dyn_count(const Dyn *m, uint32_t field) {
  const DynField *f = find(m, field);
  int n = 0;
  for (const DynVal *v = f ? f->head : NULL; v; v = v->next) n++;
  return n;
}

bool dyn_get_varint(const Dyn *m, uint32_t field, int idx, uint64_t *out) {
  const DynVal *v = nth(m, field, idx);
  if (!v || (v->kind != K_VARINT && v->kind != K_F32 && v->kind != K_F64)) return false;
  *out = v->u;
  return true;
}

bool dyn_get_bytes(const Dyn *m, uint32_t field, int idx, Str *out) {
  const DynVal *v = nth(m, field, idx);
  if (!v || (v->kind != K_BYTES && v->kind != K_RAW)) return false;
  *out = v->bytes;
  return true;
}

Dyn *dyn_get_msg(const Dyn *m, uint32_t field, int idx) {
  DynVal *v = (DynVal *)nth(m, field, idx);
  if (!v) return NULL;
  if (v->kind == K_MSG || v->kind == K_GROUP) return v->msg;
  /* Length-delimited bytes that a caller wants to read as a message: parse on
   * demand and cache. A schema-free parse cannot tell a submessage from a
   * string, so the decision belongs to whoever knows the schema. */
  if (v->kind == K_BYTES) {
    if (!v->msg) {
      const char *err = NULL;
      v->msg = dyn_parse_bytes(m->a, v->bytes, &err);
    }
    return v->msg;
  }
  return NULL;
}

Dyn *dyn_parse_bytes(Arena *a, Str in, const char **err) {
  Dyn *m = dyn_new(a);
  WRd r;
  wrd_init(&r, in);
  while (!wrd_eof(&r)) {
    uint32_t field;
    int wt;
    if (!wrd_tag(&r, &field, &wt)) { if (err) *err = r.err; return NULL; }
    switch (wt) {
      case WT_VARINT: {
        uint64_t v;
        if (!wrd_varint(&r, &v)) { if (err) *err = r.err; return NULL; }
        dyn_put_varint(m, field, v);
        break;
      }
      case WT_I32: {
        uint32_t v;
        if (!wrd_fixed32(&r, &v)) { if (err) *err = r.err; return NULL; }
        dyn_put_fixed32(m, field, v);
        break;
      }
      case WT_I64: {
        uint64_t v;
        if (!wrd_fixed64(&r, &v)) { if (err) *err = r.err; return NULL; }
        dyn_put_fixed64(m, field, v);
        break;
      }
      case WT_LEN: {
        Str s;
        if (!wrd_len(&r, &s)) { if (err) *err = r.err; return NULL; }
        dyn_put_bytes(m, field, s);
        break;
      }
      case WT_SGROUP: {
        /* Delimited group: collect until the matching EGROUP. */
        const uint8_t *start = r.p;
        int depth = 1;
        while (depth > 0 && !wrd_eof(&r)) {
          uint32_t f2;
          int w2;
          const uint8_t *before = r.p;
          if (!wrd_tag(&r, &f2, &w2)) { if (err) *err = r.err; return NULL; }
          if (w2 == WT_SGROUP) depth++;
          else if (w2 == WT_EGROUP) {
            depth--;
            if (depth == 0) {
              Str body = str_of((const char *)start, (size_t)(before - start));
              Dyn *g = dyn_parse_bytes(a, body, err);
              if (!g) return NULL;
              DynVal *slot = push(m, field, K_GROUP);
              slot->msg = g;
              break;
            }
          } else if (!wrd_skip_field(&r, f2, w2, NULL)) {
            if (err) *err = r.err;
            return NULL;
          }
        }
        if (depth > 0) { if (err) *err = "unterminated group"; return NULL; }
        break;
      }
      default:
        if (err) *err = "unexpected wire type";
        return NULL;
    }
  }
  return m;
}

int dyn_fields(const Dyn *m, uint32_t *out, int max) {
  int n = 0;
  for (const DynField *f = m->head; f && n < max; f = f->next) out[n++] = f->num;
  return n;
}

static void serialize_into(const Dyn *m, Buf *b) {
  for (const DynField *f = m->head; f; f = f->next) {
    for (const DynVal *v = f->head; v; v = v->next) {
      switch (v->kind) {
        case K_VARINT: wire_put_tag(b, f->num, WT_VARINT); wire_put_varint(b, v->u); break;
        case K_F32:    wire_put_tag(b, f->num, WT_I32);    wire_put_fixed32(b, (uint32_t)v->u); break;
        case K_F64:    wire_put_tag(b, f->num, WT_I64);    wire_put_fixed64(b, v->u); break;
        case K_BYTES:  wire_put_len(b, f->num, v->bytes); break;
        case K_RAW:    buf_put(b, v->bytes.p, v->bytes.n); break;
        case K_MSG: {
          Buf inner; buf_init(&inner, b->a);
          serialize_into(v->msg, &inner);
          wire_put_len(b, f->num, buf_str(&inner));
          break;
        }
        case K_GROUP:
          wire_put_tag(b, f->num, WT_SGROUP);
          serialize_into(v->msg, b);
          wire_put_tag(b, f->num, WT_EGROUP);
          break;
        default: break;
      }
    }
  }
}

void dyn_pack_field(Dyn *m, uint32_t field) {
  DynField *f = NULL;
  for (DynField *x = m->head; x; x = x->next)
    if (x->num == field) { f = x; break; }
  if (!f || !f->head) return;
  Buf b;
  buf_init(&b, m->a);
  for (DynVal *v = f->head; v; v = v->next) {
    switch (v->kind) {
      case K_VARINT: wire_put_varint(&b, v->u); break;
      case K_F32:    wire_put_fixed32(&b, (uint32_t)v->u); break;
      case K_F64:    wire_put_fixed64(&b, v->u); break;
      default: return;  /* not a packable field after all */
    }
  }
  Str packed = buf_str(&b);
  char *copy = arena_alloc(m->a, packed.n ? packed.n : 1);
  if (packed.n) memcpy(copy, packed.p, packed.n);
  DynVal *only = arena_calloc(m->a, sizeof(DynVal));
  only->kind = K_BYTES;
  only->bytes = str_of(copy, packed.n);
  f->head = only;
  f->tail = &only->next;
}

Str dyn_serialize(const Dyn *m, Arena *a) {
  Buf b; buf_init(&b, a);
  serialize_into(m, &b);
  return buf_str(&b);
}
