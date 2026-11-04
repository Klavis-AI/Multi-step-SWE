#include "internal.h"

/* ===================== globals ===================== */
static ivtbl *gtbl;
VALUE rb_backref = 0;
static VALUE main_obj;
VALUE rb_gv_get(ID id){ int f; return ivtbl_get(gtbl,id,&f); }
void rb_gv_set(ID id, VALUE v){ ivtbl_set(&gtbl,id,v); }
int rb_gv_defined(ID id){ int f; ivtbl_get(gtbl,id,&f); return f; }
void rb_set_backref(VALUE m){ rb_backref = m; }
VALUE rb_get_backref(void){ return rb_backref ? rb_backref : Qnil; }

/* keyword arguments in flight between a call site and parameter binding */
static VALUE g_pending_kw;
static int g_has_pending_kw;

static ID id_call, id_to_ary, id_to_a, id_to_proc, id_eqq, id_each,
  id_method_missing, id_initialize, id_to_hash, id_deconstruct, id_deconstruct_keys;

/* ===================== env ===================== */
/* A method whose body cannot make a non-local jump does not need a tag. */
static int node_can_jump(Node *n){
  if(!n) return 0;
  switch(n->type){
    case N_RETURN: case N_RETRY: case N_BREAK: case N_NEXT: case N_REDO: return 1;
    case N_DEF: case N_DEFS: case N_CLASS: case N_MODULE: case N_SCLASS: return 0;
    default: break;
  }
  if(node_can_jump(n->a) || node_can_jump(n->b) || node_can_jump(n->c) || node_can_jump(n->d))
    return 1;
  for(int i=0;i<n->nitems;i++) if(node_can_jump(n->items[i])) return 1;
  return 0;
}
static Env empty_env_storage;

/* One allocation for the scope and its slots: method calls are the hot path. */
static Env *env_new(LocalTbl *tbl, Env *parent){
  int n = tbl ? tbl->n : 0;
  Env *e = xmalloc(sizeof(Env) + (size_t)n * sizeof(VALUE));
  e->n = n; e->parent = parent; e->tbl = tbl;
  e->vars = n ? (VALUE*)(e + 1) : NULL;
  for(int i=0;i<n;i++) e->vars[i] = Qnil;
  return e;
}
static VALUE *env_slot_grow(Env *e, int idx);
static inline VALUE *env_slot(Env *e, int depth, int idx){
  while(depth-- > 0 && e) e = e->parent;
  if(!e) return NULL;
  if(__builtin_expect(idx < e->n, 1)) return &e->vars[idx];
  return env_slot_grow(e, idx);
}
static VALUE *env_slot_grow(Env *e, int idx){
  {
    int old = e->n;
    int want = e->tbl ? e->tbl->n : idx+1;
    if(want <= idx) want = idx+1;
    VALUE *nv = xmalloc(sizeof(VALUE)*want);
    memcpy(nv, e->vars, sizeof(VALUE)*old);
    e->vars = nv;
    for(int i=old;i<want;i++) e->vars[i]=Qnil;
    e->n = want;
  }
  return &e->vars[idx];
}

/* ===================== frames ===================== */
static Frame toplevel_frame;
static cref_t toplevel_cref;

int rb_block_given(void){
  Frame *f = rb_curframe;
  while(f && f->is_block) f = f->home;
  return f && f->blk;
}

/* ===================== helper: argument evaluation ===================== */
typedef struct ArgList { VALUE *v; int n, cap; VALUE kw; int has_kw; VALUE buf[8]; } ArgList;
static void al_init(ArgList *a){ a->v=a->buf; a->n=0; a->cap=8; a->kw=0; a->has_kw=0; }
static void al_push(ArgList *a, VALUE v){
  if(a->n>=a->cap){
    int nc = a->cap*2;
    VALUE *nv = xmalloc(nc*sizeof(VALUE));
    memcpy(nv, a->v, a->n*sizeof(VALUE));
    a->v = nv; a->cap = nc;
  }
  a->v[a->n++]=v;
}

static VALUE eval_hash_node(Node *h, Frame *f);

static RProc *eval_block_arg(Node *call, Frame *f);

static void eval_args(Node *call, Frame *f, ArgList *out){
  al_init(out);
  for(int i=0;i<call->nitems;i++){
    Node *a = call->items[i];
    if(a->type==N_SPLAT){
      VALUE v = rb_eval(a->a,f);
      VALUE ary = rb_check_array_type(v);
      if(NIL_P(ary)){ al_push(out,v); }
      else for(long k=0;k<RARR(ary)->len;k++) al_push(out, RARR(ary)->ptr[k]);
    } else if(a->type==N_ANONSPLAT){
      int d,i2; VALUE *slot = NULL;
      ID aid = ID_CACHED("__anon_rest");
      Node *tmp = NULL; (void)tmp; (void)d; (void)i2; (void)slot;
      VALUE v = Qnil;
      /* look up the anonymous rest parameter in the current scope chain */
      Env *e = f->env; LocalTbl *t = e? e->tbl : NULL;
      if(t) for(int k=0;k<t->n;k++) if(t->names[k]==aid){ v = e->vars[k]; break; }
      if(!NIL_P(v)) for(long k=0;k<RARR(v)->len;k++) al_push(out, RARR(v)->ptr[k]);
    } else {
      al_push(out, rb_eval(a,f));
    }
  }
  if(call->d){
    VALUE h = eval_hash_node(call->d,f);
    out->kw = h; out->has_kw = 1;
  }
}

/* ===================== inspect helpers used by errors ===================== */
static void arity_error(int given, int min, int max){
  if(max < 0) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %d+)", given, min);
  else if(min==max) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %d)", given, min);
  else rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %d..%d)", given, min, max);
}

/* ===================== parameter binding ===================== */
typedef struct ParamInfo {
  int nreq, nopt, nrest, npost, nkey, nkeyreq, nkeyrest, nblock, nokey;
  Node *rest, *keyrest, *block;
} ParamInfo;

static void param_info(Node *params, ParamInfo *pi){
  memset(pi,0,sizeof *pi);
  if(!params) return;
  for(int i=0;i<params->nitems;i++){
    Node *e = params->items[i];
    switch(e->num){
      case PARAM_REQ: case PARAM_DESTRUCT: pi->nreq++; break;
      case PARAM_OPT: pi->nopt++; break;
      case PARAM_REST: pi->nrest=1; pi->rest=e; break;
      case PARAM_POST: pi->npost++; break;
      case PARAM_KEY: pi->nkey++; break;
      case PARAM_KEYREQ: pi->nkeyreq++; break;
      case PARAM_KEYREST: pi->nkeyrest=1; pi->keyrest=e; break;
      case PARAM_BLOCK: pi->nblock=1; pi->block=e; break;
      case PARAM_NOKEY: pi->nokey=1; break;
    }
  }
}
static int params_accept_kw(Node *params){
  ParamInfo pi; param_info(params,&pi);
  return pi.nkey || pi.nkeyreq || pi.nkeyrest;
}

static void destructure_into(Node *sub, Env *env, VALUE val);

/* A parameter slot.  Ruby 1.8 block parameters may alias a local of the
   enclosing scope, which the parser records as a depth in the high bits. */
static VALUE *param_slot(Env *env, Node *e){
  return env_slot(env, e->flags >> 20, e->flags & 0xFFFFF);
}
static void bind_one(Node *e, Env *env, VALUE v){
  if(e->num==PARAM_DESTRUCT){ destructure_into(e->a, env, v); return; }
  VALUE *slot = param_slot(env, e);
  if(slot) *slot = v;
}
static void destructure_into(Node *sub, Env *env, VALUE val){
  VALUE ary = rb_check_array_type(val);
  if(NIL_P(ary)){ ary = rb_ary_new(); rb_ary_push(ary,val); }
  long n = RARR(ary)->len;
  int nreq_pre=0, nrest=-1, nreq_post=0;
  for(int i=0;i<sub->nitems;i++){
    if(sub->items[i]->num==PARAM_REST){ nrest=i; }
    else if(nrest<0) nreq_pre++;
    else nreq_post++;
  }
  long idx=0;
  for(int i=0;i<nreq_pre;i++) bind_one(sub->items[i],env, idx<n? RARR(ary)->ptr[idx++] : Qnil);
  if(nrest>=0){
    long take = n - idx - nreq_post; if(take<0) take=0;
    VALUE r = rb_ary_new();
    for(long k=0;k<take;k++) rb_ary_push(r, RARR(ary)->ptr[idx+k]);
    idx += take;
    if(sub->items[nrest]->id != (ID)-1 && sub->items[nrest]->id >= 0){
      VALUE *slot = param_slot(env, sub->items[nrest]); if(slot) *slot = r;
    }
    for(int i=nrest+1;i<sub->nitems;i++) bind_one(sub->items[i],env, idx<n? RARR(ary)->ptr[idx++] : Qnil);
  }
}

