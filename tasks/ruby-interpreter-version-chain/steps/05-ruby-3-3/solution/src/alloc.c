#include "internal.h"

/* ===================== encodings ===================== */
static rb_encoding enc_bin_s = { "ASCII-8BIT", 0 };
static rb_encoding enc_asc_s = { "US-ASCII", 1 };
static rb_encoding enc_utf_s = { "UTF-8", 2 };
rb_encoding *enc_binary = &enc_bin_s, *enc_usascii = &enc_asc_s, *enc_utf8 = &enc_utf_s;

rb_encoding *rb_enc_find(const char *name){
  if(!name) return NULL;
  if(!strcasecmp(name,"UTF-8")||!strcasecmp(name,"utf8")) return enc_utf8;
  if(!strcasecmp(name,"US-ASCII")||!strcasecmp(name,"ASCII")||!strcasecmp(name,"ANSI_X3.4-1968")) return enc_usascii;
  if(!strcasecmp(name,"ASCII-8BIT")||!strcasecmp(name,"BINARY")) return enc_binary;
  return NULL;
}
long rb_enc_mbclen(rb_encoding *enc, const char *p, const char *e){
  if(enc != enc_utf8) return 1;
  unsigned char c = (unsigned char)*p;
  long n = (c < 0x80) ? 1 : (c < 0xC0) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : (c < 0xF8) ? 4 : 1;
  if(p + n > e) return 1;
  for(long i=1;i<n;i++) if(((unsigned char)p[i] & 0xC0) != 0x80) return 1;
  return n;
}
int rb_enc_codepoint(rb_encoding *enc, const char *p, const char *e, int *len){
  if(enc != enc_utf8){ if(len)*len=1; return (unsigned char)*p; }
  long n = rb_enc_mbclen(enc,p,e);
  if(len) *len = (int)n;
  unsigned char c = (unsigned char)*p;
  if(n==1) return c;
  int cp = c & (0xFF >> (n+1));
  for(long i=1;i<n;i++) cp = (cp<<6) | ((unsigned char)p[i] & 0x3F);
  return cp;
}
long rb_enc_codelen(rb_encoding *enc, int c){
  if(enc != enc_utf8) return 1;
  if(c < 0x80) return 1; if(c < 0x800) return 2; if(c < 0x10000) return 3; return 4;
}
long rb_enc_mbcput(rb_encoding *enc, int c, char *buf){
  if(enc != enc_utf8 || c < 0x80){ buf[0] = (char)c; return 1; }
  if(c < 0x800){ buf[0]=(char)(0xC0|(c>>6)); buf[1]=(char)(0x80|(c&0x3F)); return 2; }
  if(c < 0x10000){ buf[0]=(char)(0xE0|(c>>12)); buf[1]=(char)(0x80|((c>>6)&0x3F)); buf[2]=(char)(0x80|(c&0x3F)); return 3; }
  buf[0]=(char)(0xF0|(c>>18)); buf[1]=(char)(0x80|((c>>12)&0x3F));
  buf[2]=(char)(0x80|((c>>6)&0x3F)); buf[3]=(char)(0x80|(c&0x3F)); return 4;
}

/* ===================== allocation ===================== */
static void *obj_alloc(size_t sz, int type, RClass *k){
  RBasic *b = xcalloc(1,sz);
  b->type = (unsigned char)type; b->klass = k;
  return b;
}

VALUE rb_str_new_enc(const char *p, long len, rb_encoding *enc){
  RString *s = obj_alloc(sizeof(RString), T_STRING, rb_cString);
  s->len = len; s->cap = len+1;
  s->ptr = xmalloc(s->cap);
  if(p) memcpy(s->ptr,p,len);
  s->ptr[len] = 0;
  s->enc = enc;
  return (VALUE)s;
}
VALUE rb_str_new(const char *p, long len){
#if RUBY_TARGET < 20
  return rb_str_new_enc(p,len,enc_binary);
#else
  return rb_str_new_enc(p,len,enc_utf8);
#endif
}
VALUE rb_str_new_cstr(const char *p){ return rb_str_new(p, (long)strlen(p)); }
VALUE rb_str_dup(VALUE s){
  VALUE n = rb_str_new_enc(RSTR(s)->ptr, RSTR(s)->len, RSTR(s)->enc);
  BASIC(n)->klass = BASIC(s)->klass;
  return n;
}
VALUE rb_str_freeze(VALUE s){ BASIC(s)->frozen = 1; return s; }
VALUE rb_str_cat(VALUE str, const char *p, long len){
  RString *s = RSTR(str);
  if(s->len + len + 1 > s->cap){
    s->cap = (s->len + len + 1) * 2;
    s->ptr = xrealloc(s->ptr, s->cap);
  }
  memcpy(s->ptr + s->len, p, len);
  s->len += len; s->ptr[s->len]=0;
  return str;
}
VALUE rb_str_cat_cstr(VALUE str, const char *p){ return rb_str_cat(str,p,(long)strlen(p)); }
const char *rb_str_cstr(VALUE s){ return RSTR(s)->ptr; }

