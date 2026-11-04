/* numeric.c -- Integer, Float, Numeric and a minimal arbitrary-precision Bignum. */
#include "internal.h"
#include <math.h>
#include <float.h>

extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);

static ID id_coerce, id_cmp, id_eq, id_to_i, id_divmod, id_step,
  id_times, id_upto, id_downto;

/* ===================== bignum representation ===================== */

typedef struct RBignum { RBasic basic; int sign; int n; uint32_t *d; } RBignum;

#define RBIG(v)     ((RBignum*)(v))
#define BIGNUM_P(v) (!FIXNUM_P(v) && BASIC(v)->type == T_BIGNUM)
#define INT_P(v)    (FIXNUM_P(v) || BIGNUM_P(v))
#define FLO_P(v)    (!FIXNUM_P(v) && BASIC(v)->type == T_FLOAT)

static const char digit_chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static RBignum *big_alloc(int n){
  RBignum *b = xcalloc(1, sizeof(RBignum));
  b->basic.type = T_BIGNUM;
#if RUBY_TARGET < 27
  b->basic.klass = rb_cBignum ? rb_cBignum : rb_cInteger;
#else
  b->basic.klass = rb_cInteger;
#endif
  b->basic.frozen = 1;
  b->sign = 1; b->n = n;
  b->d = n > 0 ? xcalloc((size_t)n, sizeof(uint32_t)) : NULL;
  return b;
}
static void big_trim(RBignum *b){
  while(b->n > 0 && b->d[b->n-1] == 0) b->n--;
  if(b->n == 0) b->sign = 1;
}
static void big_grow(RBignum *b, int n){
  if(n <= b->n) return;
  b->d = xrealloc(b->d, (size_t)n * sizeof(uint32_t));
  memset(b->d + b->n, 0, (size_t)(n - b->n) * sizeof(uint32_t));
  b->n = n;
}
static int big_fits_long(RBignum *b, long *out){
  if(b->n == 0){ *out = 0; return 1; }
  if(b->n > 2) return 0;
  unsigned long long m = b->d[0];
  if(b->n == 2) m |= (unsigned long long)b->d[1] << 32;
  if(b->sign > 0){
    if(m > (unsigned long long)LONG_MAX) return 0;
    *out = (long)m; return 1;
  }
  if(m > (unsigned long long)LONG_MAX + 1ULL) return 0;
  *out = (m == (unsigned long long)LONG_MAX + 1ULL) ? LONG_MIN : -(long)m;
  return 1;
}
static VALUE big_norm(RBignum *b){
  long v;
  big_trim(b);
  if(big_fits_long(b, &v) && v >= FIXNUM_MIN && v <= FIXNUM_MAX) return LONG2FIX(v);
  return (VALUE)b;
}

VALUE rb_big_from_ll(long long v){
  unsigned long long m = (v < 0) ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
  RBignum *b = big_alloc(2);
  b->d[0] = (uint32_t)(m & 0xFFFFFFFFULL);
  b->d[1] = (uint32_t)(m >> 32);
  b->sign = v < 0 ? -1 : 1;
  big_trim(b);
  return (VALUE)b;
}
long rb_big2long(VALUE v){
  long out;
  if(!big_fits_long(RBIG(v), &out))
    rb_raise(rb_eRangeError, "bignum too big to convert into 'long'");
  return out;
}
double rb_big2dbl(VALUE v){
  RBignum *b = RBIG(v);
  double d = 0.0;
  for(int i = b->n - 1; i >= 0; i--) d = d * 4294967296.0 + (double)b->d[i];
  return b->sign < 0 ? -d : d;
}

static void big_muladd_small(RBignum *b, uint32_t m, uint32_t a){
  uint64_t carry = a;
  for(int i = 0; i < b->n; i++){
    uint64_t t = (uint64_t)b->d[i] * m + carry;
    b->d[i] = (uint32_t)t; carry = t >> 32;
  }
  if(carry){ int k = b->n; big_grow(b, b->n + 1); b->d[k] = (uint32_t)carry; }
}

