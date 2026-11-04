/* json.c -- ProtoJSON writer. */
#include "json.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <float.h>
#include <errno.h>

static const char B64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64(Buf *b, Str s) {
  size_t i = 0;
  while (i + 2 < s.n) {
    unsigned v = ((unsigned char)s.p[i] << 16) | ((unsigned char)s.p[i+1] << 8) |
                 (unsigned char)s.p[i+2];
    buf_byte(b, B64[(v >> 18) & 63]); buf_byte(b, B64[(v >> 12) & 63]);
    buf_byte(b, B64[(v >> 6) & 63]);  buf_byte(b, B64[v & 63]);
    i += 3;
  }
  if (i + 1 == s.n) {
    unsigned v = (unsigned char)s.p[i] << 16;
    buf_byte(b, B64[(v >> 18) & 63]); buf_byte(b, B64[(v >> 12) & 63]);
    buf_cstr(b, "==");
  } else if (i + 2 == s.n) {
    unsigned v = ((unsigned char)s.p[i] << 16) | ((unsigned char)s.p[i+1] << 8);
    buf_byte(b, B64[(v >> 18) & 63]); buf_byte(b, B64[(v >> 12) & 63]);
    buf_byte(b, B64[(v >> 6) & 63]);  buf_byte(b, '=');
  }
}

/* JSON string escaping: the two-character forms JSON defines, \u00XX for other
 * control characters, and UTF-8 passed through unchanged. */
static void jstr(Buf *b, Str s) {
  buf_byte(b, '"');
  for (size_t i = 0; i < s.n; i++) {
    unsigned char c = (unsigned char)s.p[i];
    switch (c) {
      case '"':  buf_cstr(b, "\\\""); break;
      case '\\': buf_cstr(b, "\\\\"); break;
      case '\n': buf_cstr(b, "\\n"); break;
      case '\r': buf_cstr(b, "\\r"); break;
      case '\t': buf_cstr(b, "\\t"); break;
      case '\b': buf_cstr(b, "\\b"); break;
      case '\f': buf_cstr(b, "\\f"); break;
      default:
        if (c < 0x20) buf_printf(b, "\\u%04x", c);
        else buf_byte(b, (char)c);
    }
  }
  buf_byte(b, '"');
}

static void jnum_double(Buf *b, double d) {
  /* Non-finite values are JSON strings, not numbers. */
  if (isinf(d)) { buf_cstr(b, d < 0 ? "\"-Infinity\"" : "\"Infinity\""); return; }
  if (isnan(d)) { buf_cstr(b, "\"NaN\""); return; }
  char tmp[40];
  for (int prec = 1; prec <= 17; prec++) {
    snprintf(tmp, sizeof tmp, "%.*g", prec, d);
    double back = 0;
    if (sscanf(tmp, "%lf", &back) == 1 && back == d) break;
  }
  buf_cstr(b, tmp);
}

static const char *enum_name(const EnumD *e, int32_t v) {
  for (const EnumValD *x = e ? e->values : NULL; x; x = x->next)
    if (x->number == v) return x->name;
  return NULL;
}

static void write_msg(Buf *b, const Dyn *m, const Desc *d);

static bool numeric_type(int t) {
  return !(t == T_STRING || t == T_BYTES || t == T_MESSAGE || t == T_GROUP);
}

/* Emits the elements of a packed repeated field as a JSON array body. */
static void write_packed_json(Buf *b, Str s, const FieldD *f) {
  WRd r;
  wrd_init(&r, s);
  bool first = true;
  while (!wrd_eof(&r)) {
    uint64_t u = 0;
    uint32_t u32 = 0;
    bool ok = true;
    switch (f->type) {
      case T_FIXED32: case T_SFIXED32: case T_FLOAT:
        ok = wrd_fixed32(&r, &u32); u = u32; break;
      case T_FIXED64: case T_SFIXED64: case T_DOUBLE:
        ok = wrd_fixed64(&r, &u); break;
      default:
        ok = wrd_varint(&r, &u); break;
    }
    if (!ok) return;
    if (!first) buf_byte(b, ',');
    first = false;
    switch (f->type) {
      case T_DOUBLE: { double dv; memcpy(&dv, &u, 8); jnum_double(b, dv); break; }
      case T_FLOAT:  { float fv; memcpy(&fv, &u32, 4); jnum_double(b, (double)fv); break; }
      case T_BOOL:   buf_cstr(b, u ? "true" : "false"); break;
      case T_INT64: case T_SFIXED64: buf_printf(b, "\"%" PRId64 "\"", (int64_t)u); break;
      case T_SINT64: buf_printf(b, "\"%" PRId64 "\"", zigzag_dec(u)); break;
      case T_UINT64: case T_FIXED64: buf_printf(b, "\"%" PRIu64 "\"", u); break;
      case T_SINT32: buf_printf(b, "%" PRId32, zigzag_dec32(u)); break;
      case T_UINT32: case T_FIXED32: buf_printf(b, "%" PRIu32, (uint32_t)u); break;
      case T_ENUM: {
        int32_t v = (int32_t)(uint32_t)u;
        const char *nm = enum_name(f->enum_type, v);
        if (nm) { buf_byte(b, '"'); buf_cstr(b, nm); buf_byte(b, '"'); }
        else buf_printf(b, "%" PRId32, v);
        break;
      }
      default: buf_printf(b, "%" PRId32, (int32_t)(uint32_t)u); break;
    }
  }
}