void rb_bind_params(Node *params, Frame *f, Env *env, int argc, VALUE *argv,
                    RProc *blk, int is_lambda, int is_block){
  /* the overwhelmingly common shape: N required positional parameters, no
     keywords in flight, no block parameter */
  if(!g_has_pending_kw && params && params->flags == 0){
    int simple = 1;
    for(int i=0;i<params->nitems;i++)
      if(params->items[i]->num != PARAM_REQ){ simple = 0; break; }
    if(simple){
      int nreq = params->nitems;
      if(argc == nreq){
        for(int i=0;i<nreq;i++){
          Node *e = params->items[i];
          VALUE *slot = env_slot(env, e->flags >> 20, e->flags & 0xFFFFF);
          if(slot) *slot = argv[i];
        }
        return;
      }
      if(is_lambda || !is_block) arity_error(argc, nreq, nreq);
    }
  }
  ParamInfo pi; param_info(params,&pi);
  VALUE kwhash = g_pending_kw;
  int have_kw = g_has_pending_kw;
  g_pending_kw = 0; g_has_pending_kw = 0;

  int accepts_kw = pi.nkey || pi.nkeyreq || pi.nkeyrest;

  if(have_kw && !accepts_kw){
    /* keywords passed to a method that takes none: becomes a positional Hash
       (all versions), unless it is empty under 3.0+ semantics */
    if(!(RHSH(kwhash)->size == 0 && rb_target_version >= 30)){
      VALUE *na = xmalloc(sizeof(VALUE)*(argc+1));
      memcpy(na,argv,sizeof(VALUE)*argc); na[argc]=kwhash; argv=na; argc++;
    }
    have_kw = 0;
  } else if(!have_kw && accepts_kw && argc > 0){
    /* pre-3.0: a trailing Hash with only Symbol keys is converted to keywords */
    if(rb_target_version < 30){
      VALUE last = argv[argc-1];
      if(!FIXNUM_P(last) && BASIC(last)->type==T_HASH){
        int allsym = 1;
        RHash *h = RHSH(last);
        for(long i=0;i<h->n;i++) if(h->ent[i].used && (FIXNUM_P(h->ent[i].key) || BASIC(h->ent[i].key)->type != T_SYMBOL)) { allsym=0; break; }
        long need = pi.nreq + pi.npost;
        if(allsym && argc > need){
          if(rb_target_version == 27)
            rb_warn_deprecated("Using the last argument as keyword parameters is deprecated; maybe ** should be added to the call");
          kwhash = last; have_kw = 1; argc--;
        }
      }
    }
  }
  if(have_kw && RHSH(kwhash)->size==0 && !pi.nkeyrest && rb_target_version>=30){ have_kw = 0; }

  /* block auto-splat: a non-lambda block with more than one formal parameter
     receiving a single Array argument spreads it */
  if(is_block && !is_lambda && argc==1){
    int nformal = pi.nreq + pi.nopt + pi.npost + pi.nrest;
    if(nformal > 1 || (pi.nrest && pi.nreq+pi.nopt+pi.npost>0) ||
       (params && params->nitems==1 && params->items[0]->num==PARAM_DESTRUCT)){
      VALUE a = rb_check_array_type(argv[0]);
      if(!NIL_P(a)){ argc = (int)RARR(a)->len; argv = RARR(a)->ptr; }
    }
  }

  int nmandatory = pi.nreq + pi.npost;
  if(is_lambda || !is_block){
    if(argc < nmandatory || (!pi.nrest && argc > nmandatory + pi.nopt))
      arity_error(argc, nmandatory, pi.nrest ? -1 : nmandatory + pi.nopt);
  }

  int idx = 0;
  int opt_avail = argc - nmandatory; if(opt_avail<0) opt_avail=0;
  int given_opt = opt_avail < pi.nopt ? opt_avail : pi.nopt;
  int post_start = argc - pi.npost; if(post_start < 0) post_start = 0;
  int nopt_seen = 0;
  int post_i = 0;
  for(int i=0;params && i<params->nitems;i++){
    Node *e = params->items[i];
    switch(e->num){
      case PARAM_REQ: case PARAM_DESTRUCT:
        bind_one(e, env, idx<argc? argv[idx] : Qnil); idx++; break;
      case PARAM_OPT:
        if(nopt_seen < given_opt){ bind_one(e,env, idx<argc?argv[idx]:Qnil); idx++; }
        else { VALUE *slot = param_slot(env, e); Frame sf = *f; sf.env = env;
               if(slot) *slot = rb_eval(e->a,&sf); }
        nopt_seen++; break;
      case PARAM_REST: {
        long take = argc - idx - pi.npost; if(take<0) take=0;
        VALUE r = rb_ary_new();
        for(long k=0;k<take;k++) rb_ary_push(r, argv[idx+k]);
        idx += take;
        if(e->id >= 0){ VALUE *slot = param_slot(env, e); if(slot) *slot = r; }
        break; }
      case PARAM_POST: {
        int k = post_start + post_i;
        bind_one(e, env, (k>=0 && k<argc)? argv[k] : Qnil);
        post_i++; break; }
      default: break;
    }
  }
  if(!pi.nrest && pi.npost==0 && 0) {}

  /* keywords */
  if(accepts_kw){
    VALUE used = rb_hash_new();
    for(int i=0;params && i<params->nitems;i++){
      Node *e = params->items[i];
      if(e->num != PARAM_KEY && e->num != PARAM_KEYREQ) continue;
      VALUE key = rb_sym(e->id);
      int found = 0; VALUE v = Qnil;
      if(have_kw){ VALUE probe = rb_hash_lookup(kwhash, key, Qundef);
        if(probe != Qundef){ found=1; v = probe; } }
      if(found){ rb_hash_aset(used,key,Qtrue); VALUE *slot=param_slot(env,e); if(slot)*slot=v; }
      else if(e->num==PARAM_KEYREQ) rb_raise(rb_eArgError, "missing keyword: :%s", rb_id2name(e->id));
      else { Frame sf = *f; sf.env = env; VALUE *slot=param_slot(env,e); if(slot)*slot = rb_eval(e->a,&sf); }
    }
    if(pi.nkeyrest){
      VALUE rest = rb_hash_new();
      if(have_kw){ RHash *h = RHSH(kwhash);
        for(long i=0;i<h->n;i++) if(h->ent[i].used){
          if(rb_hash_lookup(used,h->ent[i].key,Qundef)==Qundef) rb_hash_aset(rest,h->ent[i].key,h->ent[i].val); } }
      if(pi.keyrest->id >= 0){ VALUE *slot=param_slot(env,pi.keyrest); if(slot)*slot=rest; }
    } else if(have_kw){
      RHash *h = RHSH(kwhash);
      for(long i=0;i<h->n;i++) if(h->ent[i].used){
        if(rb_hash_lookup(used,h->ent[i].key,Qundef)==Qundef){
          VALUE k = h->ent[i].key;
          rb_raise(rb_eArgError, "unknown keyword: %s", rb_str_cstr(rb_inspect(k)));
        } }
    }
  }
  if(pi.nblock && pi.block->id >= 0){
    VALUE *slot = param_slot(env,pi.block);
    if(slot) *slot = blk ? (VALUE)blk : Qnil;
  }
}

/* ===================== method invocation ===================== */
static VALUE call_cfunc(method_entry *me, VALUE recv, int argc, VALUE *argv, RProc *blk){
  VALUE (*fn)() = me->cfunc;
  if(me->argc == ARGC_ANY) return fn(recv, argc, argv, blk);
  if(argc != me->argc) arity_error(argc, me->argc, me->argc);
  switch(me->argc){
    case 0: return fn(recv, blk);
    case 1: return fn(recv, argv[0], blk);
    case 2: return fn(recv, argv[0], argv[1], blk);
    case 3: return fn(recv, argv[0], argv[1], argv[2], blk);
    case 4: return fn(recv, argv[0], argv[1], argv[2], argv[3], blk);
    default: rb_raise(rb_eRuntimeError, "bad cfunc arity");
  }
  return Qnil;
}

VALUE rb_invoke_method(VALUE recv, method_entry *me, RClass *owner, ID mid,
                       int argc, VALUE *argv, RProc *blk, int is_super){
  switch(me->type){
    case ME_CFUNC: {
      Frame fr; memset(&fr,0,sizeof fr);
      fr.self = recv; fr.defclass = owner; fr.mid = mid; fr.blk = blk;
      fr.prev = rb_curframe; fr.cref = rb_curframe? rb_curframe->cref : &toplevel_cref;
      fr.argc = argc; fr.argv = argv; fr.home = &fr;
      rb_curframe = &fr;
      VALUE saved_kw = g_pending_kw; int saved_has = g_has_pending_kw;
      if(saved_has && !(RHSH(saved_kw)->size==0 && rb_target_version>=30)){
        VALUE *na = xmalloc(sizeof(VALUE)*(argc+1));
        memcpy(na,argv,sizeof(VALUE)*argc); na[argc]=saved_kw; argv=na; argc++;
        fr.argc = argc; fr.argv = argv;
      }
      g_pending_kw = 0; g_has_pending_kw = 0;
      VALUE r;
      rb_tag tag; TAG_PUSH(tag);
      if(setjmp(tag.buf)==0){ r = call_cfunc(me, recv, argc, argv, blk); TAG_POP(tag); }
      else { TAG_POP(tag); rb_curframe = fr.prev;
             rb_jump_tag(tag.state, tag.retval, tag.target); }
      rb_curframe = fr.prev;
      return r; }
    case ME_ATTR_READER: {
      if(argc!=0) arity_error(argc,0,0);
      return rb_ivar_get(recv, me->attr_id); }
    case ME_ATTR_WRITER: {
      if(argc!=1) arity_error(argc,1,1);
      rb_ivar_set(recv, me->attr_id, argv[0]); return argv[0]; }
    case ME_NOTIMPL: rb_raise(rb_eNotImpError, "%s is not implemented", rb_id2name(mid));
    case ME_PROC: {
      /* define_method: the body runs with self bound to the receiver and with
         the defining class as its method-lookup origin, but keeps the closure
         it was created in. */
      RProc p2 = *me->proc;
      p2.self = recv;
      p2.defclass = owner;
      p2.frame = NULL;
      p2.is_lambda = 1;
      Frame fr; memset(&fr,0,sizeof fr);
      fr.self = recv; fr.defclass = owner; fr.mid = mid; fr.blk = blk;
      fr.prev = rb_curframe; fr.home = &fr; fr.env = NULL;
      fr.cref = rb_curframe ? rb_curframe->cref : NULL;
      rb_curframe = &fr;
      VALUE r = Qnil;
      rb_tag tag; TAG_PUSH(tag);
      if(setjmp(tag.buf)==0){ r = rb_proc_invoke(&p2, argc, argv, blk, 1); TAG_POP(tag); }
      else { TAG_POP(tag);
        if(tag.state==TAG_RETURN && tag.target==NULL) r = tag.retval;
        else { rb_curframe = fr.prev; rb_jump_tag(tag.state,tag.retval,tag.target); } }
      rb_curframe = fr.prev;
      return r; }
    case ME_ISEQ: {
      Frame fr; memset(&fr,0,sizeof fr);
      LocalTbl *tbl = (LocalTbl*)me->env;
      Env *env;
      if(tbl && tbl->n) env = env_new(tbl, NULL);
      else { env = &empty_env_storage; env->n = 0; env->parent = NULL; env->tbl = tbl; env->vars = NULL; }
      fr.self = recv; fr.defclass = owner; fr.mid = mid; fr.blk = blk;
      fr.prev = rb_curframe; fr.env = env; fr.home = &fr;
      fr.cref = (cref_t*)me->cref;
      fr.argc = argc; fr.argv = argv;
      rb_curframe = &fr;
      VALUE r = Qnil;
      if(me->needs_tag == 0) me->needs_tag = node_can_jump(me->body) ? 1 : 2;
      if(me->needs_tag == 2){
        rb_bind_params(me->params, &fr, env, argc, argv, blk, 1, 0);
        r = rb_eval(me->body, &fr);
        rb_curframe = fr.prev;
        return r;
      }
      rb_tag tag; TAG_PUSH(tag);
      if(setjmp(tag.buf)==0){
        rb_bind_params(me->params, &fr, env, argc, argv, blk, 1, 0);
        r = rb_eval(me->body, &fr);
        TAG_POP(tag);
      } else {
        TAG_POP(tag);
        if(tag.state==TAG_RETURN && (tag.target==NULL || tag.target==&fr)){ r = tag.retval; }
        else { rb_curframe = fr.prev; rb_jump_tag(tag.state, tag.retval, tag.target); }
      }
      rb_curframe = fr.prev;
      return r; }
    default: break;
  }
  rb_raise(rb_eNoMethodError, "undefined method '%s'", rb_id2name(mid));
  return Qnil;
}

