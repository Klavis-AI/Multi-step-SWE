/* textfmt.c -- protobuf text format.
 *
 * The output shape was measured against protoc 32.1 rather than taken from the
 * spec, because --decode output is compared byte for byte:
 *
 *     a: 1
 *     b: "xy"
 *     packed_nums: 5
 *     packed_nums: 6
 *     Grp {
 *       gi: 9
 *     }
 *
 * Points that cost bytes when wrong: two-space indent; a submessage opens with
 * `name {` and closes on its own line; a GROUP prints the *message* name
 * (`Grp`), not the lower-cased field name; every element of a repeated field
 * gets its own line; enums print the value's name.
 */
#include "desc.h"
#include "textfmt.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

static void indent(Buf *b, int depth) {
  for (int i = 0; i < depth; i++) buf_cstr(b, "  ");
}

/* Text-format string escaping, as protoc emits it: \n \r \t \\ \" and
 * three-digit octal for anything non-printable. */
void tf_quote(Buf *b, Str s) {
  buf_byte(b, '"');
  for (size_t i = 0; i < s.n; i++) {
    unsigned char c = (unsigned char)s.p[i];
    switch (c) {
      case '\n': buf_cstr(b, "\\n"); break;
      case '\r': buf_cstr(b, "\\r"); break;
      case '\t': buf_cstr(b, "\\t"); break;
      case '"':  buf_cstr(b, "\\\""); break;
      case '\'': buf_cstr(b, "\\'"); break;
      case '\\': buf_cstr(b, "\\\\"); break;
      default:
        if (c < 32 || c >= 127) buf_printf(b, "\\%03o", c);
        else buf_byte(b, (char)c);
    }
  }
  buf_byte(b, '"');
}

static void put_double(Buf *b, double d) {
  if (isinf(d)) { buf_cstr(b, d < 0 ? "-inf" : "inf"); return; }
  if (isnan(d)) { buf_cstr(b, "nan"); return; }
  char tmp[40];
  /* Shortest representation that round-trips, which is what protoc prints. */
  for (int prec = 1; prec <= 17; prec++) {
    snprintf(tmp, sizeof tmp, "%.*g", prec, d);
    double back = 0;
    if (sscanf(tmp, "%lf", &back) == 1 && back == d) break;
  }
  buf_cstr(b, tmp);
}

static const char *enum_name_for(const EnumD *e, int64_t v) {
  if (!e) return NULL;
  for (const EnumValD *x = e->values; x; x = x->next)
    if (x->number == v) return x->name;
  return NULL;
}

static void write_msg(Buf *b, const Dyn *m, const Desc *d, int depth);

static void write_value(Buf *b, const Dyn *m, const FieldD *f, int idx, int depth) {
  uint64_t u = 0;
  Str s;
  switch (f->type) {
    case T_DOUBLE:
      if (dyn_get_varint(m, f->number, idx, &u)) { double dv; memcpy(&dv, &u, 8); put_double(b, dv); }
      break;
    case T_FLOAT:
      if (dyn_get_varint(m, f->number, idx, &u)) {
        uint32_t bits = (uint32_t)u; float fv; memcpy(&fv, &bits, 4); put_double(b, (double)fv);
      }
      break;
    case T_INT64: case T_SFIXED64:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_printf(b, "%" PRId64, (int64_t)u);
      break;
    case T_SINT64:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_printf(b, "%" PRId64, zigzag_dec(u));
      break;
    case T_UINT64: case T_FIXED64:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_printf(b, "%" PRIu64, u);
      break;
    case T_INT32: case T_SFIXED32:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_printf(b, "%" PRId32, (int32_t)(uint32_t)u);
      break;
    case T_SINT32:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_printf(b, "%" PRId32, zigzag_dec32(u));
      break;
    case T_UINT32: case T_FIXED32:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_printf(b, "%" PRIu32, (uint32_t)u);
      break;
    case T_BOOL:
      if (dyn_get_varint(m, f->number, idx, &u)) buf_cstr(b, u ? "true" : "false");
      break;
    case T_ENUM:
      if (dyn_get_varint(m, f->number, idx, &u)) {
        const char *nm = enum_name_for(f->enum_type, (int64_t)(int32_t)(uint32_t)u);
        if (nm) buf_cstr(b, nm); else buf_printf(b, "%" PRId32, (int32_t)(uint32_t)u);
      }
      break;
    case T_STRING:
      if (dyn_get_bytes(m, f->number, idx, &s)) tf_quote(b, s);
      break;
    case T_BYTES:
      if (dyn_get_bytes(m, f->number, idx, &s)) tf_quote(b, s);
      break;
    case T_MESSAGE: case T_GROUP: {
      Dyn *sub = dyn_get_msg(m, f->number, idx);
      buf_cstr(b, "{\n");
      if (sub && f->msg_type) write_msg(b, sub, f->msg_type, depth + 1);
      indent(b, depth);
      buf_byte(b, '}');
      break;
    }
    default: break;
  }
}

