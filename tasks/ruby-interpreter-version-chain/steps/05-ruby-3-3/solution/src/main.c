#include "internal.h"
#include <unistd.h>
extern void rb_gv_set(ID, VALUE);
extern VALUE rb_main_object(void);

static char *read_all(const char *path, long *len){
  FILE *f = fopen(path,"rb");
  if(!f) return NULL;
  fseek(f,0,SEEK_END); long n = ftell(f); fseek(f,0,SEEK_SET);
  char *buf = xmalloc(n+1);
  size_t rd = fread(buf,1,n,f);
  buf[rd]=0; *len = (long)rd;
  fclose(f);
  return buf;
}

int main(int argc, char **argv){
  const char *fname = NULL;
  char *script = NULL;
  VALUE argv_ary;
  int i = 1;
  int verbose = 0, dep = -1;
  VALUE load_path = 0;
  const char *requires[16]; int nreq = 0;
  rb_init();
  argv_ary = rb_ary_new();
  load_path = rb_ary_new();
  for(; i<argc; i++){
    char *a = argv[i];
    if(a[0] != '-' || a[1]==0) break;
    if(strcmp(a,"--")==0){ i++; break; }
    if(strcmp(a,"--version")==0){ printf("ruby %d.%d.0 (mini) [x86_64-linux]\n", RUBY_TARGET/10, RUBY_TARGET%10); return 0; }
    if(strcmp(a,"-v")==0){ verbose = 1; continue; }
    if(strcmp(a,"-w")==0){ verbose = 1; continue; }
    if(strncmp(a,"-W",2)==0){
      const char *p = a+2;
      if(*p==0) verbose = 1;
      else if(strcmp(p,"0")==0) verbose = -1;
      else if(strcmp(p,"1")==0) verbose = 0;
      else if(strcmp(p,"2")==0) verbose = 1;
      else if(strcmp(p,":deprecated")==0) dep = 1;
      else if(strcmp(p,":no-deprecated")==0) dep = 0;
      else if(strcmp(p,":experimental")==0) { }
      else if(strcmp(p,":no-experimental")==0) { }
      continue; }
    if(strncmp(a,"-I",2)==0){
      if(a[2]) rb_ary_push(load_path, rb_str_new_cstr(a+2));
      else if(i+1<argc) rb_ary_push(load_path, rb_str_new_cstr(argv[++i]));
      continue; }
    if(strncmp(a,"-e",2)==0){
      const char *code = a[2] ? a+2 : (i+1<argc ? argv[++i] : "");
      long n = (long)strlen(code);
      if(script){ char *ns = xmalloc(strlen(script)+n+2); sprintf(ns,"%s\n%s",script,code); script = ns; }
      else { script = xmalloc(n+1); memcpy(script,code,n+1); }
      fname = "-e";
      continue; }
    if(strncmp(a,"-r",2)==0){
      const char *lib = a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL);
      if(lib){ if(nreq < 16) requires[nreq++] = lib; }
      continue; }
    if(strcmp(a,"--disable=gems")==0||strcmp(a,"--disable-gems")==0) continue;
    if(strcmp(a,"--enable=frozen-string-literal")==0||strcmp(a,"--enable-frozen-string-literal")==0) continue;
    /* unknown switch: ignore */
  }
  rb_verbose = verbose;
  rb_warn_deprecated_on = (dep >= 0) ? dep : (verbose > 0);
  if(!script){
    if(i >= argc){ fprintf(stderr,"ruby: no script given\n"); return 1; }
    fname = argv[i++];
    long n;
    script = read_all(fname, &n);
    if(!script){ fprintf(stderr,"ruby: No such file or directory -- %s (LoadError)\n", fname); return 1; }
  }
  for(; i<argc; i++) rb_ary_push(argv_ary, rb_str_new_cstr(argv[i]));
  rb_const_set(rb_cObject, ID_CACHED("ARGV"), argv_ary);
  rb_gv_set(ID_CACHED("$0"), rb_str_new_cstr(fname));
  rb_gv_set(ID_CACHED("$PROGRAM_NAME"), rb_str_new_cstr(fname));
  rb_gv_set(ID_CACHED("$LOAD_PATH"), load_path);
  rb_gv_set(ID_CACHED("$:"), load_path);
  rb_gv_set(ID_CACHED("$stdout"), Qnil);
  rb_gv_set(ID_CACHED("$,"), Qnil);
  rb_gv_set(ID_CACHED("$/"), rb_str_new_cstr("\n"));
  rb_source_file = fname;
  for(int k=0;k<nreq;k++){
    rb_tag rt; TAG_PUSH(rt);
    if(setjmp(rt.buf)==0){ rb_funcall(rb_main_object(), ID_CACHED("require"), 1, rb_str_new_cstr(requires[k])); TAG_POP(rt); }
    else { TAG_POP(rt); fprintf(stderr, "ruby: cannot load -- %s (LoadError)\n", requires[k]); return 1; }
  }

  rb_tag tag; TAG_PUSH(tag);
  int status = 0;
  if(setjmp(tag.buf)==0){
    rb_eval_string_file(script, fname);
    TAG_POP(tag);
  } else {
    TAG_POP(tag);
    fflush(stdout);
    if(tag.state==TAG_RAISE){
      VALUE exc = tag.retval;
      if(rb_is_a(exc, rb_eSystemExit)){
        VALUE st = rb_funcall(exc, ID_CACHED("status"), 0);
        fflush(stdout);
        return (int)rb_num2long(st);
      }
      VALUE msg = Qnil, cls = Qnil;
      rb_tag t2; TAG_PUSH(t2);
      if(setjmp(t2.buf)==0){ msg = rb_funcall(exc, ID_CACHED("message"), 0); TAG_POP(t2); }
      else { TAG_POP(t2); msg = rb_str_new_cstr("(error)"); }
      cls = rb_class_name(rb_class_of(exc));
      fprintf(stderr, "%s: ", fname);
      fwrite(RSTR(msg)->ptr,1,RSTR(msg)->len,stderr);
      fprintf(stderr, " (%s)\n", RSTR(cls)->ptr);
      status = 1;
    } else if(tag.state==TAG_THROW){
      fprintf(stderr, "%s: uncaught throw (UncaughtThrowError)\n", fname);
      status = 1;
    } else {
      fprintf(stderr, "%s: unexpected break/return (LocalJumpError)\n", fname);
      status = 1;
    }
  }
  fflush(stdout);
  return status;
}
