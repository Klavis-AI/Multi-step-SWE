#include "internal.h"
#include <errno.h>
#include <ctype.h>

extern VALUE rb_obj_alloc_of(RClass *);
extern void rb_const_set(RClass *, ID, VALUE);
extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);
extern RClass **rb_class_ancestors(RClass *, int *);
extern VALUE rb_call_full(VALUE, ID, int, VALUE *, RProc *, int, int);
extern VALUE rb_main_object(void);
extern VALUE rb_gv_get(ID); extern void rb_gv_set(ID, VALUE);

static unsigned long objid_counter = 8;

VALUE rb_any_to_s(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->iv == NULL) {}
  char buf[128];
  snprintf(buf,sizeof buf,"#<%s:0x%016lx>", rb_obj_classname(v), (unsigned long)v);
  return rb_str_new_cstr(buf);
}
VALUE rb_obj_inspect(VALUE v){
  if(FIXNUM_P(v)) return rb_funcall(v, ID_CACHED("to_s"), 0);
  ivtbl *t = BASIC(v)->iv;
  if(!t || t->n==0) return rb_any_to_s(v);
  char buf[128];
  snprintf(buf,sizeof buf,"#<%s", rb_obj_classname(v));
  VALUE s = rb_str_new_cstr(buf);
  int first = 1;
  for(int i=0;i<t->n;i++){
    const char *nm = rb_id2name(t->keys[i]);
    if(nm[0]=='_' && nm[1]=='_') continue;
    rb_str_cat_cstr(s, first? " @" : ", @"); first = 0;
    rb_str_cat_cstr(s, nm); rb_str_cat_cstr(s,"=");
    VALUE iv = rb_inspect(t->vals[i]);
    rb_str_cat(s, RSTR(iv)->ptr, RSTR(iv)->len);
  }
  rb_str_cat_cstr(s,">");
  return s;
}

/* ===================== Object / Kernel ===================== */
static VALUE obj_class(VALUE self, RProc *b){ (void)b; return (VALUE)rb_class_of(self); }
static VALUE obj_singleton_class(VALUE self, RProc *b){ (void)b; return (VALUE)rb_singleton_class(self); }
static VALUE obj_is_a(VALUE self, VALUE c, RProc *b){ (void)b;
  if(FIXNUM_P(c) || (BASIC(c)->type != T_CLASS && BASIC(c)->type != T_MODULE))
    rb_raise(rb_eTypeError, "class or module required");
  return rb_is_a(self,(RClass*)c)? Qtrue : Qfalse; }
static VALUE obj_instance_of(VALUE self, VALUE c, RProc *b){ (void)b;
  return (VALUE)rb_class_of(self)==c? Qtrue:Qfalse; }
static VALUE obj_nil_p(VALUE self, RProc *b){ (void)b; return NIL_P(self)?Qtrue:Qfalse; }
static VALUE obj_equal(VALUE self, VALUE o, RProc *b){ (void)b; return self==o?Qtrue:Qfalse; }
static VALUE obj_not_equal(VALUE self, VALUE o, RProc *b){ (void)b;
  return RTEST(rb_funcall(self,ID_CACHED("=="),1,o))? Qfalse:Qtrue; }
static VALUE obj_eqq(VALUE self, VALUE o, RProc *b){ (void)b; return rb_funcall(self,ID_CACHED("=="),1,o); }
static VALUE obj_match(VALUE self, VALUE o, RProc *b){ (void)self;(void)o;(void)b; return Qnil; }
static VALUE obj_nmatch(VALUE self, VALUE o, RProc *b){ (void)b;
  return RTEST(rb_funcall(self,ID_CACHED("=~"),1,o))? Qfalse:Qtrue; }
static VALUE obj_cmp(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return LONG2FIX(0);
  if(RTEST(rb_funcall(self,ID_CACHED("=="),1,o))) return LONG2FIX(0);
  return Qnil; }
static VALUE obj_not(VALUE self, RProc *b){ (void)b; return RTEST(self)?Qfalse:Qtrue; }
static VALUE obj_hash(VALUE self, RProc *b){ (void)b; return LONG2FIX((long)(self>>3)); }
static VALUE obj_object_id(VALUE self, RProc *b){ (void)b;
  if(FIXNUM_P(self)) return LONG2FIX(FIX2LONG(self)*2+1);
  if(NIL_P(self)) return LONG2FIX(8);
  if(self==Qtrue) return LONG2FIX(20);
  if(self==Qfalse) return LONG2FIX(0);
  ID k = ID_CACHED("__object_id__");
  if(rb_ivar_defined(self,k)) return rb_ivar_get(self,k);
  VALUE id = LONG2FIX((long)(objid_counter += 8));
  ivtbl_set(&BASIC(self)->iv, k, id);
  return id; }
static VALUE obj_to_s(VALUE self, RProc *b){ (void)b; return rb_any_to_s(self); }
static VALUE obj_inspect(VALUE self, RProc *b){ (void)b; return rb_obj_inspect(self); }
static VALUE obj_freeze(VALUE self, RProc *b){ (void)b; return rb_obj_freeze(self); }
static VALUE obj_frozen(VALUE self, RProc *b){ (void)b; return rb_obj_frozen(self)?Qtrue:Qfalse; }
static VALUE obj_itself(VALUE self, RProc *b){ (void)b; return self; }
static VALUE obj_tap(VALUE self, RProc *b){ rb_yield1(b,self); return self; }
static VALUE obj_then(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("then"), 0, NULL);
  return rb_yield1(b,self); }

VALUE rb_obj_dup(VALUE v){
  if(FIXNUM_P(v)) return v;
  switch(BASIC(v)->type){
    case T_NIL: case T_TRUE: case T_FALSE: case T_SYMBOL: case T_FLOAT: return v;
    case T_STRING: return rb_str_dup(v);
    case T_ARRAY: return rb_ary_dup(v);
    case T_HASH: { VALUE n = rb_hash_new(); RHash *h = RHSH(v);
      for(long i=0;i<h->n;i++) if(h->ent[i].used) rb_hash_aset(n,h->ent[i].key,h->ent[i].val);
      RHSH(n)->ifnone = h->ifnone; RHSH(n)->defproc = h->defproc;
      RHSH(n)->compare_by_identity = h->compare_by_identity;
      BASIC(n)->klass = BASIC(v)->klass; return n; }
    case T_CLASS: case T_MODULE: return v;
    default: break;
  }
  size_t sz = sizeof(RObject);
  switch(BASIC(v)->type){
    case T_RANGE: sz = sizeof(RRange); break;
    case T_PROC: sz = sizeof(RProc); break;
    case T_REGEXP: sz = sizeof(RRegexp); break;
    case T_MATCH: sz = sizeof(RMatch); break;
    default: sz = sizeof(RObject); break;
  }
  RBasic *n = xmalloc(sz < 512 ? 512 : sz);
  memcpy(n, (void*)v, sz);
  n->frozen = 0; n->iv = NULL;
  if(BASIC(v)->iv){ ivtbl *t = BASIC(v)->iv;
    for(int i=0;i<t->n;i++) if(t->keys[i] != ID_CACHED("__object_id__")) ivtbl_set(&n->iv,t->keys[i],t->vals[i]); }
  return (VALUE)n;
}
static VALUE obj_dup(VALUE self, RProc *b){ (void)b;
  VALUE n = rb_obj_dup(self);
  if(n != self && rb_respond_to(n, ID_CACHED("initialize_copy")))
    rb_funcall(n, ID_CACHED("initialize_copy"), 1, self);
  return n; }
static VALUE obj_clone(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  int freeze = -1;
  if(argc>0 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_HASH){
    VALUE f = rb_hash_lookup(argv[0], rb_sym(ID_CACHED("freeze")), Qundef);
    if(f != Qundef) freeze = RTEST(f)?1:0;
  }
  VALUE n = rb_obj_dup(self);
  if(n != self){
    if(!FIXNUM_P(self) && BASIC(self)->klass && BASIC(self)->klass->is_singleton){
      RClass *sc = rb_singleton_class(n);
      mtbl *src = BASIC(self)->klass->m;
      if(src) for(int i=0;i<src->n;i++) rb_add_method(sc, src->keys[i], src->vals[i]);
    }
    if(rb_respond_to(n, ID_CACHED("initialize_copy")))
      rb_funcall(n, ID_CACHED("initialize_copy"), 1, self);
    if(freeze==1 || (freeze==-1 && rb_obj_frozen(self))) rb_obj_freeze(n);
  }
  return n; }
static VALUE obj_init_copy(VALUE self, VALUE o, RProc *b){ (void)o;(void)b; return self; }
static VALUE obj_initialize(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b; return Qnil; }

static VALUE obj_send(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc<1) rb_raise(rb_eArgError,"no method name given");
  ID mid;
  if(!FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_SYMBOL) mid = RSYM(argv[0])->id;
  else mid = rb_intern2(RSTR(rb_str_to_str(argv[0]))->ptr, RSTR(rb_str_to_str(argv[0]))->len);
  return rb_call_full(self, mid, argc-1, argv+1, b, 1, 0); }
static VALUE obj_public_send(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc<1) rb_raise(rb_eArgError,"no method name given");
  ID mid;
  if(!FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_SYMBOL) mid = RSYM(argv[0])->id;
  else mid = rb_intern2(RSTR(rb_str_to_str(argv[0]))->ptr, RSTR(rb_str_to_str(argv[0]))->len);
  return rb_call_full(self, mid, argc-1, argv+1, b, 0, 0); }
static ID to_mid(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type==T_SYMBOL) return RSYM(v)->id;
  VALUE s = rb_str_to_str(v);
  return rb_intern2(RSTR(s)->ptr, RSTR(s)->len);
}
static VALUE obj_respond_to(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc<1) rb_raise(rb_eArgError,"wrong number of arguments (given 0, expected 1..2)");
  ID mid = to_mid(argv[0]);
  int all = argc>1 && RTEST(argv[1]);
  RClass *ow; method_entry *me = rb_method_lookup(FIXNUM_P(self)?rb_cInteger:BASIC(self)->klass, mid, &ow);
  if(me) return (me->visibility==VIS_PUBLIC || all)? Qtrue : Qfalse;
  VALUE args[2] = { rb_sym(mid), all?Qtrue:Qfalse };
  return rb_call(self, ID_CACHED("respond_to_missing?"), 2, args, NULL); }
static VALUE obj_respond_to_missing(VALUE self, VALUE a, VALUE c, RProc *b){ (void)self;(void)a;(void)c;(void)b; return Qfalse; }
static VALUE obj_ivar_get(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name);
  const char *nm = rb_id2name(id);
  if(nm[0] != '@') rb_raise(rb_eNameError, "'%s' is not allowed as an instance variable name", nm);
  return rb_ivar_get(self, rb_intern(nm+1)); }
static VALUE obj_ivar_set(VALUE self, VALUE name, VALUE val, RProc *b){ (void)b;
  ID id = to_mid(name); const char *nm = rb_id2name(id);
  if(nm[0] != '@') rb_raise(rb_eNameError, "'%s' is not allowed as an instance variable name", nm);
  rb_ivar_set(self, rb_intern(nm+1), val); return val; }
static VALUE obj_ivar_defined(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name); const char *nm = rb_id2name(id);
  if(nm[0] != '@') rb_raise(rb_eNameError, "'%s' is not allowed as an instance variable name", nm);
  return rb_ivar_defined(self, rb_intern(nm+1))? Qtrue:Qfalse; }
