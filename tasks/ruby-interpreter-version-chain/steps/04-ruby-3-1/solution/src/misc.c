#include "internal.h"
#include <unistd.h>
extern VALUE rb_call_full(VALUE, ID, int, VALUE *, RProc *, int, int);
extern VALUE rb_obj_alloc_of(RClass *);
extern void rb_const_set(RClass *, ID, VALUE);
extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);
extern ID to_mid_pub(VALUE);
extern VALUE rb_gv_get(ID); extern void rb_gv_set(ID, VALUE);

/* ===================== Symbol ===================== */
static VALUE sym_to_s(VALUE self, RProc *b){ (void)b; return rb_str_new_cstr(rb_id2name(RSYM(self)->id)); }
static VALUE sym_to_sym(VALUE self, RProc *b){ (void)b; return self; }
#if RUBY_TARGET >= 31
/* Symbol#name is Symbol#to_s except that the string it hands back is frozen. */
static VALUE sym_name(VALUE self, RProc *b){ (void)b;
  VALUE s = rb_str_new_cstr(rb_id2name(RSYM(self)->id));
  BASIC(s)->frozen = 1; return s; }
#endif
static VALUE sym_inspect(VALUE self, RProc *b){ (void)b;
  const char *n = rb_id2name(RSYM(self)->id);
  int simple = 1;
  if(!n[0]) simple = 0;
  else if(!((n[0]>='a'&&n[0]<='z')||(n[0]>='A'&&n[0]<='Z')||n[0]=='_')){
    static const char *ops[] = {"+","-","*","/","%","**","==","!=","<",">","<=",">=","<=>","===",
      "[]","[]=","<<",">>","!","~","&","|","^","=~","!~","+@","-@","call","`",NULL};
    simple = 0;
    for(int i=0;ops[i];i++) if(strcmp(n,ops[i])==0){ simple=1; break; }
  } else {
    for(const char *p=n;*p;p++){
      if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_') continue;
      if((*p=='?'||*p=='!'||*p=='=') && p[1]==0) continue;
      simple = 0; break; }
  }
  VALUE s = rb_str_new_cstr(":");
  if(simple) rb_str_cat_cstr(s,n);
  else { VALUE q = rb_inspect(rb_str_new_cstr(n)); rb_str_cat(s,RSTR(q)->ptr,RSTR(q)->len); }
  return s; }
static VALUE sym_to_proc_body(VALUE data, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc<1) rb_raise(rb_eArgError,"no receiver given");
  return rb_call_full(argv[0], RSYM(data)->id, argc-1, argv+1, NULL, 0, 0); }
static VALUE sym_to_proc(VALUE self, RProc *b){ (void)b;
  RProc *p = rb_cfunc_proc(sym_to_proc_body, self, -2);
#if RUBY_TARGET >= 31
  p->is_lambda = 1;
#else
  p->is_lambda = 0;
#endif
  return (VALUE)p; }
static VALUE sym_length(VALUE self, RProc *b){ (void)b;
  VALUE s = rb_str_new_cstr(rb_id2name(RSYM(self)->id)); return rb_funcall(s,ID_CACHED("length"),0); }
static VALUE sym_cmp(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o)||BASIC(o)->type != T_SYMBOL) return Qnil;
  int r = strcmp(rb_id2name(RSYM(self)->id), rb_id2name(RSYM(o)->id));
  return LONG2FIX(r<0?-1:(r>0?1:0)); }
static VALUE sym_eq(VALUE self, VALUE o, RProc *b){ (void)b; return self==o?Qtrue:Qfalse; }
static VALUE sym_hash(VALUE self, RProc *b){ (void)b; return LONG2FIX((long)RSYM(self)->id*2654435761u); }
static VALUE sym_succ(VALUE self, RProc *b){ (void)b;
  VALUE s = rb_funcall(rb_str_new_cstr(rb_id2name(RSYM(self)->id)), ID_CACHED("succ"), 0);
  return rb_sym_from_str(s); }
static VALUE sym_upcase(VALUE self, RProc *b){ (void)b;
  return rb_sym_from_str(rb_funcall(rb_str_new_cstr(rb_id2name(RSYM(self)->id)),ID_CACHED("upcase"),0)); }
static VALUE sym_downcase(VALUE self, RProc *b){ (void)b;
  return rb_sym_from_str(rb_funcall(rb_str_new_cstr(rb_id2name(RSYM(self)->id)),ID_CACHED("downcase"),0)); }
static VALUE sym_capitalize(VALUE self, RProc *b){ (void)b;
  return rb_sym_from_str(rb_funcall(rb_str_new_cstr(rb_id2name(RSYM(self)->id)),ID_CACHED("capitalize"),0)); }
static VALUE sym_empty(VALUE self, RProc *b){ (void)b; return rb_id_len(RSYM(self)->id)==0?Qtrue:Qfalse; }
static VALUE sym_aref(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE s = rb_str_new_cstr(rb_id2name(RSYM(self)->id));
  return rb_call(s, ID_CACHED("[]"), argc, argv, NULL); }
static VALUE sym_start_with(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE s = rb_str_new_cstr(rb_id2name(RSYM(self)->id));
  return rb_call(s, ID_CACHED("start_with?"), argc, argv, NULL); }
static VALUE sym_end_with(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE s = rb_str_new_cstr(rb_id2name(RSYM(self)->id));
  return rb_call(s, ID_CACHED("end_with?"), argc, argv, NULL); }
static VALUE sym_all_syms(VALUE self, RProc *b){ (void)self;(void)b; return rb_ary_new(); }

/* ===================== Proc ===================== */
static VALUE proc_call(VALUE self, int argc, VALUE *argv, RProc *b){
  return rb_proc_invoke((RProc*)self, argc, argv, b, RPRC(self)->is_lambda); }
static VALUE proc_lambda_p(VALUE self, RProc *b){ (void)b; return RPRC(self)->is_lambda?Qtrue:Qfalse; }
static int proc_param_counts(RProc *p, int *req, int *opt, int *rest, int *keyreq, int *key, int *keyrest){
  *req=*opt=*rest=*keyreq=*key=*keyrest=0;
  Node *params = p->params;
  if(!params) return 0;
  for(int i=0;i<params->nitems;i++){
    switch(params->items[i]->num){
      case PARAM_REQ: case PARAM_POST: case PARAM_DESTRUCT: (*req)++; break;
      case PARAM_OPT: (*opt)++; break;
      case PARAM_REST: *rest=1; break;
      case PARAM_KEYREQ: (*keyreq)++; break;
      case PARAM_KEY: (*key)++; break;
      case PARAM_KEYREST: *keyrest=1; break; } }
  return 1; }
