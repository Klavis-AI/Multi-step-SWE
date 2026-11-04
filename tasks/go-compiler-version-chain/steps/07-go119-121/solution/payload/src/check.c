#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <stdarg.h>
#include <math.h>

/* ===================== annotations attached to AST nodes ================= */

enum { M_INVALID, M_VALUE, M_CONST, M_TYPE, M_BUILTIN, M_VOID, M_TUPLE, M_PKG, M_MAPINDEX, M_COMMAOK };

typedef struct Ann {
    Type *t;
    Const *c;
    int mode;
    Sym *sym;
    int depth, slot;
    Package *pkg;
    Method *method;
    Field *field;
    Vec path;
    FuncDef *fn;
    int builtin;
    int isglobal;
    int addressable;
    int iface_call;
    Type *elemt;
    void *aux;
    int flag2;
    int nilcmp;
    int litslot;
} Ann;

enum { M_INVALID_FWD = 0 };
static void ann_reset_tree(Node *n);
static void ann_reset_tree(Node *n){
    if (!n) return;
    if (n->xtra){ Ann *a = (Ann*)n->xtra; memset(a,0,sizeof *a); a->mode = M_INVALID; }
    ann_reset_tree(n->x); ann_reset_tree(n->y); ann_reset_tree(n->z); ann_reset_tree(n->w);
    for (int i=0;i<n->list.len;i++) ann_reset_tree(n->list.p[i]);
    for (int i=0;i<n->list2.len;i++) ann_reset_tree(n->list2.p[i]);
}

static Ann *ann(Node *n){
    if (!n->xtra){ Ann *a = xcalloc(1,sizeof *a); a->mode = M_INVALID; n->xtra = a; }
    return (Ann*)n->xtra;
}

/* ============================= compile state ============================= */

typedef struct CEnv { struct CEnv *parent; int nslots; int level; } CEnv;

typedef struct Scope {
    struct Scope *parent;
    Vec names;     /* of Sym* */
    CEnv *env;
} Scope;

typedef struct LoopCtx {
    struct LoopCtx *parent;
    const char *label;
    Vec breaks, continues;   /* of int* (patch positions) */
    int is_switch;
    int is_select;
    int has_post;
    int envlevel;            /* env level the body runs at */
} LoopCtx;

typedef struct Checker {
    Package *pkg;
    Scope *scope;
    CEnv *env;
    FuncDef *fn;
    Type *sigresults;
    LoopCtx *loop;
    CEnv *fnenv;
    Node *file;
    int lang;
    const char *file_name;
    Vec deferred;
    int in_defer_args;
    Vec labels;
    int iota_value;
    int has_iota;
    Vec tp_names;   /* Sym* for type params in scope */
} Checker;

static Checker C;

static void cerrorf(Pos p, const char *fmt, ...){
    va_list ap; va_start(ap,fmt);
    char buf[512];
    vsnprintf(buf,sizeof buf,fmt,ap);
    va_end(ap);
    errorf(p,"%s",buf);
}

/* --------------------------------------------------------------- emit --- */

static int emit(int op, int a, int b, void *p, int line){
    FuncDef *f = C.fn;
    if (!f) return -1;
    if (f->ncode == f->codecap){
        f->codecap = f->codecap ? f->codecap*2 : 64;
        f->code = xrealloc(f->code,(size_t)f->codecap*sizeof(Instr));
    }
    Instr *in = &f->code[f->ncode++];
    memset(in,0,sizeof *in);
    in->op = (uint16_t)op; in->a=a; in->b=b; in->p=p; in->line=line;
    return f->ncode-1;
}
#define E(op,a,b,p,n) emit(op,a,b,p,((Node*)(n))?((Node*)(n))->pos.line:0)
static int here(void){ return C.fn->ncode; }
static void patch(int pos, int target){ if (pos>=0) C.fn->code[pos].a = target; }

static int addconst(Value v){
    FuncDef *f = C.fn;
    Value *pv = xmalloc(sizeof(Value));
    *pv = v;
    vec_push(&f->consts,pv);
    return f->consts.len-1;
}

/* --------------------------------------------------------------- scopes --- */

static Scope *scope_push(Scope *parent){
    Scope *s = xcalloc(1,sizeof *s);
    s->parent = parent;
    s->env = C.env;
    return s;
}
static Sym *scope_find(Scope *s, const char *name){
    for (; s; s = s->parent)
        for (int i=s->names.len-1;i>=0;i--){
            Sym *sym = s->names.p[i];
            if (sym->name == name) return sym;
        }
    return NULL;
}
static Sym *scope_find_local(Scope *s, const char *name){
    for (int i=s->names.len-1;i>=0;i--){ Sym *sym = s->names.p[i]; if (sym->name==name) return sym; }
    return NULL;
}

static Sym *declare_var(const char *name, Type *t, Pos pos){
    Sym *s = xcalloc(1,sizeof *s);
    s->name = name; s->kind = SYM_VAR; s->type = t; s->pos = pos;
    s->globidx = C.env->nslots++;
    s->builtin_id = C.env->level;
    vec_push(&C.scope->names,s);
    if (C.fn && C.env->nslots > C.fn->nlocals && C.env == (CEnv*)C.fn->tparams.p) { }
    return s;
}

/* track the max slots for the function env */
static CEnv *cenv_new(CEnv *parent){
    CEnv *e = xcalloc(1,sizeof *e);
    e->parent = parent;
    e->level = parent ? parent->level+1 : 0;
    return e;
}

/* ------------------------------------------------------- type resolution --- */

static Type *resolve_type(Node *n);
static void check_expr(Node *n, Type *hint);
static void emit_expr(Node *n);
static void check_stmt(Node *n);
static void emit_stmt(Node *n);
static Type *check_type_expr(Node *n);

static Sym *lookup_sym(const char *name, Pos pos, int quiet){
    Sym *s = scope_find(C.scope,name);
    if (s) return s;
    if (!quiet) cerrorf(pos,"undefined: %s",name);
    return NULL;
}

static Type *named_type_of(Sym *s);
static Type *tp_lookup(const char *name);

static Type *type_from_ident(Node *n){
    Type *tp = tp_lookup(n->name);
    if (tp){ ann(n)->mode = M_TYPE; ann(n)->t = tp; return tp; }
    Sym *s = lookup_sym(n->name,n->pos,0);
    if (!s) return ty_invalid;
    if (s->kind != SYM_TYPE){ cerrorf(n->pos,"%s is not a type",n->name); return ty_invalid; }
    ann(n)->sym = s;
    return named_type_of(s);
}

static Type *instantiate_named(Type *origin, Vec *targs);

static Type *resolve_type(Node *n){
    if (!n) return ty_invalid;
    switch (n->kind){
    case N_IDENT: return type_from_ident(n);
    case N_PAREN: return resolve_type(n->x);
    case N_STAR: return type_ptr(resolve_type(n->x));
    case N_SELECTOR: {
        /* pkg.Type */
        if (n->x->kind==N_IDENT){
            Sym *ps = scope_find(C.scope,n->x->name);
            if (ps && ps->kind==SYM_PKG){
                Package *p = (Package*)ps->type;
                ps->used = 1;
                if (p->inited == -1) return ty_invalid;
                Sym *ts = pkg_sym(p,n->name);
                if (!ts){ cerrorf(n->pos,"undefined: %s.%s",n->x->name,n->name); return ty_invalid; }
                if (ts->kind != SYM_TYPE){ cerrorf(n->pos,"%s.%s is not a type",n->x->name,n->name); return ty_invalid; }
                return named_type_of(ts);
            }
        }
        cerrorf(n->pos,"not a type");
        return ty_invalid;
    }
    case N_ARRAYTYPE: {
        Type *e = resolve_type(n->y);
        if (!n->x){ if (n->flag) return type_array(e,-1); return type_slice(e); }
        check_expr(n->x,ty_int);
        Ann *a = ann(n->x);
        int64_t len = 0;
        if (a->mode==M_CONST && a->c){
            Const *ic = NULL;
            if (const_representable(a->c,ty_int,&ic)) { int ok; len = big_to_i64(&ic->i,&ok); }
            else cerrorf(n->x->pos,"array length must be an integer constant");
            if (len < 0){ cerrorf(n->x->pos,"invalid array length"); len = 0; }
        } else {
            cerrorf(n->x->pos,"array length must be a constant");
        }
        return type_array(e,len);
    }
    case N_MAPTYPE: {
        Type *k = resolve_type(n->x), *v = resolve_type(n->y);
        if (!type_comparable(k) && !is_typeparam(k) && underlying(k)->k!=T_INTERFACE)
            cerrorf(n->pos,"invalid map key type %s",type_string(k));
        return type_map(k,v);
    }
    case N_CHANTYPE: return type_chan(resolve_type(n->x), n->flag);
    case N_STRUCTTYPE: {
        Type *t = type_new(T_STRUCT);
        for (int i=0;i<n->list.len;i++){
            Node *fn_ = n->list.p[i];
            Type *ft = resolve_type(fn_->x);
            if (fn_->flag){
                Node *tn = fn_->x;
                const char *nm = NULL;
                if (tn->kind==N_STAR) tn = tn->x;
                if (tn->kind==N_INDEXLIST) tn = tn->x;
                if (tn->kind==N_IDENT) nm = tn->name;
                else if (tn->kind==N_SELECTOR) nm = tn->name;
                Field *f = xcalloc(1,sizeof *f);
                f->name = nm?nm:intern("?"); f->type = ft; f->embedded = 1; f->tag = fn_->name; f->pos = fn_->pos;
                vec_push(&t->fields,f);
            } else {
                for (int j=0;j<fn_->list.len;j++){
                    Node *id = fn_->list.p[j];
                    Field *f = xcalloc(1,sizeof *f);
                    f->name = id->name; f->type = ft; f->tag = fn_->name; f->pos = id->pos;
                    vec_push(&t->fields,f);
                }
            }
        }
        return t;
    }
    case N_FUNCTYPE: {
        Type *t = type_new(T_FUNC);
        if (n->x){
            for (int i=0;i<n->x->list.len;i++){
                Node *p = n->x->list.p[i];
                Node *tn = p->x;
                int variadic = 0;
                if (tn && tn->kind==N_ELLIPSIS){ variadic=1; tn = tn->x; }
                Type *pt = resolve_type(tn);
                if (variadic){ pt = type_slice(pt); t->variadic = 1; }
                if (p->list.len==0){
                    Field *f = xcalloc(1,sizeof *f); f->name=NULL; f->type=pt; vec_push(&t->fields,f);
                } else {
                    for (int j=0;j<p->list.len;j++){
                        Node *id = p->list.p[j];
                        Field *f = xcalloc(1,sizeof *f); f->name=id->name; f->type=pt; f->pos=id->pos;
                        vec_push(&t->fields,f);
                    }
                }
            }
        }
        if (n->y){
            for (int i=0;i<n->y->list.len;i++){
                Node *p = n->y->list.p[i];
                Type *rt = resolve_type(p->x);
                if (p->list.len==0){ Field *f=xcalloc(1,sizeof *f); f->type=rt; vec_push(&t->results,f); }
                else for (int j=0;j<p->list.len;j++){
                    Node *id=p->list.p[j];
                    Field *f=xcalloc(1,sizeof *f); f->name=id->name; f->type=rt; f->pos=id->pos;
                    vec_push(&t->results,f);
                }
            }
        }
        return t;
    }
    case N_INTERFACETYPE: {
        Type *t = type_new(T_INTERFACE);
        for (int i=0;i<n->list.len;i++){
            Node *f = n->list.p[i];
            if (!f->flag){
                Method *m = xcalloc(1,sizeof *m);
                m->name = f->name;
                m->sig = resolve_type(f->x);
                m->pos = f->pos;
                vec_push(&t->fields,m);
            } else {
                /* embedded interface or type term(s) */
                Node *e = f->x;
                Vec terms; memset(&terms,0,sizeof terms);
                Node *stack[64]; int sp=0; stack[sp++]=e;
                while (sp){
                    Node *cur = stack[--sp];
                    if (cur->kind==N_BINARY && cur->op==TOK_OR){ stack[sp++]=cur->x; stack[sp++]=cur->y; continue; }
                    vec_push(&terms,cur);
                }
                for (int j=terms.len-1;j>=0;j--){
                    Node *tn = terms.p[j];
                    int tilde = 0;
                    if (tn->kind==N_UNARY && tn->op==TOK_TILDE){ tilde=1; tn=tn->x; }
                    Type *et = resolve_type(tn);
                    if (!tilde && terms.len==1 && is_iface(et) && et!=ty_comparable){
                        Type *eu = underlying(et);
                        for (int k=0;k<eu->fields.len;k++) vec_push(&t->fields,eu->fields.p[k]);
                        for (int k=0;k<eu->terms.len;k++) vec_push(&t->terms,eu->terms.p[k]);
                        if (eu->comparable_req) t->comparable_req = 1;
                    } else if (!tilde && et==ty_comparable){
                        t->comparable_req = 1;
                    } else {
                        TypeTerm *tt = xcalloc(1,sizeof *tt);
                        tt->t = et; tt->tilde = tilde;
                        vec_push(&t->terms,tt);
                    }
                }
                vec_free(&terms);
            }
        }
        return t;
    }
    case N_INDEXLIST: case N_INDEX: {
        /* generic instantiation */
        Type *base = resolve_type(n->x);
        Vec targs; memset(&targs,0,sizeof targs);
        if (n->kind==N_INDEX) vec_push(&targs, resolve_type(n->y));
        else for (int i=0;i<n->list.len;i++) vec_push(&targs, resolve_type(n->list.p[i]));
        if (!lang_at_least(18)) cerrorf(n->pos,"type parameters require go1.18 or later");
        return instantiate_named(base,&targs);
    }
    case N_ELLIPSIS: return type_slice(resolve_type(n->x));
    case N_UNARY:
        if (n->op==TOK_TILDE){
            Type *t = type_new(T_INTERFACE);
            TypeTerm *tt = xcalloc(1,sizeof *tt);
            tt->t = resolve_type(n->x); tt->tilde = 1;
            vec_push(&t->terms,tt);
            return t;
        }
        cerrorf(n->pos,"invalid type expression");
        return ty_invalid;
    case N_BINARY:
        if (n->op==TOK_OR){
            /* a union of type terms used directly as a constraint */
            Type *t = type_new(T_INTERFACE);
            Node *stack[64]; int sp=0; stack[sp++]=n;
            Vec terms; memset(&terms,0,sizeof terms);
            while (sp){
                Node *cur = stack[--sp];
                if (cur->kind==N_BINARY && cur->op==TOK_OR){ stack[sp++]=cur->x; stack[sp++]=cur->y; continue; }
                vec_push(&terms,cur);
            }
            for (int i=terms.len-1;i>=0;i--){
                Node *tn = terms.p[i];
                int tilde = 0;
                if (tn->kind==N_UNARY && tn->op==TOK_TILDE){ tilde=1; tn=tn->x; }
                TypeTerm *tt = xcalloc(1,sizeof *tt);
                tt->t = resolve_type(tn); tt->tilde = tilde;
                vec_push(&t->terms,tt);
            }
            vec_free(&terms);
            return t;
        }
        cerrorf(n->pos,"invalid type expression");
        return ty_invalid;
    default:
        cerrorf(n->pos,"invalid type expression");
        return ty_invalid;
    }
}

/* ====================== named types and instantiation ==================== */

static Type *compile_named(Sym *s);
static FuncDef *compile_function(Node *decl, Type *sig, const char *name, Vec *tparams, Type *recvtype, Node *recvfield);

static Type *named_type_of(Sym *s){
    if (s->type) return s->type;
    return compile_named(s);
}

typedef struct TPBind { const char *name; Type *t; } TPBind;
static Vec g_tpbind;     /* active type-parameter bindings (stack) */

static Type *tp_lookup(const char *name);
static Type *tp_lookup_impl(const char *name){
    for (int i=g_tpbind.len-1;i>=0;i--){
        TPBind *b = g_tpbind.p[i];
        if (b->name == name) return b->t;
    }
    return NULL;
}
static Type *tp_lookup(const char *name){ return tp_lookup_impl(name); }

static Type *compile_named(Sym *s){
    Node *spec = s->decl;
    if (!spec) return ty_invalid;
    if (s->checked == 1){
        /* recursive reference: return the placeholder */
        return s->type ? s->type : ty_invalid;
    }
    s->checked = 1;
    Package *savepkg = C.pkg;
    Scope *savescope = C.scope;
    if (s->pkg && s->pkg != C.pkg){
        /* resolving a type from another package: switch context */
        C.pkg = s->pkg;
    }
    Type *t;
    if (spec->flag){
        /* alias */
        if (!lang_at_least(9)) errorf(spec->pos,"type alias requires go1.9 or later");
        if (!spec->w){
            t = resolve_type(spec->x);
            s->type = t;
            s->checked = 2;
            C.pkg = savepkg; C.scope = savescope;
            return t;
        }
        /* parameterised alias: keep the type parameters so uses can be
         * instantiated, but instantiation yields the aliased type itself */
        if (!lang_at_least(23)) errorf(spec->pos,"generic type alias requires go1.23 or later");
        t = type_new(T_NAMED);
        t->name = s->name; t->pkg = s->pkg?s->pkg->path:intern(""); t->decl = spec;
        t->is_alias = 1; t->owner = s->pkg;
        s->type = t;
        int nb = g_tpbind.len;
        Node *tps = spec->w;
        int idx = 0;
        for (int i=0;i<tps->list.len;i++){
            Node *f = tps->list.p[i];
            for (int j=0;j<f->list.len;j++){
                Node *id = f->list.p[j];
                Type *tp = type_new(T_TYPEPARAM); tp->name = id->name; tp->tpindex = idx++;
                vec_push(&t->tparams,tp);
                TPBind *b = xcalloc(1,sizeof *b); b->name=id->name; b->t=tp; vec_push(&g_tpbind,b);
            }
        }
        int k = 0;
        for (int i=0;i<tps->list.len;i++){
            Node *f = tps->list.p[i];
            Type *ct = resolve_type(f->x);
            for (int j=0;j<f->list.len;j++){ Type *tp = t->tparams.p[k++]; tp->constraint = ct; }
        }
        t->under = resolve_type(spec->x);
        g_tpbind.len = nb;
        s->checked = 2;
        C.pkg = savepkg; C.scope = savescope;
        return t;
    }
    t = type_new(T_NAMED);
    t->name = s->name;
    t->pkg = s->pkg ? s->pkg->path : intern("");
    t->decl = spec;
    t->owner = s->pkg;
    s->type = t;
    /* type parameters */
    int nbind = g_tpbind.len;
    if (spec->w && !lang_at_least(18)) errorf(spec->pos,"type parameters require go1.18 or later");
    if (spec->w){
        Node *tps = spec->w;
        int idx = 0;
        for (int i=0;i<tps->list.len;i++){
            Node *f = tps->list.p[i];
            for (int j=0;j<f->list.len;j++){
                Node *id = f->list.p[j];
                Type *tp = type_new(T_TYPEPARAM);
                tp->name = id->name;
                tp->tpindex = idx++;
                vec_push(&t->tparams,tp);
                TPBind *b = xcalloc(1,sizeof *b); b->name=id->name; b->t=tp;
                vec_push(&g_tpbind,b);
            }
        }
        int k = 0;
        for (int i=0;i<tps->list.len;i++){
            Node *f = tps->list.p[i];
            Type *ct = resolve_type(f->x);
            for (int j=0;j<f->list.len;j++){ Type *tp = t->tparams.p[k++]; tp->constraint = ct; }
        }
    }
    t->under = resolve_type(spec->x);
    g_tpbind.len = nbind;
    s->checked = 2;
    C.pkg = savepkg; C.scope = savescope;
    return t;
}

static Vec g_instcache;    /* of Type* instantiated named types */

static Type *instantiate_named(Type *origin, Vec *targs){
    if (!origin || origin->k != T_NAMED) return origin;
    if (origin->tparams.len == 0) return origin;
    if (targs->len != origin->tparams.len) return origin;
    for (int i=0;i<g_instcache.len;i++){
        Type *c = g_instcache.p[i];
        if (c->targs_of != origin) continue;
        int same = 1;
        for (int j=0;j<targs->len;j++) if (!type_identical(c->targs.p[j],targs->p[j])){ same=0; break; }
        if (same) return c;
    }
    if (origin->is_alias) return type_subst(origin->under,&origin->tparams,targs);
    Type *t = type_new(T_NAMED);
    t->name = origin->name;
    t->pkg = origin->pkg;
    t->targs_of = origin;
    t->decl = origin->decl;
    t->owner = origin->owner;
    for (int i=0;i<targs->len;i++) vec_push(&t->targs,targs->p[i]);
    vec_push(&g_instcache,t);
    t->under = type_subst(origin->under,&origin->tparams,targs);
    for (int i=0;i<origin->methods.len;i++){
        Method *om = origin->methods.p[i];
        Method *m = xcalloc(1,sizeof *m);
        *m = *om;
        m->sig = type_subst(om->sig,&origin->tparams,targs);
        m->recv = om->ptr_recv ? type_ptr(t) : t;
        m->def = NULL;
        vec_push(&t->methods,m);
    }
    for (int i=0;i<t->methods.len;i++){
        extern void compile_method_instance(Method *m, Type *inst, Vec *targs);
        compile_method_instance(t->methods.p[i], t, targs);
    }
    return t;
}
Type *instantiate_named_pub(Type *o, Vec *a){ return instantiate_named(o,a); }

/* ========================= package declaration pass ===================== */

static Sym *new_sym(Package *p, const char *name, int kind, Node *decl, Pos pos){
    Sym *s = xcalloc(1,sizeof *s);
    s->name = name; s->kind = kind; s->decl = decl; s->pkg = p; s->pos = pos;
    return s;
}

Sym *pkg_sym(Package *p, const char *name){
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->name == name) return s;
    }
    return NULL;
}

static Vec g_universe;

static void declare_universe(void){
    if (g_universe.len) return;
    struct { const char *n; Type *t; } types[] = {
        {"bool",NULL},{"int",NULL},{"int8",NULL},{"int16",NULL},{"int32",NULL},{"int64",NULL},
        {"uint",NULL},{"uint8",NULL},{"uint16",NULL},{"uint32",NULL},{"uint64",NULL},{"uintptr",NULL},
        {"float32",NULL},{"float64",NULL},{"string",NULL},{"byte",NULL},{"rune",NULL},{"error",NULL},
        {"any",NULL},{"comparable",NULL},{NULL,NULL}
    };
    Type *tv[] = {ty_bool,ty_int,ty_int8,ty_int16,ty_int32,ty_int64,ty_uint,ty_uint8,ty_uint16,
                  ty_uint32,ty_uint64,ty_uintptr,ty_float32,ty_float64,ty_string,ty_uint8,ty_int32,
                  ty_error,ty_any,ty_comparable};
    for (int i=0;types[i].n;i++){
        Sym *s = xcalloc(1,sizeof *s);
        s->name = intern(types[i].n); s->kind = SYM_TYPE; s->type = tv[i]; s->checked = 2;
        vec_push(&g_universe,s);
    }
    const char *builtins[] = {"len","cap","new","make","append","copy","delete","panic","recover",
                              "print","println","close","complex","real","imag","min","max","clear",NULL};
    for (int i=0;builtins[i];i++){
        Sym *s = xcalloc(1,sizeof *s);
        s->name = intern(builtins[i]); s->kind = SYM_BUILTIN; s->builtin_id = i+1;
        vec_push(&g_universe,s);
    }
    const char *consts[] = {"true","false","iota","nil",NULL};
    for (int i=0;consts[i];i++){
        Sym *s = xcalloc(1,sizeof *s);
        s->name = intern(consts[i]); s->kind = SYM_CONST;
        if (i==0){ s->cval = const_new(C_BOOL); s->cval->bval=1; s->type = ty_untyped_bool; }
        else if (i==1){ s->cval = const_new(C_BOOL); s->cval->bval=0; s->type = ty_untyped_bool; }
        else if (i==2){ s->type = ty_untyped_int; s->builtin_id = 100; }
        else { s->type = ty_untyped_nil; s->kind = SYM_CONST; s->builtin_id = 101; }
        vec_push(&g_universe,s);
    }
}

enum { B_LEN=1,B_CAP,B_NEW,B_MAKE,B_APPEND,B_COPY,B_DELETE,B_PANIC,B_RECOVER,
       B_PRINT,B_PRINTLN,B_CLOSE,B_COMPLEX,B_REAL,B_IMAG,B_MIN,B_MAX,B_CLEAR,
       B_SIZEOF=30,B_ALIGNOF,B_OFFSETOF,B_UADD,B_USLICE,B_USTRING,B_USTRINGDATA,B_USLICEDATA };

