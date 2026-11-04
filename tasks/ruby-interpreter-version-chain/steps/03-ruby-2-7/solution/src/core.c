#include "internal.h"

int rb_target_version = RUBY_TARGET;
unsigned long rb_method_serial = 1;
int rb_init_done;
int rb_verbose = 0;
int rb_warn_deprecated_on = 0;
const char *rb_source_file = "-";

void *xmalloc(size_t n){ void *p = malloc(n ? n : 1); if(!p){ fputs("out of memory\n",stderr); exit(1);} return p; }
void *xcalloc(size_t n, size_t m){ void *p = calloc(n?n:1, m?m:1); if(!p){ fputs("out of memory\n",stderr); exit(1);} return p; }
void *xrealloc(void *q, size_t n){ void *p = realloc(q, n?n:1); if(!p){ fputs("out of memory\n",stderr); exit(1);} return p; }
char *xstrdup(const char *s){ size_t n = strlen(s)+1; char *p = xmalloc(n); memcpy(p,s,n); return p; }

/* ===================== ID table ===================== */
static struct { char **names; long *lens; int n, cap; } idtbl;
static struct { int *buckets; int cap; } idhash;

static unsigned long strhash(const char *s, long len){
  unsigned long h = 5381; for(long i=0;i<len;i++) h = h*33 + (unsigned char)s[i]; return h;
}
static void idhash_grow(void){
  int ncap = idhash.cap ? idhash.cap*2 : 1024;
  int *nb = xcalloc(ncap, sizeof(int));
  for(int i=0;i<idtbl.n;i++){
    unsigned long h = strhash(idtbl.names[i], idtbl.lens[i]) & (ncap-1);
    while(nb[h]) h = (h+1)&(ncap-1);
    nb[h] = i+1;
  }
  free(idhash.buckets); idhash.buckets = nb; idhash.cap = ncap;
}
ID rb_intern2(const char *s, long len){
  if(idhash.cap == 0 || idtbl.n*2 >= idhash.cap) idhash_grow();
  unsigned long h = strhash(s,len) & (idhash.cap-1);
  while(idhash.buckets[h]){
    int i = idhash.buckets[h]-1;
    if(idtbl.lens[i]==len && memcmp(idtbl.names[i], s, len)==0) return i;
    h = (h+1)&(idhash.cap-1);
  }
  if(idtbl.n >= idtbl.cap){
    idtbl.cap = idtbl.cap? idtbl.cap*2 : 512;
    idtbl.names = xrealloc(idtbl.names, idtbl.cap*sizeof(char*));
    idtbl.lens  = xrealloc(idtbl.lens, idtbl.cap*sizeof(long));
  }
  char *c = xmalloc(len+1); memcpy(c,s,len); c[len]=0;
  idtbl.names[idtbl.n]=c; idtbl.lens[idtbl.n]=len;
  idhash.buckets[h] = idtbl.n+1;
  return idtbl.n++;
}
ID rb_intern(const char *s){ return rb_intern2(s, (long)strlen(s)); }
/* burn id 0 so ID_CACHED can use it as "unset" */
static void __attribute__((constructor)) rb_burn_id0(void){ rb_intern2("\x01__unused__", 11); }
const char *rb_id2name(ID id){ return (id>=0 && id<idtbl.n) ? idtbl.names[id] : "?"; }
long rb_id_len(ID id){ return (id>=0 && id<idtbl.n) ? idtbl.lens[id] : 1; }

/* ===================== singletons ===================== */
static RBasic nil_obj, true_obj, false_obj, undef_obj;
VALUE Qnil, Qtrue, Qfalse, Qundef;

RClass *rb_cBasicObject, *rb_cObject, *rb_cModule, *rb_cClass, *rb_cString,
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

Frame *rb_curframe;
rb_tag *rb_curtag;
VALUE rb_errinfo;

