/* string.c -- Ruby's String class. */
#include "internal.h"
#include <ctype.h>
#include <math.h>

extern VALUE rb_reg_make_match(VALUE, VALUE, long *, long *, int);
extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);
VALUE rb_str_format(VALUE fmt, VALUE args);

/* ===================== basics ===================== */

static rb_encoding *default_enc(void){
#if RUBY_TARGET < 20
  return enc_binary;
#else
  return enc_utf8;
#endif
}
static rb_encoding *str_enc(VALUE s){
  rb_encoding *e = RSTR(s)->enc;
  return e ? e : default_enc();
}
static char *sptr(VALUE v){
  RString *s = RSTR(v);
  if(!s->ptr){ s->ptr = xmalloc(1); s->ptr[0] = 0; s->cap = 1; s->len = 0; }
  return s->ptr;
}
#define SLEN(v) (RSTR(v)->len)

static int is_str(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_STRING; }
static int is_reg(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_REGEXP; }
static int is_hash(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_HASH; }
static int is_ary(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_ARRAY; }
static int is_int(VALUE v){ return FIXNUM_P(v) || (!FIXNUM_P(v) && BASIC(v)->type == T_BIGNUM); }

static VALUE str_new(const char *p, long len, rb_encoding *enc){
  return rb_str_new_enc(p, len, enc ? enc : default_enc());
}
static VALUE str_like(VALUE tmpl, const char *p, long len){
  return str_new(p, len, str_enc(tmpl));
}
static VALUE str_buf(VALUE tmpl){ return str_new("", 0, str_enc(tmpl)); }

static void str_modify(VALUE s){ rb_check_frozen(s); }

static void str_resize(VALUE v, long newlen){
  RString *s = RSTR(v);
  sptr(v);
  if(newlen + 1 > s->cap){
    long nc = s->cap > 0 ? s->cap : 16;
    while(nc < newlen + 1) nc *= 2;
    s->ptr = xrealloc(s->ptr, nc);
    s->cap = nc;
  }
  s->len = newlen;
  s->ptr[newlen] = 0;
}
/* replace bytes [beg,beg+len) with rep[0,replen) */
static void str_splice(VALUE v, long beg, long len, const char *rep, long replen){
  RString *s = RSTR(v);
  sptr(v);
  long tail = s->len - (beg + len);
  if(tail < 0) tail = 0;
  char *tmp = NULL;
  if(replen > 0){ tmp = xmalloc(replen); memcpy(tmp, rep, replen); }
  if(replen > len){
    long old = s->len;
    str_resize(v, old + (replen - len));
    memmove(s->ptr + beg + replen, s->ptr + beg + len, tail);
  } else if(replen < len){
    memmove(s->ptr + beg + replen, s->ptr + beg + len, tail);
    str_resize(v, s->len - (len - replen));
  }
  if(replen > 0){ memcpy(s->ptr + beg, tmp, replen); free(tmp); }
  s->ptr[s->len] = 0;
}
static void str_set(VALUE v, const char *p, long len){
  str_resize(v, len);
  if(len > 0) memmove(RSTR(v)->ptr, p, len);
  RSTR(v)->ptr[len] = 0;
}
static VALUE str_cat_str(VALUE v, VALUE other){
  return rb_str_cat(v, sptr(other), SLEN(other));
}

/* ===================== encoding / character walking ===================== */

static long char_size(rb_encoding *enc, const char *p, const char *e){
#if RUBY_TARGET < 20
  (void)enc; (void)p; (void)e; return 1;
#else
  long n = rb_enc_mbclen(enc, p, e);
  return n < 1 ? 1 : n;
#endif
}
static long char_size_at(VALUE s, long byteoff){
  const char *p = sptr(s);
  if(byteoff >= SLEN(s)) return 1;
  return char_size(str_enc(s), p + byteoff, p + SLEN(s));
}

long rb_str_strlen(VALUE str){
#if RUBY_TARGET < 20
  return SLEN(str);
#else
  rb_encoding *enc = str_enc(str);
  if(enc != enc_utf8) return SLEN(str);
  const char *p = sptr(str), *e = p + SLEN(str);
  long n = 0;
  while(p < e){ p += char_size(enc, p, e); n++; }
  return n;
#endif
}

long rb_str_offset(VALUE str, long pos){
#if RUBY_TARGET < 20
  if(pos < 0) return 0;
  return pos > SLEN(str) ? SLEN(str) : pos;
#else
  rb_encoding *enc = str_enc(str);
  if(enc != enc_utf8) return pos < 0 ? 0 : (pos > SLEN(str) ? SLEN(str) : pos);
  const char *b = sptr(str), *p = b, *e = b + SLEN(str);
  while(pos > 0 && p < e){ p += char_size(enc, p, e); pos--; }
  return p - b;
#endif
}

/* byte offset -> character index */
static long str_sublen(VALUE str, long byteoff){
#if RUBY_TARGET < 20
  (void)str;
  return byteoff;
#else
  rb_encoding *enc = str_enc(str);
  if(enc != enc_utf8) return byteoff;
  const char *b = sptr(str), *p = b, *e = b + SLEN(str);
  long n = 0;
  while(p < e && (p - b) < byteoff){ p += char_size(enc, p, e); n++; }
  return n;
#endif
}

VALUE rb_str_substr(VALUE str, long beg, long len){
  long slen = rb_str_strlen(str);
  if(len < 0) return Qnil;
  if(beg < 0){ beg += slen; if(beg < 0) return Qnil; }
  if(beg > slen) return Qnil;
  if(beg == slen || len == 0) return str_buf(str);
  if(len > slen - beg) len = slen - beg;
  long b = rb_str_offset(str, beg);
  long e = rb_str_offset(str, beg + len);
  return str_like(str, sptr(str) + b, e - b);
}
/* byte-range subsequence */
static VALUE str_subseq(VALUE str, long beg, long len){
  return str_like(str, sptr(str) + beg, len);
}

static int enc_ascii_compat(rb_encoding *e){ (void)e; return 1; }

static int str_is_ascii_only(VALUE s){
  const char *p = sptr(s);
  long n = SLEN(s);
  for(long i = 0; i < n; i++) if((unsigned char)p[i] & 0x80) return 0;
  return 1;
}
/* two encodings may be compared byte-for-byte */
static int str_comparable(VALUE a, VALUE b){
  rb_encoding *ea = str_enc(a), *eb = str_enc(b);
  if(ea == eb) return 1;
  if(!enc_ascii_compat(ea) || !enc_ascii_compat(eb)) return 0;
  if(str_is_ascii_only(a) || str_is_ascii_only(b)) return 1;
  return 0;
}
/* result encoding when concatenating */
static rb_encoding *enc_combine(VALUE a, VALUE b){
  rb_encoding *ea = str_enc(a), *eb = str_enc(b);
  if(ea == eb) return ea;
  if(SLEN(b) == 0) return ea;
  if(SLEN(a) == 0) return eb;
  if(str_is_ascii_only(b)) return ea;
  if(str_is_ascii_only(a)) return eb;
  rb_raise(rb_eEncCompatError, "incompatible character encodings: %s and %s",
           ea->name, eb->name);
  return ea;
}

#if RUBY_TARGET >= 20
static int utf8_valid(const char *p, long len){
  const char *e = p + len;
  while(p < e){
    unsigned char c = (unsigned char)*p;
    long n;
    if(c < 0x80) n = 1;
    else if(c < 0xC2) return 0;
    else if(c < 0xE0) n = 2;
    else if(c < 0xF0) n = 3;
    else if(c < 0xF5) n = 4;
    else return 0;
    if(p + n > e) return 0;
    for(long i = 1; i < n; i++)
      if(((unsigned char)p[i] & 0xC0) != 0x80) return 0;
    if(n == 3){
      unsigned int cp = ((c & 0x0F) << 12) | (((unsigned char)p[1] & 0x3F) << 6) |
                        ((unsigned char)p[2] & 0x3F);
      if(cp < 0x800) return 0;
      if(cp >= 0xD800 && cp <= 0xDFFF) return 0;
    } else if(n == 4){
      unsigned int cp = ((c & 0x07) << 18) | (((unsigned char)p[1] & 0x3F) << 12) |
                        (((unsigned char)p[2] & 0x3F) << 6) | ((unsigned char)p[3] & 0x3F);
      if(cp < 0x10000 || cp > 0x10FFFF) return 0;
    }
    p += n;
  }
  return 1;
}
static int str_valid_enc(VALUE s){
  rb_encoding *enc = str_enc(s);
  if(enc == enc_binary) return 1;
  if(enc == enc_usascii) return str_is_ascii_only(s);
  return utf8_valid(sptr(s), SLEN(s));
}
#endif

/* ===================== comparison ===================== */

int rb_str_cmp(VALUE a, VALUE b){
  long la = SLEN(a), lb = SLEN(b);
  long n = la < lb ? la : lb;
  int r = n ? memcmp(sptr(a), sptr(b), n) : 0;
  if(r == 0){
    if(la == lb){
      rb_encoding *ea = str_enc(a), *eb = str_enc(b);
      if(ea == eb || str_comparable(a, b)) return 0;
      return ea->index > eb->index ? 1 : (ea->index < eb->index ? -1 : 0);
    }
    return la > lb ? 1 : -1;
  }
  return r > 0 ? 1 : -1;
}
int rb_str_equal(VALUE a, VALUE b){
  if(a == b) return 1;
  if(!is_str(b)) return 0;
  if(SLEN(a) != SLEN(b)) return 0;
  if(!str_comparable(a, b)) return 0;
  return memcmp(sptr(a), sptr(b), SLEN(a)) == 0;
}

static VALUE str_eq(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(is_str(other)) return rb_str_equal(self, other) ? Qtrue : Qfalse;
  if(!FIXNUM_P(other) && rb_respond_to(other, ID_CACHED("to_str")))
    return RTEST(rb_funcall(other, ID_CACHED("=="), 1, self)) ? Qtrue : Qfalse;
  return Qfalse;
}
static VALUE str_eql(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(!is_str(other)) return Qfalse;
  return rb_str_equal(self, other) ? Qtrue : Qfalse;
}
static VALUE str_equal_p(VALUE self, VALUE other, RProc *blk){
  (void)blk; return self == other ? Qtrue : Qfalse;
}
static VALUE str_cmp_m(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(is_str(other)) return LONG2FIX(rb_str_cmp(self, other));
  if(FIXNUM_P(other)) return Qnil;
  if(!rb_respond_to(other, ID_CACHED("to_str")) || !rb_respond_to(other, ID_CACHED("<=>")))
    return Qnil;
  VALUE r = rb_funcall(other, ID_CACHED("<=>"), 1, self);
  if(NIL_P(r)) return Qnil;
  long n = rb_num2long(r);
  return LONG2FIX(n > 0 ? -1 : (n < 0 ? 1 : 0));
}
static VALUE str_between(VALUE self, VALUE lo, VALUE hi, RProc *blk){
  (void)blk;
  VALUE a = str_cmp_m(self, lo, NULL);
  if(NIL_P(a)) rb_raise(rb_eArgError, "comparison of String with %s failed", rb_obj_classname(lo));
  if(FIX2LONG(a) < 0) return Qfalse;
  VALUE b = str_cmp_m(self, hi, NULL);
  if(NIL_P(b)) rb_raise(rb_eArgError, "comparison of String with %s failed", rb_obj_classname(hi));
  return FIX2LONG(b) > 0 ? Qfalse : Qtrue;
}
static VALUE str_hash_m(VALUE self, RProc *blk){
  (void)blk;
  unsigned long h = 5381;
  const char *p = sptr(self);
  for(long i = 0; i < SLEN(self); i++) h = h * 33 + (unsigned char)p[i];
  return LONG2FIX((long)(h & (unsigned long)FIXNUM_MAX));
}

/* ===================== case mapping ===================== */

#if RUBY_TARGET >= 27
# define UNICODE_CASE 1
#else
# define UNICODE_CASE 0
#endif

static int cp_upcase(int c){
  if(c >= 'a' && c <= 'z') return c - 32;
#if RUBY_TARGET >= 20
  if(!UNICODE_CASE) return c;
  if(c < 0x80) return c;
  if(c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;          /* latin-1 */
  if(c == 0xFF) return 0x178;                                      /* y-diaeresis */
  if(c == 0xB5) return 0x39C;                                      /* micro sign */
  if(c >= 0x100 && c <= 0x17F){ if(c & 1) return c - 1; return c; }
  if(c >= 0x180 && c <= 0x24F){
    if(c >= 0x1F8 && c <= 0x21F) return (c & 1) ? c - 1 : c;
    if(c >= 0x222 && c <= 0x233) return (c & 1) ? c - 1 : c;
    if(c >= 0x246 && c <= 0x24F) return (c & 1) ? c - 1 : c;
    return c;
  }
  if(c == 0x3C2) return 0x3A3;                                     /* final sigma */
  if(c >= 0x3B1 && c <= 0x3C9) return c - 32;                      /* greek */
  if(c >= 0x3AC && c <= 0x3AF){ static const int m[] = {0x386,0x388,0x389,0x38A}; return m[c-0x3AC]; }
  if(c == 0x3CC) return 0x38C;
  if(c == 0x3CD) return 0x38E;
  if(c == 0x3CE) return 0x38F;
  if(c >= 0x430 && c <= 0x44F) return c - 32;                      /* cyrillic */
  if(c >= 0x450 && c <= 0x45F) return c - 80;
  if(c >= 0x460 && c <= 0x481){ if(c & 1) return c - 1; return c; }
  if(c >= 0x48A && c <= 0x4BF){ if(c & 1) return c - 1; return c; }
  if(c >= 0x4C1 && c <= 0x4CE){ if(!(c & 1)) return c - 1; return c; }
  if(c >= 0x4D0 && c <= 0x52F){ if(c & 1) return c - 1; return c; }
  if(c >= 0x561 && c <= 0x586) return c - 48;                      /* armenian */
  if(c >= 0x1E00 && c <= 0x1E95){ if(c & 1) return c - 1; return c; }
  if(c >= 0x1EA0 && c <= 0x1EFF){ if(c & 1) return c - 1; return c; }
  if(c >= 0x2170 && c <= 0x217F) return c - 16;                    /* roman numerals */
  if(c >= 0x24D0 && c <= 0x24E9) return c - 26;                    /* circled */
  if(c >= 0xFF41 && c <= 0xFF5A) return c - 32;                    /* fullwidth */
#endif
  return c;
}
static int cp_downcase(int c){
  if(c >= 'A' && c <= 'Z') return c + 32;
#if RUBY_TARGET >= 20
  if(!UNICODE_CASE) return c;
  if(c < 0x80) return c;
  if(c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if(c == 0x178) return 0xFF;
  if(c >= 0x100 && c <= 0x17F){ if(!(c & 1)) return c + 1; return c; }
  if(c >= 0x1F8 && c <= 0x21F) return (c & 1) ? c : c + 1;
  if(c >= 0x222 && c <= 0x233) return (c & 1) ? c : c + 1;
  if(c >= 0x246 && c <= 0x24F) return (c & 1) ? c : c + 1;
  if(c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
  if(c == 0x386) return 0x3AC;
  if(c >= 0x388 && c <= 0x38A) return c + 37;
  if(c == 0x38C) return 0x3CC;
  if(c == 0x38E) return 0x3CD;
  if(c == 0x38F) return 0x3CE;
  if(c >= 0x410 && c <= 0x42F) return c + 32;
  if(c >= 0x400 && c <= 0x40F) return c + 80;
  if(c >= 0x460 && c <= 0x481){ if(!(c & 1)) return c + 1; return c; }
  if(c >= 0x48A && c <= 0x4BF){ if(!(c & 1)) return c + 1; return c; }
  if(c >= 0x4C1 && c <= 0x4CE){ if(c & 1) return c + 1; return c; }
  if(c >= 0x4D0 && c <= 0x52F){ if(!(c & 1)) return c + 1; return c; }
  if(c >= 0x531 && c <= 0x556) return c + 48;
  if(c >= 0x1E00 && c <= 0x1E95){ if(!(c & 1)) return c + 1; return c; }
  if(c >= 0x1EA0 && c <= 0x1EFF){ if(!(c & 1)) return c + 1; return c; }
  if(c >= 0x2160 && c <= 0x216F) return c + 16;
  if(c >= 0x24B6 && c <= 0x24CF) return c + 26;
  if(c >= 0xFF21 && c <= 0xFF3A) return c + 32;
#endif
  return c;
}

enum { CASE_UP, CASE_DOWN, CASE_CAPITALIZE, CASE_SWAP };

static void cat_cp(VALUE out, rb_encoding *enc, int cp){
  char buf[8];
  long n = rb_enc_mbcput(enc, cp, buf);
  rb_str_cat(out, buf, n);
}
/* write the case-converted form of self into out; return 1 when it differs */
static int case_convert(VALUE self, VALUE out, int mode){
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  int changed = 0, first = 1;
  int unicode = (enc == enc_utf8) && UNICODE_CASE;
  while(p < e){
    int len = 1, cp;
#if RUBY_TARGET < 20
    cp = (unsigned char)*p;
#else
    cp = rb_enc_codepoint(enc, p, e, &len);
    if(len < 1) len = 1;
#endif
    if(cp >= 0x80 && !unicode){
      rb_str_cat(out, p, len);
      p += len; first = 0; continue;
    }
    int want = mode;
    if(mode == CASE_CAPITALIZE) want = first ? CASE_UP : CASE_DOWN;
    else if(mode == CASE_SWAP)  want = (cp_downcase(cp) != cp) ? CASE_DOWN : CASE_UP;
    int nc = cp;
    if(want == CASE_UP){
      if(unicode && cp == 0xDF){
        rb_str_cat(out, (mode == CASE_CAPITALIZE && first) ? "Ss" : "SS", 2);
        changed = 1; p += len; first = 0; continue;
      }
      nc = cp_upcase(cp);
    } else {
      nc = cp_downcase(cp);
    }
    if(nc != cp){ changed = 1; cat_cp(out, enc, nc); }
    else rb_str_cat(out, p, len);
    p += len; first = 0;
  }
  return changed;
}
static VALUE case_copy(VALUE self, int mode){
  VALUE out = str_buf(self);
  case_convert(self, out, mode);
  return out;
}
static VALUE case_bang(VALUE self, int mode){
  str_modify(self);
  VALUE out = str_buf(self);
  int changed = case_convert(self, out, mode);
  if(!changed) return Qnil;
  str_set(self, sptr(out), SLEN(out));
  return self;
}
static VALUE str_upcase(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_copy(self, CASE_UP); }
static VALUE str_downcase(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_copy(self, CASE_DOWN); }
static VALUE str_capitalize(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_copy(self, CASE_CAPITALIZE); }
static VALUE str_swapcase(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_copy(self, CASE_SWAP); }
static VALUE str_upcase_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_bang(self, CASE_UP); }
static VALUE str_downcase_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_bang(self, CASE_DOWN); }
static VALUE str_capitalize_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_bang(self, CASE_CAPITALIZE); }
static VALUE str_swapcase_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)argc; (void)argv; (void)blk; return case_bang(self, CASE_SWAP); }

