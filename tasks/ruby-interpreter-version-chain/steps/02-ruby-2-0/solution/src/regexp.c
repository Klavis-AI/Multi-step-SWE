#include "internal.h"

VALUE rb_reg_make_match(VALUE re, VALUE str, long *beg, long *end, int ngroups);

/* ===================== ASCII predicates ===================== */
static int a_upper(int x){ return x>='A' && x<='Z'; }
static int a_lower(int x){ return x>='a' && x<='z'; }
static int a_alpha(int x){ return a_upper(x) || a_lower(x); }
static int a_digit(int x){ return x>='0' && x<='9'; }
static int a_alnum(int x){ return a_alpha(x) || a_digit(x); }
static int a_space(int x){ return x==' '||x=='\t'||x=='\n'||x=='\v'||x=='\f'||x=='\r'; }
static int a_xdigit(int x){ return a_digit(x)||(x>='a'&&x<='f')||(x>='A'&&x<='F'); }
static int a_punct(int x){ return x>=33 && x<=126 && !a_alnum(x); }
static int a_word(int x){ return a_alnum(x) || x=='_'; }
static int a_blank(int x){ return x==' '||x=='\t'; }
static int a_cntrl(int x){ return (x>=0 && x<32) || x==127; }
static int a_print(int x){ return x>=32 && x<127; }
static int a_graph(int x){ return x>32 && x<127; }
static int a_ascii(int x){ return x>=0 && x<128; }

static int lc(int x){ return a_upper(x) ? x+32 : x; }

/* ===================== node tree ===================== */
enum {
  RX_CHAR, RX_ANY, RX_CLASS, RX_BOL, RX_EOL, RX_BOS, RX_EOS, RX_EOSNL,
  RX_WORDB, RX_NWORDB, RX_GPOS, RX_GROUP, RX_GEND, RX_ALT, RX_REP,
  RX_BACKREF, RX_LOOK, RX_ATOMIC, RX_NOP
};

typedef struct crange { int lo, hi; } crange;

typedef struct renode {
  short type;
  short ci;
  struct renode *next;
  struct renode *sub;
  struct renode **alts; int nalts;
  int min, max, greedy, simple;
  int gnum;
  unsigned char *bmap;
  crange *ranges; int nranges;
  int neg;
  int dotall;
  int behind;
  int hascap;
  long minw, maxw;
  char lit[8]; int litlen;
} renode;

#define RX_NCAP 32
#define RX_MAXSTEPS 2000000L
#define RX_MAXDEPTH 8000

struct regex {
  renode *prog;
  int ngroups;
  int opts;
  char **names; int *nameidx; int nnames;
  char *src; long srclen;
};

static renode *nnew(int t){
  renode *n = xcalloc(1, sizeof(renode));
  n->type = (short)t; n->maxw = -1;
  return n;
}
static renode *chain_append(renode *head, renode *tailn){
  if(!head) return tailn;
  renode *t = head; while(t->next) t = t->next;
  t->next = tailn; return head;
}

/* ---- character class construction ---- */
static void bset(renode *n, int c){ n->bmap[(c)>>3] |= (unsigned char)(1u << ((c)&7)); }
static int  bget(renode *n, int c){ return (n->bmap[(c)>>3] >> ((c)&7)) & 1; }

static void add_range(renode *n, int lo, int hi){
  if(hi < lo) return;
  n->ranges = xrealloc(n->ranges, (n->nranges+1)*sizeof(crange));
  n->ranges[n->nranges].lo = lo; n->ranges[n->nranges].hi = hi; n->nranges++;
}
static void cls_add(renode *n, int lo, int hi, int ci){
  if(lo < 0) lo = 0;
  int bhi = hi > 255 ? 255 : hi;
  for(int x = lo; x <= bhi; x++){
    bset(n, x);
    if(ci && x < 128){
      if(a_lower(x)) bset(n, x-32);
      else if(a_upper(x)) bset(n, x+32);
    }
  }
  if(hi >= 0x80) add_range(n, lo < 0x80 ? 0x80 : lo, hi);
}
static void cls_add_pred(renode *n, int (*pred)(int), int neg){
  for(int x = 0; x < 256; x++){
    int in = pred(x) != 0;
    if(neg) in = !in;
    if(in) bset(n, x);
  }
  if(neg) add_range(n, 0x80, 0x10FFFF);
}
static renode *new_class(void){
  renode *n = nnew(RX_CLASS);
  n->bmap = xcalloc(32, 1);
  return n;
}
static void add_shorthand(renode *n, int d){
  switch(d){
    case 'd': cls_add_pred(n, a_digit, 0); break;
    case 'D': cls_add_pred(n, a_digit, 1); break;
    case 'w': cls_add_pred(n, a_word, 0); break;
    case 'W': cls_add_pred(n, a_word, 1); break;
    case 's': cls_add_pred(n, a_space, 0); break;
    case 'S': cls_add_pred(n, a_space, 1); break;
    case 'h': cls_add_pred(n, a_xdigit, 0); break;
    case 'H': cls_add_pred(n, a_xdigit, 1); break;
    default: break;
  }
}
static int add_posix(renode *n, const char *s, long len, int neg, int ci){
  static const struct { const char *nm; int (*fn)(int); } tbl[] = {
    {"alpha",a_alpha},{"digit",a_digit},{"space",a_space},{"upper",a_upper},
    {"lower",a_lower},{"alnum",a_alnum},{"punct",a_punct},{"xdigit",a_xdigit},
    {"word",a_word},{"blank",a_blank},{"cntrl",a_cntrl},{"print",a_print},
    {"graph",a_graph},{"ascii",a_ascii}
  };
  for(unsigned i = 0; i < sizeof(tbl)/sizeof(tbl[0]); i++){
    if((long)strlen(tbl[i].nm) == len && memcmp(tbl[i].nm, s, (size_t)len) == 0){
      if(ci && !neg && (tbl[i].fn == a_upper || tbl[i].fn == a_lower)){
        cls_add_pred(n, a_alpha, 0);
        return 1;
      }
      cls_add_pred(n, tbl[i].fn, neg);
      return 1;
    }
  }
  return 0;
}

/* ===================== parser ===================== */
typedef struct {
  const char *p, *e;
  int opts;
  int ngroups;
  int dontcap;
  const char *err;
  char **names; int *nameidx; int nnames;
} pctx;

static renode *p_alt(pctx *c);
static renode *p_cat(pctx *c);
static renode *p_atom(pctx *c);

static int hexv(int ch){
  if(ch>='0'&&ch<='9') return ch-'0';
  if(ch>='a'&&ch<='f') return ch-'a'+10;
  if(ch>='A'&&ch<='F') return ch-'A'+10;
  return -1;
}
static void skip_x(pctx *c){
  if(!(c->opts & RE_EXTENDED)) return;
  for(;;){
    while(c->p < c->e && a_space((unsigned char)*c->p)) c->p++;
    if(c->p < c->e && *c->p == '#'){
      while(c->p < c->e && *c->p != '\n') c->p++;
      continue;
    }
    break;
  }
}
static void add_name(pctx *c, const char *s, long len, int idx){
  char *nm = xmalloc((size_t)len+1);
  memcpy(nm, s, (size_t)len); nm[len] = 0;
  c->names = xrealloc(c->names, (size_t)(c->nnames+1)*sizeof(char*));
  c->nameidx = xrealloc(c->nameidx, (size_t)(c->nnames+1)*sizeof(int));
  c->names[c->nnames] = nm; c->nameidx[c->nnames] = idx; c->nnames++;
}
static int find_name(pctx *c, const char *s, long len){
  for(int i = c->nnames-1; i >= 0; i--)
    if((long)strlen(c->names[i]) == len && memcmp(c->names[i], s, (size_t)len) == 0)
      return c->nameidx[i];
  return -1;
}

