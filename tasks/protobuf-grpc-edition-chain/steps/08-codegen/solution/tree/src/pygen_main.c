/* protoc-gen-py -- a protoc code-generation plugin emitting Python.
 *
 * Reads a CodeGeneratorRequest on stdin, writes a CodeGeneratorResponse on
 * stdout. Depends on libc alone: it re-serialises descriptors while recording
 * byte offsets, which the tree's Dyn API does not expose.
 *
 * Every layout rule here was measured against protoc 32.1 output. The
 * counter-intuitive ones:
 *   - the offset listing is enums, then messages, then services at FILE level,
 *     but a message lists ITSELF, then nested messages, then nested enums.
 *     Those two orders disagree; both were confirmed with the enum declared on
 *     the far side of a message.
 *   - the embedded blob is the FileDescriptorProto with json_name SUPPRESSED.
 *   - a file WITH options writes _globals['DESCRIPTOR']._loaded_options; a file
 *     without writes a bare DESCRIPTOR._loaded_options. Different line, not an
 *     extra one.
 *   - type options precede field options, which is not wire order. */
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; } B;

static void bput(B *b, const void *s, size_t n) {
  if (b->n + n > b->cap) {
    size_t c = b->cap ? b->cap * 2 : 256;
    while (c < b->n + n) c *= 2;
    b->p = realloc(b->p, c);
    if (!b->p) { fputs("protoc-gen-py: out of memory\n", stderr); exit(1); }
    b->cap = c;
  }
  memcpy(b->p + b->n, s, n);
  b->n += n;
}
static void bs(B *b, const char *s) { bput(b, s, strlen(s)); }
static void bfmt(B *b, const char *fmt, ...) {
  char t[2048];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(t, sizeof t, fmt, ap);
  va_end(ap);
  if (n > 0) bput(b, t, (size_t)n);
}

typedef struct { const unsigned char *p, *end; } Rdr;

static bool rvar(Rdr *r, uint64_t *v) {
  uint64_t x = 0; int s = 0;
  while (r->p < r->end) {
    unsigned char c = *r->p++;
    x |= (uint64_t)(c & 0x7F) << s; s += 7;
    if (!(c & 0x80)) { *v = x; return true; }
    if (s > 63) return false;
  }
  return false;
}

typedef struct {
  uint32_t num; int wt;
  const unsigned char *val; size_t vlen;
  const unsigned char *rec; size_t rlen;
} Rec;

static bool rnext(Rdr *r, Rec *o) {
  if (r->p >= r->end) return false;
  const unsigned char *st = r->p;
  uint64_t tag;
  if (!rvar(r, &tag)) return false;
  o->num = (uint32_t)(tag >> 3); o->wt = (int)(tag & 7);
  switch (o->wt) {
    case 0: { uint64_t v; const unsigned char *s = r->p;
              if (!rvar(r, &v)) return false;
              o->val = s; o->vlen = (size_t)(r->p - s); break; }
    case 1: if (r->end - r->p < 8) return false; o->val = r->p; o->vlen = 8; r->p += 8; break;
    case 5: if (r->end - r->p < 4) return false; o->val = r->p; o->vlen = 4; r->p += 4; break;
    case 2: { uint64_t n; if (!rvar(r, &n)) return false;
              if ((uint64_t)(r->end - r->p) < n) return false;
              o->val = r->p; o->vlen = (size_t)n; r->p += n; break; }
    default: return false;
  }
  o->rec = st; o->rlen = (size_t)(r->p - st);
  return true;
}

static size_t varlen(uint64_t v) { size_t n = 1; while (v >>= 7) n++; return n; }
static void wvar(B *b, uint64_t v) {
  unsigned char t[10]; size_t n = 0;
  do { unsigned char c = v & 0x7F; v >>= 7; t[n++] = c | (v ? 0x80 : 0); } while (v);
  bput(b, t, n);
}
static void wlen(B *b, uint32_t num, const void *p, size_t n) {
  wvar(b, ((uint64_t)num << 3) | 2); wvar(b, n); bput(b, p, n);
}

