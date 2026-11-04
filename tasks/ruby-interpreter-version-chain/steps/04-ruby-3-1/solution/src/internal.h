#ifndef MINIRUBY_INTERNAL_H
#define MINIRUBY_INTERNAL_H
#include "ruby.h"

/* ===================== AST ===================== */
enum node_type {
  N_NIL, N_TRUE, N_FALSE, N_SELF, N_INT, N_FLOAT, N_STR, N_DSTR, N_XSTR,
  N_SYM, N_DSYM, N_REGX, N_DREGX, N_ARRAY, N_HASH, N_RANGE, N_LVAR, N_LASGN,
  N_IVAR, N_IASGN, N_GVAR, N_GASGN, N_CVAR, N_CVASGN, N_CONST, N_COLON2,
  N_COLON3, N_CDECL, N_CALL, N_SAFECALL, N_VCALL, N_FCALL, N_SUPER, N_ZSUPER,
  N_YIELD, N_IF, N_WHILE, N_UNTIL, N_FOR, N_CASE, N_CASE_IN, N_WHEN, N_IN,
  N_BEGIN, N_RESCUE, N_RESBODY, N_ENSURE, N_CLASS, N_MODULE, N_SCLASS, N_DEF,
  N_DEFS, N_RETURN, N_BREAK, N_NEXT, N_REDO, N_RETRY, N_AND, N_OR, N_NOT,
  N_DEFINED, N_BLOCK, N_OPASGN, N_OPASGN_IDX, N_OPASGN_ATTR, N_OPASGN_CONST,
  N_MASGN, N_ATTRASGN, N_IDXASGN, N_LAMBDA, N_BLOCKARG, N_SPLAT, N_KWSPLAT,
  N_ALIAS, N_UNDEF, N_ARGS, N_ARGENT, N_BLOCKPASS, N_ITER, N_STRCAT,
  N_DEFINED_EXPR, N_NTH_REF, N_BACK_REF, N_MATCH, N_FLIPFLOP, N_HASHPAT,
  N_ARRPAT, N_FNDPAT, N_PINNED, N_ALTPAT, N_BEGINLESS, N_NUMPARAM, N_ITPARAM,
  N_ONCE, N_ERRINFO, N_MATCHPAT, N_MATCHPATP, N_ENDLESS_RANGE, N_NILPAT,
  N_STR_CHILLED, N_POSTEXE, N_LVAR_OR_CALL, N_KWHASH, N_ARGS_FWD, N_ANONSPLAT,
  N_ANONKWSPLAT, N_ANONBLOCK
};

struct Node {
  short type; int flags; int line;
  Node *a, *b, *c, *d;
  VALUE val;
  ID id; int num;
  Node **items; int nitems;
};

Node *node_new(int type, int line);
void node_add(Node *n, Node *child);

/* params node: a = list of ARGENT, num = flags */
enum { PARAM_REQ, PARAM_OPT, PARAM_REST, PARAM_POST, PARAM_KEY, PARAM_KEYREQ,
       PARAM_KEYREST, PARAM_BLOCK, PARAM_NOKEY, PARAM_DESTRUCT };

/* ===================== scopes / envs ===================== */
typedef struct LocalTbl {
  ID *names; int n, cap;
  struct LocalTbl *parent;   /* enclosing block scope (NULL at method boundary) */
  int is_block;
  int numparam_max;          /* _1.._9 usage */
  int uses_it;
} LocalTbl;

struct Env {
  struct Env *parent;
  int n;
  VALUE *vars;
  LocalTbl *tbl;
};

/* ===================== methods ===================== */
enum { ME_CFUNC, ME_ISEQ, ME_PROC, ME_ATTR_READER, ME_ATTR_WRITER, ME_UNDEF,
       ME_ZSUPER, ME_NOTIMPL, ME_ALIAS };
enum { VIS_PUBLIC, VIS_PRIVATE, VIS_PROTECTED, VIS_MODFUNC };

struct method_entry {
  ID mid;
  int type;
  int visibility;
  RClass *owner;
  /* cfunc */
  VALUE (*cfunc)();
  int argc;
  /* iseq */
  Node *params; Node *body; RClass *cref;
  Env *env;                    /* for define_method closures */
  RProc *proc;
  ID attr_id;
  struct method_entry *orig;   /* alias target */
  int needs_tag;               /* 0 unknown, 1 body can jump, 2 body cannot */
  ID orig_mid;
};