static VALUE method_missing(VALUE recv, ID mid, int argc, VALUE *argv, RProc *blk, int priv){
  RClass *owner;
  method_entry *mm = rb_method_lookup(FIXNUM_P(recv)?rb_cInteger:BASIC(recv)->klass, id_method_missing, &owner);
  if(mm && owner != rb_cBasicObject){
    VALUE *na = xmalloc(sizeof(VALUE)*(argc+1));
    na[0] = rb_sym(mid);
    memcpy(na+1, argv, sizeof(VALUE)*argc);
    return rb_invoke_method(recv, mm, owner, id_method_missing, argc+1, na, blk, 0);
  }
  VALUE ins = rb_inspect(recv);
  const char *cn = rb_obj_classname(recv);
  VALUE exc;
  if(priv)
    exc = rb_exc_new(rb_eNoMethodError, rb_sprintf_val("private method '%s' called for an instance of %s", rb_id2name(mid), cn));
  else if(NIL_P(recv))
    exc = rb_exc_new(rb_eNoMethodError, rb_sprintf_val("undefined method '%s' for nil", rb_id2name(mid)));
  else if(!FIXNUM_P(recv) && (BASIC(recv)->type==T_CLASS||BASIC(recv)->type==T_MODULE))
    exc = rb_exc_new(rb_eNoMethodError, rb_sprintf_val("undefined method '%s' for %s", rb_id2name(mid), rb_str_cstr(ins)));
  else
    exc = rb_exc_new(rb_eNoMethodError, rb_sprintf_val("undefined method '%s' for an instance of %s", rb_id2name(mid), cn));
  rb_ivar_set(exc, ID_CACHED("@name"), rb_sym(mid));
  rb_ivar_set(exc, ID_CACHED("@receiver"), recv);
  rb_exc_raise(exc);
  return Qnil;
}

/* ---- fast path for arithmetic and comparison on small integers ----
   Only taken while none of the operators involved has been redefined. */
int rb_int_ops_redefined;
static ID op_plus, op_minus, op_mul, op_div, op_mod, op_lt, op_gt, op_le,
          op_ge, op_eq, op_neq, op_cmp, op_and, op_or, op_xor, op_lshift, op_aref;

static inline int fix_binop(VALUE l, ID mid, VALUE r, VALUE *out){
  if(rb_int_ops_redefined) return 0;
  if(!FIXNUM_P(l) || !FIXNUM_P(r)) return 0;
  long a = FIX2LONG(l), b = FIX2LONG(r), c;
  if(mid == op_plus){ if(__builtin_add_overflow(a,b,&c)) return 0; }
  else if(mid == op_minus){ if(__builtin_sub_overflow(a,b,&c)) return 0; }
  else if(mid == op_mul){ if(__builtin_mul_overflow(a,b,&c)) return 0; }
  else if(mid == op_lt){ *out = a < b ? Qtrue : Qfalse; return 1; }
  else if(mid == op_gt){ *out = a > b ? Qtrue : Qfalse; return 1; }
  else if(mid == op_le){ *out = a <= b ? Qtrue : Qfalse; return 1; }
  else if(mid == op_ge){ *out = a >= b ? Qtrue : Qfalse; return 1; }
  else if(mid == op_eq){ *out = a == b ? Qtrue : Qfalse; return 1; }
  else if(mid == op_neq){ *out = a != b ? Qtrue : Qfalse; return 1; }
  else if(mid == op_cmp){ *out = LONG2FIX(a < b ? -1 : (a > b ? 1 : 0)); return 1; }
  else if(mid == op_and){ *out = LONG2FIX(a & b); return 1; }
  else if(mid == op_or){ *out = LONG2FIX(a | b); return 1; }
  else if(mid == op_xor){ *out = LONG2FIX(a ^ b); return 1; }
  else if(mid == op_mod){ if(b == 0) return 0; c = a % b; if(c != 0 && ((c < 0) != (b < 0))) c += b; }
  else if(mid == op_div){ if(b == 0) return 0; c = a / b; if((a % b) != 0 && ((a < 0) != (b < 0))) c--; }
  else return 0;
  if(c > FIXNUM_MAX || c < FIXNUM_MIN) return 0;
  *out = LONG2FIX(c);
  return 1;
}

static RClass *lookup_start_class(VALUE recv){
  if(FIXNUM_P(recv)) return rb_cInteger;
  return BASIC(recv)->klass;
}

/* refinements active in the current cref */
unsigned long rb_refinement_count;
static method_entry *find_refinement(ID mid, VALUE recv, RClass **owner){
  if(rb_refinement_count == 0) return NULL;
  Frame *f = rb_curframe;
  cref_t *cr = f ? f->cref : NULL;
  RClass *target = rb_class_of(recv);
  while(cr){
    if(cr->refinements){
      RArray *r = cr->refinements;
      for(long i=0;i<r->len;i+=2){
        RClass *refined = (RClass*)r->ptr[i];
        RClass *mod = (RClass*)r->ptr[i+1];
        if(rb_is_a(recv,refined)){
          method_entry *me = rb_method_lookup(mod, mid, owner);
          if(me) return me;
        }
      }
    }
    cr = cr->next;
  }
  return NULL;
}

/* (class, mid) -> method_entry, invalidated whenever any method table changes */
#define MCACHE_BITS 12
#define MCACHE_SIZE (1 << MCACHE_BITS)
static struct { RClass *klass; ID mid; method_entry *me; RClass *owner; unsigned long serial; } mcache[MCACHE_SIZE];
extern unsigned long rb_method_serial;

static inline method_entry *cached_lookup(RClass *k, ID mid, RClass **owner){
  unsigned h = (unsigned)((((uintptr_t)k) >> 4) ^ ((unsigned)mid * 2654435761u)) & (MCACHE_SIZE - 1);
  if(mcache[h].klass == k && mcache[h].mid == mid && mcache[h].serial == rb_method_serial){
    *owner = mcache[h].owner;
    return mcache[h].me;
  }
  RClass *ow = NULL;
  method_entry *me = rb_method_lookup(k, mid, &ow);
  mcache[h].klass = k; mcache[h].mid = mid; mcache[h].me = me;
  mcache[h].owner = ow; mcache[h].serial = rb_method_serial;
  *owner = ow;
  return me;
}

VALUE rb_call_full(VALUE recv, ID mid, int argc, VALUE *argv, RProc *blk, int allow_private, int self_call){
  RClass *owner = NULL;
  method_entry *me = find_refinement(mid, recv, &owner);
  if(!me) me = cached_lookup(lookup_start_class(recv), mid, &owner);
  if(!me) return method_missing(recv, mid, argc, argv, blk, 0);
  if(me->visibility==VIS_PRIVATE && !allow_private){
    const char *nm = rb_id2name(mid);
    long l = strlen(nm);
    if(!(l>0 && nm[l-1]=='=' && self_call))
      return method_missing(recv, mid, argc, argv, blk, 1);
  }
  if(me->visibility==VIS_PROTECTED && !allow_private){
    Frame *f = rb_curframe;
    VALUE cur = f? f->self : Qnil;
    if(!rb_is_a(cur, me->owner)) return method_missing(recv, mid, argc, argv, blk, 1);
  }
  return rb_invoke_method(recv, me, owner, mid, argc, argv, blk, 0);
}
VALUE rb_call(VALUE recv, ID mid, int argc, VALUE *argv, RProc *blk){
  return rb_call_full(recv, mid, argc, argv, blk, 1, 0);
}
VALUE rb_funcall(VALUE recv, ID mid, int argc, ...){
  VALUE argv[8]; va_list ap; va_start(ap,argc);
  for(int i=0;i<argc && i<8;i++) argv[i]=va_arg(ap,VALUE);
  va_end(ap);
  return rb_call_full(recv, mid, argc, argv, NULL, 1, 0);
}

/* ===================== procs and blocks ===================== */
RProc *rb_proc_new_from_block(Node *params, Node *body, Frame *f, int is_lambda){
  RProc *p = xcalloc(1,sizeof(RProc));
  p->basic.type = T_PROC; p->basic.klass = rb_cProc;
  p->params = params; p->body = body; p->env = f->env; p->self = f->self;
  p->cref = NULL; p->cref_list = (struct rb_cref*)(f->cref);
  p->is_lambda = is_lambda;
  p->frame = f->home ? f->home : f;
  p->defclass = f->defclass;
  return p;
}
RProc *rb_cfunc_proc(VALUE (*fn)(VALUE,int,VALUE*,RProc*), VALUE data, int arity){
  RProc *p = xcalloc(1,sizeof(RProc));
  p->basic.type = T_PROC; p->basic.klass = rb_cProc;
  p->cfunc = fn; p->cdata = data; p->is_lambda = 1; p->arity_override = arity;
  return p;
}

VALUE rb_proc_invoke(RProc *p, int argc, VALUE *argv, RProc *blk, int as_lambda){
  if(p->cfunc) return p->cfunc(p->cdata, argc, argv, blk);
  Frame fr; memset(&fr,0,sizeof fr);
  LocalTbl *tbl = p->body ? NULL : NULL;
  Node *iterlv = NULL; (void)iterlv;
  tbl = (LocalTbl*)p->cdata;   /* block's own LocalTbl stored in cdata */
  Env *env = env_new(tbl, p->env);
  fr.self = p->self; fr.env = env; fr.prev = rb_curframe;
  fr.defclass = p->defclass;
  fr.cref = (cref_t*)p->cref_list;
  fr.is_block = 1;
  fr.home = p->frame;
  fr.mid = p->frame ? p->frame->mid : 0;
  fr.blk = p->frame ? p->frame->blk : NULL;
  fr.in_lambda = p->is_lambda;
  rb_curframe = &fr;
  VALUE r = Qnil;
  int lam = as_lambda || p->is_lambda;
  int nnum = (int)(intptr_t)p->refined_from;
  if(!p->params && nnum > 1 && argc==1){
    VALUE a = rb_check_array_type(argv[0]);
    if(!NIL_P(a)){ argc = (int)RARR(a)->len; argv = RARR(a)->ptr; }
  }
  fr.argc = argc; fr.argv = argv;
 redo_block:
  {
    rb_tag tag; TAG_PUSH(tag);
    if(setjmp(tag.buf)==0){
      rb_bind_params(p->params, &fr, env, argc, argv, blk, lam, 1);
      r = rb_eval(p->body, &fr);
      TAG_POP(tag);
    } else {
      TAG_POP(tag);
      if(tag.state==TAG_NEXT && (tag.target==NULL || tag.target==p)){ r = tag.retval; }
      else if(tag.state==TAG_REDO){ goto redo_block; }
      else if(tag.state==TAG_RETURN && lam && p->is_lambda){ r = tag.retval; }
      else { rb_curframe = fr.prev; rb_jump_tag(tag.state, tag.retval, tag.target); }
    }
  }
  rb_curframe = fr.prev;
  return r;
}
VALUE rb_yield_values(RProc *blk, int argc, VALUE *argv){
  if(!blk) rb_raise(rb_eLocalJumpError, "no block given (yield)");
  return rb_proc_invoke(blk, argc, argv, NULL, 0);
}
VALUE rb_yield1(RProc *blk, VALUE v){ return rb_yield_values(blk,1,&v); }
VALUE rb_proc_call(RProc *p, int argc, VALUE *argv, RProc *blk){
  return rb_proc_invoke(p, argc, argv, blk, p->is_lambda);
}

static RProc *proc_from_iter(Node *iter, Frame *f, int is_lambda){
  RProc *p = rb_proc_new_from_block(iter->a, iter->b, f, is_lambda);
  p->cdata = iter->val;
  p->arity_override = -1;
  p->refined_from = (RProc*)(intptr_t)iter->num;    /* numparam count */
  p->defclass = f->defclass;
  return p;
}