static bool numeric_type(int t) {
  return !(t == T_STRING || t == T_BYTES || t == T_MESSAGE || t == T_GROUP);
}

/* One line per element of a packed repeated field. */
static void write_packed(Buf *b, Str s, const FieldD *f, int depth) {
  WRd r;
  wrd_init(&r, s);
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
    indent(b, depth);
    buf_cstr(b, f->name);
    buf_cstr(b, ": ");
    switch (f->type) {
      case T_DOUBLE: { double dv; memcpy(&dv, &u, 8); put_double(b, dv); break; }
      case T_FLOAT:  { float fv; memcpy(&fv, &u32, 4); put_double(b, (double)fv); break; }
      case T_BOOL:   buf_cstr(b, u ? "true" : "false"); break;
      case T_SINT32: buf_printf(b, "%" PRId32, zigzag_dec32(u)); break;
      case T_SINT64: buf_printf(b, "%" PRId64, zigzag_dec(u)); break;
      case T_UINT32: case T_FIXED32: buf_printf(b, "%" PRIu32, (uint32_t)u); break;
      case T_UINT64: case T_FIXED64: buf_printf(b, "%" PRIu64, u); break;
      case T_INT32: case T_SFIXED32: buf_printf(b, "%" PRId32, (int32_t)(uint32_t)u); break;
      case T_ENUM: {
        const char *nm = enum_name_for(f->enum_type, (int64_t)(int32_t)(uint32_t)u);
        if (nm) buf_cstr(b, nm); else buf_printf(b, "%" PRId32, (int32_t)(uint32_t)u);
        break;
      }
      default: buf_printf(b, "%" PRId64, (int64_t)u); break;
    }
    buf_byte(b, '\n');
  }
}

static void write_msg(Buf *b, const Dyn *m, const Desc *d, int depth) {
  for (const FieldD *f = d->fields; f; f = f->next) {
    /* A packed repeated numeric field arrives as a single bytes value. */
    if (f->label == L_REPEATED && numeric_type(f->type)) {
      Str pk;
      if (dyn_count(m, f->number) == 1 && dyn_get_bytes(m, f->number, 0, &pk)) {
        write_packed(b, pk, f, depth);
        continue;
      }
    }
    int n = dyn_count(m, f->number);
    for (int i = 0; i < n; i++) {
      indent(b, depth);
      /* A group prints its message name; everything else prints the field. */
      if (f->type == T_GROUP && f->msg_type) buf_cstr(b, f->msg_type->name);
      else buf_cstr(b, f->name);
      if (f->type == T_MESSAGE || f->type == T_GROUP) buf_byte(b, ' ');
      else buf_cstr(b, ": ");
      write_value(b, m, f, i, depth);
      buf_byte(b, '\n');
    }
  }
}

Str tf_write(Arena *a, const Dyn *m, const Desc *d) {
  Buf b;
  buf_init(&b, a);
  write_msg(&b, m, d, 0);
  return buf_str(&b);
}

