/* pb.h — shared contract for the protobuf/gRPC implementation.
 *
 * Layering (each depends only on the ones above it):
 *   arena/str   : allocation + byte strings
 *   wire        : varint/fixed/length-delimited codec over byte strings
 *   dyn         : a dynamic message: values keyed by field number, driven by
 *                 a Desc* rather than by generated structs
 *   lex/parse   : .proto source -> Ast
 *   build       : Ast -> FileDescriptorProto (as a dyn message) + symbol table
 *   desc        : resolved descriptor graph used by every consumer
 *
 * The compiler is self-hosting: it parses its own bundled
 * google/protobuf/descriptor.proto at startup and uses the resulting
 * descriptors to serialize every FileDescriptorProto it emits. There is no
 * embedded binary schema blob anywhere in the tree.
 */
#ifndef PB_H
#define PB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ------------------------------------------------------------------ arena */
typedef struct Arena Arena;
Arena *arena_new(void);
void  *arena_alloc(Arena *a, size_t n);
void  *arena_calloc(Arena *a, size_t n);
char  *arena_strdup(Arena *a, const char *s);
char  *arena_strndup(Arena *a, const char *s, size_t n);
char  *arena_printf(Arena *a, const char *fmt, ...);
void   arena_free(Arena *a);

/* ------------------------------------------------------------------ bytes */
typedef struct { const char *p; size_t n; } Str;
static inline Str str_of(const char *p, size_t n) { Str s; s.p = p; s.n = n; return s; }
Str  str_cstr(const char *s);
bool str_eq(Str a, Str b);
bool str_eq_c(Str a, const char *b);

typedef struct { char *p; size_t n, cap; Arena *a; } Buf;
void buf_init(Buf *b, Arena *a);
void buf_put(Buf *b, const void *p, size_t n);
void buf_byte(Buf *b, uint8_t c);
void buf_cstr(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...);
Str  buf_str(const Buf *b);

/* ------------------------------------------------------------------- wire */
enum { WT_VARINT = 0, WT_I64 = 1, WT_LEN = 2, WT_SGROUP = 3, WT_EGROUP = 4, WT_I32 = 5 };

void wire_varint(Buf *b, uint64_t v);
void wire_tag(Buf *b, uint32_t field, int wt);
void wire_i32(Buf *b, uint32_t v);
void wire_i64(Buf *b, uint64_t v);
void wire_bytes(Buf *b, uint32_t field, Str s);
static inline uint64_t zigzag_enc(int64_t v) { return ((uint64_t)v << 1) ^ (uint64_t)(v >> 63); }
static inline int64_t  zigzag_dec(uint64_t v) { return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }

typedef struct { const uint8_t *p, *end; const char *err; } Rd;
void rd_init(Rd *r, Str s);
bool rd_varint(Rd *r, uint64_t *out);
bool rd_i32(Rd *r, uint32_t *out);
bool rd_i64(Rd *r, uint64_t *out);
bool rd_bytes(Rd *r, Str *out);
bool rd_skip(Rd *r, int wt, uint32_t field);
static inline bool rd_done(const Rd *r) { return r->p >= r->end; }

/* ------------------------------------------------------------- descriptors */
typedef struct Desc Desc;       /* message descriptor  */
typedef struct FieldD FieldD;   /* field descriptor    */
typedef struct EnumD EnumD;     /* enum descriptor     */
typedef struct FileD FileD;     /* file descriptor     */
typedef struct Pool Pool;       /* descriptor pool     */

/* FieldDescriptorProto.Type, values are the wire values in descriptor.proto */
enum {
  T_DOUBLE = 1, T_FLOAT = 2, T_INT64 = 3, T_UINT64 = 4, T_INT32 = 5,
  T_FIXED64 = 6, T_FIXED32 = 7, T_BOOL = 8, T_STRING = 9, T_GROUP = 10,
  T_MESSAGE = 11, T_BYTES = 12, T_UINT32 = 13, T_ENUM = 14, T_SFIXED32 = 15,
  T_SFIXED64 = 16, T_SINT32 = 17, T_SINT64 = 18
};
/* FieldDescriptorProto.Label */
enum { L_OPTIONAL = 1, L_REQUIRED = 2, L_REPEATED = 3 };

/* Resolved feature values (editions). proto2/proto3 resolve to these too, so
 * every consumer asks the feature, never the syntax. */
enum { FP_EXPLICIT = 1, FP_IMPLICIT = 2, FP_LEGACY_REQUIRED = 3 };
enum { ET_OPEN = 1, ET_CLOSED = 2 };
enum { RFE_PACKED = 1, RFE_EXPANDED = 2 };
enum { U8_VERIFY = 2, U8_NONE = 3 };
enum { ME_LENGTH_PREFIXED = 1, ME_DELIMITED = 2 };
enum { JF_ALLOW = 1, JF_LEGACY_BEST_EFFORT = 2 };

typedef struct {
  int field_presence, enum_type, repeated_field_encoding;
  int utf8_validation, message_encoding, json_format;
  int enforce_naming_style, default_symbol_visibility;
} Features;

/* Era of a file, from `syntax`/`edition`. EDITION_* values match descriptor.proto. */
enum { ED_LEGACY = 900, ED_PROTO2 = 998, ED_PROTO3 = 999, ED_2023 = 1000, ED_2024 = 1001 };

/* ------------------------------------------------------------ dyn message */
/* The dynamic message API lives in wire.h. It is deliberately schema-free so
 * that the bootstrap path -- emitting descriptor.proto's own
 * FileDescriptorProto before any descriptor exists -- can use it. Only the
 * forward declaration is needed here, for pool_add_file below. */
typedef struct Dyn Dyn;

/* ------------------------------------------------------------------- pool */
Pool       *pool_new(Arena *a);
const FileD *pool_add_file(Pool *p, Dyn *fdproto, const char **err);
const Desc  *pool_find_message(const Pool *p, const char *fullname);
const EnumD *pool_find_enum(const Pool *p, const char *fullname);

/* ----------------------------------------------------------------- errors */
/* Compiler diagnostics carry file/line/column so --error_format can render
 * them; line and column are 0-based internally and printed 1-based. */
typedef struct Diag { const char *file; int line, col; const char *msg; struct Diag *next; } Diag;
typedef struct { Diag *head, **tail; Arena *a; int n; } Diags;
void diags_init(Diags *d, Arena *a);
void diag_add(Diags *d, const char *file, int line, int col, const char *fmt, ...);

#endif /* PB_H */
