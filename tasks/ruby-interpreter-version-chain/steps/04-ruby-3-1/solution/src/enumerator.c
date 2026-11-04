/* enumerator.c -- Fiber, Enumerator, Enumerator::Yielder, Enumerator::Lazy. */
#include "internal.h"

#if RUBY_TARGET >= 20
#include <ucontext.h>
#endif

extern VALUE rb_obj_alloc_of(RClass *);

VALUE rb_lazy_new(VALUE src);

static ID id_each, id_size, id_rewind, id_with_index, id_each_with_index,
          id_with_object, id_each_with_object, id_result;
#if RUBY_TARGET >= 20
static RClass *cYielder, *cGenerator;
#endif

static inline int is_ary(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_ARRAY; }
static inline int is_str(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_STRING; }
static inline int is_proc(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_PROC; }
static inline int is_enum(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_ENUMERATOR; }

/* a block that yields n values delivers them to C as one packed value */
static VALUE pack_values(int argc, VALUE *argv){
  if(argc <= 0) return Qnil;
  if(argc == 1) return argv[0];
  return rb_ary_new_from(argc, argv);
}
static void cat(VALUE s, VALUE v){ rb_str_cat(s, RSTR(v)->ptr, RSTR(v)->len); }

/* ===================== Fiber ===================== */
#if RUBY_TARGET >= 20

#define FIBER_STACK_SIZE (256 * 1024)

enum { FIBER_CREATED = 0, FIBER_SUSPENDED, FIBER_RUNNING, FIBER_DEAD };

struct fiber_ctx {
  ucontext_t uc;               /* the fiber's own machine context */
  ucontext_t back;             /* the context that resumed it */
  char *stack;
  RFiber *fib;
  Frame *frame;                /* interpreter frame stack while suspended */
  rb_tag *tag;                 /* interpreter tag stack while suspended */
  VALUE args;                  /* arguments of the very first resume */
  VALUE err; int has_err;      /* exception escaping the fiber body */
  int started;
};

static struct fiber_ctx *fiber_cur;     /* NULL while running on the root fiber */
static struct fiber_ctx *fiber_launch;  /* handoff to fiber_entry */
static RFiber *fiber_root;

static void fiber_init(RFiber *f, RProc *blk){
  f->basic.type = T_FIBER;
  f->basic.klass = rb_cFiber;
  f->blk = blk;
  f->state = FIBER_CREATED;
  f->xfer = Qnil;
  struct fiber_ctx *fc = xcalloc(1, sizeof(struct fiber_ctx));
  fc->fib = f; fc->err = Qnil; fc->args = rb_ary_new();
  /* the fiber gets a frame stack of its own, rooted in a frame that copies
     the block's lexical context; its tag stack starts empty. */
  Frame *root = xcalloc(1, sizeof(Frame));
  root->self = (blk && blk->self) ? blk->self : Qnil;
  root->defclass = blk ? blk->defclass : NULL;
  root->cref = blk ? (cref_t*)blk->cref_list : NULL;
  root->home = root;
  fc->frame = root;
  fc->tag = NULL;
  f->ctx = fc;
}

static void fiber_entry(void){
  struct fiber_ctx *fc = fiber_launch;
  RFiber *fib = fc->fib;
  VALUE result = Qnil;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf) == 0){
    VALUE a = fc->args;
    result = rb_proc_invoke(fib->blk, (int)RARR(a)->len, RARR(a)->ptr, NULL, 0);
    TAG_POP(tag);
  } else {
    TAG_POP(tag);
    if(tag.state == TAG_RAISE){ fc->err = tag.retval; fc->has_err = 1; }
    else result = tag.retval;
  }
  fib->xfer = result;
  fib->state = FIBER_DEAD;
  swapcontext(&fc->uc, &fc->back);
}

