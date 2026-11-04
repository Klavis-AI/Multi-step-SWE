/* A Ruby interpreter in C11.  Single shared header. */
#ifndef MINIRUBY_H
#define MINIRUBY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>
#include <limits.h>

#ifndef RUBY_TARGET
#define RUBY_TARGET 18   /* 18, 20, 27, 31, 33, 34 */
#endif

typedef uintptr_t VALUE;
typedef int ID;

/* ---- value tagging: bit0 => Fixnum, otherwise pointer ---- */
#define FIXNUM_P(v)   (((VALUE)(v)) & 1)
#define LONG2FIX(i)   ((VALUE)(((uintptr_t)(intptr_t)(i) << 1) | 1))
#define FIX2LONG(v)   (((intptr_t)(v)) >> 1)
#define FIXNUM_MAX    (LONG_MAX >> 1)
#define FIXNUM_MIN    (LONG_MIN >> 1)

enum rb_type {
  T_NIL, T_TRUE, T_FALSE, T_OBJECT, T_CLASS, T_MODULE, T_ICLASS,
  T_STRING, T_ARRAY, T_HASH, T_SYMBOL, T_FLOAT, T_RANGE, T_PROC,
  T_METHODOBJ, T_EXCEPTION, T_REGEXP, T_MATCH, T_STRUCTOBJ, T_ENUMERATOR,
  T_FIBER, T_BINDING, T_DATAOBJ, T_RACTOR, T_BIGNUM, T_UNBOUND, T_FIXNUM
};

struct RClass;
typedef struct ivtbl ivtbl;

typedef struct RBasic {
  unsigned char type;
  unsigned char frozen;
  unsigned char chilled;       /* 3.4 chilled string literal */
  unsigned char mark;
  struct RClass *klass;
  ivtbl *iv;
} RBasic;

typedef struct rb_encoding {
  const char *name;
  int index;                   /* 0 ascii-8bit, 1 us-ascii, 2 utf-8 */
} rb_encoding;

typedef struct RString {
  RBasic basic;
  char *ptr;
  long len, cap;
  rb_encoding *enc;
} RString;

typedef struct RArray {
  RBasic basic;
  VALUE *ptr;
  long len, cap;
} RArray;

typedef struct hentry { VALUE key, val; long hash; int used; } hentry;
typedef struct RHash {
  RBasic basic;
  hentry *ent;                 /* insertion-ordered entries */
  long n, cap;                 /* n includes deleted slots */
  long size;                   /* live entries */
  long *idx; long idxcap;      /* open addressing index into ent (+1), 0 empty */
  VALUE ifnone; struct RProc *defproc;
  int compare_by_identity;
} RHash;

typedef struct RSymbol { RBasic basic; ID id; } RSymbol;
typedef struct RFloat { RBasic basic; double f; } RFloat;
typedef struct RRange { RBasic basic; VALUE beg, end; int excl; } RRange;

typedef struct RObject { RBasic basic; } RObject;

typedef struct method_entry method_entry;
typedef struct mtbl mtbl;

typedef struct RClass {
  RBasic basic;
  struct RClass *super;
  mtbl *m;
  ivtbl *consts;
  ID name;
  struct RClass *lexical_parent;
  int is_module, is_singleton;
  VALUE attached;              /* for singleton classes */
  struct RClass **includes; int nincludes, cincludes;
  struct RClass **prepends; int nprepends, cprepends;
  struct RClass **anc; int nanc;   /* cached linearization */
  unsigned long anc_serial;
  struct RClass **refinements; int nrefine, crefine; /* modules refining this */
} RClass;

typedef struct Node Node;
typedef struct Env Env;

typedef struct RProc {
  RBasic basic;
  Node *params;
  Node *body;
  Env *env;
  VALUE self;
  RClass *defclass;
  RClass *cref;
  int is_lambda;
  struct Frame *frame;         /* frame to return from (for proc return) */
  VALUE (*cfunc)(VALUE, int, VALUE*, struct RProc*);
  VALUE cdata;                 /* bound data for cfunc procs (Symbol#to_proc, Method#to_proc) */
  struct rb_cref *cref_list;
  struct RProc *refined_from;
  int arity_override;
} RProc;

