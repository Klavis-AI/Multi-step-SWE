/* array.c -- Ruby's Array class. */

#include "internal.h"

extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);

#define ARY_MAX_SIZE  ((long)(LONG_MAX / (long)sizeof(VALUE)))
#define ALEN(a)       (RARR(a)->len)
#define APTR(a)       (RARR(a)->ptr)
#define REC_MAX       64

/* ===================== basic helpers ===================== */

static void ary_modify(VALUE ary){ rb_check_frozen(ary); }

static void ary_ensure_capa(VALUE ary, long capa){
  RArray *a = RARR(ary);
  if(capa <= a->cap) return;
  if(capa > ARY_MAX_SIZE) rb_raise(rb_eArgError, "array size too big");
  long nc = a->cap > 0 ? a->cap : 4;
  while(nc < capa){ if(nc > ARY_MAX_SIZE/2){ nc = capa; break; } nc *= 2; }
  a->ptr = xrealloc(a->ptr, sizeof(VALUE)*nc);
  a->cap = nc;
}

/* grow with nils / shrink */
static void ary_resize(VALUE ary, long len){
  RArray *a = RARR(ary);
  if(len > a->len){
    ary_ensure_capa(ary, len);
    for(long i=a->len;i<len;i++) a->ptr[i] = Qnil;
  }
  a->len = len;
}

static void ary_store(VALUE ary, long idx, VALUE val){
  long len = ALEN(ary);
  if(idx < 0){
    idx += len;
    if(idx < 0) rb_raise(rb_eIndexError, "index %ld too small for array; minimum: %ld",
                         idx - len, -len);
  }
  else if(idx >= ARY_MAX_SIZE) rb_raise(rb_eIndexError, "index %ld too big", idx);
  ary_modify(ary);
  if(idx >= len) ary_resize(ary, idx+1);
  APTR(ary)[idx] = val;
}

static VALUE ary_alloc(RClass *k, long capa){
  VALUE a = rb_ary_new_capa(capa);
  BASIC(a)->klass = k;
  return a;
}

/* plain Array holding a copy of n values */
static VALUE ary_new_from(long n, VALUE *v){
  VALUE a = rb_ary_new_capa(n > 0 ? n : 1);
  for(long i=0;i<n;i++) APTR(a)[i] = v[i];
  RARR(a)->len = n;
  return a;
}

/* Class of a "partial" result (slices, first(n), take, flatten, uniq, *).
   Ruby 3.0 made these return Array even for subclasses. */
static RClass *ary_partial_class(VALUE ary){
#if RUBY_TARGET >= 31
  (void)ary; return rb_cArray;
#else
  return rb_class_of(ary);
#endif
}

static VALUE ary_make_partial(VALUE ary, long beg, long len){
  RArray *a = RARR(ary);
  if(beg < 0) beg = 0;
  if(len > a->len - beg) len = a->len - beg;
  if(len < 0) len = 0;
  VALUE r = ary_alloc(ary_partial_class(ary), len);
  for(long i=0;i<len;i++) APTR(r)[i] = a->ptr[beg+i];
  RARR(r)->len = len;
  return r;
}

/* rb_ary_subseq(): nil when out of bounds */
static VALUE ary_subseq(VALUE ary, long beg, long len){
  long alen = ALEN(ary);
  if(beg > alen) return Qnil;
  if(beg < 0 || len < 0) return Qnil;
  if(alen < len || alen < beg + len) len = alen - beg;
  return ary_make_partial(ary, beg, len);
}

static void ary_arity_err(int given, int min, int max) __attribute__((noreturn));
static void ary_arity_err(int given, int min, int max){
#if RUBY_TARGET >= 27
  if(max < 0) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %d+)", given, min);
  if(min == max) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %d)", given, min);
  rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected %d..%d)", given, min, max);
#else
  if(max < 0) rb_raise(rb_eArgError, "wrong number of arguments (%d for %d+)", given, min);
  if(min == max) rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)", given, min);
  rb_raise(rb_eArgError, "wrong number of arguments (%d for %d..%d)", given, min, max);
#endif
}
static void ary_check_arity(int argc, int min, int max){
  if(argc < min || (max >= 0 && argc > max)) ary_arity_err(argc, min, max);
}

static VALUE ary_enum(VALUE self, const char *name, int argc, VALUE *argv){
  return rb_enum_new(self, rb_intern(name), argc, argv);
}

static void str_append(VALUE dst, VALUE src){
  if(RSTR(dst)->enc == enc_usascii && RSTR(src)->enc != enc_usascii)
    RSTR(dst)->enc = RSTR(src)->enc;
  rb_str_cat(dst, RSTR(src)->ptr, RSTR(src)->len);
}

static int is_ary(VALUE v){ return !FIXNUM_P(v) && TYPE(v) == T_ARRAY; }
static int is_str(VALUE v){ return !FIXNUM_P(v) && TYPE(v) == T_STRING; }

/* to_ary conversion used by the mutating/binary operators */
static VALUE to_ary(VALUE v){
  VALUE a = rb_check_array_type(v);
  if(NIL_P(a)) rb_raise(rb_eTypeError, "no implicit conversion of %s into Array",
                        NIL_P(v) ? "nil" : rb_obj_classname(v));
  return a;
}

/* run fn(a,b), restoring *depth even when it raises */
static VALUE ary_protect(VALUE (*fn)(VALUE,VALUE), VALUE a, VALUE b, int *depth){
  int saved = *depth;
  VALUE r = Qnil;
  rb_tag tag; TAG_PUSH(tag);
  if(setjmp(tag.buf) == 0){ r = fn(a,b); TAG_POP(tag); }
  else { TAG_POP(tag); *depth = saved; rb_jump_tag(tag.state, tag.retval, tag.target); }
  *depth = saved;
  return r;
}

/* ===================== range helper ===================== */

enum { RANGE_NOT = 0, RANGE_OK = 1, RANGE_OUT = -1 };

static int ary_range_beg_len(VALUE range, long *begp, long *lenp, long alen, int err){
  if(FIXNUM_P(range) || TYPE(range) != T_RANGE) return RANGE_NOT;
  VALUE b = RRNG(range)->beg, e = RRNG(range)->end;
  int excl = RRNG(range)->excl;
  long beg = NIL_P(b) ? 0 : rb_num2long(b);
  long end = NIL_P(e) ? -1 : rb_num2long(e);
  long origbeg = beg, origend = end, len;
  if(NIL_P(e)) excl = 0;
  if(beg < 0){
    beg += alen;
    if(beg < 0) goto out_of_range;
  }
  if(err == 0 || err == 2){
    if(beg > alen) goto out_of_range;
    if(end > alen) end = alen;
  }
  if(end < 0) end += alen;
  if(!excl) end++;
  if(end < 0) end = 0;
  len = end - beg;
  if(len < 0) len = 0;
  *begp = beg; *lenp = len;
  return RANGE_OK;
 out_of_range:
  if(err) rb_raise(rb_eRangeError, "%ld..%s%ld out of range", origbeg, excl ? "." : "", origend);
  return RANGE_OUT;
}

/* ===================== splice ===================== */

static void ary_splice(VALUE ary, long beg, long len, VALUE *rptr, long rlen){
  if(len < 0) rb_raise(rb_eIndexError, "negative length (%ld)", len);
  long olen = ALEN(ary);
  if(beg < 0){
    beg += olen;
    if(beg < 0) rb_raise(rb_eIndexError, "index %ld too small for array; minimum: %ld",
                         beg - olen, -olen);
  }
  if(olen < len || olen < beg + len) len = olen - beg;
  if(len < 0) len = 0;
  ary_modify(ary);

  /* the replacement may live inside this very array: copy it away first */
  VALUE *src = rptr;
  if(rlen > 0 && rptr >= APTR(ary) && rptr < APTR(ary) + RARR(ary)->cap){
    src = xmalloc(sizeof(VALUE)*rlen);
    memcpy(src, rptr, sizeof(VALUE)*rlen);
  }

  if(beg >= olen){
    ary_ensure_capa(ary, beg + rlen);
    for(long i=olen;i<beg;i++) APTR(ary)[i] = Qnil;
    for(long i=0;i<rlen;i++) APTR(ary)[beg+i] = src[i];
    RARR(ary)->len = beg + rlen;
  }
  else {
    long alen = olen + rlen - len;
    ary_ensure_capa(ary, alen);
    if(len != rlen){
      memmove(APTR(ary)+beg+rlen, APTR(ary)+beg+len, sizeof(VALUE)*(olen-(beg+len)));
      RARR(ary)->len = alen;
    }
    for(long i=0;i<rlen;i++) APTR(ary)[beg+i] = src[i];
  }
  if(src != rptr) free(src);
}

/* ===================== allocation / initialize ===================== */

static VALUE ary_replace(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  ary_modify(self);
  VALUE o = to_ary(other);
  if(self == o) return self;
  long n = ALEN(o);
  ary_ensure_capa(self, n);
  for(long i=0;i<n;i++) APTR(self)[i] = APTR(o)[i];
  RARR(self)->len = n;
  return self;
}

static VALUE ary_initialize(VALUE self, int argc, VALUE *argv, RProc *blk){
  ary_check_arity(argc, 0, 2);
  ary_modify(self);
  if(argc == 0){ RARR(self)->len = 0; return self; }

  VALUE size = argv[0];
  if(argc == 1 && !FIXNUM_P(size)){
    VALUE t = rb_check_array_type(size);
    if(!NIL_P(t)) return ary_replace(self, t, NULL);
  }
  long len = rb_num2long(size);
  if(len < 0) rb_raise(rb_eArgError, "negative array size");
  if(len > ARY_MAX_SIZE) rb_raise(rb_eArgError, "array size too big");
  ary_ensure_capa(self, len);
  RARR(self)->len = 0;
  if(blk){
    if(argc == 2) rb_warning("block supersedes default value argument");
    for(long i=0;i<len;i++) ary_store(self, i, rb_yield1(blk, LONG2FIX(i)));
  }
  else {
    VALUE val = argc > 1 ? argv[1] : Qnil;
    for(long i=0;i<len;i++) APTR(self)[i] = val;
    RARR(self)->len = len;
  }
  return self;
}

