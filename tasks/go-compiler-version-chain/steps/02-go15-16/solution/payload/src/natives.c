#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

extern Value str_concat(Value a, Value b);
extern int str_cmp(Value a, Value b);
extern Value slice_append(Value s, Value *elems, int n, Type *slicet);
extern Value make_method_value(FuncDef *fn, Value recv);
extern Value chan_make(Type *t, int64_t cap);
extern int utf8_decode(const char *s, int64_t len, unsigned *r);
extern Value vm_convert(Value v, Type *to);
extern int64_t vm_nanotime(void);
extern void ensure_method_compiled(Method *m);
extern Value make_runtime_error(const char *msg);
extern int g_panic_nil_error;
extern Type *ty_runtime_error, *ty_panicnil;
extern void gc_pin_obj(Obj *o);

static const char *sdata(Value v){ return v.o ? OBJ_BYTEPTR(v.o)+v.off : ""; }

Value mkstr(const char *s, size_t n){ return val_string(s,n); }
static Value mkstr0(const char *s){ return val_string(s,strlen(s)); }

/* ---------------------------------------------------------- float fmt --- */

static int shortest_digits(double d, int bits, char *out, size_t outn){
    for (int prec=1; prec<=17; prec++){
        snprintf(out,outn,"%.*e",prec-1,d);
        double back = strtod(out,NULL);
        if (bits==32 ? ((float)back==(float)d) : (back==d)) return prec;
    }
    snprintf(out,outn,"%.17e",d);
    return 17;
}

/* Format like Go's strconv.FormatFloat(d, fmtc, prec, bits). */
void go_format_float(Buf *b, double d, char fmtc, int prec, int bits){
    if (isnan(d)){ buf_puts(b,"NaN"); return; }
    if (isinf(d)){ buf_puts(b, d<0?"-Inf":"+Inf"); return; }
    char ebuf[64];
    int nd = 0, dexp = 0;
    if (prec < 0){
        nd = shortest_digits(d,bits,ebuf,sizeof ebuf);
        const char *ep = strchr(ebuf,'e');
        dexp = ep ? atoi(ep+1) : 0;
    }
    char tmp[512];
    switch (fmtc){
    case 'e': case 'E': {
        int p = prec<0 ? nd-1 : prec;
        snprintf(tmp,sizeof tmp,"%.*e",p,d);
        /* C prints at least two exponent digits, like Go */
        buf_puts(b,tmp);
        if (fmtc=='E') for (size_t i=0;i<b->len;i++) if (b->p[i]=='e') b->p[i]='E';
        return;
    }
    case 'f': case 'F': {
        int p = prec;
        if (p < 0){
            p = nd-1-dexp;
            if (p < 0) p = 0;
        }
        snprintf(tmp,sizeof tmp,"%.*f",p,d);
        buf_puts(b,tmp);
        return;
    }
    case 'g': case 'G': {
        int eprec = prec;
        if (prec < 0) eprec = 6;
        else if (eprec == 0) eprec = 1;
        int exp;
        if (prec < 0) exp = dexp;
        else {
            snprintf(tmp,sizeof tmp,"%.*e",prec>0?prec-1:0,d);
            const char *ep = strchr(tmp,'e');
            exp = ep?atoi(ep+1):0;
        }
        if (prec >= 0 && eprec > 21) eprec = 21;
        if (exp < -4 || exp >= eprec){
            Buf t2; buf_init(&t2);
            go_format_float(&t2,d,fmtc=='g'?'e':'E',prec<0?-1:prec-1,bits);
            /* strip trailing zeros in the mantissa for shortest */
            buf_put(b,t2.p,t2.len);
            buf_free(&t2);
            return;
        }
        Buf t2; buf_init(&t2);
        go_format_float(&t2,d,fmtc=='g'?'f':'F',prec<0?-1:(prec-1-exp<0?0:prec-1-exp),bits);
        buf_put(b,t2.p,t2.len);
        buf_free(&t2);
        return;
    }
    case 'b': case 'x': case 'X':
    default:
        snprintf(tmp,sizeof tmp,"%g",d);
        buf_puts(b,tmp);
        return;
    }
}

/* ------------------------------------------------------------ printing --- */

static void quote_string(Buf *b, const char *s, int64_t n, int rune_quote){
    buf_putc(b, rune_quote?'\'':'"');
    for (int64_t i=0;i<n;){
        unsigned r; int sz = utf8_decode(s+i,n-i,&r);
        if (sz==0) sz=1;
        unsigned char c = (unsigned char)s[i];
        if (r==0xFFFD && sz==1 && c>=0x80){ buf_printf(b,"\\x%02x",c); i++; continue; }
        switch (r){
        case '\\': buf_puts(b,"\\\\"); break;
        case '\n': buf_puts(b,"\\n"); break;
        case '\t': buf_puts(b,"\\t"); break;
        case '\r': buf_puts(b,"\\r"); break;
        case '\a': buf_puts(b,"\\a"); break;
        case '\b': buf_puts(b,"\\b"); break;
        case '\f': buf_puts(b,"\\f"); break;
        case '\v': buf_puts(b,"\\v"); break;
        case '"': if (!rune_quote) buf_puts(b,"\\\""); else buf_putc(b,'"'); break;
        case '\'': if (rune_quote) buf_puts(b,"\\'"); else buf_putc(b,'\''); break;
        default:
            if (r < 0x20 || r == 0x7f) buf_printf(b,"\\x%02x",r);
            else buf_put(b,s+i,(size_t)sz);
            break;
        }
        i += sz;
    }
    buf_putc(b, rune_quote?'\'':'"');
}

typedef struct FmtFlags { int minus, plus, sharp, space, zero; int width, prec; int haswidth, hasprec; } FmtFlags;

static void format_value(Buf *b, Value v, char verb, FmtFlags *f, int depth);

static int try_stringer(Buf *b, Value v, char verb, int depth){
    if (depth > 8) return 0;
    if (!v.t) return 0;
    if (verb!='v' && verb!='s' && verb!='q') return 0;
    const char *names[2] = {"Error","String"};
    for (int i=0;i<2;i++){
        int ind = 0;
        Method *m = lookup_method(v.t,intern(names[i]),&ind);
        if (!m) continue;
        if (m->sig->fields.len != 0 || m->sig->results.len != 1) continue;
        if (!is_string(((Field*)m->sig->results.p[0])->type)) continue;
        if (m->ptr_recv && v.k != VK_PTR) continue;
        ensure_method_compiled(m);
        if (!m->def) continue;
        if (v.k==VK_PTR && !v.o) { buf_puts(b,"<nil>"); return 1; }
        Value fn = make_method_value(m->def,v);
        Value rets[2];
        int n = vm_call_value(fn,NULL,0,rets,2);
        if (n>=1 && rets[0].k==VK_STRING){
            if (verb=='q') quote_string(b,sdata(rets[0]),rets[0].len,0);
            else buf_put(b,sdata(rets[0]),(size_t)rets[0].len);
            return 1;
        }
        return 1;
    }
    return 0;
}