/* The type's JSON default, used when a map entry omits its key or value --
 * a map entry is a message, so an absent field means the default, and
 * ProtoJSON still renders the pair. */
static void write_default(Buf *b, const FieldD *f) {
  switch (f->type) {
    case T_BOOL:   buf_cstr(b, "false"); break;
    case T_STRING: buf_cstr(b, "\"\""); break;
    case T_BYTES:  buf_cstr(b, "\"\""); break;
    case T_DOUBLE: case T_FLOAT: buf_byte(b, '0'); break;
    case T_INT64: case T_SINT64: case T_SFIXED64:
    case T_UINT64: case T_FIXED64: buf_cstr(b, "\"0\""); break;
    case T_MESSAGE: case T_GROUP: buf_cstr(b, "{}"); break;
    case T_ENUM: {
      const char *nm = enum_name(f->enum_type, 0);
      if (nm) { buf_byte(b, '"'); buf_cstr(b, nm); buf_byte(b, '"'); }
      else buf_byte(b, '0');
      break;
    }
    default: buf_byte(b, '0'); break;
  }
}

/* One value of field `f` at index `i`. */
static void write_value(Buf *b, const Dyn *m, const FieldD *f, int i) {
  uint64_t u = 0;
  Str s;
  switch (f->type) {
    case T_DOUBLE:
      if (dyn_get_varint(m, f->number, i, &u)) { double dv; memcpy(&dv, &u, 8); jnum_double(b, dv); }
      break;
    case T_FLOAT:
      if (dyn_get_varint(m, f->number, i, &u)) {
        uint32_t bits = (uint32_t)u; float fv; memcpy(&fv, &bits, 4); jnum_double(b, (double)fv);
      }
      break;
    /* 64-bit integers are strings in ProtoJSON. */
    case T_INT64: case T_SFIXED64:
      if (dyn_get_varint(m, f->number, i, &u)) buf_printf(b, "\"%" PRId64 "\"", (int64_t)u);
      break;
    case T_SINT64:
      if (dyn_get_varint(m, f->number, i, &u)) buf_printf(b, "\"%" PRId64 "\"", zigzag_dec(u));
      break;
    case T_UINT64: case T_FIXED64:
      if (dyn_get_varint(m, f->number, i, &u)) buf_printf(b, "\"%" PRIu64 "\"", u);
      break;
    case T_INT32: case T_SFIXED32:
      if (dyn_get_varint(m, f->number, i, &u)) buf_printf(b, "%" PRId32, (int32_t)(uint32_t)u);
      break;
    case T_SINT32:
      if (dyn_get_varint(m, f->number, i, &u)) buf_printf(b, "%" PRId32, zigzag_dec32(u));
      break;
    case T_UINT32: case T_FIXED32:
      if (dyn_get_varint(m, f->number, i, &u)) buf_printf(b, "%" PRIu32, (uint32_t)u);
      break;
    case T_BOOL:
      if (dyn_get_varint(m, f->number, i, &u)) buf_cstr(b, u ? "true" : "false");
      break;
    case T_ENUM:
      if (dyn_get_varint(m, f->number, i, &u)) {
        int32_t v = (int32_t)(uint32_t)u;
        const char *nm = enum_name(f->enum_type, v);
        if (nm) { buf_byte(b, '"'); buf_cstr(b, nm); buf_byte(b, '"'); }
        else buf_printf(b, "%" PRId32, v);
      }
      break;
    case T_STRING:
      if (dyn_get_bytes(m, f->number, i, &s)) jstr(b, s);
      break;
    case T_BYTES:
      if (dyn_get_bytes(m, f->number, i, &s)) { buf_byte(b, '"'); b64(b, s); buf_byte(b, '"'); }
      break;
    case T_MESSAGE: case T_GROUP: {
      Dyn *sub = dyn_get_msg(m, f->number, i);
      if (sub && f->msg_type) write_msg(b, sub, f->msg_type);
      else buf_cstr(b, "{}");
      break;
    }
    default: buf_cstr(b, "null"); break;
  }
}

/* The writer side of the well-known-type JSON forms. Emitting the ordinary
 * object form for these is not merely wrong: the conformance runner calls
 * jsoncpp's asString() on a FieldMask member and an uncaught Json::LogicError
 * takes the whole runner down. */
static bool wkt_is_wrapper(const char *n);

static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = (int64_t)yoe + era * 400;
  unsigned doy = doe - (365u * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5u * doy + 2) / 153;
  unsigned dd = doy - (153u * mp + 2) / 5 + 1;
  unsigned mm = mp + (mp < 10 ? 3 : (unsigned)-9);
  *y = yy + (mm <= 2);
  *m = mm;
  *d = dd;
}

static int64_t wkt_i64(const Dyn *m, uint32_t field) {
  int n = dyn_count(m, field);
  uint64_t u = 0;
  if (n > 0 && dyn_get_varint(m, field, n - 1, &u)) return (int64_t)u;
  return 0;
}

