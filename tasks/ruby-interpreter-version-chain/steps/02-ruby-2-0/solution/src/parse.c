/* Lexer + recursive-descent parser for the Ruby subset. */
#include "internal.h"
#include <ctype.h>

/* ===================== tokens ===================== */
enum {
  tEOF = 0, tNL, tINT, tFLOAT, tSTR, tDSTR, tSYM, tDSYM, tREGX, tXSTR,
  tIDENT, tCONST, tIVAR, tCVAR, tGVAR, tLABEL, tNTHREF, tBACKREF,
  tLPAREN, tLPAREN_ARG, tRPAREN, tLBRACK, tLBRACK_ARG, tRBRACK,
  tLBRACE, tLBRACE_ARG, tRBRACE, tCOMMA, tSEMI, tDOT, tSAFEDOT, tCOLON2,
  tCOLON3, tCOLON, tQUESTION, tARROW, tASSOC, tSTAR, tDSTAR, tAMPER, tUAMPER,
  tOP, tOPASGN, tASGN, tMATCH, tNMATCH, tANDOP, tOROP, tNOT, tDOT2, tDOT3,
  tBDOT2, tBDOT3, tPOW, tUMINUS, tUPLUS, tBANG, tTILDE, tCMP, tEQ, tEQQ, tNEQ,
  tGEQ, tLEQ, tLSHFT, tRSHFT, tANDDOT, tLAMBEG, tRIGHTWARD,
  /* keywords */
  kBEGIN_, kEND_, kALIAS, kAND, kBEGIN, kBREAK, kCASE, kCLASS, kDEF, kDEFINED,
  kDO, kDO_BLOCK, kDO_COND, kELSE, kELSIF, kEND, kENSURE, kFALSE, kFOR, kIF,
  kIN, kMODULE, kNEXT, kNIL, kNOT, kOR, kREDO, kRESCUE, kRETRY, kRETURN,
  kSELF, kSUPER, kTHEN, kTRUE, kUNDEF, kUNLESS, kUNTIL, kWHEN, kWHILE, kYIELD,
  kIF_MOD, kUNLESS_MOD, kWHILE_MOD, kUNTIL_MOD, kRESCUE_MOD, k__METHOD__,
  k__FILE__, k__LINE__, k__DIR__, kLAST
};

typedef struct { int type; ID id; VALUE val; int line; int spc; Node *node; int nlbefore; } Token;

/* lexer states */
enum { EXPR_BEG=1, EXPR_END=2, EXPR_ARG=4, EXPR_FNAME=8, EXPR_DOT=16,
       EXPR_CLASS=32, EXPR_MID=64, EXPR_ENDFN=128, EXPR_LABEL=256, EXPR_VALUE=512 };

typedef struct Heredoc { char *id; int squiggly; int dash; int interp; struct Heredoc *next; } Heredoc;

typedef struct Parser {
  const char *src; long len, pos;
  int line;
  int state;
  int paren_nest, cond_nest, cmdarg;
  Token tok, ahead; int has_ahead; int ahead_state;
  LocalTbl *lvtbl;
  const char *fname;
  int in_def, in_class, in_single;
  Heredoc *pending_heredoc;
  long heredoc_restore;
  int fsl;                     /* frozen_string_literal magic comment: -1 unset */
  rb_encoding *enc;
  int last_line;
} Parser;

static Parser *P;

void rb_syntax_error(const char *fmt, ...){
  char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  VALUE msg = rb_str_new_cstr("");
  char pre[256]; snprintf(pre,sizeof pre, "%s:%d: syntax error, ", P?P->fname:"-", P?P->line:0);
  rb_str_cat_cstr(msg, pre); rb_str_cat_cstr(msg, buf);
  rb_exc_raise(rb_exc_new(rb_eSyntaxError, msg));
}

/* ===================== node helpers ===================== */
Node *node_new(int type, int line){
  Node *n = xcalloc(1,sizeof(Node)); n->type = type; n->line = line; return n;
}
void node_add(Node *n, Node *c){
  if(n->nitems % 8 == 0) n->items = xrealloc(n->items, (n->nitems+8)*sizeof(Node*));
  n->items[n->nitems++] = c;
}
static Node *nd(int t){ return node_new(t, P->line); }

/* ===================== local variable tables ===================== */
static LocalTbl *lvtbl_new(LocalTbl *parent, int is_block){
  LocalTbl *t = xcalloc(1,sizeof(LocalTbl)); t->parent = parent; t->is_block = is_block; return t;
}
static int lv_find_in(LocalTbl *t, ID id){
  for(int i=0;i<t->n;i++) if(t->names[i]==id) return i;
  return -1;
}
static int lv_add(LocalTbl *t, ID id){
  int i = lv_find_in(t,id); if(i>=0) return i;
  if(t->n >= t->cap){ t->cap = t->cap? t->cap*2 : 8; t->names = xrealloc(t->names, t->cap*sizeof(ID)); }
  t->names[t->n]=id; return t->n++;
}
/* returns 1 and sets depth/idx if found */
static int lv_lookup(LocalTbl *t, ID id, int *depth, int *idx){
  int d=0;
  while(t){
    int i = lv_find_in(t,id);
    if(i>=0){ *depth=d; *idx=i; return 1; }
    if(!t->is_block) return 0;
    t = t->parent; d++;
  }
  return 0;
}
/* Block *parameters*: Ruby 1.8 reuses an outer local of the same name (so the
   assignment leaks out of the block); 1.9 made them always block-local. */
static void lv_declare_param(ID id, int *depth, int *idx){
  if(rb_target_version < 20){
    if(lv_lookup(P->lvtbl, id, depth, idx)) return;
  }
  *depth = 0; *idx = lv_add(P->lvtbl, id);
}
static void lv_declare(ID id, int *depth, int *idx){
  if(lv_lookup(P->lvtbl, id, depth, idx)) return;
  *depth = 0; *idx = lv_add(P->lvtbl, id);
}

/* ===================== character helpers ===================== */
#define CUR   (P->pos < P->len ? P->src[P->pos] : 0)
#define AT(i) (P->pos+(i) < P->len ? P->src[P->pos+(i)] : 0)
static int is_ident_char(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||(unsigned char)c>=0x80; }
static int is_ident_start(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(unsigned char)c>=0x80; }
static int is_digit(int c){ return c>='0'&&c<='9'; }
static int is_space(int c){ return c==' '||c=='\t'||c=='\r'||c=='\f'||c=='\v'; }

/* ===================== keyword table ===================== */
static struct { const char *name; int tok; int mod; } kwtbl[] = {
  {"BEGIN",kBEGIN_,0},{"END",kEND_,0},{"alias",kALIAS,0},{"and",kAND,0},
  {"begin",kBEGIN,0},{"break",kBREAK,0},{"case",kCASE,0},{"class",kCLASS,0},
  {"def",kDEF,0},{"defined?",kDEFINED,0},{"do",kDO,0},{"else",kELSE,0},
  {"elsif",kELSIF,0},{"end",kEND,0},{"ensure",kENSURE,0},{"false",kFALSE,0},
  {"for",kFOR,0},{"if",kIF,kIF_MOD},{"in",kIN,0},{"module",kMODULE,0},
  {"next",kNEXT,0},{"nil",kNIL,0},{"not",kNOT,0},{"or",kOR,0},{"redo",kREDO,0},
  {"rescue",kRESCUE,kRESCUE_MOD},{"retry",kRETRY,0},{"return",kRETURN,0},
  {"self",kSELF,0},{"super",kSUPER,0},{"then",kTHEN,0},{"true",kTRUE,0},
  {"undef",kUNDEF,0},{"unless",kUNLESS,kUNLESS_MOD},{"until",kUNTIL,kUNTIL_MOD},
  {"when",kWHEN,0},{"while",kWHILE,kWHILE_MOD},{"yield",kYIELD,0},
  {"__method__",k__METHOD__,0},{"__FILE__",k__FILE__,0},{"__LINE__",k__LINE__,0},
  {"__dir__",k__DIR__,0},{NULL,0,0}
};

static int lv_lookup(LocalTbl *t, ID id, int *depth, int *idx);
/* forward decls */
static Node *parse_stmts(int *terms, int nterms);
static Node *parse_stmt(void);
static Node *parse_expr(void);
static Node *parse_arg(void);
static Node *parse_primary(void);
static Node *parse_ternary(void);
static void next_token(void);
static Node *parse_sub_source(const char *s, long len, int line);
static Node *parse_pattern_top(void);

/* ===================== string scanning ===================== */
typedef struct { VALUE buf; Node *parts; int interp; } StrAcc;

static void sacc_init(StrAcc *a){ a->buf = rb_str_new("",0); a->parts = NULL; a->interp = 0; }
static void sacc_flush(StrAcc *a){
  if(!a->parts) { a->parts = node_new(N_DSTR, P->line); }
  if(RSTR(a->buf)->len > 0){
    Node *s = node_new(N_STR, P->line); s->val = a->buf; node_add(a->parts, s);
    a->buf = rb_str_new("",0);
  }
}
static void sacc_addchar(StrAcc *a, int c){ char ch=(char)c; rb_str_cat(a->buf,&ch,1); }
static void sacc_addstr(StrAcc *a, const char *s, long n){ rb_str_cat(a->buf,s,n); }

static int read_escape(StrAcc *a, int in_regex);

/* read balanced #{...} content, pos is just after '{' */
static Node *read_interp(void){
  long start = P->pos;
  int depth = 1;
  while(P->pos < P->len){
    char c = P->src[P->pos];
    if(c=='\n') P->line++;
    if(c=='{') depth++;
    else if(c=='}'){ depth--; if(depth==0) break; }
    else if(c=='"' || c=='\''){
      char q=c; P->pos++;
      while(P->pos<P->len && P->src[P->pos]!=q){ if(P->src[P->pos]=='\\') P->pos++; if(P->src[P->pos]=='\n') P->line++; P->pos++; }
    }
    P->pos++;
  }
  long n = P->pos - start;
  Node *sub = parse_sub_source(P->src + start, n, P->line);
  P->pos++; /* skip } */
  return sub;
}

static int closing_for(int open){
  switch(open){ case '(': return ')'; case '[': return ']'; case '{': return '}'; case '<': return '>'; }
  return open;
}

/* Scan a quoted literal body up to terminator. Returns node (N_STR or N_DSTR). */
static Node *scan_string_body(int term, int open, int interp, int is_regex){
  StrAcc a; sacc_init(&a);
  int nest = 0;
  while(1){
    if(P->pos >= P->len) rb_syntax_error("unterminated string meets end of file");
    char c = P->src[P->pos];
    if(c == '\\'){
      P->pos++;
      if(is_regex || !interp){
        char e = CUR;
        if(!interp){
          /* single-quote rules: only \\ and \term are escapes */
          if(e == (char)term || e=='\\'){ sacc_addchar(&a,e); P->pos++; }
          else { sacc_addchar(&a,'\\'); }
          continue;
        }
        /* regex: keep escapes verbatim except \<newline> */
        if(e=='\n'){ P->line++; P->pos++; continue; }
        sacc_addchar(&a,'\\'); sacc_addchar(&a,e); P->pos++;
        continue;
      }
      read_escape(&a, 0);
      continue;
    }
    if(c == '#' && interp && P->pos+1 < P->len){
      char d = P->src[P->pos+1];
      if(d == '{'){
        P->pos += 2;
        sacc_flush(&a);
        Node *sub = read_interp();
        a.interp = 1;
        node_add(a.parts, sub);
        continue;
      } else if(d=='@' || d=='$'){
        long save = P->pos;
        P->pos++;
        long s0 = P->pos;
        if(CUR=='@'){ P->pos++; if(CUR=='@') P->pos++; }
        else P->pos++;
        if(is_ident_start(CUR)){
          while(is_ident_char(CUR)) P->pos++;
          sacc_flush(&a); a.interp = 1;
          Node *sub = parse_sub_source(P->src+s0, P->pos-s0, P->line);
          node_add(a.parts, sub);
          continue;
        }
        P->pos = save;
      }
    }
    if(open != term && c == (char)open){ nest++; sacc_addchar(&a,c); P->pos++; continue; }
    if(c == (char)term){
      if(nest > 0 && open != term){ nest--; sacc_addchar(&a,c); P->pos++; continue; }
      P->pos++; break;
    }
    if(c=='\n') P->line++;
    sacc_addchar(&a,c); P->pos++;
  }
  if(a.interp){
    sacc_flush(&a);
    return a.parts;
  }
  Node *s = node_new(N_STR, P->line); s->val = a.buf; return s;
}

static int hexval(int c){ if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; }

static int read_escape(StrAcc *a, int in_regex){
  char c = CUR; P->pos++;
  switch(c){
    case 'n': sacc_addchar(a,'\n'); break;
    case 't': sacc_addchar(a,'\t'); break;
    case 'r': sacc_addchar(a,'\r'); break;
    case 'f': sacc_addchar(a,'\f'); break;
    case 'v': sacc_addchar(a,'\v'); break;
    case 'a': sacc_addchar(a,'\a'); break;
    case 'b': sacc_addchar(a,'\b'); break;
    case 'e': sacc_addchar(a,27); break;
    case 's': sacc_addchar(a,' '); break;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
      int v = c-'0';
      for(int i=0;i<2 && CUR>='0'&&CUR<='7';i++){ v = v*8 + (CUR-'0'); P->pos++; }
      sacc_addchar(a, v & 0xff); break; }
    case 'x': { int v=0,n=0; while(n<2 && hexval(CUR)>=0){ v=v*16+hexval(CUR); P->pos++; n++; }
      sacc_addchar(a, v & 0xff); break; }
    case 'u': {
      if(CUR=='{'){ P->pos++;
        while(1){ while(is_space(CUR)) P->pos++;
          if(CUR=='}'){ P->pos++; break; }
          int v=0; int got=0;
          while(hexval(CUR)>=0){ v=v*16+hexval(CUR); P->pos++; got=1; }
          if(!got){ P->pos++; break; }
          char buf[8]; long n = rb_enc_mbcput(enc_utf8, v, buf); sacc_addstr(a,buf,n);
        }
      } else { int v=0,n=0; while(n<4 && hexval(CUR)>=0){ v=v*16+hexval(CUR); P->pos++; n++; }
        char buf[8]; long n2 = rb_enc_mbcput(enc_utf8, v, buf); sacc_addstr(a,buf,n2); }
      break; }
    case '\n': P->line++; break;
    default: sacc_addchar(a, c); break;
  }
  return 0;
}