static VALUE ary_s_new(VALUE klass, int argc, VALUE *argv, RProc *blk){
  VALUE ary = ary_alloc((RClass*)klass, 0);
  rb_call(ary, ID_CACHED("initialize"), argc, argv, blk);
  return ary;
}

static VALUE ary_s_create(VALUE klass, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  VALUE ary = ary_alloc((RClass*)klass, argc);
  for(int i=0;i<argc;i++) APTR(ary)[i] = argv[i];
  RARR(ary)->len = argc;
  return ary;
}

/* ===================== size / emptiness ===================== */

static VALUE ary_length(VALUE self, RProc *blk){ (void)blk; return LONG2FIX(ALEN(self)); }
static VALUE ary_empty_p(VALUE self, RProc *blk){ (void)blk; return ALEN(self) == 0 ? Qtrue : Qfalse; }

static VALUE ary_clear(VALUE self, RProc *blk){
  (void)blk;
  ary_modify(self);
  RARR(self)->len = 0;
  return self;
}

/* ===================== element reference ===================== */

static VALUE ary_aref(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long beg, len;
  if(argc == 2){
    beg = rb_num2long(argv[0]);
    len = rb_num2long(argv[1]);
    if(beg < 0) beg += ALEN(self);
    return ary_subseq(self, beg, len);
  }
  ary_check_arity(argc, 1, 2);
  if(FIXNUM_P(argv[0])) return rb_ary_entry(self, FIX2LONG(argv[0]));
  switch(ary_range_beg_len(argv[0], &beg, &len, ALEN(self), 0)){
    case RANGE_OUT: return Qnil;
    case RANGE_OK:  return ary_make_partial(self, beg, len);
    default: break;
  }
  return rb_ary_entry(self, rb_num2long(argv[0]));
}

static VALUE ary_at(VALUE self, VALUE pos, RProc *blk){
  (void)blk;
  return rb_ary_entry(self, rb_num2long(pos));
}

static VALUE ary_fetch(VALUE self, int argc, VALUE *argv, RProc *blk){
  ary_check_arity(argc, 1, 2);
  if(blk && argc == 2) rb_warning("block supersedes default value argument");
  long alen = ALEN(self);
  long idx = rb_num2long(argv[0]);
  long i = idx;
  if(i < 0) i += alen;
  if(i < 0 || alen <= i){
    if(blk) return rb_yield1(blk, argv[0]);
    if(argc == 1) rb_raise(rb_eIndexError, "index %ld outside of array bounds: %ld...%ld",
                           idx, -alen, alen);
    return argv[1];
  }
  return APTR(self)[i];
}

static VALUE ary_first(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  if(argc == 0) return ALEN(self) == 0 ? Qnil : APTR(self)[0];
  long n = rb_num2long(argv[0]);
  if(n < 0) rb_raise(rb_eArgError, "negative array size");
  if(n > ALEN(self)) n = ALEN(self);
  return ary_make_partial(self, 0, n);
}

static VALUE ary_last(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  long alen = ALEN(self);
  if(argc == 0) return alen == 0 ? Qnil : APTR(self)[alen-1];
  long n = rb_num2long(argv[0]);
  if(n < 0) rb_raise(rb_eArgError, "negative array size");
  if(n > alen) n = alen;
  return ary_make_partial(self, alen - n, n);
}

static VALUE ary_values_at(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long alen = ALEN(self);
  VALUE result = rb_ary_new_capa(argc);
  for(int i=0;i<argc;i++){
    long beg, len;
    switch(ary_range_beg_len(argv[i], &beg, &len, alen, 0)){
      case RANGE_OUT: continue;
      case RANGE_OK:
        for(long j=0;j<len;j++) rb_ary_push(result, rb_ary_entry(self, beg+j));
        continue;
      default: break;
    }
    rb_ary_push(result, rb_ary_entry(self, rb_num2long(argv[i])));
  }
  return result;
}

/* ===================== element assignment ===================== */

static VALUE ary_aset_splice(VALUE self, long beg, long len, VALUE val){
#if RUBY_TARGET < 20
  /* 1.8: a[i,n] = nil deletes that range */
  if(NIL_P(val)){ ary_splice(self, beg, len, NULL, 0); return val; }
#endif
  VALUE rpl = rb_ary_to_ary(val);
  ary_splice(self, beg, len, APTR(rpl), ALEN(rpl));
  return val;
}

static VALUE ary_aset(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long beg, len;
  ary_check_arity(argc, 2, 3);
  ary_modify(self);
  if(argc == 3){
    beg = rb_num2long(argv[0]);
    len = rb_num2long(argv[1]);
    return ary_aset_splice(self, beg, len, argv[2]);
  }
  if(FIXNUM_P(argv[0])){
    ary_store(self, FIX2LONG(argv[0]), argv[1]);
    return argv[1];
  }
  if(ary_range_beg_len(argv[0], &beg, &len, ALEN(self), 1) == RANGE_OK)
    return ary_aset_splice(self, beg, len, argv[1]);
  ary_store(self, rb_num2long(argv[0]), argv[1]);
  return argv[1];
}

/* ===================== push / pop / shift ===================== */

static VALUE ary_push_m(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_modify(self);
  for(int i=0;i<argc;i++) rb_ary_push(self, argv[i]);
  return self;
}

static VALUE ary_push_one(VALUE self, VALUE item, RProc *blk){
  (void)blk;
  ary_modify(self);
  rb_ary_push(self, item);
  return self;
}

static VALUE ary_pop(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  ary_modify(self);
  long alen = ALEN(self);
  if(argc == 0){
    if(alen == 0) return Qnil;
    RARR(self)->len = alen - 1;
    return APTR(self)[alen-1];
  }
  long n = rb_num2long(argv[0]);
  if(n < 0) rb_raise(rb_eArgError, "negative array size");
  if(n > alen) n = alen;
  VALUE result = ary_make_partial(self, alen - n, n);
  RARR(self)->len = alen - n;
  return result;
}

VALUE rb_ary_shift_n(VALUE ary, long n){
  long alen = ALEN(ary);
  if(n < 0) rb_raise(rb_eArgError, "negative array size");
  if(n > alen) n = alen;
  VALUE result = ary_make_partial(ary, 0, n);
  memmove(APTR(ary), APTR(ary)+n, sizeof(VALUE)*(alen-n));
  RARR(ary)->len = alen - n;
  return result;
}

static VALUE ary_shift(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  ary_modify(self);
  if(argc == 0){
    long alen = ALEN(self);
    if(alen == 0) return Qnil;
    VALUE top = APTR(self)[0];
    memmove(APTR(self), APTR(self)+1, sizeof(VALUE)*(alen-1));
    RARR(self)->len = alen - 1;
    return top;
  }
  return rb_ary_shift_n(self, rb_num2long(argv[0]));
}

static VALUE ary_unshift(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_modify(self);
  if(argc == 0) return self;
  ary_splice(self, 0, 0, argv, argc);
  return self;
}

static VALUE ary_insert(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 1, -1);
  ary_modify(self);
  long pos = rb_num2long(argv[0]);
  if(argc == 1) return self;
  if(pos == -1) pos = ALEN(self);
  else if(pos < 0){
    long minpos = -ALEN(self) - 1;
    if(pos < minpos) rb_raise(rb_eIndexError, "index %ld too small for array; minimum: %ld",
                              pos, minpos);
    pos++;
  }
  ary_splice(self, pos, 0, argv+1, argc-1);
  return self;
}

static void ary_append(VALUE self, VALUE other){
  long n = ALEN(other);
  if(n == 0) return;
  ary_ensure_capa(self, ALEN(self) + n);
  memmove(APTR(self)+ALEN(self), APTR(other), sizeof(VALUE)*n);
  RARR(self)->len += n;
}

static VALUE ary_concat(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
#if RUBY_TARGET >= 27
  ary_check_arity(argc, 0, -1);
#else
  ary_check_arity(argc, 1, 1);
#endif
  ary_modify(self);
  if(argc == 1){ ary_append(self, to_ary(argv[0])); return self; }
  if(argc > 1){
    VALUE tmp = rb_ary_new_capa(argc);
    for(int i=0;i<argc;i++) ary_append(tmp, to_ary(argv[i]));
    ary_append(self, tmp);
  }
  return self;
}

/* ===================== iteration ===================== */

static VALUE ary_each(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "each", 0, NULL);
  for(long i=0;i<ALEN(self);i++) rb_yield1(blk, APTR(self)[i]);
  return self;
}

static VALUE ary_each_index(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "each_index", 0, NULL);
  for(long i=0;i<ALEN(self);i++) rb_yield1(blk, LONG2FIX(i));
  return self;
}

static VALUE ary_each_with_index(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "each_with_index", 0, NULL);
  for(long i=0;i<ALEN(self);i++){
    VALUE args[2]; args[0] = APTR(self)[i]; args[1] = LONG2FIX(i);
    rb_yield_values(blk, 2, args);
  }
  return self;
}

static VALUE ary_reverse_each(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "reverse_each", 0, NULL);
  long len = ALEN(self);
  while(len--){
    rb_yield1(blk, rb_ary_entry(self, len));
    long nlen = ALEN(self);
    if(nlen < len) len = nlen;
  }
  return self;
}

/* ===================== searching ===================== */

static VALUE ary_includes(VALUE self, VALUE item, RProc *blk){
  (void)blk;
  for(long i=0;i<ALEN(self);i++) if(rb_equal(APTR(self)[i], item)) return Qtrue;
  return Qfalse;
}

