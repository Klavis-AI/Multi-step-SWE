#include "internal.h"
extern VALUE rb_obj_alloc_of(RClass *);
extern RClass *rb_class_boot(RClass *);
extern void rb_const_set(RClass *, ID, VALUE);
extern ID to_mid_pub(VALUE);
extern VALUE rb_call_full(VALUE, ID, int, VALUE *, RProc *, int, int);

static ID id_members;
static VALUE struct_class_members(VALUE self, RProc *b);
static VALUE struct_klass_new(VALUE self, int argc, VALUE *argv, RProc *b);

static VALUE struct_members_of(VALUE self){
  RClass *c = FIXNUM_P(self)? NULL : (BASIC(self)->type==T_CLASS? (RClass*)self : rb_class_of(self));
  while(c){ int f; VALUE m = ivtbl_get(c->basic.iv, id_members, &f); if(f) return m; c = c->super; }
  rb_raise(rb_eTypeError, "not a struct"); return Qnil;
}
static VALUE struct_members(VALUE self, RProc *b){ (void)b; return rb_ary_dup(struct_members_of(self)); }
static VALUE struct_init(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE mem = struct_members_of(self);
  long n = RARR(mem)->len;
  RClass *c = rb_class_of(self);
  int kwinit = 0;
  { RClass *k=c; while(k){ int f; VALUE v = ivtbl_get(k->basic.iv, ID_CACHED("__kwinit__"), &f);
      if(f){ kwinit = RTEST(v); break; } k=k->super; } }
#if RUBY_TARGET >= 33
  /* 3.2 on: unless keyword_init: false was asked for, a lone Hash whose keys are
     all members initialises by keyword just as a positional list does */
  if(!kwinit && argc==1 && n>0 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_HASH){
    int f; VALUE given = Qnil; RClass *kk = c;
    while(kk){ VALUE v = ivtbl_get(kk->basic.iv, ID_CACHED("__kwinit__"), &f);
      if(f){ given = v; break; } kk = kk->super; }
    if(given != Qfalse){
      RHash *hh = RHSH(argv[0]); int all = hh->n > 0;
      for(long i=0;i<hh->n && all;i++) if(hh->ent[i].used){
        ID key = to_mid_pub(hh->ent[i].key); int found=0;
        for(long j=0;j<n;j++) if(RSYM(RARR(mem)->ptr[j])->id==key) found=1;
        if(!found) all = 0; }
      if(all) kwinit = 1; } }
#endif
  if(kwinit){
    if(argc>1) rb_raise(rb_eArgError,"wrong number of arguments (given %d, expected 0)",argc);
    VALUE h = argc? argv[0] : rb_hash_new();
    if(!FIXNUM_P(h) && BASIC(h)->type != T_HASH) rb_raise(rb_eArgError,"wrong number of arguments");
    RHash *hh = RHSH(h);
    for(long i=0;i<hh->n;i++) if(hh->ent[i].used){
      ID k = to_mid_pub(hh->ent[i].key);
      int found=0; for(long j=0;j<n;j++) if(RSYM(RARR(mem)->ptr[j])->id==k) found=1;
      if(!found) rb_raise(rb_eArgError,"unknown keywords: :%s", rb_id2name(k)); }
    for(long i=0;i<n;i++){ ID k = RSYM(RARR(mem)->ptr[i])->id;
      VALUE v = rb_hash_lookup(h, RARR(mem)->ptr[i], Qnil);
      rb_ivar_set(self, k, v); }
    return Qnil; }
  if(argc > n) rb_raise(rb_eArgError, "struct size differs");
  for(long i=0;i<n;i++) rb_ivar_set(self, RSYM(RARR(mem)->ptr[i])->id, i<argc? argv[i] : Qnil);
  return Qnil; }
static VALUE struct_to_a(VALUE self, RProc *b){ (void)b;
  VALUE mem = struct_members_of(self), a = rb_ary_new();
  for(long i=0;i<RARR(mem)->len;i++) rb_ary_push(a, rb_ivar_get(self, RSYM(RARR(mem)->ptr[i])->id));
  return a; }
static VALUE struct_to_h(VALUE self, RProc *b){
  VALUE mem = struct_members_of(self), h = rb_hash_new();
  for(long i=0;i<RARR(mem)->len;i++){
    VALUE k = RARR(mem)->ptr[i], v = rb_ivar_get(self, RSYM(k)->id);
    if(b){ VALUE p[2] = {k,v}; VALUE r = rb_yield_values(b,2,p);
      rb_hash_aset(h, rb_ary_entry(r,0), rb_ary_entry(r,1)); }
    else rb_hash_aset(h,k,v); }
  return h; }
