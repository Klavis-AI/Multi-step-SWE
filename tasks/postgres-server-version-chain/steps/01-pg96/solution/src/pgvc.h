/* pgvc.h - shared declarations for the PostgreSQL-compatible server. */
#ifndef PGVC_H
#define PGVC_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#ifndef PGVC_MAJOR
#define PGVC_MAJOR 96          /* 96,10,11,...,18 */
#endif
/* PGVC_MAJOR is spelled the way PostgreSQL spells the release (96 for 9.6),
 * so it does not order numerically.  PGVC_VN does: 96, 100, 110, ... 180. */
#if PGVC_MAJOR == 96
#define PGVC_VN 96
#else
#define PGVC_VN (PGVC_MAJOR * 10)
#endif

typedef uint32_t Oid;
typedef int64_t  int64;
typedef int32_t  int32;
typedef int16_t  int16;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t  uint8;

/* ---------------------------------------------------------------- buffers */
typedef struct {
    char  *data;
    size_t len, cap;
} Buf;

void  buf_init(Buf *b);
void  buf_free(Buf *b);
void  buf_reset(Buf *b);
void  buf_add(Buf *b, const void *p, size_t n);
void  buf_addc(Buf *b, char c);
void  buf_adds(Buf *b, const char *s);
void  buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf,2,3)));
char *buf_steal(Buf *b);                     /* NUL-terminated, malloc'd */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* ---------------------------------------------------------------- type oids
 * Real PostgreSQL type OIDs: the wire protocol's RowDescription carries them
 * and every driver keys off them, so they are part of the interface. */
#define BOOLOID        16
#define BYTEAOID       17
#define CHAROID        18
#define NAMEOID        19
#define INT8OID        20
#define INT2OID        21
#define INT4OID        23
#define TEXTOID        25
#define OIDOID         26
#define JSONOID       114
#define XMLOID        142
#define FLOAT4OID     700
#define FLOAT8OID     701
#define UNKNOWNOID    705
#define POINTOID      600
#define POINTARRAYOID 1017
#define CIRCLEOID     718
#define POINTOID      600
#define LSEGOID       601
#define PATHOID       602
#define BOXOID        603
#define POLYGONOID    604
#define LINEOID       628
#define MONEYOID      790
#define BPCHAROID    1042
#define VARCHAROID   1043
#define DATEOID      1082
#define TIMEOID      1083
#define TIMESTAMPOID 1114
#define TIMESTAMPTZOID 1184
#define INTERVALOID  1186
#define TIMETZOID    1266
#define NUMERICOID   1700
#define PGLSNOID     3220
#define XIDOID         28
#define XID8OID      5069
#define PGSNAPSHOTOID 5038
#define TXIDSNAPSHOTOID 2970
#define INT2VECTOROID  22
#define OIDVECTOROID   30
#define REGCLASSOID  2205
#define REGTYPEOID   2206
#define REGNAMESPACEOID 4089
#define REGROLEOID   4096
#define REGPROCOID     24
#define UUIDOID      2950
#define JSONBOID     3802
#define JSONPATHOID  4072
#define TSVECTOROID  3614
#define TSQUERYOID   3615
#define REGCONFIGOID 3734
#define RECORDOID    2249
#define VOIDOID      2278
#define ANYOID       2276
#define INT4RANGEOID 3904
#define NUMRANGEOID  3906
#define TSRANGEOID   3908
#define TSTZRANGEOID 3910
#define DATERANGEOID 3912
#define INT8RANGEOID 3926
#define INT4MULTIRANGEOID 4451
#define NUMMULTIRANGEOID  4532
#define TSMULTIRANGEOID   4533
#define TSTZMULTIRANGEOID 4534
#define DATEMULTIRANGEOID 4535
#define INT8MULTIRANGEOID 4536
/* array type oids */
#define BOOLARRAYOID    1000
#define BYTEAARRAYOID   1001
#define NAMEARRAYOID    1003
#define INT2ARRAYOID    1005
#define INT4ARRAYOID    1007
#define TEXTARRAYOID    1009
#define BPCHARARRAYOID  1014
#define VARCHARARRAYOID 1015
#define INT8ARRAYOID    1016
#define FLOAT4ARRAYOID  1021
#define FLOAT8ARRAYOID  1022
#define OIDARRAYOID     1028
#define XIDARRAYOID     1011
#define XID8ARRAYOID     271
#define PGSNAPSHOTARRAYOID 5039
#define TXIDSNAPSHOTARRAYOID 2949
#define DATEARRAYOID    1182
#define TIMEARRAYOID    1183
#define TIMESTAMPARRAYOID 1115
#define TIMESTAMPTZARRAYOID 1185
#define INTERVALARRAYOID 1187
#define NUMERICARRAYOID 1231
#define UUIDARRAYOID    2951
#define JSONARRAYOID     199
#define JSONBARRAYOID   3807
#define RECORDARRAYOID  2287
#define ANYARRAYOID     2277

