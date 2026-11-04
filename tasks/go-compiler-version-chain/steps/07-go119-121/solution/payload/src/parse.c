#include "go.h"
#include <stdarg.h>

Vec g_file_comments;

Node *node_new(NodeKind k, Pos pos){
    Node *n = xcalloc(1,sizeof *n);
    n->kind = k; n->pos = pos;
    return n;
}

typedef struct P {
    Lexer lx;
    Token tok;
    int exprlev;     /* < 0 means composite literals need parens */
    int nerr;
    int lang;
} P;

static void next(P *p){ p->tok = lex_next(&p->lx); }
static int at(P *p, TokKind k){ return p->tok.kind == k; }

static void perr(P *p, const char *fmt, ...){
    va_list ap; va_start(ap,fmt);
    char *m = xmalloc(512);
    vsnprintf(m,512,fmt,ap); va_end(ap);
    if (p->nerr < 10) errorf(p->tok.pos, "syntax error: %s", m);
    p->nerr++;
}

static Token expect(P *p, TokKind k){
    Token t = p->tok;
    if (p->tok.kind != k){
        perr(p, "unexpected %s, expected %s",
             p->tok.kind==TOK_SEMICOLON && strcmp(p->tok.lit,"\n")==0 ? "newline" : tok_str(p->tok.kind),
             tok_str(k));
        /* do not advance on mismatch for closing tokens to avoid runaway */
        if (k==TOK_SEMICOLON || k==TOK_RBRACE || k==TOK_RPAREN || k==TOK_RBRACK) return t;
    }
    next(p);
    return t;
}
static int got(P *p, TokKind k){ if (at(p,k)){ next(p); return 1; } return 0; }

static void expect_semi(P *p){
    if (at(p,TOK_RPAREN) || at(p,TOK_RBRACE)) return;
    if (at(p,TOK_SEMICOLON)){ next(p); return; }
    perr(p,"unexpected %s at end of statement", tok_str(p->tok.kind));
    while (!at(p,TOK_SEMICOLON) && !at(p,TOK_EOF) && !at(p,TOK_RBRACE)) next(p);
    got(p,TOK_SEMICOLON);
}

static Node *parse_expr(P *p);
static Node *parse_type(P *p);
static Node *try_type(P *p);
static Node *parse_block(P *p);
static Node *parse_stmt(P *p);
static Node *parse_simple_stmt(P *p, int labelok, Node **label_out);

/* ---------------------------------------------------------------- types --- */


static int starts_type_tok(TokKind k){
    switch (k){
    case TOK_IDENT: case TOK_LBRACK: case TOK_STRUCT: case TOK_MUL: case TOK_FUNC:
    case TOK_INTERFACE: case TOK_MAP: case TOK_CHAN: case TOK_ARROW: case TOK_LPAREN:
    case TOK_ELLIPSIS: case TOK_TILDE:
        return 1;
    default: return 0;
    }
}

/* At '[': decide whether the preceding identifier is a parameter/field NAME
 * followed by an array or slice type ("a []int", "a [N]int"), or is itself a
 * generic type being instantiated ("Pair[int]"). */
static int bracket_then_type(P *p){
    Lexer sl = p->lx; Token st = p->tok;
    int depth = 0;
    do {
        if (at(p,TOK_LBRACK)) depth++;
        else if (at(p,TOK_RBRACK)) depth--;
        next(p);
    } while (depth > 0 && !at(p,TOK_EOF));
    int r = starts_type_tok(p->tok.kind);
    p->lx = sl; p->tok = st;
    return r;
}

static Node *parse_ident(P *p){
    Node *n = node_new(N_IDENT, p->tok.pos);
    if (at(p,TOK_IDENT)){ n->name = p->tok.lit; next(p); }
    else { perr(p,"unexpected %s, expected name", tok_str(p->tok.kind)); n->name = intern("_"); }
    return n;
}

static Node *parse_typename(P *p){
    Node *n = parse_ident(p);
    if (at(p,TOK_PERIOD)){
        next(p);
        Node *sel = node_new(N_SELECTOR, n->pos);
        sel->x = n; sel->name = at(p,TOK_IDENT)?p->tok.lit:intern("_");
        if (at(p,TOK_IDENT)) next(p); else perr(p,"expected name after '.'");
        n = sel;
    }
    if (at(p,TOK_LBRACK) && p->lang != 0 && p->lang < 18) return n;
    if (at(p,TOK_LBRACK)){
        /* generic instantiation: Name[T1, T2] */
        Pos bp = p->tok.pos;
        next(p);
        if (at(p,TOK_RBRACK)){ perr(p,"expected type argument"); next(p); return n; }
        Node *ix = node_new(N_INDEXLIST, bp);
        ix->x = n;
        int save = p->exprlev; p->exprlev = 1;
        for (;;){
            Node *t = parse_type(p);
            vec_push(&ix->list, t);
            if (!got(p,TOK_COMMA)) break;
            if (at(p,TOK_RBRACK)) break;
        }
        p->exprlev = save;
        expect(p,TOK_RBRACK);
        return ix;
    }
    return n;
}

static void parse_field_decl_list(P *p, Node *st);

static Node *parse_struct_type(P *p){
    Node *n = node_new(N_STRUCTTYPE, p->tok.pos);
    expect(p,TOK_STRUCT);
    expect(p,TOK_LBRACE);
    while (!at(p,TOK_RBRACE) && !at(p,TOK_EOF)){
        parse_field_decl_list(p,n);
    }
    expect(p,TOK_RBRACE);
    return n;
}