static void format_int(Buf *b, Value v, char verb, FmtFlags *f){
    char tmp[128];
    int isneg = 0;
    uint64_t u;
    if (v.k==VK_UINT) u = v.b.u;
    else { int64_t x = v.b.i; if (x<0){ isneg=1; u = (uint64_t)(-(x+1))+1; } else u=(uint64_t)x; }
    int base = 10;
    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    switch (verb){
    case 'b': base=2; break;
    case 'o': case 'O': base=8; break;
    case 'x': base=16; break;
    case 'X': base=16; digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"; break;
    case 'c': { Buf t; buf_init(&t); extern void utf8_put_pub(Buf*,unsigned); utf8_put_pub(&t,(unsigned)(v.k==VK_UINT?v.b.u:v.b.i)); buf_put(b,t.p,t.len); buf_free(&t); return; }
    case 'q': { Buf t; buf_init(&t); extern void utf8_put_pub(Buf*,unsigned); utf8_put_pub(&t,(unsigned)(v.k==VK_UINT?v.b.u:v.b.i)); quote_string(b,t.p,t.len,1); buf_free(&t); return; }
    case 'U': { buf_printf(b,"U+%04llX",(unsigned long long)(v.k==VK_UINT?v.b.u:v.b.i)); return; }
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        { double d = v.k==VK_UINT?(double)v.b.u:(double)v.b.i;
          go_format_float(b,d,verb,f->hasprec?f->prec:-1,64); return; }
    default: base=10; break;
    }
    int n = 0;
    if (u==0) tmp[n++]='0';
    while (u){ tmp[n++] = digits[u % (unsigned)base]; u /= (unsigned)base; }
    Buf num; buf_init(&num);
    if (isneg) buf_putc(&num,'-');
    else if (f->plus) buf_putc(&num,'+');
    else if (f->space) buf_putc(&num,' ');
    if (f->sharp){
        if (base==8) buf_putc(&num,'0');
        else if (base==16 && verb=='x') buf_puts(&num,"0x");
        else if (base==16 && verb=='X') buf_puts(&num,"0X");
        else if (base==2) buf_puts(&num,"0b");
    }
    if (verb=='O') buf_puts(&num,"0o");
    if (f->hasprec){
        for (int i=n;i<f->prec;i++) buf_putc(&num,'0');
    }
    for (int i=n-1;i>=0;i--) buf_putc(&num,tmp[i]);
    buf_put(b,num.p?num.p:"",num.len);
    buf_free(&num);
}

static void pad(Buf *out, Buf *body, FmtFlags *f){
    int w = f->haswidth ? f->width : 0;
    /* count runes, not bytes */
    int64_t n = 0;
    for (size_t i=0;i<body->len;){ unsigned r; int sz = utf8_decode(body->p+i,(int64_t)(body->len-i),&r); if (sz==0) sz=1; i+=sz; n++; }
    int64_t padn = w - n;
    if (padn <= 0){ buf_put(out,body->p?body->p:"",body->len); return; }
    if (f->minus){
        buf_put(out,body->p?body->p:"",body->len);
        for (int64_t i=0;i<padn;i++) buf_putc(out,' ');
    } else {
        char pc = f->zero ? '0' : ' ';
        if (pc=='0' && body->len && (body->p[0]=='-'||body->p[0]=='+')){
            buf_putc(out,body->p[0]);
            for (int64_t i=0;i<padn;i++) buf_putc(out,pc);
            buf_put(out,body->p+1,body->len-1);
            return;
        }
        for (int64_t i=0;i<padn;i++) buf_putc(out,pc);
        buf_put(out,body->p?body->p:"",body->len);
    }
}

static int map_sorted_pairs(Value v, Vec *out);