/* --------------------------------------------------------------- numeric */
typedef struct Numeric Numeric;              /* opaque, defined in numeric.c */
Numeric *num_from_str(const char *s, bool *ok);
Numeric *num_from_int64(int64 v);
Numeric *num_from_double(double d);
Numeric *num_copy(const Numeric *a);
void     num_free(Numeric *a);
char    *num_to_str(const Numeric *a);        /* malloc'd, PostgreSQL display form */
double   num_to_double(const Numeric *a);
bool     num_to_int64(const Numeric *a, int64 *out);   /* false on overflow/frac */
int      num_cmp(const Numeric *a, const Numeric *b);
Numeric *num_add(const Numeric *a, const Numeric *b);
Numeric *num_sub(const Numeric *a, const Numeric *b);
Numeric *num_mul(const Numeric *a, const Numeric *b);
Numeric *num_div(const Numeric *a, const Numeric *b, bool *divzero);   /* PG select-list rules */
Numeric *num_div_trunc(const Numeric *a, const Numeric *b, bool *divzero);
Numeric *num_mod(const Numeric *a, const Numeric *b, bool *divzero);
Numeric *num_neg(const Numeric *a);
Numeric *num_abs(const Numeric *a);
Numeric *num_round(const Numeric *a, int scale);
Numeric *num_trunc(const Numeric *a, int scale);
Numeric *num_ceil(const Numeric *a);
Numeric *num_floor(const Numeric *a);
Numeric *num_pow(const Numeric *a, const Numeric *b, bool *err);
Numeric *num_sqrt(const Numeric *a, bool *err);
bool     num_is_nan(const Numeric *a);
bool     num_is_infinite(const Numeric *a);
Numeric *num_apply_typmod(const Numeric *a, int32 typmod, bool *overflow);
int      num_scale(const Numeric *a);

/* --------------------------------------------------------- date and time */
/* All internal times are PostgreSQL's: microseconds since 2000-01-01 00:00 UTC
 * for timestamps, days since 2000-01-01 for dates, microseconds since midnight
 * for time. */
#define PGVC_EPOCH_JDATE 2451545      /* == julian day of 2000-01-01 */
typedef struct { int64 usec; int32 day; int32 month; } PgInterval;

bool dt_parse_date(const char *s, int32 *out_day, const char **err);
bool dt_parse_time(const char *s, int64 *out_usec, const char **err);
bool dt_parse_timestamp(const char *s, int64 *out_usec, bool with_tz, const char **err);
bool dt_parse_interval(const char *s, PgInterval *out, const char **err);
char *dt_format_date(int32 day);
char *dt_format_time(int64 usec);
char *dt_format_timestamp(int64 usec, bool with_tz);
char *dt_format_interval(const PgInterval *iv);
void  dt_date_to_ymd(int32 day, int *y, int *m, int *d);
int32 dt_ymd_to_date(int y, int m, int d);
bool dt_to_date(const char *s, const char *fmt, int32 *out, const char **err);
bool dt_to_timestamp(const char *s, const char *fmt, int64 *out, const char **err);
double dt_extract(const char *field, Oid type, int64 val, const PgInterval *iv, bool *ok);
char *dt_extract_numeric(const char *field, Oid type, int64 val, const PgInterval *iv, bool *ok);
int   dt_last_error_kind(void);   /* -3 month/day overflow, -6 unknown time zone */
const char *dt_last_error(void);