static void parse_field_decl_list(P *p, Node *st){
    Node *f = node_new(N_FIELD, p->tok.pos);
    /* embedded: *T, T, pkg.T possibly followed by tag/semicolon */
    if (at(p,TOK_MUL)){
        next(p);
        Node *t = parse_typename(p);
        Node *star = node_new(N_STAR, f->pos); star->x = t;
        f->x = star; f->flag = 1;
    } else if (at(p,TOK_IDENT)){
        /* could be name list or embedded */
        Vec names; memset(&names,0,sizeof names);
        Node *first = parse_ident(p);
        if (at(p,TOK_PERIOD)){
            next(p);
            Node *sel = node_new(N_SELECTOR, first->pos);
            sel->x = first; sel->name = p->tok.lit;
            expect(p,TOK_IDENT);
            f->x = sel; f->flag = 1;
        } else if (at(p,TOK_SEMICOLON) || at(p,TOK_RBRACE) || at(p,TOK_STRING)){
            f->x = first; f->flag = 1;
        } else if (at(p,TOK_LBRACK) && !bracket_then_type(p)) {
            /* embedded generic instantiation  Foo[int] */
            Pos bp = p->tok.pos; next(p);
            Node *ix = node_new(N_INDEXLIST, bp); ix->x = first;
            for(;;){ vec_push(&ix->list, parse_type(p)); if(!got(p,TOK_COMMA)) break; if (at(p,TOK_RBRACK)) break; }
            expect(p,TOK_RBRACK);
            f->x = ix; f->flag = 1;
        } else {
            vec_push(&names, first);
            while (got(p,TOK_COMMA)) vec_push(&names, parse_ident(p));
            f->list = names;
            f->x = parse_type(p);
        }
    } else {
        perr(p,"unexpected %s in struct field", tok_str(p->tok.kind));
        while (!at(p,TOK_SEMICOLON) && !at(p,TOK_RBRACE) && !at(p,TOK_EOF)) next(p);
    }
    if (at(p,TOK_STRING)){ f->name = p->tok.lit; next(p); }
    vec_push(&st->list, f);
    if (!at(p,TOK_RBRACE)) expect_semi(p);
}

static Node *parse_params(P *p, int *variadic);

static Node *parse_signature(P *p){
    Node *ft = node_new(N_FUNCTYPE, p->tok.pos);
    int variadic = 0;
    ft->x = parse_params(p, &variadic);
    ft->flag = variadic;
    /* results */
    if (at(p,TOK_LPAREN)){
        int v2=0;
        ft->y = parse_params(p,&v2);
    } else {
        Node *t = try_type(p);
        if (t){
            Node *res = node_new(N_FUNCTYPE, t->pos);
            Node *f = node_new(N_FIELD, t->pos); f->x = t;
            vec_push(&res->list, f);
            ft->y = res;
        }
    }
    return ft;
}

/* returns a node whose ->list is a Vec of N_FIELD */
static Node *parse_params(P *p, int *variadic){
    Node *g = node_new(N_FUNCTYPE, p->tok.pos);
    expect(p,TOK_LPAREN);
    if (at(p,TOK_RPAREN)){ next(p); return g; }
    /* Parse a list of "entries"; each entry is [names] type or just type. */
    Vec entries; memset(&entries,0,sizeof entries);
    int named = 0;
    for (;;){
        Node *e = node_new(N_FIELD, p->tok.pos);
        /* try: IDENT followed by a type-start token => named */
        if (at(p,TOK_IDENT)){
            Token save_tok = p->tok;
            Lexer save_lx = p->lx;
            Node *id = parse_ident(p);
            if (at(p,TOK_COMMA) || at(p,TOK_RPAREN) || at(p,TOK_PERIOD)
                || (at(p,TOK_LBRACK) && !bracket_then_type(p))){
                /* ambiguous: could be a bare type name. Record as "maybe name". */
                if (at(p,TOK_PERIOD) || at(p,TOK_LBRACK)){
                    /* it's a qualified type or generic instantiation: restore */
                    p->tok = save_tok; p->lx = save_lx;
                    e->x = parse_type(p);
                } else {
                    e->name = id->name;      /* maybe-name, resolved below */
                    e->flag = 1;             /* flag=1: bare ident, ambiguous */
                }
            } else if (at(p,TOK_ELLIPSIS)){
                Pos ep = p->tok.pos; next(p);
                Node *el = node_new(N_ELLIPSIS, ep); el->x = parse_type(p);
                vec_push(&e->list, id); e->x = el; named = 1; *variadic = 1;
            } else {
                vec_push(&e->list, id);
                e->x = parse_type(p);
                named = 1;
            }
        } else if (at(p,TOK_ELLIPSIS)){
            Pos ep = p->tok.pos; next(p);
            Node *el = node_new(N_ELLIPSIS, ep); el->x = parse_type(p);
            e->x = el; *variadic = 1;
        } else {
            e->x = parse_type(p);
        }
        vec_push(&entries, e);
        if (!got(p,TOK_COMMA)) break;
        if (at(p,TOK_RPAREN)) break;
    }
    expect(p,TOK_RPAREN);
    if (named){
        /* Grouped names: a, b int  => entries with flag=1 belong to the next typed entry */
        Vec pending; memset(&pending,0,sizeof pending);
        for (int i=0;i<entries.len;i++){
            Node *e = entries.p[i];
            if (e->flag && !e->x){
                Node *id = node_new(N_IDENT, e->pos); id->name = e->name;
                vec_push(&pending, id);
            } else {
                if (pending.len){
                    /* pending names come first, then this entry's own names */
                    int own = e->list.len;
                    void **ownp = xmalloc((size_t)(own?own:1)*sizeof(void*));
                    for (int j=0;j<own;j++) ownp[j] = e->list.p[j];
                    e->list.len = 0;
                    for (int j=0;j<pending.len;j++) vec_push(&e->list, pending.p[j]);
                    for (int j=0;j<own;j++) vec_push(&e->list, ownp[j]);
                    free(ownp);
                    pending.len = 0;
                }
                e->flag = 0;
                vec_push(&g->list, e);
            }
        }
        for (int j=0;j<pending.len;j++){
            Node *id = pending.p[j];
            perr(p,"mixed named and unnamed parameters");
            Node *e = node_new(N_FIELD, id->pos); e->x = id;
            vec_push(&g->list, e);
        }
        vec_free(&pending);
    } else {
        for (int i=0;i<entries.len;i++){
            Node *e = entries.p[i];
            if (e->flag && !e->x){
                Node *id = node_new(N_IDENT, e->pos); id->name = e->name;
                e->x = id; e->flag = 0; e->name = NULL;
            }
            vec_push(&g->list, e);
        }
    }
    vec_free(&entries);
    return g;
}

