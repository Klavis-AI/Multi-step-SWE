#include "go.h"
#include <stdarg.h>

Type *ty_invalid, *ty_bool, *ty_int, *ty_int8, *ty_int16, *ty_int32, *ty_int64;
Type *ty_uint, *ty_uint8, *ty_uint16, *ty_uint32, *ty_uint64, *ty_uintptr;
Type *ty_float32, *ty_float64, *ty_string, *ty_unsafeptr;
Type *ty_untyped_bool, *ty_untyped_int, *ty_untyped_rune, *ty_untyped_float;
Type *ty_untyped_string, *ty_untyped_nil;
Type *ty_error, *ty_any, *ty_comparable;

Type *type_new(TKind k){ Type *t = xcalloc(1,sizeof *t); t->k=k; return t; }

static Type *basic(TKind k, const char *name){
    Type *t = type_new(k); t->name = intern(name); return t;
}

void types_init(void){
    if (ty_int) return;
    ty_invalid = basic(T_INVALID,"invalid type");
    ty_bool = basic(T_BOOL,"bool");
    ty_int = basic(T_INT,"int");
    ty_int8 = basic(T_INT8,"int8");
    ty_int16 = basic(T_INT16,"int16");
    ty_int32 = basic(T_INT32,"int32");
    ty_int64 = basic(T_INT64,"int64");
    ty_uint = basic(T_UINT,"uint");
    ty_uint8 = basic(T_UINT8,"uint8");
    ty_uint16 = basic(T_UINT16,"uint16");
    ty_uint32 = basic(T_UINT32,"uint32");
    ty_uint64 = basic(T_UINT64,"uint64");
    ty_uintptr = basic(T_UINTPTR,"uintptr");
    ty_float32 = basic(T_FLOAT32,"float32");
    ty_float64 = basic(T_FLOAT64,"float64");
    ty_string = basic(T_STRING,"string");
    ty_unsafeptr = basic(T_UNSAFEPTR,"unsafe.Pointer");
    ty_untyped_bool = basic(T_UNTYPED_BOOL,"untyped bool");
    ty_untyped_int = basic(T_UNTYPED_INT,"untyped int");
    ty_untyped_rune = basic(T_UNTYPED_RUNE,"untyped rune");
    ty_untyped_float = basic(T_UNTYPED_FLOAT,"untyped float");
    ty_untyped_string = basic(T_UNTYPED_STRING,"untyped string");
    ty_untyped_nil = basic(T_UNTYPED_NIL,"untyped nil");

    /* error interface */
    Type *ifc = type_new(T_INTERFACE);
    Method *m = xcalloc(1,sizeof *m);
    m->name = intern("Error");
    Type *sig = type_new(T_FUNC);
    Field *r = xcalloc(1,sizeof *r); r->type = ty_string;
    vec_push(&sig->results, r);
    m->sig = sig;
    vec_push(&ifc->fields, m);
    ty_error = type_new(T_NAMED);
    ty_error->name = intern("error");
    ty_error->pkg = intern("");
    ty_error->under = ifc;

    ty_any = type_new(T_INTERFACE);

    ty_comparable = type_new(T_NAMED);
    ty_comparable->name = intern("comparable");
    ty_comparable->pkg = intern("");
    Type *cif = type_new(T_INTERFACE);
    cif->comparable_req = 1;
    ty_comparable->under = cif;
}

Type *type_ptr(Type *e){ Type *t=type_new(T_PTR); t->elem=e; return t; }
Type *type_slice(Type *e){ Type *t=type_new(T_SLICE); t->elem=e; return t; }
Type *type_array(Type *e, int64_t n){ Type *t=type_new(T_ARRAY); t->elem=e; t->alen=n; return t; }
Type *type_map(Type *k, Type *v){ Type *t=type_new(T_MAP); t->key=k; t->elem=v; return t; }
Type *type_chan(Type *e, int dir){ Type *t=type_new(T_CHAN); t->elem=e; t->chandir=dir; return t; }

Type *underlying(Type *t){
    if (!t) return ty_invalid;
    int guard = 0;
    while (t->k == T_NAMED){ t = t->under ? t->under : ty_invalid; if (++guard > 100) return ty_invalid; }
    return t;
}