static VALUE obj_ivars(VALUE self, RProc *b){ (void)b;
  VALUE a = rb_ary_new();
  if(FIXNUM_P(self)) return a;
  ivtbl *t = BASIC(self)->iv;
  if(t) for(int i=0;i<t->n;i++){ const char *nm = rb_id2name(t->keys[i]);
    if(nm[0]=='_'&&nm[1]=='_') continue;
    char buf[256]; snprintf(buf,sizeof buf,"@%s",nm); rb_ary_push(a, rb_sym(rb_intern(buf))); }
  return a; }
static VALUE obj_remove_ivar(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name); const char *nm = rb_id2name(id);
  VALUE out;
  if(!ivtbl_delete(BASIC(self)->iv, rb_intern(nm+1), &out))
    rb_raise(rb_eNameError, "instance variable %s not defined", nm);
  return out; }
static VALUE obj_extend(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  for(int i=argc-1;i>=0;i--){
    if(FIXNUM_P(argv[i]) || BASIC(argv[i])->type != T_MODULE)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Module)", rb_obj_classname(argv[i]));
    RClass *m = (RClass*)argv[i];
    RClass *ow; method_entry *hook = rb_method_lookup(BASIC((VALUE)m)->klass, ID_CACHED("extended"), &ow);
    rb_include_module(rb_singleton_class(self), m);
    if(hook && ow != rb_cObject && ow != rb_cModule && ow != rb_cKernel && ow != rb_cBasicObject){
      VALUE a1[1] = { self }; rb_invoke_method((VALUE)m,hook,ow,ID_CACHED("extended"),1,a1,NULL,0); }
  }
  return self; }

static VALUE obj_methods_list(VALUE self, int which){
  VALUE a = rb_ary_new();
  RClass *k = FIXNUM_P(self)? rb_cInteger : BASIC(self)->klass;
  int n; RClass **anc = rb_class_ancestors(k,&n);
  for(int i=0;i<n;i++){
    mtbl *t = anc[i]->m; if(!t) continue;
    for(int j=0;j<t->n;j++){
      if(t->vals[j]->type==ME_UNDEF) continue;
      if(which==0 && t->vals[j]->visibility != VIS_PUBLIC && t->vals[j]->visibility != VIS_PROTECTED) continue;
      if(which==1 && t->vals[j]->visibility != VIS_PRIVATE) continue;
      if(which==2 && t->vals[j]->visibility != VIS_PUBLIC) continue;
      VALUE s = rb_sym(t->keys[j]);
      int dup=0; for(long q=0;q<RARR(a)->len;q++) if(RARR(a)->ptr[q]==s){dup=1;break;}
      if(!dup) rb_ary_push(a,s);
    }
  }
  return a; }
static VALUE obj_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b; return obj_methods_list(self,0); }
static VALUE obj_public_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b; return obj_methods_list(self,2); }
static VALUE obj_private_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b; return obj_methods_list(self,1); }
static VALUE obj_singleton_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b;
  VALUE a = rb_ary_new();
  if(FIXNUM_P(self)) return a;
  RClass *k = BASIC(self)->klass;
  if(!k || !k->is_singleton) return a;
  mtbl *t = k->m; if(t) for(int j=0;j<t->n;j++) if(t->vals[j]->type != ME_UNDEF) rb_ary_push(a, rb_sym(t->keys[j]));
  return a; }

static VALUE mk_method_obj(VALUE recv, RClass *owner, ID mid, method_entry *me, int unbound){
  RMethodObj *m = (RMethodObj*)xcalloc(1,sizeof(RMethodObj));
  m->basic.type = T_METHODOBJ; m->basic.klass = unbound? rb_cUnboundMethod : rb_cMethod;
  m->recv = recv; m->owner = owner; m->mid = mid; m->me = me; m->unbound = unbound;
  return (VALUE)m;
}
static VALUE obj_method(VALUE self, VALUE name, RProc *b){ (void)b;
  ID mid = to_mid(name);
  RClass *ow; method_entry *me = rb_method_lookup(FIXNUM_P(self)?rb_cInteger:BASIC(self)->klass, mid, &ow);
  if(!me){
    if(rb_respond_to(self,mid)) { }
    else rb_raise(rb_eNameError, "undefined method '%s' for %s", rb_id2name(mid), rb_str_cstr(rb_inspect(self)));
  }
  return mk_method_obj(self, ow, mid, me, 0); }

static VALUE obj_instance_eval(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) rb_raise(rb_eArgError, "block not supplied");
  (void)argc;(void)argv;
  RProc p = *b;
  p.self = self;
  RClass *sc = (FIXNUM_P(self) || BASIC(self)->type==T_SYMBOL) ? rb_class_of(self) : rb_singleton_class(self);
  p.defclass = sc;
  cref_t *cr = xcalloc(1,sizeof(cref_t)); cr->klass = sc; cr->next = (cref_t*)b->cref_list;
  p.cref_list = (struct rb_cref*)cr;
  VALUE a1[1] = { self };
  return rb_proc_invoke(&p, 1, a1, NULL, 0); }
static VALUE obj_instance_exec(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) rb_raise(rb_eArgError, "block not supplied");
  RProc p = *b; p.self = self;
  RClass *sc = (FIXNUM_P(self)||BASIC(self)->type==T_SYMBOL)? rb_class_of(self) : rb_singleton_class(self);
  p.defclass = sc;
  cref_t *cr = xcalloc(1,sizeof(cref_t)); cr->klass = sc; cr->next = (cref_t*)b->cref_list;
  p.cref_list = (struct rb_cref*)cr;
  return rb_proc_invoke(&p, argc, argv, NULL, 0); }
static VALUE obj_define_singleton_method(VALUE self, int argc, VALUE *argv, RProc *b){
  RClass *sc = rb_singleton_class(self);
  VALUE a2[2]; int n=0;
  a2[n++] = argv[0];
  if(argc>1) a2[n++] = argv[1];
  return rb_call((VALUE)sc, ID_CACHED("define_method"), n, a2, b); }
static VALUE obj_display(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b;
  VALUE s = rb_obj_as_string(self); fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stdout); return Qnil; }

/* ===================== Kernel IO / control ===================== */
static void puts_one(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type==T_ARRAY){
    if(RARR(v)->len==0){ fputc('\n',stdout); return; }
    for(long i=0;i<RARR(v)->len;i++) puts_one(RARR(v)->ptr[i]);
    return;
  }
  VALUE a = rb_check_array_type(v);
  if(!NIL_P(a) && !FIXNUM_P(v) && BASIC(v)->type != T_STRING){
    for(long i=0;i<RARR(a)->len;i++) puts_one(RARR(a)->ptr[i]);
    return;
  }
  VALUE s;
  if(NIL_P(v)) s = rb_str_new_cstr("");
  else s = rb_obj_as_string(v);
  fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stdout);
  if(RSTR(s)->len==0 || RSTR(s)->ptr[RSTR(s)->len-1] != '\n') fputc('\n',stdout);
}
static VALUE krn_puts(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  if(argc==0){ fputc('\n',stdout); return Qnil; }
  for(int i=0;i<argc;i++) puts_one(argv[i]);
  return Qnil; }
static VALUE krn_print(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  for(int i=0;i<argc;i++){ VALUE s = rb_obj_as_string(argv[i]); fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stdout); }
  return Qnil; }
static VALUE krn_p(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  for(int i=0;i<argc;i++){ VALUE s = rb_inspect(argv[i]);
    fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stdout); fputc('\n',stdout); }
  if(argc==0) return Qnil;
  if(argc==1) return argv[0];
  return rb_ary_new_from(argc,argv); }
static VALUE krn_pp(VALUE self, int argc, VALUE *argv, RProc *b){ return krn_p(self,argc,argv,b); }
extern VALUE rb_str_format(VALUE fmt, VALUE args);
static VALUE krn_sprintf(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  if(argc<1) rb_raise(rb_eArgError,"wrong number of arguments");
  VALUE rest = rb_ary_new_from(argc-1, argv+1);
  return rb_str_format(rb_str_to_str(argv[0]), rest); }
static VALUE krn_printf(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc==0) return Qnil;
  VALUE s = krn_sprintf(self,argc,argv,b);
  fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stdout); return Qnil; }
static VALUE krn_warn(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  for(int i=0;i<argc;i++){
    if(!FIXNUM_P(argv[i]) && BASIC(argv[i])->type==T_HASH) continue;
    VALUE s = rb_obj_as_string(argv[i]);
    fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stderr);
    if(RSTR(s)->len==0||RSTR(s)->ptr[RSTR(s)->len-1] != '\n') fputc('\n',stderr); }
  return Qnil; }
/* `block_given?` asks about the method that called it, not about the C frame
   this built-in itself runs in. */
static VALUE krn_block_given(VALUE self, RProc *b){ (void)self;(void)b;
  Frame *f = rb_curframe ? rb_curframe->prev : NULL;
  while(f && f->is_block) f = f->home;
  return (f && f->blk) ? Qtrue : Qfalse; }

VALUE rb_make_exception(int argc, VALUE *argv){
  if(argc==0) return Qnil;
  VALUE e = argv[0];
  if(!FIXNUM_P(e) && BASIC(e)->type==T_STRING)
    return rb_exc_new(rb_eRuntimeError, e);
  if(!FIXNUM_P(e) && (BASIC(e)->type==T_CLASS)){
    VALUE msg = argc>1? argv[1] : Qundef;
    VALUE r;
    if(msg==Qundef) r = rb_call(e, ID_CACHED("new"), 0, NULL, NULL);
    else r = rb_call(e, ID_CACHED("new"), 1, &msg, NULL);
    if(!rb_is_a(r, rb_cException)) rb_raise(rb_eTypeError,"exception class/object expected");
    return r;
  }
  if(rb_is_a(e, rb_cException)){
    if(argc>1){ VALUE r = rb_funcall(e, ID_CACHED("exception"), 1, argv[1]); return r; }
    return e;
  }
  if(rb_respond_to(e, ID_CACHED("exception"))){
    VALUE r = argc>1 ? rb_funcall(e,ID_CACHED("exception"),1,argv[1]) : rb_funcall(e,ID_CACHED("exception"),0);
    if(rb_is_a(r,rb_cException)) return r;
  }
  rb_raise(rb_eTypeError, "exception class/object expected");
  return Qnil;
}
static VALUE krn_raise(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  if(argc==0){
    if(!NIL_P(rb_errinfo)) rb_exc_raise(rb_errinfo);
    rb_exc_raise(rb_exc_new(rb_eRuntimeError, rb_str_new_cstr("unhandled exception")));
  }
  rb_exc_raise(rb_make_exception(argc,argv));
  return Qnil; }
static VALUE krn_loop(VALUE self, RProc *b){ (void)self;
  if(!b) return rb_enum_new(self, ID_CACHED("loop"), 0, NULL);
  while(1){
    rb_tag tag; TAG_PUSH(tag);
    if(setjmp(tag.buf)==0){ rb_yield_values(b,0,NULL); TAG_POP(tag); }
    else { TAG_POP(tag);
      if(tag.state==TAG_RAISE && rb_is_a(tag.retval, rb_eStopIteration)){
        VALUE r = rb_funcall(tag.retval, ID_CACHED("result"), 0);
#if RUBY_TARGET >= 20
        return r;
#else
        (void)r; return Qnil;
#endif
      }
      rb_jump_tag(tag.state,tag.retval,tag.target); }
  }
  return Qnil; }