#if RUBY_TARGET >= 20
static VALUE ary_index_impl(VALUE self, int argc, VALUE *argv, RProc *blk, const char *name){
  if(argc == 0){
    if(!blk) return ary_enum(self, name, 0, NULL);
    for(long i=0;i<ALEN(self);i++)
      if(RTEST(rb_yield1(blk, APTR(self)[i]))) return LONG2FIX(i);
    return Qnil;
  }
  ary_check_arity(argc, 0, 1);
  if(blk) rb_warning("given block not used");
  for(long i=0;i<ALEN(self);i++) if(rb_equal(APTR(self)[i], argv[0])) return LONG2FIX(i);
  return Qnil;
}
static VALUE ary_index(VALUE self, int argc, VALUE *argv, RProc *blk){
  return ary_index_impl(self, argc, argv, blk, "index");
}
static VALUE ary_find_index(VALUE self, int argc, VALUE *argv, RProc *blk){
  return ary_index_impl(self, argc, argv, blk, "find_index");
}
static VALUE ary_rindex(VALUE self, int argc, VALUE *argv, RProc *blk){
  long i = ALEN(self);
  if(argc == 0){
    if(!blk) return ary_enum(self, "rindex", 0, NULL);
    while(i--){
      if(RTEST(rb_yield1(blk, rb_ary_entry(self, i)))) return LONG2FIX(i);
      long len = ALEN(self);
      if(i > len) i = len;
    }
    return Qnil;
  }
  ary_check_arity(argc, 0, 1);
  if(blk) rb_warning("given block not used");
  while(i--){
    if(rb_equal(rb_ary_entry(self, i), argv[0])) return LONG2FIX(i);
    long len = ALEN(self);
    if(i > len) i = len;
  }
  return Qnil;
}
#else
/* 1.8 has no block form of index/rindex */
static VALUE ary_index(VALUE self, VALUE val, RProc *blk){
  (void)blk;
  for(long i=0;i<ALEN(self);i++) if(rb_equal(APTR(self)[i], val)) return LONG2FIX(i);
  return Qnil;
}
static VALUE ary_rindex(VALUE self, VALUE val, RProc *blk){
  (void)blk;
  long i = ALEN(self);
  while(i--) if(rb_equal(rb_ary_entry(self, i), val)) return LONG2FIX(i);
  return Qnil;
}
#endif

static VALUE ary_count(VALUE self, int argc, VALUE *argv, RProc *blk){
  ary_check_arity(argc, 0, 1);
  long n = 0;
  if(argc == 0){
    if(!blk) return LONG2FIX(ALEN(self));
    for(long i=0;i<ALEN(self);i++) if(RTEST(rb_yield1(blk, APTR(self)[i]))) n++;
  }
  else {
    if(blk) rb_warning("given block not used");
    for(long i=0;i<ALEN(self);i++) if(rb_equal(APTR(self)[i], argv[0])) n++;
  }
  return LONG2FIX(n);
}

static VALUE ary_assoc(VALUE self, VALUE key, RProc *blk){
  (void)blk;
  for(long i=0;i<ALEN(self);i++){
    VALUE v = rb_check_array_type(APTR(self)[i]);
    if(!NIL_P(v) && ALEN(v) > 0 && rb_equal(APTR(v)[0], key)) return v;
  }
  return Qnil;
}

static VALUE ary_rassoc(VALUE self, VALUE value, RProc *blk){
  (void)blk;
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    if(is_ary(v) && ALEN(v) > 1 && rb_equal(APTR(v)[1], value)) return v;
  }
  return Qnil;
}

/* ===================== join / inspect / to_s ===================== */

static VALUE join_stack[REC_MAX]; static int join_n;
static VALUE insp_stack[REC_MAX]; static int insp_n;
static VALUE hash_stack[REC_MAX]; static int hash_n;

static void ary_join_1(VALUE result, VALUE ary, VALUE sep);

static void ary_join_sub(VALUE result, VALUE ary, VALUE sep){
  for(int i=0;i<join_n;i++)
    if(join_stack[i] == ary) rb_raise(rb_eArgError, "recursive array join");
  if(join_n < REC_MAX) join_stack[join_n++] = ary;
  ary_join_1(result, ary, sep);
  if(join_n > 0) join_n--;
}

static void ary_join_1(VALUE result, VALUE ary, VALUE sep){
  for(long i=0;i<ALEN(ary);i++){
    if(i > 0 && !NIL_P(sep)) str_append(result, sep);
    VALUE v = APTR(ary)[i], tmp;
    if(is_str(v)){ str_append(result, v); continue; }
    if(is_ary(v)){ ary_join_sub(result, v, sep); continue; }
    tmp = rb_check_string_type(v);
    if(!NIL_P(tmp)){ str_append(result, tmp); continue; }
    tmp = rb_check_array_type(v);
    if(!NIL_P(tmp)){ ary_join_sub(result, tmp, sep); continue; }
    str_append(result, rb_obj_as_string(v));
  }
}

static VALUE ary_join_body(VALUE ary, VALUE sep){
  VALUE result = rb_str_new_enc("", 0, enc_usascii);
  ary_join_sub(result, ary, sep);
  return result;
}

static VALUE ary_join(VALUE ary, VALUE sep){
  if(ALEN(ary) == 0) return rb_str_new_enc("", 0, enc_usascii);
  if(!NIL_P(sep)) sep = rb_str_to_str(sep);
  return ary_protect(ary_join_body, ary, sep, &join_n);
}

static VALUE ary_join_m(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  return ary_join(self, argc > 0 ? argv[0] : Qnil);
}

static VALUE ary_inspect_body(VALUE ary, VALUE unused){
  (void)unused;
  if(insp_n < REC_MAX) insp_stack[insp_n++] = ary;
  VALUE str = rb_str_new_cstr("[");
  for(long i=0;i<ALEN(ary);i++){
    VALUE s = rb_inspect(APTR(ary)[i]);
    if(i > 0) rb_str_cat_cstr(str, ", ");
    str_append(str, s);
  }
  rb_str_cat_cstr(str, "]");
  return str;
}

static VALUE ary_inspect(VALUE self, RProc *blk){
  (void)blk;
  if(ALEN(self) == 0) return rb_str_new_cstr("[]");
  for(int i=0;i<insp_n;i++) if(insp_stack[i] == self) return rb_str_new_cstr("[...]");
  return ary_protect(ary_inspect_body, self, Qnil, &insp_n);
}

static VALUE ary_to_s(VALUE self, RProc *blk){
#if RUBY_TARGET < 20
  (void)blk;
  return ary_join(self, Qnil);
#else
  return ary_inspect(self, blk);
#endif
}

/* ===================== reverse / rotate ===================== */

static VALUE ary_reverse_bang(VALUE self, RProc *blk){
  (void)blk;
  ary_modify(self);
  VALUE *p = APTR(self);
  long i = 0, j = ALEN(self) - 1;
  while(i < j){ VALUE t = p[i]; p[i] = p[j]; p[j] = t; i++; j--; }
  return self;
}

static VALUE ary_reverse(VALUE self, RProc *blk){
  (void)blk;
  long len = ALEN(self);
  VALUE r = rb_ary_new_capa(len);
  for(long i=0;i<len;i++) APTR(r)[i] = APTR(self)[len-1-i];
  RARR(r)->len = len;
  return r;
}

#if RUBY_TARGET >= 20
static long rotate_count(long cnt, long len){
  return (cnt < 0) ? (len - (~cnt % len) - 1) : (cnt % len);
}

static VALUE ary_rotate_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  ary_modify(self);
  long cnt = argc > 0 ? rb_num2long(argv[0]) : 1;
  long len = ALEN(self);
  if(len > 0){
    cnt = rotate_count(cnt, len);
    if(cnt > 0){
      VALUE *tmp = xmalloc(sizeof(VALUE)*cnt);
      memcpy(tmp, APTR(self), sizeof(VALUE)*cnt);
      memmove(APTR(self), APTR(self)+cnt, sizeof(VALUE)*(len-cnt));
      memcpy(APTR(self)+len-cnt, tmp, sizeof(VALUE)*cnt);
      free(tmp);
    }
  }
  return self;
}

static VALUE ary_rotate(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 0, 1);
  long cnt = argc > 0 ? rb_num2long(argv[0]) : 1;
  long len = ALEN(self);
  VALUE r = rb_ary_new_capa(len);
  if(len > 0){
    cnt = rotate_count(cnt, len);
    for(long i=0;i<len;i++) APTR(r)[i] = APTR(self)[(cnt+i) % len];
  }
  RARR(r)->len = len;
  return r;
}
#endif

/* ===================== comparison helpers ===================== */

static void ary_cmperr(VALUE x, VALUE y) __attribute__((noreturn));
static void ary_cmperr(VALUE x, VALUE y){
  VALUE ys;
  int t = FIXNUM_P(y) ? T_FIXNUM : TYPE(y);
  if(t == T_FIXNUM || t == T_NIL || t == T_TRUE || t == T_FALSE ||
     t == T_SYMBOL || t == T_FLOAT)
    ys = rb_inspect(y);
  else
    ys = rb_class_name(rb_class_of(y));
  VALUE xs = rb_class_name(rb_class_of(x));
  rb_raise(rb_eArgError, "comparison of %s with %s failed",
           rb_str_cstr(xs), rb_str_cstr(ys));
}

static int ary_cmpint(VALUE v, VALUE a, VALUE b){
  if(NIL_P(v)) ary_cmperr(a, b);
  if(FIXNUM_P(v)){ long l = FIX2LONG(v); return l > 0 ? 1 : (l < 0 ? -1 : 0); }
  if(TYPE(v) == T_FLOAT){ double d = RFLT(v)->f; return d > 0 ? 1 : (d < 0 ? -1 : 0); }
  if(RTEST(rb_funcall(v, ID_CACHED(">"), 1, LONG2FIX(0)))) return 1;
  if(RTEST(rb_funcall(v, ID_CACHED("<"), 1, LONG2FIX(0)))) return -1;
  return 0;
}