int is_untyped(Type *t){
    if (!t) return 0;
    switch (t->k){
    case T_UNTYPED_BOOL: case T_UNTYPED_INT: case T_UNTYPED_RUNE:
    case T_UNTYPED_FLOAT: case T_UNTYPED_STRING: case T_UNTYPED_NIL: return 1;
    default: return 0;
    }
}
int is_typeparam(Type *t){ return t && t->k == T_TYPEPARAM; }
int is_iface(Type *t){ Type *u = underlying(t); return u && u->k == T_INTERFACE; }

int is_integer(Type *t){
    Type *u = underlying(t);
    switch (u->k){
    case T_INT: case T_INT8: case T_INT16: case T_INT32: case T_INT64:
    case T_UINT: case T_UINT8: case T_UINT16: case T_UINT32: case T_UINT64:
    case T_UINTPTR: case T_UNTYPED_INT: case T_UNTYPED_RUNE: return 1;
    default: return 0;
    }
}
int is_unsigned(Type *t){
    Type *u = underlying(t);
    switch (u->k){
    case T_UINT: case T_UINT8: case T_UINT16: case T_UINT32: case T_UINT64: case T_UINTPTR: return 1;
    default: return 0;
    }
}
int is_float(Type *t){
    Type *u = underlying(t);
    return u->k==T_FLOAT32||u->k==T_FLOAT64||u->k==T_UNTYPED_FLOAT;
}
int is_numeric(Type *t){ return is_integer(t)||is_float(t); }
int is_string(Type *t){ Type *u=underlying(t); return u->k==T_STRING||u->k==T_UNTYPED_STRING; }
int is_boolean(Type *t){ Type *u=underlying(t); return u->k==T_BOOL||u->k==T_UNTYPED_BOOL; }

Type *default_type(Type *t){
    if (!t) return t;
    switch (t->k){
    case T_UNTYPED_BOOL: return ty_bool;
    case T_UNTYPED_INT: return ty_int;
    case T_UNTYPED_RUNE: return ty_int32;
    case T_UNTYPED_FLOAT: return ty_float64;
    case T_UNTYPED_STRING: return ty_string;
    default: return t;
    }
}

/* ------------------------------------------------------------ identity --- */

static int fields_identical(Vec *a, Vec *b);

int type_identical(Type *a, Type *b){
    if (a==b) return 1;
    if (!a||!b) return 0;
    if (a->k==T_NAMED || b->k==T_NAMED){
        if (a->k!=b->k) return 0;
        if (a->name != b->name) return 0;
        if (a->pkg != b->pkg) return 0;
        if (a->targs.len != b->targs.len) return 0;
        for (int i=0;i<a->targs.len;i++) if (!type_identical(a->targs.p[i], b->targs.p[i])) return 0;
        return 1;
    }
    if (a->k != b->k) return 0;
    switch (a->k){
    case T_PTR: case T_SLICE: return type_identical(a->elem,b->elem);
    case T_ARRAY: return a->alen==b->alen && type_identical(a->elem,b->elem);
    case T_MAP: return type_identical(a->key,b->key) && type_identical(a->elem,b->elem);
    case T_CHAN: return a->chandir==b->chandir && type_identical(a->elem,b->elem);
    case T_STRUCT: {
        if (a->fields.len != b->fields.len) return 0;
        for (int i=0;i<a->fields.len;i++){
            Field *x=a->fields.p[i], *y=b->fields.p[i];
            if (x->name != y->name) return 0;
            if (x->embedded != y->embedded) return 0;
            if ((x->tag?x->tag:"") != (y->tag?y->tag:"") && strcmp(x->tag?x->tag:"", y->tag?y->tag:"")) return 0;
            if (!type_identical(x->type,y->type)) return 0;
        }
        return 1;
    }
    case T_FUNC: {
        if (a->variadic != b->variadic) return 0;
        if (!fields_identical(&a->fields,&b->fields)) return 0;
        if (!fields_identical(&a->results,&b->results)) return 0;
        return 1;
    }
    case T_INTERFACE: {
        if (a->fields.len != b->fields.len) return 0;
        for (int i=0;i<a->fields.len;i++){
            Method *x=a->fields.p[i];
            int found=0;
            for (int j=0;j<b->fields.len;j++){
                Method *y=b->fields.p[j];
                if (x->name==y->name && type_identical(x->sig,y->sig)){ found=1; break; }
            }
            if (!found) return 0;
        }
        if (a->terms.len != b->terms.len) return 0;
        return 1;
    }
    case T_TYPEPARAM: return a==b;
    case T_TUPLE: {
        if (a->tuple.len!=b->tuple.len) return 0;
        for (int i=0;i<a->tuple.len;i++) if (!type_identical(a->tuple.p[i],b->tuple.p[i])) return 0;
        return 1;
    }
    default: return a->k==b->k;
    }
}