static VALUE fiber_resume(RFiber *fib, int argc, VALUE *argv){
  struct fiber_ctx *fc = fib->ctx;
  if(fib == fiber_root || fib->state == FIBER_RUNNING || (fc && fc == fiber_cur))
    rb_raise(rb_eFiberError, "double resume");
  if(!fc || fib->state == FIBER_DEAD) rb_raise(rb_eFiberError, "dead fiber called");

  fc->args = rb_ary_new_from(argc, argv);
  fib->xfer = pack_values(argc, argv);
  if(!fc->started){
    fc->started = 1;
    fc->stack = xmalloc(FIBER_STACK_SIZE);
    getcontext(&fc->uc);
    fc->uc.uc_stack.ss_sp = fc->stack;
    fc->uc.uc_stack.ss_size = FIBER_STACK_SIZE;
    fc->uc.uc_link = NULL;
    makecontext(&fc->uc, fiber_entry, 0);
  }
  Frame *sframe = rb_curframe;
  rb_tag *stag = rb_curtag;
  struct fiber_ctx *sprev = fiber_cur;
  if(sprev) sprev->fib->state = FIBER_SUSPENDED;
  fiber_cur = fc;
  fiber_launch = fc;
  fib->state = FIBER_RUNNING;
  rb_curframe = fc->frame;
  rb_curtag = fc->tag;
  swapcontext(&fc->back, &fc->uc);
  fiber_cur = sprev;
  if(sprev) sprev->fib->state = FIBER_RUNNING;
  rb_curframe = sframe;
  rb_curtag = stag;
  if(fc->has_err){
    VALUE e = fc->err;
    fc->has_err = 0; fc->err = Qnil;
    rb_exc_raise(e);
  }
  return fib->xfer;
}

static VALUE fiber_yield(int argc, VALUE *argv){
  struct fiber_ctx *fc = fiber_cur;
  if(!fc) rb_raise(rb_eFiberError, "can't yield from root fiber");
  fc->fib->xfer = pack_values(argc, argv);
  fc->fib->state = FIBER_SUSPENDED;
  fc->frame = rb_curframe;
  fc->tag = rb_curtag;
  swapcontext(&fc->uc, &fc->back);
  return fc->fib->xfer;
}

static VALUE fiber_s_new(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv;
  if(!blk) rb_raise(rb_eArgError, "tried to create a Fiber without a block");
  RClass *k = (!FIXNUM_P(self) && BASIC(self)->type == T_CLASS) ? (RClass*)self : rb_cFiber;
  RFiber *f = xcalloc(1, sizeof(RFiber));
  fiber_init(f, blk);
  f->basic.klass = k;
  return (VALUE)f;
}
static VALUE fiber_m_resume(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  return fiber_resume((RFiber*)self, argc, argv);
}
static VALUE fiber_s_yield(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)self; (void)blk;
  return fiber_yield(argc, argv);
}
static VALUE fiber_m_alive(VALUE self, RProc *blk){
  (void)blk;
  return ((RFiber*)self)->state == FIBER_DEAD ? Qfalse : Qtrue;
}
static VALUE fiber_s_current(VALUE self, RProc *blk){
  (void)self; (void)blk;
  if(fiber_cur) return (VALUE)fiber_cur->fib;
  if(!fiber_root){
    RFiber *f = xcalloc(1, sizeof(RFiber));
    f->basic.type = T_FIBER; f->basic.klass = rb_cFiber;
    f->state = FIBER_RUNNING; f->xfer = Qnil;
    fiber_root = f;
  }
  return (VALUE)fiber_root;
}
#endif /* RUBY_TARGET >= 20 */

/* ===================== Enumerator::Yielder / Generator ===================== */
#if RUBY_TARGET >= 20

static VALUE yielder_new(RProc *blk){
  VALUE y = rb_obj_alloc_of(cYielder);
  rb_ivar_set(y, ID_CACHED("__blk"), blk ? (VALUE)blk : Qnil);
  return y;
}
static VALUE yielder_yield(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  VALUE p = rb_ivar_get(self, ID_CACHED("__blk"));
  if(!is_proc(p)) rb_raise(rb_eLocalJumpError, "no block given (yield)");
  return rb_proc_invoke((RProc*)p, argc, argv, NULL, 0);
}
static VALUE yielder_ltlt(VALUE self, int argc, VALUE *argv, RProc *blk){
  yielder_yield(self, argc, argv, blk);
  return self;
}
static VALUE generator_new(RProc *blk){
  VALUE g = rb_obj_alloc_of(cGenerator);
  rb_ivar_set(g, ID_CACHED("__blk"), (VALUE)blk);
  return g;
}
static VALUE generator_each(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv;
  VALUE p = rb_ivar_get(self, ID_CACHED("__blk"));
  if(!is_proc(p)) return self;
  return rb_proc_invoke((RProc*)p, 1, (VALUE[]){ yielder_new(blk) }, NULL, 0);
}
#endif

/* ===================== Enumerator ===================== */

#if RUBY_TARGET >= 20
static VALUE lazy_inspect(VALUE self);
#endif

static REnumerator *enum_alloc(RClass *k){
  REnumerator *e = xcalloc(1, sizeof(REnumerator));
  e->basic.type = T_ENUMERATOR;
  e->basic.klass = k ? k : rb_cEnumerator;
  e->obj = Qnil;
  e->mid = id_each;
  e->args = rb_ary_new();
  e->sizev = Qnil;
  return e;
}

