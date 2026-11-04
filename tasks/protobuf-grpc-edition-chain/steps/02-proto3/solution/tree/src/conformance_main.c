/* conformance_main.c -- the protobuf conformance testing program.
 *
 * The interface is upstream's: the runner forks this program and speaks the
 * framing documented in conformance/fork_pipe_runner.cc --
 *
 *   1. tester sends 4-byte length N (little endian)
 *   2. tester sends N bytes of serialized ConformanceRequest
 *   3. testee sends 4-byte length M (little endian)
 *   4. testee sends M bytes of serialized ConformanceResponse
 *
 * and closes the pipe when the suite is done. ConformanceRequest carries a
 * payload oneof plus requested_output_format and message_type;
 * ConformanceResponse answers with exactly one of parse_error,
 * serialize_error, runtime_error, skipped, or a payload.
 *
 * This program is deliberately schema-driven: it loads the conformance and
 * test-message .proto files through the same compiler front end the rest of
 * the project uses, so there is no generated code and no second parser.
 */
#include "pb.h"
#include "wire.h"
#include "desc.h"
#include "ast.h"
#include "build.h"
#include "json.h"
#include "wkt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* conformance.proto field numbers (from the shipped schema). */
enum {
  REQ_PROTOBUF_PAYLOAD = 1, REQ_JSON_PAYLOAD = 2, REQ_OUTPUT_FORMAT = 3,
  REQ_MESSAGE_TYPE = 4, REQ_TEST_CATEGORY = 5, REQ_TEXT_PAYLOAD = 8,
  RSP_PARSE_ERROR = 1, RSP_RUNTIME_ERROR = 2, RSP_PROTOBUF_PAYLOAD = 3,
  RSP_JSON_PAYLOAD = 4, RSP_SKIPPED = 5, RSP_SERIALIZE_ERROR = 6,
  RSP_TEXT_PAYLOAD = 8,
};
/* conformance.WireFormat */
enum { WF_UNSPECIFIED = 0, WF_PROTOBUF = 1, WF_JSON = 2, WF_JSPB = 3, WF_TEXT_FORMAT = 4 };

static bool read_exact(int fd, void *buf, size_t n) {
  unsigned char *p = buf;
  while (n) {
    ssize_t r = read(fd, p, n);
    if (r <= 0) return false;
    p += r; n -= (size_t)r;
  }
  return true;
}

static void write_exact(int fd, const void *buf, size_t n) {
  const unsigned char *p = buf;
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w <= 0) exit(1);
    p += w; n -= (size_t)w;
  }
}

/* Loads the suite's schemas into one pool, resolving imports the way protoc
 * does: a dependency is read from the schema directory, or from the compiler's
 * own embedded well-known-type sources. Loading the three top-level files in
 * isolation left google.protobuf.Timestamp and Duration absent from the pool,
 * so every field of those types resolved to nothing. */
static void load_one(Arena *a, Diags *d, BuildCtx *ctx, Pool *pool,
                     const char *dir, const char *name, const char **seen,
                     int *nseen) {
  for (int i = 0; i < *nseen; i++)
    if (strcmp(seen[i], name) == 0) return;
  if (*nseen < 64) seen[(*nseen)++] = arena_strdup(a, name);

  Str src = str_of("", 0);
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) {
    const char *base = strrchr(name, '/');
    snprintf(path, sizeof path, "%s/%s", dir, base ? base + 1 : name);
    f = fopen(path, "rb");
  }
  if (f) {
    Buf b; buf_init(&b, a);
    char tmp[65536]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(&b, tmp, n);
    fclose(f);
    src = buf_str(&b);
  } else {
    const WktFile *w = wkt_lookup(name);
    if (!w) return;
    src = str_of(w->text, w->len);
  }

  AstFile *af = ast_parse(a, d, name, src);
  if (!af) return;
  /* Dependencies first, so every type is in the pool before it is referenced. */
  for (const AstImport *im = af->imports; im; im = im->next) {
    char *dep = arena_strndup(a, im->path.p, im->path.n);
    load_one(a, d, ctx, pool, dir, dep, seen, nseen);
  }
  Dyn *fd = build_file(ctx, af, name);
  if (fd) {
    const char *err = NULL;
    pool_add_serialized(pool, dyn_serialize(fd, a), &err);
  }
}

