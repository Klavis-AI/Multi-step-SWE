/* protoc_main.c -- the protoc-compatible command line.
 *
 * The interface is protoc's own, as published by `protoc --help` for the
 * pinned release: every flag, its spelling and its semantics come from that
 * document.
 *
 * Import resolution mirrors protoc's: each -I root is searched in order, and
 * a path that matches a bundled well-known type resolves to the embedded copy
 * so that `import "google/protobuf/descriptor.proto"` works with no -I
 * pointing at a distribution. That embedding is also what ties the compiler to
 * one protoc release: the WKT sources change between majors.
 */
#include "pb.h"
#include "ast.h"
#include "build.h"
#include "wire.h"
#include "wkt.h"
#include "names.h"
#include "desc.h"
#include "textfmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define PROTOC_VERSION "32.1"

typedef struct StrList { const char *s; struct StrList *next; } StrList;

typedef struct {
  StrList *proto_path, *inputs;
  const char *descriptor_set_out;
  const char *dependency_out;
  const char *encode, *decode;
  bool decode_raw, include_imports, include_source_info, retain_options;
  bool deterministic_output, fatal_warnings, print_free_field_numbers;
  bool version, help;
  const char *error_format;
  StrList *plugins, *gen_out;
} Opts;

static void list_push(Arena *a, StrList **head, const char *s) {
  StrList *n = arena_calloc(a, sizeof(StrList));
  n->s = s;
  StrList **p = head;
  while (*p) p = &(*p)->next;
  *p = n;
}

/* ------------------------------------------------------------ file loading */

typedef struct Loaded {
  const char *name;          /* import path, as it appears in the descriptor */
  Str src;
  AstFile *ast;
  bool done;                 /* false while its imports are still being walked */
  struct Loaded *next;       /* output order: dependencies first */
  struct Loaded *next_seen;  /* every file reached, in-progress ones included */
} Loaded;

static Str read_whole(Arena *a, const char *path, bool *ok) {
  FILE *f = fopen(path, "rb");
  if (!f) { *ok = false; return str_of("", 0); }
  Buf b; buf_init(&b, a);
  char tmp[65536];
  size_t n;
  while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(&b, tmp, n);
  fclose(f);
  *ok = true;
  return buf_str(&b);
}

/* Resolves an import path against the -I roots, then against the embedded
 * well-known types. Returns the source, or ok=false when not found. */
static Str resolve_source(Arena *a, const Opts *o, const char *name, bool *ok) {
  for (StrList *p = o->proto_path; p; p = p->next) {
    char *full = arena_printf(a, "%s/%s", p->s, name);
    Str s = read_whole(a, full, ok);
    if (*ok) return s;
  }
  Str direct = read_whole(a, name, ok);
  if (*ok) return direct;
  const WktFile *w = wkt_lookup(name);
  if (w) { *ok = true; return str_of(w->text, w->len); }
  *ok = false;
  return str_of("", 0);
}

static Loaded *load_recursive(Arena *a, Diags *d, const Opts *o, Loaded **all,
                              Loaded **seen, const char *name) {
  for (Loaded *l = *seen; l; l = l->next_seen)
    if (strcmp(l->name, name) == 0) {
      if (!l->done) {
        diag_add(d, name, 0, 0, "File recursively imports itself: %s", name);
        return NULL;
      }
      return l;
    }

  bool ok = false;
  Str src = resolve_source(a, o, name, &ok);
  if (!ok) {
    diag_add(d, name, 0, 0, "File not found.");
    return NULL;
  }
  Loaded *l = arena_calloc(a, sizeof(Loaded));
  l->name = arena_strdup(a, name);
  l->src = src;
  /* Visible to cycle detection BEFORE its imports are walked. */
  l->next_seen = *seen;
  *seen = l;
  l->ast = ast_parse(a, d, l->name, src);

  /* Dependencies first: the set is emitted in topological order, and
   * resolution needs imported symbols to already exist. */
  if (l->ast)
    for (const AstImport *im = l->ast->imports; im; im = im->next) {
      char *dep = arena_strndup(a, im->path.p, im->path.n);
      if (!load_recursive(a, d, o, all, seen, dep))
        diag_add(d, l->name, im->line, im->col,
                 "Import \"%.*s\" was not found or had errors.",
                 (int)im->path.n, im->path.p);
    }

  /* Appended after its dependencies, so the output list is post-order and
   * --include_imports emits dependencies first, as protoc does. */
  Loaded **tail = all;
  while (*tail) tail = &(*tail)->next;
  *tail = l;
  l->done = true;
  return l;
}