VALUE rb_enum_new(VALUE obj, ID mid, int argc, VALUE *argv){
  REnumerator *e = enum_alloc(rb_cEnumerator);
  e->obj = obj;
  e->mid = mid;
  e->args = rb_ary_new_from(argc, argv);
  return (VALUE)e;
}

#if RUBY_TARGET >= 20
static VALUE enum_s_new(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(!blk) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1+)", argc);
  RClass *k = (!FIXNUM_P(self) && BASIC(self)->type == T_CLASS) ? (RClass*)self : rb_cEnumerator;
  REnumerator *e = enum_alloc(k);
  e->gen = blk;
  e->obj = generator_new(blk);
  e->sizev = argc > 0 ? argv[0] : Qnil;
  for(RClass *c = k; c; c = c->super)
    if(c == rb_cLazy){ e->is_lazy = 1; e->lazy_ops = (RArray*)rb_ary_new(); break; }
  return (VALUE)e;
}
#endif

/* obj.mid(*stored_args, *extra) { ... } */
static VALUE enum_each(VALUE self, int argc, VALUE *argv, RProc *blk){
  REnumerator *e = RENUM(self);
  long n0 = RARR(e->args)->len;
  if(!blk){
    if(argc == 0) return self;
    REnumerator *n = enum_alloc(BASIC(self)->klass);
    n->obj = e->obj; n->mid = e->mid; n->gen = e->gen; n->sizev = e->sizev;
    n->is_lazy = e->is_lazy; n->lazy_ops = e->lazy_ops;
    VALUE a = rb_ary_new();
    for(long i = 0; i < n0; i++) rb_ary_push(a, RARR(e->args)->ptr[i]);
    for(int i = 0; i < argc; i++) rb_ary_push(a, argv[i]);
    n->args = a;
    return (VALUE)n;
  }
#if RUBY_TARGET >= 20
  if(e->gen) return rb_proc_invoke(e->gen, 1, (VALUE[]){ yielder_new(blk) }, NULL, 0);
#endif
  int n = (int)n0 + argc;
  VALUE *av = NULL;
  if(n > 0){
    av = xmalloc(sizeof(VALUE) * (size_t)n);
    for(long i = 0; i < n0; i++) av[i] = RARR(e->args)->ptr[i];
    for(int i = 0; i < argc; i++) av[n0 + i] = argv[i];
  }
  return rb_call(e->obj, e->mid, n, av, blk);
}

static VALUE collect_cb(VALUE y, VALUE ctx, int argc, VALUE *argv){
  (void)y;
  rb_ary_push(ctx, pack_values(argc, argv));
  return Qnil;
}
static VALUE enum_entries(VALUE self, int argc, VALUE *argv){
  VALUE a = rb_ary_new();
  rb_block_call(self, id_each, argc, argv, collect_cb, a);
  return a;
}
static VALUE enum_to_a(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  return enum_entries(self, argc, argv);
}

/* ---- external iteration: next / peek / rewind ---- */

typedef struct ExtIter {
  RFiber fib;                  /* first member: e->fib points here */
  VALUE peeked; int have_peek;
  int done; VALUE result;
  VALUE lst; long idx;
} ExtIter;

#if RUBY_TARGET >= 20
static VALUE ext_yield_cb(VALUE y, VALUE ctx, int argc, VALUE *argv){
  (void)y; (void)ctx;
  VALUE v = pack_values(argc, argv);
  return fiber_yield(1, &v);
}
static VALUE ext_fiber_body(VALUE data, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk;
  return rb_block_call(data, id_each, 0, NULL, ext_yield_cb, Qnil);
}
#endif

static ExtIter *ext_iter(REnumerator *e){
  ExtIter *x = (ExtIter*)e->fib;
  if(x) return x;
  x = xcalloc(1, sizeof(ExtIter));
  x->peeked = Qnil; x->result = Qnil; x->lst = Qnil;
#if RUBY_TARGET >= 20
  fiber_init(&x->fib, rb_cfunc_proc(ext_fiber_body, (VALUE)e, 0));
#else
  x->lst = enum_entries((VALUE)e, 0, NULL);
#endif
  e->fib = (RFiber*)x;
  return x;
}