static int fields_identical(Vec *a, Vec *b){
    if (a->len != b->len) return 0;
    for (int i=0;i<a->len;i++){
        Field *x=a->p[i], *y=b->p[i];
        if (!type_identical(x->type,y->type)) return 0;
    }
    return 1;
}

/* -------------------------------------------------------------- methods --- */

static Method *find_method_direct(Type *t, const char *name){
    if (!t) return NULL;
    if (t->k==T_NAMED){
        for (int i=0;i<t->methods.len;i++){
            Method *m = t->methods.p[i];
            if (m->name == name) return m;
        }
    }
    Type *u = underlying(t);
    if (u->k==T_INTERFACE){
        for (int i=0;i<u->fields.len;i++){
            Method *m = u->fields.p[i];
            if (m->name == name) return m;
        }
    }
    return NULL;
}

/* Search method sets breadth-first over embedded fields. */
Method *lookup_method(Type *t, const char *name, int *indirect){
    if (indirect) *indirect = 0;
    if (!t) return NULL;
    Type *base = t;
    int ptr = 0;
    if (underlying(base)->k==T_PTR && base->k != T_NAMED){ base = underlying(base)->elem; ptr = 1; }
    else if (base->k==T_PTR){ base = base->elem; ptr = 1; }
    Method *m = find_method_direct(base,name);
    if (m){ if (indirect) *indirect = ptr; return m; }
    if (is_typeparam(base) && base->constraint){
        m = find_method_direct(base->constraint,name);
        if (m) { if (indirect) *indirect = ptr; return m; }
        Type *cu = underlying(base->constraint);
        if (cu && cu->k==T_INTERFACE){
            for (int i=0;i<cu->fields.len;i++){ Method *mm = cu->fields.p[i]; if (mm->name==name) return mm; }
        }
    }
    /* embedded */
    Type *u = underlying(base);
    if (u->k==T_STRUCT){
        for (int i=0;i<u->fields.len;i++){
            Field *f = u->fields.p[i];
            if (!f->embedded) continue;
            int sub = 0;
            Method *em = lookup_method(f->type,name,&sub);
            if (em){
                if (indirect) *indirect = ptr || sub || underlying(f->type)->k==T_PTR || f->type->k==T_PTR;
                return em;
            }
        }
    }
    return NULL;
}

Field *lookup_field(Type *t, const char *name, Vec *path){
    if (!t) return NULL;
    Type *base = t;
    if (base->k==T_PTR) base = base->elem;
    else if (underlying(base)->k==T_PTR) base = underlying(base)->elem;
    Type *u = underlying(base);
    if (u->k != T_STRUCT) return NULL;
    for (int i=0;i<u->fields.len;i++){
        Field *f = u->fields.p[i];
        if (f->name == name){
            if (path){ int *idx = xmalloc(sizeof(int)); *idx=i; vec_push(path,idx); }
            return f;
        }
    }
    for (int i=0;i<u->fields.len;i++){
        Field *f = u->fields.p[i];
        if (!f->embedded) continue;
        Vec sub; memset(&sub,0,sizeof sub);
        Field *ef = lookup_field(f->type,name,&sub);
        if (ef){
            if (path){
                int *idx = xmalloc(sizeof(int)); *idx=i; vec_push(path,idx);
                for (int j=0;j<sub.len;j++) vec_push(path,sub.p[j]);
            }
            vec_free(&sub);
            return ef;
        }
        vec_free(&sub);
    }
    return NULL;
}

