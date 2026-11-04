/* ast.h -- tokens and the .proto abstract syntax tree.
 *
 * The lexer and parser in this layer are deliberately *semantic-free*:
 *   - no type-name resolution,
 *   - no default-value interpretation,
 *   - no option interpretation (options stay as name-parts plus a literal or
 *     an aggregate text-format tree),
 *   - no synthesis of map-entry messages or proto3 synthetic oneofs.
 * All of that belongs to the `build` stage.  What this layer guarantees is
 * that the grammar of proto2, proto3, edition 2023 and edition 2024 is
 * accepted exactly, and that anything else yields a diagnostic carrying a
 * 0-based line/column instead of a crash.
 */
#ifndef PB_AST_H
#define PB_AST_H

#include "pb.h"

/* ================================================================= tokens */

typedef enum {
  TK_EOF = 0,
  TK_IDENT, /* [A-Za-z_][A-Za-z0-9_]*                                      */
  TK_INT,   /* decimal / 0octal / 0xhex; never carries a sign              */
  TK_FLOAT, /* 1.5  1.  .5  1e9  1.5E-3                                    */
  TK_STR,   /* "..." or '...'; sval holds the decoded bytes                */
  TK_SYM    /* any other single printable character                        */
} TokKind;

typedef struct {
  TokKind kind;
  int line, col; /* 0-based; printed 1-based */
  Str text;      /* exact source spelling, quotes included for TK_STR */
  Str sval;      /* TK_STR: decoded bytes */
  uint64_t ival; /* TK_INT: value */
  double fval;   /* TK_FLOAT: value */
  char sym;      /* TK_SYM: the character */
  bool ovf;      /* TK_INT: literal does not fit in a uint64_t */
} Token;

typedef struct {
  Token *v;
  size_t n; /* always >= 1; v[n-1] is a TK_EOF sentinel */
} Tokens;

/* Tokenizes all of `src`.  Lexical errors go to `d`; a token stream is still
 * produced so the parser can continue and report more. */
Tokens lex_file(Arena *a, Diags *d, const char *file, Str src);

const char *tok_kind_name(TokKind k);

/* ============================================================= ast values */

/* An option value.  Mirrors UninterpretedOption's value oneof, plus the
 * text-format tree of an aggregate, without interpreting anything. */
typedef enum {
  AV_IDENT, /* bare identifier: SPEED, true, false, inf, nan             */
  AV_INT,   /* `ival` is the magnitude, `negative` carries the sign      */
  AV_FLOAT, /* `fval` is the magnitude, `negative` carries the sign      */
  AV_STR,   /* `sval` holds the decoded bytes of all concatenated parts  */
  AV_AGG,   /* `{ ... }` or `< ... >` text-format message                */
  AV_LIST   /* `[ a, b, c ]` inside an aggregate                         */
} AstValueKind;

typedef struct AstAggField AstAggField;

typedef struct AstValue {
  AstValueKind kind;
  int line, col;
  bool negative; /* a leading '-' was present (AV_IDENT/INT/FLOAT)       */
  bool ovf;      /* AV_INT: magnitude did not fit in a uint64_t          */
  Str sval;      /* AV_IDENT: the identifier; AV_STR: decoded bytes      */
  Str raw;       /* AV_AGG: protoc-normalised body text (all tokens
                  * between the braces joined by single spaces)          */
  uint64_t ival;
  double fval;
  AstAggField *agg;      /* AV_AGG: field list                           */
  struct AstValue *list; /* AV_LIST: elements, linked through ->next     */
  struct AstValue *next;
} AstValue;

/* One `name: value` entry inside an aggregate option value. */
struct AstAggField {
  Str name;          /* plain field name, or the dotted extension name   */
  Str any_prefix;    /* `[type.googleapis.com/foo.Bar]` -> the URL part  */
  bool is_extension; /* the name was written inside `[ ]`                */
  bool has_colon;
  AstValue *value;
  int line, col;
  AstAggField *next;
};

/* ============================================================ ast options */

typedef struct AstOptName {
  Str name;          /* `foo`, or `foo.bar` / `.foo.bar` for extensions  */
  bool is_extension; /* written as `( ... )`                             */
  int line, col;
  struct AstOptName *next;
} AstOptName;

typedef struct AstOption {
  AstOptName *name;
  AstValue *value;
  int line, col;
  struct AstOption *next;
} AstOption;

/* ============================================================= ast ranges */

typedef struct AstRange {
  int64_t start;   /* inclusive, as written                              */
  int64_t end;     /* inclusive; only meaningful when has_end            */
  bool has_end;    /* `to` was present                                   */
  bool end_is_max; /* `to max`; the real bound depends on context        */
  int line, col;
  struct AstRange *next;
} AstRange;

typedef struct AstRName {
  Str name;
  bool quoted; /* written as a string literal, not an identifier         */
  int line, col;
  struct AstRName *next;
} AstRName;

typedef struct AstReserved {
  AstRange *ranges; /* number form */
  AstRName *names;  /* name form   */
  int line, col;
  struct AstReserved *next;
} AstReserved;

typedef struct AstExtRange {
  AstRange *ranges;
  AstOption *options; /* `[declaration = {...}, verification = ...]`     */
  int line, col;
  struct AstExtRange *next;
} AstExtRange;

