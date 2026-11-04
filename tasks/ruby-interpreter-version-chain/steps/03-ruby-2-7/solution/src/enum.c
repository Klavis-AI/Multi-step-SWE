#include "internal.h"
extern VALUE rb_call_full(VALUE, ID, int, VALUE *, RProc *, int, int);
extern VALUE rb_lazy_new(VALUE);
ID to_mid_pub(VALUE v);

/* Enumerable is implemented by driving the receiver's #each with a C block. */
typedef struct { VALUE acc; RProc *blk; VALUE aux, aux2; long n, i; int flag; VALUE cmp; } EnumCtx;

static VALUE pack_yield(int argc, VALUE *argv){
  if(argc==1) return argv[0];
  if(argc==0) return Qnil;
  return rb_ary_new_from(argc,argv);
}
#define EACH(recv, fn, ctx) rb_block_call((recv), ID_CACHED("each"), 0, NULL, (fn), (VALUE)(ctx))

static VALUE cb_to_a(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  rb_ary_push(((EnumCtx*)c)->acc, pack_yield(argc,argv)); return Qnil; }
static VALUE enum_to_a(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  EnumCtx c = {0}; c.acc = rb_ary_new();
  if(argc>0) rb_block_call(self, ID_CACHED("each"), argc, argv, cb_to_a, (VALUE)&c);
  else EACH(self, cb_to_a, &c);
  return c.acc; }

static VALUE cb_map(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  rb_ary_push(e->acc, rb_yield_values(e->blk, argc, argv)); return Qnil; }
static VALUE enum_map(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("map"), 0, NULL);
  EnumCtx c = {0}; c.acc = rb_ary_new(); c.blk = b;
  EACH(self, cb_map, &c); return c.acc; }

static VALUE cb_flatmap(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = rb_yield_values(e->blk, argc, argv);
  VALUE a = rb_check_array_type(v);
  if(NIL_P(a)) rb_ary_push(e->acc, v);
  else for(long i=0;i<RARR(a)->len;i++) rb_ary_push(e->acc, RARR(a)->ptr[i]);
  return Qnil; }
static VALUE enum_flat_map(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("flat_map"), 0, NULL);
  EnumCtx c = {0}; c.acc = rb_ary_new(); c.blk = b;
  EACH(self, cb_flatmap, &c); return c.acc; }

static VALUE cb_select(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  if(RTEST(rb_yield_values(e->blk,argc,argv))) rb_ary_push(e->acc, v);
  return Qnil; }
static VALUE enum_select(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("select"), 0, NULL);
  EnumCtx c = {0}; c.acc = rb_ary_new(); c.blk = b;
  EACH(self, cb_select, &c); return c.acc; }
static VALUE cb_reject(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  if(!RTEST(rb_yield_values(e->blk,argc,argv))) rb_ary_push(e->acc, v);
  return Qnil; }
static VALUE enum_reject(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("reject"), 0, NULL);
  EnumCtx c = {0}; c.acc = rb_ary_new(); c.blk = b;
  EACH(self, cb_reject, &c); return c.acc; }

static VALUE cb_each_with_index(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  VALUE pair[2] = { v, LONG2FIX(e->i++) };
  if(e->blk) rb_yield_values(e->blk,2,pair);
  else rb_ary_push(e->acc, rb_ary_new_from(2,pair));
  return Qnil; }
static VALUE enum_each_with_index(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each_with_index"), argc, argv);
  EnumCtx c = {0}; c.blk = b; c.acc = rb_ary_new();
  if(argc>0) rb_block_call(self, ID_CACHED("each"), argc, argv, cb_each_with_index, (VALUE)&c);
  else EACH(self, cb_each_with_index, &c);
  return self; }
static VALUE cb_with_object(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE pair[2] = { pack_yield(argc,argv), e->aux };
  rb_yield_values(e->blk,2,pair); return Qnil; }
static VALUE enum_each_with_object(VALUE self, VALUE memo, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each_with_object"), 1, &memo);
  EnumCtx c = {0}; c.blk = b; c.aux = memo;
  EACH(self, cb_with_object, &c); return memo; }