static VALUE str_casecmp(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(!is_str(other)){
    if(FIXNUM_P(other) || !rb_respond_to(other, ID_CACHED("to_str"))) return Qnil;
    other = rb_str_to_str(other);
  }
  const char *a = sptr(self), *b = sptr(other);
  long la = SLEN(self), lb = SLEN(other), n = la < lb ? la : lb;
  for(long i = 0; i < n; i++){
    int x = (unsigned char)a[i], y = (unsigned char)b[i];
    if(x >= 'A' && x <= 'Z') x += 32;
    if(y >= 'A' && y <= 'Z') y += 32;
    if(x != y) return LONG2FIX(x < y ? -1 : 1);
  }
  if(la == lb) return LONG2FIX(0);
  return LONG2FIX(la < lb ? -1 : 1);
}
#if RUBY_TARGET >= 27
static VALUE str_casecmp_p(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(!is_str(other)){
    if(FIXNUM_P(other) || !rb_respond_to(other, ID_CACHED("to_str"))) return Qnil;
    other = rb_str_to_str(other);
  }
  if(!str_comparable(self, other)) return Qnil;
  VALUE a = case_copy(self, CASE_DOWN), b = case_copy(other, CASE_DOWN);
  return rb_str_equal(a, b) ? Qtrue : Qfalse;
}
#endif

/* ===================== inspect ===================== */

static void inspect_hex(VALUE out, unsigned char c){
#if RUBY_TARGET < 20
  char b[8];
  snprintf(b, sizeof b, "\\%03o", (unsigned)c);   /* 1.8 escapes in octal */
  rb_str_cat(out, b, 4);
#else
  static const char *H = "0123456789ABCDEF";
  char b[4] = { '\\', 'x', H[c >> 4], H[c & 15] };
  rb_str_cat(out, b, 4);
#endif
}
static VALUE str_inspect(VALUE self, RProc *blk){
  (void)blk;
  VALUE out = str_new("\"", 1, default_enc());
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  while(p < e){
    unsigned char c = (unsigned char)*p;
    if(c < 0x80){
      switch(c){
        case '"':  rb_str_cat(out, "\\\"", 2); p++; continue;
        case '\\': rb_str_cat(out, "\\\\", 2); p++; continue;
        case '\n': rb_str_cat(out, "\\n", 2); p++; continue;
        case '\t': rb_str_cat(out, "\\t", 2); p++; continue;
        case '\r': rb_str_cat(out, "\\r", 2); p++; continue;
        case '\f': rb_str_cat(out, "\\f", 2); p++; continue;
        case '\v': rb_str_cat(out, "\\v", 2); p++; continue;
        case '\b': rb_str_cat(out, "\\b", 2); p++; continue;
        case '\a': rb_str_cat(out, "\\a", 2); p++; continue;
        case 033:  rb_str_cat(out, "\\e", 2); p++; continue;
        case '#':
          if(p + 1 < e && (p[1] == '{' || p[1] == '$' || p[1] == '@')){
            rb_str_cat(out, "\\#", 2); p++; continue;
          }
          rb_str_cat(out, "#", 1); p++; continue;
        default: break;
      }
      if(c >= 0x20 && c < 0x7F) rb_str_cat(out, (const char *)p, 1);
      else inspect_hex(out, c);
      p++;
      continue;
    }
#if RUBY_TARGET >= 20
    if(enc == enc_utf8){
      long n = rb_enc_mbclen(enc, p, e);
      if(n > 1 && utf8_valid(p, n)){
        int len = 0;
        int cp = rb_enc_codepoint(enc, p, e, &len);
        if(cp >= 0xA0 && cp != 0x2028 && cp != 0x2029 && !(cp >= 0xFFF9 && cp <= 0xFFFB)){
          rb_str_cat(out, p, n);
        } else {
          char b[16];
          snprintf(b, sizeof b, "\\u{%x}", (unsigned)cp);
          rb_str_cat_cstr(out, b);
        }
        p += n;
        continue;
      }
    }
#endif
    (void)enc;
    inspect_hex(out, c);
    p++;
  }
  rb_str_cat(out, "\"", 1);
  return out;
}

/* ===================== plus / times ===================== */

VALUE rb_str_plus(VALUE a, VALUE b){
  rb_encoding *enc = enc_combine(a, b);
  VALUE r = str_new(NULL, 0, enc);
  rb_str_cat(r, sptr(a), SLEN(a));
  rb_str_cat(r, sptr(b), SLEN(b));
  return r;
}
static VALUE str_plus_m(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(!is_str(other)){
    if(FIXNUM_P(other) || !rb_respond_to(other, ID_CACHED("to_str")))
      rb_raise(rb_eTypeError, "no implicit conversion of %s into String",
               NIL_P(other) ? "nil" : rb_obj_classname(other));
    other = rb_str_to_str(other);
  }
  return rb_str_plus(self, other);
}
static VALUE str_times(VALUE self, VALUE num, RProc *blk){
  (void)blk;
  long n = rb_num2long(num);
  if(n < 0) rb_raise(rb_eArgError, "negative argument");
  long l = SLEN(self);
  if(l && n > (LONG_MAX - 1) / l) rb_raise(rb_eArgError, "argument too big");
  VALUE r = str_buf(self);
  for(long i = 0; i < n; i++) rb_str_cat(r, sptr(self), l);
  return r;
}

/* ===================== format ===================== */

typedef struct {
  VALUE *argv; int argc, next;
  VALUE hash;
} fmt_args;

static VALUE fmt_next(fmt_args *a){
  if(a->next >= a->argc) rb_raise(rb_eArgError, "too few arguments");
  return a->argv[a->next++];
}
static long fmt_int_arg(fmt_args *a){ return rb_num2long(fmt_next(a)); }

static void fmt_pad(VALUE out, long n, char c){
  char buf[64];
  while(n > 0){
    long k = n > 64 ? 64 : n;
    memset(buf, c, k);
    rb_str_cat(out, buf, k);
    n -= k;
  }
}
static long fmt_wlen(VALUE s){
#if RUBY_TARGET < 20
  return SLEN(s);
#else
  return rb_str_strlen(s);
#endif
}
static VALUE to_integer_for_fmt(VALUE v){
  if(is_int(v)) return v;
  if(!FIXNUM_P(v) && BASIC(v)->type == T_FLOAT) return rb_int_new((long long)RFLT(v)->f);
  if(is_str(v)) return rb_funcall(v, ID_CACHED("to_i"), 0);
  if(NIL_P(v)) rb_raise(rb_eTypeError, "can't convert nil into Integer");
  return rb_to_int(v);
}

typedef struct { int minus, plus, space, zero, sharp; long width, prec; } fmt_spec;

static void fmt_emit_int(VALUE out, long long val, int base, int upper, fmt_spec *f){
  static const char *LOW = "0123456789abcdefghijklmnopqrstuvwxyz";
  static const char *UPP = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const char *dig = upper ? UPP : LOW;
  char body[160]; int n = 0;
  char sign = 0;
  const char *prefix = "";
  int complement = 0;

  if(val < 0 && base != 10 && !f->plus && !f->space) complement = 1;
  if(complement){
    unsigned long long u = (unsigned long long)val;
    char tmp[160]; int m = 0;
    do { tmp[m++] = dig[u % (unsigned)base]; u /= (unsigned)base; } while(u);
    while(m > 0 && tmp[m-1] == dig[base-1]) m--;   /* drop leading (base-1) digits */
    body[n++] = dig[base-1];
    while(m > 0) body[n++] = tmp[--m];
  } else {
    unsigned long long u;
    if(val < 0){ sign = '-'; u = (unsigned long long)(-(val + 1)) + 1ULL; }
    else { u = (unsigned long long)val; if(f->plus) sign = '+'; else if(f->space) sign = ' '; }
    char tmp[160]; int m = 0;
    do { tmp[m++] = dig[u % (unsigned)base]; u /= (unsigned)base; } while(u);
    while(m > 0) body[n++] = tmp[--m];
  }
  if(f->sharp){
    if(base == 16) prefix = upper ? "0X" : "0x";
    else if(base == 8) prefix = "0";
    else if(base == 2) prefix = upper ? "0B" : "0b";
  }
  long plen = (long)strlen(prefix);
  long digits = n;
  long zeros = 0;
  if(f->prec >= 0 && f->prec > digits && !complement) zeros = f->prec - digits;
  long total = (sign ? 1 : 0) + plen + (complement ? 2 : 0) + zeros + digits;
  long padw = f->width > total ? f->width - total : 0;

  if(padw && !f->minus && !(f->zero && f->prec < 0)) fmt_pad(out, padw, ' ');
  if(sign) rb_str_cat(out, &sign, 1);
  if(plen) rb_str_cat(out, prefix, plen);
  if(complement) rb_str_cat(out, "..", 2);
  if(padw && !f->minus && f->zero && f->prec < 0)
    fmt_pad(out, padw, complement ? dig[base-1] : '0');
  if(zeros) fmt_pad(out, zeros, '0');
  rb_str_cat(out, body, n);
  if(padw && f->minus) fmt_pad(out, padw, ' ');
}

static void fmt_emit_float(VALUE out, double d, char conv, fmt_spec *f){
  char spec[64]; int k = 0;
  spec[k++] = '%';
  if(f->minus) spec[k++] = '-';
  if(f->plus)  spec[k++] = '+';
  if(f->space) spec[k++] = ' ';
  if(f->zero)  spec[k++] = '0';
  if(f->sharp) spec[k++] = '#';
  if(f->width >= 0) k += snprintf(spec + k, sizeof spec - k, "%ld", f->width);
  if(f->prec >= 0)  k += snprintf(spec + k, sizeof spec - k, ".%ld", f->prec);
  spec[k++] = conv; spec[k] = 0;

  if(isnan(d) || isinf(d)){
    char txt[8];
    int k2 = 0;
    if(isinf(d) && d < 0) txt[k2++] = '-';
    else if(f->plus) txt[k2++] = '+';
    else if(f->space) txt[k2++] = ' ';
    strcpy(txt + k2, isnan(d) ? "NaN" : "Inf");
    long pad = f->width - (long)strlen(txt);
    if(pad > 0 && !f->minus) fmt_pad(out, pad, ' ');
    rb_str_cat_cstr(out, txt);
    if(pad > 0 && f->minus) fmt_pad(out, pad, ' ');
    return;
  }
  long need = 64 + (f->width > 0 ? f->width : 0) + (f->prec > 0 ? f->prec : 0) + 330;
  char *buf = xmalloc(need);
  int m = snprintf(buf, need, spec, d);
  if(m < 0) m = 0;
  if(m > need - 1) m = need - 1;
  rb_str_cat(out, buf, m);
}

static void fmt_emit_str(VALUE out, VALUE s, fmt_spec *f){
  if(f->prec >= 0){
    VALUE t = rb_str_substr(s, 0, f->prec);
    if(!NIL_P(t)) s = t;
  }
  long w = fmt_wlen(s);
  long pad = f->width > w ? f->width - w : 0;
  if(pad && !f->minus) fmt_pad(out, pad, ' ');
  rb_str_cat(out, sptr(s), SLEN(s));
  if(pad && f->minus) fmt_pad(out, pad, ' ');
}

