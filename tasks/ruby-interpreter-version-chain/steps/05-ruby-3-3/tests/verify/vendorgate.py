#!/usr/bin/env python3
"""Check that a tree is not, or does not contain, an existing Ruby implementation.

The listed vendor markers (runner.py) are defeated by a consistent rename, so the
token SHAPE of the C sources is compared as well: comments and layout are dropped,
every identifier becomes `I` and every literal `N`/`S`/`C`, runs of table data
collapse to one token, and each 40-token window is hashed.  A fixed 1-in-8 sample
of those hashes is compared with the same sample taken from CRuby 1.8.7-p374,
2.0.0-p648, 2.7.8, 3.1.6, 3.3.6 and 3.4.1 (the interpreter core: top-level sources,
include/, internal/, missing/ -- not ext/, lib/, test/ or enc/) and mruby 3.3.0
(src/, include/, mrbgems/), shipped as `ruby-fingerprints.bin`.

Any two C interpreters share some shapes, so the limits are set from measurement,
not at zero.  The reference is recorded from C-family files only (.c .h .cc .cpp
.y .def .inc -- the set the line budget counts); the scan compares every text file
in the tree, whatever it is called, because a compiler reads a file of any name.

`vendorgate.py --build-db <out> <root>[:<skip>,...] ... --minus <dir>` re-records the
fingerprint file from source trees; it is the recorder, and the only way that file
should change.  `<dir>` is the output of `vendorgate.py --skeletons <dir>`, run where
bison, byacc, flex, gperf, re2c and lemon are installed: parser- and scanner-generator
boilerplate (bison's skeleton, generic constructor-call actions) that CRuby's parse.c
and mruby's y.tab.c share with ANY grammar compiled by the same tool.  Its windows are
removed from the list, so an original grammar compiled with bison is not taken for
CRuby's.
"""
import hashlib
import os
import re
import struct
import sys

SKIP_DIRS = {".git"}
C_EXT = (".c", ".h", ".cc", ".cpp", ".y", ".def", ".inc")

# ------------------------------------------------------------------ token shape
K = 40                  # tokens per window
P = 8                   # keep windows whose hash is 0 mod P
MIN_DISTINCT = 8        # windows this repetitive say nothing about provenance
TREE_LIMIT = 6000       # shared sampled windows, whole tree
TREE_SHARE_MIN = 1500   # ... or at least this many
TREE_SHARE = 0.25       # ... that are this share of the tree's windows
FILE_LIMIT = 400        # shared sampled windows in one file
FILE_SHARE_MIN = 60     # ... or at least this many
FILE_SHARE = 0.6        # ... that are this share of the file's windows
DB_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "ruby-fingerprints.bin")

KEYWORDS = frozenset(b"""
auto break case char const continue default do double else enum extern float for goto if
inline int long register restrict return short signed sizeof static struct switch typedef
union unsigned void volatile while _Bool _Complex _Imaginary _Alignas _Alignof _Atomic
_Generic _Noreturn _Static_assert _Thread_local
alignas alignof and and_eq asm bitand bitor bool catch char8_t char16_t char32_t class
compl concept consteval constexpr constinit const_cast co_await co_return co_yield
decltype delete dynamic_cast explicit export false friend mutable namespace new noexcept
not not_eq nullptr operator or or_eq private protected public reinterpret_cast requires
static_assert static_cast template this thread_local throw true try typeid typename using
virtual wchar_t xor xor_eq
define include ifdef ifndef endif elif undef pragma error line defined
""".split())

TOKEN_RE = re.compile(rb"""
  (?P<ws>\s+)
| (?P<lc>//[^\n]*)
| (?P<bc>/\*.*?(?:\*/|\Z))
| (?P<str>"(?:\\.|[^"\\\n])*"?)
| (?P<chr>'(?:\\.|[^'\\\n])*'?)
| (?P<num>\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.'])*)
| (?P<id>[A-Za-z_$][A-Za-z0-9_$]*)
| (?P<op>->\*?|\+\+|--|<<=|>>=|<=>|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&^|]=|::|\.\.\.|\#\#|.)
""", re.S | re.X)

_LIT = (b"N", b"S", b"C")
_TABLE = frozenset(_LIT + (b",", b"{", b"}", b"-", b"+", b"(", b")"))
_M64 = (1 << 64) - 1
_BASE = 0x100000001B3
_TOKVAL = {}


def shape(data):
    """The token stream with names, literals, comments and layout abstracted away."""
    out = []
    for m in TOKEN_RE.finditer(data):
        kind = m.lastgroup
        if kind in ("ws", "lc", "bc"):
            continue
        if kind == "str":
            if out and out[-1] == b"S":       # adjacent literals concatenate
                continue
            out.append(b"S")
        elif kind == "chr":
            out.append(b"C")
        elif kind == "num":
            out.append(b"N")
        elif kind == "id":
            tok = m.group()
            out.append(tok if tok in KEYWORDS else b"I")
        else:
            out.append(m.group())
    res = []
    i, n = 0, len(out)
    while i < n:
        if out[i] in _TABLE:
            j, lits = i, 0
            while j < n and out[j] in _TABLE:
                if out[j] in _LIT:
                    lits += 1
                j += 1
            if lits >= 4 and j - i >= 8:      # a data table: one token for the run
                res.append(b"T")
                i = j
                continue
        res.append(out[i])
        i += 1
    return res