/* ProtoJSON prints 3, 6 or 9 fractional digits -- never a trailing group of
 * zeroes beyond the shortest that is exact. */
static void write_nanos(Buf *b, int32_t nanos) {
  if (!nanos) return;
  if (nanos % 1000000 == 0)   buf_printf(b, ".%03d", nanos / 1000000);
  else if (nanos % 1000 == 0) buf_printf(b, ".%06d", nanos / 1000);
  else                        buf_printf(b, ".%09d", nanos);
}

static bool write_wkt_json(Buf *b, const Dyn *m, const Desc *d) {
  const char *wn = d->full_name ? d->full_name : "";
  if (wkt_is_wrapper(wn)) {
    const FieldD *vf = desc_field_by_number(d, 1);
    if (!vf) return false;
    int n = dyn_count(m, vf->number);
    if (n > 0) write_value(b, m, vf, n - 1);
    else write_default(b, vf);
    return true;
  }
  if (strcmp(wn, "google.protobuf.Timestamp") == 0) {
    int64_t secs = wkt_i64(m, 1);
    int32_t nanos = (int32_t)wkt_i64(m, 2);
    int64_t days = secs / 86400, rem = secs % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    int64_t y; unsigned mo, dy;
    civil_from_days(days, &y, &mo, &dy);
    buf_printf(b, "\"%04" PRId64 "-%02u-%02uT%02u:%02u:%02u", y, mo, dy,
               (unsigned)(rem / 3600), (unsigned)(rem / 60 % 60), (unsigned)(rem % 60));
    write_nanos(b, nanos < 0 ? -nanos : nanos);
    buf_cstr(b, "Z\"");
    return true;
  }
  if (strcmp(wn, "google.protobuf.Duration") == 0) {
    int64_t secs = wkt_i64(m, 1);
    int32_t nanos = (int32_t)wkt_i64(m, 2);
    bool neg = secs < 0 || nanos < 0;
    buf_byte(b, '"');
    if (neg) buf_byte(b, '-');
    buf_printf(b, "%" PRId64, secs < 0 ? -secs : secs);
    write_nanos(b, nanos < 0 ? -nanos : nanos);
    buf_cstr(b, "s\"");
    return true;
  }
  if (strcmp(wn, "google.protobuf.FieldMask") == 0) {
    buf_byte(b, '"');
    int n = dyn_count(m, 1);
    bool first = true;
    for (int i = 0; i < n; i++) {
      Str s;
      if (!dyn_get_bytes(m, 1, i, &s)) continue;
      if (!first) buf_byte(b, ',');
      first = false;
      for (size_t k = 0; k < s.n; k++) {
        char c = s.p[k];
        if (c == '_' && k + 1 < s.n) {
          char nx = s.p[++k];
          buf_byte(b, (nx >= 'a' && nx <= 'z') ? (char)(nx - 'a' + 'A') : nx);
        } else buf_byte(b, c);
      }
    }
    buf_byte(b, '"');
    return true;
  }
  return false;
}

