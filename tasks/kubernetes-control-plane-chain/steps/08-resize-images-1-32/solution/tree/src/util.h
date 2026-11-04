#ifndef KCP_UTIL_H
#define KCP_UTIL_H
#include <stddef.h>
#include <stdarg.h>

typedef struct { char *p; size_t n, c; } buf;
void buf_add(buf *b, const void *d, size_t n);
void buf_str(buf *b, const char *s);
void buf_printf(buf *b, const char *fmt, ...);
void buf_free(buf *b);

char *xstrdup2(const char *s);
char *xasprintf(const char *fmt, ...);
int  str_has_prefix(const char *s, const char *p);
int  str_has_suffix(const char *s, const char *p);
char **str_split(const char *s, char sep, int *count);   /* caller frees with str_split_free */
void  str_split_free(char **v, int n);
char *url_decode(const char *s, size_t n);
char *url_encode(const char *s);

char *b64_encode(const unsigned char *d, size_t n);
unsigned char *b64_decode(const char *s, size_t *out);

void rand_suffix(char *out, int n);       /* [bcdfghjklmnpqrstvwxz2456789] */
void rand_hex(char *out, int n);
char *new_uid(void);
char *now_rfc3339(void);
long long now_unix(void);
#endif
