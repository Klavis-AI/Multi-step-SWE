#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <setjmp.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

typedef struct PanicRec { Value v; int recovered; } PanicRec;

G *g_current = NULL;
Vec g_allg;
int g_gomaxprocs = 0;   /* 0 means: not yet initialised */
int64_t g_fake_now = 0;
int g_in_synctest = 0;
int vm_exit_code = 0;
int vm_exiting = 0;

Type *ty_runtime_error = NULL;
Type *ty_panicnil = NULL;
int g_panic_nil_error = 0;     /* 1 when go>=1.21 semantics apply */

static int next_gid = 1;
static Vec runq;
static Vec selwait;
static int64_t g_steps_per_slice = 10000;

typedef struct JB { jmp_buf jb; struct JB *prev; int stopframes; } JB;
static JB *cur_jb = NULL;

void gc_mark_root(Value v);
void gc_mark_root_obj(Obj *o);
extern void gc_pin(Obj *o);

static uint64_t nanotime(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}
int64_t vm_nanotime(void){ return g_in_synctest ? g_fake_now : (int64_t)nanotime(); }

/* ---------------------------------------------------------- goroutines --- */

static void g_ensure_stack(G *g, int n){
    if (g->sp + n <= g->stackcap) return;
    int nc = g->stackcap ? g->stackcap*2 : 512;
    while (nc < g->sp+n) nc *= 2;
    g->stack = xrealloc(g->stack,(size_t)nc*sizeof(Value));
    memset(g->stack+g->stackcap, 0, (size_t)(nc-g->stackcap)*sizeof(Value));
    g->stackcap = nc;
}
static void push(G *g, Value v){ g_ensure_stack(g,1); g->stack[g->sp++] = v; }
static Value pop(G *g){ return g->stack[--g->sp]; }

static Frame *g_push_frame(G *g){
    if (g->nframes == g->framecap){
        int nc = g->framecap?g->framecap*2:64;
        g->frames = xrealloc(g->frames,(size_t)nc*sizeof(Frame));
        memset(g->frames+g->framecap,0,(size_t)(nc-g->framecap)*sizeof(Frame));
        g->framecap = nc;
    }
    Frame *f = &g->frames[g->nframes++];
    memset(f,0,sizeof *f);
    return f;
}

static G *g_new(void){
    G *g = xcalloc(1,sizeof *g);
    g->id = next_gid++;
    g->status = G_RUNNABLE;
    g_ensure_stack(g,128);
    vec_push(&g_allg,g);
    return g;
}

void vm_mark_roots(void){
    for (int i=0;i<g_allg.len;i++){
        G *g = g_allg.p[i];
        if (g->status == G_DEAD) continue;
        for (int j=0;j<g->sp;j++) gc_mark_root(g->stack[j]);
        for (int j=0;j<g->nframes;j++){
            Frame *f = &g->frames[j];
            if (f->env) gc_mark_root_obj(f->env);
            for (int d=0;d<f->defers.len;d++){
                DeferRec *dr = f->defers.p[d];
                gc_mark_root(dr->fn);
                for (int a=0;a<dr->nargs;a++) gc_mark_root(dr->args[a]);
            }
        }
        gc_mark_root(g->panic_value);
        for (int j=0;j<g->panic_chain.len;j++){ PanicRec *pr = g->panic_chain.p[j]; gc_mark_root(pr->v); }
        gc_mark_root(g->recv_value);
        gc_mark_root(g->startfn);
        for (int a=0;a<g->nstartargs;a++) gc_mark_root(g->startargs[a]);
    }
}

/* ------------------------------------------------------------- panics --- */

Value make_runtime_error(const char *msg){
    Value v = val_string(msg,strlen(msg));
    if (ty_runtime_error) v.t = ty_runtime_error;
    return v;
}

void vm_throw(Value v){
    G *g = g_current;
    if (!g->panicking && !g->in_recovery) g->panic_chain.len = 0;
    PanicRec *pr = xcalloc(1,sizeof *pr);
    pr->v = v;
    vec_push(&g->panic_chain, pr);
    g->panic_value = v;
    g->panicking = 1;
    if (cur_jb) longjmp(cur_jb->jb, 1);
    fprintf(stderr,"panic: unhandled\n");
    exit(2);
}

void vm_panic_str(const char *fmt, ...){
    va_list ap; va_start(ap,fmt);
    char buf[512];
    vsnprintf(buf,sizeof buf,fmt,ap);
    va_end(ap);
    vm_throw(make_runtime_error(buf));
}

/* --------------------------------------------------------------- chans --- */

static Chan *chan_of(Value v){ return v.o ? (Chan*)v.o->aux : NULL; }

Value chan_make(Type *t, int64_t cap){
    if (cap < 0) vm_panic_str("makechan: size out of range");
    Obj *o = gc_alloc_obj(OBJ_CHAN,0);
    Chan *ch = xcalloc(1,sizeof *ch);
    ch->bufcap = (int)cap;
    if (cap>0) ch->buf = xcalloc((size_t)cap,sizeof(Value));
    ch->elem = underlying(t)->elem;
    o->aux = ch;
    Value v = val_nil(); v.k=VK_CHAN; v.o=o; v.t=t;
    return v;
}

static void runq_add(G *g){
    if (g->status == G_DEAD) return;
    g->status = G_RUNNABLE;
    for (int i=0;i<runq.len;i++) if (runq.p[i]==g) return;
    vec_push(&runq,g);
}

static G *runq_next(void){
    if (!runq.len) return NULL;
    G *g = runq.p[0];
    for (int i=1;i<runq.len;i++) runq.p[i-1]=runq.p[i];
    runq.len--;
    return g;
}

static void wake_select_waiters(void){
    for (int i=0;i<selwait.len;i++){
        G *g = selwait.p[i];
        if (g->status == G_WAITING) runq_add(g);
    }
    selwait.len = 0;
}

static void q_remove(Vec *q, G *g){
    int k = 0;
    for (int i=0;i<q->len;i++) if (q->p[i]!=g) q->p[k++]=q->p[i];
    q->len = k;
}

/* returns 1 if the send completed, 0 if the caller must block */
static int chan_send(Value cv, Value v){
    Chan *ch = chan_of(cv);
    if (!ch){ g_current->status = G_WAITING; g_current->wait_reason="chan send (nil chan)"; return 0; }
    if (ch->closed) vm_panic_str("send on closed channel");
    while (ch->recvq.len){
        G *r = ch->recvq.p[0];
        q_remove(&ch->recvq,r);
        if (r->status != G_WAITING) continue;
        r->recv_value = val_copy(v); r->recv_ok = 1; r->selected = 1;
        runq_add(r);
        wake_select_waiters();
        return 1;
    }
    if (ch->buflen < ch->bufcap){
        ch->buf[(ch->bufhead+ch->buflen)%ch->bufcap] = val_copy(v);
        ch->buflen++;
        wake_select_waiters();
        return 1;
    }
    return 0;
}

/* returns 1 if the receive completed */
static int chan_recv(Value cv, Value *out, int *ok){
    Chan *ch = chan_of(cv);
    if (!ch){ return 0; }
    if (ch->buflen > 0){
        *out = ch->buf[ch->bufhead];
        ch->bufhead = (ch->bufhead+1)%ch->bufcap;
        ch->buflen--;
        *ok = 1;
        /* let a blocked sender fill the freed slot */
        while (ch->sendq.len){
            G *s = ch->sendq.p[0];
            q_remove(&ch->sendq,s);
            if (s->status != G_WAITING) continue;
            ch->buf[(ch->bufhead+ch->buflen)%ch->bufcap] = val_copy(s->recv_value);
            ch->buflen++;
            s->selected = 1;
            runq_add(s);
            break;
        }
        wake_select_waiters();
        return 1;
    }
    while (ch->sendq.len){
        G *s = ch->sendq.p[0];
        q_remove(&ch->sendq,s);
        if (s->status != G_WAITING) continue;
        *out = s->recv_value; *ok = 1;
        s->selected = 1;
        runq_add(s);
        wake_select_waiters();
        return 1;
    }
    if (ch->closed){
        *out = val_zero(ch->elem?ch->elem:ty_int);
        *ok = 0;
        return 1;
    }
    return 0;
}

static void chan_close(Value cv){
    Chan *ch = chan_of(cv);
    if (!ch) vm_panic_str("close of nil channel");
    if (ch->closed) vm_panic_str("close of closed channel");
    ch->closed = 1;
    for (int i=0;i<ch->recvq.len;i++){ G *r = ch->recvq.p[i]; r->selected = 0; runq_add(r); }
    ch->recvq.len = 0;
    for (int i=0;i<ch->sendq.len;i++){ G *s = ch->sendq.p[i]; s->selected = 0; runq_add(s); }
    ch->sendq.len = 0;
    wake_select_waiters();
}

/* --------------------------------------------------------------- envs --- */

static Obj *env_new(int nslots, Obj *parent){
    Obj *e = gc_alloc_cells(nslots+1);
    Value *c = OBJ_CELLPTR(e);
    Value pv = val_nil(); pv.k = VK_PTR; pv.o = parent;
    c[0] = pv;
    for (int i=1;i<=nslots;i++){ c[i] = val_nil(); }
    return e;
}
static Obj *env_at(Obj *env, int depth){
    while (depth-- > 0 && env){ Value *c = OBJ_CELLPTR(env); env = c[0].o; }
    return env;
}

/* ------------------------------------------------------------ strings --- */

static const char *str_ptr(Value v){ return v.o ? OBJ_BYTEPTR(v.o)+v.off : ""; }

Value str_concat(Value a, Value b){
    int64_t n = a.len + b.len;
    Value v = val_nil(); v.k=VK_STRING; v.t=ty_string;
    if (n == 0) return v;
    Obj *o = gc_alloc_bytes((size_t)n);
    if (a.len) memcpy(OBJ_BYTEPTR(o), str_ptr(a), (size_t)a.len);
    if (b.len) memcpy(OBJ_BYTEPTR(o)+a.len, str_ptr(b), (size_t)b.len);
    v.o = o; v.len = n;
    return v;
}
int str_cmp(Value a, Value b){
    int64_t n = a.len<b.len?a.len:b.len;
    int c = n? memcmp(str_ptr(a),str_ptr(b),(size_t)n) : 0;
    if (c) return c<0?-1:1;
    return a.len==b.len?0:(a.len<b.len?-1:1);
}

