#define _GNU_SOURCE
#include "cel.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <regex.h>

/* A small CEL interpreter: the subset Kubernetes admission expressions use.
 * Grammar (loosest to tightest):
 *   ternary  := or ('?' ternary ':' ternary)?
 *   or       := and ('||' and)*
 *   and      := rel ('&&' rel)*
 *   rel      := add (('=='|'!='|'<'|'<='|'>'|'>='|'in') add)*
 *   add      := mul (('+'|'-') mul)*
 *   mul      := unary (('*'|'/'|'%') unary)*
 *   unary    := ('!'|'-')* postfix
 *   postfix  := primary ('.' IDENT ('(' args ')')? | '[' ternary ']')*
 *   primary  := NUMBER | STRING | 'true' | 'false' | 'null' | IDENT ('(' args ')')?
 *             | '[' list ']' | '{' map '}' | '(' ternary ')'
 */

typedef struct {
    const char *p;
    json *act;
    char err[256];
    int failed;
    int depth;
} cctx;

static json *parse_ternary(cctx *c);

static void skip(cctx *c){ while(*c->p && isspace((unsigned char)*c->p)) c->p++; }
static int eat(cctx *c, const char *tok){
    skip(c);
    size_t n = strlen(tok);
    if(strncmp(c->p, tok, n)) return 0;
    /* word operators must not be a prefix of an identifier */
    if(isalpha((unsigned char)tok[0]) && (isalnum((unsigned char)c->p[n]) || c->p[n] == '_'))
        return 0;
    c->p += n;
    return 1;
}
static void fail(cctx *c, const char *fmt, ...){
    if(c->failed) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(c->err, sizeof c->err, fmt, ap);
    va_end(ap);
    c->failed = 1;
}

static int truthy(json *v){ return v && v->t == J_BOOL && v->b; }
static json *jerr(void){ return NULL; }

/* set while cel_check() is running: a missing name, key or type is a runtime
   condition, not a compile error, so parsing must carry on past it. */
static __thread int CHECK_ONLY = 0;

static json *lookup(cctx *c, const char *name){
    json *v = j_get(c->act, name);
    if(!v){
        if(CHECK_ONLY) return j_obj();
        fail(c, "undeclared reference to '%s'", name); return jerr();
    }
    return j_clone(v);
}

static int num_of(json *v, double *out){
    if(v && v->t == J_NUM){ *out = v->n; return 1; }
    return 0;
}

static json *parse_primary(cctx *c);

static json *call_fn(cctx *c, const char *name, json *recv, json **args, int nargs);

static json *parse_args(cctx *c, json **out, int max, int *n){
    *n = 0;
    skip(c);
    if(eat(c, ")")) return NULL;
    for(;;){
        if(*n >= max){ fail(c, "too many arguments"); return NULL; }
        out[(*n)++] = parse_ternary(c);
        skip(c);
        if(eat(c, ",")) continue;
        if(eat(c, ")")) break;
        fail(c, "expected ',' or ')'");
        return NULL;
    }
    return NULL;
}