/* escape -> codepoint.  *raw means "emit this exact byte". */
static int esc_char(pctx *c, int *cp, int *raw){
  *raw = 0;
  if(c->p >= c->e){ c->err = "too short escape sequence"; return 0; }
  char ch = *c->p++;
  switch(ch){
    case 'n': *cp = '\n'; return 1;
    case 't': *cp = '\t'; return 1;
    case 'r': *cp = '\r'; return 1;
    case 'f': *cp = '\f'; return 1;
    case 'v': *cp = '\v'; return 1;
    case 'a': *cp = 7;    return 1;
    case 'e': *cp = 27;   return 1;
    case 'x': {
      if(c->p < c->e && *c->p == '{'){
        c->p++;
        int v = 0, any = 0;
        while(c->p < c->e && hexv((unsigned char)*c->p) >= 0){ v = v*16 + hexv((unsigned char)*c->p); c->p++; any = 1; }
        if(c->p >= c->e || *c->p != '}' || !any){ c->err = "invalid hex escape"; return 0; }
        c->p++; *cp = v; return 1;
      }
      int v = 0, nd = 0;
      while(nd < 2 && c->p < c->e && hexv((unsigned char)*c->p) >= 0){ v = v*16 + hexv((unsigned char)*c->p); c->p++; nd++; }
      if(!nd){ c->err = "invalid hex escape"; return 0; }
      *cp = v; *raw = 1; return 1;
    }
    case 'u': {
      if(c->p < c->e && *c->p == '{'){
        c->p++;
        while(c->p < c->e && (*c->p == ' ' || *c->p == '\t')) c->p++;
        int v = 0, any = 0;
        while(c->p < c->e && hexv((unsigned char)*c->p) >= 0){ v = v*16 + hexv((unsigned char)*c->p); c->p++; any = 1; }
        while(c->p < c->e && *c->p != '}') c->p++;
        if(c->p >= c->e || !any){ c->err = "invalid Unicode escape"; return 0; }
        c->p++; *cp = v; return 1;
      }
      int v = 0, nd = 0;
      while(nd < 4 && c->p < c->e && hexv((unsigned char)*c->p) >= 0){ v = v*16 + hexv((unsigned char)*c->p); c->p++; nd++; }
      if(nd < 4){ c->err = "invalid Unicode escape"; return 0; }
      *cp = v; return 1;
    }
    case 'c': {
      if(c->p >= c->e){ c->err = "too short control escape"; return 0; }
      int b = (unsigned char)*c->p++;
      if(b == '\\' && c->p < c->e) b = (unsigned char)*c->p++;
      *cp = b & 0x1f; *raw = 1; return 1;
    }
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
      int v = ch - '0', nd = 1;
      while(nd < 3 && c->p < c->e && *c->p >= '0' && *c->p <= '7'){ v = v*8 + (*c->p - '0'); c->p++; nd++; }
      *cp = v & 0xff; *raw = 1; return 1;
    }
    default:
      if((unsigned char)ch >= 0x80){
        c->p--;
        long l = rb_enc_mbclen(enc_utf8, c->p, c->e);
        if(l == 1){ *cp = (unsigned char)*c->p; *raw = 1; }
        else *cp = rb_enc_codepoint(enc_utf8, c->p, c->e, NULL);
        c->p += l;
        return 1;
      }
      *cp = (unsigned char)ch;
      return 1;
  }
}

static renode *lit_node(pctx *c, int cp, int raw){
  renode *n = nnew(RX_CHAR);
  if(raw || cp < 0x80){ n->lit[0] = (char)cp; n->litlen = 1; }
  else n->litlen = (int)rb_enc_mbcput(enc_utf8, cp, n->lit);
  n->ci = (short)((c->opts & RE_IGNORECASE) && n->litlen == 1);
  return n;
}

/* one class element: 1 = *cp filled, 0 = consumed (shorthand) or error */
static int cls_char(pctx *c, renode *n, int *cp){
  if(c->p >= c->e){ c->err = "premature end of char-class"; return 0; }
  if(*c->p == '\\'){
    c->p++;
    if(c->p >= c->e){ c->err = "too short escape sequence"; return 0; }
    char d = *c->p;
    if(d=='d'||d=='D'||d=='w'||d=='W'||d=='s'||d=='S'||d=='h'||d=='H'){
      c->p++; add_shorthand(n, d); return 0;
    }
    if(d == 'b'){ c->p++; *cp = 8; return 1; }
    int raw;
    if(!esc_char(c, cp, &raw)) return 0;
    return 1;
  }
  long l = rb_enc_mbclen(enc_utf8, c->p, c->e);
  if(l == 1) *cp = (unsigned char)*c->p;
  else *cp = rb_enc_codepoint(enc_utf8, c->p, c->e, NULL);
  c->p += l;
  return 1;
}

static renode *p_class(pctx *c){
  c->p++;                                  /* '[' */
  renode *n = new_class();
  int ci = (c->opts & RE_IGNORECASE) != 0;
  if(c->p < c->e && *c->p == '^'){ n->neg = 1; c->p++; }
  int first = 1;
  for(;;){
    if(c->p >= c->e){ c->err = "premature end of char-class"; return NULL; }
    if(*c->p == ']' && !first){ c->p++; break; }
    first = 0;
    if(*c->p == '[' && c->p+1 < c->e && c->p[1] == ':'){
      const char *q = c->p + 2; int pneg = 0;
      if(q < c->e && *q == '^'){ pneg = 1; q++; }
      const char *s = q;
      while(q < c->e && *q != ':' && *q != ']') q++;
      if(q+1 < c->e && q[0] == ':' && q[1] == ']'){
        if(!add_posix(n, s, q - s, pneg, ci)){ c->err = "invalid POSIX bracket type"; return NULL; }
        c->p = q + 2;
        continue;
      }
    }
    int lo;
    if(!cls_char(c, n, &lo)){ if(c->err) return NULL; continue; }
    if(c->p + 1 < c->e && *c->p == '-' && c->p[1] != ']'){
      const char *save = c->p;
      c->p++;
      int hi;
      if(!cls_char(c, n, &hi)){
        if(c->err) return NULL;
        c->p = save;
        cls_add(n, lo, lo, ci);
        continue;
      }
      if(hi < lo){ c->err = "empty range in char class"; return NULL; }
      cls_add(n, lo, hi, ci);
    } else {
      cls_add(n, lo, lo, ci);
    }
  }
  return n;
}

static int has_capture(renode *n){
  for(; n; n = n->next){
    if(n->type == RX_GROUP && n->gnum) return 1;
    if(n->sub && has_capture(n->sub)) return 1;
    if(n->type == RX_ALT)
      for(int i = 0; i < n->nalts; i++) if(has_capture(n->alts[i])) return 1;
  }
  return 0;
}

static void node_width(renode *n, long *mn, long *mx){
  long a = 0, b = 0;
  for(; n; n = n->next){
    long x = 0, y = 0;
    switch(n->type){
      case RX_CHAR: x = y = n->litlen; break;
      case RX_ANY: case RX_CLASS: x = 1; y = 4; break;
      case RX_GROUP: case RX_ATOMIC: node_width(n->sub, &x, &y); break;
      case RX_ALT: {
        long lo = -1, hi = 0;
        for(int i = 0; i < n->nalts; i++){
          long p1, p2; node_width(n->alts[i], &p1, &p2);
          if(lo < 0 || p1 < lo) lo = p1;
          if(hi >= 0){ if(p2 < 0) hi = -1; else if(p2 > hi) hi = p2; }
        }
        x = lo < 0 ? 0 : lo; y = hi;
        break; }
      case RX_REP: {
        long p1, p2; node_width(n->sub, &p1, &p2);
        x = p1 * n->min;
        y = (n->max < 0 || p2 < 0) ? -1 : p2 * n->max;
        break; }
      case RX_BACKREF: x = 0; y = -1; break;
      default: break;
    }
    a += x;
    if(b >= 0){ if(y < 0) b = -1; else b += y; }
  }
  *mn = a; *mx = b;
}

static renode *p_group(pctx *c){
  c->p++;                                  /* '(' */
  int kind = 0;                            /* 0 cap 1 plain 2 atomic 3 la 4 !la 5 lb 6 !lb */
  const char *gname = NULL; long gnamelen = 0;
  if(c->p < c->e && *c->p == '?'){
    c->p++;
    if(c->p >= c->e){ c->err = "end pattern in group"; return NULL; }
    char d = *c->p;
    if(d == ':'){ c->p++; kind = 1; }
    else if(d == '>'){ c->p++; kind = 2; }
    else if(d == '='){ c->p++; kind = 3; }
    else if(d == '!'){ c->p++; kind = 4; }
    else if(d == '#'){
      while(c->p < c->e && *c->p != ')') c->p++;
      if(c->p >= c->e){ c->err = "end pattern in group"; return NULL; }
      c->p++; return NULL;
    }
    else if(d == '<' && c->p+1 < c->e && (c->p[1] == '=' || c->p[1] == '!')){
      kind = (c->p[1] == '=') ? 5 : 6;
      c->p += 2;
    }
    else if(d == '<' || d == '\''){
      char close = (d == '<') ? '>' : '\'';
      c->p++;
      const char *s = c->p;
      while(c->p < c->e && *c->p != close) c->p++;
      if(c->p >= c->e || c->p == s){ c->err = "invalid group name"; return NULL; }
      gname = s; gnamelen = c->p - s;
      c->p++;
      kind = 0;
    }
    else {
      int on = 0, off = 0, neg = 0;
      for(;;){
        if(c->p >= c->e) break;
        char o = *c->p;
        int bit = (o=='i') ? RE_IGNORECASE : (o=='m') ? RE_MULTILINE : (o=='x') ? RE_EXTENDED : 0;
        if(o == '-'){ neg = 1; c->p++; continue; }
        if(!bit) break;
        if(neg) off |= bit; else on |= bit;
        c->p++;
      }
      if(c->p < c->e && *c->p == ')'){
        c->p++;
        c->opts = (c->opts | on) & ~off;
        return NULL;
      }
      if(c->p < c->e && *c->p == ':'){
        c->p++;
        int save = c->opts;
        c->opts = (c->opts | on) & ~off;
        renode *body = p_alt(c);
        if(c->err) return NULL;
        if(c->p >= c->e || *c->p != ')'){ c->err = "end pattern with unmatched parenthesis"; return NULL; }
        c->p++;
        c->opts = save;
        renode *g = nnew(RX_GROUP);
        g->sub = body ? body : nnew(RX_NOP);
        return g;
      }
      c->err = "undefined group option";
      return NULL;
    }
  }
  if(kind == 0 && !gname && c->dontcap) kind = 1;

  int gnum = 0;
  if(kind == 0){
    int raw = ++c->ngroups;
    gnum = (raw < RX_NCAP) ? raw : 0;
    if(gname) add_name(c, gname, gnamelen, raw);
  }
  int save = c->opts;
  renode *body = p_alt(c);
  if(c->err) return NULL;
  if(c->p >= c->e || *c->p != ')'){ c->err = "end pattern with unmatched parenthesis"; return NULL; }
  c->p++;
  c->opts = save;

  if(kind >= 3){
    renode *n = nnew(RX_LOOK);
    n->sub = body;
    n->neg = (kind == 4 || kind == 6);
    n->behind = (kind >= 5);
    n->hascap = has_capture(body);
    node_width(body, &n->minw, &n->maxw);
    return n;
  }
  if(kind == 2){
    renode *n = nnew(RX_ATOMIC);
    n->sub = body ? body : nnew(RX_NOP);
    n->hascap = has_capture(n->sub);
    return n;
  }
  renode *g = nnew(RX_GROUP);
  g->gnum = gnum;
  if(gnum){
    renode *ge = nnew(RX_GEND);
    ge->gnum = gnum;
    body = chain_append(body, ge);
  } else if(!body) body = nnew(RX_NOP);
  g->sub = body;
  return g;
}