static void format_value(Buf *b, Value v, char verb, FmtFlags *f, int depth){
    if (verb=='T'){ buf_puts(b, v.t?type_string(v.t):"<nil>"); return; }
    if (verb=='p'){
        if (!v.o) buf_puts(b,"<nil>");
        else buf_printf(b,"0x%llx",(unsigned long long)(uintptr_t)v.o + v.off);
        return;
    }
    if (v.k==VK_NIL && !v.t){ buf_puts(b, verb=='v'?"<nil>":"%!"); if (verb!='v') buf_printf(b,"%c(<nil>)",verb); return; }
    if (f->sharp && verb=='v'){
        /* Go-syntax representation (limited) */
        if (v.k==VK_STRING){ quote_string(b,sdata(v),v.len,0); return; }
    }
    if (try_stringer(b,v,verb,depth)) return;
    switch (v.k){
    case VK_BOOL:
        if (verb=='v'||verb=='t') buf_puts(b, v.b.i?"true":"false");
        else buf_printf(b,"%%!%c(bool=%s)",verb,v.b.i?"true":"false");
        return;
    case VK_INT: case VK_UINT: format_int(b,v,verb=='v'?'d':verb,f); return;
    case VK_FLOAT: {
        char fv = verb=='v'?'g':verb;
        if (fv=='d'){ buf_printf(b,"%%!d(float64=%g)",v.b.d); return; }
        int prec = f->hasprec ? f->prec : (verb=='v'?-1:(fv=='g'||fv=='G'?-1:6));
        Buf t; buf_init(&t);
        if (f->plus && v.b.d >= 0) buf_putc(&t,'+');
        go_format_float(&t,v.b.d,fv,prec,underlying(v.t?v.t:ty_float64)->k==T_FLOAT32?32:64);
        buf_put(b,t.p?t.p:"",t.len);
        buf_free(&t);
        return;
    }
    case VK_STRING: {
        if (verb=='q'){ quote_string(b,sdata(v),v.len,0); return; }
        if (verb=='x'||verb=='X'){
            const char *s = sdata(v);
            for (int64_t i=0;i<v.len;i++) buf_printf(b,verb=='x'?"%02x":"%02X",(unsigned char)s[i]);
            return;
        }
        if (verb=='d'){ buf_printf(b,"%%!d(string=%.*s)",(int)v.len,sdata(v)); return; }
        if (f->hasprec && f->prec < v.len) buf_put(b,sdata(v),(size_t)f->prec);
        else buf_put(b,sdata(v),(size_t)v.len);
        return;
    }
    case VK_SLICE: case VK_ARRAY: {
        Type *u = v.t?underlying(v.t):NULL;
        Type *et = u?u->elem:NULL;
        if (et && underlying(et)->k==T_UINT8 && (verb=='s'||verb=='q'||verb=='x'||verb=='X')){
            Buf t; buf_init(&t);
            int64_t n = v.k==VK_SLICE?v.len:(v.o?v.o->n:0);
            Value *c = v.o?OBJ_CELLPTR(v.o):NULL;
            for (int64_t i=0;i<n;i++) buf_putc(&t,(int)(c[v.off+i].k==VK_UINT?c[v.off+i].b.u:c[v.off+i].b.i));
            Value sv = val_nil(); sv.k=VK_STRING; sv.len=t.len;
            if (verb=='q') quote_string(b,t.p?t.p:"",t.len,0);
            else if (verb=='x'||verb=='X'){ for (size_t i=0;i<t.len;i++) buf_printf(b,verb=='x'?"%02x":"%02X",(unsigned char)t.p[i]); }
            else buf_put(b,t.p?t.p:"",t.len);
            buf_free(&t);
            return;
        }
        if (v.k==VK_SLICE && !v.o && verb=='v' && !f->sharp){ buf_puts(b,"[]"); return; }
        buf_putc(b,'[');
        int64_t n = v.k==VK_SLICE?v.len:(v.o?v.o->n:0);
        Value *c = v.o?OBJ_CELLPTR(v.o):NULL;
        for (int64_t i=0;i<n;i++){
            if (i) buf_putc(b,' ');
            format_value(b,c[(v.k==VK_SLICE?v.off:0)+i],verb,f,depth+1);
        }
        buf_putc(b,']');
        return;
    }
    case VK_STRUCT: {
        Type *u = v.t?underlying(v.t):NULL;
        buf_putc(b,'{');
        if (u && v.o){
            Value *c = OBJ_CELLPTR(v.o);
            FmtFlags inner = *f;
            inner.plus = 0; inner.haswidth = 0; inner.hasprec = 0; inner.zero = 0;
            for (int i=0;i<u->fields.len && i<(int)v.o->n;i++){
                if (i) buf_putc(b,' ');
                if (f->plus){ Field *fl=u->fields.p[i]; buf_printf(b,"%s:",fl->name?fl->name:""); }
                format_value(b,c[i],verb,&inner,depth+1);
            }
        }
        buf_putc(b,'}');
        return;
    }
    case VK_MAP: {
        if (!v.o){ buf_puts(b,"map[]"); return; }
        buf_puts(b,"map[");
        Vec pairs; memset(&pairs,0,sizeof pairs);
        map_sorted_pairs(v,&pairs);
        for (int i=0;i<pairs.len;i++){
            Value *kv = pairs.p[i];
            if (i) buf_putc(b,' ');
            format_value(b,kv[0],verb,f,depth+1);
            buf_putc(b,':');
            format_value(b,kv[1],verb,f,depth+1);
            free(kv);
        }
        vec_free(&pairs);
        buf_putc(b,']');
        return;
    }
    case VK_PTR: {
        if (!v.o){ buf_puts(b,"<nil>"); return; }
        Type *u = v.t?underlying(v.t):NULL;
        if (u && u->k==T_PTR && underlying(u->elem)->k==T_STRUCT && depth<6){
            buf_putc(b,'&');
            format_value(b,OBJ_CELLPTR(v.o)[v.off],verb,f,depth+1);
            return;
        }
        buf_printf(b,"0x%llx",(unsigned long long)(uintptr_t)v.o+v.off);
        return;
    }
    case VK_CHAN: case VK_FUNC:
        if (!v.o) buf_puts(b,"<nil>");
        else buf_printf(b,"0x%llx",(unsigned long long)(uintptr_t)v.o);
        return;
    default:
        buf_puts(b,"<nil>");
        return;
    }
}

static int pair_cmp(const void *A, const void *B){
    Value *a = *(Value *const *)A, *bq = *(Value *const *)B;
    Value x=a[0], y=bq[0];
    if (x.k==VK_STRING && y.k==VK_STRING) return str_cmp(x,y);
    if (x.k==VK_FLOAT||y.k==VK_FLOAT){ double dx=x.b.d,dy=y.b.d; return dx<dy?-1:(dx>dy?1:0); }
    if (x.k==VK_UINT||y.k==VK_UINT){ return x.b.u<y.b.u?-1:(x.b.u>y.b.u?1:0); }
    if (x.k==VK_BOOL) return (int)x.b.i-(int)y.b.i;
    return x.b.i<y.b.i?-1:(x.b.i>y.b.i?1:0);
}

static int map_sorted_pairs(Value v, Vec *out){
    if (!v.o) return 0;
    map_keys((GoMap*)v.o->aux,out);
    if (out->len) qsort(out->p,(size_t)out->len,sizeof(void*),pair_cmp);
    return out->len;
}

char *format_value_for_panic(Value v){
    Buf b; buf_init(&b);
    FmtFlags f; memset(&f,0,sizeof f);
    if (v.t && ty_runtime_error && type_identical(v.t,ty_runtime_error)){
        buf_put(&b,sdata(v),(size_t)v.len);
    } else if (v.k==VK_STRING && (!v.t || underlying(v.t)->k==T_STRING) && (!v.t || v.t==ty_string)){
        buf_put(&b,sdata(v),(size_t)v.len);
    } else {
        format_value(&b,v,'v',&f,0);
        if (v.k==VK_STRING) { buf_free(&b); buf_init(&b); buf_put(&b,sdata(v),(size_t)v.len); }
    }
    return b.p?b.p:xstrdup("");
}

/* `go test` forwards everything the test binary writes -- stdout and stderr
 * alike -- to its own standard output, so the built-in print stream follows
 * the test driver when one is running. */
FILE *g_print_stream = NULL;