static VALUE cb_inject(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  if(e->flag==0){ e->acc = v; e->flag=1; return Qnil; }
  if(e->blk){ VALUE pair[2] = { e->acc, v }; e->acc = rb_yield_values(e->blk,2,pair); }
  else e->acc = rb_funcall(e->acc, (ID)e->n, 1, v);
  return Qnil; }
static VALUE enum_inject(VALUE self, int argc, VALUE *argv, RProc *b){
  EnumCtx c = {0}; c.blk = b; c.flag = 0;
  if(argc==2){ c.acc = argv[0]; c.flag=1; c.n = (long)to_mid_pub(argv[1]); c.blk=NULL; }
  else if(argc==1){
    if(!b){ c.n = (long)to_mid_pub(argv[0]); c.flag=0; }
    else { c.acc = argv[0]; c.flag = 1; }
  }
  EACH(self, cb_inject, &c);
  if(!c.flag) return Qnil;
  return c.acc; }

static VALUE cb_each_entry(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  rb_yield1(e->blk, pack_yield(argc,argv)); return Qnil; }

static VALUE cb_find(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  if(RTEST(rb_yield_values(e->blk,argc,argv))){ e->acc = v; e->flag=1; rb_jump_tag(TAG_BREAK,Qnil,NULL); }
  return Qnil; }
static VALUE enum_find(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("find"), 0, NULL);
  EnumCtx c = {0}; c.blk = b; c.acc = Qnil;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ EACH(self, cb_find, &c); TAG_POP(tag); }
  else { TAG_POP(tag); if(tag.state != TAG_BREAK) rb_jump_tag(tag.state,tag.retval,tag.target); }
  if(c.flag) return c.acc;
  if(argc>0 && !NIL_P(argv[0])) return rb_funcall(argv[0], ID_CACHED("call"), 0);
  return Qnil; }
static VALUE cb_find_index(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  int hit = e->blk ? RTEST(rb_yield_values(e->blk,argc,argv)) : rb_equal(e->aux, v);
  if(hit){ e->acc = LONG2FIX(e->i); e->flag=1; rb_jump_tag(TAG_BREAK,Qnil,NULL); }
  e->i++; return Qnil; }
static VALUE enum_find_index(VALUE self, int argc, VALUE *argv, RProc *b){
  EnumCtx c = {0}; c.blk = b; c.acc = Qnil;
  if(argc>0){ c.aux = argv[0]; c.blk = NULL; }
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ EACH(self, cb_find_index, &c); TAG_POP(tag); }
  else { TAG_POP(tag); if(tag.state != TAG_BREAK) rb_jump_tag(tag.state,tag.retval,tag.target); }
  return c.flag? c.acc : Qnil; }
static VALUE cb_find_all_pred(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e = (EnumCtx*)c;
  VALUE v = pack_yield(argc,argv);
  int t;
  if(e->blk) t = RTEST(rb_yield_values(e->blk,argc,argv));
  else if(e->aux2 != 0) t = RTEST(rb_funcall(e->aux2, ID_CACHED("==="), 1, v));
  else t = RTEST(v);
  if(e->flag==1){ if(!t){ e->acc=Qfalse; rb_jump_tag(TAG_BREAK,Qnil,NULL);} }
  else if(e->flag==2){ if(t){ e->acc=Qtrue; rb_jump_tag(TAG_BREAK,Qnil,NULL);} }
  else if(e->flag==3){ if(t){ e->acc=Qfalse; rb_jump_tag(TAG_BREAK,Qnil,NULL);} }
  else if(e->flag==4){ if(t){ e->n++; if(e->n>1){ e->acc=Qfalse; rb_jump_tag(TAG_BREAK,Qnil,NULL);} } }
  else if(e->flag==5){ if(t) e->n++; }
  return Qnil; }
static VALUE pred_run(VALUE self, int argc, VALUE *argv, RProc *b, int mode, VALUE init){
  EnumCtx c = {0}; c.blk = b; c.acc = init; c.flag = mode; c.aux2 = argc>0? argv[0] : 0;
  if(argc>0) c.blk = NULL;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ EACH(self, cb_find_all_pred, &c); TAG_POP(tag); }
  else { TAG_POP(tag); if(tag.state != TAG_BREAK) rb_jump_tag(tag.state,tag.retval,tag.target); }
  if(mode==4) return c.acc==Qfalse? Qfalse : (c.n==1?Qtrue:Qfalse);
  if(mode==5) return LONG2FIX(c.n);
  return c.acc; }