static renode *p_escape(pctx *c){
  c->p++;                                  /* '\\' */
  if(c->p >= c->e){ c->err = "too short escape sequence"; return NULL; }
  char ch = *c->p;
  switch(ch){
    case 'd': case 'D': case 'w': case 'W':
    case 's': case 'S': case 'h': case 'H': {
      c->p++;
      renode *n = new_class();
      add_shorthand(n, ch);
      return n; }
    case 'A': c->p++; return nnew(RX_BOS);
    case 'z': c->p++; return nnew(RX_EOS);
    case 'Z': c->p++; return nnew(RX_EOSNL);
    case 'b': c->p++; return nnew(RX_WORDB);
    case 'B': c->p++; return nnew(RX_NWORDB);
    case 'G': c->p++; return nnew(RX_GPOS);
    case 'k':
      if(c->p+1 < c->e && (c->p[1] == '<' || c->p[1] == '\'')){
        char close = (c->p[1] == '<') ? '>' : '\'';
        c->p += 2;
        const char *s = c->p;
        while(c->p < c->e && *c->p != close) c->p++;
        if(c->p >= c->e){ c->err = "invalid backref name"; return NULL; }
        long nl = c->p - s;
        c->p++;
        int idx = find_name(c, s, nl);
        if(idx < 0){ c->err = "undefined name reference"; return NULL; }
        renode *n = nnew(RX_BACKREF);
        n->gnum = idx;
        n->ci = (short)((c->opts & RE_IGNORECASE) != 0);
        return n;
      }
      break;
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': {
      int v = ch - '0';
      c->p++;
      while(c->p < c->e && a_digit((unsigned char)*c->p) &&
            v*10 + (*c->p - '0') <= c->ngroups){
        v = v*10 + (*c->p - '0'); c->p++;
      }
      renode *n = nnew(RX_BACKREF);
      n->gnum = v;
      n->ci = (short)((c->opts & RE_IGNORECASE) != 0);
      return n; }
    default: break;
  }
  int cp, raw;
  if(!esc_char(c, &cp, &raw)) return NULL;
  return lit_node(c, cp, raw);
}

static renode *p_atom(pctx *c){
  char ch = *c->p;
  switch(ch){
    case '(': return p_group(c);
    case '[': return p_class(c);
    case '.': { c->p++; renode *n = nnew(RX_ANY); n->dotall = (c->opts & RE_MULTILINE) != 0; return n; }
    case '^': c->p++; return nnew(RX_BOL);
    case '$': c->p++; return nnew(RX_EOL);
    case '*': case '+':
      c->err = "target of repeat operator is not specified"; return NULL;
    case '\\': return p_escape(c);
    default: {
      long l = rb_enc_mbclen(enc_utf8, c->p, c->e);
      renode *n = nnew(RX_CHAR);
      memcpy(n->lit, c->p, (size_t)l);
      n->litlen = (int)l;
      n->ci = (short)((c->opts & RE_IGNORECASE) && l == 1);
      c->p += l;
      return n; }
  }
}

static renode *p_quant(pctx *c, renode *a){
  long mn, mx;
  if(c->p >= c->e) return a;
  char ch = *c->p;
  if(ch == '*'){ mn = 0; mx = -1; c->p++; }
  else if(ch == '+'){ mn = 1; mx = -1; c->p++; }
  else if(ch == '?'){ mn = 0; mx = 1; c->p++; }
  else if(ch == '{'){
    const char *q = c->p + 1;
    long lo = 0, hi = -1; int haslo = 0, hashi = 0;
    while(q < c->e && a_digit((unsigned char)*q)){ lo = lo*10 + (*q - '0'); haslo = 1; q++; }
    if(q < c->e && *q == ','){
      q++;
      while(q < c->e && a_digit((unsigned char)*q)){ if(!hashi) hi = 0; hi = hi*10 + (*q - '0'); hashi = 1; q++; }
    } else { hi = lo; hashi = haslo; }
    if(q < c->e && *q == '}' && (haslo || hashi)){
      c->p = q + 1;
      mn = haslo ? lo : 0;
      mx = hashi ? hi : -1;
    } else return a;
  }
  else return a;

  if(mx >= 0 && mn > mx){ c->err = "min repeat greater than max repeat"; return NULL; }
  int greedy = 1;
  if(c->p < c->e && *c->p == '?'){ greedy = 0; c->p++; }
  else if(c->p < c->e && *c->p == '+'){ c->p++; }

  renode *r = nnew(RX_REP);
  a->next = NULL;
  renode *b = a;
  while(b->type == RX_GROUP && b->gnum == 0 && b->sub && !b->sub->next) b = b->sub;
  r->simple = (b->type == RX_CHAR || b->type == RX_ANY || b->type == RX_CLASS);
  r->sub = r->simple ? b : a;
  r->min = (int)mn; r->max = (int)mx; r->greedy = greedy;
  return r;
}

static renode *p_cat(pctx *c){
  renode *head = NULL, *tail = NULL;
  for(;;){
    skip_x(c);
    if(c->p >= c->e) break;
    if(*c->p == '|' || *c->p == ')') break;
    renode *a = p_atom(c);
    if(c->err) return NULL;
    if(!a) continue;
    skip_x(c);
    a = p_quant(c, a);
    if(c->err) return NULL;
    if(!head) head = tail = a;
    else { tail->next = a; tail = a; }
  }
  return head;
}

static renode *p_alt(pctx *c){
  renode *first = p_cat(c);
  if(c->err) return NULL;
  if(c->p >= c->e || *c->p != '|') return first;
  renode *alt = nnew(RX_ALT);
  alt->alts = xmalloc(sizeof(renode*));
  alt->alts[0] = first; alt->nalts = 1;
  while(c->p < c->e && *c->p == '|'){
    c->p++;
    renode *b = p_cat(c);
    if(c->err) return NULL;
    alt->alts = xrealloc(alt->alts, (size_t)(alt->nalts+1)*sizeof(renode*));
    alt->alts[alt->nalts++] = b;
  }
  return alt;
}

static int scan_has_named(const char *p, const char *e){
  int incls = 0;
  while(p < e){
    if(*p == '\\'){ p += 2; continue; }
    if(incls){ if(*p == ']') incls = 0; p++; continue; }
    if(*p == '['){ incls = 1; p++; continue; }
    if(*p == '(' && p+2 < e && p[1] == '?'){
      if(p[2] == '\'') return 1;
      if(p[2] == '<' && p+3 < e && p[3] != '=' && p[3] != '!') return 1;
    }
    p++;
  }
  return 0;
}

struct regex *rb_reg_compile(const char *pat, long len, int opts, const char **err){
  pctx c;
  memset(&c, 0, sizeof c);
  c.p = pat; c.e = pat + len; c.opts = opts;
  c.dontcap = scan_has_named(pat, pat + len);
  renode *prog = p_alt(&c);
  if(!c.err && c.p != c.e) c.err = "unmatched close parenthesis";
  if(c.err){ if(err) *err = c.err; return NULL; }
  struct regex *re = xcalloc(1, sizeof(struct regex));
  re->prog = prog;
  re->opts = opts;
  re->ngroups = c.ngroups > RX_NCAP-1 ? RX_NCAP-1 : c.ngroups;
  re->names = c.names; re->nameidx = c.nameidx; re->nnames = c.nnames;
  re->src = xmalloc((size_t)len + 1);
  memcpy(re->src, pat, (size_t)len);
  re->src[len] = 0;
  re->srclen = len;
  if(err) *err = NULL;
  return re;
}

int rb_reg_ngroups(struct regex *re){ return re ? re->ngroups : 0; }
int rb_reg_name_count(struct regex *re){ return re ? re->nnames : 0; }
const char *rb_reg_name_at(struct regex *re, int i, int *idx){
  if(!re || i < 0 || i >= re->nnames) return NULL;
  if(idx) *idx = re->nameidx[i];
  return re->names[i];
}