void unsafe_pkg_init(Package *p){
    if (p->syms.len) return;
    types_init();
    struct { const char *n; int kind; int id; } defs[] = {
        {"Pointer",SYM_TYPE,0},{"Sizeof",SYM_BUILTIN,B_SIZEOF},{"Alignof",SYM_BUILTIN,B_ALIGNOF},
        {"Offsetof",SYM_BUILTIN,B_OFFSETOF},{"Add",SYM_BUILTIN,B_UADD},{"Slice",SYM_BUILTIN,B_USLICE},
        {"String",SYM_BUILTIN,B_USTRING},{"StringData",SYM_BUILTIN,B_USTRINGDATA},
        {"SliceData",SYM_BUILTIN,B_USLICEDATA},{NULL,0,0}
    };
    for (int i=0;defs[i].n;i++){
        Sym *s = xcalloc(1,sizeof *s);
        s->name = intern(defs[i].n); s->kind = defs[i].kind; s->pkg = p; s->checked = 2;
        if (defs[i].kind==SYM_TYPE) s->type = ty_unsafeptr;
        else s->builtin_id = defs[i].id;
        vec_push(&p->syms,s);
    }
}

static Scope *universe_scope(void){
    static Scope *u = NULL;
    if (u) return u;
    declare_universe();
    u = xcalloc(1,sizeof *u);
    for (int i=0;i<g_universe.len;i++) vec_push(&u->names,g_universe.p[i]);
    return u;
}

/* ============================== expressions ============================= */

enum { SEL_PKG=1, SEL_FIELD, SEL_METHOD, SEL_METHODEXPR };
enum { CK_FUNC=1, CK_METHOD, CK_IFACE, CK_CONV, CK_BUILTIN };

static void convert_untyped(Node *n, Type *target);
static Type *check_call(Node *n, Type *hint);
static void check_composite(Node *n, Type *hint);

extern Const *const_to_int(const Const *c);
static int64_t const_i64(Const *c){ int ok; Const *ic = const_to_int(c); return big_to_i64(&ic->i,&ok); }
extern Const *const_binop(TokKind op, const Const *a, const Const *b, const char **err);
extern Const *const_unop(TokKind op, const Const *a, Type *t, const char **err);
extern Const *const_shift(TokKind op, const Const *a, uint64_t n, const char **err);
extern int const_cmp(const Const *a, const Const *b);
extern Const *const_int_i64(int64_t v);
extern Const *const_bool(int v);
extern Const *const_str(const char *s, size_t n);
extern Const *const_float_rat(BigInt *num, BigInt *den);
extern int const_is_integral(const Const *c);

static Value const_to_value(Const *c, Type *t){
    Type *u = underlying(t);
    if (!c || c->k==C_INVALID) return val_zero(t);
    if ((is_integer(u)||is_float(u)) && c->k!=C_INT && c->k!=C_FLOAT) return val_zero(t);
    if (is_string(u) && c->k!=C_STRING) return val_zero(t);
    if (is_boolean(u) && c->k!=C_BOOL) return val_zero(t);
    switch (u->k){
    case T_BOOL: case T_UNTYPED_BOOL: { Value v = val_bool(c->bval); v.t = t; return v; }
    case T_STRING: case T_UNTYPED_STRING: { Value v = val_string(c->s,c->slen); v.t=t; if (v.o) gc_pin_obj(v.o); return v; }
    case T_FLOAT32: case T_FLOAT64: case T_UNTYPED_FLOAT: {
        double d = const_to_double(c);
        if (u->k==T_FLOAT32) d = (double)(float)d;
        return val_float(d,t);
    }
    case T_UINT: case T_UINT8: case T_UINT16: case T_UINT32: case T_UINT64: case T_UINTPTR: {
        Const *ic = const_to_int(c); int ok; uint64_t x = big_to_u64(&ic->i,&ok);
        if (!ok){ int ok2; int64_t sx = big_to_i64(&ic->i,&ok2); x = (uint64_t)sx; }
        return val_uint(x,t);
    }
    case T_INTERFACE: return const_to_value(c, c->k==C_STRING?ty_string:(c->k==C_BOOL?ty_bool:(c->k==C_FLOAT?ty_float64:ty_int)));
    default: {
        Const *ic = const_to_int(c); int ok;
        if (!ic) return val_zero(t);
        return val_int(big_to_i64(&ic->i,&ok),t);
    }
    }
}

static void set_const(Node *n, Const *c, Type *t){
    Ann *a = ann(n);
    a->mode = M_CONST; a->c = c; a->t = t;
}

static Type *combine_untyped(Type *a, Type *b){
    int ra = a->k==T_UNTYPED_INT?1:(a->k==T_UNTYPED_RUNE?2:(a->k==T_UNTYPED_FLOAT?3:0));
    int rb = b->k==T_UNTYPED_INT?1:(b->k==T_UNTYPED_RUNE?2:(b->k==T_UNTYPED_FLOAT?3:0));
    if (ra && rb) return ra>rb?a:b;
    return a;
}

static void convert_untyped(Node *n, Type *target){
    Ann *a = ann(n);
    if (!a->t || !is_untyped(a->t) || !target) return;
    if (is_untyped(target)) return;
    Type *u = underlying(target);
    if (u->k==T_INTERFACE){
        a->t = default_type(a->t);
        if (a->mode==M_CONST && a->c && u->fields.len==0) return;
        return;
    }
    if (a->mode==M_CONST && a->c){
        if (a->t->k==T_UNTYPED_NIL){
            switch (u->k){
            case T_PTR: case T_SLICE: case T_MAP: case T_CHAN: case T_FUNC: case T_INTERFACE: case T_UNSAFEPTR:
                a->t = target; return;
            default: cerrorf(n->pos,"cannot use nil as %s value",type_string(target)); a->t=ty_invalid; return;
            }
        }
        Const *out = NULL;
        if (!const_representable(a->c,target,&out)){
            char *cs = const_string(a->c);
            if ((is_integer(target)||is_float(target)) && (a->c->k==C_INT||a->c->k==C_FLOAT))
                cerrorf(n->pos,"cannot use %s (untyped %s constant) as %s value in assignment (overflows)",
                        cs, a->c->k==C_INT?"int":"float", type_string(target));
            else
                cerrorf(n->pos,"cannot use %s (untyped constant) as %s value",cs,type_string(target));
            free(cs);
            a->t = target;
            return;
        }
        a->c = out?out:a->c;
        a->t = target;
        return;
    }
    a->t = target;
}

static void default_untyped(Node *n){
    Ann *a = ann(n);
    if (a->t && is_untyped(a->t) && a->t->k != T_UNTYPED_NIL) a->t = default_type(a->t);
}

static int assignable_or_error(Node *n, Type *target, const char *ctx){
    Ann *a = ann(n);
    if (!a->t || a->t==ty_invalid || !target || target==ty_invalid) return 1;
    if (is_untyped(a->t)){ convert_untyped(n,target); return 1; }
    if (!type_assignable(a->t,target)){
        const char *missing = NULL;
        if (is_iface(target) && !iface_satisfied(a->t,target,&missing) && missing)
            cerrorf(n->pos,"cannot use value (variable of type %s) as %s value in %s: %s does not implement %s (missing method %s)",
                    type_string(a->t),type_string(target),ctx,type_string(a->t),type_string(target),missing);
        else
            cerrorf(n->pos,"cannot use value (variable of type %s) as %s value in %s",
                    type_string(a->t),type_string(target),ctx);
        return 0;
    }
    return 1;
}

static void check_ident(Node *n, Type *hint){
    Ann *a = ann(n);
    if (n->name == intern("_")){ a->mode = M_VALUE; a->t = ty_invalid; return; }
    Type *tp = tp_lookup(n->name);
    if (tp){ a->mode = M_TYPE; a->t = tp; return; }
    Sym *s = lookup_sym(n->name,n->pos,0);
    if (!s){ a->mode=M_VALUE; a->t = ty_invalid; return; }
    a->sym = s;
    s->used = 1;
    switch (s->kind){
    case SYM_CONST:
        if (s->builtin_id==100){ /* iota */
            if (!C.has_iota) cerrorf(n->pos,"cannot use iota outside constant declaration");
            set_const(n,const_int_i64(C.iota_value),ty_untyped_int);
            return;
        }
        if (s->builtin_id==101){ a->mode=M_CONST; a->t=ty_untyped_nil; a->c=const_new(C_INVALID); return; }
        if (!s->cval && s->decl){ extern void resolve_const_sym(Sym *s); resolve_const_sym(s); }
        a->mode = M_CONST; a->c = s->cval; a->t = s->type?s->type:ty_invalid;
        return;
    case SYM_TYPE: a->mode = M_TYPE; a->t = named_type_of(s); return;
    case SYM_FUNC: {
        extern void resolve_func_sym(Sym *s);
        if (!s->type) resolve_func_sym(s);
        a->mode = M_VALUE; a->t = s->type; a->fn = s->fn;
        return;
    }
    case SYM_PKG: a->mode = M_PKG; a->pkg = (Package*)s->type; return;
    case SYM_BUILTIN: a->mode = M_BUILTIN; a->builtin = s->builtin_id; return;
    default: {
        if (!s->type && s->decl){ extern void resolve_var_sym(Sym *s); resolve_var_sym(s); }
        a->mode = M_VALUE; a->t = s->type?s->type:ty_invalid;
        a->addressable = 1;
        if (s->pkg && s->kind==SYM_VAR && s->globidx>=0 && s->builtin_id==-1){ a->isglobal=1; a->pkg=s->pkg; a->slot=s->globidx; }
        else { a->depth = C.env->level - s->builtin_id; a->slot = s->globidx; }
        return;
    }
    }
}

static void check_basiclit(Node *n){
    Ann *a = ann(n);
    switch (n->op){
    case TOK_INT: {
        const char *s = n->name;
        int base = 10; const char *p = s;
        if (s[0]=='0'){
            if (s[1]=='x'||s[1]=='X'){ base=16; p=s+2; }
            else if (s[1]=='b'||s[1]=='B'){ base=2; p=s+2; }
            else if (s[1]=='o'||s[1]=='O'){ base=8; p=s+2; }
            else if (s[1]) { base=8; p=s+1; }
        }
        Const *c = const_new(C_INT);
        big_parse(&c->i,p,base);
        set_const(n,c,ty_untyped_int);
        return;
    }
    case TOK_CHAR: {
        Const *c = const_new(C_INT);
        big_from_i64(&c->i, (int64_t)strtoll(n->name,NULL,10));
        set_const(n,c,ty_untyped_rune);
        return;
    }
    case TOK_STRING: set_const(n,const_str(n->name,n->namelen),ty_untyped_string); return;
    case TOK_FLOAT: {
        /* parse decimal or hex float into an exact rational */
        const char *s = n->name;
        Buf clean; buf_init(&clean);
        for (const char *p=s;*p;p++) if (*p!='_') buf_putc(&clean,*p);
        BigInt num, den; big_init(&num); big_init(&den); big_from_i64(&den,1);
        const char *t = clean.p;
        int neg = 0;
        if (*t=='-'){ neg=1; t++; }
        if (t[0]=='0' && (t[1]=='x'||t[1]=='X')){
            double d = strtod(clean.p,NULL);
            /* exact enough: use the double */
            Const *c = const_new(C_FLOAT);
            int e; double m = frexp(d,&e);
            int64_t mm = (int64_t)ldexp(m,53); e -= 53;
            big_from_i64(&c->fnum,mm);
            big_init(&c->fden); big_from_i64(&c->fden,1);
            if (e>0){ big_shl(&c->fnum,&c->fnum,(unsigned)e); }
            else if (e<0){ big_shl(&c->fden,&c->fden,(unsigned)(-e)); }
            set_const(n,c,ty_untyped_float);
            buf_free(&clean); free(num.d); free(den.d);
            return;
        }
        Buf digits; buf_init(&digits);
        int exp10 = 0, seendot = 0;
        const char *q = t;
        for (; *q; q++){
            if (*q=='.'){ seendot=1; continue; }
            if (*q=='e'||*q=='E'){ exp10 += (int)strtol(q+1,NULL,10); break; }
            buf_putc(&digits,*q);
            if (seendot) exp10--;
        }
        big_parse(&num, digits.p?digits.p:"0", 10);
        num.neg = neg && !big_is_zero(&num);
        BigInt ten; big_init(&ten); big_from_i64(&ten,10);
        BigInt pow; big_init(&pow); big_from_i64(&pow,1);
        for (int i=0;i<(exp10<0?-exp10:exp10);i++){ BigInt t2; big_init(&t2); big_mul(&t2,&pow,&ten); free(pow.d); pow=t2; }
        if (exp10>=0){ BigInt t2; big_init(&t2); big_mul(&t2,&num,&pow); free(num.d); num=t2; }
        else { big_copy(&den,&pow); }
        Const *c = const_float_rat(&num,&den);
        set_const(n,c,ty_untyped_float);
        buf_free(&clean); buf_free(&digits);
        free(num.d); free(den.d); free(ten.d); free(pow.d);
        return;
    }
    case TOK_IMAG:
        cerrorf(n->pos,"complex literals are not supported by this toolchain");
        a->mode = M_VALUE; a->t = ty_invalid;
        return;
    default: a->mode=M_VALUE; a->t=ty_invalid; return;
    }
}

static Field *find_field_path(Type *t, const char *name, Vec *path){
    return lookup_field(t,name,path);
}

static void check_selector(Node *n, Type *hint){
    Ann *a = ann(n);
    /* package selector? */
    if (n->x->kind==N_IDENT){
        Sym *ps = scope_find(C.scope,n->x->name);
        if (ps && ps->kind==SYM_PKG){
            Package *p = (Package*)ps->type;
            ps->used = 1;
            if (p->inited == -1){ a->mode=M_VALUE; a->t=ty_invalid; return; }
            Sym *s = pkg_sym(p,n->name);
            if (!s){ cerrorf(n->pos,"undefined: %s.%s",n->x->name,n->name); a->t=ty_invalid; a->mode=M_VALUE; return; }
            a->flag2 = SEL_PKG; a->sym = s; a->pkg = p;
            switch (s->kind){
            case SYM_TYPE: a->mode=M_TYPE; a->t = named_type_of(s); return;
            case SYM_CONST: { extern void resolve_const_sym(Sym *); if (!s->cval) resolve_const_sym(s); a->mode=M_CONST; a->c=s->cval; a->t=s->type; return; }
            case SYM_FUNC: { extern void resolve_func_sym(Sym *); if (!s->type) resolve_func_sym(s); a->mode=M_VALUE; a->t=s->type; a->fn=s->fn; return; }
            case SYM_BUILTIN: a->mode=M_BUILTIN; a->builtin=s->builtin_id; return;
            default: { extern void resolve_var_sym(Sym *); if (!s->type) resolve_var_sym(s); a->mode=M_VALUE; a->t=s->type; a->isglobal=1; a->slot=s->globidx; a->addressable=1; return; }
            }
        }
    }
    check_expr(n->x,NULL);
    Ann *xa = ann(n->x);
    if (xa->mode==M_TYPE){
        /* method expression T.M */
        int ind=0;
        Method *m = lookup_method(xa->t,n->name,&ind);
        if (!m){ cerrorf(n->pos,"%s.%s undefined",type_string(xa->t),n->name); a->t=ty_invalid; a->mode=M_VALUE; return; }
        a->flag2 = SEL_METHODEXPR; a->method = m;
        Type *sig = type_new(T_FUNC);
        Field *rf = xcalloc(1,sizeof *rf); rf->type = xa->t;
        vec_push(&sig->fields,rf);
        for (int i=0;i<m->sig->fields.len;i++) vec_push(&sig->fields,m->sig->fields.p[i]);
        for (int i=0;i<m->sig->results.len;i++) vec_push(&sig->results,m->sig->results.p[i]);
        sig->variadic = m->sig->variadic;
        a->mode = M_VALUE; a->t = sig;
        return;
    }
    Type *t = xa->t;
    if (!t || t==ty_invalid){ a->t=ty_invalid; a->mode=M_VALUE; return; }
    Vec path; memset(&path,0,sizeof path);
    Field *f = find_field_path(t,n->name,&path);
    if (f){
        a->flag2 = SEL_FIELD; a->field = f; a->path = path; a->mode = M_VALUE; a->t = f->type;
        a->addressable = xa->addressable || underlying(t)->k==T_PTR || t->k==T_PTR;
        return;
    }
    int ind = 0;
    Method *m = lookup_method(t,n->name,&ind);
    if (m){
        /* record the embedded-field path leading to the method's receiver */
        Type *cur = t;
        if (cur && underlying(cur)->k==T_PTR) cur = underlying(cur)->elem;
        for (int guard=0; guard<8; guard++){
            Type *cu = underlying(cur);
            int direct = 0;
            if (cur->k==T_NAMED) for (int i=0;i<cur->methods.len;i++) if (((Method*)cur->methods.p[i])->name==n->name) direct=1;
            if (cu->k==T_INTERFACE) direct = 1;
            if (is_typeparam(cur)) direct = 1;
            if (direct) break;
            if (cu->k != T_STRUCT) break;
            int found = -1;
            for (int i=0;i<cu->fields.len;i++){
                Field *fl = cu->fields.p[i];
                if (!fl->embedded) continue;
                if (lookup_method(fl->type,n->name,NULL)){ found=i; break; }
            }
            if (found<0) break;
            int *idx = xmalloc(sizeof(int)); *idx = found;
            vec_push(&a->path,idx);
            cur = ((Field*)cu->fields.p[found])->type;
            if (underlying(cur)->k==T_PTR) cur = underlying(cur)->elem;
        }
        a->flag2 = SEL_METHOD; a->method = m; a->mode = M_VALUE; a->t = m->sig;
        a->iface_call = is_iface(t) || is_typeparam(t);
        if (m->ptr_recv && !xa->addressable && underlying(t)->k!=T_PTR && t->k!=T_PTR && !is_iface(t))
            cerrorf(n->pos,"cannot call pointer method %s on %s",n->name,type_string(t));
        return;
    }
    cerrorf(n->pos,"%s undefined (type %s has no field or method %s)",n->name,type_string(t),n->name);
    a->t = ty_invalid; a->mode = M_VALUE;
}

static void check_index(Node *n, Type *hint){
    Ann *a = ann(n);
    check_expr(n->x,NULL);
    Ann *xa = ann(n->x);
    if (xa->mode==M_TYPE || (xa->mode==M_VALUE && xa->fn && xa->fn->tparams.len)){
        /* generic instantiation */
        Vec targs; memset(&targs,0,sizeof targs);
        if (n->kind==N_INDEX) vec_push(&targs, check_type_expr(n->y));
        else for (int i=0;i<n->list.len;i++) vec_push(&targs, check_type_expr(n->list.p[i]));
        if (!lang_at_least(18)) cerrorf(n->pos,"type parameters require go1.18 or later (-lang was set to go1.%d)",C.lang);
        if (xa->mode==M_TYPE){
            a->mode = M_TYPE; a->t = instantiate_named(xa->t,&targs);
            return;
        }
        extern FuncDef *instantiate_func(Sym *s, Vec *targs, Pos pos);
        FuncDef *inst = instantiate_func(xa->sym,&targs,n->pos);
        a->mode = M_VALUE; a->fn = inst; a->t = inst?inst->sig:ty_invalid;
        return;
    }
    Type *t = xa->t ? underlying(xa->t) : ty_invalid;
    if (t->k==T_PTR && underlying(t->elem)->k==T_ARRAY){ t = underlying(t->elem); a->flag2=1; }
    if (is_typeparam(xa->t)){ Type *ct = core_type(xa->t); if (ct) t = ct; }
    check_expr(n->y, t->k==T_MAP ? t->key : ty_int);
    Ann *ya = ann(n->y);
    switch (t->k){
    case T_MAP:
        convert_untyped(n->y,t->key);
        assignable_or_error(n->y,t->key,"map index");
        a->mode = M_MAPINDEX; a->t = t->elem; a->elemt = t->elem;
        return;
    case T_SLICE: convert_untyped(n->y,ty_int); a->mode=M_VALUE; a->t=t->elem; a->addressable=1; return;
    case T_ARRAY: convert_untyped(n->y,ty_int); a->mode=M_VALUE; a->t=t->elem; a->addressable=xa->addressable||a->flag2; return;
    case T_STRING: case T_UNTYPED_STRING:
        convert_untyped(n->x,ty_string); convert_untyped(n->y,ty_int);
        a->mode=M_VALUE; a->t=ty_uint8; return;
    default:
        if (xa->t != ty_invalid) cerrorf(n->pos,"invalid operation: cannot index %s",type_string(xa->t));
        a->mode=M_VALUE; a->t=ty_invalid;
        return;
    }
}

static void check_sliceexpr(Node *n){
    Ann *a = ann(n);
    check_expr(n->x,NULL);
    Ann *xa = ann(n->x);
    Type *t = xa->t?underlying(xa->t):ty_invalid;
    if (is_typeparam(xa->t)){ Type *ct = core_type(xa->t); if (ct) t = ct; }
    if (n->y){ check_expr(n->y,ty_int); convert_untyped(n->y,ty_int); }
    if (n->z){ check_expr(n->z,ty_int); convert_untyped(n->z,ty_int); }
    if (n->w){ check_expr(n->w,ty_int); convert_untyped(n->w,ty_int); }
    a->mode = M_VALUE;
    switch (t->k){
    case T_STRING: a->t = is_untyped(xa->t)?ty_string:xa->t; return;
    case T_SLICE: a->t = xa->t; return;
    case T_ARRAY:
        if (!xa->addressable) cerrorf(n->pos,"invalid operation: slice of unaddressable value");
        a->t = type_slice(t->elem); return;
    case T_PTR:
        if (underlying(t->elem)->k==T_ARRAY){ a->t = type_slice(underlying(t->elem)->elem); return; }
        /* fallthrough */
    default:
        if (xa->t!=ty_invalid) cerrorf(n->pos,"cannot slice %s",type_string(xa->t));
        a->t = ty_invalid; return;
    }
}

static void check_typeassert(Node *n){
    Ann *a = ann(n);
    check_expr(n->x,NULL);
    Ann *xa = ann(n->x);
    if (xa->t && xa->t!=ty_invalid && !is_iface(xa->t))
        cerrorf(n->pos,"invalid operation: %s is not an interface",type_string(xa->t));
    Type *want = check_type_expr(n->y);
    if (!is_iface(want) && xa->t && is_iface(xa->t)){
        const char *missing=NULL;
        Type *u = underlying(xa->t);
        if (u->fields.len && !iface_satisfied(want,xa->t,&missing))
            cerrorf(n->pos,"impossible type assertion: %s does not implement %s (missing method %s)",
                    type_string(want),type_string(xa->t),missing?missing:"?");
    }
    a->mode = M_VALUE; a->t = want;
}

static void check_star(Node *n){
    Ann *a = ann(n);
    check_expr(n->x,NULL);
    Ann *xa = ann(n->x);
    if (xa->mode==M_TYPE){ a->mode=M_TYPE; a->t = type_ptr(xa->t); return; }
    Type *t = xa->t?underlying(xa->t):ty_invalid;
    if (t->k != T_PTR){
        if (xa->t!=ty_invalid) cerrorf(n->pos,"invalid operation: cannot indirect %s",type_string(xa->t));
        a->mode=M_VALUE; a->t=ty_invalid; return;
    }
    a->mode = M_VALUE; a->t = t->elem; a->addressable = 1;
}

static void check_unary(Node *n, Type *hint){
    Ann *a = ann(n);
    if (n->op==TOK_AND){
        check_expr(n->x, hint && underlying(hint)->k==T_PTR ? underlying(hint)->elem : NULL);
        Ann *xa = ann(n->x);
        if (n->x->kind==N_COMPOSITELIT){ a->mode=M_VALUE; a->t=type_ptr(xa->t); return; }
        if (!xa->addressable) cerrorf(n->pos,"invalid operation: cannot take address of expression");
        a->mode=M_VALUE; a->t = type_ptr(xa->t?xa->t:ty_invalid);
        return;
    }
    if (n->op==TOK_ARROW){
        check_expr(n->x,NULL);
        Ann *xa = ann(n->x);
        Type *t = xa->t?underlying(xa->t):ty_invalid;
        if (is_typeparam(xa->t)){ Type *ct=core_type(xa->t); if(ct) t=ct; }
        if (t->k != T_CHAN){ if (xa->t!=ty_invalid) cerrorf(n->pos,"invalid operation: receive from non-chan type %s",type_string(xa->t)); a->mode=M_VALUE; a->t=ty_invalid; return; }
        if (t->chandir==1) cerrorf(n->pos,"invalid operation: cannot receive from send-only channel");
        a->mode = M_VALUE; a->t = t->elem;
        return;
    }
    check_expr(n->x,hint);
    Ann *xa = ann(n->x);
    if (xa->mode==M_CONST && xa->c && xa->c->k!=C_INVALID){
        const char *err=NULL;
        Const *r = const_unop(n->op,xa->c,is_untyped(xa->t)?NULL:xa->t,&err);
        if (err){ cerrorf(n->pos,"invalid operation: %s",err); a->mode=M_VALUE; a->t=ty_invalid; return; }
        if (!is_untyped(xa->t)){
            Const *out=NULL;
            if (!const_representable(r,xa->t,&out)) cerrorf(n->pos,"constant overflows %s",type_string(xa->t));
            else r = out?out:r;
        }
        set_const(n,r,xa->t);
        return;
    }
    a->mode = M_VALUE; a->t = xa->t;
    switch (n->op){
    case TOK_NOT: if (xa->t && !is_boolean(xa->t) && xa->t!=ty_invalid) cerrorf(n->pos,"invalid operation: operator ! not defined on %s",type_string(xa->t)); break;
    case TOK_SUB: case TOK_ADD: if (xa->t && !is_numeric(xa->t) && xa->t!=ty_invalid && !is_typeparam(xa->t)) cerrorf(n->pos,"invalid operation: operator %s not defined on %s",tok_str(n->op),type_string(xa->t)); break;
    case TOK_XOR: if (xa->t && !is_integer(xa->t) && xa->t!=ty_invalid && !is_typeparam(xa->t)) cerrorf(n->pos,"invalid operation: operator ^ not defined on %s",type_string(xa->t)); break;
    default: break;
    }
}