int iface_satisfied(Type *t, Type *iface, const char **missing){
    Type *u = underlying(iface);
    if (!u || u->k != T_INTERFACE) return 1;
    if (u->comparable_req && !type_comparable(t)){ if (missing) *missing = intern("comparable"); return 0; }
    for (int i=0;i<u->fields.len;i++){
        Method *want = u->fields.p[i];
        int indirect = 0;
        Method *got = lookup_method(t,want->name,&indirect);
        if (!got){ if (missing) *missing = want->name; return 0; }
        if (!type_identical(got->sig, want->sig)){ if (missing) *missing = want->name; return 0; }
        /* pointer-receiver methods are in the method set of *T and of any type
         * that reaches the receiver through a pointer (embedded *T). */
        if (got->ptr_recv && !indirect && t->k != T_PTR && !(underlying(t)->k==T_PTR)
            && !is_iface(t) && !is_typeparam(t)){
            if (missing) *missing = want->name;
            return 0;
        }
    }
    if (u->terms.len){
        Type *tu = underlying(t);
        int ok = 0;
        for (int i=0;i<u->terms.len;i++){
            TypeTerm *tt = u->terms.p[i];
            if (tt->tilde){ if (type_identical(underlying(tt->t), tu)) { ok=1; break; } }
            else if (type_identical(tt->t, t)) { ok=1; break; }
        }
        if (!ok){ if (missing) *missing = intern("type set"); return 0; }
    }
    return 1;
}

/* ----------------------------------------------------------- assignable --- */

int type_comparable(Type *t){
    Type *u = underlying(t);
    switch (u->k){
    case T_SLICE: case T_MAP: case T_FUNC: return 0;
    case T_STRUCT:
        for (int i=0;i<u->fields.len;i++){ Field *f=u->fields.p[i]; if (!type_comparable(f->type)) return 0; }
        return 1;
    case T_ARRAY: return type_comparable(u->elem);
    case T_TYPEPARAM: {
        if (u->constraint){ Type *cu = underlying(u->constraint); if (cu && cu->k==T_INTERFACE && cu->comparable_req) return 1;
            if (cu && cu->k==T_INTERFACE && cu->terms.len){
                for (int i=0;i<cu->terms.len;i++){ TypeTerm *tt=cu->terms.p[i]; if (!type_comparable(tt->t)) return 0; }
                return 1;
            }
        }
        return 0;
    }
    case T_INVALID: return 1;
    default: return 1;
    }
}
int type_ordered(Type *t){
    Type *u = underlying(t);
    if (u->k==T_TYPEPARAM){
        Type *cu = u->constraint ? underlying(u->constraint) : NULL;
        if (cu && cu->k==T_INTERFACE && cu->terms.len){
            for (int i=0;i<cu->terms.len;i++){ TypeTerm *tt=cu->terms.p[i]; if (!type_ordered(tt->t)) return 0; }
            return 1;
        }
        return 0;
    }
    return is_numeric(u) || is_string(u);
}

int type_assignable(Type *src, Type *dst){
    if (!src||!dst) return 1;
    if (src->k==T_INVALID||dst->k==T_INVALID) return 1;
    if (type_identical(src,dst)) return 1;
    Type *su = underlying(src), *du = underlying(dst);
    /* untyped */
    if (is_untyped(src)){
        if (src->k==T_UNTYPED_NIL){
            switch (du->k){
            case T_PTR: case T_SLICE: case T_MAP: case T_CHAN: case T_FUNC:
            case T_INTERFACE: case T_UNSAFEPTR: return 1;
            case T_TYPEPARAM: return 0;
            default: return 0;
            }
        }
        if (du->k==T_INTERFACE) return iface_satisfied(default_type(src),dst,NULL);
        if (du->k==T_TYPEPARAM) {
            Type *cu = du->constraint?underlying(du->constraint):NULL;
            if (cu && cu->terms.len){
                for (int i=0;i<cu->terms.len;i++){ TypeTerm *tt=cu->terms.p[i]; if (!type_assignable(src,tt->t)) return 0; }
                return 1;
            }
            return 0;
        }
        switch (src->k){
        case T_UNTYPED_BOOL: return is_boolean(dst);
        case T_UNTYPED_INT: case T_UNTYPED_RUNE: return is_numeric(dst);
        case T_UNTYPED_FLOAT: return is_float(dst) || is_integer(dst);
        case T_UNTYPED_STRING: return is_string(dst);
        default: return 0;
        }
    }
    /* identical underlying, at least one not named */
    if ((src->k != T_NAMED || dst->k != T_NAMED) && !is_typeparam(src) && !is_typeparam(dst)
        && type_identical(su,du)) return 1;
    /* interface satisfaction */
    if (du->k==T_INTERFACE) return iface_satisfied(src,dst,NULL);
    /* channel assignment */
    if (su->k==T_CHAN && du->k==T_CHAN && type_identical(su->elem,du->elem)
        && su->chandir==0 && (src->k!=T_NAMED||dst->k!=T_NAMED)) return 1;
    return 0;
}