static int ary_sort_cmp(VALUE a, VALUE b, RProc *blk){
  VALUE r;
  if(blk){ VALUE args[2]; args[0] = a; args[1] = b; r = rb_yield_values(blk, 2, args); }
  else r = rb_funcall(a, ID_CACHED("<=>"), 1, b);
  return ary_cmpint(r, a, b);
}

/* stable merge sort over vals[], optionally keyed by keys[] */
static void ary_merge(VALUE *v, VALUE *k, VALUE *tv, VALUE *tk,
                      long lo, long mid, long hi, RProc *blk){
  long i = lo, j = mid, o = lo;
  while(i < mid && j < hi){
    VALUE a = k ? k[i] : v[i], b = k ? k[j] : v[j];
    if(ary_sort_cmp(a, b, k ? NULL : blk) <= 0){
      tv[o] = v[i]; if(k) tk[o] = k[i]; i++;
    } else {
      tv[o] = v[j]; if(k) tk[o] = k[j]; j++;
    }
    o++;
  }
  while(i < mid){ tv[o] = v[i]; if(k) tk[o] = k[i]; i++; o++; }
  while(j < hi){  tv[o] = v[j]; if(k) tk[o] = k[j]; j++; o++; }
  for(long x=lo;x<hi;x++){ v[x] = tv[x]; if(k) k[x] = tk[x]; }
}

static void ary_msort(VALUE *v, VALUE *k, VALUE *tv, VALUE *tk,
                      long lo, long hi, RProc *blk){
  if(hi - lo < 2) return;
  long mid = lo + (hi - lo) / 2;
  ary_msort(v, k, tv, tk, lo, mid, blk);
  ary_msort(v, k, tv, tk, mid, hi, blk);
  ary_merge(v, k, tv, tk, lo, mid, hi, blk);
}

static void ary_sort_buf(VALUE *vals, VALUE *keys, long n, RProc *blk){
  if(n < 2) return;
  VALUE *tv = xmalloc(sizeof(VALUE)*n);
  VALUE *tk = keys ? xmalloc(sizeof(VALUE)*n) : NULL;
  ary_msort(vals, keys, tv, tk, 0, n, blk);
  free(tv); free(tk);
}

/* ===================== sort / min / max / sum ===================== */

static VALUE ary_sort_bang(VALUE self, RProc *blk){
  ary_modify(self);
  long n = ALEN(self);
  if(n > 1){
    VALUE *buf = xmalloc(sizeof(VALUE)*n);
    memcpy(buf, APTR(self), sizeof(VALUE)*n);
    ary_sort_buf(buf, NULL, n, blk);
    ary_ensure_capa(self, n);
    memcpy(APTR(self), buf, sizeof(VALUE)*n);
    RARR(self)->len = n;
    free(buf);
  }
  return self;
}

static VALUE ary_sort(VALUE self, RProc *blk){
  long n = ALEN(self);
  VALUE r = ary_new_from(n, APTR(self));
  if(n > 1) ary_sort_buf(APTR(r), NULL, n, blk);
  return r;
}

#if RUBY_TARGET >= 20
static VALUE ary_sort_by_bang(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "sort_by!", 0, NULL);
  ary_modify(self);
  long n = ALEN(self);
  if(n > 1){
    VALUE *vals = xmalloc(sizeof(VALUE)*n);
    VALUE *keys = xmalloc(sizeof(VALUE)*n);
    memcpy(vals, APTR(self), sizeof(VALUE)*n);
    for(long i=0;i<n;i++) keys[i] = rb_yield1(blk, vals[i]);
    ary_sort_buf(vals, keys, n, NULL);
    ary_ensure_capa(self, n);
    memcpy(APTR(self), vals, sizeof(VALUE)*n);
    RARR(self)->len = n;
    free(vals); free(keys);
  }
  return self;
}
#endif

#if RUBY_TARGET >= 27
/* the n smallest (dir<0) or n largest (dir>0), in order */
static VALUE ary_nmin_run(VALUE self, VALUE num, RProc *blk, int dir){
  long n = rb_num2long(num);
  if(n < 0) rb_raise(rb_eArgError, "negative size (%ld)", n);
  long len = ALEN(self);
  VALUE *buf = xmalloc(sizeof(VALUE)*(len > 0 ? len : 1));
  memcpy(buf, APTR(self), sizeof(VALUE)*len);
  ary_sort_buf(buf, NULL, len, blk);
  if(n > len) n = len;
  VALUE r = rb_ary_new_capa(n > 0 ? n : 1);
  for(long i=0;i<n;i++) APTR(r)[i] = dir < 0 ? buf[i] : buf[len-1-i];
  RARR(r)->len = n;
  free(buf);
  return r;
}
#endif

static VALUE ary_minmax(VALUE self, int argc, VALUE *argv, RProc *blk, int dir){
#if RUBY_TARGET >= 27
  ary_check_arity(argc, 0, 1);
  if(argc == 1 && !NIL_P(argv[0])) return ary_nmin_run(self, argv[0], blk, dir);
#else
  ary_check_arity(argc, 0, 0);
  (void)argv;
#endif
  VALUE result = Qundef;
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    if(result == Qundef){ result = v; continue; }
    int c;
    if(blk){
      VALUE args[2]; args[0] = v; args[1] = result;
      c = ary_cmpint(rb_yield_values(blk, 2, args), v, result);
    }
    else c = ary_sort_cmp(v, result, NULL);
    if(dir > 0 ? (c > 0) : (c < 0)) result = v;
  }
  return result == Qundef ? Qnil : result;
}

static VALUE ary_max(VALUE self, int argc, VALUE *argv, RProc *blk){
  return ary_minmax(self, argc, argv, blk, 1);
}
static VALUE ary_min(VALUE self, int argc, VALUE *argv, RProc *blk){
  return ary_minmax(self, argc, argv, blk, -1);
}

#if RUBY_TARGET >= 27
static VALUE ary_sum(VALUE self, int argc, VALUE *argv, RProc *blk){
  ary_check_arity(argc, 0, 1);
  VALUE v = argc > 0 ? argv[0] : LONG2FIX(0);
  for(long i=0;i<ALEN(self);i++){
    VALUE e = APTR(self)[i];
    if(blk) e = rb_yield1(blk, e);
    v = rb_funcall(v, ID_CACHED("+"), 1, e);
  }
  return v;
}
#endif

/* ===================== uniq / compact / flatten ===================== */

static VALUE ary_uniq_hash(VALUE self, RProc *blk, VALUE keep){
  VALUE h = rb_hash_new();
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    VALUE k = v;
#if RUBY_TARGET >= 20
    if(blk) k = rb_yield1(blk, v);
#else
    (void)blk;
#endif
    if(rb_hash_lookup(h, k, Qundef) == Qundef){
      rb_hash_aset(h, k, v);
      rb_ary_push(keep, v);
    }
  }
  return keep;
}

static VALUE ary_uniq(VALUE self, RProc *blk){
  VALUE r = ary_alloc(ary_partial_class(self), ALEN(self));
  return ary_uniq_hash(self, blk, r);
}

static VALUE ary_uniq_bang(VALUE self, RProc *blk){
  ary_modify(self);
  long olen = ALEN(self);
  VALUE keep = ary_uniq_hash(self, blk, rb_ary_new_capa(olen));
  if(ALEN(keep) == olen) return Qnil;
  ary_ensure_capa(self, ALEN(keep));
  memcpy(APTR(self), APTR(keep), sizeof(VALUE)*ALEN(keep));
  RARR(self)->len = ALEN(keep);
  return self;
}

static VALUE ary_compact(VALUE self, RProc *blk){
  (void)blk;
  VALUE r = rb_ary_new_capa(ALEN(self));
  for(long i=0;i<ALEN(self);i++) if(!NIL_P(APTR(self)[i])) rb_ary_push(r, APTR(self)[i]);
  return r;
}

static VALUE ary_compact_bang(VALUE self, RProc *blk){
  (void)blk;
  ary_modify(self);
  long i = 0, j = 0, len = ALEN(self);
  for(;i<len;i++) if(!NIL_P(APTR(self)[i])) APTR(self)[j++] = APTR(self)[i];
  if(j == len) return Qnil;
  RARR(self)->len = j;
  return self;
}

static void ary_flatten_1(VALUE result, VALUE ary, long level, int *mod,
                          VALUE *stack, int *sp){
  for(long i=0;i<ALEN(ary);i++){
    VALUE e = APTR(ary)[i];
    VALUE t = (level == 0) ? Qnil : rb_check_array_type(e);
    if(NIL_P(t)){ rb_ary_push(result, e); continue; }
    *mod = 1;
    for(int k=0;k<*sp;k++)
      if(stack[k] == t) rb_raise(rb_eArgError, "tried to flatten recursive array");
    if(*sp >= REC_MAX) rb_raise(rb_eArgError, "tried to flatten recursive array");
    stack[(*sp)++] = t;
    ary_flatten_1(result, t, level - 1, mod, stack, sp);
    (*sp)--;
  }
}

static long ary_flatten_level(int argc, VALUE *argv){
  ary_check_arity(argc, 0, 1);
  if(argc == 0 || NIL_P(argv[0])) return -1;
  return rb_num2long(argv[0]);
}

static VALUE ary_flatten(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long level = ary_flatten_level(argc, argv);
  VALUE result = ary_alloc(ary_partial_class(self), ALEN(self));
  VALUE stack[REC_MAX]; int sp = 0, mod = 0;
  stack[sp++] = self;
  ary_flatten_1(result, self, level, &mod, stack, &sp);
  return result;
}