static Node *parse_typeparams(P *p){
    /* assumes current token is '[' */
    Node *g = node_new(N_FUNCTYPE, p->tok.pos);
    expect(p,TOK_LBRACK);
    for (;;){
        if (at(p,TOK_RBRACK)) break;
        Node *e = node_new(N_FIELD, p->tok.pos);
        vec_push(&e->list, parse_ident(p));
        while (got(p,TOK_COMMA)){
            if (at(p,TOK_RBRACK)) break;
            vec_push(&e->list, parse_ident(p));
        }
        e->x = parse_type(p);   /* constraint */
        vec_push(&g->list, e);
        if (!got(p,TOK_COMMA)) break;
    }
    expect(p,TOK_RBRACK);
    return g;
}

static Node *parse_interface_type(P *p){
    Node *n = node_new(N_INTERFACETYPE, p->tok.pos);
    expect(p,TOK_INTERFACE);
    expect(p,TOK_LBRACE);
    while (!at(p,TOK_RBRACE) && !at(p,TOK_EOF)){
        Node *f = node_new(N_FIELD, p->tok.pos);
        if (at(p,TOK_IDENT)){
            Token save_tok = p->tok; Lexer save_lx = p->lx;
            Node *id = parse_ident(p);
            if (at(p,TOK_LPAREN)){
                f->name = id->name;
                f->x = parse_signature(p);
                f->flag = 0;
            } else {
                p->tok = save_tok; p->lx = save_lx;
                f->x = parse_type(p);
                f->flag = 1;
                while (got(p,TOK_OR)){
                    Node *u = node_new(N_BINARY, f->pos); u->op = TOK_OR; u->x = f->x; u->y = parse_type(p);
                    f->x = u;
                }
            }
        } else {
            f->x = parse_type(p);
            f->flag = 1;
            while (got(p,TOK_OR)){
                Node *u = node_new(N_BINARY, f->pos); u->op = TOK_OR; u->x = f->x; u->y = parse_type(p);
                f->x = u;
            }
        }
        vec_push(&n->list, f);
        if (!at(p,TOK_RBRACE)) expect_semi(p);
    }
    expect(p,TOK_RBRACE);
    return n;
}

static Node *try_type(P *p){
    Pos pos = p->tok.pos;
    switch (p->tok.kind){
    case TOK_IDENT: return parse_typename(p);
    case TOK_LBRACK: {
        next(p);
        if (got(p,TOK_RBRACK)){
            Node *n = node_new(N_ARRAYTYPE,pos); n->y = parse_type(p); return n;
        }
        Node *n = node_new(N_ARRAYTYPE,pos);
        if (at(p,TOK_ELLIPSIS)){ next(p); n->flag = 1; }
        else { int save=p->exprlev; p->exprlev=1; n->x = parse_expr(p); p->exprlev=save; }
        expect(p,TOK_RBRACK);
        n->y = parse_type(p);
        return n;
    }
    case TOK_STRUCT: return parse_struct_type(p);
    case TOK_MUL: { next(p); Node *n=node_new(N_STAR,pos); n->x = parse_type(p); return n; }
    case TOK_FUNC: {
        next(p);
        Node *sig = parse_signature(p);
        sig->pos = pos;
        return sig;
    }
    case TOK_INTERFACE: return parse_interface_type(p);
    case TOK_MAP: {
        next(p); expect(p,TOK_LBRACK);
        Node *n = node_new(N_MAPTYPE,pos);
        n->x = parse_type(p);
        expect(p,TOK_RBRACK);
        n->y = parse_type(p);
        return n;
    }
    case TOK_CHAN: {
        next(p);
        Node *n = node_new(N_CHANTYPE,pos);
        n->flag = 0;
        if (got(p,TOK_ARROW)) n->flag = 1; /* send-only */
        n->x = parse_type(p);
        return n;
    }
    case TOK_ARROW: {
        next(p); expect(p,TOK_CHAN);
        Node *n = node_new(N_CHANTYPE,pos);
        n->flag = 2; /* recv only */
        n->x = parse_type(p);
        return n;
    }
    case TOK_LPAREN: {
        next(p);
        Node *t = parse_type(p);
        expect(p,TOK_RPAREN);
        Node *par = node_new(N_PAREN,pos); par->x = t;
        return par;
    }
    case TOK_TILDE: {
        next(p);
        Node *n = node_new(N_UNARY,pos); n->op = TOK_TILDE; n->x = parse_type(p);
        return n;
    }
    default: return NULL;
    }
}