static RProc *eval_block_arg(Node *call, Frame *f){
  if(call->b) return proc_from_iter(call->b, f, 0);
  if(call->c){
    if(call->c->type==N_ANONBLOCK){
      Env *e = f->env; ID aid = ID_CACHED("__anon_blk");
      if(e && e->tbl) for(int k=0;k<e->tbl->n;k++) if(e->tbl->names[k]==aid){
        VALUE v = e->vars[k]; return NIL_P(v)? NULL : (RProc*)v; }
      return NULL;
    }
    VALUE v = rb_eval(call->c,f);
    if(NIL_P(v)) return NULL;
    if(!FIXNUM_P(v) && BASIC(v)->type==T_PROC) return (RProc*)v;
    if(!FIXNUM_P(v) && BASIC(v)->type==T_SYMBOL) return (RProc*)rb_funcall(v, id_to_proc, 0);
    VALUE pv = rb_check_convert(v, id_to_proc);
    if(NIL_P(pv)) rb_raise(rb_eTypeError, "wrong argument type %s (expected Proc)", rb_obj_classname(v));
    return (RProc*)pv;
  }
  return NULL;
}

static VALUE eval_hash_node(Node *h, Frame *f){
  VALUE hash = rb_hash_new();
  for(int i=0;i+1<h->nitems || i<h->nitems;i+=2){
    Node *k = h->items[i];
    if(k && k->type==N_KWSPLAT){
      VALUE src = rb_eval(k->a,f);
      if(NIL_P(src)) continue;
      VALUE hh = rb_check_convert(src, id_to_hash);
      if(NIL_P(hh)) rb_raise(rb_eTypeError, "no implicit conversion of %s into Hash", rb_obj_classname(src));
      RHash *s = RHSH(hh);
      for(long j=0;j<s->n;j++) if(s->ent[j].used) rb_hash_aset(hash, s->ent[j].key, s->ent[j].val);
      continue;
    }
    if(k && k->type==N_ANONKWSPLAT){
      Env *e = f->env; ID aid = ID_CACHED("__anon_kw");
      if(e && e->tbl) for(int q=0;q<e->tbl->n;q++) if(e->tbl->names[q]==aid){
        VALUE src = e->vars[q];
        if(!NIL_P(src)){ RHash *s = RHSH(src);
          for(long j=0;j<s->n;j++) if(s->ent[j].used) rb_hash_aset(hash, s->ent[j].key, s->ent[j].val); } }
      continue;
    }
    if(i+1 >= h->nitems) break;
    VALUE kv = rb_eval(k,f);
    VALUE vv = rb_eval(h->items[i+1],f);
    rb_hash_aset(hash, kv, vv);
  }
  return hash;
}

static VALUE splat_to_array(VALUE v){
  VALUE a = rb_check_array_type(v);
  if(NIL_P(a)){ a = rb_ary_new(); if(v != Qnil) rb_ary_push(a,v); }
  return a;
}

static void masgn_assign(Node *lhs_list, Node *rhsnode, VALUE val, Frame *f);

static VALUE do_asgn_target(Node *t, VALUE v, Frame *f){
  switch(t->type){
    case N_LASGN: { VALUE *s = env_slot(f->env, t->num, t->flags); if(s) *s = v; return v; }
    case N_IASGN: rb_ivar_set(f->self, t->id, v); return v;
    case N_GASGN: rb_gv_set(t->id, v); return v;
    case N_CVASGN: {
      RClass *c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      if(c->is_singleton && c->attached && !FIXNUM_P(c->attached) &&
         (BASIC(c->attached)->type==T_CLASS||BASIC(c->attached)->type==T_MODULE)) c = (RClass*)c->attached;
      int n; RClass **a = rb_class_ancestors(c,&n);
      for(int i=0;i<n;i++){ int fnd; ivtbl_get(a[i]->basic.iv, t->id, &fnd);
        if(fnd){ ivtbl_set(&a[i]->basic.iv, t->id, v); return v; } }
      ivtbl_set(&c->basic.iv, t->id, v); return v; }
    case N_CDECL: {
      RClass *c;
      if(t->b) c = (RClass*)rb_eval(t->b,f);
      else c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      int fnd; ivtbl_get(c->consts, t->id, &fnd);
      if(fnd) rb_warning("already initialized constant %s", rb_id2name(t->id));
      ivtbl_set(&c->consts, t->id, v);
      if(!FIXNUM_P(v) && (BASIC(v)->type==T_CLASS||BASIC(v)->type==T_MODULE)){
        RClass *k = (RClass*)v;
        if(k->name < 0){ k->name = t->id; k->lexical_parent = (c==rb_cObject)? NULL : c; }
      }
      return v; }
    case N_ATTRASGN: {
      VALUE recv = rb_eval(t->a,f);
      if(t->num && NIL_P(recv)) return Qnil;
      VALUE args[1] = { v };
      rb_call_full(recv, t->id, 1, args, NULL, (t->a->type==N_SELF), (t->a->type==N_SELF));
      return v; }
    case N_IDXASGN: {
      VALUE recv = rb_eval(t->a,f);
      ArgList al; al_init(&al);
      for(int i=0;i<t->nitems;i++){
        Node *a = t->items[i];
        if(a->type==N_SPLAT){ VALUE sv = splat_to_array(rb_eval(a->a,f));
          for(long k=0;k<RARR(sv)->len;k++) al_push(&al,RARR(sv)->ptr[k]); }
        else al_push(&al, rb_eval(a,f));
      }
      al_push(&al, v);
      rb_call(recv, ID_CACHED("[]="), al.n, al.v, NULL);
      return v; }
    case N_MASGN: { masgn_assign(t->a, NULL, v, f); return v; }
    case N_SPLAT: { return do_asgn_target(t->a, v, f); }
    default: rb_raise(rb_eRuntimeError, "bad assignment target %d", t->type);
  }
  return v;
}

static void masgn_assign(Node *lhs_list, Node *rhsnode, VALUE val, Frame *f){
  VALUE ary = rb_check_array_type(val);
  if(NIL_P(ary)){ ary = rb_ary_new(); rb_ary_push(ary,val); }
  long n = RARR(ary)->len;
  int rest = -1;
  for(int i=0;i<lhs_list->nitems;i++) if(lhs_list->items[i]->type==N_SPLAT){ rest=i; break; }
  long idx = 0;
  int pre = (rest<0)? lhs_list->nitems : rest;
  for(int i=0;i<pre;i++) do_asgn_target(lhs_list->items[i], idx<n? RARR(ary)->ptr[idx++] : Qnil, f);
  if(rest>=0){
    int post = lhs_list->nitems - rest - 1;
    long take = n - idx - post; if(take<0) take=0;
    VALUE r = rb_ary_new();
    for(long k=0;k<take;k++) rb_ary_push(r, RARR(ary)->ptr[idx+k]);
    idx += take;
    if(lhs_list->items[rest]->a) do_asgn_target(lhs_list->items[rest]->a, r, f);
    for(int i=rest+1;i<lhs_list->nitems;i++)
      do_asgn_target(lhs_list->items[i], idx<n? RARR(ary)->ptr[idx++] : Qnil, f);
  }
}

/* ===================== defined? ===================== */
static VALUE eval_defined(Node *n, Frame *f){
  Node *e = n->a;
  const char *s = NULL;
  switch(e->type){
    case N_NIL: s="expression"; break;
    case N_TRUE: case N_FALSE: s="expression"; break;
    case N_SELF: s="self"; break;
    case N_LVAR: s="local-variable"; break;
    case N_IVAR: return rb_ivar_defined(f->self,e->id)? rb_str_new_cstr("instance-variable") : Qnil;
    case N_GVAR: return rb_gv_defined(e->id)? rb_str_new_cstr("global-variable") : Qnil;
    case N_CVAR: {
      RClass *c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      int nn; RClass **a = rb_class_ancestors(c,&nn);
      for(int i=0;i<nn;i++){ int fnd; ivtbl_get(a[i]->basic.iv,e->id,&fnd); if(fnd) return rb_str_new_cstr("class variable"); }
      return Qnil; }
    case N_CONST: case N_COLON2: case N_COLON3: {
      rb_tag tag; TAG_PUSH(tag);
      if(setjmp(tag.buf)==0){ rb_eval(e,f); TAG_POP(tag); return rb_str_new_cstr("constant"); }
      TAG_POP(tag); if(tag.state != TAG_RAISE) rb_jump_tag(tag.state,tag.retval,tag.target);
      return Qnil; }
    case N_VCALL: case N_FCALL: case N_CALL: case N_SAFECALL: {
      rb_tag tag; TAG_PUSH(tag);
      if(setjmp(tag.buf)==0){
        VALUE recv;
        ID mid = e->id;
        if(e->type==N_CALL||e->type==N_SAFECALL){ recv = rb_eval(e->a,f); }
        else recv = f->self;
        for(int i=0;i<e->nitems;i++) rb_eval(e->items[i],f);
        RClass *owner;
        method_entry *me = rb_method_lookup(lookup_start_class(recv), mid, &owner);
        TAG_POP(tag);
        if(!me){
          if(e->type != N_CALL && e->type != N_SAFECALL) return Qnil;
          return Qnil;
        }
        if(me->visibility != VIS_PUBLIC && (e->type==N_CALL||e->type==N_SAFECALL)) return Qnil;
        return rb_str_new_cstr("method");
      }
      TAG_POP(tag);
      if(tag.state != TAG_RAISE) rb_jump_tag(tag.state,tag.retval,tag.target);
      return Qnil; }
    case N_YIELD: return rb_block_given()? rb_str_new_cstr("yield") : Qnil;
    case N_LASGN: case N_IASGN: case N_GASGN: case N_CVASGN: case N_CDECL:
    case N_MASGN: case N_OPASGN: case N_ATTRASGN: case N_IDXASGN:
      s = "assignment"; break;
    case N_ZSUPER: case N_SUPER: {
      Frame *h = f; while(h && h->is_block) h = h->home;
      if(!h || !h->defclass) return Qnil;
      RClass *owner; method_entry *me = rb_method_lookup_super(lookup_start_class(h->self), h->defclass, h->mid, &owner);
      return me? rb_str_new_cstr("super") : Qnil; }
    default: {
      rb_tag tag; TAG_PUSH(tag);
      if(setjmp(tag.buf)==0){ rb_eval(e,f); TAG_POP(tag); return rb_str_new_cstr("expression"); }
      TAG_POP(tag); if(tag.state != TAG_RAISE) rb_jump_tag(tag.state,tag.retval,tag.target);
      return Qnil; }
  }
  return rb_str_new_cstr(s);
}

/* ===================== pattern matching ===================== */
static int pat_match(Node *pat, VALUE v, Frame *f);

