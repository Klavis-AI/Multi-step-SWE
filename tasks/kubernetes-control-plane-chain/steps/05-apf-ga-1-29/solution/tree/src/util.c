#define _GNU_SOURCE
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <openssl/rand.h>

void buf_add(buf *b, const void *d, size_t n){
    if(b->n+n+1 > b->c){ b->c = (b->n+n+1)*2; b->p = realloc(b->p, b->c); }
    memcpy(b->p+b->n, d, n); b->n += n; b->p[b->n]=0;
}
void buf_str(buf *b, const char *s){ buf_add(b, s, strlen(s)); }
void buf_printf(buf *b, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if(n < 0) return;
    if((size_t)n < sizeof tmp){ buf_add(b, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n+1);
    va_start(ap, fmt); vsnprintf(big, (size_t)n+1, fmt, ap); va_end(ap);
    buf_add(b, big, (size_t)n); free(big);
}
void buf_free(buf *b){ free(b->p); b->p=NULL; b->n=b->c=0; }

char *xstrdup2(const char *s){ if(!s) return NULL; size_t n=strlen(s)+1; char *p=malloc(n); memcpy(p,s,n); return p; }
char *xasprintf(const char *fmt, ...){
    va_list ap; va_start(ap,fmt);
    char tmp[1024];
    int n = vsnprintf(tmp,sizeof tmp,fmt,ap); va_end(ap);
    if(n<0) return xstrdup2("");
    if((size_t)n < sizeof tmp) return xstrdup2(tmp);
    char *big = malloc((size_t)n+1);
    va_start(ap,fmt); vsnprintf(big,(size_t)n+1,fmt,ap); va_end(ap);
    return big;
}
int str_has_prefix(const char *s, const char *p){ return strncmp(s,p,strlen(p))==0; }
int str_has_suffix(const char *s, const char *p){
    size_t a=strlen(s), b=strlen(p); return a>=b && strcmp(s+a-b,p)==0;
}
char **str_split(const char *s, char sep, int *count){
    int n=1; for(const char *q=s;*q;q++) if(*q==sep) n++;
    char **v = calloc(n, sizeof(char*));
    int i=0; const char *start=s;
    for(const char *q=s;;q++){
        if(*q==sep || !*q){
            size_t len=(size_t)(q-start);
            v[i]=malloc(len+1); memcpy(v[i],start,len); v[i][len]=0; i++;
            if(!*q) break;
            start=q+1;
        }
    }
    *count=i; return v;
}
void str_split_free(char **v, int n){ for(int i=0;i<n;i++) free(v[i]); free(v); }

static int hexval(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
char *url_decode(const char *s, size_t n){
    char *o = malloc(n+1); size_t k=0;
    for(size_t i=0;i<n;i++){
        if(s[i]=='%' && i+2<n){ int a=hexval(s[i+1]), b=hexval(s[i+2]);
            if(a>=0&&b>=0){ o[k++]=(char)(a*16+b); i+=2; continue; } }
        if(s[i]=='+'){ o[k++]=' '; continue; }
        o[k++]=s[i];
    }
    o[k]=0; return o;
}
char *url_encode(const char *s){
    buf b={0};
    for(const unsigned char *p=(const unsigned char*)s; *p; p++){
        if(isalnum(*p) || *p=='-'||*p=='_'||*p=='.'||*p=='~'||*p=='/') buf_add(&b,p,1);
        else buf_printf(&b,"%%%02X",*p);
    }
    if(!b.p) buf_str(&b,"");
    return b.p;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char *b64_encode(const unsigned char *d, size_t n){
    size_t out = ((n+2)/3)*4;
    char *r = malloc(out+1); size_t k=0;
    for(size_t i=0;i<n;i+=3){
        unsigned v = d[i]<<16;
        if(i+1<n) v |= d[i+1]<<8;
        if(i+2<n) v |= d[i+2];
        r[k++]=B64[(v>>18)&63]; r[k++]=B64[(v>>12)&63];
        r[k++]= (i+1<n)? B64[(v>>6)&63] : '=';
        r[k++]= (i+2<n)? B64[v&63] : '=';
    }
    r[k]=0; return r;
}
unsigned char *b64_decode(const char *s, size_t *out){
    int rev[256]; for(int i=0;i<256;i++) rev[i]=-1;
    for(int i=0;i<64;i++) rev[(unsigned char)B64[i]]=i;
    size_t n=strlen(s);
    unsigned char *r = malloc(n/4*3+4); size_t k=0;
    int acc=0, bits=0;
    for(size_t i=0;i<n;i++){
        unsigned char c=(unsigned char)s[i];
        if(c=='='||c=='\n'||c=='\r') continue;
        if(rev[c]<0){ free(r); return NULL; }
        acc = (acc<<6) | rev[c]; bits += 6;
        if(bits>=8){ bits-=8; r[k++] = (unsigned char)((acc>>bits)&0xFF); }
    }
    *out=k; return r;
}

static const char SAFE[] = "bcdfghjklmnpqrstvwxz2456789";
void rand_suffix(char *out, int n){
    unsigned char b[64]; RAND_bytes(b, n);
    for(int i=0;i<n;i++) out[i]=SAFE[b[i]%(sizeof(SAFE)-1)];
    out[n]=0;
}
void rand_hex(char *out, int n){
    unsigned char b[64]; RAND_bytes(b, (n+1)/2);
    static const char H[]="0123456789abcdef";
    for(int i=0;i<n;i++) out[i]=H[(i%2)? (b[i/2]&15) : (b[i/2]>>4)];
    out[n]=0;
}
char *new_uid(void){
    char h[33]; rand_hex(h,32);
    return xasprintf("%.8s-%.4s-4%.3s-a%.3s-%.12s", h, h+8, h+12, h+16, h+20);
}
char *now_rfc3339(void){
    time_t t=time(NULL); struct tm g; gmtime_r(&t,&g);
    char b[32]; strftime(b,sizeof b,"%Y-%m-%dT%H:%M:%SZ",&g);
    return xstrdup2(b);
}
long long now_unix(void){ return (long long)time(NULL); }