static Node *parse_type(P *p){
    Node *t = try_type(p);
    if (!t){ perr(p,"unexpected %s, expected type", tok_str(p->tok.kind)); t = node_new(N_IDENT,p->tok.pos); t->name=intern("int"); next(p); }
    return t;
}

/* ------------------------------------------------------------ expressions --- */

static int prec(TokKind k){
    switch (k){
    case TOK_LOR: return 1;
    case TOK_LAND: return 2;
    case TOK_EQL: case TOK_NEQ: case TOK_LSS: case TOK_LEQ: case TOK_GTR: case TOK_GEQ: return 3;
    case TOK_ADD: case TOK_SUB: case TOK_OR: case TOK_XOR: return 4;
    case TOK_MUL: case TOK_QUO: case TOK_REM: case TOK_SHL: case TOK_SHR:
    case TOK_AND: case TOK_AND_NOT: return 5;
    default: return 0;
    }
}

static Node *parse_composite_body(P *p, Node *typ, Pos pos);

static Node *parse_operand(P *p){
    Pos pos = p->tok.pos;
    switch (p->tok.kind){
    case TOK_IDENT: {
        Node *n = node_new(N_IDENT,pos); n->name = p->tok.lit; next(p);
        return n;
    }
    case TOK_INT: case TOK_FLOAT: case TOK_IMAG: case TOK_CHAR: case TOK_STRING: {
        Node *n = node_new(N_BASICLIT,pos);
        n->op = p->tok.kind; n->name = p->tok.lit; n->namelen = p->tok.litlen;
        next(p);
        return n;
    }
    case TOK_LPAREN: {
        next(p);
        int save = p->exprlev; p->exprlev = 1;
        Node *x = parse_expr(p);
        p->exprlev = save;
        expect(p,TOK_RPAREN);
        Node *n = node_new(N_PAREN,pos); n->x = x;
        return n;
    }
    case TOK_FUNC: {
        next(p);
        Node *sig = parse_signature(p);
        sig->pos = pos;
        if (at(p,TOK_LBRACE)){
            Node *n = node_new(N_FUNCLIT,pos);
            n->x = sig;
            int save = p->exprlev; p->exprlev = 1;
            n->y = parse_block(p);
            p->exprlev = save;
            return n;
        }
        return sig;
    }
    default: {
        Node *t = try_type(p);
        if (t) return t;
        perr(p,"unexpected %s, expected expression", tok_str(p->tok.kind));
        Node *n = node_new(N_IDENT,pos); n->name = intern("_");
        if (!at(p,TOK_EOF) && !at(p,TOK_SEMICOLON) && !at(p,TOK_RBRACE)) next(p);
        return n;
    }
    }
}

static Node *parse_call(P *p, Node *fun){
    Pos pos = p->tok.pos;
    expect(p,TOK_LPAREN);
    Node *n = node_new(N_CALL,pos);
    n->x = fun;
    int save = p->exprlev; p->exprlev = 1;
    while (!at(p,TOK_RPAREN) && !at(p,TOK_EOF)){
        Node *a = parse_expr(p);
        if (at(p,TOK_ELLIPSIS)){ next(p); n->flag = 1; }
        vec_push(&n->list, a);
        if (!got(p,TOK_COMMA)) break;
    }
    p->exprlev = save;
    expect(p,TOK_RPAREN);
    return n;
}

static Node *parse_composite_body(P *p, Node *typ, Pos pos){
    Node *n = node_new(N_COMPOSITELIT,pos);
    n->x = typ;
    expect(p,TOK_LBRACE);
    int save = p->exprlev; p->exprlev = 1;
    while (!at(p,TOK_RBRACE) && !at(p,TOK_EOF)){
        Node *e;
        if (at(p,TOK_LBRACE)) e = parse_composite_body(p,NULL,p->tok.pos);
        else e = parse_expr(p);
        if (got(p,TOK_COLON)){
            Node *kv = node_new(N_KEYVALUE, e->pos);
            kv->x = e;
            if (at(p,TOK_LBRACE)) kv->y = parse_composite_body(p,NULL,p->tok.pos);
            else kv->y = parse_expr(p);
            e = kv;
        }
        vec_push(&n->list, e);
        if (!got(p,TOK_COMMA)) break;
    }
    p->exprlev = save;
    expect(p,TOK_RBRACE);
    return n;
}

