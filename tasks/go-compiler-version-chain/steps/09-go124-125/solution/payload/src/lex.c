#include "go.h"
#include <ctype.h>

static const char *tok_names[TOK__MAX] = {
    "EOF","IDENT","INT","FLOAT","IMAG","CHAR","STRING",
    "+","-","*","/","%","&","|","^","<<",">>","&^",
    "+=","-=","*=","/=","%=","&=","|=","^=","<<=",">>=","&^=",
    "&&","||","<-","++","--","==","<",">","=","!","!=","<=",">=",":=","...",
    "(","[","{",",",".",")","]","}",";",":","~",
    "break","case","chan","const","continue","default","defer","else","fallthrough",
    "for","func","go","goto","if","import","interface","map","package","range",
    "return","select","struct","switch","type","var"
};
const char *tok_str(TokKind k){ return (k>=0&&k<TOK__MAX)?tok_names[k]:"?"; }

static struct { const char *kw; TokKind t; } keywords[] = {
    {"break",TOK_BREAK},{"case",TOK_CASE},{"chan",TOK_CHAN},{"const",TOK_CONST},
    {"continue",TOK_CONTINUE},{"default",TOK_DEFAULT},{"defer",TOK_DEFER},{"else",TOK_ELSE},
    {"fallthrough",TOK_FALLTHROUGH},{"for",TOK_FOR},{"func",TOK_FUNC},{"go",TOK_GO},
    {"goto",TOK_GOTO},{"if",TOK_IF},{"import",TOK_IMPORT},{"interface",TOK_INTERFACE},
    {"map",TOK_MAP},{"package",TOK_PACKAGE},{"range",TOK_RANGE},{"return",TOK_RETURN},
    {"select",TOK_SELECT},{"struct",TOK_STRUCT},{"switch",TOK_SWITCH},{"type",TOK_TYPE},
    {"var",TOK_VAR},{NULL,0}
};

void lex_init(Lexer *lx, const char *file, const char *src, size_t len){
    memset(lx, 0, sizeof *lx);
    lx->src = src; lx->len = len; lx->off = 0;
    lx->pos.file = intern(file); lx->pos.line = 1; lx->pos.col = 1;
    /* skip UTF-8 BOM */
    if (len >= 3 && (unsigned char)src[0]==0xEF && (unsigned char)src[1]==0xBB && (unsigned char)src[2]==0xBF) lx->off = 3;
}

static int peek(Lexer *lx, int n){ return (lx->off + (size_t)n < lx->len) ? (unsigned char)lx->src[lx->off+n] : -1; }
static int cur(Lexer *lx){ return peek(lx,0); }
static void adv(Lexer *lx){
    if (lx->off >= lx->len) return;
    if (lx->src[lx->off]=='\n'){ lx->pos.line++; lx->pos.col=1; }
    else lx->pos.col++;
    lx->off++;
}

static int is_letter(int c){ return c=='_' || isalpha(c) || c >= 0x80; }
static int is_digit(int c){ return c>='0'&&c<='9'; }
static int is_hex(int c){ return isxdigit(c); }

static void lex_err(Lexer *lx, Pos p, const char *msg){ errorf(p, "%s", msg); lx->nerr++; }

static Token mk(Lexer *lx, TokKind k, Pos p, const char *lit){
    Token t; t.kind=k; t.pos=p; t.lit=lit; t.litlen=lit?strlen(lit):0; lx->prev=t; return t;
}
static Token mkn(Lexer *lx, TokKind k, Pos p, const char *lit, size_t n){
    Token t; t.kind=k; t.pos=p; t.lit=lit; t.litlen=n; lx->prev=t; return t;
}