static VALUE struct_each(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each"), 0, NULL);
  VALUE a = struct_to_a(self,NULL);
  for(long i=0;i<RARR(a)->len;i++) rb_yield1(b, RARR(a)->ptr[i]);
  return self; }
static VALUE struct_each_pair(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each_pair"), 0, NULL);
  VALUE mem = struct_members_of(self);
  for(long i=0;i<RARR(mem)->len;i++){
    VALUE p[2] = { RARR(mem)->ptr[i], rb_ivar_get(self,RSYM(RARR(mem)->ptr[i])->id) };
    rb_yield_values(b,2,p); }
  return self; }
static VALUE struct_aref(VALUE self, VALUE k, RProc *b){ (void)b;
  VALUE mem = struct_members_of(self);
  if(FIXNUM_P(k)){ long i = FIX2LONG(k); long n = RARR(mem)->len;
    if(i<0) i+=n;
    if(i<0||i>=n) rb_raise(rb_eIndexError,"offset %ld too large for struct(size:%ld)", FIX2LONG(k), n);
    return rb_ivar_get(self, RSYM(RARR(mem)->ptr[i])->id); }
  ID id = to_mid_pub(k);
  for(long i=0;i<RARR(mem)->len;i++) if(RSYM(RARR(mem)->ptr[i])->id==id) return rb_ivar_get(self,id);
  rb_raise(rb_eNameError, "no member '%s' in struct", rb_id2name(id)); return Qnil; }
static VALUE struct_aset(VALUE self, VALUE k, VALUE v, RProc *b){ (void)b;
  rb_check_frozen(self);
  VALUE mem = struct_members_of(self);
  if(FIXNUM_P(k)){ long i = FIX2LONG(k); long n = RARR(mem)->len;
    if(i<0) i+=n;
    if(i<0||i>=n) rb_raise(rb_eIndexError,"offset too large for struct");
    rb_ivar_set(self, RSYM(RARR(mem)->ptr[i])->id, v); return v; }
  ID id = to_mid_pub(k);
  for(long i=0;i<RARR(mem)->len;i++) if(RSYM(RARR(mem)->ptr[i])->id==id){ rb_ivar_set(self,id,v); return v; }
  rb_raise(rb_eNameError, "no member '%s' in struct", rb_id2name(id)); return Qnil; }
static VALUE struct_size(VALUE self, RProc *b){ (void)b; return LONG2FIX(RARR(struct_members_of(self))->len); }
static VALUE struct_eq(VALUE self, VALUE o, RProc *b){ (void)b;
  if(self==o) return Qtrue;
  if(FIXNUM_P(o) || rb_class_of(self) != rb_class_of(o)) return Qfalse;
  return rb_equal(struct_to_a(self,NULL), struct_to_a(o,NULL))?Qtrue:Qfalse; }
static VALUE struct_hash(VALUE self, RProc *b){ (void)b;
  return rb_funcall(struct_to_a(self,NULL), ID_CACHED("hash"), 0); }
static VALUE struct_inspect(VALUE self, RProc *b){ (void)b;
  RClass *c = rb_class_of(self);
  VALUE s = rb_str_new_cstr("#<struct ");
  if(c->name >= 0){ VALUE n = rb_class_name(c); rb_str_cat(s,RSTR(n)->ptr,RSTR(n)->len); rb_str_cat_cstr(s," "); }
  VALUE mem = struct_members_of(self);
  for(long i=0;i<RARR(mem)->len;i++){
    if(i) rb_str_cat_cstr(s,", ");
    rb_str_cat_cstr(s, rb_id2name(RSYM(RARR(mem)->ptr[i])->id));
    rb_str_cat_cstr(s,"=");
    VALUE v = rb_inspect(rb_ivar_get(self,RSYM(RARR(mem)->ptr[i])->id));
    rb_str_cat(s,RSTR(v)->ptr,RSTR(v)->len); }
  rb_str_cat_cstr(s,">"); return s; }
static VALUE struct_deconstruct(VALUE self, RProc *b){ return struct_to_a(self,b); }
static VALUE struct_deconstruct_keys(VALUE self, VALUE keys, RProc *b){ (void)b;(void)keys;
  return struct_to_h(self,NULL); }
static VALUE struct_dig(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE cur = struct_aref(self, argv[0], NULL);
  for(int i=1;i<argc && !NIL_P(cur);i++) cur = rb_funcall(cur, ID_CACHED("dig"), 1, argv[i]);
  return cur; }
static VALUE struct_values_at(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE a = struct_to_a(self,NULL);
  return rb_call(a, ID_CACHED("values_at"), argc, argv, NULL); }