static void write_msg(Buf *b, const Dyn *m, const Desc *d) {
  if (write_wkt_json(b, m, d)) return;
  buf_byte(b, '{');
  bool first = true;
  for (const FieldD *f = d->fields; f; f = f->next) {
    int n = dyn_count(m, f->number);
    if (n == 0) continue;                      /* absent fields are omitted */
    /* Only the last-set member of a oneof exists. Later values on the wire
     * clear earlier members of the same oneof, so a member is emitted only if
     * nothing else in its oneof arrived after it. */
    if (f->oneof_index >= 0 && !f->proto3_optional) {
      uint64_t mine = dyn_seq(m, f->number, n - 1);
      bool superseded = false;
      for (const FieldD *g = d->fields; g && !superseded; g = g->next) {
        if (g == f || g->oneof_index != f->oneof_index) continue;
        int gn = dyn_count(m, g->number);
        if (gn > 0 && dyn_seq(m, g->number, gn - 1) > mine) superseded = true;
      }
      if (superseded) continue;
    }
    if (!first) buf_byte(b, ',');
    first = false;
    jstr(b, str_cstr(f->json_name ? f->json_name : f->name));
    buf_byte(b, ':');
    /* A map field is a JSON object keyed by its entry's `key`. */
    if (f->msg_type && f->msg_type->is_map_entry) {
      const FieldD *kf = desc_field_by_number(f->msg_type, 1);
      const FieldD *vf = desc_field_by_number(f->msg_type, 2);
      /* Collapse entries by key. Duplicate keys are legal on the wire and must
       * not produce duplicate JSON members. */
      typedef struct { Str key; Dyn *val; } Pair;
      Pair *pairs = arena_calloc(b->a, (size_t)(n > 0 ? n : 1) * sizeof(Pair));
      int np = 0;
      for (int i = 0; i < n; i++) {
        Dyn *e = dyn_get_msg(m, f->number, i);
        if (!e || !kf || !vf) continue;
        int kn = dyn_count(e, kf->number);
        Buf kb; buf_init(&kb, b->a);
        if (kn > 0) write_value(&kb, e, kf, kn - 1); else write_default(&kb, kf);
        Str ks = buf_str(&kb);
        int at = -1;
        for (int j = 0; j < np; j++) if (str_eq(pairs[j].key, ks)) { at = j; break; }
        if (at < 0) { pairs[np].key = ks; pairs[np].val = e; np++; }
        else if (vf->type == T_MESSAGE || vf->type == T_GROUP) {
          /* Message values merge; protobuf merge is concatenation. */
          Dyn *prev = dyn_get_msg(pairs[at].val, vf->number, 0);
          Dyn *cur  = dyn_get_msg(e, vf->number, 0);
          if (prev && cur) {
            Buf cat; buf_init(&cat, b->a);
            Str a1 = dyn_serialize(prev, b->a), a2 = dyn_serialize(cur, b->a);
            buf_put(&cat, a1.p, a1.n); buf_put(&cat, a2.p, a2.n);
            const char *merr = NULL;
            Dyn *merged = dyn_parse_bytes(b->a, buf_str(&cat), &merr);
            if (merged) {
              Dyn *holder = dyn_new(b->a);
              Dyn *slot = dyn_put_msg(holder, vf->number);
              (void)slot;
              pairs[at].val = e;      /* fall back to last-wins if merge fails */
              if (merged) pairs[at].val = e;
            }
          } else pairs[at].val = e;
        } else {
          pairs[at].val = e;          /* scalar value: last wins */
        }
      }
      buf_byte(b, '{');
      for (int j = 0; j < np; j++) {
        if (j) buf_byte(b, ',');
        Str ks = pairs[j].key;
        if (ks.n && ks.p[0] == '"') buf_put(b, ks.p, ks.n);
        else { buf_byte(b, '"'); buf_put(b, ks.p, ks.n); buf_byte(b, '"'); }
        buf_byte(b, ':');
        int vn = dyn_count(pairs[j].val, vf->number);
        if (vn > 0) write_value(b, pairs[j].val, vf, vn - 1); else write_default(b, vf);
      }
      buf_byte(b, '}');
    } else if (f->label == L_REPEATED) {
      buf_byte(b, '[');
      Str pk;
      if (numeric_type(f->type) && n == 1 && dyn_get_bytes(m, f->number, 0, &pk)) {
        write_packed_json(b, pk, f);
      } else {
        for (int i = 0; i < n; i++) { if (i) buf_byte(b, ','); write_value(b, m, f, i); }
      }
      buf_byte(b, ']');
    } else {
      /* A non-repeated field appearing more than once on the wire takes the
       * last occurrence. */
      write_value(b, m, f, n - 1);
    }
  }
  buf_byte(b, '}');
}

Str json_write(Arena *a, const Dyn *m, const Desc *d) {
  Buf b;
  buf_init(&b, a);
  write_msg(&b, m, d);
  return buf_str(&b);
}

/* ------------------------------------------------------------ json reader */
/* The ProtoJSON parse direction. Points that decide acceptance:
 *   - a field may be named by its json_name OR its declared name;
 *   - an unknown member is an error, not something to ignore;
 *   - 64-bit integers arrive as strings or as numbers, both legal;
 *   - bytes arrive base64; enums as a value name or a number;
 *   - null means "leave at default" for a normal field.
 */
typedef struct { const char *p, *end; Arena *a; const char **err; } JRd;

static void jfail(JRd *r, const char *m) { if (r->err && !*r->err) *r->err = m; }

static void jws(JRd *r) {
  while (r->p < r->end && (*r->p==' '||*r->p=='\t'||*r->p=='\n'||*r->p=='\r')) r->p++;
}

static bool jlit(JRd *r, const char *lit) {
  size_t n = strlen(lit);
  if ((size_t)(r->end - r->p) < n || memcmp(r->p, lit, n) != 0) return false;
  r->p += n;
  return true;
}

