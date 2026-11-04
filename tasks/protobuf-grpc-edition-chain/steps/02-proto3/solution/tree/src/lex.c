/* lex.c -- the .proto tokenizer.
 *
 * Deliberately semantic-free, like the parser above it: it decides what a
 * token *is*, never what it means. Numbers carry no sign (a leading '-' is a
 * TK_SYM the parser attaches), and adjacent string literals are separate
 * TK_STRs that the parser concatenates -- both because protoc treats them that
 * way and because the option grammar needs the pieces.
 *
 * Positions are 0-based here and printed 1-based, matching protoc's
 * `file:line:col:` diagnostics.
 */
#include "ast.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

typedef struct {
  const char *p, *end, *line_start;
  int line;
  Arena *a;
  Diags *d;
  const char *file;
} Lx;

static int col_of(const Lx *x, const char *p) { return (int)(p - x->line_start); }

static void lx_newline(Lx *x) { x->line++; x->line_start = x->p + 1; }

/* Whitespace and both comment forms. An unterminated block comment is an
 * error protoc reports, so it is reported here rather than silently eating the
 * rest of the file. */
static void skip_trivia(Lx *x) {
  for (;;) {
    while (x->p < x->end && (*x->p == ' ' || *x->p == '\t' || *x->p == '\r' ||
                             *x->p == '\n' || *x->p == '\f' || *x->p == '\v')) {
      if (*x->p == '\n') lx_newline(x);
      x->p++;
    }
    if (x->p + 1 < x->end && x->p[0] == '/' && x->p[1] == '/') {
      while (x->p < x->end && *x->p != '\n') x->p++;
      continue;
    }
    if (x->p + 1 < x->end && x->p[0] == '/' && x->p[1] == '*') {
      const char *start = x->p;
      int sl = x->line, sc = col_of(x, x->p);
      x->p += 2;
      bool closed = false;
      while (x->p + 1 < x->end) {
        if (x->p[0] == '*' && x->p[1] == '/') { x->p += 2; closed = true; break; }
        if (*x->p == '\n') lx_newline(x);
        x->p++;
      }
      if (!closed) {
        x->p = x->end;
        diag_add(x->d, x->file, sl, sc, "Unexpected end of stream while parsing comment.");
        (void)start;
      }
      continue;
    }
    return;
  }
}

static bool is_ident_start(int c) { return isalpha(c) || c == '_'; }
static bool is_ident_cont(int c) { return isalnum(c) || c == '_'; }

