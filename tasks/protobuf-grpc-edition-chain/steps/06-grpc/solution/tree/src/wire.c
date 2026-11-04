/* wire.c — varint / fixed / length-delimited codec. */
#include "wire.h"
#include <string.h>

size_t wire_varint_size(uint64_t v) {
  size_t n = 1;
  while (v >= 0x80) { v >>= 7; n++; }
  return n;
}

void wire_put_varint(Buf *b, uint64_t v) {
  uint8_t tmp[10]; size_t n = 0;
  do { uint8_t byte = (uint8_t)(v & 0x7f); v >>= 7; if (v) byte |= 0x80; tmp[n++] = byte; } while (v);
  buf_put(b, tmp, n);
}

void wire_put_tag(Buf *b, uint32_t field, int wt) {
  wire_put_varint(b, ((uint64_t)field << 3) | (uint64_t)wt);
}

void wire_put_fixed32(Buf *b, uint32_t v) {
  uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
  buf_put(b, t, 4);
}

void wire_put_fixed64(Buf *b, uint64_t v) {
  uint8_t t[8];
  for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * i));
  buf_put(b, t, 8);
}

void wire_put_len(Buf *b, uint32_t field, Str payload) {
  wire_put_tag(b, field, WT_LEN);
  wire_put_varint(b, payload.n);
  buf_put(b, payload.p, payload.n);
}

void wrd_init(WRd *r, Str s) {
  r->p = (const uint8_t *)s.p; r->end = r->p + s.n; r->err = NULL;
}

bool wrd_varint(WRd *r, uint64_t *v) {
  uint64_t out = 0; int shift = 0;
  while (r->p < r->end) {
    uint8_t byte = *r->p++;
    /* Protobuf allows up to 10 bytes; the 10th contributes only bit 63. */
    if (shift < 64) out |= ((uint64_t)(byte & 0x7f)) << shift;
    shift += 7;
    if (!(byte & 0x80)) { *v = out; return true; }
    if (shift >= 70) { r->err = "varint too long"; return false; }
  }
  r->err = "truncated varint";
  return false;
}

bool wrd_tag(WRd *r, uint32_t *field, int *wt) {
  uint64_t t;
  if (!wrd_varint(r, &t)) return false;
  /* Truncate to 32 bits first: protoc's tags are uint32, and the difference
   * shows on any varint wider than 32 bits. */
  uint32_t t32 = (uint32_t)t;
  uint32_t f = t32 >> 3;
  int w = (int)(t32 & 7);
  if (f == 0) { r->err = "field number 0"; return false; }
  if (w > 5 || w == 6 || w == 7) { r->err = "bad wire type"; return false; }
  *field = f; *wt = w;
  return true;
}

bool wrd_fixed32(WRd *r, uint32_t *v) {
  if ((size_t)(r->end - r->p) < 4) { r->err = "truncated fixed32"; return false; }
  uint32_t out = 0;
  for (int i = 0; i < 4; i++) out |= ((uint32_t)r->p[i]) << (8 * i);
  r->p += 4; *v = out; return true;
}

bool wrd_fixed64(WRd *r, uint64_t *v) {
  if ((size_t)(r->end - r->p) < 8) { r->err = "truncated fixed64"; return false; }
  uint64_t out = 0;
  for (int i = 0; i < 8; i++) out |= ((uint64_t)r->p[i]) << (8 * i);
  r->p += 8; *v = out; return true;
}

bool wrd_len(WRd *r, Str *s) {
  uint64_t n;
  if (!wrd_varint(r, &n)) return false;
  if (n > (uint64_t)(r->end - r->p)) { r->err = "truncated length-delimited"; return false; }
  s->p = (const char *)r->p; s->n = (size_t)n;
  r->p += n;
  return true;
}

/* Skip one field, copying its raw bytes (tag included) into `raw` when asked.
 * Groups recurse until their matching EGROUP, which is what unknown-field
 * retention needs for proto2 group-shaped unknowns. */
bool wrd_skip_field(WRd *r, uint32_t field, int wt, Buf *raw) {
  const uint8_t *start = r->p;
  switch (wt) {
    case WT_VARINT: { uint64_t v; if (!wrd_varint(r, &v)) return false; break; }
    case WT_I64:    { uint64_t v; if (!wrd_fixed64(r, &v)) return false; break; }
    case WT_I32:    { uint32_t v; if (!wrd_fixed32(r, &v)) return false; break; }
    case WT_LEN:    { Str s; if (!wrd_len(r, &s)) return false; break; }
    case WT_SGROUP: {
      for (;;) {
        uint32_t f2; int w2;
        if (wrd_eof(r)) { r->err = "unterminated group"; return false; }
        if (!wrd_tag(r, &f2, &w2)) return false;
        if (w2 == WT_EGROUP) {
          if (f2 != field) { r->err = "mismatched group end"; return false; }
          break;
        }
        if (!wrd_skip_field(r, f2, w2, NULL)) return false;
      }
      break;
    }
    default: r->err = "unexpected wire type"; return false;
  }
  if (raw) {
    /* Re-emit the tag, then the body exactly as it appeared. */
    wire_put_tag(raw, field, wt);
    buf_put(raw, (const char *)start, (size_t)(r->p - start));
  }
  return true;
}