static VALUE proc_arity(VALUE self, RProc *b){ (void)b;
  RProc *p = RPRC(self);
  if(p->cfunc) return LONG2FIX(p->arity_override==-2? -2 : -1);
  int req,opt,rest,keyreq,key,keyrest;
  proc_param_counts(p,&req,&opt,&rest,&keyreq,&key,&keyrest);
  if(!p->params){
    int nnum = (int)(intptr_t)p->refined_from;
    if(nnum>0) return LONG2FIX(p->is_lambda? nnum : nnum);
    return LONG2FIX(p->is_lambda? 0 : 0); }
  if(keyreq) req++;
  if(rest || opt || (!p->is_lambda && 0)) return LONG2FIX(-(req+1));
  return LONG2FIX(req); }
static VALUE proc_parameters(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  RProc *p = RPRC(self);
  /* `lambda:` says how to *report* the parameters, whatever the Proc is. */
  int as_lambda = p->is_lambda;
#if RUBY_TARGET >= 33
  if(argc==1 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_HASH){
    VALUE v = rb_hash_lookup(argv[0], rb_sym(ID_CACHED("lambda")), Qundef);
    if(v != Qundef) as_lambda = RTEST(v);
    else if(RHSH(argv[0])->n) rb_raise(rb_eArgError, "unknown keyword");
    argc = 0; }
#endif
  if(argc) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0)", argc);
  VALUE r = rb_ary_new();
  if(!p->params) return r;
  for(int i=0;i<p->params->nitems;i++){
    Node *e = p->params->items[i];
    const char *kind = "req";
    switch(e->num){
      case PARAM_REQ: case PARAM_POST: case PARAM_DESTRUCT: kind = as_lambda? "req":"opt"; break;
      case PARAM_OPT: kind="opt"; break;
      case PARAM_REST: kind="rest"; break;
      case PARAM_KEY: kind="key"; break;
      case PARAM_KEYREQ: kind="keyreq"; break;
      case PARAM_KEYREST: kind="keyrest"; break;
      case PARAM_BLOCK: kind="block"; break;
      default: continue; }
    VALUE pair = rb_ary_new(); rb_ary_push(pair, rb_sym(rb_intern(kind)));
    if(e->id>=0) rb_ary_push(pair, rb_sym(e->id));
    rb_ary_push(r,pair); }
  return r; }
static VALUE proc_to_proc(VALUE self, RProc *b){ (void)b; return self; }
static VALUE proc_curry_body(VALUE data, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE pr = RARR(data)->ptr[0];
  VALUE got = RARR(data)->ptr[1];
  long want = FIX2LONG(RARR(data)->ptr[2]);
  VALUE ng = rb_ary_dup(got);
  for(int i=0;i<argc;i++) rb_ary_push(ng,argv[i]);
  if(RARR(ng)->len >= want) return rb_proc_invoke((RProc*)pr, (int)RARR(ng)->len, RARR(ng)->ptr, NULL, 1);
  VALUE nd2 = rb_ary_new(); rb_ary_push(nd2,pr); rb_ary_push(nd2,ng); rb_ary_push(nd2,LONG2FIX(want));
  RProc *np = rb_cfunc_proc(proc_curry_body, nd2, -1); np->is_lambda=1;
  return (VALUE)np; }
static VALUE proc_curry(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  long want;
  if(argc>0) want = rb_num2long(argv[0]);
  else { VALUE a = proc_arity(self,NULL); want = FIX2LONG(a); if(want<0) want = -want-1; }
  VALUE d = rb_ary_new(); rb_ary_push(d,self); rb_ary_push(d,rb_ary_new()); rb_ary_push(d,LONG2FIX(want));
  RProc *np = rb_cfunc_proc(proc_curry_body, d, -1); np->is_lambda=1;
  return (VALUE)np; }
#if RUBY_TARGET >= 27
static VALUE proc_compose_body(VALUE data, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE f = RARR(data)->ptr[0], g = RARR(data)->ptr[1];
  VALUE r = rb_call(f, ID_CACHED("call"), argc, argv, NULL);
  return rb_funcall(g, ID_CACHED("call"), 1, r); }
static VALUE proc_rshift(VALUE self, VALUE o, RProc *b){ (void)b;
  if(!rb_respond_to(o, ID_CACHED("call"))) rb_raise(rb_eTypeError,"callable object is expected");
  VALUE d = rb_ary_new(); rb_ary_push(d,self); rb_ary_push(d,o);
  RProc *p = rb_cfunc_proc(proc_compose_body,d,-1); p->is_lambda=1; return (VALUE)p; }
static VALUE proc_lshift(VALUE self, VALUE o, RProc *b){ (void)b;
  if(!rb_respond_to(o, ID_CACHED("call"))) rb_raise(rb_eTypeError,"callable object is expected");
  VALUE d = rb_ary_new(); rb_ary_push(d,o); rb_ary_push(d,self);
  RProc *p = rb_cfunc_proc(proc_compose_body,d,-1); p->is_lambda=1; return (VALUE)p; }
#endif
static VALUE proc_inspect(VALUE self, RProc *b){ (void)b;
  char buf[128]; snprintf(buf,sizeof buf,"#<Proc:0x%016lx%s>",(unsigned long)self, RPRC(self)->is_lambda?" (lambda)":"");
  return rb_str_new_cstr(buf); }
static VALUE proc_new(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)argc;(void)argv;
  if(!b) rb_raise(rb_eArgError,"tried to create Proc object without a block");
  RProc *p = xmalloc(sizeof(RProc)); *p = *b; p->is_lambda = 0; return (VALUE)p; }

/* ===================== Range ===================== */
static VALUE rng_begin(VALUE self, RProc *b){ (void)b; return RRNG(self)->beg; }
static VALUE rng_end(VALUE self, RProc *b){ (void)b; return RRNG(self)->end; }
static VALUE rng_excl(VALUE self, RProc *b){ (void)b; return RRNG(self)->excl?Qtrue:Qfalse; }
static VALUE rng_new(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  if(argc<2) rb_raise(rb_eArgError,"wrong number of arguments");
  return rb_range_new(argv[0],argv[1], argc>2 && RTEST(argv[2])); }