/* -------------------------------------------------------------- slices --- */

Value slice_append(Value s, Value *elems, int n, Type *slicet){
    Type *st = slicet ? slicet : s.t;
    int64_t oldlen = (s.k==VK_SLICE)?s.len:0;
    int64_t newlen = oldlen + n;
    Obj *base = (s.k==VK_SLICE)?s.o:NULL;
    int64_t off = (s.k==VK_SLICE)?(int64_t)s.off:0;
    int64_t cap = (s.k==VK_SLICE)?s.cap:0;
    Type *et = underlying(st)->elem;
    if (newlen > cap){
        int64_t ncap = cap ? cap : 0;
        if (ncap == 0) ncap = newlen;
        while (ncap < newlen) ncap = ncap < 256 ? ncap*2 : ncap + ncap/4;
        Obj *nb = gc_alloc_cells((int)ncap);
        Value *dst = OBJ_CELLPTR(nb);
        if (base){ Value *src = OBJ_CELLPTR(base); for (int64_t i=0;i<oldlen;i++) dst[i] = src[off+i]; }
        for (int64_t i=oldlen;i<ncap;i++) dst[i] = val_zero(et);
        base = nb; off = 0; cap = ncap;
    }
    Value *cells = OBJ_CELLPTR(base);
    for (int i=0;i<n;i++) cells[off+oldlen+i] = val_copy(elems[i]);
    Value r = val_nil();
    r.k = VK_SLICE; r.o = base; r.off = (uint32_t)off; r.len = newlen; r.cap = cap; r.t = st;
    return r;
}

/* ============================== arithmetic ============================== */

static int64_t trunc_int(int64_t v, Type *t){
    switch (underlying(t)->k){
    case T_INT8: return (int8_t)v;
    case T_INT16: return (int16_t)v;
    case T_INT32: return (int32_t)v;
    default: return v;
    }
}
static uint64_t trunc_uint(uint64_t v, Type *t){
    switch (underlying(t)->k){
    case T_UINT8: return (uint8_t)v;
    case T_UINT16: return (uint16_t)v;
    case T_UINT32: return (uint32_t)v;
    default: return v;
    }
}

Value vm_binop(TokKind op, Value a, Value b, Type *ty){
    Type *u = underlying(ty?ty:(a.t?a.t:ty_int));
    if (u->k==T_STRING || a.k==VK_STRING){
        Value r = str_concat(a,b); r.t = ty?ty:a.t; return r;
    }
    if (u->k==T_FLOAT32||u->k==T_FLOAT64||a.k==VK_FLOAT){
        double x=a.b.d, y=b.b.d, z=0;
        switch (op){
        case TOK_ADD: z=x+y; break;
        case TOK_SUB: z=x-y; break;
        case TOK_MUL: z=x*y; break;
        case TOK_QUO: z=x/y; break;
        default: vm_panic_str("invalid float operation"); break;
        }
        if (u->k==T_FLOAT32) z = (double)(float)z;
        return val_float(z, ty?ty:a.t);
    }
    if (a.k==VK_UINT || is_unsigned(u)){
        uint64_t x=a.b.u, y=b.b.u, z=0;
        switch (op){
        case TOK_ADD: z=x+y; break;
        case TOK_SUB: z=x-y; break;
        case TOK_MUL: z=x*y; break;
        case TOK_QUO: if(!y) vm_panic_str("runtime error: integer divide by zero"); z=x/y; break;
        case TOK_REM: if(!y) vm_panic_str("runtime error: integer divide by zero"); z=x%y; break;
        case TOK_AND: z=x&y; break;
        case TOK_OR: z=x|y; break;
        case TOK_XOR: z=x^y; break;
        case TOK_AND_NOT: z=x&~y; break;
        default: vm_panic_str("invalid operation"); break;
        }
        return val_uint(trunc_uint(z,ty?ty:a.t), ty?ty:a.t);
    }
    {
        int64_t x=a.b.i, y=b.b.i, z=0;
        switch (op){
        case TOK_ADD: z=(int64_t)((uint64_t)x+(uint64_t)y); break;
        case TOK_SUB: z=(int64_t)((uint64_t)x-(uint64_t)y); break;
        case TOK_MUL: z=(int64_t)((uint64_t)x*(uint64_t)y); break;
        case TOK_QUO: if(!y) vm_panic_str("runtime error: integer divide by zero");
                      z = (x==INT64_MIN && y==-1) ? x : x/y; break;
        case TOK_REM: if(!y) vm_panic_str("runtime error: integer divide by zero");
                      z = (x==INT64_MIN && y==-1) ? 0 : x%y; break;
        case TOK_AND: z=x&y; break;
        case TOK_OR: z=x|y; break;
        case TOK_XOR: z=x^y; break;
        case TOK_AND_NOT: z=x&~y; break;
        default: vm_panic_str("invalid operation"); break;
        }
        return val_int(trunc_int(z,ty?ty:a.t), ty?ty:a.t);
    }
}

int vm_compare_ord(TokKind op, Value a, Value b){
    int c;
    if (a.k==VK_STRING) c = str_cmp(a,b);
    else if (a.k==VK_FLOAT || b.k==VK_FLOAT){
        double x = a.k==VK_FLOAT?a.b.d:(a.k==VK_UINT?(double)a.b.u:(double)a.b.i);
        double y = b.k==VK_FLOAT?b.b.d:(b.k==VK_UINT?(double)b.b.u:(double)b.b.i);
        if (isnan(x)||isnan(y)) return 0;
        c = x<y?-1:(x>y?1:0);
    }
    else if (a.k==VK_UINT || b.k==VK_UINT){ uint64_t x=a.b.u,y=b.b.u; c = x<y?-1:(x>y?1:0); }
    else { int64_t x=a.b.i,y=b.b.i; c = x<y?-1:(x>y?1:0); }
    switch (op){
    case TOK_LSS: return c<0;
    case TOK_LEQ: return c<=0;
    case TOK_GTR: return c>0;
    case TOK_GEQ: return c>=0;
    default: return 0;
    }
}

Value vm_shift(TokKind op, Value a, uint64_t n, Type *ty){
    Type *u = underlying(ty?ty:a.t);
    int bits = (int)(type_size(u)*8);
    if (is_unsigned(u) || a.k==VK_UINT){
        uint64_t x = a.b.u;
        uint64_t z;
        if (op==TOK_SHL) z = n>=64 ? 0 : x<<n;
        else z = n>=64 ? 0 : x>>n;
        return val_uint(trunc_uint(z,ty?ty:a.t), ty?ty:a.t);
    }
    int64_t x = a.b.i, z;
    if (op==TOK_SHL) z = n>=64 ? 0 : (int64_t)((uint64_t)x<<n);
    else z = n>=64 ? (x<0?-1:0) : (x>>n);
    (void)bits;
    return val_int(trunc_int(z,ty?ty:a.t), ty?ty:a.t);
}

/* =============================== conversion ============================= */

void utf8_put_pub(Buf *b, unsigned r);
static void utf8_put(Buf *b, unsigned r){
    if (r > 0x10FFFF || (r>=0xD800&&r<0xE000)) r = 0xFFFD;
    if (r < 0x80) buf_putc(b,(int)r);
    else if (r < 0x800){ buf_putc(b,(int)(0xC0|(r>>6))); buf_putc(b,(int)(0x80|(r&0x3F))); }
    else if (r < 0x10000){ buf_putc(b,(int)(0xE0|(r>>12))); buf_putc(b,(int)(0x80|((r>>6)&0x3F))); buf_putc(b,(int)(0x80|(r&0x3F))); }
    else { buf_putc(b,(int)(0xF0|(r>>18))); buf_putc(b,(int)(0x80|((r>>12)&0x3F))); buf_putc(b,(int)(0x80|((r>>6)&0x3F))); buf_putc(b,(int)(0x80|(r&0x3F))); }
}

void utf8_put_pub(Buf *b, unsigned r){ utf8_put(b,r); }

int utf8_decode(const char *s, int64_t len, unsigned *r){
    if (len<=0){ *r=0xFFFD; return 0; }
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80){ *r=c; return 1; }
    int n; unsigned v;
    if ((c&0xE0)==0xC0){ n=2; v=c&0x1F; }
    else if ((c&0xF0)==0xE0){ n=3; v=c&0x0F; }
    else if ((c&0xF8)==0xF0){ n=4; v=c&0x07; }
    else { *r=0xFFFD; return 1; }
    if (len < n){ *r=0xFFFD; return 1; }
    for (int i=1;i<n;i++){
        unsigned char cc=(unsigned char)s[i];
        if ((cc&0xC0)!=0x80){ *r=0xFFFD; return 1; }
        v = (v<<6)|(cc&0x3F);
    }
    if ((n==2&&v<0x80)||(n==3&&v<0x800)||(n==4&&v<0x10000)||(v>=0xD800&&v<0xE000)||v>0x10FFFF){ *r=0xFFFD; return 1; }
    *r = v;
    return n;
}