VALUE rb_big_new_str(const char *s, int base){
  int sign = 1;
  while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  if(*s == '+') s++;
  else if(*s == '-'){ sign = -1; s++; }
  if(base <= 1) base = 10;
  if(base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  else if(base == 8 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) s += 2;
  else if(base == 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) s += 2;
  RBignum *b = big_alloc(1);
  for(; *s; s++){
    int d;
    if(*s == '_') continue;
    if(*s >= '0' && *s <= '9') d = *s - '0';
    else if(*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
    else break;
    if(d >= base) break;
    big_muladd_small(b, (uint32_t)base, (uint32_t)d);
  }
  b->sign = sign;
  return big_norm(b);
}

/* ---- magnitude primitives ---- */
static int mag_cmp(const uint32_t *a, int an, const uint32_t *b, int bn){
  if(an != bn) return an < bn ? -1 : 1;
  for(int i = an - 1; i >= 0; i--) if(a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}
static RBignum *mag_add(const uint32_t *a, int an, const uint32_t *b, int bn){
  if(an < bn){ const uint32_t *t = a; a = b; b = t; int n = an; an = bn; bn = n; }
  RBignum *r = big_alloc(an + 1);
  uint64_t c = 0;
  for(int i = 0; i < an; i++){
    uint64_t t = (uint64_t)a[i] + c + (i < bn ? b[i] : 0);
    r->d[i] = (uint32_t)t; c = t >> 32;
  }
  r->d[an] = (uint32_t)c;
  return r;
}
/* requires |a| >= |b| */
static RBignum *mag_sub(const uint32_t *a, int an, const uint32_t *b, int bn){
  RBignum *r = big_alloc(an > 0 ? an : 1);
  int64_t borrow = 0;
  for(int i = 0; i < an; i++){
    int64_t t = (int64_t)a[i] - borrow - (int64_t)(i < bn ? b[i] : 0);
    if(t < 0){ t += 4294967296LL; borrow = 1; } else borrow = 0;
    r->d[i] = (uint32_t)t;
  }
  return r;
}
static RBignum *mag_mul(const uint32_t *a, int an, const uint32_t *b, int bn){
  RBignum *r = big_alloc(an + bn);
  for(int i = 0; i < an; i++){
    uint32_t ai = a[i];
    if(!ai) continue;
    uint64_t c = 0;
    for(int j = 0; j < bn; j++){
      uint64_t t = (uint64_t)ai * b[j] + r->d[i+j] + c;
      r->d[i+j] = (uint32_t)t; c = t >> 32;
    }
    int k = i + bn;
    while(c){ uint64_t t = (uint64_t)r->d[k] + c; r->d[k] = (uint32_t)t; c = t >> 32; k++; }
  }
  return r;
}

/* Knuth algorithm D; q gets m-n+1 limbs, r gets n limbs. */
static void divmnu(uint32_t *q, uint32_t *r, const uint32_t *u, const uint32_t *v, int m, int n){
  const uint64_t b = 4294967296ULL;
  uint32_t *un, *vn;
  uint64_t qhat, rhat, p;
  int64_t t, k;
  int s, i, j;

  if(n == 1){
    uint64_t kk = 0;
    for(j = m - 1; j >= 0; j--){
      uint64_t cur = kk * b + u[j];
      q[j] = (uint32_t)(cur / v[0]);
      kk = cur - (uint64_t)q[j] * v[0];
    }
    if(r) r[0] = (uint32_t)kk;
    return;
  }

  s = __builtin_clz(v[n-1]);
  vn = xmalloc(sizeof(uint32_t) * (size_t)n);
  for(i = n - 1; i > 0; i--) vn[i] = (v[i] << s) | (uint32_t)((uint64_t)v[i-1] >> (32 - s));
  vn[0] = v[0] << s;
  un = xmalloc(sizeof(uint32_t) * (size_t)(m + 1));
  un[m] = (uint32_t)((uint64_t)u[m-1] >> (32 - s));
  for(i = m - 1; i > 0; i--) un[i] = (u[i] << s) | (uint32_t)((uint64_t)u[i-1] >> (32 - s));
  un[0] = u[0] << s;

  for(j = m - n; j >= 0; j--){
    uint64_t num = (uint64_t)un[j+n] * b + un[j+n-1];
    qhat = num / vn[n-1];
    rhat = num - qhat * vn[n-1];
    while(qhat >= b || qhat * vn[n-2] > b * rhat + un[j+n-2]){
      qhat--; rhat += vn[n-1];
      if(rhat >= b) break;
    }
    k = 0;
    for(i = 0; i < n; i++){
      p = qhat * vn[i];
      t = (int64_t)un[i+j] - k - (int64_t)(p & 0xFFFFFFFFULL);
      un[i+j] = (uint32_t)t;
      k = (int64_t)(p >> 32) - (t >> 32);
    }
    t = (int64_t)un[j+n] - k;
    un[j+n] = (uint32_t)t;
    q[j] = (uint32_t)qhat;
    if(t < 0){
      q[j]--;
      k = 0;
      for(i = 0; i < n; i++){
        uint64_t tt = (uint64_t)un[i+j] + vn[i] + (uint64_t)k;
        un[i+j] = (uint32_t)tt; k = (int64_t)(tt >> 32);
      }
      un[j+n] = (uint32_t)((uint64_t)un[j+n] + (uint64_t)k);
    }
  }
  if(r){
    for(i = 0; i < n - 1; i++)
      r[i] = (uint32_t)((un[i] >> s) | ((uint64_t)un[i+1] << (32 - s)));
    r[n-1] = un[n-1] >> s;
  }
  free(un); free(vn);
}

/* truncated magnitude division */
static void big_divrem(RBignum *x, RBignum *y, RBignum **qp, RBignum **rp){
  int m = x->n, n = y->n;
  if(n == 0) rb_raise(rb_eZeroDiv, "divided by 0");
  if(m < n){
    if(qp) *qp = big_alloc(0);
    if(rp){
      RBignum *r = big_alloc(m > 0 ? m : 0);
      if(m > 0) memcpy(r->d, x->d, (size_t)m * sizeof(uint32_t));
      big_trim(r); *rp = r;
    }
    return;
  }
  RBignum *q = big_alloc(m - n + 1);
  RBignum *r = big_alloc(n);
  divmnu(q->d, r->d, x->d, y->d, m, n);
  big_trim(q); big_trim(r);
  if(qp) *qp = q;
  if(rp) *rp = r;
}

static RBignum *as_big(VALUE v){
  if(FIXNUM_P(v)) return RBIG(rb_big_from_ll((long long)FIX2LONG(v)));
  return RBIG(v);
}
static VALUE big_addsub(RBignum *x, RBignum *y, int ysign){
  int xs = x->sign, ys = ysign * y->sign;
  RBignum *r;
  if(y->n == 0){ r = mag_add(x->d, x->n, NULL, 0); r->sign = xs; return big_norm(r); }
  if(x->n == 0){ r = mag_add(y->d, y->n, NULL, 0); r->sign = ys; return big_norm(r); }
  if(xs == ys){ r = mag_add(x->d, x->n, y->d, y->n); r->sign = xs; }
  else {
    int c = mag_cmp(x->d, x->n, y->d, y->n);
    if(c == 0) return LONG2FIX(0);
    if(c > 0){ r = mag_sub(x->d, x->n, y->d, y->n); r->sign = xs; }
    else { r = mag_sub(y->d, y->n, x->d, x->n); r->sign = ys; }
  }
  return big_norm(r);
}

/* ===================== generic integer arithmetic ===================== */

static VALUE int_add(VALUE x, VALUE y){
  if(FIXNUM_P(x) && FIXNUM_P(y)){
    long r;
    if(!__builtin_add_overflow(FIX2LONG(x), FIX2LONG(y), &r) && r >= FIXNUM_MIN && r <= FIXNUM_MAX)
      return LONG2FIX(r);
  }
  return big_addsub(as_big(x), as_big(y), 1);
}
static VALUE int_sub(VALUE x, VALUE y){
  if(FIXNUM_P(x) && FIXNUM_P(y)){
    long r;
    if(!__builtin_sub_overflow(FIX2LONG(x), FIX2LONG(y), &r) && r >= FIXNUM_MIN && r <= FIXNUM_MAX)
      return LONG2FIX(r);
  }
  return big_addsub(as_big(x), as_big(y), -1);
}
static VALUE int_mul(VALUE x, VALUE y){
  if(FIXNUM_P(x) && FIXNUM_P(y)){
    long r;
    if(!__builtin_mul_overflow(FIX2LONG(x), FIX2LONG(y), &r) && r >= FIXNUM_MIN && r <= FIXNUM_MAX)
      return LONG2FIX(r);
  }
  RBignum *a = as_big(x), *b = as_big(y);
  if(a->n == 0 || b->n == 0) return LONG2FIX(0);
  RBignum *r = mag_mul(a->d, a->n, b->d, b->n);
  r->sign = a->sign * b->sign;
  return big_norm(r);
}
static int int_cmp(VALUE x, VALUE y){
  if(FIXNUM_P(x) && FIXNUM_P(y)){
    long a = FIX2LONG(x), b = FIX2LONG(y);
    return a < b ? -1 : a > b ? 1 : 0;
  }
  RBignum *a = as_big(x), *b = as_big(y);
  int as = a->n ? a->sign : 0, bs = b->n ? b->sign : 0;
  if(as != bs) return as < bs ? -1 : 1;
  int c = mag_cmp(a->d, a->n, b->d, b->n);
  return as < 0 ? -c : c;
}
static int int_zero(VALUE x){ return FIXNUM_P(x) ? FIX2LONG(x) == 0 : RBIG(x)->n == 0; }
static int int_negative(VALUE x){ return FIXNUM_P(x) ? FIX2LONG(x) < 0 : (RBIG(x)->n != 0 && RBIG(x)->sign < 0); }
static VALUE int_neg_v(VALUE x){ return int_sub(LONG2FIX(0), x); }
static VALUE int_abs_v(VALUE x){ return int_negative(x) ? int_neg_v(x) : x; }
static double int2dbl(VALUE v){ return FIXNUM_P(v) ? (double)FIX2LONG(v) : rb_big2dbl(v); }

static void ldivmod(long x, long y, long *qq, long *rr){
  long q = x / y, r = x % y;
  if(r != 0 && ((r < 0) != (y < 0))){ q--; r += y; }
  *qq = q; *rr = r;
}
/* floor division: x = q*y + r, r takes the sign of y */
static void int_divmod(VALUE x, VALUE y, VALUE *qo, VALUE *ro){
  if(FIXNUM_P(x) && FIXNUM_P(y)){
    long yl = FIX2LONG(y);
    if(yl == 0) rb_raise(rb_eZeroDiv, "divided by 0");
    long q, r;
    ldivmod(FIX2LONG(x), yl, &q, &r);
    if(qo) *qo = rb_int_new(q);
    if(ro) *ro = LONG2FIX(r);
    return;
  }
  RBignum *a = as_big(x), *b = as_big(y);
  if(b->n == 0) rb_raise(rb_eZeroDiv, "divided by 0");
  RBignum *q = NULL, *r = NULL;
  big_divrem(a, b, &q, &r);
  if(a->n == 0 || a->sign == b->sign){
    q->sign = 1;
    r->sign = b->sign;
  } else if(r->n != 0){
    static const uint32_t one[1] = { 1 };
    RBignum *q1 = mag_add(q->d, q->n, one, 1);
    q1->sign = -1; q = q1;
    RBignum *r1 = mag_sub(b->d, b->n, r->d, r->n);
    r1->sign = b->sign; r = r1;
  } else {
    q->sign = -1;
  }
  if(qo) *qo = big_norm(q);
  if(ro) *ro = big_norm(r);
}
static VALUE int_pow_nonneg(VALUE x, long e){
  VALUE r = LONG2FIX(1), b = x;
  while(e > 0){
    if(e & 1) r = int_mul(r, b);
    e >>= 1;
    if(e) b = int_mul(b, b);
  }
  return r;
}
VALUE rb_int_pow(long x, long y){
  if(y < 0) return rb_float_new(pow((double)x, (double)y));
  return int_pow_nonneg(rb_int_new(x), y);
}

/* ===================== conversions and coercion ===================== */

static VALUE dbl2ival(double d){
  if(isnan(d)) rb_raise(rb_eFloatDomainError, "NaN");
  if(isinf(d)) rb_raise(rb_eFloatDomainError, "%s", d < 0 ? "-Infinity" : "Infinity");
  if(d >= (double)FIXNUM_MIN && d <= (double)FIXNUM_MAX) return LONG2FIX((long)d);
  int neg = d < 0, e;
  double a = fabs(d);
  frexp(a, &e);
  if(e <= 0) return LONG2FIX(0);
  int nl = (e + 31) / 32;
  RBignum *b = big_alloc(nl);
  double t = a;
  for(int i = nl - 1; i >= 0; i--){
    double scale = ldexp(1.0, 32 * i);
    double q = floor(t / scale);
    b->d[i] = (uint32_t)q;
    t -= q * scale;
  }
  b->sign = neg ? -1 : 1;
  return big_norm(b);
}

static void coerce_failed(VALUE x, VALUE y) __attribute__((noreturn));
static void coerce_failed(VALUE x, VALUE y){
  VALUE s;
  if(NIL_P(y) || y == Qtrue || y == Qfalse || (!FIXNUM_P(y) && BASIC(y)->type == T_SYMBOL))
    s = rb_inspect(y);
  else
    s = rb_str_new_cstr(rb_obj_classname(y));
  rb_raise(rb_eTypeError, "%s can't be coerced into %s", rb_str_cstr(s), rb_obj_classname(x));
}
VALUE rb_num_coerce_bin(VALUE x, VALUE y, ID op){
  if(!rb_respond_to(y, id_coerce)) coerce_failed(x, y);
  VALUE r = rb_funcall(y, id_coerce, 1, x);
  if(FIXNUM_P(r) || BASIC(r)->type != T_ARRAY || RARR(r)->len != 2)
    rb_raise(rb_eTypeError, "coerce must return [x, y]");
  return rb_funcall(RARR(r)->ptr[0], op, 1, RARR(r)->ptr[1]);
}
static void cmp_failed(VALUE x, VALUE y) __attribute__((noreturn));
static void cmp_failed(VALUE x, VALUE y){
  VALUE s;
  if(FIXNUM_P(y) || FLO_P(y) || NIL_P(y) || y == Qtrue || y == Qfalse)
    s = rb_inspect(y);
  else
    s = rb_str_new_cstr(rb_obj_classname(y));
  rb_raise(rb_eArgError, "comparison of %s with %s failed", rb_obj_classname(x), rb_str_cstr(s));
}
/* <=> against something non-numeric yields nil rather than an exception */
static VALUE num_coerce_cmp(VALUE x, VALUE y){
  if(!rb_respond_to(y, id_coerce)) return Qnil;
  VALUE r = rb_funcall(y, id_coerce, 1, x);
  if(FIXNUM_P(r) || BASIC(r)->type != T_ARRAY || RARR(r)->len != 2) return Qnil;
  return rb_funcall(RARR(r)->ptr[0], id_cmp, 1, RARR(r)->ptr[1]);
}
static int cmp_or_fail(VALUE x, VALUE y, VALUE c){
  if(NIL_P(c)) cmp_failed(x, y);
  return (int)rb_num2long(c);
}

/* ===================== Float#to_s ===================== */

/* shortest round-tripping digits plus CRuby's decimal point position */
static VALUE flo_to_s_val(double value){
  if(isnan(value)) return rb_str_new_cstr("NaN");
  if(isinf(value)) return rb_str_new_cstr(value < 0 ? "-Infinity" : "Infinity");
  int sign = signbit(value) ? 1 : 0;
  double a = sign ? -value : value;
  char digits[40];
  int digs, decpt;
  if(a == 0.0){ digits[0] = '0'; digits[1] = 0; digs = 1; decpt = 1; }
  else {
    char tmp[64];
    int p;
    for(p = 1; p <= 17; p++){
      snprintf(tmp, sizeof tmp, "%.*e", p - 1, a);
      if(strtod(tmp, NULL) == a) break;
    }
    if(p > 17) snprintf(tmp, sizeof tmp, "%.*e", 16, a);
    char *ep = strchr(tmp, 'e');
    int exp10 = ep ? atoi(ep + 1) : 0;
    digs = 0;
    for(char *q = tmp; ep && q < ep; q++) if(*q >= '0' && *q <= '9') digits[digs++] = *q;
    while(digs > 1 && digits[digs-1] == '0') digs--;
    digits[digs] = 0;
    decpt = exp10 + 1;
  }
  VALUE s = rb_str_new(sign ? "-" : "", sign ? 1 : 0);
  char buf[64];
  if(decpt > 0 && decpt <= DBL_DIG){
    if(decpt < digs){
      memcpy(buf, digits, (size_t)decpt);
      buf[decpt] = '.';
      memcpy(buf + decpt + 1, digits + decpt, (size_t)(digs - decpt));
      rb_str_cat(s, buf, digs + 1);
    } else {
      memcpy(buf, digits, (size_t)digs);
      memset(buf + digs, '0', (size_t)(decpt - digs));
      rb_str_cat(s, buf, decpt);
      rb_str_cat_cstr(s, ".0");
    }
  } else if(decpt <= 0 && decpt > -4){
    rb_str_cat_cstr(s, "0.");
    for(int i = 0; i < -decpt; i++) rb_str_cat(s, "0", 1);
    rb_str_cat(s, digits, digs);
  } else {
    buf[0] = digits[0]; buf[1] = '.';
    if(digs > 1){ memcpy(buf + 2, digits + 1, (size_t)(digs - 1)); rb_str_cat(s, buf, digs + 1); }
    else { buf[2] = '0'; rb_str_cat(s, buf, 3); }
    snprintf(buf, sizeof buf, "e%+03d", decpt - 1);
    rb_str_cat_cstr(s, buf);
  }
  return s;
}

/* ===================== Integer#to_s ===================== */

static VALUE int_to_s_base(VALUE x, int base){
  if(base < 2 || base > 36) rb_raise(rb_eArgError, "invalid radix %d", base);
  if(FIXNUM_P(x)){
    long v = FIX2LONG(x);
    char buf[80];
    int i = (int)sizeof(buf);
    int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
    if(u == 0) buf[--i] = '0';
    while(u){ buf[--i] = digit_chars[u % (unsigned long)base]; u /= (unsigned long)base; }
    if(neg) buf[--i] = '-';
    return rb_str_new(buf + i, (long)sizeof(buf) - i);
  }
  RBignum *b = RBIG(x);
  uint32_t chunk = 1;
  int cdig = 0;
  while((uint64_t)chunk * (uint32_t)base <= 0xFFFFFFFFULL){ chunk *= (uint32_t)base; cdig++; }
  int n = b->n;
  uint32_t *w = xmalloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
  if(n > 0) memcpy(w, b->d, sizeof(uint32_t) * (size_t)n);
  long cap = (long)n * 32 + 48;
  char *out = xmalloc((size_t)cap + 2);
  long pos = cap;
  while(n > 0){
    uint64_t rem = 0;
    for(int i = n - 1; i >= 0; i--){
      uint64_t cur = (rem << 32) | w[i];
      w[i] = (uint32_t)(cur / chunk);
      rem = cur % chunk;
    }
    while(n > 0 && w[n-1] == 0) n--;
    for(int k = 0; k < cdig; k++){
      out[--pos] = digit_chars[rem % (unsigned)base];
      rem /= (unsigned)base;
      if(n == 0 && rem == 0) break;
    }
  }
  if(pos == cap) out[--pos] = '0';
  if(b->sign < 0) out[--pos] = '-';
  VALUE s = rb_str_new(out + pos, cap - pos);
  free(out); free(w);
  return s;
}

/* ===================== rounding helpers ===================== */

static double round_half_up_s(double x, double s){
  double f, xs = x * s;
  f = round(xs);
  if(s == 1.0) return f;
  if(x > 0){ if((f + 0.5) / s <= x) f += 1; }
  else { if((f - 0.5) / s >= x) f -= 1; }
  return f;
}
static double round_apply(double x, double s, int mode){
  if(mode == 1) return rint(x * s);
  if(mode == 2){ double xs = x * s; return xs >= 0 ? ceil(xs - 0.5) : floor(xs + 0.5); }
  return round_half_up_s(x, s);
}
#if RUBY_TARGET >= 27
/* strips a trailing `half:` keyword hash; 0 = up, 1 = even, 2 = down */
static int parse_half(int *argc, VALUE *argv){
  int mode = 0;
  if(*argc > 0 && !FIXNUM_P(argv[*argc-1]) && BASIC(argv[*argc-1])->type == T_HASH){
    VALUE h = argv[*argc-1];
    VALUE hv = rb_hash_lookup(h, rb_sym(ID_CACHED("half")), Qundef);
    if(hv == Qundef && RHSH(h)->size != 0) return mode;
    (*argc)--;
    if(hv != Qundef){
      const char *nm = NULL;
      if(!FIXNUM_P(hv) && BASIC(hv)->type == T_SYMBOL) nm = rb_id2name(RSYM(hv)->id);
      else if(!FIXNUM_P(hv) && BASIC(hv)->type == T_STRING) nm = rb_str_cstr(hv);
      if(nm && !strcmp(nm, "up")) mode = 0;
      else if(nm && !strcmp(nm, "even")) mode = 1;
      else if(nm && !strcmp(nm, "down")) mode = 2;
      else rb_raise(rb_eArgError, "invalid rounding mode: %s", nm ? nm : "");
    }
  }
  return mode;
}
#endif

/* ===================== Integer ===================== */

static VALUE int_to_f_v(VALUE x){ return rb_float_new(int2dbl(x)); }

static VALUE int_plus(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_add(x, y);
  if(FLO_P(y)) return rb_float_new(int2dbl(x) + RFLT(y)->f);
  return rb_num_coerce_bin(x, y, ID_CACHED("+")); }
static VALUE int_minus(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_sub(x, y);
  if(FLO_P(y)) return rb_float_new(int2dbl(x) - RFLT(y)->f);
  return rb_num_coerce_bin(x, y, ID_CACHED("-")); }
static VALUE int_star(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_mul(x, y);
  if(FLO_P(y)) return rb_float_new(int2dbl(x) * RFLT(y)->f);
  return rb_num_coerce_bin(x, y, ID_CACHED("*")); }
static VALUE int_slash(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)){ VALUE q; int_divmod(x, y, &q, NULL); return q; }
  if(FLO_P(y)) return rb_float_new(int2dbl(x) / RFLT(y)->f);
  return rb_num_coerce_bin(x, y, ID_CACHED("/")); }

static double flo_mod_d(double x, double y){
  double z = fmod(x, y);
  if(z != 0.0 && ((y < 0 && z > 0) || (y > 0 && z < 0))) z += y;
  return z;
}
static VALUE int_percent(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)){ VALUE r; int_divmod(x, y, NULL, &r); return r; }
  if(FLO_P(y)) return rb_float_new(flo_mod_d(int2dbl(x), RFLT(y)->f));
  return rb_num_coerce_bin(x, y, ID_CACHED("%")); }