VALUE rb_ary_new_capa(long cap){
  RArray *a = obj_alloc(sizeof(RArray), T_ARRAY, rb_cArray);
  a->cap = cap>0?cap:4; a->len = 0; a->ptr = xmalloc(sizeof(VALUE)*a->cap);
  return (VALUE)a;
}
VALUE rb_ary_new(void){ return rb_ary_new_capa(4); }
VALUE rb_ary_new_from(long n, VALUE *v){
  VALUE a = rb_ary_new_capa(n>0?n:1);
  for(long i=0;i<n;i++) RARR(a)->ptr[i]=v[i];
  RARR(a)->len = n; return a;
}
void rb_ary_push(VALUE ary, VALUE v){
  RArray *a = RARR(ary);
  if(a->len >= a->cap){ a->cap = a->cap? a->cap*2 : 4; a->ptr = xrealloc(a->ptr,sizeof(VALUE)*a->cap); }
  a->ptr[a->len++] = v;
}
VALUE rb_ary_entry(VALUE ary, long i){
  RArray *a = RARR(ary);
  if(i<0) i += a->len;
  if(i<0 || i>=a->len) return Qnil;
  return a->ptr[i];
}
void rb_ary_store(VALUE ary, long i, VALUE v){
  RArray *a = RARR(ary);
  if(i<0){ i += a->len; if(i<0) rb_raise(rb_eIndexError,"index out of array"); }
  if(i >= a->cap){ long nc = a->cap? a->cap:4; while(nc<=i) nc*=2; a->ptr = xrealloc(a->ptr,sizeof(VALUE)*nc); a->cap=nc; }
  for(long k=a->len;k<i;k++) a->ptr[k]=Qnil;
  if(i >= a->len) a->len = i+1;
  a->ptr[i]=v;
}
VALUE rb_ary_dup(VALUE ary){
  VALUE n = rb_ary_new_from(RARR(ary)->len, RARR(ary)->ptr);
  BASIC(n)->klass = BASIC(ary)->klass;
  return n;
}

VALUE rb_float_new(double d){
  RFloat *f = obj_alloc(sizeof(RFloat), T_FLOAT, rb_cFloat);
  f->f = d; f->basic.frozen = 1;
  return (VALUE)f;
}
VALUE rb_int_new(long long v){
  if(v >= FIXNUM_MIN && v <= FIXNUM_MAX) return LONG2FIX((long)v);
  extern VALUE rb_big_from_ll(long long);
  return rb_big_from_ll(v);
}

static RSymbol **symtbl; static int symtbl_n;
VALUE rb_sym(ID id){
  if(id >= symtbl_n){
    int old = symtbl_n; symtbl_n = id+64;
    symtbl = xrealloc(symtbl, symtbl_n*sizeof(RSymbol*));
    for(int i=old;i<symtbl_n;i++) symtbl[i]=NULL;
  }
  if(!symtbl[id]){
    RSymbol *s = obj_alloc(sizeof(RSymbol), T_SYMBOL, rb_cSymbol);
    s->id = id; s->basic.frozen = 1;
    symtbl[id]=s;
  }
  return (VALUE)symtbl[id];
}
VALUE rb_sym_from_str(VALUE s){ return rb_sym(rb_intern2(RSTR(s)->ptr, RSTR(s)->len)); }

VALUE rb_range_new(VALUE b, VALUE e, int excl){
  if(!NIL_P(b) && !NIL_P(e)){
    VALUE c = rb_funcall(b, ID_CACHED("<=>"), 1, e);
    if(NIL_P(c)) rb_raise(rb_eArgError, "bad value for range");
  }
  RRange *r = obj_alloc(sizeof(RRange), T_RANGE, rb_cRange);
  r->beg=b; r->end=e; r->excl=excl; r->basic.frozen = 1;
  return (VALUE)r;
}