#if RUBY_TARGET >= 31
static VALUE struct_class_kwinit_p(VALUE self, RProc *b){ (void)b;
  RClass *k = (RClass*)self;
  while(k){ int f; VALUE v = ivtbl_get(k->basic.iv, ID_CACHED("__kwinit__"), &f);
    if(f) return v; k = k->super; }
  return Qnil; }
#endif
static VALUE struct_new_class(VALUE self, int argc, VALUE *argv, RProc *b){
  (void)self;
  int start = 0;
  const char *cname = NULL;
  int kwinit = 0, kwgiven = 0;
  if(argc>0 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_STRING){ cname = RSTR(argv[0])->ptr; start = 1; }
  if(argc>0 && !FIXNUM_P(argv[argc-1]) && BASIC(argv[argc-1])->type==T_HASH){
    VALUE k = rb_hash_lookup(argv[argc-1], rb_sym(ID_CACHED("keyword_init")), Qundef);
    if(k != Qundef){ kwinit = RTEST(k); kwgiven = 1; argc--; } }
  VALUE mem = rb_ary_new();
  for(int i=start;i<argc;i++){
    if(FIXNUM_P(argv[i]) || (BASIC(argv[i])->type != T_SYMBOL && BASIC(argv[i])->type != T_STRING))
      rb_raise(rb_eTypeError, "%s is not a symbol nor a string", rb_str_cstr(rb_inspect(argv[i])));
    rb_ary_push(mem, rb_sym(to_mid_pub(argv[i]))); }
  RClass *c = rb_class_boot(rb_cStruct);
  c->basic.klass = rb_cClass; c->name = -1;
  RClass *csing = rb_singleton_class((VALUE)c);
  rb_define_method(csing, "new", struct_klass_new, ARGC_ANY);
  rb_define_method(csing, "[]", struct_klass_new, ARGC_ANY);
  rb_define_method(csing, "members", struct_class_members, 0);
#if RUBY_TARGET >= 31
  rb_define_method(csing, "keyword_init?", struct_class_kwinit_p, 0);
#endif
  ivtbl_set(&c->basic.iv, id_members, mem);
  /* nil, not false, when `keyword_init:` was never passed -- Struct.keyword_init?
     reports the three states apart */
  ivtbl_set(&c->basic.iv, ID_CACHED("__kwinit__"), kwgiven ? (kwinit?Qtrue:Qfalse) : Qnil);
  for(long i=0;i<RARR(mem)->len;i++){
    ID id = RSYM(RARR(mem)->ptr[i])->id;
    method_entry *r = xcalloc(1,sizeof(method_entry));
    r->type = ME_ATTR_READER; r->attr_id = id; r->visibility = VIS_PUBLIC;
    rb_add_method(c, id, r);
    char buf[256]; snprintf(buf,sizeof buf,"%s=",rb_id2name(id));
    method_entry *w = xcalloc(1,sizeof(method_entry));
    w->type = ME_ATTR_WRITER; w->attr_id = id; w->visibility = VIS_PUBLIC;
    rb_add_method(c, rb_intern(buf), w); }
  if(cname){ rb_const_set(rb_cStruct, rb_intern(cname), (VALUE)c);
    c->name = rb_intern(cname); c->lexical_parent = rb_cStruct; }
  if(b){ VALUE a1[1] = { (VALUE)c };
    RProc p = *b; p.self = (VALUE)c; p.defclass = c;
    cref_t *cr = xcalloc(1,sizeof(cref_t)); cr->klass = c; cr->next = (cref_t*)b->cref_list;
    p.cref_list = (struct rb_cref*)cr;
    rb_proc_invoke(&p,1,a1,NULL,0); }
  return (VALUE)c; }
static VALUE struct_klass_new(VALUE self, int argc, VALUE *argv, RProc *b){
  VALUE obj = rb_obj_alloc_of((RClass*)self);
  rb_call_full(obj, ID_CACHED("initialize"), argc, argv, b, 1, 0);
  return obj; }
static VALUE struct_class_members(VALUE self, RProc *b){ (void)b; return rb_ary_dup(struct_members_of(self)); }
static VALUE struct_class_sq(VALUE self, int argc, VALUE *argv, RProc *b){
  return rb_call(self, ID_CACHED("new"), argc, argv, b); }

#if RUBY_TARGET >= 33
/* ===================== Data (Ruby 3.2) ===================== */
/* A one-member Data still takes `new(x: 1)`, so a lone Hash is a keyword list
   when every one of its keys names a member. */
static int data_hash_is_kwargs(VALUE h, VALUE mem){
  RHash *hh = RHSH(h);
  if(hh->n == 0) return 0;
  long live = 0;
  for(long i = 0; i < hh->n; i++) if(hh->ent[i].used){
    live++;
    if(FIXNUM_P(hh->ent[i].key) || BASIC(hh->ent[i].key)->type != T_SYMBOL) return 0;
    ID k = RSYM(hh->ent[i].key)->id; int found = 0;
    for(long j = 0; j < RARR(mem)->len; j++)
      if(RSYM(RARR(mem)->ptr[j])->id == k) found = 1;
    if(!found) return 0; }
  return live > 0; }