Value vm_convert(Value v, Type *to){
    Type *u = underlying(to);
    Type *su = v.t ? underlying(v.t) : NULL;
    switch (u->k){
    case T_BOOL: { Value r = v; r.k=VK_BOOL; r.t=to; return r; }
    case T_INT: case T_INT8: case T_INT16: case T_INT32: case T_INT64: {
        int64_t x;
        if (v.k==VK_FLOAT){ double d=v.b.d; if (isnan(d)) x=INT64_MIN; else if (d >= 9.2233720368547758e18) x=INT64_MAX; else if (d <= -9.2233720368547758e18) x=INT64_MIN; else x=(int64_t)d; }
        else if (v.k==VK_UINT) x=(int64_t)v.b.u;
        else x=v.b.i;
        return val_int(trunc_int(x,to),to);
    }
    case T_UINT: case T_UINT8: case T_UINT16: case T_UINT32: case T_UINT64: case T_UINTPTR: {
        uint64_t x;
        if (v.k==VK_FLOAT){ double d=v.b.d; if (isnan(d)||d<=-1) x=0; else if (d >= 1.8446744073709552e19) x=UINT64_MAX; else x=(uint64_t)d; }
        else if (v.k==VK_UINT) x=v.b.u;
        else x=(uint64_t)v.b.i;
        return val_uint(trunc_uint(x,to),to);
    }
    case T_FLOAT32: { double d = v.k==VK_FLOAT?v.b.d:(v.k==VK_UINT?(double)v.b.u:(double)v.b.i); return val_float((double)(float)d,to); }
    case T_FLOAT64: { double d = v.k==VK_FLOAT?v.b.d:(v.k==VK_UINT?(double)v.b.u:(double)v.b.i); return val_float(d,to); }
    case T_STRING: {
        if (v.k==VK_STRING){ Value r=v; r.t=to; return r; }
        if (v.k==VK_SLICE){
            Type *et = su?underlying(su->elem):NULL;
            Buf b; buf_init(&b);
            Value *cells = v.o?OBJ_CELLPTR(v.o):NULL;
            for (int64_t i=0;i<v.len;i++){
                Value e = cells[v.off+i];
                if (et && et->k==T_INT32) utf8_put(&b,(unsigned)e.b.i);
                else buf_putc(&b,(int)(e.k==VK_UINT?e.b.u:e.b.i));
            }
            Value r = val_string(b.p?b.p:"",b.len); r.t=to; buf_free(&b);
            return r;
        }
        { Buf b; buf_init(&b);
          uint64_t cp = v.k==VK_UINT?v.b.u:(uint64_t)v.b.i;
          if (v.k==VK_INT && v.b.i < 0) cp = 0xFFFD;
          utf8_put(&b,(unsigned)cp);
          Value r = val_string(b.p,b.len); r.t=to; buf_free(&b); return r; }
    }
    case T_SLICE: {
        Type *et = underlying(u->elem);
        if (v.k==VK_STRING){
            const char *s = str_ptr(v);
            if (et->k==T_UINT8){
                Obj *o = gc_alloc_cells((int)v.len);
                Value *c = OBJ_CELLPTR(o);
                for (int64_t i=0;i<v.len;i++) c[i] = val_uint((unsigned char)s[i],u->elem);
                Value r=val_nil(); r.k=VK_SLICE; r.o=o; r.len=v.len; r.cap=v.len; r.t=to; return r;
            } else {
                Vec runes; memset(&runes,0,sizeof runes);
                int64_t i=0;
                Buf tmp; buf_init(&tmp);
                int cnt=0;
                Obj *o = gc_alloc_cells((int)v.len+1);
                Value *c = OBJ_CELLPTR(o);
                while (i<v.len){
                    unsigned r; int n = utf8_decode(s+i,v.len-i,&r);
                    c[cnt++] = val_int((int64_t)r,u->elem);
                    i += n?n:1;
                }
                buf_free(&tmp); vec_free(&runes);
                Value rv=val_nil(); rv.k=VK_SLICE; rv.o=o; rv.len=cnt; rv.cap=cnt; rv.t=to; return rv;
            }
        }
        Value r = v; r.t = to; return r;
    }
    case T_ARRAY: {
        if (v.k==VK_SLICE){
            if (v.len < u->alen) vm_panic_str("runtime error: cannot convert slice with length %lld to array or pointer to array with length %lld",(long long)v.len,(long long)u->alen);
            Obj *o = gc_alloc_cells((int)u->alen);
            Value *dst = OBJ_CELLPTR(o), *src = v.o?OBJ_CELLPTR(v.o):NULL;
            for (int64_t i=0;i<u->alen;i++) dst[i] = val_copy(src[v.off+i]);
            Value r = val_nil(); r.k=VK_ARRAY; r.o=o; r.t=to; return r;
        }
        Value r = v; r.t = to; return r;
    }
    case T_PTR: {
        if (v.k==VK_SLICE && underlying(u->elem)->k==T_ARRAY){
            int64_t need = underlying(u->elem)->alen;
            if (v.len < need) vm_panic_str("runtime error: cannot convert slice with length %lld to array or pointer to array with length %lld",(long long)v.len,(long long)need);
            Value r = val_nil(); r.k=VK_PTR; r.o=v.o; r.off=v.off; r.t=to;
            r.len = need; r.cap = -3;   /* pointer to an array laid out in these cells */
            return r;
        }
        Value r = v; r.t = to; return r;
    }
    case T_INTERFACE: {
        Value r = v;
        if (is_untyped(v.t)) r.t = default_type(v.t);
        return r;
    }
    case T_UNSAFEPTR: {
        Value r = v;
        Type *vu = v.t ? underlying(v.t) : NULL;
        if (v.k==VK_PTR && vu && vu->k==T_PTR && r.cap != -3) r.len = type_size(vu->elem);
        r.t = to;
        return r;
    }
    default: { Value r = v; r.t = to; return r; }
    }
}

/* ============================== call support ============================ */

static Obj *closure_new(FuncDef *fn, Obj *env, Value *recv){
    Obj *o = gc_alloc_obj(OBJ_FUNC, 3*sizeof(Value));
    o->n = 3;
    o->aux = fn;
    Value *c = OBJ_CELLPTR(o);
    Value ev = val_nil(); ev.k=VK_PTR; ev.o=env;
    c[0] = ev;
    c[1] = recv ? *recv : val_nil();
    c[2] = val_bool(recv!=NULL);
    return o;
}

Value make_closure(FuncDef *fn, Obj *env){
    if (!fn) vm_panic_str("runtime error: call of a function with no body");
    Value v = val_nil(); v.k=VK_FUNC; v.o=closure_new(fn,env,NULL); v.t=fn->sig;
    return v;
}
Value make_method_value(FuncDef *fn, Value recv){
    if (!fn) vm_panic_str("runtime error: call of a method with no body");
    Value v = val_nil(); v.k=VK_FUNC; v.o=closure_new(fn,NULL,&recv); v.t=fn->sig;
    return v;
}

static void exec(G *g, int stopframes);
Value adjust_receiver(Value recv, Method *m);

/* Set up a call. Returns 1 if it was a native call completed in place. */
static int do_call(G *g, Value fnv, int nargs){
    if (fnv.k != VK_FUNC || !fnv.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
    FuncDef *fn = (FuncDef*)fnv.o->aux;
    Value *c = OBJ_CELLPTR(fnv.o);
    Obj *cenv = c[0].o;
    int bound = c[2].b.i != 0;
    if (bound){
        /* insert the receiver as the first argument */
        g_ensure_stack(g,1);
        for (int i=0;i<nargs;i++) g->stack[g->sp-i] = g->stack[g->sp-i-1];
        g->stack[g->sp-nargs] = c[1];
        g->sp++;
        nargs++;
    }
    if (fn->native){
        Value rets[8];
        int base = g->sp - nargs;
        int nr = native_call(fn->native, &g->stack[base], nargs, rets);
        g->sp = base;
        for (int i=0;i<nr;i++) push(g,rets[i]);
        return 1;
    }
    Obj *env = env_new(fn->nlocals, cenv);
    int base = g->sp - nargs;
    Value *cells = OBJ_CELLPTR(env);
    int nfixed = fn->nparams;
    if (fn->variadic) nfixed--;
    for (int i=0;i<nfixed;i++) cells[1+i] = (i<nargs) ? g->stack[base+i] : val_nil();
    if (fn->variadic){
        Type *vt = NULL;
        if (fn->sig && fn->sig->fields.len) vt = ((Field*)fn->sig->fields.p[fn->sig->fields.len-1])->type;
        int extra = nargs - nfixed;
        if (extra == 1 && g->stack[base+nfixed].k==VK_SLICE && g->stack[base+nfixed].cap == -1){
            Value sv = g->stack[base+nfixed]; sv.cap = sv.len; sv.t = vt;
            cells[1+nfixed] = sv;
        } else {
            Value z = val_nil(); z.k=VK_SLICE; z.t=vt; z.len=0; z.cap=0; z.o=NULL;
            if (extra > 0) z = slice_append(z,&g->stack[base+nfixed],extra,vt);
            cells[1+nfixed] = z;
        }
    }
    g->sp = base;
    Frame *f = g_push_frame(g);
    f->fn = fn; f->pc = 0; f->env = env; f->fnenv = env; f->sp_base = g->sp;
    f->nres = fn->sig ? fn->sig->results.len : 0;
    return 0;
}

int vm_call_value(Value fn, Value *args, int nargs, Value *rets, int maxrets){
    G *g = g_current;
    int base = g->sp;
    int bf = g->nframes;
    for (int i=0;i<nargs;i++) push(g,args[i]);
    int nat = do_call(g,fn,nargs);
    if (!nat) exec(g,bf);
    int n = g->sp - base;
    if (n > maxrets) n = maxrets;
    for (int i=0;i<n;i++) rets[i] = g->stack[base+i];
    g->sp = base;
    return n;
}

/* ------------------------------------------------------------- defers --- */

static void frame_pop_run_defers(G *g);

/* =============================== execution ============================== */

typedef struct SelCase { int kind; int has_ok; int target; } SelCase;   /* 0 recv, 1 send, 2 default */
typedef struct SelDesc { int n; SelCase *c; } SelDesc;

static Value *cellptr_of(Value ptr){
    if (!ptr.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
    return &OBJ_CELLPTR(ptr.o)[ptr.off];
}

static Value index_value(Value c, int64_t i){
    switch (c.k){
    case VK_SLICE:
        if (i<0 || i>=c.len) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)c.len);
        return OBJ_CELLPTR(c.o)[c.off+i];
    case VK_ARRAY: {
        int64_t n = c.o?c.o->n:0;
        if (i<0 || i>=n) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)n);
        return OBJ_CELLPTR(c.o)[i];
    }
    case VK_STRING:
        if (i<0 || i>=c.len) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)c.len);
        return val_uint((unsigned char)str_ptr(c)[i], ty_uint8);
    case VK_PTR: {
        if (!c.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
        if (c.cap == -3){
            if (i<0 || i>=c.len) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)c.len);
            return OBJ_CELLPTR(c.o)[c.off+i];
        }
        Value arr = OBJ_CELLPTR(c.o)[c.off];
        if (arr.k==VK_ARRAY) return index_value(arr,i);
        vm_panic_str("runtime error: invalid indirect");
    }
    default:
        vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
    }
    return val_nil();
}