def _tokval(tok):
    v = _TOKVAL.get(tok)
    if v is None:
        v = struct.unpack("<Q", hashlib.blake2b(tok, digest_size=8).digest())[0] | 1
        _TOKVAL[tok] = v
    return v


def _mix(x):
    x ^= x >> 30
    x = (x * 0xBF58476D1CE4E5B9) & _M64
    x ^= x >> 27
    x = (x * 0x94D049BB133111EB) & _M64
    return x ^ (x >> 31)


def fingerprints(data):
    """The sampled window hashes of one file (a rolling hash over the shape)."""
    toks = shape(data)
    fps = set()
    n = len(toks)
    if n < K:
        return fps
    vals = [_tokval(t) for t in toks]
    top = pow(_BASE, K - 1, 1 << 64)
    h = 0
    for i in range(K):
        h = (h * _BASE + vals[i]) & _M64
    i = 0
    while True:
        v = _mix(h)
        if v % P == 0 and len(set(toks[i:i + K])) >= MIN_DISTINCT:
            fps.add(v)
        if i + K >= n:
            break
        h = ((h - vals[i] * top) * _BASE + vals[i + K]) & _M64
        i += 1
    return fps


def text_files(root, skip=(), any_name=False):
    """(path, bytes) for every text file under root, the set both checks read.
    The recorder reads C-family files only; the scan (any_name) reads every text
    file, whatever it is called."""
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            if skip and os.path.relpath(path, root).startswith(skip):
                continue
            # The scan is not limited to C-family names: a compiler reads a file of
            # any name (`cc -x c core.dat`, `#include "core.dat"`), so every text file
            # is compared.  A file with a NUL byte in its first 8 KiB is binary.
            if not any_name and not name.endswith(C_EXT):
                continue
            if os.path.islink(path):
                # a source name linked to a file elsewhere in the tree is read
                # through the link (the compiler would); a link out of it is not
                real, top = os.path.realpath(path), os.path.realpath(root)
                if os.path.commonpath([real, top]) != top:
                    continue
                path = real
            blob = read_regular(path)
            if blob is None:
                continue
            if not name.endswith(C_EXT) and b"\0" in blob[:8192]:
                continue
            yield path, blob


