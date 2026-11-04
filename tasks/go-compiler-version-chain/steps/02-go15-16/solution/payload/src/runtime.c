#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <stdarg.h>
#include <math.h>
#include <time.h>

/* ============================== GC heap ================================= */

size_t gc_heap_bytes = 0;
size_t gc_next_gc = 4u<<20;
uint64_t gc_num_gc = 0, gc_forced_gc = 0;
uint64_t gc_total_alloc = 0;
uint64_t gc_mallocs = 0, gc_frees = 0;
int64_t gc_gogc = 100;
int64_t gc_memlimit = 0;
uint64_t gc_pause_total_ns = 0;
int gc_phase = 0;
uint64_t gc_mark_work = 0;

static Obj *allobjs = NULL;
static Vec g_pinned;
static int gc_enabled = 1;
static int gc_in_progress = 0;

typedef struct FinRec {
    Obj *obj;
    Value fn;
    Value arg;
    int is_cleanup;
    int queued;
    struct FinRec *next;
} FinRec;
static FinRec *finalizers = NULL;
static Vec fin_queue;   /* of FinRec* ready to run */

/* gray stack for the incremental marker */
static Obj **graystack = NULL;
static int graylen = 0, graycap = 0;

static void gray_push(Obj *o){
    if (!o || o->mark) return;
    o->mark = 1;
    if (o->noscan) return;
    if (graylen == graycap){ graycap = graycap?graycap*2:1024; graystack = xrealloc(graystack,(size_t)graycap*sizeof(Obj*)); }
    graystack[graylen++] = o;
}

Obj *gc_alloc_obj(ObjKind k, size_t extra){
    gc_maybe();
    Obj *o = xcalloc(1, sizeof(Obj)+extra);
    o->kind = (uint8_t)k;
    o->bytes = sizeof(Obj)+extra;
    o->next = allobjs; allobjs = o;
    gc_heap_bytes += o->bytes;
    gc_total_alloc += o->bytes;
    gc_mallocs++;
    if (gc_phase) { o->mark = 1; }   /* allocate black during marking */
    return o;
}

Obj *gc_alloc_cells(int n){
    Obj *o = gc_alloc_obj(OBJ_CELLS, (size_t)(n<0?0:n)*sizeof(Value));
    o->n = (uint32_t)(n<0?0:n);
    return o;
}
Obj *gc_alloc_bytes(size_t n){
    Obj *o = gc_alloc_obj(OBJ_BYTES, n+1);
    o->n = (uint32_t)n;
    o->noscan = 1;
    return o;
}

void gc_pin(Obj *o){ vec_push(&g_pinned,o); }
void gc_pin_obj(Obj *o){ if (o) vec_push(&g_pinned,o); }

static void mark_value(Value v);

static void mark_obj_children(Obj *o){
    switch (o->kind){
    case OBJ_CELLS: {
        Value *c = OBJ_CELLPTR(o);
        for (uint32_t i=0;i<o->n;i++) mark_value(c[i]);
        break;
    }
    case OBJ_MAP: map_mark((GoMap*)o->aux); break;
    case OBJ_CHAN: {
        Chan *ch = (Chan*)o->aux;
        for (int i=0;i<ch->buflen;i++) mark_value(ch->buf[(ch->bufhead+i)%ch->bufcap]);
        break;
    }
    case OBJ_FUNC: {
        Value *c = OBJ_CELLPTR(o);
        for (uint32_t i=0;i<o->n;i++) mark_value(c[i]);
        break;
    }
    default: break;
    }
}

static void mark_value(Value v){
    if (v.o) gray_push(v.o);
}

static void drain_gray(void){
    while (graylen){
        Obj *o = graystack[--graylen];
        gc_mark_work++;
        mark_obj_children(o);
    }
}

void gc_barrier(Value v){
    if (gc_phase && v.o && !v.o->mark) gray_push(v.o);
}

extern void vm_mark_roots(void);

static void mark_roots(void){
    for (int i=0;i<g_pinned.len;i++) gray_push((Obj*)g_pinned.p[i]);
    for (int i=0;i<g_packages.len;i++){
        Package *p = g_packages.p[i];
        if (p->globstore) gray_push(p->globstore);
    }
    vm_mark_roots();
    for (FinRec *f = finalizers; f; f=f->next){
        mark_value(f->fn);
        mark_value(f->arg);
    }
    for (int i=0;i<fin_queue.len;i++){
        FinRec *f = fin_queue.p[i];
        gray_push(f->obj);
        mark_value(f->fn); mark_value(f->arg);
    }
}