static Pool *load_schemas(Arena *a, const char *dir) {
  static const char *roots[] = {
    "conformance.proto",
    "test_messages_proto2.proto",
    "test_messages_proto3.proto",
  };
  Diags d; diags_init(&d, a);
  BuildCtx *ctx = build_ctx_new(a, &d);
  Pool *pool = pool_new(a);
  const char *seen[64];
  int nseen = 0;
  for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++)
    load_one(a, &d, ctx, pool, dir, roots[i], seen, &nseen);
  return pool;
}

/* google.protobuf.Timestamp and Duration have legal ranges, and a value
 * outside them has no ProtoJSON representation -- the mapping requires a
 * serialize error rather than output. The out-of-range value sits in a
 * submessage field of the test message, so this recurses. */
static bool wkt_range_ok(const Dyn *m, const Desc *d) {
  if (!d) return true;
  if (d->full_name) {
    bool ts = strcmp(d->full_name, "google.protobuf.Timestamp") == 0;
    bool du = strcmp(d->full_name, "google.protobuf.Duration") == 0;
    if (ts || du) {
      uint64_t sec_u = 0, nan_u = 0;
      dyn_get_varint(m, 1, 0, &sec_u);
      dyn_get_varint(m, 2, 0, &nan_u);
      int64_t sec = (int64_t)sec_u;
      int32_t nanos = (int32_t)(uint32_t)nan_u;
      if (ts) {
        if (sec < -62135596800LL || sec > 253402300799LL) return false;
        if (nanos < 0 || nanos > 999999999) return false;
      } else {
        if (sec < -315576000000LL || sec > 315576000000LL) return false;
        if (nanos < -999999999 || nanos > 999999999) return false;
        if ((sec < 0 && nanos > 0) || (sec > 0 && nanos < 0)) return false;
      }
    }
  }
  for (const FieldD *f = d->fields; f; f = f->next) {
    if (f->type != T_MESSAGE && f->type != T_GROUP) continue;
    if (!f->msg_type) continue;
    int n = dyn_count(m, f->number);
    for (int i = 0; i < n; i++) {
      Dyn *sub = dyn_get_msg(m, f->number, i);
      if (sub && !wkt_range_ok(sub, f->msg_type)) return false;
    }
  }
  return true;
}

static void put_str_field(Dyn *m, uint32_t field, const char *s) {
  dyn_put_bytes(m, field, str_of(s, strlen(s)));
}