/* ------------------------------------------------------------ decode_raw */
/* protoc reinterprets a length-delimited field as a submessage whenever the
 * bytes parse as one -- the string "xy" comes back as `2 { 15: 121 }`. That
 * ambiguity is protoc's, and matching it is part of the parity. */

static bool parses_as_message(Str s) {
  if (s.n == 0) return false;
  WRd r;
  wrd_init(&r, s);
  while (!wrd_eof(&r)) {
    uint32_t f;
    int wt;
    if (!wrd_tag(&r, &f, &wt)) return false;
    if (wt == WT_EGROUP) return false;
    if (!wrd_skip_field(&r, f, wt, NULL)) return false;
  }
  return true;
}

static void raw_dump(Buf *b, Str in, int depth) {
  WRd r;
  wrd_init(&r, in);
  while (!wrd_eof(&r)) {
    uint32_t field;
    int wt;
    if (!wrd_tag(&r, &field, &wt)) return;
    switch (wt) {
      case WT_VARINT: {
        uint64_t v;
        if (!wrd_varint(&r, &v)) return;
        indent(b, depth);
        buf_printf(b, "%" PRIu32 ": %" PRIu64 "\n", field, v);
        break;
      }
      case WT_I32: {
        uint32_t v;
        if (!wrd_fixed32(&r, &v)) return;
        indent(b, depth);
        buf_printf(b, "%" PRIu32 ": 0x%08" PRIx32 "\n", field, v);
        break;
      }
      case WT_I64: {
        uint64_t v;
        if (!wrd_fixed64(&r, &v)) return;
        indent(b, depth);
        buf_printf(b, "%" PRIu32 ": 0x%016" PRIx64 "\n", field, v);
        break;
      }
      case WT_LEN: {
        Str s;
        if (!wrd_len(&r, &s)) return;
        indent(b, depth);
        if (parses_as_message(s)) {
          buf_printf(b, "%" PRIu32 " {\n", field);
          raw_dump(b, s, depth + 1);
          indent(b, depth);
          buf_cstr(b, "}\n");
        } else {
          buf_printf(b, "%" PRIu32 ": ", field);
          tf_quote(b, s);
          buf_byte(b, '\n');
        }
        break;
      }
      case WT_SGROUP: {
        indent(b, depth);
        buf_printf(b, "%" PRIu32 " {\n", field);
        const uint8_t *start = r.p;
        int d2 = 1;
        while (d2 > 0 && !wrd_eof(&r)) {
          uint32_t f2;
          int w2;
          const uint8_t *before = r.p;
          if (!wrd_tag(&r, &f2, &w2)) return;
          if (w2 == WT_SGROUP) d2++;
          else if (w2 == WT_EGROUP) {
            if (--d2 == 0) {
              raw_dump(b, str_of((const char *)start, (size_t)(before - start)), depth + 1);
              break;
            }
          } else if (!wrd_skip_field(&r, f2, w2, NULL)) return;
        }
        indent(b, depth);
        buf_cstr(b, "}\n");
        break;
      }
      default:
        return;
    }
  }
}

Str tf_decode_raw(Arena *a, Str in) {
  Buf b;
  buf_init(&b, a);
  raw_dump(&b, in, 0);
  return buf_str(&b);
}

/* ------------------------------------------------------------ tf reader */
/* The --encode direction. Text format is far simpler than .proto source, so
 * this is a small hand-rolled scanner rather than a second use of the .proto
 * lexer. It accepts what protoc accepts: `name: value`, `name { ... }`,
 * `name < ... >`, repeated entries one per line or in `[a, b]` lists,
 * `[pkg.Ext]` extension names, bare enum identifiers, and adjacent string
 * literal concatenation. */

typedef struct { const char *p, *end; Diags *d; const char *file; int line; } TfRd;