/* ============================================================= ast fields */

typedef enum { AST_FIELD_NORMAL, AST_FIELD_GROUP, AST_FIELD_MAP } AstFieldKind;

enum { AST_LABEL_NONE = 0 }; /* other label values reuse pb.h's L_*      */

typedef struct AstMessage AstMessage;

typedef struct AstField {
  AstFieldKind kind;
  int label; /* 0 | L_OPTIONAL | L_REQUIRED | L_REPEATED                 */

  Str type_name; /* as written: "int32", "foo.Bar", ".foo.Bar"; for a
                  * group this is the group's declared name             */
  int builtin;   /* T_* when type_name names a builtin scalar, else 0    */
  Str key_type;  /* AST_FIELD_MAP                                       */
  int key_builtin;
  Str value_type;
  int value_builtin;

  Str name;       /* field name; for a group the lowercased group name   */
  Str group_name; /* AST_FIELD_GROUP: the name exactly as written        */

  uint64_t number; /* magnitude as written                               */
  bool has_number;
  bool number_ovf;

  Str extendee; /* set when the field came from an `extend` block        */
  bool is_extension;

  int oneof_index; /* index into the message's oneof list, or -1         */

  AstOption *options;
  AstValue *default_value; /* `[default = ...]`, not an option           */
  Str json_name;           /* `[json_name = "..."]`, not an option       */
  bool has_json_name;

  AstMessage *group_body; /* AST_FIELD_GROUP; also linked into the
                           * enclosing scope's message list              */
  int line, col;
  int name_line, name_col;
  struct AstField *next;
} AstField;

typedef struct AstOneof {
  Str name;
  AstOption *options;
  int index;
  int line, col;
  struct AstOneof *next;
} AstOneof;

/* ============================================================== ast enums */

enum { AST_VIS_UNSET = 0, AST_VIS_LOCAL = 1, AST_VIS_EXPORT = 2 };

typedef struct AstEnumValue {
  Str name;
  int64_t number;
  bool has_number;
  bool number_ovf;
  AstOption *options;
  int line, col;
  struct AstEnumValue *next;
} AstEnumValue;

typedef struct AstEnum {
  Str name;
  int visibility;
  AstOption *options;
  AstEnumValue *values;
  AstReserved *reserved;
  int line, col;
  struct AstEnum *next;
} AstEnum;

/* =========================================================== ast messages */

typedef struct AstExtend {
  Str extendee;
  AstField *fields;
  int line, col;
  struct AstExtend *next;
} AstExtend;

struct AstMessage {
  Str name;
  int visibility;
  /* `fields` is in source order and includes oneof members (tagged with
   * oneof_index) and group fields -- exactly like DescriptorProto.field. */
  AstField *fields;
  AstOneof *oneofs;
  AstMessage *messages; /* nested messages; group bodies included        */
  AstEnum *enums;
  AstExtend *extends;
  AstExtRange *ext_ranges;
  AstReserved *reserved;
  AstOption *options;
  bool is_group; /* synthesised from a group field                       */
  int line, col;
  AstMessage *next;
};

/* =========================================================== ast services */

typedef struct AstMethod {
  Str name;
  Str input_type;
  Str output_type;
  bool client_streaming, server_streaming;
  bool has_body; /* written as `{ ... }` rather than `;` */
  AstOption *options;
  int line, col;
  struct AstMethod *next;
} AstMethod;

typedef struct AstService {
  Str name;
  AstOption *options;
  AstMethod *methods;
  int line, col;
  struct AstService *next;
} AstService;

/* =============================================================== ast file */

enum {
  AST_IMP_DEFAULT = 0,
  AST_IMP_PUBLIC = 1,
  AST_IMP_WEAK = 2,
  AST_IMP_OPTION = 3
};

typedef struct AstImport {
  Str path;
  int kind;
  int line, col;
  struct AstImport *next;
} AstImport;

typedef enum {
  AST_SYN_ABSENT = 0, /* no syntax/edition statement -> proto2           */
  AST_SYN_PROTO2,
  AST_SYN_PROTO3,
  AST_SYN_EDITION
} AstSyntaxKind;

typedef struct AstFile {
  const char *filename;
  AstSyntaxKind syntax_kind;
  Str syntax_text; /* "proto2" | "proto3" | "2023" | "2024" | ...        */
  int edition;     /* ED_PROTO2 | ED_PROTO3 | ED_2023 | ED_2024 | 0      */
  int syntax_line, syntax_col;

  Str package;
  bool has_package;
  int package_line, package_col;

  AstImport *imports;
  AstOption *options;
  AstMessage *messages;
  AstEnum *enums;
  AstExtend *extends;
  AstService *services;

  Arena *arena;
} AstFile;

/* Parses `src`.  Always returns a non-NULL AstFile (possibly partial); every
 * problem is reported through `d`.  Inspect `d->n` for success. */
AstFile *ast_parse(Arena *a, Diags *d, const char *filename, Str src);

/* Deterministic indentation-based dump of the whole tree. */
void ast_dump(FILE *out, const AstFile *f);

/* T_* for a builtin scalar type name, or 0.  ("group" maps to T_GROUP.) */
int ast_builtin_type(Str name);

#endif /* PB_AST_H */