VALUE rb_str_format(VALUE fmt, VALUE args){
  fmt_args A; A.hash = Qnil;
  VALUE single = args;
  if(is_ary(args)){ A.argv = RARR(args)->ptr; A.argc = (int)RARR(args)->len; }
  else { A.argv = &single; A.argc = 1; if(is_hash(args)) A.hash = args; }
  A.next = 0;

  VALUE out = str_buf(fmt);
  const char *p = sptr(fmt), *e = p + SLEN(fmt);
  while(p < e){
    if(*p != '%'){
      const char *q = p;
      while(q < e && *q != '%') q++;
      rb_str_cat(out, p, q - p);
      p = q;
      continue;
    }
    p++;
    if(p >= e) rb_raise(rb_eArgError, "incomplete format specifier");
    fmt_spec f; memset(&f, 0, sizeof f);
    f.width = -1; f.prec = -1;
    VALUE named = Qundef;
    int done = 0;

    for(; p < e && !done; ){
      switch(*p){
        case '-': f.minus = 1; p++; continue;
        case '+': f.plus  = 1; p++; continue;
        case ' ': f.space = 1; p++; continue;
        case '0': f.zero  = 1; p++; continue;
        case '#': f.sharp = 1; p++; continue;
        case '<': case '{': {
          char close = (*p == '<') ? '>' : '}';
          const char *s = ++p;
          while(p < e && *p != close) p++;
          if(p >= e) rb_raise(rb_eArgError, "malformed name - unmatched parenthesis");
          VALUE key = rb_sym(rb_intern2(s, p - s));
          p++;
          if(NIL_P(A.hash)) rb_raise(rb_eArgError, "one hash required");
          VALUE v = rb_hash_lookup(A.hash, key, Qundef);
          if(v == Qundef)
            rb_raise(rb_eKeyError, "key<%.*s> not found", (int)(p - s - 1), s);
          if(close == '}'){ str_cat_str(out, rb_obj_as_string(v)); done = 2; }
          else named = v;
          continue;
        }
        default: done = 1; break;
      }
    }
    if(done == 2) continue;
    if(p >= e) rb_raise(rb_eArgError, "incomplete format specifier");

    if(*p == '*'){
      p++;
      long w = fmt_int_arg(&A);
      if(w < 0){ f.minus = 1; w = -w; }
      f.width = w;
    } else {
      while(p < e && *p >= '0' && *p <= '9'){
        if(f.width < 0) f.width = 0;
        f.width = f.width * 10 + (*p - '0');
        p++;
      }
    }
    if(p < e && *p == '.'){
      p++;
      if(p < e && *p == '*'){ p++; f.prec = fmt_int_arg(&A); if(f.prec < 0) f.prec = -1; }
      else { f.prec = 0; while(p < e && *p >= '0' && *p <= '9'){ f.prec = f.prec * 10 + (*p - '0'); p++; } }
    }
    if(p >= e) rb_raise(rb_eArgError, "incomplete format specifier");
    char conv = *p++;
    VALUE v = Qundef;
#define FMT_VAL() (named != Qundef ? named : (v != Qundef ? v : (v = fmt_next(&A))))
    switch(conv){
      case '%': rb_str_cat(out, "%", 1); break;
      case 'd': case 'i': case 'u':
        fmt_emit_int(out, (long long)rb_num2long(to_integer_for_fmt(FMT_VAL())), 10, 0, &f); break;
      case 'x': fmt_emit_int(out, (long long)rb_num2long(to_integer_for_fmt(FMT_VAL())), 16, 0, &f); break;
      case 'X': fmt_emit_int(out, (long long)rb_num2long(to_integer_for_fmt(FMT_VAL())), 16, 1, &f); break;
      case 'o': fmt_emit_int(out, (long long)rb_num2long(to_integer_for_fmt(FMT_VAL())), 8, 0, &f); break;
      case 'b': fmt_emit_int(out, (long long)rb_num2long(to_integer_for_fmt(FMT_VAL())), 2, 0, &f); break;
      case 'B': fmt_emit_int(out, (long long)rb_num2long(to_integer_for_fmt(FMT_VAL())), 2, 1, &f); break;
      case 'f': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
        fmt_emit_float(out, rb_num2dbl(FMT_VAL()), conv, &f); break;
      case 's': fmt_emit_str(out, rb_obj_as_string(FMT_VAL()), &f); break;
      case 'p': fmt_emit_str(out, rb_inspect(FMT_VAL()), &f); break;
      case 'c': {
        VALUE a = FMT_VAL();
        VALUE ch;
        if(is_str(a)){
          if(rb_str_strlen(a) != 1) rb_raise(rb_eArgError, "%%c requires a character");
          ch = a;
        } else {
          long cp = rb_num2long(a);
          char b[8];
#if RUBY_TARGET < 20
          if(cp < 0 || cp > 255) rb_raise(rb_eRangeError, "%ld out of char range", cp);
          b[0] = (char)cp; ch = str_new(b, 1, str_enc(fmt));
#else
          long n = rb_enc_mbcput(str_enc(fmt), (int)cp, b);
          ch = str_new(b, n, str_enc(fmt));
#endif
        }
        fmt_emit_str(out, ch, &f);
        break;
      }
      default:
        rb_raise(rb_eArgError, "malformed format string - %%%c", conv);
    }
#undef FMT_VAL
  }
  return out;
}
static VALUE str_format_m(VALUE self, VALUE arg, RProc *blk){
  (void)blk; return rb_str_format(self, arg);
}

/* ===================== pattern machinery ===================== */

typedef struct {
  int is_reg;
  VALUE pat;                 /* Regexp or String */
  VALUE rex;                 /* Regexp used for building MatchData */
  long beg[32], end[32];
  int ng;
} SMatch;

static VALUE str_quote_regexp(VALUE s){
  VALUE q = str_buf(s);
  const char *p = sptr(s), *e = p + SLEN(s);
  while(p < e){
    unsigned char c = (unsigned char)*p;
    if(c < 0x80 && c && strchr("\\^$.|?*+()[]{}-", c)) rb_str_cat(q, "\\", 1);
    if(c == '\n'){ rb_str_cat(q, "\\n", 2); p++; continue; }
    if(c == '\t'){ rb_str_cat(q, "\\t", 2); p++; continue; }
    if(c == '\r'){ rb_str_cat(q, "\\r", 2); p++; continue; }
    if(c == '\f'){ rb_str_cat(q, "\\f", 2); p++; continue; }
    if(c == '\v'){ rb_str_cat(q, "\\v", 2); p++; continue; }
    rb_str_cat(q, p, 1);
    p++;
  }
  return q;
}

static void m_init(SMatch *m, VALUE pat){
  memset(m, 0, sizeof *m);
  m->pat = pat; m->rex = Qnil;
  m->is_reg = is_reg(pat);
  if(m->is_reg) m->rex = pat;
  for(int i = 0; i < 32; i++){ m->beg[i] = -1; m->end[i] = -1; }
}
/* byte-oriented search at or after byte offset `pos` */
static int m_search(SMatch *m, VALUE str, long pos){
  for(int i = 0; i < 32; i++){ m->beg[i] = -1; m->end[i] = -1; }
  m->ng = 0;
  if(pos < 0 || pos > SLEN(str)) return 0;
  if(m->is_reg){
    int r = rb_reg_search(RREG(m->pat)->re, sptr(str), SLEN(str), pos,
                          m->beg, m->end, &m->ng);
    if(r < 0 || m->beg[0] < 0) return 0;
    if(m->ng > 31) m->ng = 31;
    return 1;
  }
  const char *hay = sptr(str), *ned = sptr(m->pat);
  long hl = SLEN(str), nl = SLEN(m->pat);
  if(nl == 0){ if(pos > hl) return 0; m->beg[0] = pos; m->end[0] = pos; return 1; }
  for(long i = pos; i + nl <= hl; i++){
    if(hay[i] == ned[0] && memcmp(hay + i, ned, nl) == 0){
      m->beg[0] = i; m->end[0] = i + nl; return 1;
    }
  }
  return 0;
}
static VALUE m_regexp(SMatch *m){
  if(NIL_P(m->rex)) m->rex = rb_reg_new_str(str_quote_regexp(m->pat), 0);
  return m->rex;
}
static void m_backref(SMatch *m, VALUE str, int matched){
  if(!matched){ rb_set_backref(Qnil); return; }
  rb_set_backref(rb_reg_make_match(m_regexp(m), str, m->beg, m->end, m->ng));
}
static VALUE m_group(SMatch *m, VALUE str, int i){
  if(i < 0 || i > 31 || m->beg[i] < 0) return Qnil;
  return str_subseq(str, m->beg[i], m->end[i] - m->beg[i]);
}
static int m_name_index(SMatch *m, const char *name, long len){
  if(!m->is_reg) return -1;
  struct regex *re = RREG(m->pat)->re;
  int n = rb_reg_name_count(re);
  for(int i = 0; i < n; i++){
    int idx = -1;
    const char *nm = rb_reg_name_at(re, i, &idx);
    if(nm && (long)strlen(nm) == len && memcmp(nm, name, len) == 0) return idx;
  }
  return -1;
}
/* a Regexp is required for =~ / match; convert Strings and Symbols */
static VALUE to_regexp(VALUE v){
  if(is_reg(v)) return v;
  if(!FIXNUM_P(v) && BASIC(v)->type == T_SYMBOL)
    v = rb_str_new_cstr(rb_id2name(RSYM(v)->id));
  if(is_str(v)) return rb_reg_new_str(v, 0);
  VALUE s = rb_check_string_type(v);
  if(!NIL_P(s)) return rb_reg_new_str(s, 0);
  rb_raise(rb_eTypeError, "wrong argument type %s (expected Regexp)", rb_obj_classname(v));
  return Qnil;
}
/* sub/gsub/split/index take Strings literally */
static VALUE to_pattern(VALUE v){
  if(is_reg(v)) return v;
  VALUE s = rb_check_string_type(v);
  if(!NIL_P(s)) return s;
  rb_raise(rb_eTypeError, "wrong argument type %s (expected Regexp)", rb_obj_classname(v));
  return Qnil;
}

/* ===================== index / search ===================== */

static long byte_rindex(const char *hay, long hl, const char *ned, long nl, long start){
  if(nl == 0) return start > hl ? hl : start;
  if(start > hl - nl) start = hl - nl;
  for(long i = start; i >= 0; i--)
    if(hay[i] == ned[0] && memcmp(hay + i, ned, nl) == 0) return i;
  return -1;
}
/* find the last match starting at or before byte offset `pos` */
static int m_rsearch(SMatch *m, VALUE str, long pos){
  if(!m->is_reg){
    long r = byte_rindex(sptr(str), SLEN(str), sptr(m->pat), SLEN(m->pat), pos);
    if(r < 0) return 0;
    m->beg[0] = r; m->end[0] = r + SLEN(m->pat); m->ng = 0;
    return 1;
  }
  if(pos > SLEN(str)) pos = SLEN(str);
  for(long p = pos; p >= 0; p--){
    if(m_search(m, str, p) && m->beg[0] == p) return 1;
  }
  return 0;
}

static VALUE str_index_m(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  long slen = rb_str_strlen(self);
  long pos = 0;
  if(argc == 2){
    pos = rb_num2long(argv[1]);
    if(pos < 0){ pos += slen; if(pos < 0){ if(is_reg(argv[0])) rb_set_backref(Qnil); return Qnil; } }
  }
  if(pos > slen){ if(is_reg(argv[0])) rb_set_backref(Qnil); return Qnil; }
#if RUBY_TARGET < 20
  if(is_int(argv[0])){
    long c = rb_num2long(argv[0]);
    const char *p = sptr(self);
    for(long i = pos; i < SLEN(self); i++) if((unsigned char)p[i] == (unsigned char)c) return LONG2FIX(i);
    return Qnil;
  }
#endif
  VALUE pat = to_pattern(argv[0]);
  SMatch m; m_init(&m, pat);
  int ok = m_search(&m, self, rb_str_offset(self, pos));
  if(m.is_reg) m_backref(&m, self, ok);
  if(!ok) return Qnil;
  return LONG2FIX(str_sublen(self, m.beg[0]));
}
static VALUE str_rindex_m(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  long slen = rb_str_strlen(self);
  long pos = slen;
  if(argc == 2){
    pos = rb_num2long(argv[1]);
    if(pos < 0){ pos += slen; if(pos < 0){ if(is_reg(argv[0])) rb_set_backref(Qnil); return Qnil; } }
    if(pos > slen) pos = slen;
  }
#if RUBY_TARGET < 20
  if(is_int(argv[0])){
    long c = rb_num2long(argv[0]);
    const char *p = sptr(self);
    for(long i = (pos < SLEN(self) ? pos : SLEN(self) - 1); i >= 0; i--)
      if((unsigned char)p[i] == (unsigned char)c) return LONG2FIX(i);
    return Qnil;
  }
#endif
  VALUE pat = to_pattern(argv[0]);
  SMatch m; m_init(&m, pat);
  int ok = m_rsearch(&m, self, rb_str_offset(self, pos));
  if(m.is_reg) m_backref(&m, self, ok);
  if(!ok) return Qnil;
  return LONG2FIX(str_sublen(self, m.beg[0]));
}
static VALUE str_include(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  VALUE o = rb_str_to_str(other);
  SMatch m; m_init(&m, o);
  return m_search(&m, self, 0) ? Qtrue : Qfalse;
}
static VALUE str_start_with(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  for(int i = 0; i < argc; i++){
#if RUBY_TARGET >= 27
    if(is_reg(argv[i])){
      SMatch m; m_init(&m, argv[i]);
      int ok = m_search(&m, self, 0) && m.beg[0] == 0;
      m_backref(&m, self, ok);
      if(ok) return Qtrue;
      continue;
    }
#endif
    VALUE o = rb_str_to_str(argv[i]);
    if(SLEN(o) <= SLEN(self) && memcmp(sptr(self), sptr(o), SLEN(o)) == 0) return Qtrue;
  }
  return Qfalse;
}
static VALUE str_end_with(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  for(int i = 0; i < argc; i++){
    VALUE o = rb_str_to_str(argv[i]);
    if(SLEN(o) <= SLEN(self) &&
       memcmp(sptr(self) + SLEN(self) - SLEN(o), sptr(o), SLEN(o)) == 0) return Qtrue;
  }
  return Qfalse;
}
static VALUE str_match_op(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(is_str(other)) rb_raise(rb_eTypeError, "type mismatch: String given");
  if(!is_reg(other)) return rb_funcall(other, ID_CACHED("=~"), 1, self);
  SMatch m; m_init(&m, other);
  int ok = m_search(&m, self, 0);
  m_backref(&m, self, ok);
  if(!ok) return Qnil;
  return LONG2FIX(str_sublen(self, m.beg[0]));
}
static VALUE str_match_m(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE re = to_regexp(argv[0]);
  long pos = 0;
  if(argc == 2){
    pos = rb_num2long(argv[1]);
    if(pos < 0) pos += rb_str_strlen(self);
    if(pos < 0 || pos > rb_str_strlen(self)){ rb_set_backref(Qnil); return Qnil; }
  }
  SMatch m; m_init(&m, re);
  int ok = m_search(&m, self, rb_str_offset(self, pos));
  m_backref(&m, self, ok);
  if(!ok) return Qnil;
  VALUE md = rb_get_backref();
  if(blk) return rb_yield1(blk, md);
  return md;
}
#if RUBY_TARGET >= 27
static VALUE str_match_p(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE re = to_regexp(argv[0]);
  long pos = 0;
  if(argc == 2){
    pos = rb_num2long(argv[1]);
    if(pos < 0) pos += rb_str_strlen(self);
    if(pos < 0 || pos > rb_str_strlen(self)) return Qfalse;
  }
  SMatch m; m_init(&m, re);
  return m_search(&m, self, rb_str_offset(self, pos)) ? Qtrue : Qfalse;
}
#endif

/* ===================== sub / gsub ===================== */

static void sub_expand(VALUE out, VALUE repl, VALUE str, SMatch *m){
  const char *p = sptr(repl), *e = p + SLEN(repl);
  while(p < e){
    if(*p != '\\' || p + 1 >= e){ rb_str_cat(out, p, 1); p++; continue; }
    char c = p[1];
    p += 2;
    if(c >= '0' && c <= '9'){
      VALUE g = m_group(m, str, c - '0');
      if(!NIL_P(g)) str_cat_str(out, g);
      continue;
    }
    switch(c){
      case '&': { VALUE g = m_group(m, str, 0); if(!NIL_P(g)) str_cat_str(out, g); break; }
      case '`': rb_str_cat(out, sptr(str), m->beg[0]); break;
      case '\'': rb_str_cat(out, sptr(str) + m->end[0], SLEN(str) - m->end[0]); break;
      case '+': {
        for(int i = m->ng; i > 0; i--)
          if(m->beg[i] >= 0){ str_cat_str(out, m_group(m, str, i)); break; }
        break;
      }
      case '\\': rb_str_cat(out, "\\", 1); break;
      case 'k': {
        if(p < e && *p == '<'){
          const char *s = ++p;
          while(p < e && *p != '>') p++;
          int idx = m_name_index(m, s, p - s);
          if(p < e) p++;
          if(idx < 0) rb_raise(rb_eIndexError, "undefined group name reference: %.*s", (int)(p - s - 1), s);
          VALUE g = m_group(m, str, idx);
          if(!NIL_P(g)) str_cat_str(out, g);
        } else { rb_str_cat(out, "\\k", 2); }
        break;
      }
      default: rb_str_cat(out, "\\", 1); rb_str_cat(out, &c, 1); break;
    }
  }
}