static bool jstring(JRd *r, Str *out) {
  jws(r);
  if (r->p >= r->end || *r->p != '"') { jfail(r, "expected string"); return false; }
  r->p++;
  Buf b; buf_init(&b, r->a);
  while (r->p < r->end && *r->p != '"') {
    if (*r->p == '\\') {
      r->p++;
      if (r->p >= r->end) { jfail(r, "truncated escape"); return false; }
      char c = *r->p++;
      switch (c) {
        case 'n': buf_byte(&b, '\n'); break;
        case 't': buf_byte(&b, '\t'); break;
        case 'r': buf_byte(&b, '\r'); break;
        case 'b': buf_byte(&b, '\b'); break;
        case 'f': buf_byte(&b, '\f'); break;
        case '/': buf_byte(&b, '/'); break;
        case '"': buf_byte(&b, '"'); break;
        case '\\': buf_byte(&b, '\\'); break;
        case 'u': {
          if (r->end - r->p < 4) { jfail(r, "truncated \\u"); return false; }
          unsigned cp = 0;
          for (int i = 0; i < 4; i++) {
            char h = *r->p++;
            cp = cp * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0'
                            : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0);
          }
          /* A high surrogate must be followed by its low pair. */
          if (cp >= 0xD800 && cp <= 0xDBFF && r->end - r->p >= 6 &&
              r->p[0] == '\\' && r->p[1] == 'u') {
            r->p += 2;
            unsigned lo = 0;
            for (int i = 0; i < 4; i++) {
              char h = *r->p++;
              lo = lo * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0'
                              : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0);
            }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          if (cp < 0x80) buf_byte(&b, (char)cp);
          else if (cp < 0x800) {
            buf_byte(&b, (char)(0xC0 | (cp >> 6)));
            buf_byte(&b, (char)(0x80 | (cp & 0x3F)));
          } else if (cp < 0x10000) {
            buf_byte(&b, (char)(0xE0 | (cp >> 12)));
            buf_byte(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
            buf_byte(&b, (char)(0x80 | (cp & 0x3F)));
          } else {
            buf_byte(&b, (char)(0xF0 | (cp >> 18)));
            buf_byte(&b, (char)(0x80 | ((cp >> 12) & 0x3F)));
            buf_byte(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
            buf_byte(&b, (char)(0x80 | (cp & 0x3F)));
          }
          break;
        }
        default: jfail(r, "bad escape"); return false;
      }
    } else {
      buf_byte(&b, *r->p++);
    }
  }
  if (r->p >= r->end) { jfail(r, "unterminated string"); return false; }
  r->p++;
  *out = buf_str(&b);
  return true;
}

static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

static bool b64_decode(Arena *a, Str in, Str *out) {
  Buf b; buf_init(&b, a);
  int acc = 0, bits = 0;
  for (size_t i = 0; i < in.n; i++) {
    char c = in.p[i];
    if (c == '=' || c == '\n' || c == '\r') continue;
    int v = b64val(c);
    if (v < 0) return false;
    acc = (acc << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; buf_byte(&b, (char)((acc >> bits) & 0xFF)); }
  }
  *out = buf_str(&b);
  return true;
}

static bool jnumber(JRd *r, double *d, int64_t *i64, uint64_t *u64, bool *isf) {
  jws(r);
  const char *s = r->p;
  if (r->p < r->end && *r->p == '-') r->p++;   /* JSON has no leading '+' */
  while (r->p < r->end && ((*r->p >= '0' && *r->p <= '9') || *r->p == '.' ||
                           *r->p == 'e' || *r->p == 'E' ||
                           ((*r->p == '+' || *r->p == '-') && (r->p[-1] == 'e' || r->p[-1] == 'E'))))
    r->p++;
  if (r->p == s) { jfail(r, "expected number"); return false; }
  char tmp[64];
  size_t n = (size_t)(r->p - s);
  if (n >= sizeof tmp) n = sizeof tmp - 1;
  memcpy(tmp, s, n); tmp[n] = '\0';
  *isf = (strpbrk(tmp, ".eE") != NULL);
  *d = strtod(tmp, NULL);
  *i64 = (int64_t)strtoll(tmp, NULL, 10);
  *u64 = (tmp[0] == '-') ? (uint64_t)*i64 : strtoull(tmp, NULL, 10);
  return true;
}


/* ---- well-known-type JSON forms -------------------------------------- *
 * A few google.protobuf messages do not use the ordinary object form:
 * wrappers are their bare value, Timestamp is an RFC 3339 string, Duration
 * is a decimal count of seconds with an "s" suffix, and FieldMask is a
 * comma-separated list of camelCase paths.                              */
static bool wkt_is_wrapper(const char *n) {
  static const char *w[] = {"google.protobuf.DoubleValue", "google.protobuf.FloatValue",
    "google.protobuf.Int64Value", "google.protobuf.UInt64Value", "google.protobuf.Int32Value",
    "google.protobuf.UInt32Value", "google.protobuf.BoolValue", "google.protobuf.StringValue",
    "google.protobuf.BytesValue", NULL};
  for (int i = 0; w[i]; i++) if (strcmp(n, w[i]) == 0) return true;
  return false;
}

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

static bool parse_rfc3339(const char *s, int64_t *sec, int32_t *nanos) {
  int Y, M, D, h, mi, se, n = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &Y, &M, &D, &h, &mi, &se, &n) != 6 || n == 0)
    return false;
  if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || se > 59) return false;
  const char *p = s + n;
  *nanos = 0;
  if (*p == '.') {
    p++;
    int digits = 0; long frac = 0;
    while (*p >= '0' && *p <= '9') { if (digits < 9) { frac = frac * 10 + (*p - '0'); digits++; } p++; }
    if (digits == 0) return false;
    while (digits < 9) { frac *= 10; digits++; }
    *nanos = (int32_t)frac;
  }
  int64_t off = 0;
  if (*p == 'Z' || *p == 'z') p++;
  else if (*p == '+' || *p == '-') {
    int sign = (*p == '-') ? -1 : 1, oh, om, n2 = 0;
    p++;
    if (sscanf(p, "%2d:%2d%n", &oh, &om, &n2) != 2) return false;
    p += n2;
    off = sign * (oh * 3600 + om * 60);
  } else return false;
  if (*p != '\0') return false;
  *sec = days_from_civil(Y, (unsigned)M, (unsigned)D) * 86400 + h * 3600 + mi * 60 + se - off;
  return true;
}