static VALUE krn_lambda(VALUE self, RProc *b){ (void)self;
  if(!b) rb_raise(rb_eArgError, "tried to create Proc object without a block");
  RProc *p = xmalloc(sizeof(RProc)); *p = *b; p->is_lambda = 1; return (VALUE)p; }
static VALUE krn_proc(VALUE self, RProc *b){ (void)self;
  if(!b) rb_raise(rb_eArgError, "tried to create Proc object without a block");
  RProc *p = xmalloc(sizeof(RProc)); *p = *b;
#if RUBY_TARGET < 20
  p->is_lambda = 1;   /* 1.8: Kernel#proc is a synonym for lambda */
#else
  p->is_lambda = 0;
#endif
  return (VALUE)p; }

static VALUE krn_catch(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  VALUE tagv = argc>0? argv[0] : rb_obj_alloc_of(rb_cObject);
  rb_tag tag; TAG_PUSH(tag);
  VALUE r;
  if(setjmp(tag.buf)==0){ r = rb_yield1(b, tagv); TAG_POP(tag); return r; }
  TAG_POP(tag);
  if(tag.state==TAG_THROW){
    VALUE pair = tag.retval;
    if(rb_equal(RARR(pair)->ptr[0], tagv)) return RARR(pair)->ptr[1];
  }
  rb_jump_tag(tag.state,tag.retval,tag.target);
  return Qnil; }
static VALUE krn_throw(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE pair = rb_ary_new();
  rb_ary_push(pair, argv[0]);
  rb_ary_push(pair, argc>1? argv[1] : Qnil);
  rb_jump_tag(TAG_THROW, pair, NULL);
  return Qnil; }
static VALUE krn_exit(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  int code = 0;
  if(argc>0){ if(argv[0]==Qtrue) code=0; else if(argv[0]==Qfalse) code=1; else code=(int)rb_num2long(argv[0]); }
  VALUE e = rb_exc_new(rb_eSystemExit, rb_str_new_cstr("exit"));
  rb_ivar_set(e, ID_CACHED("@status"), LONG2FIX(code));
  rb_exc_raise(e); return Qnil; }
static VALUE krn_abort(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  if(argc>0){ VALUE s = rb_obj_as_string(argv[0]); fwrite(RSTR(s)->ptr,1,RSTR(s)->len,stderr); fputc('\n',stderr); }
  VALUE e = rb_exc_new(rb_eSystemExit, argc>0? argv[0] : rb_str_new_cstr("abort"));
  rb_ivar_set(e, ID_CACHED("@status"), LONG2FIX(1));
  rb_exc_raise(e); return Qnil; }
static VALUE krn_freeze_noop(VALUE self, RProc *b){ (void)b; return self; }
static VALUE krn_to_enum(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  ID mid = argc>0? to_mid(argv[0]) : ID_CACHED("each");
  return rb_enum_new(self, mid, argc>1?argc-1:0, argc>1?argv+1:NULL); }
extern VALUE rb_load_source_file(const char *path);
extern const char *rb_current_file(void);

static VALUE loaded_features(void){
  VALUE v = rb_gv_get(ID_CACHED("$LOADED_FEATURES"));
  if(NIL_P(v) || FIXNUM_P(v)){ v = rb_ary_new(); rb_gv_set(ID_CACHED("$LOADED_FEATURES"), v);
    rb_gv_set(ID_CACHED("$\""), v); }
  return v;
}
static int file_readable(const char *p){ FILE *f = fopen(p,"rb"); if(f){ fclose(f); return 1; } return 0; }
static VALUE join_path(const char *dir, const char *name){
  VALUE s = rb_str_new_cstr(dir);
  if(RSTR(s)->len && RSTR(s)->ptr[RSTR(s)->len-1] != '/') rb_str_cat_cstr(s,"/");
  rb_str_cat_cstr(s,name); return s;
}
static VALUE find_feature(const char *n){
  long len = (long)strlen(n);
  int has_rb = (len > 3 && strcmp(n+len-3,".rb")==0);
  if(n[0]=='/' || (n[0]=='.' && n[1]=='/')){
    if(has_rb && file_readable(n)) return rb_str_new_cstr(n);
    if(!has_rb){ VALUE c = rb_str_new_cstr(n); rb_str_cat_cstr(c,".rb");
      if(file_readable(RSTR(c)->ptr)) return c; }
    return Qnil;
  }
  VALUE lp = rb_gv_get(ID_CACHED("$LOAD_PATH"));
  if(NIL_P(lp) || FIXNUM_P(lp)) lp = rb_ary_new();
  VALUE dirs = rb_ary_dup(lp);
  rb_ary_push(dirs, rb_str_new_cstr("."));
  for(long i=0;i<RARR(dirs)->len;i++){
    VALUE d = RARR(dirs)->ptr[i];
    if(FIXNUM_P(d) || BASIC(d)->type != T_STRING) continue;
    VALUE cand = join_path(RSTR(d)->ptr, n);
    if(has_rb){ if(file_readable(RSTR(cand)->ptr)) return cand; }
    else { rb_str_cat_cstr(cand,".rb");
      if(file_readable(RSTR(cand)->ptr)) return cand; }
  }
  return Qnil;
}
static VALUE do_require(VALUE path_or_name, int relative){
  VALUE s = rb_str_to_str(path_or_name);
  const char *n = RSTR(s)->ptr;
  VALUE found;
  if(relative){
    const char *cf = rb_current_file();
    char dir[4096]; snprintf(dir,sizeof dir,"%s",cf);
    char *slash = strrchr(dir,'/');
    if(slash) *slash = 0; else strcpy(dir,".");
    VALUE cand = join_path(dir,n);
    long l = RSTR(cand)->len;
    if(!(l>3 && memcmp(RSTR(cand)->ptr+l-3,".rb",3)==0)) rb_str_cat_cstr(cand,".rb");
    found = file_readable(RSTR(cand)->ptr) ? cand : Qnil;
  } else {
    static const char *builtin[] = {"set","enumerator","thread","fiber","English","pp",
                                    "prettyprint","stringio","date","time",NULL};
    found = find_feature(n);
    if(NIL_P(found)){
      for(int i=0;builtin[i];i++) if(strcmp(n,builtin[i])==0) return Qfalse;
    }
  }
  if(NIL_P(found)) rb_raise(rb_eScriptError, "cannot load such file -- %s", n);
  VALUE feats = loaded_features();
  for(long i=0;i<RARR(feats)->len;i++)
    if(rb_str_equal(RARR(feats)->ptr[i], found)) return Qfalse;
  rb_ary_push(feats, found);
  return rb_load_source_file(RSTR(found)->ptr);
}
static VALUE krn_require(VALUE self, VALUE name, RProc *b){ (void)self;(void)b;
  return do_require(name, 0); }
static VALUE krn_require_rel(VALUE self, VALUE name, RProc *b){ (void)self;(void)b;
  return do_require(name, 1); }
static VALUE krn_load(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE s = rb_str_to_str(argv[0]);
  VALUE found = find_feature(RSTR(s)->ptr);
  if(NIL_P(found)) rb_raise(rb_eScriptError, "cannot load such file -- %s", RSTR(s)->ptr);
  return rb_load_source_file(RSTR(found)->ptr); }

static VALUE krn_Integer(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE v = argv[0];
  int base = argc>1 && !NIL_P(argv[1]) && FIXNUM_P(argv[1]) ? (int)FIX2LONG(argv[1]) : 0;
  int exception = 1;
  if(argc>1 && !FIXNUM_P(argv[argc-1]) && BASIC(argv[argc-1])->type==T_HASH){
    VALUE e = rb_hash_lookup(argv[argc-1], rb_sym(ID_CACHED("exception")), Qundef);
    if(e != Qundef) exception = RTEST(e);
  }
  if(FIXNUM_P(v)) return v;
  if(BASIC(v)->type==T_BIGNUM) return v;
  if(BASIC(v)->type==T_FLOAT){ double d = RFLT(v)->f;
    if(d != d || d==1.0/0.0 || d==-1.0/0.0){ if(!exception) return Qnil; rb_raise(rb_eFloatDomainError,"%g",d); }
    return rb_int_new((long long)d); }
  if(BASIC(v)->type==T_STRING){
    RString *s = RSTR(v);
    char *buf = xmalloc(s->len+1); long k=0;
    for(long i=0;i<s->len;i++){ if(s->ptr[i]=='_'){ if(i==0||i==s->len-1||s->ptr[i-1]=='_') { k=-1; break; } continue; }
      buf[k++]=s->ptr[i]; }
    if(k<0){ if(!exception) return Qnil; rb_raise(rb_eArgError,"invalid value for Integer(): %s", rb_str_cstr(rb_inspect(v))); }
    buf[k]=0;
    char *p = buf; while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    char *endp; errno = 0;
    int bb = base;
    if(bb==0){
      if(p[0]=='0' && (p[1]=='x'||p[1]=='X')) bb=16;
      else if(p[0]=='0' && (p[1]=='b'||p[1]=='B')) bb=2;
      else if(p[0]=='0' && (p[1]=='o'||p[1]=='O')) bb=8;
      else if(p[0]=='0' && p[1]) bb=8;
      else bb=10;
    }
    if((bb==16 && p[0]=='0' && (p[1]=='x'||p[1]=='X'))) p+=2;
    else if(bb==2 && p[0]=='0' && (p[1]=='b'||p[1]=='B')) p+=2;
    else if(bb==8 && p[0]=='0' && (p[1]=='o'||p[1]=='O')) p+=2;
    long long r = strtoll(p,&endp,bb);
    if(endp==p){ if(!exception) return Qnil; rb_raise(rb_eArgError,"invalid value for Integer(): %s", rb_str_cstr(rb_inspect(v))); }
    while(*endp==' '||*endp=='\t'||*endp=='\n'||*endp=='\r') endp++;
    if(*endp){ if(!exception) return Qnil; rb_raise(rb_eArgError,"invalid value for Integer(): %s", rb_str_cstr(rb_inspect(v))); }
    return rb_int_new(r); }
  if(NIL_P(v)){ if(!exception) return Qnil; rb_raise(rb_eTypeError,"can't convert nil into Integer"); }
  VALUE t = rb_check_to_int(v);
  if(NIL_P(t)){
    if(rb_respond_to(v, ID_CACHED("to_i"))) return rb_funcall(v, ID_CACHED("to_i"), 0);
    if(!exception) return Qnil;
    rb_raise(rb_eTypeError, "can't convert %s into Integer", rb_obj_classname(v)); }
  return t; }