/* ------------------------------------------------------------------- main */

static void print_version(void) { printf("libprotoc %s\n", PROTOC_VERSION); }

/* The path a name resolved to, which is what --dependency_out prints:
 * absolute when found under an include directory, relative when found locally. */
static const char *resolved_path(Arena *a, const Opts *o, const char *name) {
  for (StrList *p = o->proto_path; p; p = p->next) {
    bool here = (p->s[0] == '.' && p->s[1] == '\0');
    char *full = here ? (char *)name : arena_printf(a, "%s/%s", p->s, name);
    FILE *f = fopen(full, "rb");
    if (f) { fclose(f); return full; }
  }
  return name;
}

static void free_fields_for(Arena *a, const AstMessage *m, const char *scope);

static void print_free_fields(Arena *a, const AstMessage *m, const char *scope) {
  /* protoc prints nested types before their parent, and a map field's
   * synthesized entry message is a nested type like any other -- it has
   * fields 1 and 2 occupied, so it reports `free: 3-INF`. */
  for (const AstMessage *x = m; x; x = x->next) {
    const char *fq = scope && *scope
        ? arena_printf(a, "%s.%.*s", scope, (int)x->name.n, x->name.p)
        : arena_strndup(a, x->name.p, x->name.n);
    typedef struct { long key; const AstMessage *msg; const AstField *map_field; } Slot;
    int ns = 0;
    for (const AstMessage *n = x->messages; n; n = n->next) ns++;
    for (const AstField *fl = x->fields; fl; fl = fl->next)
      if (fl->kind == AST_FIELD_MAP) ns++;
    Slot *slots = arena_calloc(a, (size_t)(ns > 0 ? ns : 1) * sizeof(Slot));
    int k = 0;
    for (const AstMessage *n = x->messages; n; n = n->next) {
      slots[k].key = (long)n->line * 100000L + n->col; slots[k].msg = n; k++;
    }
    for (const AstField *fl = x->fields; fl; fl = fl->next)
      if (fl->kind == AST_FIELD_MAP) {
        slots[k].key = (long)fl->line * 100000L + fl->col; slots[k].map_field = fl; k++;
      }
    for (int i = 1; i < k; i++) {
      Slot t = slots[i]; int j = i - 1;
      while (j >= 0 && slots[j].key > t.key) { slots[j+1] = slots[j]; j--; }
      slots[j+1] = t;
    }
    for (int i = 0; i < k; i++) {
      if (slots[i].msg) {
        AstMessage one = *slots[i].msg;
        one.next = NULL;
        print_free_fields(a, &one, fq);
      } else {
        const AstField *fl = slots[i].map_field;
        char *nm = arena_strndup(a, fl->name.p, fl->name.n);
        printf("%-35s free: 3-INF\n",
               arena_printf(a, "%s.%s", fq, map_entry_name_of(a, nm)));
      }
    }
    free_fields_for(a, x, fq);
  }
}