def read_regular(path, limit=64 << 20):
    """The bytes of a regular file, read without following a symlink and without
    blocking: this runs as root over trees the candidate wrote, where a FIFO or a
    device named `x.c` would otherwise hang or exhaust the scan.  None otherwise."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        import stat as _stat
        if not _stat.S_ISREG(os.fstat(fd).st_mode):
            return None
        chunks, size = [], 0
        while size < limit:
            b = os.read(fd, min(1 << 20, limit - size))
            if not b:
                break
            chunks.append(b)
            size += len(b)
        return b"".join(chunks)
    except OSError:
        return None
    finally:
        os.close(fd)


def scan(root, db, shared, seen, flagged):
    for path, blob in text_files(root, any_name=True):
        # A binary payload has no token shape.
        if db is None or b"\0" in blob[:4096]:
            continue
        fps = fingerprints(blob)
        common = fps & db
        seen |= fps
        shared |= common
        if len(common) >= FILE_LIMIT or (
                len(common) >= FILE_SHARE_MIN and len(common) >= FILE_SHARE * len(fps)):
            flagged.append((path, len(common), len(fps)))


def load_db(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    return set(struct.unpack("<%dQ" % (len(raw) // 8), raw[: len(raw) // 8 * 8]))


def build_db(out, specs):
    """specs: <root>[:<skip>,...] ... [--minus <root> ...].  The windows of every
    root after --minus (generator boilerplate, see skeletons()) are removed."""
    minus = []
    if "--minus" in specs:
        i = specs.index("--minus")
        specs, minus = specs[:i], specs[i + 1:]
    fps = set()
    for spec in specs:
        root, _, skip = spec.partition(":")
        skip = tuple(s for s in skip.split(",") if s)
        for _path, blob in text_files(root, skip):
            if b"\0" in blob[:4096]:
                continue
            fps |= fingerprints(blob)
    drop = set()
    for root in minus:
        for _path, blob in text_files(root):
            if b"\0" not in blob[:4096]:
                drop |= fingerprints(blob)
    before = len(fps)
    fps -= drop
    with open(out, "wb") as fh:
        for v in sorted(fps):
            fh.write(struct.pack("<Q", v))
    print("%d fingerprints -> %s (%d generator-boilerplate windows removed of %d)"
          % (len(fps), out, before - len(fps), len(drop)))
    return 0


# ------------------------------------------------------- generator boilerplate
# A parser or scanner generator wraps ANY grammar in the same driver code, and
# CRuby's parse.c and mruby's y.tab.c are bison output.  Their skeleton windows
# are therefore shared with every grammar compiled by the same tool, so
# they are subtracted from the reference: `--skeletons <dir>` writes a set of
# generic grammars and scanners (nothing taken from any Ruby) and runs every
# generator it finds on them with the option sets below, and `--build-db ...
# --minus <dir>` removes every window of that output from the list.  What stays
# of parse.c is the part that is CRuby's own: its actions, and the prologue and
# epilogue (the lexer) that bison copies through verbatim.
SKELETON_INPUTS = {}
SKELETON_INPUTS["fx_lang.y"] = r'''%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct ast { int kind; struct ast *kid[4]; long num; double dbl; char *str; };
static struct ast *ast_new(int kind, struct ast *a, struct ast *b, struct ast *c, struct ast *d)
{
    struct ast *n = calloc(1, sizeof *n);
    n->kind = kind; n->kid[0] = a; n->kid[1] = b; n->kid[2] = c; n->kid[3] = d;
    return n;
}
static struct ast *ast_leaf(int kind, long num, char *str)
{
    struct ast *n = ast_new(kind, NULL, NULL, NULL, NULL);
    n->num = num; n->str = str;
    return n;
}
static struct ast *list_add(struct ast *list, struct ast *item)
{
    return ast_new(1, list, item, NULL, NULL);
}
int yylex(void);
void yyerror(const char *msg);
struct ast *root;
%}
%union { long num; double dbl; char *str; struct ast *ast; int op; }
%token <num> T_INT
%token <dbl> T_FLOAT
%token <str> T_IDENT T_STRING T_CONST
%token K_IF K_ELSE K_ELIF K_WHILE K_FOR K_IN K_DO K_END K_FUNC K_RETURN K_BREAK
%token K_CONTINUE K_NIL K_TRUE K_FALSE K_AND K_OR K_NOT K_CLASS K_THEN K_LOCAL
%token T_EQ T_NE T_LE T_GE T_SHL T_SHR T_POW T_ARROW T_DCOLON T_DOTDOT T_PLUSEQ T_MINUSEQ
%type <ast> program block stmts stmt opt_else expr primary call args arglist
%type <ast> params paramlist opt_params elements pairs pair
%type <op> assign_op
%right '=' T_PLUSEQ T_MINUSEQ
%left K_OR
%left K_AND
%right K_NOT
%nonassoc T_EQ T_NE '<' '>' T_LE T_GE
%left T_DOTDOT
%left '|'
%left '^'
%left '&'
%left T_SHL T_SHR
%left '+' '-'
%left '*' '/' '%'
%right UMINUS '!' '~'
%right T_POW
%destructor { free($$); } <str>
%start program
%%
program : stmts                                 { root = $1; }
        ;
stmts   : %empty                                { $$ = NULL; }
        | stmts stmt term                       { $$ = list_add($1, $2); }
        | stmts term                            { $$ = $1; }
        | stmts error term                      { $$ = $1; yyerrok; }
        ;
term    : '\n' | ';' ;
block   : stmts K_END                           { $$ = $1; }
        ;
stmt    : expr                                  { $$ = $1; }
        | K_IF expr then stmts opt_else K_END   { $$ = ast_new(10, $2, $4, $5, NULL); }
        | K_WHILE expr do_opt stmts K_END       { $$ = ast_new(11, $2, $4, NULL, NULL); }
        | K_FOR T_IDENT K_IN expr do_opt block  { $$ = ast_new(12, ast_leaf(2, 0, $2), $4, $6, NULL); }
        | K_FUNC T_IDENT opt_params block       { $$ = ast_new(13, ast_leaf(2, 0, $2), $3, $4, NULL); }
        | K_CLASS T_CONST block                 { $$ = ast_new(14, ast_leaf(3, 0, $2), $3, NULL, NULL); }
        | K_CLASS T_CONST '<' expr term block   { $$ = ast_new(14, ast_leaf(3, 0, $2), $6, $4, NULL); }
        | K_RETURN                              { $$ = ast_new(15, NULL, NULL, NULL, NULL); }
        | K_RETURN expr                         { $$ = ast_new(15, $2, NULL, NULL, NULL); }
        | K_BREAK                               { $$ = ast_new(16, NULL, NULL, NULL, NULL); }
        | K_CONTINUE                            { $$ = ast_new(17, NULL, NULL, NULL, NULL); }
        | K_LOCAL T_IDENT '=' expr              { $$ = ast_new(18, ast_leaf(2, 0, $2), $4, NULL, NULL); }
        | primary assign_op expr                { $$ = ast_new(19, $1, $3, NULL, NULL); $$->num = $2; }
        ;
then    : K_THEN | term | term K_THEN ;
do_opt  : K_DO | term | term K_DO ;
opt_else: %empty                                { $$ = NULL; }
        | K_ELSE stmts                          { $$ = $2; }
        | K_ELIF expr then stmts opt_else       { $$ = ast_new(10, $2, $4, $5, NULL); }
        ;
assign_op: '='                                  { $$ = '='; }
        | T_PLUSEQ                              { $$ = '+'; }
        | T_MINUSEQ                             { $$ = '-'; }
        ;
expr    : primary                               { $$ = $1; }
        | expr '+' expr                         { $$ = ast_new('+', $1, $3, NULL, NULL); }
        | expr '-' expr                         { $$ = ast_new('-', $1, $3, NULL, NULL); }
        | expr '*' expr                         { $$ = ast_new('*', $1, $3, NULL, NULL); }
        | expr '/' expr                         { $$ = ast_new('/', $1, $3, NULL, NULL); }
        | expr '%' expr                         { $$ = ast_new('%', $1, $3, NULL, NULL); }
        | expr T_POW expr                       { $$ = ast_new(T_POW, $1, $3, NULL, NULL); }
        | expr '&' expr                         { $$ = ast_new('&', $1, $3, NULL, NULL); }
        | expr '|' expr                         { $$ = ast_new('|', $1, $3, NULL, NULL); }
        | expr '^' expr                         { $$ = ast_new('^', $1, $3, NULL, NULL); }
        | expr T_SHL expr                       { $$ = ast_new(T_SHL, $1, $3, NULL, NULL); }
        | expr T_SHR expr                       { $$ = ast_new(T_SHR, $1, $3, NULL, NULL); }
        | expr T_EQ expr                        { $$ = ast_new(T_EQ, $1, $3, NULL, NULL); }
        | expr T_NE expr                        { $$ = ast_new(T_NE, $1, $3, NULL, NULL); }
        | expr '<' expr                         { $$ = ast_new('<', $1, $3, NULL, NULL); }
        | expr '>' expr                         { $$ = ast_new('>', $1, $3, NULL, NULL); }
        | expr T_LE expr                        { $$ = ast_new(T_LE, $1, $3, NULL, NULL); }
        | expr T_GE expr                        { $$ = ast_new(T_GE, $1, $3, NULL, NULL); }
        | expr T_DOTDOT expr                    { $$ = ast_new(T_DOTDOT, $1, $3, NULL, NULL); }
        | expr K_AND expr                       { $$ = ast_new(K_AND, $1, $3, NULL, NULL); }
        | expr K_OR expr                        { $$ = ast_new(K_OR, $1, $3, NULL, NULL); }
        | K_NOT expr                            { $$ = ast_new(K_NOT, $2, NULL, NULL, NULL); }
        | '-' expr %prec UMINUS                 { $$ = ast_new(UMINUS, $2, NULL, NULL, NULL); }
        | '!' expr                              { $$ = ast_new('!', $2, NULL, NULL, NULL); }
        | '~' expr                              { $$ = ast_new('~', $2, NULL, NULL, NULL); }
        ;
primary : T_INT                                 { $$ = ast_leaf(4, $1, NULL); }
        | T_FLOAT                               { $$ = ast_leaf(5, 0, NULL); $$->dbl = $1; }
        | T_STRING                              { $$ = ast_leaf(6, 0, $1); }
        | T_IDENT                               { $$ = ast_leaf(2, 0, $1); }
        | T_CONST                               { $$ = ast_leaf(3, 0, $1); }
        | K_NIL                                 { $$ = ast_leaf(7, 0, NULL); }
        | K_TRUE                                { $$ = ast_leaf(8, 1, NULL); }
        | K_FALSE                               { $$ = ast_leaf(8, 0, NULL); }
        | '(' expr ')'                          { $$ = $2; }
        | '[' elements ']'                      { $$ = ast_new(20, $2, NULL, NULL, NULL); }
        | '{' pairs '}'                         { $$ = ast_new(21, $2, NULL, NULL, NULL); }
        | call                                  { $$ = $1; }
        | primary '.' T_IDENT                   { $$ = ast_new(22, $1, ast_leaf(2, 0, $3), NULL, NULL); }
        | primary '[' expr ']'                  { $$ = ast_new(23, $1, $3, NULL, NULL); }
        | primary T_DCOLON T_CONST              { $$ = ast_new(24, $1, ast_leaf(3, 0, $3), NULL, NULL); }
        | K_FUNC opt_params block               { $$ = ast_new(25, $2, $3, NULL, NULL); }
        ;
call    : T_IDENT '(' args ')'                  { $$ = ast_new(26, ast_leaf(2, 0, $1), $3, NULL, NULL); }
        | primary '.' T_IDENT '(' args ')'      { $$ = ast_new(27, $1, ast_leaf(2, 0, $3), $5, NULL); }
        ;
args    : %empty                                { $$ = NULL; }
        | arglist                               { $$ = $1; }
        ;
arglist : expr                                  { $$ = list_add(NULL, $1); }
        | arglist ',' expr                      { $$ = list_add($1, $3); }
        ;
opt_params: %empty                              { $$ = NULL; }
        | '(' params ')'                        { $$ = $2; }
        ;
params  : %empty                                { $$ = NULL; }
        | paramlist                             { $$ = $1; }
        ;
paramlist: T_IDENT                              { $$ = list_add(NULL, ast_leaf(2, 0, $1)); }
        | paramlist ',' T_IDENT                 { $$ = list_add($1, ast_leaf(2, 0, $3)); }
        ;
elements: %empty                                { $$ = NULL; }
        | arglist                               { $$ = $1; }
        | arglist ','                           { $$ = $1; }
        ;
pairs   : %empty                                { $$ = NULL; }
        | pair                                  { $$ = list_add(NULL, $1); }
        | pairs ',' pair                        { $$ = list_add($1, $3); }
        ;
pair    : expr T_ARROW expr                     { $$ = ast_new(28, $1, $3, NULL, NULL); }
        ;
%%
void yyerror(const char *msg) { fprintf(stderr, "error: %s\n", msg); }
'''
SKELETON_INPUTS["fx_calc.y"] = r'''%{
#include <stdio.h>
#include <ctype.h>
int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "%s\n", s); }
%}
%token NUM
%left '+' '-'
%left '*' '/'
%right NEG
%%
input : /* empty */ | input line ;
line  : '\n' | exp '\n' { printf("%d\n", $1); } ;
exp   : NUM             { $$ = $1; }
      | exp '+' exp     { $$ = $1 + $3; }
      | exp '-' exp     { $$ = $1 - $3; }
      | exp '*' exp     { $$ = $1 * $3; }
      | exp '/' exp     { $$ = $1 / $3; }
      | '-' exp %prec NEG { $$ = -$2; }
      | '(' exp ')'     { $$ = $2; }
      ;
%%
int yylex(void)
{
    int c;
    while ((c = getchar()) == ' ' || c == '\t')
        continue;
    if (isdigit(c)) {
        ungetc(c, stdin);
        if (scanf("%d", &yylval) != 1)
            return 0;
        return NUM;
    }
    if (c == EOF)
        return 0;
    return c;
}
int main(void) { return yyparse(); }
'''
SKELETON_INPUTS["fx_union.y"] = r'''%define api.value.type union
%code requires { struct ast; }
%code {
#include <stdio.h>
int yylex(void);
void yyerror(const char *s);
struct ast *mk(int op, struct ast *a, struct ast *b);
struct ast *num(long v);
struct ast *var(char *name);
}
%token <long> NUM
%token <char *> NAME
%nterm <struct ast *> exp stmt list
%left '+' '-'
%left '*' '/'
%%
list : %empty          { $$ = 0; }
     | list stmt ';'   { $$ = mk(';', $1, $2); }
     ;
stmt : exp             { $$ = $1; }
     | NAME '=' exp    { $$ = mk('=', var($1), $3); }
     ;
exp  : NUM             { $$ = num($1); }
     | NAME            { $$ = var($1); }
     | exp '+' exp     { $$ = mk('+', $1, $3); }
     | exp '-' exp     { $$ = mk('-', $1, $3); }
     | exp '*' exp     { $$ = mk('*', $1, $3); }
     | exp '/' exp     { $$ = mk('/', $1, $3); }
     | '(' exp ')'     { $$ = $2; }
     ;
%%
void yyerror(const char *s) { fprintf(stderr, "%s\n", s); }
'''
SKELETON_INPUTS["fx_lex.l"] = r'''%{
#include <stdlib.h>
#include <string.h>
#include "fx_lang.tab.h"
%}
%option noyywrap nounput noinput
%x COMMENT
DIGIT   [0-9]
ID      [a-z_][A-Za-z0-9_]*
CONST   [A-Z][A-Za-z0-9_]*
%%
"if"                    { return K_IF; }
"else"                  { return K_ELSE; }
"elif"                  { return K_ELIF; }
"while"                 { return K_WHILE; }
"for"                   { return K_FOR; }
"in"                    { return K_IN; }
"do"                    { return K_DO; }
"end"                   { return K_END; }
"func"                  { return K_FUNC; }
"return"                { return K_RETURN; }
"break"                 { return K_BREAK; }
"continue"              { return K_CONTINUE; }
"nil"                   { return K_NIL; }
"true"                  { return K_TRUE; }
"false"                 { return K_FALSE; }
"and"                   { return K_AND; }
"or"                    { return K_OR; }
"not"                   { return K_NOT; }
"class"                 { return K_CLASS; }
"then"                  { return K_THEN; }
"local"                 { return K_LOCAL; }
{DIGIT}+                { yylval.num = strtol(yytext, NULL, 10); return T_INT; }
{DIGIT}+"."{DIGIT}+     { yylval.dbl = strtod(yytext, NULL); return T_FLOAT; }
{ID}                    { yylval.str = strdup(yytext); return T_IDENT; }
{CONST}                 { yylval.str = strdup(yytext); return T_CONST; }
\"([^"\\\n]|\\.)*\"     { yylval.str = strndup(yytext + 1, yyleng - 2); return T_STRING; }
"#"[^\n]*               { }
"/*"                    { BEGIN(COMMENT); }
<COMMENT>"*/"           { BEGIN(INITIAL); }
<COMMENT>.|\n           { }
"=="                    { return T_EQ; }
"!="                    { return T_NE; }
"<="                    { return T_LE; }
">="                    { return T_GE; }
"<<"                    { return T_SHL; }
">>"                    { return T_SHR; }
"**"                    { return T_POW; }
"=>"                    { return T_ARROW; }
"::"                    { return T_DCOLON; }
".."                    { return T_DOTDOT; }
"+="                    { return T_PLUSEQ; }
"-="                    { return T_MINUSEQ; }
[ \t\r]+                { }
\n                      { return '\n'; }
.                       { return yytext[0]; }
%%
'''
SKELETON_INPUTS["fx_kw.gperf"] = r'''%{
#include <string.h>
%}
struct fx_keyword { const char *name; int id; int state; };
%%
if, 1, 0
else, 2, 0
elif, 3, 0
while, 4, 0
for, 5, 0
in, 6, 0
do, 7, 0
end, 8, 0
func, 9, 0
return, 10, 1
break, 11, 1
continue, 12, 1
nil, 13, 2
true, 14, 2
false, 15, 2
and, 16, 0
or, 17, 0
not, 18, 0
class, 19, 0
then, 20, 0
local, 21, 0
self, 22, 2
super, 23, 2
yield, 24, 1
%%
'''
SKELETON_INPUTS["fx_lex.re"] = r'''#include <stddef.h>
enum { T_EOF, T_INT, T_IDENT, T_STRING, T_OP, T_ERR };
int fx_lex(const unsigned char **cursor, const unsigned char **tok)
{
    const unsigned char *YYCURSOR = *cursor, *YYMARKER;
    for (;;) {
        *tok = YYCURSOR;
        /*!re2c
            re2c:define:YYCTYPE = "unsigned char";
            re2c:yyfill:enable = 0;
            [\x00]                       { *cursor = YYCURSOR; return T_EOF; }
            [ \t\r\n]+                   { continue; }
            "#" [^\n\x00]*               { continue; }
            [0-9]+                       { *cursor = YYCURSOR; return T_INT; }
            [a-zA-Z_][a-zA-Z0-9_]*       { *cursor = YYCURSOR; return T_IDENT; }
            ["] ([^"\\\x00] | [\\].)* ["] { *cursor = YYCURSOR; return T_STRING; }
            "==" | "!=" | "<=" | ">=" | "&&" | "||" | [-+*/%<>=!(){}\[\],.;] { *cursor = YYCURSOR; return T_OP; }
            *                            { *cursor = YYCURSOR; return T_ERR; }
        */
    }
}
'''
SKELETON_INPUTS["fx_lemon.y"] = r'''%include {
#include <stdio.h>
#include <assert.h>
}
%token_type { long }
%type expr { long }
%extra_argument { long *result }
%left OR.
%left AND.
%left EQ NE.
%left PLUS MINUS.
%left TIMES DIVIDE MOD.
%right NOT.
%syntax_error { fprintf(stderr, "syntax error\n"); }
%parse_failure { fprintf(stderr, "parse failure\n"); }
program ::= expr(A).                  { *result = A; }
expr(A) ::= expr(B) PLUS expr(C).     { A = B + C; }
expr(A) ::= expr(B) MINUS expr(C).    { A = B - C; }
expr(A) ::= expr(B) TIMES expr(C).    { A = B * C; }
expr(A) ::= expr(B) DIVIDE expr(C).   { A = C ? B / C : 0; }
expr(A) ::= expr(B) MOD expr(C).      { A = C ? B % C : 0; }
expr(A) ::= expr(B) EQ expr(C).       { A = B == C; }
expr(A) ::= expr(B) NE expr(C).       { A = B != C; }
expr(A) ::= expr(B) AND expr(C).      { A = B && C; }
expr(A) ::= expr(B) OR expr(C).       { A = B || C; }
expr(A) ::= NOT expr(B).              { A = !B; }
expr(A) ::= LPAREN expr(B) RPAREN.    { A = B; }
expr(A) ::= INTEGER(B).               { A = B; }
'''



def _state_grammar(typed):
    """A grammar whose actions are all of the reentrant-constructor shape
    `$$ = f(p, <children, constants, locations>, &@$);` in a fixed pseudo-random
    order, so that the windows between two such actions are covered."""
    state = [20261013]

    def rnd(m):
        state[0] = (state[0] * 6364136223846793005 + 1442695040888963407) & _M64
        return (state[0] >> 33) % m

    toks = ["T%d" % i for i in range(24)]
    alts, rules = [], []
    for k in range(960):
        n = 1 + rnd(6)
        rhs = " ".join(toks[(k + 5 * j) % 24] for j in range(n))
        kind = rnd(16)
        if kind == 0:
            act = "$$ = $%d;" % (1 + rnd(n))
        elif kind == 1:
            act = "$$ = %s;" % ("NULL", "0")[rnd(2)]
        else:
            args = []
            for _ in range(rnd(6)):
                i = 1 + rnd(n)
                args.append(["$%d" % i, "$%d" % n, "&@%d" % i, "&@%d" % n, "0", "NULL",
                             "'+'", "f(p, $%d, &@%d)" % (i, i), "g(p, $%d)" % i][rnd(9)])
            if rnd(4):
                args.append("&@$")
            act = "$$ = c%d(%s);" % (k % 50, ", ".join(["p"] + args))
        alts.append("s%d" % k)
        rules.append("s%d : %s { %s } ;" % (k, rhs, act))
    head = "%union { struct node *node; long num; }\n" if typed else "%define api.value.type {struct node *}\n"
    if typed:
        head += "%token <node> " + " ".join(toks) + "\n%type <node> " + " ".join(alts) + "\n"
    else:
        head += "%token " + " ".join(toks) + "\n"
    return ("%{\nstruct node;\n%}\n" + head + "%%\nprogram : %empty | program item ;\nitem : "
            + "\n     | ".join(alts) + " ;\n" + "\n".join(rules) + "\n%%\n")


SKELETON_INPUTS["fx_state.y"] = _state_grammar(True)
SKELETON_INPUTS["fx_state_untyped.y"] = _state_grammar(False)

# (output name, input, directives prepended, transform, extra argv)
_PURE = "%define api.pure full\n%parse-param {struct parser *p}\n%lex-param {struct parser *p}\n"
_LOC = "%locations\n"
BISON_RUNS = [
    ("b_plain", "fx_lang.y", "", "", []),
    ("b_nolines", "fx_lang.y", "", "", ["-l"]),
    ("b_yacc", "fx_lang.y", "", "", ["-y"]),
    ("b_pure", "fx_lang.y", _PURE, "p", []),
    ("b_verbose", "fx_lang.y", "%define parse.error verbose\n", "", []),
    ("b_pure_verbose", "fx_lang.y", _PURE + "%define parse.error verbose\n", "p", []),
    ("b_detailed_loc", "fx_lang.y", _PURE + _LOC + "%define parse.error detailed\n", "pl", []),
    ("b_custom_loc", "fx_lang.y", _PURE + _LOC + "%define parse.error custom\n", "pl", []),
    ("b_verbose_loc_nolines", "fx_lang.y", _PURE + _LOC + "%define parse.error verbose\n", "pl", ["-l"]),
    ("b_loc", "fx_lang.y", _LOC, "l", []),
    ("b_push", "fx_lang.y", "%define api.pure full\n%define api.push-pull push\n", "", []),
    ("b_both_loc", "fx_lang.y", "%define api.pure full\n%define api.push-pull both\n" + _LOC, "l", []),
    ("b_trace", "fx_lang.y", "%define parse.trace\n%define parse.lac full\n%define lr.type ielr\n"
                             "%define parse.error verbose\n%token-table\n", "", []),
    ("b_prefix", "fx_lang.y", "%define api.prefix {zz}\n%debug\n" + _PURE + _LOC
                              + "%define parse.error verbose\n", "pl", []),
    ("b_glr", "fx_lang.y", "%glr-parser\n%define parse.error verbose\n", "", []),
    ("b_glr_pure_loc", "fx_lang.y", "%glr-parser\n" + _PURE.replace(" full", "") + _LOC,
     "pl", []),
    ("b_nodestr", "fx_lang.y", "", "d", []),
    ("b_nodestr_pure", "fx_lang.y", _PURE, "pd", []),
    ("b_nodestr_loc_verbose", "fx_lang.y", _PURE + _LOC + "%define parse.error verbose\n", "pld", []),
    ("b_state_loc", "fx_lang.y", _PURE + _LOC + "%define parse.error verbose\n", "sd", []),
    ("b_state_loc_destr", "fx_lang.y", _PURE + _LOC + "%define parse.error verbose\n", "s", []),
    ("b_state_loc_detailed", "fx_lang.y", _PURE + _LOC + "%define parse.error detailed\n", "sd", []),
    ("b_state_loc_nolines", "fx_lang.y", _PURE + _LOC + "%define parse.error verbose\n", "sd", ["-l"]),
    ("b_state_loc_2param", "fx_lang.y", _PURE + "%parse-param {void *scanner}\n" + _LOC
                                        + "%define parse.error verbose\n", "sd", []),
    ("b_ctor", "fx_state.y", _PURE + _LOC + "%define parse.error verbose\n", "", []),
    ("b_ctor_nolines", "fx_state.y", _PURE + _LOC + "%define parse.error verbose\n", "", ["-l"]),
    ("b_ctor_noparam", "fx_state.y", _LOC, "n", []),
    ("b_ctor_untyped", "fx_state_untyped.y", _PURE + _LOC + "%define parse.error verbose\n", "", []),
    ("b_ctor_untyped_noparam", "fx_state_untyped.y", _LOC, "n", []),
    ("b_calc", "fx_calc.y", "", "", []),
    ("b_calc_yacc", "fx_calc.y", "", "", ["-y"]),
    ("b_union", "fx_union.y", "", "", []),
    ("b_union_pure", "fx_union.y", "%define api.pure full\n%define parse.error detailed\n", "", []),
]
BYACC_RUNS = [("y_lang", []), ("y_lang_pure", ["-P"]), ("y_lang_trace", ["-t"]),
              ("y_lang_nolines", ["-l"]), ("y_calc", [])]
FLEX_RUNS = [("f_plain", "", []), ("f_full", "", ["-f"]), ("f_cf", "", ["-Cf"]),
             ("f_cF", "", ["-CF"]), ("f_nolines", "", ["-L"]),
             ("f_reentrant", "%option reentrant bison-bridge yylineno\n", [])]
GPERF_RUNS = [("g_ansi", ["-t", "-C", "-L", "ANSI-C", "-N", "fx_kw_lookup", "-H", "fx_kw_hash"]),
              ("g_plain", ["-t"]), ("g_global", ["-t", "-C", "-G", "-l", "-E"]),
              ("g_switch", ["-t", "-L", "ANSI-C", "-S", "1"])]
RE2C_RUNS = [("r_plain", []), ("r_nested", ["-s"]), ("r_bitmap", ["-b"]),
             ("r_goto", ["-g"]), ("r_nolines", ["-i"])]
LEMON_RUNS = [("l_plain", []), ("l_nolines", ["-l"])]


def _transform(text, how):
    if "n" in how:
        text = text.replace("(p, ", "(").replace("(p)", "()")
    if "d" in how:
        text = "\n".join(x for x in text.split("\n") if not x.startswith("%destructor"))
    if "s" in how:      # constructor(parser, children..., location): the reentrant style
        text = re.sub(r"\{ \$\$ = (\w+)\((.*)\);( \$\$->\w+ = \$\d;)? \}",
                      r"{ $$ = \1(p, \2, &@$);\3 }", text)
        text = text.replace("ast_leaf(", "ast_leaf(p, ")
    if "p" in how:
        text = text.replace("ast_new(", "ast_new(p, ").replace("list_add(", "list_add(p, ")
    if "l" in how:
        text = text.replace("{ $$ = ast_new(", "{ $$ = ast_at(&@$, ast_new(")
        text = text.replace("{ $$ = ast_leaf(", "{ $$ = ast_at(&@1, ast_leaf(")
    return text


def skeletons(outdir):
    """Write the generic inputs to <outdir>/in and every generator's output for
    them to <outdir>/out.  Prints, and records in out/MANIFEST, each run's result
    and each tool's version, so a re-recording can be compared with this one."""
    import shutil as _sh
    import subprocess as _sp
    src, out = os.path.join(outdir, "in"), os.path.join(outdir, "out")
    os.makedirs(src, exist_ok=True)
    os.makedirs(out, exist_ok=True)
    for name, text in SKELETON_INPUTS.items():
        with open(os.path.join(src, name), "w") as fh:
            fh.write(text)
    log = []

    def run(tag, argv, cwd=out):
        try:
            p = _sp.run(argv, cwd=cwd, capture_output=True, timeout=120)
            rc, err = p.returncode, p.stderr.decode("utf-8", "replace").strip().splitlines()
        except (OSError, _sp.TimeoutExpired) as exc:
            rc, err = -1, [str(exc)]
        log.append("%-24s rc=%d %s" % (tag, rc, (err[-1] if err and rc else "")))
        return rc

    def version(tool, flag):
        try:
            p = _sp.run([tool, flag], capture_output=True, timeout=30)
            return (p.stdout or p.stderr).decode("utf-8", "replace").strip().splitlines()[0]
        except (OSError, _sp.TimeoutExpired, IndexError):
            return "absent"

    tools = {"bison": "--version", "byacc": "-V", "flex": "--version",
             "gperf": "--version", "re2c": "--version", "lemon": "-x"}
    have = {t: _sh.which(t) is not None for t in tools}
    for t, flag in tools.items():
        log.append("tool %-6s %s" % (t, version(t, flag) if have[t] else "absent"))
    if have["bison"]:
        for tag, inp, pre, how, extra in BISON_RUNS:
            gin = os.path.join(src, tag + ".y")
            with open(gin, "w") as fh:
                fh.write(pre + _transform(SKELETON_INPUTS[inp], how))
            run(tag, ["bison", "-Wnone", "-d"] + extra
                + ["-o", os.path.join(out, tag + ".c"), gin])
    if have["byacc"]:
        lang = SKELETON_INPUTS["fx_lang.y"].replace("%empty", "")
        lang = "\n".join(x for x in lang.split("\n") if not x.startswith("%destructor"))
        for tag, extra in BYACC_RUNS:
            gin = os.path.join(src, tag + ".y")
            with open(gin, "w") as fh:
                fh.write(SKELETON_INPUTS["fx_calc.y"] if "calc" in tag else lang)
            run(tag, ["byacc", "-d"] + extra + ["-o", os.path.join(out, tag + ".c"), gin])
    if have["flex"]:
        for tag, pre, extra in FLEX_RUNS:
            text = SKELETON_INPUTS["fx_lex.l"]
            if pre:
                text = text.replace("%option noyywrap", pre + "%option noyywrap")
                text = text.replace("yylval.", "yylval->")
            gin = os.path.join(src, tag + ".l")
            with open(gin, "w") as fh:
                fh.write(text)
            run(tag, ["flex"] + extra + ["--header-file=" + os.path.join(out, tag + ".h"),
                                         "-o", os.path.join(out, tag + ".c"), gin])
    if have["gperf"]:
        for tag, extra in GPERF_RUNS:
            run(tag, ["gperf"] + extra + ["--output-file=" + os.path.join(out, tag + ".c"),
                                          os.path.join(src, "fx_kw.gperf")])
    if have["re2c"]:
        for tag, extra in RE2C_RUNS:
            run(tag, ["re2c"] + extra + ["-o", os.path.join(out, tag + ".c"),
                                         os.path.join(src, "fx_lex.re")])
    if have["lemon"]:
        tmpl = [a for a in ("/usr/share/lemon/lempar.c",) if os.path.exists(a)]
        for tag, extra in LEMON_RUNS:
            d = os.path.join(out, tag)
            os.makedirs(d, exist_ok=True)
            run(tag, ["lemon"] + extra + ["-T" + t for t in tmpl[:1]]
                + ["-d" + d, os.path.join(src, "fx_lemon.y")])
    with open(os.path.join(out, "MANIFEST"), "w") as fh:
        fh.write("\n".join(log) + "\n")
    print("\n".join(log))
    return 0 if have["bison"] else 1


