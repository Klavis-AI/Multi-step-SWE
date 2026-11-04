#include "go.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

void *xmalloc(size_t n){ void *p = malloc(n?n:1); if(!p) fatalf("out of memory"); return p; }
void *xcalloc(size_t n, size_t sz){ void *p = calloc(n?n:1, sz?sz:1); if(!p) fatalf("out of memory"); return p; }
void *xrealloc(void *p, size_t n){ void *q = realloc(p, n?n:1); if(!q) fatalf("out of memory"); return q; }
char *xstrdup(const char *s){ size_t n=strlen(s); char *r=xmalloc(n+1); memcpy(r,s,n+1); return r; }
char *xstrndup(const char *s, size_t n){ char *r=xmalloc(n+1); memcpy(r,s,n); r[n]=0; return r; }

char *xsprintf(const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *r = xmalloc((size_t)n+1);
    vsnprintf(r, (size_t)n+1, fmt, ap2);
    va_end(ap2);
    return r;
}

void fatalf(const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "go: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

void buf_init(Buf *b){ b->p=NULL; b->len=0; b->cap=0; }
static void buf_grow(Buf *b, size_t need){
    if (b->len + need + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap*2 : 64;
    while (nc < b->len + need + 1) nc *= 2;
    b->p = xrealloc(b->p, nc); b->cap = nc;
}
void buf_put(Buf *b, const void *data, size_t n){ buf_grow(b,n); memcpy(b->p+b->len, data, n); b->len+=n; b->p[b->len]=0; }
void buf_puts(Buf *b, const char *s){ buf_put(b, s, strlen(s)); }
void buf_putc(Buf *b, int c){ char ch=(char)c; buf_put(b,&ch,1); }
void buf_printf(Buf *b, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    buf_grow(b, (size_t)n);
    vsnprintf(b->p + b->len, (size_t)n+1, fmt, ap2); va_end(ap2);
    b->len += (size_t)n;
}
void buf_free(Buf *b){ free(b->p); b->p=NULL; b->len=b->cap=0; }

void vec_push(Vec *v, void *x){
    if (v->len == v->cap){ v->cap = v->cap ? v->cap*2 : 8; v->p = xrealloc(v->p, (size_t)v->cap*sizeof(void*)); }
    v->p[v->len++] = x;
}
void vec_free(Vec *v){ free(v->p); v->p=NULL; v->len=v->cap=0; }

/* ------------------------------------------------------------ intern --- */
#define INTERN_BUCKETS 8192
typedef struct IStr { struct IStr *next; size_t len; char s[1]; } IStr;
static IStr *itab[INTERN_BUCKETS];
static size_t ihash(const char *s, size_t n){ size_t h=1469598103934665603ULL; for(size_t i=0;i<n;i++){ h^=(unsigned char)s[i]; h*=1099511628211ULL; } return h; }
const char *internn(const char *s, size_t n){
    size_t h = ihash(s,n) % INTERN_BUCKETS;
    for (IStr *p=itab[h]; p; p=p->next) if (p->len==n && memcmp(p->s,s,n)==0) return p->s;
    IStr *p = xmalloc(sizeof(IStr)+n);
    p->len=n; memcpy(p->s,s,n); p->s[n]=0; p->next=itab[h]; itab[h]=p;
    return p->s;
}
const char *intern(const char *s){ return internn(s, strlen(s)); }

/* ------------------------------------------------------------- files --- */
int file_exists(const char *path){ struct stat st; return stat(path,&st)==0; }
int is_dir(const char *path){ struct stat st; return stat(path,&st)==0 && S_ISDIR(st.st_mode); }

char *read_file(const char *path, size_t *len_out){
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0){ fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0){ fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)sz+1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    if (len_out) *len_out = got;
    return buf;
}

int write_file(const char *path, const void *data, size_t len){
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}

int mkdir_p(const char *path){
    char *tmp = xstrdup(path);
    size_t n = strlen(tmp);
    for (size_t i = 1; i <= n; i++){
        if (tmp[i]=='/' || tmp[i]==0){
            char c = tmp[i]; tmp[i]=0;
            if (!is_dir(tmp)) { if (mkdir(tmp, 0777)!=0 && errno!=EEXIST){ free(tmp); return -1; } }
            tmp[i]=c;
        }
    }
    free(tmp); return 0;
}

char *path_join(const char *a, const char *b){
    if (!a || !*a) return xstrdup(b);
    if (!b || !*b) return xstrdup(a);
    if (b[0]=='/') return xstrdup(b);
    size_t la = strlen(a);
    if (a[la-1]=='/') return xsprintf("%s%s", a, b);
    return xsprintf("%s/%s", a, b);
}
char *path_dir(const char *p){
    const char *slash = strrchr(p, '/');
    if (!slash) return xstrdup(".");
    if (slash == p) return xstrdup("/");
    return xstrndup(p, (size_t)(slash - p));
}
char *path_base(const char *p){
    const char *slash = strrchr(p, '/');
    return xstrdup(slash ? slash+1 : p);
}

static int cmp_str(const void *a, const void *b){
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
Vec list_dir(const char *path){
    Vec v; memset(&v,0,sizeof v);
    DIR *d = opendir(path);
    if (!d) return v;
    struct dirent *e;
    while ((e = readdir(d))){
        if (strcmp(e->d_name,".")==0 || strcmp(e->d_name,"..")==0) continue;
        vec_push(&v, xstrdup(e->d_name));
    }
    closedir(d);
    if (v.len) qsort(v.p, (size_t)v.len, sizeof(void*), cmp_str);
    return v;
}

/* ------------------------------------------------------------ errors --- */
Vec g_errors;
int g_lang = 0;

int lang_at_least(int minor){ return g_lang == 0 || g_lang >= minor; }

static int err_cmp(const void *a, const void *b){
    const CompileError *x = *(CompileError *const *)a, *y = *(CompileError *const *)b;
    if (x->pos.file != y->pos.file){ int c = strcmp(x->pos.file?x->pos.file:"", y->pos.file?y->pos.file:""); if (c) return c; }
    if (x->pos.line != y->pos.line) return x->pos.line - y->pos.line;
    return x->pos.col - y->pos.col;
}

void errorf(Pos pos, const char *fmt, ...){
    if (g_errors.len >= 200) return;
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    char *msg = xmalloc((size_t)n+1);
    vsnprintf(msg, (size_t)n+1, fmt, ap2); va_end(ap2);
    /* suppress duplicate errors on the same line */
    for (int i = 0; i < g_errors.len; i++){
        CompileError *e = g_errors.p[i];
        if (e->pos.line == pos.line && e->pos.file == pos.file && strcmp(e->msg, msg)==0){ free(msg); return; }
    }
    CompileError *e = xcalloc(1,sizeof *e);
    e->pos = pos; e->msg = msg;
    vec_push(&g_errors, e);
}
void errors_reset(void){ g_errors.len = 0; }
int errors_len(void){ return g_errors.len; }
void errors_print(FILE *f){
    if (g_errors.len > 1) qsort(g_errors.p, (size_t)g_errors.len, sizeof(void*), err_cmp);
    int n = g_errors.len;
    int limit = n > 20 ? 20 : n;
    for (int i = 0; i < limit; i++){
        CompileError *e = g_errors.p[i];
        if (e->pos.file)
            fprintf(f, "%s:%d:%d: %s\n", e->pos.file, e->pos.line, e->pos.col, e->msg);
        else
            fprintf(f, "%s\n", e->msg);
    }
    if (n > limit) fprintf(f, "too many errors\n");
}

/* ------------------------------------------------------------ bigint --- */
void big_init(BigInt *b){ b->neg=0; b->len=0; b->d=NULL; }
static void big_norm(BigInt *b){ while (b->len>0 && b->d[b->len-1]==0) b->len--; if (b->len==0) b->neg=0; }
static void big_reserve(BigInt *b, int n){ b->d = xrealloc(b->d, (size_t)(n<1?1:n)*sizeof(uint32_t)); }
int big_is_zero(const BigInt *b){ return b->len==0; }
void big_copy(BigInt *r, const BigInt *a){
    if (r==a) return;
    big_reserve(r, a->len?a->len:1);
    memcpy(r->d, a->d, (size_t)a->len*sizeof(uint32_t));
    r->len = a->len; r->neg = a->neg;
}
void big_from_u64(BigInt *b, uint64_t v){
    big_reserve(b,2); b->d[0]=(uint32_t)v; b->d[1]=(uint32_t)(v>>32); b->len=2; b->neg=0; big_norm(b);
}
void big_from_i64(BigInt *b, int64_t v){
    if (v<0){ big_from_u64(b, (uint64_t)(-(v+1))+1u); b->neg=1; }
    else big_from_u64(b,(uint64_t)v);
}
int big_cmp_abs(const BigInt *a, const BigInt *b){
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    for (int i=a->len-1;i>=0;i--) if (a->d[i]!=b->d[i]) return a->d[i]<b->d[i] ? -1 : 1;
    return 0;
}
int big_cmp(const BigInt *a, const BigInt *b){
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    int c = big_cmp_abs(a,b);
    return a->neg ? -c : c;
}
static void uadd(BigInt *r, const BigInt *a, const BigInt *b){
    int n = (a->len>b->len?a->len:b->len)+1;
    uint32_t *d = xcalloc((size_t)n, sizeof(uint32_t));
    uint64_t carry=0;
    for (int i=0;i<n;i++){
        uint64_t s = carry;
        if (i<a->len) s += a->d[i];
        if (i<b->len) s += b->d[i];
        d[i] = (uint32_t)s; carry = s>>32;
    }
    free(r->d); r->d=d; r->len=n; r->neg=0; big_norm(r);
}
static void usub(BigInt *r, const BigInt *a, const BigInt *b){ /* |a| >= |b| */
    int n = a->len;
    uint32_t *d = xcalloc((size_t)(n?n:1), sizeof(uint32_t));
    int64_t borrow=0;
    for (int i=0;i<n;i++){
        int64_t s = (int64_t)a->d[i] - borrow - (i<b->len ? (int64_t)b->d[i] : 0);
        if (s<0){ s += ((int64_t)1<<32); borrow=1; } else borrow=0;
        d[i]=(uint32_t)s;
    }
    free(r->d); r->d=d; r->len=n; r->neg=0; big_norm(r);
}
void big_add(BigInt *r, const BigInt *a, const BigInt *b){
    BigInt ta, tb; big_init(&ta); big_init(&tb); big_copy(&ta,a); big_copy(&tb,b);
    if (ta.neg == tb.neg){ int s=ta.neg; uadd(r,&ta,&tb); r->neg = r->len? s:0; }
    else {
        if (big_cmp_abs(&ta,&tb) >= 0){ int s=ta.neg; usub(r,&ta,&tb); r->neg = r->len? s:0; }
        else { int s=tb.neg; usub(r,&tb,&ta); r->neg = r->len? s:0; }
    }
    free(ta.d); free(tb.d);
}
void big_neg(BigInt *r, const BigInt *a){ big_copy(r,a); if (r->len) r->neg = !r->neg; }
void big_sub(BigInt *r, const BigInt *a, const BigInt *b){
    BigInt nb; big_init(&nb); big_neg(&nb,b); big_add(r,a,&nb); free(nb.d);
}
void big_mul(BigInt *r, const BigInt *a, const BigInt *b){
    if (a->len==0||b->len==0){ free(r->d); r->d=NULL; r->len=0; r->neg=0; return; }
    int n=a->len+b->len;
    uint32_t *d = xcalloc((size_t)n,sizeof(uint32_t));
    for (int i=0;i<a->len;i++){
        uint64_t carry=0;
        for (int j=0;j<b->len;j++){
            uint64_t cur = (uint64_t)d[i+j] + (uint64_t)a->d[i]*b->d[j] + carry;
            d[i+j] = (uint32_t)cur; carry = cur>>32;
        }
        int k=i+b->len;
        while (carry){ uint64_t cur = (uint64_t)d[k]+carry; d[k]=(uint32_t)cur; carry=cur>>32; k++; }
    }
    int neg = a->neg ^ b->neg;
    free(r->d); r->d=d; r->len=n; r->neg=neg; big_norm(r);
}
static void ushr1(BigInt *b){
    uint32_t carry=0;
    for (int i=b->len-1;i>=0;i--){ uint32_t nc = b->d[i]&1; b->d[i] = (b->d[i]>>1) | (carry<<31); carry=nc; }
    big_norm(b);
}
static void ushl1(BigInt *b){
    big_reserve(b, b->len+1);
    uint32_t carry=0;
    for (int i=0;i<b->len;i++){ uint32_t nc = b->d[i]>>31; b->d[i] = (b->d[i]<<1)|carry; carry=nc; }
    if (carry){ b->d[b->len++]=carry; }
}
void big_divmod(BigInt *q, BigInt *rem, const BigInt *a, const BigInt *b){
    /* simple shift-subtract long division on magnitudes */
    BigInt A,B,Q,R; big_init(&A); big_init(&B); big_init(&Q); big_init(&R);
    big_copy(&A,a); A.neg=0; big_copy(&B,b); B.neg=0;
    if (B.len==0){ if(q){free(q->d); q->d=NULL;q->len=0;q->neg=0;} if(rem){free(rem->d);rem->d=NULL;rem->len=0;rem->neg=0;} free(A.d); free(B.d); return; }
    int bits = A.len*32;
    big_reserve(&Q, A.len?A.len:1); memset(Q.d,0,(size_t)(A.len?A.len:1)*sizeof(uint32_t)); Q.len=A.len;
    for (int i=bits-1;i>=0;i--){
        ushl1(&R);
        uint32_t bit = (i/32 < A.len) ? ((A.d[i/32] >> (i%32)) & 1u) : 0u;
        if (bit){ if (R.len==0){ big_reserve(&R,1); R.d[0]=1; R.len=1; } else R.d[0] |= 1u; }
        if (big_cmp_abs(&R,&B) >= 0){
            BigInt t; big_init(&t); usub(&t,&R,&B); free(R.d); R=t;
            Q.d[i/32] |= (1u << (i%32));
        }
    }
    big_norm(&Q); big_norm(&R);
    int qneg = a->neg ^ b->neg;
    if (q){ free(q->d); *q = Q; q->neg = Q.len? qneg : 0; } else free(Q.d);
    if (rem){ free(rem->d); *rem = R; rem->neg = R.len? a->neg : 0; } else free(R.d);
    free(A.d); free(B.d);
}
void big_shl(BigInt *r, const BigInt *a, unsigned n){
    if (a->len==0){ big_copy(r,a); return; }
    int words = (int)(n/32), bits=(int)(n%32);
    int nl = a->len + words + 1;
    uint32_t *d = xcalloc((size_t)nl, sizeof(uint32_t));
    for (int i=0;i<a->len;i++){
        uint64_t v = (uint64_t)a->d[i] << bits;
        d[i+words] |= (uint32_t)v;
        d[i+words+1] |= (uint32_t)(v>>32);
    }
    int neg=a->neg;
    free(r->d); r->d=d; r->len=nl; r->neg=neg; big_norm(r);
}
void big_shr(BigInt *r, const BigInt *a, unsigned n){
    /* arithmetic shift right (floor division by 2^n) */
    BigInt t; big_init(&t); big_copy(&t,a);
    int neg = t.neg; t.neg = 0;
    int lost = 0;
    for (unsigned i=0;i<n;i++){ if (t.len && (t.d[0]&1)) lost=1; ushr1(&t); }
    if (neg && lost){ BigInt one; big_init(&one); big_from_i64(&one,1); BigInt s; big_init(&s); uadd(&s,&t,&one); free(t.d); t=s; free(one.d); }
    t.neg = t.len ? neg : 0;
    free(r->d); *r = t;
}
/* two's complement bitwise ops via magnitude conversion */
static void to_twos(const BigInt *a, uint32_t *d, int n){
    for (int i=0;i<n;i++) d[i] = i<a->len ? a->d[i] : 0;
    if (a->neg){
        for (int i=0;i<n;i++) d[i] = ~d[i];
        uint64_t carry=1;
        for (int i=0;i<n && carry;i++){ uint64_t s=(uint64_t)d[i]+carry; d[i]=(uint32_t)s; carry=s>>32; }
    }
}
static void from_twos(BigInt *r, uint32_t *d, int n){
    int neg = (n>0) && (d[n-1] & 0x80000000u);
    if (neg){
        uint64_t borrow=1;
        for (int i=0;i<n;i++){ uint64_t s=(uint64_t)d[i]-borrow; d[i]=(uint32_t)s; borrow = (s>>63)&1; if(!borrow) break; }
        for (int i=0;i<n;i++) d[i] = ~d[i];
    }
    free(r->d); r->d = xmalloc((size_t)(n?n:1)*sizeof(uint32_t));
    memcpy(r->d, d, (size_t)n*sizeof(uint32_t)); r->len=n; r->neg=neg; big_norm(r);
}
#define BITOP(name, expr) \
void name(BigInt *r, const BigInt *a, const BigInt *b){ \
    int n = (a->len>b->len?a->len:b->len)+1; \
    uint32_t *da=xcalloc((size_t)n,4), *db=xcalloc((size_t)n,4), *dr=xcalloc((size_t)n,4); \
    to_twos(a,da,n); to_twos(b,db,n); \
    for (int i=0;i<n;i++){ uint32_t x=da[i], y=db[i]; dr[i] = (expr); } \
    from_twos(r,dr,n); free(da); free(db); free(dr); \
}
BITOP(big_and, x & y)
BITOP(big_or, x | y)
BITOP(big_xor, x ^ y)
BITOP(big_andnot, x & ~y)

int big_parse(BigInt *b, const char *s, int base){
    big_init(b);
    BigInt bb; big_init(&bb); big_from_i64(&bb, base);
    BigInt acc; big_init(&acc);
    for (const char *p=s; *p; p++){
        if (*p=='_') continue;
        int d;
        if (*p>='0'&&*p<='9') d=*p-'0';
        else if (*p>='a'&&*p<='f') d=*p-'a'+10;
        else if (*p>='A'&&*p<='F') d=*p-'A'+10;
        else { free(bb.d); free(acc.d); return -1; }
        if (d >= base){ free(bb.d); free(acc.d); return -1; }
        BigInt t; big_init(&t); big_mul(&t,&acc,&bb);
        BigInt dv; big_init(&dv); big_from_i64(&dv,d);
        BigInt s2; big_init(&s2); big_add(&s2,&t,&dv);
        free(acc.d); acc = s2; free(t.d); free(dv.d);
    }
    free(bb.d);
    *b = acc;
    return 0;
}
int64_t big_to_i64(const BigInt *b, int *ok){
    if (ok) *ok = 1;
    if (b->len > 2){ if (ok) *ok=0; return 0; }
    uint64_t v = 0;
    if (b->len>0) v |= b->d[0];
    if (b->len>1) v |= ((uint64_t)b->d[1])<<32;
    if (b->neg){
        if (v > (uint64_t)1<<63){ if(ok)*ok=0; return 0; }
        if (v == ((uint64_t)1<<63)) return INT64_MIN;
        return -(int64_t)v;
    }
    if (v > (uint64_t)INT64_MAX){ if(ok)*ok=0; return 0; }
    return (int64_t)v;
}
uint64_t big_to_u64(const BigInt *b, int *ok){
    if (ok) *ok = 1;
    if (b->neg || b->len > 2){ if (ok) *ok=0; return 0; }
    uint64_t v=0;
    if (b->len>0) v |= b->d[0];
    if (b->len>1) v |= ((uint64_t)b->d[1])<<32;
    return v;
}
double big_to_double(const BigInt *b){
    double r=0;
    for (int i=b->len-1;i>=0;i--) r = r*4294967296.0 + (double)b->d[i];
    return b->neg ? -r : r;
}
char *big_to_string(const BigInt *b){
    if (b->len==0) return xstrdup("0");
    BigInt t; big_init(&t); big_copy(&t,b); t.neg=0;
    BigInt ten; big_init(&ten); big_from_i64(&ten,1000000000);
    Buf digits; buf_init(&digits);
    Vec chunks; memset(&chunks,0,sizeof chunks);
    while (t.len){
        BigInt q,r; big_init(&q); big_init(&r);
        big_divmod(&q,&r,&t,&ten);
        int ok; uint64_t rv = big_to_u64(&r,&ok);
        long *c = xmalloc(sizeof(long)); *c = (long)rv;
        vec_push(&chunks, c);
        free(t.d); t=q; free(r.d);
    }
    free(ten.d);
    if (b->neg) buf_putc(&digits,'-');
    for (int i=chunks.len-1;i>=0;i--){
        long v = *(long*)chunks.p[i];
        if (i==chunks.len-1) buf_printf(&digits, "%ld", v);
        else buf_printf(&digits, "%09ld", v);
        free(chunks.p[i]);
    }
    vec_free(&chunks);
    return digits.p;
}