static VALUE enum_all(VALUE self, int argc, VALUE *argv, RProc *b){ return pred_run(self,argc,argv,b,1,Qtrue); }
static VALUE enum_any(VALUE self, int argc, VALUE *argv, RProc *b){ return pred_run(self,argc,argv,b,2,Qfalse); }
static VALUE enum_none(VALUE self, int argc, VALUE *argv, RProc *b){ return pred_run(self,argc,argv,b,3,Qtrue); }
static VALUE enum_one(VALUE self, int argc, VALUE *argv, RProc *b){ return pred_run(self,argc,argv,b,4,Qtrue); }
static VALUE enum_count(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc==0 && !b){ VALUE a = enum_to_a(self,0,NULL,NULL); return LONG2FIX(RARR(a)->len); }
  if(argc>0){ EnumCtx c={0}; c.flag=5; c.aux2=argv[0]; EACH(self,cb_find_all_pred,&c); return LONG2FIX(c.n); }
  return pred_run(self,argc,argv,b,5,Qnil); }
static VALUE cb_include(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  if(rb_equal(v, e->aux)){ e->acc=Qtrue; rb_jump_tag(TAG_BREAK,Qnil,NULL); } return Qnil; }
static VALUE enum_include(VALUE self, VALUE o, RProc *b){ (void)b;
  EnumCtx c = {0}; c.aux=o; c.acc=Qfalse;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ EACH(self, cb_include, &c); TAG_POP(tag); }
  else { TAG_POP(tag); if(tag.state != TAG_BREAK) rb_jump_tag(tag.state,tag.retval,tag.target); }
  return c.acc; }

static long cmp2(VALUE a, VALUE b){
  VALUE c = rb_funcall(a, ID_CACHED("<=>"), 1, b);
  if(NIL_P(c)) rb_raise(rb_eArgError, "comparison of %s with %s failed", rb_obj_classname(a),
                        NIL_P(b)?"nil":rb_obj_classname(b));
  return rb_num2long(c);
}
static VALUE cb_minmax(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  if(!e->flag){ e->acc=v; e->aux=v; e->flag=1; return Qnil; }
  long r;
  if(e->blk){ VALUE p[2]={v,e->acc}; VALUE rr = rb_yield_values(e->blk,2,p);
    if(NIL_P(rr)) rb_raise(rb_eArgError,"comparison failed"); r = rb_num2long(rr); }
  else r = cmp2(v,e->acc);
  if(r<0) e->acc=v;
  if(e->blk){ VALUE p[2]={v,e->aux}; VALUE rr = rb_yield_values(e->blk,2,p); r = rb_num2long(rr); }
  else r = cmp2(v,e->aux);
  if(r>0) e->aux=v;
  return Qnil; }
static VALUE enum_min(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc>0){ VALUE a = enum_to_a(self,0,NULL,NULL);
    VALUE s = rb_call(a, ID_CACHED("sort"), 0, NULL, b);
    long n = rb_num2long(argv[0]); VALUE r = rb_ary_new();
    for(long i=0;i<n && i<RARR(s)->len;i++) rb_ary_push(r,RARR(s)->ptr[i]);
    return r; }
  EnumCtx c={0}; c.blk=b; c.acc=Qnil; EACH(self,cb_minmax,&c); return c.acc; }
static VALUE enum_max(VALUE self, int argc, VALUE *argv, RProc *b){
  if(argc>0){ VALUE a = enum_to_a(self,0,NULL,NULL);
    VALUE s = rb_call(a, ID_CACHED("sort"), 0, NULL, b);
    long n = rb_num2long(argv[0]); VALUE r = rb_ary_new();
    for(long i=RARR(s)->len-1;i>=0 && RARR(r)->len<n;i--) rb_ary_push(r,RARR(s)->ptr[i]);
    return r; }
  EnumCtx c={0}; c.blk=b; c.aux=Qnil; EACH(self,cb_minmax,&c); return c.flag?c.aux:Qnil; }
