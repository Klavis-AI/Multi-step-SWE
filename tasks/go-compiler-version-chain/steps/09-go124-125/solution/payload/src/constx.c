#include "go.h"
#include <math.h>
#include <stdarg.h>

Const *const_new(ConstKind k){
    Const *c = xcalloc(1,sizeof *c);
    c->k = k;
    big_init(&c->i); big_init(&c->fnum); big_init(&c->fden);
    if (k==C_FLOAT){ big_from_i64(&c->fden,1); }
    return c;
}

Const *const_int_i64(int64_t v){ Const *c = const_new(C_INT); big_from_i64(&c->i,v); return c; }
Const *const_int_big(BigInt *b){ Const *c = const_new(C_INT); big_copy(&c->i,b); return c; }
Const *const_bool(int v){ Const *c = const_new(C_BOOL); c->bval = v!=0; return c; }
Const *const_str(const char *s, size_t n){ Const *c = const_new(C_STRING); c->s = internn(s,n); c->slen = n; return c; }

static void rat_norm(Const *c){
    if (big_is_zero(&c->fnum)){ big_from_i64(&c->fden,1); return; }
    /* reduce by gcd using binary-free Euclid */
    BigInt a,b,t; big_init(&a); big_init(&b); big_init(&t);
    big_copy(&a,&c->fnum); a.neg=0; big_copy(&b,&c->fden); b.neg=0;
    while (!big_is_zero(&b)){
        BigInt r; big_init(&r);
        big_divmod(NULL,&r,&a,&b);
        free(a.d); a=b; b=r;
    }
    if (!big_is_zero(&a) && !(a.len==1 && a.d[0]==1)){
        BigInt q; big_init(&q);
        big_divmod(&q,NULL,&c->fnum,&a); int neg=c->fnum.neg; free(c->fnum.d); c->fnum=q; c->fnum.neg = c->fnum.len?neg:0;
        BigInt q2; big_init(&q2);
        big_divmod(&q2,NULL,&c->fden,&a); free(c->fden.d); c->fden=q2; c->fden.neg=0;
    }
    free(a.d); free(b.d); free(t.d);
}

Const *const_float_rat(BigInt *num, BigInt *den){
    Const *c = const_new(C_FLOAT);
    big_copy(&c->fnum,num); big_copy(&c->fden,den);
    if (c->fden.neg){ c->fden.neg=0; c->fnum.neg = !c->fnum.neg; }
    rat_norm(c);
    return c;
}

double const_to_double(const Const *c){
    if (c->k==C_INT) return big_to_double(&c->i);
    if (c->k!=C_FLOAT) return 0;
    if (big_is_zero(&c->fden)) return 0;
    double n = big_to_double(&c->fnum), d = big_to_double(&c->fden);
    if (isfinite(n) && isfinite(d) && d != 0) return n/d;
    /* fall back: scale by shifting */
    BigInt a,b; big_init(&a); big_init(&b);
    big_copy(&a,&c->fnum); big_copy(&b,&c->fden);
    int shift = 0;
    while (a.len > 3){ big_shr(&a,&a,32); shift += 32; }
    while (b.len > 3){ big_shr(&b,&b,32); shift -= 32; }
    double r = big_to_double(&a)/big_to_double(&b);
    free(a.d); free(b.d);
    return ldexp(r, shift);
}

int const_is_zero(const Const *c){
    if (c->k==C_INT) return big_is_zero(&c->i);
    if (c->k==C_FLOAT) return big_is_zero(&c->fnum);
    return 0;
}

/* convert an int constant to rational form */
static void as_rat(const Const *c, BigInt *num, BigInt *den){
    if (c->k==C_INT){ big_copy(num,&c->i); big_from_i64(den,1); }
    else { big_copy(num,&c->fnum); big_copy(den,&c->fden); }
}

int const_is_integral(const Const *c){
    if (c->k==C_INT) return 1;
    if (c->k!=C_FLOAT) return 0;
    if (c->fden.len==1 && c->fden.d[0]==1) return 1;
    BigInt r; big_init(&r);
    big_divmod(NULL,&r,&c->fnum,&c->fden);
    int z = big_is_zero(&r);
    free(r.d);
    return z;
}

Const *const_to_int(const Const *c){
    if (c->k==C_INT) return (Const*)c;
    if (c->k!=C_FLOAT) return NULL;
    BigInt q; big_init(&q);
    big_divmod(&q,NULL,&c->fnum,&c->fden);
    Const *r = const_new(C_INT); big_copy(&r->i,&q); free(q.d);
    return r;
}