static VALUE krn_Float(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE v = argv[0];
  int exception = 1;
  if(argc>1 && !FIXNUM_P(argv[argc-1]) && BASIC(argv[argc-1])->type==T_HASH){
    VALUE e = rb_hash_lookup(argv[argc-1], rb_sym(ID_CACHED("exception")), Qundef);
    if(e != Qundef) exception = RTEST(e); }
  if(!FIXNUM_P(v) && BASIC(v)->type==T_STRING){
    RString *s = RSTR(v);
    char *buf = xmalloc(s->len+1); long k=0;
    for(long i=0;i<s->len;i++) if(s->ptr[i] != '_') buf[k++]=s->ptr[i];
    buf[k]=0;
    char *p = buf; while(*p==' '||*p=='\t'||*p=='\n') p++;
    char *endp; double d = strtod(p,&endp);
    if(endp==p){ if(!exception) return Qnil; rb_raise(rb_eArgError,"invalid value for Float(): %s", rb_str_cstr(rb_inspect(v))); }
    while(*endp==' '||*endp=='\t'||*endp=='\n') endp++;
    if(*endp){ if(!exception) return Qnil; rb_raise(rb_eArgError,"invalid value for Float(): %s", rb_str_cstr(rb_inspect(v))); }
    return rb_float_new(d); }
  if(NIL_P(v)){ if(!exception) return Qnil; rb_raise(rb_eTypeError,"can't convert nil into Float"); }
  return rb_float_new(rb_num2dbl(v)); }
static VALUE krn_String(VALUE self, VALUE v, RProc *b){ (void)self;(void)b;
  VALUE s = rb_check_string_type(v);
  if(!NIL_P(s)) return s;
  return rb_obj_as_string(v); }
static VALUE krn_Array(VALUE self, VALUE v, RProc *b){ (void)self;(void)b;
  if(NIL_P(v)) return rb_ary_new();
  VALUE a = rb_check_array_type(v);
  if(!NIL_P(a)) return a;
  if(rb_respond_to(v, ID_CACHED("to_a"))){ VALUE r = rb_funcall(v,ID_CACHED("to_a"),0);
    if(!NIL_P(r)) return r; }
  VALUE r = rb_ary_new(); rb_ary_push(r,v); return r; }
static VALUE krn_Hash(VALUE self, VALUE v, RProc *b){ (void)self;(void)b;
  if(NIL_P(v)) return rb_hash_new();
  if(!FIXNUM_P(v) && BASIC(v)->type==T_ARRAY && RARR(v)->len==0) return rb_hash_new();
  VALUE h = rb_check_convert(v, ID_CACHED("to_hash"));
  if(NIL_P(h)) rb_raise(rb_eTypeError,"can't convert %s into Hash", rb_obj_classname(v));
  return h; }

/* ===================== Module / Class ===================== */
static VALUE mod_name(VALUE self, RProc *b){ (void)b;
  RClass *c = (RClass*)self;
  if(c->is_singleton || c->name < 0){
    int f; VALUE tmp = ivtbl_get(c->basic.iv, ID_CACHED("__tmpname__"), &f);
    if(f) return tmp;
    return Qnil; }
  return rb_str_freeze(rb_class_name(c)); }
static VALUE mod_to_s(VALUE self, RProc *b){ (void)b; return rb_class_name((RClass*)self); }
static VALUE mod_eqq(VALUE self, VALUE o, RProc *b){ (void)b; return rb_is_a(o,(RClass*)self)?Qtrue:Qfalse; }
static int mod_le(RClass *a, RClass *b){ int n; RClass **anc = rb_class_ancestors(a,&n);
  for(int i=0;i<n;i++) if(anc[i]==b) return 1; return 0; }
static VALUE mod_lt(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o)||(BASIC(o)->type != T_CLASS && BASIC(o)->type != T_MODULE)) rb_raise(rb_eTypeError,"compared with non class/module");
  if(self==o) return Qfalse;
  if(mod_le((RClass*)self,(RClass*)o)) return Qtrue;
  if(mod_le((RClass*)o,(RClass*)self)) return Qfalse;
  return Qnil; }
static VALUE mod_le_m(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o)||(BASIC(o)->type != T_CLASS && BASIC(o)->type != T_MODULE)) rb_raise(rb_eTypeError,"compared with non class/module");
  if(self==o) return Qtrue;
  if(mod_le((RClass*)self,(RClass*)o)) return Qtrue;
  if(mod_le((RClass*)o,(RClass*)self)) return Qfalse;
  return Qnil; }
static VALUE mod_gt(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return Qfalse;
  VALUE r = mod_le_m(o,self,b); return NIL_P(r)?Qnil:(RTEST(r)?Qtrue:Qfalse); }
static VALUE mod_ge(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return Qtrue;
  VALUE r = mod_le_m(o,self,b); return NIL_P(r)?Qnil:(RTEST(r)?Qtrue:Qfalse); }
static VALUE mod_cmp(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return LONG2FIX(0);
  if(FIXNUM_P(o)||(BASIC(o)->type != T_CLASS && BASIC(o)->type != T_MODULE)) return Qnil;
  if(mod_le((RClass*)self,(RClass*)o)) return LONG2FIX(-1);
  if(mod_le((RClass*)o,(RClass*)self)) return LONG2FIX(1);
  return Qnil; }
static VALUE mod_ancestors(VALUE self, RProc *b){ (void)b;
  int n; RClass **a = rb_class_ancestors((RClass*)self,&n);
  VALUE r = rb_ary_new();
  for(int i=0;i<n;i++) if(!a[i]->is_singleton || a[i]==(RClass*)self) rb_ary_push(r,(VALUE)a[i]);
  return r; }
static VALUE mod_superclass(VALUE self, RProc *b){ (void)b;
  RClass *s = ((RClass*)self)->super;
  return s? (VALUE)s : Qnil; }
static VALUE mod_include(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  for(int i=argc-1;i>=0;i--){
    if(FIXNUM_P(argv[i]) || BASIC(argv[i])->type != T_MODULE)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Module)", rb_obj_classname(argv[i]));
    RClass *m = (RClass*)argv[i];
    rb_include_module((RClass*)self, m);
    RClass *ow; method_entry *hook = rb_method_lookup(BASIC((VALUE)m)->klass, ID_CACHED("included"), &ow);
    if(hook && ow != rb_cObject && ow != rb_cModule && ow != rb_cKernel && ow != rb_cBasicObject){
      VALUE a1[1] = { self }; rb_invoke_method((VALUE)m,hook,ow,ID_CACHED("included"),1,a1,NULL,0); } }
  return self; }
static VALUE mod_prepend(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  for(int i=argc-1;i>=0;i--){
    if(FIXNUM_P(argv[i]) || BASIC(argv[i])->type != T_MODULE)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Module)", rb_obj_classname(argv[i]));
    RClass *m = (RClass*)argv[i];
    rb_prepend_module((RClass*)self, m);
    RClass *ow; method_entry *hook = rb_method_lookup(BASIC((VALUE)m)->klass, ID_CACHED("prepended"), &ow);
    if(hook && ow != rb_cObject && ow != rb_cModule && ow != rb_cKernel && ow != rb_cBasicObject){
      VALUE a1[1] = { self }; rb_invoke_method((VALUE)m,hook,ow,ID_CACHED("prepended"),1,a1,NULL,0); } }
  return self; }
static VALUE mod_include_p(VALUE self, VALUE m, RProc *b){ (void)b;
  if(self==m) return Qfalse;
  return mod_le((RClass*)self,(RClass*)m)?Qtrue:Qfalse; }
static VALUE mod_included_modules(VALUE self, RProc *b){ (void)b;
  int n; RClass **a = rb_class_ancestors((RClass*)self,&n);
  VALUE r = rb_ary_new();
  for(int i=0;i<n;i++) if(a[i]->is_module) rb_ary_push(r,(VALUE)a[i]);
  return r; }
static void set_attr(RClass *c, ID name, int reader, int writer, int visibility){
  if(reader){ method_entry *me = xcalloc(1,sizeof(method_entry));
    me->type = ME_ATTR_READER; me->attr_id = name; me->visibility = visibility;
    rb_add_method(c, name, me); }
  if(writer){ char buf[256]; snprintf(buf,sizeof buf,"%s=",rb_id2name(name));
    method_entry *me = xcalloc(1,sizeof(method_entry));
    me->type = ME_ATTR_WRITER; me->attr_id = name; me->visibility = visibility;
    rb_add_method(c, rb_intern(buf), me); }
}
static VALUE mod_attr_accessor(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE r = rb_ary_new();
  for(int i=0;i<argc;i++){ ID id = to_mid(argv[i]); set_attr((RClass*)self,id,1,1,VIS_PUBLIC);
    rb_ary_push(r, rb_sym(id)); char bf[256]; snprintf(bf,sizeof bf,"%s=",rb_id2name(id)); rb_ary_push(r, rb_sym(rb_intern(bf))); }
#if RUBY_TARGET >= 31
  return r;
#else
  return Qnil;
#endif
}
static VALUE mod_attr_reader(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE r = rb_ary_new();
  for(int i=0;i<argc;i++){ ID id=to_mid(argv[i]); set_attr((RClass*)self,id,1,0,VIS_PUBLIC); rb_ary_push(r,rb_sym(id)); }
#if RUBY_TARGET >= 31
  return r;
#else
  return Qnil;
#endif
}
static VALUE mod_attr_writer(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE r = rb_ary_new();
  for(int i=0;i<argc;i++){ ID id=to_mid(argv[i]); set_attr((RClass*)self,id,0,1,VIS_PUBLIC);
    char bf[256]; snprintf(bf,sizeof bf,"%s=",rb_id2name(id)); rb_ary_push(r,rb_sym(rb_intern(bf))); }
#if RUBY_TARGET >= 31
  return r;
#else
  return Qnil;
#endif
}
static VALUE mod_attr(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc==2 && (argv[1]==Qtrue||argv[1]==Qfalse)){
    ID id = to_mid(argv[0]); set_attr((RClass*)self,id,1,argv[1]==Qtrue,VIS_PUBLIC); return Qnil; }
  return mod_attr_reader(self,argc,argv,b); }

static VALUE mod_define_method(VALUE self, int argc, VALUE *argv, RProc *b){
  ID mid = to_mid(argv[0]);
  RProc *body = b;
  if(argc>1){
    VALUE v = argv[1];
    if(!FIXNUM_P(v) && BASIC(v)->type==T_PROC) body = (RProc*)v;
    else if(!FIXNUM_P(v) && (BASIC(v)->type==T_METHODOBJ)){
      RMethodObj *m = (RMethodObj*)v;
      method_entry *me = xmalloc(sizeof(method_entry)); *me = *m->me;
      rb_add_method((RClass*)self, mid, me);
      return rb_sym(mid); }
    else rb_raise(rb_eTypeError, "wrong argument type %s (expected Proc/Method)", rb_obj_classname(v));
  }
  if(!body) rb_raise(rb_eArgError, "tried to create Proc object without a block");
  method_entry *me = xcalloc(1,sizeof(method_entry));
  me->type = ME_PROC; me->proc = body;
  RProc *lp = xmalloc(sizeof(RProc)); *lp = *body; lp->is_lambda = 1; me->proc = lp;
  rb_add_method((RClass*)self, mid, me);
  return rb_sym(mid); }
static VALUE mod_alias_method(VALUE self, VALUE nn, VALUE oo, RProc *b){ (void)b;
  ID nid = to_mid(nn), oid = to_mid(oo);
  RClass *ow; method_entry *me = rb_method_lookup((RClass*)self, oid, &ow);
  if(!me) rb_raise(rb_eNameError, "undefined method '%s' for class '%s'", rb_id2name(oid), rb_str_cstr(rb_class_name((RClass*)self)));
  method_entry *m2 = xmalloc(sizeof(method_entry)); *m2 = *me;
  rb_add_method((RClass*)self, nid, m2);
#if RUBY_TARGET >= 31
  return self;
#else
  return self;
#endif
}
static VALUE mod_remove_method(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  for(int i=0;i<argc;i++){ ID id = to_mid(argv[i]);
    RClass *c = (RClass*)self;
    method_entry *found = NULL;
    mtbl *t = c->m; if(t) for(int j=0;j<t->n;j++) if(t->keys[j]==id) found = t->vals[j];
    if(!found) rb_raise(rb_eNameError,"method '%s' not defined in %s", rb_id2name(id), rb_str_cstr(rb_class_name(c)));
    rb_remove_method_entry(c,id); }
  return self; }