int type_convertible(Type *src, Type *dst){
    if (!src||!dst) return 1;
    if (src->k==T_INVALID||dst->k==T_INVALID) return 1;
    if (type_assignable(src,dst)) return 1;
    Type *su = underlying(src), *du = underlying(dst);
    if (type_identical(su,du)) return 1;
    if (su->k==T_PTR && du->k==T_PTR && type_identical(underlying(su->elem),underlying(du->elem))) return 1;
    if (is_numeric(su) && is_numeric(du)) return 1;
    if (is_integer(su) && du->k==T_STRING) return 1;
    if (su->k==T_STRING && du->k==T_SLICE){
        Type *e = underlying(du->elem);
        if (e->k==T_UINT8||e->k==T_INT32) return 1;
    }
    if (du->k==T_STRING && su->k==T_SLICE){
        Type *e = underlying(su->elem);
        if (e->k==T_UINT8||e->k==T_INT32) return 1;
    }
    if (su->k==T_SLICE && du->k==T_PTR && underlying(du->elem)->k==T_ARRAY
        && type_identical(su->elem, underlying(du->elem)->elem)) return 1;
    if (su->k==T_SLICE && du->k==T_ARRAY && type_identical(su->elem,du->elem)) return 1;
    if (su->k==T_UNSAFEPTR && (du->k==T_PTR||du->k==T_UINTPTR||du->k==T_UNSAFEPTR)) return 1;
    if (du->k==T_UNSAFEPTR && (su->k==T_PTR||su->k==T_UINTPTR||su->k==T_UNSAFEPTR)) return 1;
    if (is_untyped(src)) return 0;
    return 0;
}

/* --------------------------------------------------------------- layout --- */

int64_t type_align(Type *t){
    Type *u = underlying(t);
    switch (u->k){
    case T_BOOL: case T_INT8: case T_UINT8: return 1;
    case T_INT16: case T_UINT16: return 2;
    case T_INT32: case T_UINT32: case T_FLOAT32: return 4;
    case T_ARRAY: return type_align(u->elem);
    case T_STRUCT: {
        int64_t a = 1;
        for (int i=0;i<u->fields.len;i++){ Field *f=u->fields.p[i]; int64_t fa = type_align(f->type); if (fa>a) a=fa; }
        return a;
    }
    default: return 8;
    }
}

int64_t type_size(Type *t){
    Type *u = underlying(t);
    if (u->size_cached) return u->size;
    int64_t s = 0;
    switch (u->k){
    case T_BOOL: case T_INT8: case T_UINT8: s=1; break;
    case T_INT16: case T_UINT16: s=2; break;
    case T_INT32: case T_UINT32: case T_FLOAT32: s=4; break;
    case T_INT: case T_UINT: case T_UINTPTR: case T_INT64: case T_UINT64:
    case T_FLOAT64: case T_PTR: case T_UNSAFEPTR: case T_CHAN: case T_MAP: case T_FUNC: s=8; break;
    case T_STRING: s=16; break;
    case T_INTERFACE: s=16; break;
    case T_SLICE: s=24; break;
    case T_ARRAY: s = u->alen * type_size(u->elem); break;
    case T_STRUCT: {
        int64_t off = 0, maxa = 1;
        for (int i=0;i<u->fields.len;i++){
            Field *f = u->fields.p[i];
            int64_t a = type_align(f->type);
            if (a > maxa) maxa = a;
            if (off % a) off += a - (off % a);
            off += type_size(f->type);
        }
        if (off % maxa) off += maxa - (off % maxa);
        s = off;
        break;
    }
    default: s = 8; break;
    }
    u->size_cached = 1; u->size = s;
    return s;
}

/* ------------------------------------------------------------- printing --- */

static void type_str_buf(Buf *b, Type *t, int depth);

static void sig_str(Buf *b, Type *t, int depth){
    buf_putc(b,'(');
    for (int i=0;i<t->fields.len;i++){
        Field *f = t->fields.p[i];
        if (i) buf_puts(b,", ");
        if (t->variadic && i==t->fields.len-1){ buf_puts(b,"..."); type_str_buf(b, underlying(f->type)->elem?underlying(f->type)->elem:f->type, depth+1); }
        else type_str_buf(b,f->type,depth+1);
    }
    buf_putc(b,')');
    if (t->results.len==1){ buf_putc(b,' '); type_str_buf(b,((Field*)t->results.p[0])->type,depth+1); }
    else if (t->results.len>1){
        buf_puts(b," (");
        for (int i=0;i<t->results.len;i++){ if(i) buf_puts(b,", "); type_str_buf(b,((Field*)t->results.p[i])->type,depth+1); }
        buf_putc(b,')');
    }
}