static VALUE gsub_impl(VALUE self, int argc, VALUE *argv, RProc *blk, int once, int bang){
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE repl = Qnil, hash = Qnil;
  int use_blk = 0;
  if(argc == 2){
    if(is_hash(argv[1])) hash = argv[1];
    else repl = rb_str_to_str(argv[1]);
  } else if(blk){
    use_blk = 1;
  } else {
    if(once) rb_raise(rb_eArgError, "wrong number of arguments (given 1, expected 2)");
    return rb_enum_new(self, ID_CACHED("gsub"), argc, argv);
  }
  VALUE pat = to_pattern(argv[0]);
  if(bang) str_modify(self);

  SMatch m, lastm;
  m_init(&m, pat);
  lastm = m;
  VALUE out = str_buf(self);
  long pos = 0, last = 0;
  int found = 0;
  long slen = SLEN(self);
  while(pos <= slen && m_search(&m, self, pos)){
    found = 1;
    rb_str_cat(out, sptr(self) + last, m.beg[0] - last);
    VALUE matched = m_group(&m, self, 0);
    if(use_blk || !NIL_P(hash)) m_backref(&m, self, 1);
    lastm = m;
    if(use_blk){
      VALUE r = rb_yield1(blk, matched);
      str_cat_str(out, rb_obj_as_string(r));
    } else if(!NIL_P(hash)){
      VALUE r = rb_hash_aref(hash, matched);
      str_cat_str(out, NIL_P(r) ? str_buf(self) : rb_obj_as_string(r));
    } else {
      sub_expand(out, repl, self, &m);
    }
    last = m.end[0];
    if(m.end[0] == m.beg[0]){
      if(m.end[0] < slen){
        long cl = char_size_at(self, m.end[0]);
        rb_str_cat(out, sptr(self) + m.end[0], cl);
        last = m.end[0] + cl;
        pos = last;
      } else pos = m.end[0] + 1;
    } else pos = m.end[0];
    if(once) break;
  }
  if(!found){
    m_backref(&m, self, 0);
    if(bang) return Qnil;
    VALUE c = str_like(self, sptr(self), SLEN(self));
    return c;
  }
  m_backref(&lastm, self, 1);
  if(last < slen) rb_str_cat(out, sptr(self) + last, slen - last);
  if(bang){ str_set(self, sptr(out), SLEN(out)); return self; }
  return out;
}
static VALUE str_sub(VALUE self, int argc, VALUE *argv, RProc *blk){ return gsub_impl(self, argc, argv, blk, 1, 0); }
static VALUE str_gsub(VALUE self, int argc, VALUE *argv, RProc *blk){ return gsub_impl(self, argc, argv, blk, 0, 0); }
static VALUE str_sub_bang(VALUE self, int argc, VALUE *argv, RProc *blk){ return gsub_impl(self, argc, argv, blk, 1, 1); }
static VALUE str_gsub_bang(VALUE self, int argc, VALUE *argv, RProc *blk){ return gsub_impl(self, argc, argv, blk, 0, 1); }

/* ===================== scan / partition ===================== */

static VALUE scan_result(SMatch *m, VALUE str){
  if(m->ng <= 0) return m_group(m, str, 0);
  VALUE a = rb_ary_new_capa(m->ng);
  for(int i = 1; i <= m->ng; i++) rb_ary_push(a, m_group(m, str, i));
  return a;
}
static VALUE str_scan(VALUE self, VALUE patv, RProc *blk){
  VALUE pat = to_pattern(patv);
  SMatch m; m_init(&m, pat);
  VALUE ary = blk ? Qnil : rb_ary_new();
  long pos = 0, slen = SLEN(self);
  int any = 0;
  while(pos <= slen && m_search(&m, self, pos)){
    any = 1;
    VALUE v = scan_result(&m, self);
    m_backref(&m, self, 1);
    if(blk) rb_yield1(blk, v);
    else rb_ary_push(ary, v);
    if(m.end[0] == m.beg[0]) pos = m.end[0] + (m.end[0] < slen ? char_size_at(self, m.end[0]) : 1);
    else pos = m.end[0];
  }
  if(!any) rb_set_backref(Qnil);
  return blk ? self : ary;
}
static VALUE str_partition(VALUE self, VALUE sep, RProc *blk){
  (void)blk;
  VALUE pat = to_pattern(sep);
  SMatch m; m_init(&m, pat);
  VALUE a = rb_ary_new_capa(3);
  if(!m_search(&m, self, 0)){
    if(m.is_reg) rb_set_backref(Qnil);
    rb_ary_push(a, str_like(self, sptr(self), SLEN(self)));
    rb_ary_push(a, str_buf(self));
    rb_ary_push(a, str_buf(self));
    return a;
  }
  if(m.is_reg) m_backref(&m, self, 1);
  rb_ary_push(a, str_subseq(self, 0, m.beg[0]));
  rb_ary_push(a, str_subseq(self, m.beg[0], m.end[0] - m.beg[0]));
  rb_ary_push(a, str_subseq(self, m.end[0], SLEN(self) - m.end[0]));
  return a;
}
static VALUE str_rpartition(VALUE self, VALUE sep, RProc *blk){
  (void)blk;
  VALUE pat = to_pattern(sep);
  SMatch m; m_init(&m, pat);
  VALUE a = rb_ary_new_capa(3);
  if(!m_rsearch(&m, self, SLEN(self))){
    if(m.is_reg) rb_set_backref(Qnil);
    rb_ary_push(a, str_buf(self));
    rb_ary_push(a, str_buf(self));
    rb_ary_push(a, str_like(self, sptr(self), SLEN(self)));
    return a;
  }
  if(m.is_reg) m_backref(&m, self, 1);
  rb_ary_push(a, str_subseq(self, 0, m.beg[0]));
  rb_ary_push(a, str_subseq(self, m.beg[0], m.end[0] - m.beg[0]));
  rb_ary_push(a, str_subseq(self, m.end[0], SLEN(self) - m.end[0]));
  return a;
}

/* ===================== split ===================== */

static int is_space_char(int c){
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
static VALUE str_split(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..2)", argc);
  VALUE pat = argc > 0 ? argv[0] : Qnil;
  long lim = 0; int limited = 0;
  if(argc > 1 && !NIL_P(argv[1])){
    lim = rb_num2long(argv[1]);
    limited = (lim > 0);
    if(lim == 1){
      VALUE r = rb_ary_new();
      if(SLEN(self) > 0) rb_ary_push(r, str_like(self, sptr(self), SLEN(self)));
      if(blk){ for(long i = 0; i < RARR(r)->len; i++) rb_yield1(blk, RARR(r)->ptr[i]); return self; }
      return r;
    }
  }
  int awk = 0;
  if(NIL_P(pat)) awk = 1;
  else if(is_str(pat) && SLEN(pat) == 1 && sptr(pat)[0] == ' ') awk = 1;

  VALUE result = rb_ary_new();
  const char *ptr = sptr(self);
  long slen = SLEN(self);

  if(awk){
    long beg = 0;
    int skip = 1;
    for(long p = 0; p < slen; p++){
      unsigned char c = (unsigned char)ptr[p];
      if(skip){
        if(is_space_char(c)) continue;
        beg = p; skip = 0;
        if(limited && (long)(RARR(result)->len + 1) >= lim){
          rb_ary_push(result, str_subseq(self, beg, slen - beg));
          beg = slen; skip = 1;
          goto awk_done;
        }
      } else {
        if(is_space_char(c)){
          rb_ary_push(result, str_subseq(self, beg, p - beg));
          skip = 1; beg = slen;
        }
      }
    }
    if(!skip && beg < slen) rb_ary_push(result, str_subseq(self, beg, slen - beg));
  awk_done: ;
  } else {
    VALUE p2 = to_pattern(pat);
    SMatch m; m_init(&m, p2);
    long beg = 0, start = 0;
    int last_null = 0;
    long nfields = 0;
    while(start <= slen && m_search(&m, self, start)){
      long e = m.beg[0];
      if(start == e && m.beg[0] == m.end[0]){
        if(slen == 0){ rb_ary_push(result, str_buf(self)); break; }
        else if(last_null){
          long cl = char_size_at(self, beg);
          if(beg + cl > slen) cl = slen - beg;
          rb_ary_push(result, str_subseq(self, beg, cl));
          beg = start;
        } else {
          start += (start == slen) ? 1 : char_size_at(self, start);
          last_null = 1;
          continue;
        }
      } else {
        rb_ary_push(result, str_subseq(self, beg, e - beg));
        beg = start = m.end[0];
      }
      last_null = 0;
      for(int g = 1; g <= m.ng; g++) rb_ary_push(result, m_group(&m, self, g));
      nfields++;
      if(limited && lim <= nfields + 1) break;
    }
    if(slen > 0 && (limited || slen > beg || lim < 0))
      rb_ary_push(result, str_subseq(self, beg, slen - beg));
  }
  if(!limited && lim == 0){
    long n = RARR(result)->len;
    while(n > 0){
      VALUE last = RARR(result)->ptr[n-1];
      if(!NIL_P(last) && is_str(last) && SLEN(last) == 0) n--;
      else break;
    }
    RARR(result)->len = n;
  }
  if(blk){
    for(long k = 0; k < RARR(result)->len; k++) rb_yield1(blk, RARR(result)->ptr[k]);
    return self;
  }
  return result;
}

/* ===================== length / basic queries ===================== */

static VALUE str_length(VALUE self, RProc *blk){ (void)blk; return LONG2FIX(rb_str_strlen(self)); }
static VALUE str_bytesize(VALUE self, RProc *blk){ (void)blk; return LONG2FIX(SLEN(self)); }
static VALUE str_empty(VALUE self, RProc *blk){ (void)blk; return SLEN(self) == 0 ? Qtrue : Qfalse; }

/* ===================== indexing ===================== */

static int range_beg_len(VALUE range, long *begp, long *lenp, long len, int trunc){
  RRange *r = RRNG(range);
  long b, e;
  if(NIL_P(r->beg)) b = 0; else b = rb_num2long(r->beg);
  if(NIL_P(r->end)) e = len; else e = rb_num2long(r->end);
  if(b < 0){ b += len; if(b < 0) return 0; }
  if(!NIL_P(r->end)){
    if(e < 0) e += len;
    if(!r->excl) e++;
  }
  if(b > len) return 0;
  if(trunc && e > len) e = len;
  long l = e - b;
  if(l < 0) l = 0;
  *begp = b; *lenp = l;
  return 1;
}
static int is_range(VALUE v){ return !FIXNUM_P(v) && BASIC(v)->type == T_RANGE; }

static VALUE reg_capture_ref(SMatch *m, VALUE str, VALUE nth){
  if(is_str(nth)){
    int idx = m_name_index(m, sptr(nth), SLEN(nth));
    if(idx < 0) rb_raise(rb_eIndexError, "undefined group name reference: %s", sptr(nth));
    return m_group(m, str, idx);
  }
  if(!FIXNUM_P(nth) && BASIC(nth)->type == T_SYMBOL){
    const char *nm = rb_id2name(RSYM(nth)->id);
    int idx = m_name_index(m, nm, (long)strlen(nm));
    if(idx < 0) rb_raise(rb_eIndexError, "undefined group name reference: %s", nm);
    return m_group(m, str, idx);
  }
  long n = rb_num2long(nth);
  if(n < 0) n += m->ng + 1;
  if(n < 0 || n > m->ng) return Qnil;
  return m_group(m, str, (int)n);
}