/* comprehension macros: recv.all(x, expr) etc. */
static json *macro(cctx *c, const char *name, json *recv){
    skip(c);
    const char *save = c->p;
    char var[64];
    int k = 0;
    while(*c->p && (isalnum((unsigned char)*c->p) || *c->p == '_') && k < 63) var[k++] = *c->p++;
    var[k] = 0;
    skip(c);
    if(!k || !eat(c, ",")){ c->p = save; return NULL; }
    const char *body = c->p;
    /* find the matching ')' */
    int depth = 0;
    const char *q = body;
    while(*q){
        if(*q == '(' || *q == '[' || *q == '{') depth++;
        else if(*q == ')' && depth == 0) break;
        else if(*q == ')' || *q == ']' || *q == '}') depth--;
        else if(*q == '\'' || *q == '"'){ char quote = *q++; while(*q && *q != quote) q++; }
        q++;
    }
    if(*q != ')'){ fail(c, "unterminated macro"); return NULL; }
    size_t blen = (size_t)(q - body);
    char *bexpr = malloc(blen + 1);
    memcpy(bexpr, body, blen); bexpr[blen] = 0;
    c->p = q + 1;
    json *out = NULL;
    int is_all = !strcmp(name, "all"), is_exists = !strcmp(name, "exists");
    int is_filter = !strcmp(name, "filter"), is_map = !strcmp(name, "map");
    int is_one = !strcmp(name, "exists_one");
    if(!(is_all || is_exists || is_filter || is_map || is_one)){
        free(bexpr);
        return NULL;
    }
    json *items = j_arr();
    if(recv && recv->t == J_ARR)
        for(size_t i = 0; i < recv->nitems; i++) j_push(items, j_clone(recv->items[i]));
    else if(recv && recv->t == J_OBJ)
        for(size_t i = 0; i < recv->nmem; i++) j_push(items, j_str(recv->mem[i].key));
    int acc = is_all ? 1 : 0, count = 0;
    json *coll = (is_filter || is_map) ? j_arr() : NULL;
    json *saved = j_get(c->act, var) ? j_clone(j_get(c->act, var)) : NULL;
    for(size_t i = 0; i < items->nitems && !c->failed; i++){
        j_set(c->act, var, j_clone(items->items[i]));
        char *e2 = NULL;
        json *r = cel_eval(bexpr, c->act, &e2);
        if(!r){ fail(c, "%s", e2 ? e2 : "macro body failed"); free(e2); break; }
        if(is_all && !truthy(r)) acc = 0;
        if((is_exists || is_one) && truthy(r)) count++;
        if(is_filter && truthy(r)) j_push(coll, j_clone(items->items[i]));
        if(is_map) j_push(coll, j_clone(r));
        j_free(r);
    }
    if(saved) j_set(c->act, var, saved); else j_del(c->act, var);
    j_free(items);
    free(bexpr);
    if(c->failed){ j_free(coll); j_free(recv); return NULL; }
    if(is_all) out = j_bool(acc);
    else if(is_exists) out = j_bool(count > 0);
    else if(is_one) out = j_bool(count == 1);
    else out = coll;
    j_free(recv);
    return out;
}

static json *parse_postfix(cctx *c){
    json *v = parse_primary(c);
    for(;;){
        if(c->failed){ j_free(v); return NULL; }
        skip(c);
        if(*c->p == '.'){
            c->p++;
            skip(c);
            char name[128];
            int k = 0;
            while(*c->p && (isalnum((unsigned char)*c->p) || *c->p == '_') && k < 127)
                name[k++] = *c->p++;
            name[k] = 0;
            if(!k){ fail(c, "expected identifier after '.'"); j_free(v); return NULL; }
            skip(c);
            if(*c->p == '('){
                c->p++;
                json *m = macro(c, name, v);
                if(m || c->failed){ v = m; continue; }
                json *args[8]; int n = 0;
                parse_args(c, args, 8, &n);
                if(c->failed){ j_free(v); return NULL; }
                v = call_fn(c, name, v, args, n);
                continue;
            }
            json *f = (v && v->t == J_OBJ) ? j_get(v, name) : NULL;
            if(!f){
                if(CHECK_ONLY){ j_free(v); v = j_obj(); continue; }
                fail(c, "no such key: %s", name); j_free(v); return NULL;
            }
            json *nv = j_clone(f);
            j_free(v);
            v = nv;
            continue;
        }
        if(*c->p == '['){
            c->p++;
            json *idx = parse_ternary(c);
            skip(c);
            if(!eat(c, "]")){ fail(c, "expected ']'"); j_free(v); j_free(idx); return NULL; }
            json *nv = NULL;
            if(v && v->t == J_ARR && idx && idx->t == J_NUM){
                json *e = j_at(v, (size_t)idx->n);
                if(!e){ if(CHECK_ONLY) nv = j_obj(); else fail(c, "index out of range"); }
                else nv = j_clone(e);
            } else if(v && v->t == J_OBJ && idx && idx->t == J_STR){
                json *e = j_get(v, idx->s);
                if(!e){ if(CHECK_ONLY) nv = j_obj(); else fail(c, "no such key: %s", idx->s); }
                else nv = j_clone(e);
            } else if(CHECK_ONLY) nv = j_obj();
            else fail(c, "unsupported index");
            j_free(idx); j_free(v);
            if(c->failed) return NULL;
            v = nv;
            continue;
        }
        return v;
    }
}