static VALUE rng_each(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each"), 0, NULL);
  RRange *r = RRNG(self);
  if(FIXNUM_P(r->beg)){
    if(NIL_P(r->end)){ for(long i=FIX2LONG(r->beg);;i++) rb_yield1(b,LONG2FIX(i)); }
    long e = rb_num2long(r->end);
    for(long i=FIX2LONG(r->beg); r->excl? i<e : i<=e; i++) rb_yield1(b,LONG2FIX(i));
    return self; }
  if(NIL_P(r->beg)) rb_raise(rb_eTypeError, "can't iterate from NilClass");
  if(!rb_respond_to(r->beg, ID_CACHED("succ")))
    rb_raise(rb_eTypeError, "can't iterate from %s", rb_obj_classname(r->beg));
  VALUE cur = r->beg;
  while(1){
    if(!NIL_P(r->end)){
      VALUE c = rb_funcall(cur, ID_CACHED("<=>"), 1, r->end);
      if(NIL_P(c)) break;
      long cv = rb_num2long(c);
      if(r->excl ? cv>=0 : cv>0) break; }
    rb_yield1(b,cur);
    if(!NIL_P(r->end) && !r->excl){
      VALUE c = rb_funcall(cur, ID_CACHED("<=>"), 1, r->end);
      if(rb_num2long(c)==0) break; }
    cur = rb_funcall(cur, ID_CACHED("succ"), 0); }
  return self; }
static VALUE rng_to_a(VALUE self, RProc *b){ (void)b;
  RRange *r = RRNG(self);
  if(NIL_P(r->end)) rb_raise(rb_eRangeError,"cannot convert endless range to an array");
  VALUE a = rb_ary_new();
  if(FIXNUM_P(r->beg) && FIXNUM_P(r->end)){
    long e = FIX2LONG(r->end);
    for(long i=FIX2LONG(r->beg); r->excl? i<e : i<=e; i++) rb_ary_push(a,LONG2FIX(i));
    return a; }
  VALUE cur = r->beg;
  while(1){
    VALUE c = rb_funcall(cur, ID_CACHED("<=>"), 1, r->end);
    if(NIL_P(c)) break;
    long cv = rb_num2long(c);
    if(r->excl? cv>=0 : cv>0) break;
    rb_ary_push(a,cur);
    if(!r->excl && cv==0) break;
    cur = rb_funcall(cur, ID_CACHED("succ"), 0); }
  return a; }
static int rng_covers(VALUE self, VALUE v){
  RRange *r = RRNG(self);
  if(!NIL_P(r->beg)){ VALUE c = rb_funcall(r->beg, ID_CACHED("<=>"), 1, v);
    if(NIL_P(c) || rb_num2long(c)>0) return 0; }
  if(!NIL_P(r->end)){ VALUE c = rb_funcall(v, ID_CACHED("<=>"), 1, r->end);
    if(NIL_P(c)) return 0;
    long cv = rb_num2long(c);
    if(r->excl? cv>=0 : cv>0) return 0; }
  return 1; }
static VALUE rng_include(VALUE self, VALUE v, RProc *b){ (void)b; return rng_covers(self,v)?Qtrue:Qfalse; }
static VALUE rng_eqq(VALUE self, VALUE v, RProc *b){ (void)b; return rng_covers(self,v)?Qtrue:Qfalse; }
static VALUE rng_size(VALUE self, RProc *b){ (void)b;
  RRange *r = RRNG(self);
  if(!FIXNUM_P(r->beg) && !NIL_P(r->beg)) {
    if(BASIC(r->beg)->type != T_FLOAT) return Qnil; }
  if(NIL_P(r->end)) return rb_float_new(1.0/0.0);
  if(!FIXNUM_P(r->end) && BASIC(r->end)->type != T_FLOAT) return Qnil;
  double b0 = rb_num2dbl(r->beg), e0 = rb_num2dbl(r->end);
  double n = e0 - b0;
  if(n < 0) return LONG2FIX(0);
  long cnt = (long)(r->excl ? (n>0? (long)(n - (n==(long)n?1:0)) + 1 : 0) : (long)n + 1);
  if(r->excl && n==(double)(long)n) cnt = (long)n;
  else if(r->excl) cnt = (long)n + 1;
  if(cnt<0) cnt=0;
  return LONG2FIX(cnt); }
static VALUE rng_inspect(VALUE self, RProc *b){ (void)b;
  RRange *r = RRNG(self);
  VALUE s = rb_str_new("",0);
  if(!NIL_P(r->beg)){ VALUE t = rb_inspect(r->beg); rb_str_cat(s,RSTR(t)->ptr,RSTR(t)->len); }
  rb_str_cat_cstr(s, r->excl? "..." : "..");
  if(!NIL_P(r->end)){ VALUE t = rb_inspect(r->end); rb_str_cat(s,RSTR(t)->ptr,RSTR(t)->len); }
  return s; }
static VALUE rng_to_s(VALUE self, RProc *b){ (void)b;
  RRange *r = RRNG(self);
  VALUE s = rb_str_new("",0);
  if(!NIL_P(r->beg)){ VALUE t = rb_obj_as_string(r->beg); rb_str_cat(s,RSTR(t)->ptr,RSTR(t)->len); }
  rb_str_cat_cstr(s, r->excl? "..." : "..");
  if(!NIL_P(r->end)){ VALUE t = rb_obj_as_string(r->end); rb_str_cat(s,RSTR(t)->ptr,RSTR(t)->len); }
  return s; }
static VALUE rng_eq(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o)||BASIC(o)->type != T_RANGE) return Qfalse;
  RRange *a=RRNG(self),*c=RRNG(o);
  return (rb_equal(a->beg,c->beg) && rb_equal(a->end,c->end) && a->excl==c->excl)?Qtrue:Qfalse; }
static VALUE rng_hash(VALUE self, RProc *b){ (void)b;
  RRange *r=RRNG(self);
  long h = rb_hash_code(r->beg)*31 + rb_hash_code(r->end)*7 + r->excl;
  return LONG2FIX(h); }
static VALUE rng_step(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("step"), argc, argv);
  RRange *r = RRNG(self);
  VALUE stepv = argc>0? argv[0] : LONG2FIX(1);
  if(FIXNUM_P(r->beg) && (NIL_P(r->end)||FIXNUM_P(r->end)) && FIXNUM_P(stepv)){
    long st = FIX2LONG(stepv);
    if(st<=0) rb_raise(rb_eArgError,"step can't be negative");
    if(NIL_P(r->end)){ for(long i=FIX2LONG(r->beg);;i+=st) rb_yield1(b,LONG2FIX(i)); }
    long e = FIX2LONG(r->end);
    for(long i=FIX2LONG(r->beg); r->excl? i<e : i<=e; i+=st) rb_yield1(b,LONG2FIX(i));
    return self; }
#if RUBY_TARGET >= 34
  { VALUE cur = r->beg;
    while(1){
      if(!NIL_P(r->end)){ VALUE c = rb_funcall(cur,ID_CACHED("<=>"),1,r->end);
        if(NIL_P(c)) break; long cv = rb_num2long(c);
        if(r->excl? cv>=0 : cv>0) break; }
      rb_yield1(b,cur);
      cur = rb_funcall(cur, ID_CACHED("+"), 1, stepv); }
    return self; }
