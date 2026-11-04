#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void *xmalloc(size_t n){ void *p = calloc(1, n); if(!p){ fprintf(stderr,"oom\n"); abort(); } return p; }
static char *xstrdup(const char *s){ size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }

static json *nv(jtype t){ json *v = xmalloc(sizeof(json)); v->t = t; return v; }
json *j_null(void){ return nv(J_NULL); }
json *j_bool(int b){ json *v=nv(J_BOOL); v->b=!!b; return v; }
json *j_int(long long x){ json *v=nv(J_NUM); v->n=(double)x; v->is_int=1; return v; }
json *j_num(double x){ json *v=nv(J_NUM); v->n=x; v->is_int=(x==(double)(long long)x); return v; }
json *j_str(const char *s){ json *v=nv(J_STR); v->s=xstrdup(s?s:""); return v; }
json *j_strn(const char *s, size_t n){ json *v=nv(J_STR); v->s=xmalloc(n+1); memcpy(v->s,s,n); return v; }
json *j_arr(void){ return nv(J_ARR); }
json *j_obj(void){ return nv(J_OBJ); }

void j_free(json *v){
    if(!v) return;
    if(v->t==J_STR) free(v->s);
    else if(v->t==J_ARR){ for(size_t i=0;i<v->nitems;i++) j_free(v->items[i]); free(v->items); }
    else if(v->t==J_OBJ){ for(size_t i=0;i<v->nmem;i++){ free(v->mem[i].key); j_free(v->mem[i].val);} free(v->mem); }
    free(v);
}

json *j_clone(const json *v){
    if(!v) return NULL;
    switch(v->t){
    case J_NULL: return j_null();
    case J_BOOL: return j_bool(v->b);
    case J_NUM: { json *o=nv(J_NUM); o->n=v->n; o->is_int=v->is_int; return o; }
    case J_STR: return j_str(v->s);
    case J_ARR: { json *o=j_arr(); for(size_t i=0;i<v->nitems;i++) j_push(o, j_clone(v->items[i])); return o; }
    case J_OBJ: { json *o=j_obj(); for(size_t i=0;i<v->nmem;i++) j_set(o, v->mem[i].key, j_clone(v->mem[i].val)); return o; }
    }
    return j_null();
}

int j_is(const json *v, jtype t){ return v && v->t==t; }
const char *j_str_of(const json *v){ return (v && v->t==J_STR) ? v->s : NULL; }

json *j_get(const json *o, const char *key){
    if(!o || o->t!=J_OBJ) return NULL;
    for(size_t i=0;i<o->nmem;i++) if(strcmp(o->mem[i].key,key)==0) return o->mem[i].val;
    return NULL;
}
const char *j_gets(const json *o, const char *key){ return j_str_of(j_get(o,key)); }
long long j_geti(const json *o, const char *key, long long d){
    json *v=j_get(o,key); return (v&&v->t==J_NUM)?(long long)v->n:d;
}
int j_getb(const json *o, const char *key, int d){
    json *v=j_get(o,key); return (v&&v->t==J_BOOL)?v->b:d;
}
void j_set(json *o, const char *key, json *v){
    if(!o||o->t!=J_OBJ){ j_free(v); return; }
    for(size_t i=0;i<o->nmem;i++) if(strcmp(o->mem[i].key,key)==0){ j_free(o->mem[i].val); o->mem[i].val=v; return; }
    if(o->nmem==o->cmem){ o->cmem = o->cmem? o->cmem*2 : 8; o->mem = realloc(o->mem, o->cmem*sizeof(jmember)); }
    o->mem[o->nmem].key = xstrdup(key); o->mem[o->nmem].val = v; o->nmem++;
}
void j_del(json *o, const char *key){
    if(!o||o->t!=J_OBJ) return;
    for(size_t i=0;i<o->nmem;i++) if(strcmp(o->mem[i].key,key)==0){
        free(o->mem[i].key); j_free(o->mem[i].val);
        memmove(&o->mem[i], &o->mem[i+1], (o->nmem-i-1)*sizeof(jmember)); o->nmem--; return;
    }
}
json *j_ensure_obj(json *o, const char *key){
    json *v=j_get(o,key);
    if(!v || v->t!=J_OBJ){ v=j_obj(); j_set(o,key,v); }
    return v;
}
void j_push(json *a, json *v){
    if(!a||a->t!=J_ARR){ j_free(v); return; }
    if(a->nitems==a->citems){ a->citems = a->citems? a->citems*2 : 8; a->items = realloc(a->items, a->citems*sizeof(json*)); }
    a->items[a->nitems++] = v;
}
json *j_at(const json *a, size_t i){ return (a&&a->t==J_ARR&&i<a->nitems)?a->items[i]:NULL; }
void j_remove_at(json *a, size_t i){
    if(!a||a->t!=J_ARR||i>=a->nitems) return;
    j_free(a->items[i]);
    memmove(&a->items[i], &a->items[i+1], (a->nitems-i-1)*sizeof(json*)); a->nitems--;
}