static json *call_fn(cctx *c, const char *name, json *recv, json **args, int nargs){
    json *out = NULL;
    const char *s = (recv && recv->t == J_STR) ? recv->s : NULL;
    const char *a0 = (nargs > 0 && args[0] && args[0]->t == J_STR) ? args[0]->s : NULL;
    if(!strcmp(name, "startsWith") && s && a0) out = j_bool(str_has_prefix(s, a0));
    else if(!strcmp(name, "endsWith") && s && a0) out = j_bool(str_has_suffix(s, a0));
    else if(!strcmp(name, "contains") && s && a0) out = j_bool(strstr(s, a0) != NULL);
    else if(!strcmp(name, "matches") && s && a0){
        regex_t re;
        if(regcomp(&re, a0, REG_EXTENDED) == 0){
            out = j_bool(regexec(&re, s, 0, NULL, 0) == 0);
            regfree(&re);
        } else fail(c, "invalid regular expression");
    }
    else if(!strcmp(name, "size")) {
        if(recv && recv->t == J_STR) out = j_int((long long)strlen(recv->s));
        else if(recv && recv->t == J_ARR) out = j_int((long long)recv->nitems);
        else if(recv && recv->t == J_OBJ) out = j_int((long long)recv->nmem);
        else if(CHECK_ONLY) out = j_int(0);
        else fail(c, "size() on unsupported type");
    }
    else if(!strcmp(name, "lowerAscii") && s){
        char *d = xstrdup2(s);
        for(char *q = d; *q; q++) *q = (char)tolower((unsigned char)*q);
        out = j_str(d); free(d);
    }
    else if(!strcmp(name, "upperAscii") && s){
        char *d = xstrdup2(s);
        for(char *q = d; *q; q++) *q = (char)toupper((unsigned char)*q);
        out = j_str(d); free(d);
    }
    else fail(c, "unsupported function %s", name);
    j_free(recv);
    for(int i = 0; i < nargs; i++) j_free(args[i]);
    return out;
}

static json *global_fn(cctx *c, const char *name, json **args, int nargs){
    json *out = NULL;
    if(!strcmp(name, "size") && nargs == 1){
        json *v = args[0];
        if(v && v->t == J_STR) out = j_int((long long)strlen(v->s));
        else if(v && v->t == J_ARR) out = j_int((long long)v->nitems);
        else if(v && v->t == J_OBJ) out = j_int((long long)v->nmem);
        else if(CHECK_ONLY) out = j_int(0);
        else fail(c, "size() on unsupported type");
    } else if(!strcmp(name, "string") && nargs == 1){
        json *v = args[0];
        if(v && v->t == J_STR) out = j_clone(v);
        else { char *t = j_dump(v); out = j_str(t); free(t); }
    } else if(!strcmp(name, "int") && nargs == 1){
        double d;
        if(num_of(args[0], &d)) out = j_int((long long)d);
        else if(args[0] && args[0]->t == J_STR) out = j_int(strtoll(args[0]->s, NULL, 10));
        else if(CHECK_ONLY) out = j_int(0);
        else fail(c, "int() on unsupported type");
    } else if(!strcmp(name, "double") && nargs == 1){
        double d;
        if(num_of(args[0], &d)) out = j_num(d);
        else if(CHECK_ONLY) out = j_num(0);
        else fail(c, "double() on unsupported type");
    } else if(!strcmp(name, "type") && nargs == 1){
        static const char *N[] = {"null_type","bool","int","string","list","map"};
        out = j_str(args[0] ? N[args[0]->t] : "null_type");
    } else fail(c, "unsupported function %s", name);
    for(int i = 0; i < nargs; i++) j_free(args[i]);
    return out;
}

/* has(a.b.c) is a macro over the *unevaluated* selector */
static json *has_macro(cctx *c){
    skip(c);
    const char *start = c->p;
    int depth = 0;
    while(*c->p){
        if(*c->p == '(' ) depth++;
        else if(*c->p == ')'){ if(!depth) break; depth--; }
        c->p++;
    }
    if(*c->p != ')'){ fail(c, "unterminated has()"); return NULL; }
    size_t n = (size_t)(c->p - start);
    char *sel = malloc(n + 1);
    memcpy(sel, start, n); sel[n] = 0;
    c->p++;
    char *dot = strrchr(sel, '.');
    if(!dot){ free(sel); fail(c, "has() requires a field selector"); return NULL; }
    *dot = 0;
    char *e2 = NULL;
    json *base = cel_eval(sel, c->act, &e2);
    json *out;
    if(!base){ free(e2); out = j_bool(0); }
    else {
        json *f = (base->t == J_OBJ) ? j_get(base, dot + 1) : NULL;
        out = j_bool(f != NULL && f->t != J_NULL);
        j_free(base);
    }
    free(sel);
    return out;
}

