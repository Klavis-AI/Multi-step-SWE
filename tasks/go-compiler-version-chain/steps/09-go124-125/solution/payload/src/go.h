/* go.h - shared declarations for the Go toolchain.
 *
 * Layering: util -> lex -> parse (AST) -> types -> check (type check + bytecode)
 *           -> vm/runtime -> gocmd (the `go` command).
 */
#ifndef GO_H
#define GO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- util --- */

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xsprintf(const char *fmt, ...);

typedef struct Buf { char *p; size_t len, cap; } Buf;
void buf_init(Buf *b);
void buf_put(Buf *b, const void *data, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, int c);
void buf_printf(Buf *b, const char *fmt, ...);
void buf_free(Buf *b);

typedef struct Vec { void **p; int len, cap; } Vec;
void vec_push(Vec *v, void *x);
void vec_free(Vec *v);

const char *intern(const char *s);
const char *internn(const char *s, size_t n);

int file_exists(const char *path);
int is_dir(const char *path);
char *read_file(const char *path, size_t *len_out);
int write_file(const char *path, const void *data, size_t len);
int mkdir_p(const char *path);
char *path_join(const char *a, const char *b);
char *path_dir(const char *p);
char *path_base(const char *p);
Vec list_dir(const char *path);

void fatalf(const char *fmt, ...);

/* --------------------------------------------------------------- errors --- */

typedef struct Pos { const char *file; int line, col; } Pos;

typedef struct CompileError { Pos pos; char *msg; } CompileError;

extern Vec g_errors;
void errorf(Pos pos, const char *fmt, ...);
void errors_reset(void);
int errors_len(void);
void errors_print(FILE *f);

/* ------------------------------------------------------------ language --- */

extern int g_lang;
int lang_at_least(int minor);

/* ------------------------------------------------------------------ lex --- */

typedef enum {
    TOK_EOF, TOK_IDENT, TOK_INT, TOK_FLOAT, TOK_IMAG, TOK_CHAR, TOK_STRING,
    TOK_ADD, TOK_SUB, TOK_MUL, TOK_QUO, TOK_REM,
    TOK_AND, TOK_OR, TOK_XOR, TOK_SHL, TOK_SHR, TOK_AND_NOT,
    TOK_ADD_ASSIGN, TOK_SUB_ASSIGN, TOK_MUL_ASSIGN, TOK_QUO_ASSIGN, TOK_REM_ASSIGN,
    TOK_AND_ASSIGN, TOK_OR_ASSIGN, TOK_XOR_ASSIGN, TOK_SHL_ASSIGN, TOK_SHR_ASSIGN,
    TOK_AND_NOT_ASSIGN,
    TOK_LAND, TOK_LOR, TOK_ARROW, TOK_INC, TOK_DEC,
    TOK_EQL, TOK_LSS, TOK_GTR, TOK_ASSIGN, TOK_NOT,
    TOK_NEQ, TOK_LEQ, TOK_GEQ, TOK_DEFINE, TOK_ELLIPSIS,
    TOK_LPAREN, TOK_LBRACK, TOK_LBRACE, TOK_COMMA, TOK_PERIOD,
    TOK_RPAREN, TOK_RBRACK, TOK_RBRACE, TOK_SEMICOLON, TOK_COLON, TOK_TILDE,
    TOK_BREAK, TOK_CASE, TOK_CHAN, TOK_CONST, TOK_CONTINUE, TOK_DEFAULT,
    TOK_DEFER, TOK_ELSE, TOK_FALLTHROUGH, TOK_FOR, TOK_FUNC, TOK_GO, TOK_GOTO,
    TOK_IF, TOK_IMPORT, TOK_INTERFACE, TOK_MAP, TOK_PACKAGE, TOK_RANGE,
    TOK_RETURN, TOK_SELECT, TOK_STRUCT, TOK_SWITCH, TOK_TYPE, TOK_VAR,
    TOK__MAX
} TokKind;

const char *tok_str(TokKind k);

typedef struct Token { TokKind kind; Pos pos; const char *lit; size_t litlen; } Token;

typedef struct Comment { Pos pos; const char *text; } Comment;