#else
  { long st = rb_num2long(stepv); long i=0; VALUE cur = r->beg;
    while(1){
      if(!NIL_P(r->end)){ VALUE c = rb_funcall(cur,ID_CACHED("<=>"),1,r->end);
        if(NIL_P(c)) break; long cv = rb_num2long(c);
        if(r->excl? cv>=0 : cv>0) break; }
      if(i % st == 0) rb_yield1(b,cur);
      i++;
      cur = rb_funcall(cur, ID_CACHED("succ"), 0); }
    return self; }
#endif
}
static VALUE rng_first(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  RRange *r = RRNG(self);
  if(argc==0){ if(NIL_P(r->beg)) rb_raise(rb_eRangeError,"cannot get the first element of beginless range"); return r->beg; }
  VALUE a = rb_ary_new(); long n = rb_num2long(argv[0]);
  if(FIXNUM_P(r->beg)){ long i = FIX2LONG(r->beg);
    for(long k=0;k<n;k++){ if(!NIL_P(r->end)){ long e=rb_num2long(r->end); if(r->excl? i>=e : i>e) break; }
      rb_ary_push(a,LONG2FIX(i)); i++; }
    return a; }
  return rb_call(self, ID_CACHED("take"), 1, argv, NULL); }
static VALUE rng_last(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  RRange *r = RRNG(self);
  if(argc==0) return r->end;
  VALUE a = rng_to_a(self,NULL);
  long n = rb_num2long(argv[0]);
  VALUE res = rb_ary_new();
  for(long i = RARR(a)->len - n; i<RARR(a)->len; i++) if(i>=0) rb_ary_push(res,RARR(a)->ptr[i]);
  return res; }
static VALUE rng_min(VALUE self, int argc, VALUE *argv, RProc *b){
  RRange *r = RRNG(self);
  if(argc==0 && !b){ if(NIL_P(r->end)) return r->beg;
    VALUE c = rb_funcall(r->beg,ID_CACHED("<=>"),1,r->end);
    if(NIL_P(c) || rb_num2long(c)>0) return Qnil;
    if(r->excl && rb_num2long(c)==0) return Qnil;
    return r->beg; }
  return rb_call(rng_to_a(self,NULL), ID_CACHED("min"), argc, argv, b); }
static VALUE rng_max(VALUE self, int argc, VALUE *argv, RProc *b){
  RRange *r = RRNG(self);
  if(argc==0 && !b && FIXNUM_P(r->end)){
    VALUE c = rb_funcall(r->beg,ID_CACHED("<=>"),1,r->end);
    if(NIL_P(c) || rb_num2long(c)>0) return Qnil;
    if(r->excl){ if(rb_num2long(c)==0) return Qnil; return LONG2FIX(FIX2LONG(r->end)-1); }
    return r->end; }
  if(argc==0 && !b && !r->excl) return r->end;
  return rb_call(rng_to_a(self,NULL), ID_CACHED("max"), argc, argv, b); }
static VALUE rng_sum(VALUE self, int argc, VALUE *argv, RProc *b){
  RRange *r = RRNG(self);
  if(!b && FIXNUM_P(r->beg) && FIXNUM_P(r->end)){
    long lo = FIX2LONG(r->beg), hi = FIX2LONG(r->end);
    if(r->excl) hi--;
    if(hi < lo) return argc>0? argv[0] : LONG2FIX(0);
    long long s = ((long long)(lo+hi))*(hi-lo+1)/2;
    if(argc>0) return rb_funcall(argv[0], ID_CACHED("+"), 1, rb_int_new(s));
    return rb_int_new(s); }
  return rb_call(rng_to_a(self,NULL), ID_CACHED("sum"), argc, argv, b); }
#if RUBY_TARGET >= 33
static VALUE rng_overlap(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o)||BASIC(o)->type != T_RANGE) rb_raise(rb_eTypeError,"wrong argument type");
  RRange *a=RRNG(self),*c=RRNG(o);
  if(!NIL_P(a->beg) && !NIL_P(c->end)){ VALUE k = rb_funcall(a->beg,ID_CACHED("<=>"),1,c->end);
    if(NIL_P(k)) return Qfalse; long kv = rb_num2long(k);
    if(c->excl? kv>=0 : kv>0) return Qfalse; }
  if(!NIL_P(c->beg) && !NIL_P(a->end)){ VALUE k = rb_funcall(c->beg,ID_CACHED("<=>"),1,a->end);
    if(NIL_P(k)) return Qfalse; long kv = rb_num2long(k);
    if(a->excl? kv>=0 : kv>0) return Qfalse; }
  return Qtrue; }
#endif

/* ===================== Exception ===================== */
static VALUE exc_initialize(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc>0) rb_ivar_set(self, ID_CACHED("@__message__"), argv[0]);
  return self; }
static VALUE exc_message(VALUE self, RProc *b){ (void)b;
  VALUE m = rb_ivar_get(self, ID_CACHED("@__message__"));
  if(NIL_P(m)) return rb_class_name(rb_class_of(self));
  return rb_obj_as_string(m); }
static VALUE exc_to_s(VALUE self, RProc *b){ return exc_message(self,b); }
static VALUE exc_inspect(VALUE self, RProc *b){ (void)b;
  VALUE m = rb_funcall(self, ID_CACHED("message"), 0);
  VALUE cn = rb_class_name(rb_class_of(self));
  if(RSTR(m)->len==0) return cn;
#if RUBY_TARGET >= 34
  VALUE s = rb_str_new_cstr("#<");
  rb_str_cat(s,RSTR(cn)->ptr,RSTR(cn)->len); rb_str_cat_cstr(s,": ");
  int needq = 0;
  for(long i=0;i<RSTR(m)->len;i++){ char c = RSTR(m)->ptr[i];
    if(c=='\n'||c=='"'||c=='\\'||(unsigned char)c<0x20){ needq=1; break; } }
  if(needq){ VALUE q = rb_inspect(m); rb_str_cat(s,RSTR(q)->ptr,RSTR(q)->len); }
  else rb_str_cat(s,RSTR(m)->ptr,RSTR(m)->len);
  rb_str_cat_cstr(s,">");
  return s;
#else
  VALUE s = rb_str_new_cstr("#<");
  rb_str_cat(s,RSTR(cn)->ptr,RSTR(cn)->len); rb_str_cat_cstr(s,": ");
  rb_str_cat(s,RSTR(m)->ptr,RSTR(m)->len); rb_str_cat_cstr(s,">");
  return s;
#endif
}
static VALUE exc_full_message(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;
  return exc_message(self,b); }