static void check_binary(Node *n, Type *hint){
    Ann *a = ann(n);
    TokKind op = n->op;
    int is_cmp = (op==TOK_EQL||op==TOK_NEQ||op==TOK_LSS||op==TOK_LEQ||op==TOK_GTR||op==TOK_GEQ);
    int is_shift = (op==TOK_SHL||op==TOK_SHR);
    check_expr(n->x, is_cmp||is_shift?NULL:hint);
    Ann *xa = ann(n->x);
    if (is_shift){
        check_expr(n->y,ty_uint);
        Ann *ya = ann(n->y);
        if (ya->mode==M_CONST && ya->c){
            if (ya->c->k==C_FLOAT && !const_is_integral(ya->c)) cerrorf(n->y->pos,"invalid operation: shift count must be integer");
            Const *ic = const_to_int(ya->c);
            int ok; int64_t sc = big_to_i64(&ic->i,&ok);
            if (sc < 0) cerrorf(n->y->pos,"invalid operation: negative shift count");
            if (xa->mode==M_CONST && xa->c){
                const char *err=NULL;
                Const *r = const_shift(op,xa->c,(uint64_t)(sc<0?0:sc),&err);
                if (err){ cerrorf(n->pos,"%s",err); a->mode=M_VALUE; a->t=ty_invalid; return; }
                Type *rt = xa->t;
                if (is_untyped(rt) && rt->k==T_UNTYPED_FLOAT) rt = ty_untyped_int;
                if (hint && !is_untyped(hint) && is_untyped(rt)) rt = hint;
                if (!is_untyped(rt)){ Const *out=NULL; if (!const_representable(r,rt,&out)) cerrorf(n->pos,"constant overflows %s",type_string(rt)); else r = out?out:r; }
                set_const(n,r,rt);
                return;
            }
        } else {
            default_untyped(n->y);
            Ann *y2 = ann(n->y);
            if (y2->t && !is_integer(y2->t) && y2->t!=ty_invalid) cerrorf(n->y->pos,"invalid operation: shift count type %s, must be integer",type_string(y2->t));
            else if (y2->t && is_integer(y2->t) && !is_unsigned(y2->t) && !lang_at_least(13)) cerrorf(n->y->pos,"invalid operation: signed shift count of type %s requires go1.13 or later",type_string(y2->t));
        }
        if (is_untyped(xa->t)){
            if (hint && !is_untyped(hint)) convert_untyped(n->x,hint);
            else convert_untyped(n->x,ty_int);
        }
        a->mode = M_VALUE; a->t = ann(n->x)->t;
        if (a->t && !is_integer(a->t) && a->t!=ty_invalid && !is_typeparam(a->t)) cerrorf(n->pos,"invalid operation: operand must be integer");
        return;
    }
    check_expr(n->y, is_cmp?(xa->mode==M_CONST?NULL:xa->t):(hint?hint:(is_untyped(xa->t)?NULL:xa->t)));
    Ann *ya = ann(n->y);
    if (xa->t==ty_invalid || ya->t==ty_invalid){ a->mode=M_VALUE; a->t = is_cmp?ty_untyped_bool:ty_invalid; return; }
    if (is_cmp){
        int lnil0 = xa->t && xa->t->k==T_UNTYPED_NIL, rnil0 = ya->t && ya->t->k==T_UNTYPED_NIL;
        if (lnil0 || rnil0){
            Type *other = lnil0 ? ya->t : xa->t;
            Type *ou = other?underlying(other):ty_invalid;
            if (op!=TOK_EQL && op!=TOK_NEQ)
                cerrorf(n->pos,"invalid operation: operator %s not defined on nil",tok_str(op));
            else switch (ou->k){
            case T_PTR: case T_SLICE: case T_MAP: case T_CHAN: case T_FUNC:
            case T_INTERFACE: case T_UNSAFEPTR: case T_UNTYPED_NIL: case T_INVALID: break;
            default: cerrorf(n->pos,"invalid operation: cannot compare %s to nil",type_string(other)); break;
            }
            if (ou->k==T_INTERFACE) a->nilcmp = lnil0 ? 2 : 1;
            a->mode = M_VALUE; a->t = ty_untyped_bool;
            return;
        }
    }
    /* untyped conversions */
    if (is_untyped(xa->t) && !is_untyped(ya->t)) convert_untyped(n->x,ya->t);
    else if (!is_untyped(xa->t) && is_untyped(ya->t)) convert_untyped(n->y,xa->t);
    xa = ann(n->x); ya = ann(n->y);
    if (op==TOK_LAND||op==TOK_LOR){
        if (!is_boolean(xa->t)||!is_boolean(ya->t)) cerrorf(n->pos,"invalid operation: operator %s not defined on non-boolean",tok_str(op));
    }
    if (xa->mode==M_CONST && ya->mode==M_CONST && xa->c && ya->c && xa->c->k!=C_INVALID && ya->c->k!=C_INVALID){
        if (is_cmp){
            int c = const_cmp(xa->c,ya->c);
            int r = 0;
            switch (op){
            case TOK_EQL: r = c==0; break; case TOK_NEQ: r = c!=0; break;
            case TOK_LSS: r = c<0; break; case TOK_LEQ: r = c<=0; break;
            case TOK_GTR: r = c>0; break; case TOK_GEQ: r = c>=0; break;
            default: break;
            }
            set_const(n,const_bool(r),ty_untyped_bool);
            return;
        }
        if (op==TOK_LAND||op==TOK_LOR){
            int r = op==TOK_LAND ? (xa->c->bval&&ya->c->bval) : (xa->c->bval||ya->c->bval);
            set_const(n,const_bool(r),ty_untyped_bool);
            return;
        }
        const char *err=NULL;
        Const *r = const_binop(op,xa->c,ya->c,&err);
        if (err){ cerrorf(n->pos,"invalid operation: %s",err); a->mode=M_VALUE; a->t=ty_invalid; return; }
        Type *rt = is_untyped(xa->t)?(is_untyped(ya->t)?combine_untyped(xa->t,ya->t):ya->t):xa->t;
        if (!is_untyped(rt)){
            Const *out=NULL;
            if (!const_representable(r,rt,&out)) cerrorf(n->pos,"constant %s overflows %s",const_string(r),type_string(rt));
            else r = out?out:r;
        } else if (rt->k==T_UNTYPED_INT && r->k==C_FLOAT && !const_is_integral(r)){
            rt = ty_untyped_float;
        }
        set_const(n,r,rt);
        return;
    }
    if (is_cmp){
        Type *lt = xa->t, *rt = ya->t;
        int lnil = lt && lt->k==T_UNTYPED_NIL, rnil = rt && rt->k==T_UNTYPED_NIL;
        if (lnil || rnil){
            Type *other = lnil ? rt : lt;
            Type *ou = other?underlying(other):ty_invalid;
            if (op!=TOK_EQL && op!=TOK_NEQ)
                cerrorf(n->pos,"invalid operation: operator %s not defined on nil",tok_str(op));
            else switch (ou->k){
            case T_PTR: case T_SLICE: case T_MAP: case T_CHAN: case T_FUNC:
            case T_INTERFACE: case T_UNSAFEPTR: case T_UNTYPED_NIL: case T_INVALID: break;
            default: cerrorf(n->pos,"invalid operation: cannot compare %s to nil",type_string(other)); break;
            }
            if (ou->k==T_INTERFACE) a->nilcmp = lnil ? 2 : 1;
            a->mode = M_VALUE; a->t = ty_untyped_bool;
            return;
        }
        if (!type_assignable(lt,rt) && !type_assignable(rt,lt))
            cerrorf(n->pos,"invalid operation: mismatched types %s and %s",type_string(lt),type_string(rt));
        else if (op==TOK_EQL||op==TOK_NEQ){
            if (!type_comparable(lt) && !is_iface(lt))
                cerrorf(n->pos,"invalid operation: %s cannot be compared",type_string(lt));
        } else {
            if (!type_ordered(lt) && !is_typeparam(lt))
                cerrorf(n->pos,"invalid operation: operator %s not defined on %s",tok_str(op),type_string(lt));
        }
        a->mode = M_VALUE; a->t = ty_untyped_bool;
        return;
    }
    Type *lt = ann(n->x)->t, *rt = ann(n->y)->t;
    if (!type_identical(lt,rt) && !(is_untyped(lt)||is_untyped(rt)))
        cerrorf(n->pos,"invalid operation: mismatched types %s and %s",type_string(lt),type_string(rt));
    Type *res = is_untyped(lt)?rt:lt;
    if (op==TOK_ADD){
        if (!is_numeric(res) && !is_string(res) && !is_typeparam(res) && res!=ty_invalid)
            cerrorf(n->pos,"invalid operation: operator + not defined on %s",type_string(res));
    } else if (op==TOK_QUO||op==TOK_MUL||op==TOK_SUB){
        if (!is_numeric(res) && !is_typeparam(res) && res!=ty_invalid)
            cerrorf(n->pos,"invalid operation: operator %s not defined on %s",tok_str(op),type_string(res));
    } else if (op==TOK_REM||op==TOK_AND||op==TOK_OR||op==TOK_XOR||op==TOK_AND_NOT){
        if (!is_integer(res) && !is_typeparam(res) && res!=ty_invalid)
            cerrorf(n->pos,"invalid operation: operator %s not defined on %s",tok_str(op),type_string(res));
    }
    if ((op==TOK_QUO||op==TOK_REM) && ya->mode==M_CONST && ya->c && const_is_zero(ya->c))
        cerrorf(n->y->pos,"invalid operation: division by zero");
    a->mode = M_VALUE; a->t = res;
}

/* ------------------------------------------------------- composite lits --- */

static void check_composite_elems(Node *n, Type *t);

static void check_composite(Node *n, Type *hint){
    Ann *a = ann(n);
    Type *t = NULL;
    if (n->x) t = check_type_expr(n->x);
    else if (hint) t = hint;
    else { cerrorf(n->pos,"missing type in composite literal"); a->t=ty_invalid; a->mode=M_VALUE; return; }
    if (!n->x && underlying(t)->k==T_PTR){
        /* &T{...} may be written {...} inside a composite literal whose element
         * type is a pointer type (Go spec, Composite literals) */
        a->flag2 = 1;
        t = underlying(t)->elem;
    }
    if (underlying(t)->k==T_ARRAY && underlying(t)->alen<0){
        /* [...]T{...} : compute length */
        int64_t maxi = 0, idx = 0;
        for (int i=0;i<n->list.len;i++){
            Node *e = n->list.p[i];
            if (e->kind==N_KEYVALUE){
                check_expr(e->x,ty_int);
                Ann *ka = ann(e->x);
                if (ka->mode==M_CONST && ka->c) idx = const_i64(ka->c);
            }
            idx++;
            if (idx>maxi) maxi=idx;
        }
        t = type_array(underlying(t)->elem,maxi);
    }
    a->mode = M_VALUE; a->t = t;
    check_composite_elems(n,t);
}

static void check_composite_elems(Node *n, Type *t){
    Type *u = underlying(t);
    Ann *a = ann(n);
    if (u->k==T_PTR) { u = underlying(u->elem); }
    switch (u->k){
    case T_STRUCT: {
        int keyed = n->list.len && ((Node*)n->list.p[0])->kind==N_KEYVALUE;
        if (keyed){
            for (int i=0;i<n->list.len;i++){
                Node *e = n->list.p[i];
                if (e->kind!=N_KEYVALUE){ cerrorf(e->pos,"mixture of field:value and value elements in struct literal"); continue; }
                const char *fname = e->x->kind==N_IDENT ? e->x->name : NULL;
                int fi = -1;
                for (int j=0;j<u->fields.len;j++){ Field *f=u->fields.p[j]; if (f->name==fname){ fi=j; break; } }
                if (fi<0){ cerrorf(e->x->pos,"unknown field %s in struct literal",fname?fname:"?"); continue; }
                Field *f = u->fields.p[fi];
                ann(e)->litslot = fi;
                check_expr(e->y,f->type);
                convert_untyped(e->y,f->type);
                assignable_or_error(e->y,f->type,"struct literal");
            }
        } else {
            if (n->list.len && n->list.len != u->fields.len)
                cerrorf(n->pos,"too few values in struct literal");
            for (int i=0;i<n->list.len && i<u->fields.len;i++){
                Node *e = n->list.p[i];
                Field *f = u->fields.p[i];
                check_expr(e,f->type);
                ann(e)->litslot = i;
                convert_untyped(e,f->type);
                assignable_or_error(e,f->type,"struct literal");
            }
        }
        return;
    }
    case T_ARRAY: case T_SLICE: {
        int64_t idx = 0;
        for (int i=0;i<n->list.len;i++){
            Node *e = n->list.p[i];
            Node *val = e;
            if (e->kind==N_KEYVALUE){
                check_expr(e->x,ty_int);
                Ann *ka = ann(e->x);
                if (ka->mode==M_CONST && ka->c) idx = const_i64(ka->c);
                val = e->y;
            }
            if (val->kind==N_COMPOSITELIT && !val->x) check_composite(val,u->elem);
            else { check_expr(val,u->elem); convert_untyped(val,u->elem); assignable_or_error(val,u->elem,"array or slice literal"); }
            ann(e)->litslot = (int)idx;
            if (u->k==T_ARRAY && u->alen>=0 && idx>=u->alen) cerrorf(val->pos,"index %lld out of bounds [0:%lld]",(long long)idx,(long long)u->alen);
            idx++;
        }
        ann(n)->litslot = (int)(u->k==T_ARRAY?u->alen:idx);
        return;
    }
    case T_MAP: {
        for (int i=0;i<n->list.len;i++){
            Node *e = n->list.p[i];
            if (e->kind!=N_KEYVALUE){ cerrorf(e->pos,"missing key in map literal"); continue; }
            if (e->x->kind==N_COMPOSITELIT && !e->x->x) check_composite(e->x,u->key);
            else { check_expr(e->x,u->key); convert_untyped(e->x,u->key); assignable_or_error(e->x,u->key,"map literal"); }
            if (e->y->kind==N_COMPOSITELIT && !e->y->x) check_composite(e->y,u->elem);
            else { check_expr(e->y,u->elem); convert_untyped(e->y,u->elem); assignable_or_error(e->y,u->elem,"map literal"); }
        }
        return;
    }
    default:
        if (t!=ty_invalid) cerrorf(n->pos,"invalid composite literal type %s",type_string(t));
        return;
    }
}

/* ------------------------------------------------------------- generics --- */

static int unify(Type *param, Type *arg, Vec *tps, Type **bind){
    if (!param || !arg) return 0;
    if (param->k==T_TYPEPARAM){
        for (int i=0;i<tps->len;i++){
            if (tps->p[i]==param){
                if (!bind[i]) bind[i] = default_type(arg);
                return 1;
            }
        }
        return 1;
    }
    Type *pu = param, *au = arg;
    switch (pu->k){
    case T_PTR: case T_SLICE: case T_CHAN:
        if (underlying(au)->k != pu->k) return 0;
        return unify(pu->elem, underlying(au)->elem, tps, bind);
    case T_ARRAY:
        if (underlying(au)->k != T_ARRAY) return 0;
        return unify(pu->elem, underlying(au)->elem, tps, bind);
    case T_MAP:
        if (underlying(au)->k != T_MAP) return 0;
        return unify(pu->key,underlying(au)->key,tps,bind) && unify(pu->elem,underlying(au)->elem,tps,bind);
    case T_FUNC: {
        Type *a2 = underlying(au);
        if (a2->k != T_FUNC) return 0;
        for (int i=0;i<pu->fields.len && i<a2->fields.len;i++)
            unify(((Field*)pu->fields.p[i])->type, ((Field*)a2->fields.p[i])->type, tps, bind);
        for (int i=0;i<pu->results.len && i<a2->results.len;i++)
            unify(((Field*)pu->results.p[i])->type, ((Field*)a2->results.p[i])->type, tps, bind);
        return 1;
    }
    case T_NAMED: {
        if (au->k==T_NAMED && pu->targs.len && au->targs.len==pu->targs.len){
            for (int i=0;i<pu->targs.len;i++) unify(pu->targs.p[i],au->targs.p[i],tps,bind);
        }
        return 1;
    }
    default: return 1;
    }
}

static Vec g_instfuncs;

FuncDef *instantiate_func(Sym *s, Vec *targs, Pos pos){
    if (!s || !s->fn) return NULL;
    FuncDef *orig = s->fn;
    if (orig->tparams.len==0) return orig;
    for (int i=0;i<g_instfuncs.len;i++){
        FuncDef *f = g_instfuncs.p[i];
        if (f->decl != orig->decl) continue;
        if (f->recvtp.len != targs->len) continue;
        int same=1;
        for (int j=0;j<targs->len;j++) if (!type_identical(f->recvtp.p[j],targs->p[j])){ same=0; break; }
        if (same) return f;
    }
    extern FuncDef *compile_generic_instance(Sym *s, Vec *targs);
    return compile_generic_instance(s,targs);
}

/* ----------------------------------------------------------------- calls --- */

static Type *tuple_type(Type *sig){
    if (sig->results.len==1) return ((Field*)sig->results.p[0])->type;
    Type *t = type_new(T_TUPLE);
    for (int i=0;i<sig->results.len;i++) vec_push(&t->tuple, ((Field*)sig->results.p[i])->type);
    return t;
}

static void check_builtin(Node *n, int b, Type *hint);

static Type *check_call(Node *n, Type *hint){
    Ann *a = ann(n);
    Node *fnode = n->x;
    while (fnode->kind==N_PAREN) fnode = fnode->x;
    check_expr(fnode,NULL);
    Ann *fa = ann(fnode);
    if (fa->mode==M_BUILTIN){ check_builtin(n,fa->builtin,hint); return a->t; }
    if (fa->mode==M_TYPE){
        /* conversion */
        a->flag2 = CK_CONV;
        if (n->list.len != 1){ cerrorf(n->pos,"wrong number of arguments in conversion to %s",type_string(fa->t)); a->mode=M_VALUE; a->t=fa->t; return a->t; }
        Node *arg = n->list.p[0];
        check_expr(arg,fa->t);
        Ann *aa = ann(arg);
        if (aa->mode==M_CONST && aa->c && !is_iface(fa->t) && underlying(fa->t)->k!=T_SLICE && aa->c->k!=C_INVALID){
            Const *out=NULL;
            if (is_string(fa->t) && (aa->c->k==C_INT)){
                Buf b; buf_init(&b);
                extern void utf8_put_pub(Buf *b, unsigned r);
                int ok; int64_t cp = big_to_i64(&aa->c->i,&ok);
                utf8_put_pub(&b,(unsigned)((!ok||cp<0||cp>0x10FFFF)?0xFFFD:cp));
                set_const(n,const_str(b.p?b.p:"",b.len),fa->t);
                buf_free(&b);
                return fa->t;
            }
            if (const_representable(aa->c,fa->t,&out)){
                set_const(n,out?out:aa->c,fa->t);
                return fa->t;
            }
            if (is_numeric(fa->t)){
                cerrorf(n->pos,"cannot convert %s to type %s",const_string(aa->c),type_string(fa->t));
                a->mode=M_VALUE; a->t=fa->t; return fa->t;
            }
        }
        if (is_untyped(aa->t) && aa->t->k!=T_UNTYPED_NIL){
            Type *cu = underlying(fa->t);
            Type *ct = (cu->k==T_INTERFACE || cu->k==T_SLICE || cu->k==T_ARRAY || cu->k==T_PTR)
                       ? default_type(aa->t) : fa->t;
            convert_untyped(arg, ct);
        }
        else if (!type_convertible(ann(arg)->t,fa->t))
            cerrorf(n->pos,"cannot convert value of type %s to type %s",type_string(ann(arg)->t),type_string(fa->t));
        if (underlying(fa->t)->k==T_PTR && underlying(fa->t)->elem && underlying(underlying(fa->t)->elem)->k==T_ARRAY
            && ann(arg)->t && underlying(ann(arg)->t)->k==T_SLICE && !lang_at_least(17))
            cerrorf(n->pos,"conversion of slices to array pointers requires go1.17 or later");
        if (underlying(fa->t)->k==T_ARRAY && ann(arg)->t && underlying(ann(arg)->t)->k==T_SLICE && !lang_at_least(20))
            cerrorf(n->pos,"conversion of slices to arrays requires go1.20 or later");
        a->mode = M_VALUE; a->t = fa->t;
        return fa->t;
    }
    /* generic function without explicit instantiation */
    if (fa->fn && fa->fn->tparams.len && fnode->kind!=N_INDEX && fnode->kind!=N_INDEXLIST){
        FuncDef *orig = fa->fn;
        int ntp = orig->tparams.len;
        Type **bind = xcalloc((size_t)ntp,sizeof(Type*));
        Type *sig = orig->sig;
        /* the trailing parameter of a variadic signature is `[]T`, but the
         * arguments at that position have type T, so it is unified below and
         * must not be unified here against the slice type */
        int nfixed = sig->variadic ? sig->fields.len-1 : sig->fields.len;
        for (int i=0;i<n->list.len && i<nfixed;i++){
            Node *arg = n->list.p[i];
            check_expr(arg,NULL);
            Ann *aa = ann(arg);
            if (aa->mode==M_CONST) { Type *dt = default_type(aa->t); unify(((Field*)sig->fields.p[i])->type,dt,&orig->tparams,bind); }
            else unify(((Field*)sig->fields.p[i])->type,aa->t,&orig->tparams,bind);
        }
        if (sig->variadic && n->list.len >= sig->fields.len){
            Type *last = ((Field*)sig->fields.p[sig->fields.len-1])->type;
            for (int i=sig->fields.len-1;i<n->list.len;i++){
                Node *arg = n->list.p[i];
                check_expr(arg,NULL);
                Ann *aa = ann(arg);
                Type *et = underlying(last)->elem;
                unify(et, aa->mode==M_CONST?default_type(aa->t):aa->t, &orig->tparams, bind);
            }
        }
        /* constraint type inference: a bound type parameter whose constraint
         * has a single type term determines the parameters that term mentions
         * (Go spec, Type inference). */
        for (int round=0; round<3; round++){
            for (int i=0;i<ntp;i++){
                Type *tp = orig->tparams.p[i];
                if (!bind[i] || !tp->constraint) continue;
                Type *cu = underlying(tp->constraint);
                if (!cu || cu->k!=T_INTERFACE || cu->terms.len!=1) continue;
                TypeTerm *tt = cu->terms.p[0];
                unify(tt->t, tt->tilde ? underlying(bind[i]) : bind[i], &orig->tparams, bind);
            }
        }
        Vec targs; memset(&targs,0,sizeof targs);
        int ok = 1;
        for (int i=0;i<ntp;i++){
            if (!bind[i]){ ok=0; break; }
            vec_push(&targs,bind[i]);
        }
        if (!ok){
            cerrorf(n->pos,"cannot infer type arguments for %s",orig->name);
            a->mode=M_VALUE; a->t=ty_invalid;
            return a->t;
        }
        FuncDef *inst = instantiate_func(fa->sym,&targs,n->pos);
        if (inst){ fa->fn = inst; fa->t = inst->sig; }
    }
    Type *ft = fa->t ? underlying(fa->t) : ty_invalid;
    if (ft->k != T_FUNC){
        if (fa->t != ty_invalid) cerrorf(n->pos,"invalid operation: cannot call non-function");
        a->mode=M_VALUE; a->t=ty_invalid;
        return a->t;
    }
    a->flag2 = fa->flag2==SEL_METHOD ? (fa->iface_call?CK_IFACE:CK_METHOD) : CK_FUNC;
    int nparams = ft->fields.len;
    int nfixed = ft->variadic ? nparams-1 : nparams;
    /* f(g()) where g returns multiple values */
    if (n->list.len==1){
        Node *arg = n->list.p[0];
        Type *pt0 = NULL;
        if (nfixed > 0) pt0 = ((Field*)ft->fields.p[0])->type;
        else if (ft->variadic && nparams > 0){
            Type *lastp = ((Field*)ft->fields.p[nparams-1])->type;
            pt0 = n->flag ? lastp : underlying(lastp)->elem;
        }
        check_expr(arg,pt0);
        Ann *aa = ann(arg);
        if (aa->t && aa->t->k==T_TUPLE){
            if (aa->t->tuple.len != nparams && !ft->variadic)
                cerrorf(n->pos,"not enough arguments in call");
            a->flag2 |= 0x100; /* tuple spread */
            a->mode = M_VALUE; a->t = tuple_type(ft);
            if (ft->results.len==0) a->mode = M_VOID;
            return a->t;
        }
    }
    if (n->flag){
        /* f(xs...) */
        if (!ft->variadic) cerrorf(n->pos,"cannot use ... in call to non-variadic function");
    }
    for (int i=0;i<n->list.len;i++){
        Node *arg = n->list.p[i];
        Type *pt = NULL;
        if (i < nfixed) pt = ((Field*)ft->fields.p[i])->type;
        else if (ft->variadic) pt = n->flag ? ((Field*)ft->fields.p[nparams-1])->type : underlying(((Field*)ft->fields.p[nparams-1])->type)->elem;
        if (!ann(arg)->t) check_expr(arg,pt);
        if (pt){
            convert_untyped(arg,pt);
            assignable_or_error(arg,pt,"argument");
        } else default_untyped(arg);
    }
    if (!ft->variadic && n->list.len != nparams)
        cerrorf(n->pos,"%s arguments in call (have %d, want %d)", n->list.len<nparams?"not enough":"too many", n->list.len, nparams);
    if (ft->variadic && n->list.len < nfixed)
        cerrorf(n->pos,"not enough arguments in call");
    a->mode = ft->results.len ? M_VALUE : M_VOID;
    a->t = ft->results.len ? tuple_type(ft) : NULL;
    if (ft->results.len > 1) a->mode = M_TUPLE;
    return a->t;
}