static Node *parse_primary(P *p){
    Node *x = parse_operand(p);
    for (;;){
        Pos pos = p->tok.pos;
        switch (p->tok.kind){
        case TOK_PERIOD: {
            next(p);
            if (at(p,TOK_LPAREN)){
                next(p);
                Node *n = node_new(N_TYPEASSERT,pos);
                n->x = x;
                if (at(p,TOK_TYPE)){ next(p); n->y = NULL; n->flag = 1; }
                else n->y = parse_type(p);
                expect(p,TOK_RPAREN);
                x = n;
            } else {
                Node *n = node_new(N_SELECTOR,pos);
                n->x = x; n->name = at(p,TOK_IDENT)?p->tok.lit:intern("_");
                expect(p,TOK_IDENT);
                x = n;
            }
            break;
        }
        case TOK_LBRACK: {
            next(p);
            int save = p->exprlev; p->exprlev = 1;
            if (at(p,TOK_COLON)){
                /* x[:...] */
                next(p);
                Node *n = node_new(N_SLICEEXPR,pos); n->x = x;
                if (!at(p,TOK_RBRACK) && !at(p,TOK_COLON)) n->z = parse_expr(p);
                if (got(p,TOK_COLON)){ n->w = parse_expr(p); n->flag = 1; }
                p->exprlev = save;
                expect(p,TOK_RBRACK);
                x = n; break;
            }
            Node *i0 = parse_expr(p);
            if (at(p,TOK_COLON)){
                next(p);
                Node *n = node_new(N_SLICEEXPR,pos); n->x = x; n->y = i0;
                if (!at(p,TOK_RBRACK) && !at(p,TOK_COLON)) n->z = parse_expr(p);
                if (got(p,TOK_COLON)){ n->w = parse_expr(p); n->flag = 1; }
                p->exprlev = save;
                expect(p,TOK_RBRACK);
                x = n; break;
            }
            if (at(p,TOK_COMMA)){
                Node *n = node_new(N_INDEXLIST,pos); n->x = x;
                vec_push(&n->list, i0);
                while (got(p,TOK_COMMA)){
                    if (at(p,TOK_RBRACK)) break;
                    vec_push(&n->list, parse_expr(p));
                }
                p->exprlev = save;
                expect(p,TOK_RBRACK);
                x = n; break;
            }
            Node *n = node_new(N_INDEX,pos); n->x = x; n->y = i0;
            p->exprlev = save;
            expect(p,TOK_RBRACK);
            x = n; break;
        }
        case TOK_LPAREN:
            x = parse_call(p,x);
            break;
        case TOK_LBRACE: {
            /* composite literal if x denotes a type and context allows */
            int isTypeExpr = (x->kind==N_ARRAYTYPE||x->kind==N_STRUCTTYPE||x->kind==N_MAPTYPE);
            int isNameish = (x->kind==N_IDENT||x->kind==N_SELECTOR||x->kind==N_INDEX||x->kind==N_INDEXLIST);
            if (isTypeExpr || (isNameish && p->exprlev >= 0)){
                x = parse_composite_body(p,x,pos);
                break;
            }
            return x;
        }
        default:
            return x;
        }
    }
}

static Node *parse_unary(P *p){
    Pos pos = p->tok.pos;
    switch (p->tok.kind){
    case TOK_ADD: case TOK_SUB: case TOK_NOT: case TOK_XOR: case TOK_AND: {
        TokKind op = p->tok.kind; next(p);
        Node *n = node_new(N_UNARY,pos); n->op = op; n->x = parse_unary(p);
        return n;
    }
    case TOK_ARROW: {
        next(p);
        /* could be <-chan T type */
        if (at(p,TOK_CHAN)){
            next(p);
            Node *n = node_new(N_CHANTYPE,pos); n->flag = 2; n->x = parse_type(p);
            return n;
        }
        Node *n = node_new(N_UNARY,pos); n->op = TOK_ARROW; n->x = parse_unary(p);
        return n;
    }
    case TOK_MUL: {
        next(p);
        Node *n = node_new(N_STAR,pos); n->x = parse_unary(p);
        return n;
    }
    default:
        return parse_primary(p);
    }
}

static Node *parse_binary(P *p, int minprec){
    Node *x = parse_unary(p);
    for (;;){
        int pr = prec(p->tok.kind);
        if (pr < minprec || pr == 0) return x;
        TokKind op = p->tok.kind;
        Pos pos = p->tok.pos;
        next(p);
        Node *y = parse_binary(p, pr+1);
        Node *n = node_new(N_BINARY,pos); n->op = op; n->x = x; n->y = y;
        x = n;
    }
}

static Node *parse_expr(P *p){ return parse_binary(p,1); }

/* ------------------------------------------------------------- statements --- */

static Node *parse_block(P *p){
    Node *b = node_new(N_BLOCK, p->tok.pos);
    expect(p,TOK_LBRACE);
    while (!at(p,TOK_RBRACE) && !at(p,TOK_EOF)){
        Node *s = parse_stmt(p);
        if (s) vec_push(&b->list, s);
    }
    expect(p,TOK_RBRACE);
    return b;
}

static Node *parse_valuespec(P *p, TokKind kw, int iota_index);
static Node *parse_gendecl(P *p, TokKind kw);

static Node *parse_simple_stmt(P *p, int labelok, Node **label_out){
    Pos pos = p->tok.pos;
    if (label_out) *label_out = NULL;
    if (at(p,TOK_SEMICOLON) || at(p,TOK_LBRACE)) return node_new(N_EMPTY,pos);
    Vec lhs; memset(&lhs,0,sizeof lhs);
    vec_push(&lhs, parse_expr(p));
    while (got(p,TOK_COMMA)) vec_push(&lhs, parse_expr(p));

    switch (p->tok.kind){
    case TOK_ASSIGN: case TOK_DEFINE:
    case TOK_ADD_ASSIGN: case TOK_SUB_ASSIGN: case TOK_MUL_ASSIGN: case TOK_QUO_ASSIGN:
    case TOK_REM_ASSIGN: case TOK_AND_ASSIGN: case TOK_OR_ASSIGN: case TOK_XOR_ASSIGN:
    case TOK_SHL_ASSIGN: case TOK_SHR_ASSIGN: case TOK_AND_NOT_ASSIGN: {
        TokKind op = p->tok.kind;
        Pos apos = p->tok.pos;
        next(p);
        Node *n = node_new(N_ASSIGN,apos);
        n->op = op; n->list = lhs;
        if (at(p,TOK_RANGE) && (op==TOK_ASSIGN||op==TOK_DEFINE)){
            next(p);
            Node *r = node_new(N_RANGE,apos);
            r->op = op;
            r->x = lhs.len>0 ? lhs.p[0] : NULL;
            r->y = lhs.len>1 ? lhs.p[1] : NULL;
            int save = p->exprlev; p->exprlev = -1;
            r->z = parse_expr(p);
            p->exprlev = save;
            return r;
        }
        vec_push(&n->list2, parse_expr(p));
        while (got(p,TOK_COMMA)) vec_push(&n->list2, parse_expr(p));
        return n;
    }
    case TOK_ARROW: {
        next(p);
        Node *n = node_new(N_SEND,pos);
        n->x = lhs.p[0]; n->y = parse_expr(p);
        return n;
    }
    case TOK_INC: case TOK_DEC: {
        Node *n = node_new(N_INCDEC,p->tok.pos);
        n->op = p->tok.kind; n->x = lhs.p[0];
        next(p);
        return n;
    }
    case TOK_COLON: {
        if (labelok && lhs.len==1 && ((Node*)lhs.p[0])->kind==N_IDENT){
            next(p);
            Node *n = node_new(N_LABELED,pos);
            n->name = ((Node*)lhs.p[0])->name;
            if (label_out) *label_out = n;
            return n;
        }
        break;
    }
    default: break;
    }
    if (lhs.len > 1) perr(p,"expected one expression");
    Node *n = node_new(N_EXPRSTMT,pos);
    n->x = lhs.p[0];
    return n;
}