typedef struct RMethodObj { RBasic basic; VALUE recv; RClass *owner; ID mid; method_entry *me; int unbound; } RMethodObj;
typedef struct RRegexp { RBasic basic; struct regex *re; VALUE src; int opts; } RRegexp;
typedef struct RMatch { RBasic basic; VALUE str; VALUE re; int ngroups; long *beg, *end; char **names; int *nameidx; int nnames; } RMatch;
typedef struct REnumerator { RBasic basic; VALUE obj; ID mid; VALUE args; struct RProc *gen; struct RArray *lazy_ops; int is_lazy; struct RFiber *fib; VALUE sizev; } REnumerator;
typedef struct RFiber { RBasic basic; struct fiber_ctx *ctx; struct RProc *blk; int state; VALUE xfer; } RFiber;
typedef struct RData { RBasic basic; } RData;
typedef struct RRactor { RBasic basic; VALUE value; int taken; VALUE err; } RRactor;

/* ---- globals ---- */
extern VALUE Qnil, Qtrue, Qfalse, Qundef;
extern RClass *rb_cBasicObject, *rb_cObject, *rb_cModule, *rb_cClass, *rb_cString,
  *rb_cArray, *rb_cHash, *rb_cSymbol, *rb_cInteger, *rb_cFixnum, *rb_cBignum,
  *rb_cFloat, *rb_cNumeric, *rb_cRange, *rb_cProc, *rb_cMethod, *rb_cUnboundMethod,
  *rb_cNilClass, *rb_cTrueClass, *rb_cFalseClass, *rb_cRegexp, *rb_cMatchData,
  *rb_cStruct, *rb_cEnumerator, *rb_cLazy, *rb_cFiber, *rb_cBinding, *rb_cData,
  *rb_cSet, *rb_cRactor, *rb_cComparable, *rb_cEnumerable, *rb_cKernel, *rb_cEncoding,
  *rb_cException, *rb_eStandardError, *rb_eRuntimeError, *rb_eArgError, *rb_eTypeError,
  *rb_eNameError, *rb_eNoMethodError, *rb_eIndexError, *rb_eKeyError, *rb_eRangeError,
  *rb_eZeroDiv, *rb_eStopIteration, *rb_eNotImpError, *rb_eFrozenError, *rb_eLocalJumpError,
  *rb_eScriptError, *rb_eSyntaxError, *rb_eSystemExit, *rb_eFloatDomainError,
  *rb_eIOError, *rb_eEncodingError, *rb_eNoMatchingPattern, *rb_eNoMatchingPatternKey,
  *rb_eRactorIsolationError, *rb_eFiberError, *rb_eClosedQueueError, *rb_eInterrupt,
  *rb_eSignalException, *rb_eSystemStackError, *rb_eNoMemoryError, *rb_eSecurityError,
  *rb_eEncCompatError, *rb_eUncaughtThrow, *rb_eRactorError, *rb_eDeadlockError,
  *rb_eConvError;

extern rb_encoding *enc_binary, *enc_usascii, *enc_utf8;

/* ---- allocation ---- */
void *xmalloc(size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);

/* ---- ids ---- */
ID rb_intern(const char *);
ID rb_intern2(const char *, long);
const char *rb_id2name(ID);
long rb_id_len(ID);

/* ---- basic ---- */
#define BASIC(v)   ((RBasic*)(v))
static inline int rb_type(VALUE v){ return FIXNUM_P(v) ? T_FIXNUM : (int)((RBasic*)v)->type; }
#define TYPE(v)    rb_type(v)
#define RSTR(v)  ((RString*)(v))
#define RARR(v)  ((RArray*)(v))
#define RHSH(v)  ((RHash*)(v))
#define RCLS(v)  ((RClass*)(v))
#define RSYM(v)  ((RSymbol*)(v))
#define RFLT(v)  ((RFloat*)(v))
#define RRNG(v)  ((RRange*)(v))
#define RPRC(v)  ((RProc*)(v))
#define RREG(v)  ((RRegexp*)(v))
#define RMTCH(v) ((RMatch*)(v))
#define RENUM(v) ((REnumerator*)(v))

#define RTEST(v) (((VALUE)(v)) != Qnil && ((VALUE)(v)) != Qfalse)
#define NIL_P(v) (((VALUE)(v)) == Qnil)