/* Scan a number literal beginning at the current position. */
static Token scan_number(Lexer *lx, Pos p){
    size_t start = lx->off;
    int base = 10, isfloat = 0, isimag = 0;
    int sawdigit = 0, sawus = 0;
    if (cur(lx)=='0'){
        int c1 = peek(lx,1);
        if (c1=='x'||c1=='X'){
            base=16; adv(lx); adv(lx);
            while (is_hex(cur(lx)) || cur(lx)=='_'){ if(cur(lx)=='_') sawus=1; else sawdigit=1; adv(lx); }
            if (cur(lx)=='.'){
                isfloat = 1; adv(lx);
                while (is_hex(cur(lx)) || cur(lx)=='_'){ if(cur(lx)=='_') sawus=1; adv(lx); }
            }
            if (cur(lx)=='p'||cur(lx)=='P'){
                isfloat = 1; adv(lx);
                if (cur(lx)=='+'||cur(lx)=='-') adv(lx);
                while (is_digit(cur(lx))||cur(lx)=='_') adv(lx);
                if (!lang_at_least(13)) lex_err(lx, p, xsprintf("hexadecimal floating-point literal requires go1.13 or later"));
            } else if (isfloat){
                lex_err(lx, p, "hexadecimal mantissa requires a 'p' exponent");
            }
        } else if (c1=='b'||c1=='B'){
            base=2; adv(lx); adv(lx);
            if (!lang_at_least(13)) lex_err(lx, p, xsprintf("binary literal requires go1.13 or later"));
            while ((cur(lx)>='0'&&cur(lx)<='9')||cur(lx)=='_'){ if(cur(lx)=='_') sawus=1; else { sawdigit=1; if (cur(lx)>'1') lex_err(lx,p,"invalid digit in binary literal"); } adv(lx); }
        } else if (c1=='o'||c1=='O'){
            base=8; adv(lx); adv(lx);
            if (!lang_at_least(13)) lex_err(lx, p, xsprintf("0o/0O prefix requires go1.13 or later"));
            while ((cur(lx)>='0'&&cur(lx)<='9')||cur(lx)=='_'){ if(cur(lx)=='_') sawus=1; else { sawdigit=1; if (cur(lx)>'7') lex_err(lx,p,"invalid digit in octal literal"); } adv(lx); }
        } else {
            goto decimal;
        }
        goto finish;
    }
decimal:
    while (is_digit(cur(lx))||cur(lx)=='_'){ if(cur(lx)=='_') sawus=1; else sawdigit=1; adv(lx); }
    if (cur(lx)=='.' && peek(lx,1) != '.'){
        isfloat=1; adv(lx);
        while (is_digit(cur(lx))||cur(lx)=='_'){ if(cur(lx)=='_') sawus=1; adv(lx); }
    }
    if (cur(lx)=='e'||cur(lx)=='E'){
        isfloat=1; adv(lx);
        if (cur(lx)=='+'||cur(lx)=='-') adv(lx);
        while (is_digit(cur(lx))||cur(lx)=='_') adv(lx);
    }
finish:
    if (cur(lx)=='i'){ isimag=1; adv(lx); }
    if (sawus && !lang_at_least(13)) lex_err(lx, p, "underscore in numeric literal requires go1.13 or later");
    (void)sawdigit; (void)base;
    const char *lit = internn(lx->src+start, lx->off-start);
    return mk(lx, isimag?TOK_IMAG:(isfloat?TOK_FLOAT:TOK_INT), p, lit);
}

static void utf8_encode(Buf *b, unsigned r){
    if (r < 0x80) buf_putc(b,(int)r);
    else if (r < 0x800){ buf_putc(b,(int)(0xC0|(r>>6))); buf_putc(b,(int)(0x80|(r&0x3F))); }
    else if (r < 0x10000){ buf_putc(b,(int)(0xE0|(r>>12))); buf_putc(b,(int)(0x80|((r>>6)&0x3F))); buf_putc(b,(int)(0x80|(r&0x3F))); }
    else { buf_putc(b,(int)(0xF0|(r>>18))); buf_putc(b,(int)(0x80|((r>>12)&0x3F))); buf_putc(b,(int)(0x80|((r>>6)&0x3F))); buf_putc(b,(int)(0x80|(r&0x3F))); }
}