static VALUE ary_flatten_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long level = ary_flatten_level(argc, argv);
  ary_modify(self);
  VALUE result = rb_ary_new_capa(ALEN(self));
  VALUE stack[REC_MAX]; int sp = 0, mod = 0;
  stack[sp++] = self;
  ary_flatten_1(result, self, level, &mod, stack, &sp);
  if(!mod) return Qnil;
  ary_ensure_capa(self, ALEN(result));
  memcpy(APTR(self), APTR(result), sizeof(VALUE)*ALEN(result));
  RARR(self)->len = ALEN(result);
  return self;
}

/* ===================== deletion ===================== */

static VALUE ary_delete(VALUE self, VALUE item, RProc *blk){
  VALUE v = item;
  long i1, i2 = 0;
  for(i1=0;i1<ALEN(self);i1++){
    VALUE e = APTR(self)[i1];
    if(rb_equal(e, item)){ v = e; continue; }
    if(i1 != i2) ary_store(self, i2, e);
    i2++;
  }
  if(ALEN(self) == i2){
    if(blk) return rb_yield1(blk, item);
    return Qnil;
  }
  ary_modify(self);
  RARR(self)->len = i2;
  return v;
}

static VALUE ary_delete_at_n(VALUE self, long pos){
  long len = ALEN(self);
  if(pos >= len) return Qnil;
  if(pos < 0){ pos += len; if(pos < 0) return Qnil; }
  ary_modify(self);
  VALUE del = APTR(self)[pos];
  memmove(APTR(self)+pos, APTR(self)+pos+1, sizeof(VALUE)*(len-pos-1));
  RARR(self)->len = len - 1;
  return del;
}

static VALUE ary_delete_at(VALUE self, VALUE pos, RProc *blk){
  (void)blk;
  return ary_delete_at_n(self, rb_num2long(pos));
}

static VALUE ary_slice_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_modify(self);
  ary_check_arity(argc, 1, 2);
  long pos, len, orig_len;

  if(argc == 2){ pos = rb_num2long(argv[0]); len = rb_num2long(argv[1]); }
  else {
    if(!FIXNUM_P(argv[0])){
      switch(ary_range_beg_len(argv[0], &pos, &len, ALEN(self), 0)){
        case RANGE_OK: goto delete_pos_len;
        case RANGE_OUT: return Qnil;
        default: break;
      }
    }
    return ary_delete_at_n(self, rb_num2long(argv[0]));
  }
 delete_pos_len:
  if(len < 0) return Qnil;
  orig_len = ALEN(self);
  if(pos < 0){ pos += orig_len; if(pos < 0) return Qnil; }
  else if(orig_len < pos) return Qnil;
  if(orig_len < pos + len) len = orig_len - pos;
  if(len == 0) return rb_ary_new_capa(0);
  VALUE result = ary_make_partial(self, pos, len);
  ary_splice(self, pos, len, NULL, 0);
  return result;
}

/* keep == 0: drop the elements the block likes (reject!/delete_if)
   keep == 1: keep the elements the block likes (select!/keep_if) */
static long ary_filter_bang(VALUE self, RProc *blk, int keep){
  long i1, i2 = 0;
  for(i1=0;i1<ALEN(self);i1++){
    VALUE v = APTR(self)[i1];
    int t = RTEST(rb_yield1(blk, v));
    if(keep ? !t : t) continue;
    if(i2 != i1) APTR(self)[i2] = v;
    i2++;
  }
  long removed = ALEN(self) - i2;
  RARR(self)->len = i2;
  return removed;
}

static VALUE ary_reject_bang(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "reject!", 0, NULL);
  ary_modify(self);
  return ary_filter_bang(self, blk, 0) == 0 ? Qnil : self;
}

static VALUE ary_delete_if(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "delete_if", 0, NULL);
  ary_modify(self);
  ary_filter_bang(self, blk, 0);
  return self;
}

#if RUBY_TARGET >= 20
static VALUE ary_select_bang_impl(VALUE self, RProc *blk, const char *name){
  if(!blk) return ary_enum(self, name, 0, NULL);
  ary_modify(self);
  return ary_filter_bang(self, blk, 1) == 0 ? Qnil : self;
}
static VALUE ary_select_bang(VALUE self, RProc *blk){
  return ary_select_bang_impl(self, blk, "select!");
}
static VALUE ary_keep_if(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "keep_if", 0, NULL);
  ary_modify(self);
  ary_filter_bang(self, blk, 1);
  return self;
}
#if RUBY_TARGET >= 27
static VALUE ary_filter_bang_m(VALUE self, RProc *blk){
  return ary_select_bang_impl(self, blk, "filter!");
}
#endif
#endif

static VALUE ary_reject(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "reject", 0, NULL);
  VALUE r = rb_ary_new_capa(ALEN(self));
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    if(!RTEST(rb_yield1(blk, v))) rb_ary_push(r, v);
  }
  return r;
}

static VALUE ary_select_impl(VALUE self, RProc *blk, const char *name){
  if(!blk) return ary_enum(self, name, 0, NULL);
  VALUE r = rb_ary_new_capa(ALEN(self));
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    if(RTEST(rb_yield1(blk, v))) rb_ary_push(r, v);
  }
  return r;
}
static VALUE ary_select(VALUE self, RProc *blk){ return ary_select_impl(self, blk, "select"); }
#if RUBY_TARGET >= 27
static VALUE ary_filter(VALUE self, RProc *blk){ return ary_select_impl(self, blk, "filter"); }
#endif

/* ===================== map ===================== */

static VALUE ary_collect_impl(VALUE self, RProc *blk, const char *name){
  if(!blk) return ary_enum(self, name, 0, NULL);
  VALUE r = rb_ary_new_capa(ALEN(self));
  for(long i=0;i<ALEN(self);i++) rb_ary_push(r, rb_yield1(blk, APTR(self)[i]));
  return r;
}
static VALUE ary_map(VALUE self, RProc *blk){ return ary_collect_impl(self, blk, "map"); }
static VALUE ary_collect(VALUE self, RProc *blk){ return ary_collect_impl(self, blk, "collect"); }

static VALUE ary_collect_bang_impl(VALUE self, RProc *blk, const char *name){
  if(!blk) return ary_enum(self, name, 0, NULL);
  ary_modify(self);
  for(long i=0;i<ALEN(self);i++) ary_store(self, i, rb_yield1(blk, APTR(self)[i]));
  return self;
}
static VALUE ary_map_bang(VALUE self, RProc *blk){ return ary_collect_bang_impl(self, blk, "map!"); }
static VALUE ary_collect_bang(VALUE self, RProc *blk){ return ary_collect_bang_impl(self, blk, "collect!"); }

#if RUBY_TARGET >= 20
static VALUE ary_flat_map_impl(VALUE self, RProc *blk, const char *name){
  if(!blk) return ary_enum(self, name, 0, NULL);
  VALUE r = rb_ary_new_capa(ALEN(self));
  for(long i=0;i<ALEN(self);i++){
    VALUE v = rb_yield1(blk, APTR(self)[i]);
    VALUE t = rb_check_array_type(v);
    if(NIL_P(t)) rb_ary_push(r, v);
    else for(long j=0;j<ALEN(t);j++) rb_ary_push(r, APTR(t)[j]);
  }
  return r;
}
static VALUE ary_flat_map(VALUE self, RProc *blk){ return ary_flat_map_impl(self, blk, "flat_map"); }
static VALUE ary_collect_concat(VALUE self, RProc *blk){ return ary_flat_map_impl(self, blk, "collect_concat"); }
#endif

/* ===================== take / drop / cycle / zip ===================== */

static VALUE ary_take(VALUE self, VALUE n, RProc *blk){
  (void)blk;
  long len = rb_num2long(n);
  if(len < 0) rb_raise(rb_eArgError, "attempt to take negative size");
  if(len > ALEN(self)) len = ALEN(self);
  return ary_make_partial(self, 0, len);
}

static VALUE ary_take_while(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "take_while", 0, NULL);
  long i;
  for(i=0;i<ALEN(self);i++) if(!RTEST(rb_yield1(blk, APTR(self)[i]))) break;
  return ary_make_partial(self, 0, i);
}

static VALUE ary_drop(VALUE self, VALUE n, RProc *blk){
  (void)blk;
  long pos = rb_num2long(n);
  if(pos < 0) rb_raise(rb_eArgError, "attempt to drop negative size");
  if(pos > ALEN(self)) pos = ALEN(self);
  return ary_make_partial(self, pos, ALEN(self) - pos);
}

static VALUE ary_drop_while(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "drop_while", 0, NULL);
  long i;
  for(i=0;i<ALEN(self);i++) if(!RTEST(rb_yield1(blk, APTR(self)[i]))) break;
  return ary_make_partial(self, i, ALEN(self) - i);
}

static VALUE ary_cycle(VALUE self, int argc, VALUE *argv, RProc *blk){
  ary_check_arity(argc, 0, 1);
  if(!blk) return ary_enum(self, "cycle", argc, argv);
  long n = -1;
  if(argc > 0 && !NIL_P(argv[0])){
    n = rb_num2long(argv[0]);
    if(n <= 0) return Qnil;
  }
  while(ALEN(self) > 0 && (n < 0 || 0 < n--))
    for(long i=0;i<ALEN(self);i++) rb_yield1(blk, APTR(self)[i]);
  return Qnil;
}

static VALUE ary_zip(VALUE self, int argc, VALUE *argv, RProc *blk){
  long len = ALEN(self);
  VALUE *others = xmalloc(sizeof(VALUE)*(argc > 0 ? argc : 1));
  for(int i=0;i<argc;i++){
    VALUE t = rb_check_array_type(argv[i]);
    if(NIL_P(t)) t = rb_funcall(argv[i], ID_CACHED("to_a"), 0);
    others[i] = t;
  }
  VALUE result = blk ? Qnil : rb_ary_new_capa(len);
  for(long i=0;i<len;i++){
    VALUE tuple = rb_ary_new_capa(argc+1);
    rb_ary_push(tuple, APTR(self)[i]);
    for(int j=0;j<argc;j++) rb_ary_push(tuple, rb_ary_entry(others[j], i));
    if(blk) rb_yield1(blk, tuple);
    else rb_ary_push(result, tuple);
  }
  free(others);
  return result;
}