static void free_fields_for(Arena *a, const AstMessage *m, const char *fq) {
  /* Collect used numbers: fields, extension ranges and reserved ranges. */
  enum { MAXN = 4096 };
  int64_t lo[MAXN], hi[MAXN];
  int n = 0;
  for (const AstField *f = m->fields; f && n < MAXN; f = f->next)
    if (f->has_number) { lo[n] = (int64_t)f->number; hi[n] = lo[n]; n++; }
  for (const AstExtRange *r = m->ext_ranges; r; r = r->next)
    for (const AstRange *g = r->ranges; g && n < MAXN; g = g->next) {
      lo[n] = g->start;
      hi[n] = g->has_end ? (g->end_is_max ? 536870911 : g->end) : g->start;
      n++;
    }
  for (const AstReserved *rr = m->reserved; rr; rr = rr->next)
    for (const AstRange *g = rr->ranges; g && n < MAXN; g = g->next) {
      lo[n] = g->start;
      hi[n] = g->has_end ? (g->end_is_max ? 536870911 : g->end) : g->start;
      n++;
    }
  for (int i = 1; i < n; i++) {
    int64_t a1 = lo[i], b1 = hi[i];
    int j = i - 1;
    while (j >= 0 && lo[j] > a1) { lo[j+1] = lo[j]; hi[j+1] = hi[j]; j--; }
    lo[j+1] = a1; hi[j+1] = b1;
  }
  Buf b; buf_init(&b, a);
  int64_t next = 1;
  for (int i = 0; i < n; i++) {
    if (lo[i] > next) {
      if (lo[i] - 1 == next) buf_printf(&b, " %lld", (long long)next);
      else buf_printf(&b, " %lld-%lld", (long long)next, (long long)(lo[i] - 1));
    }
    if (hi[i] + 1 > next) next = hi[i] + 1;
  }
  if (next <= 536870911) buf_printf(&b, " %lld-INF", (long long)next);
  Str s = buf_str(&b);
  printf("%-35s free:%.*s\n", fq, (int)s.n, s.p);
}

static void emit_diags(const Diags *d, const char *fmt) {
  bool msvs = fmt && strcmp(fmt, "msvs") == 0;
  for (Diag *x = d->head; x; x = x->next) {
    if (msvs)
      fprintf(stderr, "%s(%d) : error in column=%d: %s\n", x->file, x->line + 1,
              x->col + 1, x->msg);
    else
      fprintf(stderr, "%s:%d:%d: %s\n", x->file, x->line + 1, x->col + 1, x->msg);
  }
}

/* ----------------------------------------------------- code generation */
/* protoc drives a generator as a subprocess: a CodeGeneratorRequest goes in on
 * stdin, a CodeGeneratorResponse comes back on stdout.
 *
 * Field numbers and layout were read off a request captured from protoc 32.1.
 * Two of them are not what they look like:
 *   - proto_file carries the whole import closure, dependencies first;
 *     source_file_descriptors carries ONLY the files named on the command line.
 *     A single-file capture cannot tell those apart -- the two-file one can.
 *   - compiler_version is the LIBRARY version 6.32.1, not the 32.1 on the tin,
 *     and its suffix is present as an empty string. */
#define CGR_FILE_TO_GENERATE  1
#define CGR_PARAMETER         2
#define CGR_COMPILER_VERSION  3
#define CGR_PROTO_FILE       15
#define CGR_SOURCE_FDS       17
#define CGRESP_ERROR          1
#define CGRESP_FILE          15
#define CGRESP_FILE_NAME      1
#define CGRESP_FILE_CONTENT  15

static bool mkdirs_for(const char *path) {
  char buf[4096];
  size_t n = strlen(path);
  if (n >= sizeof buf) return false;
  memcpy(buf, path, n + 1);
  for (char *q = buf + 1; *q; q++) {
    if (*q != '/') continue;
    *q = '\0';
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) return false;
    *q = '/';
  }
  return true;
}

/* protoc's rule: --<name>_out looks for an executable protoc-gen-<name>,
 * overridable by --plugin=protoc-gen-<name>=PATH. */
static const char *plugin_path_for(Arena *a, const Opts *o, const char *name) {
  char want[256];
  snprintf(want, sizeof want, "protoc-gen-%s", name);
  size_t wn = strlen(want);
  for (StrList *pl = o->plugins; pl; pl = pl->next) {
    const char *eq = strchr(pl->s, '=');
    if (!eq) continue;
    if ((size_t)(eq - pl->s) == wn && strncmp(pl->s, want, wn) == 0) return eq + 1;
  }
  char *dup = arena_calloc(a, wn + 1);
  memcpy(dup, want, wn + 1);
  return dup;                      /* resolved through PATH by execvp */
}