json *j_path(const json *root, const char *path){
    const json *cur = root; char buf[256];
    while(cur && *path){
        if(*path=='.') path++;
        size_t k=0;
        while(path[k] && path[k]!='.' && path[k]!='[') k++;
        if(k){ if(k>=sizeof buf) return NULL; memcpy(buf,path,k); buf[k]=0; cur = j_get(cur, buf); path += k; }
        while(cur && *path=='['){
            char *end; long idx = strtol(path+1,&end,10);
            if(*end!=']') return NULL;
            cur = j_at(cur, (size_t)idx); path = end+1;
        }
    }
    return (json*)cur;
}

int j_equal(const json *a, const json *b){
    if(a==b) return 1;
    if(!a||!b||a->t!=b->t) return 0;
    switch(a->t){
    case J_NULL: return 1;
    case J_BOOL: return a->b==b->b;
    case J_NUM: return a->n==b->n;
    case J_STR: return strcmp(a->s,b->s)==0;
    case J_ARR:
        if(a->nitems!=b->nitems) return 0;
        for(size_t i=0;i<a->nitems;i++) if(!j_equal(a->items[i],b->items[i])) return 0;
        return 1;
    case J_OBJ:
        if(a->nmem!=b->nmem) return 0;
        for(size_t i=0;i<a->nmem;i++){
            json *o = j_get(b, a->mem[i].key);
            if(!o || !j_equal(a->mem[i].val, o)) return 0;
        }
        return 1;
    }
    return 0;
}

/* ---------- serialisation ---------- */
typedef struct { char *p; size_t n, c; } sbuf;
static void sb_ensure(sbuf *s, size_t extra){
    if(s->n+extra+1 > s->c){ s->c = (s->n+extra+1)*2; s->p = realloc(s->p, s->c); }
}
static void sb_add(sbuf *s, const char *t, size_t n){ sb_ensure(s,n); memcpy(s->p+s->n,t,n); s->n+=n; s->p[s->n]=0; }
static void sb_cstr(sbuf *s, const char *t){ sb_add(s,t,strlen(t)); }
static void sb_char(sbuf *s, char c){ sb_add(s,&c,1); }

static void esc(sbuf *s, const char *t){
    sb_char(s,'"');
    for(const unsigned char *p=(const unsigned char*)t; *p; p++){
        switch(*p){
        case '"': sb_cstr(s,"\\\""); break;
        case '\\': sb_cstr(s,"\\\\"); break;
        case '\n': sb_cstr(s,"\\n"); break;
        case '\r': sb_cstr(s,"\\r"); break;
        case '\t': sb_cstr(s,"\\t"); break;
        case '\b': sb_cstr(s,"\\b"); break;
        case '\f': sb_cstr(s,"\\f"); break;
        default:
            if(*p < 0x20){ char b[8]; snprintf(b,sizeof b,"\\u%04x",*p); sb_cstr(s,b); }
            else sb_char(s,(char)*p);
        }
    }
    sb_char(s,'"');
}

