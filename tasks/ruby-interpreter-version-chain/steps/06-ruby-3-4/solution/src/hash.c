/* hash.c -- Ruby's Hash class */
#include "internal.h"
#include <ctype.h>

extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);

static ID id_each, id_to_hash;

static RHash *hash_ptr(VALUE v){
  if(FIXNUM_P(v) || BASIC(v)->type != T_HASH)
    rb_raise(rb_eTypeError, "wrong argument type %s (expected Hash)", rb_obj_classname(v));
  return RHSH(v);
}
static VALUE pair_new(VALUE k, VALUE v){
  VALUE a = rb_ary_new_capa(2);
  RARR(a)->ptr[0] = k; RARR(a)->ptr[1] = v; RARR(a)->len = 2;
  return a;
}
static VALUE to_hash_arg(VALUE v){
  if(!FIXNUM_P(v) && BASIC(v)->type == T_HASH) return v;
  VALUE r = FIXNUM_P(v) ? Qnil : rb_check_convert(v, id_to_hash);
  if(NIL_P(r) || FIXNUM_P(r) || BASIC(r)->type != T_HASH)
    rb_raise(rb_eTypeError, "no implicit conversion of %s into Hash",
             NIL_P(v) ? "nil" : rb_obj_classname(v));
  return r;
}

/* ---- index maintenance mirroring alloc.c, for compare_by_identity ---- */
static void h_reindex(RHash *h){
  long want = 16;
  while(want < h->n * 2) want *= 2;
  h->idx = xrealloc(h->idx, (size_t)want * sizeof(long));
  h->idxcap = want;
  memset(h->idx, 0, (size_t)want * sizeof(long));
  for(long i = 0; i < h->n; i++){
    if(!h->ent[i].used) continue;
    unsigned long p = (unsigned long)h->ent[i].hash & (unsigned long)(want - 1);
    while(h->idx[p]) p = (p + 1) & (unsigned long)(want - 1);
    h->idx[p] = i + 1;
  }
}
/* identity insert: unlike rb_hash_aset this never copies a String key */
static void h_aset_ident(VALUE hash, VALUE key, VALUE val){
  RHash *h = RHSH(hash);
  long hv = (long)key;
  if(h->idxcap){
    unsigned long p = (unsigned long)hv & (unsigned long)(h->idxcap - 1);
    while(h->idx[p]){
      long i = h->idx[p] - 1;
      if(h->ent[i].used && h->ent[i].key == key){ h->ent[i].val = val; return; }
      p = (p + 1) & (unsigned long)(h->idxcap - 1);
    }
  }
  if(h->n >= h->cap){ h->cap = h->cap ? h->cap * 2 : 8; h->ent = xrealloc(h->ent, (size_t)h->cap * sizeof(hentry)); }
  h->ent[h->n].key = key; h->ent[h->n].val = val; h->ent[h->n].hash = hv; h->ent[h->n].used = 1;
  h->n++; h->size++;
  if(h->n * 2 >= h->idxcap) h_reindex(h);
  else {
    unsigned long p = (unsigned long)hv & (unsigned long)(h->idxcap - 1);
    while(h->idx[p]) p = (p + 1) & (unsigned long)(h->idxcap - 1);
    h->idx[p] = h->n;
  }
}
static void h_set(VALUE hash, VALUE key, VALUE val){
  if(RHSH(hash)->compare_by_identity) h_aset_ident(hash, key, val);
  else rb_hash_aset(hash, key, val);
}
static void h_clear(RHash *h){
  h->n = 0; h->size = 0;
  if(h->idxcap) memset(h->idx, 0, (size_t)h->idxcap * sizeof(long));
}

/* ---- recursion guards ---- */
typedef struct { VALUE v[64]; int sp; } recst;
static recst insp_rec, hash_rec, eq_rec;
static int rec_enter(recst *r, VALUE v){
  for(int i = 0; i < r->sp; i++) if(r->v[i] == v) return 0;
  if(r->sp >= 64) return 0;
  r->v[r->sp++] = v;
  return 1;
}
static void rec_leave(recst *r){ if(r->sp) r->sp--; }

/* ===================== construction ===================== */