static Value *index_addr(Value c, int64_t i){
    switch (c.k){
    case VK_SLICE:
        if (i<0 || i>=c.len) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)c.len);
        return &OBJ_CELLPTR(c.o)[c.off+i];
    case VK_ARRAY: {
        int64_t n = c.o?c.o->n:0;
        if (i<0 || i>=n) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)n);
        return &OBJ_CELLPTR(c.o)[i];
    }
    case VK_PTR: {
        if (!c.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
        if (c.cap == -3){
            if (i<0 || i>=c.len) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)i,(long long)c.len);
            return &OBJ_CELLPTR(c.o)[c.off+i];
        }
        Value arr = OBJ_CELLPTR(c.o)[c.off];
        if (arr.k==VK_ARRAY) return index_addr(arr,i);
        vm_panic_str("runtime error: invalid indirect");
    }
    default: vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
    }
    return NULL;
}

static Value struct_of(Value v){
    if (v.k==VK_PTR){
        if (!v.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
        return OBJ_CELLPTR(v.o)[v.off];
    }
    return v;
}

int type_assert_ok(Value v, Type *want){
    if (v.k==VK_NIL && !v.t) return 0;
    if (is_iface(want)) return iface_satisfied(v.t?v.t:ty_invalid, want, NULL);
    return v.t && type_identical(v.t, want);
}

static int64_t sched_slice_steps(void){ return g_steps_per_slice; }

static void exec(G *g, int stopframes){
    JB jb; jb.prev = cur_jb; jb.stopframes = stopframes; cur_jb = &jb;
    int64_t budget = sched_slice_steps();
    if (setjmp(jb.jb)){
        /* a panic arrived: unwind frames running defers */
        goto unwind;
    }
    for (;;){
        if (g->nframes <= stopframes){ cur_jb = jb.prev; return; }
        Frame *f = &g->frames[g->nframes-1];
        FuncDef *fn = f->fn;
        int retn = -1;
        if (f->pc < 0 || f->pc >= fn->ncode){
            f->pc = fn->ncode;
            retn = 0;
            goto do_return;
        }
        Instr *in = &fn->code[f->pc++];
        switch (in->op){
        case OP_NOP: break;
        case OP_POP: g->sp -= in->a?in->a:1; break;
        case OP_DUP: { Value v = g->stack[g->sp-1]; push(g,v); break; }
        case OP_SWAP: { Value t = g->stack[g->sp-1]; g->stack[g->sp-1]=g->stack[g->sp-2]; g->stack[g->sp-2]=t; break; }
        case OP_CONST: push(g, *(Value*)fn->consts.p[in->a]); break;
        case OP_NILV: { Value v = val_nil(); v.t=(Type*)in->p; if (in->p) v = val_zero((Type*)in->p); push(g,v); break; }
        case OP_BOOLV: push(g, val_bool(in->a)); break;
        case OP_ZERO: push(g, val_zero((Type*)in->p)); break;
        case OP_LOADVAR: { Obj *e = env_at(f->env,in->a);
            if (!e || (uint32_t)(in->b+1) >= e->n) vm_panic_str("runtime error: internal: variable slot %d out of range in %s", in->b, fn->name?fn->name:"?");
            push(g, OBJ_CELLPTR(e)[in->b+1]); break; }
        case OP_STOREVAR: { Obj *e = env_at(f->env,in->a); Value v = pop(g);
            if (!e || (uint32_t)(in->b+1) >= e->n) vm_panic_str("runtime error: internal: variable slot %d out of range in %s", in->b, fn->name?fn->name:"?");
            OBJ_CELLPTR(e)[in->b+1] = v; gc_barrier(v); break; }
        case OP_ADDRVAR: { Obj *e = env_at(f->env,in->a);
            if (!e || (uint32_t)(in->b+1) >= e->n) vm_panic_str("runtime error: internal: variable slot %d out of range in %s", in->b, fn->name?fn->name:"?");
            Value p = val_nil(); p.k=VK_PTR; p.o=e; p.off=(uint32_t)(in->b+1); p.t=(Type*)in->p; push(g,p); break; }
        case OP_LOADGLOB: { Package *pk=(Package*)in->p; push(g, OBJ_CELLPTR(pk->globstore)[in->a]); break; }
        case OP_STOREGLOB: { Package *pk=(Package*)in->p; Value v = pop(g); OBJ_CELLPTR(pk->globstore)[in->a]=v; gc_barrier(v); break; }
        case OP_ADDRGLOB: { Package *pk=(Package*)in->p; Value p=val_nil(); p.k=VK_PTR; p.o=pk->globstore; p.off=(uint32_t)in->a; push(g,p); break; }
        case OP_LOADPTR: { Value p = pop(g); push(g, *cellptr_of(p)); break; }
        case OP_STOREPTR: { Value v = pop(g); Value p = pop(g); *cellptr_of(p) = v; gc_barrier(v); break; }
        case OP_FIELD: { Value s = struct_of(pop(g)); if (!s.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference"); push(g, OBJ_CELLPTR(s.o)[in->a]); break; }
        case OP_ADDRFIELD: { Value s = struct_of(pop(g)); if (!s.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
            Value p=val_nil(); p.k=VK_PTR; p.o=s.o; p.off=(uint32_t)in->a; p.t=(Type*)in->p; push(g,p); break; }
        case OP_INDEX: { Value i = pop(g); Value c = pop(g);
            int64_t idx = i.k==VK_UINT?(int64_t)i.b.u:i.b.i;
            push(g, index_value(c,idx)); break; }
        case OP_ADDRINDEX: { Value i = pop(g); Value c = pop(g);
            int64_t idx = i.k==VK_UINT?(int64_t)i.b.u:i.b.i;
            Value *slot = index_addr(c,idx);
            Obj *base=NULL; uint32_t off=0;
            if (c.k==VK_SLICE){ base=c.o; off=(uint32_t)(c.off+idx); }
            else if (c.k==VK_ARRAY){ base=c.o; off=(uint32_t)idx; }
            else if (c.k==VK_PTR){ Value arr = OBJ_CELLPTR(c.o)[c.off]; base=arr.o; off=(uint32_t)idx; }
            (void)slot;
            Value p=val_nil(); p.k=VK_PTR; p.o=base; p.off=off; p.t=(Type*)in->p; push(g,p); break; }
        case OP_INDEXSET: { Value v = pop(g); Value i = pop(g); Value c = pop(g);
            int64_t idx = i.k==VK_UINT?(int64_t)i.b.u:i.b.i;
            *index_addr(c,idx) = v; gc_barrier(v); break; }
        case OP_MAPGET: { Value k = pop(g); Value m = pop(g);
            Value out; Type *mt = (Type*)in->p;
            int ok = m.o ? map_get((GoMap*)m.o->aux,k,&out) : 0;
            if (!ok) out = val_zero(underlying(mt)->elem);
            push(g,out);
            if (in->a) push(g,val_bool(ok));
            break; }
        case OP_MAPSET: { Value v = pop(g); Value k = pop(g); Value m = pop(g);
            if (!m.o) vm_panic_str("assignment to entry in nil map");
            map_set((GoMap*)m.o->aux,k,v); gc_barrier(k); gc_barrier(v); break; }
        case OP_MAPDEL: { Value k = pop(g); Value m = pop(g); if (m.o) map_delete((GoMap*)m.o->aux,k); break; }
        case OP_SLICE: {
            Value mx=val_nil(), hi=val_nil(), lo=val_nil();
            if (in->a & 4) mx = pop(g);
            if (in->a & 2) hi = pop(g);
            if (in->a & 1) lo = pop(g);
            Value c = pop(g);
            int64_t l = (in->a&1) ? (lo.k==VK_UINT?(int64_t)lo.b.u:lo.b.i) : 0;
            int64_t clen, ccap; Obj *base; int64_t coff;
            if (c.k==VK_STRING){
                clen = c.len; ccap = c.len; base=c.o; coff=c.off;
                int64_t h = (in->a&2) ? (hi.k==VK_UINT?(int64_t)hi.b.u:hi.b.i) : clen;
                if (h>clen) vm_panic_str("runtime error: slice bounds out of range [:%lld] with length %lld",(long long)h,(long long)clen);
                if (l<0) vm_panic_str("runtime error: slice bounds out of range [%lld:]",(long long)l);
                if (h<l) vm_panic_str("runtime error: slice bounds out of range [%lld:%lld]",(long long)l,(long long)h);
                Value r = c; r.off = (uint32_t)(coff+l); r.len = h-l; if (r.len==0) { r.o=c.o; }
                push(g,r); break;
            }
            if (c.k==VK_ARRAY){ clen = c.o?c.o->n:0; ccap=clen; base=c.o; coff=0; }
            else if (c.k==VK_PTR && c.cap==-3){ clen = c.len; ccap = c.len; base=c.o; coff=c.off; }
            else if (c.k==VK_PTR){ Value arr = OBJ_CELLPTR(c.o)[c.off]; clen = arr.o?arr.o->n:0; ccap=clen; base=arr.o; coff=0; }
            else if (c.k==VK_SLICE){ clen=c.len; ccap=c.cap; base=c.o; coff=c.off; }
            else if (c.k==VK_NIL){ clen=0; ccap=0; base=NULL; coff=0; }
            else { vm_panic_str("runtime error: cannot slice value"); break; }
            int64_t h = (in->a&2) ? (hi.k==VK_UINT?(int64_t)hi.b.u:hi.b.i) : clen;
            int64_t m = (in->a&4) ? (mx.k==VK_UINT?(int64_t)mx.b.u:mx.b.i) : ccap;
            if (m>ccap) vm_panic_str("runtime error: slice bounds out of range [::%lld] with capacity %lld",(long long)m,(long long)ccap);
            if (h>m) vm_panic_str("runtime error: slice bounds out of range [:%lld] with capacity %lld",(long long)h,(long long)ccap);
            if (l<0) vm_panic_str("runtime error: slice bounds out of range [%lld:]",(long long)l);
            if (h<l) vm_panic_str("runtime error: slice bounds out of range [%lld:%lld]",(long long)l,(long long)h);
            Value r = val_nil(); r.k=VK_SLICE; r.o=base; r.off=(uint32_t)(coff+l); r.len=h-l; r.cap=m-l;
            r.t = (Type*)in->p;
            push(g,r); break;
        }
        case OP_BIN: { Value b = pop(g); Value a = pop(g); push(g, vm_binop((TokKind)in->a,a,b,(Type*)in->p)); break; }
        case OP_SHIFT: { Value b = pop(g); Value a = pop(g);
            int64_t sc = b.k==VK_UINT?(int64_t)b.b.u:b.b.i;
            if (sc < 0) vm_panic_str("runtime error: negative shift amount");
            push(g, vm_shift((TokKind)in->a,a,(uint64_t)sc,(Type*)in->p)); break; }
        case OP_UN: {
            Value a = pop(g);
            switch ((TokKind)in->a){
            case TOK_SUB:
                if (a.k==VK_FLOAT) push(g,val_float(-a.b.d,a.t));
                else if (a.k==VK_UINT) push(g,val_uint(trunc_uint((uint64_t)(0-a.b.u),a.t),a.t));
                else push(g,val_int(trunc_int(-a.b.i,a.t),a.t));
                break;
            case TOK_NOT: push(g,val_bool(!(a.b.i!=0))); break;
            case TOK_XOR:
                if (a.k==VK_UINT) push(g,val_uint(trunc_uint(~a.b.u,a.t),a.t));
                else push(g,val_int(trunc_int(~a.b.i,a.t),a.t));
                break;
            default: push(g,a); break;
            }
            break;
        }
        case OP_CMP: {
            Value b = pop(g); Value a = pop(g);
            TokKind op = (TokKind)in->a;
            int r;
            if (in->b){
                /* interface compared against the predeclared nil: an interface is
                 * nil only when it holds no dynamic type at all */
                Value iv = (in->b==1) ? a : b;
                int isnil = (iv.k==VK_NIL && iv.t==NULL);
                r = (op==TOK_EQL) ? isnil : !isnil;
                push(g,val_bool(r));
                break;
            }
            if (op==TOK_EQL) r = val_equal(a,b);
            else if (op==TOK_NEQ) r = !val_equal(a,b);
            else r = vm_compare_ord(op,a,b);
            push(g,val_bool(r));
            break;
        }
        case OP_CONV: push(g, vm_convert(pop(g),(Type*)in->p)); break;
        case OP_MAKEIFACE: { Value v = pop(g); if (is_untyped(v.t)) v.t = default_type(v.t); push(g,v); break; }
        case OP_JMP: { int back = in->a <= f->pc; f->pc = in->a; if (back && --budget <= 0){ runq_add(g); cur_jb=jb.prev; return; } break; }
        case OP_JMPT: { Value v = pop(g); if (v.b.i) f->pc = in->a; break; }
        case OP_JMPF: { Value v = pop(g); if (!v.b.i) f->pc = in->a; break; }
        case OP_YIELDCHK: if (--budget <= 0){ runq_add(g); cur_jb=jb.prev; return; } break;
        case OP_CALL: {
            int nargs = in->a;
            Value fv = g->stack[g->sp-nargs-1];
            /* move callee below args: stack is [fn][args...] */
            for (int i=0;i<nargs;i++) g->stack[g->sp-nargs-1+i] = g->stack[g->sp-nargs+i];
            g->sp--;
            do_call(g,fv,nargs);
            if (g->status==G_WAITING){ cur_jb=jb.prev; return; }
            if (g->preempt){ g->preempt=0; cur_jb=jb.prev; return; }
            if (g->nframes==0){ cur_jb=jb.prev; return; }
            break;
        }
        case OP_CALLIFACE: {
            int nargs = in->a;
            Value recv = g->stack[g->sp-nargs-1];
            const char *name = (const char*)in->p;
            if (recv.k==VK_NIL && !recv.t) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
            int indirect=0;
            Method *m = lookup_method(recv.t,name,&indirect);
            if (!m || !m->def) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
            /* adjust receiver for embedded promotion */
            Value r2 = recv;
            r2 = adjust_receiver(recv,m);
            g->stack[g->sp-nargs-1] = r2;
            Value fnv = val_nil(); fnv.k=VK_FUNC; fnv.o=closure_new(m->def,NULL,NULL); fnv.t=m->sig;
            do_call(g,fnv,nargs+1);
            if (g->status==G_WAITING){ cur_jb=jb.prev; return; }
            break;
        }
        case OP_GO: {
            int nargs = in->a;
            Value fv = g->stack[g->sp-nargs-1];
            G *ng = g_new();
            ng->startfn = fv;
            ng->nstartargs = nargs;
            ng->startargs = xmalloc(sizeof(Value)*(size_t)(nargs?nargs:1));
            for (int i=0;i<nargs;i++) ng->startargs[i] = g->stack[g->sp-nargs+i];
            g->sp -= nargs+1;
            G *save = g_current;
            g_current = ng;
            for (int i=0;i<nargs;i++) push(ng,ng->startargs[i]);
            do_call(ng,fv,nargs);
            g_current = save;
            runq_add(ng);
            if (g->bubble){ ng->bubble = g->bubble; }
            break;
        }
        case OP_DEFER: {
            int nargs = in->a;
            Value fv = g->stack[g->sp-nargs-1];
            DeferRec *d = xcalloc(1,sizeof *d);
            d->fn = fv; d->nargs = nargs;
            d->args = xmalloc(sizeof(Value)*(size_t)(nargs?nargs:1));
            for (int i=0;i<nargs;i++) d->args[i] = g->stack[g->sp-nargs+i];
            g->sp -= nargs+1;
            vec_push(&f->defers,d);
            break;
        }
        case OP_RET:
            retn = in->a;
        do_return: {
            Frame *fr = &g->frames[g->nframes-1];
            int named = (retn == -2);
            int nres = retn < 0 ? 0 : retn;
            Value res[8];
            for (int i=0;i<nres && i<8;i++) res[i] = g->stack[g->sp-nres+i];
            g->sp = fr->sp_base;
            /* run defers */
            if (fr->defers.len){
                for (int i=fr->defers.len-1;i>=0;i--){
                    DeferRec *d = fr->defers.p[i];
                    fr->defers.len = i;
                    int bf = g->nframes;
                    int sbase = g->sp;
                    for (int a=0;a<d->nargs;a++) push(g,d->args[a]);
                    int nat = do_call(g,d->fn,d->nargs);
                    if (!nat) exec(g,bf);
                    g->sp = sbase;
                    free(d->args); free(d);
                    fr = &g->frames[g->nframes-1];
                }
                vec_free(&fr->defers);
            }
            if (named && fr->fn->nres_named){
                nres = fr->fn->nres_named;
                Value *fc = OBJ_CELLPTR(fr->fnenv);
                for (int i=0;i<nres && i<8;i++) res[i] = fc[fr->fn->res_slot0+1+i];
            }
            g->nframes--;
            for (int i=g->sp;i<g->stackcap && i<g->sp+32;i++) memset(&g->stack[i],0,sizeof(Value));
            for (int i=0;i<nres && i<8;i++) push(g,res[i]);
            if (g->nframes <= stopframes){ cur_jb = jb.prev; return; }
            break;
        }
        case OP_NEWENV: { f->env = env_new(in->a, f->env); break; }
        case OP_COPYENV: {
            Obj *old = f->env;
            Obj *ne = env_new(in->a, OBJ_CELLPTR(old)[0].o);
            Value *oc = OBJ_CELLPTR(old), *nc = OBJ_CELLPTR(ne);
            for (int i=1;i<=in->a && i<(int)old->n;i++) nc[i] = oc[i];
            f->env = ne;
            break;
        }
        case OP_SPREAD: { g->stack[g->sp-1].cap = -1; break; }
        case OP_POPENV: { f->env = env_at(f->env,1); break; }
        case OP_MAKESLICE: {
            Value cap = val_nil(); int has_cap = in->a;
            if (has_cap) cap = pop(g);
            Value ln = pop(g);
            int64_t l = ln.k==VK_UINT?(int64_t)ln.b.u:ln.b.i;
            int64_t c = has_cap ? (cap.k==VK_UINT?(int64_t)cap.b.u:cap.b.i) : l;
            if (l<0) vm_panic_str("runtime error: makeslice: len out of range");
            if (c<l) vm_panic_str("runtime error: makeslice: cap out of range");
            Type *st = (Type*)in->p;
            Obj *o = c>0 ? gc_alloc_cells((int)c) : NULL;
            if (o){ Value *cells=OBJ_CELLPTR(o); Type *et=underlying(st)->elem; for (int64_t i=0;i<c;i++) cells[i]=val_zero(et); }
            Value v = val_nil(); v.k=VK_SLICE; v.o=o; v.len=l; v.cap=c; v.t=st;
            push(g,v); break;
        }
        case OP_MAKEMAP: {
            if (in->a) pop(g);
            Type *mt = (Type*)in->p;
            Obj *o = gc_alloc_obj(OBJ_MAP,0);
            o->aux = map_new(mt);
            Value v = val_nil(); v.k=VK_MAP; v.o=o; v.t=mt;
            push(g,v); break;
        }
        case OP_MAKECHAN: {
            int64_t cap = 0;
            if (in->a){ Value cv = pop(g); cap = cv.k==VK_UINT?(int64_t)cv.b.u:cv.b.i; }
            push(g, chan_make((Type*)in->p,cap)); break;
        }
        case OP_NEW: {
            Type *t = (Type*)in->p;
            Obj *o = gc_alloc_cells(1);
            OBJ_CELLPTR(o)[0] = in->b ? pop(g) : val_zero(t);
            Value v = val_nil(); v.k=VK_PTR; v.o=o; v.off=0; v.t=type_ptr(t);
            push(g,v); break;
        }
        case OP_COMPOSITE_STRUCT: {
            int n = in->a;
            Type *t = (Type*)in->p;
            Value v = val_zero(t);
            Value *cells = OBJ_CELLPTR(v.o);
            for (int i=n-1;i>=0;i--){
                Value fv = pop(g);
                Value iv = pop(g);
                if (iv.b.i < 0 || iv.b.i >= (int64_t)v.o->n)
                    vm_panic_str("runtime error: internal: struct literal field %lld of %s (%u fields) at %s:%d in %s",
                                 (long long)iv.b.i, type_string(t), v.o->n,
                                 fn->srcfile?fn->srcfile:"?", in->line, fn->name?fn->name:"?");
                cells[iv.b.i] = val_copy(fv);
            }
            push(g,v); break;
        }
        case OP_COMPOSITE_ARRAY: case OP_COMPOSITE_SLICE: {
            int n = in->a;
            Type *t = (Type*)in->p;
            Type *u = underlying(t);
            int64_t size = in->b;
            Obj *o = gc_alloc_cells((int)size);
            Value *cells = OBJ_CELLPTR(o);
            Type *et = u->elem;
            for (int64_t i=0;i<size;i++) cells[i] = val_zero(et);
            for (int i=n-1;i>=0;i--){
                Value ev = pop(g);
                Value iv = pop(g);
                if (iv.b.i>=0 && iv.b.i<size) cells[iv.b.i] = val_copy(ev);
            }
            Value v = val_nil();
            if (in->op==OP_COMPOSITE_ARRAY){ v.k=VK_ARRAY; v.o=o; v.t=t; }
            else { v.k=VK_SLICE; v.o=o; v.len=size; v.cap=size; v.t=t; }
            push(g,v); break;
        }
        case OP_COMPOSITE_MAP: {
            int n = in->a;
            Type *t = (Type*)in->p;
            Obj *o = gc_alloc_obj(OBJ_MAP,0);
            o->aux = map_new(t);
            Value m = val_nil(); m.k=VK_MAP; m.o=o; m.t=t;
            Value *tmp = xmalloc(sizeof(Value)*(size_t)(2*n?2*n:1));
            for (int i=n-1;i>=0;i--){ tmp[2*i+1]=pop(g); tmp[2*i]=pop(g); }
            for (int i=0;i<n;i++) map_set((GoMap*)o->aux,tmp[2*i],tmp[2*i+1]);
            free(tmp);
            push(g,m); break;
        }
        case OP_SEND: {
            Value v = g->stack[g->sp-1];
            Value cv = g->stack[g->sp-2];
            if (g->selected){ g->selected=0; g->sp-=2; break; }
            if (!cv.o){ /* nil channel blocks forever */
                g->status=G_WAITING; g->wait_reason="chan send (nil chan)"; f->pc--; cur_jb=jb.prev; return;
            }
            if (chan_send(cv,v)){ g->sp -= 2; break; }
            { Chan *ch = chan_of(cv); g->recv_value = v; vec_push(&ch->sendq,g); g->status=G_WAITING; g->wait_reason="chan send"; f->pc--; cur_jb=jb.prev; return; }
        }
        case OP_RECV: {
            Value cv = g->stack[g->sp-1];
            if (g->selected){
                g->selected = 0;
                g->sp--;
                push(g,g->recv_value);
                if (in->a) push(g,val_bool(g->recv_ok));
                break;
            }
            if (!cv.o){ g->status=G_WAITING; g->wait_reason="chan receive (nil chan)"; f->pc--; cur_jb=jb.prev; return; }
            Value out; int ok=0;
            if (chan_recv(cv,&out,&ok)){
                g->sp--;
                push(g,out);
                if (in->a) push(g,val_bool(ok));
                break;
            }
            { Chan *ch = chan_of(cv); vec_push(&ch->recvq,g); g->status=G_WAITING; g->wait_reason="chan receive"; f->pc--; cur_jb=jb.prev; return; }
        }
        case OP_CLOSE: { Value cv = pop(g); chan_close(cv); break; }
        case OP_SELECT: {
            SelDesc *sd = (SelDesc*)in->p;
            int nops = 0;
            for (int i=0;i<sd->n;i++){ if (sd->c[i].kind==0) nops++; else if (sd->c[i].kind==1) nops+=2; }
            int base = g->sp - nops;
            /* find ready cases */
            int ready[64], nready=0, defidx=-1;
            int oi = 0;
            for (int i=0;i<sd->n;i++){
                if (sd->c[i].kind==2){ defidx=i; continue; }
                if (sd->c[i].kind==0){
                    Value cv = g->stack[base+oi];
                    Chan *ch = chan_of(cv);
                    if (ch && (ch->buflen>0 || ch->sendq.len>0 || ch->closed)) ready[nready++]=i;
                    oi += 1;
                } else {
                    Value cv = g->stack[base+oi];
                    Chan *ch = chan_of(cv);
                    if (ch && (ch->closed || ch->recvq.len>0 || ch->buflen<ch->bufcap)) ready[nready++]=i;
                    oi += 2;
                }
            }
            if (nready==0 && defidx>=0){
                g->sp = base;
                f->pc = sd->c[defidx].target;
                break;
            }
            if (nready==0){
                /* block: wake when any channel activity happens */
                vec_push(&selwait,g);
                g->status = G_WAITING; g->wait_reason = "select";
                f->pc--;
                cur_jb = jb.prev;
                return;
            }
            int pick = ready[(int)(nanotime()%(uint64_t)nready)];
            /* execute the chosen op */
            oi = 0;
            Value chosen_val = val_nil(); int chosen_ok = 0;
            for (int i=0;i<sd->n;i++){
                if (sd->c[i].kind==2) continue;
                if (i==pick){
                    Value cv = g->stack[base+oi];
                    if (sd->c[i].kind==0){ chan_recv(cv,&chosen_val,&chosen_ok); }
                    else { chan_send(cv,g->stack[base+oi+1]); }
                    break;
                }
                oi += (sd->c[i].kind==0)?1:2;
            }
            g->sp = base;
            if (sd->c[pick].kind==0){
                push(g,chosen_val);
                if (sd->c[pick].has_ok) push(g,val_bool(chosen_ok));
            }
            f->pc = sd->c[pick].target;
            break;
        }
        case OP_LEN: {
            Value v = pop(g);
            int64_t n = 0;
            switch (v.k){
            case VK_STRING: n=v.len; break;
            case VK_SLICE: n=v.len; break;
            case VK_ARRAY: n=v.o?v.o->n:0; break;
            case VK_MAP: n=v.o?map_len((GoMap*)v.o->aux):0; break;
            case VK_CHAN: n=v.o?chan_of(v)->buflen:0; break;
            case VK_PTR: { if (!v.o){ n=0; break; } if (v.cap==-3){ n=v.len; break; } Value a=OBJ_CELLPTR(v.o)[v.off]; n = a.o?a.o->n:0; break; }
            case VK_NIL: n=0; break;
            default: n=0; break;
            }
            push(g,val_int(n,ty_int)); break;
        }
        case OP_CAP: {
            Value v = pop(g);
            int64_t n = 0;
            switch (v.k){
            case VK_SLICE: n=v.cap; break;
            case VK_ARRAY: n=v.o?v.o->n:0; break;
            case VK_CHAN: n=v.o?chan_of(v)->bufcap:0; break;
            case VK_PTR: { if (!v.o){ n=0; break; } if (v.cap==-3){ n=v.len; break; } Value a=OBJ_CELLPTR(v.o)[v.off]; n = a.o?a.o->n:0; break; }
            default: n=0; break;
            }
            push(g,val_int(n,ty_int)); break;
        }
        case OP_APPEND: {
            int n = in->a;
            Value *elems = &g->stack[g->sp-n];
            Value s = g->stack[g->sp-n-1];
            Value r;
            if (in->b){
                /* append(s, other...) */
                Value other = elems[0];
                if (other.k==VK_STRING){
                    int64_t m = other.len; const char *p = str_ptr(other);
                    Value *tmp = xmalloc(sizeof(Value)*(size_t)(m?m:1));
                    for (int64_t i=0;i<m;i++) tmp[i]=val_uint((unsigned char)p[i],ty_uint8);
                    r = slice_append(s,tmp,(int)m,(Type*)in->p);
                    free(tmp);
                } else {
                    int64_t m = other.len;
                    Value *tmp = xmalloc(sizeof(Value)*(size_t)(m?m:1));
                    for (int64_t i=0;i<m;i++) tmp[i]=OBJ_CELLPTR(other.o)[other.off+i];
                    r = slice_append(s,tmp,(int)m,(Type*)in->p);
                    free(tmp);
                }
            } else {
                r = slice_append(s,elems,n,(Type*)in->p);
            }
            g->sp -= n+1;
            push(g,r); break;
        }
        case OP_COPY: {
            Value src = pop(g), dst = pop(g);
            int64_t n = 0;
            if (src.k==VK_STRING){
                n = dst.len < src.len ? dst.len : src.len;
                Value *d = dst.o?OBJ_CELLPTR(dst.o):NULL;
                const char *s = str_ptr(src);
                for (int64_t i=0;i<n;i++) d[dst.off+i] = val_uint((unsigned char)s[i],ty_uint8);
            } else {
                n = dst.len < src.len ? dst.len : src.len;
                Value *d = dst.o?OBJ_CELLPTR(dst.o):NULL;
                Value *s = src.o?OBJ_CELLPTR(src.o):NULL;
                if (d && s){
                    if (d==s && dst.off>src.off){ for (int64_t i=n-1;i>=0;i--) d[dst.off+i]=val_copy(s[src.off+i]); }
                    else for (int64_t i=0;i<n;i++) d[dst.off+i]=val_copy(s[src.off+i]);
                }
            }
            push(g,val_int(n,ty_int)); break;
        }
        case OP_DELETE: { Value k = pop(g); Value m = pop(g); if (m.o) map_delete((GoMap*)m.o->aux,k); break; }
        case OP_CLEAR: {
            Value v = pop(g);
            if (v.k==VK_MAP && v.o) map_clear((GoMap*)v.o->aux);
            else if (v.k==VK_SLICE && v.o){
                Value *c = OBJ_CELLPTR(v.o);
                Type *et = underlying(v.t)->elem;
                for (int64_t i=0;i<v.len;i++) c[v.off+i] = val_zero(et);
            }
            break;
        }
        case OP_MINMAX: {
            int n = in->a;
            Value best = g->stack[g->sp-n];
            for (int i=1;i<n;i++){
                Value o = g->stack[g->sp-n+i];
                int lt = vm_compare_ord(TOK_LSS,o,best);
                int isnanb = (best.k==VK_FLOAT && isnan(best.b.d));
                int isnano = (o.k==VK_FLOAT && isnan(o.b.d));
                if (isnano) best = o;
                else if (isnanb) ;
                else if (in->b ? !lt && vm_compare_ord(TOK_GTR,o,best) : lt) best = o;
            }
            g->sp -= n;
            push(g,best); break;
        }
        case OP_PANIC: {
            Value v = pop(g);
            if (v.k==VK_NIL && !v.t && g_panic_nil_error && ty_panicnil){
                Obj *o = gc_alloc_cells(1);
                OBJ_CELLPTR(o)[0] = val_zero(ty_panicnil);
                Value pv = val_nil(); pv.k=VK_PTR; pv.o=o; pv.t=type_ptr(ty_panicnil);
                v = pv;
            }
            vm_throw(v);
            break;
        }
        case OP_RECOVER: {
            /* recover() is only effective when called directly by a deferred function */
            if (g->panicking && g->nframes >= 2){
                g->panicking = 0;
                g->in_recovery = 1;
                for (int i=g->panic_chain.len-1;i>=0;i--){
                    PanicRec *pr = g->panic_chain.p[i];
                    if (!pr->recovered){ pr->recovered = 1; break; }
                }
                Value v = g->panic_value;
                g->panic_value = val_nil();
                g->panic_recovered = 1;
                push(g,v);
            } else {
                push(g,val_nil());
            }
            break;
        }
        case OP_TYPEASSERT: {
            Value v = pop(g);
            Type *want = (Type*)in->p;
            int ok = type_assert_ok(v,want);
            if (in->a){
                push(g, ok ? (is_iface(want)?v:vm_convert(v,want)) : val_zero(want));
                push(g, val_bool(ok));
            } else {
                if (!ok){
                    const char *have = (v.k==VK_NIL&&!v.t) ? "nil" : type_string(v.t);
                    if (v.k==VK_NIL && !v.t)
                        vm_panic_str("interface conversion: interface {} is nil, not %s", type_string(want));
                    else if (is_iface(want)){
                        const char *missing=NULL; iface_satisfied(v.t,want,&missing);
                        vm_panic_str("interface conversion: %s is not %s: missing method %s", have, type_string(want), missing?missing:"?");
                    } else
                        vm_panic_str("interface conversion: interface {} is %s, not %s", have, type_string(want));
                }
                push(g, is_iface(want)?v:vm_convert(v,want));
            }
            break;
        }
        case OP_CLOSURE: { FuncDef *cf = (FuncDef*)in->p;
            push(g, make_closure(cf, (cf->is_closure && cf->captures) ? f->env : NULL)); break; }
        case OP_METHVAL: {
            Value recv = pop(g);
            FuncDef *md = (FuncDef*)in->p;
            if (in->b){
                const char *nm = (const char*)in->p;
                if (recv.k==VK_NIL && !recv.t) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
                int ind=0; Method *m = lookup_method(recv.t,nm,&ind);
                if (!m || !m->def) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
                md = m->def;
                recv = adjust_receiver(recv,m);
            }
            push(g, make_method_value(md, recv));
            break;
        }
        case OP_NATIVE: {
            if (in->a < 0){
                extern int unsafe_op(int op, Value *args, int nargs, Type *t, Value *out);
                int nargs2 = in->b;
                Value out;
                unsafe_op(-in->a, &g->stack[g->sp-nargs2], nargs2, (Type*)in->p, &out);
                g->sp -= nargs2;
                push(g,out);
                break;
            }
            int nargs = in->b;
            Value rets[8];
            int base = g->sp - nargs;
            int nr = native_call(in->a,&g->stack[base],nargs,rets);
            g->sp = base;
            for (int i=0;i<nr;i++) push(g,rets[i]);
            break;
        }
        case OP_RANGEINIT: {
            Value c = pop(g);
            Obj *it = gc_alloc_cells(4);
            Value *cells = OBJ_CELLPTR(it);
            cells[0] = c;
            cells[1] = val_int(0,ty_int);
            cells[2] = val_nil();
            cells[3] = val_int(0,ty_int);
            if (c.k==VK_MAP && c.o){
                Vec keys; memset(&keys,0,sizeof keys);
                map_keys((GoMap*)c.o->aux,&keys);
                Obj *ko = gc_alloc_cells(keys.len*2);
                Value *kc = OBJ_CELLPTR(ko);
                for (int i=0;i<keys.len;i++){ Value *kv = keys.p[i]; kc[2*i]=kv[0]; kc[2*i+1]=kv[1]; free(kv); }
                vec_free(&keys);
                Value kvv = val_nil(); kvv.k=VK_SLICE; kvv.o=ko; kvv.len=ko->n/2; kvv.cap=kvv.len;
                cells[2] = kvv;
                cells[3] = val_int(kvv.len,ty_int);
            }
            Value iv = val_nil(); iv.k=VK_PTR; iv.o=it; iv.off=0;
            push(g,iv); break;
        }
        case OP_RANGENEXT: {
            Value iv = g->stack[g->sp-1];
            Value *cells = OBJ_CELLPTR(iv.o);
            Value c = cells[0];
            int64_t i = cells[1].b.i;
            int wantk = in->b & 1, wantv = in->b & 2;
            if (c.k==VK_CHAN){
                if (g->selected){ g->selected=0;
                    if (!g->recv_ok){ f->pc = in->a; g->sp--; break; }
                    if (wantk) push(g,g->recv_value);
                    break;
                }
                if (!c.o){ g->status=G_WAITING; g->wait_reason="chan receive (nil chan)"; f->pc--; cur_jb=jb.prev; return; }
                Value out; int ok=0;
                if (!chan_recv(c,&out,&ok)){
                    Chan *ch = chan_of(c); vec_push(&ch->recvq,g); g->status=G_WAITING; g->wait_reason="chan receive"; f->pc--; cur_jb=jb.prev; return;
                }
                if (!ok){ f->pc = in->a; g->sp--; break; }
                if (wantk) push(g,out);
                break;
            }
            int64_t n = 0;
            switch (c.k){
            case VK_SLICE: n=c.len; break;
            case VK_ARRAY: n=c.o?c.o->n:0; break;
            case VK_STRING: n=c.len; break;
            case VK_MAP: n=cells[3].b.i; break;
            case VK_PTR: { if (c.o){ if (c.cap==-3){ n=c.len; break; } Value a=OBJ_CELLPTR(c.o)[c.off]; n = a.o?a.o->n:0; } break; }
            case VK_INT: n = c.b.i; break;
            case VK_NIL: n = 0; break;
            default: n=0; break;
            }
            if (c.k==VK_STRING){
                if (i >= n){ f->pc = in->a; g->sp--; break; }
                unsigned r; int sz = utf8_decode(str_ptr(c)+i,n-i,&r);
                cells[1] = val_int(i+(sz?sz:1),ty_int);
                if (wantk) push(g,val_int(i,ty_int));
                if (wantv) push(g,val_int((int64_t)r,ty_int32));
                break;
            }
            if (i >= n){ f->pc = in->a; g->sp--; break; }
            cells[1] = val_int(i+1,ty_int);
            if (c.k==VK_MAP){
                Value kv = cells[2];
                Value *kc = OBJ_CELLPTR(kv.o);
                if (wantk) push(g,kc[2*i]);
                if (wantv) push(g,kc[2*i+1]);
            } else if (c.k==VK_INT){
                if (wantk) push(g,val_int(i,c.t?c.t:ty_int));
            } else {
                if (wantk) push(g,val_int(i,ty_int));
                if (wantv){
                    Value e;
                    if (c.k==VK_SLICE) e = OBJ_CELLPTR(c.o)[c.off+i];
                    else if (c.k==VK_ARRAY) e = OBJ_CELLPTR(c.o)[i];
                    else if (c.k==VK_PTR && c.cap==-3) e = OBJ_CELLPTR(c.o)[c.off+i];
                    else if (c.k==VK_PTR){ Value a=OBJ_CELLPTR(c.o)[c.off]; e = OBJ_CELLPTR(a.o)[i]; }
                    else e = val_nil();
                    push(g,val_copy(e));
                }
            }
            break;
        }
        case OP_PRINT: {
            int n = in->a;
            extern void builtin_print(Value *args, int n, int ln);
            builtin_print(&g->stack[g->sp-n], n, in->b);
            g->sp -= n;
            break;
        }
        case OP_ITERSTOP: push(g,val_bool(g->iter_stop)); break;
        case OP_TUPLEGET: break;
        default:
            vm_panic_str("unimplemented opcode %d", in->op);
            break;
        }
        continue;

    unwind:
        /* panic unwinding: pop frames, running defers, until recovered */
        while (g->nframes > stopframes){
            Frame *fr = &g->frames[g->nframes-1];
            g->sp = fr->sp_base;
            int recovered = 0;
            while (fr->defers.len){
                DeferRec *d = fr->defers.p[fr->defers.len-1];
                fr->defers.len--;
                int bf = g->nframes;
                int sbase = g->sp;
                for (int a=0;a<d->nargs;a++) push(g,d->args[a]);
                int nat = do_call(g,d->fn,d->nargs);
                if (!nat){
                    JB *save = cur_jb;
                    cur_jb = &jb;
                    exec(g,bf);
                    cur_jb = save;
                }
                g->sp = sbase;
                free(d->args); free(d);
                fr = &g->frames[g->nframes-1];
                if (!g->panicking){ recovered = 1; break; }
            }
            if (recovered){
                /* the deferred call recovered: return zero values from this frame */
                Frame *rf = &g->frames[g->nframes-1];
                int nres = rf->nres;
                Type *sig_res = NULL;
                g->sp = rf->sp_base;
                vec_free(&rf->defers);
                g->nframes--;
                if (rf->fn->nres_named){
                    Value *fc = OBJ_CELLPTR(rf->fnenv);
                    for (int i=0;i<rf->fn->nres_named;i++) push(g, fc[rf->fn->res_slot0+1+i]);
                } else {
                    for (int i=0;i<nres;i++){
                        Type *rt = ((Field*)rf->fn->sig->results.p[i])->type;
                        push(g, val_zero(rt));
                    }
                }
                (void)sig_res;
                g->in_recovery = 0;
                g->panic_chain.len = 0;
                if (g->nframes <= stopframes){ cur_jb = jb.prev; return; }
                if (setjmp(jb.jb)) goto unwind;
                goto resume;
            }
            vec_free(&fr->defers);
            g->nframes--;
        }
        cur_jb = jb.prev;
        if (jb.prev) longjmp(jb.prev->jb,1);
        /* top level: report the panic */
        {
            extern void vm_report_panic(Value v);
            vm_report_panic(g->panic_value);
        }
        return;
    resume: ;
    }
}

/* ------------------------------------------------------------ top level --- */

extern char *format_value_for_panic(Value v);

extern int g_run_mode;

/* Exit like the go command: `go run` reports the program's status and exits 1. */
void go_program_exit(int code){
    fflush(stdout);
    if (g_run_mode && code != 0){
        fprintf(stderr,"exit status %d\n",code);
        fflush(stderr);
        exit(1);
    }
    exit(code);
}

void vm_report_panic(Value v){
    fflush(stdout);
    G *g = g_current;
    Buf b; buf_init(&b);
    int n = g ? g->panic_chain.len : 0;
    if (n == 0){
        char *s = format_value_for_panic(v);
        buf_printf(&b,"panic: %s\n",s);
    } else {
        for (int i=0;i<n;i++){
            PanicRec *pr = g->panic_chain.p[i];
            char *s = format_value_for_panic(pr->v);
            buf_printf(&b,"%spanic: %s%s\n", i?"\t":"", s, pr->recovered?" [recovered]":"");
        }
    }
    fprintf(stderr,"%s\ngoroutine 1 [running]:\nmain.main()\n\t/app/main.go:0 +0x0\n", b.p?b.p:"panic:");
    fflush(stderr);
    go_program_exit(2);
}

Value adjust_receiver(Value recv, Method *m){
    if (!m->recv) return recv;
    Type *want = m->recv;
    Type *wantbase = want->k==T_PTR ? want->elem : want;
    /* walk embedded fields until the receiver type matches */
    for (int depth=0; depth<8; depth++){
        Type *have = recv.t;
        Type *havebase = have && have->k==T_PTR ? have->elem : have;
        if (havebase && wantbase && type_identical(havebase,wantbase)){
            if (want->k==T_PTR && recv.k!=VK_PTR){
                /* need addressable receiver: box it */
                Obj *o = gc_alloc_cells(1);
                OBJ_CELLPTR(o)[0] = recv;
                Value p = val_nil(); p.k=VK_PTR; p.o=o; p.t=type_ptr(have);
                return p;
            }
            if (want->k!=T_PTR && recv.k==VK_PTR && recv.o){
                Value d = OBJ_CELLPTR(recv.o)[recv.off];
                return d;
            }
            return recv;
        }
        Value s = recv.k==VK_PTR ? (recv.o?OBJ_CELLPTR(recv.o)[recv.off]:val_nil()) : recv;
        if (s.k != VK_STRUCT || !s.o) return recv;
        Type *su = underlying(s.t);
        int found = -1;
        for (int i=0;i<su->fields.len;i++){
            Field *fl = su->fields.p[i];
            if (!fl->embedded) continue;
            Type *ft = fl->type;
            Type *fb = ft->k==T_PTR?ft->elem:ft;
            if (lookup_method(ft,m->name,NULL)){ found = i; break; }
            (void)fb;
        }
        if (found < 0) return recv;
        Value fv = OBJ_CELLPTR(s.o)[found];
        if (fv.k==VK_STRUCT){
            Field *fl = su->fields.p[found];
            (void)fl;
            recv = fv;
            /* keep addressability through the parent object */
            Value p = val_nil(); p.k=VK_PTR; p.o=s.o; p.off=(uint32_t)found; p.t=type_ptr(fv.t);
            Method *mm = lookup_method(fv.t,m->name,NULL);
            if (mm && mm->recv && mm->recv->k==T_PTR) recv = p;
            else recv = fv;
        } else {
            recv = fv;
        }
    }
    return recv;
}

void vm_init(void){
    gc_init();
    types_init();
}

static int any_timer(int64_t *earliest){
    int found = 0;
    for (int i=0;i<g_allg.len;i++){
        G *g = g_allg.p[i];
        if (g->status==G_WAITING && g->wake_at > 0){
            if (!found || g->wake_at < *earliest){ *earliest = g->wake_at; found = 1; }
        }
    }
    return found;
}

extern void synctest_check_idle(void);

void vm_sched(void){
    for (;;){
        G *g = runq_next();
        if (!g){
            int64_t t;
            if (any_timer(&t)){
                int64_t now = vm_nanotime();
                if (t > now){
                    if (g_in_synctest) g_fake_now = t;
                    else { struct timespec ts; int64_t d = t-now; ts.tv_sec = d/1000000000; ts.tv_nsec = d%1000000000; nanosleep(&ts,NULL); }
                }
                now = vm_nanotime();
                for (int i=0;i<g_allg.len;i++){
                    G *w = g_allg.p[i];
                    if (w->status==G_WAITING && w->wake_at>0 && w->wake_at<=now){ w->wake_at=0; runq_add(w); }
                }
                continue;
            }
            /* nothing runnable */
            int alive = 0;
            for (int i=0;i<g_allg.len;i++){ G *w=g_allg.p[i]; if (w->status!=G_DEAD) alive++; }
            if (alive > 0){
                gc_run_pending_finalizers();
                if (runq.len) continue;
                fflush(stdout);
                fprintf(stderr,"fatal error: all goroutines are asleep - deadlock!\n\n");
                fprintf(stderr,"goroutine 1 [running]:\nmain.main()\n");
                fflush(stderr);
                { extern void go_program_exit(int); go_program_exit(2); }
            }
            return;
        }
        if (g->status == G_DEAD) continue;
        g_current = g;
        g->status = G_RUNNING;
        exec(g,0);
        if (g->nframes == 0){
            g->status = G_DEAD;
            g->finished = 1;
            wake_select_waiters();
            if (g->id == 1){ return; }
        } else if (g->status == G_RUNNING){
            runq_add(g);
        }
        gc_run_pending_finalizers();
    }
}

/* testing/synctest: run f with a synthetic clock. Time only advances when no
 * goroutine can run, which is exactly the scheduler's idle path. */
void vm_run_bubble(Value fn){
    G *g = g_current;
    int saved = g_in_synctest;
    int64_t savedNow = g_fake_now;
    g_in_synctest = 1;
    if (!savedNow) g_fake_now = 1000000000;
    Value rets[4];
    vm_call_value(fn,NULL,0,rets,4);
    g_in_synctest = saved;
    if (!saved) g_fake_now = savedNow;
}

/* Wait until every other goroutine in the bubble is durably blocked: the
 * scheduler reaches that state exactly when nothing else is runnable, so one
 * yield per pending goroutine is enough. */
void vm_synctest_wait(void){
    G *g = g_current;
    int runnable = 0;
    for (int i=0;i<g_allg.len;i++){
        G *o = g_allg.p[i];
        if (o==g || o->status==G_DEAD) continue;
        if (o->status==G_RUNNABLE || o->status==G_RUNNING) runnable = 1;
    }
    if (runnable) vm_yield();
}

void vm_yield(void){
    G *g = g_current;
    runq_add(g);
    g->status = G_RUNNABLE;
    g->preempt = 1;
}
void vm_sleep_ns(int64_t d){
    G *g = g_current;
    if (d <= 0){ vm_yield(); return; }
    g->wake_at = vm_nanotime() + d;
    g->status = G_WAITING;
    g->wait_reason = "sleep";
}
void vm_goexit(void){
    G *g = g_current;
    for (int i=g->nframes-1;i>=0;i--){
        Frame *fr = &g->frames[i];
        while (fr->defers.len){
            DeferRec *dr = fr->defers.p[--fr->defers.len];
            int bf = g->nframes;
            int sbase = g->sp;
            for (int a=0;a<dr->nargs;a++) push(g,dr->args[a]);
            int nat = do_call(g,dr->fn,dr->nargs);
            if (!nat) exec(g,bf);
            g->sp = sbase;
        }
    }
    g->nframes = 0;
    g->status = G_DEAD;
}

void vm_start_goroutine_main(Value fn){
    G *g = g_new();
    g_current = g;
    push(g,fn);
    g->sp--;
    do_call(g,fn,0);
    runq_add(g);
}

int vm_run_main(Package *mainpkg){
    (void)mainpkg;
    return 0;
}