static int memcmp_key(const void *a, const void *b){
    return strcmp(((const jmember*)a)->key, ((const jmember*)b)->key);
}

static void dump(sbuf *s, const json *v, int sorted){
    if(!v){ sb_cstr(s,"null"); return; }
    char b[64];
    switch(v->t){
    case J_NULL: sb_cstr(s,"null"); break;
    case J_BOOL: sb_cstr(s, v->b?"true":"false"); break;
    case J_NUM:
        if(v->is_int && fabs(v->n) < 9.2e18) snprintf(b,sizeof b,"%lld",(long long)v->n);
        else { snprintf(b,sizeof b,"%.17g",v->n); }
        sb_cstr(s,b); break;
    case J_STR: esc(s, v->s); break;
    case J_ARR:
        sb_char(s,'[');
        for(size_t i=0;i<v->nitems;i++){ if(i) sb_char(s,','); dump(s,v->items[i],sorted); }
        sb_char(s,']'); break;
    case J_OBJ: {
        sb_char(s,'{');
        if(sorted && v->nmem){
            jmember *tmp = xmalloc(v->nmem*sizeof(jmember));
            memcpy(tmp, v->mem, v->nmem*sizeof(jmember));
            qsort(tmp, v->nmem, sizeof(jmember), memcmp_key);
            for(size_t i=0;i<v->nmem;i++){ if(i) sb_char(s,','); esc(s,tmp[i].key); sb_char(s,':'); dump(s,tmp[i].val,sorted); }
            free(tmp);
        } else {
            for(size_t i=0;i<v->nmem;i++){ if(i) sb_char(s,','); esc(s,v->mem[i].key); sb_char(s,':'); dump(s,v->mem[i].val,sorted); }
        }
        sb_char(s,'}'); break; }
    }
}

char *j_dump(const json *v){ sbuf s={0}; sb_ensure(&s,64); s.p[0]=0; dump(&s,v,0); return s.p; }
char *j_dump_sorted(const json *v){ sbuf s={0}; sb_ensure(&s,64); s.p[0]=0; dump(&s,v,1); return s.p; }

/* ---------- parsing ---------- */
typedef struct { const char *p, *end; int err; } pctx;
static void skipws(pctx *c){ while(c->p<c->end && (*c->p==' '||*c->p=='\t'||*c->p=='\n'||*c->p=='\r')) c->p++; }
static json *pval(pctx *c);

static int hex4(const char *p){
    int v=0;
    for(int i=0;i<4;i++){ int d; char x=p[i];
        if(x>='0'&&x<='9') d=x-'0'; else if(x>='a'&&x<='f') d=x-'a'+10;
        else if(x>='A'&&x<='F') d=x-'A'+10; else return -1;
        v=v*16+d; }
    return v;
}
static void utf8(sbuf *s, unsigned cp){
    if(cp<0x80) sb_char(s,(char)cp);
    else if(cp<0x800){ sb_char(s,(char)(0xC0|(cp>>6))); sb_char(s,(char)(0x80|(cp&0x3F))); }
    else if(cp<0x10000){ sb_char(s,(char)(0xE0|(cp>>12))); sb_char(s,(char)(0x80|((cp>>6)&0x3F))); sb_char(s,(char)(0x80|(cp&0x3F))); }
    else { sb_char(s,(char)(0xF0|(cp>>18))); sb_char(s,(char)(0x80|((cp>>12)&0x3F))); sb_char(s,(char)(0x80|((cp>>6)&0x3F))); sb_char(s,(char)(0x80|(cp&0x3F))); }
}