static VALUE hash_initialize(VALUE self, int argc, VALUE *argv, RProc *blk){
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  if(blk){
    if(argc > 0) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0)", argc);
    h->defproc = blk;
  } else if(argc == 1){
    h->ifnone = argv[0];
  } else if(argc > 1){
    rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  }
  return self;
}
static VALUE hash_s_new(VALUE klass, int argc, VALUE *argv, RProc *blk){
  VALUE h = rb_hash_new();
  BASIC(h)->klass = (RClass*)klass;
  rb_call(h, ID_CACHED("initialize"), argc, argv, blk);
  return h;
}
static VALUE hash_s_create(VALUE klass, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  VALUE h = rb_hash_new();
  BASIC(h)->klass = (RClass*)klass;
  if(argc == 1){
    VALUE a = argv[0];
    if(!FIXNUM_P(a) && BASIC(a)->type == T_HASH){
      RHash *s = RHSH(a);
      for(long i = 0; i < s->n; i++) if(s->ent[i].used) h_set(h, s->ent[i].key, s->ent[i].val);
      return h;
    }
    VALUE ar = FIXNUM_P(a) ? Qnil : rb_check_array_type(a);
    if(!NIL_P(ar)){
      RArray *arr = RARR(ar);
      for(long i = 0; i < arr->len; i++){
        VALUE e = FIXNUM_P(arr->ptr[i]) ? Qnil : rb_check_array_type(arr->ptr[i]);
        if(NIL_P(e)) continue;
        long n = RARR(e)->len;
        if(n < 1 || n > 2) continue;
        h_set(h, RARR(e)->ptr[0], n == 2 ? RARR(e)->ptr[1] : Qnil);
      }
      return h;
    }
  }
  if(argc % 2) rb_raise(rb_eArgError, "odd number of arguments for Hash");
  for(int i = 0; i < argc; i += 2) h_set(h, argv[i], argv[i+1]);
  return h;
}

/* ===================== element access ===================== */

static VALUE hash_aref(VALUE self, VALUE key, RProc *b){ (void)b;
  hash_ptr(self); return rb_hash_aref(self, key); }
static VALUE hash_aset(VALUE self, VALUE key, VALUE val, RProc *b){ (void)b;
  hash_ptr(self); rb_check_frozen(self); h_set(self, key, val); return val; }
static VALUE hash_fetch(VALUE self, int argc, VALUE *argv, RProc *blk){
  hash_ptr(self);
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE v = rb_hash_lookup(self, argv[0], Qundef);
  if(v != Qundef) return v;
  if(blk) return rb_yield1(blk, argv[0]);
  if(argc == 2) return argv[1];
  VALUE exc = rb_exc_new(rb_eKeyError,
    rb_sprintf_val("key not found: %s", rb_str_cstr(rb_inspect(argv[0]))));
  rb_ivar_set(exc, ID_CACHED("@key"), argv[0]);
  rb_ivar_set(exc, ID_CACHED("@receiver"), self);
  rb_exc_raise(exc);
  return Qnil;
}
static VALUE hash_default(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 1 && h->defproc) return rb_proc_call(h->defproc, 2, (VALUE[]){self, argv[0]}, NULL);
  return h->ifnone; }
static VALUE hash_set_default(VALUE self, VALUE v, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  h->defproc = NULL; h->ifnone = v;
  return v; }
static VALUE hash_default_proc(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  return h->defproc ? (VALUE)h->defproc : Qnil; }
static VALUE hash_set_default_proc(VALUE self, VALUE v, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  if(NIL_P(v)){ h->defproc = NULL; return v; }
  if(FIXNUM_P(v) || BASIC(v)->type != T_PROC)
    rb_raise(rb_eTypeError, "wrong default_proc type %s (expected Proc)", rb_obj_classname(v));
  h->defproc = RPRC(v); h->ifnone = Qnil;
  return v; }

static VALUE hash_size(VALUE self, RProc *b){ (void)b; return LONG2FIX(hash_ptr(self)->size); }
static VALUE hash_empty_p(VALUE self, RProc *b){ (void)b; return hash_ptr(self)->size == 0 ? Qtrue : Qfalse; }

static VALUE hash_keys(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  VALUE a = rb_ary_new_capa(h->size ? h->size : 1);
  for(long i = 0; i < h->n; i++) if(h->ent[i].used) rb_ary_push(a, h->ent[i].key);
  return a; }
static VALUE hash_values(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  VALUE a = rb_ary_new_capa(h->size ? h->size : 1);
  for(long i = 0; i < h->n; i++) if(h->ent[i].used) rb_ary_push(a, h->ent[i].val);
  return a; }
static VALUE hash_to_a(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  VALUE a = rb_ary_new_capa(h->size ? h->size : 1);
  for(long i = 0; i < h->n; i++) if(h->ent[i].used) rb_ary_push(a, pair_new(h->ent[i].key, h->ent[i].val));
  return a; }
static VALUE hash_to_hash(VALUE self, RProc *b){ (void)b; hash_ptr(self); return self; }

static VALUE hash_has_key(VALUE self, VALUE k, RProc *b){ (void)b;
  hash_ptr(self); return rb_hash_lookup(self, k, Qundef) != Qundef ? Qtrue : Qfalse; }