/* ===================== matcher ===================== */
typedef struct {
  const char *str, *e, *gpos, *mend;
  long capb[RX_NCAP], cape[RX_NCAP];
  long steps;
  int depth, abort;
} mctx;

enum { K_NODE, K_REP, K_POS, K_AT };

typedef struct cont {
  struct cont *up;
  renode *n;
  const char *ptr;
  const char **out;
  int count;
  int kind;
} cont;

static int mrun(mctx *c, renode *n, const char *p, cont *k);
static int mcont(mctx *c, cont *k, const char *p);
static int rep_start(mctx *c, renode *rep, const char *p, cont *after, int count);

static long one_len(mctx *c, renode *n, const char *p){
  if(p >= c->e) return -1;
  switch(n->type){
    case RX_CHAR: {
      if(p + n->litlen > c->e) return -1;
      if(n->litlen == 1){
        int a = (unsigned char)*p, b = (unsigned char)n->lit[0];
        if(a == b) return 1;
        if(n->ci && lc(a) == lc(b)) return 1;
        return -1;
      }
      return memcmp(p, n->lit, (size_t)n->litlen) ? -1 : n->litlen; }
    case RX_ANY: {
      if(!n->dotall && *p == '\n') return -1;
      return rb_enc_mbclen(enc_utf8, p, c->e); }
    case RX_CLASS: {
      long l = rb_enc_mbclen(enc_utf8, p, c->e);
      int in;
      if(l == 1) in = bget(n, (unsigned char)*p);
      else {
        int cp = rb_enc_codepoint(enc_utf8, p, c->e, NULL);
        in = 0;
        for(int i = 0; i < n->nranges; i++)
          if(cp >= n->ranges[i].lo && cp <= n->ranges[i].hi){ in = 1; break; }
      }
      if(n->neg) in = !in;
      return in ? l : -1; }
    default: return -1;
  }
}

static int is_wb(mctx *c, const char *p){
  int before = (p > c->str) && a_word((unsigned char)p[-1]);
  int after  = (p < c->e)   && a_word((unsigned char)*p);
  return before != after;
}

static long *cap_save(mctx *c){
  long *b = xmalloc(sizeof(long) * RX_NCAP * 2);
  memcpy(b, c->capb, sizeof c->capb);
  memcpy(b + RX_NCAP, c->cape, sizeof c->cape);
  return b;
}
static void cap_restore(mctx *c, long *b){
  memcpy(c->capb, b, sizeof c->capb);
  memcpy(c->cape, b + RX_NCAP, sizeof c->cape);
  free(b);
}

static int m_look(mctx *c, renode *n, const char *p){
  long *saved = (n->neg && n->hascap) ? cap_save(c) : NULL;
  int found = 0;
  if(!n->behind){
    const char *endp = NULL;
    cont acc; memset(&acc, 0, sizeof acc);
    acc.kind = K_POS; acc.out = &endp;
    found = mrun(c, n->sub, p, &acc);
  } else {
    const char *lo = c->str;
    if(n->maxw >= 0 && (p - c->str) > n->maxw) lo = p - n->maxw;
    const char *q = p;
    for(;;){
      cont acc; memset(&acc, 0, sizeof acc);
      acc.kind = K_AT; acc.ptr = p;
      if(mrun(c, n->sub, q, &acc)){ found = 1; break; }
      if(c->abort) break;
      if(q <= lo) break;
      const char *r = q - 1;
      while(r > lo && ((unsigned char)*r & 0xC0) == 0x80) r--;
      q = r;
    }
  }
  if(saved) cap_restore(c, saved);
  if(n->neg) return c->abort ? 0 : !found;
  return found;
}

static int m_atomic(mctx *c, renode *n, const char *p, cont *k){
  long *saved = n->hascap ? cap_save(c) : NULL;
  const char *endp = NULL;
  cont acc; memset(&acc, 0, sizeof acc);
  acc.kind = K_POS; acc.out = &endp;
  if(mrun(c, n->sub, p, &acc) && mrun(c, n->next, endp, k)){
    free(saved);
    return 1;
  }
  if(saved) cap_restore(c, saved);
  return 0;
}

static int rep_simple(mctx *c, renode *rep, const char *p, cont *after){
  renode *b = rep->sub;
  if(!rep->greedy){
    const char *q = p;
    int n = 0;
    for(;;){
      if(n >= rep->min){
        if(mcont(c, after, q)) return 1;
        if(c->abort) return 0;
      }
      if(rep->max >= 0 && n >= rep->max) return 0;
      long l = one_len(c, b, q);
      if(l <= 0) return 0;
      q += l; n++;
      if(++c->steps > RX_MAXSTEPS){ c->abort = 1; return 0; }
    }
  }
  const char *sbuf[64];
  const char **pos = sbuf;
  int cap = 64, n = 0, r = 0;
  const char *q = p;
  pos[0] = q;
  while(rep->max < 0 || n < rep->max){
    long l = one_len(c, b, q);
    if(l <= 0) break;
    q += l; n++;
    if(n + 1 > cap){
      int nc = cap * 2;
      if(pos == sbuf){
        pos = xmalloc((size_t)nc * sizeof(char*));
        memcpy(pos, sbuf, (size_t)cap * sizeof(char*));
      } else pos = xrealloc(pos, (size_t)nc * sizeof(char*));
      cap = nc;
    }
    pos[n] = q;
    if(++c->steps > RX_MAXSTEPS){ c->abort = 1; goto done; }
  }
  if(n < rep->min) goto done;
  for(int i = n; i >= rep->min; i--){
    if(mcont(c, after, pos[i])){ r = 1; goto done; }
    if(c->abort) goto done;
  }
 done:
  if(pos != sbuf) free(pos);
  return r;
}

static int rep_start(mctx *c, renode *rep, const char *p, cont *after, int count){
  if(c->abort) return 0;
  if(++c->steps > RX_MAXSTEPS){ c->abort = 1; return 0; }
  if(count == 0 && rep->simple) return rep_simple(c, rep, p, after);
  int canmore = (rep->max < 0 || count < rep->max);
  if(count < rep->min){
    if(!canmore) return 0;
    cont kk; memset(&kk, 0, sizeof kk);
    kk.kind = K_REP; kk.n = rep; kk.count = count + 1; kk.ptr = p; kk.up = after;
    return mrun(c, rep->sub, p, &kk);
  }
  if(rep->greedy){
    if(canmore){
      cont kk; memset(&kk, 0, sizeof kk);
      kk.kind = K_REP; kk.n = rep; kk.count = count + 1; kk.ptr = p; kk.up = after;
      if(mrun(c, rep->sub, p, &kk)) return 1;
      if(c->abort) return 0;
    }
    return mcont(c, after, p);
  }
  if(mcont(c, after, p)) return 1;
  if(c->abort) return 0;
  if(canmore){
    cont kk; memset(&kk, 0, sizeof kk);
    kk.kind = K_REP; kk.n = rep; kk.count = count + 1; kk.ptr = p; kk.up = after;
    return mrun(c, rep->sub, p, &kk);
  }
  return 0;
}

static int mcont(mctx *c, cont *k, const char *p){
  if(c->abort) return 0;
  if(!k){ c->mend = p; return 1; }
  switch(k->kind){
    case K_NODE: return mrun(c, k->n, p, k->up);
    case K_REP:
      if(p == k->ptr){
        /* A zero-width iteration is allowed once -- `(\z)?` at the end of a
           string captures "" rather than not participating -- but never twice,
           which is what would loop forever.  Continue past the repetition
           instead of abandoning the branch. */
        return mcont(c, k->up, p);
      }
      return rep_start(c, k->n, p, k->up, k->count);
    case K_POS: *k->out = p; return 1;
    default: return p == k->ptr;
  }
}