void builtin_print(Value *args, int n, int ln){
    Buf b; buf_init(&b);
    FmtFlags f; memset(&f,0,sizeof f);
    for (int i=0;i<n;i++){
        if (ln && i) buf_putc(&b,' ');
        Value v = args[i];
        switch (v.k){
        case VK_FLOAT: { char tmp[64];
            if (isnan(v.b.d)){ buf_puts(&b,"NaN"); break; }
            if (isinf(v.b.d)){ buf_puts(&b, v.b.d<0?"-Inf":"+Inf"); break; }
            snprintf(tmp,sizeof tmp,"%+e",v.b.d);
            /* gc prints three exponent digits */
            char *e = strchr(tmp,'e');
            if (e && (e[1]=='+'||e[1]=='-') && strlen(e+2)==2){ char save[8]; snprintf(save,sizeof save,"%c0%s",e[1],e+2); snprintf(e+1,sizeof tmp-(size_t)(e+1-tmp),"%s",save); }
            buf_puts(&b,tmp); break; }
        case VK_STRING: buf_put(&b,sdata(v),(size_t)v.len); break;
        case VK_BOOL: buf_puts(&b,v.b.i?"true":"false"); break;
        case VK_INT: buf_printf(&b,"%lld",(long long)v.b.i); break;
        case VK_UINT: buf_printf(&b,"%llu",(unsigned long long)v.b.u); break;
        case VK_NIL: buf_puts(&b,"<nil>"); break;
        case VK_PTR: case VK_CHAN: case VK_FUNC: case VK_MAP:
            if (!v.o) buf_puts(&b,"0x0"); else buf_printf(&b,"0x%llx",(unsigned long long)(uintptr_t)v.o);
            break;
        case VK_SLICE: buf_printf(&b,"[%lld/%lld]0x%llx",(long long)v.len,(long long)v.cap,(unsigned long long)(uintptr_t)v.o); break;
        default: format_value(&b,v,'v',&f,0); break;
        }
    }
    if (ln) buf_putc(&b,'\n');
    fflush(stdout);
    { FILE *out = g_print_stream ? g_print_stream : stderr;
      fwrite(b.p?b.p:"",1,b.len,out);
      fflush(out); }
    buf_free(&b);
}

/* ---------------------------------------------------------- sprintf --- */

Value go_sprintf(Value format, Value argslice){
    Buf out; buf_init(&out);
    const char *fs = sdata(format);
    int64_t flen = format.len;
    Value *args = argslice.o?OBJ_CELLPTR(argslice.o):NULL;
    int64_t nargs = argslice.len;
    int64_t ai = 0;
    for (int64_t i=0;i<flen;i++){
        if (fs[i] != '%'){ buf_putc(&out,fs[i]); continue; }
        i++;
        if (i>=flen){ buf_puts(&out,"%!(NOVERB)"); break; }
        FmtFlags f; memset(&f,0,sizeof f);
        for (; i<flen; i++){
            if (fs[i]=='-') f.minus=1;
            else if (fs[i]=='+') f.plus=1;
            else if (fs[i]=='#') f.sharp=1;
            else if (fs[i]==' ') f.space=1;
            else if (fs[i]=='0') f.zero=1;
            else break;
        }
        if (i<flen && fs[i]=='*'){ if (ai<nargs){ f.width=(int)args[ai].b.i; f.haswidth=1; ai++; } i++; }
        else while (i<flen && isdigit((unsigned char)fs[i])){ f.width = f.width*10 + (fs[i]-'0'); f.haswidth=1; i++; }
        if (i<flen && fs[i]=='.'){
            i++; f.hasprec=1; f.prec=0;
            if (i<flen && fs[i]=='*'){ if (ai<nargs){ f.prec=(int)args[ai].b.i; ai++; } i++; }
            else while (i<flen && isdigit((unsigned char)fs[i])){ f.prec = f.prec*10 + (fs[i]-'0'); i++; }
        }
        if (i>=flen){ buf_puts(&out,"%!(NOVERB)"); break; }
        char verb = fs[i];
        if (verb=='%'){ buf_putc(&out,'%'); continue; }
        if (verb=='w') verb = 'v';     /* %w formats like %v and records the wrapped error */
        if (ai >= nargs){ buf_printf(&out,"%%!%c(MISSING)",verb); continue; }
        Value v = args[ai++];
        Buf body; buf_init(&body);
        format_value(&body,v,verb,&f,0);
        pad(&out,&body,&f);
        buf_free(&body);
    }
    if (ai < nargs){
        buf_puts(&out,"%!(EXTRA ");
        for (int64_t i=ai;i<nargs;i++){
            if (i>ai) buf_puts(&out,", ");
            FmtFlags f; memset(&f,0,sizeof f);
            buf_printf(&out,"%s=",args[i].t?type_string(args[i].t):"<nil>");
            format_value(&out,args[i],'v',&f,0);
        }
        buf_putc(&out,')');
    }
    Value r = val_string(out.p?out.p:"",out.len);
    buf_free(&out);
    return r;
}

Value go_sprint(Value argslice, int spaces, int newline){
    Buf out; buf_init(&out);
    Value *args = argslice.o?OBJ_CELLPTR(argslice.o):NULL;
    int64_t n = argslice.len;
    for (int64_t i=0;i<n;i++){
        FmtFlags f; memset(&f,0,sizeof f);
        Value v = args[argslice.off+i];
        if (i){
            if (spaces==1) buf_putc(&out,' ');
            else if (spaces==0){
                Value prev = args[argslice.off+i-1];
                int a = prev.k!=VK_STRING, bq = v.k!=VK_STRING;
                if (a && bq) buf_putc(&out,' ');
            }
        }
        format_value(&out,v,'v',&f,0);
    }
    if (newline) buf_putc(&out,'\n');
    Value r = val_string(out.p?out.p:"",out.len);
    buf_free(&out);
    return r;
}

/* ============================ native functions =========================== */

static Value arg(Value *a, int n, int i){ return i<n ? a[i] : val_nil(); }

static int n_sprintf(Value *a, int n, Value *r){ r[0] = go_sprintf(a[0],a[1]); return 1; }
static int n_sprint(Value *a, int n, Value *r){ r[0] = go_sprint(a[0],0,0); return 1; }
static int n_sprintln(Value *a, int n, Value *r){ r[0] = go_sprint(a[0],1,1); return 1; }