static VALUE int_divmod_m(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)){ VALUE q, r; int_divmod(x, y, &q, &r);
    VALUE a = rb_ary_new_capa(2); rb_ary_push(a, q); rb_ary_push(a, r); return a; }
  if(FLO_P(y)){ double d = RFLT(y)->f;
    if(d == 0.0) rb_raise(rb_eZeroDiv, "divided by 0");
    double xx = int2dbl(x);
    VALUE a = rb_ary_new_capa(2);
    rb_ary_push(a, dbl2ival(floor(xx / d)));
    rb_ary_push(a, rb_float_new(flo_mod_d(xx, d))); return a; }
  return rb_num_coerce_bin(x, y, id_divmod); }
static VALUE int_fdiv(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return rb_float_new(int2dbl(x) / int2dbl(y));
  if(FLO_P(y)) return rb_float_new(int2dbl(x) / RFLT(y)->f);
  return rb_num_coerce_bin(x, y, ID_CACHED("fdiv")); }

static VALUE int_pow_op(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)){
    if(int_negative(y)) return rb_float_new(pow(int2dbl(x), int2dbl(y)));
    return int_pow_nonneg(x, rb_num2long(y));
  }
  if(FLO_P(y)) return rb_float_new(pow(int2dbl(x), RFLT(y)->f));
  return rb_num_coerce_bin(x, y, ID_CACHED("**")); }