#if RUBY_TARGET >= 33
/* "<message> (<class>)" -- the one-line form the default handler prints. */
static VALUE exc_detailed_message(VALUE self, int argc, VALUE *argv, RProc *b){ (void)argc;(void)argv;(void)b;
  VALUE m = rb_obj_as_string(rb_funcall(self, ID_CACHED("message"), 0));
  VALUE s = rb_str_new(RSTR(m)->ptr, RSTR(m)->len);
  rb_str_cat_cstr(s, " (");
  rb_str_cat_cstr(s, rb_obj_classname(self));
  rb_str_cat_cstr(s, ")");
  return s; }
#endif
static VALUE exc_backtrace(VALUE self, RProc *b){ (void)b;
  VALUE bt = rb_ivar_get(self, ID_CACHED("@__backtrace__"));
  return bt; }
static VALUE exc_set_backtrace(VALUE self, VALUE v, RProc *b){ (void)b;
  ivtbl_set(&BASIC(self)->iv, ID_CACHED("@__backtrace__"), v); return v; }
static VALUE exc_exception(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc==0) return self;
  if(argv[0]==self) return self;
  VALUE n = rb_obj_dup(self);
  rb_ivar_set(n, ID_CACHED("@__message__"), argv[0]);
  return n; }
static VALUE exc_class_exception(VALUE self, int argc, VALUE *argv, RProc *b){
  return rb_call(self, ID_CACHED("new"), argc, argv, b); }
static VALUE exc_eq(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return Qtrue;
  if(FIXNUM_P(o)) return Qfalse;
  if(rb_class_of(self) != rb_class_of(o)) return Qfalse;
  return rb_equal(rb_funcall(self,ID_CACHED("message"),0), rb_funcall(o,ID_CACHED("message"),0))?Qtrue:Qfalse; }