static VALUE data_init(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE mem = struct_members_of(self);
  long n = RARR(mem)->len;
  if(argc==1 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_HASH
     && (n != 1 || data_hash_is_kwargs(argv[0], mem))){
    VALUE h = argv[0]; RHash *hh = RHSH(h);
    for(long i=0;i<hh->n;i++) if(hh->ent[i].used){
      ID k = to_mid_pub(hh->ent[i].key); int found=0;
      for(long j=0;j<n;j++) if(RSYM(RARR(mem)->ptr[j])->id==k) found=1;
      if(!found) rb_raise(rb_eArgError,"unknown keyword: :%s",rb_id2name(k)); }
    for(long i=0;i<n;i++){
      VALUE v = rb_hash_lookup(h, RARR(mem)->ptr[i], Qundef);
      if(v==Qundef) rb_raise(rb_eArgError,"missing keyword: :%s", rb_id2name(RSYM(RARR(mem)->ptr[i])->id));
      ivtbl_set(&BASIC(self)->iv, RSYM(RARR(mem)->ptr[i])->id, v); }
    BASIC(self)->frozen = 1; return Qnil; }
  if(argc != n) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %ld)", argc, n);
  for(long i=0;i<n;i++) ivtbl_set(&BASIC(self)->iv, RSYM(RARR(mem)->ptr[i])->id, argv[i]);
  BASIC(self)->frozen = 1; return Qnil; }
static VALUE data_new(VALUE self, int argc, VALUE *argv, RProc *b){
  VALUE mem = struct_members_of(self);
  long n = RARR(mem)->len;
  VALUE obj = rb_obj_alloc_of((RClass*)self);
  if(argc==1 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_HASH
     && (n != 1 || data_hash_is_kwargs(argv[0], mem))){
    rb_call_full(obj, ID_CACHED("initialize"), argc, argv, b, 1, 0); return obj; }
  if(argc > n) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..%ld)", argc, n);
  VALUE h = rb_hash_new();
  for(int i=0;i<argc;i++) rb_hash_aset(h, RARR(mem)->ptr[i], argv[i]);
  if(argc < n){
    for(long i=argc;i<n;i++) rb_raise(rb_eArgError,"missing keyword: :%s", rb_id2name(RSYM(RARR(mem)->ptr[i])->id)); }
  VALUE a1[1] = { h };
  if(n==1 && argc==1){ VALUE aa[1] = { argv[0] }; rb_call_full(obj, ID_CACHED("initialize"), 1, aa, b, 1, 0); return obj; }
  rb_call_full(obj, ID_CACHED("initialize"), 1, a1, b, 1, 0);
  return obj; }
static VALUE data_define(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  VALUE mem = rb_ary_new();
  for(int i=0;i<argc;i++){
    if(FIXNUM_P(argv[i]) || (BASIC(argv[i])->type != T_SYMBOL && BASIC(argv[i])->type != T_STRING))
      rb_raise(rb_eTypeError, "%s is not a symbol nor a string", rb_str_cstr(rb_inspect(argv[i])));
    rb_ary_push(mem, rb_sym(to_mid_pub(argv[i]))); }
  RClass *c = rb_class_boot(rb_cData);
  c->basic.klass = rb_cClass; c->name = -1;
  RClass *sc = rb_singleton_class((VALUE)c);
  ivtbl_set(&c->basic.iv, id_members, mem);
  for(long i=0;i<RARR(mem)->len;i++){
    ID id = RSYM(RARR(mem)->ptr[i])->id;
    method_entry *r = xcalloc(1,sizeof(method_entry));
    r->type = ME_ATTR_READER; r->attr_id = id; r->visibility = VIS_PUBLIC;
    rb_add_method(c, id, r); }
  rb_define_method(sc, "new", data_new, ARGC_ANY);
  rb_define_method(sc, "[]", data_new, ARGC_ANY);
  rb_define_method(sc, "members", struct_class_members, 0);
  if(b){ VALUE a1[1] = { (VALUE)c };
    RProc p = *b; p.self = (VALUE)c; p.defclass = c;
    cref_t *cr = xcalloc(1,sizeof(cref_t)); cr->klass = c; cr->next = (cref_t*)b->cref_list;
    p.cref_list = (struct rb_cref*)cr;
    rb_proc_invoke(&p,1,a1,NULL,0); }
  return (VALUE)c; }