/* Scan an escape sequence; appends bytes to b. quote is '\'' or '"'. */
static int scan_escape(Lexer *lx, Buf *b, int quote, unsigned *rune_out){
    Pos p = lx->pos;
    adv(lx); /* backslash */
    int c = cur(lx);
    unsigned v=0; int n=0, base=0;
    switch (c){
    case 'a': adv(lx); buf_putc(b,7); if(rune_out)*rune_out=7; return 1;
    case 'b': adv(lx); buf_putc(b,8); if(rune_out)*rune_out=8; return 1;
    case 'f': adv(lx); buf_putc(b,12); if(rune_out)*rune_out=12; return 1;
    case 'n': adv(lx); buf_putc(b,10); if(rune_out)*rune_out=10; return 1;
    case 'r': adv(lx); buf_putc(b,13); if(rune_out)*rune_out=13; return 1;
    case 't': adv(lx); buf_putc(b,9); if(rune_out)*rune_out=9; return 1;
    case 'v': adv(lx); buf_putc(b,11); if(rune_out)*rune_out=11; return 1;
    case '\\': adv(lx); buf_putc(b,'\\'); if(rune_out)*rune_out='\\'; return 1;
    case '\'': if (quote!='\''){ lex_err(lx,p,"unknown escape sequence"); } adv(lx); buf_putc(b,'\''); if(rune_out)*rune_out='\''; return 1;
    case '"': if (quote!='"'){ lex_err(lx,p,"unknown escape sequence"); } adv(lx); buf_putc(b,'"'); if(rune_out)*rune_out='"'; return 1;
    case 'x': adv(lx); n=2; base=16; break;
    case 'u': adv(lx); n=4; base=16; break;
    case 'U': adv(lx); n=8; base=16; break;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        n=3; base=8; break;
    default: lex_err(lx,p,"unknown escape sequence"); adv(lx); return 0;
    }
    for (int i=0;i<n;i++){
        int ch = cur(lx); int d;
        if (base==16){ if (!is_hex(ch)){ lex_err(lx,p,"invalid character in hexadecimal escape"); return 0; } d = isdigit(ch)?ch-'0':(tolower(ch)-'a'+10); }
        else { if (ch<'0'||ch>'7'){ lex_err(lx,p,"invalid character in octal escape"); return 0; } d = ch-'0'; }
        v = (unsigned)(v*(unsigned)base + (unsigned)d);
        adv(lx);
    }
    if (base==8 && v > 255) lex_err(lx,p,"octal escape value > 255");
    if (rune_out) *rune_out = v;
    if (base==16 && n==2) buf_putc(b,(int)v);
    else if (base==8) buf_putc(b,(int)v);
    else {
        if (v > 0x10FFFF || (v >= 0xD800 && v < 0xE000)) lex_err(lx,p,"escape is invalid Unicode code point");
        utf8_encode(b, v);
    }
    return 1;
}