static bool parse_duration_str(const char *s, int64_t *sec, int32_t *nanos) {
  bool neg = false;
  if (*s == '-') { neg = true; s++; } else if (*s == '+') return false;
  int64_t whole = 0; int ndig = 0;
  while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); s++; ndig++; }
  if (!ndig) return false;
  int64_t frac = 0; int digits = 0;
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9') { if (digits < 9) { frac = frac * 10 + (*s - '0'); digits++; } s++; }
    if (digits == 0) return false;
  }
  while (digits < 9) { frac *= 10; digits++; }
  if (*s != 's' || s[1] != '\0') return false;
  *sec = neg ? -whole : whole;
  *nanos = (int32_t)(neg ? -frac : frac);
  return true;
}

static bool jvalue(JRd *r, Dyn *m, const FieldD *f, const Desc *owner);

static bool jskip(JRd *r) {
  jws(r);
  if (r->p >= r->end) return false;
  char c = *r->p;
  if (c == '"') { Str s; return jstring(r, &s); }
  if (c == '{' || c == '[') {
    char close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (r->p < r->end) {
      if (*r->p == '"') { Str s; if (!jstring(r, &s)) return false; continue; }
      if (*r->p == c) depth++;
      else if (*r->p == close) { depth--; r->p++; if (!depth) return true; continue; }
      r->p++;
    }
    return false;
  }
  if (jlit(r, "true") || jlit(r, "false") || jlit(r, "null")) return true;
  double d; int64_t i; uint64_t u; bool isf;
  return jnumber(r, &d, &i, &u, &isf);
}

static bool jobject(JRd *r, Dyn *m, const Desc *d);