/* ===================== heredocs ===================== */
static Node *read_heredoc(const char *id, int squiggly, int dash, int interp){
  /* Body starts at the next line.  We must scan forward from the newline that
     ends the current logical line, then splice it out. */
  long p = P->pos;
  while(p < P->len && P->src[p] != '\n') p++;
  if(p < P->len) p++;
  long body_start = p;
  long idlen = strlen(id);
  Node *result = NULL;
  /* collect raw lines */
  VALUE raw = rb_str_new("",0);
  long q = body_start;
  long end_of_heredoc = P->len;
  int min_indent = INT_MAX;
  while(q <= P->len){
    long ls = q;
    while(q < P->len && P->src[q] != '\n') q++;
    long le = q;
    if(q < P->len) q++;
    /* check terminator */
    long t = ls;
    if(squiggly || dash) while(t<le && is_space(P->src[t])) t++;
    if(le - t == idlen && memcmp(P->src+t, id, idlen)==0){
      end_of_heredoc = q; break;
    }
    if(ls >= P->len){ end_of_heredoc = P->len; break; }
    rb_str_cat(raw, P->src+ls, le-ls);
    rb_str_cat(raw, "\n", 1);
    if(squiggly){
      long ind=0; long k=ls;
      while(k<le && (P->src[k]==' '||P->src[k]=='\t')){ ind++; k++; }
      if(k<le && ind < min_indent) min_indent = (int)ind;
    }
  }
  if(squiggly && min_indent == INT_MAX) min_indent = 0;
  /* de-indent */
  if(squiggly && min_indent > 0){
    VALUE out = rb_str_new("",0);
    const char *s = RSTR(raw)->ptr; long n = RSTR(raw)->len; long i=0;
    while(i<n){
      long j=i; while(j<n && s[j] != '\n') j++;
      long skip = 0; long k=i;
      while(k<j && skip<min_indent && (s[k]==' '||s[k]=='\t')){ k++; skip++; }
      rb_str_cat(out, s+k, j-k);
      if(j<n) rb_str_cat(out,"\n",1);
      i = j+1;
    }
    raw = out;
  }
  /* now lex the collected text as a string body */
  Parser sub = *P;
  Parser *saveP = P;
  static Parser tmp;
  tmp = *P;
  tmp.src = RSTR(raw)->ptr; tmp.len = RSTR(raw)->len; tmp.pos = 0;
  P = &tmp;
  StrAcc a; sacc_init(&a);
  while(P->pos < P->len){
    char c = P->src[P->pos];
    if(interp && c=='\\'){ P->pos++; read_escape(&a,0); continue; }
    if(!interp && c=='\\' && AT(1)=='\\'){ sacc_addchar(&a,'\\'); P->pos+=2; continue; }
    if(interp && c=='#' && AT(1)=='{'){ P->pos+=2; sacc_flush(&a); Node *s2 = read_interp(); a.interp=1; node_add(a.parts,s2); continue; }
    sacc_addchar(&a,c); P->pos++;
  }
  if(a.interp){ sacc_flush(&a); result = a.parts; }
  else { result = node_new(N_STR, saveP->line); result->val = a.buf; }
  P = saveP;
  (void)sub;
  /* splice: remove [body_start, end_of_heredoc) from the source by remembering
     that the rest of the current line continues, then jump. */
  P->heredoc_restore = end_of_heredoc;
  return result;
}

/* ===================== tokenizer ===================== */
static void set_tok(Token *t, int type){ t->type = type; t->line = P->line; }

static int lex_ident(Token *t){
  long s = P->pos;
  while(is_ident_char(CUR)) P->pos++;
  if((CUR=='?'||CUR=='!') && AT(1) != '='){ P->pos++; }
  else if((P->state & EXPR_FNAME) && CUR=='=' && AT(1) != '~' && AT(1) != '>' &&
          (AT(1) != '=' || AT(2)=='>')){
    /* a setter's name includes its `=` (`def foo=(v)`, `alias_method :a=, :b=`) */
    P->pos++;
  }
  else if(CUR=='?'||CUR=='!'){
    if(AT(1)=='=' && AT(2)=='=') P->pos++;   /* foo!== -> foo! == */
  }
  long n = P->pos - s;
  char *word = xmalloc(n+1); memcpy(word,P->src+s,n); word[n]=0;
  /* label?  foo:  (not ::) */
  int allow_label = (P->state & (EXPR_BEG|EXPR_LABEL|EXPR_ARG|EXPR_END|EXPR_ENDFN)) != 0;
  if(rb_target_version >= 20 && CUR==':' && AT(1) != ':' && allow_label && !(P->state & (EXPR_FNAME|EXPR_DOT))){
    P->pos++;
    t->id = rb_intern(word); set_tok(t,tLABEL); P->state = EXPR_BEG|EXPR_LABEL;
    free(word); return 1;
  }
  if(!(P->state & (EXPR_FNAME|EXPR_DOT))){
    for(int i=0;kwtbl[i].name;i++){
      if(strcmp(kwtbl[i].name, word)==0){
        int kw = kwtbl[i].tok;
        int was_beg = (P->state & (EXPR_BEG|EXPR_CLASS|EXPR_VALUE|EXPR_LABEL)) != 0;
        if(kwtbl[i].mod && !was_beg) kw = kwtbl[i].mod;
        if(kw==kDO){
          if(P->cond_nest > 0) kw = kDO_COND;
          else kw = kDO_BLOCK;
        }
        set_tok(t, kw);
        switch(kw){
          case kEND: case kSELF: case kNIL: case kTRUE: case kFALSE: case kRETRY:
          case kREDO: case k__FILE__: case k__LINE__: case k__METHOD__: case k__DIR__:
            P->state = EXPR_END; break;
          case kDEF: P->state = EXPR_FNAME; break;
          case kCLASS: P->state = EXPR_CLASS; break;
          case kRETURN: case kBREAK: case kNEXT: case kRESCUE: case kRESCUE_MOD:
            P->state = EXPR_MID; break;
          /* `yield` and `super` are complete expressions on their own, so the
             newline after them ends the statement -- EXPR_BEG would swallow it
             and the next line would be parsed as their argument list */
          case kYIELD: case kSUPER: P->state = EXPR_ARG; break;
          default: P->state = EXPR_BEG; break;
        }
        free(word); return 1;
      }
    }
  }
  t->id = rb_intern(word);
  int c0 = word[0];
  set_tok(t, (c0>='A'&&c0<='Z') ? tCONST : tIDENT);
  int lvd, lvi;
  int is_local = (c0>='a'&&c0<='z') || c0=='_';
  is_local = is_local && !(P->state & (EXPR_FNAME|EXPR_DOT)) && lv_lookup(P->lvtbl, t->id, &lvd, &lvi);
  if(P->state & (EXPR_FNAME|EXPR_DOT)) P->state = EXPR_ARG;
  else if(is_local) P->state = EXPR_END;
  else if(P->state & (EXPR_BEG|EXPR_MID|EXPR_VALUE|EXPR_CLASS|EXPR_LABEL)) P->state = EXPR_ARG;
  else P->state = EXPR_END;
  free(word);
  return 1;
}

static int lex_number(Token *t){
  long s = P->pos;
  int isfloat = 0;
  if(CUR=='0' && (AT(1)=='x'||AT(1)=='X')){
    P->pos+=2; long v=0; while(hexval(CUR)>=0 || CUR=='_'){ if(CUR!='_') v=v*16+hexval(CUR); P->pos++; }
    t->val = rb_int_new(v); set_tok(t,tINT); P->state=EXPR_END; return 1;
  }
  if(CUR=='0' && (AT(1)=='b'||AT(1)=='B')){
    P->pos+=2; long v=0; while(CUR=='0'||CUR=='1'||CUR=='_'){ if(CUR!='_') v=v*2+(CUR-'0'); P->pos++; }
    t->val = rb_int_new(v); set_tok(t,tINT); P->state=EXPR_END; return 1;
  }
  if(CUR=='0' && (AT(1)=='o'||AT(1)=='O')){
    P->pos+=2; long v=0; while((CUR>='0'&&CUR<='7')||CUR=='_'){ if(CUR!='_') v=v*8+(CUR-'0'); P->pos++; }
    t->val = rb_int_new(v); set_tok(t,tINT); P->state=EXPR_END; return 1;
  }
  if(CUR=='0' && AT(1)>='0' && AT(1)<='7'){
    P->pos++; long v=0; while((CUR>='0'&&CUR<='7')||CUR=='_'){ if(CUR!='_') v=v*8+(CUR-'0'); P->pos++; }
    t->val = rb_int_new(v); set_tok(t,tINT); P->state=EXPR_END; return 1;
  }
  while(is_digit(CUR)||CUR=='_') P->pos++;
  if(CUR=='.' && is_digit(AT(1))){ isfloat=1; P->pos++; while(is_digit(CUR)||CUR=='_') P->pos++; }
  if((CUR=='e'||CUR=='E') && (is_digit(AT(1)) || ((AT(1)=='-'||AT(1)=='+') && is_digit(AT(2))))){
    isfloat=1; P->pos++; if(CUR=='-'||CUR=='+') P->pos++; while(is_digit(CUR)) P->pos++;
  }
  long n = P->pos - s;
  char *buf = xmalloc(n+1); long k=0;
  for(long i=0;i<n;i++) if(P->src[s+i] != '_') buf[k++]=P->src[s+i];
  buf[k]=0;
  if(isfloat){ t->val = rb_float_new(strtod(buf,NULL)); set_tok(t,tFLOAT); }
  else {
    
    char *endp; long long v = strtoll(buf,&endp,10);
    if(k > 18){ t->val = rb_big_new_str(buf,10); }
    else t->val = rb_int_new(v);
    set_tok(t,tINT);
  }
  free(buf); P->state = EXPR_END; return 1;
}

static int op_token(Token *t, const char *op, int type){
  t->id = rb_intern(op); set_tok(t,type); return 1;
}

/* method-name-ish operator after def/'.' */
static int lex_fname_op(Token *t){
  static const char *ops[] = {"[]=","[]","<=>","===","==","=~","!~","!=","<<",">>","<=",">=","**","+@","-@","!","~","+","-","*","/","%","<",">","&","|","^",NULL};
  for(int i=0;ops[i];i++){
    long n = strlen(ops[i]);
    if(P->pos+n<=P->len && memcmp(P->src+P->pos,ops[i],n)==0){
      P->pos+=n; t->id = rb_intern(ops[i]); set_tok(t,tIDENT); P->state=EXPR_ENDFN; return 1;
    }
  }
  return 0;
}

static int percent_literal(Token *t){
  char kind = CUR;
  int type = 'Q';
  if(is_ident_char(kind) && !is_digit(kind)){ type = kind; P->pos++; }
  int open = CUR; P->pos++;
  int term = closing_for(open);
  switch(type){
    case 'w': case 'i': case 'W': case 'I': {
      if((type=='i'||type=='I') && rb_target_version < 20) rb_syntax_error("unknown type of %%string");
      Node *arr = nd(type=='w'||type=='W' ? N_ARRAY : N_ARRAY);
      int sym = (type=='i'||type=='I');
      StrAcc a; sacc_init(&a);
      int nest=0; int any=0;
      while(1){
        if(P->pos>=P->len) rb_syntax_error("unterminated list");
        char c = P->src[P->pos];
        if(c=='\\'){ P->pos++; char e=CUR; P->pos++; if(e=='\n'){P->line++;} else sacc_addchar(&a,e); any=1; continue; }
        if(open!=term && c==(char)open){ nest++; sacc_addchar(&a,c); P->pos++; any=1; continue; }
        if(c==(char)term){ if(nest>0){nest--; sacc_addchar(&a,c); P->pos++; any=1; continue;} P->pos++; break; }
        if(is_space(c) || c=='\n'){
          if(c=='\n') P->line++;
          P->pos++;
          if(any){ Node *e = node_new(sym?N_SYM:N_STR,P->line);
            if(sym) e->id = rb_intern2(RSTR(a.buf)->ptr, RSTR(a.buf)->len); else e->val = a.buf;
            node_add(arr,e); sacc_init(&a); any=0; }
          continue;
        }
        sacc_addchar(&a,c); P->pos++; any=1;
      }
      if(any){ Node *e = node_new(sym?N_SYM:N_STR,P->line);
        if(sym) e->id = rb_intern2(RSTR(a.buf)->ptr, RSTR(a.buf)->len); else e->val = a.buf;
        node_add(arr,e); }
      t->node = arr; set_tok(t,tSTR); t->type = tSTR; t->node->type = N_ARRAY;
      P->state=EXPR_END; t->type = tSTR; t->id = -1;
      /* mark as pre-built node */
      t->type = tSTR; t->val = 0;
      set_tok(t, tSTR); t->node = arr; t->id = (ID)-2;
      return 1; }
    case 'q': t->node = scan_string_body(term,open,0,0); set_tok(t,tSTR); P->state=EXPR_END; return 1;
    case 'Q': t->node = scan_string_body(term,open,1,0); set_tok(t,tSTR); P->state=EXPR_END; return 1;
    case 's': { Node *s = scan_string_body(term,open,0,0);
      Node *sym = nd(N_SYM); sym->id = rb_intern2(RSTR(s->val)->ptr, RSTR(s->val)->len);
      t->node = sym; set_tok(t,tSTR); P->state=EXPR_END; return 1; }
    case 'r': { Node *s = scan_string_body(term,open,1,1);
      int opts=0;
      while(CUR=='i'||CUR=='m'||CUR=='x'||CUR=='o'||CUR=='u'||CUR=='n'||CUR=='e'||CUR=='s'){
        if(CUR=='i')opts|=RE_IGNORECASE; if(CUR=='m')opts|=RE_MULTILINE; if(CUR=='x')opts|=RE_EXTENDED; P->pos++; }
      Node *r = nd(s->type==N_STR ? N_REGX : N_DREGX);
      if(s->type==N_STR) r->val = s->val; else { r->a = s; }
      r->num = opts; t->node = r; set_tok(t,tSTR); P->state=EXPR_END; return 1; }
    case 'x': { Node *s = scan_string_body(term,open,1,0); t->node = s; set_tok(t,tSTR); P->state=EXPR_END; return 1; }
    default: rb_syntax_error("unknown %% literal");
  }
  return 0;
}

static void scan_magic_comments(void){
  /* look at the first two lines for magic comments */
  long p = 0; int lines = 0;
  while(p < P->len && lines < 3){
    long s = p;
    while(p < P->len && P->src[p] != '\n') p++;
    long e = p; if(p<P->len) p++;
    lines++;
    if(e-s > 0 && P->src[s]=='#'){
      char buf[256]; long n = e-s < 255 ? e-s : 255;
      memcpy(buf,P->src+s,n); buf[n]=0;
      char *m = strstr(buf, "frozen_string_literal:");
      if(m){ m += strlen("frozen_string_literal:"); while(*m==' ')m++;
        P->fsl = (strncmp(m,"true",4)==0) ? 1 : 0; }
      char *ec = strstr(buf, "encoding:");
      if(!ec) ec = strstr(buf, "coding:");
      if(ec){ ec = strchr(ec,':')+1; while(*ec==' ')ec++;
        char nm[64]; int k=0; while(k<63 && (isalnum((unsigned char)ec[k])||ec[k]=='-'||ec[k]=='_')){ nm[k]=ec[k]; k++; }
        nm[k]=0; rb_encoding *en = rb_enc_find(nm); if(en) P->enc = en; }
    } else if(e-s > 0 && !(s==0 && P->src[s]=='#')) break;
  }
}