static int mrun1(mctx *c, renode *n, const char *p, cont *k){
  while(n){
    switch(n->type){
      case RX_NOP: break;
      case RX_CHAR: case RX_ANY: case RX_CLASS: {
        long l = one_len(c, n, p);
        if(l < 0) return 0;
        p += l;
        break; }
      case RX_BOL:
        if(!(p == c->str || p[-1] == '\n')) return 0;
        break;
      case RX_EOL:
        if(!(p == c->e || *p == '\n')) return 0;
        break;
      case RX_BOS:
        if(p != c->str) return 0;
        break;
      case RX_EOS:
        if(p != c->e) return 0;
        break;
      case RX_EOSNL:
        if(!(p == c->e || (p + 1 == c->e && *p == '\n'))) return 0;
        break;
      case RX_WORDB:
        if(!is_wb(c, p)) return 0;
        break;
      case RX_NWORDB:
        if(is_wb(c, p)) return 0;
        break;
      case RX_GPOS:
        if(p != c->gpos) return 0;
        break;
      case RX_BACKREF: {
        int g = n->gnum;
        if(g <= 0 || g >= RX_NCAP) return 0;
        long b = c->capb[g], en = c->cape[g];
        if(b < 0 || en < 0) return 0;
        long l = en - b;
        if(p + l > c->e) return 0;
        if(n->ci){
          for(long i = 0; i < l; i++)
            if(lc((unsigned char)p[i]) != lc((unsigned char)c->str[b+i])) return 0;
        } else if(memcmp(p, c->str + b, (size_t)l)) return 0;
        p += l;
        break; }
      case RX_LOOK: {
        int ok = m_look(c, n, p);
        if(c->abort || !ok) return 0;
        break; }
      case RX_ATOMIC:
        return m_atomic(c, n, p, k);
      case RX_GROUP: {
        int g = n->gnum;
        long ob = 0, oe = 0;
        if(g){ ob = c->capb[g]; oe = c->cape[g]; c->capb[g] = p - c->str; }
        cont kk; memset(&kk, 0, sizeof kk);
        kk.kind = K_NODE; kk.n = n->next; kk.up = k;
        if(mrun(c, n->sub, p, &kk)) return 1;
        if(g){ c->capb[g] = ob; c->cape[g] = oe; }
        return 0; }
      case RX_GEND: {
        int g = n->gnum;
        long oe = c->cape[g];
        c->cape[g] = p - c->str;
        if(mcont(c, k, p)) return 1;
        c->cape[g] = oe;
        return 0; }
      case RX_ALT: {
        cont kk; memset(&kk, 0, sizeof kk);
        kk.kind = K_NODE; kk.n = n->next; kk.up = k;
        for(int i = 0; i < n->nalts; i++){
          if(mrun(c, n->alts[i], p, &kk)) return 1;
          if(c->abort) return 0;
        }
        return 0; }
      case RX_REP: {
        cont kk; memset(&kk, 0, sizeof kk);
        kk.kind = K_NODE; kk.n = n->next; kk.up = k;
        return rep_start(c, n, p, &kk, 0); }
      default: return 0;
    }
    n = n->next;
  }
  return mcont(c, k, p);
}

static int mrun(mctx *c, renode *n, const char *p, cont *k){
  if(c->abort) return 0;
  if(++c->steps > RX_MAXSTEPS){ c->abort = 1; return 0; }
  if(c->depth >= RX_MAXDEPTH){ c->abort = 1; return 0; }
  c->depth++;
  int r = mrun1(c, n, p, k);
  c->depth--;
  return r;
}

int rb_reg_search(struct regex *re, const char *str, long len, long start,
                  long *beg, long *end, int *ngroups){
  int ng = re ? re->ngroups : 0;
  if(ngroups) *ngroups = ng;
  for(int i = 0; i < RX_NCAP; i++){ beg[i] = -1; end[i] = -1; }
  if(!re || start < 0 || start > len) return 0;

  mctx c;
  memset(&c, 0, sizeof c);
  c.str = str; c.e = str + len; c.gpos = str + start;

  for(const char *p = str + start; ; ){
    for(int i = 0; i < RX_NCAP; i++){ c.capb[i] = -1; c.cape[i] = -1; }
    c.mend = NULL;
    c.depth = 0;
    if(mrun(&c, re->prog, p, NULL)){
      beg[0] = p - str;
      end[0] = c.mend - str;
      for(int i = 1; i <= ng && i < RX_NCAP; i++){ beg[i] = c.capb[i]; end[i] = c.cape[i]; }
      return 1;
    }
    if(c.abort) return 0;
    if(p >= c.e) break;
    p += rb_enc_mbclen(enc_utf8, p, c.e);
  }
  return 0;
}

/* ===================== Ruby objects ===================== */
static void *rx_alloc(size_t sz, int type, RClass *k){
  RBasic *b = xcalloc(1, sz);
  b->type = (unsigned char)type;
  b->klass = k;
  return b;
}
static RClass *reg_class(void){
  if(!rb_cRegexp) rb_cRegexp = rb_define_class("Regexp", rb_cObject);
  return rb_cRegexp;
}
static RClass *match_class(void){
  if(!rb_cMatchData) rb_cMatchData = rb_define_class("MatchData", rb_cObject);
  return rb_cMatchData;
}
static RClass *reg_err_class(void){
  static RClass *c;
  if(!c) c = rb_define_class("RegexpError", rb_eStandardError);
  return c;
}

/* byte offset -> reported offset (characters from 2.0 on) */
static long pos_out(VALUE str, long bo){
  if(bo < 0) return bo;
#if RUBY_TARGET >= 20
  RString *s = RSTR(str);
  if(s->enc == enc_utf8){
    long n = 0;
    for(long i = 0; i < bo && i < s->len; i++)
      if(((unsigned char)s->ptr[i] & 0xC0) != 0x80) n++;
    return n;
  }
#endif
  (void)str;
  return bo;
}
/* reported offset -> byte offset */
static long pos_in(VALUE str, long co){
  if(co <= 0) return co;
#if RUBY_TARGET >= 20
  RString *s = RSTR(str);
  if(s->enc == enc_utf8){
    long i = 0, n = 0;
    while(i < s->len && n < co){ i += rb_enc_mbclen(enc_utf8, s->ptr+i, s->ptr+s->len); n++; }
    return i;
  }
#endif
  (void)str;
  return co;
}
static long str_len_out(VALUE str){
#if RUBY_TARGET >= 20
  RString *s = RSTR(str);
  if(s->enc == enc_utf8){
    long i = 0, n = 0;
    while(i < s->len){ i += rb_enc_mbclen(enc_utf8, s->ptr+i, s->ptr+s->len); n++; }
    return n;
  }
#endif
  return RSTR(str)->len;
}

static VALUE str_span(VALUE str, long b, long e){
  RString *s = RSTR(str);
  if(b < 0) b = 0;
  if(e > s->len) e = s->len;
  if(e < b) e = b;
  return rb_str_new_enc(s->ptr + b, e - b, s->enc);
}

VALUE rb_reg_make_match(VALUE re, VALUE str, long *beg, long *end, int ngroups){
  if(ngroups > RX_NCAP-1) ngroups = RX_NCAP-1;
  if(ngroups < 0) ngroups = 0;
  RMatch *m = rx_alloc(sizeof(RMatch), T_MATCH, match_class());
  VALUE s = rb_str_new_enc(RSTR(str)->ptr, RSTR(str)->len, RSTR(str)->enc);
  BASIC(s)->frozen = 1;
  m->str = s;
  m->re = re;
  m->ngroups = ngroups;
  m->beg = xmalloc(sizeof(long) * (size_t)(ngroups+1));
  m->end = xmalloc(sizeof(long) * (size_t)(ngroups+1));
  for(int i = 0; i <= ngroups; i++){ m->beg[i] = beg[i]; m->end[i] = end[i]; }
  if(!NIL_P(re) && !FIXNUM_P(re) && BASIC(re)->type == T_REGEXP){
    struct regex *rx = RREG(re)->re;
    int nn = rb_reg_name_count(rx);
    if(nn > 0){
      m->names = xmalloc(sizeof(char*) * (size_t)nn);
      m->nameidx = xmalloc(sizeof(int) * (size_t)nn);
      m->nnames = 0;
      for(int i = 0; i < nn; i++){
        int idx = 0;
        const char *nm = rb_reg_name_at(rx, i, &idx);
        if(!nm) continue;
        m->names[m->nnames] = xstrdup(nm);
        m->nameidx[m->nnames] = idx;
        m->nnames++;
      }
    }
  }
  return (VALUE)m;
}

static VALUE reg_build(RClass *k, VALUE src, int opts){
  const char *err = NULL;
  struct regex *rx = rb_reg_compile(RSTR(src)->ptr, RSTR(src)->len, opts, &err);
  if(!rx)
    rb_raise(reg_err_class(), "%s: /%.*s/", err ? err : "invalid regexp",
             (int)RSTR(src)->len, RSTR(src)->ptr);
  RRegexp *r = rx_alloc(sizeof(RRegexp), T_REGEXP, k);
  r->re = rx;
  r->opts = opts;
  VALUE s = rb_str_new_enc(RSTR(src)->ptr, RSTR(src)->len, RSTR(src)->enc);
  BASIC(s)->frozen = 1;
  r->src = s;
#if RUBY_TARGET >= 31
  r->basic.frozen = 1;
#endif
  return (VALUE)r;
}

VALUE rb_reg_new_str(VALUE src, int opts){
  return reg_build(reg_class(), src, opts);
}

/* String, or nil when the operand cannot be one */
static VALUE reg_operand(VALUE v, int raise){
  if(!FIXNUM_P(v)){
    if(BASIC(v)->type == T_STRING) return v;
    if(BASIC(v)->type == T_SYMBOL){
      ID id = RSYM(v)->id;
      return rb_str_new(rb_id2name(id), rb_id_len(id));
    }
  }
  if(raise) return rb_str_to_str(v);
  return rb_check_string_type(v);
}

static VALUE do_match(VALUE re, VALUE str, long bytepos){
  long beg[RX_NCAP], end[RX_NCAP];
  int ng = 0;
  if(!rb_reg_search(RREG(re)->re, RSTR(str)->ptr, RSTR(str)->len, bytepos, beg, end, &ng))
    return Qnil;
  return rb_reg_make_match(re, str, beg, end, ng);
}