static VALUE hash_has_value(VALUE self, VALUE v, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  for(long i = 0; i < h->n; i++) if(h->ent[i].used && rb_equal(h->ent[i].val, v)) return Qtrue;
  return Qfalse; }
static VALUE hash_key(VALUE self, VALUE v, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  for(long i = 0; i < h->n; i++) if(h->ent[i].used && rb_equal(h->ent[i].val, v)) return h->ent[i].key;
  return Qnil; }
static VALUE hash_invert(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  VALUE r = rb_hash_new();
  for(long i = 0; i < h->n; i++) if(h->ent[i].used) h_set(r, h->ent[i].val, h->ent[i].key);
  return r; }
static VALUE hash_assoc(VALUE self, VALUE k, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  for(long i = 0; i < h->n; i++){
    if(!h->ent[i].used) continue;
    if(h->compare_by_identity ? h->ent[i].key == k : rb_eql(h->ent[i].key, k))
      return pair_new(h->ent[i].key, h->ent[i].val);
  }
  return Qnil; }
static VALUE hash_rassoc(VALUE self, VALUE v, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  for(long i = 0; i < h->n; i++)
    if(h->ent[i].used && rb_equal(h->ent[i].val, v)) return pair_new(h->ent[i].key, h->ent[i].val);
  return Qnil; }
static VALUE hash_flatten(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE a = hash_to_a(self, NULL);
  long level = 1;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 1) level = rb_num2long(rb_to_int(argv[0]));
  if(level == 0) return a;
  return rb_funcall(a, ID_CACHED("flatten"), 1, LONG2FIX(level)); }
static VALUE hash_values_at(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  hash_ptr(self);
  VALUE a = rb_ary_new_capa(argc ? argc : 1);
  for(int i = 0; i < argc; i++) rb_ary_push(a, rb_hash_aref(self, argv[i]));
  return a; }

/* ===================== iteration ===================== */

static VALUE hash_each(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, id_each, 0, NULL);
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    rb_yield1(blk, pair_new(h->ent[i].key, h->ent[i].val));
  }
  return self;
}
static VALUE hash_each_key(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("each_key"), 0, NULL);
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++) if(h->ent[i].used) rb_yield1(blk, h->ent[i].key);
  return self;
}
static VALUE hash_each_value(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("each_value"), 0, NULL);
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++) if(h->ent[i].used) rb_yield1(blk, h->ent[i].val);
  return self;
}
static VALUE hash_count(VALUE self, int argc, VALUE *argv, RProc *blk){
  RHash *h = hash_ptr(self);
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 0 && !blk) return LONG2FIX(h->size);
  long c = 0, n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE p = pair_new(h->ent[i].key, h->ent[i].val);
    if(argc == 1){ if(rb_equal(p, argv[0])) c++; }
    else if(RTEST(rb_yield1(blk, p))) c++;
  }
  return LONG2FIX(c);
}
/* kind: 0 any?, 1 all?, 2 none? */
static VALUE hash_quantify(VALUE self, int argc, VALUE *argv, RProc *blk, int kind){
  RHash *h = hash_ptr(self);
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 0 && !blk){
    if(kind == 0) return h->size > 0 ? Qtrue : Qfalse;
    if(kind == 2) return h->size > 0 ? Qfalse : Qtrue;
    return Qtrue;
  }
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE p = pair_new(h->ent[i].key, h->ent[i].val);
    int t = argc == 1 ? RTEST(rb_funcall(argv[0], ID_CACHED("==="), 1, p))
                      : RTEST(rb_yield1(blk, p));
    if(kind == 0 && t) return Qtrue;
    if(kind == 1 && !t) return Qfalse;
    if(kind == 2 && t) return Qfalse;
  }
  return kind == 0 ? Qfalse : Qtrue;
}
static VALUE hash_any(VALUE self, int argc, VALUE *argv, RProc *blk){ return hash_quantify(self, argc, argv, blk, 0); }
static VALUE hash_all(VALUE self, int argc, VALUE *argv, RProc *blk){ return hash_quantify(self, argc, argv, blk, 1); }
static VALUE hash_none(VALUE self, int argc, VALUE *argv, RProc *blk){ return hash_quantify(self, argc, argv, blk, 2); }
static VALUE hash_find(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("find"), 0, NULL);
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE p = pair_new(h->ent[i].key, h->ent[i].val);
    if(RTEST(rb_yield1(blk, p))) return p;
  }
  return Qnil;
}

/* ===================== mutation ===================== */

static VALUE hash_delete(VALUE self, VALUE k, RProc *blk){
  hash_ptr(self);
  rb_check_frozen(self);
  VALUE out;
  if(rb_hash_delete(self, k, &out)) return out;
  if(blk) return rb_yield1(blk, k);
  return Qnil;
}
static VALUE hash_clear(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  h_clear(h);
  return self; }