struct mtbl { ID *keys; method_entry **vals; int n, cap; };
struct ivtbl { ID *keys; VALUE *vals; int n, cap; };

method_entry *rb_method_lookup(RClass *c, ID mid, RClass **owner);
method_entry *rb_method_lookup_super(RClass *start, RClass *from, ID mid, RClass **owner);
void rb_add_method(RClass *c, ID mid, method_entry *me);
void rb_remove_method_entry(RClass *c, ID mid);
ivtbl *ivtbl_new(void);
VALUE ivtbl_get(ivtbl *t, ID k, int *found);
void ivtbl_set(ivtbl **t, ID k, VALUE v);
int ivtbl_delete(ivtbl *t, ID k, VALUE *out);

VALUE rb_obj_alloc_of(RClass *c);
RClass *rb_class_boot(RClass *super);
RClass **rb_class_ancestors(RClass *c, int *n);
void rb_const_set(RClass *c, ID id, VALUE v);
void rb_define_private_method(RClass *c, const char *name, VALUE (*fn)(), int argc);
VALUE rb_call_full(VALUE recv, ID mid, int argc, VALUE *argv, RProc *blk, int allow_private, int self_call);
VALUE rb_gv_get(ID id);
void rb_gv_set(ID id, VALUE v);
int rb_gv_defined(ID id);
VALUE rb_main_object(void);
ID to_mid_pub(VALUE v);
VALUE rb_str_format(VALUE fmt, VALUE args);
VALUE rb_reg_make_match(VALUE re, VALUE str, long *beg, long *end, int ngroups);
VALUE rb_lazy_new(VALUE src);
VALUE rb_big_from_ll(long long v);
long rb_big2long(VALUE v);
double rb_big2dbl(VALUE v);
VALUE rb_ivar_get(VALUE obj, ID id);
void rb_ivar_set(VALUE obj, ID id, VALUE val);
int rb_ivar_defined(VALUE obj, ID id);

/* ===================== frames / control ===================== */
typedef struct cref { RClass *klass; struct cref *next; RArray *refinements; } cref_t;

typedef struct Frame {
  VALUE self;
  RClass *defclass;            /* class the running method was found in */
  cref_t *cref;                /* lexical constant scope */
  RProc *blk;                  /* block passed to this method */
  ID mid;
  Env *env;
  struct Frame *prev;
  int argc; VALUE *argv;
  int is_block;                /* running a block body */
  struct Frame *home;          /* method frame for a block */
  int visibility_default;
  VALUE last_match;
  int in_lambda;
  RClass *refined_owner;       /* for super inside a refinement */
  ID refined_mid;
} Frame;

extern Frame *rb_curframe;

enum tag_state { TAG_NONE = 0, TAG_RAISE, TAG_BREAK, TAG_NEXT, TAG_RETURN,
                 TAG_REDO, TAG_RETRY, TAG_THROW, TAG_FIBER };

typedef struct rb_tag {
  jmp_buf buf;
  struct rb_tag *prev;
  int state;
  VALUE retval;
  void *target;                /* frame / proc identity for break/return */
  Frame *frame;
} rb_tag;

extern rb_tag *rb_curtag;
extern VALUE rb_errinfo;

#define TAG_PUSH(tag) do { (tag).prev = rb_curtag; (tag).state = 0; \
    (tag).retval = Qnil; (tag).target = NULL; (tag).frame = rb_curframe; \
    rb_curtag = &(tag); } while (0)
#define TAG_POP(tag) do { rb_curtag = (tag).prev; } while (0)

void rb_jump_tag(int state, VALUE val, void *target) __attribute__((noreturn));

/* ===================== eval ===================== */
VALUE rb_eval(Node *n, Frame *f);
VALUE rb_eval_seq(Node *n, Frame *f);
VALUE rb_invoke_method(VALUE recv, method_entry *me, RClass *owner, ID mid,
                       int argc, VALUE *argv, RProc *blk, int is_super);