Token lex_next(Lexer *lx){
    for (;;){
        /* skip whitespace, honouring semicolon insertion */
        while (lx->off < lx->len){
            int c = cur(lx);
            if (c=='\n'){
                if (lx->insert_semi){
                    Pos p = lx->pos; lx->insert_semi=0; adv(lx);
                    return mk(lx, TOK_SEMICOLON, p, intern("\n"));
                }
                adv(lx);
            } else if (c==' '||c=='\t'||c=='\r') adv(lx);
            else break;
        }
        if (lx->off >= lx->len){
            Pos p = lx->pos;
            if (lx->insert_semi){ lx->insert_semi=0; return mk(lx,TOK_SEMICOLON,p,intern("\n")); }
            return mk(lx, TOK_EOF, p, intern(""));
        }
        Pos p = lx->pos;
        int c = cur(lx);
        if (c=='/' && peek(lx,1)=='/'){
            size_t start = lx->off;
            while (lx->off<lx->len && cur(lx)!='\n') adv(lx);
            Comment *cm = xcalloc(1,sizeof *cm); cm->pos=p; cm->text=internn(lx->src+start, lx->off-start);
            vec_push(&lx->comments, cm);
            continue;
        }
        if (c=='/' && peek(lx,1)=='*'){
            size_t start = lx->off;
            int hasnl = 0;
            adv(lx); adv(lx);
            while (lx->off<lx->len && !(cur(lx)=='*'&&peek(lx,1)=='/')){ if (cur(lx)=='\n') hasnl=1; adv(lx); }
            if (lx->off<lx->len){ adv(lx); adv(lx); }
            Comment *cm = xcalloc(1,sizeof *cm); cm->pos=p; cm->text=internn(lx->src+start, lx->off-start);
            vec_push(&lx->comments, cm);
            if (hasnl && lx->insert_semi){ lx->insert_semi=0; return mk(lx,TOK_SEMICOLON,p,intern("\n")); }
            continue;
        }
        if (is_letter(c)){
            size_t start = lx->off;
            while (is_letter(cur(lx)) || is_digit(cur(lx))) adv(lx);
            const char *name = internn(lx->src+start, lx->off-start);
            for (int i=0;keywords[i].kw;i++) if (strcmp(keywords[i].kw,name)==0){
                TokKind k = keywords[i].t;
                lx->insert_semi = (k==TOK_BREAK||k==TOK_CONTINUE||k==TOK_FALLTHROUGH||k==TOK_RETURN);
                return mk(lx,k,p,name);
            }
            lx->insert_semi = 1;
            return mk(lx,TOK_IDENT,p,name);
        }
        if (is_digit(c) || (c=='.' && is_digit(peek(lx,1)))){
            lx->insert_semi = 1;
            return scan_number(lx,p);
        }
        switch (c){
        case '"': {
            adv(lx);
            Buf b; buf_init(&b);
            while (lx->off<lx->len && cur(lx)!='"'){
                if (cur(lx)=='\n'){ lex_err(lx,p,"string literal not terminated"); break; }
                if (cur(lx)=='\\') scan_escape(lx,&b,'"',NULL);
                else { buf_putc(&b,cur(lx)); adv(lx); }
            }
            if (cur(lx)=='"') adv(lx); else lex_err(lx,p,"string literal not terminated");
            lx->insert_semi=1;
            size_t blen = b.len;
            const char *s = internn(b.p?b.p:"", blen);
            buf_free(&b);
            return mkn(lx,TOK_STRING,p,s,blen);
        }
        case '`': {
            adv(lx);
            Buf b; buf_init(&b);
            while (lx->off<lx->len && cur(lx)!='`'){
                if (cur(lx)!='\r') buf_putc(&b,cur(lx));
                adv(lx);
            }
            if (cur(lx)=='`') adv(lx); else lex_err(lx,p,"raw string literal not terminated");
            lx->insert_semi=1;
            size_t blen = b.len;
            const char *s = internn(b.p?b.p:"", blen);
            buf_free(&b);
            return mkn(lx,TOK_STRING,p,s,blen);
        }
        case '\'': {
            adv(lx);
            Buf b; buf_init(&b);
            unsigned r = 0; int n=0;
            while (lx->off<lx->len && cur(lx)!='\''){
                if (cur(lx)=='\n'){ lex_err(lx,p,"rune literal not terminated"); break; }
                if (cur(lx)=='\\'){ scan_escape(lx,&b,'\'',&r); n++; }
                else {
                    int ch = cur(lx);
                    if (ch < 0x80){ r=(unsigned)ch; buf_putc(&b,ch); adv(lx); }
                    else {
                        /* decode utf8 */
                        int extra = (ch&0xE0)==0xC0?1:((ch&0xF0)==0xE0?2:3);
                        unsigned v = (unsigned)(ch & (0x3F>>extra));
                        buf_putc(&b,ch); adv(lx);
                        for (int i=0;i<extra;i++){ v = (v<<6)|((unsigned)cur(lx)&0x3F); buf_putc(&b,cur(lx)); adv(lx); }
                        r = v;
                    }
                    n++;
                }
            }
            if (cur(lx)=='\'') adv(lx); else lex_err(lx,p,"rune literal not terminated");
            if (n==0) lex_err(lx,p,"empty rune literal");
            else if (n>1) lex_err(lx,p,"more than one character in rune literal");
            lx->insert_semi=1;
            char *lit = xsprintf("%u", r);
            buf_free(&b);
            return mk(lx,TOK_CHAR,p,intern(lit));
        }
        }
        /* operators */
        adv(lx);
        int c2 = cur(lx);
        TokKind k = TOK_EOF;
        lx->insert_semi = 0;
        switch (c){
        case '+': if(c2=='='){adv(lx);k=TOK_ADD_ASSIGN;} else if(c2=='+'){adv(lx);k=TOK_INC;lx->insert_semi=1;} else k=TOK_ADD; break;
        case '-': if(c2=='='){adv(lx);k=TOK_SUB_ASSIGN;} else if(c2=='-'){adv(lx);k=TOK_DEC;lx->insert_semi=1;} else k=TOK_SUB; break;
        case '*': if(c2=='='){adv(lx);k=TOK_MUL_ASSIGN;} else k=TOK_MUL; break;
        case '/': if(c2=='='){adv(lx);k=TOK_QUO_ASSIGN;} else k=TOK_QUO; break;
        case '%': if(c2=='='){adv(lx);k=TOK_REM_ASSIGN;} else k=TOK_REM; break;
        case '^': if(c2=='='){adv(lx);k=TOK_XOR_ASSIGN;} else k=TOK_XOR; break;
        case '~': k=TOK_TILDE; break;
        case '<':
            if(c2=='-'){adv(lx);k=TOK_ARROW;}
            else if(c2=='='){adv(lx);k=TOK_LEQ;}
            else if(c2=='<'){adv(lx); if(cur(lx)=='='){adv(lx);k=TOK_SHL_ASSIGN;} else k=TOK_SHL;}
            else k=TOK_LSS; break;
        case '>':
            if(c2=='='){adv(lx);k=TOK_GEQ;}
            else if(c2=='>'){adv(lx); if(cur(lx)=='='){adv(lx);k=TOK_SHR_ASSIGN;} else k=TOK_SHR;}
            else k=TOK_GTR; break;
        case '=': if(c2=='='){adv(lx);k=TOK_EQL;} else k=TOK_ASSIGN; break;
        case '!': if(c2=='='){adv(lx);k=TOK_NEQ;} else k=TOK_NOT; break;
        case '&':
            if(c2=='&'){adv(lx);k=TOK_LAND;}
            else if(c2=='='){adv(lx);k=TOK_AND_ASSIGN;}
            else if(c2=='^'){adv(lx); if(cur(lx)=='='){adv(lx);k=TOK_AND_NOT_ASSIGN;} else k=TOK_AND_NOT;}
            else k=TOK_AND; break;
        case '|':
            if(c2=='|'){adv(lx);k=TOK_LOR;}
            else if(c2=='='){adv(lx);k=TOK_OR_ASSIGN;}
            else k=TOK_OR; break;
        case ':': if(c2=='='){adv(lx);k=TOK_DEFINE;} else k=TOK_COLON; break;
        case '.':
            if(c2=='.'&&peek(lx,1)=='.'){adv(lx);adv(lx);k=TOK_ELLIPSIS;}
            else k=TOK_PERIOD; break;
        case '(': k=TOK_LPAREN; break;
        case '[': k=TOK_LBRACK; break;
        case '{': k=TOK_LBRACE; break;
        case ',': k=TOK_COMMA; break;
        case ')': k=TOK_RPAREN; lx->insert_semi=1; break;
        case ']': k=TOK_RBRACK; lx->insert_semi=1; break;
        case '}': k=TOK_RBRACE; lx->insert_semi=1; break;
        case ';': k=TOK_SEMICOLON; break;
        default:
            lex_err(lx,p,xsprintf("invalid character %c in source", c));
            continue;
        }
        return mk(lx,k,p,intern(tok_str(k)));
    }
}