static VALUE hash_shift(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  for(long i = 0; i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE k = h->ent[i].key, v = h->ent[i].val;
    rb_hash_delete(self, k, NULL);
    return pair_new(k, v);
  }
#if RUBY_TARGET >= 31
  return Qnil;
#else
  return h->defproc ? rb_proc_call(h->defproc, 2, (VALUE[]){self, Qnil}, NULL) : h->ifnone;
#endif
}
static VALUE hash_replace(VALUE self, VALUE other, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  VALUE o = to_hash_arg(other);
  if(o == self) return self;
  RHash *s = RHSH(o);
  h_clear(h);
  h->compare_by_identity = s->compare_by_identity;
  for(long i = 0; i < s->n; i++) if(s->ent[i].used) h_set(self, s->ent[i].key, s->ent[i].val);
  h->ifnone = s->ifnone; h->defproc = s->defproc;
  return self; }

/* keep: 0 reject-style (drop where block is true), 1 keep-style; bang: nil when nothing changed */
static VALUE hash_filter_bang(VALUE self, RProc *blk, int keep, int bang, const char *nm){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, rb_intern(nm), 0, NULL);
  rb_check_frozen(self);
  long removed = 0, n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE k = h->ent[i].key;
    int t = RTEST(rb_yield1(blk, pair_new(k, h->ent[i].val)));
    if(keep ? !t : t){ if(rb_hash_delete(self, k, NULL)) removed++; }
  }
  if(bang && removed == 0) return Qnil;
  return self;
}
static VALUE hash_delete_if(VALUE self, RProc *blk){ return hash_filter_bang(self, blk, 0, 0, "delete_if"); }
static VALUE hash_reject_bang(VALUE self, RProc *blk){ return hash_filter_bang(self, blk, 0, 1, "reject!"); }
static VALUE hash_keep_if(VALUE self, RProc *blk){ return hash_filter_bang(self, blk, 1, 0, "keep_if"); }
static VALUE hash_select_bang(VALUE self, RProc *blk){ return hash_filter_bang(self, blk, 1, 1, "select!"); }

static VALUE hash_reject(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("reject"), 0, NULL);
  VALUE r = rb_hash_new();
  RHSH(r)->compare_by_identity = h->compare_by_identity;
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE k = h->ent[i].key, v = h->ent[i].val;
    if(!RTEST(rb_yield1(blk, pair_new(k, v)))) h_set(r, k, v);
  }
  return r;
}
static VALUE hash_select(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("select"), 0, NULL);
#if RUBY_TARGET < 20
  /* 1.8's Hash#select answered an Array of pairs */
  VALUE r = rb_ary_new();
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE p = pair_new(h->ent[i].key, h->ent[i].val);
    if(RTEST(rb_yield1(blk, p))) rb_ary_push(r, p);
  }
#else
  VALUE r = rb_hash_new();
  RHSH(r)->compare_by_identity = h->compare_by_identity;
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE k = h->ent[i].key, v = h->ent[i].val;
    if(RTEST(rb_yield1(blk, pair_new(k, v)))) h_set(r, k, v);
  }
#endif
  return r;
}

static VALUE hash_dup_of(VALUE self){
  RHash *h = RHSH(self);
  VALUE n = rb_hash_new();
  BASIC(n)->klass = BASIC(self)->klass;
  RHSH(n)->compare_by_identity = h->compare_by_identity;
  for(long i = 0; i < h->n; i++) if(h->ent[i].used) h_set(n, h->ent[i].key, h->ent[i].val);
  RHSH(n)->ifnone = h->ifnone;
  RHSH(n)->defproc = h->defproc;
  return n;
}
static VALUE hash_merge_into(VALUE dst, int argc, VALUE *argv, RProc *blk){
  for(int j = 0; j < argc; j++){
    VALUE o = to_hash_arg(argv[j]);
    RHash *s = RHSH(o);
    long n = s->n;
    for(long i = 0; i < n && i < s->n; i++){
      if(!s->ent[i].used) continue;
      VALUE k = s->ent[i].key, v = s->ent[i].val;
      if(blk){
        VALUE old = rb_hash_lookup(dst, k, Qundef);
        if(old != Qundef) v = rb_yield_values(blk, 3, (VALUE[]){k, old, v});
      }
      h_set(dst, k, v);
    }
  }
  return dst;
}
static void merge_arity(int argc){
#if RUBY_TARGET < 27
  if(argc != 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1)", argc);
#else
  (void)argc;
#endif
}
static VALUE hash_merge(VALUE self, int argc, VALUE *argv, RProc *blk){
  hash_ptr(self); merge_arity(argc);
  return hash_merge_into(hash_dup_of(self), argc, argv, blk);
}
static VALUE hash_merge_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  hash_ptr(self); merge_arity(argc); rb_check_frozen(self);
  return hash_merge_into(self, argc, argv, blk);
}