static VALUE data_with(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE mem = struct_members_of(self);
  VALUE h = rb_hash_new();
  for(long i=0;i<RARR(mem)->len;i++) rb_hash_aset(h, RARR(mem)->ptr[i], rb_ivar_get(self,RSYM(RARR(mem)->ptr[i])->id));
  if(argc>0 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type==T_HASH){
    RHash *hh = RHSH(argv[0]);
    for(long i=0;i<hh->n;i++) if(hh->ent[i].used){
      ID k = to_mid_pub(hh->ent[i].key); int found=0;
      for(long j=0;j<RARR(mem)->len;j++) if(RSYM(RARR(mem)->ptr[j])->id==k) found=1;
      if(!found) rb_raise(rb_eArgError,"unknown keyword: :%s",rb_id2name(k));
      rb_hash_aset(h, hh->ent[i].key, hh->ent[i].val); } }
  VALUE a1[1] = { h };
  return rb_call((VALUE)rb_class_of(self), ID_CACHED("new"), 1, a1, NULL); }
static VALUE data_inspect(VALUE self, RProc *b){ (void)b;
  RClass *c = rb_class_of(self);
  VALUE s = rb_str_new_cstr("#<data ");
  if(c->name >= 0){ VALUE n = rb_class_name(c); rb_str_cat(s,RSTR(n)->ptr,RSTR(n)->len); rb_str_cat_cstr(s," "); }
  VALUE mem = struct_members_of(self);
  for(long i=0;i<RARR(mem)->len;i++){
    if(i) rb_str_cat_cstr(s,", ");
    rb_str_cat_cstr(s, rb_id2name(RSYM(RARR(mem)->ptr[i])->id)); rb_str_cat_cstr(s,"=");
    VALUE v = rb_inspect(rb_ivar_get(self,RSYM(RARR(mem)->ptr[i])->id));
    rb_str_cat(s,RSTR(v)->ptr,RSTR(v)->len); }
  rb_str_cat_cstr(s,">"); return s; }

/* ===================== Set ===================== */
static VALUE set_hash_of(VALUE self){ return rb_ivar_get(self, ID_CACHED("@hash")); }
static VALUE set_init(VALUE self, int argc, VALUE *argv, RProc *b){
  VALUE h = rb_hash_new();
  ivtbl_set(&BASIC(self)->iv, ID_CACHED("@hash"), h);
  if(argc>0 && !NIL_P(argv[0])){
    VALUE a = rb_funcall(argv[0], ID_CACHED("to_a"), 0);
    for(long i=0;i<RARR(a)->len;i++){
      VALUE v = RARR(a)->ptr[i];
      if(b) v = rb_yield1(b,v);
      rb_hash_aset(h, v, Qtrue); } }
  return self; }
static VALUE set_add(VALUE self, VALUE v, RProc *b){ (void)b;
  rb_check_frozen(self); rb_hash_aset(set_hash_of(self), v, Qtrue); return self; }
static VALUE set_add_q(VALUE self, VALUE v, RProc *b){ (void)b;
  VALUE h = set_hash_of(self);
  if(rb_hash_lookup(h,v,Qundef) != Qundef) return Qnil;
  rb_hash_aset(h,v,Qtrue); return self; }
static VALUE set_delete(VALUE self, VALUE v, RProc *b){ (void)b;
  rb_check_frozen(self); rb_hash_delete(set_hash_of(self), v, NULL); return self; }
static VALUE set_include(VALUE self, VALUE v, RProc *b){ (void)b;
  return rb_hash_lookup(set_hash_of(self),v,Qundef) != Qundef ? Qtrue : Qfalse; }
static VALUE set_size(VALUE self, RProc *b){ (void)b; return LONG2FIX(RHSH(set_hash_of(self))->size); }
static VALUE set_empty(VALUE self, RProc *b){ (void)b; return RHSH(set_hash_of(self))->size==0?Qtrue:Qfalse; }
static VALUE set_to_a(VALUE self, RProc *b){ (void)b;
  VALUE a = rb_ary_new(); RHash *h = RHSH(set_hash_of(self));
  for(long i=0;i<h->n;i++) if(h->ent[i].used) rb_ary_push(a,h->ent[i].key);
  return a; }
static VALUE set_each(VALUE self, RProc *b){
  if(!b) return rb_enum_new(self, ID_CACHED("each"), 0, NULL);
  VALUE a = set_to_a(self,NULL);
  for(long i=0;i<RARR(a)->len;i++) rb_yield1(b,RARR(a)->ptr[i]);
  return self; }
static VALUE set_new_from(VALUE a){
  VALUE s = rb_obj_alloc_of(rb_cSet);
  VALUE args[1] = { a };
  set_init(s,1,args,NULL); return s; }