static void type_str_buf(Buf *b, Type *t, int depth){
    if (!t){ buf_puts(b,"<nil>"); return; }
    if (depth > 12){ buf_puts(b,"..."); return; }
    switch (t->k){
    case T_NAMED:
        if (t->pkg && *t->pkg){
            const char *slash = strrchr(t->pkg,'/');
            buf_printf(b,"%s.",slash?slash+1:t->pkg);
        }
        buf_puts(b,t->name);
        if (t->targs.len){
            buf_putc(b,'[');
            for (int i=0;i<t->targs.len;i++){ if(i) buf_putc(b,','); type_str_buf(b,t->targs.p[i],depth+1); }
            buf_putc(b,']');
        }
        return;
    case T_TYPEPARAM: buf_puts(b,t->name?t->name:"T"); return;
    case T_PTR: buf_putc(b,'*'); type_str_buf(b,t->elem,depth+1); return;
    case T_SLICE: buf_puts(b,"[]"); type_str_buf(b,t->elem,depth+1); return;
    case T_ARRAY: buf_printf(b,"[%lld]",(long long)t->alen); type_str_buf(b,t->elem,depth+1); return;
    case T_MAP: buf_puts(b,"map["); type_str_buf(b,t->key,depth+1); buf_putc(b,']'); type_str_buf(b,t->elem,depth+1); return;
    case T_CHAN:
        if (t->chandir==2) buf_puts(b,"<-chan ");
        else if (t->chandir==1) buf_puts(b,"chan<- ");
        else buf_puts(b,"chan ");
        type_str_buf(b,t->elem,depth+1); return;
    case T_FUNC: buf_puts(b,"func"); sig_str(b,t,depth); return;
    case T_STRUCT: {
        buf_puts(b,"struct {");
        for (int i=0;i<t->fields.len;i++){
            Field *f = t->fields.p[i];
            buf_printf(b,"%s%s ", i?";":"", f->name?f->name:"");
            type_str_buf(b,f->type,depth+1);
        }
        buf_puts(b,"}");
        return;
    }
    case T_INTERFACE: {
        if (t->fields.len==0 && t->terms.len==0 && !t->comparable_req){ buf_puts(b,"interface {}"); return; }
        buf_puts(b,"interface {");
        for (int i=0;i<t->fields.len;i++){
            Method *m = t->fields.p[i];
            buf_printf(b,"%s%s",i?"; ":" ",m->name);
            sig_str(b,m->sig,depth+1);
        }
        buf_puts(b," }");
        return;
    }
    case T_TUPLE: {
        buf_putc(b,'(');
        for (int i=0;i<t->tuple.len;i++){ if(i) buf_puts(b,", "); type_str_buf(b,t->tuple.p[i],depth+1); }
        buf_putc(b,')');
        return;
    }
    default:
        buf_puts(b,t->name?t->name:"?");
        return;
    }
}

const char *type_string(Type *t){
    Buf b; buf_init(&b);
    type_str_buf(&b,t,0);
    const char *s = intern(b.p?b.p:"");
    buf_free(&b);
    return s;
}

/* ---------------------------------------------------------------- core --- */

Type *core_type(Type *t){
    if (!t) return NULL;
    if (!is_typeparam(t)) return underlying(t);
    Type *c = t->constraint;
    if (!c) return NULL;
    Type *cu = underlying(c);
    if (!cu || cu->k != T_INTERFACE) return NULL;
    if (cu->terms.len == 0) return NULL;
    Type *first = NULL;
    int allchan = 1, chandir = 0;
    for (int i=0;i<cu->terms.len;i++){
        TypeTerm *tt = cu->terms.p[i];
        Type *u = underlying(tt->t);
        if (!first) first = u;
        else if (!type_identical(first,u)){
            if (!(allchan && first->k==T_CHAN && u->k==T_CHAN && type_identical(first->elem,u->elem))) return NULL;
        }
        if (u->k != T_CHAN) allchan = 0; else chandir |= u->chandir;
    }
    (void)chandir;
    return first;
}

/* ------------------------------------------------------- substitution --- */