static Node *parse_if(P *p){
    Pos pos = p->tok.pos;
    expect(p,TOK_IF);
    Node *n = node_new(N_IF,pos);
    int save = p->exprlev; p->exprlev = -1;
    Node *s = NULL;
    if (!at(p,TOK_SEMICOLON)){
        if (at(p,TOK_VAR)||at(p,TOK_CONST)||at(p,TOK_TYPE)) { perr(p,"cannot declare in if header"); }
        s = parse_simple_stmt(p,0,NULL);
    }
    if (got(p,TOK_SEMICOLON)){
        n->x = s;
        n->y = parse_expr(p);
    } else {
        if (s && s->kind==N_EXPRSTMT) n->y = s->x;
        else { perr(p,"missing condition in if statement"); n->y = NULL; }
    }
    p->exprlev = save;
    n->z = parse_block(p);
    if (got(p,TOK_ELSE)){
        if (at(p,TOK_IF)) n->w = parse_if(p);
        else n->w = parse_block(p);
    }
    return n;
}

static Node *parse_case_body(P *p){
    Node *b = node_new(N_BLOCK,p->tok.pos);
    while (!at(p,TOK_CASE) && !at(p,TOK_DEFAULT) && !at(p,TOK_RBRACE) && !at(p,TOK_EOF)){
        Node *s = parse_stmt(p);
        if (s) vec_push(&b->list, s);
    }
    return b;
}

static int is_typeswitch_guard(Node *s){
    if (!s) return 0;
    if (s->kind==N_EXPRSTMT && s->x && s->x->kind==N_TYPEASSERT && s->x->flag) return 1;
    if (s->kind==N_ASSIGN && s->op==TOK_DEFINE && s->list2.len==1){
        Node *r = s->list2.p[0];
        if (r->kind==N_TYPEASSERT && r->flag) return 1;
    }
    return 0;
}

static Node *parse_switch(P *p){
    Pos pos = p->tok.pos;
    expect(p,TOK_SWITCH);
    Node *init = NULL, *tag = NULL;
    int save = p->exprlev; p->exprlev = -1;
    if (!at(p,TOK_LBRACE)){
        if (!at(p,TOK_SEMICOLON)) tag = parse_simple_stmt(p,0,NULL);
        if (got(p,TOK_SEMICOLON)){
            init = tag; tag = NULL;
            if (!at(p,TOK_LBRACE)) tag = parse_simple_stmt(p,0,NULL);
        }
    }
    p->exprlev = save;
    int isType = is_typeswitch_guard(tag);
    Node *n = node_new(isType?N_TYPESWITCH:N_SWITCH,pos);
    n->x = init;
    if (isType) n->y = tag;
    else if (tag){
        if (tag->kind==N_EXPRSTMT) n->y = tag->x;
        else { perr(p,"invalid switch header"); }
    }
    expect(p,TOK_LBRACE);
    while (at(p,TOK_CASE)||at(p,TOK_DEFAULT)){
        Node *c = node_new(N_CASECLAUSE,p->tok.pos);
        if (got(p,TOK_CASE)){
            for(;;){
                Node *e = isType ? parse_type(p) : parse_expr(p);
                if (isType && e->kind==N_IDENT && strcmp(e->name,"nil")==0) e->flag = 99;
                vec_push(&c->list, e);
                if (!got(p,TOK_COMMA)) break;
            }
        } else { expect(p,TOK_DEFAULT); c->flag = 1; }
        expect(p,TOK_COLON);
        Node *body = parse_case_body(p);
        c->list2 = body->list;
        vec_push(&n->list, c);
    }
    expect(p,TOK_RBRACE);
    return n;
}

static Node *parse_select(P *p){
    Pos pos = p->tok.pos;
    expect(p,TOK_SELECT);
    Node *n = node_new(N_SELECT,pos);
    expect(p,TOK_LBRACE);
    while (at(p,TOK_CASE)||at(p,TOK_DEFAULT)){
        Node *c = node_new(N_COMMCLAUSE,p->tok.pos);
        if (got(p,TOK_CASE)){
            c->x = parse_simple_stmt(p,0,NULL);
        } else { expect(p,TOK_DEFAULT); c->flag = 1; }
        expect(p,TOK_COLON);
        Node *body = parse_case_body(p);
        c->list2 = body->list;
        vec_push(&n->list, c);
    }
    expect(p,TOK_RBRACE);
    return n;
}