static void next_token(void){
  if(P->has_ahead){ P->tok = P->ahead; P->has_ahead = 0; P->state = P->ahead_state; return; }
  Token *t = &P->tok;
  t->spc = 0; t->node = NULL; t->val = 0; t->id = 0; t->nlbefore = 0;
 again:
  while(P->pos < P->len){
    char c = P->src[P->pos];
    if(c==' '||c=='\t'||c=='\r'||c=='\f'||c=='\v'){ P->pos++; t->spc=1; continue; }
    if(c=='\\' && AT(1)=='\n'){ P->pos+=2; P->line++; t->spc=1; continue; }
    if(c=='#'){ while(P->pos<P->len && P->src[P->pos] != '\n') P->pos++; continue; }
    break;
  }
  if(P->pos >= P->len){ set_tok(t,tEOF); return; }
  char c = P->src[P->pos];

  if(c=='\n'){
    P->pos++; P->line++;
    if(P->heredoc_restore){ P->pos = P->heredoc_restore; P->heredoc_restore = 0; }
    if(P->state & (EXPR_BEG|EXPR_FNAME|EXPR_DOT|EXPR_CLASS|EXPR_VALUE|EXPR_LABEL)){ t->spc=1; goto again; }
    /* lookahead: a leading '.' or '&.' continues the expression */
    long q = P->pos;
    while(q<P->len && (is_space(P->src[q])||P->src[q]=='\n')) q++;
    if(q<P->len && ((P->src[q]=='.' && (q+1>=P->len || P->src[q+1] != '.')) || (P->src[q]=='&' && q+1<P->len && P->src[q+1]=='.'))){
      P->pos = q; goto again;
    }
    set_tok(t,tNL); P->state = EXPR_BEG; return;
  }
  if(P->pos+7 <= P->len && memcmp(P->src+P->pos,"__END__",7)==0 &&
     (P->pos==0 || P->src[P->pos-1]=='\n') && (P->pos+7==P->len || P->src[P->pos+7]=='\n')){
    P->pos = P->len; set_tok(t,tEOF); return;
  }

  int state = P->state;
  int beg = (state & (EXPR_BEG|EXPR_MID|EXPR_CLASS|EXPR_VALUE|EXPR_LABEL)) != 0;
  int arg = (state & EXPR_ARG) != 0;
  int fname = (state & (EXPR_FNAME|EXPR_DOT)) != 0;
  int spc = t->spc;

  if(fname){
    if(lex_fname_op(t)) return;
    if(c=='`'){ P->pos++; t->id=ID_CACHED("`"); set_tok(t,tIDENT); P->state=EXPR_ENDFN; return; }
  }

  if(is_digit(c)) return (void)lex_number(t);
  if(is_ident_start(c)) return (void)lex_ident(t);

  switch(c){
    case '"': P->pos++; t->node = scan_string_body('"','"',1,0); set_tok(t,tSTR); P->state=EXPR_END; return;
    case '\'': P->pos++; t->node = scan_string_body('\'','\'',0,0); set_tok(t,tSTR); P->state=EXPR_END; return;
    case '`': P->pos++; t->node = scan_string_body('`','`',1,0); set_tok(t,tSTR); P->state=EXPR_END; return;
    case '%':
      if(beg || (arg && spc && !is_space(AT(1)) && AT(1) != '=')){
        char nx = AT(1);
        if(!is_ident_char(nx) || (nx>='a'&&nx<='z') || (nx>='A'&&nx<='Z')){
          if(!is_digit(nx)){ P->pos++; return (void)percent_literal(t); }
        }
      }
      P->pos++;
      if(CUR=='='){ P->pos++; op_token(t,"%",tOPASGN); P->state=EXPR_BEG; return; }
      op_token(t,"%",tOP); P->state=EXPR_BEG; return;
    case '/':
      if(beg || (arg && spc && !is_space(AT(1)))){
        P->pos++;
        Node *s = scan_string_body('/','/',1,1);
        int opts=0;
        while(CUR=='i'||CUR=='m'||CUR=='x'||CUR=='o'||CUR=='u'||CUR=='n'||CUR=='e'||CUR=='s'){
          if(CUR=='i')opts|=RE_IGNORECASE; if(CUR=='m')opts|=RE_MULTILINE; if(CUR=='x')opts|=RE_EXTENDED; P->pos++; }
        Node *r = nd(s->type==N_STR ? N_REGX : N_DREGX);
        if(s->type==N_STR) r->val = s->val; else r->a = s;
        r->num = opts; t->node = r; set_tok(t,tSTR); P->state=EXPR_END; return;
      }
      P->pos++;
      if(CUR=='='){ P->pos++; op_token(t,"/",tOPASGN); P->state=EXPR_BEG; return; }
      op_token(t,"/",tOP); P->state=EXPR_BEG; return;
    case '<':
      if(AT(1)=='<' && (beg || (arg && spc) || (state&EXPR_END)==0)){
        /* heredoc? <<~ID <<-ID <<ID <<"ID" <<'ID' */
        long save = P->pos;
        P->pos += 2;
        int squig=0, dash=0;
        if(CUR=='~'){ if(rb_target_version < 27) rb_syntax_error("unexpected ~"); squig=1; P->pos++; }
        else if(CUR=='-'){ dash=1; P->pos++; }
        int interp = 1; char q = 0;
        if(CUR=='"'||CUR=='\''||CUR=='`'){ q=CUR; interp = (q != '\''); P->pos++; }
        if(is_ident_start(CUR) || (q && CUR)){
          long s0 = P->pos;
          if(q){ while(P->pos<P->len && P->src[P->pos] != q) P->pos++; }
          else { while(is_ident_char(CUR)) P->pos++; }
          long n = P->pos - s0;
          if(n>0){
            char *id = xmalloc(n+1); memcpy(id,P->src+s0,n); id[n]=0;
            if(q) P->pos++;
            t->node = read_heredoc(id, squig, dash, interp);
            set_tok(t,tSTR); P->state=EXPR_END; return;
          }
        }
        P->pos = save;
      }
      P->pos++;
      if(CUR=='<'){ P->pos++; if(CUR=='='){P->pos++; op_token(t,"<<",tOPASGN);} else op_token(t,"<<",tOP); P->state=EXPR_BEG; return; }
      if(CUR=='='){ P->pos++; if(CUR=='>'){ P->pos++; op_token(t,"<=>",tOP); } else op_token(t,"<=",tOP); P->state=EXPR_BEG; return; }
      op_token(t,"<",tOP); P->state=EXPR_BEG; return;
    case '>':
      P->pos++;
      if(CUR=='>'){ P->pos++; if(CUR=='='){P->pos++; op_token(t,">>",tOPASGN);} else op_token(t,">>",tOP); P->state=EXPR_BEG; return; }
      if(CUR=='='){ P->pos++; op_token(t,">=",tOP); P->state=EXPR_BEG; return; }
      op_token(t,">",tOP); P->state=EXPR_BEG; return;
    case '=':
      P->pos++;
      if(CUR=='='){ P->pos++; if(CUR=='='){P->pos++; op_token(t,"===",tOP);} else op_token(t,"==",tOP); P->state=EXPR_BEG; return; }
      if(CUR=='~'){ P->pos++; op_token(t,"=~",tMATCH); P->state=EXPR_BEG; return; }
      if(CUR=='>'){ P->pos++; set_tok(t,tASSOC); P->state=EXPR_BEG; return; }
      if(CUR=='b' && P->pos+5<=P->len && memcmp(P->src+P->pos,"begin",5)==0 && (P->pos==1 || P->src[P->pos-2]=='\n')){
        /* =begin ... =end block comment */
        while(P->pos<P->len){
          if(P->src[P->pos]=='\n'){ P->line++;
            if(P->pos+5 <= P->len && memcmp(P->src+P->pos+1,"=end",4)==0){
              P->pos++; while(P->pos<P->len && P->src[P->pos] != '\n') P->pos++; break; } }
          P->pos++; }
        goto again;
      }
      set_tok(t,tASGN); P->state=EXPR_BEG; return;
    case '!':
      P->pos++;
      if(CUR=='='){ P->pos++; op_token(t,"!=",tOP); P->state=EXPR_BEG; return; }
      if(CUR=='~'){ P->pos++; op_token(t,"!~",tNMATCH); P->state=EXPR_BEG; return; }
      set_tok(t,tBANG); P->state=EXPR_BEG; return;
    case '~': P->pos++; set_tok(t,tTILDE); P->state=EXPR_BEG; return;
    case '+': case '-': {
      char op = c; P->pos++;
      if(fname && CUR=='@'){ P->pos++; t->id = rb_intern(op=='+'?"+@":"-@"); set_tok(t,tIDENT); P->state=EXPR_ENDFN; return; }
      if(CUR=='='){ P->pos++; op_token(t, op=='+'?"+":"-", tOPASGN); P->state=EXPR_BEG; return; }
      if(op=='-' && CUR=='>' && rb_target_version >= 20){ P->pos++; set_tok(t,tARROW); P->state=EXPR_BEG; return; }
      if(beg || (arg && spc && !is_space(CUR))){
        set_tok(t, op=='+'?tUPLUS:tUMINUS); P->state=EXPR_BEG; return; }
      op_token(t, op=='+'?"+":"-", tOP); P->state=EXPR_BEG; return; }
    case '*':
      P->pos++;
      if(CUR=='*'){ P->pos++;
        if(CUR=='='){ P->pos++; op_token(t,"**",tOPASGN); P->state=EXPR_BEG; return; }
        if(beg || (arg && spc && !is_space(CUR))){ set_tok(t,tDSTAR); P->state=EXPR_BEG; return; }
        op_token(t,"**",tPOW); P->state=EXPR_BEG; return; }
      if(CUR=='='){ P->pos++; op_token(t,"*",tOPASGN); P->state=EXPR_BEG; return; }
      if(beg || (arg && spc && !is_space(CUR))){ set_tok(t,tSTAR); P->state=EXPR_BEG; return; }
      op_token(t,"*",tOP); P->state=EXPR_BEG; return;
    case '&':
      P->pos++;
      if(CUR=='&'){ P->pos++; if(CUR=='='){P->pos++; op_token(t,"&&",tOPASGN);} else set_tok(t,tANDOP); P->state=EXPR_BEG; return; }
      if(CUR=='.' && rb_target_version >= 27){ P->pos++; set_tok(t,tSAFEDOT); P->state=EXPR_DOT; return; }
      if(CUR=='='){ P->pos++; op_token(t,"&",tOPASGN); P->state=EXPR_BEG; return; }
      if(beg || (arg && spc && !is_space(CUR))){ set_tok(t,tAMPER); P->state=EXPR_BEG; return; }
      op_token(t,"&",tOP); P->state=EXPR_BEG; return;
    case '|':
      P->pos++;
      if(CUR=='|'){ P->pos++; if(CUR=='='){P->pos++; op_token(t,"||",tOPASGN);} else set_tok(t,tOROP); P->state=EXPR_BEG; return; }
      if(CUR=='='){ P->pos++; op_token(t,"|",tOPASGN); P->state=EXPR_BEG; return; }
      op_token(t,"|",tOP); P->state=EXPR_BEG; return;
    case '^':
      P->pos++;
      if(CUR=='='){ P->pos++; op_token(t,"^",tOPASGN); P->state=EXPR_BEG; return; }
      op_token(t,"^",tOP); P->state=EXPR_BEG; return;
    case '(':
      P->pos++;
      set_tok(t, (beg || (arg && spc)) ? tLPAREN : tLPAREN_ARG);
      if(beg) t->type = tLPAREN;
      P->paren_nest++; P->state = EXPR_BEG; return;
    case ')': P->pos++; set_tok(t,tRPAREN); P->paren_nest--; P->state = EXPR_END; return;
    case '[':
      P->pos++;
      if(fname){ if(CUR==']'){ P->pos++; if(CUR=='='){P->pos++; t->id=ID_CACHED("[]="); } else t->id=ID_CACHED("[]");
          set_tok(t,tIDENT); P->state=EXPR_ENDFN; return; } }
      set_tok(t, (beg || (arg && spc)) ? tLBRACK : tLBRACK_ARG);
      P->state = EXPR_BEG; return;
    case ']': P->pos++; set_tok(t,tRBRACK); P->state = EXPR_END; return;
    case '{':
      P->pos++;
      set_tok(t, beg ? tLBRACE : tLBRACE_ARG);
      P->state = EXPR_BEG; return;
    case '}': P->pos++; set_tok(t,tRBRACE); P->state = EXPR_END; return;
    case ',': P->pos++; set_tok(t,tCOMMA); P->state = EXPR_BEG|EXPR_LABEL; return;
    case ';': P->pos++; set_tok(t,tSEMI); P->state = EXPR_BEG; return;
    case '.':
      P->pos++;
      if(CUR=='.'){ P->pos++; if(CUR=='.'){ P->pos++; set_tok(t, beg?tBDOT3:tDOT3); } else set_tok(t, beg?tBDOT2:tDOT2);
        P->state = EXPR_BEG; return; }
      if(CUR=='('){ P->pos++; P->paren_nest++; set_tok(t,tLPAREN_ARG); t->id = ID_CACHED("call"); t->type = tDOT;
        /* handled by caller: '.(' */ P->pos--; P->paren_nest--; set_tok(t,tDOT); P->state = EXPR_DOT; return; }
      set_tok(t,tDOT); P->state = EXPR_DOT; return;
    case ':':
      P->pos++;
      if(CUR==':'){ P->pos++; set_tok(t, beg ? tCOLON3 : tCOLON2); P->state = EXPR_BEG; return; }
      if(CUR=='"'){ P->pos++; Node *s = scan_string_body('"','"',1,0);
        if(s->type==N_STR){ Node *sym = nd(N_SYM); sym->id = rb_intern2(RSTR(s->val)->ptr,RSTR(s->val)->len); t->node = sym; }
        else { Node *d = nd(N_DSYM); d->a = s; t->node = d; }
        set_tok(t,tSTR); P->state = EXPR_END; return; }
      if(CUR=='\''){ P->pos++; Node *s = scan_string_body('\'','\'',0,0);
        Node *sym = nd(N_SYM); sym->id = rb_intern2(RSTR(s->val)->ptr,RSTR(s->val)->len); t->node = sym;
        set_tok(t,tSTR); P->state = EXPR_END; return; }
      if(is_ident_start(CUR) || CUR=='@' || CUR=='$'){
        long s0 = P->pos;
        if(CUR=='@'){ P->pos++; if(CUR=='@') P->pos++; }
        else if(CUR=='$') P->pos++;
        while(is_ident_char(CUR)) P->pos++;
        if(CUR=='?'||CUR=='!'||(CUR=='=' && AT(1) != '=' && AT(1) != '>' && AT(1) != '~')) P->pos++;
        Node *sym = nd(N_SYM); sym->id = rb_intern2(P->src+s0, P->pos-s0);
        t->node = sym; set_tok(t,tSTR); P->state=EXPR_END; return;
      }
      { int save = P->state; P->state = EXPR_FNAME;
        Token tmp; tmp.spc=0;
        if(lex_fname_op(&tmp)){ Node *sym = nd(N_SYM); sym->id = tmp.id; t->node = sym;
          set_tok(t,tSTR); P->state = EXPR_END; return; }
        P->state = save; }
      set_tok(t,tCOLON); P->state = EXPR_BEG; return;
    case '?': {
      P->pos++;
      if(beg || (arg && spc)){
        char nx = CUR;
        if(nx && !is_space(nx) && nx != '\n' && !(is_ident_char(nx) && is_ident_char(AT(1)))){
          StrAcc a; sacc_init(&a);
          if(nx=='\\'){ P->pos++; read_escape(&a,0); }
          else {
            long cl = rb_enc_mbclen(P->enc, P->src+P->pos, P->src+P->len);
            sacc_addstr(&a, P->src+P->pos, cl); P->pos += cl;
          }
          Node *s = nd(N_STR); s->val = a.buf; t->node = s; set_tok(t,tSTR); P->state=EXPR_END; return;
        }
      }
      set_tok(t,tQUESTION); P->state = EXPR_BEG; return; }
    case '@': {
      P->pos++;
      int cv = 0;
      if(CUR=='@'){ cv=1; P->pos++; }
      long s0 = P->pos;
      while(is_ident_char(CUR)) P->pos++;
      t->id = rb_intern2(P->src+s0, P->pos-s0);
      set_tok(t, cv?tCVAR:tIVAR); P->state = EXPR_END; return; }
    case '$': {
      P->pos++;
      if(is_digit(CUR)){ long v=0; while(is_digit(CUR)){ v=v*10+(CUR-'0'); P->pos++; }
        t->id = (ID)v; set_tok(t,tNTHREF); P->state=EXPR_END; return; }
      if(CUR=='~'||CUR=='&'||CUR=='`'||CUR=='\''||CUR=='+'||CUR=='!'||CUR=='@'||CUR=='/'||
         CUR=='\\'||CUR==','||CUR==';'||CUR=='.'||CUR=='<'||CUR=='>'||CUR=='_'||CUR=='*'||
         CUR=='$'||CUR=='?'||CUR==':'||CUR=='"'||CUR=='0'){
        if(CUR=='_' && is_ident_char(AT(1))){ /* fallthrough to ident */ }
        else { char nm[3] = { '$', CUR, 0 }; P->pos++; t->id = rb_intern(nm); set_tok(t,tGVAR); P->state=EXPR_END; return; }
      }
      long s0 = P->pos - 1;
      while(is_ident_char(CUR)) P->pos++;
      t->id = rb_intern2(P->src+s0, P->pos-s0);
      set_tok(t,tGVAR); P->state = EXPR_END; return; }
  }
  rb_syntax_error("unexpected character '%c'", c);
}