/* ===================== ivar / const tables ===================== */
ivtbl *ivtbl_new(void){ ivtbl *t = xcalloc(1,sizeof(ivtbl)); return t; }
VALUE ivtbl_get(ivtbl *t, ID k, int *found){
  if(found) *found=0;
  if(!t) return Qnil;
  for(int i=0;i<t->n;i++) if(t->keys[i]==k){ if(found)*found=1; return t->vals[i]; }
  return Qnil;
}
void ivtbl_set(ivtbl **tp, ID k, VALUE v){
  if(!*tp) *tp = ivtbl_new();
  ivtbl *t = *tp;
  for(int i=0;i<t->n;i++) if(t->keys[i]==k){ t->vals[i]=v; return; }
  if(t->n >= t->cap){ t->cap = t->cap? t->cap*2 : 4;
    t->keys = xrealloc(t->keys, t->cap*sizeof(ID));
    t->vals = xrealloc(t->vals, t->cap*sizeof(VALUE)); }
  t->keys[t->n]=k; t->vals[t->n]=v; t->n++;
}
int ivtbl_delete(ivtbl *t, ID k, VALUE *out){
  if(!t) return 0;
  for(int i=0;i<t->n;i++) if(t->keys[i]==k){
    if(out)*out=t->vals[i];
    memmove(&t->keys[i], &t->keys[i+1], (t->n-i-1)*sizeof(ID));
    memmove(&t->vals[i], &t->vals[i+1], (t->n-i-1)*sizeof(VALUE));
    t->n--; return 1; }
  return 0;
}

/* ===================== method table ===================== */
static void mtbl_set(mtbl **tp, ID k, method_entry *v){
  if(!*tp) *tp = xcalloc(1,sizeof(mtbl));
  mtbl *t = *tp;
  for(int i=0;i<t->n;i++) if(t->keys[i]==k){ t->vals[i]=v; return; }
  if(t->n >= t->cap){ t->cap = t->cap? t->cap*2 : 8;
    t->keys = xrealloc(t->keys, t->cap*sizeof(ID));
    t->vals = xrealloc(t->vals, t->cap*sizeof(method_entry*)); }
  t->keys[t->n]=k; t->vals[t->n]=v; t->n++;
}
static method_entry *mtbl_get(mtbl *t, ID k){
  if(!t) return NULL;
  int n = t->n;
  ID *keys = t->keys;
  for(int i=0;i<n;i++) if(keys[i]==k) return t->vals[i];
  return NULL;
}
void rb_remove_method_entry(RClass *c, ID k){
  rb_method_serial++;
  mtbl *t = c->m; if(!t) return;
  for(int i=0;i<t->n;i++) if(t->keys[i]==k){
    memmove(&t->keys[i],&t->keys[i+1],(t->n-i-1)*sizeof(ID));
    memmove(&t->vals[i],&t->vals[i+1],(t->n-i-1)*sizeof(method_entry*));
    t->n--; return; }
}

static unsigned long anc_serial_global = 1;
void rb_add_method(RClass *c, ID mid, method_entry *me){
  if(rb_init_done && (c == rb_cInteger || c == rb_cNumeric || c == rb_cComparable ||
                      c == rb_cObject || c == rb_cKernel || c == rb_cBasicObject ||
                      (c && c->is_module))){
    extern int rb_int_ops_redefined;
    rb_int_ops_redefined = 1;
  }
  if(rb_init_done && rb_verbose > 0 && c->m){
    method_entry *old = NULL;
    for(int i=0;i<c->m->n;i++) if(c->m->keys[i]==mid){ old = c->m->vals[i]; break; }
    if(old && old->type != ME_UNDEF && (old->type==ME_ISEQ || old->type==ME_PROC))
      rb_warning("method redefined; discarding old %s", rb_id2name(mid));
  }
  me->mid = mid; me->owner = c;
  mtbl_set(&c->m, mid, me);
  anc_serial_global++;
  rb_method_serial++;
}