static json *parse_primary(cctx *c){
    skip(c);
    if(!*c->p){ fail(c, "unexpected end of expression"); return NULL; }
    if(*c->p == '('){
        c->p++;
        json *v = parse_ternary(c);
        skip(c);
        if(!eat(c, ")")){ fail(c, "expected ')'"); j_free(v); return NULL; }
        return v;
    }
    if(*c->p == '['){
        c->p++;
        json *l = j_arr();
        skip(c);
        if(eat(c, "]")) return l;
        for(;;){
            j_push(l, parse_ternary(c));
            skip(c);
            if(eat(c, ",")) continue;
            if(eat(c, "]")) break;
            fail(c, "expected ',' or ']'"); j_free(l); return NULL;
        }
        return l;
    }
    if(*c->p == '{'){
        c->p++;
        json *m = j_obj();
        skip(c);
        if(eat(c, "}")) return m;
        for(;;){
            json *k = parse_ternary(c);
            skip(c);
            if(!eat(c, ":")){ fail(c, "expected ':'"); j_free(k); j_free(m); return NULL; }
            json *v = parse_ternary(c);
            if(k && k->t == J_STR) j_set(m, k->s, v); else j_free(v);
            j_free(k);
            skip(c);
            if(eat(c, ",")) continue;
            if(eat(c, "}")) break;
            fail(c, "expected ',' or '}'"); j_free(m); return NULL;
        }
        return m;
    }
    if(*c->p == '\'' || *c->p == '"'){
        char q = *c->p++;
        buf b = {0};
        while(*c->p && *c->p != q){
            if(*c->p == '\\' && c->p[1]){
                c->p++;
                char e = *c->p++;
                char out = e;
                if(e == 'n') out = '\n';
                else if(e == 't') out = '\t';
                else if(e == 'r') out = '\r';
                buf_add(&b, &out, 1);
                continue;
            }
            buf_add(&b, c->p, 1);
            c->p++;
        }
        if(*c->p != q){ fail(c, "unterminated string"); buf_free(&b); return NULL; }
        c->p++;
        json *v = j_str(b.p ? b.p : "");
        buf_free(&b);
        return v;
    }
    if(isdigit((unsigned char)*c->p)){
        char *end;
        double d = strtod(c->p, &end);
        int isint = 1;
        for(const char *q = c->p; q < end; q++) if(*q == '.' || *q == 'e' || *q == 'E') isint = 0;
        c->p = end;
        if(*c->p == 'u') c->p++;
        return isint ? j_int((long long)d) : j_num(d);
    }
    if(isalpha((unsigned char)*c->p) || *c->p == '_'){
        char name[128];
        int k = 0;
        while(*c->p && (isalnum((unsigned char)*c->p) || *c->p == '_') && k < 127)
            name[k++] = *c->p++;
        name[k] = 0;
        if(!strcmp(name, "true")) return j_bool(1);
        if(!strcmp(name, "false")) return j_bool(0);
        if(!strcmp(name, "null")) return j_null();
        skip(c);
        if(*c->p == '('){
            c->p++;
            if(!strcmp(name, "has")) return has_macro(c);
            json *args[8]; int n = 0;
            parse_args(c, args, 8, &n);
            if(c->failed) return NULL;
            return global_fn(c, name, args, n);
        }
        return lookup(c, name);
    }
    fail(c, "unexpected character '%c'", *c->p);
    return NULL;
}