static VALUE set_union(VALUE self, VALUE o, RProc *b){ (void)b;
  VALUE a = rb_ary_dup(set_to_a(self,NULL));
  VALUE oa = rb_funcall(o, ID_CACHED("to_a"), 0);
  for(long i=0;i<RARR(oa)->len;i++) rb_ary_push(a,RARR(oa)->ptr[i]);
  return set_new_from(a); }
static VALUE set_inter(VALUE self, VALUE o, RProc *b){ (void)b;
  VALUE oa = rb_funcall(o, ID_CACHED("to_a"), 0);
  VALUE os = set_new_from(oa);
  VALUE a = rb_ary_new(); VALUE mine = set_to_a(self,NULL);
  for(long i=0;i<RARR(mine)->len;i++) if(RTEST(set_include(os,RARR(mine)->ptr[i],NULL))) rb_ary_push(a,RARR(mine)->ptr[i]);
  return set_new_from(a); }
static VALUE set_diff(VALUE self, VALUE o, RProc *b){ (void)b;
  VALUE oa = rb_funcall(o, ID_CACHED("to_a"), 0);
  VALUE os = set_new_from(oa);
  VALUE a = rb_ary_new(); VALUE mine = set_to_a(self,NULL);
  for(long i=0;i<RARR(mine)->len;i++) if(!RTEST(set_include(os,RARR(mine)->ptr[i],NULL))) rb_ary_push(a,RARR(mine)->ptr[i]);
  return set_new_from(a); }
static VALUE set_xor(VALUE self, VALUE o, RProc *b){
  VALUE oa = rb_funcall(o, ID_CACHED("to_a"), 0);
  VALUE os = set_new_from(oa);
  return set_union(set_diff(self,os,b), set_diff(os,self,b), b); }
static VALUE set_eq(VALUE self, VALUE o, RProc *b){ (void)b;
  if(FIXNUM_P(o) || rb_class_of(o) != rb_cSet) return Qfalse;
  if(RHSH(set_hash_of(self))->size != RHSH(set_hash_of(o))->size) return Qfalse;
  VALUE mine = set_to_a(self,NULL);
  for(long i=0;i<RARR(mine)->len;i++) if(!RTEST(set_include(o,RARR(mine)->ptr[i],NULL))) return Qfalse;
  return Qtrue; }
static VALUE set_subset(VALUE self, VALUE o, RProc *b){ (void)b;
  VALUE mine = set_to_a(self,NULL);
  for(long i=0;i<RARR(mine)->len;i++) if(!RTEST(set_include(o,RARR(mine)->ptr[i],NULL))) return Qfalse;
  return Qtrue; }
static VALUE set_superset(VALUE self, VALUE o, RProc *b){ return set_subset(o,self,b); }
static VALUE set_merge(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  for(int i=0;i<argc;i++){ VALUE a = rb_funcall(argv[i], ID_CACHED("to_a"), 0);
    for(long k=0;k<RARR(a)->len;k++) rb_hash_aset(set_hash_of(self), RARR(a)->ptr[k], Qtrue); }
  return self; }
static VALUE set_inspect(VALUE self, RProc *b){ (void)b;
  VALUE a = set_to_a(self,NULL);
  VALUE s = rb_str_new_cstr("#<Set: {");
  for(long i=0;i<RARR(a)->len;i++){ if(i) rb_str_cat_cstr(s,", ");
    VALUE v = rb_inspect(RARR(a)->ptr[i]); rb_str_cat(s,RSTR(v)->ptr,RSTR(v)->len); }
  rb_str_cat_cstr(s,"}>"); return s; }
static VALUE set_hashv(VALUE self, RProc *b){ (void)b;
  VALUE a = set_to_a(self,NULL);
  long h = 0;
  for(long i=0;i<RARR(a)->len;i++) h ^= rb_hash_code(RARR(a)->ptr[i]);
  return LONG2FIX(h); }
static VALUE set_class_sq(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  return set_new_from(rb_ary_new_from(argc,argv)); }
#endif