static void check_builtin(Node *n, int b, Type *hint){
    Ann *a = ann(n);
    a->flag2 = CK_BUILTIN;
    a->builtin = b;
    int argc = n->list.len;
    Node *a0 = argc>0?n->list.p[0]:NULL;
    Node *a1 = argc>1?n->list.p[1]:NULL;
    switch (b){
    case B_LEN: case B_CAP: {
        if (argc!=1){ cerrorf(n->pos,"wrong number of arguments to %s",b==B_LEN?"len":"cap"); a->t=ty_int; a->mode=M_VALUE; return; }
        check_expr(a0,NULL);
        Ann *aa = ann(a0);
        Type *t = aa->t?underlying(aa->t):ty_invalid;
        if (is_typeparam(aa->t)){ Type *ct=core_type(aa->t); if (ct) t=ct; }
        if (t->k==T_PTR && underlying(t->elem)->k==T_ARRAY) t = underlying(t->elem);
        int okt = (b==B_LEN) ? (t->k==T_STRING||t->k==T_ARRAY||t->k==T_SLICE||t->k==T_MAP||t->k==T_CHAN||t->k==T_UNTYPED_STRING)
                             : (t->k==T_ARRAY||t->k==T_SLICE||t->k==T_CHAN);
        if (!okt && aa->t!=ty_invalid) cerrorf(n->pos,"invalid argument: %s for built-in %s",type_string(aa->t),b==B_LEN?"len":"cap");
        if (t->k==T_ARRAY){ set_const(n,const_int_i64(t->alen),ty_int); return; }
        if (b==B_LEN && aa->mode==M_CONST && aa->c && aa->c->k==C_STRING){ set_const(n,const_int_i64((int64_t)aa->c->slen),ty_int); return; }
        a->mode=M_VALUE; a->t=ty_int; return;
    }
    case B_NEW: {
        if (argc!=1){ cerrorf(n->pos,"wrong number of arguments to new"); a->t=ty_invalid; a->mode=M_VALUE; return; }
        Type *t = check_type_expr(a0);
        a->mode=M_VALUE; a->t=type_ptr(t); a->elemt=t; return;
    }
    case B_MAKE: {
        if (argc<1){ cerrorf(n->pos,"not enough arguments to make"); a->t=ty_invalid; a->mode=M_VALUE; return; }
        Type *t = check_type_expr(a0);
        Type *u = underlying(t);
        if (is_typeparam(t)){ Type *ct=core_type(t); if (ct) u=ct; }
        if (u->k!=T_SLICE && u->k!=T_MAP && u->k!=T_CHAN){
            if (t!=ty_invalid) cerrorf(n->pos,"invalid argument: cannot make %s",type_string(t));
        }
        if (u->k==T_SLICE && argc<2) cerrorf(n->pos,"invalid operation: not enough arguments for make(%s)",type_string(t));
        if (argc>3) cerrorf(n->pos,"too many arguments to make");
        for (int i=1;i<argc;i++){ Node *e=n->list.p[i]; check_expr(e,ty_int); convert_untyped(e,ty_int);
            Ann *ea=ann(e);
            if (ea->t && !is_integer(ea->t) && ea->t!=ty_invalid) cerrorf(e->pos,"non-integer size in make");
            if (ea->mode==M_CONST && ea->c && ea->c->k==C_INT && ea->c->i.neg) cerrorf(e->pos,"negative size in make");
        }
        a->mode=M_VALUE; a->t=t; a->elemt=u; return;
    }
    case B_APPEND: {
        if (argc<1){ cerrorf(n->pos,"not enough arguments to append"); a->t=ty_invalid; a->mode=M_VALUE; return; }
        check_expr(a0,hint);
        Ann *aa = ann(a0);
        Type *t = aa->t?underlying(aa->t):ty_invalid;
        if (t->k==T_UNTYPED_NIL && hint) { convert_untyped(a0,hint); t = underlying(hint); }
        if (t->k!=T_SLICE){ if (aa->t!=ty_invalid) cerrorf(n->pos,"invalid argument: %s is not a slice",type_string(aa->t)); a->mode=M_VALUE; a->t=aa->t; return; }
        Type *et = t->elem;
        if (n->flag){
            if (argc!=2){ cerrorf(n->pos,"can only use ... with final argument"); }
            else {
                check_expr(a1,aa->t);
                Ann *ba = ann(a1);
                Type *bt = ba->t?underlying(ba->t):ty_invalid;
                if (!(bt->k==T_SLICE && type_identical(bt->elem,et)) &&
                    !(underlying(et)->k==T_UINT8 && is_string(ba->t)))
                    cerrorf(n->pos,"cannot use %s as %s in append",type_string(ba->t),type_string(aa->t));
            }
        } else {
            for (int i=1;i<argc;i++){
                Node *e = n->list.p[i];
                check_expr(e,et);
                convert_untyped(e,et);
                assignable_or_error(e,et,"append");
            }
        }
        a->mode=M_VALUE; a->t=aa->t; a->elemt=et; return;
    }
    case B_COPY: {
        if (argc!=2){ cerrorf(n->pos,"wrong number of arguments to copy"); a->t=ty_int; a->mode=M_VALUE; return; }
        check_expr(a0,NULL); check_expr(a1,ann(a0)->t);
        default_untyped(a1);
        a->mode=M_VALUE; a->t=ty_int; return;
    }
    case B_DELETE: {
        if (argc!=2){ cerrorf(n->pos,"wrong number of arguments to delete"); a->mode=M_VOID; return; }
        check_expr(a0,NULL);
        Type *t = ann(a0)->t?underlying(ann(a0)->t):ty_invalid;
        if (t->k!=T_MAP){ if (ann(a0)->t!=ty_invalid) cerrorf(n->pos,"invalid argument: %s is not a map",type_string(ann(a0)->t)); }
        check_expr(a1,t->k==T_MAP?t->key:NULL);
        if (t->k==T_MAP){ convert_untyped(a1,t->key); assignable_or_error(a1,t->key,"delete"); }
        a->mode=M_VOID; a->t=NULL; return;
    }
    case B_PANIC: {
        if (argc!=1){ cerrorf(n->pos,"wrong number of arguments to panic"); a->mode=M_VOID; return; }
        check_expr(a0,ty_any);
        default_untyped(a0);
        a->mode=M_VOID; a->t=NULL; return;
    }
    case B_RECOVER: a->mode=M_VALUE; a->t=ty_any; return;
    case B_PRINT: case B_PRINTLN: {
        for (int i=0;i<argc;i++){ Node *e=n->list.p[i]; check_expr(e,NULL); default_untyped(e); }
        a->mode=M_VOID; a->t=NULL; return;
    }
    case B_CLOSE: {
        if (argc!=1){ cerrorf(n->pos,"wrong number of arguments to close"); a->mode=M_VOID; return; }
        check_expr(a0,NULL);
        Type *t = ann(a0)->t?underlying(ann(a0)->t):ty_invalid;
        if (t->k!=T_CHAN && ann(a0)->t!=ty_invalid) cerrorf(n->pos,"invalid operation: non-chan type %s",type_string(ann(a0)->t));
        if (t->k==T_CHAN && t->chandir==2) cerrorf(n->pos,"invalid operation: cannot close receive-only channel");
        a->mode=M_VOID; a->t=NULL; return;
    }
    case B_MIN: case B_MAX: {
        if (!lang_at_least(21)) cerrorf(n->pos,"predeclared %s requires go1.21 or later",b==B_MIN?"min":"max");
        if (argc<1){ cerrorf(n->pos,"not enough arguments"); a->t=ty_invalid; a->mode=M_VALUE; return; }
        Type *rt = NULL;
        int allconst = 1;
        for (int i=0;i<argc;i++){ Node *e=n->list.p[i]; check_expr(e,hint); Ann *ea=ann(e);
            if (ea->mode!=M_CONST) allconst = 0;
            if (!rt || (is_untyped(rt) && !is_untyped(ea->t))) rt = ea->t; }
        for (int i=0;i<argc;i++){ Node *e=n->list.p[i]; if (!is_untyped(rt)) convert_untyped(e,rt); }
        if (allconst){
            Const *best = ann((Node*)n->list.p[0])->c;
            for (int i=1;i<argc;i++){
                Const *c = ann((Node*)n->list.p[i])->c;
                int cc = const_cmp(c,best);
                if (b==B_MIN ? cc<0 : cc>0) best = c;
            }
            set_const(n,best,rt);
            return;
        }
        if (rt && !type_ordered(rt) && rt!=ty_invalid) cerrorf(n->pos,"invalid argument: %s is not ordered",type_string(rt));
        a->mode=M_VALUE; a->t=rt?rt:ty_invalid; return;
    }
    case B_CLEAR: {
        if (!lang_at_least(21)) cerrorf(n->pos,"predeclared clear requires go1.21 or later");
        if (argc!=1){ cerrorf(n->pos,"wrong number of arguments to clear"); a->mode=M_VOID; return; }
        check_expr(a0,NULL);
        Type *t = ann(a0)->t?underlying(ann(a0)->t):ty_invalid;
        if (t->k!=T_MAP && t->k!=T_SLICE && ann(a0)->t!=ty_invalid) cerrorf(n->pos,"invalid argument: %s",type_string(ann(a0)->t));
        a->mode=M_VOID; a->t=NULL; return;
    }
    case B_SIZEOF: case B_ALIGNOF: {
        if (argc!=1){ cerrorf(n->pos,"wrong number of arguments"); a->t=ty_uintptr; a->mode=M_VALUE; return; }
        check_expr(a0,NULL);
        default_untyped(a0);
        Type *t = ann(a0)->t;
        set_const(n,const_int_i64(b==B_SIZEOF?type_size(t):type_align(t)),ty_uintptr);
        return;
    }
    case B_OFFSETOF: {
        if (argc!=1 || a0->kind!=N_SELECTOR){ cerrorf(n->pos,"invalid argument to unsafe.Offsetof"); a->t=ty_uintptr; a->mode=M_VALUE; return; }
        check_expr(a0,NULL);
        Ann *sa = ann(a0);
        int64_t off = 0;
        Type *bt = ann(a0->x)->t;
        Type *bu = underlying(bt);
        if (bu->k==T_PTR) bu = underlying(bu->elem);
        if (sa->path.len==1 && bu->k==T_STRUCT){
            int idx = *(int*)sa->path.p[0];
            for (int i=0;i<idx;i++){
                Field *fl = bu->fields.p[i];
                int64_t al = type_align(fl->type);
                if (off % al) off += al - (off % al);
                off += type_size(fl->type);
            }
            Field *fl = bu->fields.p[idx];
            int64_t al = type_align(fl->type);
            if (off % al) off += al - (off % al);
        }
        set_const(n,const_int_i64(off),ty_uintptr);
        return;
    }
    case B_UADD: {
        if (!lang_at_least(17)) cerrorf(n->pos,"unsafe.Add requires go1.17 or later");
        if (argc!=2){ cerrorf(n->pos,"wrong number of arguments to unsafe.Add"); a->t=ty_unsafeptr; a->mode=M_VALUE; return; }
        check_expr(a0,NULL); check_expr(a1,ty_int); convert_untyped(a1,ty_int);
        a->mode=M_VALUE; a->t=ty_unsafeptr; a->elemt=ann(a0)->t;
        return;
    }
    case B_USLICE: {
        if (!lang_at_least(17)) cerrorf(n->pos,"unsafe.Slice requires go1.17 or later");
        if (argc!=2){ cerrorf(n->pos,"wrong number of arguments to unsafe.Slice"); a->t=ty_invalid; a->mode=M_VALUE; return; }
        check_expr(a0,NULL); check_expr(a1,ty_int); convert_untyped(a1,ty_int);
        Type *pt = ann(a0)->t;
        Type *pu = pt?underlying(pt):ty_invalid;
        if (pu->k != T_PTR){ cerrorf(n->pos,"first argument to unsafe.Slice must be a pointer"); a->t=ty_invalid; a->mode=M_VALUE; return; }
        a->mode=M_VALUE; a->t=type_slice(pu->elem);
        return;
    }
    case B_USLICEDATA: {
        if (!lang_at_least(20)) cerrorf(n->pos,"unsafe.SliceData requires go1.20 or later");
        check_expr(a0,NULL);
        Type *st = ann(a0)->t; Type *su = st?underlying(st):ty_invalid;
        a->mode=M_VALUE; a->t = su->k==T_SLICE ? type_ptr(su->elem) : ty_unsafeptr;
        return;
    }
    case B_USTRING: {
        if (!lang_at_least(20)) cerrorf(n->pos,"unsafe.String requires go1.20 or later");
        if (argc!=2){ cerrorf(n->pos,"wrong number of arguments to unsafe.String"); a->t=ty_string; a->mode=M_VALUE; return; }
        check_expr(a0,NULL); check_expr(a1,ty_int); convert_untyped(a1,ty_int);
        a->mode=M_VALUE; a->t=ty_string;
        return;
    }
    case B_USTRINGDATA: {
        if (!lang_at_least(20)) cerrorf(n->pos,"unsafe.StringData requires go1.20 or later");
        check_expr(a0,NULL);
        a->mode=M_VALUE; a->t=type_ptr(ty_uint8);
        return;
    }
    case B_COMPLEX: case B_REAL: case B_IMAG:
        cerrorf(n->pos,"complex numbers are not supported by this toolchain");
        a->mode=M_VALUE; a->t=ty_invalid; return;
    default:
        a->mode=M_VALUE; a->t=ty_invalid; return;
    }
}

static Type *check_type_expr(Node *n){
    Ann *a = ann(n);
    Type *t = resolve_type(n);
    a->mode = M_TYPE; a->t = t;
    return t;
}

static void check_funclit(Node *n){
    Ann *a = ann(n);
    extern FuncDef *compile_funclit(Node *n);
    FuncDef *f = compile_funclit(n);
    a->mode = M_VALUE; a->t = f?f->sig:ty_invalid; a->fn = f;
}

static void check_expr(Node *n, Type *hint){
    if (!n) return;
    Ann *a = ann(n);
    if (a->mode != M_INVALID) return;
    switch (n->kind){
    case N_IDENT: check_ident(n,hint); break;
    case N_BASICLIT: check_basiclit(n); break;
    case N_PAREN: { check_expr(n->x,hint); Ann *xa=ann(n->x); a->mode=xa->mode; a->t=xa->t; a->c=xa->c; a->addressable=xa->addressable; a->fn=xa->fn; a->sym=xa->sym; a->pkg=xa->pkg; break; }
    case N_SELECTOR: check_selector(n,hint); break;
    case N_INDEX: case N_INDEXLIST: check_index(n,hint); break;
    case N_SLICEEXPR: check_sliceexpr(n); break;
    case N_TYPEASSERT: check_typeassert(n); break;
    case N_CALL: check_call(n,hint); break;
    case N_STAR: check_star(n); break;
    case N_UNARY:
        if (n->op==TOK_TILDE){ a->mode=M_TYPE; a->t=resolve_type(n); break; }
        check_unary(n,hint); break;
    case N_BINARY: check_binary(n,hint); break;
    case N_COMPOSITELIT: check_composite(n,hint); break;
    case N_FUNCLIT: check_funclit(n); break;
    case N_ARRAYTYPE: case N_STRUCTTYPE: case N_FUNCTYPE: case N_INTERFACETYPE:
    case N_MAPTYPE: case N_CHANTYPE:
        a->mode = M_TYPE; a->t = resolve_type(n); break;
    case N_KEYVALUE: cerrorf(n->pos,"unexpected key:value"); a->mode=M_VALUE; a->t=ty_invalid; break;
    default: a->mode=M_VALUE; a->t=ty_invalid; break;
    }
    if (a->mode==M_INVALID){ a->mode=M_VALUE; if (!a->t) a->t=ty_invalid; }
}

/* ================================ emission ============================== */

extern void gc_pin_obj(Obj *o);

static int tmp_slot(void){
    int s = C.env->nslots++;
    if (C.env == C.fnenv && C.fn && C.env->nslots > C.fn->nlocals) C.fn->nlocals = C.env->nslots;
    return s;
}

static void emit_const(Node *n){
    Ann *a = ann(n);
    Type *t = a->t;
    if (!t || t->k==T_UNTYPED_NIL || (a->c && a->c->k==C_INVALID)){
        E(OP_NILV,0,0,t&&t->k!=T_UNTYPED_NIL?t:NULL,n);
        return;
    }
    if (is_untyped(t)) t = default_type(t);
    Value v = const_to_value(a->c,t);
    E(OP_CONST,addconst(v),0,NULL,n);
}

static void emit_addr(Node *n);
static void emit_call(Node *n, int discard);

static void note_depth(int depth){
    /* a reference above the current function's own environment means the
     * closure has to keep that environment alive */
    if (depth > 0 && C.fn) C.fn->captures = 1;
}

static void emit_load_var(Node *n, Ann *a){
    if (a->isglobal){ E(OP_LOADGLOB,a->slot,0,a->pkg,n); return; }
    note_depth(a->depth);
    E(OP_LOADVAR,a->depth,a->slot,NULL,n);
}

static void emit_recv_chain(Node *n, Ann *a){
    /* emit the receiver for a method call described by a->method / a->path */
    Method *m = a->method;
    Node *x = n->x;
    Ann *xa = ann(x);
    int want_ptr = m->ptr_recv;
    Type *xt = xa->t;
    int is_ptr = xt && underlying(xt)->k==T_PTR;
    int np = a->path.len;
    if (np == 0){
        if (want_ptr && !is_ptr){ if (xa->addressable) emit_addr(x); else emit_expr(x); return; }
        emit_expr(x);
        if (!want_ptr && is_ptr) E(OP_LOADPTR,0,0,NULL,n);
        return;
    }
    if (want_ptr){
        if (is_ptr) emit_expr(x);
        else if (xa->addressable) emit_addr(x);
        else emit_expr(x);
        for (int i=0;i<np;i++){
            int idx = *(int*)a->path.p[i];
            if (i==np-1) E(OP_ADDRFIELD,idx,0,NULL,n);
            else E(OP_FIELD,idx,0,NULL,n);
        }
        return;
    }
    emit_expr(x);
    for (int i=0;i<np;i++) E(OP_FIELD,*(int*)a->path.p[i],0,NULL,n);
}

static void emit_expr(Node *n){
    if (!n) return;
    Ann *a = ann(n);
    if (a->mode==M_CONST && a->c){ emit_const(n); return; }
    switch (n->kind){
    case N_PAREN: emit_expr(n->x); return;
    case N_IDENT: {
        Sym *s = a->sym;
        if (!s){ E(OP_NILV,0,0,a->t,n); return; }
        if (s->kind==SYM_FUNC){
            extern void ensure_func_compiled(Sym *s);
            ensure_func_compiled(s);
            E(OP_CLOSURE,0,0,a->fn?a->fn:s->fn,n);
            return;
        }
        emit_load_var(n,a);
        return;
    }
    case N_SELECTOR: {
        if (a->flag2==SEL_PKG){
            Sym *s = a->sym;
            if (s->kind==SYM_FUNC){
                extern void ensure_func_compiled(Sym *s);
                ensure_func_compiled(s);
                E(OP_CLOSURE,0,0,a->fn?a->fn:s->fn,n);
            } else {
                E(OP_LOADGLOB,s->globidx,0,s->pkg,n);
            }
            return;
        }
        if (a->flag2==SEL_FIELD){
            emit_expr(n->x);
            for (int i=0;i<a->path.len;i++) E(OP_FIELD,*(int*)a->path.p[i],0,NULL,n);
            return;
        }
        if (a->flag2==SEL_METHOD){
            if (a->iface_call){
                emit_expr(n->x);
                E(OP_METHVAL,0,1,(void*)n->name,n);
            } else {
                extern void ensure_method_compiled(Method *m);
                ensure_method_compiled(a->method);
                emit_recv_chain(n,a);
                E(OP_METHVAL,0,0,a->method->def,n);
            }
            return;
        }
        if (a->flag2==SEL_METHODEXPR){
            extern void ensure_method_compiled(Method *m);
            ensure_method_compiled(a->method);
            E(OP_CLOSURE,0,0,a->method->def,n);
            return;
        }
        E(OP_NILV,0,0,a->t,n);
        return;
    }
    case N_INDEX: case N_INDEXLIST: {
        if (a->mode==M_TYPE){ E(OP_NILV,0,0,a->t,n); return; }
        if (a->fn){
            extern void ensure_funcdef_compiled(FuncDef *f);
            ensure_funcdef_compiled(a->fn);
            E(OP_CLOSURE,0,0,a->fn,n);
            return;
        }
        emit_expr(n->x);
        emit_expr(n->y);
        if (a->mode==M_MAPINDEX) E(OP_MAPGET,0,0,ann(n->x)->t,n);
        else E(OP_INDEX,0,0,NULL,n);
        return;
    }
    case N_SLICEEXPR: {
        emit_expr(n->x);
        int flags = 0;
        if (n->y){ emit_expr(n->y); flags|=1; }
        if (n->z){ emit_expr(n->z); flags|=2; }
        if (n->w){ emit_expr(n->w); flags|=4; }
        E(OP_SLICE,flags,0,a->t,n);
        return;
    }
    case N_TYPEASSERT:
        emit_expr(n->x);
        E(OP_TYPEASSERT,0,0,a->t,n);
        return;
    case N_CALL: emit_call(n,0); return;
    case N_STAR: emit_expr(n->x); E(OP_LOADPTR,0,0,NULL,n); return;
    case N_UNARY:
        if (n->op==TOK_AND){
            if (n->x->kind==N_COMPOSITELIT){
                emit_expr(n->x);
                E(OP_NEW,0,1,ann(n->x)->t,n);   /* box the value */
                return;
            }
            emit_addr(n->x);
            return;
        }
        if (n->op==TOK_ARROW){ emit_expr(n->x); E(OP_RECV,0,0,NULL,n); return; }
        emit_expr(n->x);
        E(OP_UN,(int)n->op,0,a->t,n);
        return;
    case N_BINARY: {
        TokKind op = n->op;
        if (op==TOK_LAND || op==TOK_LOR){
            emit_expr(n->x);
            int j = E(op==TOK_LAND?OP_JMPF:OP_JMPT,0,0,NULL,n);
            /* duplicate semantics: re-push the result */
            emit_expr(n->y);
            int j2 = E(OP_JMP,0,0,NULL,n);
            patch(j,here());
            E(OP_BOOLV,op==TOK_LOR,0,NULL,n);
            patch(j2,here());
            return;
        }
        if (op==TOK_EQL||op==TOK_NEQ||op==TOK_LSS||op==TOK_LEQ||op==TOK_GTR||op==TOK_GEQ){
            emit_expr(n->x); emit_expr(n->y);
            E(OP_CMP,(int)op,a->nilcmp,NULL,n);
            return;
        }
        if (op==TOK_SHL||op==TOK_SHR){
            emit_expr(n->x); emit_expr(n->y);
            E(OP_SHIFT,(int)op,0,a->t,n);
            return;
        }
        emit_expr(n->x); emit_expr(n->y);
        E(OP_BIN,(int)op,0,a->t,n);
        return;
    }
    case N_COMPOSITELIT: {
        Type *t = a->t;
        Type *u = underlying(t);
        if (u->k==T_STRUCT){
            for (int i=0;i<n->list.len;i++){
                Node *e = n->list.p[i];
                Node *val = e->kind==N_KEYVALUE ? e->y : e;
                Value iv = val_int(ann(e)->litslot,ty_int);
                E(OP_CONST,addconst(iv),0,NULL,n);
                emit_expr(val);
            }
            E(OP_COMPOSITE_STRUCT,n->list.len,0,t,n);
            if (a->flag2==1) E(OP_NEW,0,1,t,n);
            return;
        }
        if (u->k==T_ARRAY || u->k==T_SLICE){
            for (int i=0;i<n->list.len;i++){
                Node *e = n->list.p[i];
                Node *val = e->kind==N_KEYVALUE ? e->y : e;
                Value iv = val_int(ann(e)->litslot,ty_int);
                E(OP_CONST,addconst(iv),0,NULL,n);
                emit_expr(val);
            }
            int size = u->k==T_ARRAY ? (int)u->alen : a->litslot;
            E(u->k==T_ARRAY?OP_COMPOSITE_ARRAY:OP_COMPOSITE_SLICE,n->list.len,size,t,n);
            if (a->flag2==1) E(OP_NEW,0,1,t,n);
            return;
        }
        if (u->k==T_MAP){
            for (int i=0;i<n->list.len;i++){
                Node *e = n->list.p[i];
                if (e->kind!=N_KEYVALUE) continue;
                emit_expr(e->x); emit_expr(e->y);
            }
            E(OP_COMPOSITE_MAP,n->list.len,0,t,n);
            if (a->flag2==1) E(OP_NEW,0,1,t,n);
            return;
        }
        E(OP_NILV,0,0,t,n);
        return;
    }
    case N_FUNCLIT:
        if (a->fn) E(OP_CLOSURE,0,0,a->fn,n);
        else E(OP_NILV,0,0,a->t,n);
        return;
    default:
        E(OP_NILV,0,0,a->t,n);
        return;
    }
}