static VALUE int_pow_meth(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
#if RUBY_TARGET >= 27
  if(argc < 1 || argc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  if(argc == 2){
    if(!INT_P(argv[0]) || !INT_P(argv[1]))
      rb_raise(rb_eTypeError, "Integer#pow() 2nd argument not allowed unless all arguments are integers");
    if(int_negative(argv[0]))
      rb_raise(rb_eRangeError, "Integer#pow() 1st argument cannot be negative when 2nd argument specified");
    VALUE m = argv[1];
    if(int_zero(m)) rb_raise(rb_eZeroDiv, "divided by 0");
    long e = rb_num2long(argv[0]);
    VALUE base, r = LONG2FIX(1);
    int_divmod(self, m, NULL, &base);
    while(e > 0){
      if(e & 1) int_divmod(int_mul(r, base), m, NULL, &r);
      e >>= 1;
      if(e) int_divmod(int_mul(base, base), m, NULL, &base);
    }
    return r;
  }
#else
  if(argc != 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1)", argc);
#endif
  return int_pow_op(self, argv[0], NULL); }

static VALUE int_uminus(VALUE x, RProc *b){ (void)b; return int_neg_v(x); }
static VALUE int_uplus(VALUE x, RProc *b){ (void)b; return x; }
static VALUE int_abs(VALUE x, RProc *b){ (void)b; return int_abs_v(x); }

static VALUE int_cmp_m(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return LONG2FIX(int_cmp(x, y));
  if(FLO_P(y)){ double a = int2dbl(x), c = RFLT(y)->f;
    if(isnan(c)) return Qnil;
    return LONG2FIX(a < c ? -1 : a > c ? 1 : 0); }
  return num_coerce_cmp(x, y); }
static VALUE int_eq(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_cmp(x, y) == 0 ? Qtrue : Qfalse;
  if(FLO_P(y)) return int2dbl(x) == RFLT(y)->f ? Qtrue : Qfalse;
  return RTEST(rb_funcall(y, id_eq, 1, x)) ? Qtrue : Qfalse; }
static VALUE int_eql(VALUE x, VALUE y, RProc *b){ (void)b;
  return (INT_P(y) && int_cmp(x, y) == 0) ? Qtrue : Qfalse; }
static VALUE int_equal(VALUE x, VALUE y, RProc *b){ (void)b; return x == y ? Qtrue : Qfalse; }
static VALUE int_lt(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_cmp(x, y) < 0 ? Qtrue : Qfalse;
  if(FLO_P(y)) return int2dbl(x) < RFLT(y)->f ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) < 0 ? Qtrue : Qfalse; }
static VALUE int_gt(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_cmp(x, y) > 0 ? Qtrue : Qfalse;
  if(FLO_P(y)) return int2dbl(x) > RFLT(y)->f ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) > 0 ? Qtrue : Qfalse; }
static VALUE int_le(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_cmp(x, y) <= 0 ? Qtrue : Qfalse;
  if(FLO_P(y)) return int2dbl(x) <= RFLT(y)->f ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) <= 0 ? Qtrue : Qfalse; }
static VALUE int_ge(VALUE x, VALUE y, RProc *b){ (void)b;
  if(INT_P(y)) return int_cmp(x, y) >= 0 ? Qtrue : Qfalse;
  if(FLO_P(y)) return int2dbl(x) >= RFLT(y)->f ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) >= 0 ? Qtrue : Qfalse; }

static int int_bit_nonneg(VALUE v, long n){
  if(n < 0) return 0;
  if(FIXNUM_P(v)){ long x = FIX2LONG(v); if(n >= (long)(sizeof(long) * 8)) return 0; return (int)((x >> n) & 1); }
  RBignum *b = RBIG(v);
  long limb = n / 32;
  if(limb >= b->n) return 0;
  return (int)((b->d[limb] >> (n % 32)) & 1);
}
static VALUE int_aref(VALUE self, VALUE idx, RProc *b){ (void)b;
  VALUE i = rb_to_int(idx);
  if(int_negative(i)) return LONG2FIX(0);
  if(!FIXNUM_P(i)) return int_negative(self) ? LONG2FIX(1) : LONG2FIX(0);
  long n = FIX2LONG(i);
  if(!int_negative(self)) return LONG2FIX(int_bit_nonneg(self, n));
  return LONG2FIX(1 - int_bit_nonneg(int_sub(int_abs_v(self), LONG2FIX(1)), n)); }

static long int_as_long(VALUE v){ return FIXNUM_P(v) ? FIX2LONG(v) : rb_big2long(v); }
static VALUE int_bitarg(VALUE y){
  if(INT_P(y)) return y;
  VALUE t = rb_check_to_int(y);
  if(NIL_P(t)) rb_raise(rb_eTypeError, "no implicit conversion of %s into Integer",
                        NIL_P(y) ? "nil" : rb_obj_classname(y));
  return t;
}
static VALUE int_and(VALUE x, VALUE y, RProc *b){ (void)b;
  y = int_bitarg(y); return rb_int_new(int_as_long(x) & int_as_long(y)); }
static VALUE int_or(VALUE x, VALUE y, RProc *b){ (void)b;
  y = int_bitarg(y); return rb_int_new(int_as_long(x) | int_as_long(y)); }
static VALUE int_xor(VALUE x, VALUE y, RProc *b){ (void)b;
  y = int_bitarg(y); return rb_int_new(int_as_long(x) ^ int_as_long(y)); }
static VALUE int_not(VALUE x, RProc *b){ (void)b; return int_sub(LONG2FIX(-1), x); }

static VALUE int_shift_by(VALUE x, long n){
  if(n == 0) return x;
  if(n > 4000000 || n < -4000000) rb_raise(rb_eRangeError, "shift width too big");
  if(n > 0) return int_mul(x, int_pow_nonneg(LONG2FIX(2), n));
  VALUE q; int_divmod(x, int_pow_nonneg(LONG2FIX(2), -n), &q, NULL); return q;
}
static VALUE int_lshift(VALUE x, VALUE y, RProc *b){ (void)b;
  return int_shift_by(x, rb_num2long(rb_to_int(y))); }
static VALUE int_rshift(VALUE x, VALUE y, RProc *b){ (void)b;
  long n = rb_num2long(rb_to_int(y));
  return int_shift_by(x, n == LONG_MIN ? LONG_MAX : -n); }

static VALUE int_to_s(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  int base = argc ? (int)rb_num2long(rb_to_int(argv[0])) : 10;
  return int_to_s_base(self, base); }
static VALUE int_inspect(VALUE self, RProc *b){ (void)b; return int_to_s_base(self, 10); }
static VALUE int_to_i(VALUE self, RProc *b){ (void)b; return self; }
static VALUE int_to_f(VALUE self, RProc *b){ (void)b; return int_to_f_v(self); }
static VALUE int_dup(VALUE self, RProc *b){ (void)b; return self; }
static VALUE int_clone(VALUE self, int argc, VALUE *argv, RProc *b){
  (void)argc; (void)argv; (void)b; return self; }

static VALUE int_coerce(VALUE x, VALUE y, RProc *b){ (void)b;
  VALUE a = rb_ary_new_capa(2);
  if(INT_P(y)){ rb_ary_push(a, y); rb_ary_push(a, x); return a; }
  rb_ary_push(a, rb_float_new(rb_num2dbl(y)));
  rb_ary_push(a, rb_float_new(int2dbl(x)));
  return a; }