#define FDP_NAME 1
#define FDP_DEPENDENCY 3
#define FDP_MESSAGE 4
#define FDP_ENUM 5
#define FDP_SERVICE 6
#define FDP_EXTENSION 7
#define FDP_OPTIONS 8
#define DP_NAME 1
#define DP_FIELD 2
#define DP_NESTED 3
#define DP_ENUM 4
#define DP_EXTENSION 6
#define DP_OPTIONS 7
#define EDP_OPTIONS 3
#define FLD_NAME 1
#define FLD_OPTIONS 8
#define FLD_JSON_NAME 10
#define CGR_FILE_TO_GENERATE 1
#define CGR_PROTO_FILE 15
#define CGRESP_FILE 15
#define CGRESP_FILE_NAME 1
#define CGRESP_FILE_CONTENT 15

/* 'E' file-level enum, 'm' anything in a message tree (nested enums included,
 * in traversal order), 's' service. The listing is all E, then all m, then all
 * s -- which is why nested enums must NOT be filed as 'E'. */
typedef struct { char py[256]; char kind; size_t start, end; } Ent;
typedef struct { char py[256]; char field[128]; const unsigned char *opt; size_t optn; } Opt;

#define MAXE 2048
static Ent g_ent[MAXE]; static size_t g_ne;
static Opt g_opt[MAXE]; static size_t g_no;

static size_t ent_reserve(void) {
  if (g_ne >= MAXE) { fputs("protoc-gen-py: too many descriptors\n", stderr); exit(1); }
  return g_ne++;
}
static void opt_add(const char *py, const char *field, const unsigned char *o, size_t n) {
  if (g_no >= MAXE) { fputs("protoc-gen-py: too many options\n", stderr); exit(1); }
  Opt *p = &g_opt[g_no++];
  snprintf(p->py, sizeof p->py, "%s", py);
  snprintf(p->field, sizeof p->field, "%s", field ? field : "");
  p->opt = o; p->optn = n;
}

static void upper_into(char *dst, size_t cap, const char *pre,
                       const unsigned char *nm, size_t n) {
  size_t i = (pre && *pre) ? (size_t)snprintf(dst, cap, "%s_", pre)
                           : (size_t)snprintf(dst, cap, "_");
  for (size_t k = 0; k < n && i + 1 < cap; k++) dst[i++] = (char)toupper(nm[k]);
  dst[i] = '\0';
}
static void name_of(char *dst, size_t cap, const char *pre,
                    const unsigned char *v, size_t n, uint32_t namefield) {
  Rdr r = { v, v + n }; Rec rec;
  dst[0] = '\0';
  while (rnext(&r, &rec))
    if (rec.num == namefield) { upper_into(dst, cap, pre, rec.val, rec.vlen); return; }
}

static void emit_msg(B *out, const unsigned char *v, size_t n,
                     const char *parent_py, size_t base, bool record);

/* A FieldDescriptorProto with json_name dropped. */
static void emit_field(B *out, const unsigned char *v, size_t n, uint32_t out_field) {
  B tmp = {0};
  Rdr r = { v, v + n }; Rec rec;
  while (rnext(&r, &rec)) {
    if (rec.num == FLD_JSON_NAME) continue;   /* suppressed in the blob */
    bput(&tmp, rec.rec, rec.rlen);
  }
  wlen(out, out_field, tmp.p, tmp.n);
  free(tmp.p);
}

/* `base` is the absolute offset at which this message's CONTENT lands.
 * `record` is false on the sizing pass -- without it the sizing pass files
 * every nested entity a second time, with offsets that look plausible. */