/* ===================== object allocation ===================== */
static void *alloc_obj(size_t size, int type, RClass *klass){
  RBasic *b = xcalloc(1,size);
  b->type = (unsigned char)type; b->klass = klass;
  return b;
}
VALUE rb_obj_alloc_of(RClass *c){
  RObject *o = alloc_obj(sizeof(RObject), T_OBJECT, c);
  return (VALUE)o;
}

/* ===================== class system ===================== */
static RClass *class_alloc(int is_module, RClass *super){
  RClass *c = alloc_obj(sizeof(RClass), is_module ? T_MODULE : T_CLASS,
                        is_module ? rb_cModule : rb_cClass);
  c->super = super; c->is_module = is_module; c->name = -1;
  return c;
}
RClass *rb_class_boot(RClass *super){ return class_alloc(0, super); }

RClass *rb_class_of(VALUE v){
  if(FIXNUM_P(v)) return rb_cInteger;
  RBasic *b = BASIC(v);
  RClass *k = b->klass;
  while(k && (k->is_singleton || k->basic.type == T_ICLASS)) k = k->super;
  return k;
}
static RClass *real_klass(VALUE v){
  if(FIXNUM_P(v)) return rb_cInteger;
  return BASIC(v)->klass;
}

/* linearized ancestors, with cache */
static void anc_push(RClass ***a, int *n, int *cap, RClass *c){
  for(int i=0;i<*n;i++) if((*a)[i]==c) return;
  if(*n >= *cap){ *cap = *cap? *cap*2 : 8; *a = xrealloc(*a, *cap*sizeof(RClass*)); }
  (*a)[(*n)++] = c;
}
static void anc_collect(RClass *c, RClass ***a, int *n, int *cap);
static void anc_module(RClass *m, RClass ***a, int *n, int *cap){
  for(int i=m->nprepends-1;i>=0;i--) anc_module(m->prepends[i], a, n, cap);
  anc_push(a,n,cap,m);
  for(int i=m->nincludes-1;i>=0;i--) anc_module(m->includes[i], a, n, cap);
}
static void anc_collect(RClass *c, RClass ***a, int *n, int *cap){
  while(c){
    for(int i=c->nprepends-1;i>=0;i--) anc_module(c->prepends[i], a, n, cap);
    anc_push(a,n,cap,c);
    for(int i=c->nincludes-1;i>=0;i--) anc_module(c->includes[i], a, n, cap);
    c = c->super;
  }
}
RClass **rb_class_ancestors(RClass *c, int *out_n){
  if(c->anc && c->anc_serial == anc_serial_global){ *out_n = c->nanc; return c->anc; }
  RClass **a = NULL; int n=0, cap=0;
  anc_collect(c, &a, &n, &cap);
  free(c->anc); c->anc = a; c->nanc = n; c->anc_serial = anc_serial_global;
  *out_n = n; return a;
}

int rb_is_a(VALUE obj, RClass *target){
  RClass *k = real_klass(obj);
  int n; RClass **a = rb_class_ancestors(k, &n);
  for(int i=0;i<n;i++) if(a[i]==target) return 1;
  return 0;
}

void rb_include_module(RClass *c, RClass *m){
  rb_method_serial++;
  for(int i=0;i<c->nincludes;i++) if(c->includes[i]==m) return;
  if(c->nincludes >= c->cincludes){ c->cincludes = c->cincludes? c->cincludes*2 : 4;
    c->includes = xrealloc(c->includes, c->cincludes*sizeof(RClass*)); }
  c->includes[c->nincludes++] = m;
  anc_serial_global++;
}
void rb_prepend_module(RClass *c, RClass *m){
  rb_method_serial++;
  for(int i=0;i<c->nprepends;i++) if(c->prepends[i]==m) return;
  if(c->nprepends >= c->cprepends){ c->cprepends = c->cprepends? c->cprepends*2 : 4;
    c->prepends = xrealloc(c->prepends, c->cprepends*sizeof(RClass*)); }
  c->prepends[c->nprepends++] = m;
  anc_serial_global++;
}