Const *const_binop(TokKind op, const Const *a, const Const *b, const char **err){
    *err = NULL;
    if (a->k==C_STRING && b->k==C_STRING){
        if (op==TOK_ADD){
            Buf s; buf_init(&s);
            buf_put(&s,a->s,a->slen); buf_put(&s,b->s,b->slen);
            Const *c = const_str(s.p?s.p:"", s.len);
            buf_free(&s); return c;
        }
        *err = "invalid operation on strings"; return NULL;
    }
    if (a->k==C_BOOL && b->k==C_BOOL){
        if (op==TOK_LAND) return const_bool(a->bval && b->bval);
        if (op==TOK_LOR) return const_bool(a->bval || b->bval);
        *err = "invalid operation on booleans"; return NULL;
    }
    if (a->k==C_INT && b->k==C_INT){
        Const *r = const_new(C_INT);
        switch (op){
        case TOK_ADD: big_add(&r->i,&a->i,&b->i); return r;
        case TOK_SUB: big_sub(&r->i,&a->i,&b->i); return r;
        case TOK_MUL: big_mul(&r->i,&a->i,&b->i); return r;
        case TOK_QUO: if (big_is_zero(&b->i)){ *err="division by zero"; return NULL; }
                      big_divmod(&r->i,NULL,&a->i,&b->i); return r;
        case TOK_REM: if (big_is_zero(&b->i)){ *err="division by zero"; return NULL; }
                      big_divmod(NULL,&r->i,&a->i,&b->i); return r;
        case TOK_AND: big_and(&r->i,&a->i,&b->i); return r;
        case TOK_OR: big_or(&r->i,&a->i,&b->i); return r;
        case TOK_XOR: big_xor(&r->i,&a->i,&b->i); return r;
        case TOK_AND_NOT: big_andnot(&r->i,&a->i,&b->i); return r;
        default: break;
        }
    }
    if ((a->k==C_INT||a->k==C_FLOAT) && (b->k==C_INT||b->k==C_FLOAT)){
        BigInt an,ad,bn,bd; big_init(&an); big_init(&ad); big_init(&bn); big_init(&bd);
        as_rat(a,&an,&ad); as_rat(b,&bn,&bd);
        BigInt num,den,t1,t2; big_init(&num); big_init(&den); big_init(&t1); big_init(&t2);
        Const *r = NULL;
        switch (op){
        case TOK_ADD: big_mul(&t1,&an,&bd); big_mul(&t2,&bn,&ad); big_add(&num,&t1,&t2); big_mul(&den,&ad,&bd); break;
        case TOK_SUB: big_mul(&t1,&an,&bd); big_mul(&t2,&bn,&ad); big_sub(&num,&t1,&t2); big_mul(&den,&ad,&bd); break;
        case TOK_MUL: big_mul(&num,&an,&bn); big_mul(&den,&ad,&bd); break;
        case TOK_QUO:
            if (big_is_zero(&bn)){ *err="division by zero"; goto done; }
            big_mul(&num,&an,&bd); big_mul(&den,&ad,&bn); break;
        default: *err = "invalid operation"; goto done;
        }
        r = const_float_rat(&num,&den);
    done:
        free(an.d); free(ad.d); free(bn.d); free(bd.d); free(num.d); free(den.d); free(t1.d); free(t2.d);
        return r;
    }
    *err = "invalid operation";
    return NULL;
}

int const_cmp(const Const *a, const Const *b){
    if (a->k==C_STRING && b->k==C_STRING){
        size_t n = a->slen<b->slen?a->slen:b->slen;
        int c = memcmp(a->s,b->s,n);
        if (c) return c<0?-1:1;
        return a->slen==b->slen?0:(a->slen<b->slen?-1:1);
    }
    if (a->k==C_BOOL&&b->k==C_BOOL) return a->bval==b->bval?0:(a->bval<b->bval?-1:1);
    if (a->k==C_INT&&b->k==C_INT) return big_cmp(&a->i,&b->i);
    BigInt an,ad,bn,bd,t1,t2; big_init(&an); big_init(&ad); big_init(&bn); big_init(&bd); big_init(&t1); big_init(&t2);
    as_rat(a,&an,&ad); as_rat(b,&bn,&bd);
    big_mul(&t1,&an,&bd); big_mul(&t2,&bn,&ad);
    int r = big_cmp(&t1,&t2);
    free(an.d); free(ad.d); free(bn.d); free(bd.d); free(t1.d); free(t2.d);
    return r;
}

Const *const_unop(TokKind op, const Const *a, Type *t, const char **err){
    *err = NULL;
    switch (op){
    case TOK_ADD: return (Const*)a;
    case TOK_SUB: {
        if (a->k==C_INT){ Const *r = const_new(C_INT); big_neg(&r->i,&a->i); return r; }
        if (a->k==C_FLOAT){ Const *r = const_new(C_FLOAT); big_neg(&r->fnum,&a->fnum); big_copy(&r->fden,&a->fden); return r; }
        *err="invalid operation"; return NULL;
    }
    case TOK_NOT:
        if (a->k==C_BOOL) return const_bool(!a->bval);
        *err="invalid operation"; return NULL;
    case TOK_XOR: {
        if (a->k!=C_INT){ *err="invalid operation"; return NULL; }
        Const *r = const_new(C_INT);
        if (t && is_unsigned(t)){
            /* ^x for unsigned of width n is (2^n - 1) - x */
            int bits = (int)(type_size(t)*8);
            BigInt one, mask, sh; big_init(&one); big_init(&mask); big_init(&sh);
            big_from_i64(&one,1); big_shl(&sh,&one,(unsigned)bits); big_sub(&mask,&sh,&one);
            big_andnot(&r->i,&mask,&a->i);
            free(one.d); free(mask.d); free(sh.d);
        } else {
            BigInt one, t2; big_init(&one); big_init(&t2);
            big_from_i64(&one,1);
            big_add(&t2,&a->i,&one);
            big_neg(&r->i,&t2);
            free(one.d); free(t2.d);
        }
        return r;
    }
    default: *err="invalid operation"; return NULL;
    }
}