static int n_write(Value *a, int n, Value *r){
    int fd = (int)a[0].b.i;
    Value s = a[1];
    FILE *f = fd==2?stderr:stdout;
    if (s.k==VK_STRING){ fwrite(sdata(s),1,(size_t)s.len,f); }
    else if (s.k==VK_SLICE && s.o){
        Value *c = OBJ_CELLPTR(s.o);
        for (int64_t i=0;i<s.len;i++) fputc((int)(c[s.off+i].k==VK_UINT?c[s.off+i].b.u:c[s.off+i].b.i),f);
    }
    if (fd!=1) fflush(f);
    r[0] = val_int(s.len,ty_int);
    return 1;
}
static int n_exit(Value *a, int n, Value *r){ extern void go_program_exit(int); fflush(stdout); fflush(stderr); go_program_exit((int)a[0].b.i); return 0; }
static int n_getenv(Value *a, int n, Value *r){
    char *k = val_to_cstring(a[0]);
    const char *v = getenv(k);
    free(k);
    r[0] = v?mkstr0(v):mkstr("",0);
    r[1] = val_bool(v!=NULL);
    return 2;
}
extern Vec g_runargs;
static int n_osargs(Value *a, int n, Value *r){
    Value sl = val_nil(); sl.k=VK_SLICE; sl.t=type_slice(ty_string); sl.len=0; sl.cap=0;
    for (int i=0;i<g_runargs.len;i++){
        Value s = mkstr0((const char*)g_runargs.p[i]);
        sl = slice_append(sl,&s,1,sl.t);
    }
    r[0] = sl; return 1;
}
static int n_readfile(Value *a, int n, Value *r){
    char *p = val_to_cstring(a[0]);
    size_t len=0; char *d = read_file(p,&len);
    free(p);
    if (!d){ r[0] = val_nil(); r[0].k=VK_SLICE; r[0].t=type_slice(ty_uint8); r[1]=val_bool(0); return 2; }
    Obj *o = gc_alloc_cells((int)len);
    Value *c = OBJ_CELLPTR(o);
    for (size_t i=0;i<len;i++) c[i] = val_uint((unsigned char)d[i],ty_uint8);
    free(d);
    Value sl = val_nil(); sl.k=VK_SLICE; sl.o=o; sl.len=(int64_t)len; sl.cap=(int64_t)len; sl.t=type_slice(ty_uint8);
    r[0]=sl; r[1]=val_bool(1);
    return 2;
}

/* runtime */
static int n_gosched(Value *a, int n, Value *r){
    extern void vm_yield(void);
    vm_yield();
    return 0;
}
static int n_numgoroutine(Value *a, int n, Value *r){
    int c = 0;
    for (int i=0;i<g_allg.len;i++){ G *g=g_allg.p[i]; if (g->status!=G_DEAD) c++; }
    r[0]=val_int(c,ty_int); return 1;
}
static int n_gomaxprocs(Value *a, int n, Value *r){
    if (g_gomaxprocs == 0) g_gomaxprocs = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int old = g_gomaxprocs;
    if (n>0 && a[0].b.i>0) g_gomaxprocs = (int)a[0].b.i;
    r[0]=val_int(old,ty_int); return 1;
}
static int n_numcpu(Value *a, int n, Value *r){ r[0]=val_int(sysconf(_SC_NPROCESSORS_ONLN),ty_int); return 1; }
static int n_gc(Value *a, int n, Value *r){ gc_collect(1); gc_run_pending_finalizers(); gc_collect(1); gc_run_pending_finalizers(); return 0; }
static int n_setfinalizer(Value *a, int n, Value *r){
    if (a[0].k==VK_PTR && a[0].o) gc_set_finalizer(a[0].o,a[1],a[0].t);
    return 0;
}
static int n_keepalive(Value *a, int n, Value *r){ return 0; }
static int n_caller(Value *a, int n, Value *r){
    /* runtime.Caller(skip): frame 0 is the caller of Caller */
    int skip = (int)a[0].b.i;
    G *g = g_current;
    int idx = g->nframes - 2 - skip;
    if (idx < 0 || idx >= g->nframes){
        r[0]=val_uint(0,ty_uintptr); r[1]=mkstr("",0); r[2]=val_int(0,ty_int); r[3]=val_bool(0);
        return 4;
    }
    Frame *f = &g->frames[idx];
    int line = 0;
    if (f->fn && f->pc > 0 && f->pc <= f->fn->ncode) line = f->fn->code[f->pc-1].line;
    const char *file = f->fn && f->fn->srcfile ? f->fn->srcfile : "";
    r[0]=val_uint((uint64_t)(uintptr_t)f->fn,ty_uintptr);
    r[1]=mkstr0(file);
    r[2]=val_int(line,ty_int);
    r[3]=val_bool(1);
    return 4;
}
static int n_addcleanup(Value *a, int n, Value *r){
    if (a[0].k==VK_PTR && a[0].o) gc_add_cleanup(a[0].o,a[1],n>2?a[2]:val_nil());
    r[0] = val_zero(ty_int);
    return 1;
}
static int n_memstats(Value *a, int n, Value *r){
    /* returns HeapAlloc, TotalAlloc, Mallocs, Frees, NumGC, NumForcedGC, PauseTotalNs, Sys */
    r[0]=val_uint((uint64_t)gc_heap_bytes,ty_uint64);
    r[1]=val_uint(gc_total_alloc,ty_uint64);
    r[2]=val_uint(gc_mallocs,ty_uint64);
    r[3]=val_uint(gc_frees,ty_uint64);
    r[4]=val_uint(gc_num_gc,ty_uint32);
    r[5]=val_uint(gc_forced_gc,ty_uint32);
    r[6]=val_uint(gc_pause_total_ns,ty_uint64);
    r[7]=val_uint((uint64_t)gc_heap_bytes+(1u<<20),ty_uint64);
    return 8;
}
static int n_setgcpercent(Value *a, int n, Value *r){
    int64_t old = gc_gogc;
    gc_gogc = a[0].b.i;
    if (gc_gogc >= 0){ size_t t = gc_heap_bytes + gc_heap_bytes*(size_t)gc_gogc/100; gc_next_gc = t<(2u<<20)?(2u<<20):t; }
    else gc_next_gc = (size_t)-1;
    r[0]=val_int(old,ty_int); return 1;
}
static int n_setmemlimit(Value *a, int n, Value *r){
    int64_t old = gc_memlimit?gc_memlimit:INT64_MAX;
    if (a[0].b.i > 0 && a[0].b.i != INT64_MAX) gc_memlimit = a[0].b.i;
    else gc_memlimit = 0;
    r[0]=val_int(old,ty_int64); return 1;
}
static int n_freeosmem(Value *a, int n, Value *r){ gc_collect(1); return 0; }
static int n_nanotime(Value *a, int n, Value *r){ r[0]=val_int(vm_nanotime(),ty_int64); return 1; }
static int n_timenow(Value *a, int n, Value *r){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME,&ts);
    int64_t mono = vm_nanotime();
    extern int g_in_synctest; extern int64_t g_fake_now;
    if (g_in_synctest){ ts.tv_sec = 1136214245 + g_fake_now/1000000000; ts.tv_nsec = g_fake_now%1000000000; }
    r[0]=val_int((int64_t)ts.tv_sec,ty_int64);
    r[1]=val_int((int64_t)ts.tv_nsec,ty_int32);
    r[2]=val_int(mono,ty_int64);
    return 3;
}
static int n_sleep(Value *a, int n, Value *r){
    extern void vm_sleep_ns(int64_t d);
    vm_sleep_ns(a[0].b.i);
    return 0;
}
static int n_goexit(Value *a, int n, Value *r){
    extern void vm_goexit(void);
    vm_goexit();
    return 0;
}
static int n_panicnil(Value *a, int n, Value *r){ r[0]=val_bool(g_panic_nil_error); return 1; }
static int n_runbubble(Value *a, int n, Value *r){
    extern void vm_run_bubble(Value fn);
    vm_run_bubble(a[0]);
    return 0;
}
static int n_synctest_wait(Value *a, int n, Value *r){
    extern void vm_synctest_wait(void);
    vm_synctest_wait();
    return 0;
}
int g_test_exit_code = 0;
int g_test_no_tests = 0;
static int n_setexit(Value *a, int n, Value *r){ g_test_exit_code = (int)a[0].b.i; return 0; }
static int n_setnotests(Value *a, int n, Value *r){ g_test_no_tests = 1; return 0; }