typedef struct Lexer {
    const char *src; size_t len, off; Pos pos; Token prev;
    Vec comments; int insert_semi; int nerr;
} Lexer;

void lex_init(Lexer *lx, const char *file, const char *src, size_t len);
Token lex_next(Lexer *lx);

/* ------------------------------------------------------------------ ast --- */

typedef struct Node Node;
typedef struct Type Type;

typedef enum {
    N_IDENT, N_BASICLIT, N_FUNCLIT, N_COMPOSITELIT, N_PAREN, N_SELECTOR,
    N_INDEX, N_INDEXLIST, N_SLICEEXPR, N_TYPEASSERT, N_CALL, N_STAR,
    N_UNARY, N_BINARY, N_KEYVALUE, N_ELLIPSIS,
    N_ARRAYTYPE, N_STRUCTTYPE, N_FUNCTYPE, N_INTERFACETYPE, N_MAPTYPE, N_CHANTYPE,
    N_DECLSTMT, N_EMPTY, N_LABELED, N_EXPRSTMT, N_SEND, N_INCDEC, N_ASSIGN,
    N_GO, N_DEFER, N_RETURN, N_BRANCH, N_BLOCK, N_IF, N_CASECLAUSE, N_SWITCH,
    N_TYPESWITCH, N_COMMCLAUSE, N_SELECT, N_FOR, N_RANGE,
    N_IMPORT, N_VALUESPEC, N_TYPESPEC, N_FUNCDECL, N_GENDECL, N_FIELD, N_FILE
} NodeKind;

struct Node {
    NodeKind kind; Pos pos; TokKind op; const char *name; size_t namelen;
    Node *x, *y, *z, *w;
    Vec list, list2;
    Type *type; void *xtra; int flag;
};

Node *node_new(NodeKind k, Pos pos);

Node *parse_file(const char *filename, const char *src, size_t len, int lang);
extern Vec g_file_comments;

/* ---------------------------------------------------------------- types --- */

typedef enum {
    T_INVALID, T_BOOL, T_INT, T_INT8, T_INT16, T_INT32, T_INT64,
    T_UINT, T_UINT8, T_UINT16, T_UINT32, T_UINT64, T_UINTPTR,
    T_FLOAT32, T_FLOAT64, T_STRING, T_UNSAFEPTR,
    T_UNTYPED_BOOL, T_UNTYPED_INT, T_UNTYPED_RUNE, T_UNTYPED_FLOAT,
    T_UNTYPED_STRING, T_UNTYPED_NIL,
    T_ARRAY, T_SLICE, T_STRUCT, T_PTR, T_FUNC, T_INTERFACE, T_MAP, T_CHAN,
    T_NAMED, T_TYPEPARAM, T_TUPLE
} TKind;

typedef struct Field { const char *name; Type *type; int embedded; const char *tag; Pos pos; } Field;

typedef struct Method {
    const char *name; Type *sig; struct FuncDef *def; Type *recv; int ptr_recv; Pos pos; void *decl_sym;
} Method;

struct Type {
    TKind k;
    const char *name; const char *pkg;
    Type *under; Vec methods;
    Type *targs_of; Vec targs; Vec tparams;
    Node *decl; int is_alias;
    Type *elem; Type *key; int64_t alen; Node *alen_expr;
    Vec fields; Vec results; int variadic; int chandir;
    Vec embeds; Vec terms; int comparable_req;
    Type *constraint; int tpindex;
    Vec tuple;
    int size_cached; int64_t size;
    int visiting;
    struct Package *owner;
};

typedef struct TypeTerm { Type *t; int tilde; } TypeTerm;

extern Type *ty_invalid, *ty_bool, *ty_int, *ty_int8, *ty_int16, *ty_int32, *ty_int64;
extern Type *ty_uint, *ty_uint8, *ty_uint16, *ty_uint32, *ty_uint64, *ty_uintptr;
extern Type *ty_float32, *ty_float64, *ty_string, *ty_unsafeptr;
extern Type *ty_untyped_bool, *ty_untyped_int, *ty_untyped_rune, *ty_untyped_float;
extern Type *ty_untyped_string, *ty_untyped_nil;
extern Type *ty_error, *ty_any, *ty_comparable;