static VALUE int_hash(VALUE self, RProc *b){ (void)b;
  unsigned long h;
  if(FIXNUM_P(self)) h = (unsigned long)FIX2LONG(self) * 2654435761UL;
  else { RBignum *g = RBIG(self); h = 7;
    for(int i = 0; i < g->n; i++) h = h * 31 + g->d[i];
    h = h * 2 + (g->sign < 0 ? 1 : 0); }
  return LONG2FIX((long)(h & (unsigned long)FIXNUM_MAX)); }

static VALUE int_zero_p(VALUE self, RProc *b){ (void)b; return int_zero(self) ? Qtrue : Qfalse; }
static VALUE int_positive_p(VALUE self, RProc *b){ (void)b;
  return (!int_zero(self) && !int_negative(self)) ? Qtrue : Qfalse; }
static VALUE int_negative_p(VALUE self, RProc *b){ (void)b; return int_negative(self) ? Qtrue : Qfalse; }
static VALUE int_nonzero_p(VALUE self, RProc *b){ (void)b; return int_zero(self) ? Qnil : self; }
static VALUE int_even_p(VALUE self, RProc *b){ (void)b;
  int odd = FIXNUM_P(self) ? (int)(FIX2LONG(self) & 1) : (RBIG(self)->n ? (int)(RBIG(self)->d[0] & 1) : 0);
  return odd ? Qfalse : Qtrue; }
static VALUE int_odd_p(VALUE self, RProc *b){ return int_even_p(self, b) == Qtrue ? Qfalse : Qtrue; }
static VALUE int_integer_p(VALUE self, RProc *b){ (void)self; (void)b; return Qtrue; }
static VALUE int_succ(VALUE self, RProc *b){ (void)b; return int_add(self, LONG2FIX(1)); }
static VALUE int_pred(VALUE self, RProc *b){ (void)b; return int_sub(self, LONG2FIX(1)); }
static VALUE int_ord(VALUE self, RProc *b){ (void)b; return self; }
static VALUE int_size(VALUE self, RProc *b){ (void)b;
  if(FIXNUM_P(self)) return LONG2FIX((long)sizeof(long));
  long n = RBIG(self)->n;
  return LONG2FIX(n ? n * 4 : (long)sizeof(long)); }
static VALUE int_chr(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  long c = rb_num2long(self);
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(c < 0 || c > 255) rb_raise(rb_eRangeError, "%ld out of char range", c);
  char ch = (char)c;
  return rb_str_new_enc(&ch, 1, c < 128 ? enc_usascii : enc_binary); }

/* ---- rounding with a digit count ---- */
/* kind: 0 floor, 1 ceil, 2 round, 3 truncate; nd < 0 */
static VALUE int_round_to(VALUE self, long nd, int mode, int kind){
  VALUE f = int_pow_nonneg(LONG2FIX(10), -nd);
  VALUE q, r;
  int_divmod(self, f, &q, &r);
  VALUE base = int_mul(q, f);
  if(kind == 0) return base;
  if(kind == 1) return int_zero(r) ? base : int_add(base, f);
  if(kind == 3) return (int_negative(self) && !int_zero(r)) ? int_add(base, f) : base;
  int c = int_cmp(int_mul(r, LONG2FIX(2)), f);
  if(c > 0) return int_add(base, f);
  if(c < 0) return base;
  if(mode == 1) return (int_even_p(q, NULL) == Qtrue) ? base : int_add(base, f);
  if(mode == 2) return int_negative(self) ? int_add(base, f) : base;
  return int_negative(self) ? base : int_add(base, f);
}
static VALUE int_floor_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 0) return self;
  long nd = rb_num2long(rb_to_int(argv[0]));
  return nd >= 0 ? self : int_round_to(self, nd, 0, 0); }
static VALUE int_ceil_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 0) return self;
  long nd = rb_num2long(rb_to_int(argv[0]));
  return nd >= 0 ? self : int_round_to(self, nd, 0, 1); }
static VALUE int_truncate_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 0) return self;
  long nd = rb_num2long(rb_to_int(argv[0]));
  return nd >= 0 ? self : int_round_to(self, nd, 0, 3); }
static VALUE int_round_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  int mode = 0;
#if RUBY_TARGET >= 27
  mode = parse_half(&argc, argv);
#endif
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 0) return self;
  long nd = rb_num2long(rb_to_int(argv[0]));
  return nd >= 0 ? self : int_round_to(self, nd, mode, 2); }

static VALUE int_gcd_v(VALUE x, VALUE y){
  x = int_abs_v(x); y = int_abs_v(y);
  while(!int_zero(y)){ VALUE r; int_divmod(x, y, NULL, &r); x = y; y = r; }
  return x;
}
static VALUE int_gcd(VALUE self, VALUE y, RProc *b){ (void)b;
  if(!INT_P(y)) rb_raise(rb_eTypeError, "not an integer");
  return int_gcd_v(self, y); }
static VALUE int_lcm(VALUE self, VALUE y, RProc *b){ (void)b;
  if(!INT_P(y)) rb_raise(rb_eTypeError, "not an integer");
  if(int_zero(self) || int_zero(y)) return LONG2FIX(0);
  VALUE g = int_gcd_v(self, y), q;
  int_divmod(int_abs_v(int_mul(self, y)), g, &q, NULL);
  return q; }

#if RUBY_TARGET >= 27
static RClass *math_domain_error(void){
  int f;
  VALUE m = rb_const_get(rb_cObject, ID_CACHED("Math"), &f);
  if(f && !FIXNUM_P(m) && (BASIC(m)->type == T_MODULE || BASIC(m)->type == T_CLASS)){
    VALUE e = rb_const_get((RClass*)m, ID_CACHED("DomainError"), &f);
    if(f && !FIXNUM_P(e) && BASIC(e)->type == T_CLASS) return (RClass*)e;
  }
  return rb_eRangeError;
}
static VALUE int_digits(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  long base = 10;
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  if(argc == 1) base = rb_num2long(rb_to_int(argv[0]));
  if(base < 0) rb_raise(rb_eArgError, "negative radix");
  if(base < 2) rb_raise(rb_eArgError, "invalid radix %ld", base);
  if(int_negative(self)) rb_raise(math_domain_error(), "out of domain");
  VALUE a = rb_ary_new();
  if(int_zero(self)){ rb_ary_push(a, LONG2FIX(0)); return a; }
  VALUE x = self, bv = rb_int_new(base);
  while(!int_zero(x)){ VALUE q, r; int_divmod(x, bv, &q, &r); rb_ary_push(a, r); x = q; }
  return a; }

static long mag_bit_length(VALUE v){
  if(FIXNUM_P(v)){
    unsigned long x = (unsigned long)FIX2LONG(v);
    if(x == 0) return 0;
    return (long)(sizeof(unsigned long) * 8) - __builtin_clzl(x);
  }
  RBignum *b = RBIG(v);
  if(b->n == 0) return 0;
  return (long)(b->n - 1) * 32 + (32 - __builtin_clz(b->d[b->n-1]));
}
static VALUE int_bit_length(VALUE self, RProc *b){ (void)b;
  if(int_negative(self)) return LONG2FIX(mag_bit_length(int_sub(int_abs_v(self), LONG2FIX(1))));
  return LONG2FIX(mag_bit_length(self)); }
#endif
#if RUBY_TARGET >= 33
static VALUE int_ceildiv(VALUE self, VALUE y, RProc *b){ (void)b;
  VALUE q;
  if(INT_P(y)){ int_divmod(int_neg_v(self), y, &q, NULL); return int_neg_v(q); }
  return rb_funcall(rb_funcall(int_neg_v(self), id_divmod, 1, y), ID_CACHED("first"), 0); }
#endif