static VALUE ext_pull(REnumerator *e, int *eof){
  ExtIter *x = ext_iter(e);
  *eof = 0;
#if RUBY_TARGET >= 20
  if(x->done){ *eof = 1; return Qnil; }
  VALUE v = fiber_resume(&x->fib, 0, NULL);
  if(x->fib.state == FIBER_DEAD){
    x->done = 1; x->result = v; *eof = 1; return Qnil;
  }
  return v;
#else
  if(x->idx >= RARR(x->lst)->len){ *eof = 1; return Qnil; }
  return RARR(x->lst)->ptr[x->idx++];
#endif
}
static VALUE ext_next(REnumerator *e, int *eof){
  ExtIter *x = ext_iter(e);
  if(x->have_peek){ x->have_peek = 0; *eof = 0; return x->peeked; }
  return ext_pull(e, eof);
}
static VALUE ext_peek(REnumerator *e, int *eof){
  ExtIter *x = ext_iter(e);
  if(x->have_peek){ *eof = 0; return x->peeked; }
  VALUE v = ext_pull(e, eof);
  if(!*eof){ x->peeked = v; x->have_peek = 1; }
  return v;
}
static void raise_stop(REnumerator *e) __attribute__((noreturn));
static void raise_stop(REnumerator *e){
  VALUE exc = rb_exc_new(rb_eStopIteration, rb_str_new_cstr("iteration reached an end"));
  rb_ivar_set(exc, id_result, ext_iter(e)->result);
  rb_exc_raise(exc);
}

static VALUE enum_next(VALUE self, RProc *blk){
  (void)blk;
  int eof = 0;
  VALUE v = ext_next(RENUM(self), &eof);
  if(eof) raise_stop(RENUM(self));
  return v;
}
static VALUE enum_peek(VALUE self, RProc *blk){
  (void)blk;
  int eof = 0;
  VALUE v = ext_peek(RENUM(self), &eof);
  if(eof) raise_stop(RENUM(self));
  return v;
}
static VALUE enum_rewind(VALUE self, RProc *blk){
  (void)blk;
  REnumerator *e = RENUM(self);
  e->fib = NULL;
  if(!NIL_P(e->obj) && rb_respond_to(e->obj, id_rewind)) rb_funcall(e->obj, id_rewind, 0);
  return self;
}

static VALUE enum_size(VALUE self, RProc *blk){
  (void)blk;
  REnumerator *e = RENUM(self);
  if(is_proc(e->sizev)) return rb_proc_call((RProc*)e->sizev, 0, NULL, NULL);
  if(!NIL_P(e->sizev)) return e->sizev;
  if(!e->gen && RARR(e->args)->len == 0 && !NIL_P(e->obj) && rb_respond_to(e->obj, id_size))
    return rb_funcall(e->obj, id_size, 0);
  return Qnil;
}

/* ---- with_index / with_object ---- */

typedef struct IdxCtx { RProc *blk; long idx; } IdxCtx;
static VALUE widx_cb(VALUE y, VALUE ctx, int argc, VALUE *argv){
  (void)y;
  IdxCtx *c = (IdxCtx*)ctx;
  VALUE a[2];
  a[0] = pack_values(argc, argv);
  a[1] = rb_int_new((long long)c->idx++);
  return rb_yield_values(c->blk, 2, a);
}
static VALUE enum_with_index(VALUE self, int argc, VALUE *argv, RProc *blk){
  long off = (argc > 0 && !NIL_P(argv[0])) ? rb_num2long(argv[0]) : 0;
  if(!blk) return rb_enum_new(self, id_with_index, argc, argv);
  IdxCtx c; c.blk = blk; c.idx = off;
  return rb_block_call(self, id_each, 0, NULL, widx_cb, (VALUE)&c);
}
static VALUE enum_each_with_index(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(!blk) return rb_enum_new(self, id_each_with_index, argc, argv);
  IdxCtx c; c.blk = blk; c.idx = 0;
  return rb_block_call(self, id_each, 0, NULL, widx_cb, (VALUE)&c);
}

typedef struct MemoCtx { RProc *blk; VALUE memo; } MemoCtx;
static VALUE wobj_cb(VALUE y, VALUE ctx, int argc, VALUE *argv){
  (void)y;
  MemoCtx *c = (MemoCtx*)ctx;
  VALUE a[2];
  a[0] = pack_values(argc, argv);
  a[1] = c->memo;
  return rb_yield_values(c->blk, 2, a);
}
static VALUE enum_with_object(VALUE self, VALUE memo, RProc *blk){
  if(!blk) return rb_enum_new(self, id_with_object, 1, &memo);
  MemoCtx c; c.blk = blk; c.memo = memo;
  rb_block_call(self, id_each, 0, NULL, wobj_cb, (VALUE)&c);
  return memo;
}
static VALUE enum_each_with_object(VALUE self, VALUE memo, RProc *blk){
  if(!blk) return rb_enum_new(self, id_each_with_object, 1, &memo);
  MemoCtx c; c.blk = blk; c.memo = memo;
  rb_block_call(self, id_each, 0, NULL, wobj_cb, (VALUE)&c);
  return memo;
}