static VALUE mod_undef_method(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  for(int i=0;i<argc;i++){ ID id = to_mid(argv[i]);
    RClass *ow; if(!rb_method_lookup((RClass*)self,id,&ow))
      rb_raise(rb_eNameError,"undefined method '%s' for class '%s'", rb_id2name(id), rb_str_cstr(rb_class_name((RClass*)self)));
    method_entry *me = xcalloc(1,sizeof(method_entry)); me->type = ME_UNDEF;
    rb_add_method((RClass*)self,id,me); }
  return self; }
static VALUE mod_method_defined(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  ID id = to_mid(argv[0]); RClass *ow;
  method_entry *me = rb_method_lookup((RClass*)self,id,&ow);
  return (me && me->visibility != VIS_PRIVATE)?Qtrue:Qfalse; }
static VALUE mod_public_method_defined(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)b;
  ID id = to_mid(argv[0]); RClass *ow;
  method_entry *me = rb_method_lookup((RClass*)self,id,&ow);
  return (me && me->visibility==VIS_PUBLIC)?Qtrue:Qfalse; }
static VALUE mod_private_method_defined(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)b;
  ID id = to_mid(argv[0]); RClass *ow;
  method_entry *me = rb_method_lookup((RClass*)self,id,&ow);
  return (me && me->visibility==VIS_PRIVATE)?Qtrue:Qfalse; }
static VALUE mod_protected_method_defined(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)b;
  ID id = to_mid(argv[0]); RClass *ow;
  method_entry *me = rb_method_lookup((RClass*)self,id,&ow);
  return (me && me->visibility==VIS_PROTECTED)?Qtrue:Qfalse; }
static VALUE mod_instance_method(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name); RClass *ow;
  method_entry *me = rb_method_lookup((RClass*)self,id,&ow);
  if(!me) rb_raise(rb_eNameError,"undefined method '%s' for class '%s'", rb_id2name(id), rb_str_cstr(rb_class_name((RClass*)self)));
  return mk_method_obj(Qnil, ow, id, me, 1); }
static VALUE mod_instance_methods_w(VALUE self, int argc, VALUE *argv, int which){
  int inherit = (argc==0) || RTEST(argv[0]);
  VALUE r = rb_ary_new();
  int n; RClass **a = rb_class_ancestors((RClass*)self,&n);
  if(!inherit){ n = 1; }
  for(int i=0;i<n;i++){
    if(!inherit && a[i] != (RClass*)self) continue;
    mtbl *t = a[i]->m; if(!t) continue;
    for(int j=0;j<t->n;j++){
      if(t->vals[j]->type==ME_UNDEF) continue;
      int vis = t->vals[j]->visibility;
      if(which==0 && vis==VIS_PRIVATE) continue;
      if(which==1 && vis != VIS_PUBLIC) continue;
      if(which==2 && vis != VIS_PRIVATE) continue;
      if(which==3 && vis != VIS_PROTECTED) continue;
      VALUE s = rb_sym(t->keys[j]);
      int dup=0; for(long q=0;q<RARR(r)->len;q++) if(RARR(r)->ptr[q]==s){dup=1;break;}
      if(!dup) rb_ary_push(r,s); } }
  return r; }
static VALUE mod_instance_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return mod_instance_methods_w(self,argc,argv,0); }
static VALUE mod_public_instance_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return mod_instance_methods_w(self,argc,argv,1); }
static VALUE mod_private_instance_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return mod_instance_methods_w(self,argc,argv,2); }
static VALUE mod_protected_instance_methods(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return mod_instance_methods_w(self,argc,argv,3); }

static VALUE set_visibility(VALUE self, int argc, VALUE *argv, int vis){
  RClass *c = (RClass*)self;
  if(argc==0){ if(rb_curframe && rb_curframe->prev) rb_curframe->prev->visibility_default = vis; return Qnil; }
  VALUE last = Qnil;
  for(int i=0;i<argc;i++){
    if(!FIXNUM_P(argv[i]) && BASIC(argv[i])->type==T_ARRAY){
      for(long k=0;k<RARR(argv[i])->len;k++){
        ID id = to_mid(RARR(argv[i])->ptr[k]);
        RClass *ow; method_entry *me = rb_method_lookup(c,id,&ow);
        if(!me) rb_raise(rb_eNameError,"undefined method '%s'", rb_id2name(id));
        method_entry *m2 = xmalloc(sizeof(method_entry)); *m2=*me; m2->visibility=vis;
        rb_add_method(c,id,m2); }
      last = argv[i]; continue; }
    ID id = to_mid(argv[i]);
    RClass *ow; method_entry *me = rb_method_lookup(c,id,&ow);
    if(!me) rb_raise(rb_eNameError,"undefined method '%s' for class '%s'", rb_id2name(id), rb_str_cstr(rb_class_name(c)));
    method_entry *m2 = xmalloc(sizeof(method_entry)); *m2=*me; m2->visibility=vis;
    rb_add_method(c,id,m2);
    last = argv[i];
  }
#if RUBY_TARGET >= 31
  return argc==1? last : rb_ary_new_from(argc,argv);
#else
  return self;
#endif
}
static VALUE mod_private(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return set_visibility(self,argc,argv,VIS_PRIVATE); }
static VALUE mod_public(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return set_visibility(self,argc,argv,VIS_PUBLIC); }
static VALUE mod_protected(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b; return set_visibility(self,argc,argv,VIS_PROTECTED); }
static VALUE mod_module_function(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  RClass *c = (RClass*)self;
  if(argc==0){ if(rb_curframe && rb_curframe->prev) rb_curframe->prev->visibility_default = VIS_MODFUNC; return Qnil; }
  for(int i=0;i<argc;i++){ ID id = to_mid(argv[i]);
    RClass *ow; method_entry *me = rb_method_lookup(c,id,&ow);
    if(!me) rb_raise(rb_eNameError,"undefined method '%s'", rb_id2name(id));
    method_entry *m2 = xmalloc(sizeof(method_entry)); *m2=*me; m2->visibility=VIS_PUBLIC;
    rb_add_method(rb_singleton_class(self), id, m2);
    method_entry *m3 = xmalloc(sizeof(method_entry)); *m3=*me; m3->visibility=VIS_PRIVATE;
    rb_add_method(c,id,m3); }
  return self; }
static VALUE mod_private_class_method(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  return set_visibility((VALUE)rb_singleton_class(self),argc,argv,VIS_PRIVATE); }
static VALUE mod_public_class_method(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  return set_visibility((VALUE)rb_singleton_class(self),argc,argv,VIS_PUBLIC); }
static VALUE mod_private_constant(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b; return self; }
static VALUE mod_public_constant(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b; return self; }

static VALUE mod_const_get(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE nv = argv[0];
  VALUE s = (!FIXNUM_P(nv) && BASIC(nv)->type==T_SYMBOL) ? rb_str_new_cstr(rb_id2name(RSYM(nv)->id)) : rb_str_to_str(nv);
  RClass *c = (RClass*)self;
  const char *p = RSTR(s)->ptr;
  char buf[256];
  while(1){
    const char *sep = strstr(p,"::");
    long n = sep? (sep-p) : (long)strlen(p);
    if(n>=255) n=255;
    memcpy(buf,p,n); buf[n]=0;
    if(buf[0] && !(buf[0]>='A'&&buf[0]<='Z')) rb_raise(rb_eNameError,"wrong constant name %s",buf);
    int f; VALUE v = rb_const_get(c, rb_intern(buf), &f);
    if(!f){
      VALUE a1[1] = { rb_sym(rb_intern(buf)) };
      RClass *ow; method_entry *me = rb_method_lookup(BASIC((VALUE)c)->klass, ID_CACHED("const_missing"), &ow);
      if(me) v = rb_invoke_method((VALUE)c, me, ow, ID_CACHED("const_missing"), 1, a1, NULL, 0);
      else rb_raise(rb_eNameError,"uninitialized constant %s",buf);
    }
    if(!sep) return v;
    c = (RClass*)v; p = sep+2;
  } }
static VALUE mod_const_set(VALUE self, VALUE name, VALUE v, RProc *b){ (void)b;
  ID id = to_mid(name);
  const char *nm = rb_id2name(id);
  if(!(nm[0]>='A'&&nm[0]<='Z')) rb_raise(rb_eNameError, "wrong constant name %s", nm);
  rb_const_set((RClass*)self, id, v);
  if(!FIXNUM_P(v) && (BASIC(v)->type==T_CLASS||BASIC(v)->type==T_MODULE)){
    RClass *k = (RClass*)v;
    if(k->name<0){ k->name = id; k->lexical_parent = ((RClass*)self==rb_cObject)?NULL:(RClass*)self; } }
  return v; }
static VALUE mod_const_defined(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  ID id = to_mid(argv[0]);
  int inherit = (argc<2) || RTEST(argv[1]);
  if(inherit){ int f; rb_const_get((RClass*)self,id,&f); if(f) return Qtrue;
    int f2; ivtbl_get(rb_cObject->consts,id,&f2); return (f2 && ((RClass*)self)->is_module)?Qtrue:(f2 && (RClass*)self==rb_cObject?Qtrue:Qfalse); }
  int f; ivtbl_get(((RClass*)self)->consts,id,&f); return f?Qtrue:Qfalse; }
static VALUE mod_constants(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b;
  VALUE r = rb_ary_new();
  ivtbl *t = ((RClass*)self)->consts;
  if(t) for(int i=0;i<t->n;i++) rb_ary_push(r, rb_sym(t->keys[i]));
  return r; }
static VALUE mod_remove_const(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name); VALUE out;
  if(!ivtbl_delete(((RClass*)self)->consts,id,&out)) rb_raise(rb_eNameError,"constant %s not defined",rb_id2name(id));
  return out; }
static VALUE mod_cvar_get(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name); const char *nm = rb_id2name(id);
  if(nm[0]!='@'||nm[1]!='@') rb_raise(rb_eNameError,"'%s' is not allowed as a class variable name",nm);
  int n; RClass **a = rb_class_ancestors((RClass*)self,&n);
  for(int i=0;i<n;i++){ int f; VALUE v = ivtbl_get(a[i]->basic.iv, rb_intern(nm+2), &f); if(f) return v; }
  rb_raise(rb_eNameError,"uninitialized class variable %s in %s",nm,rb_str_cstr(rb_class_name((RClass*)self))); }
static VALUE mod_cvar_set(VALUE self, VALUE name, VALUE v, RProc *b){ (void)b;
  ID id = to_mid(name); const char *nm = rb_id2name(id);
  ivtbl_set(&((RClass*)self)->basic.iv, rb_intern(nm+2), v); return v; }