RClass *rb_singleton_class(VALUE obj){
  if(FIXNUM_P(obj)) rb_raise(rb_eTypeError, "can't define singleton");
  RBasic *b = BASIC(obj);
  if(b->type==T_SYMBOL) rb_raise(rb_eTypeError, "can't define singleton");
  if(b->klass && b->klass->is_singleton) return b->klass;
  RClass *sc;
  if(b->type==T_CLASS || b->type==T_MODULE){
    RClass *c = (RClass*)obj;
    RClass *ssuper;
    if(c->is_module) ssuper = rb_cModule;
    else if(c->super) ssuper = rb_singleton_class((VALUE)c->super);
    else ssuper = rb_cClass;
    sc = class_alloc(0, ssuper);
  } else {
    sc = class_alloc(0, b->klass);
  }
  sc->is_singleton = 1; sc->attached = obj; sc->basic.klass = rb_cClass;
  b->klass = sc;
  anc_serial_global++;
  rb_method_serial++;
  return sc;
}

method_entry *rb_method_lookup(RClass *c, ID mid, RClass **owner){
  int n; RClass **a = rb_class_ancestors(c, &n);
  for(int i=0;i<n;i++){
    method_entry *me = mtbl_get(a[i]->m, mid);
    if(me){ if(me->type==ME_UNDEF) return NULL; if(owner) *owner = a[i]; return me; }
  }
  return NULL;
}
method_entry *rb_method_lookup_super(RClass *start, RClass *from, ID mid, RClass **owner){
  int n; RClass **a = rb_class_ancestors(start, &n);
  int i = 0;
  for(; i<n; i++) if(a[i]==from) { i++; break; }
  for(; i<n; i++){
    method_entry *me = mtbl_get(a[i]->m, mid);
    if(me){ if(me->type==ME_UNDEF) return NULL; if(owner) *owner = a[i]; return me; }
  }
  return NULL;
}

/* ===================== ivars ===================== */
VALUE rb_ivar_get(VALUE obj, ID id){
  if(FIXNUM_P(obj)) return Qnil;
  int found; VALUE v = ivtbl_get(BASIC(obj)->iv, id, &found);
#if RUBY_TARGET < 30
  if(!found && rb_verbose > 0) rb_warning("instance variable @%s not initialized", rb_id2name(id));
#endif
  return v;
}
int rb_ivar_defined(VALUE obj, ID id){
  if(FIXNUM_P(obj)) return 0;
  int found; ivtbl_get(BASIC(obj)->iv, id, &found); return found;
}
void rb_ivar_set(VALUE obj, ID id, VALUE val){
  if(FIXNUM_P(obj)) rb_raise(rb_eFrozenError, "can't modify frozen Integer: %s", rb_str_cstr(rb_inspect(obj)));
  rb_check_frozen(obj);
  ivtbl_set(&BASIC(obj)->iv, id, val);
}

/* ===================== constants ===================== */
void rb_const_set(RClass *c, ID id, VALUE v){ ivtbl_set(&c->consts, id, v); }
VALUE rb_const_get(RClass *c, ID id, int *found){
  RClass *k = c;
  int n; RClass **a = rb_class_ancestors(k, &n);
  for(int i=0;i<n;i++){ int f; VALUE v = ivtbl_get(a[i]->consts, id, &f); if(f){ if(found)*found=1; return v; } }
  if(c->is_module){ int f; VALUE v = ivtbl_get(rb_cObject->consts, id, &f); if(f){ if(found)*found=1; return v; } }
  if(found)*found=0; return Qnil;
}