/* ===================== set operations ===================== */

static VALUE ary_plus(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  VALUE o = to_ary(other);
  long n1 = ALEN(self), n2 = ALEN(o);
  VALUE r = rb_ary_new_capa(n1+n2 > 0 ? n1+n2 : 1);
  for(long i=0;i<n1;i++) APTR(r)[i] = APTR(self)[i];
  for(long i=0;i<n2;i++) APTR(r)[n1+i] = APTR(o)[i];
  RARR(r)->len = n1+n2;
  return r;
}

static VALUE ary_make_hash(VALUE ary){
  VALUE h = rb_hash_new();
  for(long i=0;i<ALEN(ary);i++) rb_hash_aset(h, APTR(ary)[i], APTR(ary)[i]);
  return h;
}

static VALUE ary_diff_n(VALUE self, int argc, VALUE *argv){
  VALUE h = rb_hash_new();
  for(int i=0;i<argc;i++){
    VALUE o = to_ary(argv[i]);
    for(long j=0;j<ALEN(o);j++) rb_hash_aset(h, APTR(o)[j], Qtrue);
  }
  VALUE r = rb_ary_new();
  for(long i=0;i<ALEN(self);i++)
    if(rb_hash_lookup(h, APTR(self)[i], Qundef) == Qundef) rb_ary_push(r, APTR(self)[i]);
  return r;
}

static VALUE ary_diff(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  return ary_diff_n(self, 1, &other);
}

static VALUE ary_and_n(VALUE self, int argc, VALUE *argv){
  VALUE h = ary_make_hash(self);
  for(int i=0;i<argc;i++){
    VALUE o = to_ary(argv[i]);
    VALUE h2 = rb_hash_new();
    for(long j=0;j<ALEN(o);j++){
      VALUE v = APTR(o)[j];
      if(rb_hash_lookup(h, v, Qundef) != Qundef) rb_hash_aset(h2, v, v);
    }
    h = h2;
  }
  /* the receiver's order, without duplicates */
  VALUE r = rb_ary_new();
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    if(rb_hash_delete(h, v, NULL)) rb_ary_push(r, v);
  }
  return r;
}

static VALUE ary_and(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  VALUE o = to_ary(other);
  if(ALEN(self) == 0 || ALEN(o) == 0) return rb_ary_new();
  return ary_and_n(self, 1, &o);
}

static VALUE ary_or_n(VALUE self, int argc, VALUE *argv){
  VALUE h = rb_hash_new(), r = rb_ary_new();
  for(long i=0;i<ALEN(self);i++){
    VALUE v = APTR(self)[i];
    if(rb_hash_lookup(h, v, Qundef) == Qundef){ rb_hash_aset(h, v, v); rb_ary_push(r, v); }
  }
  for(int k=0;k<argc;k++){
    VALUE o = to_ary(argv[k]);
    for(long i=0;i<ALEN(o);i++){
      VALUE v = APTR(o)[i];
      if(rb_hash_lookup(h, v, Qundef) == Qundef){ rb_hash_aset(h, v, v); rb_ary_push(r, v); }
    }
  }
  return r;
}

static VALUE ary_or(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  return ary_or_n(self, 1, &other);
}

#if RUBY_TARGET >= 27
static VALUE ary_difference(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc == 0) return ary_new_from(ALEN(self), APTR(self));
  return ary_diff_n(self, argc, argv);
}
static VALUE ary_union(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  return ary_or_n(self, argc, argv);
}
static VALUE ary_intersection(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc == 0) return ary_new_from(ALEN(self), APTR(self));
  return ary_and_n(self, argc, argv);
}
#endif

#if RUBY_TARGET >= 31
static VALUE ary_intersect_p(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  VALUE o = to_ary(other);
  if(ALEN(self) == 0 || ALEN(o) == 0) return Qfalse;
  VALUE h = ary_make_hash(self);
  for(long i=0;i<ALEN(o);i++)
    if(rb_hash_lookup(h, APTR(o)[i], Qundef) != Qundef) return Qtrue;
  return Qfalse;
}
#endif

static VALUE ary_times(VALUE self, VALUE times, RProc *blk){
  (void)blk;
  VALUE tmp = rb_check_string_type(times);
  if(!NIL_P(tmp)) return ary_join(self, tmp);

  long n = rb_num2long(times);
  if(n < 0) rb_raise(rb_eArgError, "negative argument");
  long len = ALEN(self);
  if(len > 0 && n > ARY_MAX_SIZE / len) rb_raise(rb_eArgError, "argument too big");
  VALUE r = ary_alloc(ary_partial_class(self), len*n > 0 ? len*n : 1);
  for(long i=0;i<n;i++)
    for(long j=0;j<len;j++) APTR(r)[i*len+j] = APTR(self)[j];
  RARR(r)->len = len*n;
  return r;
}

/* ===================== product / combination / permutation ===================== */

static VALUE ary_product(VALUE self, int argc, VALUE *argv, RProc *blk){
  int n = argc + 1;
  VALUE *arrays = xmalloc(sizeof(VALUE)*n);
  long *counters = xcalloc(n, sizeof(long));
  arrays[0] = self;
  for(int i=1;i<n;i++) arrays[i] = to_ary(argv[i-1]);

  long resultlen = 1;
  int empty = 0;
  for(int i=0;i<n;i++){
    long k = ALEN(arrays[i]);
    if(k == 0){ empty = 1; break; }
    if(resultlen > ARY_MAX_SIZE / k) rb_raise(rb_eRangeError, "too big to product");
    resultlen *= k;
  }
  VALUE result = blk ? Qnil : rb_ary_new_capa(empty ? 1 : resultlen);
  if(!empty){
    for(;;){
      VALUE sub = rb_ary_new_capa(n);
      for(int j=0;j<n;j++) rb_ary_push(sub, rb_ary_entry(arrays[j], counters[j]));
      if(blk) rb_yield1(blk, sub);
      else rb_ary_push(result, sub);
      int m = n - 1;
      counters[m]++;
      while(counters[m] == ALEN(arrays[m])){
        counters[m] = 0;
        if(--m < 0) goto done;
        counters[m]++;
      }
    }
  }
 done:
  free(arrays); free(counters);
  return blk ? self : result;
}

static void comb_rec(VALUE ary, long n, long *idx, long depth, long start, RProc *blk){
  if(depth == n){
    VALUE t = rb_ary_new_capa(n > 0 ? n : 1);
    for(long i=0;i<n;i++) rb_ary_push(t, rb_ary_entry(ary, idx[i]));
    rb_yield1(blk, t);
    return;
  }
  for(long i=start;i<ALEN(ary);i++){
    idx[depth] = i;
    comb_rec(ary, n, idx, depth+1, i+1, blk);
  }
}

static VALUE ary_combination(VALUE self, VALUE num, RProc *blk){
  if(!blk){ VALUE a[1]; a[0] = num; return ary_enum(self, "combination", 1, a); }
  long n = rb_num2long(num);
  long len = ALEN(self);
  if(n == 0) rb_yield1(blk, rb_ary_new_capa(1));
  else if(n > 0 && n <= len){
    long *idx = xcalloc(n, sizeof(long));
    comb_rec(self, n, idx, 0, 0, blk);
    free(idx);
  }
  return self;
}

static void perm_rec(VALUE ary, long r, long *idx, char *used, long depth, RProc *blk){
  if(depth == r){
    VALUE t = rb_ary_new_capa(r > 0 ? r : 1);
    for(long i=0;i<r;i++) rb_ary_push(t, rb_ary_entry(ary, idx[i]));
    rb_yield1(blk, t);
    return;
  }
  long len = ALEN(ary);
  for(long i=0;i<len;i++){
    if(used[i]) continue;
    used[i] = 1; idx[depth] = i;
    perm_rec(ary, r, idx, used, depth+1, blk);
    used[i] = 0;
  }
}

static VALUE ary_permutation(VALUE self, int argc, VALUE *argv, RProc *blk){
  ary_check_arity(argc, 0, 1);
  if(!blk) return ary_enum(self, "permutation", argc, argv);
  long len = ALEN(self);
  long r = argc > 0 ? rb_num2long(argv[0]) : len;
  if(r == 0) rb_yield1(blk, rb_ary_new_capa(1));
  else if(r > 0 && r <= len){
    long *idx = xcalloc(r, sizeof(long));
    char *used = xcalloc(len > 0 ? len : 1, 1);
    perm_rec(self, r, idx, used, 0, blk);
    free(idx); free(used);
  }
  return self;
}

static VALUE ary_transpose(VALUE self, RProc *blk){
  (void)blk;
  long alen = ALEN(self), elen = -1;
  VALUE result = Qnil;
  if(alen == 0) return rb_ary_new();
  for(long i=0;i<alen;i++){
    VALUE tmp = to_ary(APTR(self)[i]);
    if(elen < 0){
      elen = ALEN(tmp);
      result = rb_ary_new_capa(elen > 0 ? elen : 1);
      for(long j=0;j<elen;j++) rb_ary_push(result, rb_ary_new_capa(alen));
    }
    else if(elen != ALEN(tmp))
      rb_raise(rb_eIndexError, "element size differs (%ld should be %ld)", ALEN(tmp), elen);
    for(long j=0;j<elen;j++) rb_ary_push(rb_ary_entry(result, j), APTR(tmp)[j]);
  }
  return result;
}

/* ===================== fill / bsearch / dig ===================== */