static VALUE mod_cvar_defined(VALUE self, VALUE name, RProc *b){ (void)b;
  ID id = to_mid(name); const char *nm = rb_id2name(id);
  int n; RClass **a = rb_class_ancestors((RClass*)self,&n);
  for(int i=0;i<n;i++){ int f; ivtbl_get(a[i]->basic.iv, rb_intern(nm+2), &f); if(f) return Qtrue; }
  return Qfalse; }
static VALUE mod_cvars(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b;
  VALUE r = rb_ary_new();
  ivtbl *t = ((RClass*)self)->basic.iv;
  if(t) for(int i=0;i<t->n;i++){ const char *nm = rb_id2name(t->keys[i]);
    if(nm[0]=='_'&&nm[1]=='_') continue;
    char buf[256]; snprintf(buf,sizeof buf,"@@%s",nm); rb_ary_push(r,rb_sym(rb_intern(buf))); }
  return r; }
static VALUE mod_class_eval(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) rb_raise(rb_eArgError,"block not supplied");
  (void)argc;(void)argv;
  RProc p = *b; p.self = self; p.defclass = (RClass*)self;
  cref_t *cr = xcalloc(1,sizeof(cref_t)); cr->klass = (RClass*)self; cr->next = (cref_t*)b->cref_list;
  p.cref_list = (struct rb_cref*)cr;
  VALUE a1[1] = { self };
  return rb_proc_invoke(&p,1,a1,NULL,0); }
static VALUE mod_class_exec(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) rb_raise(rb_eArgError,"block not supplied");
  RProc p = *b; p.self = self; p.defclass = (RClass*)self;
  cref_t *cr = xcalloc(1,sizeof(cref_t)); cr->klass = (RClass*)self; cr->next = (cref_t*)b->cref_list;
  p.cref_list = (struct rb_cref*)cr;
  return rb_proc_invoke(&p,argc,argv,NULL,0); }
static VALUE mod_const_missing(VALUE self, VALUE name, RProc *b){ (void)b;
  rb_raise(rb_eNameError, "uninitialized constant %s", rb_id2name(to_mid(name))); return Qnil; }
static VALUE mod_freeze(VALUE self, RProc *b){ (void)b; BASIC(self)->frozen=1; return self; }
static VALUE mod_hash(VALUE self, RProc *b){ (void)b; return LONG2FIX((long)(self>>3)); }
static VALUE mod_module_eval_noop(VALUE self, int argc, VALUE *argv, RProc *b){ return mod_class_eval(self,argc,argv,b); }
static VALUE mod_instance_variable_get_pub(VALUE self, VALUE n, RProc *b){ return obj_ivar_get(self,n,b); }
#if RUBY_TARGET >= 33
static VALUE mod_set_temporary_name(VALUE self, VALUE n, RProc *b){ (void)b;
  RClass *c = (RClass*)self;
  if(c->name >= 0) rb_raise(rb_eRuntimeError, "can't change permanent name");
  if(NIL_P(n)){ ivtbl_delete(c->basic.iv, ID_CACHED("__tmpname__"), NULL); return self; }
  VALUE s = rb_str_to_str(n);
  if(RSTR(s)->len==0) rb_raise(rb_eArgError,"empty class/module name");
  /* a temporary name may not be one a constant lookup could produce */
  { const char *q = RSTR(s)->ptr; long ln = RSTR(s)->len;
    int constish = (q[0]>='A' && q[0]<='Z');
    for(long i=0;constish && i<ln;i++){
      char ch = q[i];
      if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'||ch==':'))
        constish = 0; }
    if(constish) rb_raise(rb_eArgError,"the name must not be a constant path: %s", q); }
  ivtbl_set(&c->basic.iv, ID_CACHED("__tmpname__"), s);
  return self; }
#endif
static VALUE cls_allocate(VALUE self, RProc *b){ (void)b;
  RClass *c = (RClass*)self;
  if(c->is_singleton) rb_raise(rb_eTypeError,"can't create instance of singleton class");
  return rb_obj_alloc_of(c); }
static VALUE cls_new(VALUE self, int argc, VALUE *argv, RProc *b){
  RClass *c = (RClass*)self;
  VALUE obj;
  RClass *ow; method_entry *alloc = rb_method_lookup(BASIC(self)->klass, ID_CACHED("allocate"), &ow);
  if(alloc && ow != rb_cClass) obj = rb_invoke_method(self, alloc, ow, ID_CACHED("allocate"), 0, NULL, NULL, 0);
  else obj = rb_obj_alloc_of(c);
  rb_call_full(obj, ID_CACHED("initialize"), argc, argv, b, 1, 0);
  return obj; }
static VALUE cls_class_new(VALUE self, int argc, VALUE *argv, RProc *b){
  RClass *super = argc>0? (RClass*)argv[0] : rb_cObject;
  if(argc>0 && (FIXNUM_P(argv[0]) || BASIC(argv[0])->type != T_CLASS))
    rb_raise(rb_eTypeError, "superclass must be a Class");
  extern RClass *rb_class_boot(RClass *);
  RClass *c = rb_class_boot(super);
  c->basic.klass = rb_cClass; c->name = -1;
  rb_singleton_class((VALUE)c);
  ((RClass*)c)->basic.klass->super = rb_singleton_class((VALUE)super);
  { RClass *ow; method_entry *hook = rb_method_lookup(BASIC((VALUE)super)->klass, ID_CACHED("inherited"), &ow);
    if(hook && ow != rb_cObject && ow != rb_cClass && ow != rb_cKernel && ow != rb_cBasicObject){
      VALUE a1[1] = { (VALUE)c }; rb_invoke_method((VALUE)super,hook,ow,ID_CACHED("inherited"),1,a1,NULL,0); } }
  if(b) mod_class_eval((VALUE)c,0,NULL,b);
  (void)self; return (VALUE)c; }
static VALUE mod_module_new(VALUE self, RProc *b){ (void)self;
  extern RClass *rb_class_boot(RClass *);
  RClass *c = rb_class_boot(NULL); c->is_module = 1; c->basic.type = T_MODULE;
  c->basic.klass = rb_cModule; c->name = -1;
  if(b) mod_class_eval((VALUE)c,0,NULL,b);
  return (VALUE)c; }

/* ===================== refinements ===================== */
#if RUBY_TARGET >= 20
static VALUE mod_refine(VALUE self, VALUE target, RProc *b){
  if(!b) rb_raise(rb_eArgError,"no block given");
  if(FIXNUM_P(target) || (BASIC(target)->type != T_CLASS && BASIC(target)->type != T_MODULE))
    rb_raise(rb_eTypeError, "wrong argument type %s (expected Class or Module)", rb_obj_classname(target));
  extern RClass *rb_class_boot(RClass *);
  RClass *ref = rb_class_boot(NULL); ref->is_module=1; ref->basic.type=T_MODULE;
  ref->basic.klass = rb_cModule; ref->name = -1;
  ivtbl_set(&ref->basic.iv, ID_CACHED("__refined_class__"), target);
  extern unsigned long rb_refinement_count;
  rb_refinement_count++;
  VALUE lst = rb_ivar_get(self, ID_CACHED("__refinements__"));
  if(NIL_P(lst)){ lst = rb_ary_new(); ivtbl_set(&BASIC(self)->iv, ID_CACHED("__refinements__"), lst); }
  rb_ary_push(lst, target); rb_ary_push(lst, (VALUE)ref);
  mod_class_eval((VALUE)ref,0,NULL,b);
  return (VALUE)ref; }
static VALUE mod_refined_class(VALUE self, RProc *b){ (void)b;
  return rb_ivar_get(self, ID_CACHED("__refined_class__")); }
static VALUE krn_using(VALUE self, VALUE mod, RProc *b){ (void)b;(void)self;
  if(FIXNUM_P(mod) || BASIC(mod)->type != T_MODULE)
    rb_raise(rb_eTypeError, "wrong argument type %s (expected Module)", rb_obj_classname(mod));
  VALUE lst = rb_ivar_get(mod, ID_CACHED("__refinements__"));
  if(NIL_P(lst)) return self;
  Frame *f = rb_curframe;
  while(f && f->is_block) f = f->home;
  cref_t *cr = f? f->cref : NULL;
  if(!cr) return self;
  cref_t *ncr = xcalloc(1,sizeof(cref_t));
  *ncr = *cr;
  if(!ncr->refinements) ncr->refinements = (RArray*)rb_ary_new();
  else { VALUE cp = rb_ary_dup((VALUE)ncr->refinements); ncr->refinements = (RArray*)cp; }
  for(long i=0;i<RARR(lst)->len;i++) rb_ary_push((VALUE)ncr->refinements, RARR(lst)->ptr[i]);
  *cr = *ncr;
  return self; }
#endif

/* ===================== Method / UnboundMethod ===================== */
static VALUE meth_call(VALUE self, int argc, VALUE *argv, RProc *b){
  RMethodObj *m = (RMethodObj*)self;
  if(m->unbound) rb_raise(rb_eTypeError,"can't call unbound method");
  if(!m->me) return rb_call_full(m->recv, m->mid, argc, argv, b, 1, 0);
  return rb_invoke_method(m->recv, m->me, m->owner, m->mid, argc, argv, b, 0); }
static VALUE meth_to_proc(VALUE self, RProc *b){ (void)b;
  RMethodObj *m = (RMethodObj*)self;
  extern VALUE meth_proc_body(VALUE, int, VALUE*, RProc*);
  RProc *p = rb_cfunc_proc((VALUE(*)(VALUE,int,VALUE*,RProc*))meth_proc_body, self, -1);
  p->is_lambda = 1;
  (void)m; return (VALUE)p; }
VALUE meth_proc_body(VALUE data, int argc, VALUE *argv, RProc *b){
  return meth_call(data, argc, argv, b); }
static VALUE meth_arity(VALUE self, RProc *b){ (void)b;
  RMethodObj *m = (RMethodObj*)self;
  if(!m->me) return LONG2FIX(-1);
  if(m->me->type==ME_CFUNC) return LONG2FIX(m->me->argc==ARGC_ANY? -1 : m->me->argc);
  if(m->me->type==ME_ATTR_READER) return LONG2FIX(0);
  if(m->me->type==ME_ATTR_WRITER) return LONG2FIX(1);
  Node *params = m->me->type==ME_PROC ? m->me->proc->params : m->me->params;
  int req=0,opt=0,rest=0,keyreq=0;
  if(params) for(int i=0;i<params->nitems;i++){
    switch(params->items[i]->num){
      case PARAM_REQ: case PARAM_POST: case PARAM_DESTRUCT: req++; break;
      case PARAM_OPT: opt++; break;
      case PARAM_REST: rest=1; break;
      case PARAM_KEYREQ: keyreq=1; break; } }
  if(keyreq) req++;
  return LONG2FIX((opt||rest)? -(req+1) : req); }
static VALUE meth_name(VALUE self, RProc *b){ (void)b; return rb_sym(((RMethodObj*)self)->mid); }
static VALUE meth_owner(VALUE self, RProc *b){ (void)b; RMethodObj *m=(RMethodObj*)self;
  return m->owner? (VALUE)m->owner : Qnil; }
static VALUE meth_receiver(VALUE self, RProc *b){ (void)b; return ((RMethodObj*)self)->recv; }
static VALUE meth_unbind(VALUE self, RProc *b){ (void)b; RMethodObj *m=(RMethodObj*)self;
  return mk_method_obj(Qnil, m->owner, m->mid, m->me, 1); }