/* testing.fuzzCall(fn any, t *T, args []any): call the fuzz body with the
 * sub-test and one seed entry spread across its parameters. `go test` without
 * -fuzz runs a target against its seed corpus only, so this is the whole of
 * the fuzzing execution model the milestone needs. */
static int n_fuzzcall(Value *a, int n, Value *r){
    Value fn = a[0], t = a[1], sl = a[2];
    Value argv[16];
    int na = 0;
    argv[na++] = t;
    if (sl.k==VK_SLICE && sl.o){
        Value *c = OBJ_CELLPTR(sl.o);
        for (int64_t i=0;i<sl.len && na<16;i++) argv[na++] = c[sl.off+i];
    }
    if (fn.k != VK_FUNC || !fn.o) vm_panic_str("testing: fuzz target is not a function");
    Value rets[4];
    vm_call_value(fn, argv, na, rets, 4);
    return 0;
}

/* math */
#define M1(nm,expr) static int nm(Value *a,int n,Value *r){ double x=a[0].b.d; r[0]=val_float(expr,ty_float64); return 1; }
#define M2(nm,expr) static int nm(Value *a,int n,Value *r){ double x=a[0].b.d,y=a[1].b.d; r[0]=val_float(expr,ty_float64); return 1; }
M1(n_sqrt, sqrt(x)) M1(n_floor, floor(x)) M1(n_ceil, ceil(x)) M1(n_trunc, trunc(x))
M1(n_log, log(x)) M1(n_log2, log2(x)) M1(n_log10, log10(x)) M1(n_exp, exp(x))
M1(n_sin, sin(x)) M1(n_cos, cos(x)) M1(n_tan, tan(x)) M1(n_asin, asin(x)) M1(n_acos, acos(x))
M1(n_atan, atan(x)) M1(n_cbrt, cbrt(x)) M1(n_round, round(x)) M1(n_absf, fabs(x))
M2(n_pow, pow(x,y)) M2(n_atan2, atan2(x,y)) M2(n_modf2, fmod(x,y)) M2(n_hypot, hypot(x,y))
static int n_isnan(Value *a,int n,Value *r){ r[0]=val_bool(isnan(a[0].b.d)); return 1; }
static int n_isinf(Value *a,int n,Value *r){ int s=(int)a[1].b.i; double x=a[0].b.d; r[0]=val_bool(s>=0&&isinf(x)&&x>0?1:(s<=0&&isinf(x)&&x<0?1:0)); return 1; }
static int n_inf(Value *a,int n,Value *r){ r[0]=val_float(a[0].b.i>=0?INFINITY:-INFINITY,ty_float64); return 1; }
static int n_nan(Value *a,int n,Value *r){ r[0]=val_float(NAN,ty_float64); return 1; }
static int n_ldexp(Value *a,int n,Value *r){ r[0]=val_float(ldexp(a[0].b.d,(int)a[1].b.i),ty_float64); return 1; }
static int n_frexp(Value *a,int n,Value *r){ int e; double m=frexp(a[0].b.d,&e); r[0]=val_float(m,ty_float64); r[1]=val_int(e,ty_int); return 2; }
static int n_float64bits(Value *a,int n,Value *r){ uint64_t u; double d=a[0].b.d; memcpy(&u,&d,8); r[0]=val_uint(u,ty_uint64); return 1; }
static int n_float64frombits(Value *a,int n,Value *r){ double d; uint64_t u=a[0].b.u; memcpy(&d,&u,8); r[0]=val_float(d,ty_float64); return 1; }
static int n_float32bits(Value *a,int n,Value *r){ uint32_t u; float d=(float)a[0].b.d; memcpy(&u,&d,4); r[0]=val_uint(u,ty_uint32); return 1; }
static int n_float32frombits(Value *a,int n,Value *r){ float d; uint32_t u=(uint32_t)a[0].b.u; memcpy(&d,&u,4); r[0]=val_float((double)d,ty_float32); return 1; }

/* strconv */
static int n_formatfloat(Value *a,int n,Value *r){
    Buf b; buf_init(&b);
    go_format_float(&b,a[0].b.d,(char)a[1].b.i,(int)a[2].b.i,(int)a[3].b.i);
    r[0]=val_string(b.p?b.p:"",b.len);
    buf_free(&b);
    return 1;
}
static int n_parsefloat(Value *a,int n,Value *r){
    char *s = val_to_cstring(a[0]);
    char *end = NULL;
    double d = strtod(s,&end);
    int ok = (end && *end==0 && end!=s);
    free(s);
    r[0]=val_float(d,ty_float64); r[1]=val_bool(ok);
    return 2;
}