static int pat_match_array(Node *pat, VALUE v, Frame *f){
  VALUE ary;
  if(pat->b){
    VALUE cls = rb_eval(pat->b,f);
    if(!RTEST(rb_funcall(cls, id_eqq, 1, v))) return 0;
  }
  if(!FIXNUM_P(v) && BASIC(v)->type==T_ARRAY) ary = v;
  else if(rb_respond_to(v, id_deconstruct)){
    ary = rb_funcall(v, id_deconstruct, 0);
    if(FIXNUM_P(ary)||BASIC(ary)->type != T_ARRAY) rb_raise(rb_eTypeError,"deconstruct must return Array");
  } else return 0;
  long n = RARR(ary)->len;
  int rest = -1;
  for(int i=0;i<pat->nitems;i++) if(pat->items[i]->type==N_SPLAT){ rest=i; break; }
  if(rest<0){
    if(n != pat->nitems) return 0;
    for(int i=0;i<pat->nitems;i++) if(!pat_match(pat->items[i], RARR(ary)->ptr[i], f)) return 0;
    return 1;
  }
  int pre = rest, post = pat->nitems - rest - 1;
  if(n < pre+post) return 0;
  for(int i=0;i<pre;i++) if(!pat_match(pat->items[i], RARR(ary)->ptr[i], f)) return 0;
  for(int i=0;i<post;i++) if(!pat_match(pat->items[rest+1+i], RARR(ary)->ptr[n-post+i], f)) return 0;
  if(pat->items[rest]->id >= 0){
    VALUE r = rb_ary_new();
    for(long k=pre;k<n-post;k++) rb_ary_push(r, RARR(ary)->ptr[k]);
    VALUE *s = env_slot(f->env,0,pat->items[rest]->flags); if(s) *s = r;
  }
  return 1;
}
static int pat_match_find(Node *pat, VALUE v, Frame *f){
  VALUE ary;
  if(!FIXNUM_P(v) && BASIC(v)->type==T_ARRAY) ary = v;
  else if(rb_respond_to(v, id_deconstruct)) ary = rb_funcall(v, id_deconstruct, 0);
  else return 0;
  long n = RARR(ary)->len;
  int mid = pat->nitems - 2;
  for(long start=0; start+mid<=n; start++){
    int ok=1;
    for(int i=0;i<mid;i++) if(!pat_match(pat->items[i+1], RARR(ary)->ptr[start+i],f)){ ok=0; break; }
    if(!ok) continue;
    if(pat->items[0]->id>=0){ VALUE pre=rb_ary_new(); for(long k=0;k<start;k++) rb_ary_push(pre,RARR(ary)->ptr[k]);
      VALUE *s=env_slot(f->env,0,pat->items[0]->flags); if(s)*s=pre; }
    if(pat->items[pat->nitems-1]->id>=0){ VALUE post=rb_ary_new();
      for(long k=start+mid;k<n;k++) rb_ary_push(post,RARR(ary)->ptr[k]);
      VALUE *s=env_slot(f->env,0,pat->items[pat->nitems-1]->flags); if(s)*s=post; }
    return 1;
  }
  return 0;
}
static int pat_match_hash(Node *pat, VALUE v, Frame *f){
  if(pat->b){
    VALUE cls = rb_eval(pat->b,f);
    if(!RTEST(rb_funcall(cls, id_eqq, 1, v))) return 0;
  }
  VALUE h;
  VALUE keys = rb_ary_new();
  for(int i=0;i<pat->nitems;i+=2) rb_ary_push(keys, rb_eval(pat->items[i],f));
  if(!FIXNUM_P(v) && BASIC(v)->type==T_HASH) h = v;
  else if(rb_respond_to(v, id_deconstruct_keys)){
    VALUE arg = (pat->num==1) ? Qnil : keys;
    VALUE args[1] = { arg };
    h = rb_call(v, id_deconstruct_keys, 1, args, NULL);
    if(FIXNUM_P(h)||BASIC(h)->type != T_HASH) rb_raise(rb_eTypeError,"deconstruct_keys must return Hash");
  } else return 0;
  VALUE seen = rb_hash_new();
  for(int i=0;i<pat->nitems;i+=2){
    VALUE k = rb_eval(pat->items[i],f);
    VALUE got = rb_hash_lookup(h,k,Qundef);
    if(got==Qundef) return 0;
    rb_hash_aset(seen,k,Qtrue);
    Node *sub = pat->items[i+1];
    if(sub->type==N_ARGENT && sub->num==999 && !sub->a){
      VALUE *s = env_slot(f->env,0,sub->flags); if(s) *s = got;
    } else if(!pat_match(sub, got, f)) return 0;
  }
  if(pat->num==2){ if(RHSH(h)->size != (long)(pat->nitems/2)) return 0; }
  if(pat->num==1 && pat->id){
    VALUE rest = rb_hash_new(); RHash *hh = RHSH(h);
    for(long i=0;i<hh->n;i++) if(hh->ent[i].used && rb_hash_lookup(seen,hh->ent[i].key,Qundef)==Qundef)
      rb_hash_aset(rest,hh->ent[i].key,hh->ent[i].val);
    VALUE *s = env_slot(f->env,0,pat->flags); if(s) *s = rest;
  }
  return 1;
}
static int pat_match(Node *pat, VALUE v, Frame *f){
  switch(pat->type){
    case N_ARRPAT: return pat_match_array(pat,v,f);
    case N_FNDPAT: return pat_match_find(pat,v,f);
    case N_HASHPAT: return pat_match_hash(pat,v,f);
    case N_ALTPAT: return pat_match(pat->a,v,f) || pat_match(pat->b,v,f);
    case N_PINNED: { VALUE pv = rb_eval(pat->a,f); return RTEST(rb_funcall(pv,id_eqq,1,v)); }
    case N_ARGENT:
      if(pat->num==999){
        if(pat->a && !pat_match(pat->a,v,f)) return 0;
        VALUE *s = env_slot(f->env,0,pat->flags); if(s) *s = v; return 1;
      }
      return 0;
    default: { VALUE pv = rb_eval(pat,f); return RTEST(rb_funcall(pv,id_eqq,1,v)); }
  }
}

/* ===================== class/module opening ===================== */
static VALUE eval_in_class(RClass *c, Node *body, LocalTbl *tbl, Frame *f){
  Frame fr; memset(&fr,0,sizeof fr);
  cref_t *cr = xcalloc(1,sizeof(cref_t));
  cr->klass = c; cr->next = f->cref;
  fr.self = (VALUE)c; fr.defclass = c; fr.cref = cr; fr.prev = rb_curframe;
  fr.env = env_new(tbl,NULL); fr.home = &fr;
  rb_curframe = &fr;
  VALUE r = Qnil;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ r = rb_eval(body,&fr); TAG_POP(tag); }
  else { TAG_POP(tag); rb_curframe = fr.prev; rb_jump_tag(tag.state,tag.retval,tag.target); }
  rb_curframe = fr.prev;
  return r;
}

/* ===================== main eval ===================== */
static VALUE eval_call(Node *n, Frame *f);