static VALUE enum_minmax(VALUE self, RProc *b){
  EnumCtx c={0}; c.blk=b; EACH(self,cb_minmax,&c);
  VALUE r = rb_ary_new(); rb_ary_push(r, c.flag?c.acc:Qnil); rb_ary_push(r, c.flag?c.aux:Qnil); return r; }
static VALUE cb_minby(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  VALUE k = rb_yield_values(e->blk,argc,argv);
  if(!e->flag){ e->acc=v; e->cmp=k; e->aux=v; e->aux2=k; e->flag=1; return Qnil; }
  if(cmp2(k,e->cmp)<0){ e->acc=v; e->cmp=k; }
  if(cmp2(k,e->aux2)>0){ e->aux=v; e->aux2=k; }
  return Qnil; }
static VALUE enum_min_by(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("min_by"), 0, NULL);
  EnumCtx c={0}; c.blk=b; c.acc=Qnil; EACH(self,cb_minby,&c); return c.flag?c.acc:Qnil; }
static VALUE enum_max_by(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("max_by"), 0, NULL);
  EnumCtx c={0}; c.blk=b; c.aux=Qnil; EACH(self,cb_minby,&c); return c.flag?c.aux:Qnil; }
static VALUE enum_minmax_by(VALUE self, RProc *b){
  EnumCtx c={0}; c.blk=b; EACH(self,cb_minby,&c);
  VALUE r=rb_ary_new(); rb_ary_push(r,c.flag?c.acc:Qnil); rb_ary_push(r,c.flag?c.aux:Qnil); return r; }

static VALUE enum_sort(VALUE self, RProc *b){
  VALUE a = enum_to_a(self,0,NULL,NULL);
  return rb_call(a, ID_CACHED("sort"), 0, NULL, b); }
static VALUE cb_sortby(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  VALUE pair = rb_ary_new(); rb_ary_push(pair, rb_yield_values(e->blk,argc,argv)); rb_ary_push(pair,v);
  rb_ary_push(e->acc,pair); return Qnil; }
static VALUE enum_sort_by(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("sort_by"), 0, NULL);
  EnumCtx c={0}; c.blk=b; c.acc=rb_ary_new(); EACH(self,cb_sortby,&c);
  RArray *a = RARR(c.acc);
  for(long i=1;i<a->len;i++){ VALUE t=a->ptr[i]; long j=i-1;
    while(j>=0 && cmp2(RARR(a->ptr[j])->ptr[0], RARR(t)->ptr[0])>0){ a->ptr[j+1]=a->ptr[j]; j--; }
    a->ptr[j+1]=t; }
  VALUE r = rb_ary_new();
  for(long i=0;i<a->len;i++) rb_ary_push(r, RARR(a->ptr[i])->ptr[1]);
  return r; }
static VALUE cb_group(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  VALUE k = rb_yield_values(e->blk,argc,argv);
  VALUE lst = rb_hash_lookup(e->acc,k,Qundef);
  if(lst==Qundef){ lst = rb_ary_new(); rb_hash_aset(e->acc,k,lst); }
  rb_ary_push(lst,v); return Qnil; }
static VALUE enum_group_by(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("group_by"), 0, NULL);
  EnumCtx c={0}; c.blk=b; c.acc=rb_hash_new(); EACH(self,cb_group,&c); return c.acc; }
static VALUE cb_partition(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  rb_ary_push(RTEST(rb_yield_values(e->blk,argc,argv))? e->acc : e->aux, v); return Qnil; }
static VALUE enum_partition(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("partition"), 0, NULL);
  EnumCtx c={0}; c.blk=b; c.acc=rb_ary_new(); c.aux=rb_ary_new();
  EACH(self,cb_partition,&c);
  VALUE r=rb_ary_new(); rb_ary_push(r,c.acc); rb_ary_push(r,c.aux); return r; }
static VALUE cb_take_n(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  if(e->i >= e->n) rb_jump_tag(TAG_BREAK,Qnil,NULL);
  rb_ary_push(e->acc, pack_yield(argc,argv)); e->i++;
  if(e->i >= e->n) rb_jump_tag(TAG_BREAK,Qnil,NULL);
  return Qnil; }