/* atomic (the VM multiplexes goroutines on one OS thread, so the primitive
 * operations are already indivisible; the natives only have to respect the
 * width of the addressed variable) */
static Value *atomic_slot(Value v){
    if (v.k != VK_PTR || !v.o) vm_panic_str("runtime error: invalid memory address or nil pointer dereference");
    return &OBJ_CELLPTR(v.o)[v.off];
}
static Value atomic_store_typed(Value *p, int64_t x){
    Type *t = p->t ? p->t : ty_int64;
    Value nv = (p->k==VK_UINT) ? val_uint((uint64_t)x,t) : val_int(x,t);
    extern Value vm_convert(Value, Type *);
    nv = vm_convert(nv,t);
    *p = nv;
    return nv;
}
static int n_atomic_add(Value *a,int n,Value *r){
    Value *p = atomic_slot(a[0]);
    int64_t cur = (p->k==VK_UINT) ? (int64_t)p->b.u : p->b.i;
    Value nv = atomic_store_typed(p, cur + a[1].b.i);
    r[0]=val_int((nv.k==VK_UINT)?(int64_t)nv.b.u:nv.b.i,ty_int64); return 1;
}
static int n_atomic_load(Value *a,int n,Value *r){
    Value *p = atomic_slot(a[0]);
    r[0]=val_int((p->k==VK_UINT)?(int64_t)p->b.u:p->b.i,ty_int64); return 1;
}
static int n_atomic_store(Value *a,int n,Value *r){ atomic_store_typed(atomic_slot(a[0]),a[1].b.i); return 0; }
static int n_atomic_cas(Value *a,int n,Value *r){
    Value *p = atomic_slot(a[0]);
    int64_t cur = (p->k==VK_UINT)?(int64_t)p->b.u:p->b.i;
    if (cur == a[1].b.i){ atomic_store_typed(p,a[2].b.i); r[0]=val_bool(1); }
    else r[0]=val_bool(0);
    return 1;
}
static int n_atomic_swap(Value *a,int n,Value *r){
    Value *p = atomic_slot(a[0]);
    int64_t cur = (p->k==VK_UINT)?(int64_t)p->b.u:p->b.i;
    atomic_store_typed(p,a[1].b.i);
    r[0]=val_int(cur,ty_int64); return 1;
}
static int n_atomic_loadany(Value *a,int n,Value *r){ r[0]=*atomic_slot(a[0]); return 1; }
static int n_atomic_storeany(Value *a,int n,Value *r){ *atomic_slot(a[0]) = a[1]; return 0; }
static int n_atomic_casany(Value *a,int n,Value *r){
    Value *p = atomic_slot(a[0]);
    if (val_equal(*p,a[1])){ *p = a[2]; r[0]=val_bool(1); } else r[0]=val_bool(0);
    return 1;
}

/* misc runtime support for the standard library */
static int n_stringtobytes(Value *a,int n,Value *r){ r[0]=vm_convert(a[0],type_slice(ty_uint8)); return 1; }
static int n_hashvalue(Value *a,int n,Value *r){ r[0]=val_uint(val_hash(a[0]),ty_uint64); return 1; }
static int n_valueequal(Value *a,int n,Value *r){ r[0]=val_bool(val_equal(a[0],a[1])); return 1; }
static int n_typename(Value *a,int n,Value *r){ r[0]=mkstr0(a[0].t?type_string(a[0].t):"<nil>"); return 1; }
static int n_aserr(Value *a,int n,Value *r){
    Value err = a[0], tgt = a[1];
    if (tgt.k != VK_PTR || !tgt.o){ r[0]=val_bool(0); return 1; }
    Value *slot = &OBJ_CELLPTR(tgt.o)[tgt.off];
    Type *want = tgt.t && underlying(tgt.t)->k==T_PTR ? underlying(tgt.t)->elem : NULL;
    if (!want){ r[0]=val_bool(0); return 1; }
    if (err.k==VK_NIL && !err.t){ r[0]=val_bool(0); return 1; }
    int ok = 0;
    if (is_iface(want)) ok = err.t && iface_satisfied(err.t,want,NULL);
    else ok = err.t && type_identical(err.t,want);
    if (ok){ *slot = err; r[0]=val_bool(1); }
    else r[0]=val_bool(0);
    return 1;
}
static int n_slicelen(Value *a,int n,Value *r){
    Value s = a[0];
    if (s.k!=VK_SLICE) vm_panic_str("sort: argument is not a slice");
    r[0]=val_int(s.len,ty_int); return 1;
}
static int n_sliceswap(Value *a,int n,Value *r){
    Value s = a[0];
    int64_t i = a[1].b.i, j = a[2].b.i;
    if (s.k!=VK_SLICE || !s.o) vm_panic_str("sort: argument is not a slice");
    if (i<0||j<0||i>=s.len||j>=s.len) vm_panic_str("runtime error: index out of range [%lld] with length %lld",(long long)(i>=s.len?i:j),(long long)s.len);
    Value *c = OBJ_CELLPTR(s.o);
    Value t = c[s.off+i]; c[s.off+i]=c[s.off+j]; c[s.off+j]=t;
    return 0;
}
static int n_isnilv(Value *a,int n,Value *r){ Value v=a[0]; r[0]=val_bool(v.k==VK_NIL || ((v.k==VK_PTR||v.k==VK_SLICE||v.k==VK_MAP||v.k==VK_CHAN||v.k==VK_FUNC)&&!v.o)); return 1; }

/* unsafe.* operations: the pointer model is (object, cell index), so byte
 * offsets are interpreted against the element size of the pointee. */
enum { U_ADD=33, U_SLICE, U_STRING, U_STRINGDATA, U_SLICEDATA };