/* ---- iteration ---- */
static void float_step(double beg, double end, double unit, RProc *blk){
  if(isnan(beg) || isnan(end) || isnan(unit)) return;
  if(isinf(unit)){
    if(unit > 0 ? beg <= end : beg >= end) rb_yield1(blk, rb_float_new(beg));
    return;
  }
  if(unit == 0.0){ for(;;) rb_yield1(blk, rb_float_new(beg)); }
  double n = (end - beg) / unit;
  if(n < 0) return;
  double err = (fabs(beg) + fabs(end) + fabs(end - beg)) / fabs(unit) * DBL_EPSILON;
  if(err > 0.5) err = 0.5;
  if(isinf(n)){ for(long i = 0;; i++) rb_yield1(blk, rb_float_new(beg + (double)i * unit)); }
  n = floor(n + err);
  for(long i = 0; i <= (long)n; i++) rb_yield1(blk, rb_float_new(beg + (double)i * unit));
}
static VALUE num_step(VALUE self, int argc, VALUE *argv, RProc *blk){
  VALUE limit = Qnil, step = LONG2FIX(1);
  int nargc = argc;
  if(nargc > 0 && !FIXNUM_P(argv[nargc-1]) && BASIC(argv[nargc-1])->type == T_HASH){
    VALUE h = argv[nargc-1];
    VALUE by = rb_hash_lookup(h, rb_sym(ID_CACHED("by")), Qundef);
    VALUE to = rb_hash_lookup(h, rb_sym(ID_CACHED("to")), Qundef);
    if(by != Qundef || to != Qundef){
      if(by != Qundef) step = by;
      if(to != Qundef) limit = to;
      nargc--;
    }
  }
  if(nargc > 2) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  if(nargc >= 1) limit = argv[0];
  if(nargc >= 2) step = argv[1];
  if(!blk) return rb_enum_new(self, id_step, argc, argv);
  if(INT_P(step) && int_zero(step)) rb_raise(rb_eArgError, "step can't be 0");
  if(FLO_P(step) && RFLT(step)->f == 0.0) rb_raise(rb_eArgError, "step can't be 0");
  if(INT_P(self) && INT_P(step) && (NIL_P(limit) || INT_P(limit))){
    int down = int_negative(step);
    for(VALUE i = self;; i = int_add(i, step)){
      if(!NIL_P(limit)){ int c = int_cmp(i, limit); if(down ? c < 0 : c > 0) break; }
      rb_yield1(blk, i);
    }
    return self;
  }
  double u = rb_num2dbl(step);
  double bg = rb_num2dbl(self);
  double en = NIL_P(limit) ? (u > 0 ? HUGE_VAL : -HUGE_VAL) : rb_num2dbl(limit);
  float_step(bg, en, u, blk);
  return self;
}
static VALUE int_times(VALUE self, RProc *blk){
  if(!blk) return rb_enum_new(self, id_times, 0, NULL);
  if(FIXNUM_P(self)){
    long n = FIX2LONG(self);
    for(long i = 0; i < n; i++) rb_yield1(blk, LONG2FIX(i));
    return self;
  }
  for(VALUE i = LONG2FIX(0); int_cmp(i, self) < 0; i = int_add(i, LONG2FIX(1)))
    rb_yield1(blk, i);
  return self;
}
static VALUE int_upto(VALUE self, VALUE lim, RProc *blk){
  if(!blk) return rb_enum_new(self, id_upto, 1, &lim);
  if(INT_P(lim)){
    for(VALUE i = self; int_cmp(i, lim) <= 0; i = int_add(i, LONG2FIX(1))) rb_yield1(blk, i);
    return self;
  }
  double l = rb_num2dbl(lim);
  for(VALUE i = self; int2dbl(i) <= l; i = int_add(i, LONG2FIX(1))) rb_yield1(blk, i);
  return self;
}
static VALUE int_downto(VALUE self, VALUE lim, RProc *blk){
  if(!blk) return rb_enum_new(self, id_downto, 1, &lim);
  if(INT_P(lim)){
    for(VALUE i = self; int_cmp(i, lim) >= 0; i = int_sub(i, LONG2FIX(1))) rb_yield1(blk, i);
    return self;
  }
  double l = rb_num2dbl(lim);
  for(VALUE i = self; int2dbl(i) >= l; i = int_sub(i, LONG2FIX(1))) rb_yield1(blk, i);
  return self;
}

/* ---- Comparable-ish helpers shared by Integer, Float and Numeric ---- */
static VALUE num_between(VALUE self, VALUE lo, VALUE hi, RProc *b){ (void)b;
  if(cmp_or_fail(self, lo, rb_funcall(self, id_cmp, 1, lo)) < 0) return Qfalse;
  if(cmp_or_fail(self, hi, rb_funcall(self, id_cmp, 1, hi)) > 0) return Qfalse;
  return Qtrue; }
#if RUBY_TARGET >= 27
static VALUE num_clamp(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  VALUE lo = Qnil, hi = Qnil;
  if(argc == 1){
    VALUE r = argv[0];
    if(FIXNUM_P(r) || BASIC(r)->type != T_RANGE)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Range)", rb_obj_classname(r));
    lo = RRNG(r)->beg; hi = RRNG(r)->end;
    if(RRNG(r)->excl && !NIL_P(hi)) rb_raise(rb_eArgError, "cannot clamp with an exclusive range");
  } else if(argc == 2){ lo = argv[0]; hi = argv[1]; }
  else rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  if(!NIL_P(lo) && !NIL_P(hi)){
    VALUE c = rb_funcall(lo, id_cmp, 1, hi);
    if(NIL_P(c) || rb_num2long(c) > 0)
      rb_raise(rb_eArgError, "min argument must be less than or equal to max argument");
  }
  if(!NIL_P(lo) && cmp_or_fail(self, lo, rb_funcall(self, id_cmp, 1, lo)) < 0) return lo;
  if(!NIL_P(hi) && cmp_or_fail(self, hi, rb_funcall(self, id_cmp, 1, hi)) > 0) return hi;
  return self; }
#endif

#if RUBY_TARGET < 27
static VALUE int_class_m(VALUE self, RProc *b){ (void)b;
  return (VALUE)(FIXNUM_P(self) ? rb_cFixnum : rb_cBignum); }
#endif

/* ===================== Float ===================== */

static double flo_val(VALUE v){ return RFLT(v)->f; }
static int flo_rhs(VALUE y, double *out){
  if(FLO_P(y)){ *out = RFLT(y)->f; return 1; }
  if(INT_P(y)){ *out = int2dbl(y); return 1; }
  return 0;
}
static VALUE flo_plus(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return rb_float_new(flo_val(x) + d);
  return rb_num_coerce_bin(x, y, ID_CACHED("+")); }
static VALUE flo_minus(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return rb_float_new(flo_val(x) - d);
  return rb_num_coerce_bin(x, y, ID_CACHED("-")); }
static VALUE flo_star(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return rb_float_new(flo_val(x) * d);
  return rb_num_coerce_bin(x, y, ID_CACHED("*")); }
static VALUE flo_slash(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return rb_float_new(flo_val(x) / d);
  return rb_num_coerce_bin(x, y, ID_CACHED("/")); }
static VALUE flo_percent(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return rb_float_new(flo_mod_d(flo_val(x), d));
  return rb_num_coerce_bin(x, y, ID_CACHED("%")); }
static VALUE flo_pow(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return rb_float_new(pow(flo_val(x), d));
  return rb_num_coerce_bin(x, y, ID_CACHED("**")); }
static VALUE flo_divmod(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(!flo_rhs(y, &d)) return rb_num_coerce_bin(x, y, id_divmod);
  if(d == 0.0) rb_raise(rb_eZeroDiv, "divided by 0");
  double v = flo_val(x);
  VALUE a = rb_ary_new_capa(2);
  rb_ary_push(a, dbl2ival(floor(v / d)));
  rb_ary_push(a, rb_float_new(flo_mod_d(v, d)));
  return a; }
static VALUE flo_uminus(VALUE x, RProc *b){ (void)b; return rb_float_new(-flo_val(x)); }
static VALUE flo_uplus(VALUE x, RProc *b){ (void)b; return x; }
static VALUE flo_abs(VALUE x, RProc *b){ (void)b; return rb_float_new(fabs(flo_val(x))); }

static VALUE flo_cmp(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  double v = flo_val(x);
  if(flo_rhs(y, &d)){
    if(isnan(v) || isnan(d)) return Qnil;
    return LONG2FIX(v < d ? -1 : v > d ? 1 : 0);
  }
  return num_coerce_cmp(x, y); }
static VALUE flo_eq(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return (!isnan(flo_val(x)) && flo_val(x) == d) ? Qtrue : Qfalse;
  return RTEST(rb_funcall(y, id_eq, 1, x)) ? Qtrue : Qfalse; }
static VALUE flo_eql(VALUE x, VALUE y, RProc *b){ (void)b;
  return (FLO_P(y) && flo_val(x) == flo_val(y)) ? Qtrue : Qfalse; }
static VALUE flo_lt(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return (!isnan(flo_val(x)) && !isnan(d) && flo_val(x) < d) ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) < 0 ? Qtrue : Qfalse; }
static VALUE flo_gt(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return (!isnan(flo_val(x)) && !isnan(d) && flo_val(x) > d) ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) > 0 ? Qtrue : Qfalse; }
static VALUE flo_le(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return (!isnan(flo_val(x)) && !isnan(d) && flo_val(x) <= d) ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) <= 0 ? Qtrue : Qfalse; }
static VALUE flo_ge(VALUE x, VALUE y, RProc *b){ (void)b; double d;
  if(flo_rhs(y, &d)) return (!isnan(flo_val(x)) && !isnan(d) && flo_val(x) >= d) ? Qtrue : Qfalse;
  return cmp_or_fail(x, y, num_coerce_cmp(x, y)) >= 0 ? Qtrue : Qfalse; }

static VALUE flo_to_s(VALUE x, RProc *b){ (void)b; return flo_to_s_val(flo_val(x)); }
static VALUE flo_to_f(VALUE x, RProc *b){ (void)b; return x; }
static VALUE flo_to_i(VALUE x, RProc *b){ (void)b; return dbl2ival(trunc(flo_val(x))); }
static VALUE flo_coerce(VALUE x, VALUE y, RProc *b){ (void)b;
  VALUE a = rb_ary_new_capa(2);
  rb_ary_push(a, rb_float_new(rb_num2dbl(y)));
  rb_ary_push(a, x);
  return a; }