static VALUE hash_compare_by_identity(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  if(h->compare_by_identity) return self;
  long n = h->n;
  hentry *old = h->ent;
  h->ent = NULL; h->cap = 0;
  h_clear(h);
  h->compare_by_identity = 1;
  for(long i = 0; i < n; i++) if(old[i].used) h_aset_ident(self, old[i].key, old[i].val);
  free(old);
  return self; }
static VALUE hash_compare_by_identity_p(VALUE self, RProc *b){ (void)b;
  return hash_ptr(self)->compare_by_identity ? Qtrue : Qfalse; }

/* ===================== equality, hashing, printing ===================== */

static int hash_eq_gen(VALUE self, VALUE other, int eql){
  if(self == other) return 1;
  if(FIXNUM_P(other) || BASIC(other)->type != T_HASH) return 0;
  RHash *a = RHSH(self), *b = RHSH(other);
  if(a->size != b->size) return 0;
  if(!rec_enter(&eq_rec, self)) return 1;
  int ok = 1;
  for(long i = 0; i < a->n; i++){
    if(!a->ent[i].used) continue;
    VALUE bv = rb_hash_lookup(other, a->ent[i].key, Qundef);
    if(bv == Qundef){ ok = 0; break; }
    if(!(eql ? rb_eql(a->ent[i].val, bv) : rb_equal(a->ent[i].val, bv))){ ok = 0; break; }
  }
  rec_leave(&eq_rec);
  return ok;
}
static VALUE hash_equal(VALUE self, VALUE other, RProc *b){ (void)b;
  hash_ptr(self);
  if(!FIXNUM_P(other) && BASIC(other)->type != T_HASH && rb_respond_to(other, id_to_hash))
    return rb_equal(other, self) ? Qtrue : Qfalse;
  return hash_eq_gen(self, other, 0) ? Qtrue : Qfalse; }
static VALUE hash_eql(VALUE self, VALUE other, RProc *b){ (void)b;
  hash_ptr(self);
  return hash_eq_gen(self, other, 1) ? Qtrue : Qfalse; }
static VALUE hash_hash_m(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  if(!rec_enter(&hash_rec, self)) return LONG2FIX(0);
  unsigned long r = 0;
  for(long i = 0; i < h->n; i++){
    if(!h->ent[i].used) continue;
    unsigned long k = (unsigned long)rb_hash_code(h->ent[i].key);
    unsigned long v = (unsigned long)rb_hash_code(h->ent[i].val);
    r += k ^ (v * 0x9e3779b9UL);
  }
  rec_leave(&hash_rec);
  r = r * 31 + (unsigned long)h->size;
  return LONG2FIX((long)(r & (unsigned long)FIXNUM_MAX)); }

