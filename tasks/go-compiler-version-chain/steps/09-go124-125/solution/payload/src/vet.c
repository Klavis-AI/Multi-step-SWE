#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>

/* `go vet`: a small set of the analyzers cmd/vet runs, reported in the same
 * file:line:col form the go command uses. */

extern void discover_module_pub(const char *dir);
extern const char *main_module_path(void);
extern const char *main_module_dir(void);
extern int lang_for_build_pub(void);
extern int g_build_test;

typedef struct VetFinding { const char *file; int line, col; const char *msg; } VetFinding;
static Vec g_findings;

static void vet_report(Pos pos, const char *msg){
    VetFinding *f = xcalloc(1,sizeof *f);
    f->file = pos.file; f->line = pos.line; f->col = pos.col; f->msg = intern(msg);
    vec_push(&g_findings,f);
}

static int fcmp(const void *a, const void *b){
    const VetFinding *x = *(const VetFinding *const *)a, *y = *(const VetFinding *const *)b;
    int c = strcmp(x->file?x->file:"", y->file?y->file:"");
    if (c) return c;
    if (x->line != y->line) return x->line - y->line;
    return x->col - y->col;
}

/* ------------------------------------------------------------- printf --- */

static int verb_count(const char *fmt, size_t len){
    int n = 0;
    for (size_t i=0;i<len;i++){
        if (fmt[i] != '%') continue;
        i++;
        while (i<len && (fmt[i]=='-'||fmt[i]=='+'||fmt[i]=='#'||fmt[i]==' '||fmt[i]=='0')) i++;
        while (i<len && isdigit((unsigned char)fmt[i])) i++;
        if (i<len && fmt[i]=='.'){ i++; while (i<len && isdigit((unsigned char)fmt[i])) i++; }
        if (i>=len) break;
        if (fmt[i]=='%') continue;
        n++;
    }
    return n;
}

static const char *printf_like(Node *call, int *fmtidx){
    if (call->kind != N_CALL) return NULL;
    Node *f = call->x;
    while (f && f->kind==N_PAREN) f = f->x;
    if (!f || f->kind != N_SELECTOR) return NULL;
    if (!f->x || f->x->kind != N_IDENT) return NULL;
    if (strcmp(f->x->name,"fmt")!=0) return NULL;
    const char *m = f->name;
    if (strcmp(m,"Printf")==0 || strcmp(m,"Sprintf")==0 || strcmp(m,"Errorf")==0){ *fmtidx = 0; return m; }
    if (strcmp(m,"Fprintf")==0){ *fmtidx = 1; return m; }
    return NULL;
}

static void vet_walk(Node *n, Package *p){
    if (!n) return;
    if (n->kind==N_CALL){
        int fi = 0;
        const char *m = printf_like(n,&fi);
        if (m && n->list.len > fi){
            Node *fmtn = n->list.p[fi];
            if (fmtn->kind==N_BASICLIT && fmtn->op==TOK_STRING && !n->flag){
                int want = verb_count(fmtn->name,fmtn->namelen);
                int have = n->list.len - fi - 1;
                if (have > want)
                    vet_report(n->pos, xsprintf("fmt.%s call needs %d arg%s but has %d args",
                                                m, want, want==1?"":"s", have));
                else if (have < want)
                    vet_report(n->pos, xsprintf("fmt.%s format reads arg #%d, but call has %d arg%s",
                                                m, want, have, have==1?"":"s"));
            }
        }
    }
    /* waitgroup: wg.Add(...) as the first statement inside a `go func(){...}()` */
    if (n->kind==N_GO && n->x && n->x->kind==N_CALL){
        Node *fn = n->x->x;
        while (fn && fn->kind==N_PAREN) fn = fn->x;
        if (fn && fn->kind==N_FUNCLIT && fn->y){
            Node *body = fn->y;
            for (int i=0;i<body->list.len;i++){
                Node *st = body->list.p[i];
                if (st->kind!=N_EXPRSTMT || !st->x || st->x->kind!=N_CALL) continue;
                Node *c = st->x->x;
                while (c && c->kind==N_PAREN) c = c->x;
                if (c && c->kind==N_SELECTOR && strcmp(c->name,"Add")==0)
                    vet_report(st->pos,"WaitGroup.Add called from inside new goroutine");
            }
        }
    }
    vet_walk(n->x,p); vet_walk(n->y,p); vet_walk(n->z,p); vet_walk(n->w,p);
    for (int i=0;i<n->list.len;i++) vet_walk(n->list.p[i],p);
    for (int i=0;i<n->list2.len;i++) vet_walk(n->list2.p[i],p);
}