static VALUE flo_nan_p(VALUE x, RProc *b){ (void)b; return isnan(flo_val(x)) ? Qtrue : Qfalse; }
static VALUE flo_infinite_p(VALUE x, RProc *b){ (void)b;
  double d = flo_val(x);
  if(!isinf(d)) return Qnil;
  return LONG2FIX(d < 0 ? -1 : 1); }
static VALUE flo_finite_p(VALUE x, RProc *b){ (void)b; return isfinite(flo_val(x)) ? Qtrue : Qfalse; }
static VALUE flo_zero_p(VALUE x, RProc *b){ (void)b; return flo_val(x) == 0.0 ? Qtrue : Qfalse; }
static VALUE flo_hash(VALUE x, RProc *b){ (void)b;
  double d = flo_val(x);
  unsigned long h;
  if(d == (double)(long)d) h = (unsigned long)(long)d * 2654435761UL;
  else { unsigned long u = 0; memcpy(&u, &d, sizeof u < sizeof d ? sizeof u : sizeof d); h = u; }
  return LONG2FIX((long)(h & (unsigned long)FIXNUM_MAX)); }

static long flo_ndigits(int argc, VALUE *argv){
  if(argc > 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0..1)", argc);
  return argc ? rb_num2long(rb_to_int(argv[0])) : 0;
}
/* dir: -1 floor, +1 ceil */
static VALUE flo_floor_ceil(VALUE self, long nd, int dir){
  double d = flo_val(self);
  if(nd == 0) return dbl2ival(dir < 0 ? floor(d) : ceil(d));
  if(nd > 0){
    if(!isfinite(d) || nd > 17) return self;
    double f = pow(10.0, (double)nd), xs = d * f;
    if(!isfinite(xs)) return self;
    double mul = dir < 0 ? floor(xs) : ceil(xs);
    double res = dir < 0 ? (mul + 1) / f : (mul - 1) / f;
    if(dir < 0 ? (res > d) : (res < d)) res = mul / f;
    return rb_float_new(res);
  }
  if(!isfinite(d)) return dbl2ival(d);
  if(-nd > 308) return LONG2FIX(0);
  double f = pow(10.0, (double)(-nd));
  double t = (dir < 0 ? floor(d / f) : ceil(d / f)) * f;
  if(!isfinite(t)) return LONG2FIX(0);
  return dbl2ival(t);
}
static VALUE flo_floor_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  return flo_floor_ceil(self, flo_ndigits(argc, argv), -1); }
static VALUE flo_ceil_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  return flo_floor_ceil(self, flo_ndigits(argc, argv), 1); }
static VALUE flo_truncate_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  long nd = flo_ndigits(argc, argv);
  return flo_floor_ceil(self, nd, flo_val(self) < 0 ? 1 : -1); }
static VALUE flo_round_m(VALUE self, int argc, VALUE *argv, RProc *b){ (void)b;
  int mode = 0;
#if RUBY_TARGET >= 27
  mode = parse_half(&argc, argv);
#endif
  long nd = flo_ndigits(argc, argv);
  double d = flo_val(self);
  if(d == 0.0) return nd > 0 ? self : LONG2FIX(0);
  if(nd == 0) return dbl2ival(round_apply(d, 1.0, mode));
  if(nd > 0){
    if(!isfinite(d) || nd > 17) return self;
    double s = pow(10.0, (double)nd);
    if(!isfinite(d * s)) return self;
    return rb_float_new(round_apply(d, s, mode) / s);
  }
  if(!isfinite(d)) return dbl2ival(d);
  if(-nd > 308) return LONG2FIX(0);
  double s = pow(10.0, (double)(-nd));
  double t = round_apply(d / s, 1.0, mode) * s;
  if(!isfinite(t)) return LONG2FIX(0);
  return dbl2ival(t); }

/* ===================== Numeric ===================== */

static VALUE num_zero_p(VALUE self, RProc *b){ (void)b;
  return RTEST(rb_funcall(self, id_eq, 1, LONG2FIX(0))) ? Qtrue : Qfalse; }
static VALUE num_positive_p(VALUE self, RProc *b){ (void)b;
  return RTEST(rb_funcall(self, ID_CACHED(">"), 1, LONG2FIX(0))) ? Qtrue : Qfalse; }
static VALUE num_negative_p(VALUE self, RProc *b){ (void)b;
  return RTEST(rb_funcall(self, ID_CACHED("<"), 1, LONG2FIX(0))) ? Qtrue : Qfalse; }
static VALUE num_nonzero_p(VALUE self, RProc *b){ (void)b;
  return RTEST(rb_funcall(self, ID_CACHED("zero?"), 0)) ? Qnil : self; }
static VALUE num_integer_p(VALUE self, RProc *b){ (void)self; (void)b; return Qfalse; }
static VALUE num_abs(VALUE self, RProc *b){ (void)b;
  if(RTEST(rb_funcall(self, ID_CACHED("<"), 1, LONG2FIX(0)))) return rb_funcall(self, ID_CACHED("-@"), 0);
  return self; }
static VALUE num_uplus(VALUE self, RProc *b){ (void)b; return self; }
static VALUE num_uminus(VALUE self, RProc *b){ (void)b;
  return rb_num_coerce_bin(LONG2FIX(0), self, ID_CACHED("-")); }
static VALUE num_to_int(VALUE self, RProc *b){ (void)b; return rb_funcall(self, id_to_i, 0); }
static VALUE num_coerce(VALUE self, VALUE y, RProc *b){ (void)b;
  VALUE a = rb_ary_new_capa(2);
  if(rb_class_of(self) == rb_class_of(y)){ rb_ary_push(a, y); rb_ary_push(a, self); return a; }
  rb_ary_push(a, rb_float_new(rb_num2dbl(y)));
  rb_ary_push(a, rb_float_new(rb_num2dbl(self)));
  return a; }
static VALUE num_divmod(VALUE self, VALUE y, RProc *b){ (void)b;
  VALUE q = rb_funcall(rb_funcall(self, ID_CACHED("/"), 1, y), ID_CACHED("floor"), 0);
  VALUE r = rb_funcall(self, ID_CACHED("%"), 1, y);
  VALUE a = rb_ary_new_capa(2);
  rb_ary_push(a, q); rb_ary_push(a, r);
  return a; }
static VALUE num_remainder(VALUE self, VALUE y, RProc *b){ (void)b;
  VALUE z = rb_funcall(self, ID_CACHED("%"), 1, y);
  if(!RTEST(rb_funcall(z, id_eq, 1, LONG2FIX(0)))){
    int xn = RTEST(rb_funcall(self, ID_CACHED("<"), 1, LONG2FIX(0)));
    int yn = RTEST(rb_funcall(y, ID_CACHED("<"), 1, LONG2FIX(0)));
    int yp = RTEST(rb_funcall(y, ID_CACHED(">"), 1, LONG2FIX(0)));
    int xp = RTEST(rb_funcall(self, ID_CACHED(">"), 1, LONG2FIX(0)));
    if((xn && yp) || (xp && yn)) return rb_funcall(z, ID_CACHED("-"), 1, y);
  }
  return z; }

/* ===================== registration ===================== */

void rb_init_numeric(void){
  id_coerce = ID_CACHED("coerce"); id_cmp = ID_CACHED("<=>"); id_eq = ID_CACHED("==");
  id_to_i = ID_CACHED("to_i"); id_divmod = ID_CACHED("divmod"); id_step = ID_CACHED("step");
  id_times = ID_CACHED("times"); id_upto = ID_CACHED("upto"); id_downto = ID_CACHED("downto");

  /* ---- Numeric ---- */
  rb_define_method(rb_cNumeric, "step", num_step, ARGC_ANY);
  rb_define_method(rb_cNumeric, "positive?", num_positive_p, 0);
  rb_define_method(rb_cNumeric, "negative?", num_negative_p, 0);
  rb_define_method(rb_cNumeric, "integer?", num_integer_p, 0);
  rb_define_method(rb_cNumeric, "nonzero?", num_nonzero_p, 0);
  rb_define_method(rb_cNumeric, "zero?", num_zero_p, 0);
  rb_define_method(rb_cNumeric, "abs", num_abs, 0);
  rb_define_method(rb_cNumeric, "coerce", num_coerce, 1);
  rb_define_method(rb_cNumeric, "divmod", num_divmod, 1);
  rb_define_method(rb_cNumeric, "+@", num_uplus, 0);
  rb_define_method(rb_cNumeric, "-@", num_uminus, 0);
  rb_define_method(rb_cNumeric, "to_int", num_to_int, 0);
  rb_define_method(rb_cNumeric, "remainder", num_remainder, 1);
  rb_define_method(rb_cNumeric, "between?", num_between, 2);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cNumeric, "clamp", num_clamp, ARGC_ANY);
