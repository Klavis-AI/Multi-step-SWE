/* build.h -- AST -> FileDescriptorProto.
 *
 * This is the stage that turns semantic-free syntax into the exact bytes
 * protoc emits: it resolves type names under C++ scoping rules, synthesises
 * map-entry messages and proto3 synthetic oneofs, computes json_name, formats
 * default values as strings, and fills the dependency index lists.
 */
#ifndef PB_BUILD_H
#define PB_BUILD_H

#include "pb.h"
#include "ast.h"
#include "wire.h"
#include "desc.h"

typedef struct Sym Sym;

/* A build context accumulates the files compiled so far. Type resolution in a
 * later file searches the symbols every earlier file exported, which is what
 * makes cross-file and cross-era imports work. */
typedef struct BuildCtx {
  Arena *a;
  Diags *d;
  Sym *syms;        /* flat list of fully-qualified symbols */
  Pool *pool;       /* descriptor.proto + every file built so far */
  bool bootstrapped;
  bool no_options;  /* set while building descriptor.proto itself */
  bool retain_options; /* mirrors protoc's --retain_options */
  bool quiet;          /* pass one: interpret, but report nothing */
  const char *cur_file; /* the file being built, for diagnostics */
  struct BuiltFile *files;
  int n_files;
} BuildCtx;

typedef struct BuiltFile {
  const char *name;          /* the import path, e.g. "a/b.proto" */
  const AstFile *ast;
  Dyn *fdp;                  /* the FileDescriptorProto */
  struct BuiltFile *next;
} BuiltFile;

BuildCtx *build_ctx_new(Arena *a, Diags *d);

/* Builds one file's FileDescriptorProto. `name` is the import path as it must
 * appear in FileDescriptorProto.name. Returns NULL only on a hard error;
 * ordinary problems are reported through the context's Diags. */
Dyn *build_file(BuildCtx *ctx, const AstFile *f, const char *name);

/* Looks up a fully-qualified name (no leading dot). Returns 0 if unknown,
 * T_MESSAGE if it names a message, T_ENUM if it names an enum. */
int build_lookup(const BuildCtx *ctx, const char *fqn);

#endif /* PB_BUILD_H */