/* ===================== Regexp methods ===================== */
static VALUE reg_source(VALUE self, RProc *blk){
  (void)blk;
  return rb_str_new_enc(RSTR(RREG(self)->src)->ptr, RSTR(RREG(self)->src)->len,
                        RSTR(RREG(self)->src)->enc);
}
static VALUE reg_options(VALUE self, RProc *blk){ (void)blk; return LONG2FIX(RREG(self)->opts); }
static VALUE reg_casefold(VALUE self, RProc *blk){
  (void)blk;
  return (RREG(self)->opts & RE_IGNORECASE) ? Qtrue : Qfalse;
}
static VALUE reg_encoding(VALUE self, RProc *blk){
  (void)blk;
  return rb_enc_obj(RSTR(RREG(self)->src)->enc);
}
static VALUE reg_freeze(VALUE self, RProc *blk){ (void)blk; BASIC(self)->frozen = 1; return self; }
static VALUE reg_frozen(VALUE self, RProc *blk){ (void)blk; return BASIC(self)->frozen ? Qtrue : Qfalse; }

static VALUE reg_to_s(VALUE self, RProc *blk){
  (void)blk;
  int o = RREG(self)->opts;
  char on[4], off[4];
  int a = 0, b = 0;
  if(o & RE_MULTILINE)  on[a++] = 'm'; else off[b++] = 'm';
  if(o & RE_IGNORECASE) on[a++] = 'i'; else off[b++] = 'i';
  if(o & RE_EXTENDED)   on[a++] = 'x'; else off[b++] = 'x';
  VALUE s = rb_str_new_cstr("(?");
  if(a) rb_str_cat(s, on, a);
  if(b){ rb_str_cat_cstr(s, "-"); rb_str_cat(s, off, b); }
  rb_str_cat_cstr(s, ":");
  rb_str_cat(s, RSTR(RREG(self)->src)->ptr, RSTR(RREG(self)->src)->len);
  rb_str_cat_cstr(s, ")");
  RSTR(s)->enc = RSTR(RREG(self)->src)->enc;
  return s;
}

static VALUE reg_inspect(VALUE self, RProc *blk){
  (void)blk;
  RString *src = RSTR(RREG(self)->src);
  VALUE s = rb_str_new_cstr("/");
  for(long i = 0; i < src->len; i++){
    char ch = src->ptr[i];
    if(ch == '\\' && i + 1 < src->len){ rb_str_cat(s, src->ptr + i, 2); i++; continue; }
    if(ch == '/'){ rb_str_cat_cstr(s, "\\/"); continue; }
    rb_str_cat(s, &ch, 1);
  }
  rb_str_cat_cstr(s, "/");
  int o = RREG(self)->opts;
  if(o & RE_MULTILINE)  rb_str_cat_cstr(s, "m");
  if(o & RE_IGNORECASE) rb_str_cat_cstr(s, "i");
  if(o & RE_EXTENDED)   rb_str_cat_cstr(s, "x");
  RSTR(s)->enc = src->enc;
  return s;
}

static VALUE reg_equal(VALUE self, VALUE other, RProc *blk){
  (void)blk;
  if(self == other) return Qtrue;
  if(FIXNUM_P(other) || BASIC(other)->type != T_REGEXP) return Qfalse;
  if(RREG(self)->opts != RREG(other)->opts) return Qfalse;
  RString *a = RSTR(RREG(self)->src), *b = RSTR(RREG(other)->src);
  if(a->len != b->len || memcmp(a->ptr, b->ptr, (size_t)a->len)) return Qfalse;
  return Qtrue;
}
static VALUE reg_hash(VALUE self, RProc *blk){
  (void)blk;
  RString *s = RSTR(RREG(self)->src);
  unsigned long h = (unsigned long)RREG(self)->opts * 31u + 5381u;
  for(long i = 0; i < s->len; i++) h = h * 33u + (unsigned char)s->ptr[i];
  return LONG2FIX((long)(h & (unsigned long)FIXNUM_MAX));
}

static VALUE reg_match_op(VALUE self, VALUE sv, RProc *blk){
  (void)blk;
  if(NIL_P(sv)){ rb_set_backref(Qnil); return Qnil; }
  VALUE str = reg_operand(sv, 1);
  VALUE m = do_match(self, str, 0);
  rb_set_backref(m);
  if(NIL_P(m)) return Qnil;
  return LONG2FIX(pos_out(str, RMTCH(m)->beg[0]));
}

static VALUE reg_eqq(VALUE self, VALUE sv, RProc *blk){
  (void)blk;
  VALUE str = NIL_P(sv) ? Qnil : reg_operand(sv, 0);
  if(NIL_P(str)){ rb_set_backref(Qnil); return Qfalse; }
  VALUE m = do_match(self, str, 0);
  rb_set_backref(m);
  return NIL_P(m) ? Qfalse : Qtrue;
}

static long match_pos_arg(VALUE str, VALUE pv, int *ok){
  long p = rb_num2long(pv);
  long clen = str_len_out(str);
  *ok = 1;
  if(p < 0){
    p += clen;
    if(p < 0){ *ok = 0; return 0; }
  }
  if(p > clen){ *ok = 0; return 0; }
  return pos_in(str, p);
}

static VALUE reg_match_m(VALUE self, int argc, VALUE *argv, RProc *blk){
  if(argc < 1 || argc > 2)
    rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  if(NIL_P(argv[0])){ rb_set_backref(Qnil); return Qnil; }
  VALUE str = reg_operand(argv[0], 1);
  long bp = 0;
  if(argc == 2 && !NIL_P(argv[1])){
    int ok;
    bp = match_pos_arg(str, argv[1], &ok);
    if(!ok){ rb_set_backref(Qnil); return Qnil; }
  }
  VALUE m = do_match(self, str, bp);
  rb_set_backref(m);
  if(NIL_P(m)) return Qnil;
  if(blk) return rb_yield1(blk, m);
  return m;
}

#if RUBY_TARGET >= 27
static VALUE reg_match_p(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1 || argc > 2)
    rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  if(NIL_P(argv[0])) return Qfalse;
  VALUE str = reg_operand(argv[0], 1);
  long bp = 0;
  if(argc == 2 && !NIL_P(argv[1])){
    int ok;
    bp = match_pos_arg(str, argv[1], &ok);
    if(!ok) return Qfalse;
  }
  long beg[RX_NCAP], end[RX_NCAP];
  int ng = 0;
  return rb_reg_search(RREG(self)->re, RSTR(str)->ptr, RSTR(str)->len, bp, beg, end, &ng)
         ? Qtrue : Qfalse;
}
#endif

#if RUBY_TARGET >= 20
static VALUE reg_names(VALUE self, RProc *blk){
  (void)blk;
  struct regex *rx = RREG(self)->re;
  VALUE a = rb_ary_new();
  int n = rb_reg_name_count(rx);
  for(int i = 0; i < n; i++){
    int idx = 0;
    const char *nm = rb_reg_name_at(rx, i, &idx);
    if(!nm) continue;
    int dup = 0;
    for(long j = 0; j < RARR(a)->len; j++){
      RString *e = RSTR(RARR(a)->ptr[j]);
      if(e->len == (long)strlen(nm) && memcmp(e->ptr, nm, (size_t)e->len) == 0){ dup = 1; break; }
    }
    if(!dup) rb_ary_push(a, rb_str_new_cstr(nm));
  }
  return a;
}
static VALUE reg_named_captures(VALUE self, RProc *blk){
  (void)blk;
  struct regex *rx = RREG(self)->re;
  VALUE h = rb_hash_new();
  int n = rb_reg_name_count(rx);
  for(int i = 0; i < n; i++){
    int idx = 0;
    const char *nm = rb_reg_name_at(rx, i, &idx);
    if(!nm) continue;
    VALUE key = rb_str_new_cstr(nm);
    VALUE cur = rb_hash_lookup(h, key, Qundef);
    if(cur == Qundef){ cur = rb_ary_new(); rb_hash_aset(h, key, cur); }
    rb_ary_push(cur, LONG2FIX(idx));
  }
  return h;
}
#endif

/* ---- Regexp singleton methods ---- */
static VALUE reg_quote_str(VALUE str){
  RString *s = RSTR(str);
  VALUE out = rb_str_new_enc("", 0, s->enc);
  for(long i = 0; i < s->len; i++){
    char ch = s->ptr[i];
    switch(ch){
      case '[': case ']': case '{': case '}': case '(': case ')': case '|':
      case '-': case '*': case '.': case '\\': case '?': case '+': case '^':
      case '$': case '#': case ' ':
        rb_str_cat_cstr(out, "\\"); rb_str_cat(out, &ch, 1); break;
      case '\t': rb_str_cat_cstr(out, "\\t"); break;
      case '\n': rb_str_cat_cstr(out, "\\n"); break;
      case '\r': rb_str_cat_cstr(out, "\\r"); break;
      case '\f': rb_str_cat_cstr(out, "\\f"); break;
      case '\v': rb_str_cat_cstr(out, "\\v"); break;
      default: rb_str_cat(out, &ch, 1); break;
    }
  }
  return out;
}
static VALUE reg_s_quote(VALUE self, VALUE v, RProc *blk){
  (void)self; (void)blk;
  return reg_quote_str(reg_operand(v, 1));
}