static Node *parse_for(P *p){
    Pos pos = p->tok.pos;
    expect(p,TOK_FOR);
    int save = p->exprlev; p->exprlev = -1;
    Node *init=NULL, *cond=NULL, *post=NULL;
    if (at(p,TOK_LBRACE)){
        p->exprlev = save;
        Node *n = node_new(N_FOR,pos);
        n->w = parse_block(p);
        return n;
    }
    if (at(p,TOK_RANGE)){
        next(p);
        Node *r = node_new(N_RANGE,pos);
        r->op = 0;
        r->z = parse_expr(p);
        p->exprlev = save;
        r->w = parse_block(p);
        return r;
    }
    if (!at(p,TOK_SEMICOLON)){
        Node *s = parse_simple_stmt(p,0,NULL);
        if (s && s->kind==N_RANGE){
            p->exprlev = save;
            s->w = parse_block(p);
            return s;
        }
        if (at(p,TOK_LBRACE)){
            p->exprlev = save;
            Node *n = node_new(N_FOR,pos);
            if (s->kind==N_EXPRSTMT) n->y = s->x;
            else perr(p,"invalid for condition");
            n->w = parse_block(p);
            return n;
        }
        init = s;
    }
    expect(p,TOK_SEMICOLON);
    if (!at(p,TOK_SEMICOLON)) cond = parse_expr(p);
    expect(p,TOK_SEMICOLON);
    if (!at(p,TOK_LBRACE)) post = parse_simple_stmt(p,0,NULL);
    p->exprlev = save;
    Node *n = node_new(N_FOR,pos);
    n->x = init; n->y = cond; n->z = post;
    n->w = parse_block(p);
    return n;
}

static Node *parse_stmt(P *p){
    Pos pos = p->tok.pos;
    switch (p->tok.kind){
    case TOK_SEMICOLON: next(p); return NULL;
    case TOK_LBRACE: { Node *b = parse_block(p); expect_semi(p); return b; }
    case TOK_VAR: case TOK_CONST: case TOK_TYPE: {
        TokKind kw = p->tok.kind;
        Node *d = parse_gendecl(p,kw);
        Node *s = node_new(N_DECLSTMT,pos); s->x = d;
        expect_semi(p);
        return s;
    }
    case TOK_IF: { Node *n = parse_if(p); expect_semi(p); return n; }
    case TOK_FOR: { Node *n = parse_for(p); expect_semi(p); return n; }
    case TOK_SWITCH: { Node *n = parse_switch(p); expect_semi(p); return n; }
    case TOK_SELECT: { Node *n = parse_select(p); expect_semi(p); return n; }
    case TOK_RETURN: {
        next(p);
        Node *n = node_new(N_RETURN,pos);
        if (!at(p,TOK_SEMICOLON) && !at(p,TOK_RBRACE)){
            vec_push(&n->list, parse_expr(p));
            while (got(p,TOK_COMMA)) vec_push(&n->list, parse_expr(p));
        }
        expect_semi(p);
        return n;
    }
    case TOK_GO: case TOK_DEFER: {
        TokKind kw = p->tok.kind; next(p);
        Node *n = node_new(kw==TOK_GO?N_GO:N_DEFER,pos);
        n->x = parse_expr(p);
        expect_semi(p);
        return n;
    }
    case TOK_BREAK: case TOK_CONTINUE: case TOK_GOTO: case TOK_FALLTHROUGH: {
        TokKind kw = p->tok.kind; next(p);
        Node *n = node_new(N_BRANCH,pos); n->op = kw;
        if (at(p,TOK_IDENT)){ n->name = p->tok.lit; next(p); }
        expect_semi(p);
        return n;
    }
    default: {
        Node *label = NULL;
        Node *s = parse_simple_stmt(p,1,&label);
        if (label){
            /* labeled statement */
            if (at(p,TOK_RBRACE) || at(p,TOK_CASE) || at(p,TOK_DEFAULT)){
                label->x = node_new(N_EMPTY,pos);
            } else if (at(p,TOK_SEMICOLON)) {
                next(p);
                label->x = node_new(N_EMPTY,pos);
            } else {
                label->x = parse_stmt(p);
            }
            return label;
        }
        expect_semi(p);
        return s;
    }
    }
}

/* ----------------------------------------------------------- declarations --- */

static Node *parse_valuespec(P *p, TokKind kw, int iota_index){
    Node *n = node_new(N_VALUESPEC, p->tok.pos);
    n->op = kw;
    n->flag = iota_index;
    vec_push(&n->list, parse_ident(p));
    while (got(p,TOK_COMMA)) vec_push(&n->list, parse_ident(p));
    if (!at(p,TOK_ASSIGN) && !at(p,TOK_SEMICOLON) && !at(p,TOK_RPAREN)){
        n->x = parse_type(p);
    }
    if (got(p,TOK_ASSIGN)){
        vec_push(&n->list2, parse_expr(p));
        while (got(p,TOK_COMMA)) vec_push(&n->list2, parse_expr(p));
    }
    return n;
}

static Node *parse_typespec(P *p){
    Node *n = node_new(N_TYPESPEC, p->tok.pos);
    Node *id = parse_ident(p);
    n->name = id->name;
    if (at(p,TOK_LBRACK)){
        /* distinguish type params from array type */
        Lexer save_lx = p->lx; Token save_tok = p->tok;
        next(p);
        int isparams = 0;
        if (at(p,TOK_IDENT)){
            Lexer lx2 = p->lx; Token t2 = p->tok;
            next(p);
            if (at(p,TOK_RBRACK)) isparams = 0;               /* [N]elem */
            else if (at(p,TOK_COMMA)) isparams = 1;           /* [A, B any] */
            else isparams = 1;                                /* [T any] */
            p->lx = lx2; p->tok = t2;
        }
        p->lx = save_lx; p->tok = save_tok;
        if (isparams){
            n->w = parse_typeparams(p);
        }
    }
    if (got(p,TOK_ASSIGN)) n->flag = 1;
    n->x = parse_type(p);
    return n;
}

