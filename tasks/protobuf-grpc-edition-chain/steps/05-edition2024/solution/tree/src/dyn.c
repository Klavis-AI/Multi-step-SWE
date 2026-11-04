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
  uint64_t seq;          /* arrival order, for oneof last-wins */
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
  uint64_t next_seq;
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
  v->seq = ++m->next_seq;
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

/* Parses until end-of-input, or until an EGROUP whose field number is
 * `end_group`. Returns false on malformed input, including an EGROUP that does
 * not match the group it purports to close -- protobuf requires the closing
 * tag to carry the same field number as the opening one, and a depth counter
 * cannot tell. */
static bool dyn_parse_into(Arena *a, Dyn *m, WRd *r, int64_t end_group,
                           const char **err) {
  for (;;) {
    if (wrd_eof(r)) {
      if (end_group >= 0) { if (err) *err = "unterminated group"; return false; }
      return true;
    }
    uint32_t field;
    int wt;
    if (!wrd_tag(r, &field, &wt)) { if (err) *err = r->err; return false; }
    if (wt == WT_EGROUP) {
      if (end_group < 0 || (uint32_t)end_group != field) {
        if (err) *err = "mismatched group end tag";
        return false;
      }
      return true;
    }
    switch (wt) {
      case WT_VARINT: {
        uint64_t v;
        if (!wrd_varint(r, &v)) { if (err) *err = r->err; return false; }
        dyn_put_varint(m, field, v);
        break;
      }
      case WT_I32: {
        uint32_t v;
        if (!wrd_fixed32(r, &v)) { if (err) *err = r->err; return false; }
        dyn_put_fixed32(m, field, v);
        break;
      }
      case WT_I64: {
        uint64_t v;
        if (!wrd_fixed64(r, &v)) { if (err) *err = r->err; return false; }
        dyn_put_fixed64(m, field, v);
        break;
      }
      case WT_LEN: {
        Str sb;
        if (!wrd_len(r, &sb)) { if (err) *err = r->err; return false; }
        dyn_put_bytes(m, field, sb);
        break;
      }
      case WT_SGROUP: {
        Dyn *g = dyn_new(a);
        if (!dyn_parse_into(a, g, r, (int64_t)field, err)) return false;
        DynVal *slot = push(m, field, K_GROUP);
        slot->msg = g;
        break;
      }
      default:
        if (err) *err = "unexpected wire type";
        return false;
    }
  }
}

Dyn *dyn_parse_bytes(Arena *a, Str in, const char **err) {
  Dyn *m = dyn_new(a);
  WRd r;
  wrd_init(&r, in);
  if (!dyn_parse_into(a, m, &r, -1, err)) return NULL;
  return m;
}