static VALUE str_aref_impl(VALUE self, int argc, VALUE *argv){
  if(argc == 2){
    if(is_reg(argv[0])){
      SMatch m; m_init(&m, argv[0]);
      int ok = m_search(&m, self, 0);
      m_backref(&m, self, ok);
      if(!ok) return Qnil;
      return reg_capture_ref(&m, self, argv[1]);
    }
    long beg = rb_num2long(argv[0]), len = rb_num2long(argv[1]);
    return rb_str_substr(self, beg, len);
  }
  if(argc != 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE a = argv[0];
  if(is_reg(a)){
    SMatch m; m_init(&m, a);
    int ok = m_search(&m, self, 0);
    m_backref(&m, self, ok);
    if(!ok) return Qnil;
    return m_group(&m, self, 0);
  }
  if(is_str(a)){
    SMatch m; m_init(&m, a);
    if(!m_search(&m, self, 0)) return Qnil;
    return str_like(a, sptr(a), SLEN(a));
  }
  if(is_range(a)){
    long beg, len;
    if(!range_beg_len(a, &beg, &len, rb_str_strlen(self), 1)) return Qnil;
    return rb_str_substr(self, beg, len);
  }
  long idx = rb_num2long(a);
#if RUBY_TARGET < 20
  {
    long l = SLEN(self);
    if(idx < 0) idx += l;
    if(idx < 0 || idx >= l) return Qnil;
    return LONG2FIX((unsigned char)sptr(self)[idx]);
  }
#else
  {
    long l = rb_str_strlen(self);
    if(idx < 0) idx += l;
    if(idx < 0 || idx >= l) return Qnil;
    return rb_str_substr(self, idx, 1);
  }
#endif
}
static VALUE str_aref(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk; return str_aref_impl(self, argc, argv);
}

/* byte range for an index expression; returns 0 when out of range */
static int aset_range(VALUE self, int argc, VALUE *argv, long *bbeg, long *blen, VALUE *valp){
  long slen = rb_str_strlen(self);
  if(argc == 3){
    if(is_reg(argv[0])){
      SMatch m; m_init(&m, argv[0]);
      if(!m_search(&m, self, 0)) rb_raise(rb_eIndexError, "regexp not matched");
      m_backref(&m, self, 1);
      long n;
      if(is_str(argv[1])) n = m_name_index(&m, sptr(argv[1]), SLEN(argv[1]));
      else if(!FIXNUM_P(argv[1]) && BASIC(argv[1])->type == T_SYMBOL){
        const char *nm = rb_id2name(RSYM(argv[1])->id);
        n = m_name_index(&m, nm, (long)strlen(nm));
      } else {
        n = rb_num2long(argv[1]);
        if(n < 0) n += m.ng + 1;
      }
      if(n < 0 || n > m.ng || m.beg[n] < 0) rb_raise(rb_eIndexError, "regexp group %ld not matched", n);
      *bbeg = m.beg[n]; *blen = m.end[n] - m.beg[n];
      *valp = argv[2];
      return 1;
    }
    long beg = rb_num2long(argv[0]), len = rb_num2long(argv[1]);
    if(len < 0) rb_raise(rb_eIndexError, "negative length %ld", len);
    if(beg < 0){ beg += slen; if(beg < 0) rb_raise(rb_eIndexError, "index %ld out of string", beg - slen); }
    if(beg > slen) rb_raise(rb_eIndexError, "index %ld out of string", beg);
    if(beg + len > slen) len = slen - beg;
    *bbeg = rb_str_offset(self, beg);
    *blen = rb_str_offset(self, beg + len) - *bbeg;
    *valp = argv[2];
    return 1;
  }
  if(argc != 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 2..3)", argc);
  *valp = argv[1];
  VALUE a = argv[0];
  if(is_reg(a)){
    SMatch m; m_init(&m, a);
    if(!m_search(&m, self, 0)) rb_raise(rb_eIndexError, "regexp not matched");
    m_backref(&m, self, 1);
    *bbeg = m.beg[0]; *blen = m.end[0] - m.beg[0];
    return 1;
  }
  if(is_str(a)){
    SMatch m; m_init(&m, a);
    if(!m_search(&m, self, 0)) rb_raise(rb_eIndexError, "string not matched");
    *bbeg = m.beg[0]; *blen = m.end[0] - m.beg[0];
    return 1;
  }
  if(is_range(a)){
    long beg, len;
    if(!range_beg_len(a, &beg, &len, slen, 1)) rb_raise(rb_eRangeError, "%s out of range", sptr(rb_inspect(a)));
    *bbeg = rb_str_offset(self, beg);
    *blen = rb_str_offset(self, beg + len) - *bbeg;
    return 1;
  }
  long idx = rb_num2long(a);
  if(idx < 0){ idx += slen; if(idx < 0) rb_raise(rb_eIndexError, "index %ld out of string", idx - slen); }
  if(idx >= slen) rb_raise(rb_eIndexError, "index %ld out of string", idx);
  *bbeg = rb_str_offset(self, idx);
  *blen = rb_str_offset(self, idx + 1) - *bbeg;
  return 1;
}
static VALUE str_aset(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  long bbeg, blen; VALUE val;
  aset_range(self, argc, argv, &bbeg, &blen, &val);
#if RUBY_TARGET < 20
  if(argc == 2 && is_int(val) && !is_reg(argv[0]) && !is_str(argv[0]) && !is_range(argv[0])){
    long c = rb_num2long(val);
    if(c < 0 || c > 255) rb_raise(rb_eRangeError, "%ld out of char range", c);
    char ch = (char)c;
    str_splice(self, bbeg, blen, &ch, 1);
    return val;
  }
#endif
  VALUE sv = rb_str_to_str(val);
  enc_combine(self, sv);
  str_splice(self, bbeg, blen, sptr(sv), SLEN(sv));
  return val;
}
static VALUE str_slice_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE removed = str_aref_impl(self, argc, argv);
  if(NIL_P(removed)) return Qnil;
  long bbeg = -1, blen = 0;
  long slen = rb_str_strlen(self);
  if(argc == 2 && !is_reg(argv[0])){
    long beg = rb_num2long(argv[0]), len = rb_num2long(argv[1]);
    if(beg < 0) beg += slen;
    if(beg + len > slen) len = slen - beg;
    bbeg = rb_str_offset(self, beg);
    blen = rb_str_offset(self, beg + len) - bbeg;
  } else if(argc == 2){
    SMatch m; m_init(&m, argv[0]);
    if(!m_search(&m, self, 0)) return Qnil;
    long n;
    if(is_str(argv[1])) n = m_name_index(&m, sptr(argv[1]), SLEN(argv[1]));
    else if(!FIXNUM_P(argv[1]) && BASIC(argv[1])->type == T_SYMBOL){
      const char *nm = rb_id2name(RSYM(argv[1])->id);
      n = m_name_index(&m, nm, (long)strlen(nm));
    } else { n = rb_num2long(argv[1]); if(n < 0) n += m.ng + 1; }
    if(n < 0 || n > m.ng || m.beg[n] < 0) return Qnil;
    bbeg = m.beg[n]; blen = m.end[n] - m.beg[n];
  } else if(is_reg(argv[0])){
    SMatch m; m_init(&m, argv[0]);
    if(!m_search(&m, self, 0)) return Qnil;
    bbeg = m.beg[0]; blen = m.end[0] - m.beg[0];
  } else if(is_str(argv[0])){
    SMatch m; m_init(&m, argv[0]);
    if(!m_search(&m, self, 0)) return Qnil;
    bbeg = m.beg[0]; blen = m.end[0] - m.beg[0];
  } else if(is_range(argv[0])){
    long beg, len;
    if(!range_beg_len(argv[0], &beg, &len, slen, 1)) return Qnil;
    bbeg = rb_str_offset(self, beg);
    blen = rb_str_offset(self, beg + len) - bbeg;
  } else {
    long idx = rb_num2long(argv[0]);
    if(idx < 0) idx += slen;
    if(idx < 0 || idx >= slen) return Qnil;
    bbeg = rb_str_offset(self, idx);
    blen = rb_str_offset(self, idx + 1) - bbeg;
  }
  if(bbeg < 0) return Qnil;
  str_splice(self, bbeg, blen, "", 0);
  return removed;
}
static VALUE str_insert(VALUE self, VALUE idxv, VALUE other, RProc *blk){
  (void)blk;
  str_modify(self);
  long slen = rb_str_strlen(self);
  long idx = rb_num2long(idxv);
  if(idx < 0){
    idx += slen + 1;
    if(idx < 0) rb_raise(rb_eIndexError, "index %ld out of string", rb_num2long(idxv));
  } else if(idx > slen) rb_raise(rb_eIndexError, "index %ld out of string", idx);
  VALUE o = rb_str_to_str(other);
  enc_combine(self, o);
  str_splice(self, rb_str_offset(self, idx), 0, sptr(o), SLEN(o));
  return self;
}

/* ===================== concat / replace ===================== */

static VALUE str_append_one(VALUE self, VALUE v){
  if(is_int(v)){
    long c = rb_num2long(v);
#if RUBY_TARGET < 20
    if(c < 0 || c > 255) rb_raise(rb_eRangeError, "%ld out of char range", c);
    char ch = (char)c;
    rb_str_cat(self, &ch, 1);
#else
    if(c < 0 || c > 0x10FFFF) rb_raise(rb_eRangeError, "%ld out of char range", c);
    char buf[8];
    long n = rb_enc_mbcput(str_enc(self), (int)c, buf);
    rb_str_cat(self, buf, n);
#endif
    return self;
  }
  VALUE o = rb_str_to_str(v);
  rb_encoding *e = enc_combine(self, o);
  rb_str_cat(self, sptr(o), SLEN(o));
  RSTR(self)->enc = e;
  return self;
}
static VALUE str_concat(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
#if RUBY_TARGET < 27
  if(argc != 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1)", argc);
#endif
  for(int i = 0; i < argc; i++) str_append_one(self, argv[i]);
  return self;
}
static VALUE str_lshift(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  str_modify(self);
  return str_append_one(self, v);
}
static VALUE str_prepend(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE pre = str_buf(self);
  for(int i = 0; i < argc; i++){
    VALUE o = rb_str_to_str(argv[i]);
    enc_combine(self, o);
    str_cat_str(pre, o);
  }
  str_splice(self, 0, 0, sptr(pre), SLEN(pre));
  return self;
}
static VALUE str_replace(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE o = rb_str_to_str(other);
  if(o == self) return self;
  str_set(self, sptr(o), SLEN(o));
  RSTR(self)->enc = str_enc(o);
  return self;
}
#if RUBY_TARGET >= 20
static VALUE str_clear(VALUE self, RProc *blk){
  (void)blk;
  str_modify(self);
  str_resize(self, 0);
  return self;
}
#endif

/* ===================== strip / chomp / chop ===================== */

static int strip_space(unsigned char c){ return c == 0 || is_space_char(c); }

static void strip_range(VALUE self, int left, int right, long *b, long *e){
  const char *p = sptr(self);
  long s = 0, t = SLEN(self);
  if(left) while(s < t && strip_space((unsigned char)p[s])) s++;
  if(right) while(t > s && strip_space((unsigned char)p[t-1])) t--;
  *b = s; *e = t;
}
static VALUE strip_copy(VALUE self, int left, int right){
  long b, e; strip_range(self, left, right, &b, &e);
  return str_subseq(self, b, e - b);
}
static VALUE strip_bang(VALUE self, int left, int right){
  str_modify(self);
  long b, e; strip_range(self, left, right, &b, &e);
  if(b == 0 && e == SLEN(self)) return Qnil;
  VALUE t = str_subseq(self, b, e - b);
  str_set(self, sptr(t), SLEN(t));
  return self;
}
static VALUE str_strip(VALUE self, RProc *blk){ (void)blk; return strip_copy(self, 1, 1); }
static VALUE str_lstrip(VALUE self, RProc *blk){ (void)blk; return strip_copy(self, 1, 0); }
static VALUE str_rstrip(VALUE self, RProc *blk){ (void)blk; return strip_copy(self, 0, 1); }
static VALUE str_strip_bang(VALUE self, RProc *blk){ (void)blk; return strip_bang(self, 1, 1); }
static VALUE str_lstrip_bang(VALUE self, RProc *blk){ (void)blk; return strip_bang(self, 1, 0); }
static VALUE str_rstrip_bang(VALUE self, RProc *blk){ (void)blk; return strip_bang(self, 0, 1); }

static long chomp_newlen(VALUE self, int argc, VALUE *argv){
  long len = SLEN(self);
  const char *p = sptr(self);
  if(argc >= 1 && NIL_P(argv[0])) return len;
  if(argc == 0){
    if(len > 0 && p[len-1] == '\n'){ len--; if(len > 0 && p[len-1] == '\r') len--; }
    else if(len > 0 && p[len-1] == '\r') len--;
    return len;
  }
  VALUE sep = rb_str_to_str(argv[0]);
  if(SLEN(sep) == 0){
    while(len > 0 && p[len-1] == '\n'){ len--; if(len > 0 && p[len-1] == '\r') len--; }
    return len;
  }
  if(SLEN(sep) == 1 && sptr(sep)[0] == '\n'){
    if(len > 0 && p[len-1] == '\n'){ len--; if(len > 0 && p[len-1] == '\r') len--; }
    else if(len > 0 && p[len-1] == '\r') len--;
    return len;
  }
  if(SLEN(sep) <= len && memcmp(p + len - SLEN(sep), sptr(sep), SLEN(sep)) == 0)
    return len - SLEN(sep);
  return len;
}
static VALUE str_chomp(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  long n = chomp_newlen(self, argc, argv);
  return str_subseq(self, 0, n);
}
static VALUE str_chomp_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  long n = chomp_newlen(self, argc, argv);
  if(n == SLEN(self)) return Qnil;
  str_resize(self, n);
  return self;
}
static long chop_newlen(VALUE self){
  long len = SLEN(self);
  const char *p = sptr(self);
  if(len == 0) return 0;
  if(len >= 2 && p[len-1] == '\n' && p[len-2] == '\r') return len - 2;
  if(p[len-1] == '\n' || p[len-1] == '\r') return len - 1;
#if RUBY_TARGET >= 20
  {
    long slen = rb_str_strlen(self);
    return rb_str_offset(self, slen - 1);
  }
#else
  return len - 1;
#endif
}
static VALUE str_chop(VALUE self, RProc *blk){
  (void)blk;
  return str_subseq(self, 0, chop_newlen(self));
}
static VALUE str_chop_bang(VALUE self, RProc *blk){
  (void)blk;
  str_modify(self);
  if(SLEN(self) == 0) return Qnil;
  str_resize(self, chop_newlen(self));
  return self;
}

/* ===================== iteration ===================== */

static int opt_chomp(int *argc, VALUE *argv){
#if RUBY_TARGET >= 27
  if(*argc > 0 && is_hash(argv[*argc - 1])){
    VALUE v = rb_hash_lookup(argv[*argc - 1], rb_sym(ID_CACHED("chomp")), Qundef);
    if(v != Qundef){ (*argc)--; return RTEST(v); }
  }
#else
  (void)argc; (void)argv;
#endif
  return 0;
}
static long nl_unit(const char *p, const char *e){
  if(p >= e) return 0;
  if(*p == '\r' && p + 1 < e && p[1] == '\n') return 2;
  if(*p == '\n') return 1;
  return 0;
}
static VALUE line_chomp(VALUE line, int chomp){
  if(!chomp) return line;
  VALUE a[1] = { Qnil };
  return str_chomp(line, 0, a, NULL);
}
static VALUE each_line_do(VALUE self, int argc, VALUE *argv, RProc *blk, VALUE ary){
  int chomp = opt_chomp(&argc, argv);
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  VALUE sep = argc > 0 ? argv[0] : rb_str_new_cstr("\n");
  const char *ptr = sptr(self), *e = ptr + SLEN(self);
  if(NIL_P(sep)){
    VALUE l = line_chomp(str_like(self, ptr, SLEN(self)), chomp);
    if(SLEN(self) > 0 || argc == 0){
      if(SLEN(self) > 0){ if(blk) rb_yield1(blk, l); else rb_ary_push(ary, l); }
    }
    return NIL_P(ary) ? self : ary;
  }
  sep = rb_str_to_str(sep);
  if(SLEN(sep) == 0){
    const char *p = ptr;
    while(p < e){
      long n;
      while(p < e && (n = nl_unit(p, e)) > 0) p += n;   /* skip leading newlines */
      if(p >= e) break;
      const char *q = p;
      const char *stop = NULL;
      while(q < e){
        long n1 = nl_unit(q, e);
        if(n1 > 0){
          long n2 = nl_unit(q + n1, e);
          if(n2 > 0){ stop = q + n1 + n2; break; }
          q += n1;
        } else q += char_size(str_enc(self), q, e);
      }
      if(!stop) stop = e;
      VALUE line = str_subseq(self, p - ptr, stop - p);
      if(chomp){
        long L = SLEN(line);
        const char *lp = sptr(line);
        while(L > 0 && (lp[L-1] == '\n' || lp[L-1] == '\r')) L--;
        line = str_subseq(line, 0, L);
      }
      if(blk) rb_yield1(blk, line); else rb_ary_push(ary, line);
      p = stop;
    }
    return NIL_P(ary) ? self : ary;
  }
  {
    long sl = SLEN(sep);
    const char *sp = sptr(sep);
    const char *p = ptr, *start = ptr;
    while(p <= e - sl){
      if(*p == *sp && memcmp(p, sp, sl) == 0){
        VALUE line = str_subseq(self, start - ptr, p + sl - start);
        if(chomp) line = str_subseq(line, 0, SLEN(line) - sl);
        if(blk) rb_yield1(blk, line); else rb_ary_push(ary, line);
        p += sl; start = p;
        continue;
      }
      p++;
    }
    if(start < e){
      VALUE line = str_subseq(self, start - ptr, e - start);
      if(blk) rb_yield1(blk, line); else rb_ary_push(ary, line);
    }
  }
  return NIL_P(ary) ? self : ary;
}
static VALUE str_each_line(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(!blk) return rb_enum_new(self, ID_CACHED("each_line"), argc, argv);
  return each_line_do(self, argc, argv, blk, Qnil);
}
static VALUE str_lines(VALUE self, int argc, VALUE *argv, RProc *blk){
#if RUBY_TARGET < 20
  /* 1.8.7: String#lines returns an Enumerator */
  if(!blk) return rb_enum_new(self, ID_CACHED("each_line"), argc, argv);
  return each_line_do(self, argc, argv, blk, Qnil);
#else
  if(blk) return each_line_do(self, argc, argv, blk, Qnil);
  VALUE ary = rb_ary_new();
  each_line_do(self, argc, argv, NULL, ary);
  return ary;
#endif
}
static VALUE str_each_byte(VALUE self, RProc *blk){
  if(!blk) return rb_enum_new(self, ID_CACHED("each_byte"), 0, NULL);
  for(long i = 0; i < SLEN(self); i++)
    rb_yield1(blk, LONG2FIX((unsigned char)sptr(self)[i]));
  return self;
}
static VALUE str_bytes(VALUE self, RProc *blk){
  if(blk) return str_each_byte(self, blk);
#if RUBY_TARGET < 20
  return rb_enum_new(self, ID_CACHED("each_byte"), 0, NULL);
#else
  VALUE a = rb_ary_new_capa(SLEN(self) + 1);
  for(long i = 0; i < SLEN(self); i++)
    rb_ary_push(a, LONG2FIX((unsigned char)sptr(self)[i]));
  return a;
#endif
}
#if RUBY_TARGET >= 20
static VALUE str_each_char(VALUE self, RProc *blk){
  if(!blk) return rb_enum_new(self, ID_CACHED("each_char"), 0, NULL);
  const char *p = sptr(self), *e = p + SLEN(self);
  rb_encoding *enc = str_enc(self);
  while(p < e){
    long n = char_size(enc, p, e);
    rb_yield1(blk, str_like(self, p, n));
    p += n;
  }
  return self;
}
static VALUE str_chars(VALUE self, RProc *blk){
  if(blk) return str_each_char(self, blk);
  VALUE a = rb_ary_new();
  const char *p = sptr(self), *e = p + SLEN(self);
  rb_encoding *enc = str_enc(self);
  while(p < e){
    long n = char_size(enc, p, e);
    rb_ary_push(a, str_like(self, p, n));
    p += n;
  }
  return a;
}
static VALUE str_each_codepoint(VALUE self, RProc *blk){
  if(!blk) return rb_enum_new(self, ID_CACHED("each_codepoint"), 0, NULL);
  const char *p = sptr(self), *e = p + SLEN(self);
  rb_encoding *enc = str_enc(self);
  while(p < e){
    int len = 1;
    int cp = rb_enc_codepoint(enc, p, e, &len);
    if(len < 1) len = 1;
    rb_yield1(blk, LONG2FIX(cp));
    p += len;
  }
  return self;
}
static VALUE str_codepoints(VALUE self, RProc *blk){
  if(blk) return str_each_codepoint(self, blk);
  VALUE a = rb_ary_new();
  const char *p = sptr(self), *e = p + SLEN(self);
  rb_encoding *enc = str_enc(self);
  while(p < e){
    int len = 1;
    int cp = rb_enc_codepoint(enc, p, e, &len);
    if(len < 1) len = 1;
    rb_ary_push(a, LONG2FIX(cp));
    p += len;
  }
  return a;
}
#endif