#if RUBY_TARGET >= 31
/* ===================== Ractor (executed eagerly, semantics preserved) ===== */
static int node_refs_outer(Node *n, int depth){
  if(!n) return 0;
  if(n->type==N_LVAR || n->type==N_LASGN) if(n->num > depth) return 1;
  if(n->type==N_IVAR || n->type==N_IASGN) return 1;
  if(node_refs_outer(n->a,depth)||node_refs_outer(n->b,depth)||
     node_refs_outer(n->c,depth)||node_refs_outer(n->d,depth)) return 1;
  for(int i=0;i<n->nitems;i++) if(node_refs_outer(n->items[i],depth)) return 1;
  return 0;
}
static VALUE ractor_new(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;
  if(!b) rb_raise(rb_eArgError, "must be called with a block");
  if(node_refs_outer(b->body,0))
    /* CRuby raises ArgumentError here, not Ractor::IsolationError */
    rb_raise(rb_eArgError, "can not isolate a Proc because it accesses outer variables");
  RRactor *r = (RRactor*)xcalloc(1,sizeof(RRactor));
  r->basic.type = T_RACTOR; r->basic.klass = rb_cRactor;
  RProc p = *b; p.self = rb_obj_alloc_of(rb_cObject); p.env = NULL; p.is_lambda = 1;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf)==0){ r->value = rb_proc_invoke(&p, argc, argv, NULL, 1); TAG_POP(tag); }
  else { TAG_POP(tag);
    if(tag.state==TAG_RAISE){ r->err = tag.retval; r->value = Qnil; }
    else rb_jump_tag(tag.state,tag.retval,tag.target); }
  return (VALUE)r; }
static VALUE ractor_take(VALUE self, RProc *b){ (void)b;
  RRactor *r = (RRactor*)self;
  if(r->err){ VALUE e = rb_exc_new(rb_eRactorError, rb_str_new_cstr("thrown by remote Ractor."));
    ivtbl_set(&BASIC(e)->iv, ID_CACHED("@__cause__"), r->err); rb_exc_raise(e); }
  return r->value; }
static VALUE ractor_value(VALUE self, RProc *b){ return ractor_take(self,b); }
static int shareable_p(VALUE v){
  if(FIXNUM_P(v)) return 1;
  switch(BASIC(v)->type){
    case T_NIL: case T_TRUE: case T_FALSE: case T_SYMBOL: case T_FLOAT:
    case T_CLASS: case T_MODULE: case T_BIGNUM: return 1;
    case T_STRING: return BASIC(v)->frozen;
    case T_ARRAY: { if(!BASIC(v)->frozen) return 0;
      for(long i=0;i<RARR(v)->len;i++) if(!shareable_p(RARR(v)->ptr[i])) return 0; return 1; }
    case T_HASH: { if(!BASIC(v)->frozen) return 0;
      RHash *h = RHSH(v);
      for(long i=0;i<h->n;i++) if(h->ent[i].used && (!shareable_p(h->ent[i].key)||!shareable_p(h->ent[i].val))) return 0;
      return 1; }
    default: return BASIC(v)->frozen && !BASIC(v)->iv; }
}
static VALUE ractor_shareable(VALUE self, VALUE v, RProc *b){ (void)self;(void)b; return shareable_p(v)?Qtrue:Qfalse; }
static VALUE make_shareable(VALUE v){
  if(FIXNUM_P(v)) return v;
  if(shareable_p(v)) return v;
  switch(BASIC(v)->type){
    case T_ARRAY: for(long i=0;i<RARR(v)->len;i++) make_shareable(RARR(v)->ptr[i]); break;
    case T_HASH: { RHash *h=RHSH(v);
      for(long i=0;i<h->n;i++) if(h->ent[i].used){ make_shareable(h->ent[i].key); make_shareable(h->ent[i].val); } break; }
    default: break; }
  BASIC(v)->frozen = 1;
  if(BASIC(v)->iv){ ivtbl *t = BASIC(v)->iv; for(int i=0;i<t->n;i++) make_shareable(t->vals[i]); }
  return v; }
static VALUE ractor_make_shareable(VALUE self, int argc, VALUE *argv, RProc *b){ (void)self;(void)b;
  if(argc<1) rb_raise(rb_eArgError,"wrong number of arguments");
  return make_shareable(argv[0]); }
static VALUE ractor_current(VALUE self, RProc *b){ (void)b;(void)self;
  static VALUE cur = 0;
  if(!cur){ RRactor *r = (RRactor*)xcalloc(1,sizeof(RRactor));
    r->basic.type = T_RACTOR; r->basic.klass = rb_cRactor; r->value = Qnil; cur = (VALUE)r; }
  return cur; }
#endif