int main(int argc, char **argv) {
  /* --jsondbg <schema_dir> <type>: parse JSON from stdin and show what was
   * actually stored, so a single case can be driven without the harness. */
  if (argc > 3 && strcmp(argv[1], "--jsondbg") == 0) {
    Arena *a = arena_new();
    Pool *pool = load_schemas(a, argv[2]);
    const Desc *d = pool_find_message(pool, argv[3]);
    if (!d) { fprintf(stderr, "unknown type %s\n", argv[3]); return 2; }
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, stdin);
    buf[n] = '\0';
    const char *jerr = NULL;
    Dyn *m = json_parse(a, str_of(buf, n), d, &jerr);
    if (!m) { printf("PARSE FAILED: %s\n", jerr ? jerr : "(none)"); return 1; }
    Str w = dyn_serialize(m, a);
    printf("wire %zu:", w.n);
    for (size_t i = 0; i < w.n; i++) printf(" %02x", (unsigned char)w.p[i]);
    Str j = json_write(a, m, d);
    printf("\njson: %.*s\n", (int)j.n, j.p);
    return 0;
  }
  const char *schema_dir = (argc > 1) ? argv[1] : "/opt/verifier/proto";
  Arena *a = arena_new();
  Pool *pool = load_schemas(a, schema_dir);

  for (;;) {
    uint32_t len;
    if (!read_exact(0, &len, 4)) break;          /* EOF: suite finished */
    char *buf = arena_alloc(a, len ? len : 1);
    if (!read_exact(0, buf, len)) break;

    Arena *turn = arena_new();
    Dyn *rsp = dyn_new(turn);
    const char *err = NULL;
    Dyn *req = dyn_parse_bytes(turn, str_of(buf, len), &err);

    if (!req) {
      put_str_field(rsp, RSP_RUNTIME_ERROR, "could not parse ConformanceRequest");
    } else {
      Str type = str_of("", 0);
      dyn_get_bytes(req, REQ_MESSAGE_TYPE, 0, &type);
      char *tname = arena_strndup(turn, type.p, type.n);
      uint64_t out_fmt = WF_PROTOBUF;
      dyn_get_varint(req, REQ_OUTPUT_FORMAT, 0, &out_fmt);
      const Desc *d = pool_find_message(pool, tname);

      Str payload;
      if (!d) {
        put_str_field(rsp, RSP_RUNTIME_ERROR, "unknown message type");
      } else if (dyn_get_bytes(req, REQ_PROTOBUF_PAYLOAD, 0, &payload)) {
        const char *perr = NULL;
        Dyn *msg = dyn_parse_bytes(turn, payload, &perr);
        if (!msg) {
          put_str_field(rsp, RSP_PARSE_ERROR, perr ? perr : "malformed wire data");
        } else if (!dyn_validate_packed(msg, d)) {
          put_str_field(rsp, RSP_PARSE_ERROR, "truncated packed field value");
        } else if (out_fmt == WF_PROTOBUF) {
          dyn_normalize_wire(msg, d);
            dyn_put_bytes(rsp, RSP_PROTOBUF_PAYLOAD, dyn_serialize(msg, turn));
        } else if (out_fmt == WF_JSON) {
          if (!wkt_range_ok(msg, d))
            put_str_field(rsp, RSP_SERIALIZE_ERROR,
                          "Timestamp/Duration value out of range");
          else
            dyn_put_bytes(rsp, RSP_JSON_PAYLOAD, json_write(turn, msg, d));
        } else {
          /* Text-format output belongs to a later milestone. */
          put_str_field(rsp, RSP_SKIPPED, "output format not supported");
        }
      } else if (dyn_get_bytes(req, REQ_JSON_PAYLOAD, 0, &payload)) {
        const char *jerr = NULL;
        Dyn *msg = json_parse(turn, payload, d, &jerr);
        if (!msg) {
          put_str_field(rsp, RSP_PARSE_ERROR, jerr ? jerr : "malformed JSON");
        } else if (out_fmt == WF_PROTOBUF) {
          dyn_normalize_wire(msg, d);
            dyn_put_bytes(rsp, RSP_PROTOBUF_PAYLOAD, dyn_serialize(msg, turn));
        } else if (out_fmt == WF_JSON) {
          if (!wkt_range_ok(msg, d))
            put_str_field(rsp, RSP_SERIALIZE_ERROR, "value out of range");
          else
            dyn_put_bytes(rsp, RSP_JSON_PAYLOAD, json_write(turn, msg, d));
        } else {
          put_str_field(rsp, RSP_SKIPPED, "output format not supported");
        }
      } else {
        put_str_field(rsp, RSP_SKIPPED, "input format not supported");
      }
    }

    Str out = dyn_serialize(rsp, turn);
    uint32_t olen = (uint32_t)out.n;
    write_exact(1, &olen, 4);
    write_exact(1, out.p, out.n);
    arena_free(turn);
  }
  return 0;
}