static VALUE meth_bind(VALUE self, VALUE recv, RProc *b){ (void)b; RMethodObj *m=(RMethodObj*)self;
  if(m->owner && !rb_is_a(recv,m->owner)) rb_raise(rb_eTypeError,"bind argument must be an instance of %s", rb_str_cstr(rb_class_name(m->owner)));
  return mk_method_obj(recv, m->owner, m->mid, m->me, 0); }
static VALUE meth_bind_call(VALUE self, int argc, VALUE *argv, RProc *b){
  RMethodObj *m=(RMethodObj*)self;
  if(argc<1) rb_raise(rb_eArgError,"no receiver given");
  return rb_invoke_method(argv[0], m->me, m->owner, m->mid, argc-1, argv+1, b, 0); }
static VALUE meth_inspect(VALUE self, RProc *b){ (void)b;
  RMethodObj *m=(RMethodObj*)self;
  VALUE s = rb_str_new_cstr(m->unbound? "#<UnboundMethod: " : "#<Method: ");
  rb_str_cat_cstr(s, m->owner? rb_str_cstr(rb_class_name(m->owner)) : "?");
  rb_str_cat_cstr(s,"#"); rb_str_cat_cstr(s, rb_id2name(m->mid));
  rb_str_cat_cstr(s,"()>");
  return s; }
static VALUE meth_eq(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o) || BASIC(o)->type != T_METHODOBJ) return Qfalse;
  RMethodObj *a=(RMethodObj*)self,*c=(RMethodObj*)o;
  return (a->recv==c->recv && a->me==c->me)?Qtrue:Qfalse; }

/* ===================== nil / true / false ===================== */
static VALUE nil_to_s(VALUE self, RProc *b){ (void)self;(void)b;
#if RUBY_TARGET >= 34
  static VALUE cached = 0;
  if(!cached){ cached = rb_str_new_cstr(""); rb_str_freeze(cached); }
  return cached;
#else
  return rb_str_new_cstr("");
#endif
}
static VALUE nil_to_a(VALUE self, RProc *b){ (void)self;(void)b; return rb_ary_new(); }
static VALUE nil_to_h(VALUE self, RProc *b){ (void)self;(void)b; return rb_hash_new(); }
static VALUE nil_inspect(VALUE self, RProc *b){ (void)self;(void)b; return rb_str_new_cstr("nil"); }
static VALUE nil_and(VALUE self, VALUE o, RProc *b){ (void)self;(void)o;(void)b; return Qfalse; }
static VALUE nil_or(VALUE self, VALUE o, RProc *b){ (void)self;(void)b; return RTEST(o)?Qtrue:Qfalse; }
static VALUE nil_xor(VALUE self, VALUE o, RProc *b){ (void)self;(void)b; return RTEST(o)?Qtrue:Qfalse; }
static VALUE nil_nil(VALUE self, RProc *b){ (void)self;(void)b; return Qtrue; }
static VALUE true_to_s(VALUE self, RProc *b){ (void)self;(void)b; return rb_str_new_cstr("true"); }
static VALUE false_to_s(VALUE self, RProc *b){ (void)self;(void)b; return rb_str_new_cstr("false"); }
static VALUE true_and(VALUE self, VALUE o, RProc *b){ (void)self;(void)b; return RTEST(o)?Qtrue:Qfalse; }
static VALUE true_or(VALUE self, VALUE o, RProc *b){ (void)self;(void)o;(void)b; return Qtrue; }
static VALUE true_xor(VALUE self, VALUE o, RProc *b){ (void)self;(void)b; return RTEST(o)?Qfalse:Qtrue; }
static VALUE false_and(VALUE self, VALUE o, RProc *b){ (void)self;(void)o;(void)b; return Qfalse; }
static VALUE false_or(VALUE self, VALUE o, RProc *b){ (void)self;(void)b; return RTEST(o)?Qtrue:Qfalse; }
static VALUE false_xor(VALUE self, VALUE o, RProc *b){ (void)self;(void)b; return RTEST(o)?Qtrue:Qfalse; }

/* ===================== Comparable ===================== */
static long cmp_val(VALUE self, VALUE o){
  VALUE c = rb_funcall(self, ID_CACHED("<=>"), 1, o);
  if(NIL_P(c)) rb_raise(rb_eArgError, "comparison of %s with %s failed",
                        rb_obj_classname(self), NIL_P(o)?"nil":rb_str_cstr(rb_inspect(o)));
  return rb_num2long(c);
}
static VALUE cmp_eq(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return Qtrue;
  VALUE c = rb_funcall(self, ID_CACHED("<=>"), 1, o);
  if(NIL_P(c)) return Qfalse;
  return rb_num2long(c)==0? Qtrue:Qfalse; }
static VALUE cmp_lt(VALUE self, VALUE o, RProc *b){ (void)b; return cmp_val(self,o)<0?Qtrue:Qfalse; }
static VALUE cmp_le(VALUE self, VALUE o, RProc *b){ (void)b; return cmp_val(self,o)<=0?Qtrue:Qfalse; }
static VALUE cmp_gt(VALUE self, VALUE o, RProc *b){ (void)b; return cmp_val(self,o)>0?Qtrue:Qfalse; }
static VALUE cmp_ge(VALUE self, VALUE o, RProc *b){ (void)b; return cmp_val(self,o)>=0?Qtrue:Qfalse; }
static VALUE cmp_between(VALUE self, VALUE a, VALUE c, RProc *b){ (void)b;
  return (cmp_val(self,a)>=0 && cmp_val(self,c)<=0)?Qtrue:Qfalse; }
#if RUBY_TARGET >= 27
static VALUE cmp_clamp(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE lo,hi;
  if(argc==1){
    if(FIXNUM_P(argv[0]) || BASIC(argv[0])->type != T_RANGE) rb_raise(rb_eTypeError,"wrong argument type");
    lo = RRNG(argv[0])->beg; hi = RRNG(argv[0])->end;
    if(RRNG(argv[0])->excl && !NIL_P(hi)) rb_raise(rb_eArgError,"cannot clamp with an exclusive range");
  } else if(argc==2){ lo=argv[0]; hi=argv[1];
    if(!NIL_P(lo)&&!NIL_P(hi)&&cmp_val(lo,hi)>0) rb_raise(rb_eArgError,"min argument must be less than or equal to max argument");
  } else rb_raise(rb_eArgError,"wrong number of arguments (given %d, expected 1..2)",argc);
  if(!NIL_P(lo) && cmp_val(self,lo)<0) return lo;
  if(!NIL_P(hi) && cmp_val(self,hi)>0) return hi;
  return self; }
#endif

/* ===================== BasicObject ===================== */
static VALUE bo_initialize(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b;(void)self; return Qnil; }
static VALUE bo_method_missing(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  ID mid = argc>0? to_mid(argv[0]) : 0;
  VALUE exc = rb_exc_new(rb_eNoMethodError,
    rb_sprintf_val("undefined method '%s' for an instance of %s", rb_id2name(mid), rb_obj_classname(self)));
  rb_ivar_set(exc, ID_CACHED("@name"), rb_sym(mid));
  rb_exc_raise(exc); return Qnil; }
static VALUE bo_instance_eval(VALUE self, int argc, VALUE *argv, RProc *b){ return obj_instance_eval(self,argc,argv,b); }