static void tf_skip_ws(TfRd *r) {
  for (;;) {
    while (r->p < r->end && (*r->p==' '||*r->p=='\t'||*r->p=='\r'||*r->p=='\n')) {
      if (*r->p == '\n') r->line++;
      r->p++;
    }
    if (r->p < r->end && *r->p == '#') {
      while (r->p < r->end && *r->p != '\n') r->p++;
      continue;
    }
    break;
  }
}

static bool tf_ident(TfRd *r, Str *out) {
  tf_skip_ws(r);
  const char *s = r->p;
  while (r->p < r->end && (isalnum((unsigned char)*r->p) || *r->p=='_' || *r->p=='.'))
    r->p++;
  if (r->p == s) return false;
  *out = str_of(s, (size_t)(r->p - s));
  return true;
}

static bool tf_string(TfRd *r, Arena *a, Str *out) {
  Buf b; buf_init(&b, a);
  bool any = false;
  for (;;) {
    tf_skip_ws(r);
    if (r->p >= r->end || (*r->p != '"' && *r->p != '\'')) break;
    char q = *r->p++;
    while (r->p < r->end && *r->p != q) {
      if (*r->p == '\\' && r->p + 1 < r->end) {
        r->p++;
        char c = *r->p++;
        switch (c) {
          case 'n': buf_byte(&b, '\n'); break;
          case 'r': buf_byte(&b, '\r'); break;
          case 't': buf_byte(&b, '\t'); break;
          case 'a': buf_byte(&b, '\a'); break;
          case 'b': buf_byte(&b, '\b'); break;
          case 'f': buf_byte(&b, '\f'); break;
          case 'v': buf_byte(&b, '\v'); break;
          case '\\': case '\'': case '"': case '?': buf_byte(&b, c); break;
          case 'x': case 'X': {
            unsigned v = 0; int n = 0;
            while (r->p < r->end && n < 2 && isxdigit((unsigned char)*r->p)) {
              char h = *r->p++;
              v = v*16 + (unsigned)(isdigit((unsigned char)h) ? h-'0' : (tolower(h)-'a'+10));
              n++;
            }
            buf_byte(&b, (char)v);
            break;
          }
          default:
            if (c >= '0' && c <= '7') {
              unsigned v = (unsigned)(c - '0'); int n = 1;
              while (r->p < r->end && n < 3 && *r->p >= '0' && *r->p <= '7') {
                v = v*8 + (unsigned)(*r->p++ - '0'); n++;
              }
              buf_byte(&b, (char)v);
            } else buf_byte(&b, c);
        }
      } else {
        if (*r->p == '\n') r->line++;
        buf_byte(&b, *r->p++);
      }
    }
    if (r->p < r->end) r->p++;
    any = true;
  }
  if (!any) return false;
  *out = buf_str(&b);
  return true;
}

static bool tf_number(TfRd *r, double *dv, int64_t *iv, uint64_t *uv, bool *is_float) {
  tf_skip_ws(r);
  const char *s = r->p;
  if (r->p < r->end && (*r->p=='-'||*r->p=='+')) r->p++;
  const char *digits = r->p;
  while (r->p < r->end && (isalnum((unsigned char)*r->p) || *r->p=='.' ||
         ((*r->p=='+'||*r->p=='-') && (r->p[-1]=='e'||r->p[-1]=='E'))))
    r->p++;
  if (r->p == digits) { r->p = s; return false; }
  char tmp[64];
  size_t n = (size_t)(r->p - s);
  if (n >= sizeof tmp) n = sizeof tmp - 1;
  memcpy(tmp, s, n); tmp[n] = '\0';
  if (strchr(tmp, '.') || strpbrk(tmp, "eE") || !strcmp(tmp,"inf") || !strcmp(tmp,"-inf") || !strcmp(tmp,"nan")) {
    *is_float = true; *dv = strtod(tmp, NULL);
  } else {
    *is_float = false;
    *iv = (int64_t)strtoll(tmp, NULL, 0);
    /* An unsigned field may legitimately carry a value above INT64_MAX, and
     * strtoll saturates there. Keep both readings and let the field's type
     * choose. */
    *uv = (tmp[0] == '-') ? (uint64_t)*iv : strtoull(tmp, NULL, 0);
  }
  return true;
}