/* Look one token ahead WITHOUT letting the lexer state advance: the state the
   lookahead produced is replayed when that token is finally consumed. */
static void peek_token(void){
  if(P->has_ahead) return;
  Token save = P->tok;
  int save_state = P->state;
  next_token();
  P->ahead = P->tok; P->has_ahead = 1;
  P->ahead_state = P->state;
  P->state = save_state;
  P->tok = save;
}

/* ===================== parser helpers ===================== */
#define TOK (P->tok.type)
static int accept(int type){ if(TOK==type){ next_token(); return 1; } return 0; }
static void expect(int type, const char *what){
  if(TOK != type) rb_syntax_error("unexpected token, expecting %s", what);
  next_token();
}
static void skip_nl(void){ while(TOK==tNL||TOK==tSEMI) next_token(); }
static void skip_nl_only(void){ while(TOK==tNL) next_token(); }

static int tok_is_op(const char *s){ return TOK==tOP && P->tok.id == rb_intern(s); }

static int tok_starts_arg(void){
  switch(TOK){
    case tINT: case tFLOAT: case tSTR: case tIDENT: case tCONST: case tIVAR:
    case tGVAR: case tCVAR: case tLBRACK: case tLPAREN: case tSTAR: case tDSTAR:
    case tAMPER: case tUMINUS: case tUPLUS: case tBANG: case tTILDE: case tARROW:
    case tCOLON3: case tLABEL: case tNTHREF: case tBACKREF: case tLBRACE:
    case kNIL: case kTRUE: case kFALSE: case kSELF: case kDEFINED: case kNOT:
    case kYIELD: case kSUPER: case kCASE: case kIF: case kUNLESS: case kBEGIN:
    case kLAST: return 1;
    /* `private def foo…end` — a method definition as a command argument,
       which became useful once Ruby 2.1 made `def` return a Symbol. */
    case kDEF: return rb_target_version >= 27;
    default: return 0;
  }
}

static Node *parse_expr_stmt(void);
static Node *parse_body_until(int *terms, int nterms);
static Node *parse_def_params(int paren);
static Node *parse_block(Node *call);
static Node *parse_args_paren(Node *call);
static void parse_command_args(Node *call);
static Node *new_call(Node *recv, ID mid, int line);
static Node *parse_primary_postfix(Node *n);
static Node *parse_bodystmt(void);

static Node *new_call(Node *recv, ID mid, int line){
  Node *n = node_new(N_CALL, line); n->a = recv; n->id = mid; return n;
}
static Node *lit_nil(void){ return nd(N_NIL); }

/* variable reference or method call */
static Node *var_ref(ID id, int line){
  int d,i;
  if(lv_lookup(P->lvtbl, id, &d, &i)){
    Node *n = node_new(N_LVAR,line); n->id=id; n->num=d; n->flags=i; return n;
  }
  const char *nm = rb_id2name(id);
  if(rb_target_version >= 27 && nm[0]=='_' && nm[1]>='1' && nm[1]<='9' && nm[2]==0){
    Node *n = node_new(N_NUMPARAM,line); n->num = nm[1]-'0';
    if(P->lvtbl->numparam_max < n->num) P->lvtbl->numparam_max = n->num;
    return n;
  }
  /* `it`: Ruby 3.4 binds the first block parameter; 3.3 warns that it will. */
  if(strcmp(nm,"it")==0 && P->lvtbl->is_block && P->lvtbl->n==0 && P->lvtbl->numparam_max==0){
    if(rb_target_version >= 34){ P->lvtbl->uses_it = 1; return node_new(N_ITPARAM,line); }
    if(rb_target_version == 33){ Node *n = node_new(N_VCALL,line); n->id=id; n->flags=1; return n; }
  }
  Node *n = node_new(N_VCALL,line); n->id=id; return n;
}

static Node *asgn_node(Node *lhs, Node *val);

/* build an assignment node for an lvalue */
static Node *asgn_node(Node *lhs, Node *val){
  Node *n;
  switch(lhs->type){
    case N_LVAR: n = node_new(N_LASGN,lhs->line); n->id=lhs->id; n->num=lhs->num; n->flags=lhs->flags; n->a=val; return n;
    case N_VCALL: { int d,i; lv_declare(lhs->id,&d,&i);
      n = node_new(N_LASGN,lhs->line); n->id=lhs->id; n->num=d; n->flags=i; n->a=val; return n; }
    case N_IVAR: n = node_new(N_IASGN,lhs->line); n->id=lhs->id; n->a=val; return n;
    case N_GVAR: n = node_new(N_GASGN,lhs->line); n->id=lhs->id; n->a=val; return n;
    case N_CVAR: n = node_new(N_CVASGN,lhs->line); n->id=lhs->id; n->a=val; return n;
    case N_CONST: n = node_new(N_CDECL,lhs->line); n->id=lhs->id; n->a=val; return n;
    case N_COLON2: n = node_new(N_CDECL,lhs->line); n->id=lhs->id; n->b=lhs->a; n->a=val; return n;
    case N_CALL:
      if(lhs->id == ID_CACHED("[]")){
        n = node_new(N_IDXASGN,lhs->line); n->a=lhs->a; n->items=lhs->items; n->nitems=lhs->nitems;
        n->b=val; n->c = lhs->c; n->d = lhs->d; return n;
      } else {
        n = node_new(N_ATTRASGN,lhs->line); n->a=lhs->a;
        char buf[128]; snprintf(buf,sizeof buf,"%s=",rb_id2name(lhs->id));
        n->id = rb_intern(buf); n->b=val; n->num = lhs->num; return n;
      }
    case N_SPLAT: { Node *in = asgn_node(lhs->a, val); Node *s = node_new(N_SPLAT,lhs->line); s->a=in; return s; }
    case N_MASGN: { lhs->b = val; return lhs; }
    default: rb_syntax_error("invalid assignment target");
  }
  return NULL;
}

/* ===================== arguments ===================== */
static void args_add(Node *call, Node *arg){ node_add(call, arg); }

/* parse one argument (may be splat / kwsplat / label / block-pass) */
static int parse_one_arg(Node *call, Node **kwhash){
  if(TOK==tAMPER){
    next_token();
    if(TOK==tRPAREN || TOK==tCOMMA || TOK==tNL || TOK==tEOF){
      if(rb_target_version < 31) rb_syntax_error("no anonymous block parameter");
      Node *bp = nd(N_ANONBLOCK); call->c = bp; return 1;
    }
    call->c = parse_arg(); return 1;
  }
  if(TOK==tSTAR){
    next_token();
    Node *s = nd(N_SPLAT);
    if(TOK==tRPAREN||TOK==tCOMMA||TOK==tNL||TOK==tRBRACK){
      if(rb_target_version < 33) rb_syntax_error("no anonymous rest parameter");
      s->type = N_ANONSPLAT; }
    else s->a = parse_arg();
    args_add(call,s); return 1;
  }
  if(TOK==tDSTAR){
    if(rb_target_version < 20) rb_syntax_error("unexpected **");
    next_token();
    if(!*kwhash) *kwhash = nd(N_HASH);
    Node *ks = nd(N_KWSPLAT);
    if(TOK==tRPAREN||TOK==tCOMMA||TOK==tNL){
      if(rb_target_version < 33) rb_syntax_error("no anonymous keyword rest parameter");
      ks->type = N_ANONKWSPLAT; }
    else ks->a = parse_arg();
    node_add(*kwhash, ks); node_add(*kwhash, NULL);
    return 1;
  }
  if(TOK==tLABEL){
    ID lid = P->tok.id; int line = P->tok.line;
    next_token();
    if(!*kwhash) *kwhash = node_new(N_HASH,line);
    Node *k = node_new(N_SYM,line); k->id = lid;
    Node *v;
    if(TOK==tCOMMA||TOK==tRPAREN||TOK==tNL||TOK==tRBRACE||TOK==tEOF||TOK==tSEMI){
      if(rb_target_version < 31) rb_syntax_error("expected an expression after the label");
      v = var_ref(lid, line);       /* 3.1 hash-literal value omission */
    } else v = parse_arg();
    node_add(*kwhash,k); node_add(*kwhash,v);
    return 1;
  }
  if(TOK==tSTR && P->tok.node && (P->tok.node->type==N_STR||P->tok.node->type==N_DSTR)){
    /* "str": value  label */
    Node *sn = P->tok.node;
    peek_token();
    if(P->ahead.type==tCOLON && !P->ahead.spc){
      if(rb_target_version < 27) rb_syntax_error("unexpected ':'");
      next_token(); next_token();
      if(!*kwhash) *kwhash = nd(N_HASH);
      Node *k;
      if(sn->type==N_STR){ k = nd(N_SYM); k->id = rb_intern2(RSTR(sn->val)->ptr,RSTR(sn->val)->len); }
      else { k = nd(N_DSYM); k->a = sn; }
      node_add(*kwhash,k); node_add(*kwhash,parse_arg());
      return 1;
    }
  }
  Node *a = parse_arg();
  if(TOK==tASSOC){
    next_token();
    if(!*kwhash) *kwhash = nd(N_HASH);
    node_add(*kwhash,a); node_add(*kwhash,parse_arg());
    return 1;
  }
  args_add(call,a);
  return 1;
}

static Node *parse_args_paren(Node *call){
  Node *kwhash = NULL;
  skip_nl_only();
  if(TOK != tRPAREN){
    while(1){
      skip_nl_only();
      parse_one_arg(call,&kwhash);
      skip_nl_only();
      if(!accept(tCOMMA)) break;
      skip_nl_only();
      if(TOK==tRPAREN) break;
    }
  }
  skip_nl_only();
  expect(tRPAREN,")");
  call->d = kwhash;
  return call;
}
static void parse_command_args(Node *call){
  Node *kwhash = NULL;
  while(1){
    parse_one_arg(call,&kwhash);
    if(!accept(tCOMMA)) break;
    skip_nl_only();
  }
  call->d = kwhash;
}

/* ===================== blocks ===================== */
static Node *parse_block_params(void){
  Node *args = nd(N_ARGS);
  if(TOK != tOP || P->tok.id != ID_CACHED("|")) return NULL;
  next_token();
  if(TOK==tOROP){ next_token(); return args; }   /* || : no params */
  int post = 0;
  while(1){
    if(tok_is_op("|")){ break; }
    if(TOK==tCOMMA){ next_token(); continue; }
    if(TOK==tSTAR){ next_token(); Node *e = nd(N_ARGENT); e->num=PARAM_REST;
      if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i); e->id=P->tok.id; e->flags=i|(d<<20); next_token(); } else e->id=-1;
      node_add(args,e); post=1; continue; }
    if(TOK==tDSTAR){ next_token(); Node *e = nd(N_ARGENT); e->num=PARAM_KEYREST;
      if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i); e->id=P->tok.id; e->flags=i|(d<<20); next_token(); } else e->id=-1;
      node_add(args,e); continue; }
    if(TOK==tAMPER){ next_token(); Node *e = nd(N_ARGENT); e->num=PARAM_BLOCK;
      if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i); e->id=P->tok.id; e->flags=i|(d<<20); next_token(); } else e->id=-1;
      node_add(args,e); continue; }
    if(TOK==tLPAREN || TOK==tLPAREN_ARG){
      next_token();
      Node *e = nd(N_ARGENT); e->num=PARAM_DESTRUCT; e->a = nd(N_ARGS);
      while(TOK != tRPAREN){
        if(TOK==tSTAR){ next_token(); Node *s = nd(N_ARGENT); s->num=PARAM_REST;
          if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i); s->id=P->tok.id; s->flags=i; next_token(); } else s->id=-1;
          node_add(e->a,s); }
        else if(TOK==tLPAREN||TOK==tLPAREN_ARG){
          /* nested destructure */
          Node *saved = P->tok.node; (void)saved;
          next_token();
          Node *s = nd(N_ARGENT); s->num=PARAM_DESTRUCT; s->a = nd(N_ARGS);
          while(TOK != tRPAREN){
            if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i);
              Node *q = nd(N_ARGENT); q->num=PARAM_REQ; q->id=P->tok.id; q->flags=i; node_add(s->a,q); next_token(); }
            else if(TOK==tSTAR){ next_token(); Node *q=nd(N_ARGENT); q->num=PARAM_REST;
              if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i); q->id=P->tok.id; q->flags=i; next_token(); } else q->id=-1;
              node_add(s->a,q); }
            else break;
            if(!accept(tCOMMA)) break;
          }
          expect(tRPAREN,")");
          node_add(e->a,s);
        }
        else if(TOK==tIDENT){ int d,i; lv_declare_param(P->tok.id,&d,&i);
          Node *s = nd(N_ARGENT); s->num=PARAM_REQ; s->id=P->tok.id; s->flags=i; node_add(e->a,s); next_token(); }
        else break;
        if(!accept(tCOMMA)) break;
      }
      expect(tRPAREN,")");
      node_add(args,e); continue;
    }
    if(TOK==tLABEL){
      ID id = P->tok.id; next_token();
      int d,i; lv_declare_param(id,&d,&i);
      Node *e = nd(N_ARGENT); e->id=id; e->flags=i|(d<<20);
      if(TOK==tCOMMA || tok_is_op("|") || TOK==tSEMI){ e->num=PARAM_KEYREQ; }
      else { e->num=PARAM_KEY; e->a = parse_arg(); }
      node_add(args,e); continue;
    }
    if(TOK==tIDENT){
      ID id = P->tok.id; next_token();
      int d,i; lv_declare_param(id,&d,&i);
      Node *e = nd(N_ARGENT); e->id=id; e->flags=i|(d<<20);
      if(TOK==tASGN){ next_token(); e->num=PARAM_OPT; e->a=parse_arg(); }
      else e->num = post ? PARAM_POST : PARAM_REQ;
      node_add(args,e); continue;
    }
    if(TOK==tSEMI){ /* block-local variables */
      next_token();
      while(TOK==tIDENT){ lv_add(P->lvtbl, P->tok.id); next_token(); if(!accept(tCOMMA)) break; }
      break;
    }
    break;
  }
  if(TOK==tSEMI){
    next_token();
    while(TOK==tIDENT){ lv_add(P->lvtbl, P->tok.id); next_token(); if(!accept(tCOMMA)) break; }
  }
  if(!tok_is_op("|")) rb_syntax_error("expecting | in block parameters");
  next_token();
  return args;
}