Const *const_shift(TokKind op, const Const *a, uint64_t n, const char **err){
    *err = NULL;
    const Const *ai = a;
    if (a->k==C_FLOAT){
        if (!const_is_integral(a)){ *err="constant truncated to integer"; return NULL; }
        ai = const_to_int(a);
    }
    if (ai->k != C_INT){ *err="invalid operation"; return NULL; }
    if (n > 4096){ *err="shift count too large"; return NULL; }
    Const *r = const_new(C_INT);
    if (op==TOK_SHL) big_shl(&r->i,&ai->i,(unsigned)n);
    else big_shr(&r->i,&ai->i,(unsigned)n);
    return r;
}

static int int_fits(const BigInt *b, Type *t){
    Type *u = underlying(t);
    int bits; int uns = 0;
    switch (u->k){
    case T_INT8: bits=8; break; case T_INT16: bits=16; break;
    case T_INT32: bits=32; break; case T_INT64: case T_INT: bits=64; break;
    case T_UINT8: bits=8; uns=1; break; case T_UINT16: bits=16; uns=1; break;
    case T_UINT32: bits=32; uns=1; break;
    case T_UINT64: case T_UINT: case T_UINTPTR: bits=64; uns=1; break;
    default: return 1;
    }
    if (uns && b->neg) return 0;
    BigInt one, lim, sh; big_init(&one); big_init(&lim); big_init(&sh);
    big_from_i64(&one,1);
    big_shl(&sh,&one,(unsigned)(uns?bits:bits-1));
    int ok;
    if (uns){
        big_sub(&lim,&sh,&one);
        ok = big_cmp(b,&lim) <= 0;
    } else {
        big_sub(&lim,&sh,&one);
        if (big_cmp(b,&lim) > 0) ok = 0;
        else { BigInt neglim; big_init(&neglim); big_neg(&neglim,&sh); ok = big_cmp(b,&neglim) >= 0; free(neglim.d); }
    }
    free(one.d); free(lim.d); free(sh.d);
    return ok;
}

int const_representable(const Const *c, Type *t, Const **out){
    Type *u = underlying(t);
    if (out) *out = (Const*)c;
    if (u->k==T_TYPEPARAM) return 1;
    if (u->k==T_INTERFACE) return 1;
    if (is_integer(u)){
        const Const *ci = c;
        if (c->k==C_FLOAT){
            if (!const_is_integral(c)) return 0;
            ci = const_to_int(c);
        } else if (c->k!=C_INT) return 0;
        if (is_untyped(u)) { if (out) *out=(Const*)ci; return 1; }
        if (!int_fits(&ci->i,u)) return 0;
        if (out) *out = (Const*)ci;
        return 1;
    }
    if (is_float(u)){
        if (c->k!=C_INT && c->k!=C_FLOAT) return 0;
        double d = const_to_double(c);
        if (u->k==T_FLOAT32){ float f = (float)d; if (isinf(f) && !isinf(d)) return 0; }
        if (isinf(d)) return 0;
        if (c->k==C_INT){
            Const *r = const_new(C_FLOAT);
            big_copy(&r->fnum,&c->i); big_from_i64(&r->fden,1);
            if (out) *out = r;
        }
        return 1;
    }
    if (is_string(u)) return c->k==C_STRING;
    if (is_boolean(u)) return c->k==C_BOOL;
    return 0;
}

static void fmt_double(Buf *b, double d){
    if (d == (double)(long long)d && fabs(d) < 1e18) buf_printf(b,"%lld",(long long)d);
    else buf_printf(b,"%g",d);
}

char *const_string(const Const *c){
    Buf b; buf_init(&b);
    switch (c->k){
    case C_BOOL: buf_puts(&b, c->bval?"true":"false"); break;
    case C_INT: { char *s = big_to_string(&c->i); buf_puts(&b,s); free(s); break; }
    case C_FLOAT: {
        if (c->fden.len==1 && c->fden.d[0]==1){ char *s=big_to_string(&c->fnum); buf_puts(&b,s); free(s); }
        else fmt_double(&b, const_to_double(c));
        break;
    }
    case C_STRING: buf_putc(&b,'"'); buf_put(&b,c->s,c->slen); buf_putc(&b,'"'); break;
    default: buf_puts(&b,"?"); break;
    }
    return b.p ? b.p : xstrdup("");
}