/* tests: malformed Test function signatures in _test.go files */
static void vet_tests(Package *p){
    for (int i=0;i<p->files.len;i++){
        Node *file = p->files.p[i];
        const char *fn = file->pos.file;
        size_t l = fn?strlen(fn):0;
        if (l < 9 || strcmp(fn+l-8,"_test.go")!=0) continue;
        for (int j=0;j<file->list.len;j++){
            Node *d = file->list.p[j];
            if (d->kind!=N_FUNCDECL || d->x) continue;
            if (!d->name || strncmp(d->name,"Test",4)!=0) continue;
            if (d->name[4] && islower((unsigned char)d->name[4])) continue;
            Node *sig = d->y;
            int nparams = sig && sig->x ? sig->x->list.len : 0;
            int nres = sig && sig->y ? sig->y->list.len : 0;
            if (nparams != 1 || nres != 0)
                vet_report(d->pos, xsprintf("wrong signature for %s, must be: func %s(t *testing.T)",
                                            d->name, d->name));
        }
    }
}

int vet_main(int argc, char **argv){
    const char *target = ".";
    for (int i=0;i<argc;i++) if (argv[i][0] != '-') target = argv[i];
    discover_module_pub(".");
    int lang = lang_for_build_pub();
    const char *modpath = main_module_path();
    const char *moddir = main_module_dir();
    if (!modpath){ fprintf(stderr,"go: go.mod file not found in current directory or any parent directory; see 'go help modules'\n"); return 1; }
    Vec dirs; memset(&dirs,0,sizeof dirs);
    if (strcmp(target,"./...")==0 || strcmp(target,"...")==0){
        Vec stack; memset(&stack,0,sizeof stack);
        vec_push(&stack,(void*)moddir);
        while (stack.len){
            char *d = (char*)stack.p[--stack.len];
            Vec names = list_dir(d);
            int has_go = 0;
            for (int i=0;i<names.len;i++){
                const char *nm = names.p[i];
                char *full = path_join(d,nm);
                if (is_dir(full)){ if (nm[0]!='.' && strcmp(nm,"vendor")!=0) vec_push(&stack,full); }
                else { size_t l=strlen(nm); if (l>3 && strcmp(nm+l-3,".go")==0) has_go = 1; }
            }
            if (has_go) vec_push(&dirs,d);
        }
    } else vec_push(&dirs,(void*)(strcmp(target,".")==0?xstrdup("."):xstrdup(target)));

    for (int di=0; di<dirs.len; di++){
        const char *dir = dirs.p[di];
        char abs[4096], modabs[4096];
        if (!realpath(dir,abs)) snprintf(abs,sizeof abs,"%s",dir);
        if (!realpath(moddir,modabs)) snprintf(modabs,sizeof modabs,"%s",moddir);
        size_t ml = strlen(modabs);
        const char *ip = modpath;
        if (strncmp(abs,modabs,ml)==0 && (abs[ml]==0||abs[ml]=='/'))
            ip = abs[ml] ? intern(xsprintf("%s/%s",modpath,abs+ml+1)) : modpath;
        g_build_test = 1;
        Package *p = load_package(ip,moddir,lang);
        g_build_test = 0;
        if (!p) continue;
        for (int i=0;i<p->files.len;i++) vet_walk(p->files.p[i],p);
        vet_tests(p);
    }
    if (!g_findings.len) return 0;
    qsort(g_findings.p,(size_t)g_findings.len,sizeof(void*),fcmp);
    fprintf(stderr,"# %s\n",modpath);
    for (int i=0;i<g_findings.len;i++){
        VetFinding *f = g_findings.p[i];
        fprintf(stderr,"%s:%d:%d: %s\n",f->file,f->line,f->col,f->msg);
    }
    return 1;
}

int mod_main(int argc, char **argv){ (void)argc; (void)argv; return 0; }