static void emit_msg(B *out, const unsigned char *v, size_t n,
                     const char *parent_py, size_t base, bool record) {
  char py[256];
  name_of(py, sizeof py, parent_py, v, n, DP_NAME);

  /* The parent's slot is reserved BEFORE recursing: the listing wants self,
   * then children, and children append as they are walked. */
  size_t slot = 0;
  if (record) {
    slot = ent_reserve();
    snprintf(g_ent[slot].py, sizeof g_ent[slot].py, "%s", py);
    g_ent[slot].kind = 'm';
    g_ent[slot].start = base;
  }

  B body = {0};
  Rdr r = { v, v + n }; Rec rec;
  while (rnext(&r, &rec)) {
    if (rec.num == DP_FIELD) {
      emit_field(&body, rec.val, rec.vlen, DP_FIELD); continue;
    }
    /* A nested extension is a FieldDescriptorProto too, so its json_name is
     * suppressed in the blob exactly as a field's is. */
    if (rec.num == DP_EXTENSION) {
      emit_field(&body, rec.val, rec.vlen, DP_EXTENSION); continue;
    }
    if (rec.num == DP_NESTED) {
      B probe = {0};
      emit_msg(&probe, rec.val, rec.vlen, py, 0, false);      /* sizing only */
      size_t hn = varlen(((uint64_t)DP_NESTED << 3) | 2), ln = varlen(probe.n);
      size_t childbase = base + body.n + hn + ln;
      B sub = {0};
      emit_msg(&sub, rec.val, rec.vlen, py, childbase, record);
      wlen(&body, DP_NESTED, sub.p, sub.n);
      free(probe.p); free(sub.p);
      continue;
    }
    if (rec.num == DP_ENUM) {
      size_t hn = varlen(((uint64_t)DP_ENUM << 3) | 2), ln = varlen(rec.vlen);
      size_t childbase = base + body.n + hn + ln;
      if (record) {
        char epy[256];
        name_of(epy, sizeof epy, py, rec.val, rec.vlen, DP_NAME);
        size_t es = ent_reserve();
        snprintf(g_ent[es].py, sizeof g_ent[es].py, "%s", epy);
        g_ent[es].kind = 'm';               /* in the message tree, not file level */
        g_ent[es].start = childbase;
        g_ent[es].end = childbase + rec.vlen;
      }
      wlen(&body, DP_ENUM, rec.val, rec.vlen);
      continue;
    }
    bput(&body, rec.rec, rec.rlen);
  }
  if (record) g_ent[slot].end = base + body.n;
  bput(out, body.p, body.n);
  free(body.p);
}

static void emit_file(B *out, const unsigned char *v, size_t n) {
  Rdr r = { v, v + n }; Rec rec;
  while (rnext(&r, &rec)) {
    if (rec.num == FDP_MESSAGE) {
      B probe = {0};
      emit_msg(&probe, rec.val, rec.vlen, "", 0, false);
      size_t hn = varlen(((uint64_t)FDP_MESSAGE << 3) | 2), ln = varlen(probe.n);
      size_t base = out->n + hn + ln;
      B sub = {0};
      emit_msg(&sub, rec.val, rec.vlen, "", base, true);
      wlen(out, FDP_MESSAGE, sub.p, sub.n);
      free(probe.p); free(sub.p);
      continue;
    }
    if (rec.num == FDP_ENUM || rec.num == FDP_SERVICE) {
      size_t hn = varlen(((uint64_t)rec.num << 3) | 2), ln = varlen(rec.vlen);
      size_t base = out->n + hn + ln;
      char py[256];
      name_of(py, sizeof py, "", rec.val, rec.vlen, DP_NAME);
      size_t s = ent_reserve();
      snprintf(g_ent[s].py, sizeof g_ent[s].py, "%s", py);
      g_ent[s].kind = (rec.num == FDP_ENUM) ? 'E' : 's';
      g_ent[s].start = base; g_ent[s].end = base + rec.vlen;
      wlen(out, rec.num, rec.val, rec.vlen);
      continue;
    }
    if (rec.num == FDP_EXTENSION) {
      emit_field(out, rec.val, rec.vlen, FDP_EXTENSION); continue;
    }
    bput(out, rec.rec, rec.rlen);
  }
}

/* protoc writes \n \r \t \' \" \\ as those forms, a printable byte literally
 * unless it is a hex digit immediately after a \xNN escape, everything else as
 * lowercase \xNN. Round-tripped against a real literal character for
 * character. */