/* ----------------------------------------------------------------- jsonb */
typedef struct JsonVal JsonVal;
JsonVal *json_parse(const char *s, size_t len, const char **err);
void     json_free(JsonVal *v);
JsonVal *json_copy(const JsonVal *v);
char    *json_to_text(const JsonVal *v, bool jsonb);   /* canonical jsonb form when jsonb */
char    *json_pretty(const JsonVal *v);
int      json_cmp(const JsonVal *a, const JsonVal *b);
bool     json_contains(const JsonVal *a, const JsonVal *b);
JsonVal *json_get_key(const JsonVal *v, const char *k);
JsonVal *json_get_idx(const JsonVal *v, int i);
int      json_kind(const JsonVal *v);   /* 0 null 1 bool 2 num 3 str 4 array 5 object */
int      json_length(const JsonVal *v);
const char *json_str_value(const JsonVal *v);
const char *json_num_text(const JsonVal *v);
JsonVal *json_parse_ex(const char *s, size_t len, bool jsonb, const char **err);
JsonVal *json_parse_keys(const char *s, size_t len, bool jsonb, const char **err,
                         bool *dupkey);
size_t   json_last_error_offset(void);
const char *json_key_at(const JsonVal *v, int i);
JsonVal *json_val_at(const JsonVal *v, int i);

/* ------------------------------------------------- Unicode normalization */
/* form: 0 NFC, 1 NFD, 2 NFKC, 3 NFKD */
char *unicode_normalize(const char *s, size_t len, int form);
bool  unicode_is_normalized(const char *s, size_t len, int form);
int   unicode_combining_class(uint32 cp);

/* -------------------------------------------------------------- digests */
void md5_hex(const unsigned char *in, size_t n, char out[33]);
void sha256(const unsigned char *in, size_t n, unsigned char out[32]);
void sha224(const unsigned char *in, size_t n, unsigned char out[28]);
void sha384(const unsigned char *in, size_t n, unsigned char out[48]);
void sha512(const unsigned char *in, size_t n, unsigned char out[64]);
void hmac_sha256(const unsigned char *key, size_t klen,
                 const unsigned char *msg, size_t mlen, unsigned char out[32]);
void pbkdf2_sha256(const char *pass, size_t plen, const unsigned char *salt,
                   size_t slen, int iters, unsigned char *out, size_t outlen);
size_t b64_encode(const unsigned char *in, size_t n, char *out);
int    b64_decode(const char *in, size_t n, unsigned char *out);

/* -------------------------------------------------------------- SCRAM */
typedef struct {
    int            iterations;
    unsigned char *salt;
    size_t         salt_len;
    unsigned char  stored_key[32];
    unsigned char  server_key[32];
} ScramVerifier;

char *scram_build_verifier(const char *pass, size_t plen,
                           const unsigned char *salt, size_t slen, int iters);
bool  scram_parse_verifier(const char *verifier, ScramVerifier *out);
void  scram_free_verifier(ScramVerifier *v);
void  scram_client_key(const unsigned char salted_password[32], unsigned char out[32]);
void  scram_stored_key(const unsigned char client_key[32], unsigned char out[32]);
void  scram_server_key(const unsigned char salted_password[32], unsigned char out[32]);
void  scram_client_signature(const unsigned char stored_key[32],
                             const char *auth_message, size_t am_len, unsigned char out[32]);
void  scram_client_proof(const unsigned char client_key[32],
                         const unsigned char client_signature[32], unsigned char out[32]);
void  scram_server_signature(const unsigned char server_key[32],
                             const char *auth_message, size_t am_len, unsigned char out[32]);
char *md5_build_verifier(const char *pass, const char *user);

#endif /* PGVC_H */