static int opts_from_value(VALUE o){
  if(NIL_P(o) || o == Qfalse) return 0;
  if(FIXNUM_P(o)) return (int)FIX2LONG(o) & (RE_IGNORECASE|RE_EXTENDED|RE_MULTILINE);
  if(BASIC(o)->type == T_STRING){
    int r = 0;
    RString *s = RSTR(o);
    for(long i = 0; i < s->len; i++){
      switch(s->ptr[i]){
        case 'i': r |= RE_IGNORECASE; break;
        case 'm': r |= RE_MULTILINE; break;
        case 'x': r |= RE_EXTENDED; break;
        default: rb_raise(rb_eArgError, "unknown regexp option: %s", rb_str_cstr(o));
      }
    }
    return r;
  }
  if(BASIC(o)->type == T_HASH) return 0;
  return RE_IGNORECASE;
}

static VALUE reg_s_new(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  if(argc < 1) rb_raise(rb_eArgError, "wrong number of arguments (given 0, expected 1..2)");
  RClass *k = (!FIXNUM_P(self) && (BASIC(self)->type == T_CLASS)) ? RCLS(self) : reg_class();
  VALUE src;
  int opts = 0;
  if(!FIXNUM_P(argv[0]) && BASIC(argv[0])->type == T_REGEXP){
    src = RREG(argv[0])->src;
    opts = RREG(argv[0])->opts;
  } else {
    src = rb_str_to_str(argv[0]);
    if(argc >= 2) opts = opts_from_value(argv[1]);
  }
  return reg_build(k, src, opts);
}

static VALUE reg_s_union(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  RClass *k = (!FIXNUM_P(self) && (BASIC(self)->type == T_CLASS)) ? RCLS(self) : reg_class();
  VALUE ary;
  if(argc == 1){
    VALUE a = rb_check_array_type(argv[0]);
    if(!NIL_P(a)) ary = a;
    else { ary = rb_ary_new(); rb_ary_push(ary, argv[0]); }
  } else {
    ary = rb_ary_new_from(argc, argv);
  }
  long n = RARR(ary)->len;
  if(n == 0) return reg_build(k, rb_str_new_cstr("(?!)"), 0);
  VALUE s = rb_str_new_cstr("");
  for(long i = 0; i < n; i++){
    VALUE v = RARR(ary)->ptr[i];
    if(i) rb_str_cat_cstr(s, "|");
    VALUE t;
    if(!FIXNUM_P(v) && BASIC(v)->type == T_REGEXP) t = reg_to_s(v, NULL);
    else t = reg_quote_str(reg_operand(v, 1));
    rb_str_cat(s, RSTR(t)->ptr, RSTR(t)->len);
    if(RSTR(t)->enc == enc_utf8) RSTR(s)->enc = enc_utf8;
  }
  return reg_build(k, s, 0);
}

static VALUE reg_s_last_match(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)self; (void)blk;
  VALUE m = rb_get_backref();
  if(argc == 0 || NIL_P(argv[0])) return m;
  if(NIL_P(m)) return Qnil;
  return rb_call(m, ID_CACHED("[]"), 1, argv, NULL);
}

/* ===================== MatchData methods ===================== */
static VALUE mtch_group(VALUE self, long i){
  RMatch *m = RMTCH(self);
  if(i < 0) i += m->ngroups + 1;
  if(i < 0 || i > m->ngroups) return Qnil;
  if(m->beg[i] < 0 || m->end[i] < 0) return Qnil;
  return str_span(m->str, m->beg[i], m->end[i]);
}

#if RUBY_TARGET >= 20
static int mtch_name_index(RMatch *m, const char *nm, long len){
  int found = -1;
  for(int i = 0; i < m->nnames; i++){
    if((long)strlen(m->names[i]) != len || memcmp(m->names[i], nm, (size_t)len)) continue;
    int idx = m->nameidx[i];
    if(idx <= m->ngroups && m->beg[idx] >= 0) found = idx;
    else if(found < 0) found = idx;
  }
  return found;
}
static int mtch_name_of(RMatch *m, int idx, const char **out){
  for(int i = 0; i < m->nnames; i++)
    if(m->nameidx[i] == idx){ *out = m->names[i]; return 1; }
  return 0;
}
#endif

/* index argument: Integer, or (>= 2.0) a group name */
static long mtch_index_arg(VALUE self, VALUE v){
  (void)self;
  if(!FIXNUM_P(v)){
    int t = BASIC(v)->type;
    if(t == T_STRING || t == T_SYMBOL){
#if RUBY_TARGET >= 20
      const char *nm; long nl;
      VALUE s = (t == T_SYMBOL) ? rb_str_new(rb_id2name(RSYM(v)->id), rb_id_len(RSYM(v)->id)) : v;
      nm = RSTR(s)->ptr; nl = RSTR(s)->len;
      int idx = mtch_name_index(RMTCH(self), nm, nl);
      if(idx < 0) rb_raise(rb_eIndexError, "undefined group name reference: %.*s", (int)nl, nm);
      return idx;
#else
      rb_raise(rb_eTypeError, "no implicit conversion of %s into Integer", rb_obj_classname(v));
#endif
    }
  }
  return rb_num2long(v);
}

static VALUE mtch_to_a(VALUE self, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  VALUE a = rb_ary_new();
  for(int i = 0; i <= m->ngroups; i++) rb_ary_push(a, mtch_group(self, i));
  return a;
}
static VALUE mtch_captures(VALUE self, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  VALUE a = rb_ary_new();
  for(int i = 1; i <= m->ngroups; i++) rb_ary_push(a, mtch_group(self, i));
  return a;
}

static VALUE mtch_range(VALUE self, VALUE r){
  RMatch *m = RMTCH(self);
  long n = m->ngroups + 1;
  VALUE bv = RRNG(r)->beg, ev = RRNG(r)->end;
  long b = NIL_P(bv) ? 0 : rb_num2long(bv);
  long e;
  if(NIL_P(ev)) e = n;
  else {
    e = rb_num2long(ev);
    if(e < 0) e += n;
    if(!RRNG(r)->excl) e += 1;
  }
  if(b < 0) b += n;
  if(b < 0 || b > n) return Qnil;
  if(e > n) e = n;
  VALUE a = rb_ary_new();
  for(long i = b; i < e; i++) rb_ary_push(a, mtch_group(self, i));
  return a;
}

static VALUE mtch_aref(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  if(argc == 2){
    long st = rb_num2long(argv[0]), ln = rb_num2long(argv[1]);
    long n = m->ngroups + 1;
    if(st < 0) st += n;
    if(st < 0 || st > n || ln < 0) return Qnil;
    if(st + ln > n) ln = n - st;
    VALUE a = rb_ary_new();
    for(long i = 0; i < ln; i++) rb_ary_push(a, mtch_group(self, st + i));
    return a;
  }
  if(argc != 1) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1..2)", argc);
  VALUE v = argv[0];
  if(!FIXNUM_P(v) && BASIC(v)->type == T_RANGE) return mtch_range(self, v);
  return mtch_group(self, mtch_index_arg(self, v));
}

static VALUE mtch_values_at(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk;
  VALUE a = rb_ary_new();
  for(int i = 0; i < argc; i++){
    VALUE v = argv[i];
    if(!FIXNUM_P(v) && BASIC(v)->type == T_RANGE){
      VALUE sub = mtch_range(self, v);
      if(NIL_P(sub)) continue;
      for(long j = 0; j < RARR(sub)->len; j++) rb_ary_push(a, RARR(sub)->ptr[j]);
      continue;
    }
    rb_ary_push(a, mtch_group(self, mtch_index_arg(self, v)));
  }
  return a;
}

static long mtch_check_index(VALUE self, VALUE v){
  RMatch *m = RMTCH(self);
  long i = mtch_index_arg(self, v);
  if(i < 0) i += m->ngroups + 1;
  if(i < 0 || i > m->ngroups) rb_raise(rb_eIndexError, "index %ld out of matches", i);
  return i;
}

static VALUE mtch_begin(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  long i = mtch_check_index(self, v);
  if(m->beg[i] < 0) return Qnil;
  return LONG2FIX(pos_out(m->str, m->beg[i]));
}
static VALUE mtch_end(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  long i = mtch_check_index(self, v);
  if(m->end[i] < 0) return Qnil;
  return LONG2FIX(pos_out(m->str, m->end[i]));
}
static VALUE mtch_offset(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  long i = mtch_check_index(self, v);
  VALUE a = rb_ary_new();
  if(m->beg[i] < 0){ rb_ary_push(a, Qnil); rb_ary_push(a, Qnil); return a; }
  rb_ary_push(a, LONG2FIX(pos_out(m->str, m->beg[i])));
  rb_ary_push(a, LONG2FIX(pos_out(m->str, m->end[i])));
  return a;
}

#if RUBY_TARGET >= 33
static VALUE mtch_byteoffset(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  long i = mtch_check_index(self, v);
  VALUE a = rb_ary_new();
  if(m->beg[i] < 0){ rb_ary_push(a, Qnil); rb_ary_push(a, Qnil); return a; }
  rb_ary_push(a, LONG2FIX(m->beg[i]));
  rb_ary_push(a, LONG2FIX(m->end[i]));
  return a;
}
#endif
#if RUBY_TARGET >= 34
static VALUE mtch_bytebegin(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  long i = mtch_check_index(self, v);
  return m->beg[i] < 0 ? Qnil : LONG2FIX(m->beg[i]);
}
static VALUE mtch_byteend(VALUE self, VALUE v, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  long i = mtch_check_index(self, v);
  return m->end[i] < 0 ? Qnil : LONG2FIX(m->end[i]);
}
#endif