static json *parse_unary(cctx *c){
    skip(c);
    if(eat(c, "!")){
        json *v = parse_unary(c);
        if(c->failed){ j_free(v); return NULL; }
        json *o = j_bool(!truthy(v));
        j_free(v);
        return o;
    }
    if(*c->p == '-' && !isdigit((unsigned char)c->p[1])){
        c->p++;
        json *v = parse_unary(c);
        double d;
        if(!num_of(v, &d)){
            if(CHECK_ONLY) d = 0;
            else { fail(c, "unary - on non-number"); j_free(v); return NULL; }
        }
        int isint = v->is_int;
        j_free(v);
        return isint ? j_int((long long)-d) : j_num(-d);
    }
    if(*c->p == '-'){
        c->p++;
        json *v = parse_unary(c);
        double d;
        if(!num_of(v, &d)){
            if(CHECK_ONLY) d = 0;
            else { fail(c, "unary - on non-number"); j_free(v); return NULL; }
        }
        int isint = v->is_int;
        j_free(v);
        return isint ? j_int((long long)-d) : j_num(-d);
    }
    return parse_postfix(c);
}

static json *parse_mul(cctx *c){
    json *l = parse_unary(c);
    for(;;){
        skip(c);
        if(c->failed) return l;
        const char *op = NULL;
        if(!strncmp(c->p, "*", 1)) op = "*";
        else if(!strncmp(c->p, "/", 1)) op = "/";
        else if(!strncmp(c->p, "%", 1)) op = "%";
        else return l;
        c->p++;
        json *r = parse_unary(c);
        double a, b;
        if(!num_of(l, &a) || !num_of(r, &b)){
            if(CHECK_ONLY){ a = 0; b = 1; }
            else { fail(c, "arithmetic on non-numbers"); j_free(l); j_free(r); return NULL; }
        }
        int isint = l->is_int && r->is_int;
        double v = op[0] == '*' ? a * b : (op[0] == '/' ? (b ? a / b : 0) : (b ? fmod(a, b) : 0));
        if(op[0] != '*' && b == 0){ fail(c, "division by zero"); j_free(l); j_free(r); return NULL; }
        j_free(l); j_free(r);
        l = isint ? j_int((long long)v) : j_num(v);
    }
}

static json *parse_add(cctx *c){
    json *l = parse_mul(c);
    for(;;){
        skip(c);
        if(c->failed) return l;
        int minus;
        if(*c->p == '+') minus = 0;
        else if(*c->p == '-') minus = 1;
        else return l;
        c->p++;
        json *r = parse_mul(c);
        if(c->failed){ j_free(l); j_free(r); return NULL; }
        if(!minus && l && r && l->t == J_STR && r->t == J_STR){
            char *s = xasprintf("%s%s", l->s, r->s);
            j_free(l); j_free(r);
            l = j_str(s); free(s);
            continue;
        }
        if(!minus && l && r && l->t == J_ARR && r->t == J_ARR){
            for(size_t i = 0; i < r->nitems; i++) j_push(l, j_clone(r->items[i]));
            j_free(r);
            continue;
        }
        double a, b;
        if(!num_of(l, &a) || !num_of(r, &b)){
            if(CHECK_ONLY){ a = 0; b = 1; }
            else { fail(c, "arithmetic on non-numbers"); j_free(l); j_free(r); return NULL; }
        }
        int isint = l->is_int && r->is_int;
        double v = minus ? a - b : a + b;
        j_free(l); j_free(r);
        l = isint ? j_int((long long)v) : j_num(v);
    }
}

static int cmp_vals(json *a, json *b, int *ok){
    *ok = 1;
    double x, y;
    if(num_of(a, &x) && num_of(b, &y)) return x < y ? -1 : (x > y ? 1 : 0);
    if(a && b && a->t == J_STR && b->t == J_STR) return strcmp(a->s, b->s);
    *ok = 0;
    return 0;
}