static Str build_cg_request(Arena *a, const Opts *o, Loaded *all, BuildCtx *ctx,
                            const char *parameter) {
  Dyn *req = dyn_new(a);
  for (StrList *in = o->inputs; in; in = in->next)
    dyn_put_str(req, CGR_FILE_TO_GENERATE, in->s);
  if (parameter) dyn_put_str(req, CGR_PARAMETER, parameter);

  Dyn *ver = dyn_put_msg(req, CGR_COMPILER_VERSION);
  dyn_put_varint(ver, 1, 6);
  dyn_put_varint(ver, 2, 32);
  dyn_put_varint(ver, 3, 1);
  dyn_put_str(ver, 4, "");

  for (Loaded *l = all; l; l = l->next)
    for (BuiltFile *bf = ctx->files; bf; bf = bf->next)
      if (strcmp(bf->name, l->name) == 0) {
        dyn_put_bytes(req, CGR_PROTO_FILE, dyn_serialize(bf->fdp, a));
        break;
      }
  for (StrList *in = o->inputs; in; in = in->next)
    for (BuiltFile *bf = ctx->files; bf; bf = bf->next)
      if (strcmp(bf->name, in->s) == 0) {
        dyn_put_bytes(req, CGR_SOURCE_FDS, dyn_serialize(bf->fdp, a));
        break;
      }
  return dyn_serialize(req, a);
}

static bool run_plugin(Arena *a, const char *exe, Str req, Str *resp,
                       const char **err) {
  int inp[2], outp[2];
  if (pipe(inp) != 0) { *err = "pipe failed"; return false; }
  if (pipe(outp) != 0) { close(inp[0]); close(inp[1]); *err = "pipe failed"; return false; }
  pid_t pid = fork();
  if (pid < 0) { *err = "fork failed"; return false; }
  if (pid == 0) {
    dup2(inp[0], 0);
    dup2(outp[1], 1);
    close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
    char *av[2]; av[0] = (char *)exe; av[1] = NULL;
    execvp(exe, av);
    _exit(127);
  }
  close(inp[0]); close(outp[1]);

  /* The protocol has the generator read its whole request before answering, so
   * write-then-read cannot deadlock against a conforming plugin. */
  const char *q = req.p; size_t left = req.n;
  while (left) {
    ssize_t w = write(inp[1], q, left);
    if (w <= 0) break;
    q += w; left -= (size_t)w;
  }
  close(inp[1]);

  Buf out; buf_init(&out, a);
  char tmp[65536]; ssize_t n;
  while ((n = read(outp[0], tmp, sizeof tmp)) > 0) buf_put(&out, tmp, (size_t)n);
  close(outp[0]);

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) { *err = "waitpid failed"; return false; }
  if (!WIFEXITED(st)) { *err = "plugin died on a signal"; return false; }
  if (WEXITSTATUS(st) == 127) { *err = "plugin not found or not executable"; return false; }
  if (WEXITSTATUS(st) != 0) { *err = "plugin exited non-zero"; return false; }
  *resp = buf_str(&out);
  return true;
}

static bool write_cg_response(Arena *a, Str resp, const char *dir,
                              const char **err) {
  static char msg[512];
  const char *perr = NULL;
  Dyn *r = dyn_parse_bytes(a, resp, &perr);
  if (!r) { *err = perr ? perr : "unparsable CodeGeneratorResponse"; return false; }
  Str e;
  if (dyn_get_bytes(r, CGRESP_ERROR, 0, &e) && e.n) {
    snprintf(msg, sizeof msg, "%.*s", (int)e.n, (const char *)e.p);
    *err = msg;
    return false;
  }
  for (int i = 0; ; i++) {
    Dyn *f = dyn_get_msg(r, CGRESP_FILE, i);
    if (!f) break;
    Str name, content;
    if (!dyn_get_bytes(f, CGRESP_FILE_NAME, 0, &name)) continue;
    bool has = dyn_get_bytes(f, CGRESP_FILE_CONTENT, 0, &content);
    char path[4096];
    snprintf(path, sizeof path, "%s/%.*s", dir, (int)name.n, (const char *)name.p);
    if (!mkdirs_for(path)) { *err = "cannot create output directory"; return false; }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
      snprintf(msg, sizeof msg, "cannot write %s", path);
      *err = msg; return false;
    }
    if (has && content.n) fwrite(content.p, 1, content.n, fp);
    fclose(fp);
  }
  return true;
}