/* ---- type checks ---- */
int rb_is_a(VALUE obj, RClass *c);
RClass *rb_class_of(VALUE);
RClass *rb_singleton_class(VALUE);
const char *rb_obj_classname(VALUE);

/* ---- core constructors ---- */
VALUE rb_str_new(const char *, long);
VALUE rb_str_new_cstr(const char *);
VALUE rb_str_new_enc(const char *, long, rb_encoding *);
VALUE rb_str_dup(VALUE);
VALUE rb_ary_new(void);
VALUE rb_ary_new_capa(long);
VALUE rb_ary_new_from(long n, VALUE *v);
void  rb_ary_push(VALUE, VALUE);
VALUE rb_ary_entry(VALUE, long);
VALUE rb_hash_new(void);
VALUE rb_hash_aref(VALUE, VALUE);
VALUE rb_hash_lookup(VALUE, VALUE, VALUE);
void  rb_hash_aset(VALUE, VALUE, VALUE);
int   rb_hash_delete(VALUE, VALUE, VALUE *);
VALUE rb_float_new(double);
VALUE rb_sym(ID);
VALUE rb_sym_from_str(VALUE);
VALUE rb_range_new(VALUE, VALUE, int);
VALUE rb_int_new(long long);

/* ---- conversion ---- */
long rb_num2long(VALUE);
double rb_num2dbl(VALUE);
VALUE rb_str_to_str(VALUE);
const char *rb_str_cstr(VALUE);
VALUE rb_inspect(VALUE);
VALUE rb_obj_as_string(VALUE);
VALUE rb_to_int(VALUE);

/* ---- classes ---- */
RClass *rb_define_class(const char *, RClass *);
RClass *rb_define_class_under(RClass *, const char *, RClass *);
RClass *rb_define_module(const char *);
RClass *rb_define_module_under(RClass *, const char *);
void rb_include_module(RClass *, RClass *);
void rb_prepend_module(RClass *, RClass *);
void rb_define_method(RClass *, const char *, VALUE (*)(), int);
void rb_define_singleton_method(VALUE, const char *, VALUE (*)(), int);
void rb_define_global_function(const char *, VALUE (*)(), int);
void rb_define_alias(RClass *, const char *, const char *);
void rb_define_const(RClass *, const char *, VALUE);
VALUE rb_const_get(RClass *, ID, int *found);

/* ---- calling ---- */
typedef struct BlockArg { RProc *proc; } BlockArg;
VALUE rb_funcall(VALUE recv, ID mid, int argc, ...);
VALUE rb_call(VALUE recv, ID mid, int argc, VALUE *argv, RProc *blk);
VALUE rb_call_super(int argc, VALUE *argv, RProc *blk);
VALUE rb_proc_call(RProc *, int argc, VALUE *argv, RProc *blk);
VALUE rb_yield_values(RProc *blk, int argc, VALUE *argv);
VALUE rb_yield1(RProc *blk, VALUE v);
int rb_respond_to(VALUE, ID);

/* ---- exceptions ---- */
void rb_raise(RClass *, const char *fmt, ...) __attribute__((noreturn));
void rb_exc_raise(VALUE) __attribute__((noreturn));
VALUE rb_exc_new(RClass *, VALUE msg);
void rb_frozen_error(VALUE obj) __attribute__((noreturn));
void rb_check_frozen(VALUE obj);

/* ---- warnings ---- */
void rb_warn_deprecated(const char *fmt, ...);
void rb_warning(const char *fmt, ...);
extern int rb_verbose;        /* -1 = -W0, 0 = default, 1 = -w/-W2 */
extern int rb_warn_deprecated_on;

/* ---- interpreter entry ---- */
void rb_init(void);
VALUE rb_eval_string_file(const char *src, const char *filename);

/* ---- misc used across files ---- */
VALUE rb_obj_freeze(VALUE);
int rb_obj_frozen(VALUE);
int rb_eql(VALUE, VALUE);
int rb_equal(VALUE, VALUE);
long rb_hash_code(VALUE);
VALUE rb_obj_dup(VALUE);
VALUE rb_check_array_type(VALUE);
VALUE rb_check_string_type(VALUE);
VALUE rb_ary_to_ary(VALUE);
VALUE rb_enum_new(VALUE obj, ID mid, int argc, VALUE *argv);
int rb_block_given(void);

#define ARGC_ANY -1
#endif