/* ===================== frozen ===================== */
int rb_obj_frozen(VALUE v){
  if(FIXNUM_P(v)) return 1;
  int t = BASIC(v)->type;
  if(t==T_SYMBOL || t==T_NIL || t==T_TRUE || t==T_FALSE || t==T_FLOAT) return 1;
  return BASIC(v)->frozen;
}
VALUE rb_obj_freeze(VALUE v){ if(!FIXNUM_P(v)) BASIC(v)->frozen = 1; return v; }
void rb_frozen_error(VALUE obj){
  rb_raise(rb_eFrozenError, "can't modify frozen %s: %s",
           rb_obj_classname(obj), rb_str_cstr(rb_inspect(obj)));
}
void rb_check_frozen(VALUE obj){
  if(FIXNUM_P(obj)) return;
#if RUBY_TARGET >= 34
  if(BASIC(obj)->chilled && BASIC(obj)->type==T_STRING){
    rb_warn_deprecated("literal string will be frozen in the future");
    BASIC(obj)->chilled = 0;
    return;
  }
#endif
  if(rb_obj_frozen(obj)) rb_frozen_error(obj);
}

/* ===================== define helpers ===================== */
static method_entry *me_new(int type){ method_entry *me = xcalloc(1,sizeof(method_entry)); me->type=type; me->visibility=VIS_PUBLIC; return me; }

void rb_define_method(RClass *c, const char *name, VALUE (*fn)(), int argc){
  method_entry *me = me_new(ME_CFUNC); me->cfunc = fn; me->argc = argc;
  rb_add_method(c, rb_intern(name), me);
}
void rb_define_private_method(RClass *c, const char *name, VALUE (*fn)(), int argc){
  method_entry *me = me_new(ME_CFUNC); me->cfunc = fn; me->argc = argc; me->visibility = VIS_PRIVATE;
  rb_add_method(c, rb_intern(name), me);
}
void rb_define_singleton_method(VALUE obj, const char *name, VALUE (*fn)(), int argc){
  rb_define_method(rb_singleton_class(obj), name, fn, argc);
}
void rb_define_global_function(const char *name, VALUE (*fn)(), int argc){
  rb_define_private_method(rb_cKernel, name, fn, argc);
}
void rb_define_alias(RClass *c, const char *newn, const char *oldn){
  RClass *owner; method_entry *me = rb_method_lookup(c, rb_intern(oldn), &owner);
  if(!me) return;
  method_entry *n = xmalloc(sizeof(method_entry)); *n = *me;
  n->mid = rb_intern(newn);
  rb_add_method(c, n->mid, n);
}
void rb_define_const(RClass *c, const char *name, VALUE v){ rb_const_set(c, rb_intern(name), v); }

static void set_class_name(RClass *c, RClass *under, const char *name){
  c->name = rb_intern(name);
  c->lexical_parent = (under==rb_cObject)? NULL : under;
  rb_const_set(under, c->name, (VALUE)c);
}
RClass *rb_define_class_under(RClass *under, const char *name, RClass *super){
  int f; VALUE ex = ivtbl_get(under->consts, rb_intern(name), &f);
  if(f) return (RClass*)ex;
  RClass *c = class_alloc(0, super);
  if(super) rb_singleton_class((VALUE)c);
  set_class_name(c, under, name);
  return c;
}
RClass *rb_define_class(const char *name, RClass *super){ return rb_define_class_under(rb_cObject, name, super); }
RClass *rb_define_module_under(RClass *under, const char *name){
  int f; VALUE ex = ivtbl_get(under->consts, rb_intern(name), &f);
  if(f) return (RClass*)ex;
  RClass *c = class_alloc(1, NULL);
  set_class_name(c, under, name);
  return c;
}
RClass *rb_define_module(const char *name){ return rb_define_module_under(rb_cObject, name); }