static Type *subst(Type *t, Vec *tps, Vec *targs, Vec *seen);

static Type *subst_lookup(Type *t, Vec *tps, Vec *targs){
    for (int i=0;i<tps->len;i++){
        Type *tp = tps->p[i];
        if (tp == t) return i < targs->len ? targs->p[i] : t;
    }
    return NULL;
}

static Type *subst(Type *t, Vec *tps, Vec *targs, Vec *seen){
    if (!t) return t;
    if (t->k == T_TYPEPARAM){
        Type *r = subst_lookup(t,tps,targs);
        return r ? r : t;
    }
    switch (t->k){
    case T_PTR: { Type *e = subst(t->elem,tps,targs,seen); return e==t->elem ? t : type_ptr(e); }
    case T_SLICE: { Type *e = subst(t->elem,tps,targs,seen); return e==t->elem ? t : type_slice(e); }
    case T_ARRAY: { Type *e = subst(t->elem,tps,targs,seen); return e==t->elem ? t : type_array(e,t->alen); }
    case T_MAP: {
        Type *k = subst(t->key,tps,targs,seen), *e = subst(t->elem,tps,targs,seen);
        return (k==t->key&&e==t->elem) ? t : type_map(k,e);
    }
    case T_CHAN: { Type *e = subst(t->elem,tps,targs,seen); return e==t->elem ? t : type_chan(e,t->chandir); }
    case T_FUNC: {
        int changed = 0;
        Type *n = type_new(T_FUNC);
        n->variadic = t->variadic;
        for (int i=0;i<t->fields.len;i++){
            Field *f = t->fields.p[i];
            Field *g = xcalloc(1,sizeof *g);
            *g = *f; g->type = subst(f->type,tps,targs,seen);
            if (g->type != f->type) changed = 1;
            vec_push(&n->fields,g);
        }
        for (int i=0;i<t->results.len;i++){
            Field *f = t->results.p[i];
            Field *g = xcalloc(1,sizeof *g);
            *g = *f; g->type = subst(f->type,tps,targs,seen);
            if (g->type != f->type) changed = 1;
            vec_push(&n->results,g);
        }
        return changed ? n : t;
    }
    case T_STRUCT: {
        int changed = 0;
        Type *n = type_new(T_STRUCT);
        for (int i=0;i<t->fields.len;i++){
            Field *f = t->fields.p[i];
            Field *g = xcalloc(1,sizeof *g);
            *g = *f; g->type = subst(f->type,tps,targs,seen);
            if (g->type != f->type) changed = 1;
            vec_push(&n->fields,g);
        }
        return changed ? n : t;
    }
    case T_INTERFACE: {
        int changed = 0;
        Type *n = type_new(T_INTERFACE);
        n->comparable_req = t->comparable_req;
        for (int i=0;i<t->fields.len;i++){
            Method *m = t->fields.p[i];
            Method *g = xcalloc(1,sizeof *g);
            *g = *m; g->sig = subst(m->sig,tps,targs,seen);
            if (g->sig != m->sig) changed = 1;
            vec_push(&n->fields,g);
        }
        for (int i=0;i<t->terms.len;i++){
            TypeTerm *tt = t->terms.p[i];
            TypeTerm *g = xcalloc(1,sizeof *g);
            *g = *tt; g->t = subst(tt->t,tps,targs,seen);
            if (g->t != tt->t) changed = 1;
            vec_push(&n->terms,g);
        }
        return changed ? n : t;
    }
    case T_NAMED: {
        if (t->targs.len){
            int changed = 0;
            Vec na; memset(&na,0,sizeof na);
            for (int i=0;i<t->targs.len;i++){
                Type *a = subst(t->targs.p[i],tps,targs,seen);
                if (a != t->targs.p[i]) changed = 1;
                vec_push(&na,a);
            }
            if (!changed){ vec_free(&na); return t; }
            extern Type *instantiate_named_pub(Type *origin, Vec *targs);
            Type *r = instantiate_named_pub(t->targs_of ? t->targs_of : t, &na);
            return r;
        }
        if (t->tparams.len == 0) return t;
        return t;
    }
    default: return t;
    }
}

Type *type_subst(Type *t, Vec *tps, Vec *targs){
    if (!tps || tps->len==0) return t;
    Vec seen; memset(&seen,0,sizeof seen);
    Type *r = subst(t,tps,targs,&seen);
    vec_free(&seen);
    return r;
}