int unsafe_op(int op, Value *args, int nargs, Type *t, Value *out){
    switch (op){
    case U_ADD: {
        Value p = args[0];
        int64_t delta = args[1].k==VK_UINT ? (int64_t)args[1].b.u : args[1].b.i;
        Type *pu = p.t ? underlying(p.t) : NULL;
        int64_t esz = (pu && pu->k==T_PTR) ? type_size(pu->elem) : (p.len > 0 ? p.len : 1);
        if (esz <= 0) esz = 1;
        if (delta % esz) vm_panic_str("unsafe.Add: offset %lld is not a multiple of the element size %lld",(long long)delta,(long long)esz);
        Value r = p;
        r.off = (uint32_t)((int64_t)p.off + delta/esz);
        r.t = ty_unsafeptr;
        r.len = esz;
        *out = r;
        return 1;
    }
    case U_SLICE: {
        Value p = args[0];
        int64_t n = args[1].k==VK_UINT ? (int64_t)args[1].b.u : args[1].b.i;
        if (n < 0) vm_panic_str("unsafe.Slice: len out of range");
        if (!p.o && n > 0) vm_panic_str("unsafe.Slice: ptr is nil and len is not zero");
        Value r = val_nil();
        r.k = VK_SLICE; r.o = p.o; r.off = p.off; r.len = n; r.cap = n; r.t = t;
        *out = r;
        return 1;
    }
    case U_STRING: {
        Value p = args[0];
        int64_t n = args[1].k==VK_UINT ? (int64_t)args[1].b.u : args[1].b.i;
        Buf b; buf_init(&b);
        if (p.o){
            Value *c = OBJ_CELLPTR(p.o);
            for (int64_t i=0;i<n;i++) buf_putc(&b,(int)(c[p.off+i].k==VK_UINT?c[p.off+i].b.u:c[p.off+i].b.i));
        }
        *out = val_string(b.p?b.p:"",b.len);
        buf_free(&b);
        return 1;
    }
    case U_STRINGDATA: {
        Value s = args[0];
        Obj *o = gc_alloc_cells((int)(s.len?s.len:1));
        Value *c = OBJ_CELLPTR(o);
        const char *d = s.o ? OBJ_BYTEPTR(s.o)+s.off : "";
        for (int64_t i=0;i<s.len;i++) c[i] = val_uint((unsigned char)d[i],ty_uint8);
        Value r = val_nil(); r.k=VK_PTR; r.o=o; r.off=0; r.t=type_ptr(ty_uint8);
        *out = r;
        return 1;
    }
    case U_SLICEDATA: {
        Value s = args[0];
        Value r = val_nil(); r.k=VK_PTR; r.o=s.o; r.off=s.off; r.t=t;
        *out = r;
        return 1;
    }
    }
    *out = val_nil();
    return 0;
}

typedef struct NativeEnt { const char *pkg, *name; NativeFn fn; } NativeEnt;

static NativeEnt native_tab[] = {
    {"fmt","sprintf",n_sprintf}, {"fmt","sprint",n_sprint}, {"fmt","sprintln",n_sprintln},
    {"os","write",n_write}, {"os","exitProcess",n_exit}, {"os","getenv",n_getenv},
    {"os","args",n_osargs}, {"os","readFile",n_readfile},
    {"runtime","Gosched",n_gosched}, {"runtime","NumGoroutine",n_numgoroutine},
    {"runtime","GOMAXPROCS",n_gomaxprocs}, {"runtime","NumCPU",n_numcpu},
    {"runtime","GC",n_gc}, {"runtime","SetFinalizer",n_setfinalizer},
    {"runtime","KeepAlive",n_keepalive},
    {"runtime","callerInfo",n_caller}, {"runtime","addCleanup",n_addcleanup},
    {"runtime","readMemStats",n_memstats}, {"runtime","nanotime",n_nanotime},
    {"runtime","sleepNanos",n_sleep}, {"runtime","Goexit",n_goexit},
    {"runtime","panicNilEnabled",n_panicnil},
    {"testing","SetExitCode",n_setexit}, {"testing","fuzzCall",n_fuzzcall},
    {"testing","SetNoTestsToRun",n_setnotests},
    {"testing/synctest","runBubble",n_runbubble},
    {"testing/synctest","wait",n_synctest_wait},
    {"runtime","hashValue",n_hashvalue}, {"runtime","valueEqual",n_valueequal},
    {"runtime","typeName",n_typename}, {"runtime","isNil",n_isnilv},
    {"errors","asAssign",n_aserr},
    {"sort","sliceLen",n_slicelen}, {"sort","sliceSwap",n_sliceswap},
    {"runtime/debug","setGCPercent",n_setgcpercent},
    {"runtime/debug","setMemoryLimit",n_setmemlimit},
    {"runtime/debug","freeOSMemory",n_freeosmem},
    {"math","Sqrt",n_sqrt},{"math","Floor",n_floor},{"math","Ceil",n_ceil},{"math","Trunc",n_trunc},
    {"math","Log",n_log},{"math","Log2",n_log2},{"math","Log10",n_log10},{"math","Exp",n_exp},
    {"math","Sin",n_sin},{"math","Cos",n_cos},{"math","Tan",n_tan},{"math","Asin",n_asin},
    {"math","Acos",n_acos},{"math","Atan",n_atan},{"math","Cbrt",n_cbrt},{"math","Round",n_round},
    {"math","Abs",n_absf},{"math","Pow",n_pow},{"math","Atan2",n_atan2},{"math","Mod",n_modf2},
    {"math","Hypot",n_hypot},{"math","IsNaN",n_isnan},{"math","isInf",n_isinf},
    {"math","inf",n_inf},{"math","NaN",n_nan},{"math","Ldexp",n_ldexp},{"math","Frexp",n_frexp},
    {"math","Float64bits",n_float64bits},{"math","Float64frombits",n_float64frombits},
    {"math","Float32bits",n_float32bits},{"math","Float32frombits",n_float32frombits},
    {"strconv","formatFloat",n_formatfloat}, {"strconv","parseFloat",n_parsefloat},
    {"time","sleepNanos",n_sleep},
    {"time","now",n_timenow},
    {"sync/atomic","addInt",n_atomic_add}, {"sync/atomic","loadInt",n_atomic_load},
    {"sync/atomic","storeInt",n_atomic_store}, {"sync/atomic","casInt",n_atomic_cas},
    {"sync/atomic","swapInt",n_atomic_swap},
    {"sync/atomic","loadAny",n_atomic_loadany}, {"sync/atomic","storeAny",n_atomic_storeany},
    {"sync/atomic","casAny",n_atomic_casany},
    {NULL,NULL,NULL}
};

Vec g_native_names;

int native_lookup(const char *pkg, const char *name){
    for (int i=0; native_tab[i].pkg; i++){
        if (strcmp(native_tab[i].pkg,pkg)==0 && strcmp(native_tab[i].name,name)==0) return i+1;
    }
    return 0;
}
int native_call(int id, Value *args, int nargs, Value *rets){
    if (id<=0) return 0;
    return native_tab[id-1].fn(args,nargs,rets);
}
void natives_init(void){}