static void esc_hex(B *b, const char *s, size_t n) {
  bool prev_hex = false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\n') { bs(b, "\\n"); prev_hex = false; }
    else if (c == '\r') { bs(b, "\\r"); prev_hex = false; }
    else if (c == '\t') { bs(b, "\\t"); prev_hex = false; }
    else if (c == '\'') { bs(b, "\\'"); prev_hex = false; }
    else if (c == '"') { bs(b, "\\\""); prev_hex = false; }
    else if (c == '\\') { bs(b, "\\\\"); prev_hex = false; }
    else if (c >= 0x20 && c <= 0x7E && !(prev_hex && isxdigit(c))) {
      bput(b, &c, 1); prev_hex = false;
    } else { bfmt(b, "\\x%02x", c); prev_hex = true; }
  }
}
/* _serialized_options uses a different, octal escaper. */
static void esc_oct(B *b, const unsigned char *s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char c = s[i];
    if (c == '\n') bs(b, "\\n");
    else if (c == '\r') bs(b, "\\r");
    else if (c == '\t') bs(b, "\\t");
    else if (c == '\'') bs(b, "\\'");
    else if (c == '"') bs(b, "\\\"");
    else if (c == '\\') bs(b, "\\\\");
    else if (c >= 0x20 && c <= 0x7E) bput(b, &c, 1);
    else bfmt(b, "\\%03o", c);
  }
}

/* alias: drop .proto, double every _, turn / into _dot_, append __pb2 */
static void dep_alias(char *dst, size_t cap, const char *dep) {
  size_t i = 0, dn = strlen(dep);
  if (dn > 6 && strcmp(dep + dn - 6, ".proto") == 0) dn -= 6;
  for (size_t k = 0; k < dn && i + 8 < cap; k++) {
    if (dep[k] == '_') { dst[i++] = '_'; dst[i++] = '_'; }
    else if (dep[k] == '/') { memcpy(dst + i, "_dot_", 5); i += 5; }
    else dst[i++] = dep[k];
  }
  snprintf(dst + i, cap - i, "__pb2");
}

/* Options are listed in an order of their own, measured on c14_options.proto:
 * the file's own options, then each top-level enum, then each top-level message
 * as a BLOCK. A message's block is its nested messages (recursively), then its
 * nested enums, then its own fields' options, and the message ITSELF LAST --
 * after the fields it contains. "type options, then field options" fits every
 * fixture with only one option-bearing entity and is wrong the moment a message
 * has both. */
static void collect_msg_opts(const unsigned char *v, size_t n, const char *parent_py) {
  char py[256];
  name_of(py, sizeof py, parent_py, v, n, DP_NAME);
  Rdr r; Rec rec;

  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == DP_NESTED) collect_msg_opts(rec.val, rec.vlen, py);

  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == DP_ENUM) {
      char epy[256];
      name_of(epy, sizeof epy, py, rec.val, rec.vlen, DP_NAME);
      Rdr er = { rec.val, rec.val + rec.vlen }; Rec erec;
      while (rnext(&er, &erec))
        if (erec.num == EDP_OPTIONS) opt_add(epy, NULL, erec.val, erec.vlen);
    }

  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == DP_FIELD) {
      const unsigned char *fn = NULL, *fopt = NULL;
      size_t fnn = 0, foptn = 0;
      Rdr fr = { rec.val, rec.val + rec.vlen }; Rec frec;
      while (rnext(&fr, &frec)) {
        if (frec.num == FLD_NAME) { fn = frec.val; fnn = frec.vlen; }
        if (frec.num == FLD_OPTIONS) { fopt = frec.val; foptn = frec.vlen; }
      }
      if (fopt) {
        char nm[128];
        size_t k = fnn < sizeof nm - 1 ? fnn : sizeof nm - 1;
        if (fn) { memcpy(nm, fn, k); }
        nm[k] = '\0';
        opt_add(py, nm, fopt, foptn);
      }
    }

  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == DP_OPTIONS) opt_add(py, NULL, rec.val, rec.vlen);
}

static void collect_opts(const unsigned char *v, size_t n) {
  Rdr r; Rec rec;
  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == FDP_OPTIONS) opt_add("DESCRIPTOR", NULL, rec.val, rec.vlen);
  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == FDP_ENUM) {
      char epy[256];
      name_of(epy, sizeof epy, "", rec.val, rec.vlen, DP_NAME);
      Rdr er = { rec.val, rec.val + rec.vlen }; Rec erec;
      while (rnext(&er, &erec))
        if (erec.num == EDP_OPTIONS) opt_add(epy, NULL, erec.val, erec.vlen);
    }
  r = (Rdr){ v, v + n };
  while (rnext(&r, &rec))
    if (rec.num == FDP_MESSAGE) collect_msg_opts(rec.val, rec.vlen, "");
}