static void emit_addr(Node *n){
    Ann *a = ann(n);
    switch (n->kind){
    case N_PAREN: emit_addr(n->x); return;
    case N_IDENT: {
        if (a->isglobal) E(OP_ADDRGLOB,a->slot,0,a->pkg,n);
        else note_depth(a->depth), E(OP_ADDRVAR,a->depth,a->slot,type_ptr(a->t?a->t:ty_invalid),n);
        return;
    }
    case N_STAR: emit_expr(n->x); return;
    case N_INDEX: {
        emit_expr(n->x); emit_expr(n->y);
        E(OP_ADDRINDEX,0,0,type_ptr(a->t?a->t:ty_invalid),n);
        return;
    }
    case N_SELECTOR: {
        if (a->flag2==SEL_PKG){ E(OP_ADDRGLOB,a->sym->globidx,0,a->sym->pkg,n); return; }
        Ann *xa = ann(n->x);
        if (xa->t && underlying(xa->t)->k==T_PTR) emit_expr(n->x);
        else emit_addr(n->x);
        for (int i=0;i<a->path.len;i++){
            int idx = *(int*)a->path.p[i];
            if (i==a->path.len-1) E(OP_ADDRFIELD,idx,0,type_ptr(a->t?a->t:ty_invalid),n);
            else E(OP_FIELD,idx,0,NULL,n);
        }
        return;
    }
    default:
        emit_expr(n);
        return;
    }
}

/* ------------------------------------------------------------ emit call --- */

static void emit_args(Node *n, Type *ft){
    int nparams = ft->fields.len;
    int nfixed = ft->variadic ? nparams-1 : nparams;
    if (ann(n)->flag2 & 0x100){
        /* f(g()) spreading a tuple */
        emit_expr(n->list.p[0]);
        return;
    }
    for (int i=0;i<n->list.len;i++) emit_expr(n->list.p[i]);
    if (n->flag && ft->variadic) E(OP_SPREAD,0,0,NULL,n);
    (void)nfixed;
}

static int call_argcount(Node *n, Type *ft){
    if (ann(n)->flag2 & 0x100){
        Type *tt = ann((Node*)n->list.p[0])->t;
        return tt && tt->k==T_TUPLE ? tt->tuple.len : 1;
    }
    return n->list.len;
}

static void emit_builtin_call(Node *n, int discard);

static void emit_call(Node *n, int discard){
    Ann *a = ann(n);
    if (a->mode==M_CONST && a->c){ if (!discard) emit_const(n); return; }
    int kind = a->flag2 & 0xff;
    if (kind==CK_BUILTIN){ emit_builtin_call(n,discard); return; }
    if (kind==CK_CONV){
        Node *arg = n->list.p[0];
        emit_expr(arg);
        E(OP_CONV,0,0,a->t,n);
        if (discard) E(OP_POP,1,0,NULL,n);
        return;
    }
    Node *fnode = n->x;
    while (fnode->kind==N_PAREN) fnode = fnode->x;
    Ann *fa = ann(fnode);
    Type *ft = underlying(fa->t);
    int nargs = call_argcount(n,ft);
    if (kind==CK_IFACE){
        emit_expr(fnode->x);
        emit_args(n,ft);
        E(OP_CALLIFACE,nargs,0,(void*)fnode->name,n);
    } else if (kind==CK_METHOD){
        extern void ensure_method_compiled(Method *m);
        ensure_method_compiled(fa->method);
        E(OP_CLOSURE,0,0,fa->method->def,n);
        emit_recv_chain(fnode,fa);
        emit_args(n,ft);
        E(OP_CALL,nargs+1,0,NULL,n);
    } else {
        emit_expr(fnode);
        emit_args(n,ft);
        E(OP_CALL,nargs,0,NULL,n);
    }
    int nres = ft->results.len;
    if (discard && nres) E(OP_POP,nres,0,NULL,n);
}

static void emit_builtin_call(Node *n, int discard){
    Ann *a = ann(n);
    int b = a->builtin;
    Node *a0 = n->list.len>0?n->list.p[0]:NULL;
    Node *a1 = n->list.len>1?n->list.p[1]:NULL;
    switch (b){
    case B_LEN: emit_expr(a0); E(OP_LEN,0,0,NULL,n); break;
    case B_CAP: emit_expr(a0); E(OP_CAP,0,0,NULL,n); break;
    case B_NEW: E(OP_NEW,0,0,a->elemt,n); break;
    case B_MAKE: {
        Type *u = a->elemt;
        if (u->k==T_SLICE){
            emit_expr(a1);
            if (n->list.len>2){ emit_expr(n->list.p[2]); E(OP_MAKESLICE,1,0,a->t,n); }
            else E(OP_MAKESLICE,0,0,a->t,n);
        } else if (u->k==T_MAP){
            if (n->list.len>1){ emit_expr(a1); E(OP_MAKEMAP,1,0,a->t,n); }
            else E(OP_MAKEMAP,0,0,a->t,n);
        } else {
            if (n->list.len>1){ emit_expr(a1); E(OP_MAKECHAN,1,0,a->t,n); }
            else E(OP_MAKECHAN,0,0,a->t,n);
        }
        break;
    }
    case B_APPEND: {
        emit_expr(a0);
        for (int i=1;i<n->list.len;i++) emit_expr(n->list.p[i]);
        E(OP_APPEND,n->list.len-1,n->flag?1:0,a->t,n);
        break;
    }
    case B_COPY: emit_expr(a0); emit_expr(a1); E(OP_COPY,0,0,NULL,n); break;
    case B_DELETE: emit_expr(a0); emit_expr(a1); E(OP_MAPDEL,0,0,NULL,n); return;
    case B_PANIC: emit_expr(a0); E(OP_PANIC,0,0,NULL,n); return;
    case B_RECOVER: E(OP_RECOVER,0,0,NULL,n); break;
    case B_CLOSE: emit_expr(a0); E(OP_CLOSE,0,0,NULL,n); return;
    case B_CLEAR: emit_expr(a0); E(OP_CLEAR,0,0,NULL,n); return;
    case B_MIN: case B_MAX: {
        for (int i=0;i<n->list.len;i++) emit_expr(n->list.p[i]);
        E(OP_MINMAX,n->list.len,b==B_MAX,NULL,n);
        break;
    }
    case B_PRINT: case B_PRINTLN: {
        for (int i=0;i<n->list.len;i++) emit_expr(n->list.p[i]);
        E(OP_PRINT,n->list.len,b==B_PRINTLN,NULL,n);
        return;
    }
    case B_UADD: case B_USLICE: case B_USTRING: case B_USTRINGDATA: case B_USLICEDATA: {
        for (int i=0;i<n->list.len;i++) emit_expr(n->list.p[i]);
        E(OP_NATIVE,-b,n->list.len,a->t,n);
        break;
    }
    default: E(OP_NILV,0,0,a->t,n); break;
    }
    if (discard && a->mode!=M_VOID) E(OP_POP,1,0,NULL,n);
}

/* ============================== statements ============================== */

static void compile_stmt(Node *n);
static void compile_block(Node *n, int newscope);

static void loop_break_here(LoopCtx *l){ }

static void emit_store_to(Node *lhs, int tmp){
    /* the value is in local slot `tmp` of the current env */
    Ann *a = ann(lhs);
    if (lhs->kind==N_IDENT && lhs->name==intern("_")){ return; }
    switch (lhs->kind){
    case N_PAREN: emit_store_to(lhs->x,tmp); return;
    case N_IDENT:
        E(OP_LOADVAR,0,tmp,NULL,lhs);
        if (a->isglobal) E(OP_STOREGLOB,a->slot,0,a->pkg,lhs);
        else note_depth(a->depth), E(OP_STOREVAR,a->depth,a->slot,NULL,lhs);
        return;
    case N_STAR:
        emit_expr(lhs->x);
        E(OP_LOADVAR,0,tmp,NULL,lhs);
        E(OP_STOREPTR,0,0,NULL,lhs);
        return;
    case N_INDEX: {
        if (a->mode==M_MAPINDEX){
            emit_expr(lhs->x); emit_expr(lhs->y);
            E(OP_LOADVAR,0,tmp,NULL,lhs);
            E(OP_MAPSET,0,0,NULL,lhs);
        } else {
            emit_expr(lhs->x); emit_expr(lhs->y);
            E(OP_LOADVAR,0,tmp,NULL,lhs);
            E(OP_INDEXSET,0,0,NULL,lhs);
        }
        return;
    }
    case N_SELECTOR: {
        if (a->flag2==SEL_PKG){
            E(OP_LOADVAR,0,tmp,NULL,lhs);
            E(OP_STOREGLOB,a->sym->globidx,0,a->sym->pkg,lhs);
            return;
        }
        emit_addr(lhs);
        E(OP_LOADVAR,0,tmp,NULL,lhs);
        E(OP_STOREPTR,0,0,NULL,lhs);
        return;
    }
    default:
        E(OP_POP,1,0,NULL,lhs);
        return;
    }
}

static Sym *declare_local(const char *name, Type *t, Pos pos);
static Sym *declare_local_impl(const char *name, Type *t, Pos pos){
    Sym *s = xcalloc(1,sizeof *s);
    s->name = name; s->kind = SYM_VAR; s->type = t; s->pos = pos;
    s->globidx = C.env->nslots++;
    s->builtin_id = C.env->level;
    if (C.env==C.fnenv && C.fn && C.env->nslots > C.fn->nlocals) C.fn->nlocals = C.env->nslots;
    vec_push(&C.scope->names,s);
    return s;
}
static Sym *declare_local(const char *name, Type *t, Pos pos){ return declare_local_impl(name,t,pos); }

static void ann_var(Node *id, Sym *s){
    Ann *a = ann(id);
    a->mode = M_VALUE; a->sym = s; a->t = s->type;
    a->depth = C.env->level - s->builtin_id; a->slot = s->globidx;
    a->addressable = 1;
}

static void compile_assign(Node *n){
    int nl = n->list.len, nr = n->list2.len;
    if (n->op==TOK_DEFINE){
        /* check RHS first */
        if (nr==1 && nl>1){
            Node *r = n->list2.p[0];
            check_expr(r,NULL);
            Ann *ra = ann(r);
            Type *rt = ra->t;
            int cnt = rt && rt->k==T_TUPLE ? rt->tuple.len : 1;
            if (r->kind==N_INDEX && ra->mode==M_MAPINDEX && nl==2) cnt = 2;
            else if (r->kind==N_TYPEASSERT && nl==2) cnt = 2;
            else if (r->kind==N_UNARY && r->op==TOK_ARROW && nl==2) cnt = 2;
            if (cnt != nl){ cerrorf(n->pos,"assignment mismatch: %d variables but %d value%s",nl,cnt,cnt==1?"":"s"); }
            /* emit */
            if (r->kind==N_INDEX && ra->mode==M_MAPINDEX && nl==2){
                emit_expr(r->x); emit_expr(r->y);
                E(OP_MAPGET,1,0,ann(r->x)->t,r);
            } else if (r->kind==N_TYPEASSERT && nl==2){
                emit_expr(r->x); E(OP_TYPEASSERT,1,0,ra->t,r);
            } else if (r->kind==N_UNARY && r->op==TOK_ARROW && nl==2){
                emit_expr(r->x); E(OP_RECV,1,0,NULL,r);
            } else emit_expr(r);
            /* declare and store in reverse */
            Vec syms; memset(&syms,0,sizeof syms);
            for (int i=0;i<nl;i++){
                Node *id = n->list.p[i];
                Type *vt;
                if (rt && rt->k==T_TUPLE && i<rt->tuple.len) vt = rt->tuple.p[i];
                else if (i==0) vt = rt && rt->k==T_TUPLE ? rt->tuple.p[0] : default_type(rt);
                else vt = ty_bool;
                if (id->kind==N_IDENT && id->name==intern("_")){ vec_push(&syms,NULL); continue; }
                Sym *ex = id->kind==N_IDENT ? scope_find_local(C.scope,id->name) : NULL;
                Sym *s;
                if (ex && ex->kind==SYM_VAR){ s = ex; }
                else s = declare_local(id->kind==N_IDENT?id->name:intern("_"),vt,id->pos);
                vec_push(&syms,s);
            }
            for (int i=nl-1;i>=0;i--){
                Sym *s = syms.p[i];
                if (!s){ E(OP_POP,1,0,NULL,n); continue; }
                note_depth(C.env->level-s->builtin_id), E(OP_STOREVAR,C.env->level-s->builtin_id,s->globidx,NULL,n);
            }
            for (int i=0;i<nl;i++){ Node *id=n->list.p[i]; Sym *s=syms.p[i]; if (s) ann_var(id,s); }
            vec_free(&syms);
            return;
        }
        if (nl != nr){ cerrorf(n->pos,"assignment mismatch: %d variables but %d values",nl,nr); }
        int newvars = 0;
        for (int i=0;i<nl && i<nr;i++){
            Node *id = n->list.p[i];
            Node *r = n->list2.p[i];
            check_expr(r,NULL);
            default_untyped(r);
            Ann *ra = ann(r);
            if (ra->t && ra->t->k==T_TUPLE) cerrorf(r->pos,"multiple-value expression in single-value context");
            emit_expr(r);
            if (id->kind==N_IDENT && id->name==intern("_")){ E(OP_POP,1,0,NULL,n); continue; }
            Sym *ex = id->kind==N_IDENT ? scope_find_local(C.scope,id->name) : NULL;
            Sym *s;
            if (ex && ex->kind==SYM_VAR){
                s = ex;
                if (!type_assignable(ra->t,s->type)) cerrorf(r->pos,"cannot use value of type %s as %s value",type_string(ra->t),type_string(s->type));
            } else { s = declare_local(id->kind==N_IDENT?id->name:intern("_"),ra->t?ra->t:ty_invalid,id->pos); newvars++; }
            note_depth(C.env->level-s->builtin_id), E(OP_STOREVAR,C.env->level-s->builtin_id,s->globidx,NULL,n);
            ann_var(id,s);
        }
        if (!newvars && nl==nr && nl>0){
            int allold = 1;
            for (int i=0;i<nl;i++){ Node *id=n->list.p[i]; if (id->kind==N_IDENT && id->name==intern("_")) allold=0; }
            if (allold) cerrorf(n->pos,"no new variables on left side of :=");
        }
        return;
    }
    if (n->op != TOK_ASSIGN){
        /* op= */
        Node *lhs = n->list.p[0], *rhs = n->list2.p[0];
        check_expr(lhs,NULL);
        TokKind bop;
        switch (n->op){
        case TOK_ADD_ASSIGN: bop=TOK_ADD; break; case TOK_SUB_ASSIGN: bop=TOK_SUB; break;
        case TOK_MUL_ASSIGN: bop=TOK_MUL; break; case TOK_QUO_ASSIGN: bop=TOK_QUO; break;
        case TOK_REM_ASSIGN: bop=TOK_REM; break; case TOK_AND_ASSIGN: bop=TOK_AND; break;
        case TOK_OR_ASSIGN: bop=TOK_OR; break; case TOK_XOR_ASSIGN: bop=TOK_XOR; break;
        case TOK_SHL_ASSIGN: bop=TOK_SHL; break; case TOK_SHR_ASSIGN: bop=TOK_SHR; break;
        default: bop=TOK_AND_NOT; break;
        }
        Ann *la = ann(lhs);
        check_expr(rhs, (bop==TOK_SHL||bop==TOK_SHR)?ty_uint:la->t);
        if (bop!=TOK_SHL && bop!=TOK_SHR){ convert_untyped(rhs,la->t); assignable_or_error(rhs,la->t,"assignment"); }
        else { default_untyped(rhs); Ann *ra = ann(rhs);
            if (ra->mode!=M_CONST && ra->t && is_integer(ra->t) && !is_unsigned(ra->t) && !lang_at_least(13)) cerrorf(rhs->pos,"invalid operation: signed shift count of type %s requires go1.13 or later",type_string(ra->t)); }
        int tmp = tmp_slot();
        emit_expr(lhs);
        emit_expr(rhs);
        if (bop==TOK_SHL||bop==TOK_SHR) E(OP_SHIFT,(int)bop,0,la->t,n);
        else E(OP_BIN,(int)bop,0,la->t,n);
        E(OP_STOREVAR,0,tmp,NULL,n);
        emit_store_to(lhs,tmp);
        return;
    }
    /* plain = */
    if (nr==1 && nl>1){
        Node *r = n->list2.p[0];
        check_expr(r,NULL);
        Ann *ra = ann(r);
        Type *rt = ra->t;
        for (int i=0;i<nl;i++) check_expr(n->list.p[i],NULL);
        if (r->kind==N_INDEX && ra->mode==M_MAPINDEX && nl==2){
            emit_expr(r->x); emit_expr(r->y); E(OP_MAPGET,1,0,ann(r->x)->t,r);
        } else if (r->kind==N_TYPEASSERT && nl==2){
            emit_expr(r->x); E(OP_TYPEASSERT,1,0,ra->t,r);
        } else if (r->kind==N_UNARY && r->op==TOK_ARROW && nl==2){
            emit_expr(r->x); E(OP_RECV,1,0,NULL,r);
        } else {
            emit_expr(r);
            if (!rt || rt->k!=T_TUPLE || rt->tuple.len != nl) cerrorf(n->pos,"assignment mismatch");
        }
        int *tmps = xmalloc(sizeof(int)*(size_t)nl);
        for (int i=0;i<nl;i++) tmps[i]=tmp_slot();
        for (int i=nl-1;i>=0;i--) E(OP_STOREVAR,0,tmps[i],NULL,n);
        for (int i=0;i<nl;i++) emit_store_to(n->list.p[i],tmps[i]);
        free(tmps);
        return;
    }
    if (nl != nr){ cerrorf(n->pos,"assignment mismatch: %d variables but %d values",nl,nr); return; }
    int *tmps = xmalloc(sizeof(int)*(size_t)(nl?nl:1));
    for (int i=0;i<nl;i++){
        Node *lhs = n->list.p[i], *rhs = n->list2.p[i];
        int blank = lhs->kind==N_IDENT && lhs->name==intern("_");
        if (!blank){
            check_expr(lhs,NULL);
            Ann *la = ann(lhs);
            if (!la->addressable && la->mode!=M_MAPINDEX) cerrorf(lhs->pos,"cannot assign to expression");
            check_expr(rhs,la->t);
            convert_untyped(rhs,la->t);
            assignable_or_error(rhs,la->t,"assignment");
        } else {
            check_expr(rhs,NULL);
            default_untyped(rhs);
        }
        tmps[i] = tmp_slot();
        emit_expr(rhs);
        E(OP_STOREVAR,0,tmps[i],NULL,n);
    }
    for (int i=0;i<nl;i++) emit_store_to(n->list.p[i],tmps[i]);
    free(tmps);
}

/* --------------------------------------------------- declarations in fn --- */

void resolve_const_sym(Sym *s);
void resolve_var_sym(Sym *s);
void resolve_func_sym(Sym *s);
void ensure_func_compiled(Sym *s);
void ensure_method_compiled(Method *m);
void ensure_funcdef_compiled(FuncDef *f);

static void compile_local_gendecl(Node *d){
    if (d->op==TOK_TYPE){
        for (int i=0;i<d->list.len;i++){
            Node *spec = d->list.p[i];
            Sym *s = xcalloc(1,sizeof *s);
            s->name = spec->name; s->kind = SYM_TYPE; s->decl = spec; s->pkg = C.pkg; s->pos = spec->pos;
            vec_push(&C.scope->names,s);
            if (spec->flag && !lang_at_least(9)) cerrorf(spec->pos,"type aliases require go1.9 or later");
            compile_named(s);
        }
        return;
    }
    if (d->op==TOK_CONST){
        int saveiota = C.iota_value, savehas = C.has_iota;
        C.has_iota = 1;
        for (int i=0;i<d->list.len;i++){
            Node *spec = d->list.p[i];
            C.iota_value = spec->flag;
            Type *dt = spec->x ? resolve_type(spec->x) : NULL;
            for (int j=0;j<spec->list.len;j++){
                Node *id = spec->list.p[j];
                Node *val = j<spec->list2.len ? spec->list2.p[j] : NULL;
                Sym *s = xcalloc(1,sizeof *s);
                s->name = id->name; s->kind = SYM_CONST; s->pkg = C.pkg; s->pos = id->pos;
                if (val){
                    Node *copy = val;
                    if (spec->w) ann_reset_tree(val);
                    check_expr(copy,dt);
                    Ann *va = ann(copy);
                    if (va->mode != M_CONST){ cerrorf(val->pos,"%s (value of type %s) is not constant",id->name,type_string(va->t)); s->cval = const_int_i64(0); s->type = ty_untyped_int; }
                    else { if (dt){ convert_untyped(copy,dt); va = ann(copy); }
                           s->cval = va->c; s->type = dt?dt:va->t; }
                } else { s->cval = const_int_i64(0); s->type = ty_untyped_int; }
                vec_push(&C.scope->names,s);
            }
        }
        C.iota_value = saveiota; C.has_iota = savehas;
        return;
    }
    /* var */
    for (int i=0;i<d->list.len;i++){
        Node *spec = d->list.p[i];
        Type *dt = spec->x ? resolve_type(spec->x) : NULL;
        int nl = spec->list.len, nr = spec->list2.len;
        if (nr==0){
            for (int j=0;j<nl;j++){
                Node *id = spec->list.p[j];
                Sym *s = declare_local(id->name,dt?dt:ty_invalid,id->pos);
                E(OP_ZERO,0,0,dt,id);
                E(OP_STOREVAR,0,s->globidx,NULL,id);
                ann_var(id,s);
            }
            continue;
        }
        if (nr==1 && nl>1){
            Node *r = spec->list2.p[0];
            check_expr(r,dt);
            Ann *ra = ann(r);
            Type *rt = ra->t;
            if (r->kind==N_INDEX && ra->mode==M_MAPINDEX && nl==2){ emit_expr(r->x); emit_expr(r->y); E(OP_MAPGET,1,0,ann(r->x)->t,r); }
            else if (r->kind==N_TYPEASSERT && nl==2){ emit_expr(r->x); E(OP_TYPEASSERT,1,0,ra->t,r); }
            else if (r->kind==N_UNARY && r->op==TOK_ARROW && nl==2){ emit_expr(r->x); E(OP_RECV,1,0,NULL,r); }
            else emit_expr(r);
            Vec syms; memset(&syms,0,sizeof syms);
            for (int j=0;j<nl;j++){
                Node *id = spec->list.p[j];
                Type *vt = dt;
                if (!vt){
                    if (rt && rt->k==T_TUPLE && j<rt->tuple.len) vt = rt->tuple.p[j];
                    else if (j==1) vt = ty_bool;
                    else vt = default_type(rt);
                }
                Sym *s = declare_local(id->name,vt?vt:ty_invalid,id->pos);
                vec_push(&syms,s);
            }
            for (int j=nl-1;j>=0;j--){ Sym *s = syms.p[j]; E(OP_STOREVAR,0,s->globidx,NULL,spec); }
            for (int j=0;j<nl;j++) ann_var(spec->list.p[j],syms.p[j]);
            vec_free(&syms);
            continue;
        }
        if (nl != nr){ cerrorf(spec->pos,"assignment mismatch: %d variables but %d values",nl,nr); }
        for (int j=0;j<nl && j<nr;j++){
            Node *id = spec->list.p[j];
            Node *val = spec->list2.p[j];
            check_expr(val,dt);
            if (dt){ convert_untyped(val,dt); assignable_or_error(val,dt,"variable declaration"); }
            else default_untyped(val);
            Ann *va = ann(val);
            Type *vt = dt?dt:(va->t?va->t:ty_invalid);
            if (!dt && va->t && va->t->k==T_UNTYPED_NIL) cerrorf(val->pos,"use of untyped nil in variable declaration");
            emit_expr(val);
            Sym *s = declare_local(id->name,vt,id->pos);
            E(OP_STOREVAR,0,s->globidx,NULL,id);
            ann_var(id,s);
        }
    }
}