static Node *parse_block(Node *call){
  int brace = (TOK==tLBRACE_ARG || TOK==tLBRACE);
  next_token();
  LocalTbl *prev = P->lvtbl;
  P->lvtbl = lvtbl_new(prev, 1);
  Node *iter = nd(N_ITER);
  skip_nl_only();
  iter->a = parse_block_params();
  int terms[1] = { brace ? tRBRACE : kEND };
  iter->b = brace ? parse_stmts(terms,1) : parse_bodystmt();
  if(brace) expect(tRBRACE,"}"); else expect(kEND,"end");
  iter->val = (VALUE)P->lvtbl;
  iter->num = P->lvtbl->numparam_max;
  iter->flags = P->lvtbl->uses_it;
  P->lvtbl = prev;
  call->b = iter;
  return call;
}

/* ===================== def params ===================== */
static Node *parse_def_params(int paren){
  Node *args = nd(N_ARGS);
  int post = 0;
  if(paren) skip_nl_only();
  while(1){
    if(paren && TOK==tRPAREN) break;
    if(!paren && (TOK==tNL||TOK==tSEMI||TOK==tEOF)) break;
    if(TOK==tDOT3){ next_token(); Node *e=nd(N_ARGENT); e->num=PARAM_REST; e->id=ID_CACHED("__fwd_rest");
      int d,i; lv_declare(e->id,&d,&i); e->flags=i; node_add(args,e);
      Node *k=nd(N_ARGENT); k->num=PARAM_KEYREST; k->id=ID_CACHED("__fwd_kw"); lv_declare(k->id,&d,&i); k->flags=i; node_add(args,k);
      Node *b=nd(N_ARGENT); b->num=PARAM_BLOCK; b->id=ID_CACHED("__fwd_blk"); lv_declare(b->id,&d,&i); b->flags=i; node_add(args,b);
      args->flags = 1; continue; }
    if(TOK==tSTAR){ next_token(); Node *e=nd(N_ARGENT); e->num=PARAM_REST;
      if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i); e->id=P->tok.id; e->flags=i; next_token(); }
      else { e->id = ID_CACHED("__anon_rest"); int d,i; lv_declare(e->id,&d,&i); e->flags=i; e->num=PARAM_REST; e->line=-1; }
      node_add(args,e); post=1; goto next; }
    if(TOK==tDSTAR){ next_token();
      if(TOK==kNIL){ next_token(); Node *e=nd(N_ARGENT); e->num=PARAM_NOKEY; node_add(args,e); goto next; }
      Node *e=nd(N_ARGENT); e->num=PARAM_KEYREST;
      if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i); e->id=P->tok.id; e->flags=i; next_token(); }
      else { e->id = ID_CACHED("__anon_kw"); int d,i; lv_declare(e->id,&d,&i); e->flags=i; e->line=-1; }
      node_add(args,e); goto next; }
    if(TOK==tAMPER){ next_token(); Node *e=nd(N_ARGENT); e->num=PARAM_BLOCK;
      if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i); e->id=P->tok.id; e->flags=i; next_token(); }
      else { e->id = ID_CACHED("__anon_blk"); int d,i; lv_declare(e->id,&d,&i); e->flags=i; e->line=-1; }
      node_add(args,e); goto next; }
    if(TOK==tLPAREN||TOK==tLPAREN_ARG){
      next_token(); Node *e=nd(N_ARGENT); e->num=PARAM_DESTRUCT; e->a=nd(N_ARGS);
      while(TOK != tRPAREN){
        if(TOK==tSTAR){ next_token(); Node *s=nd(N_ARGENT); s->num=PARAM_REST;
          if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i); s->id=P->tok.id; s->flags=i; next_token(); } else s->id=-1;
          node_add(e->a,s); }
        else if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i);
          Node *s=nd(N_ARGENT); s->num=PARAM_REQ; s->id=P->tok.id; s->flags=i; node_add(e->a,s); next_token(); }
        else break;
        if(!accept(tCOMMA)) break;
      }
      expect(tRPAREN,")"); node_add(args,e); goto next;
    }
    if(TOK==tLABEL){
      ID id=P->tok.id; next_token(); int d,i; lv_declare(id,&d,&i);
      Node *e=nd(N_ARGENT); e->id=id; e->flags=i;
      if(TOK==tCOMMA||(paren&&TOK==tRPAREN)||TOK==tNL||TOK==tSEMI||TOK==tEOF){ e->num=PARAM_KEYREQ; }
      else { e->num=PARAM_KEY; e->a=parse_arg(); }
      node_add(args,e); goto next;
    }
    if(TOK==tIDENT){
      ID id=P->tok.id; next_token(); int d,i; lv_declare(id,&d,&i);
      Node *e=nd(N_ARGENT); e->id=id; e->flags=i;
      if(TOK==tASGN){ next_token(); e->num=PARAM_OPT; e->a=parse_arg(); }
      else e->num = post ? PARAM_POST : PARAM_REQ;
      node_add(args,e); goto next;
    }
    break;
   next:
    if(paren){ skip_nl_only(); if(!accept(tCOMMA)) break; skip_nl_only(); }
    else if(!accept(tCOMMA)) break;
  }
  return args;
}

/* ===================== rescue/ensure body ===================== */
static Node *parse_bodystmt(void){
  int terms[4] = { kEND, kRESCUE, kELSE, kENSURE };
  Node *body = parse_stmts(terms,4);
  Node *result = body;
  if(TOK==kRESCUE){
    Node *resc = nd(N_RESCUE); resc->a = body;
    while(TOK==kRESCUE){
      next_token();
      Node *rb = nd(N_RESBODY);
      rb->c = nd(N_ARRAY);
      while(TOK != tASSOC && TOK != kTHEN && TOK != tNL && TOK != tSEMI && TOK != tRIGHTWARD){
        if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_arg(); node_add(rb->c,s); }
        else node_add(rb->c, parse_arg());
        if(!accept(tCOMMA)) break;
      }
      if(TOK==tRIGHTWARD || TOK==tASSOC){ next_token();
        Node *lhs;
        if(TOK==tIDENT){ lhs = var_ref(P->tok.id,P->tok.line); next_token(); }
        else lhs = parse_primary();
        rb->d = asgn_node(lhs, NULL);
      }
      if(TOK==kTHEN) next_token();
      int t2[4] = { kEND, kRESCUE, kELSE, kENSURE };
      rb->a = parse_stmts(t2,4);
      node_add(resc, rb);
    }
    if(TOK==kELSE){ next_token(); int t3[3]={kEND,kENSURE,kRESCUE}; resc->b = parse_stmts(t3,3); }
    result = resc;
  } else if(TOK==kELSE){
    next_token(); int t3[2]={kEND,kENSURE}; Node *el = parse_stmts(t3,2);
    Node *blk = nd(N_BLOCK); node_add(blk,body); node_add(blk,el); result = blk;
  }
  if(TOK==kENSURE){
    next_token(); int t4[1]={kEND}; Node *ens = nd(N_ENSURE); ens->a = result; ens->b = parse_stmts(t4,1);
    result = ens;
  }
  return result;
}

/* ===================== primary ===================== */
static Node *parse_hash_literal(void){
  Node *h = nd(N_HASH);
  skip_nl_only();
  while(TOK != tRBRACE){
    skip_nl_only();
    if(TOK==tRBRACE) break;
    if(TOK==tDSTAR){ next_token(); Node *ks=nd(N_KWSPLAT); ks->a=parse_arg(); node_add(h,ks); node_add(h,NULL); }
    else if(TOK==tLABEL){
      ID id=P->tok.id; int line=P->tok.line; next_token();
      Node *k=node_new(N_SYM,line); k->id=id;
      Node *v;
      skip_nl_only();
      if(TOK==tCOMMA||TOK==tRBRACE){
        if(rb_target_version < 31) rb_syntax_error("expected an expression after the label");
        v = var_ref(id,line); }
      else v = parse_arg();
      node_add(h,k); node_add(h,v);
    } else if(TOK==tSTR && P->tok.node && P->tok.node->type==N_STR){
      Node *sn = P->tok.node;
      peek_token();
      if(P->ahead.type==tCOLON && !P->ahead.spc){
        next_token(); next_token(); skip_nl_only();
        Node *k=nd(N_SYM); k->id=rb_intern2(RSTR(sn->val)->ptr,RSTR(sn->val)->len);
        node_add(h,k); node_add(h,parse_arg());
      } else {
        Node *k = parse_arg(); skip_nl_only();
        if(TOK != tASSOC) rb_syntax_error("expected => in hash");
        next_token(); skip_nl_only(); node_add(h,k); node_add(h,parse_arg());
      }
    } else {
      Node *k = parse_arg(); skip_nl_only();
      if(TOK==tCOMMA && rb_target_version < 20){
        next_token(); skip_nl_only();
        node_add(h,k); node_add(h,parse_arg());
      } else {
        if(TOK != tASSOC) rb_syntax_error("expected => in hash");
        next_token(); skip_nl_only();
        node_add(h,k); node_add(h,parse_arg());
      }
    }
    skip_nl_only();
    if(!accept(tCOMMA)) break;
    skip_nl_only();
  }
  skip_nl_only();
  expect(tRBRACE,"}");
  return h;
}

static Node *parse_mlhs_item(void);
static Node *parse_masgn_from(Node *first);

static void warn_literal_cond(Node *c){
  if(!c) return;
  switch(c->type){
    case N_INT: case N_FLOAT: case N_STR: case N_DSTR: case N_SYM:
      rb_warning("literal in condition"); break;
    default: break;
  }
}
static int node_is_jump(Node *n){
  if(!n) return 0;
  switch(n->type){ case N_RETURN: case N_BREAK: case N_NEXT: case N_REDO: case N_RETRY: return 1;
    default: return 0; }
}

static Node *parse_if(int invert){
  int line = P->tok.line;
  next_token();
  P->cond_nest++;
  Node *cond = parse_expr_stmt();
  P->cond_nest--;
  warn_literal_cond(cond);
  if(TOK==kTHEN) next_token();
  else if(TOK==tCOLON && rb_target_version < 20) next_token();
  else skip_nl();
  Node *n = node_new(N_IF,line);
  n->a = invert ? ({ Node *no = node_new(N_NOT,line); no->a = cond; no; }) : cond;
  int terms[3] = { kEND, kELSE, kELSIF };
  n->b = parse_stmts(terms,3);
  if(TOK==kELSIF){ n->c = parse_if(0); return n; }
  if(TOK==kELSE){ next_token(); int t2[1]={kEND}; n->c = parse_stmts(t2,1); }
  expect(kEND,"end");
  return n;
}

static Node *parse_case(void){
  int line = P->tok.line; next_token();
  Node *subj = NULL;
  if(TOK != tNL && TOK != tSEMI && TOK != kWHEN) subj = parse_expr_stmt();
  skip_nl();
  if(TOK==kIN){
    if(rb_target_version < 27) rb_syntax_error("unexpected 'in'");
    Node *n = node_new(N_CASE_IN,line); n->a = subj;
    while(TOK==kIN){
      next_token();
      Node *w = nd(N_IN);
      w->c = parse_pattern_top();
      if(TOK==kIF){ next_token(); w->d = parse_expr_stmt(); }
      else if(TOK==kUNLESS){ next_token(); Node *no=nd(N_NOT); no->a=parse_expr_stmt(); w->d=no; }
      if(TOK==kTHEN) next_token();
      int t[3] = { kEND, kELSE, kIN };
      w->a = parse_stmts(t,3);
      node_add(n,w);
    }
    if(TOK==kELSE){ next_token(); int t2[1]={kEND}; n->b = parse_stmts(t2,1); n->num = 1; }
    expect(kEND,"end");
    return n;
  }
  Node *n = node_new(N_CASE,line); n->a = subj;
  while(TOK==kWHEN){
    next_token();
    Node *w = nd(N_WHEN); w->c = nd(N_ARRAY);
    while(1){
      if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_arg(); node_add(w->c,s); }
      else node_add(w->c, parse_arg());
      if(!accept(tCOMMA)) break;
      skip_nl_only();
    }
    if(TOK==kTHEN) next_token();
    else if(TOK==tCOLON && rb_target_version<20){ next_token(); }
    int t[3] = { kEND, kELSE, kWHEN };
    w->a = parse_stmts(t,3);
    node_add(n,w);
  }
  if(TOK==kELSE){ next_token(); int t2[1]={kEND}; n->b = parse_stmts(t2,1); }
  expect(kEND,"end");
  return n;
}

static Node *parse_lambda(void){
  int line = P->tok.line;
  LocalTbl *prev = P->lvtbl;
  P->lvtbl = lvtbl_new(prev,1);
  Node *params = NULL;
  if(TOK==tLPAREN || TOK==tLPAREN_ARG){ next_token(); params = parse_def_params(1); expect(tRPAREN,")"); }
  else if(TOK==tIDENT || TOK==tSTAR || TOK==tDSTAR || TOK==tAMPER || TOK==tLABEL){ params = parse_def_params(0); }
  Node *iter = node_new(N_ITER,line);
  iter->a = params;
  if(TOK==tLBRACE || TOK==tLBRACE_ARG){ next_token(); int t[1]={tRBRACE}; iter->b = parse_stmts(t,1); expect(tRBRACE,"}"); }
  else { expect(kDO_BLOCK,"do"); iter->b = parse_bodystmt(); expect(kEND,"end"); }
  iter->val = (VALUE)P->lvtbl;
  iter->num = P->lvtbl->numparam_max;
  iter->flags = P->lvtbl->uses_it;
  P->lvtbl = prev;
  Node *lam = node_new(N_LAMBDA,line); lam->b = iter;
  return lam;
}

static Node *parse_def(void){
  int line = P->tok.line;
  next_token();  /* state is EXPR_FNAME */
  ID mid; Node *singleton = NULL;
  /* def self.foo / def obj.foo / def foo */
  if(TOK==kSELF){
    peek_token();
    if(P->ahead.type==tDOT){ next_token(); next_token(); singleton = nd(N_SELF); }
  }
  if(!singleton && (TOK==tIDENT||TOK==tCONST||TOK==tIVAR||TOK==tGVAR)){
    peek_token();
    if(P->ahead.type==tDOT){
      ID base = P->tok.id; int ln = P->tok.line; int kind = TOK;
      next_token(); next_token();
      if(kind==tCONST){ Node *c=node_new(N_CONST,ln); c->id=base; singleton=c; }
      else if(kind==tIVAR){ Node *c=node_new(N_IVAR,ln); c->id=base; singleton=c; }
      else if(kind==tGVAR){ Node *c=node_new(N_GVAR,ln); c->id=base; singleton=c; }
      else if(strcmp(rb_id2name(base),"self")==0) singleton = node_new(N_SELF,ln);
      else singleton = var_ref(base,ln);
    }
  }
  if(TOK != tIDENT && TOK != tCONST && TOK != tOP && TOK != tBANG && TOK != tTILDE &&
     TOK != tLBRACK && TOK != tSTAR && TOK != tPOW && TOK != tAMPER && TOK != tMATCH &&
     TOK != tUMINUS && TOK != tUPLUS && TOK != tLABEL && TOK != tOROP && TOK != tANDOP)
    rb_syntax_error("expected method name after def");
  int was_label = (TOK==tLABEL);
  mid = P->tok.id;
  if(TOK==tBANG) mid = ID_CACHED("!");
  else if(TOK==tTILDE) mid = ID_CACHED("~");
  else if(TOK==tUMINUS) mid = ID_CACHED("-@");
  else if(TOK==tUPLUS) mid = ID_CACHED("+@");
  else if(TOK==tOROP) mid = ID_CACHED("||");
  else if(TOK==tANDOP) mid = ID_CACHED("&&");
  P->state = EXPR_ENDFN;
  next_token();

  LocalTbl *prev = P->lvtbl;
  P->lvtbl = lvtbl_new(NULL,0);
  int prev_in_def = P->in_def; P->in_def = 1;
  Node *params;
  if(was_label){
    /* def foo: ... is not valid; treat as endless with label -- unreachable */
    params = nd(N_ARGS);
  } else if(TOK==tLPAREN || TOK==tLPAREN_ARG){ next_token(); params = parse_def_params(1); skip_nl_only(); expect(tRPAREN,")"); }
  else if(TOK==tNL||TOK==tSEMI||TOK==tASGN){ params = nd(N_ARGS); }
  else params = parse_def_params(0);

  Node *body;
  if(TOK==tASGN && rb_target_version >= 31){   /* endless method */
    next_token();
    body = parse_expr_stmt();
  } else {
    body = parse_bodystmt();
    expect(kEND,"end");
  }
  Node *n = node_new(singleton?N_DEFS:N_DEF, line);
  n->id = mid; n->a = params; n->b = body; n->c = singleton;
  n->val = (VALUE)P->lvtbl;
  P->lvtbl = prev; P->in_def = prev_in_def;
  return n;
}