/* ===================== tr / count / delete / squeeze ===================== */

typedef struct { int neg; int *lo, *hi; int n, cap; } TrSet;

static void tr_add(TrSet *s, int lo, int hi){
  if(s->n >= s->cap){
    s->cap = s->cap ? s->cap * 2 : 8;
    s->lo = xrealloc(s->lo, s->cap * sizeof(int));
    s->hi = xrealloc(s->hi, s->cap * sizeof(int));
  }
  s->lo[s->n] = lo; s->hi[s->n] = hi; s->n++;
}
static int tr_getc(rb_encoding *enc, const char **pp, const char *e, int *escaped){
  const char *p = *pp;
  *escaped = 0;
  if(*p == '\\' && p + 1 < e){ p++; *escaped = 1; }
  int len = 1;
#if RUBY_TARGET >= 20
  int cp = rb_enc_codepoint(enc, p, e, &len);
  if(len < 1) len = 1;
#else
  (void)enc;
  int cp = (unsigned char)*p;
#endif
  *pp = p + len;
  return cp;
}
static void tr_parse_ex(TrSet *s, VALUE spec, int allow_neg){
  rb_encoding *enc = str_enc(spec);
  const char *p = sptr(spec), *e = p + SLEN(spec);
  s->neg = 0; s->lo = s->hi = NULL; s->n = s->cap = 0;
  if(allow_neg && p < e && *p == '^' && e - p > 1){ s->neg = 1; p++; }
  while(p < e){
    int esc;
    int c = tr_getc(enc, &p, e, &esc);
    if(p < e && *p == '-' && p + 1 < e){
      const char *save = p;
      p++;
      int esc2;
      int c2 = tr_getc(enc, &p, e, &esc2);
      if(c2 < c){
        p = save;
        tr_add(s, c, c);
        continue;
      }
      tr_add(s, c, c2);
      continue;
    }
    tr_add(s, c, c);
  }
}
static void tr_parse(TrSet *s, VALUE spec){ tr_parse_ex(s, spec, 1); }
static int tr_in(TrSet *s, int cp){
  for(int i = 0; i < s->n; i++) if(cp >= s->lo[i] && cp <= s->hi[i]) return 1;
  return 0;
}
static int tr_member(TrSet *s, int cp){ return tr_in(s, cp) ^ s->neg; }
static long tr_total(TrSet *s){
  long t = 0;
  for(int i = 0; i < s->n; i++) t += (long)s->hi[i] - s->lo[i] + 1;
  return t;
}
static long tr_index(TrSet *s, int cp){
  long t = 0;
  for(int i = 0; i < s->n; i++){
    if(cp >= s->lo[i] && cp <= s->hi[i]) return t + (cp - s->lo[i]);
    t += (long)s->hi[i] - s->lo[i] + 1;
  }
  return -1;
}
static int tr_at(TrSet *s, long idx){
  for(int i = 0; i < s->n; i++){
    long w = (long)s->hi[i] - s->lo[i] + 1;
    if(idx < w) return (int)(s->lo[i] + idx);
    idx -= w;
  }
  return -1;
}
static void tr_free(TrSet *s){ free(s->lo); free(s->hi); s->lo = s->hi = NULL; s->n = s->cap = 0; }

static VALUE tr_run(VALUE self, VALUE from, VALUE to, int squeeze){
  TrSet f, t;
  tr_parse(&f, rb_str_to_str(from));
  tr_parse_ex(&t, rb_str_to_str(to), 0);
  long ttotal = tr_total(&t);
  VALUE out = str_buf(self);
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  int last = -1;
  while(p < e){
    long clen = char_size(enc, p, e);
    int cp;
#if RUBY_TARGET < 20
    cp = (unsigned char)*p;
#else
    { int l2 = 1; cp = rb_enc_codepoint(enc, p, e, &l2); if(l2 > 0) clen = l2; }
#endif
    int repl = -1, hit = 0;
    if(f.neg){
      if(!tr_in(&f, cp)){ hit = 1; repl = ttotal > 0 ? tr_at(&t, ttotal - 1) : -1; }
    } else {
      long i = tr_index(&f, cp);
      if(i >= 0){
        hit = 1;
        if(ttotal > 0) repl = tr_at(&t, i < ttotal ? i : ttotal - 1);
      }
    }
    if(hit){
      if(repl >= 0){
        if(!(squeeze && last == repl)) cat_cp(out, enc, repl);
        last = repl;
      }
    } else {
      rb_str_cat(out, p, clen);
      last = -1;
    }
    p += clen;
  }
  tr_free(&f); tr_free(&t);
  return out;
}
static VALUE str_tr(VALUE self, VALUE from, VALUE to, RProc *blk){
  (void)blk; return tr_run(self, from, to, 0);
}
static VALUE str_tr_s(VALUE self, VALUE from, VALUE to, RProc *blk){
  (void)blk; return tr_run(self, from, to, 1);
}
static VALUE str_tr_bang(VALUE self, VALUE from, VALUE to, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE r = tr_run(self, from, to, 0);
  if(rb_str_equal(r, self)) return Qnil;
  str_set(self, sptr(r), SLEN(r));
  return self;
}
static VALUE str_tr_s_bang(VALUE self, VALUE from, VALUE to, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE r = tr_run(self, from, to, 1);
  if(rb_str_equal(r, self)) return Qnil;
  str_set(self, sptr(r), SLEN(r));
  return self;
}

static int sets_member(TrSet *sets, int n, int cp){
  for(int i = 0; i < n; i++) if(!tr_member(&sets[i], cp)) return 0;
  return 1;
}
static TrSet *sets_build(int argc, VALUE *argv){
  TrSet *s = xcalloc(argc ? argc : 1, sizeof(TrSet));
  for(int i = 0; i < argc; i++) tr_parse(&s[i], rb_str_to_str(argv[i]));
  return s;
}
static VALUE str_count(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1) rb_raise(rb_eArgError, "wrong number of arguments (given 0, expected 1+)");
  TrSet *s = sets_build(argc, argv);
  long n = 0;
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  while(p < e){
    long clen = char_size(enc, p, e);
    int cp;
#if RUBY_TARGET < 20
    cp = (unsigned char)*p;
#else
    { int l2 = 1; cp = rb_enc_codepoint(enc, p, e, &l2); if(l2 > 0) clen = l2; }
#endif
    if(sets_member(s, argc, cp)) n++;
    p += clen;
  }
  for(int i = 0; i < argc; i++) tr_free(&s[i]);
  free(s);
  return LONG2FIX(n);
}
static VALUE delete_run(VALUE self, int argc, VALUE *argv){
  TrSet *s = sets_build(argc, argv);
  VALUE out = str_buf(self);
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  while(p < e){
    long clen = char_size(enc, p, e);
    int cp;
#if RUBY_TARGET < 20
    cp = (unsigned char)*p;
#else
    { int l2 = 1; cp = rb_enc_codepoint(enc, p, e, &l2); if(l2 > 0) clen = l2; }
#endif
    if(!sets_member(s, argc, cp)) rb_str_cat(out, p, clen);
    p += clen;
  }
  for(int i = 0; i < argc; i++) tr_free(&s[i]);
  free(s);
  return out;
}
static VALUE str_delete(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1) rb_raise(rb_eArgError, "wrong number of arguments (given 0, expected 1+)");
  return delete_run(self, argc, argv);
}
static VALUE str_delete_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  if(argc < 1) rb_raise(rb_eArgError, "wrong number of arguments (given 0, expected 1+)");
  VALUE r = delete_run(self, argc, argv);
  if(rb_str_equal(r, self)) return Qnil;
  str_set(self, sptr(r), SLEN(r));
  return self;
}
static VALUE squeeze_run(VALUE self, int argc, VALUE *argv){
  TrSet *s = argc ? sets_build(argc, argv) : NULL;
  VALUE out = str_buf(self);
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  int last = -1;
  while(p < e){
    long clen = char_size(enc, p, e);
    int cp;
#if RUBY_TARGET < 20
    cp = (unsigned char)*p;
#else
    { int l2 = 1; cp = rb_enc_codepoint(enc, p, e, &l2); if(l2 > 0) clen = l2; }
#endif
    int squeezable = (argc == 0) || sets_member(s, argc, cp);
    if(!(squeezable && cp == last)) rb_str_cat(out, p, clen);
    last = cp;
    p += clen;
  }
  if(s){ for(int i = 0; i < argc; i++) tr_free(&s[i]); free(s); }
  return out;
}
static VALUE str_squeeze(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk; return squeeze_run(self, argc, argv);
}
static VALUE str_squeeze_bang(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE r = squeeze_run(self, argc, argv);
  if(rb_str_equal(r, self)) return Qnil;
  str_set(self, sptr(r), SLEN(r));
  return self;
}

/* ===================== justification ===================== */

static void pad_chars(VALUE out, VALUE pad, long n){
  if(n <= 0) return;
  rb_encoding *enc = str_enc(pad);
  const char *b = sptr(pad), *e = b + SLEN(pad);
  const char *p = b;
  while(n > 0){
    if(p >= e) p = b;
    long cl = char_size(enc, p, e);
    rb_str_cat(out, p, cl);
    p += cl;
    n--;
  }
}
static VALUE just(VALUE self, int argc, VALUE *argv, int mode){
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  long width = rb_num2long(argv[0]);
  VALUE pad = argc > 1 ? rb_str_to_str(argv[1]) : rb_str_new_cstr(" ");
  if(SLEN(pad) == 0) rb_raise(rb_eArgError, "zero width padding");
  long slen = rb_str_strlen(self);
  if(width <= slen) return str_like(self, sptr(self), SLEN(self));
  long n = width - slen;
  VALUE out = str_buf(self);
  if(mode == 1) pad_chars(out, pad, n);
  else if(mode == 2) pad_chars(out, pad, n / 2);
  rb_str_cat(out, sptr(self), SLEN(self));
  if(mode == 0) pad_chars(out, pad, n);
  else if(mode == 2) pad_chars(out, pad, n - n / 2);
  return out;
}
static VALUE str_ljust(VALUE self, int argc, VALUE *argv, RProc *blk){ (void)blk; return just(self, argc, argv, 0); }
static VALUE str_rjust(VALUE self, int argc, VALUE *argv, RProc *blk){ (void)blk; return just(self, argc, argv, 1); }
static VALUE str_center(VALUE self, int argc, VALUE *argv, RProc *blk){ (void)blk; return just(self, argc, argv, 2); }

/* ===================== reverse / succ / upto ===================== */

static VALUE reverse_of(VALUE self){
  VALUE out = str_buf(self);
  rb_encoding *enc = str_enc(self);
  const char *b = sptr(self), *e = b + SLEN(self);
  long n = SLEN(self);
  str_resize(out, n);
  char *o = sptr(out) + n;
  const char *p = b;
  while(p < e){
    long cl = char_size(enc, p, e);
    if(cl > e - p) cl = e - p;
    o -= cl;
    memcpy(o, p, cl);
    p += cl;
  }
  return out;
}
static VALUE str_reverse(VALUE self, RProc *blk){ (void)blk; return reverse_of(self); }
static VALUE str_reverse_bang(VALUE self, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE r = reverse_of(self);
  str_set(self, sptr(r), SLEN(r));
  return self;
}

static VALUE succ_of(VALUE self){
  long len = SLEN(self);
  if(len == 0) return str_buf(self);
  VALUE out = str_like(self, sptr(self), len);
  rb_encoding *enc = str_enc(out);
  char *p = sptr(out);
  long cap = 16, nch = 0;
  long *offs = xmalloc(cap * sizeof(long));
  for(long i = 0; i < len; ){
    if(nch >= cap){ cap *= 2; offs = xrealloc(offs, cap * sizeof(long)); }
    offs[nch++] = i;
    i += char_size(enc, p + i, p + len);
  }
  int found = 0, done = 0;
  char carry = 0;
  long carry_at = 0;
  for(long i = nch - 1; i >= 0; i--){
    long off = offs[i];
    long cl = ((i + 1 < nch) ? offs[i+1] : len) - off;
    if(cl != 1) continue;
    unsigned char c = (unsigned char)p[off];
    if(c >= '0' && c <= '9'){
      found = 1;
      if(c < '9'){ p[off] = (char)(c + 1); done = 1; break; }
      p[off] = '0'; carry = '1'; carry_at = off;
    } else if(c >= 'a' && c <= 'z'){
      found = 1;
      if(c < 'z'){ p[off] = (char)(c + 1); done = 1; break; }
      p[off] = 'a'; carry = 'a'; carry_at = off;
    } else if(c >= 'A' && c <= 'Z'){
      found = 1;
      if(c < 'Z'){ p[off] = (char)(c + 1); done = 1; break; }
      p[off] = 'A'; carry = 'A'; carry_at = off;
    }
  }
  if(found){
    if(!done) str_splice(out, carry_at, 0, &carry, 1);
    free(offs);
    return out;
  }
  for(long i = nch - 1; i >= 0; i--){
    long off = offs[i];
    long cl = ((i + 1 < nch) ? offs[i+1] : len) - off;
    p = sptr(out);
    if(cl == 1){
      unsigned char c = (unsigned char)p[off];
      if(c < 0xFF){ p[off] = (char)(c + 1); done = 1; break; }
      p[off] = 0;
    } else {
#if RUBY_TARGET >= 20
      int l2 = 1;
      int cp = rb_enc_codepoint(enc, p + off, p + len, &l2);
      if(rb_enc_codelen(enc, cp + 1) == cl){
        rb_enc_mbcput(enc, cp + 1, p + off);
        done = 1;
        break;
      }
#endif
      unsigned char c = (unsigned char)p[off + cl - 1];
      if(c < 0xFF){ p[off + cl - 1] = (char)(c + 1); done = 1; break; }
    }
  }
  if(!done) str_splice(out, 0, 0, "\001", 1);
  free(offs);
  return out;
}
static VALUE str_succ(VALUE self, RProc *blk){ (void)blk; return succ_of(self); }
static VALUE str_succ_bang(VALUE self, RProc *blk){
  (void)blk;
  str_modify(self);
  VALUE r = succ_of(self);
  str_set(self, sptr(r), SLEN(r));
  return self;
}
static int all_digits(VALUE s){
  if(SLEN(s) == 0) return 0;
  const char *p = sptr(s);
  for(long i = 0; i < SLEN(s); i++) if(p[i] < '0' || p[i] > '9') return 0;
  return 1;
}
static VALUE str_upto(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  if(!blk) return rb_enum_new(self, ID_CACHED("upto"), argc, argv);
  VALUE end = rb_str_to_str(argv[0]);
  int excl = argc > 1 && RTEST(argv[1]);

  if(all_digits(self) && all_digits(end) && SLEN(self) < 18 && SLEN(end) < 18){
    long b = strtol(sptr(self), NULL, 10);
    long e = strtol(sptr(end), NULL, 10);
    long width = SLEN(self);
    for(long i = b; excl ? i < e : i <= e; i++){
      char buf[64];
      snprintf(buf, sizeof buf, "%0*ld", (int)width, i);
      rb_yield1(blk, str_new(buf, (long)strlen(buf), str_enc(self)));
    }
    return self;
  }
  int n = rb_str_cmp(self, end);
  if(n > 0 || (excl && n == 0)) return self;
  VALUE after = succ_of(end);
  VALUE cur = str_like(self, sptr(self), SLEN(self));
  while(!rb_str_equal(cur, after)){
    VALUE next = Qnil;
    if(excl || !rb_str_equal(cur, end)) next = succ_of(cur);
    rb_yield1(blk, cur);
    if(NIL_P(next)) break;
    cur = next;
    if(excl && rb_str_equal(cur, end)) break;
    if(SLEN(cur) > SLEN(end) || SLEN(cur) == 0) break;
  }
  return self;
}