/* --------------------------------------------------------- control flow --- */

typedef struct LabelRec { const char *name; int pc; Vec gotos; LoopCtx *loop; } LabelRec;

static void patch_list(Vec *v, int target){
    for (int i=0;i<v->len;i++) patch(*(int*)v->p[i],target);
    v->len = 0;
}
static void add_patch(Vec *v, int pos){ int *p = xmalloc(sizeof(int)); *p=pos; vec_push(v,p); }

static void report_unused(Scope *sc){
    for (int i=0;i<sc->names.len;i++){
        Sym *s = sc->names.p[i];
        if (s->kind==SYM_VAR && !s->used && s->name!=intern("_"))
            cerrorf(s->pos,"declared and not used: %s",s->name);
    }
}

/* Does this subtree capture variables by reference (a function literal, a
 * goroutine/defer closure, or an address-of)? Blocks that both declare
 * variables and capture them need a fresh environment per execution, because
 * every execution of a declaration creates a new variable. */
static int subtree_captures(Node *n){
    if (!n) return 0;
    if (n->kind==N_FUNCLIT) return 1;
    if (n->kind==N_UNARY && n->op==TOK_AND) return 1;
    if (n->kind==N_GO || n->kind==N_DEFER) return 1;
    if (subtree_captures(n->x)||subtree_captures(n->y)||subtree_captures(n->z)||subtree_captures(n->w)) return 1;
    for (int i=0;i<n->list.len;i++) if (subtree_captures(n->list.p[i])) return 1;
    for (int i=0;i<n->list2.len;i++) if (subtree_captures(n->list2.p[i])) return 1;
    return 0;
}

static int block_declares(Node *n){
    for (int i=0;i<n->list.len;i++){
        Node *st = n->list.p[i];
        if (!st) continue;
        if (st->kind==N_ASSIGN && st->op==TOK_DEFINE) return 1;
        if (st->kind==N_DECLSTMT && st->x && st->x->op!=TOK_CONST) return 1;
        if (st->kind==N_LABELED && st->x && st->x->kind==N_ASSIGN && st->x->op==TOK_DEFINE) return 1;
    }
    return 0;
}

static void compile_block(Node *n, int newscope){
    Scope *save = C.scope;
    CEnv *saveenv = C.env;
    int envpc = -1;
    int needenv = newscope && block_declares(n) && subtree_captures(n);
    if (newscope) C.scope = scope_push(C.scope);
    if (needenv){
        envpc = E(OP_NEWENV,0,0,NULL,n);
        C.env = cenv_new(C.env);
        C.scope->env = C.env;
    }
    for (int i=0;i<n->list.len;i++) compile_stmt(n->list.p[i]);
    if (needenv){
        C.fn->code[envpc].a = C.env->nslots;
        E(OP_POPENV,0,0,NULL,n);
        C.env = saveenv;
    }
    if (newscope){ report_unused(C.scope); C.scope = save; }
}

static const char *pending_label = NULL;

static void compile_for(Node *n, const char *label){
    Scope *save = C.scope;
    C.scope = scope_push(C.scope);
    CEnv *saveenv = C.env;
    int newenv_pc = -1;
    int periter = 0;
    if (n->x && n->x->kind==N_ASSIGN && n->x->op==TOK_DEFINE && lang_at_least(22)) periter = 1;
    if (n->x){
        if (periter){
            newenv_pc = E(OP_NEWENV,0,0,NULL,n);
            C.env = cenv_new(C.env);
            C.scope->env = C.env;
        }
        compile_stmt(n->x);
    }
    int top = here();
    LoopCtx lc; memset(&lc,0,sizeof lc);
    lc.parent = C.loop; lc.label = label; lc.envlevel = C.env->level;
    C.loop = &lc;
    int condjmp = -1;
    if (n->y){
        check_expr(n->y,ty_bool);
        Ann *ca = ann(n->y);
        if (ca->t && !is_boolean(ca->t) && ca->t!=ty_invalid) cerrorf(n->y->pos,"non-boolean condition in for statement");
        emit_expr(n->y);
        condjmp = E(OP_JMPF,0,0,NULL,n);
    }
    compile_block(n->w,1);
    int posttop = here();
    patch_list(&lc.continues,posttop);
    int copyenv_pc = -1;
    if (periter && (n->z || n->y)) copyenv_pc = E(OP_COPYENV,0,0,NULL,n);
    if (n->z) compile_stmt(n->z);
    E(OP_JMP,top,0,NULL,n);
    if (condjmp>=0) patch(condjmp,here());
    patch_list(&lc.breaks,here());
    C.loop = lc.parent;
    if (periter){
        if (newenv_pc>=0) C.fn->code[newenv_pc].a = C.env->nslots;
        if (copyenv_pc>=0) C.fn->code[copyenv_pc].a = C.env->nslots;
        E(OP_POPENV,0,0,NULL,n);
        C.env = saveenv;
    }
    C.scope = save;
}

static void compile_range(Node *n, const char *label){
    Scope *save = C.scope;
    C.scope = scope_push(C.scope);
    check_expr(n->z,NULL);
    Ann *ra = ann(n->z);
    Type *rt = ra->t?underlying(ra->t):ty_invalid;
    if (is_typeparam(ra->t)){ Type *ct = core_type(ra->t); if (ct) rt = ct; }
    Type *kt=NULL, *vt=NULL;
    int is_func_range = 0;
    switch (rt->k){
    case T_SLICE: kt=ty_int; vt=rt->elem; break;
    case T_ARRAY: kt=ty_int; vt=rt->elem; break;
    case T_PTR: if (underlying(rt->elem)->k==T_ARRAY){ kt=ty_int; vt=underlying(rt->elem)->elem; } else cerrorf(n->pos,"cannot range over %s",type_string(ra->t)); break;
    case T_STRING: case T_UNTYPED_STRING: kt=ty_int; vt=ty_int32; break;
    case T_MAP: kt=rt->key; vt=rt->elem; break;
    case T_CHAN: kt=rt->elem; vt=NULL; if (rt->chandir==1) cerrorf(n->pos,"cannot range over send-only channel"); break;
    case T_INT: case T_INT8: case T_INT16: case T_INT32: case T_INT64:
    case T_UINT: case T_UINT8: case T_UINT16: case T_UINT32: case T_UINT64: case T_UNTYPED_INT:
        if (!lang_at_least(22)) cerrorf(n->pos,"cannot range over %s (requires go1.22 or later)",type_string(ra->t));
        convert_untyped(n->z,ty_int);
        kt = ann(n->z)->t; vt=NULL; break;
    case T_FUNC: {
        if (!lang_at_least(23)) cerrorf(n->pos,"cannot range over %s (requires go1.23 or later)",type_string(ra->t));
        is_func_range = 1;
        if (rt->fields.len!=1 || rt->results.len>1){ cerrorf(n->pos,"cannot range over %s",type_string(ra->t)); break; }
        Type *yield = underlying(((Field*)rt->fields.p[0])->type);
        if (yield->k!=T_FUNC){ cerrorf(n->pos,"cannot range over %s",type_string(ra->t)); break; }
        if (yield->fields.len>0) kt = ((Field*)yield->fields.p[0])->type;
        if (yield->fields.len>1) vt = ((Field*)yield->fields.p[1])->type;
        break;
    }
    default:
        if (ra->t!=ty_invalid) cerrorf(n->pos,"cannot range over %s",type_string(ra->t));
        kt = ty_invalid; vt = ty_invalid;
        break;
    }
    if (is_func_range){
        extern void compile_func_range(Node *n, Type *kt, Type *vt, const char *label);
        compile_func_range(n,kt,vt,label);
        C.scope = save;
        return;
    }
    CEnv *saveenv = C.env;
    int periter = (n->op==TOK_DEFINE) && lang_at_least(22);
    int newenv_pc = -1;
    Sym *ks=NULL, *vs=NULL;
    emit_expr(n->z);
    E(OP_RANGEINIT,0,0,NULL,n);
    if (periter){
        newenv_pc = E(OP_NEWENV,0,0,NULL,n);
        C.env = cenv_new(C.env);
        C.scope->env = C.env;
    }
    if (n->op==TOK_DEFINE){
        if (n->x && !(n->x->kind==N_IDENT && n->x->name==intern("_"))) ks = declare_local(n->x->name,kt?kt:ty_invalid,n->x->pos);
        if (n->y && !(n->y->kind==N_IDENT && n->y->name==intern("_"))) vs = declare_local(n->y->name,vt?vt:ty_invalid,n->y->pos);
    } else {
        if (n->x) { check_expr(n->x,NULL); }
        if (n->y) { check_expr(n->y,NULL); }
    }
    int top = here();
    LoopCtx lc; memset(&lc,0,sizeof lc);
    lc.parent = C.loop; lc.label = label; lc.envlevel = C.env->level;
    C.loop = &lc;
    int wantk = (n->x && !(n->x->kind==N_IDENT && n->x->name==intern("_"))) ? 1 : 0;
    int wantv = (n->y && !(n->y->kind==N_IDENT && n->y->name==intern("_"))) ? 2 : 0;
    if (rt->k==T_CHAN) wantv = 0;
    int donejmp = E(OP_RANGENEXT,0,wantk|wantv,NULL,n);
    int copyenv_pc = -1;
    if (periter) copyenv_pc = E(OP_COPYENV,0,0,NULL,n);
    if (n->op==TOK_DEFINE){
        if (wantv && vs) E(OP_STOREVAR,0,vs->globidx,NULL,n);
        else if (wantv) E(OP_POP,1,0,NULL,n);
        if (wantk && ks) E(OP_STOREVAR,0,ks->globidx,NULL,n);
        else if (wantk) E(OP_POP,1,0,NULL,n);
    } else {
        int tk=-1,tv=-1;
        if (wantv){ tv = tmp_slot(); E(OP_STOREVAR,0,tv,NULL,n); }
        if (wantk){ tk = tmp_slot(); E(OP_STOREVAR,0,tk,NULL,n); }
        if (wantk && n->x) emit_store_to(n->x,tk);
        if (wantv && n->y) emit_store_to(n->y,tv);
    }
    compile_block(n->w,1);
    patch_list(&lc.continues,here());
    E(OP_JMP,top,0,NULL,n);
    patch(donejmp,here());
    patch_list(&lc.breaks,here());
    C.loop = lc.parent;
    if (periter){
        if (newenv_pc>=0) C.fn->code[newenv_pc].a = C.env->nslots;
        if (copyenv_pc>=0) C.fn->code[copyenv_pc].a = C.env->nslots;
        E(OP_POPENV,0,0,NULL,n);
        C.env = saveenv;
    }
    C.scope = save;
}

static void compile_if(Node *n){
    Scope *save = C.scope;
    C.scope = scope_push(C.scope);
    if (n->x) compile_stmt(n->x);
    if (n->y){
        check_expr(n->y,ty_bool);
        Ann *ca = ann(n->y);
        if (ca->t && !is_boolean(ca->t) && ca->t!=ty_invalid) cerrorf(n->y->pos,"non-boolean condition in if statement");
        emit_expr(n->y);
    } else E(OP_BOOLV,1,0,NULL,n);
    int j = E(OP_JMPF,0,0,NULL,n);
    compile_block(n->z,1);
    if (n->w){
        int j2 = E(OP_JMP,0,0,NULL,n);
        patch(j,here());
        if (n->w->kind==N_BLOCK) compile_block(n->w,1);
        else compile_stmt(n->w);
        patch(j2,here());
    } else patch(j,here());
    C.scope = save;
}

static void compile_switch(Node *n, const char *label){
    Scope *save = C.scope;
    C.scope = scope_push(C.scope);
    if (n->x) compile_stmt(n->x);
    int tagtmp = -1;
    Type *tagt = ty_untyped_bool;
    if (n->y){
        check_expr(n->y,NULL);
        default_untyped(n->y);
        tagt = ann(n->y)->t;
        tagtmp = tmp_slot();
        emit_expr(n->y);
        E(OP_STOREVAR,0,tagtmp,NULL,n);
    }
    LoopCtx lc; memset(&lc,0,sizeof lc);
    lc.parent = C.loop; lc.label = label; lc.is_switch = 1; lc.envlevel = C.env->level;
    C.loop = &lc;
    Vec bodyjmps; memset(&bodyjmps,0,sizeof bodyjmps);
    int defidx = -1;
    int ncases = n->list.len;
    int *bodypc = xcalloc((size_t)(ncases?ncases:1),sizeof(int));
    /* dispatch */
    for (int i=0;i<ncases;i++){
        Node *c = n->list.p[i];
        if (c->flag){ defidx = i; continue; }
        for (int j=0;j<c->list.len;j++){
            Node *e = c->list.p[j];
            check_expr(e, n->y?tagt:ty_bool);
            if (n->y){ convert_untyped(e,tagt); }
            else { Ann *ea=ann(e); if (ea->t && !is_boolean(ea->t) && ea->t!=ty_invalid) cerrorf(e->pos,"invalid case in switch (mismatched types)"); }
            if (n->y){
                E(OP_LOADVAR,0,tagtmp,NULL,e);
                emit_expr(e);
                E(OP_CMP,(int)TOK_EQL,0,NULL,e);
            } else emit_expr(e);
            int jt = E(OP_JMPT,0,0,NULL,e);
            int *p = xmalloc(sizeof(int)); *p = jt;
            vec_push(&bodyjmps,p);
            int *ci = xmalloc(sizeof(int)); *ci = i;
            vec_push(&bodyjmps,ci);
        }
    }
    int defjmp = E(OP_JMP,0,0,NULL,n);
    /* bodies */
    Vec endjmps; memset(&endjmps,0,sizeof endjmps);
    for (int i=0;i<ncases;i++){
        Node *c = n->list.p[i];
        bodypc[i] = here();
        Scope *cs = C.scope;
        C.scope = scope_push(C.scope);
        for (int j=0;j<c->list2.len;j++){
            Node *st = c->list2.p[j];
            if (st->kind==N_BRANCH && st->op==TOK_FALLTHROUGH){
                if (i+1<ncases){ int fj = E(OP_JMP,0,0,NULL,st); int *p=xmalloc(sizeof(int)); *p=fj; vec_push(&endjmps,p); int *q=xmalloc(sizeof(int)); *q=-((i+1)+2); vec_push(&endjmps,q); }
                continue;
            }
            compile_stmt(st);
        }
        C.scope = cs;
        int ej = E(OP_JMP,0,0,NULL,c);
        int *p=xmalloc(sizeof(int)); *p=ej; vec_push(&endjmps,p);
        int *q=xmalloc(sizeof(int)); *q=0; vec_push(&endjmps,q);
    }
    int endpc = here();
    for (int i=0;i<bodyjmps.len;i+=2){
        int jt = *(int*)bodyjmps.p[i];
        int ci = *(int*)bodyjmps.p[i+1];
        patch(jt,bodypc[ci]);
    }
    patch(defjmp, defidx>=0?bodypc[defidx]:endpc);
    for (int i=0;i<endjmps.len;i+=2){
        int j = *(int*)endjmps.p[i];
        int tag = *(int*)endjmps.p[i+1];
        if (tag==0) patch(j,endpc);
        else patch(j,bodypc[-tag-2]);
    }
    patch_list(&lc.breaks,endpc);
    C.loop = lc.parent;
    free(bodypc);
    C.scope = save;
}

static void compile_typeswitch(Node *n, const char *label){
    Scope *save = C.scope;
    C.scope = scope_push(C.scope);
    if (n->x) compile_stmt(n->x);
    Node *guard = n->y;
    Node *expr = NULL;
    const char *bindname = NULL;
    if (guard->kind==N_ASSIGN){
        bindname = ((Node*)guard->list.p[0])->name;
        expr = ((Node*)guard->list2.p[0])->x;
    } else expr = guard->x->x;
    check_expr(expr,NULL);
    Ann *ea = ann(expr);
    if (ea->t && ea->t!=ty_invalid && !is_iface(ea->t) && !is_typeparam(ea->t))
        cerrorf(expr->pos,"%s is not an interface",type_string(ea->t));
    int tagtmp = tmp_slot();
    emit_expr(expr);
    E(OP_STOREVAR,0,tagtmp,NULL,n);
    LoopCtx lc; memset(&lc,0,sizeof lc);
    lc.parent = C.loop; lc.label = label; lc.is_switch = 1; lc.envlevel = C.env->level;
    C.loop = &lc;
    int ncases = n->list.len;
    int *bodypc = xcalloc((size_t)(ncases?ncases:1),sizeof(int));
    Vec jmps; memset(&jmps,0,sizeof jmps);
    int defidx = -1;
    Type **casetype = xcalloc((size_t)(ncases?ncases:1),sizeof(Type*));
    for (int i=0;i<ncases;i++){
        Node *c = n->list.p[i];
        if (c->flag){ defidx = i; continue; }
        for (int j=0;j<c->list.len;j++){
            Node *tn = c->list.p[j];
            Type *ct;
            if (tn->kind==N_IDENT && tn->flag==99){ ct = NULL; }
            else ct = resolve_type(tn);
            if (c->list.len==1) casetype[i] = ct;
            E(OP_LOADVAR,0,tagtmp,NULL,tn);
            if (!ct){
                E(OP_NILV,0,0,NULL,tn);
                E(OP_CMP,(int)TOK_EQL,0,NULL,tn);
            } else {
                E(OP_TYPEASSERT,1,0,ct,tn);
                E(OP_SWAP,0,0,NULL,tn);
                E(OP_POP,1,0,NULL,tn);
            }
            int jt = E(OP_JMPT,0,0,NULL,tn);
            int *p=xmalloc(sizeof(int)); *p=jt; vec_push(&jmps,p);
            int *q=xmalloc(sizeof(int)); *q=i; vec_push(&jmps,q);
        }
    }
    int defjmp = E(OP_JMP,0,0,NULL,n);
    Vec endjmps; memset(&endjmps,0,sizeof endjmps);
    for (int i=0;i<ncases;i++){
        Node *c = n->list.p[i];
        bodypc[i] = here();
        Scope *cs = C.scope;
        C.scope = scope_push(C.scope);
        if (bindname && bindname != intern("_")){
            Type *bt = casetype[i] ? casetype[i] : ea->t;
            Sym *s = declare_local(bindname,bt,n->pos);
            E(OP_LOADVAR,0,tagtmp,NULL,n);
            if (casetype[i] && !is_iface(casetype[i])) E(OP_CONV,0,0,casetype[i],n);
            E(OP_STOREVAR,0,s->globidx,NULL,n);
        }
        for (int j=0;j<c->list2.len;j++) compile_stmt(c->list2.p[j]);
        C.scope = cs;
        int ej = E(OP_JMP,0,0,NULL,c);
        int *p=xmalloc(sizeof(int)); *p=ej; vec_push(&endjmps,p);
    }
    int endpc = here();
    for (int i=0;i<jmps.len;i+=2) patch(*(int*)jmps.p[i], bodypc[*(int*)jmps.p[i+1]]);
    patch(defjmp, defidx>=0?bodypc[defidx]:endpc);
    for (int i=0;i<endjmps.len;i++) patch(*(int*)endjmps.p[i],endpc);
    patch_list(&lc.breaks,endpc);
    C.loop = lc.parent;
    free(bodypc); free(casetype);
    C.scope = save;
}

typedef struct SelCaseC { int kind; int has_ok; int target; } SelCaseC;
typedef struct SelDescC { int n; SelCaseC *c; } SelDescC;

static void compile_select(Node *n, const char *label){
    Scope *save = C.scope;
    C.scope = scope_push(C.scope);
    int ncases = n->list.len;
    SelDescC *sd = xcalloc(1,sizeof *sd);
    sd->n = ncases;
    sd->c = xcalloc((size_t)(ncases?ncases:1),sizeof(SelCaseC));
    /* emit channel operands in order */
    for (int i=0;i<ncases;i++){
        Node *c = n->list.p[i];
        if (c->flag){ sd->c[i].kind = 2; continue; }
        Node *comm = c->x;
        if (comm->kind==N_SEND){
            check_expr(comm->x,NULL);
            Type *ct = ann(comm->x)->t?underlying(ann(comm->x)->t):ty_invalid;
            check_expr(comm->y, ct->k==T_CHAN?ct->elem:NULL);
            if (ct->k==T_CHAN){ convert_untyped(comm->y,ct->elem); assignable_or_error(comm->y,ct->elem,"send"); }
            emit_expr(comm->x);
            emit_expr(comm->y);
            sd->c[i].kind = 1;
        } else {
            /* receive: <-ch, v := <-ch, v, ok := <-ch, v = <-ch */
            Node *recv = NULL;
            if (comm->kind==N_EXPRSTMT) recv = comm->x;
            else if (comm->kind==N_ASSIGN) recv = comm->list2.p[0];
            else if (comm->kind==N_EMPTY) recv = NULL;
            if (!recv || recv->kind!=N_UNARY || recv->op!=TOK_ARROW){
                cerrorf(comm->pos,"select case must be receive, send or assign recv");
                sd->c[i].kind = 2;
                continue;
            }
            check_expr(recv->x,NULL);
            emit_expr(recv->x);
            sd->c[i].kind = 0;
            sd->c[i].has_ok = (comm->kind==N_ASSIGN && comm->list.len>1) ? 1 : 0;
        }
    }
    int selpc = E(OP_SELECT,0,0,sd,n);
    LoopCtx lc; memset(&lc,0,sizeof lc);
    lc.parent = C.loop; lc.label = label; lc.is_select = 1; lc.envlevel = C.env->level;
    C.loop = &lc;
    Vec endjmps; memset(&endjmps,0,sizeof endjmps);
    for (int i=0;i<ncases;i++){
        Node *c = n->list.p[i];
        sd->c[i].target = here();
        Scope *cs = C.scope;
        C.scope = scope_push(C.scope);
        if (!c->flag && c->x && c->x->kind==N_ASSIGN){
            Node *asg = c->x;
            Node *recv = asg->list2.p[0];
            Type *ct = ann(recv->x)->t?underlying(ann(recv->x)->t):ty_invalid;
            Type *et = ct->k==T_CHAN?ct->elem:ty_invalid;
            if (asg->op==TOK_DEFINE){
                Sym *ks=NULL,*os=NULL;
                Node *id0 = asg->list.p[0];
                if (!(id0->kind==N_IDENT && id0->name==intern("_"))) ks = declare_local(id0->name,et,id0->pos);
                if (asg->list.len>1){
                    Node *id1 = asg->list.p[1];
                    if (!(id1->kind==N_IDENT && id1->name==intern("_"))) os = declare_local(id1->name,ty_bool,id1->pos);
                    if (os) E(OP_STOREVAR,0,os->globidx,NULL,asg); else E(OP_POP,1,0,NULL,asg);
                }
                if (ks) E(OP_STOREVAR,0,ks->globidx,NULL,asg); else E(OP_POP,1,0,NULL,asg);
            } else {
                int tv=-1,to=-1;
                if (asg->list.len>1){ to = tmp_slot(); E(OP_STOREVAR,0,to,NULL,asg); }
                tv = tmp_slot(); E(OP_STOREVAR,0,tv,NULL,asg);
                check_expr(asg->list.p[0],et);
                emit_store_to(asg->list.p[0],tv);
                if (asg->list.len>1){ check_expr(asg->list.p[1],ty_bool); emit_store_to(asg->list.p[1],to); }
            }
        } else if (!c->flag && c->x && c->x->kind==N_EXPRSTMT){
            E(OP_POP,1,0,NULL,c);
        }
        for (int j=0;j<c->list2.len;j++) compile_stmt(c->list2.p[j]);
        C.scope = cs;
        int ej = E(OP_JMP,0,0,NULL,c);
        int *p=xmalloc(sizeof(int)); *p=ej; vec_push(&endjmps,p);
    }
    int endpc = here();
    for (int i=0;i<endjmps.len;i++) patch(*(int*)endjmps.p[i],endpc);
    patch_list(&lc.breaks,endpc);
    C.loop = lc.parent;
    (void)selpc;
    C.scope = save;
}

/* ------------------------------------------------------- range over func --- */

typedef struct FRCtx {
    struct FRCtx *parent;
    int ctl_slot;        /* slot in the enclosing function env */
    int ctl_depth_base;  /* env level of the enclosing function */
    int ret_slot0;
    int nret;
    LoopCtx *loop;
    FuncDef *bodyfn;
} FRCtx;
static FRCtx *g_frctx = NULL;

static FuncDef *new_funcdef(const char *name, Type *sig);
static void compile_body_into(FuncDef *f, Node *body, Type *sig, Node *sigNode, Node *recvfield);