#endif

  /* ---- Integer ---- */
  rb_define_method(rb_cInteger, "+", int_plus, 1);
  rb_define_method(rb_cInteger, "-", int_minus, 1);
  rb_define_method(rb_cInteger, "*", int_star, 1);
  rb_define_method(rb_cInteger, "/", int_slash, 1);
  rb_define_method(rb_cInteger, "%", int_percent, 1);
  rb_define_method(rb_cInteger, "**", int_pow_op, 1);
  rb_define_method(rb_cInteger, "divmod", int_divmod_m, 1);
  rb_define_method(rb_cInteger, "fdiv", int_fdiv, 1);
  rb_define_method(rb_cInteger, "pow", int_pow_meth, ARGC_ANY);
  rb_define_method(rb_cInteger, "abs", int_abs, 0);
  rb_define_method(rb_cInteger, "-@", int_uminus, 0);
  rb_define_method(rb_cInteger, "+@", int_uplus, 0);
  rb_define_method(rb_cInteger, "<=>", int_cmp_m, 1);
  rb_define_method(rb_cInteger, "==", int_eq, 1);
  rb_define_method(rb_cInteger, "===", int_eq, 1);
  rb_define_method(rb_cInteger, "eql?", int_eql, 1);
  rb_define_method(rb_cInteger, "equal?", int_equal, 1);
  rb_define_method(rb_cInteger, "<", int_lt, 1);
  rb_define_method(rb_cInteger, ">", int_gt, 1);
  rb_define_method(rb_cInteger, "<=", int_le, 1);
  rb_define_method(rb_cInteger, ">=", int_ge, 1);
  rb_define_method(rb_cInteger, "[]", int_aref, 1);
  rb_define_method(rb_cInteger, "&", int_and, 1);
  rb_define_method(rb_cInteger, "|", int_or, 1);
  rb_define_method(rb_cInteger, "^", int_xor, 1);
  rb_define_method(rb_cInteger, "~", int_not, 0);
  rb_define_method(rb_cInteger, "<<", int_lshift, 1);
  rb_define_method(rb_cInteger, ">>", int_rshift, 1);
  rb_define_method(rb_cInteger, "to_s", int_to_s, ARGC_ANY);
  rb_define_method(rb_cInteger, "inspect", int_inspect, 0);
  rb_define_method(rb_cInteger, "to_i", int_to_i, 0);
  rb_define_method(rb_cInteger, "to_int", int_to_i, 0);
  rb_define_method(rb_cInteger, "to_f", int_to_f, 0);
  rb_define_method(rb_cInteger, "dup", int_dup, 0);
  rb_define_method(rb_cInteger, "clone", int_clone, ARGC_ANY);
  rb_define_method(rb_cInteger, "coerce", int_coerce, 1);
  rb_define_method(rb_cInteger, "times", int_times, 0);
  rb_define_method(rb_cInteger, "upto", int_upto, 1);
  rb_define_method(rb_cInteger, "downto", int_downto, 1);
  rb_define_method(rb_cInteger, "step", num_step, ARGC_ANY);
  rb_define_method(rb_cInteger, "succ", int_succ, 0);
  rb_define_method(rb_cInteger, "next", int_succ, 0);
  rb_define_method(rb_cInteger, "pred", int_pred, 0);
  rb_define_method(rb_cInteger, "chr", int_chr, ARGC_ANY);
  rb_define_method(rb_cInteger, "ord", int_ord, 0);
  rb_define_method(rb_cInteger, "size", int_size, 0);
  rb_define_method(rb_cInteger, "zero?", int_zero_p, 0);
  rb_define_method(rb_cInteger, "positive?", int_positive_p, 0);
  rb_define_method(rb_cInteger, "negative?", int_negative_p, 0);
  rb_define_method(rb_cInteger, "even?", int_even_p, 0);
  rb_define_method(rb_cInteger, "odd?", int_odd_p, 0);
  rb_define_method(rb_cInteger, "integer?", int_integer_p, 0);
  rb_define_method(rb_cInteger, "nonzero?", int_nonzero_p, 0);
  rb_define_method(rb_cInteger, "floor", int_floor_m, ARGC_ANY);
  rb_define_method(rb_cInteger, "ceil", int_ceil_m, ARGC_ANY);
  rb_define_method(rb_cInteger, "round", int_round_m, ARGC_ANY);
  rb_define_method(rb_cInteger, "truncate", int_truncate_m, ARGC_ANY);
  rb_define_method(rb_cInteger, "gcd", int_gcd, 1);
  rb_define_method(rb_cInteger, "lcm", int_lcm, 1);
  rb_define_method(rb_cInteger, "hash", int_hash, 0);
  rb_define_method(rb_cInteger, "between?", num_between, 2);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cInteger, "clamp", num_clamp, ARGC_ANY);
  rb_define_method(rb_cInteger, "digits", int_digits, ARGC_ANY);
  rb_define_method(rb_cInteger, "bit_length", int_bit_length, 0);
#endif
#if RUBY_TARGET >= 33
  rb_define_method(rb_cInteger, "ceildiv", int_ceildiv, 1);
#endif
#if RUBY_TARGET < 27
  rb_cFixnum = rb_define_class("Fixnum", rb_cInteger);
  rb_cBignum = rb_define_class("Bignum", rb_cInteger);
  rb_define_method(rb_cInteger, "class", int_class_m, 0);
#elif RUBY_TARGET < 33
  /* 2.4 unified the classes but kept the names as deprecated aliases of
     Integer; 3.2 removed them. */
  rb_define_const(rb_cObject, "Fixnum", (VALUE)rb_cInteger);
  rb_define_const(rb_cObject, "Bignum", (VALUE)rb_cInteger);
#endif

  /* ---- Float ---- */
  rb_define_method(rb_cFloat, "+", flo_plus, 1);
  rb_define_method(rb_cFloat, "-", flo_minus, 1);
  rb_define_method(rb_cFloat, "*", flo_star, 1);
  rb_define_method(rb_cFloat, "/", flo_slash, 1);
  rb_define_method(rb_cFloat, "%", flo_percent, 1);
  rb_define_method(rb_cFloat, "modulo", flo_percent, 1);
  rb_define_method(rb_cFloat, "**", flo_pow, 1);
  rb_define_method(rb_cFloat, "divmod", flo_divmod, 1);
  rb_define_method(rb_cFloat, "abs", flo_abs, 0);
  rb_define_method(rb_cFloat, "-@", flo_uminus, 0);
  rb_define_method(rb_cFloat, "+@", flo_uplus, 0);
  rb_define_method(rb_cFloat, "<=>", flo_cmp, 1);
  rb_define_method(rb_cFloat, "==", flo_eq, 1);
  rb_define_method(rb_cFloat, "===", flo_eq, 1);
  rb_define_method(rb_cFloat, "eql?", flo_eql, 1);
  rb_define_method(rb_cFloat, "<", flo_lt, 1);
  rb_define_method(rb_cFloat, ">", flo_gt, 1);
  rb_define_method(rb_cFloat, "<=", flo_le, 1);
  rb_define_method(rb_cFloat, ">=", flo_ge, 1);
  rb_define_method(rb_cFloat, "to_s", flo_to_s, 0);
  rb_define_method(rb_cFloat, "inspect", flo_to_s, 0);
  rb_define_method(rb_cFloat, "to_i", flo_to_i, 0);
  rb_define_method(rb_cFloat, "to_int", flo_to_i, 0);
  rb_define_method(rb_cFloat, "to_f", flo_to_f, 0);
  rb_define_method(rb_cFloat, "truncate", flo_truncate_m, ARGC_ANY);
  rb_define_method(rb_cFloat, "floor", flo_floor_m, ARGC_ANY);
  rb_define_method(rb_cFloat, "ceil", flo_ceil_m, ARGC_ANY);
  rb_define_method(rb_cFloat, "round", flo_round_m, ARGC_ANY);
  rb_define_method(rb_cFloat, "nan?", flo_nan_p, 0);
  rb_define_method(rb_cFloat, "infinite?", flo_infinite_p, 0);
  rb_define_method(rb_cFloat, "finite?", flo_finite_p, 0);
  rb_define_method(rb_cFloat, "zero?", flo_zero_p, 0);
  rb_define_method(rb_cFloat, "hash", flo_hash, 0);
  rb_define_method(rb_cFloat, "coerce", flo_coerce, 1);
  rb_define_method(rb_cFloat, "between?", num_between, 2);
#if RUBY_TARGET >= 27
  rb_define_method(rb_cFloat, "clamp", num_clamp, ARGC_ANY);
#endif

  rb_define_const(rb_cFloat, "INFINITY", rb_float_new(HUGE_VAL));
  rb_define_const(rb_cFloat, "NAN", rb_float_new(NAN));
  rb_define_const(rb_cFloat, "EPSILON", rb_float_new(DBL_EPSILON));
  rb_define_const(rb_cFloat, "MAX", rb_float_new(DBL_MAX));
  rb_define_const(rb_cFloat, "MIN", rb_float_new(DBL_MIN));
  rb_define_const(rb_cFloat, "DIG", LONG2FIX(DBL_DIG));
  rb_define_const(rb_cFloat, "MANT_DIG", LONG2FIX(DBL_MANT_DIG));
}