/* ===================== numeric conversion ===================== */

static VALUE str_to_inum(const char *p, long len, int base, int def_base){
  const char *e = p + len;
  while(p < e && (is_space_char((unsigned char)*p) || *p == 0)) p++;
  int neg = 0;
  if(p < e && (*p == '+' || *p == '-')){ neg = (*p == '-'); p++; }
  if(base <= 0){
    base = def_base;
    if(p + 1 < e && p[0] == '0'){
      char c = p[1];
      if(c == 'x' || c == 'X'){ base = 16; p += 2; }
      else if(c == 'b' || c == 'B'){ base = 2; p += 2; }
      else if(c == 'o' || c == 'O'){ base = 8; p += 2; }
      else if(c == 'd' || c == 'D'){ base = 10; p += 2; }
      else if(def_base == 8){ base = 8; p += 1; }
    }
  } else if(base == 16){
    if(p + 1 < e && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
  } else if(base == 2){
    if(p + 1 < e && p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) p += 2;
  } else if(base == 8){
    if(p + 1 < e && p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) p += 2;
  }
  if(base < 2 || base > 36) rb_raise(rb_eArgError, "invalid radix %d", base);

  VALUE digits = rb_str_new_enc("", 0, enc_usascii);
  unsigned long long v = 0;
  int overflow = 0, last_uscore = 1;
  for(; p < e; p++){
    char c = *p;
    if(c == '_'){
      if(last_uscore) break;
      last_uscore = 1;
      continue;
    }
    int d;
    if(c >= '0' && c <= '9') d = c - '0';
    else if(c >= 'a' && c <= 'z') d = c - 'a' + 10;
    else if(c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else break;
    if(d >= base) break;
    last_uscore = 0;
    rb_str_cat(digits, &c, 1);
    if(!overflow){
      if(v > (ULLONG_MAX - (unsigned)d) / (unsigned)base) overflow = 1;
      else v = v * (unsigned)base + (unsigned)d;
    }
  }
  if(SLEN(digits) == 0) return LONG2FIX(0);
  if(overflow || v > (unsigned long long)FIXNUM_MAX + (neg ? 1ULL : 0ULL)){
    VALUE full = rb_str_new_enc(neg ? "-" : "", neg ? 1 : 0, enc_usascii);
    rb_str_cat(full, RSTR(digits)->ptr, SLEN(digits));
    return rb_big_new_str(RSTR(full)->ptr, base);
  }
  long r = (long)v;
  return LONG2FIX(neg ? -r : r);
}
static VALUE str_to_i(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  int base = 10;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 1) base = (int)rb_num2long(argv[0]);
  if(base < 0 || base == 1 || base > 36) rb_raise(rb_eArgError, "invalid radix %d", base);
  return str_to_inum(sptr(self), SLEN(self), base, 10);
}
static VALUE str_hex(VALUE self, RProc *blk){ (void)blk; return str_to_inum(sptr(self), SLEN(self), 16, 16); }
static VALUE str_oct(VALUE self, RProc *blk){ (void)blk; return str_to_inum(sptr(self), SLEN(self), 0, 8); }

static VALUE str_to_f(VALUE self, RProc *blk){
  (void)blk;
  const char *p = sptr(self), *e = p + SLEN(self);
  char buf[512];
  long n = 0;
  while(p < e && is_space_char((unsigned char)*p)) p++;
  if(p < e && (*p == '+' || *p == '-')) buf[n++] = *p++;
  int seen_dot = 0, seen_e = 0, seen_digit = 0;
  while(p < e && n < (long)sizeof(buf) - 1){
    char c = *p;
    if(c == '_'){
      if(!seen_digit) break;
      p++;
      continue;
    }
    if(c >= '0' && c <= '9'){ seen_digit = 1; buf[n++] = c; p++; continue; }
    if(c == '.' && !seen_dot && !seen_e){
      if(p + 1 >= e || p[1] < '0' || p[1] > '9') break;
      seen_dot = 1; buf[n++] = c; p++; continue;
    }
    if((c == 'e' || c == 'E') && seen_digit && !seen_e){
      const char *q = p + 1;
      if(q < e && (*q == '+' || *q == '-')) q++;
      if(q >= e || *q < '0' || *q > '9') break;
      seen_e = 1;
      buf[n++] = c; p++;
      if(p < e && (*p == '+' || *p == '-')) buf[n++] = *p++;
      continue;
    }
    break;
  }
  buf[n] = 0;
  if(!seen_digit) return rb_float_new(0.0);
  return rb_float_new(strtod(buf, NULL));
}

/* ===================== conversion / identity ===================== */

static VALUE str_to_s(VALUE self, RProc *blk){
  (void)blk;
  if(rb_class_of(self) != rb_cString) return str_like(self, sptr(self), SLEN(self));
  return self;
}
static VALUE str_to_str(VALUE self, RProc *blk){ (void)blk; return self; }
static VALUE str_to_sym(VALUE self, RProc *blk){
  (void)blk;
  return rb_sym(rb_intern2(sptr(self), SLEN(self)));
}
static VALUE str_dup_m(VALUE self, RProc *blk){
  (void)blk;
  VALUE n = str_new(sptr(self), SLEN(self), str_enc(self));
  BASIC(n)->klass = rb_class_of(self);
  ivtbl *iv = BASIC(self)->iv;
  if(iv) for(int i = 0; i < iv->n; i++) ivtbl_set(&BASIC(n)->iv, iv->keys[i], iv->vals[i]);
  return n;
}
static VALUE str_clone(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  VALUE n = str_dup_m(self, NULL);
  int freeze = -1;
  if(argc > 0 && is_hash(argv[argc-1])){
    VALUE v = rb_hash_lookup(argv[argc-1], rb_sym(ID_CACHED("freeze")), Qundef);
    if(v == Qfalse) freeze = 0;
    else if(v == Qtrue) freeze = 1;
    else if(v != Qundef && !NIL_P(v))
      rb_raise(rb_eArgError, "unexpected value for freeze: %s", rb_obj_classname(v));
  }
  if(freeze == 1) BASIC(n)->frozen = 1;
  else if(freeze == -1){
    BASIC(n)->frozen = BASIC(self)->frozen;
    BASIC(n)->chilled = BASIC(self)->chilled;
  }
  return n;
}
static VALUE str_freeze_m(VALUE self, RProc *blk){
  (void)blk;
  BASIC(self)->chilled = 0;
  BASIC(self)->frozen = 1;
  return self;
}
static VALUE str_frozen_p(VALUE self, RProc *blk){
  (void)blk; return BASIC(self)->frozen ? Qtrue : Qfalse;
}
#if RUBY_TARGET >= 27
static VALUE str_uminus(VALUE self, RProc *blk){
  (void)blk;
  if(BASIC(self)->frozen && rb_class_of(self) == rb_cString) return self;
  VALUE n = str_new(sptr(self), SLEN(self), str_enc(self));
  BASIC(n)->frozen = 1;
  return n;
}
static VALUE str_uplus(VALUE self, RProc *blk){
  (void)blk;
  if(BASIC(self)->frozen || BASIC(self)->chilled) return str_dup_m(self, NULL);
  return self;
}
#endif

/* ===================== characters and bytes ===================== */

#if RUBY_TARGET >= 20
static VALUE str_ord(VALUE self, RProc *blk){
  (void)blk;
  if(SLEN(self) == 0) rb_raise(rb_eArgError, "empty string");
  rb_encoding *enc = str_enc(self);
  const char *p = sptr(self), *e = p + SLEN(self);
  if(enc == enc_utf8){
    long n = rb_enc_mbclen(enc, p, e);
    if(!utf8_valid(p, n)) rb_raise(rb_eArgError, "invalid byte sequence in UTF-8");
  } else if(enc == enc_usascii && ((unsigned char)*p & 0x80)){
    rb_raise(rb_eArgError, "invalid byte sequence in US-ASCII");
  }
  int len = 1;
  return LONG2FIX(rb_enc_codepoint(enc, p, e, &len));
}
#endif
#if RUBY_TARGET >= 20
static VALUE str_chr(VALUE self, RProc *blk){
  (void)blk;
  if(SLEN(self) == 0) return str_buf(self);
  return rb_str_substr(self, 0, 1);
}
#endif

#if RUBY_TARGET >= 20
static VALUE str_getbyte(VALUE self, VALUE idx, RProc *blk){
  (void)blk;
  long i = rb_num2long(idx);
  if(i < 0) i += SLEN(self);
  if(i < 0 || i >= SLEN(self)) return Qnil;
  return LONG2FIX((unsigned char)sptr(self)[i]);
}
static VALUE str_setbyte(VALUE self, VALUE idx, VALUE val, RProc *blk){
  (void)blk;
  str_modify(self);
  long i = rb_num2long(idx);
  long v = rb_num2long(val);
  if(i < 0) i += SLEN(self);
  if(i < 0 || i >= SLEN(self)) rb_raise(rb_eIndexError, "index %ld out of string", rb_num2long(idx));
  sptr(self)[i] = (char)(v & 0xFF);
  return val;
}
static VALUE str_byteslice(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  long len = SLEN(self);
  long beg, l;
  if(argc == 1){
    if(is_range(argv[0])){
      if(!range_beg_len(argv[0], &beg, &l, len, 1)) return Qnil;
    } else {
      beg = rb_num2long(argv[0]);
      l = 1;
      if(beg < 0) beg += len;
      if(beg < 0 || beg >= len) return Qnil;
    }
  } else if(argc == 2){
    beg = rb_num2long(argv[0]);
    l = rb_num2long(argv[1]);
    if(l < 0) return Qnil;
    if(beg < 0) beg += len;
    if(beg < 0 || beg > len) return Qnil;
    if(beg + l > len) l = len - beg;
  } else {
    rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
    return Qnil;
  }
  return str_subseq(self, beg, l);
}
#endif

#if RUBY_TARGET >= 33
static void check_byte_boundary(VALUE s, long off){
  if(str_enc(s) != enc_utf8) return;
  if(off <= 0 || off >= SLEN(s)) return;
  if(((unsigned char)sptr(s)[off] & 0xC0) == 0x80)
    rb_raise(rb_eIndexError, "offset %ld does not land on character boundary", off);
}
static VALUE str_bytesplice(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  long beg, len, sbeg = 0, slen = -1;
  VALUE rep;
  long blen = SLEN(self);
  if(argc >= 1 && is_range(argv[0])){
    if(argc < 2 || argc > 3) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 2..3)", argc);
    if(!range_beg_len(argv[0], &beg, &len, blen, 1)) rb_raise(rb_eRangeError, "out of range");
    rep = rb_str_to_str(argv[1]);
    if(argc == 3){
      if(!is_range(argv[2])) rb_raise(rb_eTypeError, "wrong argument type %s (expected Range)", rb_obj_classname(argv[2]));
      if(!range_beg_len(argv[2], &sbeg, &slen, SLEN(rep), 1)) rb_raise(rb_eRangeError, "out of range");
    }
  } else {
    if(argc != 3 && argc != 5) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 3 or 5)", argc);
    beg = rb_num2long(argv[0]);
    len = rb_num2long(argv[1]);
    rep = rb_str_to_str(argv[2]);
    if(argc == 5){ sbeg = rb_num2long(argv[3]); slen = rb_num2long(argv[4]); }
  }
  if(len < 0) rb_raise(rb_eIndexError, "negative length %ld", len);
  if(beg < 0) beg += blen;
  if(beg < 0 || beg > blen) rb_raise(rb_eIndexError, "index %ld out of string", beg);
  if(beg + len > blen) len = blen - beg;
  if(slen < 0) slen = SLEN(rep) - sbeg;
  if(sbeg < 0) sbeg += SLEN(rep);
  if(sbeg < 0 || sbeg > SLEN(rep)) rb_raise(rb_eIndexError, "index %ld out of string", sbeg);
  if(sbeg + slen > SLEN(rep)) slen = SLEN(rep) - sbeg;
  check_byte_boundary(self, beg);
  check_byte_boundary(self, beg + len);
  enc_combine(self, rep);
  str_splice(self, beg, len, sptr(rep) + sbeg, slen);
  return self;
}
static VALUE str_byteindex(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  long pos = 0;
  if(argc == 2){
    pos = rb_num2long(argv[1]);
    if(pos < 0){ pos += SLEN(self); if(pos < 0){ if(is_reg(argv[0])) rb_set_backref(Qnil); return Qnil; } }
  }
  if(pos > SLEN(self)) return Qnil;
  VALUE pat = to_pattern(argv[0]);
  SMatch m; m_init(&m, pat);
  int ok = m_search(&m, self, pos);
  if(m.is_reg) m_backref(&m, self, ok);
  if(!ok) return Qnil;
  return LONG2FIX(m.beg[0]);
}
static VALUE str_byterindex(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  long pos = SLEN(self);
  if(argc == 2){
    pos = rb_num2long(argv[1]);
    if(pos < 0){ pos += SLEN(self); if(pos < 0) return Qnil; }
    if(pos > SLEN(self)) pos = SLEN(self);
  }
  VALUE pat = to_pattern(argv[0]);
  SMatch m; m_init(&m, pat);
  int ok = m_rsearch(&m, self, pos);
  if(m.is_reg) m_backref(&m, self, ok);
  if(!ok) return Qnil;
  return LONG2FIX(m.beg[0]);
}
#endif

#if RUBY_TARGET >= 34
static VALUE str_append_as_bytes(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  str_modify(self);
  rb_encoding *keep = str_enc(self);
  for(int i = 0; i < argc; i++){
    if(is_int(argv[i])){
      long v = rb_num2long(argv[i]);
      char c = (char)(((unsigned long)v) & 0xFF);
      rb_str_cat(self, &c, 1);
    } else if(is_str(argv[i])){
      rb_str_cat(self, sptr(argv[i]), SLEN(argv[i]));
    } else {
      rb_raise(rb_eTypeError, "wrong argument type %s (expected String or Integer)",
               rb_obj_classname(argv[i]));
    }
  }
  RSTR(self)->enc = keep;
  return self;
}
#endif

/* ===================== delete_prefix / delete_suffix ===================== */