static Node *parse_class(void){
  int line = P->tok.line; next_token();
  if(TOK==tOP && P->tok.id==ID_CACHED("<<")){
    next_token();
    Node *n = node_new(N_SCLASS,line);
    n->a = parse_expr_stmt();
    skip_nl();
    LocalTbl *prev = P->lvtbl; P->lvtbl = lvtbl_new(NULL,0);
    n->b = parse_bodystmt(); expect(kEND,"end");
    n->val = (VALUE)P->lvtbl; P->lvtbl = prev;
    return n;
  }
  Node *cpath = parse_primary();
  Node *n = node_new(N_CLASS,line);
  n->c = cpath;
  if(TOK==tOP && P->tok.id==ID_CACHED("<")){ next_token(); n->d = parse_expr_stmt(); }
  skip_nl();
  LocalTbl *prev = P->lvtbl; P->lvtbl = lvtbl_new(NULL,0);
  n->b = parse_bodystmt(); expect(kEND,"end");
  n->val = (VALUE)P->lvtbl; P->lvtbl = prev;
  return n;
}

static Node *parse_module(void){
  int line = P->tok.line; next_token();
  Node *cpath = parse_primary();
  Node *n = node_new(N_MODULE,line);
  n->c = cpath;
  skip_nl();
  LocalTbl *prev = P->lvtbl; P->lvtbl = lvtbl_new(NULL,0);
  n->b = parse_bodystmt(); expect(kEND,"end");
  n->val = (VALUE)P->lvtbl; P->lvtbl = prev;
  return n;
}

/* ===================== pattern matching (2.7+) ===================== */
static Node *parse_pattern(void);
static Node *parse_pattern_primary(void);

static Node *parse_pattern_top(void){
  Node *p = parse_pattern();
  if(TOK==tCOMMA){
    Node *arr = nd(N_ARRPAT); node_add(arr,p);
    while(accept(tCOMMA)){
      if(TOK==kTHEN||TOK==tNL||TOK==tSEMI||TOK==kIF||TOK==kUNLESS) { Node *r=nd(N_SPLAT); r->id=-1; node_add(arr,r); break; }
      node_add(arr,parse_pattern());
    }
    return arr;
  }
  return p;
}
static Node *parse_pattern(void){
  Node *p = parse_pattern_primary();
  while(tok_is_op("|")){
    next_token();
    Node *alt = nd(N_ALTPAT); alt->a = p; alt->b = parse_pattern_primary(); p = alt;
  }
  /* `pattern => name` binds the matched value; inside a pattern the lexer
     hands `=>` over as tASSOC, so accept both spellings */
  if(TOK==tRIGHTWARD || TOK==tASSOC){
    next_token();
    if(TOK != tIDENT) rb_syntax_error("expected identifier after =>");
    int d,i; lv_declare(P->tok.id,&d,&i);
    Node *b = nd(N_ARGENT); b->id = P->tok.id; b->flags = i; b->num = d; b->a = p;
    next_token();
    Node *bind = nd(N_PINNED); bind->type = N_ARGENT; bind->num = 999; bind->a = p; bind->id = b->id; bind->flags = i;
    return bind;
  }
  return p;
}
static Node *parse_array_pattern(int term){
  Node *arr = nd(N_ARRPAT);
  int nfind = 0;
  skip_nl_only();
  while(TOK != term){
    skip_nl_only();
    if(TOK==term) break;
    if(TOK==tSTAR){ next_token(); Node *r=nd(N_SPLAT); r->id=-1;
      if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i); r->id=P->tok.id; r->flags=i; next_token(); }
      node_add(arr,r); nfind++; }
    else node_add(arr, parse_pattern());
    skip_nl_only();
    if(!accept(tCOMMA)) break;
    skip_nl_only();
  }
  skip_nl_only();
  expect(term, "pattern close");
  if(nfind==2 && arr->nitems>=3 && arr->items[0]->type==N_SPLAT && arr->items[arr->nitems-1]->type==N_SPLAT)
    arr->type = N_FNDPAT;
  return arr;
}
static Node *parse_hash_pattern(int term){
  Node *h = nd(N_HASHPAT);
  skip_nl_only();
  while(TOK != term){
    skip_nl_only();
    if(TOK==term) break;
    if(TOK==tDSTAR){ next_token();
      if(TOK==kNIL){ next_token(); h->num = 2; }
      else { h->num = 1; if(TOK==tIDENT){ int d,i; lv_declare(P->tok.id,&d,&i); h->id=P->tok.id; h->flags=i; next_token(); } }
      skip_nl_only(); if(!accept(tCOMMA)) break; continue; }
    if(TOK != tLABEL) rb_syntax_error("expected label in hash pattern");
    ID key = P->tok.id; int line = P->tok.line; next_token();
    Node *k = node_new(N_SYM,line); k->id = key;
    Node *v = NULL;
    skip_nl_only();
    if(TOK==tCOMMA || TOK==term || TOK==tNL || TOK==kTHEN || TOK==kIF){
      int d,i; lv_declare(key,&d,&i);
      v = nd(N_ARGENT); v->num = 999; v->id = key; v->flags = i; v->a = NULL;
    } else v = parse_pattern();
    node_add(h,k); node_add(h,v);
    skip_nl_only();
    if(!accept(tCOMMA)) break;
    skip_nl_only();
  }
  skip_nl_only();
  expect(term,"pattern close");
  return h;
}
static Node *parse_pattern_primary(void){
  int line = P->tok.line;
  if(TOK==tLBRACK || TOK==tLBRACK_ARG){ next_token(); return parse_array_pattern(tRBRACK); }
  if(TOK==tLBRACE || TOK==tLBRACE_ARG){ next_token(); return parse_hash_pattern(tRBRACE); }
  if(TOK==tOP && P->tok.id==ID_CACHED("^")){
    next_token();
    Node *pin = node_new(N_PINNED,line);
    if(TOK==tLPAREN||TOK==tLPAREN_ARG){ next_token(); pin->a = parse_expr_stmt(); expect(tRPAREN,")"); }
    else { pin->a = parse_primary(); }
    return pin;
  }
  if(TOK==tIDENT){
    ID id = P->tok.id; next_token();
    int d,i; lv_declare(id,&d,&i);
    Node *b = node_new(N_ARGENT,line); b->num = 999; b->id = id; b->flags = i; b->a = NULL;
    return b;
  }
  if(TOK==tCONST){
    Node *c = parse_primary();     /* handles Foo::Bar */
    if(TOK==tLPAREN_ARG || TOK==tLPAREN){
      next_token();
      /* Const(...) : array or hash pattern */
      if(TOK==tLABEL || TOK==tDSTAR){ Node *h = parse_hash_pattern(tRPAREN); h->b = c; return h; }
      Node *a = parse_array_pattern(tRPAREN); a->b = c; return a;
    }
    if(TOK==tLBRACK_ARG){ next_token(); Node *a = parse_array_pattern(tRBRACK); a->b = c; return a; }
    Node *w = node_new(N_PINNED,line); w->a = c; w->num = 1; return w;
  }
  if(TOK==kNIL){ next_token(); Node *n=node_new(N_PINNED,line); n->a=node_new(N_NIL,line); n->num=1; return n; }
  /* value pattern: any arg-level expression */
  Node *v = parse_arg();
  Node *w = node_new(N_PINNED,line); w->a = v; w->num = 1;
  return w;
}