void compile_func_range(Node *n, Type *kt, Type *vt, const char *label){
    Ann *ra = ann(n->z);
    Type *rt = underlying(ra->t);
    Type *yieldt = underlying(((Field*)rt->fields.p[0])->type);
    int ctl = tmp_slot();
    int nret = C.fn->sig ? C.fn->sig->results.len : 0;
    int ret0 = -1;
    if (nret > 0){ ret0 = C.env->nslots; for (int i=0;i<nret;i++) tmp_slot(); }
    /* ctl = 0 */
    { Value z = val_int(0,ty_int); E(OP_CONST,addconst(z),0,NULL,n); E(OP_STOREVAR,0,ctl,NULL,n); }
    emit_expr(n->z);
    /* build the yield closure */
    FuncDef *bf = new_funcdef(xsprintf("%s.func",C.fn->name?C.fn->name:"range"), yieldt);
    bf->is_closure = 1;
    bf->nparams = yieldt->fields.len;
    bf->variadic = 0;
    bf->package = C.pkg;
    bf->srcfile = n->pos.file;
    /* compile the closure body in a nested env */
    FuncDef *savefn = C.fn; CEnv *saveenv = C.env; CEnv *savefnenv = C.fnenv;
    Scope *savescope = C.scope; LoopCtx *saveloop = C.loop;
    C.fn = bf;
    C.env = cenv_new(saveenv);
    C.fnenv = C.env;
    C.scope = scope_push(savescope);
    C.scope->env = C.env;
    C.loop = NULL;
    FRCtx fr; memset(&fr,0,sizeof fr);
    fr.parent = g_frctx; fr.ctl_slot = ctl; fr.ctl_depth_base = saveenv->level;
    fr.ret_slot0 = ret0; fr.nret = nret; fr.bodyfn = bf;
    LoopCtx lc; memset(&lc,0,sizeof lc);
    lc.parent = NULL; lc.label = label; lc.envlevel = C.env->level;
    fr.loop = &lc;
    C.loop = &lc;
    g_frctx = &fr;
    Sym *ks=NULL,*vs=NULL;
    if (n->x && !(n->x->kind==N_IDENT && n->x->name==intern("_"))) ks = declare_local(n->x->name,kt?kt:ty_invalid,n->x->pos);
    else if (n->x) declare_local(intern("_k"),kt?kt:ty_invalid,n->x->pos);
    if (n->y && !(n->y->kind==N_IDENT && n->y->name==intern("_"))) vs = declare_local(n->y->name,vt?vt:ty_invalid,n->y->pos);
    else if (n->y) declare_local(intern("_v"),vt?vt:ty_invalid,n->y->pos);
    compile_block(n->w,1);
    patch_list(&lc.continues,here());
    E(OP_BOOLV,1,0,NULL,n);
    E(OP_RET,1,0,NULL,n);
    patch_list(&lc.breaks,here());
    g_frctx = fr.parent;
    C.fn = savefn; C.env = saveenv; C.fnenv = savefnenv; C.scope = savescope; C.loop = saveloop;
    (void)ks; (void)vs;
    E(OP_CLOSURE,0,0,bf,n);
    E(OP_CALL,1,0,NULL,n);
    if (rt->results.len) E(OP_POP,rt->results.len,0,NULL,n);
    /* after the iterator returns: honour a pending return */
    E(OP_LOADVAR,0,ctl,NULL,n);
    { Value two = val_int(2,ty_int); E(OP_CONST,addconst(two),0,NULL,n); }
    E(OP_CMP,(int)TOK_EQL,0,NULL,n);
    int j = E(OP_JMPF,0,0,NULL,n);
    if (nret>0){
        for (int i=0;i<nret;i++) E(OP_LOADVAR,0,ret0+i,NULL,n);
        E(OP_RET,nret,0,NULL,n);
    } else E(OP_RET,0,0,NULL,n);
    patch(j,here());
}

/* --------------------------------------------------------------- stmts --- */

static LoopCtx *find_loop(const char *label, int forcontinue){
    for (LoopCtx *l = C.loop; l; l=l->parent){
        if (label){ if (l->label==label) return l; continue; }
        if (forcontinue && (l->is_switch||l->is_select)) continue;
        return l;
    }
    return NULL;
}

static void compile_return(Node *n){
    FuncDef *f = C.fn;
    Type *sig = f->sig;
    int nres = sig?sig->results.len:0;
    if (g_frctx && g_frctx->bodyfn == f){
        /* return inside a range-over-func body */
        FRCtx *fr = g_frctx;
        int depth = C.env->level - fr->ctl_depth_base;
        for (int i=0;i<n->list.len;i++){
            Node *e = n->list.p[i];
            Type *rt = (fr->nret>i && C.fn) ? NULL : NULL;
            check_expr(e,NULL);
            default_untyped(e);
            emit_expr(e);
        }
        for (int i=n->list.len-1;i>=0;i--) note_depth(depth), E(OP_STOREVAR,depth,fr->ret_slot0+i,NULL,n);
        { Value two = val_int(2,ty_int); E(OP_CONST,addconst(two),0,NULL,n); }
        note_depth(depth), E(OP_STOREVAR,depth,fr->ctl_slot,NULL,n);
        E(OP_BOOLV,0,0,NULL,n);
        E(OP_RET,1,0,NULL,n);
        return;
    }
    if (n->list.len==0){
        if (f->nres_named){ E(OP_RET,-2,0,NULL,n); return; }
        if (nres>0){ cerrorf(n->pos,"not enough return values"); }
        E(OP_RET,0,0,NULL,n);
        return;
    }
    if (n->list.len==1 && nres>1){
        Node *e = n->list.p[0];
        check_expr(e,NULL);
        Ann *ea = ann(e);
        if (ea->t && ea->t->k==T_TUPLE && ea->t->tuple.len==nres){
            emit_expr(e);
            if (f->nres_named){
                for (int i=nres-1;i>=0;i--) E(OP_STOREVAR,0,f->res_slot0+i,NULL,n);
                E(OP_RET,-2,0,NULL,n);
            } else E(OP_RET,nres,0,NULL,n);
            return;
        }
    }
    if (n->list.len != nres) cerrorf(n->pos,"%s return values (have %d, want %d)",n->list.len<nres?"not enough":"too many",n->list.len,nres);
    for (int i=0;i<n->list.len;i++){
        Node *e = n->list.p[i];
        Type *rt = i<nres ? ((Field*)sig->results.p[i])->type : NULL;
        check_expr(e,rt);
        if (rt){ convert_untyped(e,rt); assignable_or_error(e,rt,"return statement"); }
        else default_untyped(e);
        emit_expr(e);
    }
    if (f->nres_named){
        for (int i=n->list.len-1;i>=0;i--) E(OP_STOREVAR,0,f->res_slot0+i,NULL,n);
        E(OP_RET,-2,0,NULL,n);
    } else E(OP_RET,n->list.len,0,NULL,n);
}

static void compile_go_defer(Node *n, int isgo){
    Node *call = n->x;
    while (call->kind==N_PAREN) call = call->x;
    if (call->kind != N_CALL){ cerrorf(n->pos,"expression in %s must be function call",isgo?"go":"defer"); return; }
    check_expr(call,NULL);
    Ann *ca = ann(call);
    int kind = ca->flag2 & 0xff;
    if (kind==CK_BUILTIN || kind==CK_CONV){
        /* wrap in a closure-free thunk: evaluate args now, run later */
        extern FuncDef *make_thunk(Node *call);
        FuncDef *th = make_thunk(call);
        E(OP_CLOSURE,0,0,th,n);
        for (int i=0;i<call->list.len;i++) emit_expr(call->list.p[i]);
        E(isgo?OP_GO:OP_DEFER,call->list.len,0,NULL,n);
        return;
    }
    Node *fnode = call->x;
    while (fnode->kind==N_PAREN) fnode = fnode->x;
    Ann *fa = ann(fnode);
    Type *ft = underlying(fa->t);
    int nargs = call_argcount(call,ft);
    if (kind==CK_IFACE){
        emit_expr(fnode->x);
        E(OP_METHVAL,0,1,(void*)fnode->name,n);
    } else if (kind==CK_METHOD){
        ensure_method_compiled(fa->method);
        emit_recv_chain(fnode,fa);
        E(OP_METHVAL,0,0,fa->method->def,n);
    } else {
        emit_expr(fnode);
    }
    emit_args(call,ft);
    E(isgo?OP_GO:OP_DEFER,nargs,0,NULL,n);
}

static void compile_stmt(Node *n){
    if (!n) return;
    switch (n->kind){
    case N_EMPTY: return;
    case N_BLOCK: compile_block(n,1); return;
    case N_DECLSTMT: compile_local_gendecl(n->x); return;
    case N_EXPRSTMT: {
        check_expr(n->x,NULL);
        Ann *a = ann(n->x);
        Node *e = n->x;
        while (e->kind==N_PAREN) e = e->x;
        if (e->kind==N_CALL) emit_call(e,1);
        else if (e->kind==N_UNARY && e->op==TOK_ARROW){ emit_expr(e); E(OP_POP,1,0,NULL,n); }
        else { cerrorf(n->pos,"%s is not used", e->kind==N_IDENT?e->name:"expression"); emit_expr(e); E(OP_POP,1,0,NULL,n); }
        return;
    }
    case N_ASSIGN: compile_assign(n); return;
    case N_INCDEC: {
        check_expr(n->x,NULL);
        Ann *a = ann(n->x);
        if (a->t && !is_numeric(a->t) && a->t!=ty_invalid) cerrorf(n->pos,"invalid operation: non-numeric type %s",type_string(a->t));
        int tmp = tmp_slot();
        emit_expr(n->x);
        Value one = is_float(a->t)?val_float(1,a->t):(is_unsigned(a->t)?val_uint(1,a->t):val_int(1,a->t));
        E(OP_CONST,addconst(one),0,NULL,n);
        E(OP_BIN,(int)(n->op==TOK_INC?TOK_ADD:TOK_SUB),0,a->t,n);
        E(OP_STOREVAR,0,tmp,NULL,n);
        emit_store_to(n->x,tmp);
        return;
    }
    case N_SEND: {
        check_expr(n->x,NULL);
        Type *ct = ann(n->x)->t?underlying(ann(n->x)->t):ty_invalid;
        if (is_typeparam(ann(n->x)->t)){ Type *cc = core_type(ann(n->x)->t); if (cc) ct = cc; }
        if (ct->k!=T_CHAN){ if (ann(n->x)->t!=ty_invalid) cerrorf(n->pos,"invalid operation: cannot send to non-channel %s",type_string(ann(n->x)->t)); }
        else if (ct->chandir==2) cerrorf(n->pos,"invalid operation: cannot send to receive-only channel");
        check_expr(n->y, ct->k==T_CHAN?ct->elem:NULL);
        if (ct->k==T_CHAN){ convert_untyped(n->y,ct->elem); assignable_or_error(n->y,ct->elem,"send"); }
        emit_expr(n->x); emit_expr(n->y);
        E(OP_SEND,0,0,NULL,n);
        return;
    }
    case N_IF: compile_if(n); return;
    case N_FOR: { const char *l = pending_label; pending_label=NULL; compile_for(n,l); return; }
    case N_RANGE: { const char *l = pending_label; pending_label=NULL; compile_range(n,l); return; }
    case N_SWITCH: { const char *l = pending_label; pending_label=NULL; compile_switch(n,l); return; }
    case N_TYPESWITCH: { const char *l = pending_label; pending_label=NULL; compile_typeswitch(n,l); return; }
    case N_SELECT: { const char *l = pending_label; pending_label=NULL; compile_select(n,l); return; }
    case N_RETURN: compile_return(n); return;
    case N_GO: compile_go_defer(n,1); return;
    case N_DEFER: compile_go_defer(n,0); return;
    case N_BRANCH: {
        if (n->op==TOK_BREAK || n->op==TOK_CONTINUE){
            LoopCtx *l = find_loop(n->name, n->op==TOK_CONTINUE);
            if (!l){ cerrorf(n->pos,"%s is not in a loop",tok_str(n->op)); return; }
            if (g_frctx && g_frctx->loop==l && n->op==TOK_BREAK){
                int depth = C.env->level - g_frctx->ctl_depth_base;
                Value one = val_int(1,ty_int);
                E(OP_CONST,addconst(one),0,NULL,n);
                note_depth(depth), E(OP_STOREVAR,depth,g_frctx->ctl_slot,NULL,n);
                E(OP_BOOLV,0,0,NULL,n);
                E(OP_RET,1,0,NULL,n);
                return;
            }
            for (int d = C.env->level - l->envlevel; d > 0; d--) E(OP_POPENV,0,0,NULL,n);
            int j = E(OP_JMP,0,0,NULL,n);
            add_patch(n->op==TOK_BREAK?&l->breaks:&l->continues,j);
            return;
        }
        if (n->op==TOK_GOTO){
            extern void goto_label(const char *name, Node *n);
            goto_label(n->name,n);
            return;
        }
        if (n->op==TOK_FALLTHROUGH){ cerrorf(n->pos,"fallthrough statement out of place"); return; }
        return;
    }
    case N_LABELED: {
        extern void define_label(const char *name, Node *n);
        define_label(n->name,n);
        Node *inner = n->x;
        if (inner && (inner->kind==N_FOR||inner->kind==N_RANGE||inner->kind==N_SWITCH||inner->kind==N_TYPESWITCH||inner->kind==N_SELECT)){
            pending_label = n->name;
        }
        compile_stmt(inner);
        pending_label = NULL;
        return;
    }
    default:
        cerrorf(n->pos,"unsupported statement");
        return;
    }
}

/* ----------------------------------------------------------- labels --- */

static Vec cur_labels;

void define_label(const char *name, Node *n){
    for (int i=0;i<cur_labels.len;i++){
        LabelRec *l = cur_labels.p[i];
        if (l->name==name){ l->pc = here(); patch_list(&l->gotos,l->pc); return; }
    }
    LabelRec *l = xcalloc(1,sizeof *l);
    l->name = name; l->pc = here();
    vec_push(&cur_labels,l);
}
void goto_label(const char *name, Node *n){
    for (int i=0;i<cur_labels.len;i++){
        LabelRec *l = cur_labels.p[i];
        if (l->name==name){
            if (l->pc>=0){ E(OP_JMP,l->pc,0,NULL,n); return; }
            int j = E(OP_JMP,0,0,NULL,n);
            add_patch(&l->gotos,j);
            return;
        }
    }
    LabelRec *l = xcalloc(1,sizeof *l);
    l->name = name; l->pc = -1;
    int j = E(OP_JMP,0,0,NULL,n);
    add_patch(&l->gotos,j);
    vec_push(&cur_labels,l);
}

/* ======================== function compilation ========================== */

static Vec g_allfuncs;

static FuncDef *new_funcdef(const char *name, Type *sig){
    FuncDef *f = xcalloc(1,sizeof *f);
    f->name = name ? intern(name) : intern("func");
    f->sig = sig;
    vec_push(&g_allfuncs,f);
    return f;
}

static void bind_params(FuncDef *f, Type *sig, Node *sigNode, Node *recvfield){
    int slot = 0;
    if (recvfield){
        const char *nm = recvfield->list.len ? ((Node*)recvfield->list.p[0])->name : intern("_");
        Type *rt = f->recvtp.len ? (Type*)f->recvtp.p[0] : NULL;
        Sym *s = declare_local(nm?nm:intern("_"), rt?rt:ty_invalid, recvfield->pos);
        s->used = 1;
        slot++;
    }
    for (int i=0;i<sig->fields.len;i++){
        Field *fl = sig->fields.p[i];
        const char *nm = fl->name?fl->name:intern("_");
        declare_local(nm,fl->type,fl->pos)->used = 1;
        slot++;
    }
    f->nparams = slot;
    /* named results */
    int named = 0;
    for (int i=0;i<sig->results.len;i++){ Field *fl = sig->results.p[i]; if (fl->name && fl->name!=intern("_")) named=1; }
    if (named && sig->results.len){
        f->res_slot0 = C.env->nslots;
        f->nres_named = sig->results.len;
        for (int i=0;i<sig->results.len;i++){
            Field *fl = sig->results.p[i];
            const char *nm = fl->name?fl->name:xsprintf("~r%d",i);
            declare_local(intern(nm),fl->type,fl->pos)->used = 1;
        }
    }
}

static void compile_body_into(FuncDef *f, Node *body, Type *sig, Node *sigNode, Node *recvfield){
    FuncDef *savefn = C.fn; CEnv *saveenv = C.env; CEnv *savefnenv = C.fnenv;
    Scope *savescope = C.scope; LoopCtx *saveloop = C.loop;
    Vec savelabels = cur_labels;
    FRCtx *savefr = g_frctx;
    memset(&cur_labels,0,sizeof cur_labels);
    g_frctx = NULL;
    C.fn = f;
    if (!f->is_closure){ C.env = cenv_new(NULL); }
    else C.env = cenv_new(saveenv);
    C.fnenv = C.env;
    C.scope = scope_push(savescope);
    C.scope->env = C.env;
    C.loop = NULL;
    C.sigresults = sig;
    bind_params(f,sig,sigNode,recvfield);
    if (f->nlocals < C.env->nslots) f->nlocals = C.env->nslots;
    if (f->nres_named){
        for (int i=0;i<f->nres_named;i++){
            Field *fl = sig->results.p[i];
            E(OP_ZERO,0,0,fl->type,body);
            E(OP_STOREVAR,0,f->res_slot0+i,NULL,body);
        }
    }
    if (body) compile_block(body,1);
    if (f->nres_named) E(OP_RET,-2,0,NULL,body);
    else E(OP_RET,0,0,NULL,body);
    if (f->nlocals < C.env->nslots) f->nlocals = C.env->nslots;
    f->compiled = 1;
    for (int i=0;i<cur_labels.len;i++){
        LabelRec *l = cur_labels.p[i];
        if (l->pc<0 && l->gotos.len){ Pos zp; memset(&zp,0,sizeof zp); cerrorf(body?body->pos:zp,"label %s not defined",l->name); }
    }
    cur_labels = savelabels;
    g_frctx = savefr;
    C.fn = savefn; C.env = saveenv; C.fnenv = savefnenv; C.scope = savescope; C.loop = saveloop;
}

FuncDef *compile_funclit(Node *n){
    Type *sig = resolve_type(n->x);
    FuncDef *f = new_funcdef(xsprintf("%s.func", C.fn&&C.fn->name?C.fn->name:"main"),sig);
    f->is_closure = 1;
    f->captures = 0;
    f->variadic = sig->variadic;
    f->package = C.pkg;
    f->srcfile = n->pos.file;
    compile_body_into(f,n->y,sig,n->x,NULL);
    if (f->captures && C.fn) C.fn->captures = 1;
    return f;
}

FuncDef *make_thunk(Node *call){
    /* wrap a builtin call or conversion so it can be deferred */
    Ann *ca = ann(call);
    Type *sig = type_new(T_FUNC);
    for (int i=0;i<call->list.len;i++){
        Field *fl = xcalloc(1,sizeof *fl);
        Type *at = ann((Node*)call->list.p[i])->t;
        fl->type = at?default_type(at):ty_any;
        fl->name = intern(xsprintf("a%d",i));
        vec_push(&sig->fields,fl);
    }
    FuncDef *f = new_funcdef("thunk",sig);
    f->package = C.pkg;
    f->srcfile = call->pos.file;
    FuncDef *savefn = C.fn; CEnv *saveenv = C.env; CEnv *savefnenv = C.fnenv; Scope *savescope = C.scope;
    C.fn = f; C.env = cenv_new(NULL); C.fnenv = C.env;
    C.scope = scope_push(universe_scope()); C.scope->env = C.env;
    for (int i=0;i<sig->fields.len;i++){ Field *fl = sig->fields.p[i]; declare_local(fl->name,fl->type,call->pos); }
    f->nparams = sig->fields.len;
    f->nlocals = C.env->nslots;
    int b = ca->builtin;
    for (int i=0;i<sig->fields.len;i++) E(OP_LOADVAR,0,i,NULL,call);
    switch (b){
    case B_PANIC: E(OP_PANIC,0,0,NULL,call); break;
    case B_CLOSE: E(OP_CLOSE,0,0,NULL,call); break;
    case B_PRINT: case B_PRINTLN: E(OP_PRINT,sig->fields.len,b==B_PRINTLN,NULL,call); break;
    case B_DELETE: E(OP_MAPDEL,0,0,NULL,call); break;
    case B_CLEAR: E(OP_CLEAR,0,0,NULL,call); break;
    default: for (int i=0;i<sig->fields.len;i++) E(OP_POP,1,0,NULL,call); break;
    }
    E(OP_RET,0,0,NULL,call);
    if (f->nlocals < C.env->nslots) f->nlocals = C.env->nslots;
    C.fn = savefn; C.env = saveenv; C.fnenv = savefnenv; C.scope = savescope;
    return f;
}

/* ---------------------------------------------------- symbol resolution --- */

static void with_sym_scope(Sym *s, void (*fn)(Sym*)){ }

void resolve_func_sym(Sym *s){
    if (s->type) return;
    Node *d = s->decl;
    Package *savepkg = C.pkg; Scope *savescope = C.scope; int savelang = C.lang; int savegl = g_lang;
    C.pkg = s->pkg; C.scope = (Scope*)s->fscope; C.lang = s->pkg->lang; g_lang = s->pkg->lang;
    int nbind = g_tpbind.len;
    FuncDef *f = new_funcdef(s->name,NULL);
    f->package = s->pkg;
    f->decl = d;
    s->fn = f;
    /* receiver type params (methods on generic types) */
    if (d->w){
        if (!lang_at_least(18)) cerrorf(d->pos,"type parameters require go1.18 or later");
        int idx = 0;
        for (int i=0;i<d->w->list.len;i++){
            Node *fl = d->w->list.p[i];
            for (int j=0;j<fl->list.len;j++){
                Node *id = fl->list.p[j];
                Type *tp = type_new(T_TYPEPARAM); tp->name = id->name; tp->tpindex = idx++;
                vec_push(&f->tparams,tp);
                TPBind *b = xcalloc(1,sizeof *b); b->name=id->name; b->t=tp; vec_push(&g_tpbind,b);
            }
        }
        int k=0;
        for (int i=0;i<d->w->list.len;i++){
            Node *fl = d->w->list.p[i];
            Type *ct = resolve_type(fl->x);
            for (int j=0;j<fl->list.len;j++){ Type *tp = f->tparams.p[k++]; tp->constraint = ct; }
        }
    }
    Type *sig = resolve_type(d->y);
    f->sig = sig;
    f->variadic = sig->variadic;
    s->type = sig;
    g_tpbind.len = nbind;
    C.pkg = savepkg; C.scope = savescope; C.lang = savelang; g_lang = savegl;
}

void ensure_funcdef_compiled(FuncDef *f){ (void)f; }

void ensure_func_compiled(Sym *s){
    if (!s || s->compiled) return;
    if (!s->type) resolve_func_sym(s);
    if (!s->decl){ s->compiled = 1; return; }
    if (s->fn && !s->decl->z){
        s->compiled = 1;
        int nid = native_lookup(s->pkg->path, s->name);
        if (nid <= 0 && s->recvfield) nid = native_lookup(s->pkg->path, xsprintf("%s.%s","",s->name));
        if (nid > 0) s->fn->native = nid;
        else errorf(s->pos,"missing function body");
        return;
    }
    if (!s->fn) { s->compiled = 1; return; }
    if (s->fn->recvtp.len){
        Type *rt = s->fn->recvtp.p[0];
        Type *base = (rt && rt->k==T_PTR) ? rt->elem : rt;
        if (base && base->k==T_NAMED && base->tparams.len && base->targs.len==0){
            /* a method on a generic type is compiled per instantiation */
            s->compiled = 1;
            return;
        }
    }
    if (s->fn->tparams.len) return;   /* generic: compiled per instantiation */
    s->compiled = 1;
    Package *savepkg = C.pkg; Scope *savescope = C.scope; int savelang=C.lang; int savegl=g_lang;
    const char *savefile = C.file_name;
    C.pkg = s->pkg; C.scope = (Scope*)s->fscope; C.lang = s->pkg->lang; g_lang = s->pkg->lang;
    s->fn->srcfile = s->pos.file;
    compile_body_into(s->fn,s->decl->z,s->type,s->decl->y,s->recvfield);
    C.pkg = savepkg; C.scope = savescope; C.lang = savelang; g_lang = savegl; C.file_name = savefile;
}

void ensure_method_compiled(Method *m){
    if (!m || m->def) return;
    Sym *s = (Sym*)m->decl_sym;
    if (s){ ensure_func_compiled(s); m->def = s->fn; }
}