static char *pstr(pctx *c){
    if(c->p>=c->end || *c->p!='"'){ c->err=1; return NULL; }
    c->p++;
    sbuf s={0}; sb_ensure(&s,16); s.p[0]=0;
    while(c->p<c->end && *c->p!='"'){
        if(*c->p=='\\'){
            c->p++;
            if(c->p>=c->end){ c->err=1; break; }
            switch(*c->p){
            case '"': sb_char(&s,'"'); break;
            case '\\': sb_char(&s,'\\'); break;
            case '/': sb_char(&s,'/'); break;
            case 'n': sb_char(&s,'\n'); break;
            case 't': sb_char(&s,'\t'); break;
            case 'r': sb_char(&s,'\r'); break;
            case 'b': sb_char(&s,'\b'); break;
            case 'f': sb_char(&s,'\f'); break;
            case 'u': {
                if(c->p+4>=c->end){ c->err=1; break; }
                int cp = hex4(c->p+1); if(cp<0){ c->err=1; break; }
                c->p += 4;
                if(cp>=0xD800 && cp<=0xDBFF && c->p+6<c->end && c->p[1]=='\\' && c->p[2]=='u'){
                    int lo = hex4(c->p+3);
                    if(lo>=0xDC00 && lo<=0xDFFF){ cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00); c->p += 6; }
                }
                utf8(&s,(unsigned)cp); break; }
            default: c->err=1;
            }
            c->p++;
        } else sb_char(&s,*c->p++);
    }
    if(c->p>=c->end){ c->err=1; free(s.p); return NULL; }
    c->p++;
    return s.p ? s.p : xstrdup("");
}

static json *pval(pctx *c){
    skipws(c);
    if(c->p>=c->end){ c->err=1; return NULL; }
    char ch = *c->p;
    if(ch=='{'){
        c->p++; json *o=j_obj();
        skipws(c);
        if(c->p<c->end && *c->p=='}'){ c->p++; return o; }
        for(;;){
            skipws(c);
            char *k = pstr(c);
            if(c->err){ free(k); j_free(o); return NULL; }
            skipws(c);
            if(c->p>=c->end || *c->p!=':'){ c->err=1; free(k); j_free(o); return NULL; }
            c->p++;
            json *v = pval(c);
            if(c->err){ free(k); j_free(v); j_free(o); return NULL; }
            j_set(o,k,v); free(k);
            skipws(c);
            if(c->p<c->end && *c->p==','){ c->p++; continue; }
            if(c->p<c->end && *c->p=='}'){ c->p++; return o; }
            c->err=1; j_free(o); return NULL;
        }
    }
    if(ch=='['){
        c->p++; json *a=j_arr();
        skipws(c);
        if(c->p<c->end && *c->p==']'){ c->p++; return a; }
        for(;;){
            json *v = pval(c);
            if(c->err){ j_free(v); j_free(a); return NULL; }
            j_push(a,v);
            skipws(c);
            if(c->p<c->end && *c->p==','){ c->p++; continue; }
            if(c->p<c->end && *c->p==']'){ c->p++; return a; }
            c->err=1; j_free(a); return NULL;
        }
    }
    if(ch=='"'){ char *s=pstr(c); if(c->err){ free(s); return NULL; } json *v=nv(J_STR); v->s=s; return v; }
    if(!strncmp(c->p,"true",4) && c->end-c->p>=4){ c->p+=4; return j_bool(1); }
    if(!strncmp(c->p,"false",5) && c->end-c->p>=5){ c->p+=5; return j_bool(0); }
    if(!strncmp(c->p,"null",4) && c->end-c->p>=4){ c->p+=4; return j_null(); }
    { char *e; double d = strtod(c->p,&e);
      if(e==c->p){ c->err=1; return NULL; }
      int isint = 1;
      for(const char *q=c->p;q<e;q++) if(*q=='.'||*q=='e'||*q=='E'){ isint=0; break; }
      c->p = e; json *v=nv(J_NUM); v->n=d; v->is_int=isint; return v; }
}

json *j_parse(const char *text, size_t len){
    pctx c = { text, text+len, 0 };
    json *v = pval(&c);
    if(c.err){ j_free(v); return NULL; }
    skipws(&c);
    return v;
}
json *j_parse0(const char *t){ return j_parse(t, strlen(t)); }