VALUE rb_class_name(RClass *c){
  if(c->is_singleton){
    VALUE s = rb_str_new_cstr("#<Class:");
    rb_str_cat_cstr(s, rb_str_cstr(rb_any_to_s(c->attached)));
    rb_str_cat_cstr(s, ">"); return s;
  }
  if(c->name < 0){
    int f; VALUE tmp = ivtbl_get(c->basic.iv, ID_CACHED("__tmpname__"), &f);
    if(f) return tmp;
    char buf[64]; snprintf(buf,sizeof buf, "#<%s:0x%04x>", c->is_module?"Module":"Class", (unsigned)(((uintptr_t)c>>4)&0xffff));
    return rb_str_new_cstr(buf);
  }
  if(c->lexical_parent){
    VALUE s = rb_str_dup(rb_class_name(c->lexical_parent));
    rb_str_cat_cstr(s, "::"); rb_str_cat_cstr(s, rb_id2name(c->name));
    return s;
  }
  return rb_str_new_cstr(rb_id2name(c->name));
}
const char *rb_obj_classname(VALUE v){ return rb_str_cstr(rb_class_name(rb_class_of(v))); }

/* ===================== tags ===================== */
void rb_jump_tag(int state, VALUE val, void *target){
  rb_tag *t = rb_curtag;
  if(!t){
    fprintf(stderr, "%s: unexpected jump\n", rb_source_file);
    exit(1);
  }
  t->state = state; t->retval = val; t->target = target;
  longjmp(t->buf, 1);
}

/* ===================== exceptions ===================== */
VALUE rb_exc_new(RClass *c, VALUE msg){
  VALUE e = rb_obj_alloc_of(c);
  BASIC(e)->type = T_EXCEPTION;
  ivtbl_set(&BASIC(e)->iv, ID_CACHED("@__message__"), msg);
  return e;
}
void rb_exc_raise(VALUE exc){
  rb_errinfo = exc;
  rb_jump_tag(TAG_RAISE, exc, NULL);
}
void rb_raise(RClass *c, const char *fmt, ...){
  char buf[1024]; va_list ap; va_start(ap,fmt);
  vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  rb_exc_raise(rb_exc_new(c, rb_str_new_cstr(buf)));
}
void rb_warn_deprecated(const char *fmt, ...){
  if(!rb_warn_deprecated_on) return;
  char buf[1024]; va_list ap; va_start(ap,fmt);
  vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  fprintf(stderr, "%s: warning: %s\n", rb_source_file, buf);
}
void rb_warning(const char *fmt, ...){
  if(rb_verbose <= 0) return;
  char buf[1024]; va_list ap; va_start(ap,fmt);
  vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  fprintf(stderr, "%s: warning: %s\n", rb_source_file, buf);
}

/* ===================== equality / hashing ===================== */
int rb_equal(VALUE a, VALUE b){
  if(a==b) return 1;
  VALUE r = rb_funcall(a, ID_CACHED("=="), 1, b);
  return RTEST(r);
}
int rb_eql(VALUE a, VALUE b){
  if(a==b) return 1;
  VALUE r = rb_funcall(a, ID_CACHED("eql?"), 1, b);
  return RTEST(r);
}
long rb_hash_code(VALUE v){
  VALUE h = rb_funcall(v, ID_CACHED("hash"), 0);
  if(FIXNUM_P(h)) return FIX2LONG(h);
  return (long)h;
}

/* ===================== respond_to ===================== */
int rb_respond_to(VALUE v, ID mid){
  RClass *owner;
  method_entry *me = rb_method_lookup(real_klass(v), mid, &owner);
  if(me && me->visibility == VIS_PUBLIC) return 1;
  if(me) return 0;
  method_entry *rtm = rb_method_lookup(real_klass(v), ID_CACHED("respond_to_missing?"), &owner);
  if(rtm && owner != rb_cObject && owner != rb_cKernel){
    VALUE args[2] = { rb_sym(mid), Qfalse };
    return RTEST(rb_call(v, ID_CACHED("respond_to_missing?"), 2, args, NULL));
  }
  return 0;
}

VALUE rb_check_convert(VALUE v, ID mid){
  if(!rb_respond_to(v, mid)) return Qnil;
  return rb_funcall(v, mid, 0);
}
