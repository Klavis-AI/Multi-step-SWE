#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

const char *g_goroot = NULL;
const char *g_gopath = NULL;
const char *g_gomodcache = NULL;
const char *g_goproxy = NULL;
const char *g_go111module = NULL;
const char *TOOLCHAIN_VERSION = "go1.25.1";
int TOOLCHAIN_MINOR = 25;
Vec g_runargs;
int g_run_mode = 0;

extern void vm_start_goroutine_main(Value fn);
extern void vm_sched(void);
extern Value make_closure(FuncDef *fn, Obj *env);
extern void gc_pin_obj(Obj *o);
extern int g_panic_nil_error;
extern Type *ty_runtime_error, *ty_panicnil;

typedef struct Module {
    const char *path;
    const char *dir;
    int lang;             /* go directive minor version */
    const char *toolchain;
    Vec requires;         /* of Require* */
    Vec replaces;
} Module;

static Module g_mainmod;
static int g_have_module = 0;

/* ----------------------------------------------------------- go.mod --- */

typedef struct Require { const char *path; const char *version; const char *fromver; const char *base; int indirect; } Require;
static void parse_gomod(Module *m, const char *path);

static int parse_goversion(const char *s){
    /* "1.21" or "1.21.3" or "go1.21" */
    if (strncmp(s,"go",2)==0) s += 2;
    if (s[0]!='1' || s[1]!='.') return 0;
    return atoi(s+2);
}