/* ===================== hash ===================== */
static long hash_of(VALUE v);
VALUE rb_hash_new(void){
  RHash *h = obj_alloc(sizeof(RHash), T_HASH, rb_cHash);
  h->ifnone = Qnil; h->cap = 0; h->n = 0; h->size = 0;
  return (VALUE)h;
}
static long hash_of(VALUE v){
  if(FIXNUM_P(v)) return (long)FIX2LONG(v) * 2654435761u;
  switch(BASIC(v)->type){
    case T_STRING: { unsigned long h=5381; RString *s=RSTR(v);
      for(long i=0;i<s->len;i++) h = h*33 + (unsigned char)s->ptr[i]; return (long)h; }
    case T_SYMBOL: return (long)(RSYM(v)->id * 2654435761u + 7);
    case T_NIL: return 11; case T_TRUE: return 13; case T_FALSE: return 17;
    case T_FLOAT: { double d = RFLT(v)->f; if(d == (double)(long)d) return (long)d * 2654435761u;
      unsigned long u; memcpy(&u,&d,sizeof u < sizeof d ? sizeof u : sizeof d); return (long)u; }
    case T_ARRAY: { unsigned long h=7; RArray *a=RARR(v);
      for(long i=0;i<a->len;i++) h = h*31 + (unsigned long)hash_of(a->ptr[i]); return (long)h; }
    default: {
      VALUE hv = rb_funcall(v, ID_CACHED("hash"), 0);
      if(FIXNUM_P(hv)) return FIX2LONG(hv);
      return (long)hv; }
  }
}
long rb_hash_code(VALUE v);
static int keys_eql(VALUE a, VALUE b){
  if(a==b) return 1;
  if(FIXNUM_P(a)||FIXNUM_P(b)) return 0;
  int ta = BASIC(a)->type, tb = BASIC(b)->type;
  if(ta != tb) return 0;
  if(ta==T_STRING){ RString *x=RSTR(a),*y=RSTR(b);
    return x->len==y->len && memcmp(x->ptr,y->ptr,x->len)==0 && x->enc==y->enc; }
  if(ta==T_FLOAT) return RFLT(a)->f==RFLT(b)->f;
  if(ta==T_ARRAY){ RArray *x=RARR(a),*y=RARR(b); if(x->len != y->len) return 0;
    for(long i=0;i<x->len;i++) if(!keys_eql(x->ptr[i],y->ptr[i])) return 0; return 1; }
  return RTEST(rb_funcall(a, ID_CACHED("eql?"), 1, b));
}
static void hash_reindex(RHash *h){
  long want = 16; while(want < h->n*2) want *= 2;
  h->idx = xrealloc(h->idx, want*sizeof(long));
  h->idxcap = want;
  memset(h->idx, 0, want*sizeof(long));
  for(long i=0;i<h->n;i++){
    if(!h->ent[i].used) continue;
    unsigned long p = (unsigned long)h->ent[i].hash & (want-1);
    while(h->idx[p]) p = (p+1)&(want-1);
    h->idx[p] = i+1;
  }
}
static long hash_find(RHash *h, VALUE key, long hv){
  if(!h->idxcap) return -1;
  unsigned long p = (unsigned long)hv & (h->idxcap-1);
  while(h->idx[p]){
    long i = h->idx[p]-1;
    if(h->ent[i].used && h->ent[i].hash==hv){
      if(h->compare_by_identity ? (h->ent[i].key==key) : keys_eql(h->ent[i].key,key)) return i;
    }
    p = (p+1)&(h->idxcap-1);
  }
  return -1;
}
void rb_hash_aset(VALUE hash, VALUE key, VALUE val){
  RHash *h = RHSH(hash);
  long hv = h->compare_by_identity ? (long)key : hash_of(key);
  long i = hash_find(h,key,hv);
  if(i>=0){ h->ent[i].val = val; return; }
  if(!FIXNUM_P(key) && BASIC(key)->type==T_STRING && !BASIC(key)->frozen){
    key = rb_str_freeze(rb_str_dup(key));
  }
  if(h->n >= h->cap){ h->cap = h->cap? h->cap*2 : 8; h->ent = xrealloc(h->ent, h->cap*sizeof(hentry)); }
  h->ent[h->n].key=key; h->ent[h->n].val=val; h->ent[h->n].hash=hv; h->ent[h->n].used=1;
  h->n++; h->size++;
  if(h->n*2 >= h->idxcap) hash_reindex(h); else {
    unsigned long p = (unsigned long)hv & (h->idxcap-1);
    while(h->idx[p]) p = (p+1)&(h->idxcap-1);
    h->idx[p] = h->n;
  }
}
VALUE rb_hash_lookup(VALUE hash, VALUE key, VALUE def){
  RHash *h = RHSH(hash);
  long hv = h->compare_by_identity ? (long)key : hash_of(key);
  long i = hash_find(h,key,hv);
  return i>=0 ? h->ent[i].val : def;
}
VALUE rb_hash_aref(VALUE hash, VALUE key){
  RHash *h = RHSH(hash);
  VALUE v = rb_hash_lookup(hash,key,Qundef);
  if(v != Qundef) return v;
  if(h->defproc) return rb_proc_call(h->defproc, 2, (VALUE[]){hash,key}, NULL);
  return h->ifnone;
}
int rb_hash_delete(VALUE hash, VALUE key, VALUE *out){
  RHash *h = RHSH(hash);
  long hv = h->compare_by_identity ? (long)key : hash_of(key);
  long i = hash_find(h,key,hv);
  if(i<0) return 0;
  if(out) *out = h->ent[i].val;
  h->ent[i].used = 0; h->size--;
  hash_reindex(h);
  return 1;
}