static VALUE enum_take(VALUE self, VALUE nv, RProc *b){ (void)b;
  long n = rb_num2long(nv);
  if(n<0) rb_raise(rb_eArgError,"attempt to take negative size");
  EnumCtx c={0}; c.acc=rb_ary_new(); c.n=n;
  if(n==0) return c.acc;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ EACH(self,cb_take_n,&c); TAG_POP(tag); }
  else { TAG_POP(tag); if(tag.state != TAG_BREAK) rb_jump_tag(tag.state,tag.retval,tag.target); }
  return c.acc; }
static VALUE cb_take_while(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  if(!RTEST(rb_yield_values(e->blk,argc,argv))) rb_jump_tag(TAG_BREAK,Qnil,NULL);
  rb_ary_push(e->acc, pack_yield(argc,argv)); return Qnil; }
static VALUE enum_take_while(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("take_while"), 0, NULL);
  EnumCtx c={0}; c.acc=rb_ary_new(); c.blk=b;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ EACH(self,cb_take_while,&c); TAG_POP(tag); }
  else { TAG_POP(tag); if(tag.state != TAG_BREAK) rb_jump_tag(tag.state,tag.retval,tag.target); }
  return c.acc; }
static VALUE cb_drop(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  if(e->i >= e->n) rb_ary_push(e->acc, pack_yield(argc,argv));
  e->i++; return Qnil; }
static VALUE enum_drop(VALUE self, VALUE nv, RProc *b){ (void)b;
  long n = rb_num2long(nv);
  if(n<0) rb_raise(rb_eArgError,"attempt to drop negative size");
  EnumCtx c={0}; c.acc=rb_ary_new(); c.n=n; EACH(self,cb_drop,&c); return c.acc; }
static VALUE cb_drop_while(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v = pack_yield(argc,argv);
  if(!e->flag){ if(RTEST(rb_yield_values(e->blk,argc,argv))) return Qnil; e->flag=1; }
  rb_ary_push(e->acc,v); return Qnil; }
static VALUE enum_drop_while(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("drop_while"), 0, NULL);
  EnumCtx c={0}; c.acc=rb_ary_new(); c.blk=b; EACH(self,cb_drop_while,&c); return c.acc; }
static VALUE cb_each_slice(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  if(NIL_P(e->aux)) e->aux = rb_ary_new();
  rb_ary_push(e->aux, pack_yield(argc,argv));
  if(RARR(e->aux)->len >= e->n){
    if(e->blk) rb_yield1(e->blk, e->aux); else rb_ary_push(e->acc, e->aux);
    e->aux = Qnil; }
  return Qnil; }
static VALUE enum_each_slice(VALUE self, VALUE nv, RProc *b){
  long n = rb_num2long(nv);
  if(n<=0) rb_raise(rb_eArgError,"invalid slice size");
  if(!b) return rb_enum_new(self, ID_CACHED("each_slice"), 1, &nv);
  EnumCtx c={0}; c.n=n; c.blk=b; c.acc=rb_ary_new(); c.aux=Qnil;
  EACH(self,cb_each_slice,&c);
  if(!NIL_P(c.aux)){ if(c.blk) rb_yield1(c.blk,c.aux); else rb_ary_push(c.acc,c.aux); }
#if RUBY_TARGET >= 31
  return self;
#else
  return Qnil;
#endif
}
static VALUE cb_each_cons(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  if(NIL_P(e->aux)) e->aux = rb_ary_new();
  rb_ary_push(e->aux, pack_yield(argc,argv));
  if(RARR(e->aux)->len > e->n){
    VALUE nw = rb_ary_new();
    for(long i=1;i<RARR(e->aux)->len;i++) rb_ary_push(nw,RARR(e->aux)->ptr[i]);
    e->aux = nw; }
  if(RARR(e->aux)->len == e->n){
    VALUE cp = rb_ary_dup(e->aux);
    if(e->blk) rb_yield1(e->blk,cp); else rb_ary_push(e->acc,cp); }
  return Qnil; }