void rb_init_struct(void){
  id_members = ID_CACHED("__members__");
  RClass *S = rb_cStruct;
  rb_define_method(S, "initialize", struct_init, ARGC_ANY);
  rb_define_method(S, "members", struct_members, 0);
  rb_define_method(S, "to_a", struct_to_a, 0);
  rb_define_method(S, "values", struct_to_a, 0);
  rb_define_method(S, "deconstruct", struct_deconstruct, 0);
  rb_define_method(S, "each", struct_each, 0);
  rb_define_method(S, "each_pair", struct_each_pair, 0);
  rb_define_method(S, "[]", struct_aref, 1);
  rb_define_method(S, "[]=", struct_aset, 2);
  rb_define_method(S, "size", struct_size, 0);
  rb_define_method(S, "length", struct_size, 0);
  rb_define_method(S, "==", struct_eq, 1);
  rb_define_method(S, "eql?", struct_eq, 1);
  rb_define_method(S, "hash", struct_hash, 0);
  rb_define_method(S, "inspect", struct_inspect, 0);
  rb_define_method(S, "to_s", struct_inspect, 0);
  rb_define_method(S, "values_at", struct_values_at, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cStruct, "new", struct_new_class, ARGC_ANY);
#if RUBY_TARGET >= 20
  rb_define_method(S, "to_h", struct_to_h, 0);
#endif
#if RUBY_TARGET >= 27
  rb_define_method(S, "dig", struct_dig, ARGC_ANY);
  rb_define_method(S, "deconstruct_keys", struct_deconstruct_keys, 1);
#endif
  (void)struct_class_sq;
#if RUBY_TARGET >= 33
  rb_define_singleton_method((VALUE)rb_cData, "define", data_define, ARGC_ANY);
  rb_define_method(rb_cData, "initialize", data_init, ARGC_ANY);
  rb_define_method(rb_cData, "members", struct_members, 0);
  rb_define_method(rb_cData, "to_h", struct_to_h, 0);
  rb_define_method(rb_cData, "with", data_with, ARGC_ANY);
  rb_define_method(rb_cData, "==", struct_eq, 1);
  rb_define_method(rb_cData, "eql?", struct_eq, 1);
  rb_define_method(rb_cData, "hash", struct_hash, 0);
  rb_define_method(rb_cData, "inspect", data_inspect, 0);
  rb_define_method(rb_cData, "to_s", data_inspect, 0);
  rb_define_method(rb_cData, "deconstruct", struct_deconstruct, 0);
  rb_define_method(rb_cData, "deconstruct_keys", struct_deconstruct_keys, 1);

  rb_cSet = rb_define_class("Set", rb_cObject);
  rb_include_module(rb_cSet, rb_cEnumerable);
  rb_define_method(rb_cSet, "initialize", set_init, ARGC_ANY);
  rb_define_method(rb_cSet, "add", set_add, 1);
  rb_define_method(rb_cSet, "<<", set_add, 1);
  rb_define_method(rb_cSet, "add?", set_add_q, 1);
  rb_define_method(rb_cSet, "delete", set_delete, 1);
  rb_define_method(rb_cSet, "include?", set_include, 1);
  rb_define_method(rb_cSet, "member?", set_include, 1);
  rb_define_method(rb_cSet, "===", set_include, 1);
  rb_define_method(rb_cSet, "size", set_size, 0);
  rb_define_method(rb_cSet, "length", set_size, 0);
  rb_define_method(rb_cSet, "count", set_size, 0);
  rb_define_method(rb_cSet, "empty?", set_empty, 0);
  rb_define_method(rb_cSet, "to_a", set_to_a, 0);
  rb_define_method(rb_cSet, "each", set_each, 0);
  rb_define_method(rb_cSet, "|", set_union, 1);
  rb_define_method(rb_cSet, "+", set_union, 1);
  rb_define_method(rb_cSet, "union", set_union, 1);
  rb_define_method(rb_cSet, "&", set_inter, 1);
  rb_define_method(rb_cSet, "intersection", set_inter, 1);
  rb_define_method(rb_cSet, "-", set_diff, 1);
  rb_define_method(rb_cSet, "difference", set_diff, 1);
  rb_define_method(rb_cSet, "^", set_xor, 1);
  rb_define_method(rb_cSet, "==", set_eq, 1);
  rb_define_method(rb_cSet, "subset?", set_subset, 1);
  rb_define_method(rb_cSet, "<=", set_subset, 1);
  rb_define_method(rb_cSet, "superset?", set_superset, 1);
  rb_define_method(rb_cSet, ">=", set_superset, 1);
  rb_define_method(rb_cSet, "merge", set_merge, ARGC_ANY);
  rb_define_method(rb_cSet, "inspect", set_inspect, 0);
  rb_define_method(rb_cSet, "to_s", set_inspect, 0);
  rb_define_method(rb_cSet, "hash", set_hashv, 0);
  rb_define_singleton_method((VALUE)rb_cSet, "[]", set_class_sq, ARGC_ANY);
#endif
#if RUBY_TARGET >= 31
  rb_define_singleton_method((VALUE)rb_cRactor, "new", ractor_new, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cRactor, "shareable?", ractor_shareable, 1);
  rb_define_singleton_method((VALUE)rb_cRactor, "make_shareable", ractor_make_shareable, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cRactor, "current", ractor_current, 0);
  rb_define_method(rb_cRactor, "take", ractor_take, 0);
  rb_define_method(rb_cRactor, "value", ractor_value, 0);
#endif
}