/* ---- inspect ---- */

static void append_args(VALUE s, VALUE args){
  long n = RARR(args)->len;
  if(n == 0) return;
  rb_str_cat_cstr(s, "(");
  for(long i = 0; i < n; i++){
    if(i) rb_str_cat_cstr(s, ", ");
    cat(s, rb_inspect(RARR(args)->ptr[i]));
  }
  rb_str_cat_cstr(s, ")");
}
static VALUE enum_inspect(VALUE self, RProc *blk){
  (void)blk;
  REnumerator *e = RENUM(self);
#if RUBY_TARGET >= 20
  if(e->is_lazy) return lazy_inspect(self);
#endif
  VALUE s = rb_str_new_cstr("#<");
  rb_str_cat_cstr(s, rb_obj_classname(self));
  rb_str_cat_cstr(s, ": ");
  cat(s, rb_inspect(e->obj));
  rb_str_cat_cstr(s, ":");
  rb_str_cat_cstr(s, rb_id2name(e->mid));
  append_args(s, e->args);
  rb_str_cat_cstr(s, ">");
  return s;
}

/* ===================== Enumerator::Lazy ===================== */
#if RUBY_TARGET >= 20

enum { OP_MAP = 1, OP_SELECT, OP_REJECT, OP_TAKE, OP_TAKE_WHILE, OP_DROP,
       OP_DROP_WHILE, OP_FLAT_MAP, OP_FILTER_MAP, OP_WITH_INDEX, OP_UNIQ,
       OP_COMPACT, OP_LAST };
static ID id_op[OP_LAST];

static int op_code(ID id){
  for(int i = 1; i < OP_LAST; i++) if(id_op[i] == id) return i;
  return 0;
}

static VALUE lazy_alloc(VALUE obj, ID mid, VALUE args, RArray *ops){
  REnumerator *e = enum_alloc(rb_cLazy);
  e->obj = obj; e->mid = mid; e->args = args;
  e->is_lazy = 1;
  e->lazy_ops = ops ? ops : (RArray*)rb_ary_new();
  return (VALUE)e;
}

VALUE rb_lazy_new(VALUE src){
  if(is_enum(src) && RENUM(src)->is_lazy){
    REnumerator *s = RENUM(src);
    VALUE ops = rb_ary_new();
    if(s->lazy_ops)
      for(long i = 0; i < s->lazy_ops->len; i++) rb_ary_push(ops, s->lazy_ops->ptr[i]);
    return lazy_alloc(s->obj, s->mid, s->args, (RArray*)ops);
  }
  return lazy_alloc(src, id_each, rb_ary_new(), NULL);
}

/* a new Lazy = this chain plus one more [op, data] pair */
static VALUE lazy_add(VALUE self, int code, VALUE data){
  REnumerator *e = RENUM(self);
  VALUE ops = rb_ary_new();
  if(e->lazy_ops)
    for(long i = 0; i < e->lazy_ops->len; i++) rb_ary_push(ops, e->lazy_ops->ptr[i]);
  rb_ary_push(ops, rb_sym(id_op[code]));
  rb_ary_push(ops, data);
  return lazy_alloc(e->obj, e->mid, e->args, (RArray*)ops);
}

typedef struct LazyRun {
  RArray *ops;
  int nops;
  VALUE *st;                   /* one state slot per op */
  RProc *blk;                  /* consumer block, or NULL */
  VALUE acc;                   /* accumulator array, or 0 */
} LazyRun;

static VALUE call1(VALUE pr, VALUE v){ return rb_proc_call((RProc*)pr, 1, &v, NULL); }