static VALUE exc_cause(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@__cause__")); }
static VALUE name_err_name(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@name")); }
static VALUE name_err_receiver(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@receiver")); }
static VALUE key_err_key(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@key")); }
static VALUE stop_result(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@result")); }
static VALUE sysexit_status(VALUE self, RProc *b){ (void)b;
  VALUE s = rb_ivar_get(self, ID_CACHED("@status")); return NIL_P(s)? LONG2FIX(0) : s; }
static VALUE sysexit_success(VALUE self, RProc *b){ return FIX2LONG(sysexit_status(self,b))==0?Qtrue:Qfalse; }
static VALUE frozen_err_receiver(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@receiver")); }

/* ===================== Encoding ===================== */
static VALUE enc_objs[4];
VALUE rb_enc_obj(rb_encoding *e){
  int i = e->index;
  if(!enc_objs[i]){
    VALUE o = rb_obj_alloc_of(rb_cEncoding);
    rb_ivar_set(o, ID_CACHED("@name"), rb_str_freeze(rb_str_new_cstr(e->name)));
    ivtbl_set(&BASIC(o)->iv, ID_CACHED("@__idx__"), LONG2FIX(i));
    BASIC(o)->frozen = 1;
    enc_objs[i] = o; }
  return enc_objs[i];
}
static VALUE enc_name(VALUE self, RProc *b){ (void)b; return rb_ivar_get(self, ID_CACHED("@name")); }
static VALUE enc_inspect(VALUE self, RProc *b){ (void)b;
  VALUE s = rb_str_new_cstr("#<Encoding:");
  VALUE n = rb_ivar_get(self, ID_CACHED("@name"));
  rb_str_cat(s,RSTR(n)->ptr,RSTR(n)->len); rb_str_cat_cstr(s,">"); return s; }
static VALUE enc_list(VALUE self, RProc *b){ (void)self;(void)b;
  VALUE a = rb_ary_new();
  rb_ary_push(a, rb_enc_obj(enc_utf8)); rb_ary_push(a, rb_enc_obj(enc_usascii));
  rb_ary_push(a, rb_enc_obj(enc_binary)); return a; }
static VALUE enc_find_m(VALUE self, VALUE n, RProc *b){ (void)self;(void)b;
  VALUE s = rb_str_to_str(n);
  rb_encoding *e = rb_enc_find(RSTR(s)->ptr);
  if(!e) rb_raise(rb_eArgError, "unknown encoding name - %s", RSTR(s)->ptr);
  return rb_enc_obj(e); }
static VALUE enc_default_external(VALUE self, RProc *b){ (void)self;(void)b; return rb_enc_obj(enc_utf8); }


/* ===================== Math, and the small gaps ===================== */
#include <math.h>
static RClass *rb_mMath;
static int has_method(RClass *c, const char *n){ RClass *ow; return rb_method_lookup(c, rb_intern(n), &ow) != NULL; }
static void def_if_absent(RClass *c, const char *n, VALUE (*fn)(), int argc){
  if(!has_method(c,n)) rb_define_method(c,n,fn,argc);
}
#define M1(name, expr) static VALUE math_##name(VALUE self, VALUE x, RProc *b){ (void)self;(void)b; double v = rb_num2dbl(x); return rb_float_new(expr); }
M1(sqrt, ({ if(v<0) rb_raise(rb_eArgError,"Numerical argument is out of domain - \"sqrt\""); sqrt(v); }))
M1(cbrt, cbrt(v)) M1(sin, sin(v)) M1(cos, cos(v)) M1(tan, tan(v))
M1(asin, asin(v)) M1(acos, acos(v)) M1(atan, atan(v)) M1(sinh, sinh(v))
M1(cosh, cosh(v)) M1(tanh, tanh(v)) M1(exp, exp(v)) M1(log2, log2(v))
M1(log10, log10(v)) M1(erf, erf(v))
static VALUE math_log(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  double v = rb_num2dbl(argv[0]);
  if(v < 0) rb_raise(rb_eArgError, "Numerical argument is out of domain - \"log\"");
  if(argc>1) return rb_float_new(log(v)/log(rb_num2dbl(argv[1])));
  return rb_float_new(log(v)); }
static VALUE math_atan2(VALUE self, VALUE y, VALUE x, RProc *b){ (void)self;(void)b;
  return rb_float_new(atan2(rb_num2dbl(y), rb_num2dbl(x))); }
static VALUE math_hypot(VALUE self, VALUE y, VALUE x, RProc *b){ (void)self;(void)b;
  return rb_float_new(hypot(rb_num2dbl(y), rb_num2dbl(x))); }
static VALUE math_pow(VALUE self, VALUE y, VALUE x, RProc *b){ (void)self;(void)b;
  return rb_float_new(pow(rb_num2dbl(y), rb_num2dbl(x))); }
/* ---- a minimal File, enough for the path arithmetic library code does ---- */
static VALUE path_of(VALUE v){ return rb_str_to_str(v); }
static VALUE file_dirname(VALUE self, VALUE p, RProc *b){ (void)self;(void)b;
  VALUE s = path_of(p); const char *c = RSTR(s)->ptr; long n = RSTR(s)->len;
  while(n > 1 && c[n-1]=='/') n--;
  long i = n - 1; while(i >= 0 && c[i] != '/') i--;
  if(i < 0) return rb_str_new_cstr(".");
  if(i == 0) return rb_str_new_cstr("/");
  while(i > 0 && c[i-1]=='/') i--;
  return rb_str_new(c, i); }
static VALUE file_basename(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE s = path_of(argv[0]); const char *c = RSTR(s)->ptr; long n = RSTR(s)->len;
  while(n > 1 && c[n-1]=='/') n--;
  long i = n - 1; while(i >= 0 && c[i] != '/') i--;
  VALUE base = rb_str_new(c+i+1, n-i-1);
  if(argc > 1){ VALUE ext = rb_str_to_str(argv[1]);
    long bl = RSTR(base)->len, el = RSTR(ext)->len;
    if(el && bl > el && memcmp(RSTR(base)->ptr+bl-el, RSTR(ext)->ptr, el)==0)
      base = rb_str_new(RSTR(base)->ptr, bl-el);
    else if(el==2 && memcmp(RSTR(ext)->ptr,".*",2)==0){
      long k = bl-1; while(k > 0 && RSTR(base)->ptr[k] != '.') k--;
      if(k > 0) base = rb_str_new(RSTR(base)->ptr, k); } }
  return base; }
static VALUE file_extname(VALUE self, VALUE p, RProc *b){ (void)self;(void)b;
  VALUE s = path_of(p); const char *c = RSTR(s)->ptr; long n = RSTR(s)->len;
  long i = n - 1; while(i >= 0 && c[i] != '/' && c[i] != '.') i--;
  if(i < 0 || c[i]=='/' || i==0 || c[i-1]=='/') return rb_str_new_cstr("");
  return rb_str_new(c+i, n-i); }
static VALUE file_join(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE out = rb_str_new("",0);
  for(int i=0;i<argc;i++){
    VALUE s = rb_obj_as_string(argv[i]);
    if(RSTR(out)->len && RSTR(out)->ptr[RSTR(out)->len-1] != '/' &&
       (RSTR(s)->len==0 || RSTR(s)->ptr[0] != '/')) rb_str_cat_cstr(out,"/");
    rb_str_cat(out, RSTR(s)->ptr, RSTR(s)->len); }
  return out; }
static VALUE file_expand_path(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  VALUE p = path_of(argv[0]);
  VALUE base = argc > 1 ? rb_str_to_str(argv[1]) : rb_str_new_cstr(".");
  if(RSTR(base)->len == 0 || RSTR(base)->ptr[0] != '/'){
    char cwd[4096];
    if(getcwd(cwd, sizeof cwd)){
      VALUE abs = rb_str_new_cstr(cwd);
      if(!(RSTR(base)->len == 1 && RSTR(base)->ptr[0]=='.')){
        rb_str_cat_cstr(abs, "/");
        rb_str_cat(abs, RSTR(base)->ptr, RSTR(base)->len);
      }
      base = abs;
    }
  }
  VALUE full;
  if(RSTR(p)->len && RSTR(p)->ptr[0]=='/') full = rb_str_dup(p);
  else { full = rb_str_dup(base);
    if(RSTR(full)->len && RSTR(full)->ptr[RSTR(full)->len-1] != '/') rb_str_cat_cstr(full,"/");
    rb_str_cat(full, RSTR(p)->ptr, RSTR(p)->len); }
  /* normalise . and .. */
  VALUE parts = rb_ary_new();
  const char *c = RSTR(full)->ptr; long n = RSTR(full)->len, i = 0;
  while(i < n){
    while(i < n && c[i]=='/') i++;
    long s0 = i; while(i < n && c[i] != '/') i++;
    if(i == s0) break;
    if(i-s0==1 && c[s0]=='.') continue;
    if(i-s0==2 && c[s0]=='.' && c[s0+1]=='.'){ if(RARR(parts)->len) RARR(parts)->len--; continue; }
    rb_ary_push(parts, rb_str_new(c+s0, i-s0)); }
  VALUE out = rb_str_new("",0);
  for(long k=0;k<RARR(parts)->len;k++){ rb_str_cat_cstr(out,"/");
    rb_str_cat(out, RSTR(RARR(parts)->ptr[k])->ptr, RSTR(RARR(parts)->ptr[k])->len); }
  if(RSTR(out)->len==0) rb_str_cat_cstr(out,"/");
  return out; }
static VALUE file_exist(VALUE self, VALUE p, RProc *b){ (void)self;(void)b;
  VALUE s = path_of(p); FILE *f = fopen(RSTR(s)->ptr,"rb");
  if(f){ fclose(f); return Qtrue; } return Qfalse; }
static VALUE file_read(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;(void)argc;
  VALUE s = path_of(argv[0]); FILE *f = fopen(RSTR(s)->ptr,"rb");
  if(!f) rb_raise(rb_eIOError, "No such file or directory @ rb_sysopen - %s", RSTR(s)->ptr);
  VALUE out = rb_str_new("",0); char buf[8192]; size_t r;
  while((r = fread(buf,1,sizeof buf,f)) > 0) rb_str_cat(out,buf,(long)r);
  fclose(f); return out; }
static VALUE file_split(VALUE self, VALUE p, RProc *b){
  VALUE a = rb_ary_new();
  rb_ary_push(a, file_dirname(self,p,b));
  VALUE one[1] = { p };
  rb_ary_push(a, file_basename(self,1,one,b));
  return a; }

static VALUE num_div(VALUE self, VALUE y, RProc *b){ (void)b;
  VALUE q = rb_funcall(self, ID_CACHED("/"), 1, y);
  if(!FIXNUM_P(q) && BASIC(q)->type==T_FLOAT) return rb_funcall(q, ID_CACHED("floor"), 0);
  return q; }
static VALUE num_modulo(VALUE self, VALUE y, RProc *b){ (void)b;
  return rb_funcall(self, ID_CACHED("%"), 1, y); }
static VALUE num_fdiv(VALUE self, VALUE y, RProc *b){ (void)b;
  return rb_float_new(rb_num2dbl(self) / rb_num2dbl(y)); }
#if RUBY_TARGET < 20
static VALUE obj_type_m(VALUE self, RProc *b){ (void)b;
  rb_warning("Object#type is deprecated; use Object#class");
  return rb_funcall(self, ID_CACHED("class"), 0); }
#endif
static VALUE nil_to_f(VALUE self, RProc *b){ (void)self;(void)b; return rb_float_new(0.0); }
static VALUE nil_to_i(VALUE self, RProc *b){ (void)self;(void)b; return LONG2FIX(0); }
/* top-level visibility helpers are private Kernel methods acting on Object */
static VALUE top_private(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("private"), argc, argv, b, 1, 1); }
static VALUE top_public(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("public"), argc, argv, b, 1, 1); }
static VALUE top_protected(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("protected"), argc, argv, b, 1, 1); }
static VALUE top_define_method(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("define_method"), argc, argv, b, 1, 1); }
static VALUE top_attr_accessor(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("attr_accessor"), argc, argv, b, 1, 1); }
static VALUE top_include(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("include"), argc, argv, b, 1, 1); }
static VALUE top_module_function(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  return rb_call_full((VALUE)rb_cObject, ID_CACHED("module_function"), argc, argv, b, 1, 1); }