/* ===================== primary ===================== */
static Node *parse_primary(void){
  int line = P->tok.line;
  switch(TOK){
    case tINT: { Node *n=node_new(N_INT,line); n->val=P->tok.val; next_token(); return parse_primary_postfix(n); }
    case tFLOAT:{ Node *n=node_new(N_FLOAT,line); n->val=P->tok.val; next_token(); return parse_primary_postfix(n); }
    case tSTR: {
      Node *n = P->tok.node; next_token();
      if(n->type==N_STR){
        /* adjacent string literal concatenation */
        while(TOK==tSTR && P->tok.node && (P->tok.node->type==N_STR||P->tok.node->type==N_DSTR)){
          Node *m = P->tok.node; next_token();
          if(m->type==N_STR && n->type==N_STR){ VALUE s = rb_str_dup(n->val); rb_str_cat(s, RSTR(m->val)->ptr, RSTR(m->val)->len); n = node_new(N_STR,line); n->val=s; }
          else { Node *d = node_new(N_DSTR,line);
            if(n->type==N_STR) node_add(d,n); else for(int i=0;i<n->nitems;i++) node_add(d,n->items[i]);
            if(m->type==N_STR) node_add(d,m); else for(int i=0;i<m->nitems;i++) node_add(d,m->items[i]);
            n = d; }
        }
        if(n->type==N_STR && P->fsl==1) n->num = 1;          /* frozen literal */
        else if(n->type==N_STR && rb_target_version>=34 && P->fsl==-1) n->num = 2;  /* chilled */
      }
      return parse_primary_postfix(n); }
    case tIVAR: { Node *n=node_new(N_IVAR,line); n->id=P->tok.id; next_token(); return parse_primary_postfix(n); }
    case tCVAR: { Node *n=node_new(N_CVAR,line); n->id=P->tok.id; next_token(); return parse_primary_postfix(n); }
    case tGVAR: { Node *n=node_new(N_GVAR,line); n->id=P->tok.id; next_token(); return parse_primary_postfix(n); }
    case tNTHREF:{ Node *n=node_new(N_NTH_REF,line); n->num=(int)P->tok.id; next_token(); return parse_primary_postfix(n); }
    case kNIL: next_token(); return parse_primary_postfix(node_new(N_NIL,line));
    case kTRUE: next_token(); return parse_primary_postfix(node_new(N_TRUE,line));
    case kFALSE: next_token(); return parse_primary_postfix(node_new(N_FALSE,line));
    case kSELF: next_token(); return parse_primary_postfix(node_new(N_SELF,line));
    case k__FILE__: { next_token(); Node *n=node_new(N_STR,line); n->val=rb_str_new_cstr(P->fname); return parse_primary_postfix(n); }
    case k__LINE__: { next_token(); Node *n=node_new(N_INT,line); n->val=LONG2FIX(line); return parse_primary_postfix(n); }
    case k__METHOD__: { next_token(); Node *m=node_new(N_ONCE,line); m->num=1; return parse_primary_postfix(m); }
    case kDEFINED: {
      next_token();
      Node *n = node_new(N_DEFINED,line);
      if(TOK==tLPAREN||TOK==tLPAREN_ARG){ next_token(); skip_nl_only(); n->a = parse_expr_stmt(); skip_nl_only(); expect(tRPAREN,")"); }
      else n->a = parse_arg();
      return parse_primary_postfix(n); }
    case tLPAREN: case tLPAREN_ARG: {
      next_token(); skip_nl();
      if(TOK==tRPAREN){ next_token(); return parse_primary_postfix(node_new(N_NIL,line)); }
      int terms[1] = { tRPAREN };
      Node *n = parse_stmts(terms,1);
      skip_nl();
      expect(tRPAREN,")");
          return parse_primary_postfix(n); }
    case tLBRACK: case tLBRACK_ARG: {
      next_token(); skip_nl_only();
      Node *n = node_new(N_ARRAY,line);
      Node *kw = NULL;
      while(TOK != tRBRACK){
        skip_nl_only();
        if(TOK==tRBRACK) break;
        if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_arg(); node_add(n,s); }
        else if(TOK==tLABEL){ ID id=P->tok.id; int ln=P->tok.line; next_token();
          if(!kw) kw=node_new(N_HASH,ln);
          Node *k=node_new(N_SYM,ln); k->id=id; node_add(kw,k);
          skip_nl_only();
          node_add(kw, (TOK==tCOMMA||TOK==tRBRACK)? var_ref(id,ln) : parse_arg()); }
        else if(TOK==tDSTAR){ next_token(); if(!kw) kw=nd(N_HASH); Node *ks=nd(N_KWSPLAT); ks->a=parse_arg(); node_add(kw,ks); node_add(kw,NULL); }
        else {
          Node *e = parse_arg();
          skip_nl_only();
          if(TOK==tASSOC){ next_token(); skip_nl_only(); if(!kw) kw=nd(N_HASH); node_add(kw,e); node_add(kw,parse_arg()); }
          else node_add(n,e);
        }
        skip_nl_only();
        if(!accept(tCOMMA)) break;
        skip_nl_only();
      }
      skip_nl_only();
      expect(tRBRACK,"]");
      if(kw) node_add(n,kw);
          return parse_primary_postfix(n); }
    case tLBRACE: case tLBRACE_ARG: { next_token(); Node *n = parse_hash_literal(); n->line=line; return parse_primary_postfix(n); }
    case tARROW: next_token(); return parse_primary_postfix(parse_lambda());
    case kIF: return parse_primary_postfix(parse_if(0));
    case kUNLESS: return parse_primary_postfix(parse_if(1));
    case kCASE: return parse_primary_postfix(parse_case());
    case kWHILE: case kUNTIL: {
      int until = (TOK==kUNTIL); next_token();
      P->cond_nest++;
      Node *cond = parse_expr_stmt();
      P->cond_nest--;
      warn_literal_cond(cond);
      if(TOK==kDO_COND||TOK==kDO_BLOCK||TOK==kDO) next_token();
      else if(TOK==tCOLON && rb_target_version < 20) next_token();
      else skip_nl();
      Node *n = node_new(until?N_UNTIL:N_WHILE,line);
      n->a = cond; int t[1]={kEND}; n->b = parse_stmts(t,1); expect(kEND,"end");
      return n; }
    case kFOR: {
      next_token();
      Node *n = node_new(N_FOR,line);
      Node *vars = nd(N_ARRAY);
      while(1){
        if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT);
          Node *v = TOK==tIDENT ? var_ref(P->tok.id,P->tok.line) : parse_primary();
          if(TOK==tIDENT) next_token();
          s->a = asgn_node(v,NULL); node_add(vars,s); }
        else if(TOK==tIDENT){ Node *v = var_ref(P->tok.id,P->tok.line); next_token();
          if(v->type==N_VCALL){ int d,i; lv_declare(v->id,&d,&i); v=var_ref(v->id,v->line); }
          node_add(vars, asgn_node(v,NULL)); }
        else if(TOK==tLPAREN||TOK==tLPAREN_ARG){ next_token();
          Node *sub = nd(N_MASGN); Node *sv = nd(N_ARRAY);
          while(TOK==tIDENT){ Node *v=var_ref(P->tok.id,P->tok.line); next_token();
            if(v->type==N_VCALL){ int d,i; lv_declare(v->id,&d,&i); v=var_ref(v->id,v->line); }
            node_add(sv, asgn_node(v,NULL)); if(!accept(tCOMMA)) break; }
          expect(tRPAREN,")"); sub->a = sv; node_add(vars, sub); }
        else { Node *v = parse_primary(); node_add(vars, asgn_node(v,NULL)); }
        if(!accept(tCOMMA)) break;
      }
      n->c = vars;
      if(TOK != kIN) rb_syntax_error("expected 'in' in for");
      next_token();
      P->cond_nest++;
      n->a = parse_expr_stmt();
      P->cond_nest--;
      if(TOK==kDO_COND||TOK==kDO_BLOCK||TOK==kDO) next_token(); else skip_nl();
      int t[1]={kEND}; n->b = parse_stmts(t,1); expect(kEND,"end");
      return n; }
    case kBEGIN: {
      next_token(); skip_nl();
      Node *b = parse_bodystmt(); expect(kEND,"end");
      Node *n = node_new(N_BEGIN,line); n->a = b;
      /* begin...end while/until  => do-while */
      if(TOK==kWHILE_MOD||TOK==kUNTIL_MOD){
        int until = (TOK==kUNTIL_MOD); next_token();
        Node *w = node_new(until?N_UNTIL:N_WHILE,line);
        w->a = parse_expr_stmt(); w->b = b; w->num = 1; return w;
      }
      return parse_primary_postfix(n); }
    case kDEF: return parse_primary_postfix(parse_def());
    case kCLASS: return parse_class();
    case kMODULE: return parse_module();
    case kYIELD: {
      next_token();
      Node *n = node_new(N_YIELD,line);
      if(TOK==tLPAREN_ARG || TOK==tLPAREN){ next_token(); parse_args_paren(n); }
      /* a bare `yield` ends at the end of its line: the lexer treats the newline
         after a keyword as a continuation, so without this the next statement
         is swallowed as yield's argument list */
      else if(tok_starts_arg() && TOK != tLBRACE_ARG && P->tok.line == line){ Node *kw=NULL;
        while(1){ parse_one_arg(n,&kw); if(!accept(tCOMMA)) break; } n->d=kw; }
      return parse_primary_postfix(n); }
    case kSUPER: {
      next_token();
      Node *n;
      if(TOK==tLPAREN_ARG||TOK==tLPAREN){ n = node_new(N_SUPER,line); next_token(); parse_args_paren(n); }
      else if(tok_starts_arg() && TOK != tLBRACE_ARG && TOK != tLBRACE){ n = node_new(N_SUPER,line); Node *kw=NULL;
        while(1){ parse_one_arg(n,&kw); if(!accept(tCOMMA)) break; } n->d=kw; }
      else n = node_new(N_ZSUPER,line);
      if(TOK==tLBRACE_ARG||TOK==kDO_BLOCK) parse_block(n);
      return parse_primary_postfix(n); }
    case kRETURN: case kBREAK: case kNEXT: {
      int t = TOK; next_token();
      Node *n = node_new(t==kRETURN?N_RETURN:(t==kBREAK?N_BREAK:N_NEXT),line);
      if(TOK != tNL && TOK != tSEMI && TOK != tEOF && TOK != kEND && TOK != tRBRACE &&
         TOK != kIF_MOD && TOK != kUNLESS_MOD && TOK != kWHILE_MOD && TOK != kUNTIL_MOD &&
         TOK != tRPAREN && TOK != kRESCUE_MOD && TOK != kELSE && TOK != kWHEN && TOK != kIN && TOK != tRBRACK){
        Node *vals = nd(N_ARRAY);
        while(1){
          if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_arg(); node_add(vals,s); }
          else node_add(vals, parse_arg());
          if(!accept(tCOMMA)) break;
        }
        if(vals->nitems==1 && vals->items[0]->type != N_SPLAT) n->a = vals->items[0];
        else n->a = vals;
      }
      return n; }
    case kREDO: next_token(); return node_new(N_REDO,line);
    case kRETRY: next_token(); return node_new(N_RETRY,line);
    case kALIAS: {
      next_token();
      Node *n = node_new(N_ALIAS,line);
      P->state = EXPR_FNAME;
      if(TOK==tSTR && P->tok.node && P->tok.node->type==N_SYM) n->id = P->tok.node->id;
      else if(TOK==tGVAR){ n->num = 1; n->id = P->tok.id; }
      else n->id = P->tok.id;
      P->state = EXPR_FNAME; next_token();
      if(TOK==tSTR && P->tok.node && P->tok.node->type==N_SYM) n->flags = P->tok.node->id;
      else n->flags = P->tok.id;
      next_token();
      return n; }
    case kUNDEF: {
      next_token();
      Node *n = node_new(N_UNDEF,line);
      while(1){ P->state = EXPR_FNAME;
        Node *s = nd(N_SYM);
        if(TOK==tSTR && P->tok.node && P->tok.node->type==N_SYM) s->id = P->tok.node->id;
        else s->id = P->tok.id;
        node_add(n,s); next_token();
        if(!accept(tCOMMA)) break; }
      return n; }
    case kNOT: { next_token(); Node *n=node_new(N_NOT,line); n->a = parse_expr_stmt(); return n; }
    case tCOLON3: { next_token(); Node *n=node_new(N_COLON3,line); n->id=P->tok.id; next_token(); return parse_primary_postfix(n); }
    case tCONST: { Node *n=node_new(N_CONST,line); n->id=P->tok.id; next_token();
      /* Const(...) is a method call */
      if(TOK==tLPAREN_ARG){ Node *c = new_call(NULL,n->id,line); c->type = N_FCALL; next_token(); parse_args_paren(c);
        if(TOK==tLBRACE_ARG||TOK==kDO_BLOCK) parse_block(c);
        return parse_primary_postfix(c); }
      return parse_primary_postfix(n); }
    case tIDENT: {
      ID id = P->tok.id; int spc_after;
      next_token();
      spc_after = P->tok.spc;
      if(TOK==tLPAREN_ARG){
        Node *c = new_call(NULL,id,line); c->type = N_FCALL;
        next_token(); parse_args_paren(c);
        if(TOK==tLBRACE_ARG||TOK==tLBRACE) parse_block(c);
        else if(TOK==kDO_BLOCK) parse_block(c);
        return parse_primary_postfix(c);
      }
      int d,i;
      int is_lvar = lv_lookup(P->lvtbl,id,&d,&i);
      if(!is_lvar && spc_after && tok_starts_arg() && TOK != tLBRACE_ARG){
        /* command call: foo a, b */
        if(TOK==tLBRACK_ARG || TOK==tLPAREN_ARG){ /* not an arg start */ }
        else {
          Node *c = new_call(NULL,id,line); c->type = N_FCALL;
          parse_command_args(c);
          if(TOK==kDO_BLOCK) parse_block(c);
          return c;
        }
      }
      Node *n = var_ref(id,line);
      if(!is_lvar && (TOK==tLBRACE_ARG || TOK==kDO_BLOCK)){
        Node *c = new_call(NULL,id,line); c->type=N_FCALL; parse_block(c);
        return parse_primary_postfix(c);
      }
      return parse_primary_postfix(n); }
    case tUMINUS: case tUPLUS: {
      int neg = (TOK==tUMINUS); next_token();
      Node *v;
      if(TOK==tINT||TOK==tFLOAT){
        v = node_new(TOK==tINT?N_INT:N_FLOAT,line); v->val = P->tok.val; next_token();
        if(TOK==tPOW){ next_token(); Node *pw = new_call(v, ID_CACHED("**"), line); node_add(pw, parse_primary());
          Node *m = new_call(pw, rb_intern(neg?"-@":"+@"), line); return parse_primary_postfix(m); }
        if(neg){
          if(v->type==N_INT) v->val = rb_int_new(-FIX2LONG(v->val));
          else v->val = rb_float_new(-RFLT(v->val)->f);
        }
        return parse_primary_postfix(v);
      }
      v = parse_primary();
      { Node *m = new_call(v, rb_intern(neg?"-@":"+@"), line); return parse_primary_postfix(m); } }
    case tBANG: { next_token(); Node *v = parse_ternary(); Node *n=node_new(N_NOT,line); n->a=v; return n; }
    case tTILDE: { next_token(); Node *v = parse_primary(); return new_call(v, ID_CACHED("~"), line); }
    case tAMPER: { next_token(); Node *v = parse_arg(); Node *n = node_new(N_BLOCKPASS,line); n->a=v; return n; }
    case tDOT2: case tDOT3: case tBDOT2: case tBDOT3: {
      if(rb_target_version < 27) rb_syntax_error("unexpected beginless range");
      int excl = (TOK==tDOT3||TOK==tBDOT3); next_token();
      Node *n = node_new(N_RANGE,line); n->a = node_new(N_NIL,line); n->b = parse_arg(); n->num = excl; return n; }
    case tLABEL: {
      /* bare hash as command arg handled elsewhere; here treat as hash */
      Node *h = nd(N_HASH);
      while(TOK==tLABEL){ ID id=P->tok.id; int ln=P->tok.line; next_token();
        Node *k=node_new(N_SYM,ln); k->id=id; node_add(h,k);
        node_add(h, (TOK==tCOMMA||TOK==tNL||TOK==tEOF)? var_ref(id,ln) : parse_arg());
        if(!accept(tCOMMA)) break; }
      return h; }
    default: break;
  }
  rb_syntax_error("unexpected token in expression (tok=%d)", TOK);
  return NULL;
}

/* ===================== postfix ===================== */
static Node *parse_primary_postfix(Node *n){
  while(1){
    if(TOK==tDOT || TOK==tSAFEDOT){
      int safe = (TOK==tSAFEDOT);
      next_token();
      if(TOK==tLPAREN_ARG || TOK==tLPAREN){
        if(rb_target_version < 20) rb_syntax_error("unexpected '('");
        Node *c = new_call(n, ID_CACHED("call"), P->tok.line);
        if(safe) c->num = 1;
        next_token(); parse_args_paren(c);
        if(TOK==tLBRACE_ARG||TOK==kDO_BLOCK) parse_block(c);
        n = c; continue;
      }
      ID mid;
      if(TOK==tIDENT||TOK==tCONST) mid = P->tok.id;
      else if(TOK==tOP||TOK==tOPASGN||TOK==tPOW||TOK==tMATCH) mid = P->tok.id;
      else if(TOK==tBANG) mid = ID_CACHED("!");
      else if(TOK==tTILDE) mid = ID_CACHED("~");
      else if(TOK==kCLASS) mid = ID_CACHED("class");
      else if(TOK==tLBRACK||TOK==tLBRACK_ARG) mid = ID_CACHED("[]");
      else rb_syntax_error("expected method name after .");
      int mline = P->tok.line;
      next_token();
      Node *c = new_call(n, mid, mline);
      if(safe) c->num = 1;
      int had_space = P->tok.spc;
      if(TOK==tLPAREN_ARG){ next_token(); parse_args_paren(c); }
      else if(TOK==tASGN){
        /* attribute assignment handled by caller via asgn_node */
        n = c; continue;
      }
      else if(had_space && tok_starts_arg() && TOK != tLBRACE_ARG && TOK != tLBRACE &&
              TOK != tLBRACK_ARG && TOK != tLPAREN_ARG){
        parse_command_args(c);
        if(TOK==kDO_BLOCK) parse_block(c);
        n = c; continue;
      }
      if(TOK==tLBRACE_ARG||TOK==tLBRACE) parse_block(c);
      else if(TOK==kDO_BLOCK) parse_block(c);
      n = c; continue;
    }
    if(TOK==tCOLON2){
      next_token();
      if(TOK==tCONST){
        ID id = P->tok.id; int ln = P->tok.line; next_token();
        if(TOK==tLPAREN_ARG){ Node *c = new_call(n,id,ln); next_token(); parse_args_paren(c);
          if(TOK==tLBRACE_ARG||TOK==kDO_BLOCK) parse_block(c); n = c; continue; }
        Node *c = node_new(N_COLON2,ln); c->a = n; c->id = id; n = c; continue;
      }
      ID mid = P->tok.id; int ln = P->tok.line; next_token();
      Node *c = new_call(n,mid,ln);
      if(TOK==tLPAREN_ARG){ next_token(); parse_args_paren(c); }
      if(TOK==tLBRACE_ARG||TOK==kDO_BLOCK) parse_block(c);
      n = c; continue;
    }
    if(TOK==tLBRACK_ARG){
      next_token(); skip_nl_only();
      Node *c = new_call(n, ID_CACHED("[]"), P->tok.line);
      Node *kw = NULL;
      while(TOK != tRBRACK){
        skip_nl_only();
        if(TOK==tRBRACK) break;
        parse_one_arg(c,&kw);
        skip_nl_only();
        if(!accept(tCOMMA)) break;
        skip_nl_only();
      }
      skip_nl_only();
      expect(tRBRACK,"]");
      c->d = kw;
          n = c; continue;
    }
    break;
  }
  return n;
}

/* ===================== operator precedence ===================== */
static Node *parse_pow(void){
  Node *l = parse_primary();
  if(TOK==tPOW){
    next_token();
    Node *r = parse_pow();      /* right assoc; unary minus binds looser */
    Node *c = new_call(l, ID_CACHED("**"), l->line); node_add(c,r); return c;
  }
  return l;
}
static Node *parse_unary(void){
  int line = P->tok.line;
  if(TOK==tUMINUS){
    /* `-2.abs` is `(-2).abs`: the sign belongs to a numeric literal. */
    peek_token();
    if(P->ahead.type==tINT || P->ahead.type==tFLOAT) return parse_pow();
    next_token(); Node *v = parse_unary(); return new_call(v, ID_CACHED("-@"), line); }
  if(TOK==tUPLUS){ next_token(); Node *v = parse_unary(); return new_call(v, ID_CACHED("+@"), line); }
  if(TOK==tBANG){ next_token(); Node *v = parse_unary(); Node *n=node_new(N_NOT,line); n->a=v; return n; }
  if(TOK==tTILDE){ next_token(); Node *v = parse_unary(); return new_call(v, ID_CACHED("~"), line); }
  return parse_pow();
}
static Node *binop_chain(Node *(*sub)(void), const char **ops){
  Node *l = sub();
  while(TOK==tOP || TOK==tMATCH || TOK==tNMATCH || TOK==tPOW){
    const char *nm = rb_id2name(P->tok.id);
    int found = 0;
    for(int i=0;ops[i];i++) if(strcmp(nm,ops[i])==0){ found=1; break; }
    if(!found) break;
    ID mid = P->tok.id; int line = P->tok.line;
    int nmatch = (TOK==tNMATCH);
    next_token(); skip_nl_only();
    Node *r = sub();
    if(nmatch){ Node *c = new_call(l, ID_CACHED("=~"), line); node_add(c,r);
      Node *no = node_new(N_NOT,line); no->a = c; l = no; }
    else if(mid == ID_CACHED("=~")){ Node *c = node_new(N_MATCH,line); c->a=l; c->b=r; l=c; }
    else { Node *c = new_call(l,mid,line); node_add(c,r); l = c; }
  }
  return l;
}
static const char *ops_mul[] = {"*","/","%",NULL};
static const char *ops_add[] = {"+","-",NULL};
static const char *ops_shift[] = {"<<",">>",NULL};
static const char *ops_band[] = {"&",NULL};
static const char *ops_bor[] = {"|","^",NULL};
static const char *ops_cmp[] = {"<",">","<=",">=",NULL};
static const char *ops_eq[] = {"==","!=","===","<=>","=~","!~",NULL};