static int run_codegen(Arena *a, const Opts *o, Loaded *all, BuildCtx *ctx) {
  for (StrList *g = o->gen_out; g; g = g->next) {
    const char *eq = strchr(g->s, '=');
    size_t nlen = eq ? (size_t)(eq - g->s) : 0;
    char name[128];
    if (!eq || nlen <= 4 || strncmp(eq - 4, "_out", 4) != 0 ||
        (size_t)(nlen - 4) >= sizeof name) {
      fprintf(stderr, "Unknown flag: --%s\n", g->s);
      return 1;
    }
    nlen -= 4;
    memcpy(name, g->s, nlen); name[nlen] = '\0';

    /* "opts:dir" splits at the LAST colon; with no colon the whole value is the
     * directory and no parameter is sent at all. */
    const char *val = eq + 1, *dir = val, *parameter = NULL;
    const char *colon = strrchr(val, ':');
    if (colon) {
      size_t plen = (size_t)(colon - val);
      char *pb = arena_calloc(a, plen + 1);
      memcpy(pb, val, plen);
      parameter = pb;
      dir = colon + 1;
    }

    Str req = build_cg_request(a, o, all, ctx, parameter);
    const char *exe = plugin_path_for(a, o, name);
    Str resp; const char *err = NULL;
    if (!run_plugin(a, exe, req, &resp, &err)) {
      fprintf(stderr, "--%s_out: %s: %s\n", name, exe, err);
      return 1;
    }
    if (!write_cg_response(a, resp, dir, &err)) {
      fprintf(stderr, "--%s_out: %s\n", name, err);
      return 1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  Arena *a = arena_new();
  Opts o;
  memset(&o, 0, sizeof o);
  o.error_format = "gcc";

  /* `@file` expands to one argument per line, in place. Done before the option
   * loop so an argfile can carry any flag. */
  int eargc = 0;
  char **eargv = arena_alloc(a, sizeof(char *) * 4096);
  for (int i = 1; i < argc && eargc < 4090; i++) {
    if (argv[i][0] != '@' || !argv[i][1]) { eargv[eargc++] = argv[i]; continue; }
    FILE *af = fopen(argv[i] + 1, "rb");
    if (!af) { fprintf(stderr, "%s: could not open argument file\n", argv[i] + 1); return 1; }
    char line[4096];
    while (fgets(line, sizeof line, af) && eargc < 4090) {
      size_t n = strlen(line);
      while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
      if (!n) continue;
      eargv[eargc++] = arena_strdup(a, line);
    }
    fclose(af);
  }

  for (int i = 0; i < eargc; i++) {
    const char *s = eargv[i];
    if (strncmp(s, "-I", 2) == 0 && s[2]) list_push(a, &o.proto_path, s + 2);
    else if (strcmp(s, "-I") == 0 && i + 1 < eargc) list_push(a, &o.proto_path, eargv[++i]);
    else if (strncmp(s, "--proto_path=", 13) == 0) list_push(a, &o.proto_path, s + 13);
    else if (strncmp(s, "--descriptor_set_out=", 21) == 0) o.descriptor_set_out = s + 21;
    else if (strncmp(s, "-o", 2) == 0 && s[2]) o.descriptor_set_out = s + 2;
    else if (strcmp(s, "-o") == 0 && i + 1 < eargc) o.descriptor_set_out = eargv[++i];
    else if (strncmp(s, "--dependency_out=", 17) == 0) o.dependency_out = s + 17;
    else if (strcmp(s, "--include_imports") == 0) o.include_imports = true;
    else if (strcmp(s, "--include_source_info") == 0) o.include_source_info = true;
    else if (strcmp(s, "--retain_options") == 0) o.retain_options = true;
    else if (strcmp(s, "--deterministic_output") == 0) o.deterministic_output = true;
    else if (strcmp(s, "--fatal_warnings") == 0) o.fatal_warnings = true;
    else if (strcmp(s, "--print_free_field_numbers") == 0) o.print_free_field_numbers = true;
    else if (strcmp(s, "--decode_raw") == 0) o.decode_raw = true;
    else if (strncmp(s, "--encode=", 9) == 0) o.encode = s + 9;
    else if (strncmp(s, "--decode=", 9) == 0) o.decode = s + 9;
    else if (strncmp(s, "--error_format=", 15) == 0) o.error_format = s + 15;
    else if (strcmp(s, "--version") == 0) o.version = true;
    else if (strcmp(s, "-h") == 0 || strcmp(s, "--help") == 0) o.help = true;
    else if (strncmp(s, "--plugin=", 9) == 0) list_push(a, &o.plugins, s + 9);
    /* Any --<name>_out=... that the known flags above did not claim is a
     * code generator invocation. Stored with the leading "--" stripped. */
    else if (s[0] == '-' && s[1] == '-' && strstr(s, "_out="))
      list_push(a, &o.gen_out, s + 2);
    else if (s[0] == '-' && s[1]) {
      fprintf(stderr, "Unknown flag: %s\n", s);
      return 1;
    } else {
      list_push(a, &o.inputs, s);
    }
  }

  if (o.version) { print_version(); return 0; }

  Diags d;
  diags_init(&d, a);
  Loaded *all = NULL, *seen = NULL;
  for (StrList *in = o.inputs; in; in = in->next)
    load_recursive(a, &d, &o, &all, &seen, in->s);

  if (d.n) { emit_diags(&d, o.error_format); return 1; }

  BuildCtx *ctx = build_ctx_new(a, &d);
  /* Two passes. A file may declare a custom option and then use it, so its own
   * extensions have to be in the pool before its options can be interpreted.
   * Pass one registers structure with interpretation off; pass two re-emits
   * with every extension resolvable. */
  ctx->retain_options = o.retain_options;
  ctx->quiet = true;
  for (Loaded *l = all; l; l = l->next)
    if (l->ast) {
      Dyn *fd0 = build_file(ctx, l->ast, l->name);
      if (fd0) {
        const char *perr0 = NULL;
        pool_add_serialized(ctx->pool, dyn_serialize(fd0, a), &perr0);
      }
    }
  ctx->quiet = false;
  ctx->files = NULL;
  ctx->n_files = 0;
  for (Loaded *l = all; l; l = l->next)
    if (l->ast) build_file(ctx, l->ast, l->name);

  if (d.n) { emit_diags(&d, o.error_format); return 1; }

  /* Code generators run before the descriptor/encode paths; a run that asked
   * for nothing else is complete once they finish. */
  if (o.gen_out) {
    if (run_codegen(a, &o, all, ctx) != 0) return 1;
    if (!o.descriptor_set_out && !o.encode && !o.decode &&
        !o.print_free_field_numbers)
      return 0;
  }

  /* --decode_raw needs no schema at all, so it short-circuits everything. */
  if (o.decode_raw) {
    Buf in; buf_init(&in, a);
    char tmp[65536]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, stdin)) > 0) buf_put(&in, tmp, n);
    Str out = tf_decode_raw(a, buf_str(&in));
    fwrite(out.p, 1, out.n, stdout);
    return 0;
  }

  /* --encode / --decode drive the message through the resolved descriptor
   * graph, so the compiled files have to be loaded into a pool first. */
  if (o.encode || o.decode) {
    Pool *pool = pool_new(a);
    const char *perr = NULL;
    for (Loaded *l = all; l; l = l->next)
      for (BuiltFile *bf = ctx->files; bf; bf = bf->next)
        if (strcmp(bf->name, l->name) == 0) {
          pool_add_serialized(pool, dyn_serialize(bf->fdp, a), &perr);
          break;
        }
    const char *type = o.encode ? o.encode : o.decode;
    const Desc *d2 = pool_find_message(pool, type);
    if (!d2) {
      fprintf(stderr, "Type not defined: %s\n", type);
      return 1;
    }
    Buf in; buf_init(&in, a);
    char tmp[65536]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, stdin)) > 0) buf_put(&in, tmp, n);

    if (o.encode) {
      Diags td; diags_init(&td, a);
      Dyn *m = tf_parse(a, &td, "input", buf_str(&in), d2, pool);
      if (!m || td.n) { emit_diags(&td, o.error_format); return 1; }
      Str out = dyn_serialize(m, a);
      fwrite(out.p, 1, out.n, stdout);
    } else {
      const char *derr = NULL;
      Dyn *m = dyn_parse_bytes(a, buf_str(&in), &derr);
      if (!m) {
        fprintf(stderr, "Failed to parse input: %s\n", derr ? derr : "malformed");
        return 1;
      }
      Str out = tf_write(a, m, d2, pool);
      fwrite(out.p, 1, out.n, stdout);
    }
    return 0;
  }

  if (o.print_free_field_numbers) {
    /* protoc walks every message, nested types included, and prints the gaps
     * between used field numbers. Extension ranges count as occupied. */
    for (Loaded *l = all; l; l = l->next) {
      bool named = false;
      for (StrList *in = o.inputs; in; in = in->next)
        if (strcmp(in->s, l->name) == 0) named = true;
      if (!named || !l->ast) continue;
      const char *pkg = l->ast->has_package
          ? arena_strndup(a, l->ast->package.p, l->ast->package.n) : "";
      print_free_fields(a, l->ast->messages, pkg);
    }
    return 0;
  }

  if (o.dependency_out) {
    /* A make rule: the target is the output file, the prerequisites are the
     * dependency paths as resolved through -I, joined with backslash
     * continuations. */
    FILE *df = fopen(o.dependency_out, "wb");
    if (!df) { fprintf(stderr, "%s: could not open\n", o.dependency_out); return 1; }
    fprintf(df, "%s:", o.descriptor_set_out ? o.descriptor_set_out : "");
    int count = 0, total = 0;
    for (Loaded *l = all; l; l = l->next) total++;
    for (Loaded *l = all; l; l = l->next) {
      fprintf(df, "%s %s", count ? "\\\n" : "", resolved_path(a, &o, l->name));
      count++;
    }
    fclose(df);
  }

  if (o.descriptor_set_out) {
    /* A FileDescriptorSet is `repeated FileDescriptorProto file = 1`. Without
     * --include_imports only the files named on the command line appear; with
     * it, every transitive dependency does, dependencies first. */
    Dyn *set = dyn_new(a);
    for (Loaded *l = all; l; l = l->next) {
      bool named = false;
      for (StrList *in = o.inputs; in; in = in->next)
        if (strcmp(in->s, l->name) == 0) named = true;
      if (!o.include_imports && !named) continue;
      for (BuiltFile *bf = ctx->files; bf; bf = bf->next)
        if (strcmp(bf->name, l->name) == 0) {
          Str bytes = dyn_serialize(bf->fdp, a);
          dyn_put_bytes(set, 1, bytes);
          break;
        }
    }
    Str out = dyn_serialize(set, a);
    FILE *f = strcmp(o.descriptor_set_out, "-") == 0 ? stdout
                                                     : fopen(o.descriptor_set_out, "wb");
    if (!f) {
      fprintf(stderr, "%s: could not open for writing\n", o.descriptor_set_out);
      return 1;
    }
    fwrite(out.p, 1, out.n, f);
    if (f != stdout) fclose(f);
  }

  return 0;
}