static void init_extras(void){
  RClass *cFile = rb_define_class("File", rb_cObject);
  RClass *sf = rb_singleton_class((VALUE)cFile);
  rb_define_method(sf, "dirname", file_dirname, 1);
  rb_define_method(sf, "basename", file_basename, ARGC_ANY);
  rb_define_method(sf, "extname", file_extname, 1);
  rb_define_method(sf, "join", file_join, ARGC_ANY);
  rb_define_method(sf, "expand_path", file_expand_path, ARGC_ANY);
  rb_define_method(sf, "absolute_path", file_expand_path, ARGC_ANY);
  rb_define_method(sf, "exist?", file_exist, 1);
  rb_define_method(sf, "exists?", file_exist, 1);
  rb_define_method(sf, "file?", file_exist, 1);
  rb_define_method(sf, "read", file_read, ARGC_ANY);
  rb_define_method(sf, "split", file_split, 1);
  rb_define_const(cFile, "SEPARATOR", rb_str_new_cstr("/"));
  rb_define_const(cFile, "ALT_SEPARATOR", Qnil);
  rb_mMath = rb_define_module("Math");
  rb_define_const(rb_mMath, "PI", rb_float_new(3.141592653589793));
  rb_define_const(rb_mMath, "E", rb_float_new(2.718281828459045));
  RClass *sm = rb_singleton_class((VALUE)rb_mMath);
#define MDEF(n, a) do { rb_define_method(sm, #n, math_##n, a); rb_define_method(rb_mMath, #n, math_##n, a); } while(0)
  MDEF(sqrt,1); MDEF(cbrt,1); MDEF(sin,1); MDEF(cos,1); MDEF(tan,1);
  MDEF(asin,1); MDEF(acos,1); MDEF(atan,1); MDEF(sinh,1); MDEF(cosh,1);
  MDEF(tanh,1); MDEF(exp,1); MDEF(log2,1); MDEF(log10,1); MDEF(erf,1);
  MDEF(atan2,2); MDEF(hypot,2); MDEF(pow,2);
  rb_define_method(sm, "log", math_log, ARGC_ANY);
  rb_define_method(rb_mMath, "log", math_log, ARGC_ANY);
#undef MDEF
  rb_define_const(rb_cFloat, "MAX_10_EXP", LONG2FIX(308));
  rb_define_const(rb_cFloat, "MIN_10_EXP", LONG2FIX(-307));
  rb_define_const(rb_cFloat, "MAX_EXP", LONG2FIX(1024));
  rb_define_const(rb_cFloat, "MIN_EXP", LONG2FIX(-1021));
  rb_define_const(rb_cFloat, "RADIX", LONG2FIX(2));
  rb_define_const(rb_cFloat, "ROUNDS", LONG2FIX(1));
#if RUBY_TARGET < 20
  rb_define_method(rb_cKernel, "type", obj_type_m, 0);
#endif
  def_if_absent(rb_cNumeric, "div", num_div, 1);
  def_if_absent(rb_cNumeric, "modulo", num_modulo, 1);
  def_if_absent(rb_cNumeric, "fdiv", num_fdiv, 1);
  def_if_absent(rb_cInteger, "div", num_div, 1);
  def_if_absent(rb_cInteger, "modulo", num_modulo, 1);
  def_if_absent(rb_cFloat, "fdiv", num_fdiv, 1);
  def_if_absent(rb_cFloat, "div", num_div, 1);
  def_if_absent(rb_cNilClass, "to_f", nil_to_f, 0);
  def_if_absent(rb_cNilClass, "to_i", nil_to_i, 0);
  rb_define_private_method(rb_cKernel, "private", top_private, ARGC_ANY);
  rb_define_private_method(rb_cKernel, "public", top_public, ARGC_ANY);
  rb_define_private_method(rb_cKernel, "protected", top_protected, ARGC_ANY);
  rb_define_private_method(rb_cKernel, "define_method", top_define_method, ARGC_ANY);
  rb_define_private_method(rb_cKernel, "attr_accessor", top_attr_accessor, ARGC_ANY);
  rb_define_private_method(rb_cKernel, "include", top_include, ARGC_ANY);
  rb_define_private_method(rb_cKernel, "module_function", top_module_function, ARGC_ANY);
}