static VALUE enum_each_cons(VALUE self, VALUE nv, RProc *b){
  long n = rb_num2long(nv);
  if(n<=0) rb_raise(rb_eArgError,"invalid size");
  if(!b) return rb_enum_new(self, ID_CACHED("each_cons"), 1, &nv);
  EnumCtx c={0}; c.n=n; c.blk=b; c.acc=rb_ary_new(); c.aux=Qnil;
  EACH(self,cb_each_cons,&c);
#if RUBY_TARGET >= 31
  return self;
#else
  return Qnil;
#endif
}
static VALUE cb_zip(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  VALUE row = rb_ary_new(); rb_ary_push(row, pack_yield(argc,argv));
  for(long k=0;k<RARR(e->aux)->len;k++){
    VALUE other = RARR(e->aux)->ptr[k];
    rb_ary_push(row, rb_ary_entry(other, e->i)); }
  if(e->blk) rb_yield1(e->blk,row); else rb_ary_push(e->acc,row);
  e->i++; return Qnil; }
static VALUE enum_zip(VALUE self, int argc, VALUE *argv, RProc *b){
  EnumCtx c={0}; c.acc=rb_ary_new(); c.blk=b; c.aux=rb_ary_new();
  for(int i=0;i<argc;i++){
    VALUE a = rb_check_array_type(argv[i]);
    if(NIL_P(a)) a = rb_funcall(argv[i], ID_CACHED("to_a"), 0);
    rb_ary_push(c.aux, a); }
  EACH(self,cb_zip,&c);
  return b? Qnil : c.acc; }
static VALUE enum_first(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc==0){ VALUE r = enum_take(self, LONG2FIX(1), NULL);
    return RARR(r)->len? RARR(r)->ptr[0] : Qnil; }
  return enum_take(self, argv[0], NULL); }
static VALUE enum_entries(VALUE self, int argc, VALUE *argv, RProc *b){ return enum_to_a(self,argc,argv,b); }
static VALUE enum_each_entry(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each_entry"), 0, NULL);
  EnumCtx c={0}; c.blk=b; EACH(self,cb_each_entry,&c); return self; }
static VALUE cb_rev(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  rb_ary_push(((EnumCtx*)c)->acc, pack_yield(argc,argv)); return Qnil; }
static VALUE enum_reverse_each(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("reverse_each"), 0, NULL);
  EnumCtx c={0}; c.acc=rb_ary_new(); EACH(self,cb_rev,&c);
  for(long i=RARR(c.acc)->len-1;i>=0;i--) rb_yield1(b, RARR(c.acc)->ptr[i]);
  return self; }
static VALUE enum_to_h(VALUE self, int argc, VALUE *argv, RProc *b){
  VALUE a = enum_to_a(self,argc,argv,NULL);
  VALUE h = rb_hash_new();
  for(long i=0;i<RARR(a)->len;i++){
    VALUE e = RARR(a)->ptr[i];
    if(b) e = rb_yield1(b, e);
    VALUE p = rb_check_array_type(e);
    if(NIL_P(p) || RARR(p)->len != 2)
      rb_raise(rb_eTypeError, "wrong element type %s (expected array)", rb_obj_classname(e));
    rb_hash_aset(h, RARR(p)->ptr[0], RARR(p)->ptr[1]); }
  return h; }
static VALUE cb_sum(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  VALUE v = e->blk? rb_yield_values(e->blk,argc,argv) : pack_yield(argc,argv);
  e->acc = rb_funcall(e->acc, ID_CACHED("+"), 1, v); return Qnil; }
static VALUE enum_sum(VALUE self, int argc, VALUE *argv, RProc *b){
  EnumCtx c={0}; c.blk=b; c.acc = argc>0? argv[0] : LONG2FIX(0);
  EACH(self,cb_sum,&c); return c.acc; }
static VALUE cb_tally(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v=pack_yield(argc,argv);
  VALUE cur = rb_hash_lookup(e->acc,v,Qundef);
  rb_hash_aset(e->acc, v, LONG2FIX(cur==Qundef?1:FIX2LONG(cur)+1)); return Qnil; }