#if RUBY_TARGET >= 34
static int sym_simple(const char *s, long len){
  if(len == 0) return 0;
  if(!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
  long i = 1;
  while(i < len && (isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
  if(i == len) return 1;
  if(i == len - 1 && (s[i] == '?' || s[i] == '!')) return 1;
  return 0;
}
#endif
static VALUE hash_inspect(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  if(h->size == 0) return rb_str_new_cstr("{}");
  if(!rec_enter(&insp_rec, self)) return rb_str_new_cstr("{...}");
  VALUE s = rb_str_new_cstr("{");
  int first = 1;
  for(long i = 0; i < h->n; i++){
    if(!h->ent[i].used) continue;
    if(!first) rb_str_cat_cstr(s, ", ");
    first = 0;
    VALUE k = h->ent[i].key, v = h->ent[i].val;
#if RUBY_TARGET >= 34
    if(!FIXNUM_P(k) && BASIC(k)->type == T_SYMBOL){
      const char *nm = rb_id2name(RSYM(k)->id);
      long nl = rb_id_len(RSYM(k)->id);
      if(sym_simple(nm, nl)) rb_str_cat(s, nm, nl);
      else { VALUE q = rb_inspect(rb_str_new(nm, nl)); rb_str_cat(s, RSTR(q)->ptr, RSTR(q)->len); }
      rb_str_cat_cstr(s, ": ");
    } else {
      VALUE ki = rb_inspect(k);
      rb_str_cat(s, RSTR(ki)->ptr, RSTR(ki)->len);
      rb_str_cat_cstr(s, " => ");
    }
#else
    VALUE ki = rb_inspect(k);
    rb_str_cat(s, RSTR(ki)->ptr, RSTR(ki)->len);
    rb_str_cat_cstr(s, "=>");
#endif
    VALUE vi = rb_inspect(v);
    rb_str_cat(s, RSTR(vi)->ptr, RSTR(vi)->len);
  }
  rb_str_cat_cstr(s, "}");
  rec_leave(&insp_rec);
  return s; }

static VALUE hash_to_s(VALUE self, RProc *b){
#if RUBY_TARGET < 20
  (void)b;
  /* 1.8: Hash#to_s is to_a.to_s, which flattens and concatenates */
  return rb_funcall(hash_to_a(self, NULL), ID_CACHED("to_s"), 0);
#else
  return hash_inspect(self, b);
#endif
}

/* ===================== version-gated methods ===================== */

#if RUBY_TARGET >= 20
static VALUE hash_to_h(VALUE self, RProc *blk){
  hash_ptr(self);
#if RUBY_TARGET >= 27
  if(blk){
    RHash *h = RHSH(self);
    VALUE r = rb_hash_new();
    long n = h->n;
    for(long i = 0; i < n && i < h->n; i++){
      if(!h->ent[i].used) continue;
      VALUE p = rb_yield1(blk, pair_new(h->ent[i].key, h->ent[i].val));
      VALUE a = FIXNUM_P(p) ? Qnil : rb_check_array_type(p);
      if(NIL_P(a) || RARR(a)->len != 2)
        rb_raise(rb_eTypeError, "wrong element type %s (expected array)", rb_obj_classname(p));
      h_set(r, RARR(a)->ptr[0], RARR(a)->ptr[1]);
    }
    return r;
  }
#else
  (void)blk;
#endif
  if(BASIC(self)->klass == rb_cHash) return self;
  VALUE r = hash_dup_of(self);
  BASIC(r)->klass = rb_cHash;
  return r;
}
#endif

#if RUBY_TARGET < 20
static VALUE hash_index(VALUE self, VALUE v, RProc *b){ return hash_key(self, v, b); }
#endif

#if RUBY_TARGET >= 27
static VALUE hash_dig(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  hash_ptr(self);
  if(argc < 1) rb_raise(rb_eArgError, "wrong number of arguments (given 0, expected 1+)");
  VALUE v = rb_hash_aref(self, argv[0]);
  if(argc == 1) return v;
  if(NIL_P(v)) return Qnil;
  if(!rb_respond_to(v, ID_CACHED("dig")))
    rb_raise(rb_eTypeError, "%s does not have #dig method", rb_obj_classname(v));
  return rb_call(v, ID_CACHED("dig"), argc - 1, argv + 1, NULL); }

static VALUE hash_fetch_values(VALUE self, int argc, VALUE *argv, RProc *blk){
  hash_ptr(self);
  VALUE a = rb_ary_new_capa(argc ? argc : 1);
  for(int i = 0; i < argc; i++){
    VALUE v = rb_hash_lookup(self, argv[i], Qundef);
    if(v == Qundef){
      if(blk){ rb_ary_push(a, rb_yield1(blk, argv[i])); continue; }
      VALUE exc = rb_exc_new(rb_eKeyError,
        rb_sprintf_val("key not found: %s", rb_str_cstr(rb_inspect(argv[i]))));
      rb_ivar_set(exc, ID_CACHED("@key"), argv[i]);
      rb_ivar_set(exc, ID_CACHED("@receiver"), self);
      rb_exc_raise(exc);
    }
    rb_ary_push(a, v);
  }
  return a; }

static VALUE hash_transform_values(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("transform_values"), 0, NULL);
  VALUE r = rb_hash_new();
  RHSH(r)->compare_by_identity = h->compare_by_identity;
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++)
    if(h->ent[i].used) h_set(r, h->ent[i].key, rb_yield1(blk, h->ent[i].val));
  return r; }
static VALUE hash_transform_values_bang(VALUE self, RProc *blk){
  RHash *h = hash_ptr(self);
  if(!blk) return rb_enum_new(self, ID_CACHED("transform_values!"), 0, NULL);
  rb_check_frozen(self);
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++)
    if(h->ent[i].used) h->ent[i].val = rb_yield1(blk, h->ent[i].val);
  return self; }

static VALUE hash_transform_keys(VALUE self, int argc, VALUE *argv, RProc *blk){
  RHash *h = hash_ptr(self);
  VALUE map = Qnil;
#if RUBY_TARGET >= 31
  if(argc == 1) map = to_hash_arg(argv[0]);
  else if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
#else
  if(argc > 0) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0)", argc);
#endif
  if(NIL_P(map) && !blk) return rb_enum_new(self, ID_CACHED("transform_keys"), 0, NULL);
  VALUE r = rb_hash_new();
  RHSH(r)->compare_by_identity = h->compare_by_identity;
  long n = h->n;
  for(long i = 0; i < n && i < h->n; i++){
    if(!h->ent[i].used) continue;
    VALUE k = h->ent[i].key, nk;
    if(!NIL_P(map)){
      VALUE m = rb_hash_lookup(map, k, Qundef);
      if(m != Qundef) nk = m;
      else if(blk) nk = rb_yield1(blk, k);
      else nk = k;
    } else nk = rb_yield1(blk, k);
    h_set(r, nk, h->ent[i].val);
  }
  return r; }