void rb_init_misc(void){
  RClass *S = rb_cSymbol;
  rb_define_method(S, "to_s", sym_to_s, 0);
  rb_define_method(S, "id2name", sym_to_s, 0);
#if RUBY_TARGET >= 31
  rb_define_method(S, "name", sym_name, 0);   /* 3.0: frozen string */
#endif
  rb_define_method(S, "to_sym", sym_to_sym, 0);
  rb_define_method(S, "inspect", sym_inspect, 0);
  rb_define_method(S, "to_proc", sym_to_proc, 0);
  rb_define_method(S, "length", sym_length, 0);
  rb_define_method(S, "size", sym_length, 0);
  rb_define_method(S, "<=>", sym_cmp, 1);
  rb_define_method(S, "==", sym_eq, 1);
  rb_define_method(S, "===", sym_eq, 1);
  rb_define_method(S, "eql?", sym_eq, 1);
  rb_define_method(S, "equal?", sym_eq, 1);
  rb_define_method(S, "hash", sym_hash, 0);
  rb_define_method(S, "succ", sym_succ, 0);
  rb_define_method(S, "next", sym_succ, 0);
  rb_define_method(S, "upcase", sym_upcase, 0);
  rb_define_method(S, "downcase", sym_downcase, 0);
  rb_define_method(S, "capitalize", sym_capitalize, 0);
  rb_define_method(S, "empty?", sym_empty, 0);
  rb_define_method(S, "[]", sym_aref, ARGC_ANY);
  rb_define_method(S, "start_with?", sym_start_with, ARGC_ANY);
  rb_define_method(S, "end_with?", sym_end_with, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cSymbol, "all_symbols", sym_all_syms, 0);

  RClass *P = rb_cProc;
  rb_define_method(P, "call", proc_call, ARGC_ANY);
  rb_define_method(P, "()", proc_call, ARGC_ANY);
  rb_define_method(P, "yield", proc_call, ARGC_ANY);
  rb_define_method(P, "[]", proc_call, ARGC_ANY);
  rb_define_method(P, "===", proc_call, ARGC_ANY);
  rb_define_method(P, "lambda?", proc_lambda_p, 0);
  rb_define_method(P, "arity", proc_arity, 0);
  rb_define_method(P, "parameters", proc_parameters, ARGC_ANY);
  rb_define_method(P, "to_proc", proc_to_proc, 0);
  rb_define_method(P, "curry", proc_curry, ARGC_ANY);
  rb_define_method(P, "inspect", proc_inspect, 0);
  rb_define_method(P, "to_s", proc_inspect, 0);
  rb_define_singleton_method((VALUE)rb_cProc, "new", proc_new, ARGC_ANY);
#if RUBY_TARGET >= 27
  rb_define_method(P, ">>", proc_rshift, 1);
  rb_define_method(P, "<<", proc_lshift, 1);
#endif

  RClass *R = rb_cRange;
  rb_define_method(R, "begin", rng_begin, 0);
  rb_define_method(R, "first", rng_first, ARGC_ANY);
  rb_define_method(R, "end", rng_end, 0);
  rb_define_method(R, "last", rng_last, ARGC_ANY);
  rb_define_method(R, "exclude_end?", rng_excl, 0);
  rb_define_method(R, "each", rng_each, 0);
  rb_define_method(R, "to_a", rng_to_a, 0);
  rb_define_method(R, "entries", rng_to_a, 0);
  rb_define_method(R, "include?", rng_include, 1);
  rb_define_method(R, "member?", rng_include, 1);
  rb_define_method(R, "cover?", rng_include, 1);
  rb_define_method(R, "===", rng_eqq, 1);
  rb_define_method(R, "size", rng_size, 0);
  rb_define_method(R, "count", rng_size, 0);
  rb_define_method(R, "inspect", rng_inspect, 0);
  rb_define_method(R, "to_s", rng_to_s, 0);
  rb_define_method(R, "==", rng_eq, 1);
  rb_define_method(R, "eql?", rng_eq, 1);
  rb_define_method(R, "hash", rng_hash, 0);
  rb_define_method(R, "step", rng_step, ARGC_ANY);
  rb_define_method(R, "%", rng_step, ARGC_ANY);
  rb_define_method(R, "min", rng_min, ARGC_ANY);
  rb_define_method(R, "max", rng_max, ARGC_ANY);
  rb_define_method(R, "sum", rng_sum, ARGC_ANY);
  rb_define_method(R, "initialize", rng_new, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cRange, "new", rng_new, ARGC_ANY);
#if RUBY_TARGET >= 33
  rb_define_method(R, "overlap?", rng_overlap, 1);
#endif

  RClass *X = rb_cException;
  rb_define_method(X, "initialize", exc_initialize, ARGC_ANY);
  rb_define_method(X, "message", exc_message, 0);
  rb_define_method(X, "to_s", exc_to_s, 0);
  rb_define_method(X, "inspect", exc_inspect, 0);
  rb_define_method(X, "full_message", exc_full_message, ARGC_ANY);
#if RUBY_TARGET >= 33
  rb_define_method(X, "detailed_message", exc_detailed_message, ARGC_ANY);
#endif
  rb_define_method(X, "backtrace", exc_backtrace, 0);
  rb_define_method(X, "set_backtrace", exc_set_backtrace, 1);
  rb_define_method(X, "exception", exc_exception, ARGC_ANY);
  rb_define_method(X, "==", exc_eq, 1);
  rb_define_method(X, "cause", exc_cause, 0);
  rb_define_singleton_method((VALUE)rb_cException, "exception", exc_class_exception, ARGC_ANY);
  rb_define_method(rb_eNameError, "name", name_err_name, 0);
  rb_define_method(rb_eNameError, "receiver", name_err_receiver, 0);
  rb_define_method(rb_eKeyError, "key", key_err_key, 0);
  rb_define_method(rb_eKeyError, "receiver", name_err_receiver, 0);
  rb_define_method(rb_eStopIteration, "result", stop_result, 0);
  rb_define_method(rb_eSystemExit, "status", sysexit_status, 0);
  rb_define_method(rb_eSystemExit, "success?", sysexit_success, 0);
  rb_define_method(rb_eFrozenError, "receiver", frozen_err_receiver, 0);

  rb_define_method(rb_cEncoding, "name", enc_name, 0);
  rb_define_method(rb_cEncoding, "to_s", enc_name, 0);
  rb_define_method(rb_cEncoding, "inspect", enc_inspect, 0);
  rb_define_singleton_method((VALUE)rb_cEncoding, "list", enc_list, 0);
  rb_define_singleton_method((VALUE)rb_cEncoding, "find", enc_find_m, 1);
  rb_define_singleton_method((VALUE)rb_cEncoding, "default_external", enc_default_external, 0);
  rb_define_singleton_method((VALUE)rb_cEncoding, "default_internal", enc_default_external, 0);
  rb_define_const(rb_cEncoding, "UTF_8", rb_enc_obj(enc_utf8));
  rb_define_const(rb_cEncoding, "US_ASCII", rb_enc_obj(enc_usascii));
  rb_define_const(rb_cEncoding, "ASCII_8BIT", rb_enc_obj(enc_binary));
  rb_define_const(rb_cEncoding, "BINARY", rb_enc_obj(enc_binary));
  rb_define_const(rb_cObject, "ARGV", rb_ary_new());
  rb_define_const(rb_cObject, "ENV", rb_hash_new());
  rb_define_const(rb_cFloat, "INFINITY", rb_float_new(1.0/0.0));
  rb_define_const(rb_cFloat, "NAN", rb_float_new(0.0/0.0));
  rb_define_const(rb_cFloat, "EPSILON", rb_float_new(2.220446049250313e-16));
  rb_define_const(rb_cFloat, "MAX", rb_float_new(1.7976931348623157e308));
  rb_define_const(rb_cFloat, "MIN", rb_float_new(2.2250738585072014e-308));
  rb_define_const(rb_cFloat, "DIG", LONG2FIX(15));
  rb_define_const(rb_cFloat, "MANT_DIG", LONG2FIX(53));
  rb_define_const(rb_cInteger, "MAX", LONG2FIX(FIXNUM_MAX));
  init_extras();
}