static VALUE enum_tally(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  EnumCtx c={0};
#if RUBY_TARGET >= 31
  /* with a hash argument the counts are added to that hash, which is returned */
  if(argc>1) rb_raise(rb_eArgError,"wrong number of arguments (given %d, expected 0..1)",argc);
  if(argc==1){
    if(FIXNUM_P(argv[0]) || BASIC(argv[0])->type != T_HASH)
      rb_raise(rb_eTypeError,"wrong argument type %s (expected Hash)", rb_obj_classname(argv[0]));
    rb_check_frozen(argv[0]);
    c.acc = argv[0];
  } else c.acc = rb_hash_new();
#else
  if(argc>0) rb_raise(rb_eArgError,"wrong number of arguments (given %d, expected 0)",argc);
  c.acc = rb_hash_new();
#endif
  EACH(self,cb_tally,&c); return c.acc; }
static VALUE cb_filter_map(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c;
  VALUE v = rb_yield_values(e->blk,argc,argv);
  if(RTEST(v)) rb_ary_push(e->acc,v); return Qnil; }
static VALUE enum_filter_map(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("filter_map"), 0, NULL);
  EnumCtx c={0}; c.blk=b; c.acc=rb_ary_new(); EACH(self,cb_filter_map,&c); return c.acc; }
static VALUE cb_uniq(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v=pack_yield(argc,argv);
  VALUE k = e->blk? rb_yield_values(e->blk,argc,argv) : v;
  if(rb_hash_lookup(e->aux,k,Qundef)==Qundef){ rb_hash_aset(e->aux,k,Qtrue); rb_ary_push(e->acc,v); }
  return Qnil; }
static VALUE enum_uniq(VALUE self, RProc *b){
  EnumCtx c={0}; c.blk=b; c.acc=rb_ary_new(); c.aux=rb_hash_new(); EACH(self,cb_uniq,&c); return c.acc; }
static VALUE cb_chunk_while(VALUE y, VALUE c, int argc, VALUE *argv){ (void)y;
  EnumCtx *e=(EnumCtx*)c; VALUE v=pack_yield(argc,argv);
  if(!e->flag){ e->aux = rb_ary_new(); rb_ary_push(e->aux,v); e->cmp=v; e->flag=1; return Qnil; }
  VALUE p[2] = { e->cmp, v };
  int cont = RTEST(rb_yield_values(e->blk,2,p));
  if(e->n) cont = !cont;
  if(cont) rb_ary_push(e->aux,v);
  else { rb_ary_push(e->acc,e->aux); e->aux=rb_ary_new(); rb_ary_push(e->aux,v); }
  e->cmp = v; return Qnil; }
static VALUE enum_chunk_while(VALUE self, RProc *b){
  EnumCtx c={0}; c.blk=b; c.acc=rb_ary_new(); EACH(self,cb_chunk_while,&c);
  if(c.flag) rb_ary_push(c.acc,c.aux); return c.acc; }
static VALUE enum_slice_when(VALUE self, RProc *b){
  EnumCtx c={0}; c.blk=b; c.acc=rb_ary_new(); c.n=1; EACH(self,cb_chunk_while,&c);
  if(c.flag) rb_ary_push(c.acc,c.aux); return c.acc; }
static VALUE enum_lazy(VALUE self, RProc *b){ (void)b;
  return rb_lazy_new(rb_enum_new(self, ID_CACHED("each"), 0, NULL)); }
static VALUE enum_each_dummy(VALUE self, RProc *b){ (void)b;
  rb_raise(rb_eNotImpError, "each is not implemented for %s", rb_obj_classname(self)); return Qnil; }
static VALUE enum_chain(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE a = enum_to_a(self,0,NULL,NULL);
  VALUE r = rb_ary_dup(a);
  for(int i=0;i<argc;i++){ VALUE o = rb_funcall(argv[i], ID_CACHED("to_a"), 0);
    for(long k=0;k<RARR(o)->len;k++) rb_ary_push(r,RARR(o)->ptr[k]); }
  return rb_enum_new(r, ID_CACHED("each"), 0, NULL); }
static VALUE enum_cycle(VALUE self, int argc, VALUE *argv, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("cycle"), argc, argv);
  VALUE a = enum_to_a(self,0,NULL,NULL);
  if(RARR(a)->len==0) return Qnil;
  long times = (argc>0 && !NIL_P(argv[0]))? rb_num2long(argv[0]) : -1;
  for(long t=0; times<0 || t<times; t++)
    for(long i=0;i<RARR(a)->len;i++) rb_yield1(b, RARR(a)->ptr[i]);
  return Qnil; }