uint64_t dyn_seq(const Dyn *m, uint32_t field, int idx) {
  const DynVal *v = nth(m, field, idx);
  return v ? v->seq : 0;
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

static bool unpack_varint(const char **p, const char *e, uint64_t *out) {
  uint64_t v = 0; int shift = 0;
  while (*p < e) {
    unsigned char c = (unsigned char)*(*p)++;
    v |= (uint64_t)(c & 0x7f) << shift;
    if (!(c & 0x80)) { *out = v; return true; }
    shift += 7;
    if (shift > 63) return false;
  }
  return false;
}

/* Expands a repeated numeric field's packed payloads into individual values,
 * preserving arrival order. A repeated field may legally arrive split across
 * packed and unpacked occurrences, so every K_BYTES value is expanded in place
 * and the already-individual values are kept where they sit. */
void dyn_unpack_field(Dyn *m, uint32_t field, int wire_type) {
  DynField *f = NULL;
  for (DynField *x = m->head; x; x = x->next)
    if (x->num == field) { f = x; break; }
  if (!f || !f->head) return;
  DynVal *head = NULL, **tail = &head;
  for (DynVal *v = f->head; v; v = v->next) {
    if (v->kind != K_BYTES) {
      DynVal *c = arena_calloc(m->a, sizeof(DynVal));
      *c = *v; c->next = NULL;
      *tail = c; tail = &c->next;
      continue;
    }
    const char *q = v->bytes.p, *e = v->bytes.p + v->bytes.n;
    while (q < e) {
      uint64_t u = 0;
      if (wire_type == WT_VARINT) { if (!unpack_varint(&q, e, &u)) break; }
      else if (wire_type == WT_I32) { if (e - q < 4) break; uint32_t t; memcpy(&t, q, 4); q += 4; u = t; }
      else if (wire_type == WT_I64) { if (e - q < 8) break; uint64_t t; memcpy(&t, q, 8); q += 8; u = t; }
      else break;
      DynVal *c = arena_calloc(m->a, sizeof(DynVal));
      c->kind = (wire_type == WT_VARINT) ? K_VARINT : (wire_type == WT_I32 ? K_F32 : K_F64);
      c->u = u;
      c->seq = v->seq;
      *tail = c; tail = &c->next;
    }
  }
  if (head) { f->head = head; f->tail = tail; }
}

/* A 32-bit field truncates to 32 bits on re-encode: a varint decoding to
 * 4294967298 is written back as 2. int32 and enum then sign-extend, so -1
 * survives as a full ten-byte varint. A bool is any non-zero written as 1. */
void dyn_canon_field(Dyn *m, uint32_t field, int mode) {
  if (mode == DYN_CANON_NONE) return;
  DynField *f = NULL;
  for (DynField *x = m->head; x; x = x->next)
    if (x->num == field) { f = x; break; }
  if (!f) return;
  for (DynVal *v = f->head; v; v = v->next) {
    if (v->kind != K_VARINT) continue;
    switch (mode) {
      case DYN_CANON_I32:  v->u = (uint64_t)(int64_t)(int32_t)(uint32_t)v->u; break;
      case DYN_CANON_U32:  v->u = (uint32_t)v->u; break;
      case DYN_CANON_BOOL: v->u = v->u ? 1 : 0; break;
      default: break;
    }
  }
}

/* A singular scalar appearing more than once keeps only the last occurrence.
 * Singular MESSAGE fields are excluded by the caller: protobuf merges those
 * rather than taking the last, and collapsing them would drop fields. */
void dyn_keep_last(Dyn *m, uint32_t field) {
  DynField *f = NULL;
  for (DynField *x = m->head; x; x = x->next)
    if (x->num == field) { f = x; break; }
  if (!f || !f->head || !f->head->next) return;
  DynVal *last = f->head;
  while (last->next) last = last->next;
  f->head = last;
  f->tail = &last->next;
}

/* Unlinks the most recently appended value of `field`. Used when a value is
 * built optimistically and then turns out not to belong: a map entry is
 * created before its value is parsed, so an ignored value would otherwise
 * leave an entry holding a default. */
void dyn_drop_last(Dyn *m, uint32_t field) {
  DynField *f = NULL;
  for (DynField *x = m->head; x; x = x->next)
    if (x->num == field) { f = x; break; }
  if (!f || !f->head) return;
  if (!f->head->next) { dyn_clear_field(m, field); return; }
  DynVal *v = f->head;
  while (v->next && v->next->next) v = v->next;
  v->next = NULL;
  f->tail = &v->next;
}

void dyn_clear_field(Dyn *m, uint32_t field) {
  DynField **pp = &m->head;
  while (*pp) {
    if ((*pp)->num == field) { *pp = (*pp)->next; return; }
    pp = &(*pp)->next;
  }
}

/* dyn_get_msg caches its parse but leaves the value K_BYTES, so serialize_into
 * would re-emit the ORIGINAL bytes and silently discard anything done to the
 * cached submessage. Promoting to K_MSG makes re-serialisation follow it. */
/* Rewrites a message-valued field to the group wire shape. Editions reach the
 * delimited encoding by feature rather than by declaring a proto2 group, so
 * the conversion happens after parsing, like packing. */
void dyn_make_group(Dyn *m, uint32_t field) {
  DynField *f = NULL;
  for (DynField *x = m->head; x; x = x->next)
    if (x->num == field) { f = x; break; }
  if (!f) return;
  for (DynVal *v = f->head; v; v = v->next) {
    if (v->kind == K_MSG) { v->kind = K_GROUP; continue; }
    if (v->kind == K_BYTES) {
      if (!v->msg) { const char *e = NULL; v->msg = dyn_parse_bytes(m->a, v->bytes, &e); }
      if (v->msg) v->kind = K_GROUP;
    }
  }
}

bool dyn_promote_msg(Dyn *m, uint32_t field, int idx) {
  DynVal *v = (DynVal *)nth(m, field, idx);
  if (!v) return false;
  if (v->kind == K_MSG || v->kind == K_GROUP) return true;
  if (v->kind != K_BYTES) return false;
  if (!v->msg) {
    const char *err = NULL;
    v->msg = dyn_parse_bytes(m->a, v->bytes, &err);
  }
  if (!v->msg) return false;
  v->kind = K_MSG;
  return true;
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