static VALUE hash_transform_keys_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  hash_ptr(self);
  rb_check_frozen(self);
  VALUE r = hash_transform_keys(self, argc, argv, blk);
  if(FIXNUM_P(r) || BASIC(r)->type != T_HASH) return r;   /* enumerator */
  return hash_replace(self, r, NULL); }

static VALUE hash_to_proc_call(VALUE data, int argc, VALUE *argv, RProc *blk){ (void)blk;
  if(argc != 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1)", argc);
  return rb_hash_aref(data, argv[0]); }
static VALUE hash_to_proc(VALUE self, RProc *b){ (void)b;
  hash_ptr(self);
  return (VALUE)rb_cfunc_proc(hash_to_proc_call, self, 1); }

static VALUE hash_compact(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  VALUE r = rb_hash_new();
  RHSH(r)->compare_by_identity = h->compare_by_identity;
  for(long i = 0; i < h->n; i++)
    if(h->ent[i].used && !NIL_P(h->ent[i].val)) h_set(r, h->ent[i].key, h->ent[i].val);
  return r; }
static VALUE hash_compact_bang(VALUE self, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  rb_check_frozen(self);
  long removed = 0;
  for(long i = 0; i < h->n; i++)
    if(h->ent[i].used && NIL_P(h->ent[i].val) && rb_hash_delete(self, h->ent[i].key, NULL)) removed++;
  return removed ? self : Qnil; }

static VALUE hash_slice(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  RHash *h = hash_ptr(self);
  VALUE r = rb_hash_new();
  RHSH(r)->compare_by_identity = h->compare_by_identity;
  for(int i = 0; i < argc; i++){
    VALUE v = rb_hash_lookup(self, argv[i], Qundef);
    if(v != Qundef) h_set(r, argv[i], v);
  }
  return r; }

static VALUE hash_deconstruct_keys(VALUE self, VALUE keys, RProc *b){ (void)b; (void)keys;
  hash_ptr(self); return self; }

static VALUE hash_each_entry(VALUE self, RProc *blk){
  if(!blk) return rb_enum_new(self, ID_CACHED("each_entry"), 0, NULL);
  return hash_each(self, blk); }
#endif  /* RUBY_TARGET >= 27 */

#if RUBY_TARGET >= 31
static VALUE hash_except(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  hash_ptr(self);
  VALUE r = hash_dup_of(self);
  BASIC(r)->klass = rb_cHash;
  RHSH(r)->ifnone = Qnil; RHSH(r)->defproc = NULL;
  for(int i = 0; i < argc; i++) rb_hash_delete(r, argv[i], NULL);
  return r; }
#endif

/* ===================== registration ===================== */