static VALUE ary_fill(VALUE self, int argc, VALUE *argv, RProc *blk){
  VALUE item = Qundef, arg1 = Qnil, arg2 = Qnil;
  long beg = 0, len = 0, end;
  int n;

  if(blk){
    ary_check_arity(argc, 0, 2);
    if(argc > 0) arg1 = argv[0];
    if(argc > 1) arg2 = argv[1];
    n = argc + 1;
  }
  else {
    ary_check_arity(argc, 1, 3);
    item = argv[0];
    if(argc > 1) arg1 = argv[1];
    if(argc > 2) arg2 = argv[2];
    n = argc;
  }

  switch(n){
    case 1:
      beg = 0; len = ALEN(self);
      break;
    case 2:
      if(ary_range_beg_len(arg1, &beg, &len, ALEN(self), 1) == RANGE_OK) break;
      /* fall through */
    default:
      beg = NIL_P(arg1) ? 0 : rb_num2long(arg1);
      if(beg < 0){ beg += ALEN(self); if(beg < 0) beg = 0; }
      len = NIL_P(arg2) ? ALEN(self) - beg : rb_num2long(arg2);
      break;
  }
  ary_modify(self);
  if(len < 0) return self;
  if(beg >= ARY_MAX_SIZE || len > ARY_MAX_SIZE - beg)
    rb_raise(rb_eArgError, "argument too big");
  end = beg + len;
  if(ALEN(self) < end) ary_resize(self, end);

  if(item == Qundef){
    for(long i=beg;i<end;i++){
      VALUE v = rb_yield1(blk, LONG2FIX(i));
      if(i >= ALEN(self)) break;
      APTR(self)[i] = v;
    }
  }
  else {
    for(long i=beg;i<end;i++) APTR(self)[i] = item;
  }
  return self;
}

#if RUBY_TARGET >= 20
static VALUE ary_bsearch(VALUE self, RProc *blk){
  if(!blk) return ary_enum(self, "bsearch", 0, NULL);
  long low = 0, high = ALEN(self), mid;
  int smaller = 0, satisfied = 0;
  while(low < high){
    mid = low + ((high - low) / 2);
    VALUE val = rb_ary_entry(self, mid);
    VALUE v = rb_yield1(blk, val);
    if(FIXNUM_P(v)){
      if(v == LONG2FIX(0)) return val;
      smaller = FIX2LONG(v) < 0;
    }
    else if(v == Qtrue){ satisfied = 1; smaller = 1; }
    else if(!RTEST(v)) smaller = 0;
    else if(rb_is_a(v, rb_cNumeric)){
      switch(ary_cmpint(rb_funcall(v, ID_CACHED("<=>"), 1, LONG2FIX(0)), v, LONG2FIX(0))){
        case 0: return val;
        case 1: smaller = 0; break;
        default: smaller = 1; break;
      }
    }
    else rb_raise(rb_eTypeError, "wrong argument type %s (must be numeric, true, false or nil)",
                  rb_obj_classname(v));
    if(smaller) high = mid;
    else low = mid + 1;
  }
  if(!satisfied) return Qnil;
  return rb_ary_entry(self, low);
}
#endif

#if RUBY_TARGET >= 27
static VALUE ary_dig(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  ary_check_arity(argc, 1, -1);
  VALUE v = rb_ary_entry(self, rb_num2long(argv[0]));
  if(argc == 1 || NIL_P(v)) return v;
  if(!rb_respond_to(v, ID_CACHED("dig")))
    rb_raise(rb_eTypeError, "%s does not have #dig method", rb_obj_classname(v));
  return rb_call(v, ID_CACHED("dig"), argc-1, argv+1, NULL);
}
#endif

#if RUBY_TARGET >= 34
static VALUE ary_fetch_values(VALUE self, int argc, VALUE *argv, RProc *blk){
  VALUE result = rb_ary_new_capa(argc > 0 ? argc : 1);
  long alen = ALEN(self);
  for(int i=0;i<argc;i++){
    long idx = rb_num2long(argv[i]), j = idx;
    if(j < 0) j += alen;
    if(j < 0 || alen <= j){
      if(blk){ rb_ary_push(result, rb_yield1(blk, argv[i])); continue; }
      rb_raise(rb_eIndexError, "index %ld outside of array bounds: %ld...%ld", idx, -alen, alen);
    }
    rb_ary_push(result, APTR(self)[j]);
  }
  return result;
}
#endif

/* ===================== equality / hash ===================== */

enum { PAIR_EQ, PAIR_EQL, PAIR_CMP };
static struct { VALUE a, b; int kind; } pair_stack[REC_MAX];
static int pair_n;

static int pair_seen(VALUE a, VALUE b, int kind){
  for(int i=0;i<pair_n;i++)
    if(pair_stack[i].kind == kind && pair_stack[i].a == a && pair_stack[i].b == b) return 1;
  return 0;
}
static void pair_push(VALUE a, VALUE b, int kind){
  if(pair_n < REC_MAX){
    pair_stack[pair_n].a = a; pair_stack[pair_n].b = b;
    pair_stack[pair_n].kind = kind; pair_n++;
  }
}

static VALUE ary_equal_body(VALUE a, VALUE b){
  pair_push(a, b, PAIR_EQ);
  for(long i=0;i<ALEN(a);i++){
    if(i >= ALEN(b)) return Qfalse;
    if(!rb_equal(APTR(a)[i], APTR(b)[i])) return Qfalse;
  }
  return Qtrue;
}

static VALUE ary_equal(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(self == other) return Qtrue;
  if(!is_ary(other)){
    if(FIXNUM_P(other) || !rb_respond_to(other, ID_CACHED("to_ary"))) return Qfalse;
    return rb_equal(other, self) ? Qtrue : Qfalse;
  }
  if(ALEN(self) != ALEN(other)) return Qfalse;
  if(pair_seen(self, other, PAIR_EQ)) return Qtrue;
  return ary_protect(ary_equal_body, self, other, &pair_n);
}

static VALUE ary_eql_body(VALUE a, VALUE b){
  pair_push(a, b, PAIR_EQL);
  for(long i=0;i<ALEN(a);i++){
    if(i >= ALEN(b)) return Qfalse;
    if(!rb_eql(APTR(a)[i], APTR(b)[i])) return Qfalse;
  }
  return Qtrue;
}

static VALUE ary_eql(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(self == other) return Qtrue;
  if(!is_ary(other)) return Qfalse;
  if(ALEN(self) != ALEN(other)) return Qfalse;
  if(pair_seen(self, other, PAIR_EQL)) return Qtrue;
  return ary_protect(ary_eql_body, self, other, &pair_n);
}

static VALUE ary_cmp_body(VALUE a, VALUE b){
  pair_push(a, b, PAIR_CMP);
  long len = ALEN(a);
  if(len > ALEN(b)) len = ALEN(b);
  for(long i=0;i<len;i++){
    VALUE v = rb_funcall(rb_ary_entry(a, i), ID_CACHED("<=>"), 1, rb_ary_entry(b, i));
    if(v != LONG2FIX(0)) return v;
  }
  return Qundef;
}

static VALUE ary_cmp(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  VALUE o = rb_check_array_type(other);
  if(NIL_P(o)) return Qnil;
  if(self == o) return LONG2FIX(0);
  VALUE v = pair_seen(self, o, PAIR_CMP) ? Qundef
          : ary_protect(ary_cmp_body, self, o, &pair_n);
  if(v != Qundef) return v;
  long len = ALEN(self) - ALEN(o);
  return LONG2FIX(len == 0 ? 0 : (len > 0 ? 1 : -1));
}

static VALUE ary_hash_body(VALUE ary, VALUE unused){
  (void)unused;
  if(hash_n < REC_MAX) hash_stack[hash_n++] = ary;
  unsigned long h = 7;
  for(long i=0;i<ALEN(ary);i++) h = h*31 + (unsigned long)rb_hash_code(APTR(ary)[i]);
  return LONG2FIX((long)(h & (unsigned long)FIXNUM_MAX));
}

static VALUE ary_hash(VALUE self, RProc *blk){
  (void)blk;
  for(int i=0;i<hash_n;i++) if(hash_stack[i] == self) return LONG2FIX(0);
  return ary_protect(ary_hash_body, self, Qnil, &hash_n);
}

/* ===================== conversion / copying ===================== */

static VALUE ary_to_a(VALUE self, RProc *blk){
  (void)blk;
  if(rb_class_of(self) != rb_cArray) return ary_new_from(ALEN(self), APTR(self));
  return self;
}

static VALUE ary_to_ary_m(VALUE self, RProc *blk){ (void)blk; return self; }

static void ary_copy_ivars(VALUE dst, VALUE src){
  ivtbl *t = BASIC(src)->iv;
  if(!t) return;
  for(int i=0;i<t->n;i++) ivtbl_set(&BASIC(dst)->iv, t->keys[i], t->vals[i]);
}

static VALUE ary_dup(VALUE self, RProc *blk){
  (void)blk;
  VALUE d = ary_alloc(rb_class_of(self), ALEN(self));
  for(long i=0;i<ALEN(self);i++) APTR(d)[i] = APTR(self)[i];
  RARR(d)->len = ALEN(self);
  ary_copy_ivars(d, self);
  return d;
}

static VALUE ary_clone(VALUE self, int argc, VALUE *argv, RProc *blk){
  int freeze = -1;
#if RUBY_TARGET >= 31
  ary_check_arity(argc, 0, 1);
  if(argc == 1){
    if(FIXNUM_P(argv[0]) || TYPE(argv[0]) != T_HASH) ary_arity_err(argc, 0, 0);
    VALUE f = rb_hash_lookup(argv[0], rb_sym(ID_CACHED("freeze")), Qundef);
    if(f != Qundef){
      if(f == Qtrue) freeze = 1;
      else if(f == Qfalse) freeze = 0;
      else if(!NIL_P(f)) rb_raise(rb_eArgError, "unexpected value for freeze: %s",
                                  rb_obj_classname(f));
    }
  }
#else
  ary_check_arity(argc, 0, 0);
  (void)argv;
#endif
  VALUE d = ary_dup(self, blk);
  if(freeze == 1 || (freeze < 0 && rb_obj_frozen(self))) rb_obj_freeze(d);
  return d;
}

