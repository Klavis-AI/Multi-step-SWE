#define _POSIX_C_SOURCE 200809L
#include "go.h"
#include <unistd.h>
#include <sys/stat.h>

/* `go test`: compile the package with its _test.go files, then run a generated
 * main that hands the discovered Test functions to testing.MainWithFlags, the
 * same shape cmd/go uses. */

extern int g_build_test;
extern const char *module_path_of(const char *dir);
extern void discover_module_pub(const char *dir);
extern const char *main_module_path(void);
extern const char *main_module_dir(void);
extern int run_program_pub(Package *mainpkg);
extern int lang_for_build_pub(void);
extern void apply_lang_semantics_pub(int lang);
extern Package *load_main_from_files_pub(Vec *files, int lang);

int test_main(int argc, char **argv){
    const char *run = "";
    int verbose = 0;
    Vec pkgs; memset(&pkgs,0,sizeof pkgs);
    for (int i=0;i<argc;i++){
        const char *a = argv[i];
        if (strcmp(a,"-v")==0 || strcmp(a,"-test.v")==0){ verbose = 1; continue; }
        if (strncmp(a,"-run=",5)==0){ run = a+5; continue; }
        if (strcmp(a,"-run")==0 && i+1<argc){ run = argv[++i]; continue; }
        if (strncmp(a,"-count",6)==0 || strncmp(a,"-timeout",8)==0){ if (!strchr(a,'=') && i+1<argc) i++; continue; }
        if (a[0]=='-') continue;
        vec_push(&pkgs,(void*)a);
    }
    discover_module_pub(".");
    int lang = lang_for_build_pub();
    apply_lang_semantics_pub(lang);
    const char *modpath = main_module_path();
    const char *moddir = main_module_dir();
    if (!modpath){ fprintf(stderr,"go: cannot find main module\n"); return 1; }

    const char *target = pkgs.len ? (const char*)pkgs.p[0] : ".";
    Vec dirs; memset(&dirs,0,sizeof dirs);
    if (strcmp(target,"./...")==0 || strcmp(target,"...")==0){
        /* walk the module for directories holding _test.go files */
        Vec stack; memset(&stack,0,sizeof stack);
        vec_push(&stack,(void*)moddir);
        while (stack.len){
            char *d = (char*)stack.p[--stack.len];
            Vec names = list_dir(d);
            int has_test = 0;
            for (int i=0;i<names.len;i++){
                const char *nm = names.p[i];
                char *full = path_join(d,nm);
                if (is_dir(full)){
                    if (nm[0] != '.' && strcmp(nm,"vendor")!=0) vec_push(&stack,full);
                } else {
                    size_t l = strlen(nm);
                    if (l>8 && strcmp(nm+l-8,"_test.go")==0) has_test = 1;
                }
            }
            if (has_test) vec_push(&dirs,d);
        }
    } else {
        char *d = (strcmp(target,".")==0) ? xstrdup(".") : xstrdup(target);
        vec_push(&dirs,d);
    }
    if (!dirs.len){ printf("no test files\n"); return 0; }

    int rc = 0;
    for (int di=0; di<dirs.len; di++){
        const char *dir = dirs.p[di];
        char abs[4096];
        if (!realpath(dir,abs)) snprintf(abs,sizeof abs,"%s",dir);
        /* import path of this directory within the module */
        const char *ip = modpath;
        size_t ml = strlen(moddir);
        char modabs[4096];
        if (!realpath(moddir,modabs)) snprintf(modabs,sizeof modabs,"%s",moddir);
        ml = strlen(modabs);
        if (strncmp(abs,modabs,ml)==0 && (abs[ml]==0 || abs[ml]=='/')){
            if (abs[ml]==0) ip = modpath;
            else ip = intern(xsprintf("%s/%s",modpath,abs+ml+1));
        }
        g_build_test = 1;
        Package *p = load_package(ip,moddir,lang);
        g_build_test = 0;
        if (!p){ fprintf(stderr,"go: cannot load package %s\n",ip); rc = 1; continue; }
        if (errors_len()){ errors_print(stderr); return 1; }
        Vec tests; memset(&tests,0,sizeof tests);
        Vec fuzzes; memset(&fuzzes,0,sizeof fuzzes);
        for (int i=0;i<p->syms.len;i++){
            Sym *s = p->syms.p[i];
            if (s->kind != SYM_FUNC || !s->name) continue;
            if (s->recvfield) continue;
            if (!s->type || s->type->fields.len != 1 || s->type->results.len != 0) continue;
            if (strncmp(s->name,"Test",4)==0){
                if (s->name[4] && s->name[4] >= 'a' && s->name[4] <= 'z') continue;
                vec_push(&tests,s);
            } else if (strncmp(s->name,"Fuzz",4)==0){
                if (s->name[4] && s->name[4] >= 'a' && s->name[4] <= 'z') continue;
                vec_push(&fuzzes,s);
            }
        }
        Buf b; buf_init(&b);
        buf_printf(&b,"package main\n\nimport (\n\t\"testing\"\n\tpkgundertest %c%s%c\n)\n\n",'"',ip,'"');
        buf_printf(&b,"func main() {\n\ttesting.SetExitCode(testing.MainWithFlags(%c%s%c, %s, []testing.InternalTest{\n",
                   '"', run, '"', verbose?"true":"false");
        for (int i=0;i<tests.len;i++){
            Sym *s = tests.p[i];
            buf_printf(&b,"\t\t{%c%s%c, pkgundertest.%s},\n",'"',s->name,'"',s->name);
        }
        buf_puts(&b,"\t}, []testing.InternalFuzzTarget{\n");
        for (int i=0;i<fuzzes.len;i++){
            Sym *s = fuzzes.p[i];
            buf_printf(&b,"\t\t{%c%s%c, pkgundertest.%s},\n",'"',s->name,'"',s->name);
        }
        buf_puts(&b,"\t}))\n}\n");
        char tmpl[] = "/tmp/gotestXXXXXX";
        char *td = mkdtemp(tmpl);
        char *mf = path_join(td,"main.go");
        write_file(mf,b.p,b.len);
        buf_free(&b);
        Vec files; memset(&files,0,sizeof files);
        vec_push(&files,mf);
        Package *mp = load_main_from_files_pub(&files,lang);
        if (errors_len()){ errors_print(stderr); return 1; }
        extern int g_test_exit_code;
        extern int g_test_no_tests;
        extern FILE *g_print_stream;
        g_test_exit_code = 0;
        g_test_no_tests = 0;
        /* Without -v the go command shows the test binary's output only when
         * the package fails, so capture it and decide afterwards. */
        FILE *cap = NULL;
        int saved = -1;
        if (!verbose){
            cap = tmpfile();
            if (cap){
                fflush(stdout);
                saved = dup(1);
                dup2(fileno(cap), 1);
            }
        }
        g_print_stream = stdout;
        int r = run_program_pub(mp);
        g_print_stream = NULL;
        if (cap){
            fflush(stdout);
            if (saved >= 0){ dup2(saved, 1); close(saved); }
            if (r != 0 || g_test_exit_code != 0){
                rewind(cap);
                char buf[4096];
                size_t k;
                while ((k = fread(buf, 1, sizeof buf, cap)) > 0) fwrite(buf, 1, k, stdout);
            }
            fclose(cap);
            fflush(stdout);
        }
        if (r != 0 || g_test_exit_code != 0) rc = 1;
        fflush(stdout);
        if (g_test_exit_code != 0 || r != 0){
            fprintf(stdout,"FAIL\t%s\t0.001s\n", ip);
            fflush(stdout);
        } else {
            /* gc appends this when -run matched nothing */
            printf("ok  \t%s\t0.001s%s\n", ip,
                   g_test_no_tests ? " [no tests to run]" : "");
            fflush(stdout);
        }
    }
    if (rc != 0){ fflush(stdout); fprintf(stderr,"FAIL\n"); fflush(stderr); }
    return rc;
}