void parse_gomod_impl(Module *m, const char *path);
static void parse_gomod(Module *m, const char *path){
    size_t len;
    char *src = read_file(path,&len);
    if (!src) return;
    m->dir = path_dir(path);
    char *line = src;
    int inblock = 0;
    const char *blockkind = NULL;
    while (line && *line){
        char *nl = strchr(line,'\n');
        if (nl) *nl = 0;
        char *s = line;
        while (*s==' '||*s=='\t') s++;
        char *cmt = strstr(s,"//");
        int indirect = cmt && strstr(cmt,"indirect")!=NULL;
        if (cmt) *cmt = 0;
        char *e = s+strlen(s);
        while (e>s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r')) *--e = 0;
        if (*s){
            if (inblock && strcmp(s,")")==0){ inblock = 0; }
            else if (inblock){
                char w1[512]={0}, w2[512]={0}, w3[512]={0}, w4[512]={0};
                int nw = sscanf(s,"%511s %511s %511s %511s",w1,w2,w3,w4);
                if (strcmp(blockkind,"require")==0 && nw>=2){
                    Require *r = xcalloc(1,sizeof *r);
                    r->path = intern(w1); r->version = intern(w2); r->indirect = indirect;
                    vec_push(&m->requires,r);
                } else if (strcmp(blockkind,"replace")==0 && nw>=3){
                    Require *r = xcalloc(1,sizeof *r);
                    if (strcmp(w2,"=>")==0){ r->path=intern(w1); r->fromver=NULL; r->version=intern(w3); }
                    else if (nw>=4){ r->path=intern(w1); r->fromver=intern(w2); r->version=intern(w4); }
                    r->base = m->dir;
                    if (r->path) vec_push(&m->replaces,r);
                }
            } else {
                char kw[64]={0}, rest[900]={0};
                int nw = sscanf(s,"%63s %899[^\n]",kw,rest);
                if (strcmp(kw,"module")==0 && nw>=2) m->path = intern(rest);
                else if (strcmp(kw,"go")==0 && nw>=2) m->lang = parse_goversion(rest);
                else if (strcmp(kw,"toolchain")==0 && nw>=2) m->toolchain = intern(rest);
                else if ((strcmp(kw,"require")==0||strcmp(kw,"replace")==0||strcmp(kw,"exclude")==0)){
                    if (nw>=2 && rest[0]=='('){ inblock=1; blockkind = strcmp(kw,"require")==0?"require":(strcmp(kw,"replace")==0?"replace":"exclude"); }
                    else if (nw>=2 && strcmp(kw,"require")==0){
                        char w1[512]={0}, w2[512]={0};
                        if (sscanf(rest,"%511s %511s",w1,w2)>=2){
                            Require *r = xcalloc(1,sizeof *r); r->path=intern(w1); r->version=intern(w2); r->indirect=indirect;
                            vec_push(&m->requires,r);
                        }
                    } else if (nw>=2 && strcmp(kw,"replace")==0){
                        char w1[512]={0}, w2[512]={0}, w3[512]={0}, w4[512]={0};
                        int k = sscanf(rest,"%511s %511s %511s %511s",w1,w2,w3,w4);
                        if (k>=3){
                            Require *r=xcalloc(1,sizeof *r); r->path=intern(w1);
                            if (strcmp(w2,"=>")==0) r->version = intern(w3);
                            else { r->fromver = intern(w2); r->version = intern(w4); }
                            r->base = m->dir;
                            vec_push(&m->replaces,r);
                        }
                    }
                }
            }
        }
        if (!nl) break;
        line = nl+1;
    }
    free(src);
}

static char *find_gomod(const char *start){
    char *dir = xstrdup(start);
    for (;;){
        char *p = path_join(dir,"go.mod");
        if (file_exists(p)){ free(dir); return p; }
        free(p);
        char *up = path_dir(dir);
        if (strcmp(up,dir)==0){ free(up); free(dir); return NULL; }
        free(dir); dir = up;
    }
}

/* -------------------------------------------------- package loading --- */

int g_build_test = 0;

static int has_suffix(const char *s, const char *suf){
    size_t a=strlen(s), b=strlen(suf);
    return a>=b && strcmp(s+a-b,suf)==0;
}

static int build_constraints_ok(const char *src){
    /* honour //go:build lines for the tags we set */
    const char *p = src;
    for (int i=0;i<60 && p && *p;i++){
        const char *nl = strchr(p,'\n');
        size_t len = nl ? (size_t)(nl-p) : strlen(p);
        if (len>10 && strncmp(p,"//go:build",10)==0){
            char buf[512]; size_t n = len-10<511?len-10:511;
            memcpy(buf,p+10,n); buf[n]=0;
            if (strstr(buf,"ignore")) return 0;
            return 1;
        }
        if (len>0 && strncmp(p,"package ",8)==0) break;
        if (!nl) break;
        p = nl+1;
    }
    return 1;
}

static Package *new_package(const char *path, const char *name, const char *dir, int lang){
    Package *p = xcalloc(1,sizeof *p);
    p->path = intern(path); p->name = name?intern(name):NULL; p->dir = dir?intern(dir):NULL;
    p->lang = lang;
    vec_push(&g_packages,p);
    return p;
}

static const char *module_dir_for(const char *importpath);

Package *new_error_package(const char *path){
    Package *p = xcalloc(1,sizeof *p);
    p->path = intern(path); p->name = intern(path_base((char*)path));
    p->checked = 1; p->inited = -1;
    return p;
}

Package *load_package(const char *importpath, const char *fromdir, int lang){
    importpath = intern(importpath);
    Package *p = pkg_lookup(importpath);
    if (p) return p;
    if (strcmp(importpath,"unsafe")==0){
        p = new_package("unsafe","unsafe",NULL,lang);
        p->checked = 1;
        extern void unsafe_pkg_init(Package *p);
        unsafe_pkg_init(p);
        return p;
    }
    const char *dir = NULL;
    char *stddir = path_join(path_join((char*)g_goroot,"src"),importpath);
    if (is_dir(stddir)) dir = stddir;
    if (!dir) dir = module_dir_for(importpath);
    if (!dir) return NULL;
    Vec names = list_dir(dir);
    Vec files; memset(&files,0,sizeof files);
    const char *pkgname = NULL;
    int plang = lang;
    /* std packages compile at the toolchain's own language version */
    if (strncmp(dir,g_goroot,strlen(g_goroot))==0) plang = TOOLCHAIN_MINOR;
    p = new_package(importpath,NULL,dir,plang);
    for (int i=0;i<names.len;i++){
        const char *nm = names.p[i];
        if (!has_suffix(nm,".go")) continue;
        if (has_suffix(nm,"_test.go") && !g_build_test) continue;
        if (nm[0]=='_' || nm[0]=='.') continue;
        char *full = path_join(dir,nm);
        size_t len; char *src = read_file(full,&len);
        if (!src) continue;
        if (!build_constraints_ok(src)){ free(src); continue; }
        Node *f = parse_file(full,src,len,plang);
        if (!pkgname) pkgname = f->name;
        vec_push(&p->files,f);
    }
    p->name = pkgname?pkgname:path_base((char*)importpath);
    check_package(p);
    return p;
}

/* ------------------------------------------------ module graph + MVS --- */

typedef struct ModNode { const char *path; const char *version; const char *dir; } ModNode;
static Vec g_modgraph;   /* of ModNode*, one per selected module path */

static int semver_cmp(const char *a, const char *b){
    /* compare vMAJOR.MINOR.PATCH; missing components are zero */
    if (!a) return b?-1:0;
    if (!b) return 1;
    if (*a=='v') a++;
    if (*b=='v') b++;
    for (int i=0;i<3;i++){
        long x = strtol(a,(char**)&a,10);
        long y = strtol(b,(char**)&b,10);
        if (x != y) return x<y?-1:1;
        if (*a=='.') a++;
        if (*b=='.') b++;
    }
    return strcmp(a,b);
}

static ModNode *modgraph_find(const char *path){
    for (int i=0;i<g_modgraph.len;i++){
        ModNode *m = g_modgraph.p[i];
        if (m->path == path) return m;
    }
    return NULL;
}

/* A `replace` directive whose target starts with ./ or ../ or / names a
 * directory; that is the only source of module code this toolchain uses. */
static const char *replacement_dir(Module *m, const char *path, const char *version, const char **base){
    Require *wildcard = NULL;
    for (int i=0;i<m->replaces.len;i++){
        Require *r = m->replaces.p[i];
        if (strcmp(r->path,path)!=0) continue;
        if (!r->fromver){ if (!wildcard) wildcard = r; continue; }
        if (version && strcmp(r->fromver,version)==0){ if (base) *base = r->base; return r->version; }
    }
    if (wildcard){ if (base) *base = wildcard->base; return wildcard->version; }
    return NULL;
}

static void modgraph_add(Module *from, const char *path, const char *version);

static void modgraph_load(Module *from, const char *path, const char *version, const char *dir){
    ModNode *ex = modgraph_find(path);
    if (ex){
        if (semver_cmp(version, ex->version) <= 0) return;   /* MVS: keep the max */
        ex->version = version; ex->dir = dir;
    } else {
        ModNode *n = xcalloc(1,sizeof *n);
        n->path = path; n->version = version; n->dir = dir;
        vec_push(&g_modgraph,n);
    }
    char *gm = path_join((char*)dir,"go.mod");
    if (!file_exists(gm)) return;
    Module sub; memset(&sub,0,sizeof sub);
    parse_gomod(&sub,gm);
    for (int i=0;i<sub.requires.len;i++){
        Require *r = sub.requires.p[i];
        modgraph_add(&sub,r->path,r->version);
    }
}

static void modgraph_add(Module *from, const char *path, const char *version){
    const char *base = NULL;
    const char *dir = replacement_dir(from,path,version,&base);
    if (!dir){ base = NULL; dir = replacement_dir(&g_mainmod,path,version,&base); }
    if (!dir) return;                   /* unresolved: reported at import time */
    if (!base) base = g_mainmod.dir;
    char *abs = (dir[0]=='/') ? xstrdup(dir) : path_join((char*)base,dir);
    modgraph_load(from,intern(path),intern(version?version:"v0.0.0"),intern(abs));
}

static void build_module_graph(void){
    g_modgraph.len = 0;
    if (!g_have_module) return;
    for (int i=0;i<g_mainmod.requires.len;i++){
        Require *r = g_mainmod.requires.p[i];
        modgraph_add(&g_mainmod,r->path,r->version);
    }
}

/* module resolution (single-module + module graph + vendor) */
static Vec g_modroots;   /* of char* "path\tdir" */

static const char *module_dir_for(const char *importpath){
    if (g_have_module && g_mainmod.path){
        size_t n = strlen(g_mainmod.path);
        if (strncmp(importpath,g_mainmod.path,n)==0 && (importpath[n]==0 || importpath[n]=='/')){
            char *d = importpath[n] ? path_join((char*)g_mainmod.dir,importpath+n+1) : xstrdup(g_mainmod.dir);
            if (is_dir(d)) return d;
        }
        /* vendor directory */
        char *v = path_join(path_join((char*)g_mainmod.dir,"vendor"),importpath);
        if (is_dir(v)) return v;
    }
    for (int i=0;i<g_modgraph.len;i++){
        ModNode *m = g_modgraph.p[i];
        size_t n = strlen(m->path);
        if (strncmp(importpath,m->path,n)==0 && (importpath[n]==0||importpath[n]=='/')){
            char *d = importpath[n] ? path_join((char*)m->dir,importpath+n+1) : xstrdup(m->dir);
            if (is_dir(d)) return d;
        }
    }
    for (int i=0;i<g_modroots.len;i++){
        char *ent = g_modroots.p[i];
        char *tab = strchr(ent,'\t');
        if (!tab) continue;
        size_t n = (size_t)(tab-ent);
        if (strncmp(importpath,ent,n)==0 && (importpath[n]==0||importpath[n]=='/')){
            char *d = importpath[n] ? path_join(tab+1,importpath+n+1) : xstrdup(tab+1);
            if (is_dir(d)) return d;
        }
    }
    if (g_gomodcache){
        char *d = path_join((char*)g_gomodcache,importpath);
        if (is_dir(d)) return d;
    }
    return NULL;
}

/* ------------------------------------------------------------- entry --- */

static void collect_init_order(Package *p, Vec *out, Vec *seen){
    for (int i=0;i<seen->len;i++) if (seen->p[i]==p) return;
    vec_push(seen,p);
    for (int i=0;i<p->imports.len;i++) collect_init_order(p->imports.p[i],out,seen);
    vec_push(out,p);
}

static FuncDef *build_entry(Package *mainpkg){
    FuncDef *f = xcalloc(1,sizeof *f);
    f->name = intern("main.entry");
    f->sig = type_new(T_FUNC);
    f->package = mainpkg;
    Vec order; memset(&order,0,sizeof order);
    Vec seen; memset(&seen,0,sizeof seen);
    collect_init_order(mainpkg,&order,&seen);
    int cap = order.len*2+8;
    f->code = xcalloc((size_t)cap,sizeof(Instr));
    int n = 0;
    for (int i=0;i<order.len;i++){
        Package *p = order.p[i];
        for (int j=0;j<p->init_funcs.len;j++){
            f->code[n].op = OP_CLOSURE; f->code[n].p = p->init_funcs.p[j]; n++;
            f->code[n].op = OP_CALL; f->code[n].a = 0; n++;
        }
    }
    Sym *ms = pkg_sym(mainpkg,intern("main"));
    if (!ms || ms->kind!=SYM_FUNC){
        fprintf(stderr,"runtime.main_main·f: function main is undeclared in the main package\n");
        exit(1);
    }
    f->code[n].op = OP_CLOSURE; f->code[n].p = ms->fn; n++;
    f->code[n].op = OP_CALL; f->code[n].a = 0; n++;
    f->code[n].op = OP_RET; f->code[n].a = 0; n++;
    f->ncode = n;
    f->nlocals = 0;
    f->compiled = 1;
    return f;
}

static void bind_runtime_types(void){
    Package *rt = pkg_lookup(intern("runtime"));
    if (!rt) return;
    Sym *s = pkg_sym(rt,intern("runtimeError"));
    if (s && s->kind==SYM_TYPE) ty_runtime_error = s->type;
    Sym *s2 = pkg_sym(rt,intern("PanicNilError"));
    if (s2 && s2->kind==SYM_TYPE) ty_panicnil = s2->type;
}

static int run_program(Package *mainpkg){
    if (errors_len()){ errors_print(stderr); return 1; }
    load_package("runtime", NULL, TOOLCHAIN_MINOR);
    bind_runtime_types();
    FuncDef *entry = build_entry(mainpkg);
    if (errors_len()){ errors_print(stderr); return 1; }
    Value fn = make_closure(entry,NULL);
    vm_start_goroutine_main(fn);
    vm_sched();
    fflush(stdout);
    return 0;
}

/* --------------------------------------------------------- go command --- */

static void setup_env(void){
    const char *r = getenv("GOROOT");
    if (!r){
        /* derive from argv[0]'s directory: <goroot>/bin/go */
        r = "/app/goroot";
    }
    g_goroot = intern(r);
    const char *gp = getenv("GOPATH");
    g_gopath = intern(gp?gp:"/root/go");
    const char *mc = getenv("GOMODCACHE");
    g_gomodcache = mc?intern(mc):intern(path_join((char*)g_gopath,"pkg/mod"));
    const char *px = getenv("GOPROXY");
    g_goproxy = intern(px?px:"https://proxy.golang.org,direct");
    const char *gm = getenv("GO111MODULE");
    g_go111module = intern(gm?gm:"");
}

void discover_module_pub(const char *dir);
static void discover_module(const char *dir){
    char *gm = find_gomod(dir);
    if (!gm) return;
    memset(&g_mainmod,0,sizeof g_mainmod);
    parse_gomod(&g_mainmod,gm);
    g_have_module = 1;
    if (g_mainmod.lang == 0) g_mainmod.lang = 16;
    free(gm);
    build_module_graph();
}

static int cmd_env(int argc, char **argv){
    struct { const char *k; const char *v; } vars[] = {
        {"GOROOT",g_goroot},{"GOPATH",g_gopath},{"GOMODCACHE",g_gomodcache},
        {"GOPROXY",g_goproxy},{"GOVERSION",TOOLCHAIN_VERSION},{"GOOS","linux"},
        {"GOARCH","amd64"},{"CGO_ENABLED","0"},
        {"GO111MODULE",g_go111module},{"GOFLAGS",getenv("GOFLAGS")?getenv("GOFLAGS"):""},
        {"GOMOD", NULL},{NULL,NULL}
    };
    char *gm = find_gomod(".");
    for (int i=0;vars[i].k;i++){
        const char *v = vars[i].v;
        if (strcmp(vars[i].k,"GOMOD")==0) v = gm?gm:"/dev/null";
        if (argc>0){
            for (int j=0;j<argc;j++) if (strcmp(argv[j],vars[i].k)==0) printf("%s\n",v?v:"");
        } else {
            printf("%s=\"%s\"\n",vars[i].k,v?v:"");
        }
    }
    return 0;
}

static Package *load_main_from_files(Vec *gofiles, int lang){
    Package *p = new_package("main",NULL,xstrdup("."),lang);
    const char *pkgname = NULL;
    for (int i=0;i<gofiles->len;i++){
        const char *fn = gofiles->p[i];
        size_t len; char *src = read_file(fn,&len);
        if (!src){ fprintf(stderr,"go: cannot read %s\n",fn); exit(1); }
        Node *f = parse_file(fn,src,len,lang);
        if (!pkgname) pkgname = f->name;
        vec_push(&p->files,f);
    }
    p->name = pkgname?pkgname:intern("main");
    p->dir = intern(path_dir((char*)gofiles->p[0]));
    check_package(p);
    return p;
}

static Package *load_main_from_dir(const char *dir, int lang){
    if (dir && (strcmp(dir,"./...")==0 || strcmp(dir,"...")==0)) dir = ".";
    Vec names = list_dir(dir);
    Vec files; memset(&files,0,sizeof files);
    for (int i=0;i<names.len;i++){
        const char *nm = names.p[i];
        if (!has_suffix(nm,".go")) continue;
        if (has_suffix(nm,"_test.go")) continue;
        if (nm[0]=='_'||nm[0]=='.') continue;
        vec_push(&files,path_join(dir,nm));
    }
    if (!files.len){ fprintf(stderr,"go: no Go files in %s\n",dir); exit(1); }
    return load_main_from_files(&files,lang);
}

void discover_module_pub(const char *dir){ discover_module(dir); }
const char *main_module_path(void){ return g_have_module ? g_mainmod.path : NULL; }
const char *main_module_dir(void){ return g_have_module ? g_mainmod.dir : "."; }
static int lang_for_build(void);
int lang_for_build_pub(void);
static int lang_for_build(void){
    if (g_have_module && g_mainmod.lang) return g_mainmod.lang;
    return TOOLCHAIN_MINOR;
}

/* Module mode is on by default from Go 1.16: building outside a module is an
 * error unless GO111MODULE says otherwise. */
static int require_module(void){
    if (g_have_module) return 0;
    if (g_go111module && (strcmp(g_go111module,"off")==0 || strcmp(g_go111module,"auto")==0)) return 0;
    fprintf(stderr,"go: go.mod file not found in current directory or any parent directory; see 'go help modules'\n");
    return 1;
}

int lang_for_build_pub(void){ return lang_for_build(); }
static void apply_lang_semantics(int lang);
void apply_lang_semantics_pub(int lang);
static void apply_lang_semantics(int lang){
    g_panic_nil_error = lang >= 21;
}

void apply_lang_semantics_pub(int lang){ apply_lang_semantics(lang); }
int run_program_pub(Package *p);
Package *load_main_from_files_pub(Vec *files, int lang);
static int cmd_run(int argc, char **argv){
    g_run_mode = 1;
    Vec gofiles; memset(&gofiles,0,sizeof gofiles);
    const char *dir = NULL;
    int i = 0;
    for (; i<argc; i++){
        if (argv[i][0]=='-' && !has_suffix(argv[i],".go")) continue;
        break;
    }
    for (; i<argc; i++){
        if (has_suffix(argv[i],".go")) vec_push(&gofiles,argv[i]);
        else break;
    }
    /* a single package argument: `.` or a relative directory such as ./cmd */
    if (!gofiles.len && i<argc && argv[i][0] != '-'){ dir = argv[i]; i++; }
    /* os.Args[0] is the program itself, as it is under gc: `go run` builds the
     * binary into the build cache and runs it from there. */
    vec_push(&g_runargs,(void*)intern("/tmp/go-build/b001/exe/main"));
    for (; i<argc; i++) vec_push(&g_runargs,argv[i]);
    discover_module(".");
    if (require_module()) return 1;
    int lang = lang_for_build();
    apply_lang_semantics(lang);
    Package *p;
    if (gofiles.len) p = load_main_from_files(&gofiles,lang);
    else p = load_main_from_dir(dir?dir:".",lang);
    if (errors_len()){ errors_print(stderr); return 1; }
    if (strcmp(p->name,"main")!=0){ fprintf(stderr,"go: cannot run non-main package\n"); return 1; }
    return run_program(p);
}

int run_program_pub(Package *p){ return run_program(p); }
Package *load_main_from_files_pub(Vec *files, int lang){ return load_main_from_files(files,lang); }
static int cmd_build(int argc, char **argv){
    const char *out = NULL;
    Vec gofiles; memset(&gofiles,0,sizeof gofiles);
    const char *dir = NULL;
    for (int i=0;i<argc;i++){
        if (strcmp(argv[i],"-o")==0 && i+1<argc){ out = argv[++i]; continue; }
        if (argv[i][0]=='-') continue;
        if (has_suffix(argv[i],".go")) vec_push(&gofiles,argv[i]);
        else dir = argv[i];
    }
    discover_module(".");
    if (require_module()) return 1;
    int lang = lang_for_build();
    apply_lang_semantics(lang);
    Package *p;
    if (gofiles.len) p = load_main_from_files(&gofiles,lang);
    else p = load_main_from_dir(dir?dir:".",lang);
    if (errors_len()){ errors_print(stderr); return 1; }
    if (strcmp(p->name,"main")!=0) return 0;
    if (!out){
        if (gofiles.len){ char *b = path_base((char*)gofiles.p[0]); b[strlen(b)-3]=0; out = b; }
        else out = path_base((char*)(dir?dir:(g_have_module&&g_mainmod.path?path_base((char*)g_mainmod.path):"main")));
    }
    /* write a self-contained archive script */
    Buf b; buf_init(&b);
    buf_printf(&b,"#!/bin/sh\nexec %s/bin/go toolexec \"$0\" \"$@\"\n### GOARCHIVE\n",g_goroot);
    for (int i=0;i<p->files.len;i++){
        Node *f = p->files.p[i];
        const char *fn = f->pos.file;
        size_t len; char *src = read_file(fn,&len);
        if (!src) continue;
        buf_printf(&b,"--file %s %zu\n",path_base((char*)fn),len);
        buf_put(&b,src,len);
        buf_putc(&b,'\n');
        free(src);
    }
    buf_printf(&b,"--lang %d\n",lang);
    if (write_file(out,b.p,b.len)!=0){ fprintf(stderr,"go: cannot write %s\n",out); return 1; }
    chmod(out,0755);
    buf_free(&b);
    return 0;
}

static int cmd_toolexec(int argc, char **argv){
    if (argc<1) return 2;
    const char *archive = argv[0];
    for (int i=1;i<argc;i++) vec_push(&g_runargs,argv[i]);
    size_t len; char *src = read_file(archive,&len);
    if (!src){ fprintf(stderr,"go: cannot read %s\n",archive); return 1; }
    char *p = strstr(src,"### GOARCHIVE\n");
    if (!p){ fprintf(stderr,"go: malformed executable\n"); return 1; }
    p += strlen("### GOARCHIVE\n");
    char tmpl[] = "/tmp/gobuildXXXXXX";
    char *dir = mkdtemp(tmpl);
    Vec files; memset(&files,0,sizeof files);
    int lang = TOOLCHAIN_MINOR;
    while (p && *p){
        if (strncmp(p,"--file ",7)==0){
            char name[512]; size_t sz=0;
            if (sscanf(p+7,"%511s %zu",name,&sz)!=2) break;
            char *nl = strchr(p,'\n');
            if (!nl) break;
            char *data = nl+1;
            char *full = path_join(dir,name);
            write_file(full,data,sz);
            vec_push(&files,full);
            p = data+sz+1;
            continue;
        }
        if (strncmp(p,"--lang ",7)==0){ lang = atoi(p+7); break; }
        break;
    }
    apply_lang_semantics(lang);
    Package *pk = load_main_from_files(&files,lang);
    if (errors_len()){ errors_print(stderr); return 1; }
    return run_program(pk);
}

static int cmd_version(int argc, char **argv){
    printf("go version %s linux/amd64\n",TOOLCHAIN_VERSION);
    return 0;
}

extern int vet_main(int argc, char **argv);
extern int test_main(int argc, char **argv);
extern int mod_main(int argc, char **argv);

int go_main(int argc, char **argv){
    setup_env();
    types_init();
    gc_init();
    natives_init();
    if (argc < 2){
        fprintf(stderr,"Go is a tool for managing Go source code.\n\nUsage:\n\n\tgo <command> [arguments]\n");
        return 2;
    }
    const char *cmd = argv[1];
    int rest = argc-2;
    char **restv = argv+2;
    if (strcmp(cmd,"run")==0) return cmd_run(rest,restv);
    if (strcmp(cmd,"build")==0) return cmd_build(rest,restv);
    if (strcmp(cmd,"env")==0) return cmd_env(rest,restv);
    if (strcmp(cmd,"version")==0) return cmd_version(rest,restv);
    if (strcmp(cmd,"toolexec")==0) return cmd_toolexec(rest,restv);
    if (strcmp(cmd,"vet")==0) return vet_main(rest,restv);
    if (strcmp(cmd,"test")==0) return test_main(rest,restv);
    if (strcmp(cmd,"mod")==0) return mod_main(rest,restv);
    fprintf(stderr,"go %s: unknown command\n",cmd);
    return 2;
}

int main(int argc, char **argv){ return go_main(argc,argv); }

/* ------------------------------------------------------------- go:embed --- */

static int path_match(const char *pat, const char *name){
    /* path.Match over one element: * matches any run of non-separator chars */
    while (*pat){
        if (*pat=='*'){
            pat++;
            if (!*pat) return strchr(name,'/')==NULL;
            for (const char *n=name; *n && *n!='/'; n++)
                if (path_match(pat,n)) return 1;
            return path_match(pat,name+strlen(name));
        }
        if (*name != *pat) return 0;
        pat++; name++;
    }
    return *name==0;
}

static void embed_walk(const char *root, const char *rel, Vec *names, Vec *datas, int all){
    char *dir = rel[0] ? path_join((char*)root,rel) : xstrdup(root);
    Vec ents = list_dir(dir);
    for (int i=0;i<ents.len;i++){
        const char *nm = ents.p[i];
        if (!all && (nm[0]=='.' || nm[0]=='_')) continue;
        char *sub = rel[0] ? xsprintf("%s/%s",rel,nm) : xstrdup(nm);
        char *full = path_join(dir,nm);
        if (is_dir(full)) embed_walk(root,sub,names,datas,all);
        else {
            size_t len; char *d = read_file(full,&len);
            if (d){ vec_push(names,(void*)intern(sub)); vec_push(datas,d); }
        }
    }
}

int embed_expand(const char *dir, const char *patterns, Vec *names, Vec *datas){
    char *copy = xstrdup(patterns);
    char *save = NULL;
    int found = 0;
    for (char *tok = strtok_r(copy," \t\r\n",&save); tok; tok = strtok_r(NULL," \t\r\n",&save)){
        int all = 0;
        if (strncmp(tok,"all:",4)==0){ all = 1; tok += 4; }
        if (tok[0]=='"'){ size_t l=strlen(tok); if (l>1&&tok[l-1]=='"'){ tok[l-1]=0; tok++; } }
        char *full = path_join((char*)dir,tok);
        if (is_dir(full)){ embed_walk(dir,tok,names,datas,all); found = 1; continue; }
        if (!strchr(tok,'*')){
            size_t len; char *d = read_file(full,&len);
            if (d){ vec_push(names,(void*)intern(tok)); vec_push(datas,d); found = 1; }
            continue;
        }
        /* glob within one directory */
        char *pdir = path_dir(tok);
        char *pbase = path_base(tok);
        char *absdir = strcmp(pdir,".")==0 ? xstrdup(dir) : path_join((char*)dir,pdir);
        Vec ents = list_dir(absdir);
        for (int i=0;i<ents.len;i++){
            const char *nm = ents.p[i];
            if (nm[0]=='.'||nm[0]=='_') continue;
            if (!path_match(pbase,nm)) continue;
            char *rel = strcmp(pdir,".")==0 ? xstrdup(nm) : xsprintf("%s/%s",pdir,nm);
            char *fp = path_join(absdir,nm);
            if (is_dir(fp)){ embed_walk(dir,rel,names,datas,all); found=1; continue; }
            size_t len; char *d = read_file(fp,&len);
            if (d){ vec_push(names,(void*)intern(rel)); vec_push(datas,d); found = 1; }
        }
    }
    free(copy);
    return found ? 0 : -1;
}