static Node *parse_mul(void){ return binop_chain(parse_unary, ops_mul); }
static Node *parse_add(void){ return binop_chain(parse_mul, ops_add); }
static Node *parse_shift(void){ return binop_chain(parse_add, ops_shift); }
static Node *parse_band(void){ return binop_chain(parse_shift, ops_band); }
static Node *parse_bor(void){ return binop_chain(parse_band, ops_bor); }
static Node *parse_cmp(void){ return binop_chain(parse_bor, ops_cmp); }
static Node *parse_eqn(void){
  Node *l = parse_cmp();
  while((TOK==tOP && ({ const char *nm=rb_id2name(P->tok.id); int f=0;
         for(int i=0;ops_eq[i];i++) if(strcmp(nm,ops_eq[i])==0){f=1;break;} f; })) || TOK==tMATCH || TOK==tNMATCH){
    int line = P->tok.line;
    if(TOK==tMATCH){ next_token(); skip_nl_only(); Node *r = parse_cmp(); Node *c=node_new(N_MATCH,line); c->a=l;c->b=r; l=c; continue; }
    if(TOK==tNMATCH){ next_token(); skip_nl_only(); Node *r = parse_cmp(); Node *c=node_new(N_MATCH,line); c->a=l;c->b=r;
      Node *no=node_new(N_NOT,line); no->a=c; l=no; continue; }
    ID mid = P->tok.id; next_token(); skip_nl_only();
    Node *r = parse_cmp(); Node *c = new_call(l,mid,line); node_add(c,r); l=c;
  }
  return l;
}
static Node *parse_defined_lvl(void){ return parse_eqn(); }
static Node *parse_andand(void){
  Node *l = parse_defined_lvl();
  while(TOK==tANDOP){ int line=P->tok.line; next_token(); skip_nl_only();
    Node *r = parse_defined_lvl(); Node *n=node_new(N_AND,line); n->a=l;n->b=r; l=n; }
  return l;
}
static Node *parse_oror(void){
  Node *l = parse_andand();
  while(TOK==tOROP){ int line=P->tok.line; next_token(); skip_nl_only();
    Node *r = parse_andand(); Node *n=node_new(N_OR,line); n->a=l;n->b=r; l=n; }
  return l;
}
static int tok_ends_expr(void){
  switch(TOK){ case tNL: case tSEMI: case tEOF: case tRPAREN: case tRBRACK:
    case tRBRACE: case tCOMMA: case kEND: case kTHEN: case kDO_BLOCK: case kDO_COND:
    case kIF_MOD: case kUNLESS_MOD: case kWHILE_MOD: case kUNTIL_MOD: case kRESCUE_MOD:
    case kELSE: case kELSIF: case kWHEN: case kIN: case tASSOC: case tRIGHTWARD:
      return 1; default: return 0; }
}
static Node *parse_range(void){
  Node *l = parse_oror();
  if(TOK==tDOT2 || TOK==tDOT3){
    int excl = (TOK==tDOT3); int line=P->tok.line; next_token();
    Node *n = node_new(N_RANGE,line); n->a = l; n->num = excl;
    if(tok_ends_expr()){
      if(rb_target_version < 27) rb_syntax_error("unexpected end of range");
      n->b = node_new(N_NIL,line); }
    else n->b = parse_oror();
    return n;
  }
  return l;
}
static Node *parse_ternary(void){
  Node *c = parse_range();
  if(TOK==tQUESTION){
    int line=P->tok.line; next_token(); skip_nl_only();
    Node *a = parse_ternary();
    skip_nl_only();
    if(TOK==tCOLON) next_token();
    else if(TOK==tSTR && P->tok.node && P->tok.node->type==N_SYM){
      /* `x ? a :b` lexed as symbol */
      Node *sym = P->tok.node; next_token();
      Node *n = node_new(N_IF,line); n->a=c; n->b=a; n->c = var_ref(sym->id,line); return n;
    }
    else rb_syntax_error("expected : in ternary");
    skip_nl_only();
    Node *b = parse_ternary();
    Node *n = node_new(N_IF,line); n->a=c; n->b=a; n->c=b; return n;
  }
  return c;
}

/* ===================== assignment ===================== */
static int is_lhs_node(Node *n){
  switch(n->type){
    case N_LVAR: case N_VCALL: case N_IVAR: case N_GVAR: case N_CVAR:
    case N_CONST: case N_COLON2: return 1;
    case N_CALL: return 1;
    case N_MASGN: return 1;
    default: return 0;
  }
}
static Node *parse_mrhs(void){
  Node *first;
  if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_ternary();
    Node *arr = nd(N_ARRAY); node_add(arr,s); arr->num=1;
    while(accept(tCOMMA)){ skip_nl_only();
      if(TOK==tSTAR){ next_token(); Node *s2=nd(N_SPLAT); s2->a=parse_ternary(); node_add(arr,s2); }
      else node_add(arr,parse_ternary()); }
    return arr; }
  first = parse_expr(); /* allows nested assignment */
  if(TOK==tCOMMA){
    Node *arr = nd(N_ARRAY); node_add(arr,first); arr->num=1;
    while(accept(tCOMMA)){ skip_nl_only();
      if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_ternary(); node_add(arr,s); }
      else node_add(arr,parse_ternary()); }
    return arr;
  }
  return first;
}
static Node *parse_masgn_from(Node *first){
  Node *m = nd(N_MASGN);
  Node *lhs = nd(N_ARRAY);
  if(first) node_add(lhs, asgn_node(first,NULL));
  while(accept(tCOMMA)){
    if(TOK==tASGN) break;
    if(TOK==tSTAR){ next_token();
      Node *s = nd(N_SPLAT);
      if(TOK != tASGN && TOK != tCOMMA){ Node *t = parse_ternary(); s->a = asgn_node(t,NULL); }
      node_add(lhs,s); continue; }
    if(TOK==tLPAREN||TOK==tLPAREN_ARG){
      next_token();
      Node *sub = parse_masgn_from(NULL);
      expect(tRPAREN,")");
      node_add(lhs,sub); continue;
    }
    Node *t = parse_ternary();
    node_add(lhs, asgn_node(t,NULL));
  }
  m->a = lhs;
  if(TOK==tASGN){ next_token(); skip_nl_only(); m->b = parse_mrhs(); }
  return m;
}

static Node *parse_expr(void){
  Node *l;
  int line = P->tok.line;
  if(TOK==tSTAR){
    /* *a = ... */
    Node *save = NULL; (void)save;
    next_token();
    Node *t = parse_ternary();
    Node *m = nd(N_MASGN); Node *lhs = nd(N_ARRAY);
    Node *s = nd(N_SPLAT); s->a = asgn_node(t,NULL); node_add(lhs,s);
    if(TOK==tCOMMA){ Node *m2 = parse_masgn_from(NULL);
      for(int i=0;i<m2->a->nitems;i++) node_add(lhs,m2->a->items[i]);
      m->a = lhs; m->b = m2->b; return m; }
    m->a = lhs;
    expect(tASGN,"="); skip_nl_only(); m->b = parse_mrhs();
    return m;
  }
  l = parse_ternary();
  if(TOK==tCOMMA && is_lhs_node(l)){
    /* could be multiple assignment: look ahead for '=' */
    return parse_masgn_from(l);
  }
  if(TOK==tASGN){
    if(!is_lhs_node(l)) rb_syntax_error("invalid assignment target");
    next_token(); skip_nl_only();
    Node *rhs;
    if(TOK==tSTAR){ rhs = parse_mrhs(); }
    else { rhs = parse_expr();
      if(TOK==tCOMMA){ Node *arr=nd(N_ARRAY); node_add(arr,rhs); arr->num=1;
        while(accept(tCOMMA)){ skip_nl_only();
          if(TOK==tSTAR){ next_token(); Node *s=nd(N_SPLAT); s->a=parse_ternary(); node_add(arr,s); }
          else node_add(arr,parse_ternary()); }
        rhs = arr; } }
    return asgn_node(l, rhs);
  }
  if(TOK==tOPASGN){
    ID op = P->tok.id; next_token(); skip_nl_only();
    Node *rhs = parse_expr();
    Node *n;
    if(l->type==N_CALL && l->id==ID_CACHED("[]")){
      n = node_new(N_OPASGN_IDX,line); n->a=l->a; n->items=l->items; n->nitems=l->nitems;
      n->b=rhs; n->id=op; n->d = l->d;
    } else if(l->type==N_CALL){
      n = node_new(N_OPASGN_ATTR,line); n->a=l->a; n->b=rhs; n->id=op; n->flags=l->id; n->num=l->num;
    } else if(l->type==N_CONST || l->type==N_COLON2){
      n = node_new(N_OPASGN_CONST,line); n->c=l; n->b=rhs; n->id=op;
    } else {
      if(l->type==N_VCALL){ int d,i; lv_declare(l->id,&d,&i); l = var_ref(l->id,l->line); }
      n = node_new(N_OPASGN,line); n->c=l; n->b=rhs; n->id=op;
    }
    return n;
  }
  if(TOK==tASSOC && rb_target_version>=30){
    next_token();
    Node *n = node_new(N_MATCHPAT,line); n->a = l; n->b = parse_pattern_top(); return n;
  }
  if(TOK==kIN && rb_target_version>=27 && P->cond_nest==0){
    next_token();
    Node *n = node_new(N_MATCHPATP,line); n->a = l; n->b = parse_pattern_top(); return n;
  }
  return l;
}

static Node *parse_arg(void){
  /* argument-level expression: ternary plus nested assignment */
  Node *l = parse_ternary();
  if(TOK==tASGN && is_lhs_node(l)){
    next_token(); skip_nl_only();
    return asgn_node(l, parse_arg());
  }
  if(TOK==tOPASGN){
    ID op = P->tok.id; int line=P->tok.line; next_token(); skip_nl_only();
    Node *rhs = parse_arg();
    Node *n;
    if(l->type==N_CALL && l->id==ID_CACHED("[]")){
      n = node_new(N_OPASGN_IDX,line); n->a=l->a; n->items=l->items; n->nitems=l->nitems; n->b=rhs; n->id=op; n->d=l->d;
    } else if(l->type==N_CALL){
      n = node_new(N_OPASGN_ATTR,line); n->a=l->a; n->b=rhs; n->id=op; n->flags=l->id;
    } else if(l->type==N_CONST||l->type==N_COLON2){
      n = node_new(N_OPASGN_CONST,line); n->c=l; n->b=rhs; n->id=op;
    } else {
      if(l->type==N_VCALL){ int d,i; lv_declare(l->id,&d,&i); l = var_ref(l->id,l->line); }
      n = node_new(N_OPASGN,line); n->c=l; n->b=rhs; n->id=op;
    }
    return n;
  }
  return l;
}

/* expr level with and/or/not */
static Node *parse_expr_stmt(void){
  Node *l;
  if(TOK==kNOT){ int line=P->tok.line; next_token(); Node *n=node_new(N_NOT,line); n->a=parse_expr_stmt(); l=n; }
  else l = parse_expr();
  while(TOK==kAND || TOK==kOR){
    int isand = (TOK==kAND); int line=P->tok.line;
    next_token(); skip_nl_only();
    Node *r;
    if(TOK==kNOT){ next_token(); Node *n=node_new(N_NOT,line); n->a=parse_expr(); r=n; }
    else r = parse_expr();
    Node *n = node_new(isand?N_AND:N_OR,line); n->a=l; n->b=r; l=n;
  }
  return l;
}

/* ===================== statements ===================== */
static Node *parse_stmt(void){
  int line = P->tok.line;
  Node *n = parse_expr_stmt();
  while(1){
    if(TOK==kIF_MOD){ next_token(); Node *c=parse_expr_stmt(); warn_literal_cond(c);
      Node *f=node_new(N_IF,line); f->a=c; f->b=n; n=f; continue; }
    if(TOK==kUNLESS_MOD){ next_token(); Node *c=parse_expr_stmt(); Node *no=node_new(N_NOT,line); no->a=c;
      Node *f=node_new(N_IF,line); f->a=no; f->b=n; n=f; continue; }
    if(TOK==kWHILE_MOD){ next_token(); Node *c=parse_expr_stmt(); Node *w=node_new(N_WHILE,line); w->a=c; w->b=n;
      if(n->type==N_BEGIN) w->num=1; n=w; continue; }
    if(TOK==kUNTIL_MOD){ next_token(); Node *c=parse_expr_stmt(); Node *w=node_new(N_UNTIL,line); w->a=c; w->b=n;
      if(n->type==N_BEGIN) w->num=1; n=w; continue; }
    if(TOK==kRESCUE_MOD){ next_token(); Node *r=parse_expr_stmt();
      Node *resc=node_new(N_RESCUE,line); resc->a=n;
      Node *rb=node_new(N_RESBODY,line); rb->c=nd(N_ARRAY); rb->a=r; node_add(resc,rb); n=resc; continue; }
    break;
  }
  return n;
}

static int in_terms(int t, int *terms, int n){ for(int i=0;i<n;i++) if(terms[i]==t) return 1; return 0; }

static Node *parse_stmts(int *terms, int nterms){
  Node *blk = nd(N_BLOCK);
  skip_nl();
  int warned_unreachable = 0;
  while(TOK != tEOF && !in_terms(TOK,terms,nterms)){
    if(blk->nitems && node_is_jump(blk->items[blk->nitems-1]) && !warned_unreachable){
      rb_warning("statement not reached"); warned_unreachable = 1;
    }
    Node *s = parse_stmt();
    node_add(blk,s);
    if(TOK==tNL||TOK==tSEMI) skip_nl();
    else break;
  }
  if(blk->nitems==1) return blk->items[0];
  if(blk->nitems==0) return node_new(N_NIL,P->line);
  return blk;
}

/* ===================== entry ===================== */
static Node *parse_sub_source(const char *s, long len, int line){
  Parser save = *P;
  P->src = s; P->len = len; P->pos = 0; P->line = line;
  P->state = EXPR_BEG; P->has_ahead = 0; P->heredoc_restore = 0;
  P->paren_nest = 0; P->cond_nest = 0;
  next_token();
  int terms[1] = { tEOF };
  Node *n = parse_stmts(terms,1);
  LocalTbl *lv = P->lvtbl;
  int fsl = P->fsl;
  rb_encoding *enc = P->enc;
  *P = save;
  P->lvtbl = lv; P->fsl = fsl; P->enc = enc;
  return n;
}

Node *rb_parse_string(const char *src, const char *fname, LocalTbl **tbl_out){
  Parser par; memset(&par,0,sizeof par);
  Parser *saveP = P;
  P = &par;
  P->src = src; P->len = (long)strlen(src); P->pos = 0; P->line = 1;
  P->state = EXPR_BEG; P->fname = fname; P->fsl = -1; P->enc = enc_utf8;
  P->lvtbl = lvtbl_new(NULL,0);
  if(P->len>=3 && (unsigned char)src[0]==0xEF && (unsigned char)src[1]==0xBB && (unsigned char)src[2]==0xBF) P->pos = 3;
  scan_magic_comments();
  next_token();
  int terms[1] = { tEOF };
  Node *n = parse_stmts(terms,1);
  if(TOK != tEOF) rb_syntax_error("unexpected token at top level");
  if(tbl_out) *tbl_out = P->lvtbl;
  P = saveP;
  return n;
}