void compile_method_instance(Method *m, Type *inst, Vec *targs){
    Sym *s = (Sym*)m->decl_sym;
    if (!s || !s->decl || !s->decl->z) return;
    extern Node *receiver_base_pub(Node *tn, Vec *tpnames, int *isptr);
    FuncDef *f = new_funcdef(xsprintf("%s.%s",inst->name,m->name),NULL);
    f->package = s->pkg;
    f->decl = s->decl;
    vec_push(&f->recvtp, m->recv);
    m->def = f;
    Package *savepkg = C.pkg; Scope *savescope = C.scope; int savelang=C.lang; int savegl=g_lang;
    C.pkg = s->pkg; C.scope = (Scope*)s->fscope; C.lang = s->pkg->lang; g_lang = s->pkg->lang;
    int nbind = g_tpbind.len;
    Vec tpnames; memset(&tpnames,0,sizeof tpnames);
    int isptr = 0;
    receiver_base_pub(s->recvfield->x,&tpnames,&isptr);
    for (int i=0;i<tpnames.len && i<targs->len;i++){
        TPBind *b = xcalloc(1,sizeof *b);
        b->name = (const char*)tpnames.p[i];
        b->t = targs->p[i];
        vec_push(&g_tpbind,b);
    }
    ann_reset_tree(s->decl->y);
    ann_reset_tree(s->decl->z);
    ann_reset_tree(s->recvfield);
    Type *sig = resolve_type(s->decl->y);
    f->sig = sig;
    f->variadic = sig->variadic;
    f->compiled = 1;
    f->srcfile = s->pos.file;
    compile_body_into(f,s->decl->z,sig,s->decl->y,s->recvfield);
    g_tpbind.len = nbind;
    vec_free(&tpnames);
    C.pkg = savepkg; C.scope = savescope; C.lang = savelang; g_lang = savegl;
}

FuncDef *compile_generic_instance(Sym *s, Vec *targs){
    resolve_func_sym(s);
    FuncDef *orig = s->fn;
    FuncDef *f = new_funcdef(s->name,NULL);
    f->package = s->pkg;
    f->decl = s->decl;
    for (int i=0;i<targs->len;i++) vec_push(&f->recvtp,targs->p[i]);
    vec_push(&g_instfuncs,f);
    Package *savepkg = C.pkg; Scope *savescope = C.scope; int savelang=C.lang; int savegl=g_lang;
    C.pkg = s->pkg; C.scope = (Scope*)s->fscope; C.lang = s->pkg->lang; g_lang = s->pkg->lang;
    int nbind = g_tpbind.len;
    for (int i=0;i<orig->tparams.len && i<targs->len;i++){
        Type *tp = orig->tparams.p[i];
        TPBind *b = xcalloc(1,sizeof *b); b->name = tp->name; b->t = targs->p[i];
        vec_push(&g_tpbind,b);
    }
    /* the body AST is shared by every instantiation: clear the annotations
     * left by the previous one before checking it against these type
     * arguments */
    ann_reset_tree(s->decl->y);
    ann_reset_tree(s->decl->z);
    if (s->recvfield) ann_reset_tree(s->recvfield);
    Type *sig = resolve_type(s->decl->y);
    f->sig = sig;
    f->variadic = sig->variadic;
    f->compiled = 1;
    if (s->decl->z) compile_body_into(f,s->decl->z,sig,s->decl->y,s->recvfield);
    g_tpbind.len = nbind;
    C.pkg = savepkg; C.scope = savescope; C.lang = savelang; g_lang = savegl;
    return f;
}

void resolve_const_sym(Sym *s){
    if (s->cval) return;
    Node *spec = s->decl;
    if (!spec){ s->cval = const_int_i64(0); s->type = ty_untyped_int; return; }
    Package *savepkg=C.pkg; Scope *savescope=C.scope; int savelang=C.lang; int savegl=g_lang;
    int saveiota=C.iota_value, savehas=C.has_iota;
    C.pkg = s->pkg; C.scope = (Scope*)s->fscope; C.lang = s->pkg->lang; g_lang = s->pkg->lang;
    C.iota_value = spec->flag; C.has_iota = 1;
    Type *dt = spec->x ? resolve_type(spec->x) : NULL;
    int idx = s->globidx;
    Node *val = (idx < spec->list2.len) ? spec->list2.p[idx] : NULL;
    if (val){
        if (spec->w) ann_reset_tree(val);
        check_expr(val,dt);
        Ann *va = ann(val);
        if (va->mode != M_CONST){ cerrorf(val->pos,"%s is not constant",s->name); s->cval = const_int_i64(0); s->type = ty_untyped_int; }
        else { if (dt) convert_untyped(val,dt); va = ann(val); s->cval = va->c; s->type = dt?dt:va->t; }
    } else { s->cval = const_int_i64(0); s->type = dt?dt:ty_untyped_int; }
    C.iota_value=saveiota; C.has_iota=savehas;
    C.pkg=savepkg; C.scope=savescope; C.lang=savelang; g_lang=savegl;
}

void resolve_var_sym(Sym *s){
    if (s->type) return;
    Node *spec = s->decl;
    if (!spec){ s->type = ty_invalid; return; }
    Package *savepkg=C.pkg; Scope *savescope=C.scope; int savelang=C.lang; int savegl=g_lang;
    C.pkg = s->pkg; C.scope = (Scope*)s->fscope; C.lang = s->pkg->lang; g_lang = s->pkg->lang;
    if (spec->x) s->type = resolve_type(spec->x);
    else {
        int idx = s->specidx;
        Node *val = (idx < spec->list2.len) ? spec->list2.p[idx] : (spec->list2.len==1?spec->list2.p[0]:NULL);
        if (val){
            check_expr(val,NULL);
            Ann *va = ann(val);
            Type *vt = va->t;
            if (vt && vt->k==T_TUPLE) vt = (idx<vt->tuple.len)?vt->tuple.p[idx]:ty_invalid;
            s->type = default_type(vt?vt:ty_invalid);
        } else s->type = ty_invalid;
    }
    C.pkg=savepkg; C.scope=savescope; C.lang=savelang; g_lang=savegl;
}

/* ============================ package driver ============================ */

Vec g_packages;

Package *pkg_lookup(const char *path){
    for (int i=0;i<g_packages.len;i++){
        Package *p = g_packages.p[i];
        if (p->path == path) return p;
    }
    return NULL;
}

Node *receiver_base_pub(Node *tn, Vec *tpnames, int *isptr);
static Node *receiver_base(Node *tn, Vec *tpnames, int *isptr){
    *isptr = 0;
    if (tn->kind==N_STAR){ *isptr = 1; tn = tn->x; }
    if (tn->kind==N_INDEX){ if (tpnames && tn->y->kind==N_IDENT) vec_push(tpnames,(void*)tn->y->name); tn = tn->x; }
    else if (tn->kind==N_INDEXLIST){
        if (tpnames) for (int i=0;i<tn->list.len;i++){ Node *e=tn->list.p[i]; if (e->kind==N_IDENT) vec_push(tpnames,(void*)e->name); }
        tn = tn->x;
    }
    return tn;
}

Node *receiver_base_pub(Node *tn, Vec *tpnames, int *isptr){ return receiver_base(tn,tpnames,isptr); }

static void collect_globals_refs(Node *n, Package *p, Vec *out){
    if (!n) return;
    if (n->kind==N_IDENT){
        for (int i=0;i<p->syms.len;i++){
            Sym *s = p->syms.p[i];
            if (s->name==n->name && (s->kind==SYM_VAR || s->kind==SYM_FUNC)) vec_push(out,s);
        }
    }
    collect_globals_refs(n->x,p,out); collect_globals_refs(n->y,p,out);
    collect_globals_refs(n->z,p,out); collect_globals_refs(n->w,p,out);
    for (int i=0;i<n->list.len;i++) collect_globals_refs(n->list.p[i],p,out);
    for (int i=0;i<n->list2.len;i++) collect_globals_refs(n->list2.p[i],p,out);
}

typedef struct VarInit { Sym **syms; int nsyms; Node *spec; Scope *fscope; int done; Vec deps; } VarInit;

void check_package(Package *p){
    if (p->checked) return;
    p->checked = 1;
    Package *savepkg = C.pkg; Scope *savescope = C.scope; int savelang = C.lang; int savegl = g_lang;
    FuncDef *savefn = C.fn; CEnv *saveenv = C.env, *savefnenv = C.fnenv;
    C.pkg = p;
    C.lang = p->lang;
    g_lang = p->lang;
    Scope *pkgscope = scope_push(universe_scope());
    Vec filescopes; memset(&filescopes,0,sizeof filescopes);

    /* imports first */
    for (int fi=0; fi<p->files.len; fi++){
        Node *file = p->files.p[fi];
        Scope *fs = scope_push(pkgscope);
        vec_push(&filescopes,fs);
        for (int i=0;i<file->list2.len;i++){
            Node *d = file->list2.p[i];
            for (int j=0;j<d->list.len;j++){
                Node *imp = d->list.p[j];
                if (!imp->x) continue;
                const char *path = imp->x->name;
                /* Go 1.5 internal-package rule: an import path containing an
                 * "internal" element is importable only from within the tree
                 * rooted at that element's parent. */
                {
                    const char *ip = path;
                    const char *found = NULL;
                    if (strncmp(ip,"internal/",9)==0 || strcmp(ip,"internal")==0) found = ip;
                    else { const char *q = strstr(ip,"/internal"); if (q && (q[9]=='/'||q[9]==0)) found = q+1; }
                    if (found){
                        size_t plen = (size_t)(found - ip);
                        if (plen > 0) plen--;                 /* drop the trailing '/' */
                        const char *me = p->path;
                        int ok = (strncmp(me,ip,plen)==0 && (me[plen]==0 || me[plen]=='/'));
                        if (plen == 0) ok = (strchr(me,'/')==NULL) || strncmp(me,"internal",8)==0
                                            || pkg_lookup(intern(me)) == NULL;
                        if (plen == 0){
                            /* a top-level "internal" is importable only from the
                             * module (or GOROOT) root tree it belongs to */
                            ok = 1;
                        }
                        if (!ok){
                            errorf(imp->pos,"use of internal package %s not allowed",path);
                            extern Package *new_error_package(const char *path);
                            Package *bad = new_error_package(path);
                            Sym *ps = xcalloc(1,sizeof *ps);
                            ps->name = imp->name ? imp->name : intern(path_base((char*)path));
                            ps->kind = SYM_PKG; ps->type = (Type*)bad; ps->pos = imp->pos; ps->used = 1;
                            vec_push(&fs->names,ps);
                            continue;
                        }
                    }
                }
                Package *dep = load_package(path,p->dir,p->lang);
                if (!dep){
                    errorf(imp->pos,"package %s is not in std",path);
                    extern Package *new_error_package(const char *path);
                    Package *bad = new_error_package(path);
                    Sym *ps = xcalloc(1,sizeof *ps);
                    ps->name = imp->name ? imp->name : intern(path_base((char*)path));
                    ps->kind = SYM_PKG; ps->type = (Type*)bad; ps->pos = imp->pos; ps->used = 1;
                    vec_push(&fs->names,ps);
                    continue;
                }
                vec_push(&p->imports,dep);
                const char *local = imp->name ? imp->name : dep->name;
                if (local == intern(".")){
                    for (int k=0;k<dep->syms.len;k++){
                        Sym *ds = dep->syms.p[k];
                        if (ds->name[0]>='A'&&ds->name[0]<='Z') vec_push(&fs->names,ds);
                    }
                    continue;
                }
                if (local == intern("_")) continue;
                Sym *ps = xcalloc(1,sizeof *ps);
                ps->name = local; ps->kind = SYM_PKG; ps->type = (Type*)dep; ps->pos = imp->pos;
                vec_push(&fs->names,ps);
            }
        }
    }
    /* collect top-level declarations */
    Vec methods; memset(&methods,0,sizeof methods);
    Vec varinits; memset(&varinits,0,sizeof varinits);
    for (int fi=0; fi<p->files.len; fi++){
        Node *file = p->files.p[fi];
        Scope *fs = filescopes.p[fi];
        for (int i=0;i<file->list.len;i++){
            Node *d = file->list.p[i];
            if (d->kind==N_FUNCDECL){
                if (d->x){ vec_push(&methods,d); ((Ann*)ann(d))->aux = fs; continue; }
                Sym *s = new_sym(p,d->name,SYM_FUNC,d,d->pos);
                s->fscope = fs;
                vec_push(&p->syms,s);
                vec_push(&pkgscope->names,s);
                continue;
            }
            if (d->kind!=N_GENDECL) continue;
            if (d->op==TOK_TYPE){
                for (int j=0;j<d->list.len;j++){
                    Node *spec = d->list.p[j];
                    Sym *s = new_sym(p,spec->name,SYM_TYPE,spec,spec->pos);
                    s->fscope = fs;
                    vec_push(&p->syms,s);
                    vec_push(&pkgscope->names,s);
                }
            } else if (d->op==TOK_CONST){
                for (int j=0;j<d->list.len;j++){
                    Node *spec = d->list.p[j];
                    for (int k=0;k<spec->list.len;k++){
                        Node *id = spec->list.p[k];
                        Sym *s = new_sym(p,id->name,SYM_CONST,spec,id->pos);
                        s->fscope = fs; s->globidx = k;
                        vec_push(&p->syms,s);
                        vec_push(&pkgscope->names,s);
                    }
                }
            } else if (d->op==TOK_VAR){
                for (int j=0;j<d->list.len;j++){
                    Node *spec = d->list.p[j];
                    VarInit *vi = xcalloc(1,sizeof *vi);
                    vi->spec = spec; vi->fscope = fs;
                    vi->syms = xcalloc((size_t)spec->list.len,sizeof(Sym*));
                    vi->nsyms = spec->list.len;
                    for (int k=0;k<spec->list.len;k++){
                        Node *id = spec->list.p[k];
                        Sym *s = new_sym(p,id->name,SYM_VAR,spec,id->pos);
                        s->fscope = fs;
                        s->specidx = k;
                        s->builtin_id = k;
                        s->globidx = p->nglobals++;
                        vi->syms[k] = s;
                        vec_push(&p->syms,s);
                        vec_push(&pkgscope->names,s);
                    }
                    vec_push(&varinits,vi);
                }
            }
        }
    }
    /* mark globals: builtin_id is the spec index; use a separate marker */
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind==SYM_VAR) s->builtin_id = -1;
    }
    /* attach methods to their receiver types */
    for (int i=0;i<methods.len;i++){
        Node *d = methods.p[i];
        Scope *fs = (Scope*)((Ann*)ann(d))->aux;
        C.scope = fs;
        Vec tpnames; memset(&tpnames,0,sizeof tpnames);
        int isptr = 0;
        Node *basen = receiver_base(d->x->x,&tpnames,&isptr);
        if (basen->kind != N_IDENT){ errorf(d->pos,"invalid receiver type"); continue; }
        Sym *ts = NULL;
        for (int j=0;j<p->syms.len;j++){ Sym *s=p->syms.p[j]; if (s->name==basen->name && s->kind==SYM_TYPE){ ts=s; break; } }
        if (!ts){ errorf(d->pos,"undefined type %s in method receiver",basen->name); continue; }
        Type *bt = named_type_of(ts);
        int nbind = g_tpbind.len;
        for (int j=0;j<tpnames.len && j<bt->tparams.len;j++){
            TPBind *b = xcalloc(1,sizeof *b);
            b->name = (const char*)tpnames.p[j];
            b->t = bt->tparams.p[j];
            vec_push(&g_tpbind,b);
        }
        Sym *fsym = new_sym(p,d->name,SYM_FUNC,d,d->pos);
        fsym->fscope = fs;
        fsym->recvfield = d->x;
        FuncDef *f = new_funcdef(xsprintf("%s.%s",bt->name,d->name),NULL);
        f->package = p; f->decl = d;
        fsym->fn = f;
        Type *rt = isptr ? type_ptr(bt) : bt;
        vec_push(&f->recvtp,rt);
        Type *sig = resolve_type(d->y);
        f->sig = sig; f->variadic = sig->variadic;
        fsym->type = sig;
        Method *m = xcalloc(1,sizeof *m);
        m->name = d->name; m->sig = sig; m->recv = rt; m->ptr_recv = isptr; m->pos = d->pos;
        m->decl_sym = fsym;
        for (int j=0;j<bt->methods.len;j++){
            Method *om = bt->methods.p[j];
            if (om->name==m->name){ errorf(d->pos,"method %s.%s already declared",bt->name,d->name); }
        }
        vec_push(&bt->methods,m);
        g_tpbind.len = nbind;
        vec_free(&tpnames);
    }
    /* resolve every declared type and constant */
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        C.scope = (Scope*)s->fscope;
        if (s->kind==SYM_TYPE) named_type_of(s);
    }
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind==SYM_CONST) resolve_const_sym(s);
    }
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind==SYM_FUNC && !s->type){ C.scope=(Scope*)s->fscope; resolve_func_sym(s); }
    }
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind==SYM_VAR && !s->type){ C.scope=(Scope*)s->fscope; resolve_var_sym(s); }
    }
    /* globals storage */
    p->globstore = gc_alloc_cells(p->nglobals+1);
    gc_pin_obj(p->globstore);
    { Value *cells = OBJ_CELLPTR(p->globstore);
      for (int i=0;i<p->syms.len;i++){ Sym *s=p->syms.p[i]; if (s->kind==SYM_VAR && s->globidx>=0) cells[s->globidx] = val_zero(s->type); } }

    /* package init function: variable initialisers then init() */
    FuncDef *initf = new_funcdef(xsprintf("%s.init",p->name),type_new(T_FUNC));
    initf->package = p;
    C.fn = initf;
    C.env = cenv_new(NULL);
    C.fnenv = C.env;
    Scope *initscope = scope_push(pkgscope);
    initscope->env = C.env;
    C.scope = initscope;
    /* dependency-ordered initialisation */
    for (int i=0;i<varinits.len;i++){
        VarInit *vi = varinits.p[i];
        collect_globals_refs(vi->spec->list2.len?NULL:NULL,p,&vi->deps);
        for (int j=0;j<vi->spec->list2.len;j++) collect_globals_refs(vi->spec->list2.p[j],p,&vi->deps);
    }
    int remaining = varinits.len;
    int guard = 0;
    while (remaining > 0 && guard++ < varinits.len+2){
        for (int i=0;i<varinits.len;i++){
            VarInit *vi = varinits.p[i];
            if (vi->done) continue;
            int ready = 1;
            for (int j=0;j<vi->deps.len;j++){
                Sym *dep = vi->deps.p[j];
                if (dep->kind != SYM_VAR) continue;
                for (int k=0;k<varinits.len;k++){
                    VarInit *ov = varinits.p[k];
                    if (ov==vi || ov->done) continue;
                    for (int m=0;m<ov->nsyms;m++) if (ov->syms[m]==dep) ready = 0;
                }
            }
            if (!ready) continue;
            vi->done = 1; remaining--;
            C.scope = scope_push(pkgscope);
            C.scope->env = C.env;
            ((Scope*)C.scope)->parent = vi->fscope;
            Node *spec = vi->spec;
            int nl = spec->list.len, nr = spec->list2.len;
            if (nr==0) continue;
            if (nr==1 && nl>1){
                Node *r = spec->list2.p[0];
                Ann *ra = ann(r);
                if (ra->mode==M_INVALID) check_expr(r,NULL);
                emit_expr(r);
                for (int j=nl-1;j>=0;j--) E(OP_STOREGLOB,vi->syms[j]->globidx,0,p,spec);
            } else {
                for (int j=0;j<nl && j<nr;j++){
                    Node *val = spec->list2.p[j];
                    Ann *va = ann(val);
                    Type *dt = vi->syms[j]->type;
                    if (va->mode==M_INVALID) check_expr(val,dt);
                    convert_untyped(val,dt);
                    emit_expr(val);
                    E(OP_STOREGLOB,vi->syms[j]->globidx,0,p,spec);
                }
            }
        }
    }
    /* //go:embed directives: initialise the annotated variables from the
     * package directory before any user init runs */
    {
        extern int embed_expand(const char *dir, const char *patterns, Vec *names, Vec *datas);
        for (int fi=0; fi<p->files.len; fi++){
            Node *file = p->files.p[fi];
            Vec *comments = (Vec*)file->xtra;
            if (!comments) continue;
            for (int i=0;i<file->list.len;i++){
                Node *d = file->list.p[i];
                if (d->kind!=N_GENDECL || d->op!=TOK_VAR) continue;
                for (int j=0;j<d->list.len;j++){
                    Node *spec = d->list.p[j];
                    const char *pat = NULL;
                    Pos patpos = spec->pos;
                    for (int k=0;k<comments->len;k++){
                        Comment *cm = comments->p[k];
                        if (cm->pos.file != spec->pos.file) continue;
                        if (cm->pos.line >= spec->pos.line || cm->pos.line < spec->pos.line-3) continue;
                        if (strncmp(cm->text,"//go:embed ",11)==0){
                            pat = cm->text+11;
                            /* gc reports embed problems at the pattern inside
                             * the directive, not at the variable it annotates */
                            patpos = cm->pos;
                            patpos.col += 11;
                        }
                    }
                    if (!pat) continue;
                    if (spec->list.len != 1){ errorf(spec->pos,"go:embed cannot apply to multiple vars"); continue; }
                    Sym *vs = NULL;
                    for (int k=0;k<p->syms.len;k++){
                        Sym *cand = p->syms.p[k];
                        if (cand->kind==SYM_VAR && cand->decl==spec && cand->name==((Node*)spec->list.p[0])->name){ vs = cand; break; }
                    }
                    if (!vs) continue;
                    if (!vs->type) resolve_var_sym(vs);
                    Vec names, datas; memset(&names,0,sizeof names); memset(&datas,0,sizeof datas);
                    if (embed_expand(p->dir,pat,&names,&datas) != 0){
                        errorf(patpos,"pattern %s: no matching files found",pat);
                        continue;
                    }
                    C.fn = initf;
                    Type *vt = vs->type;
                    Type *vu = underlying(vt);
                    if (vu->k==T_STRING || (vu->k==T_SLICE && underlying(vu->elem)->k==T_UINT8)){
                        if (names.len != 1){ errorf(spec->pos,"invalid go:embed: multiple files for %s",type_string(vt)); continue; }
                        Value sv = val_string((const char*)datas.p[0], strlen((const char*)datas.p[0]));
                        gc_pin_obj(sv.o);
                        E(OP_CONST,addconst(sv),0,NULL,spec);
                        if (vu->k!=T_STRING) E(OP_CONV,0,0,vt,spec);
                        E(OP_STOREGLOB,vs->globidx,0,p,spec);
                    } else {
                        /* embed.FS: hand the names and contents to embed.newFS */
                        Package *ep = pkg_lookup(intern("embed"));
                        Sym *mk = ep ? pkg_sym(ep,intern("newFS")) : NULL;
                        if (!mk){ errorf(spec->pos,"go:embed requires importing \"embed\""); continue; }
                        ensure_func_compiled(mk);
                        E(OP_CLOSURE,0,0,mk->fn,spec);
                        Type *sst = type_slice(ty_string);
                        for (int pass=0; pass<2; pass++){
                            Vec *src = pass ? &datas : &names;
                            for (int k=0;k<src->len;k++){
                                Value iv = val_int(k,ty_int);
                                E(OP_CONST,addconst(iv),0,NULL,spec);
                                Value sv = val_string((const char*)src->p[k], strlen((const char*)src->p[k]));
                                gc_pin_obj(sv.o);
                                E(OP_CONST,addconst(sv),0,NULL,spec);
                            }
                            E(OP_COMPOSITE_SLICE,src->len,src->len,sst,spec);
                        }
                        E(OP_CALL,2,0,NULL,spec);
                        E(OP_STOREGLOB,vs->globidx,0,p,spec);
                    }
                }
            }
        }
    }

    /* run init() functions */
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind==SYM_FUNC && s->name==intern("init") && s->decl && s->decl->z && !s->recvfield){
            ensure_func_compiled(s);
            C.fn = initf;
            E(OP_CLOSURE,0,0,s->fn,s->decl);
            E(OP_CALL,0,0,NULL,s->decl);
            s->name = intern("init#");
        }
    }
    C.fn = initf;
    E(OP_RET,0,0,NULL,NULL);
    initf->nlocals = C.env->nslots;
    vec_push(&p->init_funcs,initf);

    /* compile all function bodies */
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind==SYM_FUNC) ensure_func_compiled(s);
    }
    /* every method must have a compiled body: interface dispatch resolves at run time */
    for (int i=0;i<p->syms.len;i++){
        Sym *s = p->syms.p[i];
        if (s->kind!=SYM_TYPE || !s->type) continue;
        Type *t = s->type;
        for (int j=0;j<t->methods.len;j++) ensure_method_compiled(t->methods.p[j]);
    }
    for (int fi=0; fi<filescopes.len; fi++){
        Scope *fs = filescopes.p[fi];
        for (int i=0;i<fs->names.len;i++){
            Sym *s = fs->names.p[i];
            if (s->kind==SYM_PKG && !s->used)
                errorf(s->pos,"\"%s\" imported and not used",((Package*)s->type)->path);
        }
    }
    C.pkg = savepkg; C.scope = savescope; C.lang = savelang; g_lang = savegl;
    C.fn = savefn; C.env = saveenv; C.fnenv = savefnenv;
}