static void lazy_feed(LazyRun *r, int i, VALUE v){
  if(i >= r->nops){
    if(r->acc) rb_ary_push(r->acc, v);
    else if(r->blk) rb_yield1(r->blk, v);
    return;
  }
  VALUE data = r->ops->ptr[i*2 + 1];
  switch(op_code(RSYM(r->ops->ptr[i*2])->id)){
    case OP_MAP:
      lazy_feed(r, i+1, call1(data, v));
      break;
    case OP_SELECT:
      if(RTEST(call1(data, v))) lazy_feed(r, i+1, v);
      break;
    case OP_REJECT:
      if(!RTEST(call1(data, v))) lazy_feed(r, i+1, v);
      break;
    case OP_TAKE: {
      long lim = FIX2LONG(data), c = FIX2LONG(r->st[i]);
      if(c >= lim) rb_jump_tag(TAG_BREAK, Qnil, NULL);
      r->st[i] = LONG2FIX(c + 1);
      lazy_feed(r, i+1, v);
      if(c + 1 >= lim) rb_jump_tag(TAG_BREAK, Qnil, NULL);
      break; }
    case OP_TAKE_WHILE:
      if(!RTEST(call1(data, v))) rb_jump_tag(TAG_BREAK, Qnil, NULL);
      lazy_feed(r, i+1, v);
      break;
    case OP_DROP: {
      long lim = FIX2LONG(data), c = FIX2LONG(r->st[i]);
      if(c < lim){ r->st[i] = LONG2FIX(c + 1); break; }
      lazy_feed(r, i+1, v);
      break; }
    case OP_DROP_WHILE:
      if(r->st[i] == LONG2FIX(0)){
        if(RTEST(call1(data, v))) break;
        r->st[i] = LONG2FIX(1);
      }
      lazy_feed(r, i+1, v);
      break;
    case OP_FLAT_MAP: {
      VALUE x = call1(data, v);
      VALUE a = (FIXNUM_P(x) || is_str(x)) ? Qnil : rb_check_array_type(x);
      if(NIL_P(a)) lazy_feed(r, i+1, x);
      else for(long k = 0; k < RARR(a)->len; k++) lazy_feed(r, i+1, RARR(a)->ptr[k]);
      break; }
    case OP_FILTER_MAP: {
      VALUE x = call1(data, v);
      if(RTEST(x)) lazy_feed(r, i+1, x);
      break; }
    case OP_WITH_INDEX: {
      long c = FIX2LONG(r->st[i]);
      r->st[i] = LONG2FIX(c + 1);
      VALUE idx = rb_int_new((long long)rb_num2long(RARR(data)->ptr[0]) + c);
      VALUE pr = RARR(data)->ptr[1];
      VALUE a[2]; a[0] = v; a[1] = idx;
      if(is_proc(pr)) lazy_feed(r, i+1, rb_proc_call((RProc*)pr, 2, a, NULL));
      else lazy_feed(r, i+1, rb_ary_new_from(2, a));
      break; }
    case OP_UNIQ: {
      if(FIXNUM_P(r->st[i])) r->st[i] = rb_hash_new();
      VALUE key = is_proc(data) ? call1(data, v) : v;
      if(rb_hash_lookup(r->st[i], key, Qundef) != Qundef) break;
      rb_hash_aset(r->st[i], key, Qtrue);
      lazy_feed(r, i+1, v);
      break; }
    case OP_COMPACT:
      if(!NIL_P(v)) lazy_feed(r, i+1, v);
      break;
    default:
      lazy_feed(r, i+1, v);
      break;
  }
}

static VALUE lazy_cb(VALUE y, VALUE ctx, int argc, VALUE *argv){
  (void)y;
  lazy_feed((LazyRun*)ctx, 0, pack_values(argc, argv));
  return Qnil;
}

/* walk the source once, pushing every element through the op chain */
static void lazy_run(VALUE self, RProc *blk, VALUE acc){
  REnumerator *e = RENUM(self);
  LazyRun r;
  r.ops = e->lazy_ops;
  r.nops = r.ops ? (int)(r.ops->len / 2) : 0;
  r.st = r.nops ? xmalloc(sizeof(VALUE) * (size_t)r.nops) : NULL;
  for(int i = 0; i < r.nops; i++) r.st[i] = LONG2FIX(0);
  r.blk = blk;
  r.acc = acc;
  int argc = (int)RARR(e->args)->len;
  VALUE *argv = argc ? RARR(e->args)->ptr : NULL;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf) == 0){
    rb_block_call(e->obj, e->mid, argc, argv, lazy_cb, (VALUE)&r);
    TAG_POP(tag);
  } else {
    TAG_POP(tag);
    if(tag.state != TAG_BREAK) rb_jump_tag(tag.state, tag.retval, tag.target);
  }
}

static VALUE lazy_force(VALUE self, RProc *blk){
  (void)blk;
  VALUE a = rb_ary_new();
  lazy_run(self, NULL, a);
  return a;
}
static VALUE lazy_each(VALUE self, RProc *blk){
  if(!blk) return self;
  lazy_run(self, blk, 0);
  return self;
}

static RProc *need_block(RProc *blk, const char *name){
  if(!blk) rb_raise(rb_eArgError, "tried to call lazy %s without a block", name);
  return blk;
}
static VALUE lazy_map(VALUE self, RProc *blk){
  return lazy_add(self, OP_MAP, (VALUE)need_block(blk, "map")); }
static VALUE lazy_select(VALUE self, RProc *blk){
  return lazy_add(self, OP_SELECT, (VALUE)need_block(blk, "select")); }
static VALUE lazy_reject(VALUE self, RProc *blk){
  return lazy_add(self, OP_REJECT, (VALUE)need_block(blk, "reject")); }