void types_init(void);
Type *type_new(TKind k);
Type *type_ptr(Type *elem);
Type *type_slice(Type *elem);
Type *type_array(Type *elem, int64_t n);
Type *type_map(Type *k, Type *v);
Type *type_chan(Type *elem, int dir);
Type *underlying(Type *t);
int type_identical(Type *a, Type *b);
int type_assignable(Type *src, Type *dst);
int type_convertible(Type *src, Type *dst);
int type_comparable(Type *t);
int type_ordered(Type *t);
int is_integer(Type *t);
int is_unsigned(Type *t);
int is_float(Type *t);
int is_numeric(Type *t);
int is_string(Type *t);
int is_boolean(Type *t);
int is_untyped(Type *t);
int is_iface(Type *t);
int is_typeparam(Type *t);
Type *default_type(Type *t);
const char *type_string(Type *t);
int64_t type_size(Type *t);
int64_t type_align(Type *t);
Method *lookup_method(Type *t, const char *name, int *indirect);
Field *lookup_field(Type *t, const char *name, Vec *path);
int iface_satisfied(Type *t, Type *iface, const char **missing);
Type *type_subst(Type *t, Vec *tparams, Vec *targs);
Type *core_type(Type *t);

/* ----------------------------------------------------------- constants --- */

typedef enum { C_INVALID, C_BOOL, C_INT, C_FLOAT, C_STRING } ConstKind;

typedef struct BigInt { int neg; int len; uint32_t *d; } BigInt;

typedef struct Const {
    ConstKind k;
    int bval;
    BigInt i;
    BigInt fnum, fden;
    const char *s;
    size_t slen;
} Const;

void big_init(BigInt *b);
void big_from_i64(BigInt *b, int64_t v);
void big_from_u64(BigInt *b, uint64_t v);
int big_parse(BigInt *b, const char *s, int base);
int64_t big_to_i64(const BigInt *b, int *ok);
uint64_t big_to_u64(const BigInt *b, int *ok);
double big_to_double(const BigInt *b);
char *big_to_string(const BigInt *b);
int big_cmp(const BigInt *a, const BigInt *b);
int big_cmp_abs(const BigInt *a, const BigInt *b);
void big_add(BigInt *r, const BigInt *a, const BigInt *b);
void big_sub(BigInt *r, const BigInt *a, const BigInt *b);
void big_mul(BigInt *r, const BigInt *a, const BigInt *b);
void big_divmod(BigInt *q, BigInt *r, const BigInt *a, const BigInt *b);
void big_shl(BigInt *r, const BigInt *a, unsigned n);
void big_shr(BigInt *r, const BigInt *a, unsigned n);
void big_and(BigInt *r, const BigInt *a, const BigInt *b);
void big_or(BigInt *r, const BigInt *a, const BigInt *b);
void big_xor(BigInt *r, const BigInt *a, const BigInt *b);
void big_andnot(BigInt *r, const BigInt *a, const BigInt *b);
void big_neg(BigInt *r, const BigInt *a);
int big_is_zero(const BigInt *b);
void big_copy(BigInt *r, const BigInt *a);

Const *const_new(ConstKind k);
double const_to_double(const Const *c);
int const_representable(const Const *c, Type *t, Const **out);
char *const_string(const Const *c);
int const_is_zero(const Const *c);

/* ------------------------------------------------------------- bytecode --- */