#if RUBY_TARGET >= 27
static long prefix_len(VALUE self, VALUE pre){
  if(SLEN(pre) == 0 || SLEN(pre) > SLEN(self)) return 0;
  if(memcmp(sptr(self), sptr(pre), SLEN(pre)) == 0) return SLEN(pre);
  return 0;
}
static long suffix_len(VALUE self, VALUE suf){
  if(SLEN(suf) == 0 || SLEN(suf) > SLEN(self)) return 0;
  if(memcmp(sptr(self) + SLEN(self) - SLEN(suf), sptr(suf), SLEN(suf)) == 0) return SLEN(suf);
  return 0;
}
static VALUE str_delete_prefix(VALUE self, VALUE pre, RProc *blk){
  (void)blk;
  long n = prefix_len(self, rb_str_to_str(pre));
  return str_subseq(self, n, SLEN(self) - n);
}
static VALUE str_delete_suffix(VALUE self, VALUE suf, RProc *blk){
  (void)blk;
  long n = suffix_len(self, rb_str_to_str(suf));
  return str_subseq(self, 0, SLEN(self) - n);
}
static VALUE str_delete_prefix_bang(VALUE self, VALUE pre, RProc *blk){
  (void)blk;
  str_modify(self);
  long n = prefix_len(self, rb_str_to_str(pre));
  if(n == 0) return Qnil;
  str_splice(self, 0, n, "", 0);
  return self;
}
static VALUE str_delete_suffix_bang(VALUE self, VALUE suf, RProc *blk){
  (void)blk;
  str_modify(self);
  long n = suffix_len(self, rb_str_to_str(suf));
  if(n == 0) return Qnil;
  str_resize(self, SLEN(self) - n);
  return self;
}
#endif

/* ===================== encoding ===================== */

#if RUBY_TARGET >= 20
static rb_encoding *enc_from_arg(VALUE v){
  if(NIL_P(v)) rb_raise(rb_eTypeError, "no implicit conversion of nil into String");
  VALUE name = rb_check_string_type(v);
  if(NIL_P(name)) name = rb_funcall(v, ID_CACHED("name"), 0);
  if(!is_str(name)) rb_raise(rb_eTypeError, "no implicit conversion into String");
  const char *cs = sptr(name);
  if(!strcasecmp(cs, "locale") || !strcasecmp(cs, "external") || !strcasecmp(cs, "filesystem"))
    return default_enc();
  if(!strcasecmp(cs, "internal")) return enc_binary;
  rb_encoding *e = rb_enc_find(cs);
  if(!e) rb_raise(rb_eArgError, "unknown encoding name - %s", cs);
  return e;
}
static VALUE str_encoding(VALUE self, RProc *blk){ (void)blk; return rb_enc_obj(str_enc(self)); }
static VALUE str_force_encoding(VALUE self, VALUE encv, RProc *blk){
  (void)blk;
  str_modify(self);
  RSTR(self)->enc = enc_from_arg(encv);
  return self;
}
static VALUE str_valid_encoding(VALUE self, RProc *blk){ (void)blk; return str_valid_enc(self) ? Qtrue : Qfalse; }
static VALUE str_ascii_only(VALUE self, RProc *blk){ (void)blk; return str_is_ascii_only(self) ? Qtrue : Qfalse; }
static VALUE str_b(VALUE self, RProc *blk){ (void)blk; return str_new(sptr(self), SLEN(self), enc_binary); }
static VALUE str_encode(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  while(argc > 0 && is_hash(argv[argc-1])) argc--;
  rb_encoding *dst = argc > 0 ? enc_from_arg(argv[0]) : default_enc();
  rb_encoding *src = argc > 1 ? enc_from_arg(argv[1]) : str_enc(self);
  if(dst != enc_binary){
    if(dst == enc_usascii && !str_is_ascii_only(self))
      rb_raise(rb_eEncodingError, "\"\\x%02X\" from %s to US-ASCII",
               (unsigned char)sptr(self)[0], src->name);
    if(dst == enc_utf8 && !str_is_ascii_only(self) && !utf8_valid(sptr(self), SLEN(self)))
      rb_raise(rb_eEncodingError, "\"\\x%02X\" from %s to UTF-8",
               (unsigned char)sptr(self)[0], src->name);
  }
  return str_new(sptr(self), SLEN(self), dst);
}
#endif

/* ===================== String.new ===================== */

static VALUE str_init(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  rb_check_frozen(self);
  VALUE opts = Qnil;
  if(argc > 0 && is_hash(argv[argc-1])){ opts = argv[argc-1]; argc--; }
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 1){
    VALUE o = rb_str_to_str(argv[0]);
    str_set(self, sptr(o), SLEN(o));
    RSTR(self)->enc = str_enc(o);
  } else if(!RSTR(self)->ptr){
    sptr(self);
    if(!RSTR(self)->enc) RSTR(self)->enc = enc_binary;
  }
  if(!NIL_P(opts)){
#if RUBY_TARGET >= 20
    VALUE e = rb_hash_lookup(opts, rb_sym(ID_CACHED("encoding")), Qundef);
    if(e != Qundef && !NIL_P(e)) RSTR(self)->enc = enc_from_arg(e);
#endif
    VALUE c = rb_hash_lookup(opts, rb_sym(ID_CACHED("capacity")), Qundef);
    if(c != Qundef && !NIL_P(c)){
      long n = rb_num2long(c);
      if(n > RSTR(self)->cap){
        RSTR(self)->ptr = xrealloc(sptr(self), n + 1);
        RSTR(self)->cap = n + 1;
      }
    }
  }
  if(!RSTR(self)->enc) RSTR(self)->enc = enc_binary;
  return self;
}
static VALUE str_s_alloc(VALUE klass, RProc *blk){
  (void)blk;
  VALUE s = rb_str_new_enc("", 0, enc_binary);
  BASIC(s)->klass = (RClass *)klass;
  return s;
}
static VALUE str_s_new(VALUE klass, int argc, VALUE *argv, RProc *blk){
  VALUE s = str_s_alloc(klass, NULL);
  rb_call(s, ID_CACHED("initialize"), argc, argv, blk);
  return s;
}

/* ===================== init ===================== */

void rb_init_string(void){
  if(rb_cComparable) rb_include_module(rb_cString, rb_cComparable);

  rb_define_singleton_method((VALUE)rb_cString, "new", str_s_new, ARGC_ANY);
  rb_define_singleton_method((VALUE)rb_cString, "allocate", str_s_alloc, 0);
  rb_define_private_method(rb_cString, "initialize", str_init, ARGC_ANY);
  rb_define_private_method(rb_cString, "initialize_copy", str_replace, 1);

  rb_define_method(rb_cString, "+", str_plus_m, 1);
  rb_define_method(rb_cString, "*", str_times, 1);
  rb_define_method(rb_cString, "%", str_format_m, 1);
  rb_define_method(rb_cString, "==", str_eq, 1);
  rb_define_method(rb_cString, "===", str_eq, 1);
  rb_define_method(rb_cString, "eql?", str_eql, 1);
  rb_define_method(rb_cString, "equal?", str_equal_p, 1);
  rb_define_method(rb_cString, "<=>", str_cmp_m, 1);
  rb_define_method(rb_cString, "between?", str_between, 2);
  rb_define_method(rb_cString, "hash", str_hash_m, 0);
  rb_define_method(rb_cString, "=~", str_match_op, 1);

  rb_define_method(rb_cString, "[]", str_aref, ARGC_ANY);
  rb_define_method(rb_cString, "slice", str_aref, ARGC_ANY);
  rb_define_method(rb_cString, "[]=", str_aset, ARGC_ANY);
  rb_define_method(rb_cString, "slice!", str_slice_bang, ARGC_ANY);
  rb_define_method(rb_cString, "insert", str_insert, 2);

  rb_define_method(rb_cString, "length", str_length, 0);
  rb_define_method(rb_cString, "size", str_length, 0);
  rb_define_method(rb_cString, "bytesize", str_bytesize, 0);
  rb_define_method(rb_cString, "empty?", str_empty, 0);

  rb_define_method(rb_cString, "concat", str_concat, ARGC_ANY);
  rb_define_method(rb_cString, "<<", str_lshift, 1);
  rb_define_method(rb_cString, "prepend", str_prepend, ARGC_ANY);
  rb_define_method(rb_cString, "replace", str_replace, 1);
#if RUBY_TARGET >= 20
  rb_define_method(rb_cString, "clear", str_clear, 0);
#endif

  rb_define_method(rb_cString, "upcase", str_upcase, ARGC_ANY);
  rb_define_method(rb_cString, "downcase", str_downcase, ARGC_ANY);
  rb_define_method(rb_cString, "capitalize", str_capitalize, ARGC_ANY);
  rb_define_method(rb_cString, "swapcase", str_swapcase, ARGC_ANY);
  rb_define_method(rb_cString, "upcase!", str_upcase_bang, ARGC_ANY);
  rb_define_method(rb_cString, "downcase!", str_downcase_bang, ARGC_ANY);
  rb_define_method(rb_cString, "capitalize!", str_capitalize_bang, ARGC_ANY);
  rb_define_method(rb_cString, "swapcase!", str_swapcase_bang, ARGC_ANY);
  rb_define_method(rb_cString, "casecmp", str_casecmp, 1);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cString, "casecmp?", str_casecmp_p, 1);
#endif

  rb_define_method(rb_cString, "strip", str_strip, 0);
  rb_define_method(rb_cString, "lstrip", str_lstrip, 0);
  rb_define_method(rb_cString, "rstrip", str_rstrip, 0);
  rb_define_method(rb_cString, "strip!", str_strip_bang, 0);
  rb_define_method(rb_cString, "lstrip!", str_lstrip_bang, 0);
  rb_define_method(rb_cString, "rstrip!", str_rstrip_bang, 0);
  rb_define_method(rb_cString, "chomp", str_chomp, ARGC_ANY);
  rb_define_method(rb_cString, "chomp!", str_chomp_bang, ARGC_ANY);
  rb_define_method(rb_cString, "chop", str_chop, 0);
  rb_define_method(rb_cString, "chop!", str_chop_bang, 0);

  rb_define_method(rb_cString, "lines", str_lines, ARGC_ANY);
  rb_define_method(rb_cString, "each_line", str_each_line, ARGC_ANY);
  rb_define_method(rb_cString, "each_byte", str_each_byte, 0);
  rb_define_method(rb_cString, "bytes", str_bytes, 0);
#if RUBY_TARGET < 20
  rb_define_method(rb_cString, "each", str_each_line, ARGC_ANY);
#else
  rb_define_method(rb_cString, "chars", str_chars, 0);
  rb_define_method(rb_cString, "codepoints", str_codepoints, 0);
  rb_define_method(rb_cString, "each_char", str_each_char, 0);
  rb_define_method(rb_cString, "each_codepoint", str_each_codepoint, 0);
  rb_define_method(rb_cString, "getbyte", str_getbyte, 1);
  rb_define_method(rb_cString, "setbyte", str_setbyte, 2);
  rb_define_method(rb_cString, "byteslice", str_byteslice, ARGC_ANY);
  rb_define_method(rb_cString, "ord", str_ord, 0);
  rb_define_method(rb_cString, "encoding", str_encoding, 0);
  rb_define_method(rb_cString, "force_encoding", str_force_encoding, 1);
  rb_define_method(rb_cString, "encode", str_encode, ARGC_ANY);
  rb_define_method(rb_cString, "valid_encoding?", str_valid_encoding, 0);
  rb_define_method(rb_cString, "ascii_only?", str_ascii_only, 0);
  rb_define_method(rb_cString, "b", str_b, 0);
  rb_define_method(rb_cString, "chr", str_chr, 0);
#endif

  rb_define_method(rb_cString, "split", str_split, ARGC_ANY);
  rb_define_method(rb_cString, "index", str_index_m, ARGC_ANY);
  rb_define_method(rb_cString, "rindex", str_rindex_m, ARGC_ANY);
  rb_define_method(rb_cString, "include?", str_include, 1);
  rb_define_method(rb_cString, "start_with?", str_start_with, ARGC_ANY);
  rb_define_method(rb_cString, "end_with?", str_end_with, ARGC_ANY);

  rb_define_method(rb_cString, "sub", str_sub, ARGC_ANY);
  rb_define_method(rb_cString, "gsub", str_gsub, ARGC_ANY);
  rb_define_method(rb_cString, "sub!", str_sub_bang, ARGC_ANY);
  rb_define_method(rb_cString, "gsub!", str_gsub_bang, ARGC_ANY);
  rb_define_method(rb_cString, "scan", str_scan, 1);
  rb_define_method(rb_cString, "match", str_match_m, ARGC_ANY);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cString, "match?", str_match_p, ARGC_ANY);
#endif
  rb_define_method(rb_cString, "partition", str_partition, 1);
  rb_define_method(rb_cString, "rpartition", str_rpartition, 1);

  rb_define_method(rb_cString, "tr", str_tr, 2);
  rb_define_method(rb_cString, "tr_s", str_tr_s, 2);
  rb_define_method(rb_cString, "tr!", str_tr_bang, 2);
  rb_define_method(rb_cString, "tr_s!", str_tr_s_bang, 2);
  rb_define_method(rb_cString, "delete", str_delete, ARGC_ANY);
  rb_define_method(rb_cString, "delete!", str_delete_bang, ARGC_ANY);
  rb_define_method(rb_cString, "squeeze", str_squeeze, ARGC_ANY);
  rb_define_method(rb_cString, "squeeze!", str_squeeze_bang, ARGC_ANY);
  rb_define_method(rb_cString, "count", str_count, ARGC_ANY);

  rb_define_method(rb_cString, "center", str_center, ARGC_ANY);
  rb_define_method(rb_cString, "ljust", str_ljust, ARGC_ANY);
  rb_define_method(rb_cString, "rjust", str_rjust, ARGC_ANY);

  rb_define_method(rb_cString, "reverse", str_reverse, 0);
  rb_define_method(rb_cString, "reverse!", str_reverse_bang, 0);
  rb_define_method(rb_cString, "succ", str_succ, 0);
  rb_define_method(rb_cString, "next", str_succ, 0);
  rb_define_method(rb_cString, "succ!", str_succ_bang, 0);
  rb_define_method(rb_cString, "next!", str_succ_bang, 0);
  rb_define_method(rb_cString, "upto", str_upto, ARGC_ANY);

  rb_define_method(rb_cString, "hex", str_hex, 0);
  rb_define_method(rb_cString, "oct", str_oct, 0);
  rb_define_method(rb_cString, "to_i", str_to_i, ARGC_ANY);
  rb_define_method(rb_cString, "to_f", str_to_f, 0);
  rb_define_method(rb_cString, "to_s", str_to_s, 0);
  rb_define_method(rb_cString, "to_str", str_to_str, 0);
  rb_define_method(rb_cString, "to_sym", str_to_sym, 0);
  rb_define_method(rb_cString, "intern", str_to_sym, 0);
  rb_define_method(rb_cString, "inspect", str_inspect, 0);
  rb_define_method(rb_cString, "dup", str_dup_m, 0);
  rb_define_method(rb_cString, "clone", str_clone, ARGC_ANY);
  rb_define_method(rb_cString, "freeze", str_freeze_m, 0);
  rb_define_method(rb_cString, "frozen?", str_frozen_p, 0);

#if RUBY_TARGET >= 27
  rb_define_method(rb_cString, "delete_prefix", str_delete_prefix, 1);
  rb_define_method(rb_cString, "delete_suffix", str_delete_suffix, 1);
  rb_define_method(rb_cString, "delete_prefix!", str_delete_prefix_bang, 1);
  rb_define_method(rb_cString, "delete_suffix!", str_delete_suffix_bang, 1);
  rb_define_method(rb_cString, "-@", str_uminus, 0);
  rb_define_method(rb_cString, "+@", str_uplus, 0);
#endif
#if RUBY_TARGET >= 33
  rb_define_method(rb_cString, "dedup", str_uminus, 0);
#endif
#if RUBY_TARGET >= 33
  rb_define_method(rb_cString, "bytesplice", str_bytesplice, ARGC_ANY);
  rb_define_method(rb_cString, "byteindex", str_byteindex, ARGC_ANY);
  rb_define_method(rb_cString, "byterindex", str_byterindex, ARGC_ANY);
#endif
#if RUBY_TARGET >= 34
  rb_define_method(rb_cString, "append_as_bytes", str_append_as_bytes, ARGC_ANY);
#endif
}