VALUE rb_eval(Node *n, Frame *f){
  if(!n) return Qnil;
 tail:
  switch(n->type){
    case N_NIL: return Qnil;
    case N_TRUE: return Qtrue;
    case N_FALSE: return Qfalse;
    case N_SELF: return f->self;
    case N_INT: case N_FLOAT: return n->val;
    case N_STR: {
      if(n->num==1) return rb_str_freeze(n->val);
      VALUE s = rb_str_dup(n->val);
      if(n->num==2) BASIC(s)->chilled = 1;
      return s; }
    case N_DSTR: {
      VALUE s = rb_str_new("",0);
      for(int i=0;i<n->nitems;i++){
        Node *p = n->items[i];
        if(p->type==N_STR) rb_str_cat(s, RSTR(p->val)->ptr, RSTR(p->val)->len);
        else { VALUE v = rb_eval(p,f); VALUE t = rb_obj_as_string(v); rb_str_cat(s, RSTR(t)->ptr, RSTR(t)->len);
               if(RSTR(t)->enc==enc_utf8) RSTR(s)->enc = enc_utf8; }
      }
      return s; }
    case N_SYM: return rb_sym(n->id);
    case N_DSYM: { VALUE s = rb_eval(n->a,f); return rb_sym_from_str(s); }
    case N_REGX: { if(!n->val) return Qnil;
      if(!n->d){ n->d = (Node*)(void*)rb_reg_new_str(n->val, n->num); }
      return (VALUE)n->d; }
    case N_DREGX: { VALUE s = rb_eval(n->a,f); return rb_reg_new_str(s, n->num); }
    case N_ARRAY: {
      VALUE a = rb_ary_new();
      for(int i=0;i<n->nitems;i++){
        Node *e = n->items[i];
        if(e->type==N_SPLAT){ VALUE sv = splat_to_array(rb_eval(e->a,f));
          for(long k=0;k<RARR(sv)->len;k++) rb_ary_push(a,RARR(sv)->ptr[k]); }
        else if(e->type==N_HASH) rb_ary_push(a, eval_hash_node(e,f));
        else rb_ary_push(a, rb_eval(e,f));
      }
      return a; }
    case N_HASH: return eval_hash_node(n,f);
    case N_RANGE: return rb_range_new(rb_eval(n->a,f), rb_eval(n->b,f), n->num);
    case N_LVAR: { VALUE *s = env_slot(f->env,n->num,n->flags); return s? *s : Qnil; }
    case N_NUMPARAM: { if(n->num-1 < f->argc) return f->argv[n->num-1]; return Qnil; }
    case N_ITPARAM: return f->argc>0 ? f->argv[0] : Qnil;
    case N_IVAR: return rb_ivar_get(f->self, n->id);
    case N_GVAR: {
      if(n->id == ID_CACHED("$~")) return rb_get_backref();
      if(n->id == ID_CACHED("$!")) return rb_errinfo;
      return rb_gv_get(n->id); }
    case N_NTH_REF: {
      VALUE m = rb_get_backref();
      if(NIL_P(m)) return Qnil;
      VALUE args[1] = { LONG2FIX(n->num) };
      return rb_call(m, ID_CACHED("[]"), 1, args, NULL); }
    case N_CVAR: {
      RClass *c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      if(c->is_singleton && c->attached && !FIXNUM_P(c->attached) &&
         (BASIC(c->attached)->type==T_CLASS||BASIC(c->attached)->type==T_MODULE)) c = (RClass*)c->attached;
      int nn; RClass **a = rb_class_ancestors(c,&nn);
      for(int i=0;i<nn;i++){ int fnd; VALUE v = ivtbl_get(a[i]->basic.iv,n->id,&fnd); if(fnd) return v; }
      rb_raise(rb_eNameError, "uninitialized class variable @@%s in %s", rb_id2name(n->id), rb_str_cstr(rb_class_name(c))); }
    case N_CONST: {
      cref_t *cr = f->cref;
      while(cr){ if(cr->klass){ int fnd; VALUE v = ivtbl_get(cr->klass->consts,n->id,&fnd); if(fnd) return v; } cr = cr->next; }
      cr = f->cref;
      while(cr){ if(cr->klass){ int fnd; VALUE v = rb_const_get(cr->klass,n->id,&fnd); if(fnd) return v; } cr = cr->next; }
      { int fnd; VALUE v = rb_const_get(rb_cObject,n->id,&fnd); if(fnd) return v; }
      { VALUE args[1] = { rb_sym(n->id) };
        RClass *base = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
        RClass *owner; method_entry *me = rb_method_lookup(BASIC((VALUE)base)->klass, ID_CACHED("const_missing"), &owner);
        if(me) return rb_invoke_method((VALUE)base, me, owner, ID_CACHED("const_missing"), 1, args, NULL, 0); }
      rb_raise(rb_eNameError, "uninitialized constant %s", rb_id2name(n->id)); }
    case N_COLON2: {
      VALUE base = rb_eval(n->a,f);
      if(FIXNUM_P(base) || (BASIC(base)->type != T_CLASS && BASIC(base)->type != T_MODULE))
        rb_raise(rb_eTypeError, "%s is not a class/module", rb_str_cstr(rb_inspect(base)));
      int fnd; VALUE v = rb_const_get((RClass*)base, n->id, &fnd);
      if(fnd) return v;
      rb_raise(rb_eNameError, "uninitialized constant %s::%s", rb_str_cstr(rb_class_name((RClass*)base)), rb_id2name(n->id)); }
    case N_COLON3: { int fnd; VALUE v = ivtbl_get(rb_cObject->consts,n->id,&fnd);
      if(fnd) return v; rb_raise(rb_eNameError,"uninitialized constant %s", rb_id2name(n->id)); }
    case N_LASGN: case N_IASGN: case N_GASGN: case N_CVASGN: case N_CDECL:
    case N_ATTRASGN: case N_IDXASGN: {
      VALUE v = n->type==N_IDXASGN ? rb_eval(n->b,f) : (n->type==N_ATTRASGN ? rb_eval(n->b,f) : rb_eval(n->a,f));
      return do_asgn_target(n, v, f); }
    case N_MASGN: {
      VALUE val;
      if(n->b && n->b->type==N_ARRAY && n->b->num==1) val = rb_eval(n->b,f);
      else { val = rb_eval(n->b,f); }
      masgn_assign(n->a, n->b, val, f);
      return val; }
    case N_OPASGN: {
      VALUE cur;
      ID op = n->id;
      Node *t = n->c;
      /* `@@x ||= v` and `@@x &&= v` read an undefined class variable as nil,
         the way CRuby does, instead of raising NameError. */
      int soft = (t->type==N_CVAR) && (op==ID_CACHED("||") || op==ID_CACHED("&&"));
      if(soft){
        RClass *cc = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
        if(cc->is_singleton && cc->attached && !FIXNUM_P(cc->attached) &&
           (BASIC(cc->attached)->type==T_CLASS||BASIC(cc->attached)->type==T_MODULE)) cc = (RClass*)cc->attached;
        int nn; RClass **aa = rb_class_ancestors(cc,&nn);
        int found = 0;
        for(int i=0;i<nn && !found;i++){ int fnd; ivtbl_get(aa[i]->basic.iv,t->id,&fnd); if(fnd) found=1; }
        if(!found) cur = Qnil;
        else cur = rb_eval(t,f);
        if(op == ID_CACHED("||")){
          if(RTEST(cur)) return cur;
        } else if(!RTEST(cur)) return cur;
        VALUE v = rb_eval(n->b,f);
        Node tmp = *t; tmp.type = N_CVASGN;
        return do_asgn_target(&tmp, v, f);
      }
      if(op == ID_CACHED("||")){
        cur = rb_eval(t,f);
        if(RTEST(cur)) return cur;
        VALUE v = rb_eval(n->b,f);
        Node tmp = *t; tmp.type = (t->type==N_LVAR)?N_LASGN:(t->type==N_IVAR?N_IASGN:(t->type==N_GVAR?N_GASGN:N_CVASGN));
        return do_asgn_target(&tmp, v, f);
      }
      if(op == ID_CACHED("&&")){
        cur = rb_eval(t,f);
        if(!RTEST(cur)) return cur;
        VALUE v = rb_eval(n->b,f);
        Node tmp = *t; tmp.type = (t->type==N_LVAR)?N_LASGN:(t->type==N_IVAR?N_IASGN:(t->type==N_GVAR?N_GASGN:N_CVASGN));
        return do_asgn_target(&tmp, v, f);
      }
      cur = rb_eval(t,f);
      VALUE v = rb_eval(n->b,f);
      VALUE nv;
      if(!fix_binop(cur, op, v, &nv)) nv = rb_call_full(cur, op, 1, &v, NULL, 0, 0);
      Node tmp = *t; tmp.type = (t->type==N_LVAR)?N_LASGN:(t->type==N_IVAR?N_IASGN:(t->type==N_GVAR?N_GASGN:N_CVASGN));
      return do_asgn_target(&tmp, nv, f); }
    case N_OPASGN_CONST: {
      Node *t = n->c;
      VALUE cur = Qnil; int have = 1;
      if(n->id == ID_CACHED("||")){
        rb_tag tag; TAG_PUSH(tag);
        if(setjmp(tag.buf)==0){ cur = rb_eval(t,f); TAG_POP(tag); }
        else { TAG_POP(tag); if(tag.state != TAG_RAISE) rb_jump_tag(tag.state,tag.retval,tag.target); have=0; }
        if(have && RTEST(cur)) return cur;
        VALUE v = rb_eval(n->b,f);
        Node tmp; memset(&tmp,0,sizeof tmp); tmp.type=N_CDECL; tmp.id=t->id; tmp.b = (t->type==N_COLON2)? t->a : NULL;
        return do_asgn_target(&tmp,v,f);
      }
      cur = rb_eval(t,f);
      if(n->id == ID_CACHED("&&") && !RTEST(cur)) return cur;
      VALUE v = rb_eval(n->b,f);
      VALUE nv = (n->id==ID_CACHED("&&")) ? v : rb_funcall(cur,n->id,1,v);
      Node tmp; memset(&tmp,0,sizeof tmp); tmp.type=N_CDECL; tmp.id=t->id; tmp.b = (t->type==N_COLON2)? t->a : NULL;
      return do_asgn_target(&tmp,nv,f); }
    case N_OPASGN_ATTR: {
      VALUE recv = rb_eval(n->a,f);
      if(n->num && NIL_P(recv)) return Qnil;
      ID getter = (ID)n->flags;
      char buf[128]; snprintf(buf,sizeof buf,"%s=",rb_id2name(getter));
      ID setter = rb_intern(buf);
      VALUE cur = rb_call_full(recv,getter,0,NULL,NULL,(n->a->type==N_SELF),0);
      if(n->id==ID_CACHED("||")){ if(RTEST(cur)) return cur; VALUE v = rb_eval(n->b,f);
        VALUE a1[1]={v}; rb_call_full(recv,setter,1,a1,NULL,1,1); return v; }
      if(n->id==ID_CACHED("&&")){ if(!RTEST(cur)) return cur; VALUE v = rb_eval(n->b,f);
        VALUE a1[1]={v}; rb_call_full(recv,setter,1,a1,NULL,1,1); return v; }
      VALUE v = rb_eval(n->b,f);
      VALUE nv = rb_funcall(cur,n->id,1,v);
      VALUE a1[1]={nv}; rb_call_full(recv,setter,1,a1,NULL,1,1); return nv; }
    case N_OPASGN_IDX: {
      VALUE recv = rb_eval(n->a,f);
      ArgList al; al_init(&al);
      for(int i=0;i<n->nitems;i++){
        Node *a = n->items[i];
        if(a->type==N_SPLAT){ VALUE sv = splat_to_array(rb_eval(a->a,f));
          for(long k=0;k<RARR(sv)->len;k++) al_push(&al,RARR(sv)->ptr[k]); }
        else al_push(&al, rb_eval(a,f));
      }
      VALUE cur = rb_call(recv, ID_CACHED("[]"), al.n, al.v, NULL);
      VALUE nv;
      if(n->id==ID_CACHED("||")){ if(RTEST(cur)) return cur; nv = rb_eval(n->b,f); }
      else if(n->id==ID_CACHED("&&")){ if(!RTEST(cur)) return cur; nv = rb_eval(n->b,f); }
      else nv = rb_funcall(cur, n->id, 1, rb_eval(n->b,f));
      al_push(&al, nv);
      rb_call(recv, ID_CACHED("[]="), al.n, al.v, NULL);
      return nv; }
    case N_AND: { VALUE l = rb_eval(n->a,f); if(!RTEST(l)) return l; n = n->b; goto tail; }
    case N_OR: { VALUE l = rb_eval(n->a,f); if(RTEST(l)) return l; n = n->b; goto tail; }
    case N_NOT: { VALUE v = rb_eval(n->a,f);
      if(!FIXNUM_P(v)){ RClass *ow; method_entry *me = rb_method_lookup(lookup_start_class(v), ID_CACHED("!"), &ow);
        if(me && me->owner != rb_cBasicObject) return rb_call(v, ID_CACHED("!"), 0, NULL, NULL); }
      return RTEST(v)? Qfalse : Qtrue; }
    case N_DEFINED: return eval_defined(n,f);
    case N_BLOCK: {
      VALUE r = Qnil;
      for(int i=0;i<n->nitems-1;i++) rb_eval(n->items[i],f);
      if(n->nitems){ n = n->items[n->nitems-1]; goto tail; }
      return r; }
    case N_BEGIN: n = n->a; goto tail;
    case N_IF: { VALUE c = rb_eval(n->a,f);
      if(RTEST(c)){ if(!n->b) return Qnil; n = n->b; goto tail; }
      if(!n->c) return Qnil; n = n->c; goto tail; }
    case N_WHILE: case N_UNTIL: {
      int until = (n->type==N_UNTIL);
      VALUE r = Qnil;
      int first = 1;
      while(1){
        if(!(n->num && first)){
          VALUE c = rb_eval(n->a,f);
          int t = RTEST(c);
          if(until) t = !t;
          if(!t) break;
        }
        first = 0;
        rb_tag tag; TAG_PUSH(tag);
        if(setjmp(tag.buf)==0){ rb_eval(n->b,f); TAG_POP(tag); }
        else { TAG_POP(tag);
          if(tag.state==TAG_BREAK && tag.target==NULL){ r = tag.retval; break; }
          if(tag.state==TAG_NEXT && tag.target==NULL){ continue; }
          if(tag.state==TAG_REDO && tag.target==NULL){ continue; }
          rb_jump_tag(tag.state,tag.retval,tag.target); }
      }
      return r; }
    case N_FOR: {
      VALUE coll = rb_eval(n->a,f);
      VALUE ary = coll;
      Node *vars = n->c;
      RProc dummy;
      VALUE r = coll;
      /* iterate via each, binding into the enclosing scope */
      struct forctx { Node *vars; Node *body; Frame *f; };
      VALUE it = rb_ary_new();
      (void)dummy; (void)ary; (void)it;
      {
        Frame *ff = f;
        Node *body = n->b;
        VALUE fn_ctx[1] = { 0 };
        (void)fn_ctx;
        /* simple approach: use rb_block_call with a C callback */
        struct { Node *vars; Node *body; Frame *f; } ctx = { vars, body, ff };
        VALUE ctxv = (VALUE)&ctx;
        extern VALUE rb_for_iter_helper(VALUE yielded, VALUE ctxv, int argc, VALUE *argv);
        rb_tag tag; TAG_PUSH(tag);
        if(setjmp(tag.buf)==0){
          rb_block_call(coll, id_each, 0, NULL, rb_for_iter_helper, ctxv);
          TAG_POP(tag);
        } else { TAG_POP(tag);
          if(tag.state==TAG_BREAK && tag.target==NULL) return tag.retval;
          rb_jump_tag(tag.state,tag.retval,tag.target); }
      }
      return r; }
    case N_CASE: {
      VALUE subj = n->a ? rb_eval(n->a,f) : Qundef;
      for(int i=0;i<n->nitems;i++){
        Node *w = n->items[i];
        for(int k=0;k<w->c->nitems;k++){
          Node *cn = w->c->items[k];
          if(cn->type==N_SPLAT){
            VALUE arr = splat_to_array(rb_eval(cn->a,f));
            for(long j=0;j<RARR(arr)->len;j++){
              VALUE cv = RARR(arr)->ptr[j];
              int hit = (subj==Qundef)? RTEST(cv) : RTEST(rb_funcall(cv,id_eqq,1,subj));
              if(hit){ if(!w->a) return Qnil; n = w->a; goto tail; } }
            continue; }
          VALUE cv = rb_eval(cn,f);
          int hit = (subj==Qundef)? RTEST(cv) : RTEST(rb_funcall(cv,id_eqq,1,subj));
          if(hit){ if(!w->a) return Qnil; n = w->a; goto tail; }
        }
      }
      if(n->b){ n = n->b; goto tail; }
      return Qnil; }
    case N_CASE_IN: {
      VALUE subj = rb_eval(n->a,f);
      for(int i=0;i<n->nitems;i++){
        Node *w = n->items[i];
        if(pat_match(w->c, subj, f)){
          if(w->d && !RTEST(rb_eval(w->d,f))) continue;
          if(!w->a) return Qnil; n = w->a; goto tail; }
      }
      if(n->num){ n = n->b; goto tail; }
      rb_exc_raise(rb_exc_new(rb_eNoMatchingPattern, rb_inspect(subj))); }
    case N_MATCHPAT: {
      VALUE subj = rb_eval(n->a,f);
      if(!pat_match(n->b,subj,f)) rb_exc_raise(rb_exc_new(rb_eNoMatchingPattern, rb_inspect(subj)));
      return Qnil; }
    case N_MATCHPATP: {
      VALUE subj = rb_eval(n->a,f);
      return pat_match(n->b,subj,f)? Qtrue : Qfalse; }
    case N_MATCH: {
      VALUE l = rb_eval(n->a,f), r = rb_eval(n->b,f);
      if(!FIXNUM_P(l) && BASIC(l)->type==T_REGEXP) return rb_funcall(l,ID_CACHED("=~"),1,r);
      return rb_funcall(l,ID_CACHED("=~"),1,r); }
    case N_RETURN: {
      VALUE v = n->a? rb_eval(n->a,f) : Qnil;
      Frame *h = f; while(h && h->is_block && !h->in_lambda) h = h->home;
      rb_jump_tag(TAG_RETURN, v, h && h->is_block ? NULL : h); }
    case N_BREAK: { VALUE v = n->a? rb_eval(n->a,f) : Qnil; rb_jump_tag(TAG_BREAK, v, NULL); }
    case N_NEXT: { VALUE v = n->a? rb_eval(n->a,f) : Qnil; rb_jump_tag(TAG_NEXT, v, NULL); }
    case N_REDO: rb_jump_tag(TAG_REDO, Qnil, NULL);
    case N_RETRY: rb_jump_tag(TAG_RETRY, Qnil, NULL);
    case N_YIELD: {
      Frame *h = f; while(h && h->is_block) h = h->home;
      RProc *blk = h ? h->blk : NULL;
      if(!blk) rb_raise(rb_eLocalJumpError, "no block given (yield)");
      ArgList al; eval_args(n,f,&al);
      if(al.has_kw){ g_pending_kw = al.kw; g_has_pending_kw = 1; }
      return rb_proc_invoke(blk, al.n, al.v, NULL, 0); }
    case N_ONCE: if(n->num==1){ Frame *h=f; while(h&&h->is_block) h=h->home;
        return (h && h->mid)? rb_sym(h->mid) : Qnil; }
      return Qnil;
    case N_ERRINFO: return rb_errinfo;
    case N_LAMBDA: { RProc *p = proc_from_iter(n->b,f,1); return (VALUE)p; }
    case N_BLOCKPASS: { VALUE v = rb_eval(n->a,f); return v; }
    case N_CALL: case N_FCALL: case N_VCALL: case N_SUPER: case N_ZSUPER:
      return eval_call(n,f);
    case N_DEF: {
      method_entry *me = xcalloc(1,sizeof(method_entry));
      me->type = ME_ISEQ; me->params = n->a; me->body = n->b;
      me->env = (Env*)n->val;
      RClass *c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      me->cref = (RClass*)f->cref;
      me->visibility = (c==rb_cObject && f->self==main_obj) ? VIS_PRIVATE : VIS_PUBLIC;
      if(f->visibility_default) me->visibility = f->visibility_default;
      rb_add_method(c, n->id, me);
      if(f->visibility_default == VIS_MODFUNC){
        method_entry *m2 = xmalloc(sizeof(method_entry)); *m2 = *me; m2->visibility = VIS_PUBLIC;
        rb_add_method(rb_singleton_class((VALUE)c), n->id, m2);
        me->visibility = VIS_PRIVATE;
      }
      { RClass *ow; method_entry *hook = rb_method_lookup(BASIC((VALUE)c)->klass, ID_CACHED("method_added"), &ow);
        if(hook && ow != rb_cObject && ow != rb_cModule && ow != rb_cKernel && ow != rb_cBasicObject){
          VALUE a1[1] = { rb_sym(n->id) };
          rb_invoke_method((VALUE)c, hook, ow, ID_CACHED("method_added"), 1, a1, NULL, 0); } }
      return rb_sym(n->id); }
    case N_DEFS: {
      VALUE target = rb_eval(n->c,f);
      RClass *sc = rb_singleton_class(target);
      method_entry *me = xcalloc(1,sizeof(method_entry));
      me->type = ME_ISEQ; me->params = n->a; me->body = n->b;
      me->env = (Env*)n->val; me->cref = (RClass*)f->cref;
      rb_add_method(sc, n->id, me);
      return rb_sym(n->id); }
    case N_CLASS: {
      RClass *base;
      ID cid = n->c->id;
      if(n->c->type==N_COLON2) base = (RClass*)rb_eval(n->c->a,f);
      else if(n->c->type==N_COLON3) base = rb_cObject;
      else { base = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
             cref_t *cr = f->cref; int fnd=0;
             while(cr){ if(cr->klass){ ivtbl_get(cr->klass->consts,cid,&fnd); if(fnd){ base = cr->klass; break; } } cr=cr->next; }
             if(!fnd){ int f2; ivtbl_get(rb_cObject->consts,cid,&f2); if(f2) base = rb_cObject; } }
      int fnd; VALUE ex = ivtbl_get(base->consts, cid, &fnd);
      RClass *c;
      if(fnd){
        if(FIXNUM_P(ex)||BASIC(ex)->type != T_CLASS) rb_raise(rb_eTypeError,"%s is not a class",rb_id2name(cid));
        c = (RClass*)ex;
        if(n->d){ RClass *sup = (RClass*)rb_eval(n->d,f);
          if(c->super != sup) rb_raise(rb_eTypeError,"superclass mismatch for class %s",rb_id2name(cid)); }
      } else {
        RClass *sup = n->d ? (RClass*)rb_eval(n->d,f) : rb_cObject;
        if(FIXNUM_P((VALUE)sup) || BASIC((VALUE)sup)->type != T_CLASS)
          rb_raise(rb_eTypeError, "superclass must be a Class");
        c = rb_define_class_under(base, rb_id2name(cid), sup);
        { RClass *ow; method_entry *hook = rb_method_lookup(BASIC((VALUE)sup)->klass, ID_CACHED("inherited"), &ow);
          if(hook && ow != rb_cObject && ow != rb_cClass && ow != rb_cKernel && ow != rb_cBasicObject){
            VALUE a1[1] = { (VALUE)c };
            rb_invoke_method((VALUE)sup, hook, ow, ID_CACHED("inherited"), 1, a1, NULL, 0); } }
      }
      return eval_in_class(c, n->b, (LocalTbl*)n->val, f); }
    case N_MODULE: {
      RClass *base;
      ID cid = n->c->id;
      if(n->c->type==N_COLON2) base = (RClass*)rb_eval(n->c->a,f);
      else base = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      int fnd; VALUE ex = ivtbl_get(base->consts, cid, &fnd);
      RClass *c;
      if(fnd) c = (RClass*)ex;
      else c = rb_define_module_under(base, rb_id2name(cid));
      return eval_in_class(c, n->b, (LocalTbl*)n->val, f); }
    case N_SCLASS: {
      VALUE obj = rb_eval(n->a,f);
      RClass *sc = rb_singleton_class(obj);
      return eval_in_class(sc, n->b, (LocalTbl*)n->val, f); }
    case N_ALIAS: {
      RClass *c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      if(n->num){ rb_gv_set(n->id, rb_gv_get((ID)n->flags)); return Qnil; }
      RClass *ow; method_entry *me = rb_method_lookup(c, (ID)n->flags, &ow);
      if(!me) rb_raise(rb_eNameError, "undefined method '%s' for class '%s'", rb_id2name((ID)n->flags), rb_str_cstr(rb_class_name(c)));
      method_entry *m2 = xmalloc(sizeof(method_entry)); *m2 = *me; m2->orig_mid = (ID)n->flags;
      rb_add_method(c, n->id, m2);
      return Qnil; }
    case N_UNDEF: {
      RClass *c = f->cref && f->cref->klass ? f->cref->klass : rb_cObject;
      for(int i=0;i<n->nitems;i++){
        method_entry *me = xcalloc(1,sizeof(method_entry)); me->type = ME_UNDEF;
        rb_add_method(c, n->items[i]->id, me); }
      return Qnil; }
    case N_RESCUE: {
      while(1){
        rb_tag tag; TAG_PUSH(tag);
        VALUE r;
        if(setjmp(tag.buf)==0){
          r = rb_eval(n->a,f);
          TAG_POP(tag);
          if(n->b) r = rb_eval(n->b,f);
          return r;
        }
        TAG_POP(tag);
        if(tag.state != TAG_RAISE) rb_jump_tag(tag.state,tag.retval,tag.target);
        VALUE exc = tag.retval;
        VALUE save_err = rb_errinfo;
        rb_errinfo = exc;
        for(int i=0;i<n->nitems;i++){
          Node *rb2 = n->items[i];
          int match = 0;
          if(rb2->c->nitems==0) match = rb_is_a(exc, rb_eStandardError);
          else for(int k=0;k<rb2->c->nitems && !match;k++){
            Node *cn = rb2->c->items[k];
            if(cn->type==N_SPLAT){ VALUE arr = splat_to_array(rb_eval(cn->a,f));
              for(long j=0;j<RARR(arr)->len && !match;j++) if(RTEST(rb_funcall(RARR(arr)->ptr[j],id_eqq,1,exc))) match=1; }
            else { VALUE cv = rb_eval(cn,f);
              if(FIXNUM_P(cv) || (BASIC(cv)->type != T_CLASS && BASIC(cv)->type != T_MODULE))
                rb_raise(rb_eTypeError, "class or module required for rescue clause");
              if(RTEST(rb_funcall(cv,id_eqq,1,exc))) match=1; }
          }
          if(!match) continue;
          if(rb2->d) do_asgn_target(rb2->d, exc, f);
          rb_tag t2; TAG_PUSH(t2);
          VALUE rr;
          if(setjmp(t2.buf)==0){ rr = rb_eval(rb2->a,f); TAG_POP(t2); rb_errinfo = save_err; return rr; }
          TAG_POP(t2);
          if(t2.state==TAG_RETRY){ rb_errinfo = save_err; goto retry_rescue; }
          rb_errinfo = save_err;
          rb_jump_tag(t2.state,t2.retval,t2.target);
        }
        rb_errinfo = save_err;
        rb_jump_tag(TAG_RAISE, exc, NULL);
       retry_rescue: ;
      } }
    case N_ENSURE: {
      rb_tag tag; TAG_PUSH(tag);
      VALUE r = Qnil;
      if(setjmp(tag.buf)==0){ r = rb_eval(n->a,f); TAG_POP(tag); rb_eval(n->b,f); return r; }
      TAG_POP(tag);
      { int st = tag.state; VALUE rv = tag.retval; void *tg = tag.target;
        VALUE se = rb_errinfo;
        if(st==TAG_RAISE) rb_errinfo = rv;
        rb_eval(n->b,f);
        rb_errinfo = se;
        rb_jump_tag(st,rv,tg); } }
    default:
      rb_raise(rb_eRuntimeError, "eval: unhandled node type %d", n->type);
  }
  return Qnil;
}