static VALUE lazy_take_while(VALUE self, RProc *blk){
  return lazy_add(self, OP_TAKE_WHILE, (VALUE)need_block(blk, "take_while")); }
static VALUE lazy_drop_while(VALUE self, RProc *blk){
  return lazy_add(self, OP_DROP_WHILE, (VALUE)need_block(blk, "drop_while")); }
static VALUE lazy_flat_map(VALUE self, RProc *blk){
  return lazy_add(self, OP_FLAT_MAP, (VALUE)need_block(blk, "flat_map")); }
#if RUBY_TARGET >= 27
static VALUE lazy_filter_map(VALUE self, RProc *blk){
  return lazy_add(self, OP_FILTER_MAP, (VALUE)need_block(blk, "filter_map")); }
#endif
static VALUE lazy_take(VALUE self, VALUE n, RProc *blk){
  (void)blk;
  long v = rb_num2long(n);
  if(v < 0) rb_raise(rb_eArgError, "attempt to take negative size");
  return lazy_add(self, OP_TAKE, LONG2FIX(v)); }
static VALUE lazy_drop(VALUE self, VALUE n, RProc *blk){
  (void)blk;
  long v = rb_num2long(n);
  if(v < 0) rb_raise(rb_eArgError, "attempt to drop negative size");
  return lazy_add(self, OP_DROP, LONG2FIX(v)); }
#if RUBY_TARGET >= 27
static VALUE lazy_uniq(VALUE self, RProc *blk){
  return lazy_add(self, OP_UNIQ, blk ? (VALUE)blk : Qnil); }
#endif
#if RUBY_TARGET >= 33
static VALUE lazy_compact(VALUE self, RProc *blk){
  (void)blk;
  return lazy_add(self, OP_COMPACT, Qnil); }
#endif
static VALUE lazy_with_index(VALUE self, int argc, VALUE *argv, RProc *blk){
  VALUE d[2];
  d[0] = (argc > 0 && !NIL_P(argv[0])) ? argv[0] : LONG2FIX(0);
  d[1] = blk ? (VALUE)blk : Qnil;
  return lazy_add(self, OP_WITH_INDEX, rb_ary_new_from(2, d)); }
static VALUE lazy_first(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long n = argc > 0 ? rb_num2long(argv[0]) : 1;
  if(n < 0) rb_raise(rb_eArgError, "attempt to take negative size");
  VALUE a = lazy_force(lazy_add(self, OP_TAKE, LONG2FIX(n)), NULL);
  if(argc == 0) return RARR(a)->len ? RARR(a)->ptr[0] : Qnil;
  return a; }
static VALUE lazy_lazy(VALUE self, RProc *blk){ (void)blk; return self; }
#if RUBY_TARGET >= 27
static VALUE lazy_eager(VALUE self, RProc *blk){
  (void)blk;
  return rb_enum_new(self, id_each, 0, NULL); }
#endif

static VALUE lazy_inspect(VALUE self){
  REnumerator *e = RENUM(self);
  const char *cn = rb_obj_classname(self);
  VALUE s = rb_str_new_cstr("#<");
  rb_str_cat_cstr(s, cn);
  rb_str_cat_cstr(s, ": ");
  cat(s, rb_inspect(e->obj));
  if(e->mid != id_each || RARR(e->args)->len){
    rb_str_cat_cstr(s, ":");
    rb_str_cat_cstr(s, rb_id2name(e->mid));
    append_args(s, e->args);
  }
  rb_str_cat_cstr(s, ">");
  RArray *o = e->lazy_ops;
  for(long i = 0; o && i < o->len; i += 2){
    VALUE t = rb_str_new_cstr("#<");
    rb_str_cat_cstr(t, cn);
    rb_str_cat_cstr(t, ": ");
    cat(t, s);
    rb_str_cat_cstr(t, ":");
    rb_str_cat_cstr(t, rb_id2name(RSYM(o->ptr[i])->id));
    if(FIXNUM_P(o->ptr[i+1])){
      rb_str_cat_cstr(t, "(");
      cat(t, rb_inspect(o->ptr[i+1]));
      rb_str_cat_cstr(t, ")");
    }
    rb_str_cat_cstr(t, ">");
    s = t;
  }
  return s;
}
#else
VALUE rb_lazy_new(VALUE src){ return rb_enum_new(src, id_each, 0, NULL); }
#endif /* RUBY_TARGET >= 20 */

/* ===================== registration ===================== */

