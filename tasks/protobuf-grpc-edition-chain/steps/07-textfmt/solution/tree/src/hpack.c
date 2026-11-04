#include "hpack.h"
/* HPACK (RFC 7541) decoder. Huffman-coded strings are DETECTED AND REFUSED
 * rather than silently mis-decoded: a decoder that quietly returns garbage on an
 * unimplemented path is the same silent-divergence failure this ladder exists to
 * catch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ST[][2] = {
  {0,0},
  {":authority",""},{":method","GET"},{":method","POST"},{":path","/"},
  {":path","/index.html"},{":scheme","http"},{":scheme","https"},{":status","200"},
  {":status","204"},{":status","206"},{":status","304"},{":status","400"},
  {":status","404"},{":status","500"},{"accept-charset",""},
  {"accept-encoding","gzip, deflate"},{"accept-language",""},{"accept-ranges",""},
  {"accept",""},{"access-control-allow-origin",""},{"age",""},{"allow",""},
  {"authorization",""},{"cache-control",""},{"content-disposition",""},
  {"content-encoding",""},{"content-language",""},{"content-length",""},
  {"content-location",""},{"content-range",""},{"content-type",""},{"cookie",""},
  {"date",""},{"etag",""},{"expect",""},{"expires",""},{"from",""},{"host",""},
  {"if-match",""},{"if-modified-since",""},{"if-none-match",""},{"if-range",""},
  {"if-unmodified-since",""},{"last-modified",""},{"link",""},{"location",""},
  {"max-forwards",""},{"proxy-authenticate",""},{"proxy-authorization",""},
  {"range",""},{"referer",""},{"refresh",""},{"retry-after",""},{"server",""},
  {"set-cookie",""},{"strict-transport-security",""},{"transfer-encoding",""},
  {"user-agent",""},{"vary",""},{"via",""},{"www-authenticate",""},
};
#define NST 61


static void dyn_evict(Dyn *d) {
  while (d->size > d->cap && d->n > 0) {
    Ent *last = &d->e[d->n - 1];
    d->size -= last->sz;
    free(last->n); free(last->v);
    d->n--;
  }
}
static void dyn_add(Dyn *d, const char *n, size_t ln, const char *v, size_t lv) {
  if (d->n >= 255) return;
  memmove(&d->e[1], &d->e[0], (size_t)d->n * sizeof(Ent));
  d->e[0].n = malloc(ln + 1); memcpy(d->e[0].n, n, ln); d->e[0].n[ln] = 0;
  d->e[0].v = malloc(lv + 1); memcpy(d->e[0].v, v, lv); d->e[0].v[lv] = 0;
  d->e[0].sz = ln + lv + 32;
  d->size += d->e[0].sz;
  d->n++;
  dyn_evict(d);
}

/* RFC 7541 5.1: N-bit prefix integer. */
static int rd_int(const unsigned char *b, size_t n, size_t *i, int prefix, unsigned *out) {
  if (*i >= n) return -1;
  unsigned mask = (1u << prefix) - 1u;
  unsigned v = b[*i] & mask;
  (*i)++;
  if (v < mask) { *out = v; return 0; }
  unsigned m = 0;
  for (;;) {
    if (*i >= n) return -1;
    unsigned char c = b[(*i)++];
    v += (unsigned)(c & 0x7f) << m;
    if (!(c & 0x80)) break;
    m += 7;
    if (m > 28) return -1;
  }
  *out = v;
  return 0;
}
static int rd_str(const unsigned char *b, size_t n, size_t *i, char **out, size_t *olen) {
  if (*i >= n) return -1;
  int huff = (b[*i] & 0x80) != 0;
  unsigned len;
  if (rd_int(b, n, i, 7, &len) < 0) return -1;
  if (*i + len > n) return -1;
  if (huff) { fprintf(stderr, "HPACK: Huffman-coded string not implemented\n"); return -2; }
  char *s = malloc(len + 1);
  memcpy(s, b + *i, len); s[len] = 0;
  *i += len;
  *out = s; *olen = len;
  return 0;
}

static void (*g_cb)(const char *, const char *, void *);
static void *g_cb_user;
void hpack_on_header(void (*cb)(const char *, const char *, void *), void *u) {
  g_cb = cb; g_cb_user = u;
}
static int emit(const char *n, const char *v, int *cnt) {
  if (g_cb) g_cb(n, v, g_cb_user);
  (*cnt)++;
  return 0;
}

int hpack_decode(const unsigned char *b, size_t n, Dyn *d, int *cnt) {
  size_t i = 0;
  while (i < n) {
    unsigned char c = b[i];
    const char *nm = NULL, *val = NULL;
    char *fn = NULL, *fv = NULL;
    if (c & 0x80) {                       /* indexed header field */
      unsigned idx;
      if (rd_int(b, n, &i, 7, &idx) < 0) return -1;
      if (idx == 0) return -1;
      if (idx <= NST) { nm = ST[idx][0]; val = ST[idx][1]; }
      else if ((int)(idx - NST - 1) < d->n) { nm = d->e[idx - NST - 1].n; val = d->e[idx - NST - 1].v; }
      else return -1;
    } else if ((c & 0xe0) == 0x20) {      /* dynamic table size update */
      unsigned sz;
      if (rd_int(b, n, &i, 5, &sz) < 0) return -1;
      d->cap = sz; dyn_evict(d);
      continue;
    } else {
      int incremental = (c & 0xc0) == 0x40;
      int prefix = incremental ? 6 : 4;
      unsigned idx;
      if (rd_int(b, n, &i, prefix, &idx) < 0) return -1;
      size_t ln = 0, lv = 0;
      if (idx == 0) { int r = rd_str(b, n, &i, &fn, &ln); if (r < 0) return r; nm = fn; }
      else if (idx <= NST) nm = ST[idx][0];
      else if ((int)(idx - NST - 1) < d->n) nm = d->e[idx - NST - 1].n;
      else return -1;
      int r = rd_str(b, n, &i, &fv, &lv); if (r < 0) return r;
      val = fv;
      if (incremental) dyn_add(d, nm, strlen(nm), val, lv);
    }
    emit(nm, val, cnt);
    free(fn); free(fv);
  }
  return 0;
}