void rb_init_hash(void){
  id_each = ID_CACHED("each");
  id_to_hash = ID_CACHED("to_hash");

  rb_define_singleton_method((VALUE)rb_cHash, "new", hash_s_new, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cHash, "[]", hash_s_create, ARGC_ANY);
  rb_define_private_method(rb_cHash, "initialize", hash_initialize, ARGC_ANY);

  rb_define_method(rb_cHash, "[]", hash_aref, 1);
  rb_define_method(rb_cHash, "[]=", hash_aset, 2);
  rb_define_method(rb_cHash, "store", hash_aset, 2);
  rb_define_method(rb_cHash, "fetch", hash_fetch, ARGC_ANY);
  rb_define_method(rb_cHash, "default", hash_default, ARGC_ANY);
  rb_define_method(rb_cHash, "default=", hash_set_default, 1);
  rb_define_method(rb_cHash, "default_proc", hash_default_proc, 0);
  rb_define_method(rb_cHash, "default_proc=", hash_set_default_proc, 1);
  rb_define_method(rb_cHash, "size", hash_size, 0);
  rb_define_method(rb_cHash, "length", hash_size, 0);
  rb_define_method(rb_cHash, "count", hash_count, ARGC_ANY);
  rb_define_method(rb_cHash, "empty?", hash_empty_p, 0);
  rb_define_method(rb_cHash, "each", hash_each, 0);
  rb_define_method(rb_cHash, "each_pair", hash_each, 0);
  rb_define_method(rb_cHash, "each_key", hash_each_key, 0);
  rb_define_method(rb_cHash, "each_value", hash_each_value, 0);
  rb_define_method(rb_cHash, "keys", hash_keys, 0);
  rb_define_method(rb_cHash, "values", hash_values, 0);
  rb_define_method(rb_cHash, "values_at", hash_values_at, ARGC_ANY);
  rb_define_method(rb_cHash, "key?", hash_has_key, 1);
  rb_define_method(rb_cHash, "has_key?", hash_has_key, 1);
  rb_define_method(rb_cHash, "include?", hash_has_key, 1);
  rb_define_method(rb_cHash, "member?", hash_has_key, 1);
  rb_define_method(rb_cHash, "value?", hash_has_value, 1);
  rb_define_method(rb_cHash, "has_value?", hash_has_value, 1);
  rb_define_method(rb_cHash, "key", hash_key, 1);
  rb_define_method(rb_cHash, "invert", hash_invert, 0);
  rb_define_method(rb_cHash, "assoc", hash_assoc, 1);
  rb_define_method(rb_cHash, "rassoc", hash_rassoc, 1);
  rb_define_method(rb_cHash, "flatten", hash_flatten, ARGC_ANY);
  rb_define_method(rb_cHash, "merge", hash_merge, ARGC_ANY);
  rb_define_method(rb_cHash, "merge!", hash_merge_bang, ARGC_ANY);
  rb_define_method(rb_cHash, "update", hash_merge_bang, ARGC_ANY);
  rb_define_method(rb_cHash, "delete", hash_delete, 1);
  rb_define_method(rb_cHash, "delete_if", hash_delete_if, 0);
  rb_define_method(rb_cHash, "keep_if", hash_keep_if, 0);
  rb_define_method(rb_cHash, "reject", hash_reject, 0);
  rb_define_method(rb_cHash, "reject!", hash_reject_bang, 0);
  rb_define_method(rb_cHash, "select", hash_select, 0);
  rb_define_method(rb_cHash, "select!", hash_select_bang, 0);
  rb_define_method(rb_cHash, "to_a", hash_to_a, 0);
  rb_define_method(rb_cHash, "to_hash", hash_to_hash, 0);
  rb_define_method(rb_cHash, "to_s", hash_to_s, 0);
  rb_define_method(rb_cHash, "inspect", hash_inspect, 0);
  rb_define_method(rb_cHash, "==", hash_equal, 1);
  rb_define_method(rb_cHash, "eql?", hash_eql, 1);
  rb_define_method(rb_cHash, "hash", hash_hash_m, 0);
  rb_define_method(rb_cHash, "clear", hash_clear, 0);
  rb_define_method(rb_cHash, "shift", hash_shift, 0);
  rb_define_method(rb_cHash, "replace", hash_replace, 1);
  rb_define_method(rb_cHash, "any?", hash_any, ARGC_ANY);
  rb_define_method(rb_cHash, "all?", hash_all, ARGC_ANY);
  rb_define_method(rb_cHash, "none?", hash_none, ARGC_ANY);
  rb_define_method(rb_cHash, "find", hash_find, 0);
  rb_define_method(rb_cHash, "compare_by_identity", hash_compare_by_identity, 0);
  rb_define_method(rb_cHash, "compare_by_identity?", hash_compare_by_identity_p, 0);

#if RUBY_TARGET < 20
  rb_define_method(rb_cHash, "index", hash_index, 1);
#endif
#if RUBY_TARGET >= 20
  rb_define_method(rb_cHash, "to_h", hash_to_h, 0);
#endif
#if RUBY_TARGET >= 27
  rb_define_method(rb_cHash, "dig", hash_dig, ARGC_ANY);
  rb_define_method(rb_cHash, "fetch_values", hash_fetch_values, ARGC_ANY);
  rb_define_method(rb_cHash, "transform_values", hash_transform_values, 0);
  rb_define_method(rb_cHash, "transform_values!", hash_transform_values_bang, 0);
  rb_define_method(rb_cHash, "transform_keys", hash_transform_keys, ARGC_ANY);
  rb_define_method(rb_cHash, "transform_keys!", hash_transform_keys_bang, ARGC_ANY);
  rb_define_method(rb_cHash, "to_proc", hash_to_proc, 0);
  rb_define_method(rb_cHash, "compact", hash_compact, 0);
  rb_define_method(rb_cHash, "compact!", hash_compact_bang, 0);
  rb_define_method(rb_cHash, "slice", hash_slice, ARGC_ANY);
  rb_define_method(rb_cHash, "filter", hash_select, 0);
  rb_define_method(rb_cHash, "filter!", hash_select_bang, 0);
  rb_define_method(rb_cHash, "each_entry", hash_each_entry, 0);
  rb_define_method(rb_cHash, "deconstruct_keys", hash_deconstruct_keys, 1);
#endif
#if RUBY_TARGET >= 31
  rb_define_method(rb_cHash, "except", hash_except, ARGC_ANY);
#endif
}