static Node *parse_gendecl(P *p, TokKind kw){
    Pos pos = p->tok.pos;
    expect(p,kw);
    Node *d = node_new(N_GENDECL,pos);
    d->op = kw;
    if (got(p,TOK_LPAREN)){
        int iota = 0;
        Node *prev = NULL;
        while (!at(p,TOK_RPAREN) && !at(p,TOK_EOF)){
            Node *s;
            if (kw==TOK_TYPE) s = parse_typespec(p);
            else {
                s = parse_valuespec(p,kw,iota);
                if (kw==TOK_CONST && s->list2.len==0 && !s->x && prev){
                    s->x = prev->x;
                    s->list2 = prev->list2;
                    s->w = prev;
                } else if (kw==TOK_CONST) prev = s;
                iota++;
            }
            vec_push(&d->list, s);
            if (!at(p,TOK_RPAREN)) expect_semi(p);
        }
        expect(p,TOK_RPAREN);
    } else {
        Node *s;
        if (kw==TOK_TYPE) s = parse_typespec(p);
        else s = parse_valuespec(p,kw,0);
        vec_push(&d->list, s);
    }
    return d;
}

static Node *parse_import(P *p){
    Pos pos = p->tok.pos;
    expect(p,TOK_IMPORT);
    Node *d = node_new(N_GENDECL,pos);
    d->op = TOK_IMPORT;
    if (got(p,TOK_LPAREN)){
        while (!at(p,TOK_RPAREN) && !at(p,TOK_EOF)){
            Node *s = node_new(N_IMPORT,p->tok.pos);
            if (at(p,TOK_IDENT)||at(p,TOK_PERIOD)){
                s->name = at(p,TOK_PERIOD) ? intern(".") : p->tok.lit;
                next(p);
            }
            if (at(p,TOK_STRING)){ s->x = node_new(N_BASICLIT,p->tok.pos); s->x->name = p->tok.lit; next(p); }
            else perr(p,"missing import path");
            vec_push(&d->list, s);
            if (!at(p,TOK_RPAREN)) expect_semi(p);
        }
        expect(p,TOK_RPAREN);
    } else {
        Node *s = node_new(N_IMPORT,p->tok.pos);
        if (at(p,TOK_IDENT)||at(p,TOK_PERIOD)){ s->name = at(p,TOK_PERIOD)?intern("."):p->tok.lit; next(p); }
        if (at(p,TOK_STRING)){ s->x = node_new(N_BASICLIT,p->tok.pos); s->x->name = p->tok.lit; next(p); }
        else perr(p,"missing import path");
        vec_push(&d->list, s);
    }
    return d;
}

static Node *parse_funcdecl(P *p){
    Pos pos = p->tok.pos;
    expect(p,TOK_FUNC);
    Node *n = node_new(N_FUNCDECL,pos);
    if (at(p,TOK_LPAREN)){
        int v=0;
        Node *recv = parse_params(p,&v);
        if (recv->list.len != 1) perr(p,"method has multiple receivers");
        n->x = recv->list.len ? recv->list.p[0] : NULL;
    }
    Node *id = parse_ident(p);
    n->name = id->name;
    if (at(p,TOK_LBRACK)) n->w = parse_typeparams(p);
    n->y = parse_signature(p);
    if (at(p,TOK_LBRACE)) n->z = parse_block(p);
    expect_semi(p);
    return n;
}

Node *parse_file(const char *filename, const char *src, size_t len, int lang){
    P p; memset(&p,0,sizeof p);
    p.lang = lang;
    int savelang = g_lang;
    g_lang = lang;
    lex_init(&p.lx, filename, src, len);
    next(&p);
    Node *f = node_new(N_FILE, p.tok.pos);
    if (at(&p,TOK_PACKAGE)){
        next(&p);
        Node *id = parse_ident(&p);
        f->name = id->name;
        expect_semi(&p);
    } else {
        perr(&p,"package statement must be first");
        f->name = intern("main");
    }
    while (at(&p,TOK_IMPORT)){
        Node *d = parse_import(&p);
        vec_push(&f->list2, d);
        expect_semi(&p);
    }
    while (!at(&p,TOK_EOF)){
        switch (p.tok.kind){
        case TOK_FUNC: vec_push(&f->list, parse_funcdecl(&p)); break;
        case TOK_VAR: case TOK_CONST: case TOK_TYPE: {
            Node *d = parse_gendecl(&p,p.tok.kind);
            vec_push(&f->list, d);
            expect_semi(&p);
            break;
        }
        case TOK_IMPORT: {
            Node *d = parse_import(&p);
            vec_push(&f->list2, d);
            expect_semi(&p);
            break;
        }
        case TOK_SEMICOLON: next(&p); break;
        default:
            perr(&p,"non-declaration statement outside function body");
            while (!at(&p,TOK_EOF) && !at(&p,TOK_FUNC) && !at(&p,TOK_VAR) && !at(&p,TOK_TYPE) && !at(&p,TOK_CONST)) next(&p);
            break;
        }
    }
    f->xtra = xmalloc(sizeof(Vec));
    *(Vec*)f->xtra = p.lx.comments;
    g_file_comments = p.lx.comments;
    g_lang = savelang;
    return f;
}