VALUE rb_proc_invoke(RProc *p, int argc, VALUE *argv, RProc *blk, int as_lambda);
RProc *rb_proc_new_from_block(Node *params, Node *body, Frame *f, int is_lambda);
void rb_bind_params(Node *params, Frame *f, Env *env, int argc, VALUE *argv,
                    RProc *blk, int is_lambda, int is_block);

/* ===================== parser ===================== */
Node *rb_parse_string(const char *src, const char *fname, LocalTbl **tbl_out);
extern const char *rb_source_file;
void rb_syntax_error(const char *fmt, ...) __attribute__((noreturn));

/* ===================== misc internal ===================== */
VALUE rb_str_cat_cstr(VALUE str, const char *s);
VALUE rb_str_cat(VALUE str, const char *s, long len);
VALUE rb_sprintf_val(const char *fmt, ...);
VALUE rb_ary_dup(VALUE);
VALUE rb_ary_shift_n(VALUE, long);
void rb_ary_store(VALUE, long, VALUE);
long rb_str_strlen(VALUE);          /* characters */
long rb_str_offset(VALUE, long);    /* char index -> byte offset */
VALUE rb_str_substr(VALUE, long beg, long len);  /* char based */
VALUE rb_str_plus(VALUE, VALUE);
int rb_str_cmp(VALUE, VALUE);
int rb_str_equal(VALUE, VALUE);
VALUE rb_str_freeze(VALUE);
VALUE rb_check_to_int(VALUE);
VALUE rb_check_convert(VALUE, ID);
VALUE rb_class_name(RClass *);
VALUE rb_any_to_s(VALUE);
VALUE rb_obj_inspect(VALUE);
VALUE rb_num_coerce_bin(VALUE x, VALUE y, ID op);
VALUE rb_int_pow(long, long);
VALUE rb_big_new_str(const char *s, int base);
VALUE rb_make_exception(int argc, VALUE *argv);
VALUE rb_get_backtrace(void);
void rb_define_core(void);
void rb_init_string(void); void rb_init_array(void); void rb_init_hash(void);
void rb_init_numeric(void); void rb_init_object(void); void rb_init_enum(void);
void rb_init_proc(void); void rb_init_range(void); void rb_init_regexp(void);
void rb_init_struct(void); void rb_init_exception(void); void rb_init_symbol(void);
void rb_init_enumerator(void); void rb_init_misc(void);
VALUE rb_enumerable_each(VALUE obj, RProc *blk);
VALUE rb_block_call(VALUE recv, ID mid, int argc, VALUE *argv,
                    VALUE (*fn)(VALUE yielded, VALUE ctx, int argc, VALUE *argv), VALUE ctx);
RProc *rb_cfunc_proc(VALUE (*fn)(VALUE, int, VALUE*, RProc*), VALUE data, int arity);

/* regexp engine */
struct regex *rb_reg_compile(const char *pat, long len, int opts, const char **err);
int rb_reg_search(struct regex *re, const char *str, long len, long start,
                  long *beg, long *end, int *ngroups);
int rb_reg_ngroups(struct regex *re);
int rb_reg_name_count(struct regex *re);
const char *rb_reg_name_at(struct regex *re, int i, int *idx);
VALUE rb_reg_new_str(VALUE src, int opts);
extern VALUE rb_backref;
void rb_set_backref(VALUE m);
VALUE rb_get_backref(void);

enum { RE_IGNORECASE = 1, RE_EXTENDED = 2, RE_MULTILINE = 4 };

/* encodings */
long rb_enc_mbclen(rb_encoding *enc, const char *p, const char *e);
int  rb_enc_codepoint(rb_encoding *enc, const char *p, const char *e, int *len);
long rb_enc_codelen(rb_encoding *enc, int c);
long rb_enc_mbcput(rb_encoding *enc, int c, char *buf);
rb_encoding *rb_enc_find(const char *name);
VALUE rb_enc_obj(rb_encoding *enc);

/* Interning a literal name is a hash of the string; cache it per call site.
   ID 0 is never handed out (rb_init burns it), so 0 means "not yet cached". */
#define ID_CACHED(s) __extension__({ static ID _cached_id_; \
    if(__builtin_expect(_cached_id_ == 0, 0)) _cached_id_ = rb_intern(s); _cached_id_; })

extern int rb_target_version;   /* 18,20,27,31,33,34 */
#endif