static void gen_python(B *out, const unsigned char *fdp, size_t fdpn) {
  g_ne = 0; g_no = 0;
  B blob = {0};
  emit_file(&blob, fdp, fdpn);
  collect_opts(fdp, fdpn);

  char fname[512] = "";
  char deps[64][512]; size_t ndeps = 0;
  bool file_opts = false;
  { Rdr r = { fdp, fdp + fdpn }; Rec rec;
    while (rnext(&r, &rec)) {
      if (rec.num == FDP_NAME) {
        size_t k = rec.vlen < sizeof fname - 1 ? rec.vlen : sizeof fname - 1;
        memcpy(fname, rec.val, k); fname[k] = '\0';
      } else if (rec.num == FDP_DEPENDENCY && ndeps < 64) {
        size_t k = rec.vlen < 511 ? rec.vlen : 511;
        memcpy(deps[ndeps], rec.val, k); deps[ndeps][k] = '\0'; ndeps++;
      } else if (rec.num == FDP_OPTIONS) file_opts = true;
    } }

  char stem[512];
  snprintf(stem, sizeof stem, "%s", fname);
  { size_t sn = strlen(stem);
    if (sn > 6 && strcmp(stem + sn - 6, ".proto") == 0) stem[sn - 6] = '\0'; }

  bs(out, "# -*- coding: utf-8 -*-\n");
  bs(out, "# Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
  bs(out, "# NO CHECKED-IN PROTOBUF GENCODE\n");
  bfmt(out, "# source: %s\n", fname);
  bs(out, "# Protobuf Python Version: 6.32.1\n");
  bs(out, "\"\"\"Generated protocol buffer code.\"\"\"\n");
  bs(out, "from google.protobuf import descriptor as _descriptor\n");
  bs(out, "from google.protobuf import descriptor_pool as _descriptor_pool\n");
  bs(out, "from google.protobuf import runtime_version as _runtime_version\n");
  bs(out, "from google.protobuf import symbol_database as _symbol_database\n");
  bs(out, "from google.protobuf.internal import builder as _builder\n");
  bs(out, "_runtime_version.ValidateProtobufRuntimeVersion(\n");
  bs(out, "    _runtime_version.Domain.PUBLIC,\n");
  bs(out, "    6,\n    32,\n    1,\n    '',\n");
  bfmt(out, "    '%s'\n)\n", fname);
  bs(out, "# @@protoc_insertion_point(imports)\n\n");
  bs(out, "_sym_db = _symbol_database.Default()\n\n\n");
  for (size_t i = 0; i < ndeps; i++) {
    char alias[600]; dep_alias(alias, sizeof alias, deps[i]);
    char ds[512]; snprintf(ds, sizeof ds, "%s", deps[i]);
    size_t dn = strlen(ds);
    if (dn > 6 && strcmp(ds + dn - 6, ".proto") == 0) ds[dn - 6] = '\0';
    char *slash = strrchr(ds, '/');
    if (slash) {
      *slash = '\0';
      for (char *q = ds; *q; q++) if (*q == '/') *q = '.';
      bfmt(out, "from %s import %s_pb2 as %s\n", ds, slash + 1, alias);
    } else {
      bfmt(out, "import %s_pb2 as %s\n", ds, alias);
    }
  }
  bs(out, "\n\n");
  bs(out, "DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'");
  esc_hex(out, blob.p, blob.n);
  bs(out, "')\n\n");
  bs(out, "_globals = globals()\n");
  bs(out, "_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)\n");
  /* The module argument takes dots where the path has slashes; the generated
   * FILE name keeps the slash. Checking this on a flat fixture proves nothing,
   * because a flat name has no separator to show. */
  char modname[512];
  snprintf(modname, sizeof modname, "%s", stem);
  for (char *q = modname; *q; q++) if (*q == '/') *q = '.';
  bfmt(out, "_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, '%s_pb2', _globals)\n", modname);
  bs(out, "if not _descriptor._USE_C_DESCRIPTORS:\n");

  if (file_opts) {
    bs(out, "  _globals['DESCRIPTOR']._loaded_options = None\n");
    bs(out, "  _globals['DESCRIPTOR']._serialized_options = b'");
    for (size_t i = 0; i < g_no; i++)
      if (strcmp(g_opt[i].py, "DESCRIPTOR") == 0) { esc_oct(out, g_opt[i].opt, g_opt[i].optn); break; }
    bs(out, "'\n");
  } else {
    bs(out, "  DESCRIPTOR._loaded_options = None\n");
  }
  /* g_opt is already in protoc's order -- see collect_opts. */
  for (size_t i = 0; i < g_no; i++) {
      Opt *o = &g_opt[i];
      if (strcmp(o->py, "DESCRIPTOR") == 0) continue;
      bool isfield = o->field[0] != '\0';
      if (isfield) {
        bfmt(out, "  _globals['%s'].fields_by_name['%s']._loaded_options = None\n", o->py, o->field);
        bfmt(out, "  _globals['%s'].fields_by_name['%s']._serialized_options = b'", o->py, o->field);
      } else {
        bfmt(out, "  _globals['%s']._loaded_options = None\n", o->py);
        bfmt(out, "  _globals['%s']._serialized_options = b'", o->py);
      }
      esc_oct(out, o->opt, o->optn);
      bs(out, "'\n");
    }
  const char order[] = { 'E', 'm', 's' };
  for (size_t k = 0; k < sizeof order; k++)
    for (size_t i = 0; i < g_ne; i++)
      if (g_ent[i].kind == order[k]) {
        bfmt(out, "  _globals['%s']._serialized_start=%zu\n", g_ent[i].py, g_ent[i].start);
        bfmt(out, "  _globals['%s']._serialized_end=%zu\n", g_ent[i].py, g_ent[i].end);
      }
  bs(out, "# @@protoc_insertion_point(module_scope)\n");
  free(blob.p);
}

int main(void) {
  B in = {0};
  char tmp[65536]; size_t n;
  while ((n = fread(tmp, 1, sizeof tmp, stdin)) > 0) bput(&in, tmp, n);

  char want[64][512]; size_t nwant = 0;
  const unsigned char *files[256]; size_t flen[256], nfiles = 0;
  { Rdr r = { (const unsigned char *)in.p, (const unsigned char *)in.p + in.n }; Rec rec;
    while (rnext(&r, &rec)) {
      if (rec.num == CGR_FILE_TO_GENERATE && nwant < 64) {
        size_t k = rec.vlen < 511 ? rec.vlen : 511;
        memcpy(want[nwant], rec.val, k); want[nwant][k] = '\0'; nwant++;
      } else if (rec.num == CGR_PROTO_FILE && nfiles < 256) {
        files[nfiles] = rec.val; flen[nfiles] = rec.vlen; nfiles++;
      }
    } }

  B resp = {0};
  for (size_t i = 0; i < nwant; i++) {
    const unsigned char *fdp = NULL; size_t fdpn = 0;
    for (size_t j = 0; j < nfiles; j++) {
      char nm[512] = "";
      Rdr r = { files[j], files[j] + flen[j] }; Rec rec;
      while (rnext(&r, &rec))
        if (rec.num == FDP_NAME) {
          size_t k = rec.vlen < sizeof nm - 1 ? rec.vlen : sizeof nm - 1;
          memcpy(nm, rec.val, k); nm[k] = '\0'; break;
        }
      if (strcmp(nm, want[i]) == 0) { fdp = files[j]; fdpn = flen[j]; break; }
    }
    if (!fdp) {
      fprintf(stderr, "protoc-gen-py: %s not present in proto_file\n", want[i]);
      return 1;
    }
    B py = {0};
    gen_python(&py, fdp, fdpn);
    char outname[600];
    snprintf(outname, sizeof outname, "%s", want[i]);
    size_t on = strlen(outname);
    if (on > 6 && strcmp(outname + on - 6, ".proto") == 0)
      snprintf(outname + on - 6, sizeof outname - (on - 6), "_pb2.py");
    B one = {0};
    wlen(&one, CGRESP_FILE_NAME, outname, strlen(outname));
    wlen(&one, CGRESP_FILE_CONTENT, py.p, py.n);
    wlen(&resp, CGRESP_FILE, one.p, one.n);
    free(one.p); free(py.p);
  }
  if (resp.n) fwrite(resp.p, 1, resp.n, stdout);
  return 0;
}
