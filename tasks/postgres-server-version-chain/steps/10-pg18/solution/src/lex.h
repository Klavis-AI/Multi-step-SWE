#ifndef PGVC_LEX_H
#define PGVC_LEX_H
#include "engine.h"

typedef enum {
    T_EOF = 0, T_IDENT, T_NUMBER, T_STRING, T_PARAM, T_OP,
    T_LPAREN, T_RPAREN, T_LBRACK, T_RBRACK, T_COMMA, T_SEMI, T_DOT, T_CAST, T_COLON
} TokKind;

typedef struct {
    TokKind kind;
    const char *start, *end;
    int   pos;
    char *sval;
    int   slen;
    int   ival;
    bool  quoted, iskw, isint, bitstr;
} Tok;

typedef struct {
    Session *s;
    const char *src, *p;
    Tok *toks; int ntok, cap;
} Lexer;

void lex_init(Lexer *L, Session *s, const char *src);
void lex_all(Lexer *L);
bool kw_exists(const char *lower);
bool kw_is_reserved(const char *lower);
#endif