typedef enum {
    OP_NOP, OP_POP, OP_DUP, OP_SWAP,
    OP_CONST, OP_NILV, OP_BOOLV,
    OP_LOADVAR, OP_STOREVAR, OP_ADDRVAR,
    OP_LOADGLOB, OP_STOREGLOB, OP_ADDRGLOB,
    OP_LOADPTR, OP_STOREPTR,
    OP_FIELD, OP_ADDRFIELD,
    OP_INDEX, OP_ADDRINDEX, OP_INDEXSET,
    OP_MAPGET, OP_MAPGET2, OP_MAPSET, OP_MAPDEL,
    OP_SLICE,
    OP_BIN, OP_UN, OP_CMP, OP_SHIFT,
    OP_CONV, OP_MAKEIFACE,
    OP_JMP, OP_JMPT, OP_JMPF,
    OP_CALL, OP_CALLIFACE, OP_GO, OP_DEFER, OP_RET,
    OP_NEWENV, OP_POPENV,
    OP_MAKESLICE, OP_MAKEMAP, OP_MAKECHAN, OP_NEW,
    OP_COMPOSITE_STRUCT, OP_COMPOSITE_ARRAY, OP_COMPOSITE_SLICE, OP_COMPOSITE_MAP,
    OP_SEND, OP_RECV, OP_SELECT, OP_CLOSE,
    OP_LEN, OP_CAP, OP_APPEND, OP_COPY, OP_DELETE, OP_PANIC, OP_RECOVER,
    OP_PRINT, OP_TYPEASSERT, OP_TYPESWITCH,
    OP_RANGEINIT, OP_RANGENEXT,
    OP_CLOSURE, OP_METHVAL, OP_ZERO, OP_TUPLEGET, OP_TUPLEEXPAND,
    OP_MINMAX, OP_CLEAR, OP_NATIVE, OP_YIELDCHK, OP_ITERSTOP, OP_SPREAD, OP_COPYENV,
    OP__MAX
} Opcode;

typedef struct Instr { uint16_t op; int32_t a, b, c; void *p; int line; } Instr;

typedef struct FuncDef {
    const char *name; const char *pkg; Type *sig;
    Instr *code; int ncode, codecap;
    int nlocals; int nparams; int has_recv; int variadic;
    Vec consts;
    Node *body; Node *decl;
    struct Package *package;
    int native;
    Vec tparams;
    int is_closure;
    int captures;
    const char *srcfile;
    int compiled;
    int nres_named; int res_slot0;
    Vec insts;      /* instantiation cache for generic funcs */
    Vec recvtp;
} FuncDef;

/* ---------------------------------------------------------------- value --- */

typedef struct Obj Obj;

typedef enum {
    VK_INVALID, VK_NIL, VK_BOOL, VK_INT, VK_UINT, VK_FLOAT, VK_STRING,
    VK_PTR, VK_SLICE, VK_ARRAY, VK_STRUCT, VK_MAP, VK_CHAN, VK_FUNC, VK_TUPLE,
    VK_TYPE
} ValKind;

typedef struct Value {
    uint32_t k;
    uint32_t off;
    int64_t len;
    int64_t cap;
    Type *t;
    union { int64_t i; uint64_t u; double d; } b;
    Obj *o;
} Value;

typedef enum {
    OBJ_CELLS, OBJ_BYTES, OBJ_MAP, OBJ_CHAN, OBJ_FUNC, OBJ_MISC
} ObjKind;

struct Obj {
    uint8_t kind; uint8_t mark; uint8_t noscan; uint8_t gray;
    uint32_t n;
    Obj *next;
    size_t bytes;
    void *aux;
};

#define OBJ_CELLPTR(o) ((Value *)((char *)(o) + sizeof(Obj)))
#define OBJ_BYTEPTR(o) ((char *)(o) + sizeof(Obj))

Obj *gc_alloc_cells(int n);
Obj *gc_alloc_bytes(size_t n);
Obj *gc_alloc_obj(ObjKind k, size_t extra);
void gc_collect(int forced);
void gc_maybe(void);
void gc_init(void);
void gc_set_finalizer(Obj *o, Value fn, Type *argt);
void gc_add_cleanup(Obj *o, Value fn, Value arg);
void gc_barrier(Value v);
void gc_run_pending_finalizers(void);

extern size_t gc_heap_bytes;
extern size_t gc_next_gc;
extern uint64_t gc_num_gc, gc_forced_gc;
extern uint64_t gc_total_alloc;
extern uint64_t gc_mallocs, gc_frees;
extern int64_t gc_gogc;
extern int64_t gc_memlimit;
extern uint64_t gc_pause_total_ns;
extern int gc_phase;          /* 0 idle, 1 marking */
extern uint64_t gc_mark_work;

/* ---------------------------------------------------------------- chans --- */

typedef struct Chan {
    Value *buf; int bufcap, buflen, bufhead;
    int closed; Type *elem;
    Vec sendq, recvq;
} Chan;