VALUE rb_for_iter_helper(VALUE yielded, VALUE ctxv, int argc, VALUE *argv){
  struct { Node *vars; Node *body; Frame *f; } *ctx = (void*)ctxv;
  VALUE v = (argc==1)? argv[0] : ({ VALUE a = rb_ary_new(); for(int i=0;i<argc;i++) rb_ary_push(a,argv[i]); a; });
  if(ctx->vars->nitems==1) do_asgn_target(ctx->vars->items[0], v, ctx->f);
  else masgn_assign(ctx->vars, NULL, v, ctx->f);
  return rb_eval(ctx->body, ctx->f);
}

/* ===================== calls ===================== */
static VALUE eval_call(Node *n, Frame *f){
  if(n->type==N_VCALL){
    if(n->flags==1) rb_warn_deprecated("'it' calls without arguments will refer to the first block param in Ruby 3.4; use it() or self.it");
    /* CRuby distinguishes a bare identifier from an explicit call: `foo` that
       resolves to neither a local nor a method is a NameError, while `foo()`
       and `x.foo` are NoMethodError. */
    { RClass *ow = NULL;
      if(!rb_method_lookup(lookup_start_class(f->self), n->id, &ow)){
        RClass *ow2 = NULL;
        method_entry *mm = rb_method_lookup(lookup_start_class(f->self), id_method_missing, &ow2);
        if(!mm || ow2 == rb_cBasicObject){
          VALUE exc = rb_exc_new(rb_eNameError,
            rb_sprintf_val("undefined local variable or method '%s' for %s",
                           rb_id2name(n->id), rb_obj_classname(f->self)));
          rb_ivar_set(exc, ID_CACHED("@name"), rb_sym(n->id));
          rb_ivar_set(exc, ID_CACHED("@receiver"), f->self);
          rb_exc_raise(exc);
        } } }
    return rb_call_full(f->self, n->id, 0, NULL, NULL, 1, 1);
  }
  if(n->type==N_ZSUPER || n->type==N_SUPER){
    Frame *h = f; while(h && h->is_block) h = h->home;
    if(!h || !h->defclass) rb_raise(rb_eRuntimeError, "super called outside of method");
    RClass *owner = NULL;
    method_entry *me = rb_method_lookup_super(lookup_start_class(h->self), h->defclass, h->mid, &owner);
    RProc *blk = f->blk;
    ArgList al; al_init(&al);
    if(n->type==N_ZSUPER){ al.v = h->argv; al.n = h->argc; }
    else { eval_args(n,f,&al); if(n->b) blk = proc_from_iter(n->b,f,0); }
    if(n->type==N_SUPER && n->c) blk = eval_block_arg(n,f);
    if(!me){
      RClass *ow2; method_entry *mm = rb_method_lookup(lookup_start_class(h->self), ID_CACHED("method_missing"), &ow2);
      if(mm && ow2 != rb_cBasicObject) return method_missing(h->self, h->mid, al.n, al.v, blk, 0);
      rb_raise(rb_eNoMethodError, "super: no superclass method '%s'", rb_id2name(h->mid));
    }
    if(al.has_kw){ g_pending_kw = al.kw; g_has_pending_kw = 1; }
    return rb_invoke_method(h->self, me, owner, h->mid, al.n, al.v, blk, 1);
  }
  VALUE recv;
  int self_call = 0;
  if(n->type==N_FCALL){ recv = f->self; self_call = 1; }
  else {
    recv = rb_eval(n->a,f);
    if(n->num && NIL_P(recv)) return Qnil;         /* &. */
    if(n->a->type==N_SELF) self_call = 1;
    if(FIXNUM_P(recv) && n->nitems==1 && !n->b && !n->c && !n->d &&
       n->items[0]->type != N_SPLAT && n->items[0]->type != N_ANONSPLAT){
      VALUE rv = rb_eval(n->items[0],f), out;
      if(fix_binop(recv, n->id, rv, &out)) return out;
      return rb_call_full(recv, n->id, 1, &rv, NULL, 0, 0);
    }
  }
  ArgList al; eval_args(n,f,&al);
  RProc *blk = eval_block_arg(n,f);
  if(al.has_kw){ g_pending_kw = al.kw; g_has_pending_kw = 1; }
  VALUE r;
  if(n->b){
    /* a literal block: break unwinds to here */
    rb_tag tag; TAG_PUSH(tag);
    if(setjmp(tag.buf)==0){
      r = rb_call_full(recv, n->id, al.n, al.v, blk, self_call, self_call);
      TAG_POP(tag);
    } else {
      TAG_POP(tag);
      if(tag.state==TAG_BREAK) return tag.retval;
      rb_jump_tag(tag.state,tag.retval,tag.target);
    }
  } else {
    r = rb_call_full(recv, n->id, al.n, al.v, blk, self_call, self_call);
  }
  g_pending_kw = 0; g_has_pending_kw = 0;
  return r;
}