void rb_init_object(void){
  rb_define_method(rb_cBasicObject, "initialize", bo_initialize, ARGC_ANY);
  rb_define_method(rb_cBasicObject, "method_missing", bo_method_missing, ARGC_ANY);
  rb_define_method(rb_cBasicObject, "==", obj_equal, 1);
  rb_define_method(rb_cBasicObject, "equal?", obj_equal, 1);
  rb_define_method(rb_cBasicObject, "!", obj_not, 0);
  rb_define_method(rb_cBasicObject, "!=", obj_not_equal, 1);
  rb_define_method(rb_cBasicObject, "__send__", obj_send, ARGC_ANY);
  rb_define_method(rb_cBasicObject, "__id__", obj_object_id, 0);
  rb_define_method(rb_cBasicObject, "instance_eval", bo_instance_eval, ARGC_ANY);
  rb_define_method(rb_cBasicObject, "instance_exec", obj_instance_exec, ARGC_ANY);

  RClass *K = rb_cKernel;
  rb_define_method(K, "class", obj_class, 0);
  rb_define_method(K, "singleton_class", obj_singleton_class, 0);
  rb_define_method(K, "is_a?", obj_is_a, 1);
  rb_define_method(K, "kind_of?", obj_is_a, 1);
  rb_define_method(K, "instance_of?", obj_instance_of, 1);
  rb_define_method(K, "nil?", obj_nil_p, 0);
  rb_define_method(K, "==", obj_equal, 1);
  rb_define_method(K, "equal?", obj_equal, 1);
  rb_define_method(K, "eql?", obj_equal, 1);
  rb_define_method(K, "===", obj_eqq, 1);
  rb_define_method(K, "=~", obj_match, 1);
  rb_define_method(K, "!~", obj_nmatch, 1);
  rb_define_method(K, "<=>", obj_cmp, 1);
  rb_define_method(K, "hash", obj_hash, 0);
  rb_define_method(K, "object_id", obj_object_id, 0);
  rb_define_method(K, "to_s", obj_to_s, 0);
  rb_define_method(K, "inspect", obj_inspect, 0);
  rb_define_method(K, "freeze", obj_freeze, 0);
  rb_define_method(K, "frozen?", obj_frozen, 0);
  rb_define_method(K, "dup", obj_dup, 0);
  rb_define_method(K, "clone", obj_clone, ARGC_ANY);
  rb_define_method(K, "initialize_copy", obj_init_copy, 1);
  rb_define_method(K, "initialize_clone", obj_init_copy, 1);
  rb_define_method(K, "initialize_dup", obj_init_copy, 1);
  rb_define_method(K, "send", obj_send, ARGC_ANY);
  rb_define_method(K, "__send__", obj_send, ARGC_ANY);
  rb_define_method(K, "public_send", obj_public_send, ARGC_ANY);
  rb_define_method(K, "respond_to?", obj_respond_to, ARGC_ANY);
  rb_define_private_method(K, "respond_to_missing?", obj_respond_to_missing, 2);
  rb_define_method(K, "instance_variable_get", obj_ivar_get, 1);
  rb_define_method(K, "instance_variable_set", obj_ivar_set, 2);
  rb_define_method(K, "instance_variable_defined?", obj_ivar_defined, 1);
  rb_define_method(K, "instance_variables", obj_ivars, 0);
  rb_define_private_method(K, "remove_instance_variable", obj_remove_ivar, 1);
  rb_define_method(K, "extend", obj_extend, ARGC_ANY);
  rb_define_method(K, "methods", obj_methods, ARGC_ANY);
  rb_define_method(K, "public_methods", obj_public_methods, ARGC_ANY);
  rb_define_method(K, "private_methods", obj_private_methods, ARGC_ANY);
  rb_define_method(K, "singleton_methods", obj_singleton_methods, ARGC_ANY);
  rb_define_method(K, "method", obj_method, 1);
  rb_define_method(K, "instance_eval", obj_instance_eval, ARGC_ANY);
  rb_define_method(K, "instance_exec", obj_instance_exec, ARGC_ANY);
  rb_define_method(K, "define_singleton_method", obj_define_singleton_method, ARGC_ANY);
  rb_define_method(K, "display", obj_display, ARGC_ANY);
  rb_define_method(K, "itself", obj_itself, 0);
  rb_define_method(K, "tap", obj_tap, 0);
  rb_define_method(K, "to_enum", krn_to_enum, ARGC_ANY);
  rb_define_method(K, "enum_for", krn_to_enum, ARGC_ANY);
#if RUBY_TARGET >= 27
  rb_define_method(K, "then", obj_then, 0);
  rb_define_method(K, "yield_self", obj_then, 0);
#endif
  rb_define_method(rb_cObject, "initialize", obj_initialize, ARGC_ANY);

  rb_define_global_function("puts", krn_puts, ARGC_ANY);
  rb_define_global_function("print", krn_print, ARGC_ANY);
  rb_define_global_function("p", krn_p, ARGC_ANY);
  rb_define_global_function("pp", krn_pp, ARGC_ANY);
  rb_define_global_function("sprintf", krn_sprintf, ARGC_ANY);
  rb_define_global_function("format", krn_sprintf, ARGC_ANY);
  rb_define_global_function("printf", krn_printf, ARGC_ANY);
  rb_define_global_function("warn", krn_warn, ARGC_ANY);
  rb_define_global_function("raise", krn_raise, ARGC_ANY);
  rb_define_global_function("fail", krn_raise, ARGC_ANY);
  rb_define_global_function("block_given?", krn_block_given, 0);
  rb_define_global_function("iterator?", krn_block_given, 0);
  rb_define_global_function("loop", krn_loop, 0);
  rb_define_global_function("lambda", krn_lambda, 0);
  rb_define_global_function("proc", krn_proc, 0);
  rb_define_global_function("catch", krn_catch, ARGC_ANY);
  rb_define_global_function("throw", krn_throw, ARGC_ANY);
  rb_define_global_function("exit", krn_exit, ARGC_ANY);
  rb_define_global_function("exit!", krn_exit, ARGC_ANY);
  rb_define_global_function("abort", krn_abort, ARGC_ANY);
  rb_define_global_function("require", krn_require, 1);
  rb_define_global_function("require_relative", krn_require_rel, 1);
  rb_define_global_function("load", krn_load, ARGC_ANY);
  rb_define_global_function("Integer", krn_Integer, ARGC_ANY);
  rb_define_global_function("Float", krn_Float, ARGC_ANY);
  rb_define_global_function("String", krn_String, 1);
  rb_define_global_function("Array", krn_Array, 1);
  rb_define_global_function("Hash", krn_Hash, 1);
  (void)krn_freeze_noop;

  RClass *M = rb_cModule;
  rb_define_method(M, "name", mod_name, 0);
  rb_define_method(M, "to_s", mod_to_s, 0);
  rb_define_method(M, "inspect", mod_to_s, 0);
  rb_define_method(M, "===", mod_eqq, 1);
  rb_define_method(M, "<", mod_lt, 1);
  rb_define_method(M, "<=", mod_le_m, 1);
  rb_define_method(M, ">", mod_gt, 1);
  rb_define_method(M, ">=", mod_ge, 1);
  rb_define_method(M, "<=>", mod_cmp, 1);
  rb_define_method(M, "ancestors", mod_ancestors, 0);
  rb_define_method(M, "include", mod_include, ARGC_ANY);
  rb_define_method(M, "prepend", mod_prepend, ARGC_ANY);
  rb_define_method(M, "include?", mod_include_p, 1);
  rb_define_method(M, "included_modules", mod_included_modules, 0);
  rb_define_method(M, "attr_accessor", mod_attr_accessor, ARGC_ANY);
  rb_define_method(M, "attr_reader", mod_attr_reader, ARGC_ANY);
  rb_define_method(M, "attr_writer", mod_attr_writer, ARGC_ANY);
  rb_define_method(M, "attr", mod_attr, ARGC_ANY);
  rb_define_method(M, "define_method", mod_define_method, ARGC_ANY);
  rb_define_method(M, "alias_method", mod_alias_method, 2);
  rb_define_method(M, "remove_method", mod_remove_method, ARGC_ANY);
  rb_define_method(M, "undef_method", mod_undef_method, ARGC_ANY);
  rb_define_method(M, "method_defined?", mod_method_defined, ARGC_ANY);
  rb_define_method(M, "public_method_defined?", mod_public_method_defined, ARGC_ANY);
  rb_define_method(M, "private_method_defined?", mod_private_method_defined, ARGC_ANY);
  rb_define_method(M, "protected_method_defined?", mod_protected_method_defined, ARGC_ANY);
  rb_define_method(M, "instance_method", mod_instance_method, 1);
  rb_define_method(M, "instance_methods", mod_instance_methods, ARGC_ANY);
  rb_define_method(M, "public_instance_methods", mod_public_instance_methods, ARGC_ANY);
  rb_define_method(M, "private_instance_methods", mod_private_instance_methods, ARGC_ANY);
  rb_define_method(M, "protected_instance_methods", mod_protected_instance_methods, ARGC_ANY);
  rb_define_method(M, "private", mod_private, ARGC_ANY);
  rb_define_method(M, "public", mod_public, ARGC_ANY);
  rb_define_method(M, "protected", mod_protected, ARGC_ANY);
  rb_define_method(M, "module_function", mod_module_function, ARGC_ANY);
  rb_define_method(M, "private_class_method", mod_private_class_method, ARGC_ANY);
  rb_define_method(M, "public_class_method", mod_public_class_method, ARGC_ANY);
  rb_define_method(M, "private_constant", mod_private_constant, ARGC_ANY);
  rb_define_method(M, "public_constant", mod_public_constant, ARGC_ANY);
  rb_define_method(M, "const_get", mod_const_get, ARGC_ANY);
  rb_define_method(M, "const_set", mod_const_set, 2);
  rb_define_method(M, "const_defined?", mod_const_defined, ARGC_ANY);
  rb_define_method(M, "constants", mod_constants, ARGC_ANY);
  rb_define_method(M, "remove_const", mod_remove_const, 1);
  rb_define_method(M, "const_missing", mod_const_missing, 1);
  rb_define_method(M, "class_variable_get", mod_cvar_get, 1);
  rb_define_method(M, "class_variable_set", mod_cvar_set, 2);
  rb_define_method(M, "class_variable_defined?", mod_cvar_defined, 1);
  rb_define_method(M, "class_variables", mod_cvars, ARGC_ANY);
  rb_define_method(M, "class_eval", mod_class_eval, ARGC_ANY);
  rb_define_method(M, "module_eval", mod_module_eval_noop, ARGC_ANY);
  rb_define_method(M, "class_exec", mod_class_exec, ARGC_ANY);
  rb_define_method(M, "module_exec", mod_class_exec, ARGC_ANY);
  rb_define_method(M, "freeze", mod_freeze, 0);
  rb_define_method(M, "hash", mod_hash, 0);
  rb_define_method(M, "instance_variable_get", mod_instance_variable_get_pub, 1);
  rb_define_singleton_method((VALUE)rb_cModule, "new", mod_module_new, 0);
#if RUBY_TARGET >= 20
  rb_define_method(M, "refine", mod_refine, 1);
  rb_define_global_function("using", krn_using, 1);
  rb_define_method(M, "using", krn_using, 1);
#endif
#if RUBY_TARGET >= 33
  rb_define_method(M, "set_temporary_name", mod_set_temporary_name, 1);
  rb_define_method(M, "refined_class", mod_refined_class, 0);
#endif

  rb_define_method(rb_cClass, "new", cls_new, ARGC_ANY);
  rb_define_method(rb_cClass, "allocate", cls_allocate, 0);
  rb_define_method(rb_cClass, "superclass", mod_superclass, 0);
  rb_define_singleton_method((VALUE)rb_cClass, "new", cls_class_new, ARGC_ANY);

  rb_define_method(rb_cNilClass, "to_s", nil_to_s, 0);
  rb_define_method(rb_cNilClass, "to_a", nil_to_a, 0);
  rb_define_method(rb_cNilClass, "inspect", nil_inspect, 0);
  rb_define_method(rb_cNilClass, "&", nil_and, 1);
  rb_define_method(rb_cNilClass, "|", nil_or, 1);
  rb_define_method(rb_cNilClass, "^", nil_xor, 1);
  rb_define_method(rb_cNilClass, "nil?", nil_nil, 0);
#if RUBY_TARGET >= 20
  rb_define_method(rb_cNilClass, "to_h", nil_to_h, 0);
#endif
  rb_define_method(rb_cTrueClass, "to_s", true_to_s, 0);
  rb_define_method(rb_cTrueClass, "inspect", true_to_s, 0);
  rb_define_method(rb_cTrueClass, "&", true_and, 1);
  rb_define_method(rb_cTrueClass, "|", true_or, 1);
  rb_define_method(rb_cTrueClass, "^", true_xor, 1);
  rb_define_method(rb_cFalseClass, "to_s", false_to_s, 0);
  rb_define_method(rb_cFalseClass, "inspect", false_to_s, 0);
  rb_define_method(rb_cFalseClass, "&", false_and, 1);
  rb_define_method(rb_cFalseClass, "|", false_or, 1);
  rb_define_method(rb_cFalseClass, "^", false_xor, 1);

  RClass *C = rb_cComparable;
  rb_define_method(C, "==", cmp_eq, 1);
  rb_define_method(C, "<", cmp_lt, 1);
  rb_define_method(C, "<=", cmp_le, 1);
  rb_define_method(C, ">", cmp_gt, 1);
  rb_define_method(C, ">=", cmp_ge, 1);
  rb_define_method(C, "between?", cmp_between, 2);
#if RUBY_TARGET >= 27
  rb_define_method(C, "clamp", cmp_clamp, ARGC_ANY);
#endif

  rb_define_method(rb_cMethod, "call", meth_call, ARGC_ANY);
  rb_define_method(rb_cMethod, "()", meth_call, ARGC_ANY);
  rb_define_method(rb_cMethod, "[]", meth_call, ARGC_ANY);
  rb_define_method(rb_cMethod, "===", meth_call, ARGC_ANY);
  rb_define_method(rb_cMethod, "to_proc", meth_to_proc, 0);
  rb_define_method(rb_cMethod, "arity", meth_arity, 0);
  rb_define_method(rb_cMethod, "name", meth_name, 0);
  rb_define_method(rb_cMethod, "owner", meth_owner, 0);
  rb_define_method(rb_cMethod, "receiver", meth_receiver, 0);
  rb_define_method(rb_cMethod, "unbind", meth_unbind, 0);
  rb_define_method(rb_cMethod, "inspect", meth_inspect, 0);
  rb_define_method(rb_cMethod, "to_s", meth_inspect, 0);
  rb_define_method(rb_cMethod, "==", meth_eq, 1);
  rb_define_method(rb_cUnboundMethod, "bind", meth_bind, 1);
  rb_define_method(rb_cUnboundMethod, "name", meth_name, 0);
  rb_define_method(rb_cUnboundMethod, "owner", meth_owner, 0);
  rb_define_method(rb_cUnboundMethod, "arity", meth_arity, 0);
  rb_define_method(rb_cUnboundMethod, "inspect", meth_inspect, 0);
  rb_define_method(rb_cUnboundMethod, "to_s", meth_inspect, 0);
#if RUBY_TARGET >= 31
  rb_define_method(rb_cUnboundMethod, "bind_call", meth_bind_call, ARGC_ANY);
#endif
}