static bool tf_parse_msg(TfRd *r, Arena *a, Dyn *m, const Desc *d, char close);

static bool tf_put_scalar(TfRd *r, Arena *a, Dyn *m, const FieldD *f) {
  Str s;
  double dv = 0; int64_t iv = 0; uint64_t uv = 0; bool isf = false;
  switch (f->type) {
    case T_STRING: case T_BYTES:
      if (!tf_string(r, a, &s)) return false;
      dyn_put_bytes(m, f->number, s);
      return true;
    case T_BOOL: {
      Str id;
      if (tf_ident(r, &id)) {
        bool v = str_eq_c(id, "true") || str_eq_c(id, "True") || str_eq_c(id, "1");
        dyn_put_bool(m, f->number, v);
        return true;
      }
      if (tf_number(r, &dv, &iv, &uv, &isf)) { dyn_put_bool(m, f->number, iv != 0); return true; }
      return false;
    }
    case T_ENUM: {
      Str id;
      tf_skip_ws(r);
      if (r->p < r->end && (isdigit((unsigned char)*r->p) || *r->p == '-')) {
        if (tf_number(r, &dv, &iv, &uv, &isf)) { dyn_put_i32(m, f->number, (int32_t)iv); return true; }
        return false;
      }
      if (tf_ident(r, &id)) {
        char *nm = arena_strndup(a, id.p, id.n);
        for (const EnumValD *x = f->enum_type ? f->enum_type->values : NULL; x; x = x->next)
          if (strcmp(x->name, nm) == 0) { dyn_put_i32(m, f->number, x->number); return true; }
        return false;
      }
      if (tf_number(r, &dv, &iv, &uv, &isf)) { dyn_put_i32(m, f->number, (int32_t)iv); return true; }
      return false;
    }
    case T_DOUBLE:
      if (!tf_number(r, &dv, &iv, &uv, &isf)) return false;
      dyn_put_double(m, f->number, isf ? dv : (double)iv);
      return true;
    case T_FLOAT:
      if (!tf_number(r, &dv, &iv, &uv, &isf)) return false;
      dyn_put_float(m, f->number, (float)(isf ? dv : (double)iv));
      return true;
    default:
      if (!tf_number(r, &dv, &iv, &uv, &isf)) return false;
      if (isf) iv = (int64_t)dv;
      switch (f->type) {
        case T_INT32: case T_INT64: dyn_put_i64(m, f->number, iv); break;
        case T_UINT32: case T_UINT64: dyn_put_u64(m, f->number, uv); break;
        case T_SINT32: dyn_put_sint32(m, f->number, (int32_t)iv); break;
        case T_SINT64: dyn_put_sint64(m, f->number, iv); break;
        case T_FIXED32: dyn_put_fixed32(m, f->number, (uint32_t)iv); break;
        case T_SFIXED32: dyn_put_fixed32(m, f->number, (uint32_t)(int32_t)iv); break;
        case T_FIXED64: dyn_put_fixed64(m, f->number, uv); break;
        case T_SFIXED64: dyn_put_fixed64(m, f->number, (uint64_t)iv); break;
        default: return false;
      }
      return true;
  }
}