/* C-callback block support */
typedef struct CBlock { VALUE (*fn)(VALUE,VALUE,int,VALUE*); VALUE ctx; } CBlock;
static VALUE cblock_call(VALUE data, int argc, VALUE *argv, RProc *blk){
  CBlock *cb = (CBlock*)data;
  VALUE y = argc? argv[0] : Qnil;
  return cb->fn(y, cb->ctx, argc, argv);
}
VALUE rb_block_call(VALUE recv, ID mid, int argc, VALUE *argv,
                    VALUE (*fn)(VALUE,VALUE,int,VALUE*), VALUE ctx){
  CBlock *cb = xmalloc(sizeof(CBlock)); cb->fn = fn; cb->ctx = ctx;
  RProc *p = rb_cfunc_proc(cblock_call, (VALUE)cb, -1);
  p->is_lambda = 0;
  return rb_call(recv, mid, argc, argv, p);
}

/* ===================== top level ===================== */
VALUE rb_main_object(void){ return main_obj; }

/* Kernel#require / #require_relative / #load: evaluate another source file in a
   fresh top-level scope that shares globals, constants and methods. */
static const char *cur_file_stack[64];
static int cur_file_depth;
const char *rb_current_file(void){
  return cur_file_depth > 0 ? cur_file_stack[cur_file_depth - 1] : rb_source_file;
}
VALUE rb_load_source_file(const char *path){
  FILE *f = fopen(path, "rb");
  if(!f) return Qfalse;
  fseek(f,0,SEEK_END); long n = ftell(f); fseek(f,0,SEEK_SET);
  char *buf = xmalloc(n+1);
  size_t rd = fread(buf,1,n,f); buf[rd]=0; fclose(f);
  LocalTbl *tbl = NULL;
  const char *save_src = rb_source_file;
  rb_source_file = path;
  if(cur_file_depth < 64) cur_file_stack[cur_file_depth++] = path;
  Node *node = rb_parse_string(buf, path, &tbl);
  Frame fr; memset(&fr,0,sizeof fr);
  fr.self = main_obj; fr.cref = &toplevel_cref; fr.defclass = rb_cObject;
  fr.env = env_new(tbl,NULL); fr.prev = rb_curframe; fr.home = &fr;
  rb_curframe = &fr;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ rb_eval(node,&fr); TAG_POP(tag); }
  else { TAG_POP(tag); rb_curframe = fr.prev;
         if(cur_file_depth) cur_file_depth--;
         rb_source_file = save_src;
         rb_jump_tag(tag.state,tag.retval,tag.target); }
  rb_curframe = fr.prev;
  if(cur_file_depth) cur_file_depth--;
  rb_source_file = save_src;
  return Qtrue;
}

VALUE rb_eval_string_file(const char *src, const char *filename){
  LocalTbl *tbl = NULL;
  Node *n = rb_parse_string(src, filename, &tbl);
  Frame *fr = &toplevel_frame;
  fr->env = env_new(tbl,NULL);
  fr->self = main_obj;
  fr->cref = &toplevel_cref;
  fr->defclass = rb_cObject;
  fr->home = fr;
  rb_curframe = fr;
  return rb_eval(n, fr);
}

void rb_eval_init(void){
  id_call = ID_CACHED("call"); id_to_ary = ID_CACHED("to_ary"); id_to_a = ID_CACHED("to_a");
  id_to_proc = ID_CACHED("to_proc"); id_eqq = ID_CACHED("==="); id_each = ID_CACHED("each");
  id_method_missing = ID_CACHED("method_missing"); id_initialize = ID_CACHED("initialize");
  id_to_hash = ID_CACHED("to_hash"); id_deconstruct = ID_CACHED("deconstruct");
  id_deconstruct_keys = ID_CACHED("deconstruct_keys");
  op_plus = ID_CACHED("+"); op_minus = ID_CACHED("-"); op_mul = ID_CACHED("*");
  op_div = ID_CACHED("/"); op_mod = ID_CACHED("%"); op_lt = ID_CACHED("<");
  op_gt = ID_CACHED(">"); op_le = ID_CACHED("<="); op_ge = ID_CACHED(">=");
  op_eq = ID_CACHED("=="); op_neq = ID_CACHED("!="); op_cmp = ID_CACHED("<=>");
  op_and = ID_CACHED("&"); op_or = ID_CACHED("|"); op_xor = ID_CACHED("^");
  op_lshift = ID_CACHED("<<"); op_aref = ID_CACHED("[]");
  toplevel_cref.klass = rb_cObject;
  main_obj = rb_obj_alloc_of(rb_cObject);
  memset(&toplevel_frame,0,sizeof toplevel_frame);
  toplevel_frame.self = main_obj;
  toplevel_frame.cref = &toplevel_cref;
  toplevel_frame.defclass = rb_cObject;
  toplevel_frame.home = &toplevel_frame;
  rb_curframe = &toplevel_frame;
}