ID to_mid_pub(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type==T_SYMBOL) return RSYM(v)->id;
  VALUE s = rb_str_to_str(v);
  return rb_intern2(RSTR(s)->ptr, RSTR(s)->len);
}

void rb_init_enum(void){
  RClass *E = rb_cEnumerable;
  rb_define_method(E, "to_a", enum_to_a, ARGC_ANY);
  rb_define_method(E, "entries", enum_entries, ARGC_ANY);
  rb_define_method(E, "map", enum_map, 0);
  rb_define_method(E, "collect", enum_map, 0);
  rb_define_method(E, "flat_map", enum_flat_map, 0);
  rb_define_method(E, "collect_concat", enum_flat_map, 0);
  rb_define_method(E, "select", enum_select, 0);
  rb_define_method(E, "find_all", enum_select, 0);
  rb_define_method(E, "reject", enum_reject, 0);
  rb_define_method(E, "each_with_index", enum_each_with_index, ARGC_ANY);
  rb_define_method(E, "each_with_object", enum_each_with_object, 1);
  rb_define_method(E, "inject", enum_inject, ARGC_ANY);
  rb_define_method(E, "reduce", enum_inject, ARGC_ANY);
  rb_define_method(E, "find", enum_find, ARGC_ANY);
  rb_define_method(E, "detect", enum_find, ARGC_ANY);
  rb_define_method(E, "find_index", enum_find_index, ARGC_ANY);
  rb_define_method(E, "all?", enum_all, ARGC_ANY);
  rb_define_method(E, "any?", enum_any, ARGC_ANY);
  rb_define_method(E, "none?", enum_none, ARGC_ANY);
  rb_define_method(E, "one?", enum_one, ARGC_ANY);
  rb_define_method(E, "count", enum_count, ARGC_ANY);
  rb_define_method(E, "include?", enum_include, 1);
  rb_define_method(E, "member?", enum_include, 1);
  rb_define_method(E, "min", enum_min, ARGC_ANY);
  rb_define_method(E, "max", enum_max, ARGC_ANY);
  rb_define_method(E, "minmax", enum_minmax, 0);
  rb_define_method(E, "min_by", enum_min_by, 0);
  rb_define_method(E, "max_by", enum_max_by, 0);
  rb_define_method(E, "minmax_by", enum_minmax_by, 0);
  rb_define_method(E, "sort", enum_sort, 0);
  rb_define_method(E, "sort_by", enum_sort_by, 0);
  rb_define_method(E, "group_by", enum_group_by, 0);
  rb_define_method(E, "partition", enum_partition, 0);
  rb_define_method(E, "take", enum_take, 1);
  rb_define_method(E, "take_while", enum_take_while, 0);
  rb_define_method(E, "drop", enum_drop, 1);
  rb_define_method(E, "drop_while", enum_drop_while, 0);
  rb_define_method(E, "each_slice", enum_each_slice, 1);
  rb_define_method(E, "each_cons", enum_each_cons, 1);
  rb_define_method(E, "zip", enum_zip, ARGC_ANY);
  rb_define_method(E, "first", enum_first, ARGC_ANY);
  rb_define_method(E, "each_entry", enum_each_entry, 0);
  rb_define_method(E, "reverse_each", enum_reverse_each, 0);
  rb_define_method(E, "cycle", enum_cycle, ARGC_ANY);
  rb_define_method(E, "each", enum_each_dummy, 0);
#if RUBY_TARGET >= 20
  rb_define_method(E, "to_h", enum_to_h, ARGC_ANY);
  rb_define_method(E, "lazy", enum_lazy, 0);
  rb_define_method(E, "chunk_while", enum_chunk_while, 0);
  rb_define_method(E, "slice_when", enum_slice_when, 0);
#endif
#if RUBY_TARGET >= 27
  rb_define_method(E, "sum", enum_sum, ARGC_ANY);
  rb_define_method(E, "tally", enum_tally, ARGC_ANY);
  rb_define_method(E, "filter", enum_select, 0);
  rb_define_method(E, "filter_map", enum_filter_map, 0);
  rb_define_method(E, "uniq", enum_uniq, 0);
  rb_define_method(E, "chain", enum_chain, ARGC_ANY);
#endif
}