static VALUE mtch_pre(VALUE self, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  return str_span(m->str, 0, m->beg[0]);
}
static VALUE mtch_post(VALUE self, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  return str_span(m->str, m->end[0], RSTR(m->str)->len);
}
static VALUE mtch_to_s(VALUE self, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  return str_span(m->str, m->beg[0], m->end[0]);
}
static VALUE mtch_string(VALUE self, RProc *blk){ (void)blk; return RMTCH(self)->str; }
static VALUE mtch_size(VALUE self, RProc *blk){ (void)blk; return LONG2FIX(RMTCH(self)->ngroups + 1); }

#if RUBY_TARGET >= 20
static VALUE mtch_names(VALUE self, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  VALUE a = rb_ary_new();
  for(int i = 0; i < m->nnames; i++){
    int dup = 0;
    for(long j = 0; j < RARR(a)->len; j++){
      RString *e = RSTR(RARR(a)->ptr[j]);
      if(e->len == (long)strlen(m->names[i]) &&
         memcmp(e->ptr, m->names[i], (size_t)e->len) == 0){ dup = 1; break; }
    }
    if(!dup) rb_ary_push(a, rb_str_new_cstr(m->names[i]));
  }
  return a;
}
#endif

#if RUBY_TARGET >= 27
static VALUE mtch_named_captures(VALUE self, int argc, VALUE *argv, RProc *blk){
  (void)blk; (void)argv;
  int symbolize = 0;
#if RUBY_TARGET >= 34
  if(argc == 1 && !FIXNUM_P(argv[0]) && BASIC(argv[0])->type == T_HASH){
    VALUE v = rb_hash_lookup(argv[0], rb_sym(ID_CACHED("symbolize_names")), Qfalse);
    symbolize = RTEST(v);
    argc = 0;
  }
#endif
  if(argc != 0) rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 0)", argc);
  RMatch *m = RMTCH(self);
  VALUE h = rb_hash_new();
  for(int i = 0; i < m->nnames; i++){
    VALUE key = rb_str_new_cstr(m->names[i]);
    if(symbolize) key = rb_sym_from_str(key);
    int idx = m->nameidx[i];
    VALUE val = (idx <= m->ngroups) ? mtch_group(self, idx) : Qnil;
    VALUE old = rb_hash_lookup(h, key, Qundef);
    if(old != Qundef && NIL_P(val)) continue;
    rb_hash_aset(h, key, val);
  }
  return h;
}
#endif

#if RUBY_TARGET >= 33
static VALUE mtch_deconstruct(VALUE self, RProc *blk){ return mtch_captures(self, blk); }
static VALUE mtch_deconstruct_keys(VALUE self, VALUE keys, RProc *blk){
  (void)blk;
  RMatch *m = RMTCH(self);
  VALUE h = rb_hash_new();
  if(NIL_P(keys)){
    for(int i = 0; i < m->nnames; i++){
      VALUE key = rb_sym(rb_intern(m->names[i]));
      int idx = m->nameidx[i];
      rb_hash_aset(h, key, (idx <= m->ngroups) ? mtch_group(self, idx) : Qnil);
    }
    return h;
  }
  if(FIXNUM_P(keys) || BASIC(keys)->type != T_ARRAY)
    rb_raise(rb_eTypeError, "wrong argument type %s (expected Array)", rb_obj_classname(keys));
  if(RARR(keys)->len > m->nnames) return h;
  for(long i = 0; i < RARR(keys)->len; i++){
    VALUE k = RARR(keys)->ptr[i];
    if(FIXNUM_P(k) || BASIC(k)->type != T_SYMBOL)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Symbol)", rb_obj_classname(k));
    const char *nm = rb_id2name(RSYM(k)->id);
    int idx = mtch_name_index(m, nm, (long)strlen(nm));
    if(idx < 0) return h;
    rb_hash_aset(h, k, (idx <= m->ngroups) ? mtch_group(self, idx) : Qnil);
  }
  return h;
}
#endif

static VALUE mtch_inspect(VALUE self, RProc *blk){
  (void)blk;
#if RUBY_TARGET < 20
  return rb_any_to_s(self);
#else
  RMatch *m = RMTCH(self);
  VALUE s = rb_str_new_cstr("#<MatchData");
  for(int i = 0; i <= m->ngroups; i++){
    rb_str_cat_cstr(s, " ");
    if(i > 0){
      const char *nm = NULL;
      if(mtch_name_of(m, i, &nm)){ rb_str_cat_cstr(s, nm); }
      else { char buf[32]; snprintf(buf, sizeof buf, "%d", i); rb_str_cat_cstr(s, buf); }
      rb_str_cat_cstr(s, ":");
    }
    VALUE g = mtch_group(self, i);
    if(NIL_P(g)) rb_str_cat_cstr(s, "nil");
    else {
      VALUE q = rb_inspect(g);
      rb_str_cat(s, RSTR(q)->ptr, RSTR(q)->len);
    }
  }
  rb_str_cat_cstr(s, ">");
  return s;
#endif
}

/* ===================== registration ===================== */
void rb_init_regexp(void){
  RClass *R = reg_class();
  RClass *M = match_class();

  rb_define_const(R, "IGNORECASE", LONG2FIX(RE_IGNORECASE));
  rb_define_const(R, "EXTENDED",   LONG2FIX(RE_EXTENDED));
  rb_define_const(R, "MULTILINE",  LONG2FIX(RE_MULTILINE));

  rb_define_singleton_method((VALUE)R, "new",        reg_s_new,        ARGC_ANY);
  rb_define_singleton_method((VALUE)R, "compile",    reg_s_new,        ARGC_ANY);
  rb_define_singleton_method((VALUE)R, "escape",     reg_s_quote,      1);
  rb_define_singleton_method((VALUE)R, "quote",      reg_s_quote,      1);
  rb_define_singleton_method((VALUE)R, "union",      reg_s_union,      ARGC_ANY);
  rb_define_singleton_method((VALUE)R, "last_match", reg_s_last_match, ARGC_ANY);

  rb_define_method(R, "=~",         reg_match_op, 1);
  rb_define_method(R, "===",        reg_eqq,      1);
  rb_define_method(R, "match",      reg_match_m,  ARGC_ANY);
#if RUBY_TARGET >= 27
  rb_define_method(R, "match?",     reg_match_p,  ARGC_ANY);
#endif
  rb_define_method(R, "source",     reg_source,   0);
  rb_define_method(R, "options",    reg_options,  0);
  rb_define_method(R, "inspect",    reg_inspect,  0);
  rb_define_method(R, "to_s",       reg_to_s,     0);
  rb_define_method(R, "==",         reg_equal,    1);
  rb_define_method(R, "eql?",       reg_equal,    1);
  rb_define_method(R, "hash",       reg_hash,     0);
#if RUBY_TARGET >= 20
  rb_define_method(R, "names",           reg_names,           0);
  rb_define_method(R, "named_captures",  reg_named_captures,  0);
#endif
  rb_define_method(R, "casefold?",  reg_casefold, 0);
  rb_define_method(R, "encoding",   reg_encoding, 0);
  rb_define_method(R, "freeze",     reg_freeze,   0);
  rb_define_method(R, "frozen?",    reg_frozen,   0);

  rb_define_method(M, "[]",         mtch_aref,      ARGC_ANY);
  rb_define_method(M, "begin",      mtch_begin,     1);
  rb_define_method(M, "end",        mtch_end,       1);
  rb_define_method(M, "offset",     mtch_offset,    1);
#if RUBY_TARGET >= 33
  rb_define_method(M, "byteoffset", mtch_byteoffset, 1);
#endif
#if RUBY_TARGET >= 34
  rb_define_method(M, "bytebegin",  mtch_bytebegin, 1);
  rb_define_method(M, "byteend",    mtch_byteend,   1);
#endif
  rb_define_method(M, "pre_match",  mtch_pre,       0);
  rb_define_method(M, "post_match", mtch_post,      0);
  rb_define_method(M, "to_a",       mtch_to_a,      0);
  rb_define_method(M, "captures",   mtch_captures,  0);
#if RUBY_TARGET >= 20
  rb_define_method(M, "names",      mtch_names,     0);
#endif
#if RUBY_TARGET >= 27
  rb_define_method(M, "named_captures", mtch_named_captures, ARGC_ANY);
#endif
#if RUBY_TARGET >= 33
  rb_define_method(M, "deconstruct",      mtch_deconstruct,      0);
  rb_define_method(M, "deconstruct_keys", mtch_deconstruct_keys, 1);
#endif
  rb_define_method(M, "size",       mtch_size,      0);
  rb_define_method(M, "length",     mtch_size,      0);
  rb_define_method(M, "to_s",       mtch_to_s,      0);
  rb_define_method(M, "values_at",  mtch_values_at, ARGC_ANY);
  rb_define_method(M, "inspect",    mtch_inspect,   0);
  rb_define_method(M, "string",     mtch_string,    0);
}