def main(roots):
    if roots and roots[0] == "--build-db":
        return build_db(roots[1], roots[2:])
    if roots and roots[0] == "--skeletons":
        return skeletons(roots[1])
    try:
        db = load_db(DB_DEFAULT)
    except OSError as exc:
        # Fail closed: without the reference the shape check cannot run, and a
        # check that silently passes everything is the broken kind.
        print("fingerprint list unavailable: %s" % exc)
        return 1
    shared, seen, flagged = set(), set(), []
    for root in roots:
        if os.path.exists(root):
            scan(root, db, shared, seen, flagged)
    total = len(seen)
    bad_tree = len(shared) >= TREE_LIMIT or (
        len(shared) >= TREE_SHARE_MIN and len(shared) >= TREE_SHARE * max(total, 1))
    if flagged or bad_tree:
        print("Found code taken from an existing Ruby implementation")
        print("(%d sampled code windows of %d shared with one). The implementation"
              % (len(shared), total))
        print("must be original work.")
        for path, n, m in sorted(flagged, key=lambda x: -x[1])[:40]:
            print("  %s (%d of %d)" % (path, n, m))
        return 1
    print("vendor scan: clean (%s; %d of %d sampled code windows shared)"
          % (", ".join(roots), len(shared), total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:] or ["/app"]))