static uint64_t nanotime(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

void gc_collect(int forced){
    if (!gc_enabled || gc_in_progress) return;
    gc_in_progress = 1;
    uint64_t t0 = nanotime();
    gc_phase = 1;
    for (Obj *o = allobjs; o; o=o->next) o->mark = 0;
    graylen = 0;
    mark_roots();
    drain_gray();
    /* finalizers: objects reachable only from the finalizer table are resurrected once */
    for (FinRec *f = finalizers; f; f=f->next){
        if (f->obj && !f->obj->mark && !f->queued){
            f->queued = 1;
            vec_push(&fin_queue, f);
            gray_push(f->obj);
            mark_value(f->fn); mark_value(f->arg);
        }
    }
    drain_gray();
    /* sweep */
    Obj **pp = &allobjs;
    while (*pp){
        Obj *o = *pp;
        if (!o->mark){
            *pp = o->next;
            gc_heap_bytes -= o->bytes;
            gc_frees++;
            if (o->kind==OBJ_MAP && o->aux){ extern void map_free(GoMap*); map_free((GoMap*)o->aux); }
            if (o->kind==OBJ_CHAN && o->aux){ Chan *ch=(Chan*)o->aux; free(ch->buf); vec_free(&ch->sendq); vec_free(&ch->recvq); free(ch); }
            free(o);
        } else {
            pp = &o->next;
        }
    }
    gc_phase = 0;
    gc_num_gc++;
    if (forced) gc_forced_gc++;
    gc_pause_total_ns += nanotime()-t0;
    size_t target = gc_heap_bytes + gc_heap_bytes*(size_t)(gc_gogc<0?100:gc_gogc)/100;
    if (target < (2u<<20)) target = 2u<<20;
    if (gc_memlimit > 0 && target > (size_t)gc_memlimit) target = (size_t)gc_memlimit;
    gc_next_gc = target;
    if (gc_gogc < 0 && gc_memlimit <= 0) gc_next_gc = (size_t)-1;
    gc_in_progress = 0;
}

void gc_maybe(void){
    if (gc_heap_bytes >= gc_next_gc && !gc_in_progress) gc_collect(0);
}

void gc_init(void){ }

void gc_set_finalizer(Obj *o, Value fn, Type *argt){
    (void)argt;
    for (FinRec **pp=&finalizers; *pp; pp=&(*pp)->next){
        if ((*pp)->obj == o && !(*pp)->is_cleanup){
            if (fn.k==VK_NIL){ FinRec *d=*pp; *pp=d->next; free(d); return; }
            (*pp)->fn = fn; return;
        }
    }
    if (fn.k==VK_NIL) return;
    FinRec *f = xcalloc(1,sizeof *f);
    f->obj = o; f->fn = fn; f->arg = val_nil(); f->is_cleanup = 0;
    f->next = finalizers; finalizers = f;
}

void gc_add_cleanup(Obj *o, Value fn, Value arg){
    FinRec *f = xcalloc(1,sizeof *f);
    f->obj = o; f->fn = fn; f->arg = arg; f->is_cleanup = 1;
    f->next = finalizers; finalizers = f;
}

void gc_run_pending_finalizers(void){
    while (fin_queue.len){
        FinRec *f = fin_queue.p[--fin_queue.len];
        /* remove from the finalizer list */
        for (FinRec **pp=&finalizers; *pp; pp=&(*pp)->next){
            if (*pp == f){ *pp = f->next; break; }
        }
        Value args[1];
        int nargs = 0;
        if (f->is_cleanup){ args[0] = f->arg; nargs = 1; }
        else { Value pv; memset(&pv,0,sizeof pv); pv.k = VK_PTR; pv.o = f->obj; pv.off = 0; pv.t = (Type*)f->arg.t; args[0] = pv; nargs = 1; }
        Value rets[4];
        if (f->fn.k == VK_FUNC) vm_call_value(f->fn, args, nargs, rets, 4);
        free(f);
    }
}

/* ============================== values ================================== */

Value val_nil(void){ Value v; memset(&v,0,sizeof v); v.k = VK_NIL; return v; }
Value val_bool(int b){ Value v = val_nil(); v.k=VK_BOOL; v.b.i = b?1:0; v.t = ty_bool; return v; }
Value val_int(int64_t x, Type *t){ Value v = val_nil(); v.k=VK_INT; v.b.i=x; v.t=t?t:ty_int; return v; }
Value val_uint(uint64_t x, Type *t){ Value v = val_nil(); v.k=VK_UINT; v.b.u=x; v.t=t?t:ty_uint; return v; }
Value val_float(double x, Type *t){ Value v = val_nil(); v.k=VK_FLOAT; v.b.d=x; v.t=t?t:ty_float64; return v; }

Value val_string(const char *s, size_t n){
    Value v = val_nil();
    v.k = VK_STRING; v.t = ty_string;
    if (n == 0){ v.o = NULL; v.off = 0; v.len = 0; return v; }
    Obj *o = gc_alloc_bytes(n);
    memcpy(OBJ_BYTEPTR(o), s, n);
    v.o = o; v.off = 0; v.len = (int64_t)n;
    return v;
}

static Value zero_struct(Type *t){
    Type *u = underlying(t);
    Obj *o = gc_alloc_cells(u->fields.len);
    Value *c = OBJ_CELLPTR(o);
    for (int i=0;i<u->fields.len;i++){ Field *f=u->fields.p[i]; c[i] = val_zero(f->type); }
    Value v = val_nil(); v.k=VK_STRUCT; v.o=o; v.t=t;
    return v;
}

Value val_zero(Type *t){
    Type *u = underlying(t);
    Value v = val_nil();
    v.t = t;
    switch (u->k){
    case T_BOOL: case T_UNTYPED_BOOL: v.k=VK_BOOL; v.b.i=0; break;
    case T_INT: case T_INT8: case T_INT16: case T_INT32: case T_INT64:
    case T_UNTYPED_INT: case T_UNTYPED_RUNE: v.k=VK_INT; v.b.i=0; break;
    case T_UINT: case T_UINT8: case T_UINT16: case T_UINT32: case T_UINT64: case T_UINTPTR:
        v.k=VK_UINT; v.b.u=0; break;
    case T_FLOAT32: case T_FLOAT64: case T_UNTYPED_FLOAT: v.k=VK_FLOAT; v.b.d=0; break;
    case T_STRING: case T_UNTYPED_STRING: v.k=VK_STRING; v.o=NULL; v.len=0; break;
    case T_PTR: case T_UNSAFEPTR: v.k=VK_PTR; v.o=NULL; break;
    case T_SLICE: v.k=VK_SLICE; v.o=NULL; v.len=0; v.cap=0; break;
    case T_MAP: v.k=VK_MAP; v.o=NULL; break;
    case T_CHAN: v.k=VK_CHAN; v.o=NULL; break;
    case T_FUNC: v.k=VK_FUNC; v.o=NULL; break;
    case T_INTERFACE: v.k=VK_NIL; v.t=NULL; break;
    case T_STRUCT: return zero_struct(t);
    case T_ARRAY: {
        Obj *o = gc_alloc_cells((int)u->alen);
        Value *c = OBJ_CELLPTR(o);
        for (int64_t i=0;i<u->alen;i++) c[i] = val_zero(u->elem);
        v.k=VK_ARRAY; v.o=o;
        break;
    }
    case T_TYPEPARAM: v.k=VK_NIL; v.t=NULL; break;
    default: v.k=VK_NIL; break;
    }
    return v;
}

Value val_copy(Value v){
    if (v.k==VK_STRUCT && v.o){
        Obj *o = gc_alloc_cells((int)v.o->n);
        Value *src = OBJ_CELLPTR(v.o), *dst = OBJ_CELLPTR(o);
        for (uint32_t i=0;i<v.o->n;i++) dst[i] = val_copy(src[i]);
        Value r = v; r.o = o; return r;
    }
    if (v.k==VK_ARRAY && v.o){
        Obj *o = gc_alloc_cells((int)v.o->n);
        Value *src = OBJ_CELLPTR(v.o), *dst = OBJ_CELLPTR(o);
        for (uint32_t i=0;i<v.o->n;i++) dst[i] = val_copy(src[i]);
        Value r = v; r.o = o; return r;
    }
    return v;
}

int val_truth(Value v){ return v.k==VK_BOOL ? (v.b.i!=0) : (v.k!=VK_NIL); }

static const char *str_data(Value v){ return v.o ? OBJ_BYTEPTR(v.o)+v.off : ""; }

int val_equal(Value a, Value b){
    if (a.k==VK_NIL || b.k==VK_NIL){
        if (a.k==VK_NIL && b.k==VK_NIL) return 1;
        Value o = a.k==VK_NIL ? b : a;
        switch (o.k){
        case VK_PTR: case VK_SLICE: case VK_MAP: case VK_CHAN: case VK_FUNC: return o.o==NULL;
        default: return 0;
        }
    }
    if (a.k != b.k){
        /* int/uint/float mixing can occur through interfaces of different types */
        if ((a.k==VK_INT&&b.k==VK_UINT)||(a.k==VK_UINT&&b.k==VK_INT)){
            if (a.t && b.t && !type_identical(a.t,b.t)) return 0;
            return a.b.u == b.b.u;
        }
        return 0;
    }
    if (a.t && b.t && a.t != b.t && !type_identical(a.t,b.t)) {
        /* different dynamic types are never equal (interface comparison) */
        if (a.k!=VK_PTR || 1) return 0;
    }
    switch (a.k){
    case VK_BOOL: case VK_INT: return a.b.i==b.b.i;
    case VK_UINT: return a.b.u==b.b.u;
    case VK_FLOAT: return a.b.d==b.b.d;
    case VK_STRING:
        if (a.len != b.len) return 0;
        return a.len==0 || memcmp(str_data(a),str_data(b),(size_t)a.len)==0;
    case VK_PTR: return a.o==b.o && a.off==b.off;
    case VK_CHAN: case VK_MAP: return a.o==b.o;
    case VK_FUNC: return a.o==b.o;
    case VK_STRUCT: case VK_ARRAY: {
        if (!a.o || !b.o) return a.o==b.o;
        if (a.o->n != b.o->n) return 0;
        Value *x=OBJ_CELLPTR(a.o), *y=OBJ_CELLPTR(b.o);
        for (uint32_t i=0;i<a.o->n;i++) if (!val_equal(x[i],y[i])) return 0;
        return 1;
    }
    default: return 0;
    }
}

uint64_t val_hash(Value v){
    uint64_t h = 1469598103934665603ull;
#define MIX(x) do { uint64_t _t=(x); for(int _i=0;_i<8;_i++){ h ^= (_t>>(_i*8))&0xff; h *= 1099511628211ull; } } while(0)
    MIX((uint64_t)v.k);
    switch (v.k){
    case VK_BOOL: case VK_INT: MIX((uint64_t)v.b.i); break;
    case VK_UINT: MIX(v.b.u); break;
    case VK_FLOAT: { double d = v.b.d; if (d==0) d=0; uint64_t u; memcpy(&u,&d,8); MIX(u); break; }
    case VK_STRING: {
        const char *s = str_data(v);
        for (int64_t i=0;i<v.len;i++){ h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
        break;
    }
    case VK_PTR: MIX((uint64_t)(uintptr_t)v.o); MIX((uint64_t)v.off); break;
    case VK_CHAN: case VK_MAP: case VK_FUNC: MIX((uint64_t)(uintptr_t)v.o); break;
    case VK_STRUCT: case VK_ARRAY: {
        if (v.o){ Value *c = OBJ_CELLPTR(v.o); for (uint32_t i=0;i<v.o->n;i++){ uint64_t sub = val_hash(c[i]); MIX(sub); } }
        break;
    }
    default: break;
    }
    if (v.t && (v.k==VK_INT||v.k==VK_UINT||v.k==VK_FLOAT||v.k==VK_STRING||v.k==VK_BOOL)){
        const char *ts = type_string(v.t);
        for (const char *p=ts; *p; p++){ h ^= (unsigned char)*p; h *= 1099511628211ull; }
    }
    return h;
#undef MIX
}

char *val_to_cstring(Value v){
    if (v.k != VK_STRING) return xstrdup("");
    char *s = xmalloc((size_t)v.len+1);
    if (v.len) memcpy(s, str_data(v), (size_t)v.len);
    s[v.len] = 0;
    return s;
}

/* ================================ maps ================================== */

typedef struct MapEntry { Value key, val; uint64_t hash; int used; int order; } MapEntry;

struct GoMap {
    MapEntry *e;
    int cap, len, nextorder;
    Type *t;
};

GoMap *map_new(Type *t){
    GoMap *m = xcalloc(1,sizeof *m);
    m->cap = 8;
    m->e = xcalloc((size_t)m->cap,sizeof(MapEntry));
    m->t = t;
    return m;
}
void map_free(GoMap *m){ if(!m) return; free(m->e); free(m); }

static void map_grow(GoMap *m){
    int oc = m->cap;
    MapEntry *oe = m->e;
    m->cap = oc*2;
    m->e = xcalloc((size_t)m->cap,sizeof(MapEntry));
    int n = 0;
    for (int i=0;i<oc;i++){
        if (oe[i].used != 1) continue;
        uint64_t h = oe[i].hash;
        int j = (int)(h % (uint64_t)m->cap);
        while (m->e[j].used==1) j = (j+1)%m->cap;
        m->e[j] = oe[i];
        n++;
    }
    m->len = n;
    free(oe);
}

int map_get(GoMap *m, Value k, Value *out){
    if (!m) return 0;
    uint64_t h = val_hash(k);
    int j = (int)(h % (uint64_t)m->cap);
    for (int probes=0; probes<m->cap; probes++){
        if (m->e[j].used==0) return 0;
        if (m->e[j].used==1 && m->e[j].hash==h && val_equal(m->e[j].key,k)){ if(out) *out = m->e[j].val; return 1; }
        j = (j+1)%m->cap;
    }
    return 0;
}

void map_set(GoMap *m, Value k, Value v){
    if (!m) return;
    if (m->len*4 >= m->cap*3) map_grow(m);
    uint64_t h = val_hash(k);
    int j = (int)(h % (uint64_t)m->cap);
    int firstdel = -1;
    for (int probes=0; probes<m->cap; probes++){
        if (m->e[j].used==0){
            int slot = firstdel>=0?firstdel:j;
            m->e[slot].used=1; m->e[slot].key=val_copy(k); m->e[slot].val=val_copy(v); m->e[slot].hash=h;
            m->e[slot].order = m->nextorder++;
            m->len++;
            return;
        }
        if (m->e[j].used==2 && firstdel<0) firstdel = j;
        if (m->e[j].used==1 && m->e[j].hash==h && val_equal(m->e[j].key,k)){ m->e[j].val = val_copy(v); return; }
        j = (j+1)%m->cap;
    }
    map_grow(m);
    map_set(m,k,v);
}

void map_delete(GoMap *m, Value k){
    if (!m) return;
    uint64_t h = val_hash(k);
    int j = (int)(h % (uint64_t)m->cap);
    for (int probes=0; probes<m->cap; probes++){
        if (m->e[j].used==0) return;
        if (m->e[j].used==1 && m->e[j].hash==h && val_equal(m->e[j].key,k)){
            m->e[j].used = 2; m->len--;
            memset(&m->e[j].key,0,sizeof(Value)); memset(&m->e[j].val,0,sizeof(Value));
            return;
        }
        j = (j+1)%m->cap;
    }
}

int map_len(GoMap *m){ return m?m->len:0; }
void map_clear(GoMap *m){ if(!m) return; memset(m->e,0,(size_t)m->cap*sizeof(MapEntry)); m->len=0; }

static int order_cmp(const void *a, const void *b){
    const MapEntry *const *x = a, *const *y = b;
    return (*x)->order - (*y)->order;
}
void map_keys(GoMap *m, Vec *out){
    if (!m) return;
    Vec tmp; memset(&tmp,0,sizeof tmp);
    for (int i=0;i<m->cap;i++) if (m->e[i].used==1) vec_push(&tmp,&m->e[i]);
    if (tmp.len) qsort(tmp.p,(size_t)tmp.len,sizeof(void*),order_cmp);
    for (int i=0;i<tmp.len;i++){
        MapEntry *e = tmp.p[i];
        Value *kv = xmalloc(sizeof(Value)*2);
        kv[0]=e->key; kv[1]=e->val;
        vec_push(out,kv);
    }
    vec_free(&tmp);
}

void map_mark(GoMap *m){
    if (!m) return;
    for (int i=0;i<m->cap;i++){
        if (m->e[i].used!=1) continue;
        if (m->e[i].key.o) gray_push(m->e[i].key.o);
        if (m->e[i].val.o) gray_push(m->e[i].val.o);
    }
}

void gc_mark_root(Value v){ if (v.o) gray_push(v.o); }
void gc_mark_root_obj(Obj *o){ if (o) gray_push(o); }