void rb_init_enumerator(void){
  id_each = ID_CACHED("each");
  id_size = ID_CACHED("size");
  id_rewind = ID_CACHED("rewind");
  id_with_index = ID_CACHED("with_index");
  id_each_with_index = ID_CACHED("each_with_index");
  id_with_object = ID_CACHED("with_object");
  id_each_with_object = ID_CACHED("each_with_object");
  id_result = ID_CACHED("@result");

#if RUBY_TARGET >= 20
  cYielder = rb_define_class_under(rb_cEnumerator, "Yielder", rb_cObject);
  cGenerator = rb_define_class_under(rb_cEnumerator, "Generator", rb_cObject);
  rb_define_method(cYielder, "yield", yielder_yield, ARGC_ANY);
  rb_define_method(cYielder, "call", yielder_yield, ARGC_ANY);
  rb_define_method(cYielder, "<<", yielder_ltlt, ARGC_ANY);
  rb_define_method(cGenerator, "each", generator_each, ARGC_ANY);
#endif

  RClass *E = rb_cEnumerator;
  rb_define_method(E, "each", enum_each, ARGC_ANY);
  rb_define_method(E, "next", enum_next, 0);
  rb_define_method(E, "peek", enum_peek, 0);
  rb_define_method(E, "rewind", enum_rewind, 0);
  rb_define_method(E, "size", enum_size, 0);
  rb_define_method(E, "with_index", enum_with_index, ARGC_ANY);
  rb_define_method(E, "each_with_index", enum_each_with_index, ARGC_ANY);
  rb_define_method(E, "with_object", enum_with_object, 1);
  rb_define_method(E, "each_with_object", enum_each_with_object, 1);
  rb_define_method(E, "to_a", enum_to_a, ARGC_ANY);
  rb_define_method(E, "entries", enum_to_a, ARGC_ANY);
  rb_define_method(E, "inspect", enum_inspect, 0);
#if RUBY_TARGET >= 20
  rb_define_singleton_method((VALUE)E, "new", enum_s_new, ARGC_ANY);

  RClass *L = rb_cLazy;
  rb_define_method(L, "each", lazy_each, 0);
  rb_define_method(L, "map", lazy_map, 0);
  rb_define_method(L, "collect", lazy_map, 0);
  rb_define_method(L, "select", lazy_select, 0);
  rb_define_method(L, "reject", lazy_reject, 0);
  rb_define_method(L, "take", lazy_take, 1);
  rb_define_method(L, "take_while", lazy_take_while, 0);
  rb_define_method(L, "drop", lazy_drop, 1);
  rb_define_method(L, "drop_while", lazy_drop_while, 0);
  rb_define_method(L, "flat_map", lazy_flat_map, 0);
  rb_define_method(L, "collect_concat", lazy_flat_map, 0);
  rb_define_method(L, "with_index", lazy_with_index, ARGC_ANY);
  rb_define_method(L, "force", lazy_force, 0);
  rb_define_method(L, "to_a", lazy_force, 0);
  rb_define_method(L, "first", lazy_first, ARGC_ANY);
  rb_define_method(L, "lazy", lazy_lazy, 0);
#if RUBY_TARGET >= 27
  rb_define_method(L, "filter", lazy_select, 0);
  rb_define_method(L, "filter_map", lazy_filter_map, 0);
  rb_define_method(L, "uniq", lazy_uniq, 0);
  rb_define_method(L, "eager", lazy_eager, 0);
#endif
#if RUBY_TARGET >= 33
  rb_define_method(L, "compact", lazy_compact, 0);
#endif
  id_op[OP_MAP] = ID_CACHED("map");
  id_op[OP_SELECT] = ID_CACHED("select");
  id_op[OP_REJECT] = ID_CACHED("reject");
  id_op[OP_TAKE] = ID_CACHED("take");
  id_op[OP_TAKE_WHILE] = ID_CACHED("take_while");
  id_op[OP_DROP] = ID_CACHED("drop");
  id_op[OP_DROP_WHILE] = ID_CACHED("drop_while");
  id_op[OP_FLAT_MAP] = ID_CACHED("flat_map");
  id_op[OP_FILTER_MAP] = ID_CACHED("filter_map");
  id_op[OP_WITH_INDEX] = ID_CACHED("with_index");
  id_op[OP_UNIQ] = ID_CACHED("uniq");
  id_op[OP_COMPACT] = ID_CACHED("compact");

  rb_define_singleton_method((VALUE)rb_cFiber, "new", fiber_s_new, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cFiber, "yield", fiber_s_yield, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cFiber, "current", fiber_s_current, 0);
  rb_define_method(rb_cFiber, "resume", fiber_m_resume, ARGC_ANY);
  rb_define_method(rb_cFiber, "alive?", fiber_m_alive, 0);
#endif
}