/* One escape sequence, already past the backslash. Appends to `out`. */
static void read_escape(Lx *x, Buf *out) {
  if (x->p >= x->end) return;
  char c = *x->p++;
  switch (c) {
    case 'a': buf_byte(out, '\a'); return;
    case 'b': buf_byte(out, '\b'); return;
    case 'f': buf_byte(out, '\f'); return;
    case 'n': buf_byte(out, '\n'); return;
    case 'r': buf_byte(out, '\r'); return;
    case 't': buf_byte(out, '\t'); return;
    case 'v': buf_byte(out, '\v'); return;
    case '\\': case '\'': case '"': case '?': buf_byte(out, c); return;
    case 'x': case 'X': {
      unsigned v = 0; int n = 0;
      while (x->p < x->end && n < 2 && isxdigit((unsigned char)*x->p)) {
        char h = *x->p++;
        v = v * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0'
                                                          : (tolower((unsigned char)h) - 'a' + 10));
        n++;
      }
      if (n == 0) diag_add(x->d, x->file, x->line, col_of(x, x->p),
                           "Expected hex digits for escape sequence.");
      buf_byte(out, (char)v);
      return;
    }
    case 'u': case 'U': {
      /* \uXXXX and \UXXXXXXXX are code points, encoded as UTF-8. */
      int want = (c == 'u') ? 4 : 8;
      unsigned long cp = 0; int n = 0;
      while (x->p < x->end && n < want && isxdigit((unsigned char)*x->p)) {
        char h = *x->p++;
        cp = cp * 16 + (unsigned long)(isdigit((unsigned char)h) ? h - '0'
                                       : (tolower((unsigned char)h) - 'a' + 10));
        n++;
      }
      if (n != want) {
        diag_add(x->d, x->file, x->line, col_of(x, x->p),
                 "Expected %d hex digits for \\%c escape.", want, c);
        return;
      }
      if (cp < 0x80) buf_byte(out, (char)cp);
      else if (cp < 0x800) {
        buf_byte(out, (char)(0xC0 | (cp >> 6)));
        buf_byte(out, (char)(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        buf_byte(out, (char)(0xE0 | (cp >> 12)));
        buf_byte(out, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_byte(out, (char)(0x80 | (cp & 0x3F)));
      } else {
        buf_byte(out, (char)(0xF0 | (cp >> 18)));
        buf_byte(out, (char)(0x80 | ((cp >> 12) & 0x3F)));
        buf_byte(out, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_byte(out, (char)(0x80 | (cp & 0x3F)));
      }
      return;
    }
    default:
      if (c >= '0' && c <= '7') {
        unsigned v = (unsigned)(c - '0'); int n = 1;
        while (x->p < x->end && n < 3 && *x->p >= '0' && *x->p <= '7') {
          v = v * 8 + (unsigned)(*x->p++ - '0');
          n++;
        }
        buf_byte(out, (char)v);
        return;
      }
      diag_add(x->d, x->file, x->line, col_of(x, x->p - 1),
               "Invalid escape sequence in string literal.");
      buf_byte(out, c);
      return;
  }
}

static void lex_string(Lx *x, Token *t) {
  char quote = *x->p;
  const char *start = x->p;
  x->p++;
  Buf out; buf_init(&out, x->a);
  bool closed = false;
  while (x->p < x->end) {
    char c = *x->p;
    if (c == quote) { x->p++; closed = true; break; }
    if (c == '\n') {
      diag_add(x->d, x->file, t->line, t->col,
               "Unexpected end of string literal at end of line.");
      break;
    }
    if (c == '\\') { x->p++; read_escape(x, &out); continue; }
    buf_byte(&out, c);
    x->p++;
  }
  if (!closed && x->p >= x->end)
    diag_add(x->d, x->file, t->line, t->col, "Unexpected end of stream in string literal.");
  t->kind = TK_STR;
  t->sval = buf_str(&out);
  t->text = str_of(start, (size_t)(x->p - start));
}

static void lex_number(Lx *x, Token *t) {
  const char *start = x->p;
  bool is_float = false;

  if (x->p + 1 < x->end && x->p[0] == '0' && (x->p[1] == 'x' || x->p[1] == 'X')) {
    x->p += 2;
    while (x->p < x->end && isxdigit((unsigned char)*x->p)) x->p++;
  } else {
    while (x->p < x->end && isdigit((unsigned char)*x->p)) x->p++;
    if (x->p < x->end && *x->p == '.') {
      is_float = true;
      x->p++;
      while (x->p < x->end && isdigit((unsigned char)*x->p)) x->p++;
    }
    if (x->p < x->end && (*x->p == 'e' || *x->p == 'E')) {
      const char *save = x->p;
      x->p++;
      if (x->p < x->end && (*x->p == '+' || *x->p == '-')) x->p++;
      if (x->p < x->end && isdigit((unsigned char)*x->p)) {
        is_float = true;
        while (x->p < x->end && isdigit((unsigned char)*x->p)) x->p++;
      } else {
        x->p = save;
      }
    }
  }

  size_t n = (size_t)(x->p - start);
  char *buf = arena_alloc(x->a, n + 1);
  memcpy(buf, start, n);
  buf[n] = '\0';
  t->text = str_of(start, n);

  if (is_float) {
    t->kind = TK_FLOAT;
    t->fval = strtod(buf, NULL);
  } else {
    t->kind = TK_INT;
    errno = 0;
    /* base 0 gives C's own 0x/0 octal handling, which is protoc's too */
    unsigned long long v = strtoull(buf, NULL, 0);
    t->ovf = (errno == ERANGE);
    t->ival = (uint64_t)v;
  }
  /* A number may not run straight into an identifier character. */
  if (x->p < x->end && is_ident_start((unsigned char)*x->p))
    diag_add(x->d, x->file, t->line, t->col,
             "Need space between number and identifier.");
}

Tokens lex_file(Arena *a, Diags *d, const char *file, Str src) {
  Lx x;
  x.p = src.p;
  x.end = src.p + src.n;
  x.line_start = src.p;
  x.line = 0;
  x.a = a;
  x.d = d;
  x.file = file;

  size_t cap = 256, n = 0;
  Token *v = arena_alloc(a, cap * sizeof(Token));

  for (;;) {
    skip_trivia(&x);
    if (n + 1 >= cap) {
      size_t ncap = cap * 2;
      Token *nv = arena_alloc(a, ncap * sizeof(Token));
      memcpy(nv, v, n * sizeof(Token));
      v = nv;
      cap = ncap;
    }
    Token *t = &v[n];
    memset(t, 0, sizeof *t);
    t->line = x.line;
    t->col = col_of(&x, x.p);

    if (x.p >= x.end) {
      t->kind = TK_EOF;
      t->text = str_of(x.p, 0);
      n++;
      break;
    }

    unsigned char c = (unsigned char)*x.p;
    if (is_ident_start(c)) {
      const char *s = x.p;
      while (x.p < x.end && is_ident_cont((unsigned char)*x.p)) x.p++;
      t->kind = TK_IDENT;
      t->text = str_of(s, (size_t)(x.p - s));
    } else if (isdigit(c) ||
               (c == '.' && x.p + 1 < x.end && isdigit((unsigned char)x.p[1]))) {
      lex_number(&x, t);
    } else if (c == '"' || c == '\'') {
      lex_string(&x, t);
    } else {
      t->kind = TK_SYM;
      t->sym = (char)c;
      t->text = str_of(x.p, 1);
      x.p++;
    }
    n++;
  }

  Tokens out;
  out.v = v;
  out.n = n;
  return out;
}

const char *tok_kind_name(TokKind k) {
  switch (k) {
    case TK_EOF: return "end of file";
    case TK_IDENT: return "identifier";
    case TK_INT: return "integer";
    case TK_FLOAT: return "number";
    case TK_STR: return "string";
    case TK_SYM: return "symbol";
  }
  return "token";
}

/* The builtin scalar type names, and `group`. Anything else is a type
 * reference the build stage resolves. */
int ast_builtin_type(Str name) {
  static const struct { const char *n; int t; } tbl[] = {
    {"double", T_DOUBLE}, {"float", T_FLOAT}, {"int64", T_INT64},
    {"uint64", T_UINT64}, {"int32", T_INT32}, {"fixed64", T_FIXED64},
    {"fixed32", T_FIXED32}, {"bool", T_BOOL}, {"string", T_STRING},
    {"group", T_GROUP}, {"bytes", T_BYTES}, {"uint32", T_UINT32},
    {"sfixed32", T_SFIXED32}, {"sfixed64", T_SFIXED64},
    {"sint32", T_SINT32}, {"sint64", T_SINT64},
  };
  for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++)
    if (str_eq_c(name, tbl[i].n)) return tbl[i].t;
  return 0;
}