/* ===================== conversions ===================== */
long rb_num2long(VALUE v){
  if(FIXNUM_P(v)) return FIX2LONG(v);
  if(NIL_P(v)) rb_raise(rb_eTypeError, "no implicit conversion from nil to integer");
  if(BASIC(v)->type==T_FLOAT){
    double d = RFLT(v)->f;
    if(d != d || d > 9.2e18 || d < -9.2e18) rb_raise(rb_eFloatDomainError, "%g", d);
    return (long)d;
  }
  if(BASIC(v)->type==T_BIGNUM){ extern long rb_big2long(VALUE); return rb_big2long(v); }
  VALUE t = rb_check_to_int(v);
  if(NIL_P(t)) rb_raise(rb_eTypeError, "no implicit conversion of %s into Integer", rb_obj_classname(v));
  return rb_num2long(t);
}
double rb_num2dbl(VALUE v){
  if(FIXNUM_P(v)) return (double)FIX2LONG(v);
  if(BASIC(v)->type==T_FLOAT) return RFLT(v)->f;
  if(BASIC(v)->type==T_BIGNUM){ extern double rb_big2dbl(VALUE); return rb_big2dbl(v); }
  if(NIL_P(v)) rb_raise(rb_eTypeError, "can't convert nil into Float");
  VALUE t = rb_check_convert(v, ID_CACHED("to_f"));
  if(NIL_P(t)) rb_raise(rb_eTypeError, "can't convert %s into Float", rb_obj_classname(v));
  return rb_num2dbl(t);
}
VALUE rb_check_to_int(VALUE v){
  if(FIXNUM_P(v)) return v;
  if(BASIC(v)->type==T_BIGNUM) return v;
  if(!rb_respond_to(v, ID_CACHED("to_int"))) return Qnil;
  VALUE r = rb_funcall(v, ID_CACHED("to_int"), 0);
  if(!FIXNUM_P(r) && BASIC(r)->type != T_BIGNUM) return Qnil;
  return r;
}
VALUE rb_to_int(VALUE v){
  VALUE t = rb_check_to_int(v);
  if(NIL_P(t)) rb_raise(rb_eTypeError, "no implicit conversion of %s into Integer", NIL_P(v)?"nil":rb_obj_classname(v));
  return t;
}
VALUE rb_check_array_type(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type==T_ARRAY) return v;
  if(FIXNUM_P(v) || !rb_respond_to(v, ID_CACHED("to_ary"))) return Qnil;
  VALUE r = rb_funcall(v, ID_CACHED("to_ary"), 0);
  if(NIL_P(r)) return Qnil;
  if(FIXNUM_P(r) || BASIC(r)->type != T_ARRAY)
    rb_raise(rb_eTypeError, "can't convert %s to Array", rb_obj_classname(v));
  return r;
}
VALUE rb_ary_to_ary(VALUE v){
  VALUE a = rb_check_array_type(v);
  if(!NIL_P(a)) return a;
  VALUE r = rb_ary_new(); rb_ary_push(r,v); return r;
}
VALUE rb_check_string_type(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type==T_STRING) return v;
  if(FIXNUM_P(v) || !rb_respond_to(v, ID_CACHED("to_str"))) return Qnil;
  return rb_funcall(v, ID_CACHED("to_str"), 0);
}
VALUE rb_str_to_str(VALUE v){
  VALUE s = rb_check_string_type(v);
  if(NIL_P(s)) rb_raise(rb_eTypeError, "no implicit conversion of %s into String", NIL_P(v)?"nil":rb_obj_classname(v));
  return s;
}
VALUE rb_obj_as_string(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type==T_STRING) return v;
  VALUE s = rb_funcall(v, ID_CACHED("to_s"), 0);
  if(FIXNUM_P(s) || BASIC(s)->type != T_STRING) return rb_any_to_s(v);
  return s;
}
VALUE rb_inspect(VALUE v){
  VALUE s = rb_funcall(v, ID_CACHED("inspect"), 0);
  if(FIXNUM_P(s) || BASIC(s)->type != T_STRING) return rb_any_to_s(v);
  return s;
}
VALUE rb_sprintf_val(const char *fmt, ...){
  char buf[2048]; va_list ap; va_start(ap,fmt);
  vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  return rb_str_new_cstr(buf);
}