static bool tf_parse_msg(TfRd *r, Arena *a, Dyn *m, const Desc *d, char close) {
  for (;;) {
    tf_skip_ws(r);
    if (r->p >= r->end) return close == 0;
    if (close && *r->p == close) { r->p++; return true; }

    Str name;
    if (!tf_ident(r, &name)) {
      diag_add(r->d, r->file, r->line, 0, "Expected field name.");
      return false;
    }
    char *nm = arena_strndup(a, name.p, name.n);
    const FieldD *f = desc_field_by_name(d, nm);
    if (!f) {
      /* A group is written under its message name, not its field name. */
      for (const FieldD *g = d->fields; g; g = g->next)
        if (g->type == T_GROUP && g->msg_type && strcmp(g->msg_type->name, nm) == 0) { f = g; break; }
    }
    if (!f) {
      diag_add(r->d, r->file, r->line, 0, "Message type \"%s\" has no field named \"%s\".",
               d->full_name, nm);
      return false;
    }

    tf_skip_ws(r);
    bool had_colon = false;
    if (r->p < r->end && *r->p == ':') { r->p++; had_colon = true; }
    tf_skip_ws(r);

    if (f->type == T_MESSAGE || f->type == T_GROUP) {
      if (r->p >= r->end || (*r->p != '{' && *r->p != '<')) {
        diag_add(r->d, r->file, r->line, 0, "Expected \"{\".");
        return false;
      }
      char open = *r->p++;
      Dyn *sub = (f->type == T_GROUP) ? dyn_put_group(m, f->number)
                                      : dyn_put_msg(m, f->number);
      if (!f->msg_type) return false;
      if (!tf_parse_msg(r, a, sub, f->msg_type, open == '{' ? '}' : '>')) return false;
      /* A map entry always carries both key and value on the wire, whatever
       * the text supplied. */
      if (f->msg_type->is_map_entry)
        for (const FieldD *kv = f->msg_type->fields; kv; kv = kv->next) {
          if (dyn_count(sub, kv->number) > 0) continue;
          switch (kv->type) {
            case T_STRING: case T_BYTES: dyn_put_bytes(sub, kv->number, str_of("", 0)); break;
            case T_MESSAGE: case T_GROUP: dyn_put_bytes(sub, kv->number, str_of("", 0)); break;
            case T_FLOAT: case T_FIXED32: case T_SFIXED32: dyn_put_fixed32(sub, kv->number, 0); break;
            case T_DOUBLE: case T_FIXED64: case T_SFIXED64: dyn_put_fixed64(sub, kv->number, 0); break;
            default: dyn_put_varint(sub, kv->number, 0); break;
          }
        }
    } else if (had_colon && r->p < r->end && *r->p == '[') {
      r->p++;
      for (;;) {
        tf_skip_ws(r);
        if (r->p < r->end && *r->p == ']') { r->p++; break; }
        if (!tf_put_scalar(r, a, m, f)) return false;
        tf_skip_ws(r);
        if (r->p < r->end && *r->p == ',') r->p++;
      }
    } else {
      if (!tf_put_scalar(r, a, m, f)) {
        diag_add(r->d, r->file, r->line, 0, "Bad value for field \"%s\".", f->name);
        return false;
      }
    }
    tf_skip_ws(r);
    if (r->p < r->end && (*r->p == ';' || *r->p == ',')) r->p++;
  }
}

/* protoc encodes a packed repeated numeric field as ONE length-delimited field
 * holding the concatenated values. Applied after parsing because text format
 * presents the elements separately. */
static void tf_pack_fields(Dyn *m, const Desc *d) {
  for (const FieldD *f = d->fields; f; f = f->next) {
    if (f->label == L_REPEATED && f->packed && dyn_count(m, f->number) > 0)
      dyn_pack_field(m, f->number);
    if ((f->type == T_MESSAGE || f->type == T_GROUP) && f->msg_type)
      for (int i = 0; i < dyn_count(m, f->number); i++) {
        Dyn *sub = dyn_get_msg(m, f->number, i);
        if (sub) tf_pack_fields(sub, f->msg_type);
      }
  }
}

Dyn *tf_parse(Arena *a, Diags *d, const char *file, Str in, const Desc *desc) {
  TfRd r = { in.p, in.p + in.n, d, file, 0 };
  Dyn *m = dyn_new(a);
  if (!tf_parse_msg(&r, a, m, desc, 0)) return NULL;
  tf_pack_fields(m, desc);
  return m;
}