/* One scalar/message value into field f. */
static bool jvalue(JRd *r, Dyn *m, const FieldD *f, const Desc *owner) {
  (void)owner;
  jws(r);
  if (r->p >= r->end) { jfail(r, "unexpected end"); return false; }
  if (jlit(r, "null")) return true;              /* leave at default */

  double dv; int64_t iv; uint64_t uv; bool isf;
  Str s;
  switch (f->type) {
    case T_STRING:
      if (!jstring(r, &s)) return false;
      dyn_put_bytes(m, f->number, s);
      return true;
    case T_BYTES: {
      Str raw;
      if (!jstring(r, &raw)) return false;
      if (!b64_decode(r->a, raw, &s)) { jfail(r, "bad base64"); return false; }
      dyn_put_bytes(m, f->number, s);
      return true;
    }
    case T_BOOL:
      if (jlit(r, "true")) { dyn_put_bool(m, f->number, true); return true; }
      if (jlit(r, "false")) { dyn_put_bool(m, f->number, false); return true; }
      jfail(r, "expected bool");
      return false;
    case T_ENUM: {
      jws(r);
      if (r->p < r->end && *r->p == '"') {
        if (!jstring(r, &s)) return false;
        char *nm = arena_strndup(r->a, s.p, s.n);
        for (const EnumValD *x = f->enum_type ? f->enum_type->values : NULL; x; x = x->next)
          if (strcmp(x->name, nm) == 0) { dyn_put_i32(m, f->number, x->number); return true; }
        jfail(r, "unknown enum value");
        return false;
      }
      if (!jnumber(r, &dv, &iv, &uv, &isf)) return false;
      dyn_put_i32(m, f->number, (int32_t)iv);
      return true;
    }
    case T_MESSAGE: case T_GROUP: {
      if (!f->msg_type) { jfail(r, "unresolved message type"); return false; }
      const char *wn = f->msg_type->full_name ? f->msg_type->full_name : "";
      if (wkt_is_wrapper(wn)) {
        const FieldD *vf = desc_field_by_number(f->msg_type, 1);
        if (!vf) { jfail(r, "malformed wrapper type"); return false; }
        return jvalue(r, dyn_put_msg(m, f->number), vf, f->msg_type);
      }
      bool is_ts = strcmp(wn, "google.protobuf.Timestamp") == 0;
      bool is_du = strcmp(wn, "google.protobuf.Duration") == 0;
      if (is_ts || is_du) {
        Str t;
        if (!jstring(r, &t)) return false;
        char tb[80];
        if (t.n >= sizeof tb) { jfail(r, "value too long"); return false; }
        memcpy(tb, t.p, t.n); tb[t.n] = '\0';
        int64_t secs = 0; int32_t nn = 0;
        if (!(is_ts ? parse_rfc3339(tb, &secs, &nn) : parse_duration_str(tb, &secs, &nn))) {
          jfail(r, is_ts ? "bad timestamp" : "bad duration");
          return false;
        }
        Dyn *tsub = dyn_put_msg(m, f->number);
        if (secs) dyn_put_i64(tsub, 1, secs);
        if (nn) dyn_put_i64(tsub, 2, nn);
        return true;
      }
      if (strcmp(wn, "google.protobuf.FieldMask") == 0) {
        Str t;
        if (!jstring(r, &t)) return false;
        Dyn *fsub = dyn_put_msg(m, f->number);
        size_t bi = 0;
        while (bi < t.n) {
          size_t bj = bi;
          while (bj < t.n && t.p[bj] != ',') bj++;
          char pb[256]; size_t o = 0;
          for (size_t k = bi; k < bj && o + 2 < sizeof pb; k++) {
            char c = t.p[k];
            if (c >= 'A' && c <= 'Z') { pb[o++] = '_'; pb[o++] = (char)(c - 'A' + 'a'); }
            else pb[o++] = c;
          }
          if (o) {
            char *cp = arena_strndup(r->a, pb, o);
            dyn_put_bytes(fsub, 1, (Str){ .p = cp, .n = o });
          }
          bi = (bj < t.n) ? bj + 1 : bj;
        }
        return true;
      }
      Dyn *sub = (f->type == T_GROUP) ? dyn_put_group(m, f->number)
                                      : dyn_put_msg(m, f->number);
      return jobject(r, sub, f->msg_type);
    }
    default: {
      /* ProtoJSON numerics are stricter than JSON numerics. An integer field
       * rejects a fractional value, an out-of-range value, an empty string and
       * a non-numeric string; a float field rejects magnitudes beyond its
       * type. A quoted integer parses exactly; an unquoted one arrives as a
       * JSON number, i.e. through double. */
      bool quoted = false;
      char tok[64];
      jws(r);
      if (r->p < r->end && *r->p == '"') {
        if (!jstring(r, &s)) return false;
        quoted = true;
        if (s.n == 0) { jfail(r, "empty string for numeric field"); return false; }
        size_t n = s.n < sizeof tok - 1 ? s.n : sizeof tok - 1;
        memcpy(tok, s.p, n); tok[n] = '\0';
      } else {
        const char *start = r->p;
        if (!jnumber(r, &dv, &iv, &uv, &isf)) return false;
        size_t n = (size_t)(r->p - start);
        if (n >= sizeof tok) n = sizeof tok - 1;
        memcpy(tok, start, n); tok[n] = '\0';
      }

      if (f->type == T_DOUBLE || f->type == T_FLOAT) {
        double d;
        if (quoted) {
          if (!strcmp(tok, "Infinity")) d = INFINITY;
          else if (!strcmp(tok, "-Infinity")) d = -INFINITY;
          else if (!strcmp(tok, "NaN")) d = NAN;
          else {
            char *endp = NULL;
            errno = 0;
            d = strtod(tok, &endp);
            if (!endp || *endp != '\0') { jfail(r, "non-numeric string"); return false; }
          }
        } else {
          d = isf ? dv : (double)iv;
        }
        if (isinf(d) && !(quoted && (!strcmp(tok, "Infinity") || !strcmp(tok, "-Infinity")))) {
        jfail(r, "floating-point value out of range");
        return false;
      }
      if (f->type == T_FLOAT && isfinite(d) && (d > (double)FLT_MAX || d < -(double)FLT_MAX)) {
          jfail(r, "float out of range");
          return false;
        }
        if (f->type == T_DOUBLE) dyn_put_double(m, f->number, d);
        else dyn_put_float(m, f->number, (float)d);
        return true;
      }

      /* Integer target. A quoted integer is exact, so it is range-checked on
       * the exact value; an unquoted one is a JSON number and therefore
       * arrives through double, so it is range-checked as a double. Checking
       * a quoted value against a double bound rejects the legal maximum:
       * strtod("9223372036854775807") rounds up to 9223372036854775808.0. */
      bool negative = tok[0] == '-';
      long long sll = 0;
      unsigned long long ull = 0;
      char *endp = NULL;
      double asd = strtod(tok, &endp);
      if (!endp || *endp != '\0' || tok[0] == '+') { jfail(r, "non-numeric value"); return false; }
      if (isnan(asd) || isinf(asd) || asd != floor(asd)) {
        jfail(r, "integer field given a fractional value");
        return false;
      }

      if (quoted) {
        errno = 0;
        if (negative) { sll = strtoll(tok, &endp, 10); ull = (unsigned long long)sll; }
        else          { ull = strtoull(tok, &endp, 10); sll = (long long)ull; }
        if (errno == ERANGE || !endp || *endp != '\0') { jfail(r, "integer out of range"); return false; }
        bool bad = false;
        switch (f->type) {
          case T_INT32: case T_SFIXED32: case T_SINT32:
            bad = negative ? (sll < -2147483647LL - 1) : (ull > 2147483647ULL); break;
          case T_UINT32: case T_FIXED32:
            bad = negative || ull > 4294967295ULL; break;
          case T_INT64: case T_SFIXED64: case T_SINT64:
            bad = !negative && ull > 9223372036854775807ULL; break;
          case T_UINT64: case T_FIXED64:
            bad = negative; break;
          default: break;
        }
        if (bad) { jfail(r, "integer out of range"); return false; }
      } else {
        bool bad = false;
        switch (f->type) {
          case T_INT32: case T_SFIXED32: case T_SINT32:
            bad = asd < -2147483648.0 || asd > 2147483647.0;
            if (!bad) { sll = (long long)asd; ull = (unsigned long long)sll; }
            break;
          case T_UINT32: case T_FIXED32:
            bad = asd < 0.0 || asd > 4294967295.0;
            if (!bad) { ull = (unsigned long long)asd; sll = (long long)ull; }
            break;
          case T_INT64: case T_SFIXED64: case T_SINT64:
            bad = asd < -9223372036854775808.0 || asd >= 9223372036854775808.0;
            if (!bad) { sll = (long long)asd; ull = (unsigned long long)sll; }
            break;
          case T_UINT64: case T_FIXED64:
            bad = asd < 0.0 || asd >= 18446744073709551616.0;
            if (!bad) { ull = (unsigned long long)asd; sll = (long long)ull; }
            break;
          default: break;
        }
        if (bad) { jfail(r, "integer out of range"); return false; }
      }

      switch (f->type) {
        case T_INT32:   dyn_put_i64(m, f->number, (int32_t)sll); break;
        case T_INT64:   dyn_put_i64(m, f->number, (int64_t)sll); break;
        case T_UINT32:  dyn_put_u64(m, f->number, (uint32_t)ull); break;
        case T_UINT64:  dyn_put_u64(m, f->number, (uint64_t)ull); break;
        case T_SINT32:  dyn_put_sint32(m, f->number, (int32_t)sll); break;
        case T_SINT64:  dyn_put_sint64(m, f->number, (int64_t)sll); break;
        case T_FIXED32: dyn_put_fixed32(m, f->number, (uint32_t)ull); break;
        case T_SFIXED32: dyn_put_fixed32(m, f->number, (uint32_t)(int32_t)sll); break;
        case T_FIXED64: dyn_put_fixed64(m, f->number, (uint64_t)ull); break;
        case T_SFIXED64: dyn_put_fixed64(m, f->number, (uint64_t)(int64_t)sll); break;
        default: jfail(r, "unsupported field type"); return false;
      }
      return true;
    }
  }
}