static VALUE ary_freeze(VALUE self, RProc *blk){ (void)blk; return rb_obj_freeze(self); }
static VALUE ary_frozen_p(VALUE self, RProc *blk){
  (void)blk;
  return rb_obj_frozen(self) ? Qtrue : Qfalse;
}

#if RUBY_TARGET < 20
static VALUE ary_nitems(VALUE self, RProc *blk){
  (void)blk;
  long n = 0;
  for(long i=0;i<ALEN(self);i++) if(!NIL_P(APTR(self)[i])) n++;
  return LONG2FIX(n);
}
#endif

/* ===================== registration ===================== */

void rb_init_array(void){
  rb_define_singleton_method((VALUE)rb_cArray, "new", ary_s_new, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cArray, "[]", ary_s_create, ARGC_ANY);

  rb_define_private_method(rb_cArray, "initialize", ary_initialize, ARGC_ANY);
  rb_define_private_method(rb_cArray, "initialize_copy", ary_replace, 1);

  rb_define_method(rb_cArray, "[]", ary_aref, ARGC_ANY);
  rb_define_method(rb_cArray, "slice", ary_aref, ARGC_ANY);
  rb_define_method(rb_cArray, "[]=", ary_aset, ARGC_ANY);
  rb_define_method(rb_cArray, "at", ary_at, 1);
  rb_define_method(rb_cArray, "fetch", ary_fetch, ARGC_ANY);
  rb_define_method(rb_cArray, "first", ary_first, ARGC_ANY);
  rb_define_method(rb_cArray, "last", ary_last, ARGC_ANY);
  rb_define_method(rb_cArray, "values_at", ary_values_at, ARGC_ANY);
  rb_define_method(rb_cArray, "slice!", ary_slice_bang, ARGC_ANY);

  rb_define_method(rb_cArray, "push", ary_push_m, ARGC_ANY);
  rb_define_method(rb_cArray, "<<", ary_push_one, 1);
  rb_define_method(rb_cArray, "pop", ary_pop, ARGC_ANY);
  rb_define_method(rb_cArray, "shift", ary_shift, ARGC_ANY);
  rb_define_method(rb_cArray, "unshift", ary_unshift, ARGC_ANY);
  rb_define_method(rb_cArray, "insert", ary_insert, ARGC_ANY);
  rb_define_method(rb_cArray, "concat", ary_concat, ARGC_ANY);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cArray, "prepend", ary_unshift, ARGC_ANY);
  rb_define_method(rb_cArray, "append", ary_push_m, ARGC_ANY);
#endif

  rb_define_method(rb_cArray, "each", ary_each, 0);
  rb_define_method(rb_cArray, "each_index", ary_each_index, 0);
  rb_define_method(rb_cArray, "each_with_index", ary_each_with_index, 0);
  rb_define_method(rb_cArray, "reverse_each", ary_reverse_each, 0);

  rb_define_method(rb_cArray, "length", ary_length, 0);
  rb_define_method(rb_cArray, "size", ary_length, 0);
  rb_define_method(rb_cArray, "empty?", ary_empty_p, 0);
  rb_define_method(rb_cArray, "include?", ary_includes, 1);
  rb_define_method(rb_cArray, "count", ary_count, ARGC_ANY);
  rb_define_method(rb_cArray, "assoc", ary_assoc, 1);
  rb_define_method(rb_cArray, "rassoc", ary_rassoc, 1);
#if RUBY_TARGET >= 20
  rb_define_method(rb_cArray, "index", ary_index, ARGC_ANY);
  rb_define_method(rb_cArray, "find_index", ary_find_index, ARGC_ANY);
  rb_define_method(rb_cArray, "rindex", ary_rindex, ARGC_ANY);
#else
  rb_define_method(rb_cArray, "index", ary_index, 1);
  rb_define_method(rb_cArray, "find_index", ary_index, 1);
  rb_define_method(rb_cArray, "rindex", ary_rindex, 1);
  rb_define_method(rb_cArray, "nitems", ary_nitems, 0);
#endif

  rb_define_method(rb_cArray, "join", ary_join_m, ARGC_ANY);
  rb_define_method(rb_cArray, "inspect", ary_inspect, 0);
  rb_define_method(rb_cArray, "to_s", ary_to_s, 0);

  rb_define_method(rb_cArray, "reverse", ary_reverse, 0);
  rb_define_method(rb_cArray, "reverse!", ary_reverse_bang, 0);
#if RUBY_TARGET >= 20
  rb_define_method(rb_cArray, "rotate", ary_rotate, ARGC_ANY);
  rb_define_method(rb_cArray, "rotate!", ary_rotate_bang, ARGC_ANY);
  rb_define_method(rb_cArray, "sort_by!", ary_sort_by_bang, 0);
  rb_define_method(rb_cArray, "bsearch", ary_bsearch, 0);
#endif
  rb_define_method(rb_cArray, "sort", ary_sort, 0);
  rb_define_method(rb_cArray, "sort!", ary_sort_bang, 0);
  rb_define_method(rb_cArray, "min", ary_min, ARGC_ANY);
  rb_define_method(rb_cArray, "max", ary_max, ARGC_ANY);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cArray, "sum", ary_sum, ARGC_ANY);
  rb_define_method(rb_cArray, "dig", ary_dig, ARGC_ANY);
#endif
#if RUBY_TARGET >= 34
  rb_define_method(rb_cArray, "fetch_values", ary_fetch_values, ARGC_ANY);
#endif

  rb_define_method(rb_cArray, "uniq", ary_uniq, 0);
  rb_define_method(rb_cArray, "uniq!", ary_uniq_bang, 0);
  rb_define_method(rb_cArray, "compact", ary_compact, 0);
  rb_define_method(rb_cArray, "compact!", ary_compact_bang, 0);
  rb_define_method(rb_cArray, "flatten", ary_flatten, ARGC_ANY);
  rb_define_method(rb_cArray, "flatten!", ary_flatten_bang, ARGC_ANY);

  rb_define_method(rb_cArray, "delete", ary_delete, 1);
  rb_define_method(rb_cArray, "delete_at", ary_delete_at, 1);
  rb_define_method(rb_cArray, "delete_if", ary_delete_if, 0);
  rb_define_method(rb_cArray, "reject", ary_reject, 0);
  rb_define_method(rb_cArray, "reject!", ary_reject_bang, 0);
  rb_define_method(rb_cArray, "select", ary_select, 0);
#if RUBY_TARGET >= 20
  rb_define_method(rb_cArray, "select!", ary_select_bang, 0);
  rb_define_method(rb_cArray, "keep_if", ary_keep_if, 0);
#endif
#if RUBY_TARGET >= 27
  rb_define_method(rb_cArray, "filter", ary_filter, 0);
  rb_define_method(rb_cArray, "filter!", ary_filter_bang_m, 0);
#endif

  rb_define_method(rb_cArray, "map", ary_map, 0);
  rb_define_method(rb_cArray, "map!", ary_map_bang, 0);
  rb_define_method(rb_cArray, "collect", ary_collect, 0);
  rb_define_method(rb_cArray, "collect!", ary_collect_bang, 0);
#if RUBY_TARGET >= 20
  rb_define_method(rb_cArray, "flat_map", ary_flat_map, 0);
  rb_define_method(rb_cArray, "collect_concat", ary_collect_concat, 0);
#endif

  rb_define_method(rb_cArray, "zip", ary_zip, ARGC_ANY);
  rb_define_method(rb_cArray, "take", ary_take, 1);
  rb_define_method(rb_cArray, "take_while", ary_take_while, 0);
  rb_define_method(rb_cArray, "drop", ary_drop, 1);
  rb_define_method(rb_cArray, "drop_while", ary_drop_while, 0);
  rb_define_method(rb_cArray, "cycle", ary_cycle, ARGC_ANY);

  rb_define_method(rb_cArray, "+", ary_plus, 1);
  rb_define_method(rb_cArray, "-", ary_diff, 1);
  rb_define_method(rb_cArray, "*", ary_times, 1);
  rb_define_method(rb_cArray, "&", ary_and, 1);
  rb_define_method(rb_cArray, "|", ary_or, 1);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cArray, "difference", ary_difference, ARGC_ANY);
  rb_define_method(rb_cArray, "union", ary_union, ARGC_ANY);
  rb_define_method(rb_cArray, "intersection", ary_intersection, ARGC_ANY);
#endif
#if RUBY_TARGET >= 31
  rb_define_method(rb_cArray, "intersect?", ary_intersect_p, 1);   /* 3.1 */
#endif

  rb_define_method(rb_cArray, "product", ary_product, ARGC_ANY);
  rb_define_method(rb_cArray, "combination", ary_combination, 1);
  rb_define_method(rb_cArray, "permutation", ary_permutation, ARGC_ANY);
  rb_define_method(rb_cArray, "transpose", ary_transpose, 0);
  rb_define_method(rb_cArray, "fill", ary_fill, ARGC_ANY);
  rb_define_method(rb_cArray, "replace", ary_replace, 1);
  rb_define_method(rb_cArray, "clear", ary_clear, 0);

  rb_define_method(rb_cArray, "==", ary_equal, 1);
  rb_define_method(rb_cArray, "eql?", ary_eql, 1);
  rb_define_method(rb_cArray, "<=>", ary_cmp, 1);
  rb_define_method(rb_cArray, "hash", ary_hash, 0);

  rb_define_method(rb_cArray, "to_a", ary_to_a, 0);
  rb_define_method(rb_cArray, "to_ary", ary_to_ary_m, 0);
  rb_define_method(rb_cArray, "frozen?", ary_frozen_p, 0);
  rb_define_method(rb_cArray, "freeze", ary_freeze, 0);
  rb_define_method(rb_cArray, "dup", ary_dup, 0);
  rb_define_method(rb_cArray, "clone", ary_clone, ARGC_ANY);
}