/* ------------------------------------------------------------- goroutine --- */

typedef struct DeferRec { Value fn; Value *args; int nargs; int nres; } DeferRec;

typedef struct Frame {
    FuncDef *fn; int pc; Obj *env; Obj *fnenv; int sp_base;
    Vec defers;
    int recovered;
    int in_defer;
    int nres;
} Frame;

typedef enum { G_RUNNABLE, G_RUNNING, G_WAITING, G_DEAD } GStatus;

typedef struct G {
    int id; GStatus status;
    Value *stack; int sp, stackcap;
    Frame *frames; int nframes, framecap;
    int panicking;
    Value panic_value;
    int panic_recovered;
    Vec panic_chain;
    int in_recovery;
    int64_t wake_at;
    int selected;
    Value recv_value; int recv_ok;
    const char *wait_reason;
    int preempt;
    int64_t budget;
    void *bubble;
    int durable;
    int finished;
    Value startfn; Value *startargs; int nstartargs;
    int iter_stop;
} G;

extern G *g_current;
extern Vec g_allg;
extern int g_gomaxprocs;
extern int64_t g_fake_now;
extern int g_in_synctest;

/* ------------------------------------------------------------- packages --- */

typedef struct Sym {
    const char *name; int kind; Type *type; Const *cval; FuncDef *fn;
    int globidx; Node *decl; struct Package *pkg; int checked;
    Pos pos; int used; int builtin_id; int specidx; Vec tparams; void *fscope; Node *recvfield; int compiled;
} Sym;

enum { SYM_VAR, SYM_CONST, SYM_TYPE, SYM_FUNC, SYM_PKG, SYM_BUILTIN };

typedef struct EmbedFile { const char *name; char *data; size_t len; } EmbedFile;

typedef struct Package {
    const char *path; const char *name; const char *dir;
    Vec files; Vec syms; Vec imports; Vec init_funcs;
    int checked; int loading; int lang;
    Obj *globstore; int nglobals;
    Vec embed_files;
    int inited;
} Package;

Package *pkg_lookup(const char *path);
Package *load_package(const char *importpath, const char *fromdir, int lang);
Sym *pkg_sym(Package *p, const char *name);
extern Vec g_packages;

/* ---------------------------------------------------------------- check --- */

void check_package(Package *p);

/* ------------------------------------------------------------------- vm --- */

void vm_init(void);
int vm_run_main(Package *mainpkg);
int vm_call_value(Value fn, Value *args, int nargs, Value *rets, int maxrets);
void vm_panic_str(const char *fmt, ...);
void vm_throw(Value v);
extern int vm_exit_code;
extern int vm_exiting;

typedef int (*NativeFn)(Value *args, int nargs, Value *rets);
int native_lookup(const char *pkg, const char *name);
int native_call(int id, Value *args, int nargs, Value *rets);
void natives_init(void);
extern Vec g_native_names;

Value val_int(int64_t v, Type *t);
Value val_uint(uint64_t v, Type *t);
Value val_float(double v, Type *t);
Value val_bool(int v);
Value val_nil(void);
Value val_string(const char *s, size_t n);
Value val_zero(Type *t);
Value val_copy(Value v);
int val_equal(Value a, Value b);
char *val_to_cstring(Value v);
uint64_t val_hash(Value v);
int val_truth(Value v);

typedef struct GoMap GoMap;
GoMap *map_new(Type *t);
int map_get(GoMap *m, Value k, Value *out);
void map_set(GoMap *m, Value k, Value v);
void map_delete(GoMap *m, Value k);
int map_len(GoMap *m);
void map_clear(GoMap *m);
void map_keys(GoMap *m, Vec *out);
void map_mark(GoMap *m);

/* --------------------------------------------------------------- gocmd --- */

extern const char *g_goroot;
extern const char *g_gopath;
extern const char *g_gomodcache;
extern const char *g_goproxy;
extern const char *g_go111module;
extern const char *TOOLCHAIN_VERSION;
extern int TOOLCHAIN_MINOR;
extern Vec g_runargs;

int go_main(int argc, char **argv);

#endif /* GO_H */