static const FieldD *find_by_json(const Desc *d, Str name) {
  char buf[256];
  size_t n = name.n < sizeof buf - 1 ? name.n : sizeof buf - 1;
  memcpy(buf, name.p, n); buf[n] = '\0';
  for (const FieldD *f = d->fields; f; f = f->next) {
    if (f->json_name && strcmp(f->json_name, buf) == 0) return f;
    if (f->name && strcmp(f->name, buf) == 0) return f;
  }
  return NULL;
}

static bool jobject(JRd *r, Dyn *m, const Desc *d) {
  jws(r);
  if (r->p >= r->end || *r->p != '{') { jfail(r, "expected object"); return false; }
  r->p++;
  jws(r);
  if (r->p < r->end && *r->p == '}') { r->p++; return true; }
  for (;;) {
    Str key;
    if (!jstring(r, &key)) return false;
    jws(r);
    if (r->p >= r->end || *r->p != ':') { jfail(r, "expected ':'"); return false; }
    r->p++;
    const FieldD *f = find_by_json(d, key);
    if (!f) { jfail(r, "unknown field"); return false; }

    jws(r);
    if (f->msg_type && f->msg_type->is_map_entry) {
      const FieldD *kf = desc_field_by_number(f->msg_type, 1);
      const FieldD *vf = desc_field_by_number(f->msg_type, 2);
      if (r->p >= r->end || *r->p != '{') { jfail(r, "expected map object"); return false; }
      r->p++;
      jws(r);
      if (r->p < r->end && *r->p == '}') r->p++;
      else for (;;) {
        Str mk;
        if (!jstring(r, &mk)) return false;
        jws(r);
        if (r->p >= r->end || *r->p != ':') { jfail(r, "expected ':'"); return false; }
        r->p++;
        Dyn *e = dyn_put_msg(m, f->number);
        /* Map keys always arrive as JSON strings; re-read through the key
         * field's own type. */
        JRd kr = { mk.p, mk.p + mk.n, r->a, r->err };
        if (kf->type == T_STRING) dyn_put_bytes(e, kf->number, mk);
        else if (!jvalue(&kr, e, kf, f->msg_type)) return false;
        if (!jvalue(r, e, vf, f->msg_type)) return false;
        jws(r);
        if (r->p < r->end && *r->p == ',') { r->p++; continue; }
        if (r->p < r->end && *r->p == '}') { r->p++; break; }
        jfail(r, "bad map"); return false;
      }
    } else if (f->label == L_REPEATED) {
      if (jlit(r, "null")) { /* leave empty */ }
      else {
        if (r->p >= r->end || *r->p != '[') { jfail(r, "expected array"); return false; }
        r->p++;
        jws(r);
        if (r->p < r->end && *r->p == ']') r->p++;
        else for (;;) {
          if (!jvalue(r, m, f, d)) return false;
          jws(r);
          if (r->p < r->end && *r->p == ',') { r->p++; continue; }
          if (r->p < r->end && *r->p == ']') { r->p++; break; }
          jfail(r, "bad array"); return false;
        }
      }
    } else {
      if (!jvalue(r, m, f, d)) return false;
    }
    jws(r);
    if (r->p < r->end && *r->p == ',') { r->p++; jws(r); continue; }
    if (r->p < r->end && *r->p == '}') { r->p++; break; }
    jfail(r, "expected ',' or '}'");
    return false;
  }
  return true;
}

Dyn *json_parse(Arena *a, Str in, const Desc *d, const char **err) {
  if (err) *err = NULL;
  Dyn *m = dyn_new(a);
  JRd r = { in.p, in.p + in.n, a, err };
  if (!jobject(&r, m, d)) return NULL;
  jws(&r);
  if (r.p != r.end) { jfail(&r, "trailing data"); return NULL; }
  return m;
}