static json *parse_rel(cctx *c){
    json *l = parse_add(c);
    for(;;){
        skip(c);
        if(c->failed) return l;
        const char *op = NULL;
        if(!strncmp(c->p, "==", 2)) op = "==";
        else if(!strncmp(c->p, "!=", 2)) op = "!=";
        else if(!strncmp(c->p, "<=", 2)) op = "<=";
        else if(!strncmp(c->p, ">=", 2)) op = ">=";
        else if(*c->p == '<') op = "<";
        else if(*c->p == '>') op = ">";
        else if(!strncmp(c->p, "in", 2) && !isalnum((unsigned char)c->p[2]) && c->p[2] != '_') op = "in";
        else return l;
        c->p += strlen(op);
        json *r = parse_add(c);
        if(c->failed){ j_free(l); j_free(r); return NULL; }
        json *out;
        if(!strcmp(op, "==")) out = j_bool(j_equal(l, r));
        else if(!strcmp(op, "!=")) out = j_bool(!j_equal(l, r));
        else if(!strcmp(op, "in")){
            int found = 0;
            if(r && r->t == J_ARR){
                for(size_t i = 0; i < r->nitems; i++)
                    if(j_equal(r->items[i], l)) found = 1;
            } else if(r && r->t == J_OBJ && l && l->t == J_STR){
                found = j_get(r, l->s) != NULL;
            } else if(!CHECK_ONLY){
                fail(c, "'in' requires a list or map");
            }
            out = j_bool(found);
        } else {
            int ok;
            int cv = cmp_vals(l, r, &ok);
            if(!ok && CHECK_ONLY) cv = 0;
            else if(!ok){ fail(c, "comparison of incompatible types"); j_free(l); j_free(r); return NULL; }
            out = j_bool(!strcmp(op, "<") ? cv < 0 : !strcmp(op, "<=") ? cv <= 0 :
                         !strcmp(op, ">") ? cv > 0 : cv >= 0);
        }
        j_free(l); j_free(r);
        l = out;
    }
}

/* CEL's logical operators absorb an error on the other side: `true || <error>`
   is true and `false && <error>` is false, whichever side the error is on. */
static json *parse_and(cctx *c){
    json *l = parse_rel(c);
    while(!c->failed){
        skip(c);
        if(strncmp(c->p, "&&", 2)) break;
        c->p += 2;
        int lfalse = l && l->t == J_BOOL && !l->b;
        char saved[sizeof c->err];
        memcpy(saved, c->err, sizeof saved);
        json *r = parse_rel(c);
        if(c->failed && lfalse){          /* false absorbs the error */
            c->failed = 0;
            memcpy(c->err, saved, sizeof saved);
            j_free(r); r = j_bool(0);
        }
        if(c->failed){ j_free(l); j_free(r); return NULL; }
        json *o = j_bool(truthy(l) && truthy(r));
        j_free(l); j_free(r);
        l = o;
    }
    return l;
}

static json *parse_or(cctx *c){
    json *l = parse_and(c);
    while(!c->failed){
        skip(c);
        if(strncmp(c->p, "||", 2)) break;
        c->p += 2;
        int ltrue = truthy(l);
        char saved[sizeof c->err];
        memcpy(saved, c->err, sizeof saved);
        json *r = parse_and(c);
        if(c->failed && ltrue){           /* true absorbs the error */
            c->failed = 0;
            memcpy(c->err, saved, sizeof saved);
            j_free(r); r = j_bool(0);
        }
        if(c->failed){ j_free(l); j_free(r); return NULL; }
        json *o = j_bool(ltrue || truthy(r));
        j_free(l); j_free(r);
        l = o;
    }
    return l;
}

static json *parse_ternary(cctx *c){
    if(++c->depth > 64){ fail(c, "expression too deep"); return NULL; }
    json *cond = parse_or(c);
    skip(c);
    if(!c->failed && *c->p == '?'){
        c->p++;
        json *a = parse_ternary(c);
        skip(c);
        if(!eat(c, ":")){ fail(c, "expected ':'"); j_free(cond); j_free(a); c->depth--; return NULL; }
        json *b = parse_ternary(c);
        json *out = truthy(cond) ? a : b;
        j_free(truthy(cond) ? b : a);
        j_free(cond);
        c->depth--;
        return out;
    }
    c->depth--;
    return cond;
}

json *cel_eval(const char *expr, json *activation, char **err){
    cctx c = { expr, activation, {0}, 0, 0 };
    json *v = parse_ternary(&c);
    if(!c.failed){
        skip(&c);
        if(*c.p) fail(&c, "unexpected trailing input");
    }
    if(c.failed){
        j_free(v);
        if(err) *err = xstrdup2(c.err);
        return NULL;
    }
    if(err) *err = NULL;
    return v;
}

char *cel_check(const char *expr){
    /* Compile check: parse the whole expression with data-dependent problems
       neutralised, so only a syntax or type error is reported. */
    json *act = j_obj();
    char *e = NULL;
    CHECK_ONLY = 1;
    json *v = cel_eval(expr, act, &e);
    CHECK_ONLY = 0;
    j_free(v);
    j_free(act);
    return e;
}
