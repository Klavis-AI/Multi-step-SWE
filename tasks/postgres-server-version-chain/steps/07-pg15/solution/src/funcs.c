/* funcs.c - built-in function and operator implementations.
 *
 * The single entry point is call_builtin(); everything else in this file is
 * private.  `name` is either a PostgreSQL function name or an operator symbol
 * exactly as the parser produced it ("+", "||", "@>", "!post", "!!", ...).
 * When the (name, nargs, argtypes) combination is not one we implement we set
 * *found = false and let the caller raise the error, which is also how the
 * version gates work: a function that does not exist in the configured major
 * simply is not offered.
 *
 * Numeric, date/time, json and digest work is delegated to the APIs declared
 * in pgvc.h.  Helpers that pgvc.h does not declare (date_trunc, age, to_char,
 * a small JSON DOM, ...) are private to this file; SQL/JSON path is its own
 * file, jsonpath.c.
 */
#include "engine.h"
#include "jsonpath.h"
#include <regex.h>
#include <sys/time.h>
#include <unistd.h>

/* ===================================================================== misc */

#define NOTFOUND() do { *found = false; return d_null(TEXTOID); } while (0)

static bool ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static bool f_is_int(Oid t)   { return t==INT2OID||t==INT4OID||t==INT8OID; }
static bool f_is_float(Oid t) { return t==FLOAT4OID||t==FLOAT8OID; }
static bool f_is_numfam(Oid t){ return f_is_int(t)||f_is_float(t)||t==NUMERICOID||t==OIDOID; }
static bool f_is_str(Oid t)
{
    return t==TEXTOID||t==VARCHAROID||t==BPCHAROID||t==NAMEOID||t==CHAROID||t==UNKNOWNOID;
}
static bool f_is_strish(Oid t)
{
    return f_is_str(t)||t==JSONOID||t==JSONPATHOID||t==UUIDOID||t==XMLOID||t==BYTEAOID
           ||t==TSVECTOROID||t==TSQUERYOID||t==REGCONFIGOID;
}
static bool f_is_range(Oid t)
{
    return t==INT4RANGEOID||t==INT8RANGEOID||t==NUMRANGEOID||t==DATERANGEOID||
           t==TSRANGEOID||t==TSTZRANGEOID;
}
static bool f_is_mrange(Oid t)
{
    return t==INT4MULTIRANGEOID||t==INT8MULTIRANGEOID||t==NUMMULTIRANGEOID||
           t==DATEMULTIRANGEOID||t==TSMULTIRANGEOID||t==TSTZMULTIRANGEOID;
}
static Oid f_range_subtype(Oid t)
{
    switch (t) {
    case INT4RANGEOID: return INT4OID;
    case INT8RANGEOID: return INT8OID;
    case NUMRANGEOID:  return NUMERICOID;
    case DATERANGEOID: return DATEOID;
    case TSRANGEOID:   return TIMESTAMPOID;
    case TSTZRANGEOID: return TIMESTAMPTZOID;
    }
    return 0;
}
static Oid f_mrange_of(Oid rng)
{
    switch (rng) {
    case INT4RANGEOID: return INT4MULTIRANGEOID;
    case INT8RANGEOID: return INT8MULTIRANGEOID;
    case NUMRANGEOID:  return NUMMULTIRANGEOID;
    case DATERANGEOID: return DATEMULTIRANGEOID;
    case TSRANGEOID:   return TSMULTIRANGEOID;
    case TSTZRANGEOID: return TSTZMULTIRANGEOID;
    }
    return 0;
}
static Oid f_range_of_mrange(Oid m)
{
    switch (m) {
    case INT4MULTIRANGEOID: return INT4RANGEOID;
    case INT8MULTIRANGEOID: return INT8RANGEOID;
    case NUMMULTIRANGEOID:  return NUMRANGEOID;
    case DATEMULTIRANGEOID: return DATERANGEOID;
    case TSMULTIRANGEOID:   return TSRANGEOID;
    case TSTZMULTIRANGEOID: return TSTZRANGEOID;
    }
    return 0;
}

/* PostgreSQL's SQL spelling of a type, used inside error messages. */
static const char *sqlname(Oid t)
{
    switch (t) {
    case INT2OID: return "smallint";
    case INT4OID: return "integer";
    case INT8OID: return "bigint";
    case FLOAT4OID: return "real";
    case FLOAT8OID: return "double precision";
    case NUMERICOID: return "numeric";
    case BOOLOID: return "boolean";
    case VARCHAROID: return "character varying";
    case BPCHAROID: return "character";
    case TIMESTAMPOID: return "timestamp without time zone";
    case TIMESTAMPTZOID: return "timestamp with time zone";
    case TIMEOID: return "time without time zone";
    case TIMETZOID: return "time with time zone";
    case UNKNOWNOID: return "unknown";
    default: return type_name(t);
    }
}

static void no_operator(Session *s, const char *op, Oid l, Oid r) __attribute__((noreturn));
static void no_operator(Session *s, const char *op, Oid l, Oid r)
{
    err_hint(s, "No operator matches the given name and argument types. "
                "You might need to add explicit type casts.");
    if (l == 0) ereport_pos(s, "42883", s->oppos, "operator does not exist: %s %s", op, sqlname(r));
    ereport_pos(s, "42883", s->oppos, "operator does not exist: %s %s %s",
                sqlname(l), op, sqlname(r));
}

/* --------------------------------------------------------------- accessors */

static char *as_cstr(Session *s, Datum d)
{
    if (d.isnull) return xstrdup("");
    if (f_is_strish(d.type) && d.v.s.p) return xstrndup(d.v.s.p, (size_t)d.v.s.len);
    return out_func(s, d);
}
/* text bytes without copying when possible */
static const char *as_bytes(Session *s, Datum d, int32 *len, char **owned)
{
    *owned = NULL;
    if (f_is_strish(d.type) && d.v.s.p) { *len = d.v.s.len; return d.v.s.p; }
    char *t = out_func(s, d);
    *owned = t; *len = (int32)strlen(t);
    return t;
}

static Numeric *as_num(Session *s, Datum d)
{
    switch (d.type) {
    case NUMERICOID: return d.v.num;
    case FLOAT4OID: case FLOAT8OID: return num_from_double(d.v.f);
    case INT2OID: case INT4OID: case INT8OID: case OIDOID: case BOOLOID:
        return num_from_int64(d.v.i);
    default: break;
    }
    char *t = as_cstr(s, d);
    bool ok = false;
    Numeric *n = num_from_str(t, &ok);
    if (!ok) ereport(s, "22P02", "invalid input syntax for type numeric: \"%s\"", t);
    free(t);
    return n;
}
static double as_f8(Session *s, Datum d)
{
    switch (d.type) {
    case NUMERICOID: return num_to_double(d.v.num);
    case FLOAT4OID: case FLOAT8OID: return d.v.f;
    case INT2OID: case INT4OID: case INT8OID: case OIDOID: case BOOLOID:
        return (double)d.v.i;
    default: break;
    }
    char *t = as_cstr(s, d);
    double v = strtod(t, NULL);
    free(t);
    return v;
}
static int64 as_i8(Session *s, Datum d)
{
    switch (d.type) {
    case INT2OID: case INT4OID: case INT8OID: case OIDOID: case BOOLOID:
    case DATEOID: case TIMEOID: case TIMESTAMPOID: case TIMESTAMPTZOID:
        return d.v.i;
    case FLOAT4OID: case FLOAT8OID: {
        double x = d.v.f;
        if (isnan(x) || isinf(x)) ereport(s, "22003", "bigint out of range");
        x = (x >= 0) ? floor(x + 0.5) : ceil(x - 0.5);
        return (int64)x;
    }
    case REGCLASSOID: case REGTYPEOID: case REGPROCOID:
    case REGNAMESPACEOID: case REGROLEOID:
        return d.v.i;
    case NUMERICOID: {
        int64 v;
        Numeric *r = num_round(d.v.num, 0);
        if (!num_to_int64(r, &v)) ereport(s, "22003", "bigint out of range");
        return v;
    }
    default: break;
    }
    char *t = as_cstr(s, d);
    int64 v = strtoll(t, NULL, 10);
    free(t);
    return v;
}
static int as_i4(Session *s, Datum d) { return (int)as_i8(s, d); }
static bool as_bool(Session *s, Datum d)
{
    if (d.type == BOOLOID) return d.v.i != 0;
    Datum c = coerce_to(s, d, BOOLOID, -1, 2);
    return c.v.i != 0;
}

static Datum d_textbuf(Oid t, Buf *b)
{
    Datum d = d_textn(t, b->data ? b->data : "", (int32)b->len);
    buf_free(b);
    return d;
}
static Datum d_void(void)
{
    Datum d; memset(&d, 0, sizeof d);
    d.type = VOIDOID; d.v.s.p = xstrdup(""); d.v.s.len = 0;
    return d;
}
static Datum d_interval(PgInterval iv)
{
    Datum d; memset(&d, 0, sizeof d);
    d.type = INTERVALOID; d.v.iv = iv;
    return d;
}
static Datum d_arr(Oid arrtype, ArrayVal *a)
{
    Datum d; memset(&d, 0, sizeof d);
    d.type = arrtype; d.v.arr = a;
    return d;
}
static ArrayVal *arr_new(Oid elem)
{
    ArrayVal *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->elemtype = elem;
    return a;
}
static void arr_push(ArrayVal *a, Datum e)
{
    a->elems = xrealloc(a->elems, (size_t)(a->nelem + 1) * sizeof(Datum));
    a->elems[a->nelem++] = e;
    a->ndim = 1; a->dims[0] = a->nelem; a->lbound[0] = 1;
}
static Datum d_emptyarr(Oid elem)
{
    ArrayVal *a = arr_new(elem);
    return d_arr(array_type_of(elem) ? array_type_of(elem) : TEXTARRAYOID, a);
}
static Datum d_textarr_from(char **vals, int n)
{
    ArrayVal *a = arr_new(TEXTOID);
    for (int i = 0; i < n; i++)
        arr_push(a, vals[i] ? d_text(TEXTOID, vals[i]) : d_null(TEXTOID));
    return d_arr(TEXTARRAYOID, a);
}

/* ======================================================= integer arithmetic */

static void int_range_err(Session *s, Oid t) __attribute__((noreturn));
static void int_range_err(Session *s, Oid t)
{
    ereport(s, "22003", "%s out of range",
            t == INT2OID ? "smallint" : t == INT4OID ? "integer" : "bigint");
}
static int64 int_lo(Oid t) { return t==INT2OID ? -32768 : t==INT4OID ? -2147483648LL : INT64_MIN; }
static int64 int_hi(Oid t) { return t==INT2OID ?  32767 : t==INT4OID ?  2147483647LL : INT64_MAX; }

static Datum int_result(Session *s, Oid t, __int128 v)
{
    if (v < (__int128)int_lo(t) || v > (__int128)int_hi(t)) int_range_err(s, t);
    return d_int(t, (int64)v);
}

/* PostgreSQL's numeric type resolution for the binary arithmetic operators. */
static Oid arith_type(Oid a, Oid b)
{
    if (a == FLOAT8OID || b == FLOAT8OID) return FLOAT8OID;
    if (a == FLOAT4OID && b == FLOAT4OID) return FLOAT4OID;
    if (a == FLOAT4OID || b == FLOAT4OID) return FLOAT8OID;
    if (a == NUMERICOID || b == NUMERICOID) return NUMERICOID;
    if (a == INT8OID || b == INT8OID) return INT8OID;
    if (a == INT4OID || b == INT4OID) return INT4OID;
    if (a == INT2OID && b == INT2OID) return INT2OID;
    if (a == OIDOID || b == OIDOID) return OIDOID;
    return INT4OID;
}

/* ========================================================= date/time helpers
 * pgvc.h gives us parsing, formatting and field extraction; the calendar
 * arithmetic below (date_trunc, age, justify_*, make_*, to_char, to_date)
 * is private to this file. */

#define USECS_PER_DAY   86400000000LL
#define USECS_PER_HOUR   3600000000LL
#define USECS_PER_MIN      60000000LL
#define USECS_PER_SEC       1000000LL
#define SECS_PER_DAY          86400

static const int mdays[2][12] = {
    {31,28,31,30,31,30,31,31,30,31,30,31},
    {31,29,31,30,31,30,31,31,30,31,30,31}
};
static bool f_isleap(int y) { return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)); }
static int  f_mdays(int y, int m) { return mdays[f_isleap(y) ? 1 : 0][m - 1]; }

static void ts_split(int64 us, int32 *day, int64 *tod)
{
    int64 d = us / USECS_PER_DAY, t = us % USECS_PER_DAY;
    if (t < 0) { t += USECS_PER_DAY; d--; }
    *day = (int32)d; *tod = t;
}
static void ts_to_parts(int64 us, int *y, int *mo, int *d, int *h, int *mi, int *sec, int *fs)
{
    int32 day; int64 tod;
    ts_split(us, &day, &tod);
    dt_date_to_ymd(day, y, mo, d);
    *h  = (int)(tod / USECS_PER_HOUR);
    *mi = (int)((tod / USECS_PER_MIN) % 60);
    *sec= (int)((tod / USECS_PER_SEC) % 60);
    *fs = (int)(tod % USECS_PER_SEC);
}
static int64 parts_to_ts(int y, int mo, int d, int h, int mi, int sec, int fs)
{
    int32 day = dt_ymd_to_date(y, mo, d);
    return (int64)day * USECS_PER_DAY + (int64)h * USECS_PER_HOUR +
           (int64)mi * USECS_PER_MIN + (int64)sec * USECS_PER_SEC + fs;
}

static int64 ts_add_interval(int64 us, const PgInterval *iv, int sign)
{
    int32 day; int64 tod;
    ts_split(us, &day, &tod);
    if (iv->month) {
        int y, mo, d;
        dt_date_to_ymd(day, &y, &mo, &d);
        int64 tot = (int64)y * 12 + (mo - 1) + (int64)sign * iv->month;
        int ny = (int)(tot / 12); int nm = (int)(tot % 12);
        if (nm < 0) { nm += 12; ny--; }
        nm++;
        int ml = f_mdays(ny, nm);
        if (d > ml) d = ml;
        day = dt_ymd_to_date(ny, nm, d);
    }
    day += sign * iv->day;
    return (int64)day * USECS_PER_DAY + tod + (int64)sign * iv->usec;
}

static double tsround(double j) { return rint(j * 1000000.0) / 1000000.0; }

static PgInterval iv_mul(const PgInterval *span, double factor)
{
    PgInterval r; memset(&r, 0, sizeof r);
    double month_remainder_days, sec_remainder, rd;
    int32 om = span->month, od = span->day;
    rd = (double)om * factor;  r.month = (int32)rd;
    rd = (double)od * factor;  r.day   = (int32)rd;
    month_remainder_days = ((double)om * factor - r.month) * 30.0;
    month_remainder_days = tsround(month_remainder_days);
    sec_remainder = ((double)od * factor - r.day + month_remainder_days -
                     (int)month_remainder_days) * (double)SECS_PER_DAY;
    sec_remainder = tsround(sec_remainder);
    if (fabs(sec_remainder) >= (double)SECS_PER_DAY) {
        r.day += (int)(sec_remainder / SECS_PER_DAY);
        sec_remainder -= (int)(sec_remainder / SECS_PER_DAY) * (double)SECS_PER_DAY;
    }
    r.day += (int32)month_remainder_days;
    r.usec = (int64)rint((double)span->usec * factor + sec_remainder * 1000000.0);
    return r;
}
static PgInterval iv_div(Session *s, const PgInterval *span, double factor)
{
    if (factor == 0.0) ereport(s, "22012", "division by zero");
    PgInterval r; memset(&r, 0, sizeof r);
    double month_remainder_days, sec_remainder;
    r.month = (int32)((double)span->month / factor);
    r.day   = (int32)((double)span->day / factor);
    month_remainder_days = ((double)span->month / factor - r.month) * 30.0;
    month_remainder_days = tsround(month_remainder_days);
    sec_remainder = ((double)span->day / factor - r.day + month_remainder_days -
                     (int)month_remainder_days) * (double)SECS_PER_DAY;
    sec_remainder = tsround(sec_remainder);
    if (fabs(sec_remainder) >= (double)SECS_PER_DAY) {
        r.day += (int)(sec_remainder / SECS_PER_DAY);
        sec_remainder -= (int)(sec_remainder / SECS_PER_DAY) * (double)SECS_PER_DAY;
    }
    r.day += (int32)month_remainder_days;
    r.usec = (int64)rint((double)span->usec / factor + sec_remainder * 1000000.0);
    return r;
}
/* sum(interval) and avg(interval).  PostgreSQL adds the three fields
 * separately and reports an overflow of any of them as "interval out of
 * range"; avg() then divides the total by the number of non-null inputs. */
Datum interval_agg(Session *s, Datum *vals, int nv, bool isavg)
{
    PgInterval acc; memset(&acc, 0, sizeof acc);
    int64 n = 0;
    for (int i = 0; i < nv; i++) {
        if (vals[i].isnull) continue;
        PgInterval v = vals[i].v.iv;
        int64 m = (int64)acc.month + v.month;
        int64 d = (int64)acc.day + v.day;
        if (m > INT32_MAX || m < INT32_MIN || d > INT32_MAX || d < INT32_MIN)
            ereport(s, "22008", "interval out of range");
        int64 u = acc.usec + v.usec;
        if ((v.usec > 0 && u < acc.usec) || (v.usec < 0 && u > acc.usec))
            ereport(s, "22008", "interval out of range");
        acc.month = (int32)m; acc.day = (int32)d; acc.usec = u;
        n++;
    }
    if (!n) return d_null(INTERVALOID);
    if (isavg) return d_interval(iv_div(s, &acc, (double)n));
    return d_interval(acc);
}

static PgInterval iv_justify_hours(PgInterval v)
{
    int64 wholeday = v.usec / USECS_PER_DAY;
    if (wholeday) v.usec -= wholeday * USECS_PER_DAY;
    v.day += (int32)wholeday;
    if (v.day > 0 && v.usec < 0) { v.usec += USECS_PER_DAY; v.day--; }
    else if (v.day < 0 && v.usec > 0) { v.usec -= USECS_PER_DAY; v.day++; }
    return v;
}
static PgInterval iv_justify_days(PgInterval v)
{
    int32 wholemonth = v.day / 30;
    if (wholemonth) v.day -= wholemonth * 30;
    v.month += wholemonth;
    if (v.month > 0 && v.day < 0) { v.day += 30; v.month--; }
    else if (v.month < 0 && v.day > 0) { v.day -= 30; v.month++; }
    return v;
}
static PgInterval iv_justify_interval(PgInterval v)
{
    int64 wholeday = v.usec / USECS_PER_DAY;
    if (wholeday) v.usec -= wholeday * USECS_PER_DAY;
    v.day += (int32)wholeday;
    int32 wholemonth = v.day / 30;
    if (wholemonth) v.day -= wholemonth * 30;
    v.month += wholemonth;
    if (v.month > 0 && (v.day < 0 || (v.day == 0 && v.usec < 0))) { v.day += 30; v.month--; }
    else if (v.month < 0 && (v.day > 0 || (v.day == 0 && v.usec > 0))) { v.day -= 30; v.month++; }
    if (v.day > 0 && v.usec < 0) { v.usec += USECS_PER_DAY; v.day--; }
    else if (v.day < 0 && v.usec > 0) { v.usec -= USECS_PER_DAY; v.day++; }
    return v;
}

/* PostgreSQL's timestamp_age(): a symbolic (field-wise) difference. */
static PgInterval ts_age(int64 dt1, int64 dt2)
{
    int y1,o1,d1,h1,m1,s1,f1, y2,o2,d2,h2,m2,s2,f2;
    ts_to_parts(dt1, &y1,&o1,&d1,&h1,&m1,&s1,&f1);
    ts_to_parts(dt2, &y2,&o2,&d2,&h2,&m2,&s2,&f2);
    int fs = f1 - f2, sec = s1 - s2, mi = m1 - m2, hr = h1 - h2;
    int md = d1 - d2, mo = o1 - o2, yr = y1 - y2;
    bool flip = dt1 < dt2;
    if (flip) { fs=-fs; sec=-sec; mi=-mi; hr=-hr; md=-md; mo=-mo; yr=-yr; }
    while (fs < 0) { fs += 1000000; sec--; }
    while (sec < 0) { sec += 60; mi--; }
    while (mi < 0) { mi += 60; hr--; }
    while (hr < 0) { hr += 24; md--; }
    while (md < 0) {
        if (flip) { md += f_mdays(y1, o1 > 0 ? o1 : 1); }
        else      { md += f_mdays(y2, o2 > 0 ? o2 : 1); }
        mo--;
    }
    while (mo < 0) { mo += 12; yr--; }
    if (flip) { fs=-fs; sec=-sec; mi=-mi; hr=-hr; md=-md; mo=-mo; yr=-yr; }
    PgInterval r; memset(&r, 0, sizeof r);
    r.month = yr * 12 + mo;
    r.day = md;
    r.usec = (int64)hr * USECS_PER_HOUR + (int64)mi * USECS_PER_MIN +
             (int64)sec * USECS_PER_SEC + fs;
    return r;
}

/* date_trunc over timestamps */
static int64 ts_trunc(Session *s, const char *field, int64 us, bool *ok)
{
    int y,mo,d,h,mi,sec,fs;
    ts_to_parts(us, &y,&mo,&d,&h,&mi,&sec,&fs);
    *ok = true;
    if (ci_eq(field,"microseconds") || ci_eq(field,"microsecond")) return us;
    if (ci_eq(field,"milliseconds") || ci_eq(field,"millisecond"))
        return parts_to_ts(y,mo,d,h,mi,sec, (fs/1000)*1000);
    if (ci_eq(field,"second") || ci_eq(field,"seconds")) return parts_to_ts(y,mo,d,h,mi,sec,0);
    if (ci_eq(field,"minute") || ci_eq(field,"minutes")) return parts_to_ts(y,mo,d,h,mi,0,0);
    if (ci_eq(field,"hour")   || ci_eq(field,"hours"))   return parts_to_ts(y,mo,d,h,0,0,0);
    if (ci_eq(field,"day")    || ci_eq(field,"days"))    return parts_to_ts(y,mo,d,0,0,0,0);
    if (ci_eq(field,"week")   || ci_eq(field,"weeks")) {
        int32 day = dt_ymd_to_date(y,mo,d);
        /* 2000-01-01 was a Saturday, i.e. 5 days past Monday */
        int dow = (int)(((int64)day % 7 + 7 + 5) % 7);   /* 0 = Monday */
        day -= dow;
        return (int64)day * USECS_PER_DAY;
    }
    if (ci_eq(field,"month") || ci_eq(field,"months")) return parts_to_ts(y,mo,1,0,0,0,0);
    if (ci_eq(field,"quarter") || ci_eq(field,"quarters"))
        return parts_to_ts(y, ((mo-1)/3)*3+1, 1,0,0,0,0);
    if (ci_eq(field,"year") || ci_eq(field,"years")) return parts_to_ts(y,1,1,0,0,0,0);
    if (ci_eq(field,"decade") || ci_eq(field,"decades"))
        return parts_to_ts((y/10)*10,1,1,0,0,0,0);
    if (ci_eq(field,"century") || ci_eq(field,"centuries")) {
        int c = (y > 0) ? ((y - 1) / 100) * 100 + 1 : -(((99 - (y + 1)) / 100) * 100) + 1;
        return parts_to_ts(c,1,1,0,0,0,0);
    }
    if (ci_eq(field,"millennium") || ci_eq(field,"millenniums") || ci_eq(field,"millennia")) {
        int c = (y > 0) ? ((y - 1) / 1000) * 1000 + 1 : -(((999 - (y + 1)) / 1000) * 1000) + 1;
        return parts_to_ts(c,1,1,0,0,0,0);
    }
    *ok = false;
    return us;                    /* the caller names the type in its message */
}

static PgInterval iv_trunc(Session *s, const char *field, PgInterval v, bool *ok)
{
    *ok = true;
    if (ci_eq(field,"microseconds")||ci_eq(field,"microsecond")) return v;
    if (ci_eq(field,"milliseconds")||ci_eq(field,"millisecond")) { v.usec = (v.usec/1000)*1000; return v; }
    if (ci_eq(field,"second")||ci_eq(field,"seconds")) { v.usec = (v.usec/USECS_PER_SEC)*USECS_PER_SEC; return v; }
    if (ci_eq(field,"minute")||ci_eq(field,"minutes")) { v.usec = (v.usec/USECS_PER_MIN)*USECS_PER_MIN; return v; }
    if (ci_eq(field,"hour")||ci_eq(field,"hours"))     { v.usec = (v.usec/USECS_PER_HOUR)*USECS_PER_HOUR; return v; }
    if (ci_eq(field,"day")||ci_eq(field,"days"))       { v.usec = 0; return v; }
    if (ci_eq(field,"month")||ci_eq(field,"months"))   { v.usec = 0; v.day = 0; return v; }
    if (ci_eq(field,"quarter")||ci_eq(field,"quarters")){ v.usec=0; v.day=0; v.month=(v.month/3)*3; return v; }
    if (ci_eq(field,"year")||ci_eq(field,"years"))     { v.usec=0; v.day=0; v.month=(v.month/12)*12; return v; }
    if (ci_eq(field,"decade")||ci_eq(field,"decades")) { v.usec=0; v.day=0; v.month=(v.month/120)*120; return v; }
    if (ci_eq(field,"century")||ci_eq(field,"centuries")){v.usec=0;v.day=0;v.month=(v.month/1200)*1200; return v;}
    if (ci_eq(field,"millennium")||ci_eq(field,"millenniums")||ci_eq(field,"millennia"))
        { v.usec=0; v.day=0; v.month=(v.month/12000)*12000; return v; }
    *ok = false;
    ereport(s, "22023", "unit \"%s\" not recognized for type interval", field);
}

/* now(): frozen for the life of a (session, xid) pair, which is what
 * transaction_timestamp() means; clock_timestamp() always reads the clock. */
static int64 clock_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* PostgreSQL's epoch is 2000-01-01; the Unix epoch is 946684800s earlier */
    return ((int64)tv.tv_sec - 946684800LL) * 1000000LL + tv.tv_usec;
}
static int64 xact_us(Session *s)
{
    static Session *cs = (Session *)-1;
    static uint64 cxid = 0;
    static int64  cval = 0;
    if (cs != s || (s && s->xid != cxid) || cval == 0) {
        cval = clock_us(); cs = s; cxid = s ? s->xid : 0;
    }
    return cval;
}

/* ============================================================== arithmetic */

/* Smallest display scale that still represents the value exactly.  Returns -1
 * for NaN and the infinities, which have no scale (PostgreSQL's min_scale
 * returns NULL for those). */
static int num_display_min_scale(const Numeric *n)
{
    char *t = num_to_str(n);
    if (!t) return -1;
    int ms = -1;
    char *dot = strchr(t, '.');
    if (dot) {
        int e = (int)strlen(dot + 1);
        while (e > 0 && dot[e] == '0') e--;
        ms = e;
    } else if (strpbrk(t, "0123456789") != NULL) {
        ms = 0;                        /* an integral value, or a NaN/Infinity */
    }
    if (ms >= 0 && strpbrk(t, "NnIi") != NULL) ms = -1;
    free(t);
    return ms;
}

/* gcd()/lcm() over numeric, following PostgreSQL 13's gcd_var()/numeric_lcm():
 * Euclid on the absolute values, and a result display scale of
 * max(dscale(a), dscale(b)). */
static Datum num_gcd_lcm(Session *s, const Numeric *a, const Numeric *b, bool want_lcm)
{
    (void) s;
    if (num_is_nan(a) || num_is_nan(b) ||
        num_display_min_scale(a) < 0 || num_display_min_scale(b) < 0) {
        bool ok = false;
        return d_num(num_from_str("NaN", &ok));
    }
    int rd = num_scale(a);
    if (num_scale(b) > rd) rd = num_scale(b);
    Numeric *x = num_abs(a), *y = num_abs(b);
    Numeric *zero = num_from_int64(0);
    if (want_lcm && (num_cmp(x, zero) == 0 || num_cmp(y, zero) == 0)) {
        num_free(x); num_free(y);
        Numeric *r = num_round(zero, rd);
        num_free(zero);
        return d_num(r);
    }
    /* Euclid: gcd of the absolute values. */
    Numeric *p = num_copy(x), *q = num_copy(y);
    while (num_cmp(q, zero) != 0) {
        bool dz = false;
        Numeric *m = num_mod(p, q, &dz);
        num_free(p);
        p = q;
        q = m;
    }
    /* p is the gcd, at its natural scale. */
    if (!want_lcm) {
        num_free(x); num_free(y); num_free(q); num_free(zero);
        Numeric *r = num_round(p, rd);
        num_free(p);
        return d_num(r);
    }
    /* lcm(x,y) = abs(x / gcd * y), the division being exact. */
    bool dz = false;
    Numeric *quot = num_div_trunc(x, p, &dz);
    Numeric *prod = num_mul(quot, y);
    Numeric *r = num_round(prod, rd > num_scale(prod) ? rd : num_scale(prod));
    num_free(x); num_free(y); num_free(p); num_free(q); num_free(zero);
    num_free(quot); num_free(prod);
    return d_num(r);
}

static Datum num_binop(Session *s, const char *op, Datum a, Datum b, bool *found)
{
    Oid ta = a.type == UNKNOWNOID ? (f_is_numfam(b.type) ? b.type : NUMERICOID) : a.type;
    Oid tb = b.type == UNKNOWNOID ? (f_is_numfam(a.type) ? a.type : NUMERICOID) : b.type;
    if (!f_is_numfam(ta) || !f_is_numfam(tb)) NOTFOUND();
    /* PostgreSQL has no % for the float types */
    if (!strcmp(op, "%") && (f_is_float(ta) || f_is_float(tb))) NOTFOUND();
    Oid rt = arith_type(ta, tb);
    if (!strcmp(op, "^")) rt = (f_is_float(ta) || f_is_float(tb)) ? FLOAT8OID
                             : (ta == NUMERICOID || tb == NUMERICOID) ? NUMERICOID : FLOAT8OID;
    if (a.isnull || b.isnull) return d_null(rt);

    if (rt == NUMERICOID) {
        Numeric *x = as_num(s, a), *y = as_num(s, b);
        bool dz = false, err = false;
        Numeric *r = NULL;
        switch (op[0]) {
        case '+': r = num_add(x, y); break;
        case '-': r = num_sub(x, y); break;
        case '*': r = num_mul(x, y); break;
        case '/': r = num_div(x, y, &dz); break;
        case '%': r = num_mod(x, y, &dz); break;
        case '^': {
            /* PostgreSQL's error wording for the degenerate cases */
            double bx = num_to_double(x), by = num_to_double(y);
            if (bx == 0.0 && by < 0.0)
                ereport(s, "2201F", "zero raised to a negative power is undefined");
            if (bx < 0.0 && by != floor(by))
                ereport(s, "2201F", "a negative number raised to a non-integer power "
                                    "yields a complex result");
            r = num_pow(x, y, &err);
            if (err) ereport(s, "22003", "value overflows numeric format");
            break;
        }
        default: NOTFOUND();
        }
        if (dz) ereport(s, "22012", "division by zero");
        return d_num(r);
    }
    if (rt == FLOAT8OID || rt == FLOAT4OID) {
        double x = as_f8(s, a), y = as_f8(s, b), r;
        switch (op[0]) {
        case '+': r = x + y; break;
        case '-': r = x - y; break;
        case '*': r = x * y; break;
        case '/': if (y == 0.0) ereport(s, "22012", "division by zero"); r = x / y; break;
        case '^':
            if (x == 0.0 && y < 0.0)
                ereport(s, "2201F", "zero raised to a negative power is undefined");
            if (x < 0.0 && y != floor(y))
                ereport(s, "2201F", "a negative number raised to a non-integer power "
                                    "yields a complex result");
            r = pow(x, y);
            break;
        default: NOTFOUND();
        }
        if (isinf(r) && !isinf(x) && !isinf(y))
            ereport(s, "22003", "value out of range: overflow");
        if (rt == FLOAT4OID) {
            float f = (float)r;
            if (isinf(f) && !isinf(r)) ereport(s, "22003", "value out of range: overflow");
            return d_float(FLOAT4OID, (double)f);
        }
        return d_float(FLOAT8OID, r);
    }
    /* integer family */
    {
        int64 x = as_i8(s, a), y = as_i8(s, b);
        switch (op[0]) {
        case '+': return int_result(s, rt, (__int128)x + y);
        case '-': return int_result(s, rt, (__int128)x - y);
        case '*': return int_result(s, rt, (__int128)x * y);
        case '/':
            if (y == 0) ereport(s, "22012", "division by zero");
            if (y == -1) return int_result(s, rt, -(__int128)x);
            return int_result(s, rt, (__int128)(x / y));
        case '%':
            if (y == 0) ereport(s, "22012", "division by zero");
            if (y == -1) return d_int(rt, 0);
            return d_int(rt, x % y);
        default: break;
        }
        NOTFOUND();
    }
}

static bool is_dt_type(Oid t)
{
    return t==DATEOID||t==TIMEOID||t==TIMESTAMPOID||t==TIMESTAMPTZOID||t==INTERVALOID;
}

/* date/time + - */
static Datum dt_binop(Session *s, const char *op, Datum a, Datum b, bool *found, bool *handled)
{
    *handled = true;
    Oid ta = a.type, tb = b.type;
    bool plus = (op[0] == '+');
    /* unknown literals adjacent to a date/time operand adopt its type */
    if (ta == UNKNOWNOID && is_dt_type(tb)) { a = coerce_to(s, a, tb, -1, 1); ta = tb; }
    if (tb == UNKNOWNOID && is_dt_type(ta)) { b = coerce_to(s, b, ta, -1, 1); tb = ta; }
    if (!is_dt_type(ta) && !is_dt_type(tb)) { *handled = false; return d_null(TEXTOID); }

    /* interval * / numeric */
    if (ta == INTERVALOID && f_is_numfam(tb) && (op[0]=='*' || op[0]=='/')) {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        double f = as_f8(s, b);
        return d_interval(op[0]=='*' ? iv_mul(&a.v.iv, f) : iv_div(s, &a.v.iv, f));
    }
    if (f_is_numfam(ta) && tb == INTERVALOID && op[0]=='*') {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        return d_interval(iv_mul(&b.v.iv, as_f8(s, a)));
    }
    if (op[0] != '+' && op[0] != '-') { *handled = false; return d_null(TEXTOID); }

    /* date +/- integer -> date  (also integer + date) */
    if (ta == DATEOID && f_is_int(tb)) {
        if (a.isnull || b.isnull) return d_null(DATEOID);
        return d_int(DATEOID, a.v.i + (plus ? 1 : -1) * as_i8(s, b));
    }
    if (f_is_int(ta) && tb == DATEOID && plus) {
        if (a.isnull || b.isnull) return d_null(DATEOID);
        return d_int(DATEOID, b.v.i + as_i8(s, a));
    }
    /* date - date -> integer */
    if (ta == DATEOID && tb == DATEOID && !plus) {
        if (a.isnull || b.isnull) return d_null(INT4OID);
        return d_int(INT4OID, a.v.i - b.v.i);
    }
    /* date +/- interval -> timestamp */
    if (ta == DATEOID && tb == INTERVALOID) {
        if (a.isnull || b.isnull) return d_null(TIMESTAMPOID);
        return d_int(TIMESTAMPOID, ts_add_interval(a.v.i * USECS_PER_DAY, &b.v.iv, plus ? 1 : -1));
    }
    if (ta == INTERVALOID && tb == DATEOID && plus) {
        if (a.isnull || b.isnull) return d_null(TIMESTAMPOID);
        return d_int(TIMESTAMPOID, ts_add_interval(b.v.i * USECS_PER_DAY, &a.v.iv, 1));
    }
    /* date +/- time -> timestamp */
    if (ta == DATEOID && (tb == TIMEOID)) {
        if (a.isnull || b.isnull) return d_null(TIMESTAMPOID);
        return d_int(TIMESTAMPOID, a.v.i * USECS_PER_DAY + (plus ? b.v.i : -b.v.i));
    }
    if (ta == TIMEOID && tb == DATEOID && plus) {
        if (a.isnull || b.isnull) return d_null(TIMESTAMPOID);
        return d_int(TIMESTAMPOID, b.v.i * USECS_PER_DAY + a.v.i);
    }
    /* timestamp +/- interval */
    if ((ta == TIMESTAMPOID || ta == TIMESTAMPTZOID) && tb == INTERVALOID) {
        if (a.isnull || b.isnull) return d_null(ta);
        return d_int(ta, ts_add_interval(a.v.i, &b.v.iv, plus ? 1 : -1));
    }
    if (ta == INTERVALOID && (tb == TIMESTAMPOID || tb == TIMESTAMPTZOID) && plus) {
        if (a.isnull || b.isnull) return d_null(tb);
        return d_int(tb, ts_add_interval(b.v.i, &a.v.iv, 1));
    }
    /* timestamp - timestamp -> interval (justified into days) */
    if ((ta == TIMESTAMPOID || ta == TIMESTAMPTZOID) &&
        (tb == TIMESTAMPOID || tb == TIMESTAMPTZOID) && !plus) {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        PgInterval v; memset(&v, 0, sizeof v);
        v.usec = a.v.i - b.v.i;
        return d_interval(iv_justify_hours(v));
    }
    /* timestamp - date / date - timestamp */
    if ((ta == TIMESTAMPOID || ta == TIMESTAMPTZOID) && tb == DATEOID && !plus) {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        PgInterval v; memset(&v, 0, sizeof v);
        v.usec = a.v.i - b.v.i * USECS_PER_DAY;
        return d_interval(iv_justify_hours(v));
    }
    if (ta == DATEOID && (tb == TIMESTAMPOID || tb == TIMESTAMPTZOID) && !plus) {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        PgInterval v; memset(&v, 0, sizeof v);
        v.usec = a.v.i * USECS_PER_DAY - b.v.i;
        return d_interval(iv_justify_hours(v));
    }
    /* time +/- interval -> time (wraps at 24h) */
    if (ta == TIMEOID && tb == INTERVALOID) {
        if (a.isnull || b.isnull) return d_null(TIMEOID);
        int64 t = a.v.i + (plus ? b.v.iv.usec : -b.v.iv.usec);
        t %= USECS_PER_DAY;
        if (t < 0) t += USECS_PER_DAY;
        return d_int(TIMEOID, t);
    }
    if (ta == INTERVALOID && tb == TIMEOID && plus) {
        if (a.isnull || b.isnull) return d_null(TIMEOID);
        int64 t = (b.v.i + a.v.iv.usec) % USECS_PER_DAY;
        if (t < 0) t += USECS_PER_DAY;
        return d_int(TIMEOID, t);
    }
    /* time - time -> interval */
    if (ta == TIMEOID && tb == TIMEOID && !plus) {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        PgInterval v; memset(&v, 0, sizeof v);
        v.usec = a.v.i - b.v.i;
        return d_interval(v);
    }
    /* interval +/- interval */
    if (ta == INTERVALOID && tb == INTERVALOID) {
        if (a.isnull || b.isnull) return d_null(INTERVALOID);
        PgInterval v;
        v.month = a.v.iv.month + (plus ? b.v.iv.month : -b.v.iv.month);
        v.day   = a.v.iv.day   + (plus ? b.v.iv.day   : -b.v.iv.day);
        v.usec  = a.v.iv.usec  + (plus ? b.v.iv.usec  : -b.v.iv.usec);
        return d_interval(v);
    }
    *handled = false;
    return d_null(TEXTOID);
}

/* ============================================================ text helpers */

static int u8_charlen(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
static int u8_len(const char *p, int32 n)
{
    int c = 0;
    for (int32 i = 0; i < n; ) { i += u8_charlen((unsigned char)p[i]); c++; }
    return c;
}
/* byte offset of character index `ci` (0-based); n if past the end */
static int32 u8_off(const char *p, int32 n, int ci)
{
    int32 i = 0;
    while (ci > 0 && i < n) { i += u8_charlen((unsigned char)p[i]); ci--; }
    return i > n ? n : i;
}
static int u8_decode(const char *p, int *adv)
{
    unsigned char c = (unsigned char)p[0];
    int l = u8_charlen(c);
    *adv = l;
    if (l == 1) return c;
    int cp = c & (0xFF >> (l + 1));
    for (int i = 1; i < l; i++) cp = (cp << 6) | ((unsigned char)p[i] & 0x3F);
    return cp;
}
static int u8_encode(int cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0|(cp>>6)); out[1] = (char)(0x80|(cp&0x3F)); return 2; }
    if (cp < 0x10000) { out[0]=(char)(0xE0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&0x3F));
                        out[2]=(char)(0x80|(cp&0x3F)); return 3; }
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F));
    out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}

/* PostgreSQL under the C/POSIX ctype rules only case-maps ASCII, which is what
 * the reference servers do for multibyte text. */
static Datum text_case(Oid rt, const char *p, int32 n, int mode)
{
    Buf b; buf_init(&b);
    bool prev_alnum = false;
    for (int32 i = 0; i < n; ) {
        int l = u8_charlen((unsigned char)p[i]);
        if (l == 1) {
            unsigned char c = (unsigned char)p[i];
            if (mode == 0) buf_addc(&b, (char)toupper(c));
            else if (mode == 1) buf_addc(&b, (char)tolower(c));
            else {
                bool al = isalnum(c) != 0;
                buf_addc(&b, (char)(prev_alnum ? tolower(c) : toupper(c)));
                prev_alnum = al;
            }
        } else {
            buf_add(&b, p + i, (size_t)l);
            if (mode == 2) prev_alnum = true;
        }
        i += l;
    }
    return d_textbuf(rt, &b);
}

static Datum text_sub(Oid rt, Session *s, const char *p, int32 n, int64 start,
                      bool havelen, int64 len)
{
    if (havelen && len < 0) ereport(s, "22011", "negative substring length not allowed");
    int total = u8_len(p, n);
    int64 S1 = start > 1 ? start : 1;
    int64 E1;
    if (!havelen) E1 = total + 1;
    else {
        int64 E = start + len;
        if (E < start) ereport(s, "22011", "negative substring length not allowed");
        E1 = E < total + 1 ? E : total + 1;
    }
    if (E1 <= S1) return d_text(rt, "");
    int32 bo = u8_off(p, n, (int)(S1 - 1));
    int32 eo = u8_off(p, n, (int)(E1 - 1));
    return d_textn(rt, p + bo, eo - bo);
}

static Datum text_pad(Session *s, const char *p, int32 n, int64 want,
                      const char *fill, int32 fn, bool left)
{
    (void)s;
    if (want < 0) want = 0;
    int total = u8_len(p, n);
    if (total > want) {
        int32 eo = u8_off(p, n, (int)want);
        return d_textn(TEXTOID, p, eo);
    }
    int need = (int)(want - total);
    int fl = u8_len(fill, fn);
    Buf b; buf_init(&b);
    if (!left) buf_add(&b, p, (size_t)n);
    if (fl > 0) {
        int done = 0;
        while (done < need) {
            int take = need - done < fl ? need - done : fl;
            buf_add(&b, fill, (size_t)u8_off(fill, fn, take));
            done += take;
        }
    }
    if (left) buf_add(&b, p, (size_t)n);
    return d_textbuf(TEXTOID, &b);
}

/* does the character starting at p[i] appear in the `set` string? */
static bool in_charset(const char *set, int32 sn, const char *ch, int cl)
{
    for (int32 j = 0; j < sn; ) {
        int l = u8_charlen((unsigned char)set[j]);
        if (l == cl && !memcmp(set + j, ch, (size_t)cl)) return true;
        j += l;
    }
    return false;
}
static Datum text_trim(Oid rt, const char *p, int32 n, const char *set, int32 sn, int mode)
{
    int32 b = 0, e = n;
    if (mode != 2) {   /* leading */
        while (b < e) {
            int l = u8_charlen((unsigned char)p[b]);
            if (!in_charset(set, sn, p + b, l)) break;
            b += l;
        }
    }
    if (mode != 1) {   /* trailing */
        while (e > b) {
            int32 k = e - 1;
            while (k > b && ((unsigned char)p[k] & 0xC0) == 0x80) k--;
            int l = u8_charlen((unsigned char)p[k]);
            if (k + l != e || !in_charset(set, sn, p + k, l)) break;
            e = k;
        }
    }
    return d_textn(rt, p + b, e - b);
}

static int32 bytes_find(const char *h, int32 hn, const char *nd, int32 nn, int32 from)
{
    if (nn == 0) return from;
    for (int32 i = from; i + nn <= hn; i++)
        if (h[i] == nd[0] && !memcmp(h + i, nd, (size_t)nn)) return i;
    return -1;
}

static const char *reserved_kw[] = {
  "all","analyse","analyze","and","any","array","as","asc","asymmetric","authorization",
  "between","bigint","binary","bit","boolean","both","case","cast","char","character",
  "check","coalesce","collate","collation","column","concurrently","constraint","create",
  "cross","current_catalog","current_date","current_role","current_schema","current_time",
  "current_timestamp","current_user","dec","decimal","default","deferrable","desc",
  "distinct","do","else","end","except","exists","extract","false","fetch","float","for",
  "foreign","freeze","from","full","grant","greatest","group","grouping","having","ilike",
  "in","initially","inner","inout","int","integer","intersect","interval","into","is",
  "isnull","join","json","json_array","json_arrayagg","json_exists","json_object",
  "json_objectagg","json_query","json_scalar","json_serialize","json_table","json_value",
  "lateral","leading","least","left","like","limit","localtime","localtimestamp",
  "merge_action","national","natural","nchar","none","normalize","not","notnull","null",
  "nullif","numeric","offset","on","only","or","order","out","outer","overlaps","overlay",
  "placing","position","precision","primary","real","references","returning","right","row",
  "select","session_user","setof","similar","smallint","some","substring","symmetric",
  "system_user","table","tablesample","then","time","timestamp","to","trailing","treat",
  "trim","true","union","unique","user","using","values","varchar","variadic","verbose",
  "when","where","window","with","xmlattributes","xmlconcat","xmlelement","xmlexists",
  "xmlforest","xmlnamespaces","xmlparse","xmlpi","xmlroot","xmlserialize","xmltable", NULL
};
static bool is_reserved_kw(const char *w)
{
    for (int i = 0; reserved_kw[i]; i++) if (!strcmp(reserved_kw[i], w)) return true;
    return false;
}
static char *quote_ident_str(const char *p, int32 n)
{
    bool safe = (n > 0);
    if (safe && !(islower((unsigned char)p[0]) || p[0] == '_')) safe = false;
    for (int32 i = 0; safe && i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (!(islower(c) || isdigit(c) || c == '_' || c == '$')) safe = false;
    }
    if (safe) {
        char *w = xstrndup(p, (size_t)n);
        if (is_reserved_kw(w)) safe = false;
        free(w);
    }
    if (safe) return xstrndup(p, (size_t)n);
    Buf b; buf_init(&b);
    buf_addc(&b, '"');
    for (int32 i = 0; i < n; i++) { if (p[i] == '"') buf_addc(&b, '"'); buf_addc(&b, p[i]); }
    buf_addc(&b, '"');
    return buf_steal(&b);
}
static char *quote_literal_str(const char *p, int32 n)
{
    bool bs = false;
    for (int32 i = 0; i < n; i++) if (p[i] == '\\') bs = true;
    Buf b; buf_init(&b);
    if (bs) buf_addc(&b, 'E');
    buf_addc(&b, '\'');
    for (int32 i = 0; i < n; i++) {
        if (p[i] == '\'') buf_addc(&b, '\'');
        else if (p[i] == '\\') buf_addc(&b, '\\');
        buf_addc(&b, p[i]);
    }
    buf_addc(&b, '\'');
    return buf_steal(&b);
}

/* ========================================================= pattern matching */

/* PostgreSQL's LIKE, implemented directly (bytes, but multibyte-aware for _ ) */
static int like_match(const char *t, int32 tn, const char *p, int32 pn,
                      int esc, bool ci, Session *s)
{
    int32 ti = 0, pi = 0;
    while (pi < pn) {
        unsigned char pc = (unsigned char)p[pi];
        if (esc >= 0 && pc == (unsigned char)esc) {
            pi++;
            if (pi >= pn)
                ereport(s, "22025", "LIKE pattern must not end with escape character");
            int pl = u8_charlen((unsigned char)p[pi]);
            if (ti >= tn) return 0;
            int tl = u8_charlen((unsigned char)t[ti]);
            if (tl != pl) return 0;
            if (ci) {
                for (int k = 0; k < tl; k++)
                    if (tolower((unsigned char)t[ti+k]) != tolower((unsigned char)p[pi+k])) return 0;
            } else if (memcmp(t + ti, p + pi, (size_t)tl)) return 0;
            ti += tl; pi += pl;
            continue;
        }
        if (pc == '%') {
            pi++;
            /* collapse a run of % and _ */
            while (pi < pn) {
                if ((unsigned char)p[pi] == '%') { pi++; continue; }
                if ((unsigned char)p[pi] == '_') {
                    if (ti >= tn) return 0;
                    ti += u8_charlen((unsigned char)t[ti]);
                    pi++;
                    continue;
                }
                break;
            }
            if (pi >= pn) return 1;
            while (ti <= tn) {
                if (like_match(t + ti, tn - ti, p + pi, pn - pi, esc, ci, s)) return 1;
                if (ti >= tn) break;
                ti += u8_charlen((unsigned char)t[ti]);
            }
            return 0;
        }
        if (pc == '_') {
            if (ti >= tn) return 0;
            ti += u8_charlen((unsigned char)t[ti]);
            pi++;
            continue;
        }
        {
            int pl = u8_charlen(pc);
            if (ti >= tn) return 0;
            int tl = u8_charlen((unsigned char)t[ti]);
            if (tl != pl) return 0;
            if (ci) {
                for (int k = 0; k < tl; k++)
                    if (tolower((unsigned char)t[ti+k]) != tolower((unsigned char)p[pi+k])) return 0;
            } else if (memcmp(t + ti, p + pi, (size_t)tl)) return 0;
            ti += tl; pi += pl;
        }
    }
    return ti == tn;
}

/* PostgreSQL's ARE class shorthands are not in POSIX ERE; rewrite them. */
static char *re_to_posix(const char *p, int32 n, bool extended_ws)
{
    Buf b; buf_init(&b);
    bool inbr = false;
    for (int32 i = 0; i < n; i++) {
        char c = p[i];
        if (extended_ws && !inbr && isspace((unsigned char)c)) continue;
        if (extended_ws && !inbr && c == '#') { while (i < n && p[i] != '\n') i++; continue; }
        if (c == '\\' && i + 1 < n) {
            char e = p[i+1];
            const char *rep = NULL;
            switch (e) {
            case 'd': rep = inbr ? "[:digit:]" : "[[:digit:]]"; break;
            case 'D': rep = inbr ? "[:digit:]" : "[^[:digit:]]"; break;
            case 's': rep = inbr ? "[:space:]" : "[[:space:]]"; break;
            case 'S': rep = inbr ? "[:space:]" : "[^[:space:]]"; break;
            case 'w': rep = inbr ? "[:alnum:]_" : "[[:alnum:]_]"; break;
            case 'W': rep = inbr ? "[:alnum:]_" : "[^[:alnum:]_]"; break;
            case 'y': rep = "\\b"; break;
            case 'A': rep = "^"; break;
            case 'Z': rep = "$"; break;
            case 'n': rep = "\n"; break;
            case 't': rep = "\t"; break;
            case 'r': rep = "\r"; break;
            default: break;
            }
            if (rep) { buf_adds(&b, rep); i++; continue; }
            buf_addc(&b, c); buf_addc(&b, p[i+1]); i++;
            continue;
        }
        if (c == '[' && !inbr) {
            inbr = true; buf_addc(&b, c);
            if (i + 1 < n && p[i+1] == '^') { buf_addc(&b, '^'); i++; }
            if (i + 1 < n && p[i+1] == ']') { buf_addc(&b, ']'); i++; }
            continue;
        }
        if (c == '[' && inbr && i + 1 < n && (p[i+1]==':' || p[i+1]=='.' || p[i+1]=='=')) {
            char k = p[i+1];
            buf_addc(&b, c); buf_addc(&b, k); i += 2;
            while (i < n && !(p[i] == k && i + 1 < n && p[i+1] == ']')) buf_addc(&b, p[i++]);
            if (i < n) { buf_addc(&b, k); buf_addc(&b, ']'); i++; }
            continue;
        }
        if (c == ']' && inbr) { inbr = false; buf_addc(&b, c); continue; }
        if (c == '(' && !inbr && i + 2 < n && p[i+1] == '?' && p[i+2] == ':') {
            buf_addc(&b, '('); i += 2; continue;   /* POSIX has no non-capturing group */
        }
        buf_addc(&b, c);
    }
    return buf_steal(&b);
}

typedef struct { regex_t re; bool global; bool valid; bool literal; } PgRegex;

static void re_compile(Session *s, PgRegex *r, const char *pat, int32 pn, const char *flags)
{
    int cf = REG_EXTENDED;
    bool extws = false;
    r->global = false;
    r->literal = false;
    if (flags) {
        for (const char *f = flags; *f; f++) {
            switch (*f) {
            case 'g': r->global = true; break;
            case 'i': cf |= REG_ICASE; break;
            case 'c': cf &= ~REG_ICASE; break;
            case 'n': case 'm': cf |= REG_NEWLINE; break;
            case 'p': cf |= REG_NEWLINE; break;
            case 's': cf &= ~REG_NEWLINE; break;
            case 'x': extws = true; break;
            case 'w': break;
            case 'q': r->literal = true; break;
            case 'b': case 'e': break;
            default:
                ereport(s, "22023", "invalid regular expression option: \"%c\"", *f);
            }
        }
    }
    if (!r->literal) {
        /* PostgreSQL names the unbalanced construct; glibc does not */
        int par = 0, brc = 0; bool inbr = false;
        for (int32 i = 0; i < pn; i++) {
            char ch = pat[i];
            if (ch == '\\') { i++; continue; }
            if (inbr) { if (ch == ']') inbr = false; continue; }
            if (ch == '[') { inbr = true; continue; }
            if (ch == '(') par++;
            else if (ch == ')') { par--; if (par < 0) break; }
            else if (ch == '{') brc++;
            else if (ch == '}') { brc--; if (brc < 0) brc = 0; }
        }
        if (inbr) ereport(s, "2201B", "invalid regular expression: brackets [] not balanced");
        if (par != 0) ereport(s, "2201B",
                              "invalid regular expression: parentheses () not balanced");
        if (brc != 0) ereport(s, "2201B", "invalid regular expression: braces {} not balanced");
    }
    char *tr;
    if (r->literal) {
        Buf qb; buf_init(&qb);
        for (int32 i = 0; i < pn; i++) {
            if (strchr(".[]()*+?{}|^$\\", pat[i])) buf_addc(&qb, '\\');
            buf_addc(&qb, pat[i]);
        }
        tr = buf_steal(&qb);
    } else tr = re_to_posix(pat, pn, extws);
    int rc = regcomp(&r->re, tr, cf);
    if (rc) {
        /* PostgreSQL has its own regex engine and its own wording */
        const char *msg;
        switch (rc) {
        case REG_EBRACK:  msg = "brackets [] not balanced"; break;
        case REG_EPAREN:  msg = "parentheses () not balanced"; break;
        case REG_EBRACE:  msg = "braces {} not balanced"; break;
        case REG_ERANGE:  msg = "invalid character range"; break;
        case REG_ESUBREG: msg = "invalid backreference number"; break;
        case REG_BADRPT:  msg = "quantifier operand invalid"; break;
        case REG_EESCAPE: msg = "invalid escape \\ sequence"; break;
        case REG_BADBR:   msg = "invalid repetition count(s)"; break;
        case REG_ECTYPE:  msg = "invalid character class"; break;
        case REG_ECOLLATE: msg = "invalid collating element"; break;
        default: {
            static char b2[256];
            regerror(rc, &r->re, b2, sizeof b2);
            msg = b2;
        }
        }
        free(tr);
        ereport(s, "2201B", "invalid regular expression: %s", msg);
    }
    free(tr);
    r->valid = true;
}
static void re_free(PgRegex *r) { if (r->valid) regfree(&r->re); r->valid = false; }

/* an integer argument of a regexp function, reported at its own position */
static int64 re_intarg(Session *s, Datum *args, int i, int64 dflt)
{
    if (args[i].isnull) return dflt;
    int save = s->err.position;
    s->err.position = (i < 16) ? s->argpos[i] : 0;
    int64 v = coerce_to(s, args[i], INT4OID, -1, 1).v.i;
    s->err.position = save;
    return v;
}

static bool pgre_search(PgRegex *r, const char *str, int32 off, regmatch_t *m, size_t nm)
{
    int ef = off > 0 ? REG_NOTBOL : 0;
    if (regexec(&r->re, str + off, nm, m, ef) != 0) return false;
    for (size_t i = 0; i < nm; i++)
        if (m[i].rm_so >= 0) { m[i].rm_so += off; m[i].rm_eo += off; }
    return true;
}
static size_t re_ngroups(PgRegex *r) { return r->re.re_nsub; }

/* ============================================================== JSON helpers
 * pgvc.h's JsonVal API can read a document but not walk or rebuild one, so we
 * keep a tiny private DOM.  We always enter it by parsing the text produced by
 * json_to_text() and always leave it through json_parse(), which keeps
 * canonical formatting and number handling in jsonval.c where they belong. */

typedef struct JN JN;
struct JN {
    int   kind;             /* 0 null 1 bool 2 num 3 str 4 array 5 object */
    bool  b;
    char *num;              /* verbatim JSON number text */
    char *str;              /* decoded string */
    int   n;
    JN  **el;               /* array elements */
    char **keys; JN **vals; /* object members */
};

static JN *jn_new(int kind) { JN *j = xmalloc(sizeof *j); memset(j, 0, sizeof *j); j->kind = kind; return j; }
static JN *jn_null(void) { return jn_new(0); }
static JN *jn_bool(bool b) { JN *j = jn_new(1); j->b = b; return j; }
static JN *jn_num(const char *t) { JN *j = jn_new(2); j->num = xstrdup(t); return j; }
static JN *jn_str(const char *t) { JN *j = jn_new(3); j->str = xstrdup(t); return j; }
static JN *jn_arr(void) { return jn_new(4); }
static JN *jn_obj(void) { return jn_new(5); }
static void jn_push(JN *a, JN *v)
{ a->el = xrealloc(a->el, (size_t)(a->n + 1) * sizeof(JN *)); a->el[a->n++] = v; }
static void jn_put(JN *o, const char *k, JN *v)
{
    for (int i = 0; i < o->n; i++)
        if (!strcmp(o->keys[i], k)) { o->vals[i] = v; return; }
    o->keys = xrealloc(o->keys, (size_t)(o->n + 1) * sizeof(char *));
    o->vals = xrealloc(o->vals, (size_t)(o->n + 1) * sizeof(JN *));
    o->keys[o->n] = xstrdup(k); o->vals[o->n] = v; o->n++;
}
static JN *jn_get(const JN *o, const char *k)
{
    if (!o || o->kind != 5) return NULL;
    for (int i = 0; i < o->n; i++) if (!strcmp(o->keys[i], k)) return o->vals[i];
    return NULL;
}
static JN *jn_copy(const JN *v)
{
    if (!v) return NULL;
    JN *r = jn_new(v->kind);
    r->b = v->b;
    if (v->num) r->num = xstrdup(v->num);
    if (v->str) r->str = xstrdup(v->str);
    if (v->kind == 4) { for (int i = 0; i < v->n; i++) jn_push(r, jn_copy(v->el[i])); }
    else if (v->kind == 5) { for (int i = 0; i < v->n; i++) jn_put(r, v->keys[i], jn_copy(v->vals[i])); }
    return r;
}

static const char *jn_skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }
static JN *jn_parse_at(Session *s, const char **pp);

static char *jn_parse_string(Session *s, const char **pp)
{
    const char *p = *pp;
    if (*p != '"') ereport(s, "22P02", "invalid input syntax for type json");
    p++;
    Buf b; buf_init(&b);
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': buf_addc(&b, '\n'); break;
            case 't': buf_addc(&b, '\t'); break;
            case 'r': buf_addc(&b, '\r'); break;
            case 'b': buf_addc(&b, '\b'); break;
            case 'f': buf_addc(&b, '\f'); break;
            case '/': buf_addc(&b, '/'); break;
            case '"': buf_addc(&b, '"'); break;
            case '\\': buf_addc(&b, '\\'); break;
            case 'u': {
                int cp = (int)strtol((char[5]){p[1],p[2],p[3],p[4],0}, NULL, 16);
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p[1] == '\\' && p[2] == 'u') {
                    int lo = (int)strtol((char[5]){p[3],p[4],p[5],p[6],0}, NULL, 16);
                    p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                char tmp[4]; int l = u8_encode(cp, tmp);
                buf_add(&b, tmp, (size_t)l);
                break;
            }
            default: buf_addc(&b, *p); break;
            }
            p++;
        } else buf_addc(&b, *p++);
    }
    if (*p == '"') p++;
    *pp = p;
    char *r = b.data ? buf_steal(&b) : xstrdup("");
    return r;
}

static JN *jn_parse_at(Session *s, const char **pp)
{
    const char *p = jn_skip_ws(*pp);
    JN *r;
    if (*p == '{') {
        p++;
        r = jn_obj();
        p = jn_skip_ws(p);
        while (*p && *p != '}') {
            char *k = jn_parse_string(s, &p);
            p = jn_skip_ws(p);
            if (*p == ':') p++;
            p = jn_skip_ws(p);
            JN *v = jn_parse_at(s, &p);
            jn_put(r, k, v);
            free(k);
            p = jn_skip_ws(p);
            if (*p == ',') { p++; p = jn_skip_ws(p); }
        }
        if (*p == '}') p++;
    } else if (*p == '[') {
        p++;
        r = jn_arr();
        p = jn_skip_ws(p);
        while (*p && *p != ']') {
            jn_push(r, jn_parse_at(s, &p));
            p = jn_skip_ws(p);
            if (*p == ',') { p++; p = jn_skip_ws(p); }
        }
        if (*p == ']') p++;
    } else if (*p == '"') {
        char *v = jn_parse_string(s, &p);
        r = jn_str(v); free(v);
    } else if (!strncmp(p, "true", 4))  { r = jn_bool(true);  p += 4; }
    else if (!strncmp(p, "false", 5))   { r = jn_bool(false); p += 5; }
    else if (!strncmp(p, "null", 4))    { r = jn_null();      p += 4; }
    else {
        const char *st = p;
        if (*p == '-' || *p == '+') p++;
        while (isdigit((unsigned char)*p)) p++;
        if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
        if (*p == 'e' || *p == 'E') { p++; if (*p=='+'||*p=='-') p++; while (isdigit((unsigned char)*p)) p++; }
        if (p == st) ereport(s, "22P02", "invalid input syntax for type json");
        char *t = xstrndup(st, (size_t)(p - st));
        r = jn_num(t); free(t);
    }
    *pp = p;
    return r;
}
static JN *jn_parse(Session *s, const char *txt)
{
    const char *p = txt;
    JN *r = jn_parse_at(s, &p);
    return r;
}

static void jn_esc(Buf *b, const char *t)
{
    buf_addc(b, '"');
    for (const unsigned char *q = (const unsigned char *)t; *q; q++) {
        switch (*q) {
        case '"': buf_adds(b, "\\\""); break;
        case '\\': buf_adds(b, "\\\\"); break;
        case '\n': buf_adds(b, "\\n"); break;
        case '\r': buf_adds(b, "\\r"); break;
        case '\t': buf_adds(b, "\\t"); break;
        case '\b': buf_adds(b, "\\b"); break;
        case '\f': buf_adds(b, "\\f"); break;
        default:
            if (*q < 0x20) buf_printf(b, "\\u%04x", *q);
            else buf_addc(b, (char)*q);
        }
    }
    buf_addc(b, '"');
}

/* style 0 = compact, 1 = ", " + ": " (jsonb-like), 2 = ", " + " : " (json_build_*) */
static void jn_write(Buf *b, const JN *v, int style)
{
    const char *sep = style ? ", " : ",";
    const char *col = style == 2 ? " : " : style == 1 ? ": " : ":";
    switch (v->kind) {
    case 0: buf_adds(b, "null"); break;
    case 1: buf_adds(b, v->b ? "true" : "false"); break;
    case 2: buf_adds(b, v->num); break;
    case 3: jn_esc(b, v->str); break;
    case 4:
        buf_addc(b, '[');
        for (int i = 0; i < v->n; i++) { if (i) buf_adds(b, sep); jn_write(b, v->el[i], style); }
        buf_addc(b, ']');
        break;
    default:
        buf_addc(b, '{');
        for (int i = 0; i < v->n; i++) {
            if (i) buf_adds(b, sep);
            jn_esc(b, v->keys[i]);
            buf_adds(b, col);
            jn_write(b, v->vals[i], style);
        }
        buf_addc(b, '}');
        break;
    }
}
static char *jn_text(const JN *v, int style)
{
    Buf b; buf_init(&b);
    jn_write(&b, v, style);
    return buf_steal(&b);
}

static Datum d_jsonb_text(Session *s, const char *t)
{
    const char *err = NULL;
    JsonVal *v = json_parse(t, strlen(t), &err);
    if (!v) ereport(s, "22P02", "invalid input syntax for type jsonb");
    Datum d; memset(&d, 0, sizeof d);
    d.type = JSONBOID; d.v.jb = v;
    return d;
}
static Datum d_jsonb_node(Session *s, const JN *v)
{
    char *t = jn_text(v, 0);
    Datum d = d_jsonb_text(s, t);
    free(t);
    return d;
}
/* Read any json/jsonb Datum into the private DOM. */
static JN *jn_of(Session *s, Datum d)
{
    if (d.isnull) return NULL;
    if (d.type == JSONBOID) {
        char *t = json_to_text(d.v.jb, true);
        JN *r = jn_parse(s, t);
        free(t);
        return r;
    }
    char *t = as_cstr(s, d);
    JN *r = jn_parse(s, t);
    free(t);
    return r;
}
/* Direct JsonVal accessors, used where jsonval.c's own ordering is the
 * contract (object iteration) rather than our private DOM. */
static JsonVal *jv_of(Session *s, Datum d)
{
    if (d.isnull) return NULL;
    if (d.type == JSONBOID) return d.v.jb;
    const char *err = NULL;
    char *o; int32 n;
    const char *p = as_bytes(s, d, &n, &o);
    JsonVal *v = json_parse_ex(p, (size_t)n, false, &err);
    free(o);
    if (!v) ereport(s, "22P02", "invalid input syntax for type json");
    return v;
}
static Datum d_jsonb_val(JsonVal *v)
{
    Datum d; memset(&d, 0, sizeof d);
    d.type = JSONBOID; d.v.jb = v;
    if (!v) d.isnull = true;
    return d;
}
/* the ->> / _text rendering of a JsonVal */
static char *jv_astext(const JsonVal *v)
{
    if (!v) return NULL;
    switch (json_kind(v)) {
    case 0: return NULL;
    case 1: { char *t = json_to_text(v, true); return t; }
    case 2: return xstrdup(json_num_text(v));
    case 3: return xstrdup(json_str_value(v));
    default: return json_to_text(v, true);
    }
}

/* The leaf values a text-search vector is built from, in document order.
 * `filter` is jsonb_to_tsvector's array of "string" / "numeric" / "boolean" /
 * "key" / "all"; a NULL filter means strings only, which is what
 * to_tsvector(config, json) uses. */
static void jsonts_walk(JN *v, bool want_str, bool want_num, bool want_bool,
                        bool want_key, char ***out, int *n, int *cap)
{
    if (!v) return;
    switch (v->kind) {
    case 1:                                   /* boolean */
        if (want_bool) {
            if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *out = xrealloc(*out, (size_t)*cap * sizeof(char *)); }
            (*out)[(*n)++] = xstrdup(v->b ? "true" : "false");
        }
        return;
    case 2:                                   /* number */
        if (want_num) {
            if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *out = xrealloc(*out, (size_t)*cap * sizeof(char *)); }
            (*out)[(*n)++] = xstrdup(v->num ? v->num : "");
        }
        return;
    case 3:                                   /* string */
        if (want_str) {
            if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *out = xrealloc(*out, (size_t)*cap * sizeof(char *)); }
            (*out)[(*n)++] = xstrdup(v->str ? v->str : "");
        }
        return;
    case 4:
        for (int i = 0; i < v->n; i++)
            jsonts_walk(v->el[i], want_str, want_num, want_bool, want_key, out, n, cap);
        return;
    case 5:
        for (int i = 0; i < v->n; i++) {
            if (want_key) {
                if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *out = xrealloc(*out, (size_t)*cap * sizeof(char *)); }
                (*out)[(*n)++] = xstrdup(v->keys[i] ? v->keys[i] : "");
            }
            jsonts_walk(v->vals[i], want_str, want_num, want_bool, want_key, out, n, cap);
        }
        return;
    default:
        return;
    }
}

static char **jsonts_parts(Session *s, Datum doc, Datum *filter, int *np)
{
    bool want_str = true, want_num = false, want_bool = false, want_key = false;
    if (filter) {
        want_str = false;
        JN *f = jn_of(s, *filter);
        if (!f || f->kind != 4)
            ereport(s, "22023", "wrong flag type, only arrays and scalars are allowed");
        for (int i = 0; i < f->n; i++) {
            const char *w = f->el[i] && f->el[i]->kind == 3 ? f->el[i]->str : "";
            if (!strcmp(w, "string")) want_str = true;
            else if (!strcmp(w, "numeric")) want_num = true;
            else if (!strcmp(w, "boolean")) want_bool = true;
            else if (!strcmp(w, "key")) want_key = true;
            else if (!strcmp(w, "all")) { want_str = want_num = want_bool = want_key = true; }
            else ereport(s, "22023", "wrong flag in flag array: \"%s\"", w);
        }
    }
    JN *v = jn_of(s, doc);
    char **out = NULL; int n = 0, cap = 0;
    jsonts_walk(v, want_str, want_num, want_bool, want_key, &out, &n, &cap);
    *np = n;
    return out;
}

static const char *jn_typename(const JN *v)
{
    switch (v->kind) {
    case 0: return "null";
    case 1: return "boolean";
    case 2: return "number";
    case 3: return "string";
    case 4: return "array";
    default: return "object";
    }
}
/* scalar text of a node, as ->> and #>> produce it (NULL for a JSON null) */
static char *jn_astext(const JN *v)
{
    if (!v) return NULL;
    switch (v->kind) {
    case 0: return NULL;
    case 1: return xstrdup(v->b ? "true" : "false");
    case 2: return xstrdup(v->num);
    case 3: return xstrdup(v->str);
    default: return jn_text(v, 1);
    }
}
static int jn_cmp(const JN *a, const JN *b)
{
    /* jsonb ordering: object > array > boolean > number > string > null */
    static const int rank[6] = {0, 3, 2, 1, 4, 5};
    if (a->kind != b->kind) return rank[a->kind] < rank[b->kind] ? -1 : 1;
    switch (a->kind) {
    case 0: return 0;
    case 1: return (a->b == b->b) ? 0 : (a->b ? 1 : -1);
    case 2: { double x = strtod(a->num, NULL), y = strtod(b->num, NULL);
              return x < y ? -1 : x > y ? 1 : 0; }
    case 3: { int c = strcmp(a->str, b->str); return c < 0 ? -1 : c > 0 ? 1 : 0; }
    case 4: {
        if (a->n != b->n) return a->n < b->n ? -1 : 1;
        for (int i = 0; i < a->n; i++) { int c = jn_cmp(a->el[i], b->el[i]); if (c) return c; }
        return 0;
    }
    default: {
        if (a->n != b->n) return a->n < b->n ? -1 : 1;
        for (int i = 0; i < a->n; i++) {
            int c = strcmp(a->keys[i], b->keys[i]); if (c) return c < 0 ? -1 : 1;
            c = jn_cmp(a->vals[i], b->vals[i]); if (c) return c;
        }
        return 0;
    }
    }
}

#if PGVC_VN >= 120
/* The SQL/JSON path language lives in jsonpath.c; the helpers here convert
 * between that file's JsonVal documents and the JN nodes the SQL/JSON query
 * functions and JSON_TABLE below work with, so the server has exactly one
 * path engine. */

typedef struct { JN **v; int n, cap; } JList;
static void jl_add(JList *l, JN *v)
{
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 8;
                          l->v = xrealloc(l->v, (size_t)l->cap * sizeof(JN *)); }
    l->v[l->n++] = v;
}
static void jl_init(JList *l) { memset(l, 0, sizeof *l); }

static JsonVal *jv_of_jn(Session *s, JN *v)
{
    char *t = jn_text(v, 0);
    const char *err = NULL;
    JsonVal *r = json_parse(t, strlen(t), &err);
    free(t);
    if (!r) ereport(s, "22P02", "invalid input syntax for type json");
    return r;
}

static JN *jn_of_jv(Session *s, const JsonVal *v)
{
    char *t = json_to_text(v, true);
    JN *r = jn_parse(s, t);
    free(t);
    return r;
}

/* Run a jsonpath over a document; returns the matched items. */
static JList jp_run_vars(Session *s, JN *doc, const char *path, JN *vars)
{
    JList l; jl_init(&l);
    JsonPath *jp = jsonpath_parse(s, path, (int)strlen(path));
    JsonPathList out; memset(&out, 0, sizeof out);
    JsonVal *d = jv_of_jn(s, doc);
    JsonVal *vv = vars ? jv_of_jn(s, vars) : NULL;
    jsonpath_run(s, jp, d, vv, true, false, &out);
    for (int i = 0; i < out.n; i++) jl_add(&l, jn_of_jv(s, out.v[i]));
    free(out.v);
    jsonpath_free(jp);
    return l;
}

#if PGVC_VN >= 170
/* -------------------------------------------- SQL/JSON query functions (17)
 * JSON_EXISTS, JSON_VALUE and JSON_QUERY run a jsonpath over a context item
 * and then apply the ON EMPTY / ON ERROR behaviour the query asked for. */

/* The value an ON EMPTY / ON ERROR clause asks for. */
static Datum json_behav_value(Session *s, Expr *e, int how, Expr *dflt, Oid rt,
                              Datum (*evalx)(void *, Expr *), void *ctx)
{
    switch (how) {
    case 1: return d_null(rt);                        /* handled by the caller */
    case 3: { Buf b; buf_init(&b); buf_adds(&b, "[]");
              Datum r = in_func(s, rt == JSONBOID ? JSONBOID : JSONOID, -1, b.data, (int32)b.len);
              buf_free(&b);
              if (rt != JSONOID && rt != JSONBOID) { char *t = out_func(s, r);
                                                     Datum q = in_func(s, rt, -1, t, (int32)strlen(t));
                                                     free(t); return q; }
              return r; }
    case 4: { Buf b; buf_init(&b); buf_adds(&b, "{}");
              Datum r = in_func(s, rt == JSONBOID ? JSONBOID : JSONOID, -1, b.data, (int32)b.len);
              buf_free(&b);
              if (rt != JSONOID && rt != JSONBOID) { char *t = out_func(s, r);
                                                     Datum q = in_func(s, rt, -1, t, (int32)strlen(t));
                                                     free(t); return q; }
              return r; }
    case 5: return d_bool(true);
    case 6: return d_bool(false);
    case 7: return d_null(BOOLOID);
    case 2: { Datum d = evalx(ctx, dflt); return coerce_to(s, d, rt, e->casttypmod, 2); }
    default: return d_null(rt);
    }
}

/* PASSING values, as a jsonpath variable object. */
static JN *json_pass_vars(Session *s, JsonBehav *bh,
                          Datum (*evalx)(void *, Expr *), void *ctx)
{
    if (!bh || !bh->npassing) return NULL;
    JN *o = jn_obj();
    for (int i = 0; i < bh->npassing; i++) {
        Datum d = evalx(ctx, bh->passing[i]);
        if (d.isnull) { jn_put(o, bh->passnames[i], jn_null()); continue; }
        if (d.type == JSONOID || d.type == JSONBOID) jn_put(o, bh->passnames[i], jn_of(s, d));
        else if (d.type == BOOLOID) jn_put(o, bh->passnames[i], jn_bool(d.v.i != 0));
        else if (f_is_numfam(d.type)) { char *t = out_func(s, d);
                                        jn_put(o, bh->passnames[i], jn_num(t)); free(t); }
        else { char *t = out_func(s, d); jn_put(o, bh->passnames[i], jn_str(t)); free(t); }
    }
    return o;
}

Datum eval_json_query_fn(Session *s, Expr *e, Datum ctxitem, Datum pathd,
                         Datum (*evalx)(void *, Expr *), void *ctx)
{
    JsonBehav *bh = e->json_behav;
    int kind = e->json_ctor;                 /* 7 query, 8 value, 9 exists */
    Oid rt = e->casttype ? e->casttype
                         : (kind == 7 ? JSONBOID : kind == 8 ? TEXTOID : BOOLOID);
    if (kind == 7 && !e->casttype && ctxitem.type == JSONOID) rt = JSONOID;
    int on_empty = bh && bh->on_empty >= 0 ? bh->on_empty : 0;
    int on_error = bh && bh->on_error >= 0 ? bh->on_error
                                           : (kind == 9 ? 6 : 0);
    if (ctxitem.isnull || pathd.isnull) return d_null(rt);

    /* run the path, catching whatever it raises */
    jmp_buf save;
    ErrState prev = s->err;
    volatile int nitems = 0;
    volatile bool failed = false;
    JN **items = NULL;
    char *path = as_cstr(s, pathd);
    memcpy(save, s->jmp, sizeof(jmp_buf));
    if (setjmp(s->jmp) == 0) {
        JN *doc = jn_of(s, ctxitem);
        JList l = jp_run_vars(s, doc, path, json_pass_vars(s, bh, evalx, ctx));
        nitems = l.n;
        items = l.v;
    } else failed = true;
    memcpy(s->jmp, save, sizeof(jmp_buf));
    ErrState caught = s->err;
    s->err = prev;
    free(path);

    if (failed) {
        if (on_error == 1) {
            s->err = caught;
            longjmp(s->jmp, 1);
        }
        return json_behav_value(s, e, on_error, bh ? bh->error_default : NULL, rt, evalx, ctx);
    }
    if (kind == 9) {                                 /* JSON_EXISTS */
        return d_bool(nitems > 0);
    }
    if (nitems == 0) {
        if (on_empty == 1)
            ereport(s, "22035", "no SQL/JSON item found for specified path");
        if (on_empty == 0 && bh && bh->on_empty < 0 && on_error == 1) { }
        return json_behav_value(s, e, on_empty, bh ? bh->empty_default : NULL, rt, evalx, ctx);
    }
    if (kind == 8) {                                 /* JSON_VALUE */
        if (nitems > 1) {
            if (on_error == 1)
                ereport(s, "22000", "JSON path expression in JSON_VALUE should return "
                                    "singleton scalar item");
            return json_behav_value(s, e, on_error, bh ? bh->error_default : NULL, rt, evalx, ctx);
        }
        JN *v = items[0];
        if (v->kind == 4 || v->kind == 5) {
            if (on_error == 1)
                ereport(s, "22000", "JSON path expression in JSON_VALUE should return "
                                    "singleton scalar item");
            return json_behav_value(s, e, on_error, bh ? bh->error_default : NULL, rt, evalx, ctx);
        }
        if (v->kind == 0) return d_null(rt);
        char *t = (v->kind == 3) ? xstrdup(v->str) : jn_text(v, 0);
        Datum r;
        jmp_buf sv2;
        ErrState pv2 = s->err;
        volatile bool bad = false;
        memcpy(sv2, s->jmp, sizeof(jmp_buf));
        memset(&r, 0, sizeof r);
        if (setjmp(s->jmp) == 0) r = in_func(s, rt, e->casttypmod, t, (int32)strlen(t));
        else bad = true;
        memcpy(s->jmp, sv2, sizeof(jmp_buf));
        ErrState c2 = s->err;
        s->err = pv2;
        free(t);
        if (!bad) return r;
        if (on_error == 1) { s->err = c2; longjmp(s->jmp, 1); }
        return json_behav_value(s, e, on_error, bh ? bh->error_default : NULL, rt, evalx, ctx);
    }
    /* JSON_QUERY */
    int wrap = bh ? bh->wrapper : 0;
    bool wrapit = (wrap == 1) || (wrap == 2 && nitems > 1);
    if (!wrapit && nitems > 1) {
        if (on_error == 1)
            ereport(s, "22000", "JSON path expression in JSON_QUERY should return "
                                "singleton item without wrapper");
        return json_behav_value(s, e, on_error, bh ? bh->error_default : NULL, rt, evalx, ctx);
    }
    char *txt;
    if (wrapit) {
        JN *a = jn_arr();
        for (int i = 0; i < nitems; i++) jn_push(a, items[i]);
        txt = jn_text(a, 0);
    } else if (bh && bh->quotes && items[0]->kind == 3) {
        txt = xstrdup(items[0]->str);
    } else txt = jn_text(items[0], 0);
    Datum r;
    jmp_buf sv3;
    ErrState pv3 = s->err;
    volatile bool bad3 = false;
    memcpy(sv3, s->jmp, sizeof(jmp_buf));
    memset(&r, 0, sizeof r);
    if (setjmp(s->jmp) == 0) {
        if (rt == JSONBOID || rt == JSONOID) r = in_func(s, rt, -1, txt, (int32)strlen(txt));
        else r = in_func(s, rt, e->casttypmod, txt, (int32)strlen(txt));
    } else bad3 = true;
    memcpy(s->jmp, sv3, sizeof(jmp_buf));
    ErrState c3 = s->err;
    s->err = pv3;
    free(txt);
    if (!bad3) return r;
    if (on_error == 1) { s->err = c3; longjmp(s->jmp, 1); }
    return json_behav_value(s, e, on_error, bh ? bh->error_default : NULL, rt, evalx, ctx);
}
#endif

#if PGVC_VN >= 170
/* ------------------------------------------------------------ JSON_TABLE
 * Every column is a JSON_VALUE or JSON_QUERY over the current row item; the
 * nested paths of one level are unioned, and a level with no matching rows
 * still contributes one all-NULL row (PostgreSQL's default OUTER plan). */
typedef struct { Datum **rows; int n, cap; } JTRows;

static void jt_add(JTRows *r, Datum *row)
{
    if (r->n == r->cap) { r->cap = r->cap ? r->cap * 2 : 8;
                          r->rows = xrealloc(r->rows, (size_t)r->cap * sizeof(Datum *)); }
    r->rows[r->n++] = row;
}

/* Number the output slots of a column list, depth first. */
/* namepos doubles as the output slot once the table has been numbered. */
static void jt_number(JsonTableCol **cols, int ncols, int *next)
{
    for (int i = 0; i < ncols; i++) {
        if (cols[i]->kind == 3) { jt_number(cols[i]->cols, cols[i]->ncols, next); continue; }
        cols[i]->namepos = (*next)++;
    }
}

static void jt_collect(JsonTableCol **cols, int ncols, JsonTableCol ***out, int *nout)
{
    for (int i = 0; i < ncols; i++) {
        if (cols[i]->kind == 3) { jt_collect(cols[i]->cols, cols[i]->ncols, out, nout); continue; }
        *out = xrealloc(*out, (size_t)(*nout + 1) * sizeof(JsonTableCol *));
        (*out)[(*nout)++] = cols[i];
    }
}

typedef struct {
    Session *s;
    Datum (*evalx)(void *, Expr *);
    void  *ctx;
    JN    *vars;
    int    ntotal;
    JsonTableCol **flat;
} JTRun;

static Datum jt_column_value(JTRun *run, JsonTableCol *col, JN *item)
{
    Session *s = run->s;
    Expr fake; memset(&fake, 0, sizeof fake);
    JsonBehav bh; memset(&bh, 0, sizeof bh);
    bh.wrapper = col->wrapper; bh.quotes = col->quotes;
    bh.on_empty = col->on_empty; bh.on_error = col->on_error;
    bh.empty_default = col->empty_default; bh.error_default = col->error_default;
    fake.json_behav = &bh;
    fake.casttype = col->type; fake.casttypmod = col->typmod;
    if (col->kind == 2) {
        fake.json_ctor = 9;
        fake.casttype = 0;
        Datum d = eval_json_query_fn(s, &fake, d_jsonb_node(s, item),
                                     d_text(TEXTOID, col->path), run->evalx, run->ctx);
        if (d.isnull) return d_null(col->type);
        bool tv = d.v.i != 0;
        if (col->type == BOOLOID) return d_bool(tv);
        if (col->type == INT2OID || col->type == INT4OID || col->type == INT8OID)
            return d_int(col->type, tv ? 1 : 0);
        return in_func(s, col->type, col->typmod, tv ? "true" : "false", tv ? 4 : 5);
    }
    bool asjson = col->format_json || col->type == JSONOID || col->type == JSONBOID;
    fake.json_ctor = asjson ? 7 : 8;
    return eval_json_query_fn(s, &fake, d_jsonb_node(s, item),
                              d_text(TEXTOID, col->path), run->evalx, run->ctx);
}

static void jt_expand(JTRun *run, JsonTableCol **cols, int ncols, JN *item, int ord,
                      JTRows *out)
{
    Session *s = run->s;
    Datum *base = xmalloc((size_t)(run->ntotal ? run->ntotal : 1) * sizeof(Datum));
    for (int i = 0; i < run->ntotal; i++) base[i] = d_null(run->flat[i]->type);
    for (int i = 0; i < ncols; i++) {
        JsonTableCol *col = cols[i];
        if (col->kind == 3) continue;
        if (col->kind == 1) { base[col->namepos] = d_int(INT4OID, ord); continue; }
        base[col->namepos] = jt_column_value(run, col, item);
    }
    JTRows sub; memset(&sub, 0, sizeof sub);
    for (int i = 0; i < ncols; i++) {
        JsonTableCol *col = cols[i];
        if (col->kind != 3) continue;
        JList l = jp_run_vars(s, item, col->path, run->vars);
        for (int k = 0; k < l.n; k++)
            jt_expand(run, col->cols, col->ncols, l.v[k], k + 1, &sub);
    }
    if (sub.n == 0) { jt_add(out, base); return; }
    for (int k = 0; k < sub.n; k++) {
        Datum *row = sub.rows[k];
        for (int i = 0; i < ncols; i++) {
            JsonTableCol *col = cols[i];
            if (col->kind == 3) continue;
            row[col->namepos] = base[col->namepos];
        }
        jt_add(out, row);
    }
    free(sub.rows);
}

/* Every $name the path mentions must have been supplied by PASSING. */
void json_check_vars(Session *s, const char *path, JN *vars)
{
    for (const char *p = path; *p; p++) {
        if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } if (!*p) break; continue; }
        if (*p != '$') continue;
        const char *q = p + 1;
        if (!(isalpha((unsigned char)*q) || *q == '_')) continue;
        const char *st = q;
        while (isalnum((unsigned char)*q) || *q == '_') q++;
        char *nm = xstrndup(st, (size_t)(q - st));
        if (!vars || !jn_get(vars, nm))
            ereport(s, "42704", "could not find jsonpath variable \"%s\"", nm);
        free(nm);
        p = q - 1;
    }
}

/* The first lexical token of a leftover jsonpath fragment. */
static char *jp_first_token(const char *p)
{
    if (!*p) return NULL;
    if (*p == '$' || isalpha((unsigned char)*p) || *p == '_') {
        const char *q = p + 1;
        while (isalnum((unsigned char)*q) || *q == '_') q++;
        return xstrndup(p, (size_t)(q - p));
    }
    return xstrndup(p, 1);
}

/* Validate a jsonpath literal the way parse analysis does, reporting the text
 * left over when the path does not parse to the end.  jsonpath.c's parser
 * consumes the whole literal, so anything it rejects is reported at the token
 * its syntax error names. */
char *jsonpath_leftover(Session *s, const char *path)
{
    jmp_buf save;
    ErrState prev = s->err;
    memcpy(save, s->jmp, sizeof(jmp_buf));
    volatile bool failed = false;
    if (setjmp(s->jmp) == 0) {
        JsonPath *jp = jsonpath_parse(s, path, (int)strlen(path));
        jsonpath_free(jp);
    } else failed = true;
    memcpy(s->jmp, save, sizeof(jmp_buf));
    ErrState caught = s->err;
    s->err = prev;
    if (!failed) return NULL;
    if (strcmp(caught.sqlstate, "42601")) return NULL;
    const char *q = strstr(caught.message, "at or near \"");
    if (q) { q += 12;
             const char *e2 = strchr(q, '"');
             if (e2) return xstrndup(q, (size_t)(e2 - q)); }
    return jp_first_token(path);
}
/* Run a JSON_TABLE and hand the caller its columns and rows. */
int json_table_run(Session *s, JsonTableDef *jt, Datum ctxd, Datum pathd,
                   Datum (*evalx)(void *, Expr *), void *ctx,
                   JsonTableCol ***colsout, int *ncolsout, Datum ***rowsout)
{
    JsonTableCol **flat = NULL; int nflat = 0;
    jt_collect(jt->cols, jt->ncols, &flat, &nflat);
    int next = 0;
    jt_number(jt->cols, jt->ncols, &next);
    *colsout = flat;
    *ncolsout = nflat;
    *rowsout = NULL;
    if (ctxd.isnull || pathd.isnull) return 0;
    JsonBehav pb; memset(&pb, 0, sizeof pb);
    pb.passing = jt->passing; pb.passnames = jt->passnames; pb.npassing = jt->npassing;
    JN *vars = json_pass_vars(s, &pb, evalx, ctx);
    JN *doc = jn_of(s, ctxd);
    char *path = as_cstr(s, pathd);
    json_check_vars(s, path, vars);
    JTRun run; run.s = s; run.evalx = evalx; run.ctx = ctx; run.vars = vars;
    run.ntotal = nflat; run.flat = flat;
    JTRows out; memset(&out, 0, sizeof out);

    jmp_buf save;
    ErrState prev = s->err;
    volatile int nitems = 0;
    volatile bool failed = false;
    JN **items = NULL;
    memcpy(save, s->jmp, sizeof(jmp_buf));
    if (setjmp(s->jmp) == 0) {
        JList l = jp_run_vars(s, doc, path, vars);
        nitems = l.n; items = l.v;
    } else failed = true;
    memcpy(s->jmp, save, sizeof(jmp_buf));
    ErrState caught = s->err;
    s->err = prev;
    free(path);
    if (failed) {
        if (jt->on_error == 1) { s->err = caught; longjmp(s->jmp, 1); }
        return 0;
    }
    for (int i = 0; i < nitems; i++)
        jt_expand(&run, jt->cols, jt->ncols, items[i], i + 1, &out);
    *rowsout = out.rows;
    return out.n;
}

#endif  /* PGVC_VN >= 170 */
#endif /* PGVC_VN >= 120 */

/* ============================================================ array helpers */

static ArrayVal *arr_of(Datum d) { return d.v.arr; }
static int arr_count(Datum d) { return (d.isnull || !d.v.arr) ? 0 : d.v.arr->nelem; }

static bool arr_has(Session *s, ArrayVal *a, Datum e)
{
    for (int i = 0; i < a->nelem; i++) {
        if (a->elems[i].isnull) { if (e.isnull) return true; continue; }
        if (e.isnull) continue;
        if (d_cmp(s, a->elems[i], e) == 0) return true;
    }
    return false;
}
static bool arr_contains(Session *s, ArrayVal *a, ArrayVal *b)
{
    for (int i = 0; i < b->nelem; i++) if (!arr_has(s, a, b->elems[i])) return false;
    return true;
}
static bool arr_overlap(Session *s, ArrayVal *a, ArrayVal *b)
{
    for (int i = 0; i < b->nelem; i++)
        if (!b->elems[i].isnull && arr_has(s, a, b->elems[i])) return true;
    return false;
}
static Oid arr_elemtype(Datum d)
{
    if (d.v.arr && d.v.arr->elemtype) return d.v.arr->elemtype;
    Oid e = elem_type_of(d.type);
    return e ? e : TEXTOID;
}

/* ============================================================ range helpers */

typedef struct { bool inf, inc, lower; Datum val; } RBound;

static RBound rb_lower(RangeVal *r)
{ RBound b; b.inf = r->lower_inf; b.inc = r->lower_inc; b.lower = true; b.val = r->lower; return b; }
static RBound rb_upper(RangeVal *r)
{ RBound b; b.inf = r->upper_inf; b.inc = r->upper_inc; b.lower = false; b.val = r->upper; return b; }

static int rb_cmp(Session *s, RBound a, RBound b)
{
    if (a.inf || b.inf) {
        if (a.inf && b.inf) return (a.lower == b.lower) ? 0 : (a.lower ? -1 : 1);
        if (a.inf) return a.lower ? -1 : 1;
        return b.lower ? 1 : -1;
    }
    int c = d_cmp(s, a.val, b.val);
    if (c) return c;
    if (!a.inc && !b.inc) {
        if (a.lower == b.lower) return 0;
        return a.lower ? 1 : -1;
    }
    if (!a.inc) return a.lower ? 1 : -1;
    if (!b.inc) return b.lower ? -1 : 1;
    return 0;
}

static RangeVal *rv_new(Oid rt)
{
    RangeVal *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->rngtype = rt;
    r->subtype = f_range_subtype(rt);
    return r;
}
static Datum d_range(Oid rt, RangeVal *r)
{
    Datum d; memset(&d, 0, sizeof d);
    d.type = rt; d.v.rng = r;
    return d;
}
static bool rng_discrete(Oid sub) { return sub == INT4OID || sub == INT8OID || sub == DATEOID; }

static RangeVal *rv_make(Session *s, Oid rt, Datum lo, Datum hi, const char *flags)
{
    RangeVal *r = rv_new(rt);
    bool li = true, ui = false;
    if (flags && strlen(flags) >= 2) {
        li = flags[0] == '[';
        ui = flags[1] == ']';
        if ((flags[0] != '[' && flags[0] != '(') || (flags[1] != ']' && flags[1] != ')'))
            ereport(s, "22P02", "invalid input syntax for type %s: \"%s\"", type_name(rt), flags);
    }
    r->lower_inf = lo.isnull; r->upper_inf = hi.isnull;
    r->lower_inc = lo.isnull ? false : li;
    r->upper_inc = hi.isnull ? false : ui;
    if (!lo.isnull) r->lower = coerce_to(s, lo, r->subtype, -1, 1);
    if (!hi.isnull) r->upper = coerce_to(s, hi, r->subtype, -1, 1);
    if (rng_discrete(r->subtype)) {
        if (!r->lower_inf && !r->lower_inc) { r->lower.v.i += 1; r->lower_inc = true; }
        if (!r->upper_inf && r->upper_inc) { r->upper.v.i += 1; r->upper_inc = false; }
    }
    if (!r->lower_inf && !r->upper_inf) {
        int c = d_cmp(s, r->lower, r->upper);
        if (c > 0)
            ereport(s, "22000", "range lower bound must be less than or equal to range upper bound");
        if (c == 0 && !(r->lower_inc && r->upper_inc)) r->empty = true;
    }
    return r;
}
static RangeVal *rv_empty(Oid rt) { RangeVal *r = rv_new(rt); r->empty = true; return r; }
static RangeVal *rv_copy2(const RangeVal *a)
{
    RangeVal *r = xmalloc(sizeof *r);
    *r = *a;
    return r;
}

static bool rng_contains_elem(Session *s, RangeVal *r, Datum e)
{
    if (r->empty || e.isnull) return false;
    if (!r->lower_inf) {
        int c = d_cmp(s, e, r->lower);
        if (c < 0 || (c == 0 && !r->lower_inc)) return false;
    }
    if (!r->upper_inf) {
        int c = d_cmp(s, e, r->upper);
        if (c > 0 || (c == 0 && !r->upper_inc)) return false;
    }
    return true;
}
static bool rng_contains(Session *s, RangeVal *a, RangeVal *b)
{
    if (b->empty) return true;
    if (a->empty) return false;
    return rb_cmp(s, rb_lower(a), rb_lower(b)) <= 0 &&
           rb_cmp(s, rb_upper(a), rb_upper(b)) >= 0;
}
static bool rng_overlaps(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty || b->empty) return false;
    return rb_cmp(s, rb_lower(a), rb_upper(b)) <= 0 &&
           rb_cmp(s, rb_lower(b), rb_upper(a)) <= 0;
}
static bool rng_before(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty || b->empty) return false;
    if (a->upper_inf || b->lower_inf) return false;
    int c = d_cmp(s, a->upper, b->lower);
    if (c < 0) return true;
    if (c == 0) return !(a->upper_inc && b->lower_inc);
    return false;
}
static bool rng_adjacent(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty || b->empty) return false;
    if (!a->upper_inf && !b->lower_inf && d_cmp(s, a->upper, b->lower) == 0 &&
        (a->upper_inc != b->lower_inc)) return true;
    if (!b->upper_inf && !a->lower_inf && d_cmp(s, b->upper, a->lower) == 0 &&
        (b->upper_inc != a->lower_inc)) return true;
    return false;
}
static RangeVal *rng_merge(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty) return rv_copy2(b);
    if (b->empty) return rv_copy2(a);
    RangeVal *r = rv_new(a->rngtype);
    RBound lo = rb_cmp(s, rb_lower(a), rb_lower(b)) <= 0 ? rb_lower(a) : rb_lower(b);
    RBound hi = rb_cmp(s, rb_upper(a), rb_upper(b)) >= 0 ? rb_upper(a) : rb_upper(b);
    r->lower_inf = lo.inf; r->lower_inc = lo.inc; r->lower = lo.val;
    r->upper_inf = hi.inf; r->upper_inc = hi.inc; r->upper = hi.val;
    return r;
}
static RangeVal *rng_union(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty) return rv_copy2(b);
    if (b->empty) return rv_copy2(a);
    if (!rng_overlaps(s, a, b) && !rng_adjacent(s, a, b))
        ereport(s, "22000", "result of range union would not be contiguous");
    return rng_merge(s, a, b);
}
static RangeVal *rng_intersect(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty || b->empty || !rng_overlaps(s, a, b)) return rv_empty(a->rngtype);
    RangeVal *r = rv_new(a->rngtype);
    RBound lo = rb_cmp(s, rb_lower(a), rb_lower(b)) >= 0 ? rb_lower(a) : rb_lower(b);
    RBound hi = rb_cmp(s, rb_upper(a), rb_upper(b)) <= 0 ? rb_upper(a) : rb_upper(b);
    r->lower_inf = lo.inf; r->lower_inc = lo.inc; r->lower = lo.val;
    r->upper_inf = hi.inf; r->upper_inc = hi.inc; r->upper = hi.val;
    return r;
}
static RangeVal *rng_minus(Session *s, RangeVal *a, RangeVal *b)
{
    if (a->empty || b->empty || !rng_overlaps(s, a, b)) return rv_copy2(a);
    bool lo_in = rb_cmp(s, rb_lower(b), rb_lower(a)) <= 0;
    bool hi_in = rb_cmp(s, rb_upper(b), rb_upper(a)) >= 0;
    if (lo_in && hi_in) return rv_empty(a->rngtype);
    if (!lo_in && !hi_in)
        ereport(s, "22000", "result of range difference would not be contiguous");
    RangeVal *r = rv_copy2(a);
    if (lo_in) {   /* b covers the low end: keep the part above b */
        RBound ub = rb_upper(b);
        r->lower_inf = ub.inf; r->lower = ub.val; r->lower_inc = !ub.inc;
        if (ub.inf) r->lower_inc = false;
    } else {       /* b covers the high end: keep the part below b */
        RBound lb = rb_lower(b);
        r->upper_inf = lb.inf; r->upper = lb.val; r->upper_inc = !lb.inc;
        if (lb.inf) r->upper_inc = false;
    }
    if (!r->lower_inf && !r->upper_inf) {
        int c = d_cmp(s, r->lower, r->upper);
        if (c > 0 || (c == 0 && !(r->lower_inc && r->upper_inc))) return rv_empty(a->rngtype);
    }
    return r;
}

/* ------------------------------------------------------------- multiranges */

static Datum d_mrange(Oid mt, MRangeVal *m)
{
    Datum d; memset(&d, 0, sizeof d);
    d.type = mt; d.v.mrng = m;
    return d;
}
static MRangeVal *mr_new(Oid mt)
{
    MRangeVal *m = xmalloc(sizeof *m);
    memset(m, 0, sizeof *m);
    m->mrngtype = mt;
    m->rngtype = f_range_of_mrange(mt);
    return m;
}
static void mr_push(MRangeVal *m, RangeVal *r)
{
    if (r->empty) return;
    m->ranges = xrealloc(m->ranges, (size_t)(m->n + 1) * sizeof(RangeVal *));
    m->ranges[m->n++] = r;
}
/* sort and coalesce, which is how PostgreSQL normalises a multirange */
static void mr_normalize(Session *s, MRangeVal *m)
{
    for (int i = 1; i < m->n; i++) {
        RangeVal *k = m->ranges[i];
        int j = i - 1;
        while (j >= 0 && rb_cmp(s, rb_lower(m->ranges[j]), rb_lower(k)) > 0) {
            m->ranges[j+1] = m->ranges[j]; j--;
        }
        m->ranges[j+1] = k;
    }
    int w = 0;
    for (int i = 0; i < m->n; i++) {
        if (w > 0 && (rng_overlaps(s, m->ranges[w-1], m->ranges[i]) ||
                      rng_adjacent(s, m->ranges[w-1], m->ranges[i])))
            m->ranges[w-1] = rng_merge(s, m->ranges[w-1], m->ranges[i]);
        else m->ranges[w++] = m->ranges[i];
    }
    m->n = w;
}

/* ===================================================== to_char / to_date */

static const char *mon_abbr[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
static const char *mon_full[12] = {"January","February","March","April","May","June",
                                   "July","August","September","October","November","December"};
static const char *day_abbr[7]  = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *day_full[7]  = {"Sunday","Monday","Tuesday","Wednesday",
                                   "Thursday","Friday","Saturday"};

static void upcase_into(Buf *b, const char *t) { for (; *t; t++) buf_addc(b, (char)toupper((unsigned char)*t)); }

/* day-of-week with 0 = Sunday */
static int dow_of(int32 day) { return (int)(((int64)day % 7 + 7 + 6) % 7); }

static char *fmt_datetime(const char *fmt, int y, int mo, int d, int h, int mi,
                          int sec, int fs, int32 dayno, bool have_date)
{
    Buf b; buf_init(&b);
    for (const char *p = fmt; *p; ) {
        if (*p == '"') { p++; while (*p && *p != '"') buf_addc(&b, *p++); if (*p) p++; continue; }
        if (!strncmp(p, "YYYY", 4)) { buf_printf(&b, "%04d", y < 0 ? -y : y); p += 4; continue; }
        if (!strncmp(p, "YYY", 3))  { buf_printf(&b, "%03d", (y < 0 ? -y : y) % 1000); p += 3; continue; }
        if (!strncmp(p, "YY", 2))   { buf_printf(&b, "%02d", (y < 0 ? -y : y) % 100); p += 2; continue; }
        if (!strncmp(p, "MONTH", 5)){ upcase_into(&b, mon_full[mo-1]);
                                      for (size_t k = strlen(mon_full[mo-1]); k < 9; k++) buf_addc(&b,' ');
                                      p += 5; continue; }
        if (!strncmp(p, "Month", 5)){ buf_adds(&b, mon_full[mo-1]);
                                      for (size_t k = strlen(mon_full[mo-1]); k < 9; k++) buf_addc(&b,' ');
                                      p += 5; continue; }
        if (!strncmp(p, "MON", 3))  { upcase_into(&b, mon_abbr[mo-1]); p += 3; continue; }
        if (!strncmp(p, "Mon", 3))  { buf_adds(&b, mon_abbr[mo-1]); p += 3; continue; }
        if (!strncmp(p, "MM", 2))   { buf_printf(&b, "%02d", mo); p += 2; continue; }
        if (!strncmp(p, "DDD", 3) && have_date) {
            int32 jan1 = dt_ymd_to_date(y, 1, 1);
            buf_printf(&b, "%03d", (int)(dayno - jan1) + 1); p += 3; continue;
        }
        if (!strncmp(p, "DAY", 3) && have_date) { upcase_into(&b, day_full[dow_of(dayno)]);
            for (size_t k = strlen(day_full[dow_of(dayno)]); k < 9; k++) buf_addc(&b,' ');
            p += 3; continue; }
        if (!strncmp(p, "Day", 3) && have_date) { buf_adds(&b, day_full[dow_of(dayno)]);
            for (size_t k = strlen(day_full[dow_of(dayno)]); k < 9; k++) buf_addc(&b,' ');
            p += 3; continue; }
        if (!strncmp(p, "DY", 2) && have_date) { upcase_into(&b, day_abbr[dow_of(dayno)]); p += 2; continue; }
        if (!strncmp(p, "Dy", 2) && have_date) { buf_adds(&b, day_abbr[dow_of(dayno)]); p += 2; continue; }
        if (!strncmp(p, "DD", 2))   { buf_printf(&b, "%02d", d); p += 2; continue; }
        if (!strncmp(p, "HH24", 4)) { buf_printf(&b, "%02d", h); p += 4; continue; }
        if (!strncmp(p, "HH12", 4) || !strncmp(p, "HH", 2)) {
            int hh = h % 12; if (!hh) hh = 12;
            buf_printf(&b, "%02d", hh);
            p += (p[2] == '1') ? 4 : 2;
            continue;
        }
        if (!strncmp(p, "MI", 2))   { buf_printf(&b, "%02d", mi); p += 2; continue; }
        if (!strncmp(p, "SSSS", 4)) { buf_printf(&b, "%d", h*3600+mi*60+sec); p += 4; continue; }
        if (!strncmp(p, "SS", 2))   { buf_printf(&b, "%02d", sec); p += 2; continue; }
        if (!strncmp(p, "MS", 2))   { buf_printf(&b, "%03d", fs / 1000); p += 2; continue; }
        if (!strncmp(p, "US", 2))   { buf_printf(&b, "%06d", fs); p += 2; continue; }
        if (!strncmp(p, "AM", 2) || !strncmp(p, "PM", 2)) { buf_adds(&b, h < 12 ? "AM" : "PM"); p += 2; continue; }
        if (!strncmp(p, "am", 2) || !strncmp(p, "pm", 2)) { buf_adds(&b, h < 12 ? "am" : "pm"); p += 2; continue; }
        if (!strncmp(p, "TZ", 2))   { buf_adds(&b, "UTC"); p += 2; continue; }
        if (!strncmp(p, "OF", 2))   { buf_adds(&b, "+00"); p += 2; continue; }
        if (*p == 'Q')              { buf_printf(&b, "%d", (mo - 1) / 3 + 1); p++; continue; }
        if (!strncmp(p, "WW", 2) && have_date) {
            int32 jan1 = dt_ymd_to_date(y, 1, 1);
            buf_printf(&b, "%02d", (int)((dayno - jan1) / 7) + 1); p += 2; continue;
        }
        if (*p == 'D' && have_date)  { buf_printf(&b, "%d", dow_of(dayno) + 1); p++; continue; }
        if (*p == 'B' && !strncmp(p, "BC", 2)) { buf_adds(&b, y < 0 ? "BC" : "AD"); p += 2; continue; }
        buf_addc(&b, *p); p++;
    }
    return buf_steal(&b);
}

/* to_date / to_timestamp: read the fields the format names */
/* to_timestamp()'s time zone patterns.  *tzoff comes back in seconds east of
 * UTC, and *hastz says whether one was seen. */
static bool parse_datetime_tz(Session *s, const char *str, const char *fmt,
                              int *y, int *mo, int *d, int *h, int *mi, int *sec, int *fs,
                              int *tzoff, bool *hastz);

static bool parse_datetime(const char *str, const char *fmt,
                           int *y, int *mo, int *d, int *h, int *mi, int *sec, int *fs)
{
    int tz = 0; bool hastz = false;
    return parse_datetime_tz(NULL, str, fmt, y, mo, d, h, mi, sec, fs, &tz, &hastz);
}

static bool parse_datetime_tz(Session *s, const char *str, const char *fmt,
                              int *y, int *mo, int *d, int *h, int *mi, int *sec, int *fs,
                              int *tzoff, bool *hastz)
{
    const char *sp = str;
    *y = 1; *mo = 1; *d = 1; *h = 0; *mi = 0; *sec = 0; *fs = 0;
    *tzoff = 0; *hastz = false;
    for (const char *p = fmt; *p; ) {
        int want = 0, *dst = NULL;
        if (!strncmp(p, "TZH", 3) || !strncmp(p, "OF", 2) ||
            (!strncmp(p, "TZ", 2) && p[2] != 'M')) {
            bool numeric = (!strncmp(p, "TZH", 3) || !strncmp(p, "OF", 2));
            p += numeric ? (p[1] == 'F' ? 2 : 3) : 2;
            while (*sp == ' ') sp++;
            if (!*sp) continue;
            if (*sp == '+' || *sp == '-' || (numeric && isdigit((unsigned char)*sp))) {
                int sign = (*sp == '-') ? -1 : 1;
                if (*sp == '+' || *sp == '-') sp++;
                int hh = 0, mm = 0, n = 0;
                while (isdigit((unsigned char)*sp) && n < 2) { hh = hh*10 + (*sp++ - '0'); n++; }
                if (*sp == ':') { sp++; n = 0;
                                  while (isdigit((unsigned char)*sp) && n < 2)
                                      { mm = mm*10 + (*sp++ - '0'); n++; } }
                if (hh > 15 || mm > 59) {
                    if (s) ereport(s, "22009", "time zone displacement out of range: \"%s\"", str);
                    return false;
                }
                *tzoff = sign * (hh * 3600 + mm * 60);
                *hastz = true;
                continue;
            }
            /* a zone abbreviation */
            const char *st = sp;
            while (isalpha((unsigned char)*sp) || *sp == '_' || *sp == '/') sp++;
            char *ab = xstrndup(st, (size_t)(sp - st));
            int aboff = 0;
            bool ok = tz_abbrev_offset(ab, &aboff);
            if (!ok) {
                if (s) { err_detail(s, "Time zone abbreviation is not recognized.");
                         ereport(s, "22007", "invalid value \"%s\" for \"TZ\"", ab); }
                free(ab);
                return false;
            }
            *tzoff = aboff; *hastz = true;
            free(ab);
            continue;
        }
        if (!strncmp(p, "TZM", 3)) { p += 3; while (*sp == ' ') sp++;
                                     while (isdigit((unsigned char)*sp)) sp++;
                                     continue; }
        if (!strncmp(p, "YYYY", 4)) { want = 4; dst = y;  p += 4; }
        else if (!strncmp(p, "YY", 2)) { want = 2; dst = y;  p += 2; }
        else if (!strncmp(p, "MON", 3) || !strncmp(p, "Mon", 3)) {
            while (*sp == ' ') sp++;
            for (int k = 0; k < 12; k++)
                if (!strncasecmp(sp, mon_abbr[k], 3)) { *mo = k + 1; break; }
            sp += 3; p += 3; continue;
        }
        else if (!strncmp(p, "MM", 2)) { want = 2; dst = mo; p += 2; }
        else if (!strncmp(p, "DD", 2)) { want = 2; dst = d;  p += 2; }
        else if (!strncmp(p, "HH24", 4)) { want = 2; dst = h;  p += 4; }
        else if (!strncmp(p, "HH12", 4)) { want = 2; dst = h;  p += 4; }
        else if (!strncmp(p, "HH", 2)) { want = 2; dst = h;  p += 2; }
        else if (!strncmp(p, "MI", 2)) { want = 2; dst = mi; p += 2; }
        else if (!strncmp(p, "SS", 2)) { want = 2; dst = sec;p += 2; }
        else if (!strncmp(p, "US", 2)) { want = 6; dst = fs; p += 6 > 2 ? 2 : 2; }
        else if (!strncmp(p, "MS", 2)) { want = 3; dst = fs; p += 2; }
        else { if (*sp && !isdigit((unsigned char)*sp)) sp++; else if (*sp == *p) sp++; p++; continue; }
        while (*sp == ' ') sp++;
        int v = 0, got = 0;
        bool neg = false;
        if (*sp == '-') { neg = true; sp++; }
        while (*sp && isdigit((unsigned char)*sp) && got < want) { v = v * 10 + (*sp - '0'); sp++; got++; }
        if (!got) return false;
        *dst = neg ? -v : v;
    }
    if (*y < 100 && *y >= 0) *y += (*y < 70) ? 2000 : 1900;
    return true;
}

/* ============================================================ pg_size_pretty */
static char *size_pretty(int64 size)
{
    static const char *units[] = {"bytes", "kB", "MB", "GB", "TB",
#if PGVC_VN >= 150
                                  "PB",
#endif
                                  NULL};
    Buf b; buf_init(&b);
    int64 a = size < 0 ? -size : size;
    if (a < 10L * 1024) { buf_printf(&b, "%lld bytes", (long long)size); return buf_steal(&b); }
    for (int u = 1; units[u]; u++) {
        int bits = 10 * u;
        int64 half = 1LL << (bits - 1);
        int64 v = (size >= 0 ? (size + half) : (size - half)) >> bits;
        if (size < 0) v = -(((-size) + half) >> bits);
        int64 av = v < 0 ? -v : v;
        if (!units[u+1] || av < 10L * 1024) {
            buf_printf(&b, "%lld %s", (long long)v, units[u]);
            return buf_steal(&b);
        }
    }
    buf_printf(&b, "%lld bytes", (long long)size);
    return buf_steal(&b);
}

/* ============================================================ misc helpers */

static uint32 rnd_state_lo = 0, rnd_state_hi = 0;
static bool rnd_seeded = false;
static double pg_random(void)
{
    if (!rnd_seeded) {
        rnd_state_lo = (uint32)(clock_us() & 0xffffffffu) | 1u;
        rnd_state_hi = (uint32)getpid() * 2654435761u + 1u;
        rnd_seeded = true;
    }
    /* xorshift128-ish; only distributional quality matters here */
    uint64 x = ((uint64)rnd_state_hi << 32) | rnd_state_lo;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    rnd_state_lo = (uint32)(x & 0xffffffffu);
    rnd_state_hi = (uint32)(x >> 32);
    return (double)(x >> 11) / 9007199254740992.0;
}

static char *hex_lower(const unsigned char *p, size_t n)
{
    static const char *hx = "0123456789abcdef";
    char *o = xmalloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) { o[i*2] = hx[p[i] >> 4]; o[i*2+1] = hx[p[i] & 15]; }
    o[n*2] = 0;
    return o;
}
static Datum make_uuid_datum(const unsigned char b[16])
{
    char *h = hex_lower(b, 16);
    char out[37];
    snprintf(out, sizeof out, "%.8s-%.4s-%.4s-%.4s-%.12s", h, h+8, h+12, h+16, h+20);
    free(h);
    return d_text(UUIDOID, out);
}
static void rand_bytes(unsigned char *p, int n)
{
    for (int i = 0; i < n; i++) p[i] = (unsigned char)(pg_random() * 256.0);
}

/* SQL SIMILAR TO -> POSIX ERE */
static char *similar_to_posix(const char *p, int32 n, int esc)
{
    Buf b; buf_init(&b);
    buf_adds(&b, "^(?:");
    for (int32 i = 0; i < n; i++) {
        char c = p[i];
        if (esc >= 0 && c == (char)esc && i + 1 < n) {
            i++;
            if (strchr(".^$\\[]()|*+?{}", p[i])) buf_addc(&b, '\\');
            buf_addc(&b, p[i]);
            continue;
        }
        switch (c) {
        case '%': buf_adds(&b, ".*"); break;
        case '_': buf_addc(&b, '.'); break;
        case '.': case '^': case '$': case '\\': buf_addc(&b, '\\'); buf_addc(&b, c); break;
        default: buf_addc(&b, c);
        }
    }
    buf_adds(&b, ")$");
    char *t = buf_steal(&b);
    /* our re_to_posix strips the (?: form, which POSIX lacks */
    return t;
}

static bool regex_match(Session *s, Datum str, Datum pat, const char *flags)
{
    char *o1, *o2;
    int32 n1, n2;
    const char *t = as_bytes(s, str, &n1, &o1);
    const char *pp = as_bytes(s, pat, &n2, &o2);
    char *ts = xstrndup(t, (size_t)n1), *ps = xstrndup(pp, (size_t)n2);
    PgRegex r; memset(&r, 0, sizeof r);
    re_compile(s, &r, ps, (int32)strlen(ps), flags);
    regmatch_t m[1];
    bool ok = pgre_search(&r, ts, 0, m, 1);
    re_free(&r);
    free(ts); free(ps); free(o1); free(o2);
    return ok;
}

/* ======================================================= the dispatch table */

#define EQ(x)  (!strcmp(name, (x)))
#define EQI(x) ci_eq(name, (x))

static Datum concat_op(Session *s, Datum a, Datum b, bool *found);
static Datum jsonb_op(Session *s, const char *name, Datum *args, int nargs, bool *found,
                      bool *handled);
static Datum range_op(Session *s, const char *name, Datum *args, int nargs, bool *found,
                      bool *handled);
static Datum array_op(Session *s, const char *name, Datum *args, int nargs, bool *found,
                      bool *handled);
static Datum geo_op(Session *s, const char *name, Datum *args, int nargs, bool *handled);

/* Surface a to_date()/to_timestamp() failure the way PostgreSQL does. */
static void to_datetime_error(Session *s, const char *err, const char *str) __attribute__((noreturn));
static void to_datetime_error(Session *s, const char *err, const char *str)
{
    if (err && !strncmp(err, "hour ", 5)) {
        err_hint(s, "Use the 24-hour clock, or give an hour between 1 and 12.");
        ereport(s, "22008", "%s", err);
    }
    if (err && strstr(err, "out of range")) ereport(s, "22008", "%s", err);
    if (err && *err) ereport(s, "22007", "%s", err);
    ereport(s, "22007", "invalid value for \"%s\"", str);
}

/* Does a to_date()/to_timestamp() format name a time zone field?  dtime.c's
 * DCH scanner models only the numeric patterns, so the TZ / TZH / OF fields
 * are read (and validated) by parse_datetime_tz() alongside it. */
static bool fmt_has_tz(const char *fmt)
{
    for (const char *p = fmt; *p; p++) {
        if ((p[0] == 'T' || p[0] == 't') && (p[1] == 'Z' || p[1] == 'z')) return true;
        if ((p[0] == 'O' || p[0] == 'o') && (p[1] == 'F' || p[1] == 'f')) return true;
    }
    return false;
}

/* The offset a TZ / TZH / OF field carries, in seconds east of UTC.  Raises
 * PostgreSQL's error for an unknown abbreviation or an impossible
 * displacement; leaves *off at zero when the format names no zone. */
static int fmt_tz_offset(Session *s, const char *str, const char *fmt)
{
    int y, mo, d, h, mi, sec, fs, tz = 0;
    bool hastz = false;
    if (!parse_datetime_tz(s, str, fmt, &y, &mo, &d, &h, &mi, &sec, &fs, &tz, &hastz))
        return 0;
    return hastz ? tz : 0;
}

#if PGVC_VN >= 160
/* Resolve the type-name argument of pg_input_is_valid / pg_input_error_info. */
void pgvc_input_type(Session *s, Datum d, Oid *ty, int32 *tm)
{
    if (d.isnull) ereport(s, "22004", "type name must not be null");
    char *t = as_cstr(s, d);
    Oid o = 0; int32 m = -1;
    if (!parse_type_from_text(s, t, &o, &m, NULL) || !o)
        ereport(s, "42704", "type \"%s\" does not exist", t);
    free(t);
    *ty = o; *tm = m;
}

/* Run a type input conversion, catching whatever error it raises. */
bool pgvc_try_input(Session *s, Oid ty, int32 tm, const char *txt, ErrState *cap)
{
    jmp_buf save;
    ErrState prev = s->err;
    volatile bool ok = false;
    memcpy(save, s->jmp, sizeof(jmp_buf));
    memset(&s->err, 0, sizeof s->err);
    if (setjmp(s->jmp) == 0) {
        Datum d = in_func(s, ty, tm, txt, (int32)strlen(txt));
        (void)d;
        ok = true;
    } else {
        if (cap) *cap = s->err;
        ok = false;
    }
    memcpy(s->jmp, save, sizeof(jmp_buf));
    s->err = prev;
    return ok;
}
#endif

Datum call_builtin(Session *s, const char *name, Datum *args, int nargs, bool *found)
{
    *found = true;

#if PGVC_VN >= 110
    /* tsvector @@ tsquery, either way round */
    if (nargs == 2 && EQ("@@") &&
        (args[0].type == TSVECTOROID || args[0].type == TSQUERYOID) &&
        (args[1].type == TSVECTOROID || args[1].type == TSQUERYOID)) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        Datum vd = (args[0].type == TSQUERYOID) ? args[1] : args[0];
        Datum qd = (args[0].type == TSQUERYOID) ? args[0] : args[1];
        char *vt = as_cstr(s, vd), *qt = as_cstr(s, qd);
        bool ok = ts_match(vt, qt);
        free(vt); free(qt);
        return d_bool(ok);
    }
#endif

    /* ------------------------------------------------------------ operators */
    if (nargs == 2 &&
        (EQ("+") || EQ("-") || EQ("*") || EQ("/") || EQ("%") || EQ("^"))) {
        bool handled = false;
        /* jsonb and range overloads of - + * come first */
        if (EQ("-") || EQ("+") || EQ("*")) {
            Datum r = jsonb_op(s, name, args, nargs, found, &handled);
            if (handled) return r;
            r = range_op(s, name, args, nargs, found, &handled);
            if (handled) return r;
        }
        Datum r = dt_binop(s, name, args[0], args[1], found, &handled);
        if (handled) return r;
        if (args[0].type == PGLSNOID || args[1].type == PGLSNOID) {
            /* pg_lsn - pg_lsn is numeric; pg_lsn +/- numeric is pg_lsn */
            if (EQ("-") && args[0].type == PGLSNOID && args[1].type == PGLSNOID) {
                if (args[0].isnull || args[1].isnull) return d_null(NUMERICOID);
                return d_num(num_from_int64(args[0].v.i - args[1].v.i));
            }
            if ((EQ("-") || EQ("+")) && args[0].type == PGLSNOID) {
                if (args[0].isnull || args[1].isnull) return d_null(PGLSNOID);
                int64 off = coerce_to(s, args[1], INT8OID, -1, 1).v.i;
                return d_int(PGLSNOID, EQ("-") ? args[0].v.i - off : args[0].v.i + off);
            }
            no_operator(s, name, args[0].type, args[1].type);
        }
        Oid la = args[0].type, lb = args[1].type;
        if (!f_is_numfam(la == UNKNOWNOID ? INT4OID : la) ||
            !f_is_numfam(lb == UNKNOWNOID ? INT4OID : lb))
            no_operator(s, name, la, lb);
        return num_binop(s, name, args[0], args[1], found);
    }
    if (nargs == 1 && (EQ("-") || EQ("+"))) {
        Datum a = args[0];
        if (a.isnull) return a;
        if (EQ("+")) return a;
        switch (a.type) {
        case INT2OID: case INT4OID: case INT8OID:
            if (a.v.i == int_lo(a.type)) int_range_err(s, a.type);
            return d_int(a.type, -a.v.i);
        case FLOAT4OID: case FLOAT8OID: return d_float(a.type, -a.v.f);
        case NUMERICOID: return d_num(num_neg(a.v.num));
        case INTERVALOID: {
            PgInterval v = a.v.iv;
            v.month = -v.month; v.day = -v.day; v.usec = -v.usec;
            return d_interval(v);
        }
        case UNKNOWNOID: {
            Datum n = coerce_to(s, a, NUMERICOID, -1, 1);
            return d_num(num_neg(n.v.num));
        }
        default: break;
        }
        no_operator(s, name, 0, a.type);
    }
    if (nargs == 2 && (EQ("=") || EQ("<>") || EQ("!=") || EQ("<") || EQ("<=") ||
                       EQ(">") || EQ(">="))) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        Datum a = args[0], b = args[1];
        if (a.type == UNKNOWNOID && b.type != UNKNOWNOID) a = coerce_to(s, a, b.type, -1, 1);
        else if (b.type == UNKNOWNOID && a.type != UNKNOWNOID) b = coerce_to(s, b, a.type, -1, 1);
        int c = d_cmp(s, a, b);
        if (EQ("=")) return d_bool(c == 0);
        if (EQ("<>") || EQ("!=")) return d_bool(c != 0);
        if (EQ("<")) return d_bool(c < 0);
        if (EQ("<=")) return d_bool(c <= 0);
        if (EQ(">")) return d_bool(c > 0);
        return d_bool(c >= 0);
    }
    if (nargs == 2 && EQ("||")) return concat_op(s, args[0], args[1], found);

    /* json/range/array operator groups */
    if (nargs == 2 || nargs == 1) {
        bool handled = false;
        Datum r = jsonb_op(s, name, args, nargs, found, &handled);
        if (handled) return r;
        r = range_op(s, name, args, nargs, found, &handled);
        if (handled) return r;
        r = array_op(s, name, args, nargs, found, &handled);
        if (handled) return r;
        r = geo_op(s, name, args, nargs, &handled);
        if (handled) return r;
    }

    /* ---------------------------------------------------- pattern operators */
    if ((nargs == 2 || nargs == 3) &&
        (EQ("~~") || EQ("!~~") || EQ("~~*") || EQ("!~~*") ||
         EQI("like") || EQI("notlike") || EQI("ilike") || EQI("notilike") ||
         EQI("not_like") || EQI("not_ilike"))) {
        bool neg = (name[0] == '!') || EQI("notlike") || EQI("notilike") ||
                   EQI("not_like") || EQI("not_ilike");
        bool ci  = (strchr(name, '*') != NULL) || EQI("ilike") || EQI("notilike") ||
                   EQI("not_ilike");
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        int esc = '\\';
        if (nargs == 3) {
            if (args[2].isnull) esc = '\\';
            else {
                char *e = as_cstr(s, args[2]);
                esc = e[0] ? (unsigned char)e[0] : -1;
                free(e);
            }
        }
        char *o1, *o2; int32 n1, n2;
        const char *t = as_bytes(s, args[0], &n1, &o1);
        const char *p = as_bytes(s, args[1], &n2, &o2);
        int m = like_match(t, n1, p, n2, esc, ci, s);
        free(o1); free(o2);
        return d_bool(neg ? !m : !!m);
    }
    if ((nargs == 2 || nargs == 3) && (EQI("similar_to") || EQI("similar") ||
                                       EQ("~~~") || EQI("notsimilar"))) {
        bool neg = EQI("notsimilar");
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        int esc = -1;
        if (nargs == 3 && !args[2].isnull) {
            char *e = as_cstr(s, args[2]);
            esc = e[0] ? (unsigned char)e[0] : -1;
            free(e);
        }
        char *o2; int32 n2;
        const char *p = as_bytes(s, args[1], &n2, &o2);
        char *rx = similar_to_posix(p, n2, esc);
        Datum pd = d_text(TEXTOID, rx);
        bool m = regex_match(s, args[0], pd, NULL);
        free(rx); free(o2);
        return d_bool(neg ? !m : m);
    }
    if (nargs == 2 && (EQ("~") || EQ("~*") || EQ("!~") || EQ("!~*")) &&
        !(args[0].type == BOXOID || args[0].type == CIRCLEOID ||
          args[0].type == POINTOID || args[0].type == POLYGONOID ||
          args[0].type == PATHOID || args[0].type == LSEGOID)) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        bool ci = (name[strlen(name)-1] == '*');
        bool neg = (name[0] == '!');
        bool m = regex_match(s, args[0], args[1], ci ? "i" : NULL);
        return d_bool(neg ? !m : m);
    }

    /* ----------------------------------------------------- string functions */
    if (nargs == 1 && (EQI("upper") || EQI("lower") || EQI("initcap")) &&
        !f_is_range(args[0].type) && !f_is_mrange(args[0].type)) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        Datum r = text_case(TEXTOID, p, n, EQI("upper") ? 0 : EQI("lower") ? 1 : 2);
        free(o);
        return r;
    }
    if (nargs == 1 && (EQI("length") || EQI("char_length") || EQI("character_length"))) {
        if (args[0].isnull) return d_null(INT4OID);
        if (args[0].type == BYTEAOID) return d_int(INT4OID, args[0].v.s.len);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        int32 k = n;
        if (args[0].type == BPCHAROID) { while (k > 0 && p[k-1] == ' ') k--; }
        int r = u8_len(p, k);
        free(o);
        return d_int(INT4OID, r);
    }
    if (nargs == 1 && EQI("octet_length")) {
        if (args[0].isnull) return d_null(INT4OID);
        char *o; int32 n;
        (void)as_bytes(s, args[0], &n, &o);
        free(o);
        return d_int(INT4OID, n);
    }
    if (nargs == 1 && EQI("bit_length")) {
        if (args[0].isnull) return d_null(INT4OID);
        char *o; int32 n;
        (void)as_bytes(s, args[0], &n, &o);
        free(o);
        return d_int(INT4OID, n * 8);
    }
    if ((nargs == 2 || nargs == 3) && (EQI("substr") || EQI("substring"))) {
        if (args[0].isnull) return d_null(TEXTOID);
        /* substring(text from pattern [for escape]) */
        if (f_is_str(args[1].type) && !f_is_numfam(args[1].type)) {
            if (args[1].isnull) return d_null(TEXTOID);
            char *o1, *o2; int32 n1, n2;
            const char *t = as_bytes(s, args[0], &n1, &o1);
            const char *pt = as_bytes(s, args[1], &n2, &o2);
            char *pat;
            if (nargs == 3 && !args[2].isnull) {
                char *e = as_cstr(s, args[2]);
                pat = similar_to_posix(pt, n2, e[0] ? (unsigned char)e[0] : -1);
                free(e);
            } else pat = xstrndup(pt, (size_t)n2);
            char *ts = xstrndup(t, (size_t)n1);
            PgRegex r; memset(&r, 0, sizeof r);
            re_compile(s, &r, pat, (int32)strlen(pat), NULL);
            regmatch_t m[10];
            Datum res = d_null(TEXTOID);
            if (pgre_search(&r, ts, 0, m, 10)) {
                int g = (re_ngroups(&r) >= 1 && m[1].rm_so >= 0) ? 1 : 0;
                res = d_textn(TEXTOID, ts + m[g].rm_so, (int32)(m[g].rm_eo - m[g].rm_so));
            }
            re_free(&r);
            free(ts); free(pat); free(o1); free(o2);
            return res;
        }
        if (args[1].isnull || (nargs == 3 && args[2].isnull)) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        Datum r = text_sub(TEXTOID, s, p, n, as_i8(s, args[1]),
                           nargs == 3, nargs == 3 ? as_i8(s, args[2]) : 0);
        free(o);
        return r;
    }
    if (nargs == 2 && (EQI("left") || EQI("right"))) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        int total = u8_len(p, n);
        int64 k = as_i8(s, args[1]);
        Datum r;
        if (EQI("left")) {
            int64 cnt = k < 0 ? (total + k) : k;
            if (cnt < 0) cnt = 0;
            if (cnt > total) cnt = total;
            r = d_textn(TEXTOID, p, u8_off(p, n, (int)cnt));
        } else {
            int64 cnt = k < 0 ? (total + k) : k;
            if (cnt < 0) cnt = 0;
            if (cnt > total) cnt = total;
            int32 off = u8_off(p, n, (int)(total - cnt));
            r = d_textn(TEXTOID, p + off, n - off);
        }
        free(o);
        return r;
    }
    if ((nargs == 2 || nargs == 3) && (EQI("lpad") || EQI("rpad"))) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *o, *of = NULL; int32 n, nf = 1;
        const char *p = as_bytes(s, args[0], &n, &o);
        const char *fill = " ";
        if (nargs == 3) {
            if (args[2].isnull) return d_null(TEXTOID);
            fill = as_bytes(s, args[2], &nf, &of);
        }
        Datum r = text_pad(s, p, n, as_i8(s, args[1]), fill, nf, EQI("lpad"));
        free(o); free(of);
        return r;
    }
    if ((nargs == 1 || nargs == 2) && (EQI("ltrim") || EQI("rtrim") || EQI("btrim") || EQI("trim"))) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *o, *os = NULL; int32 n, ns = 1;
        const char *p = as_bytes(s, args[0], &n, &o);
        const char *set = " ";
        if (nargs == 2) {
            if (args[1].isnull) return d_null(TEXTOID);
            set = as_bytes(s, args[1], &ns, &os);
        }
        int mode = EQI("ltrim") ? 1 : EQI("rtrim") ? 2 : 0;
        Datum r = text_trim(TEXTOID, p, n, set, ns, mode);
        free(o); free(os);
        return r;
    }
    if (nargs == 2 && EQI("repeat")) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        int64 k = as_i8(s, args[1]);
        Buf b; buf_init(&b);
        for (int64 i = 0; i < k; i++) {
            if (b.len > 1000000000LL) ereport(s, "54000", "requested length too large");
            buf_add(&b, p, (size_t)n);
        }
        free(o);
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs == 3 && EQI("replace")) {
        if (args[0].isnull || args[1].isnull || args[2].isnull) return d_null(TEXTOID);
        char *o1,*o2,*o3; int32 n1,n2,n3;
        const char *t = as_bytes(s, args[0], &n1, &o1);
        const char *f = as_bytes(s, args[1], &n2, &o2);
        const char *r = as_bytes(s, args[2], &n3, &o3);
        Buf b; buf_init(&b);
        if (n2 == 0) buf_add(&b, t, (size_t)n1);
        else {
            int32 i = 0;
            while (i < n1) {
                int32 k = bytes_find(t, n1, f, n2, i);
                if (k < 0) { buf_add(&b, t + i, (size_t)(n1 - i)); break; }
                buf_add(&b, t + i, (size_t)(k - i));
                buf_add(&b, r, (size_t)n3);
                i = k + n2;
            }
        }
        free(o1); free(o2); free(o3);
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs == 3 && EQI("translate")) {
        if (args[0].isnull || args[1].isnull || args[2].isnull) return d_null(TEXTOID);
        char *o1,*o2,*o3; int32 n1,n2,n3;
        const char *t = as_bytes(s, args[0], &n1, &o1);
        const char *f = as_bytes(s, args[1], &n2, &o2);
        const char *r = as_bytes(s, args[2], &n3, &o3);
        Buf b; buf_init(&b);
        for (int32 i = 0; i < n1; ) {
            int cl = u8_charlen((unsigned char)t[i]);
            int idx = -1, k = 0;
            for (int32 j = 0; j < n2; k++) {
                int fl = u8_charlen((unsigned char)f[j]);
                if (fl == cl && !memcmp(f + j, t + i, (size_t)cl)) { idx = k; break; }
                j += fl;
            }
            if (idx < 0) buf_add(&b, t + i, (size_t)cl);
            else {
                int32 j = 0; int c = 0;
                while (j < n3 && c < idx) { j += u8_charlen((unsigned char)r[j]); c++; }
                if (j < n3) buf_add(&b, r + j, (size_t)u8_charlen((unsigned char)r[j]));
            }
            i += cl;
        }
        free(o1); free(o2); free(o3);
        return d_textbuf(TEXTOID, &b);
    }
#if PGVC_VN >= 180
    if (nargs == 1 && EQI("reverse") && args[0].type == BYTEAOID) {
        if (args[0].isnull) return d_null(BYTEAOID);
        Buf b; buf_init(&b);
        for (int32 i = args[0].v.s.len; i > 0; i--) buf_addc(&b, args[0].v.s.p[i-1]);
        Datum r = d_textn(BYTEAOID, b.data ? b.data : "", (int32)b.len);
        buf_free(&b);
        return r;
    }
    if (nargs == 1 && EQI("bytea") &&
        (args[0].type == INT2OID || args[0].type == INT4OID || args[0].type == INT8OID)) {
        if (args[0].isnull) return d_null(BYTEAOID);
        int w = args[0].type == INT2OID ? 2 : args[0].type == INT4OID ? 4 : 8;
        uint64 v = (uint64)args[0].v.i;
        char buf[8];
        for (int i = 0; i < w; i++) buf[i] = (char)((v >> ((w - 1 - i) * 8)) & 0xff);
        return d_textn(BYTEAOID, buf, w);
    }
    if (nargs == 1 && (EQI("crc32") || EQI("crc32c"))) {
        if (args[0].isnull) return d_null(INT8OID);
        bool c32c = EQI("crc32c");
        uint32 poly = c32c ? 0x82F63B78u : 0xEDB88320u;
        uint32 crc = 0xFFFFFFFFu;
        const unsigned char *p2 = (const unsigned char *)args[0].v.s.p;
        for (int32 i = 0; i < args[0].v.s.len; i++) {
            crc ^= p2[i];
            for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (poly & (uint32)(-(int32)(crc & 1)));
        }
        return d_int(INT8OID, (int64)(uint32)(crc ^ 0xFFFFFFFFu));
    }
    if (nargs == 1 && EQI("casefold")) {
        if (args[0].isnull) return d_null(TEXTOID);
        bool f2 = false;
        return call_builtin(s, "lower", args, 1, &f2);
    }
    if (nargs == 1 && EQI("array_reverse")) {
        if (args[0].isnull) return args[0];
        if (!type_is_array(args[0].type)) { *found = false; return d_null(TEXTOID); }
        ArrayVal *a = args[0].v.arr;
        int nd = a ? a->ndim : 0;
        int outer = nd > 0 ? a->dims[0] : 0;
        int stride = 1;
        for (int i = 1; i < nd; i++) stride *= a->dims[i];
        ArrayVal *r = arr_new(arr_elemtype(args[0]));
        for (int i = outer - 1; i >= 0; i--)
            for (int q = 0; q < stride; q++) arr_push(r, a->elems[i * stride + q]);
        if (nd > 1) {
            r->ndim = nd;
            for (int i = 0; i < nd; i++) { r->dims[i] = a->dims[i]; r->lbound[i] = a->lbound[i]; }
        }
        return d_arr(args[0].type, r);
    }
    if ((nargs >= 1 && nargs <= 3) && EQI("array_sort")) {
        if (args[0].isnull) return args[0];
        if (!type_is_array(args[0].type)) { *found = false; return d_null(TEXTOID); }
        if (nargs > 1 && args[1].isnull) return d_null(args[0].type);
        if (nargs > 2 && args[2].isnull) return d_null(args[0].type);
        bool desc = (nargs > 1) && as_bool(s, args[1]);
        bool nullsfirst = (nargs > 2) ? as_bool(s, args[2]) : desc;
        ArrayVal *a = args[0].v.arr;
        int nd = a ? a->ndim : 0;
        int outer = nd > 0 ? a->dims[0] : 0;
        int stride = 1;
        for (int i = 1; i < nd; i++) stride *= a->dims[i];
        int *ord = xmalloc((size_t)(outer ? outer : 1) * sizeof(int));
        for (int i = 0; i < outer; i++) ord[i] = i;
        for (int i = 1; i < outer; i++) {
            int cur = ord[i], j = i - 1;
            while (j >= 0) {
                Datum x = a->elems[ord[j] * stride], y = a->elems[cur * stride];
                int cmp;
                if (x.isnull || y.isnull) {
                    if (x.isnull && y.isnull) cmp = 0;
                    else if (x.isnull) cmp = nullsfirst ? -1 : 1;
                    else cmp = nullsfirst ? 1 : -1;
                } else { cmp = d_cmp(s, x, y); if (desc) cmp = -cmp; }
                if (cmp <= 0) break;
                ord[j+1] = ord[j];
                j--;
            }
            ord[j+1] = cur;
        }
        ArrayVal *r = arr_new(arr_elemtype(args[0]));
        for (int i = 0; i < outer; i++)
            for (int q = 0; q < stride; q++) arr_push(r, a->elems[ord[i] * stride + q]);
        free(ord);
        if (nd > 1) {
            r->ndim = nd;
            for (int i = 0; i < nd; i++) { r->dims[i] = a->dims[i]; r->lbound[i] = a->lbound[i]; }
        }
        return d_arr(args[0].type, r);
    }
#endif
    if (nargs == 1 && EQI("reverse")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        Buf b; buf_init(&b);
        for (int32 i = n; i > 0; ) {
            int32 k = i - 1;
            while (k > 0 && ((unsigned char)p[k] & 0xC0) == 0x80) k--;
            buf_add(&b, p + k, (size_t)(i - k));
            i = k;
        }
        free(o);
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs == 2 && (EQI("strpos") || EQI("position"))) {
        if (args[0].isnull || args[1].isnull) return d_null(INT4OID);
        char *o1,*o2; int32 n1,n2;
        const char *h = as_bytes(s, args[0], &n1, &o1);
        const char *nd = as_bytes(s, args[1], &n2, &o2);
        int32 k = bytes_find(h, n1, nd, n2, 0);
        int r = (k < 0) ? 0 : u8_len(h, k) + 1;
        free(o1); free(o2);
        return d_int(INT4OID, r);
    }
    if (nargs == 3 && EQI("split_part")) {
        if (args[0].isnull || args[1].isnull || args[2].isnull) return d_null(TEXTOID);
        char *o1,*o2; int32 n1,n2;
        const char *t = as_bytes(s, args[0], &n1, &o1);
        const char *dl = as_bytes(s, args[1], &n2, &o2);
        int64 fld = as_i8(s, args[2]);
#if PGVC_VN >= 140
        if (fld == 0) ereport(s, "22023", "field position must not be zero");
#else
        if (fld <= 0) ereport(s, "22023", "field position must be greater than zero");
#endif
        /* collect the pieces, then index from either end */
        int32 *starts = NULL, *lens = NULL; int np = 0;
        int32 i = 0;
        if (n2 == 0) {
            starts = xmalloc(sizeof(int32)); lens = xmalloc(sizeof(int32));
            starts[0] = 0; lens[0] = n1; np = 1;
        } else {
            while (1) {
                int32 k = bytes_find(t, n1, dl, n2, i);
                starts = xrealloc(starts, (size_t)(np+1)*sizeof(int32));
                lens   = xrealloc(lens,   (size_t)(np+1)*sizeof(int32));
                starts[np] = i;
                lens[np] = (k < 0 ? n1 : k) - i;
                np++;
                if (k < 0) break;
                i = k + n2;
            }
        }
        int idx = (fld > 0) ? (int)fld - 1 : np + (int)fld;
        Datum r = (idx < 0 || idx >= np) ? d_text(TEXTOID, "")
                                         : d_textn(TEXTOID, t + starts[idx], lens[idx]);
        free(starts); free(lens); free(o1); free(o2);
        return r;
    }
    if (EQI("concat")) {
        Buf b; buf_init(&b);
        for (int i = 0; i < nargs; i++) {
            if (args[i].isnull) continue;
            char *t = as_cstr(s, args[i]);
            buf_adds(&b, t);
            free(t);
        }
        return d_textbuf(TEXTOID, &b);
    }
    if (EQI("concat_ws")) {
        if (nargs == 0) NOTFOUND();
        if (args[0].isnull) return d_null(TEXTOID);
        char *sep = as_cstr(s, args[0]);
        Buf b; buf_init(&b);
        bool first = true;
        for (int i = 1; i < nargs; i++) {
            if (args[i].isnull) continue;
            if (!first) buf_adds(&b, sep);
            char *t = as_cstr(s, args[i]);
            buf_adds(&b, t);
            free(t);
            first = false;
        }
        free(sep);
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs >= 1 && EQI("format")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *f = as_cstr(s, args[0]);
        Buf b; buf_init(&b);
        int next = 1;
        for (char *p = f; *p; ) {
            if (*p != '%') { buf_addc(&b, *p++); continue; }
            p++;
            if (*p == '%') { buf_addc(&b, '%'); p++; continue; }
            int idx = -1;
            if (isdigit((unsigned char)*p)) {
                char *e;
                long v = strtol(p, &e, 10);
                if (*e == '$') { idx = (int)v; p = e + 1; }
            }
            while (*p == '-' || *p == ' ' || isdigit((unsigned char)*p)) p++;
            char conv = *p ? *p++ : 's';
            int ai = (idx > 0) ? idx : next++;
            if (ai >= nargs) ereport(s, "22023", "too few arguments for format()");
            Datum d = args[ai];
            if (conv == 's') {
                if (!d.isnull) { char *t = as_cstr(s, d); buf_adds(&b, t); free(t); }
            } else if (conv == 'I') {
                if (d.isnull) ereport(s, "22004", "null values cannot be formatted as an SQL identifier");
                char *t = as_cstr(s, d);
                char *q = quote_ident_str(t, (int32)strlen(t));
                buf_adds(&b, q); free(q); free(t);
            } else if (conv == 'L') {
                if (d.isnull) buf_adds(&b, "NULL");
                else { char *t = as_cstr(s, d);
                       char *q = quote_literal_str(t, (int32)strlen(t));
                       buf_adds(&b, q); free(q); free(t); }
            } else
                ereport(s, "22023", "unrecognized format() type specifier \"%c\"", conv);
        }
        free(f);
        return d_textbuf(TEXTOID, &b);
    }
#if PGVC_VN >= 110
    if (nargs == 2 && EQI("starts_with")) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        char *o1,*o2; int32 n1,n2;
        const char *a = as_bytes(s, args[0], &n1, &o1);
        const char *b = as_bytes(s, args[1], &n2, &o2);
        bool r = (n2 <= n1) && !memcmp(a, b, (size_t)n2);
        free(o1); free(o2);
        return d_bool(r);
    }
#endif
    if (nargs == 1 && EQI("chr")) {
        if (args[0].isnull) return d_null(TEXTOID);
        int64 cp = as_i8(s, args[0]);
        if (cp == 0) ereport(s, "54000", "null character not permitted");
        if (cp < 0 || cp > 0x10FFFF)
            ereport(s, "22023", "requested character too large for encoding: %lld", (long long)cp);
        char tmp[5]; int l = u8_encode((int)cp, tmp);
        return d_textn(TEXTOID, tmp, l);
    }
    if (nargs == 1 && EQI("ascii")) {
        if (args[0].isnull) return d_null(INT4OID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        int r = 0;
        if (n > 0) { int adv; r = u8_decode(p, &adv); }
        free(o);
        return d_int(INT4OID, r);
    }
    if (nargs == 1 && EQI("md5")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        char hex[33];
        md5_hex((const unsigned char *)p, (size_t)n, hex);
        free(o);
        return d_text(TEXTOID, hex);
    }
    if (nargs == 2 && EQI("encode")) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        char *fmtn = as_cstr(s, args[1]);
        Datum r;
        if (ci_eq(fmtn, "hex")) {
            char *h = hex_lower((const unsigned char *)p, (size_t)n);
            r = d_text(TEXTOID, h); free(h);
        } else if (ci_eq(fmtn, "base64")) {
            char *b = xmalloc((size_t)n * 2 + 8);
            size_t bl = b64_encode((const unsigned char *)p, (size_t)n, b);
            r = d_textn(TEXTOID, b, (int32)bl);
            free(b);
        } else if (ci_eq(fmtn, "escape")) {
            Buf b; buf_init(&b);
            for (int32 i = 0; i < n; i++) {
                unsigned char c = (unsigned char)p[i];
                if (c == 0 || c >= 128) buf_printf(&b, "\\%03o", c);
                else if (c == '\\') buf_adds(&b, "\\\\");
                else buf_addc(&b, (char)c);
            }
            r = d_textbuf(TEXTOID, &b);
        } else {
            free(fmtn); free(o);
            ereport(s, "22023", "unrecognized encoding: \"%s\"", "");
        }
        free(fmtn); free(o);
        return r;
    }
    if (nargs == 2 && EQI("decode")) {
        if (args[0].isnull || args[1].isnull) return d_null(BYTEAOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        char *fmtn = as_cstr(s, args[1]);
        Datum r;
        if (ci_eq(fmtn, "hex")) {
            Buf b; buf_init(&b);
            for (int32 i = 0; i + 1 < n; i += 2) {
                int hi = p[i], lo = p[i+1];
                if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo))
                    ereport(s, "22023", "invalid hexadecimal digit: \"%c\"",
                            isxdigit((unsigned char)hi) ? lo : hi);
                int hv = isdigit((unsigned char)hi) ? hi-'0' : tolower(hi)-'a'+10;
                int lv = isdigit((unsigned char)lo) ? lo-'0' : tolower(lo)-'a'+10;
                buf_addc(&b, (char)((hv << 4) | lv));
            }
            r = d_textn(BYTEAOID, b.data ? b.data : "", (int32)b.len);
            buf_free(&b);
        } else if (ci_eq(fmtn, "base64")) {
            unsigned char *out = xmalloc((size_t)n + 4);
            int ol = b64_decode(p, (size_t)n, out);
            if (ol < 0) ereport(s, "22023", "invalid base64 end sequence");
            r = d_textn(BYTEAOID, (char *)out, ol);
            free(out);
        } else if (ci_eq(fmtn, "escape")) {
            Buf b; buf_init(&b);
            for (int32 i = 0; i < n; i++) {
                if (p[i] == '\\' && i + 3 < n && isdigit((unsigned char)p[i+1])) {
                    buf_addc(&b, (char)((p[i+1]-'0')*64 + (p[i+2]-'0')*8 + (p[i+3]-'0')));
                    i += 3;
                } else if (p[i] == '\\' && i + 1 < n && p[i+1] == '\\') { buf_addc(&b,'\\'); i++; }
                else buf_addc(&b, p[i]);
            }
            r = d_textn(BYTEAOID, b.data ? b.data : "", (int32)b.len);
            buf_free(&b);
        } else {
            free(fmtn); free(o);
            ereport(s, "22023", "unrecognized encoding: \"%s\"", "");
        }
        free(fmtn); free(o);
        return r;
    }
    if (nargs == 1 && EQI("quote_ident")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        char *q = quote_ident_str(p, n);
        free(o);
        Datum r = d_text(TEXTOID, q); free(q);
        return r;
    }
    if (nargs == 1 && (EQI("quote_literal") || EQI("quote_nullable"))) {
        if (args[0].isnull)
            return EQI("quote_nullable") ? d_text(TEXTOID, "NULL") : d_null(TEXTOID);
        char *t = as_cstr(s, args[0]);
        char *q = quote_literal_str(t, (int32)strlen(t));
        free(t);
        Datum r = d_text(TEXTOID, q); free(q);
        return r;
    }
    if ((nargs == 3 || nargs == 4) && EQI("overlay")) {
        for (int i = 0; i < nargs; i++) if (args[i].isnull) return d_null(TEXTOID);
        char *o1,*o2; int32 n1,n2;
        const char *t = as_bytes(s, args[0], &n1, &o1);
        const char *np = as_bytes(s, args[1], &n2, &o2);
        int64 from = as_i8(s, args[2]);
        int64 forl = (nargs == 4) ? as_i8(s, args[3]) : u8_len(np, n2);
        if (from < 1) from = 1;
        int total = u8_len(t, n1);
        int64 endc = from - 1 + forl;
        if (endc > total) endc = total;
        if (endc < from - 1) endc = from - 1;
        Buf b; buf_init(&b);
        buf_add(&b, t, (size_t)u8_off(t, n1, (int)(from - 1)));
        buf_add(&b, np, (size_t)n2);
        int32 tailoff = u8_off(t, n1, (int)endc);
        buf_add(&b, t + tailoff, (size_t)(n1 - tailoff));
        free(o1); free(o2);
        return d_textbuf(TEXTOID, &b);
    }
    if ((nargs == 1 || nargs == 2) && EQI("to_ascii")) {
        if (args[0].isnull) return d_null(TEXTOID);
        if (nargs == 1)
            ereport(s, "0A000", "encoding conversion from UTF8 to ASCII not supported");
        char *o; int32 n;
        const char *p = as_bytes(s, args[0], &n, &o);
        Buf b; buf_init(&b);
        for (int32 i = 0; i < n; i++) if (!((unsigned char)p[i] & 0x80)) buf_addc(&b, p[i]);
        free(o);
        return d_textbuf(TEXTOID, &b);
    }

    /* ---------------------------------------------------- regular expressions */
    if (nargs >= 3 && nargs <= 6 && EQI("regexp_replace")) {
        for (int i = 0; i < 3; i++) if (args[i].isnull) return d_null(TEXTOID);
        char *src = as_cstr(s, args[0]), *pat = as_cstr(s, args[1]), *rep = as_cstr(s, args[2]);
        char *flags = NULL;
        int64 startp = 1;
        int64 whichN = 0;
        if (nargs == 4) {
            if (f_is_numfam(args[3].type)) startp = as_i8(s, args[3]);
            else flags = as_cstr(s, args[3]);
        } else if (nargs >= 5) {
            startp = args[3].isnull ? 1 : as_i8(s, args[3]);
            whichN = args[4].isnull ? 0 : as_i8(s, args[4]);
            if (nargs == 6 && !args[5].isnull) flags = as_cstr(s, args[5]);
        }
#if PGVC_VN >= 150
        if (nargs >= 5 && startp < 1)
            ereport(s, "22023", "invalid value for parameter \"start\": %lld", (long long)startp);
        if (nargs >= 5 && whichN < 0)
            ereport(s, "22023", "invalid value for parameter \"n\": %lld", (long long)whichN);
#endif
        PgRegex r; memset(&r, 0, sizeof r);
        re_compile(s, &r, pat, (int32)strlen(pat), flags);
        bool global = r.global || (whichN == 0 && nargs >= 5);
        int32 slen = (int32)strlen(src);
        int32 boff = (startp > 1) ? u8_off(src, slen, (int)startp - 1) : 0;
        Buf b; buf_init(&b);
        buf_add(&b, src, (size_t)boff);
        regmatch_t m[10];
        int32 pos = boff;
        long count = 0;
        while (pos <= slen && pgre_search(&r, src, pos, m, 10)) {
            count++;
            bool take = global ? true : (whichN <= 1 ? count == 1 : count == whichN);
            buf_add(&b, src + pos, (size_t)(m[0].rm_so - pos));
            if (take) {
                for (const char *q = rep; *q; q++) {
                    if (*q == '\\' && q[1]) {
                        q++;
                        if (*q == '&') { buf_add(&b, src+m[0].rm_so, (size_t)(m[0].rm_eo-m[0].rm_so)); }
                        else if (*q >= '1' && *q <= '9') {
                            int g = *q - '0';
                            if ((size_t)g <= re_ngroups(&r) && m[g].rm_so >= 0)
                                buf_add(&b, src+m[g].rm_so, (size_t)(m[g].rm_eo-m[g].rm_so));
                        } else if (*q == '\\') buf_addc(&b, '\\');
                        else buf_addc(&b, *q);
                    } else buf_addc(&b, *q);
                }
            } else
                buf_add(&b, src + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
            if (m[0].rm_eo == m[0].rm_so) {
                if (m[0].rm_eo < slen) buf_addc(&b, src[m[0].rm_eo]);
                pos = m[0].rm_eo + 1;
            } else pos = m[0].rm_eo;
            if (!global && count >= (whichN > 1 ? whichN : 1)) break;
        }
        if (pos < slen) buf_add(&b, src + pos, (size_t)(slen - pos));
        re_free(&r);
        free(src); free(pat); free(rep); free(flags);
        return d_textbuf(TEXTOID, &b);
    }
#if PGVC_VN < 100
    if (EQI("regexp_match")) NOTFOUND();
#endif
    if (nargs >= 2 && nargs <= 3 && (EQI("regexp_match") || EQI("regexp_matches"))) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTARRAYOID);
        char *src = as_cstr(s, args[0]), *pat = as_cstr(s, args[1]);
        char *flags = (nargs == 3 && !args[2].isnull) ? as_cstr(s, args[2]) : NULL;
        PgRegex r; memset(&r, 0, sizeof r);
        re_compile(s, &r, pat, (int32)strlen(pat), flags);
        int32 slen = (int32)strlen(src);
        regmatch_t m[10];
        bool matches = EQI("regexp_matches");
        ArrayVal *outer = arr_new(TEXTARRAYOID);
        Datum single = d_null(TEXTARRAYOID);
        int32 pos = 0;
        long cnt = 0;
        while (pos <= slen && pgre_search(&r, src, pos, m, 10)) {
            size_t ng = re_ngroups(&r);
            ArrayVal *row = arr_new(TEXTOID);
            if (ng == 0) arr_push(row, d_textn(TEXTOID, src+m[0].rm_so, (int32)(m[0].rm_eo-m[0].rm_so)));
            else for (size_t g = 1; g <= ng && g < 10; g++)
                arr_push(row, m[g].rm_so < 0 ? d_null(TEXTOID)
                          : d_textn(TEXTOID, src+m[g].rm_so, (int32)(m[g].rm_eo-m[g].rm_so)));
            Datum rd = d_arr(TEXTARRAYOID, row);
            if (cnt == 0) single = rd;
            arr_push(outer, rd);
            cnt++;
            if (!r.global) break;
            pos = (m[0].rm_eo == m[0].rm_so) ? m[0].rm_eo + 1 : m[0].rm_eo;
        }
        re_free(&r);
        free(src); free(pat); free(flags);
        if (!matches) return single;
        if (!r.global && cnt == 0) return d_arr(RECORDARRAYOID, arr_new(TEXTARRAYOID));
        return d_arr(RECORDARRAYOID, outer);
    }
    if (nargs >= 2 && nargs <= 3 &&
        (EQI("regexp_split_to_array") || EQI("regexp_split_to_table"))) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTARRAYOID);
        char *src = as_cstr(s, args[0]), *pat = as_cstr(s, args[1]);
        char *flags = (nargs == 3 && !args[2].isnull) ? as_cstr(s, args[2]) : NULL;
        PgRegex r; memset(&r, 0, sizeof r);
        re_compile(s, &r, pat, (int32)strlen(pat), flags);
        int32 slen = (int32)strlen(src);
        regmatch_t m[2];
        ArrayVal *a = arr_new(TEXTOID);
        int32 pos = 0, piece = 0;
        while (pos <= slen && pgre_search(&r, src, pos, m, 2)) {
            if (m[0].rm_eo == m[0].rm_so) {
                /* a degenerate match splits between characters, but never
                 * produces a leading or trailing empty field */
                int32 k = m[0].rm_so;
                int cl = (k < slen) ? u8_charlen((unsigned char)src[k]) : 1;
                if (k > 0 && k < slen) {
                    arr_push(a, d_textn(TEXTOID, src + piece, k - piece));
                    piece = k;
                }
                pos = k + cl;
                continue;
            }
            arr_push(a, d_textn(TEXTOID, src + piece, m[0].rm_so - piece));
            piece = m[0].rm_eo;
            pos = m[0].rm_eo;
        }
        arr_push(a, d_textn(TEXTOID, src + piece, slen - piece));
        re_free(&r);
        free(src); free(pat); free(flags);
        return d_arr(TEXTARRAYOID, a);
    }
#if PGVC_VN >= 150
    if (nargs >= 2 && nargs <= 4 && EQI("regexp_count")) {
        if (args[0].isnull || args[1].isnull) return d_null(INT4OID);
        char *src = as_cstr(s, args[0]), *pat = as_cstr(s, args[1]);
        int64 startp = (nargs >= 3 && !args[2].isnull) ? as_i8(s, args[2]) : 1;
        if (startp < 1) ereport(s, "22023", "invalid value for parameter \"start\": %lld",
                                (long long)startp);
        char *flags = (nargs == 4 && !args[3].isnull) ? as_cstr(s, args[3]) : NULL;
        PgRegex r; memset(&r, 0, sizeof r);
        re_compile(s, &r, pat, (int32)strlen(pat), flags);
        int32 slen = (int32)strlen(src);
        int32 pos = (startp > 1) ? u8_off(src, slen, (int)startp - 1) : 0;
        regmatch_t m[1];
        int n = 0;
        while (pos <= slen && pgre_search(&r, src, pos, m, 1)) {
            n++;
            pos = (m[0].rm_eo == m[0].rm_so) ? m[0].rm_eo + 1 : m[0].rm_eo;
        }
        re_free(&r); free(src); free(pat); free(flags);
        return d_int(INT4OID, n);
    }
    if (nargs >= 2 && nargs <= 7 && EQI("regexp_instr")) {
        if (args[0].isnull || args[1].isnull) return d_null(INT4OID);
        char *src = as_cstr(s, args[0]), *pat = as_cstr(s, args[1]);
        int64 startp = (nargs >= 3) ? re_intarg(s, args, 2, 1) : 1;
        int64 occ    = (nargs >= 4) ? re_intarg(s, args, 3, 1) : 1;
        int64 endopt = (nargs >= 5) ? re_intarg(s, args, 4, 0) : 0;
        char *flags  = (nargs >= 6 && !args[5].isnull) ? as_cstr(s, args[5]) : NULL;
        int64 sub    = (nargs >= 7) ? re_intarg(s, args, 6, 0) : 0;
        if (startp < 1) ereport(s, "22023", "invalid value for parameter \"start\": %lld",
                                (long long)startp);
        if (occ < 1) ereport(s, "22023", "invalid value for parameter \"n\": %lld",
                             (long long)occ);
        if (endopt != 0 && endopt != 1)
            ereport(s, "22023", "invalid value for parameter \"endoption\": %lld",
                    (long long)endopt);
        if (sub < 0) ereport(s, "22023", "invalid value for parameter \"subexpr\": %lld",
                             (long long)sub);
        PgRegex r; memset(&r, 0, sizeof r);
        re_compile(s, &r, pat, (int32)strlen(pat), flags);
        int32 slen = (int32)strlen(src);
        int32 pos = (startp > 1) ? u8_off(src, slen, (int)startp - 1) : 0;
        regmatch_t m[10];
        int64 n = 0; int res = 0;
        while (pos <= slen && pgre_search(&r, src, pos, m, 10)) {
            n++;
            if (n == occ) {
                int g = (int)sub;
                if (g > 0 && ((size_t)g > re_ngroups(&r) || m[g].rm_so < 0)) { res = 0; break; }
                res = u8_len(src, endopt ? m[g].rm_eo : m[g].rm_so) + 1;
                break;
            }
            pos = (m[0].rm_eo == m[0].rm_so) ? m[0].rm_eo + 1 : m[0].rm_eo;
        }
        re_free(&r); free(src); free(pat); free(flags);
        return d_int(INT4OID, res);
    }
    if (nargs >= 2 && nargs <= 3 && EQI("regexp_like")) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        char *flags = (nargs == 3 && !args[2].isnull) ? as_cstr(s, args[2]) : NULL;
        bool m = regex_match(s, args[0], args[1], flags);
        free(flags);
        return d_bool(m);
    }
    if (nargs >= 2 && nargs <= 6 && EQI("regexp_substr")) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *src = as_cstr(s, args[0]), *pat = as_cstr(s, args[1]);
        int64 startp = (nargs >= 3) ? re_intarg(s, args, 2, 1) : 1;
        int64 occ    = (nargs >= 4) ? re_intarg(s, args, 3, 1) : 1;
        char *flags  = (nargs >= 5 && !args[4].isnull) ? as_cstr(s, args[4]) : NULL;
        int64 sub    = (nargs >= 6) ? re_intarg(s, args, 5, 0) : 0;
        if (startp < 1) ereport(s, "22023", "invalid value for parameter \"start\": %lld",
                                (long long)startp);
        if (occ < 1) ereport(s, "22023", "invalid value for parameter \"n\": %lld",
                             (long long)occ);
        if (sub < 0) ereport(s, "22023", "invalid value for parameter \"subexpr\": %lld",
                             (long long)sub);
        PgRegex r; memset(&r, 0, sizeof r);
        re_compile(s, &r, pat, (int32)strlen(pat), flags);
        int32 slen = (int32)strlen(src);
        int32 pos = (startp > 1) ? u8_off(src, slen, (int)startp - 1) : 0;
        regmatch_t m[10];
        int64 n = 0;
        Datum res = d_null(TEXTOID);
        while (pos <= slen && pgre_search(&r, src, pos, m, 10)) {
            n++;
            if (n == occ) {
                int g = (int)sub;
                if (g > 0 && ((size_t)g > re_ngroups(&r) || m[g].rm_so < 0)) break;
                res = d_textn(TEXTOID, src + m[g].rm_so, (int32)(m[g].rm_eo - m[g].rm_so));
                break;
            }
            pos = (m[0].rm_eo == m[0].rm_so) ? m[0].rm_eo + 1 : m[0].rm_eo;
        }
        re_free(&r); free(src); free(pat); free(flags);
        return res;
    }
#endif

    /* ------------------------------------------------------------------ math */
    if (nargs == 1 && EQI("abs")) {
        Datum a = args[0];
        if (a.isnull) return a;
        switch (a.type) {
        case INT2OID: case INT4OID: case INT8OID:
            if (a.v.i == int_lo(a.type)) int_range_err(s, a.type);
            return d_int(a.type, a.v.i < 0 ? -a.v.i : a.v.i);
        case FLOAT4OID: case FLOAT8OID: return d_float(a.type, fabs(a.v.f));
        case NUMERICOID: return d_num(num_abs(a.v.num));
        case UNKNOWNOID: return d_num(num_abs(as_num(s, a)));
        default: NOTFOUND();
        }
    }
    if ((nargs == 1 || nargs == 2) &&
        (EQI("ceil") || EQI("ceiling") || EQI("floor") || EQI("round") ||
         EQI("trunc") || EQI("sign"))) {
        Datum a = args[0];
        bool two = (nargs == 2);
        if (two && !(EQI("round") || EQI("trunc"))) NOTFOUND();
        Oid at = a.type == UNKNOWNOID ? NUMERICOID : a.type;
        if (!f_is_numfam(at)) NOTFOUND();
        bool asnum = (at == NUMERICOID) || two;
        if (a.isnull) return d_null(asnum ? NUMERICOID : FLOAT8OID);
        if (asnum) {
            Numeric *n = as_num(s, a);
            int sc = two ? (args[1].isnull ? 0 : as_i4(s, args[1])) : 0;
            if (EQI("ceil") || EQI("ceiling")) return d_num(num_ceil(n));
            if (EQI("floor")) return d_num(num_floor(n));
            if (EQI("round")) return d_num(num_round(n, sc));
            if (EQI("trunc")) return d_num(num_trunc(n, sc));
            { Numeric *z = num_from_int64(0); int c = num_cmp(n, z);
              return d_num(num_from_int64(c < 0 ? -1 : c > 0 ? 1 : 0)); }
        }
        double x = as_f8(s, a), r;
        if (EQI("ceil") || EQI("ceiling")) r = ceil(x);
        else if (EQI("floor")) r = floor(x);
        else if (EQI("round")) r = rint(x);
        else if (EQI("trunc")) r = trunc(x);
        else r = (x < 0) ? -1 : (x > 0) ? 1 : 0;
        return d_float(FLOAT8OID, r);
    }
    if (nargs == 1 && EQI("sqrt")) {
        Datum a = args[0];
        Oid at = a.type == UNKNOWNOID ? NUMERICOID : a.type;
        if (!f_is_numfam(at)) NOTFOUND();
        if (a.isnull) return d_null(at == NUMERICOID ? NUMERICOID : FLOAT8OID);
        if (at == NUMERICOID) {
            bool err = false;
            Numeric *n = as_num(s, a);
            Numeric *z = num_from_int64(0);
            if (num_cmp(n, z) < 0)
                ereport(s, "2201F", "cannot take square root of a negative number");
            Numeric *r = num_sqrt(n, &err);
            if (err) ereport(s, "2201F", "cannot take square root of a negative number");
            return d_num(r);
        }
        double x = as_f8(s, a);
        if (x < 0) ereport(s, "2201F", "cannot take square root of a negative number");
        return d_float(FLOAT8OID, sqrt(x));
    }
    if (nargs == 1 && EQI("cbrt")) {
        if (args[0].isnull) return d_null(FLOAT8OID);
        return d_float(FLOAT8OID, cbrt(as_f8(s, args[0])));
    }
#if PGVC_VN < 120
    if (nargs == 1 && EQI("log10")) NOTFOUND();
#endif
    if (nargs == 1 && (EQI("exp") || EQI("ln") || EQI("log") || EQI("log10"))) {
        Datum a = args[0];
        Oid at = a.type == UNKNOWNOID ? NUMERICOID : a.type;
        if (!f_is_numfam(at)) NOTFOUND();
        bool asnum = (at == NUMERICOID) && !EQI("log10");
        if (a.isnull) return d_null(asnum ? NUMERICOID : FLOAT8OID);
        double x = as_f8(s, a);
        if (!EQI("exp")) {
            if (x == 0.0) ereport(s, "2201E", "cannot take logarithm of zero");
            if (x < 0.0)  ereport(s, "2201E", "cannot take logarithm of a negative number");
        }
        if (!asnum) {
            double r = EQI("exp") ? exp(x) : EQI("ln") ? log(x) : log10(x);
            if (isinf(r) && !isinf(x)) ereport(s, "22003", "value out of range: overflow");
            return d_float(FLOAT8OID, r);
        }
        /* numeric: pick PostgreSQL's result scale, then render at that scale */
        long double lx = (long double)x, lr;
        int rscale;
        if (EQI("exp")) {
            lr = expl(lx);
            rscale = 16 - (int)(x * 0.434294481903252);
        } else {
            lr = logl(lx);
            if (EQI("log")) lr /= logl(10.0L);
            long double ln_var = logl(lx);
            int dw = (ln_var == 0.0L) ? 0 : (int)log10(fabs((double)ln_var));
            rscale = 16 - dw;
        }
        int ds = num_scale(a.v.num ? a.v.num : as_num(s, a));
        if (rscale < ds) rscale = ds;
        if (rscale < 0) rscale = 0;
        if (rscale > 1000) rscale = 1000;
        char tmp[1100];
        snprintf(tmp, sizeof tmp, "%.*Lf", rscale, lr);
        bool ok = false;
        Numeric *n = num_from_str(tmp, &ok);
        if (!ok) ereport(s, "22003", "value out of range");
        return d_num(n);
    }
    if (nargs == 2 && EQI("log")) {
        if (args[0].isnull || args[1].isnull) return d_null(NUMERICOID);
        double b = as_f8(s, args[0]), x = as_f8(s, args[1]);
        if (b == 0.0 || x == 0.0) ereport(s, "2201E", "cannot take logarithm of zero");
        if (b < 0.0 || x < 0.0)   ereport(s, "2201E", "cannot take logarithm of a negative number");
        long double lr = logl((long double)x) / logl((long double)b);
        long double lb = logl((long double)b), ln = logl((long double)x);
        int dwb = (lb == 0.0L) ? 0 : (int)log10(fabs((double)lb));
        int dwn = (ln == 0.0L) ? 0 : (int)log10(fabs((double)ln));
        int rscale = 16 - (dwn - dwb);
        int d1 = num_scale(as_num(s, args[0])), d2 = num_scale(as_num(s, args[1]));
        if (rscale < d1) rscale = d1;
        if (rscale < d2) rscale = d2;
        if (rscale < 0) rscale = 0;
        char tmp[1100];
        snprintf(tmp, sizeof tmp, "%.*Lf", rscale, lr);
        bool ok = false;
        Numeric *n = num_from_str(tmp, &ok);
        if (!ok) ereport(s, "22003", "value out of range");
        return d_num(n);
    }
    if (nargs == 2 && (EQI("power") || EQI("pow"))) {
        Oid ta = args[0].type == UNKNOWNOID ? NUMERICOID : args[0].type;
        Oid tb = args[1].type == UNKNOWNOID ? NUMERICOID : args[1].type;
        if (!f_is_numfam(ta) || !f_is_numfam(tb)) NOTFOUND();
        Datum a2[2] = {args[0], args[1]};
        return num_binop(s, "^", a2[0], a2[1], found);
    }
    if (nargs == 0 && EQI("pi")) return d_float(FLOAT8OID, 3.14159265358979323846);
    if (nargs == 1 && (EQI("degrees") || EQI("radians") || EQI("sin") || EQI("cos") ||
                       EQI("tan") || EQI("cot") || EQI("asin") || EQI("acos") ||
                       EQI("atan") || EQI("sind") || EQI("cosd") || EQI("tand") ||
                       EQI("sinh") || EQI("cosh") || EQI("tanh") ||
                       EQI("asinh") || EQI("acosh") || EQI("atanh"))) {
        if (args[0].isnull) return d_null(FLOAT8OID);
        double x = as_f8(s, args[0]), r;
        if (EQI("degrees")) r = x * (180.0 / 3.14159265358979323846);
        else if (EQI("radians")) r = x * (3.14159265358979323846 / 180.0);
        else if (EQI("sin")) r = sin(x);
        else if (EQI("cos")) r = cos(x);
        else if (EQI("tan")) r = tan(x);
        else if (EQI("cot")) r = 1.0 / tan(x);
        else if (EQI("asin")) { if (x < -1 || x > 1) ereport(s,"22003","input is out of range"); r = asin(x); }
        else if (EQI("acos")) { if (x < -1 || x > 1) ereport(s,"22003","input is out of range"); r = acos(x); }
        else if (EQI("atan")) r = atan(x);
        else if (EQI("sind")) r = sin(x * (3.14159265358979323846/180.0));
        else if (EQI("cosd")) r = cos(x * (3.14159265358979323846/180.0));
        else if (EQI("tand")) r = tan(x * (3.14159265358979323846/180.0));
        else if (EQI("sinh")) r = sinh(x);
        else if (EQI("cosh")) r = cosh(x);
        else if (EQI("tanh")) r = tanh(x);
        else if (EQI("asinh")) r = asinh(x);
        else if (EQI("acosh")) r = acosh(x);
        else r = atanh(x);
        return d_float(FLOAT8OID, r);
    }
    if (nargs == 2 && (EQI("atan2") || EQI("atan2d"))) {
        if (args[0].isnull || args[1].isnull) return d_null(FLOAT8OID);
        double r = atan2(as_f8(s, args[0]), as_f8(s, args[1]));
        if (EQI("atan2d")) r *= 180.0 / 3.14159265358979323846;
        return d_float(FLOAT8OID, r);
    }
    if (nargs == 2 && EQI("mod")) {
        Oid ta = args[0].type == UNKNOWNOID ? INT4OID : args[0].type;
        Oid tb = args[1].type == UNKNOWNOID ? INT4OID : args[1].type;
        if (!f_is_numfam(ta) || !f_is_numfam(tb)) NOTFOUND();
        if (f_is_float(ta) || f_is_float(tb)) NOTFOUND();
        return num_binop(s, "%", args[0], args[1], found);
    }
    if (nargs == 2 && EQI("div")) {
        if (args[0].isnull || args[1].isnull) return d_null(NUMERICOID);
        bool dz = false;
        Numeric *r = num_div_trunc(as_num(s, args[0]), as_num(s, args[1]), &dz);
        if (dz) ereport(s, "22012", "division by zero");
        return d_num(r);
    }
#if PGVC_VN >= 130
    if (nargs == 2 && (EQI("gcd") || EQI("lcm"))) {
        Oid rt = arith_type(args[0].type == UNKNOWNOID ? INT4OID : args[0].type,
                            args[1].type == UNKNOWNOID ? INT4OID : args[1].type);
        if (rt == NUMERICOID) {
            if (args[0].isnull || args[1].isnull) return d_null(NUMERICOID);
            return num_gcd_lcm(s, as_num(s, args[0]), as_num(s, args[1]), EQI("lcm"));
        }
        if (rt != INT8OID) rt = INT4OID;
        if (args[0].isnull || args[1].isnull) return d_null(rt);
        int64 x = as_i8(s, args[0]), y = as_i8(s, args[1]);
        int64 ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        while (ay) { int64 t = ax % ay; ax = ay; ay = t; }
        if (EQI("gcd")) return int_result(s, rt, (__int128)ax);
        if (ax == 0) return d_int(rt, 0);
        __int128 l = ((__int128)(x < 0 ? -x : x) / ax) * (__int128)(y < 0 ? -y : y);
        return int_result(s, rt, l);
    }
#endif
    if ((nargs == 2 || nargs == 4) && EQI("width_bucket")) {
        if (nargs == 2) {
            if (args[0].isnull || args[1].isnull) return d_null(INT4OID);
            ArrayVal *a = args[1].v.arr;
            int n = 0;
            for (int i = 0; a && i < a->nelem; i++)
                if (!a->elems[i].isnull && d_cmp(s, a->elems[i], args[0]) <= 0) n++;
            return d_int(INT4OID, n);
        }
        for (int i = 0; i < 4; i++) if (args[i].isnull) return d_null(INT4OID);
        int64 cnt = as_i8(s, args[3]);
        if (cnt <= 0) ereport(s, "22004", "count must be greater than zero");
        double op = as_f8(s, args[0]), lo = as_f8(s, args[1]), hi = as_f8(s, args[2]);
        if (isnan(op) || isnan(lo) || isnan(hi))
            ereport(s, "22003", "operand, lower bound, and upper bound cannot be NaN");
        if (lo == hi) ereport(s, "22004", "lower bound cannot equal upper bound");
        int64 r;
        if (lo < hi) {
            if (op < lo) r = 0;
            else if (op >= hi) r = cnt + 1;
            else r = (int64)(((double)cnt * (op - lo)) / (hi - lo)) + 1;
        } else {
            if (op > lo) r = 0;
            else if (op <= hi) r = cnt + 1;
            else r = (int64)(((double)cnt * (lo - op)) / (lo - hi)) + 1;
        }
        return int_result(s, INT4OID, (__int128)r);
    }
    if (nargs == 1 && (EQI("factorial")
#if PGVC_VN < 140
                       || EQ("!post") || EQ("!!")
#endif
                       )) {
        if (args[0].isnull) return d_null(NUMERICOID);
        int64 n = as_i8(s, args[0]);
        if (n < 0) ereport(s, "22003", "factorial of a negative number is undefined");
        Numeric *r = num_from_int64(1);
        for (int64 i = 2; i <= n; i++) r = num_mul(r, num_from_int64(i));
        return d_num(r);
    }
    if (EQI("random")) {
        if (nargs == 0) return d_float(FLOAT8OID, pg_random());
#if PGVC_VN >= 170
        if (nargs == 2) {
            if (args[0].isnull || args[1].isnull) return d_null(args[0].type);
            Oid rt = arith_type(args[0].type == UNKNOWNOID ? INT4OID : args[0].type,
                                args[1].type == UNKNOWNOID ? INT4OID : args[1].type);
            if (rt == NUMERICOID) {
                Datum a = coerce_to(s, args[0], NUMERICOID, -1, 1);
                Datum b2 = coerce_to(s, args[1], NUMERICOID, -1, 1);
                if (num_is_nan(a.v.num)) ereport(s, "22023", "lower bound cannot be NaN");
                if (num_is_nan(b2.v.num)) ereport(s, "22023", "upper bound cannot be NaN");
                if (num_cmp(a.v.num, b2.v.num) > 0)
                    ereport(s, "22023", "lower bound must be less than or equal to upper bound");
                /* PostgreSQL keeps the wider of the two input scales */
                int sc = num_scale(a.v.num);
                if (num_scale(b2.v.num) > sc) sc = num_scale(b2.v.num);
                double lo = num_to_double(a.v.num), hi = num_to_double(b2.v.num);
                Numeric *r = num_from_double(lo + pg_random() * (hi - lo));
                return d_num(num_round(r, sc));
            }
            if (f_is_float(rt)) {
                double lo = as_f8(s, args[0]), hi = as_f8(s, args[1]);
                if (lo > hi) ereport(s, "22023", "lower bound must be less than or equal to upper bound");
                return d_float(FLOAT8OID, lo + pg_random() * (hi - lo));
            }
            int64 lo = as_i8(s, args[0]), hi = as_i8(s, args[1]);
            if (lo > hi) ereport(s, "22023", "lower bound must be less than or equal to upper bound");
            return d_int(rt, lo + (int64)(pg_random() * (double)(hi - lo + 1)));
        }
#endif
        NOTFOUND();
    }
    if (nargs == 1 && EQI("setseed")) {
        double x = as_f8(s, args[0]);
        if (x < -1.0 || x > 1.0)
            ereport(s, "22023", "setseed parameter %g is out of allowed range [-1,1]", x);
        uint64 seed = (uint64)((x + 1.0) * 4611686018427387904.0) + 1;
        rnd_state_lo = (uint32)(seed & 0xffffffffu) | 1u;
        rnd_state_hi = (uint32)(seed >> 32) | 1u;
        rnd_seeded = true;
        return d_void();
    }

    /* ------------------------------------------------------------- date/time */
    if (nargs == 0 && (EQI("now") || EQI("transaction_timestamp") ||
                       EQI("statement_timestamp") || EQI("current_timestamp")))
        return d_int(TIMESTAMPTZOID, xact_us(s));
    if (nargs == 1 && EQI("current_timestamp")) return d_int(TIMESTAMPTZOID, xact_us(s));
    if (nargs == 0 && EQI("clock_timestamp")) return d_int(TIMESTAMPTZOID, clock_us());
    if (nargs == 0 && EQI("timeofday")) {
        int64 us = clock_us();
        char *t = dt_format_timestamp(us, true);
        Datum r = d_text(TEXTOID, t); free(t);
        return r;
    }
    if (nargs == 0 && EQI("current_date")) {
        int32 day; int64 tod;
        ts_split(xact_us(s), &day, &tod);
        return d_int(DATEOID, day);
    }
    if ((nargs == 0 || nargs == 1) && (EQI("localtimestamp"))) {
        int64 us = xact_us(s);
        if (nargs == 1 && !args[0].isnull) {
            int p = as_i4(s, args[0]);
            int64 sc = 1; for (int i = p; i < 6; i++) sc *= 10;
            us = (us / sc) * sc;
        }
        return d_int(TIMESTAMPOID, us);
    }
    if ((nargs == 0 || nargs == 1) && (EQI("localtime") || EQI("current_time"))) {
        int32 day; int64 tod;
        ts_split(xact_us(s), &day, &tod);
        if (nargs == 1 && !args[0].isnull) {
            int p = as_i4(s, args[0]);
            int64 sc = 1; for (int i = p; i < 6; i++) sc *= 10;
            tod = (tod / sc) * sc;
        }
        if (EQI("localtime")) return d_int(TIMEOID, tod);
        char *t = dt_format_time(tod);
        Buf b; buf_init(&b); buf_adds(&b, t); buf_adds(&b, "+00");
        free(t);
        return d_textbuf(TIMETZOID, &b);
    }
    if ((nargs == 1 || nargs == 2) && EQI("age")) {
        if (nargs == 1) {
            if (args[0].isnull) return d_null(INTERVALOID);
            int32 day; int64 tod;
            ts_split(xact_us(s), &day, &tod);
            int64 today = (int64)day * USECS_PER_DAY;
            Datum a = coerce_to(s, args[0], TIMESTAMPOID, -1, 1);
            return d_interval(ts_age(today, a.v.i));
        }
        if (args[0].isnull || args[1].isnull) return d_null(INTERVALOID);
        Datum a = args[0], b = args[1];
        if (a.type == DATEOID) a = coerce_to(s, a, TIMESTAMPOID, -1, 1);
        if (b.type == DATEOID) b = coerce_to(s, b, TIMESTAMPOID, -1, 1);
        if ((a.type != TIMESTAMPOID && a.type != TIMESTAMPTZOID) ||
            (b.type != TIMESTAMPOID && b.type != TIMESTAMPTZOID)) {
            a = coerce_to(s, args[0], TIMESTAMPOID, -1, 1);
            b = coerce_to(s, args[1], TIMESTAMPOID, -1, 1);
        }
        return d_interval(ts_age(a.v.i, b.v.i));
    }
    if (nargs == 2 && EQI("date_trunc")) {
        if (args[0].isnull || args[1].isnull) return d_null(args[1].type);
        char *f = as_cstr(s, args[0]);
        bool ok = true;
        Datum r;
        if (args[1].type == INTERVALOID) r = d_interval(iv_trunc(s, f, args[1].v.iv, &ok));
        else {
            Datum v = args[1];
            if (v.type == DATEOID) v = coerce_to(s, v, TIMESTAMPOID, -1, 1);
            if (v.type != TIMESTAMPOID && v.type != TIMESTAMPTZOID)
                v = coerce_to(s, v, TIMESTAMPOID, -1, 1);
            int64 tv = ts_trunc(s, f, v.v.i, &ok);
            if (!ok) ereport(s, "22023", "unit \"%s\" not recognized for type %s",
                             f, sqlname(v.type));
            r = d_int(v.type, tv);
        }
        free(f);
        return r;
    }
#if PGVC_VN >= 160
    if ((nargs == 2 || nargs == 3) && (EQI("date_add") || EQI("date_subtract"))) {
        int sign = EQI("date_add") ? 1 : -1;
        for (int i = 0; i < nargs; i++) if (args[i].isnull) return d_null(TIMESTAMPTZOID);
        Datum v = coerce_to(s, args[0], TIMESTAMPTZOID, -1, 1);
        Datum iv = coerce_to(s, args[1], INTERVALOID, -1, 1);
        if (nargs == 2)
            return d_int(TIMESTAMPTZOID, ts_add_interval(v.v.i, &iv.v.iv, sign));
        char *zn = as_cstr(s, args[2]);
        PgZone z;
        if (!pgzone_resolve(zn, &z)) ereport(s, "22023", "time zone \"%s\" not recognized", zn);
        free(zn);
        int64 t = v.v.i;
        int64 loc = t + (int64)tz_offset_utc(&z, t) * 1000000;
        int64 nl = ts_add_interval(loc, &iv.v.iv, sign);
        return d_int(TIMESTAMPTZOID, nl - (int64)tz_offset_local(&z, nl) * 1000000);
    }
#endif
    if (nargs == 2 && (EQI("date_part") || EQI("extract"))) {
        if (args[0].isnull || args[1].isnull)
#if PGVC_VN >= 140
            return d_null(EQI("extract") ? NUMERICOID : FLOAT8OID);
#else
            return d_null(FLOAT8OID);
#endif
        char *f = as_cstr(s, args[0]);
        Datum v = args[1];
        Oid vt = v.type;
        if (vt == UNKNOWNOID) { v = coerce_to(s, v, TIMESTAMPOID, -1, 1); vt = TIMESTAMPOID; }
        bool ok = false;
        PgInterval iv; memset(&iv, 0, sizeof iv);
        if (vt == INTERVALOID) iv = v.v.iv;
#if PGVC_VN >= 140
        if (EQI("extract")) {
            char *t = dt_extract_numeric(f, vt, v.v.i, vt == INTERVALOID ? &iv : NULL, &ok);
            if (!ok) {
                const char *m = dt_last_error();
                if (m && *m) ereport(s, "22023", "%s", m);
                ereport(s, "22023", "unit \"%s\" not recognized for type %s", f, sqlname(vt));
            }
            bool nok = false;
            Numeric *n = num_from_str(t, &nok);
            free(t); free(f);
            if (!nok) ereport(s, "22023", "invalid extract result");
            return d_num(n);
        }
#endif
        double d = dt_extract(f, vt, v.v.i, vt == INTERVALOID ? &iv : NULL, &ok);
        if (!ok) ereport(s, "22023", "unit \"%s\" not recognized for type %s", f, sqlname(vt));
        free(f);
        return d_float(FLOAT8OID, d);
    }
    if (nargs == 2 && EQI("to_date")) {
        if (args[0].isnull || args[1].isnull) return d_null(DATEOID);
        char *str = as_cstr(s, args[0]), *fmt = as_cstr(s, args[1]);
        int32 day; const char *err = NULL;
        if (!dt_to_date(str, fmt, &day, &err)) to_datetime_error(s, err, str);
        /* to_date() drops the zone but still rejects an invalid one */
        if (fmt_has_tz(fmt)) (void)fmt_tz_offset(s, str, fmt);
        free(str); free(fmt);
        return d_int(DATEOID, day);
    }
    if (EQI("to_timestamp")) {
        if (nargs == 1) {
            if (args[0].isnull) return d_null(TIMESTAMPTZOID);
            double secs = as_f8(s, args[0]);
            return d_int(TIMESTAMPTZOID, (int64)llround((secs - 946684800.0) * 1000000.0));
        }
        if (nargs == 2) {
            if (args[0].isnull || args[1].isnull) return d_null(TIMESTAMPTZOID);
            char *str = as_cstr(s, args[0]), *fmt = as_cstr(s, args[1]);
            int64 ts; const char *err = NULL;
            if (!dt_to_timestamp(str, fmt, &ts, &err)) to_datetime_error(s, err, str);
            if (fmt_has_tz(fmt)) ts -= (int64)fmt_tz_offset(s, str, fmt) * 1000000;
            free(str); free(fmt);
            return d_int(TIMESTAMPTZOID, ts);
        }
        NOTFOUND();
    }
    if (nargs == 2 && EQI("to_char")) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *fmt = as_cstr(s, args[1]);
        Datum v = args[0];
        char *r;
        if (v.type == INTERVALOID) {
            PgInterval iv = v.v.iv;
            int64 t = iv.usec;
            int h = (int)(t / USECS_PER_HOUR);
            int mi = (int)((t / USECS_PER_MIN) % 60);
            int sec = (int)((t / USECS_PER_SEC) % 60);
            int fs = (int)(t % USECS_PER_SEC);
            int y = iv.month / 12, mo = iv.month % 12 + 1;
            r = fmt_datetime(fmt, y, mo, iv.day, h, mi, sec, fs, 0, false);
        } else if (f_is_numfam(v.type)) {
            /* minimal numeric to_char: 9 and 0 digit templates */
            char *t = as_cstr(s, v);
            int width = 0;
            for (char *q = fmt; *q; q++) if (*q == '9' || *q == '0') width++;
            Buf b; buf_init(&b);
            int len = (int)strlen(t);
            for (int i = len; i < width + 1; i++) buf_addc(&b, ' ');
            buf_adds(&b, t);
            free(t);
            r = buf_steal(&b);
        } else {
            if (v.type == DATEOID) v = coerce_to(s, v, TIMESTAMPOID, -1, 1);
            if (v.type != TIMESTAMPOID && v.type != TIMESTAMPTZOID)
                v = coerce_to(s, v, TIMESTAMPOID, -1, 1);
            int y,mo,d,h,mi,sec,fs;
            ts_to_parts(v.v.i, &y,&mo,&d,&h,&mi,&sec,&fs);
            int32 day; int64 tod;
            ts_split(v.v.i, &day, &tod);
            r = fmt_datetime(fmt, y, mo, d, h, mi, sec, fs, day, true);
        }
        free(fmt);
        Datum res = d_text(TEXTOID, r);
        free(r);
        return res;
    }
    if (nargs == 1 && EQI("isfinite")) {
        if (args[0].isnull) return d_null(BOOLOID);
        return d_bool(true);
    }
    if (nargs == 1 && (EQI("justify_days") || EQI("justify_hours") || EQI("justify_interval"))) {
        if (args[0].isnull) return d_null(INTERVALOID);
        PgInterval v = args[0].type == INTERVALOID ? args[0].v.iv
                       : coerce_to(s, args[0], INTERVALOID, -1, 1).v.iv;
        if (EQI("justify_days")) return d_interval(iv_justify_days(v));
        if (EQI("justify_hours")) return d_interval(iv_justify_hours(v));
        return d_interval(iv_justify_interval(v));
    }
    if (nargs == 3 && EQI("make_date")) {
        for (int i = 0; i < 3; i++) if (args[i].isnull) return d_null(DATEOID);
        int y = as_i4(s, args[0]), mo = as_i4(s, args[1]), d = as_i4(s, args[2]);
        if (y == 0) ereport(s, "22007", "date field value out of range: %d-%02d-%02d", y, mo, d);
        if (mo < 1 || mo > 12 || d < 1 || d > f_mdays(y, mo < 1 ? 1 : mo > 12 ? 12 : mo))
            ereport(s, "22008", "date field value out of range: %d-%02d-%02d", y, mo, d);
        return d_int(DATEOID, dt_ymd_to_date(y, mo, d));
    }
    if (nargs == 3 && EQI("make_time")) {
        for (int i = 0; i < 3; i++) if (args[i].isnull) return d_null(TIMEOID);
        int h = as_i4(s, args[0]), mi = as_i4(s, args[1]);
        double sec = as_f8(s, args[2]);
        if (h < 0 || h > 24 || mi < 0 || mi > 59 || sec < 0 || sec >= 60.0)
            ereport(s, "22008", "time field value out of range: %d:%02d:%02g", h, mi, sec);
        return d_int(TIMEOID, (int64)h * USECS_PER_HOUR + (int64)mi * USECS_PER_MIN +
                              (int64)llround(sec * 1000000.0));
    }
    if ((nargs == 6 || nargs == 7) && (EQI("make_timestamp") || EQI("make_timestamptz"))) {
        for (int i = 0; i < 6; i++) if (args[i].isnull)
            return d_null(EQI("make_timestamp") ? TIMESTAMPOID : TIMESTAMPTZOID);
        int y = as_i4(s, args[0]), mo = as_i4(s, args[1]), d = as_i4(s, args[2]);
        int h = as_i4(s, args[3]), mi = as_i4(s, args[4]);
        double sec = as_f8(s, args[5]);
        if (y == 0 || mo < 1 || mo > 12 || d < 1 || d > f_mdays(y, mo < 1 ? 1 : mo > 12 ? 12 : mo))
            ereport(s, "22008", "date field value out of range: %d-%02d-%02d", y, mo, d);
        if (h < 0 || mi < 0 || mi > 59 || sec < 0 || sec > 60.0 || h > 24 ||
            (h == 24 && (mi > 0 || sec > 0)))
            ereport(s, "22008", "time field value out of range: %d:%02d:%02g", h, mi, sec);
        int64 us = parts_to_ts(y, mo, d, h, mi, 0, 0) + (int64)llround(sec * 1000000.0);
        return d_int(EQI("make_timestamp") ? TIMESTAMPOID : TIMESTAMPTZOID, us);
    }
    if (nargs >= 1 && nargs <= 7 && EQI("make_interval")) {
        PgInterval v; memset(&v, 0, sizeof v);
        int64 yy = nargs > 0 && !args[0].isnull ? as_i8(s, args[0]) : 0;
        int64 mm = nargs > 1 && !args[1].isnull ? as_i8(s, args[1]) : 0;
        int64 ww = nargs > 2 && !args[2].isnull ? as_i8(s, args[2]) : 0;
        int64 dd = nargs > 3 && !args[3].isnull ? as_i8(s, args[3]) : 0;
        int64 hh = nargs > 4 && !args[4].isnull ? as_i8(s, args[4]) : 0;
        int64 mi = nargs > 5 && !args[5].isnull ? as_i8(s, args[5]) : 0;
        double ss = nargs > 6 && !args[6].isnull ? as_f8(s, args[6]) : 0;
        v.month = (int32)(yy * 12 + mm);
        v.day = (int32)(ww * 7 + dd);
        v.usec = hh * USECS_PER_HOUR + mi * USECS_PER_MIN + (int64)llround(ss * 1000000.0);
        return d_interval(v);
    }

    /* --------------------------------------------------------------- arrays */
    if (nargs == 2 && (EQI("array_length") || EQI("array_upper") || EQI("array_lower"))) {
        if (args[0].isnull || args[1].isnull) return d_null(INT4OID);
        ArrayVal *a = args[0].v.arr;
        int dim = as_i4(s, args[1]);
        if (!a || a->ndim == 0 || dim < 1 || dim > a->ndim) return d_null(INT4OID);
        if (EQI("array_length")) return d_int(INT4OID, a->dims[dim-1]);
        if (EQI("array_lower")) return d_int(INT4OID, a->lbound[dim-1]);
        return d_int(INT4OID, a->lbound[dim-1] + a->dims[dim-1] - 1);
    }
    if (nargs == 1 && EQI("array_ndims")) {
        if (args[0].isnull) return d_null(INT4OID);
        ArrayVal *a = args[0].v.arr;
        if (!a || a->ndim == 0) return d_null(INT4OID);
        return d_int(INT4OID, a->ndim);
    }
    if (nargs == 1 && EQI("array_dims")) {
        if (args[0].isnull) return d_null(TEXTOID);
        ArrayVal *a = args[0].v.arr;
        if (!a || a->ndim == 0) return d_null(TEXTOID);
        Buf b; buf_init(&b);
        for (int i = 0; i < a->ndim; i++)
            buf_printf(&b, "[%d:%d]", a->lbound[i], a->lbound[i] + a->dims[i] - 1);
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs == 1 && EQI("cardinality")) {
        if (args[0].isnull) return d_null(INT4OID);
        return d_int(INT4OID, arr_count(args[0]));
    }
    if (nargs == 2 && (EQI("array_append") || EQI("array_prepend"))) {
        bool app = EQI("array_append");
        Datum ad = app ? args[0] : args[1];
        Datum ed = app ? args[1] : args[0];
        Oid et = ad.isnull ? (ed.isnull ? TEXTOID : ed.type) : arr_elemtype(ad);
        ArrayVal *r = arr_new(et);
        if (!app) arr_push(r, d_copy(ed));
        if (!ad.isnull && ad.v.arr)
            for (int i = 0; i < ad.v.arr->nelem; i++) arr_push(r, ad.v.arr->elems[i]);
        if (app) arr_push(r, d_copy(ed));
        return d_arr(array_type_of(et) ? array_type_of(et) : TEXTARRAYOID, r);
    }
    if (nargs == 2 && EQI("array_cat")) {
        if (args[0].isnull) return args[1];
        if (args[1].isnull) return args[0];
        Oid et = arr_elemtype(args[0]);
        if (!et) et = arr_elemtype(args[1]);
        ArrayVal *r = arr_new(et);
        for (int k = 0; k < 2; k++)
            if (args[k].v.arr)
                for (int i = 0; i < args[k].v.arr->nelem; i++) arr_push(r, args[k].v.arr->elems[i]);
        return d_arr(args[0].type, r);
    }
    if (nargs == 2 && EQI("array_remove")) {
        if (args[0].isnull) return d_null(args[0].type);
        ArrayVal *a = args[0].v.arr;
        ArrayVal *r = arr_new(arr_elemtype(args[0]));
        for (int i = 0; a && i < a->nelem; i++) {
            if (args[1].isnull) { if (a->elems[i].isnull) continue; }
            else if (!a->elems[i].isnull && d_cmp(s, a->elems[i], args[1]) == 0) continue;
            arr_push(r, a->elems[i]);
        }
        return d_arr(args[0].type, r);
    }
    if (nargs == 3 && EQI("array_replace")) {
        if (args[0].isnull) return d_null(args[0].type);
        ArrayVal *a = args[0].v.arr;
        ArrayVal *r = arr_new(arr_elemtype(args[0]));
        for (int i = 0; a && i < a->nelem; i++) {
            bool hit = args[1].isnull ? a->elems[i].isnull
                     : (!a->elems[i].isnull && d_cmp(s, a->elems[i], args[1]) == 0);
            arr_push(r, hit ? d_copy(args[2]) : a->elems[i]);
        }
        ArrayVal *sv = args[0].v.arr;
        r->ndim = sv->ndim;
        for (int i = 0; i < sv->ndim; i++) { r->dims[i] = sv->dims[i]; r->lbound[i] = sv->lbound[i]; }
        return d_arr(args[0].type, r);
    }
    if ((nargs == 2 || nargs == 3) && EQI("array_position")) {
        if (args[0].isnull) return d_null(INT4OID);
        ArrayVal *a = args[0].v.arr;
        int from = (nargs == 3 && !args[2].isnull) ? as_i4(s, args[2]) : 1;
        for (int i = from - 1; a && i < a->nelem; i++) {
            if (args[1].isnull) { if (a->elems[i].isnull) return d_int(INT4OID, i + 1); continue; }
            if (!a->elems[i].isnull && d_cmp(s, a->elems[i], args[1]) == 0)
                return d_int(INT4OID, i + 1);
        }
        return d_null(INT4OID);
    }
    if (nargs == 2 && EQI("array_positions")) {
        if (args[0].isnull) return d_null(INT4ARRAYOID);
        ArrayVal *a = args[0].v.arr;
        ArrayVal *r = arr_new(INT4OID);
        for (int i = 0; a && i < a->nelem; i++) {
            bool hit = args[1].isnull ? a->elems[i].isnull
                     : (!a->elems[i].isnull && d_cmp(s, a->elems[i], args[1]) == 0);
            if (hit) arr_push(r, d_int(INT4OID, i + 1));
        }
        return d_arr(INT4ARRAYOID, r);
    }
    if ((nargs == 2 || nargs == 3) && EQI("array_to_string")) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        char *sep = as_cstr(s, args[1]);
        char *nul = (nargs == 3 && !args[2].isnull) ? as_cstr(s, args[2]) : NULL;
        ArrayVal *a = args[0].v.arr;
        Buf b; buf_init(&b);
        bool first = true;
        for (int i = 0; a && i < a->nelem; i++) {
            if (a->elems[i].isnull) {
                if (!nul) continue;
                if (!first) buf_adds(&b, sep);
                buf_adds(&b, nul);
                first = false;
                continue;
            }
            if (!first) buf_adds(&b, sep);
            char *t = out_func(s, a->elems[i]);
            buf_adds(&b, t); free(t);
            first = false;
        }
        free(sep); free(nul);
        return d_textbuf(TEXTOID, &b);
    }
    if ((nargs == 2 || nargs == 3) && EQI("string_to_table")) {
#if PGVC_VN < 140
        NOTFOUND();
#else
        Datum r = call_builtin(s, "string_to_array", args, nargs, found);
        return r;
#endif
    }
    if ((nargs == 2 || nargs == 3) && EQI("string_to_array")) {
        if (args[0].isnull) return d_null(TEXTARRAYOID);
        char *o; int32 n;
        const char *t = as_bytes(s, args[0], &n, &o);
        char *nul = (nargs == 3 && !args[2].isnull) ? as_cstr(s, args[2]) : NULL;
        ArrayVal *a = arr_new(TEXTOID);
        if (args[1].isnull) {
            for (int32 i = 0; i < n; ) {
                int cl = u8_charlen((unsigned char)t[i]);
                Datum e = d_textn(TEXTOID, t + i, cl);
                arr_push(a, (nul && !strncmp(t + i, nul, (size_t)cl) && (int)strlen(nul) == cl)
                            ? d_null(TEXTOID) : e);
                i += cl;
            }
        } else {
            char *ox; int32 nx;
            const char *dl = as_bytes(s, args[1], &nx, &ox);
            if (n == 0) { free(ox); free(o); free(nul); return d_arr(TEXTARRAYOID, a); }
            if (nx == 0) {
                Datum e = d_textn(TEXTOID, t, n);
                arr_push(a, e);
            } else {
                int32 i = 0;
                while (1) {
                    int32 k = bytes_find(t, n, dl, nx, i);
                    int32 end = (k < 0) ? n : k;
                    Datum e = d_textn(TEXTOID, t + i, end - i);
                    if (nul && e.v.s.len == (int32)strlen(nul) &&
                        !memcmp(e.v.s.p, nul, (size_t)e.v.s.len)) e = d_null(TEXTOID);
                    arr_push(a, e);
                    if (k < 0) break;
                    i = k + nx;
                }
            }
            free(ox);
        }
        free(o); free(nul);
        return d_arr(TEXTARRAYOID, a);
    }
    if ((nargs == 2 || nargs == 3) && EQI("array_fill")) {
        if (args[1].isnull) return d_null(TEXTARRAYOID);
        Oid et = args[0].isnull ? TEXTOID : args[0].type;
        ArrayVal *dims = args[1].v.arr;
        ArrayVal *r = arr_new(et);
        int total = 1;
        r->ndim = dims ? dims->nelem : 0;
        for (int i = 0; i < r->ndim; i++) {
            r->dims[i] = as_i4(s, dims->elems[i]);
            r->lbound[i] = 1;
            total *= r->dims[i];
        }
        if (nargs == 3 && !args[2].isnull && args[2].v.arr)
            for (int i = 0; i < r->ndim && i < args[2].v.arr->nelem; i++)
                r->lbound[i] = as_i4(s, args[2].v.arr->elems[i]);
        r->elems = xmalloc((size_t)(total ? total : 1) * sizeof(Datum));
        for (int i = 0; i < total; i++) r->elems[i] = d_copy(args[0]);
        r->nelem = total;
        return d_arr(array_type_of(et) ? array_type_of(et) : TEXTARRAYOID, r);
    }
    if (nargs == 1 && EQI("unnest")) {
        if (args[0].isnull) return d_null(args[0].type);
        if (f_is_mrange(args[0].type)) {
            MRangeVal *m = args[0].v.mrng;
            ArrayVal *r = arr_new(m->rngtype);
            for (int i = 0; i < m->n; i++) arr_push(r, d_range(m->rngtype, m->ranges[i]));
            return d_arr(RECORDARRAYOID, r);
        }
        return args[0];
    }
#if PGVC_VN >= 160
    /* array_shuffle and array_sample permute or take the FIRST dimension and
     * leave the inner dimensions alone, so a 3x2 array stays 3x2 (or Kx2). */
    if ((nargs == 1 && EQI("array_shuffle")) || (nargs == 2 && EQI("array_sample"))) {
        bool sample = (nargs == 2);
        if (args[0].isnull || (sample && args[1].isnull)) return d_null(args[0].type);
        ArrayVal *a = args[0].v.arr;
        int nd = a ? a->ndim : 0;
        int outer = (nd > 0) ? a->dims[0] : 0;
        int stride = 1;
        for (int i = 1; i < nd; i++) stride *= a->dims[i];
        int k = outer;
        if (sample) {
            k = as_i4(s, args[1]);
            if (k < 0 || k > outer)
                ereport(s, "22023", "sample size must be between 0 and %d", outer);
        }
        int *ord = xmalloc((size_t)(outer ? outer : 1) * sizeof(int));
        for (int i = 0; i < outer; i++) ord[i] = i;
        for (int i = outer - 1; i > 0; i--) {
            int j = (int)(pg_random() * (i + 1));
            if (j > i) j = i;
            int t = ord[i]; ord[i] = ord[j]; ord[j] = t;
        }
        ArrayVal *r = arr_new(arr_elemtype(args[0]));
        for (int i = 0; i < k; i++)
            for (int q = 0; q < stride; q++) arr_push(r, a->elems[ord[i] * stride + q]);
        free(ord);
        if (nd > 1) {
            r->ndim = nd;
            r->dims[0] = k; r->lbound[0] = 1;
            for (int i = 1; i < nd; i++) { r->dims[i] = a->dims[i]; r->lbound[i] = a->lbound[i]; }
            if (k == 0) r->ndim = 0;
        }
        return d_arr(args[0].type, r);
    }
    if (nargs == 1 && (EQI("erf") || EQI("erfc"))) {
        if (args[0].isnull) return d_null(FLOAT8OID);
        double v = as_f8(s, args[0]);
        return d_float(FLOAT8OID, EQI("erf") ? erf(v) : erfc(v));
    }
    if ((nargs == 0 || nargs == 2) && EQI("random_normal")) {
        double mean = 0, sd = 1;
        if (nargs == 2) {
            if (args[0].isnull || args[1].isnull) return d_null(FLOAT8OID);
            mean = as_f8(s, args[0]); sd = as_f8(s, args[1]);
        }
        if (sd == 0) return d_float(FLOAT8OID, mean);
        /* Box-Muller, the same shape PostgreSQL's pg_prng_double_normal uses */
        double u1, u2;
        do { u1 = pg_random(); } while (u1 <= 0.0);
        u2 = pg_random();
        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
        return d_float(FLOAT8OID, mean + sd * z);
    }
#endif

    /* ----------------------------------------------------------- json / jsonb */
    if (nargs == 1 && (EQI("to_json") || EQI("to_jsonb"))) {
        bool jb = EQI("to_jsonb");
        JN *n;
        {
            Datum d = args[0];
            if (d.isnull) n = jn_null();
            else if (d.type == JSONBOID || d.type == JSONOID) n = jn_of(s, d);
            else if (d.type == BOOLOID) n = jn_bool(d.v.i != 0);
            else if (f_is_numfam(d.type)) {
                if (f_is_float(d.type) && (isnan(d.v.f) || isinf(d.v.f)))
                    ereport(s, "22P02", "cannot convert infinity to jsonb");
                char *t = out_func(s, d);
                n = jn_num(t); free(t);
            } else if (type_is_array(d.type)) {
                n = jn_arr();
                ArrayVal *a = d.v.arr;
                for (int i = 0; a && i < a->nelem; i++) {
                    Datum e = a->elems[i];
                    if (e.isnull) { jn_push(n, jn_null()); continue; }
                    if (f_is_numfam(e.type)) { char *t = out_func(s, e); jn_push(n, jn_num(t)); free(t); }
                    else if (e.type == BOOLOID) jn_push(n, jn_bool(e.v.i != 0));
                    else if (e.type == JSONBOID || e.type == JSONOID) jn_push(n, jn_of(s, e));
                    else { char *t = out_func(s, e); jn_push(n, jn_str(t)); free(t); }
                }
            } else if (d.type == RECORDOID) {
                n = jn_obj();
                RecordVal *r = d.v.rec;
                for (int i = 0; r && i < r->n; i++) {
                    char nm[32];
                    const char *k = (r->names && r->names[i]) ? r->names[i]
                                  : (snprintf(nm, sizeof nm, "f%d", i+1), nm);
                    Datum e = r->vals[i];
                    if (e.isnull) jn_put(n, k, jn_null());
                    else if (f_is_numfam(e.type)) { char *t = out_func(s, e); jn_put(n, k, jn_num(t)); free(t); }
                    else if (e.type == BOOLOID) jn_put(n, k, jn_bool(e.v.i != 0));
                    else { char *t = out_func(s, e); jn_put(n, k, jn_str(t)); free(t); }
                }
            } else {
                char *t = out_func(s, d);
                n = jn_str(t); free(t);
            }
        }
        if (jb) return d_jsonb_node(s, n);
        char *t = jn_text(n, 0);
        Datum r = d_text(JSONOID, t);
        free(t);
        return r;
    }
    if (nargs == 1 && (EQI("row_to_json") || EQI("array_to_json"))) {
        Datum a[1] = { args[0] };
        bool f2;
        return call_builtin(s, "to_json", a, 1, &f2);
    }
    if (EQI("json_build_object") || EQI("jsonb_build_object")) {
        if (nargs % 2) ereport(s, "22023", "argument list must have even number of elements");
        bool jb = EQI("jsonb_build_object");
        JN *o = jn_obj();
        for (int i = 0; i < nargs; i += 2) {
            if (args[i].isnull) ereport(s, "22004", "argument %d cannot be null", i+1);
            char *k = as_cstr(s, args[i]);
            Datum vv[1] = { args[i+1] };
            bool f2;
            Datum jv = call_builtin(s, "to_jsonb", vv, 1, &f2);
            JN *n = jv.isnull ? jn_null() : jn_of(s, jv);
            jn_put(o, k, n ? n : jn_null());
            free(k);
        }
        if (jb) return d_jsonb_node(s, o);
        char *t = jn_text(o, 2);
        Datum r = d_text(JSONOID, t); free(t);
        return r;
    }
    if (EQI("json_build_array") || EQI("jsonb_build_array")) {
        bool jb = EQI("jsonb_build_array");
        JN *a = jn_arr();
        for (int i = 0; i < nargs; i++) {
            Datum vv[1] = { args[i] };
            bool f2;
            Datum jv = call_builtin(s, "to_jsonb", vv, 1, &f2);
            JN *n = jv.isnull ? jn_null() : jn_of(s, jv);
            jn_push(a, n ? n : jn_null());
        }
        if (jb) return d_jsonb_node(s, a);
        char *t = jn_text(a, 1);
        Datum r = d_text(JSONOID, t); free(t);
        return r;
    }
    if ((nargs == 1 || nargs == 2) && (EQI("json_object") || EQI("jsonb_object"))) {
        bool jb = EQI("jsonb_object");
        Datum a0 = coerce_to(s, args[0], TEXTARRAYOID, -1, 1);
        JN *o = jn_obj();
        if (nargs == 1) {
            ArrayVal *a = a0.v.arr;
            if (a && a->ndim == 2 && a->dims[1] == 2) { /* n x 2 form */ }
            if (a && (a->nelem % 2))
                ereport(s, "22023", "array must have even number of elements");
            for (int i = 0; a && i + 1 < a->nelem; i += 2) {
                char *k = as_cstr(s, a->elems[i]);
                jn_put(o, k, a->elems[i+1].isnull ? jn_null()
                              : ({ char *v = as_cstr(s, a->elems[i+1]); JN *x = jn_str(v); free(v); x; }));
                free(k);
            }
        } else {
            Datum a1 = coerce_to(s, args[1], TEXTARRAYOID, -1, 1);
            ArrayVal *ka = a0.v.arr, *va = a1.v.arr;
            if (!ka || !va || ka->nelem != va->nelem)
                ereport(s, "22023", "mismatched array dimensions");
            for (int i = 0; i < ka->nelem; i++) {
                char *k = as_cstr(s, ka->elems[i]);
                jn_put(o, k, va->elems[i].isnull ? jn_null()
                              : ({ char *v = as_cstr(s, va->elems[i]); JN *x = jn_str(v); free(v); x; }));
                free(k);
            }
        }
        if (jb) return d_jsonb_node(s, o);
        char *t = jn_text(o, 2);
        Datum r = d_text(JSONOID, t); free(t);
        return r;
    }
    if (nargs == 1 && EQI("jsonb_pretty")) {
        if (args[0].isnull) return d_null(TEXTOID);
        Datum j = args[0].type == JSONBOID ? args[0] : coerce_to(s, args[0], JSONBOID, -1, 1);
        char *t = json_pretty(j.v.jb);
        Datum r = d_text(TEXTOID, t); free(t);
        return r;
    }
#if PGVC_VN >= 130
    if (nargs >= 3 && nargs <= 5 && EQI("jsonb_set_lax")) {
        /* jsonb_set_lax decides what an SQL NULL new value means before it
         * hands the work to jsonb_set. */
        if (args[0].isnull || args[1].isnull || (nargs >= 4 && args[3].isnull))
            return d_null(JSONBOID);
        if (nargs == 5 && args[4].isnull)
            ereport(s, "22023", "null_value_treatment must be \"delete_key\", "
                    "\"return_target\", \"use_json_null\", or \"raise_exception\"");
        if (!args[2].isnull) {
            Datum sub[4];
            sub[0] = args[0]; sub[1] = args[1]; sub[2] = args[2];
            if (nargs >= 4) sub[3] = args[3];
            return call_builtin(s, "jsonb_set", sub, nargs >= 4 ? 4 : 3, found);
        }
        char *mode = (nargs == 5) ? as_cstr(s, args[4]) : xstrdup("use_json_null");
        if (!strcmp(mode, "raise_exception")) {
            err_detail(s, "Exception was raised because null_value_treatment is "
                          "\"raise_exception\".");
            err_hint(s, "To avoid, either change the null_value_treatment argument "
                        "or ensure that an SQL NULL is not passed.");
            ereport(s, "22004", "JSON value must not be null");
        }
        if (!strcmp(mode, "return_target")) {
            free(mode);
            return d_jsonb_node(s, jn_copy(jn_of(s, args[0])));
        }
        if (!strcmp(mode, "delete_key")) {
            free(mode);
            bool h = false;
            Datum sub[2];
            sub[0] = coerce_to(s, args[0], JSONBOID, -1, 1);
            sub[1] = args[1];
            return jsonb_op(s, "#-", sub, 2, found, &h);
        }
        if (strcmp(mode, "use_json_null")) {
            free(mode);
            ereport(s, "22023", "null_value_treatment must be \"delete_key\", "
                    "\"return_target\", \"use_json_null\", or \"raise_exception\"");
        }
        free(mode);
        Datum sub[4];
        sub[0] = args[0]; sub[1] = args[1];
        sub[2] = coerce_to(s, d_text(TEXTOID, "null"), JSONBOID, -1, 1);
        if (nargs >= 4) sub[3] = args[3];
        return call_builtin(s, "jsonb_set", sub, nargs >= 4 ? 4 : 3, found);
    }
#endif
    if ((nargs == 3 || nargs == 4) && (EQI("jsonb_set") || EQI("jsonb_insert"))) {
        for (int i = 0; i < 3; i++) if (args[i].isnull) return d_null(JSONBOID);
        JN *doc = jn_of(s, args[0]);
        Datum pd = coerce_to(s, args[1], TEXTARRAYOID, -1, 1);
        ArrayVal *path = pd.v.arr;
        JN *nv = jn_of(s, args[2]);
        bool flag = (nargs == 4 && !args[3].isnull) ? as_bool(s, args[3])
                                                    : EQI("jsonb_insert") ? false : true;
        bool insert = EQI("jsonb_insert");
        JN *cur = doc;
        int np = path ? path->nelem : 0;
        if (np == 0) return d_jsonb_node(s, doc);
        for (int i = 0; i < np - 1 && cur; i++) {
            char *k = as_cstr(s, path->elems[i]);
            JN *nx = NULL;
            if (cur->kind == 5) nx = jn_get(cur, k);
            else if (cur->kind == 4) {
                long idx = strtol(k, NULL, 10);
                if (idx < 0) idx += cur->n;
                if (idx >= 0 && idx < cur->n) nx = cur->el[idx];
            }
            free(k);
            cur = nx;
        }
        if (!cur) return d_jsonb_node(s, doc);
        {
            char *k = as_cstr(s, path->elems[np-1]);
            if (cur->kind == 5) {
                JN *ex = jn_get(cur, k);
                if (insert) { if (!ex) jn_put(cur, k, nv); }
                else if (ex || flag) jn_put(cur, k, nv);
            } else if (cur->kind == 4) {
                long idx = strtol(k, NULL, 10);
                if (idx < 0) idx += cur->n;
                if (insert) {
                    long at = flag ? idx + 1 : idx;
                    if (at < 0) at = 0;
                    if (at > cur->n) at = cur->n;
                    jn_push(cur, NULL);
                    for (int j = cur->n - 1; j > at; j--) cur->el[j] = cur->el[j-1];
                    cur->el[at] = nv;
                } else if (idx >= 0 && idx < cur->n) cur->el[idx] = nv;
                else if (flag) jn_push(cur, nv);
            }
            free(k);
        }
        return d_jsonb_node(s, doc);
    }
    if ((nargs == 1
#if PGVC_VN >= 180
         || nargs == 2
#endif
        ) && (EQI("jsonb_strip_nulls") || EQI("json_strip_nulls"))) {
        if (args[0].isnull) return d_null(args[0].type);
        bool striparr = (nargs == 2 && !args[1].isnull && as_bool(s, args[1]));
        JN *doc = jn_of(s, args[0]);
        if (striparr) {
            JN *st2[64]; int sp2 = 0;
            st2[sp2++] = doc;
            while (sp2) {
                JN *v = st2[--sp2];
                if (v->kind == 4) {
                    int w = 0;
                    for (int i = 0; i < v->n; i++) {
                        if (v->el[i]->kind == 0) continue;
                        v->el[w++] = v->el[i];
                        if (sp2 < 64) st2[sp2++] = v->el[w-1];
                    }
                    v->n = w;
                } else if (v->kind == 5) {
                    for (int i = 0; i < v->n; i++) if (sp2 < 64) st2[sp2++] = v->vals[i];
                }
            }
        }
        /* recursive strip */
        JN *stack[64]; int sp = 0;
        stack[sp++] = doc;
        while (sp) {
            JN *v = stack[--sp];
            if (v->kind == 5) {
                int w = 0;
                for (int i = 0; i < v->n; i++) {
                    if (v->vals[i]->kind == 0) continue;
                    v->keys[w] = v->keys[i]; v->vals[w] = v->vals[i]; w++;
                }
                v->n = w;
                for (int i = 0; i < v->n && sp < 63; i++) stack[sp++] = v->vals[i];
            } else if (v->kind == 4)
                for (int i = 0; i < v->n && sp < 63; i++) stack[sp++] = v->el[i];
        }
        if (EQI("json_strip_nulls")) {
            char *t = jn_text(doc, 0);
            Datum r = d_text(JSONOID, t); free(t);
            return r;
        }
        return d_jsonb_node(s, doc);
    }
    if (nargs == 1 && (EQI("json_typeof") || EQI("jsonb_typeof"))) {
        if (args[0].isnull) return d_null(TEXTOID);
        JN *v = jn_of(s, args[0]);
        return d_text(TEXTOID, jn_typename(v));
    }
    if (nargs == 1 && (EQI("json_array_length") || EQI("jsonb_array_length"))) {
        if (args[0].isnull) return d_null(INT4OID);
        JN *v = jn_of(s, args[0]);
        if (v->kind != 4) ereport(s, "22023", "cannot get array length of a non-array");
        return d_int(INT4OID, v->n);
    }
    if (nargs >= 2 && (EQI("json_extract_path") || EQI("jsonb_extract_path") ||
                       EQI("json_extract_path_text") || EQI("jsonb_extract_path_text"))) {
        bool astext = (strstr(name, "_text") != NULL);
        bool jb = (name[0] == 'j' && name[1] == 's' && name[2] == 'o' && name[3] == 'n' &&
                   name[4] == 'b');
        if (args[0].isnull) return d_null(astext ? TEXTOID : (jb ? JSONBOID : JSONOID));
        JN *v = jn_of(s, args[0]);
        for (int i = 1; i < nargs && v; i++) {
            if (args[i].isnull) { v = NULL; break; }
            char *k = as_cstr(s, args[i]);
            if (v->kind == 5) v = jn_get(v, k);
            else if (v->kind == 4) {
                char *e; long idx = strtol(k, &e, 10);
                if (*e) v = NULL;
                else { if (idx < 0) idx += v->n;
                       v = (idx >= 0 && idx < v->n) ? v->el[idx] : NULL; }
            } else v = NULL;
            free(k);
        }
        if (!v) return d_null(astext ? TEXTOID : (jb ? JSONBOID : JSONOID));
        if (astext) {
            char *t = jn_astext(v);
            if (!t) return d_null(TEXTOID);
            Datum r = d_text(TEXTOID, t); free(t);
            return r;
        }
        if (jb) return d_jsonb_node(s, v);
        char *t = jn_text(v, 0);
        Datum r = d_text(JSONOID, t); free(t);
        return r;
    }
    if (nargs == 1 && (EQI("json_object_keys") || EQI("jsonb_object_keys"))) {
        if (args[0].isnull) return d_null(TEXTARRAYOID);
        JsonVal *v = jv_of(s, args[0]);
        if (json_kind(v) != 5) {
            JN *n = jn_of(s, args[0]);
            ereport(s, "22023", "cannot call %s on a %s", name, jn_typename(n));
        }
        ArrayVal *a = arr_new(TEXTOID);
        int n = json_length(v);
        for (int i = 0; i < n; i++) arr_push(a, d_text(TEXTOID, json_key_at(v, i)));
        return d_arr(TEXTARRAYOID, a);
    }
    if (nargs == 1 && (EQI("json_each") || EQI("jsonb_each") ||
                       EQI("json_each_text") || EQI("jsonb_each_text"))) {
        if (args[0].isnull) return d_null(RECORDARRAYOID);
        bool astext = (strstr(name, "_text") != NULL);
        bool jb = !strncmp(name, "jsonb", 5);
        JsonVal *v = jv_of(s, args[0]);
        if (json_kind(v) != 5) {
            JN *n = jn_of(s, args[0]);
            ereport(s, "22023", "cannot call %s on a %s", name, jn_typename(n));
        }
        ArrayVal *a = arr_new(RECORDOID);
        int np = json_length(v);
        for (int i = 0; i < np; i++) {
            JsonVal *val = json_val_at(v, i);
            RecordVal *r = xmalloc(sizeof *r);
            r->n = 2;
            r->vals = xmalloc(2 * sizeof(Datum));
            r->names = xmalloc(2 * sizeof(char *));
            r->names[0] = xstrdup("key"); r->names[1] = xstrdup("value");
            r->vals[0] = d_text(TEXTOID, json_key_at(v, i));
            if (astext) {
                char *tx = jv_astext(val);
                r->vals[1] = tx ? d_text(TEXTOID, tx) : d_null(TEXTOID);
                free(tx);
            } else if (jb) r->vals[1] = d_jsonb_val(json_copy(val));
            else { char *tx = json_to_text(val, false);
                   r->vals[1] = d_text(JSONOID, tx); free(tx); }
            Datum rd; memset(&rd, 0, sizeof rd);
            rd.type = RECORDOID; rd.v.rec = r;
            arr_push(a, rd);
        }
        return d_arr(RECORDARRAYOID, a);
    }
    if (nargs == 1 && (EQI("json_array_elements") || EQI("jsonb_array_elements") ||
                       EQI("json_array_elements_text") || EQI("jsonb_array_elements_text"))) {
        if (args[0].isnull) return d_null(RECORDARRAYOID);
        bool astext = (strstr(name, "_text") != NULL);
        bool jb = !strncmp(name, "jsonb", 5);
        JN *v = jn_of(s, args[0]);
        if (v->kind != 4) ereport(s, "22023", "cannot extract elements from a %s", jn_typename(v));
        ArrayVal *a = arr_new(astext ? TEXTOID : (jb ? JSONBOID : JSONOID));
        for (int i = 0; i < v->n; i++) {
            if (astext) {
                char *t = jn_astext(v->el[i]);
                arr_push(a, t ? d_text(TEXTOID, t) : d_null(TEXTOID));
                free(t);
            } else if (jb) arr_push(a, d_jsonb_node(s, v->el[i]));
            else { char *t = jn_text(v->el[i], 0); arr_push(a, d_text(JSONOID, t)); free(t); }
        }
        return d_arr(astext ? TEXTARRAYOID : (jb ? JSONBARRAYOID : JSONARRAYOID), a);
    }
    if (nargs == 2 && (EQI("jsonb_contains") || EQI("jsonb_contained"))) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        Datum a = coerce_to(s, args[0], JSONBOID, -1, 1);
        Datum b = coerce_to(s, args[1], JSONBOID, -1, 1);
        return d_bool(EQI("jsonb_contains") ? json_contains(a.v.jb, b.v.jb)
                                            : json_contains(b.v.jb, a.v.jb));
    }
    if (nargs == 2 && EQI("jsonb_exists")) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        JN *v = jn_of(s, args[0]);
        char *k = as_cstr(s, args[1]);
        bool r = false;
        if (v->kind == 5) r = jn_get(v, k) != NULL;
        else if (v->kind == 4) {
            for (int i = 0; i < v->n; i++)
                if (v->el[i]->kind == 3 && !strcmp(v->el[i]->str, k)) { r = true; break; }
        } else if (v->kind == 3) r = !strcmp(v->str, k);
        free(k);
        return d_bool(r);
    }
#if PGVC_VN >= 120
    {   /* the jsonb_path_* family: see jsonpath.c */
        bool jf = false;
        Datum jr = jsonpath_builtin(s, name, args, nargs, &jf);
        if (jf) return jr;
    }
#endif

    /* ------------------------------------------------------ ranges / multiranges */
    if ((nargs == 2 || nargs == 3) &&
        (EQI("int4range") || EQI("int8range") || EQI("numrange") ||
         EQI("daterange") || EQI("tsrange") || EQI("tstzrange"))) {
        Oid rt = EQI("int4range") ? INT4RANGEOID : EQI("int8range") ? INT8RANGEOID :
                 EQI("numrange") ? NUMRANGEOID : EQI("daterange") ? DATERANGEOID :
                 EQI("tsrange") ? TSRANGEOID : TSTZRANGEOID;
        char *fl = NULL;
        if (nargs == 3) {
            if (args[2].isnull) ereport(s, "22000", "range constructor flags argument must not be null");
            fl = as_cstr(s, args[2]);
        }
        RangeVal *r = rv_make(s, rt, args[0], args[1], fl ? fl : "[)");
        free(fl);
        return d_range(rt, r);
    }
#if PGVC_VN >= 140
    if (EQI("int4multirange") || EQI("int8multirange") || EQI("nummultirange") ||
        EQI("datemultirange") || EQI("tsmultirange") || EQI("tstzmultirange") ||
        EQI("multirange")) {
        Oid mt;
        if (EQI("multirange")) {
            if (nargs < 1 || !f_is_range(args[0].type)) NOTFOUND();
            mt = f_mrange_of(args[0].type);
        } else
            mt = EQI("int4multirange") ? INT4MULTIRANGEOID :
                 EQI("int8multirange") ? INT8MULTIRANGEOID :
                 EQI("nummultirange")  ? NUMMULTIRANGEOID  :
                 EQI("datemultirange") ? DATEMULTIRANGEOID :
                 EQI("tsmultirange")   ? TSMULTIRANGEOID   : TSTZMULTIRANGEOID;
        MRangeVal *m = mr_new(mt);
        /* the constructor takes only its own range type */
        for (int i = 0; i < nargs; i++)
            if (f_is_range(args[i].type) && args[i].type != m->rngtype) NOTFOUND();
        for (int i = 0; i < nargs; i++) {
            if (args[i].isnull) continue;
            Datum r = args[i].type == m->rngtype ? args[i]
                    : coerce_to(s, args[i], m->rngtype, -1, 1);
            mr_push(m, r.v.rng);
        }
        mr_normalize(s, m);
        return d_mrange(mt, m);
    }
#endif
    if (nargs == 1 && (EQI("lower") || EQI("upper") || EQI("isempty") ||
                       EQI("lower_inc") || EQI("upper_inc") ||
                       EQI("lower_inf") || EQI("upper_inf"))) {
        Datum a = args[0];
        RangeVal *r = NULL;
        RangeVal tmp;
        if (a.isnull) return d_null(BOOLOID);
        if (f_is_range(a.type)) r = a.v.rng;
        else if (f_is_mrange(a.type)) {
            MRangeVal *m = a.v.mrng;
            if (m->n == 0) { memset(&tmp, 0, sizeof tmp); tmp.empty = true;
                             tmp.rngtype = m->rngtype; tmp.subtype = f_range_subtype(m->rngtype);
                             r = &tmp; }
            else {
                memset(&tmp, 0, sizeof tmp);
                tmp.rngtype = m->rngtype; tmp.subtype = f_range_subtype(m->rngtype);
                RangeVal *lo = m->ranges[0], *hi = m->ranges[m->n-1];
                tmp.lower_inf = lo->lower_inf; tmp.lower_inc = lo->lower_inc; tmp.lower = lo->lower;
                tmp.upper_inf = hi->upper_inf; tmp.upper_inc = hi->upper_inc; tmp.upper = hi->upper;
                r = &tmp;
            }
        } else NOTFOUND();
        if (EQI("isempty")) return d_bool(r->empty);
        if (EQI("lower_inc")) return d_bool(!r->empty && r->lower_inc);
        if (EQI("upper_inc")) return d_bool(!r->empty && r->upper_inc);
        if (EQI("lower_inf")) return d_bool(!r->empty && r->lower_inf);
        if (EQI("upper_inf")) return d_bool(!r->empty && r->upper_inf);
        Oid sub = f_range_subtype(r->rngtype ? r->rngtype : a.type);
        if (r->empty) return d_null(sub);
        if (EQI("lower")) return r->lower_inf ? d_null(sub) : r->lower;
        return r->upper_inf ? d_null(sub) : r->upper;
    }
    if ((nargs == 1 || nargs == 2) && EQI("range_merge")) {
        if (nargs == 2) {
            if (args[0].isnull || args[1].isnull) return d_null(args[0].type);
            return d_range(args[0].type, rng_merge(s, args[0].v.rng, args[1].v.rng));
        }
        if (args[0].isnull) return d_null(args[0].type);
        if (f_is_mrange(args[0].type)) {
            MRangeVal *m = args[0].v.mrng;
            if (m->n == 0) return d_range(m->rngtype, rv_empty(m->rngtype));
            RangeVal *r = m->ranges[0];
            for (int i = 1; i < m->n; i++) r = rng_merge(s, r, m->ranges[i]);
            return d_range(m->rngtype, r);
        }
        NOTFOUND();
    }

    /* ------------------------------------------------------- type and system */
    if (nargs == 1 && EQI("scale") && args[0].type == NUMERICOID) {
        if (args[0].isnull) return d_null(INT4OID);
        return d_int(INT4OID, num_scale(args[0].v.num));
    }
#if PGVC_VN >= 130
    if (nargs == 1 && EQI("min_scale") && f_is_numfam(args[0].type)
        && !f_is_float(args[0].type)) {
        if (args[0].isnull) return d_null(INT4OID);
        Numeric *n = as_num(s, args[0]);
        int ms = num_display_min_scale(n);
        if (ms < 0) return d_null(INT4OID);      /* NaN and the infinities */
        return d_int(INT4OID, ms);
    }
    if (nargs == 1 && EQI("trim_scale") && f_is_numfam(args[0].type)
        && !f_is_float(args[0].type)) {
        if (args[0].isnull) return d_null(NUMERICOID);
        Numeric *n = as_num(s, args[0]);
        int ms = num_display_min_scale(n);
        if (ms < 0) return d_num(num_copy(n));
        return d_num(num_round(n, ms));
    }
#endif
#if PGVC_VN >= 130
    if (nargs == 1 && (EQI("pg_snapshot_xmin") || EQI("pg_snapshot_xmax"))) {
        if (args[0].isnull) return d_null(XID8OID);
        Datum sn = coerce_to(s, args[0], PGSNAPSHOTOID, -1, 1);
        uint64 xmin = 0, xmax = 0, xip[64]; int nx = 0;
        snapshot_parts(sn.v.s.p, &xmin, &xmax, xip, 64, &nx);
        return d_int(XID8OID, (int64)(EQI("pg_snapshot_xmin") ? xmin : xmax));
    }
    if (nargs == 2 && EQI("pg_visible_in_snapshot")) {
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        Datum xv = coerce_to(s, args[0], XID8OID, -1, 1);
        Datum sn = coerce_to(s, args[1], PGSNAPSHOTOID, -1, 1);
        uint64 v = (uint64)xv.v.i, xmin = 0, xmax = 0, xip[256]; int nx = 0;
        snapshot_parts(sn.v.s.p, &xmin, &xmax, xip, 256, &nx);
        if (v < xmin) return d_bool(true);
        if (v >= xmax) return d_bool(false);
        for (int i = 0; i < nx && i < 256; i++) if (xip[i] == v) return d_bool(false);
        return d_bool(true);
    }
    if (nargs == 0 && (EQI("pg_current_xact_id") || EQI("pg_current_xact_id_if_assigned")))
        return d_int(XID8OID, (int64)(s ? s->xid : 0));
#endif
#if PGVC_VN >= 130
    if ((nargs == 1 || nargs == 2) && EQI("normalize")) {
        if (!f_is_str(args[0].type)) NOTFOUND();
        if (args[0].isnull) return d_null(TEXTOID);
        int form = (nargs == 2 && !args[1].isnull) ? (int)as_i8(s, args[1]) : 0;
        char *r = unicode_normalize(args[0].v.s.p, (size_t)args[0].v.s.len, form);
        Datum d = d_text(TEXTOID, r);
        free(r);
        return d;
    }
    if ((nargs == 1 || nargs == 2) && EQI("is_normalized")) {
        if (!f_is_str(args[0].type)) NOTFOUND();
        if (args[0].isnull) return d_null(BOOLOID);
        int form = (nargs == 2 && !args[1].isnull) ? (int)as_i8(s, args[1]) : 0;
        return d_bool(unicode_is_normalized(args[0].v.s.p, (size_t)args[0].v.s.len, form));
    }
#endif
    if (nargs == 2 && EQI("convert_to")) {
        if (args[0].isnull || args[1].isnull) return d_null(BYTEAOID);
        char *t = as_cstr(s, args[0]);
        Datum d = d_text(BYTEAOID, t);
        d.v.s.len = (int32)strlen(t);
        return d;
    }
    if (nargs == 2 && EQI("convert_from")) {
        if (args[0].isnull || args[1].isnull) return d_null(TEXTOID);
        Datum d = args[0];
        d.type = TEXTOID;
        return d;
    }
#if PGVC_VN >= 140
    /* The assignment form of jsonb subscripting.  args are the document, the
     * path as a text array and the new value; missing object levels are
     * created and an array is extended with nulls. */
    if (nargs == 3 && EQ("__jsonb_subscript_set")) {
        JN *doc = args[0].isnull ? jn_obj() : jn_copy(jn_of(s, args[0]));
        ArrayVal *path = args[1].v.arr;
        int np = path ? path->nelem : 0;
        JN *nv = args[2].isnull ? jn_new(0) : jn_of(s, args[2]);
        JN *cur = doc;
        for (int i = 0; i < np; i++) {
            char *k = as_cstr(s, path->elems[i]);
            bool last = (i == np - 1);
            if (cur->kind == 4) {
                char *end = NULL;
                long idx = strtol(k, &end, 10);
                if (!*k || (end && *end)) {
                    free(k);
                    ereport(s, "22P02", "path element at position %d is not an integer: \"%s\"",
                            i + 1, as_cstr(s, path->elems[i]));
                }
                if (idx < 0) idx += cur->n;
                if (idx < 0) idx = 0;
                while (cur->n <= idx) jn_push(cur, jn_new(0));
                if (last) cur->el[idx] = nv;
                else {
                    if (cur->el[idx]->kind != 5 && cur->el[idx]->kind != 4)
                        cur->el[idx] = jn_obj();
                    cur = cur->el[idx];
                }
                free(k);
                continue;
            }
            if (cur->kind != 5) { free(k); break; }
            if (last) jn_put(cur, k, nv);
            else {
                JN *nx = jn_get(cur, k);
                if (!nx || (nx->kind != 5 && nx->kind != 4)) { nx = jn_obj(); jn_put(cur, k, nx); }
                cur = nx;
            }
            free(k);
        }
        return d_jsonb_node(s, doc);
    }
    if (nargs == 1 && EQI("bit_count")) {
        if (args[0].isnull) return d_null(INT8OID);
        if (args[0].type != BYTEAOID && !f_is_str(args[0].type)) NOTFOUND();
        int64 n = 0;
        if (args[0].type == BYTEAOID) {
            for (int i = 0; i < args[0].v.s.len; i++) {
                unsigned char c = (unsigned char)args[0].v.s.p[i];
                while (c) { n += c & 1; c >>= 1; }
            }
        } else {
            /* a bit string is carried as its text of '0' and '1' */
            for (int i = 0; i < args[0].v.s.len; i++)
                if (args[0].v.s.p[i] == '1') n++;
        }
        return d_int(INT8OID, n);
    }
    if (nargs == 2 && EQI("trim_array")) {
        if (args[0].isnull || args[1].isnull) return d_null(args[0].type);
        if (!type_is_array(args[0].type)) NOTFOUND();
        ArrayVal *a = args[0].v.arr;
        int have = a ? a->nelem : 0;
        int64 k = as_i8(s, args[1]);
        if (k < 0 || k > have)
            ereport(s, "2202E", "number of elements to trim must be between 0 and %d", have);
        ArrayVal *r = arr_new(arr_elemtype(args[0]));
        for (int i = 0; i < have - (int)k; i++) arr_push(r, a->elems[i]);
        return d_arr(args[0].type, r);
    }
    if (nargs == 1 && EQI("unistr")) {
        if (args[0].isnull) return d_null(TEXTOID);
        if (!f_is_str(args[0].type)) NOTFOUND();
        const char *t = args[0].v.s.p;
        int len = args[0].v.s.len;
        Buf b; buf_init(&b);
        for (int i = 0; i < len; ) {
            if (t[i] != '\\') { buf_addc(&b, t[i]); i++; continue; }
            i++;
            if (i < len && t[i] == '\\') { buf_addc(&b, '\\'); i++; continue; }
            int nd = 4; bool plus = false;
            if (i < len && t[i] == '+') { nd = 6; plus = true; i++; }
            else if (i < len && t[i] == 'u') { nd = 4; i++; }
            else if (i < len && t[i] == 'U') { nd = 8; i++; }
            (void) plus;
            unsigned long cp = 0; int got = 0;
            while (got < nd && i < len && isxdigit((unsigned char)t[i])) {
                char ch = t[i];
                cp = cp * 16 + (unsigned long)(isdigit((unsigned char)ch) ? ch - '0'
                     : (tolower((unsigned char)ch) - 'a' + 10));
                i++; got++;
            }
            if (got != nd) {
                buf_free(&b);
                err_hint(s, "Unicode escapes must be \\XXXX, \\+XXXXXX, \\uXXXX, "
                            "or \\UXXXXXXXX.");
                ereport(s, "22P02", "invalid Unicode escape");
            }
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                buf_free(&b);
                ereport(s, "22P02", "invalid Unicode code point: %lX", cp);
            }
            if (cp < 0x80) buf_addc(&b, (char)cp);
            else if (cp < 0x800) { buf_addc(&b, (char)(0xC0 | (cp >> 6)));
                                   buf_addc(&b, (char)(0x80 | (cp & 0x3F))); }
            else if (cp < 0x10000) { buf_addc(&b, (char)(0xE0 | (cp >> 12)));
                                     buf_addc(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                                     buf_addc(&b, (char)(0x80 | (cp & 0x3F))); }
            else { buf_addc(&b, (char)(0xF0 | (cp >> 18)));
                   buf_addc(&b, (char)(0x80 | ((cp >> 12) & 0x3F)));
                   buf_addc(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                   buf_addc(&b, (char)(0x80 | (cp & 0x3F))); }
        }
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs == 3 && EQI("date_bin")) {
        for (int i = 0; i < 3; i++) if (args[i].isnull) return d_null(TIMESTAMPOID);
        PgInterval iv = coerce_to(s, args[0], INTERVALOID, -1, 1).v.iv;
        if (iv.month) ereport(s, "22008", "timestamps cannot be binned into intervals "
                                          "containing months or years");
        int64 stride = iv.usec + (int64)iv.day * 86400000000LL;
        if (stride <= 0) ereport(s, "22023", "stride must be greater than zero");
        Oid rt = (args[1].type == TIMESTAMPTZOID) ? TIMESTAMPTZOID : TIMESTAMPOID;
        int64 v = coerce_to(s, args[1], rt, -1, 1).v.i;
        int64 org = coerce_to(s, args[2], rt, -1, 1).v.i;
        int64 delta = v - org;
        int64 rem = delta % stride;
        if (rem < 0) rem += stride;
        return d_int(rt, v - rem);
    }
#endif
    if (nargs == 1 && EQI("pg_typeof")) {
        char *t = format_type_str(args[0].type == UNKNOWNOID ? UNKNOWNOID : args[0].type, -1);
        extern Datum reg_make(Session *s, Oid regtype, int64 oid, const char *name);
        Datum r = reg_make(s, REGTYPEOID, (int64)args[0].type, t);
        free(t);
        return r;
    }
    if (nargs == 1 && (EQI("to_hex")
#if PGVC_VN >= 170
                       || EQI("to_bin") || EQI("to_oct")
#endif
                       )) {
        if (args[0].isnull) return d_null(TEXTOID);
        Oid at = args[0].type == UNKNOWNOID ? INT4OID : args[0].type;
        if (at != INT4OID && at != INT8OID) { *found = false; return d_null(TEXTOID); }
        uint64 v = (at == INT8OID) ? (uint64)as_i8(s, args[0])
                                   : (uint64)(uint32)(int32)as_i8(s, args[0]);
        int base = EQI("to_hex") ? 16 : EQI("to_oct") ? 8 : 2;
        char buf[70];
        int n = 0;
        if (v == 0) buf[n++] = '0';
        while (v) { int d = (int)(v % (uint64)base); buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
                    v /= (uint64)base; }
        char out[70];
        for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
        out[n] = 0;
        return d_text(TEXTOID, out);
    }
#if PGVC_VN >= 170
    if (nargs == 1 && EQI("to_regtypemod")) {
        if (args[0].isnull) return d_null(INT4OID);
        char *t = as_cstr(s, args[0]);
        Oid ty = 0; int32 tm = -1;
        bool ok = parse_type_from_text(s, t, &ty, &tm, NULL);
        free(t);
        if (!ok || !ty) return d_null(INT4OID);
        return d_int(INT4OID, tm);
    }
    if (nargs == 1 && EQI("unicode_assigned")) {
        if (args[0].isnull) return d_null(BOOLOID);
        /* every code point this server can hold is an assigned one: the input
         * functions already reject malformed UTF-8 */
        return d_bool(true);
    }
    if (nargs == 0 && (EQI("unicode_version") || EQI("icu_unicode_version")))
        return d_text(TEXTOID, "15.1");
    if (nargs == 1 && EQI("pg_column_toast_chunk_id")) {
        /* nothing this server stores is out of line, so there is no chunk id */
        return d_null(OIDOID);
    }
    if (nargs == 1 && (EQI("uuid_extract_version") || EQI("uuid_extract_timestamp"))) {
        if (args[0].isnull)
            return d_null(EQI("uuid_extract_version") ? INT2OID : TIMESTAMPTZOID);
        Datum u = coerce_to(s, args[0], UUIDOID, -1, 1);
        const char *h = u.v.s.p;
        int nibbles[32]; int nn = 0;
        for (int32 i = 0; i < u.v.s.len && nn < 32; i++) {
            char c = h[i];
            if (c == '-') continue;
            nibbles[nn++] = isdigit((unsigned char)c) ? c - '0'
                                                      : tolower((unsigned char)c) - 'a' + 10;
        }
        if (nn < 32) return d_null(EQI("uuid_extract_version") ? INT2OID : TIMESTAMPTZOID);
        int ver = nibbles[12];
        if (EQI("uuid_extract_version")) {
            if (nibbles[16] < 8 || nibbles[16] > 11) return d_null(INT2OID);
            return d_int(INT2OID, ver);
        }
        if (ver == 1) {
            /* 60-bit count of 100ns intervals since 1582-10-15 */
            uint64 ts = 0;
            static const int order[15] = {13,14,15,8,9,10,11,0,1,2,3,4,5,6,7};
            for (int i = 0; i < 15; i++) ts = (ts << 4) | (uint64)nibbles[order[i]];
            /* 1582-10-15 to 2000-01-01 is 13165977600 seconds */
            int64 usec = (int64)(ts / 10) - 13165977600LL * 1000000LL;
            return d_int(TIMESTAMPTZOID, usec);
        }
#if PGVC_VN >= 180
        if (ver == 7) {
            uint64 ms = 0;
            for (int i = 0; i < 12; i++) ms = (ms << 4) | (uint64)nibbles[i];
            return d_int(TIMESTAMPTZOID, (int64)ms * 1000LL - 946684800LL * 1000000LL);
        }
#endif
        return d_null(TIMESTAMPTZOID);
    }
    if (nargs == 1 && EQI("pg_basetype")) {
        if (args[0].isnull) return d_null(REGTYPEOID);
        Oid t = (Oid)args[0].v.i;
        int32 tm = -1;
        Oid base = domain_basetype(t, &tm);
        extern Datum reg_make(Session *s, Oid regtype, int64 oid, const char *name);
        return reg_make(s, REGTYPEOID, base, NULL);
    }
    if (nargs == 0 && EQI("pg_sync_replication_slots"))
        ereport(s, "55000", "replication slots can only be synchronized to a standby server");
#endif
    if ((nargs == 1 || nargs == 2) && EQI("timezone")) {
        /* AT TIME ZONE, and AT LOCAL when the zone comes from the setting */
        Datum v = args[nargs - 1];
        PgZone z;
        if (nargs == 2) {
            if (args[0].isnull) return d_null(v.type == TIMESTAMPOID ? TIMESTAMPTZOID
                                                                     : TIMESTAMPOID);
            if (args[0].type == INTERVALOID) {
                /* an interval zone is an ISO-convention fixed offset */
                memset(&z, 0, sizeof z); z.isfixed = true;
                z.fixed = (int)(args[0].v.iv.usec / 1000000) + args[0].v.iv.day * 86400;
            } else {
                char *zn = as_cstr(s, args[0]);
                if (!pgzone_resolve(zn, &z))
                    ereport(s, "22023", "time zone \"%s\" not recognized", zn);
                free(zn);
            }
        } else {
            const char *zn = guc_get(s, "TimeZone");
            if (!zn || !pgzone_resolve(zn, &z))
                ereport(s, "22023", "time zone \"%s\" not recognized", zn ? zn : "");
        }
        Oid vt = v.type == UNKNOWNOID ? TIMESTAMPTZOID : v.type;
        if (vt != TIMESTAMPOID && vt != TIMESTAMPTZOID) {
            Datum c = coerce_to(s, v, TIMESTAMPTZOID, -1, 1);
            v = c; vt = TIMESTAMPTZOID;
        }
        if (v.isnull) return d_null(vt == TIMESTAMPOID ? TIMESTAMPTZOID : TIMESTAMPOID);
        if (vt == TIMESTAMPTZOID)
            return d_int(TIMESTAMPOID, v.v.i + (int64)tz_offset_utc(&z, v.v.i) * 1000000);
        return d_int(TIMESTAMPTZOID, v.v.i - (int64)tz_offset_local(&z, v.v.i) * 1000000);
    }
    if ((nargs == 1 || nargs == 2) && EQI("format_type")) {
        if (args[0].isnull) return d_null(TEXTOID);
        Oid t = (Oid)as_i8(s, args[0]);
        int32 tm = (nargs == 2 && !args[1].isnull) ? (int32)as_i8(s, args[1]) : -1;
        char *x = format_type_str(t, tm);
        Datum r = d_text(TEXTOID, x);
        free(x);
        return r;
    }
    if (nargs == 0 && EQI("version")) {
        Buf b; buf_init(&b);
        buf_printf(&b, "PostgreSQL %s on x86_64-pc-linux-gnu, compiled by gcc, 64-bit",
                   PGVC_VERSION_TEXT);
        return d_textbuf(TEXTOID, &b);
    }
    if (nargs == 0 && (EQI("current_database") || EQI("current_catalog")))
        return d_text(NAMEOID, s && s->dbname ? s->dbname : "postgres");
    if (nargs == 0 && EQI("current_schema")) return d_text(NAMEOID, "public");
    /* session_user is always the login role, whatever SET ROLE did */
    if (nargs == 0 && EQI("session_user"))
        return d_text(NAMEOID, s && s->user ? s->user : "postgres");
#if PGVC_VN >= 160
    /* system_user is "auth_method:identity", and NULL when the connection was
     * not authenticated (trust). */
    if (nargs == 0 && EQI("system_user")) return d_null(TEXTOID);
#endif
    /* the current-user family follows SET ROLE */
    if (nargs == 0 && (EQI("current_user") || EQI("user") || EQI("current_role")))
        return d_text(NAMEOID, session_role(s));
    if (nargs == 0 && EQI("current_schemas")) {
        ArrayVal *a = arr_new(NAMEOID);
        arr_push(a, d_text(NAMEOID, "public"));
        return d_arr(NAMEARRAYOID, a);
    }
    if (nargs == 1 && EQI("current_schemas")) {
        ArrayVal *a = arr_new(NAMEOID);
        if (!args[0].isnull && as_bool(s, args[0])) arr_push(a, d_text(NAMEOID, "pg_catalog"));
        arr_push(a, d_text(NAMEOID, "public"));
        return d_arr(NAMEARRAYOID, a);
    }
    if ((nargs == 1 || nargs == 2) && EQI("current_setting")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *n = as_cstr(s, args[0]);
        if (!strcmp(n, "role")) { free(n); return d_text(TEXTOID, s && s->role ? s->role : "none"); }
        extern char *guc_show(Session *s, const char *name);
        char *shown = guc_show(s, n);
        const char *v = shown ? shown : guc_get(s, n);
        if (!v && !strcmp(n, "server_version")) v = PGVC_VERSION_TEXT;
        if (!v && !strcmp(n, "server_version_num")) v = PGVC_VERSION_NUM;
        if (!v) {
            bool missing_ok = (nargs == 2 && !args[1].isnull && as_bool(s, args[1]));
            if (missing_ok) { free(n); return d_null(TEXTOID); }
            ereport(s, "42704", "unrecognized configuration parameter \"%s\"", n);
        }
        free(n);
        return d_text(TEXTOID, v);
    }
    if (nargs == 3 && EQI("set_config")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *n = as_cstr(s, args[0]);
        char *v = args[1].isnull ? xstrdup("") : as_cstr(s, args[1]);
        guc_set(s, n, v);
        free(n);
        Datum r = d_text(TEXTOID, v);
        free(v);
        return r;
    }
    if (nargs == 0 && EQI("pg_backend_pid")) return d_int(INT4OID, s ? (int64)s->pid : (int64)getpid());
    if (nargs == 2 && EQI("nullif")) {
        if (args[0].isnull) return args[0];
        if (args[1].isnull) return args[0];
        Datum b = args[1];
        if (args[0].type != b.type && b.type == UNKNOWNOID) b = coerce_to(s, b, args[0].type, -1, 1);
        return d_cmp(s, args[0], b) == 0 ? d_null(args[0].type) : args[0];
    }
    if (nargs >= 1 && EQI("coalesce")) {
        for (int i = 0; i < nargs; i++) if (!args[i].isnull) return args[i];
        return d_null(args[0].type);
    }
    if (nargs >= 1 && (EQI("greatest") || EQI("least"))) {
        bool gt = EQI("greatest");
        Datum best = d_null(args[0].type);
        bool have = false;
        for (int i = 0; i < nargs; i++) {
            if (args[i].isnull) continue;
            if (!have) { best = args[i]; have = true; continue; }
            int c = d_cmp(s, args[i], best);
            if (gt ? (c > 0) : (c < 0)) best = args[i];
        }
        return best;
    }
#if PGVC_VN >= 160
    if (nargs == 1 && EQI("any_value")) return args[0];
#endif
    if (nargs == 1 && EQI("pg_size_pretty")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *t = size_pretty(as_i8(s, args[0]));
        Datum r = d_text(TEXTOID, t);
        free(t);
        return r;
    }
    if (nargs == 1 && EQI("pg_column_size")) {
        if (args[0].isnull) return d_null(INT4OID);
        Datum d = args[0];
        int sz;
        switch (d.type) {
        case BOOLOID: case CHAROID: sz = 1; break;
        case INT2OID: sz = 2; break;
        case INT4OID: case FLOAT4OID: case DATEOID: case OIDOID: sz = 4; break;
        case INT8OID: case FLOAT8OID: case TIMEOID: case TIMESTAMPOID:
        case TIMESTAMPTZOID: case MONEYOID: sz = 8; break;
        case INTERVALOID: sz = 16; break;
        case UUIDOID: sz = 16; break;
        case NAMEOID: sz = 64; break;
        case NUMERICOID: {
            char *t = out_func(s, d);
            int digits = 0;
            for (char *q = t; *q; q++) if (isdigit((unsigned char)*q)) digits++;
            free(t);
            sz = 8 + ((digits + 3) / 4) * 2;
            break;
        }
        case JSONBOID: { char *t = json_to_text(d.v.jb, true); sz = (int)strlen(t) + 4; free(t); break; }
        default: {
            char *o; int32 n;
            (void)as_bytes(s, d, &n, &o);
            free(o);
            sz = n + 4;
            break;
        }
        }
        return d_int(INT4OID, sz);
    }
    if (nargs >= 1 && nargs <= 2 && EQI("has_function_privilege")) {
        /* the function has to exist */
        char *sig = as_cstr(s, args[0]);
        char *paren = strchr(sig, '(');
        if (paren) {
            char *nm = xstrndup(sig, (size_t)(paren - sig));
            bool found = false;
            int nb = 0;
            const ProcDef *pd = builtin_procs(&nb);
            for (int i = 0; i < nb; i++) {
                if (pd[i].minvn && PGVC_VN < pd[i].minvn) continue;
                if (pd[i].maxvn && PGVC_VN > pd[i].maxvn) continue;
                if (!strcmp(pd[i].name, nm)) found = true;
            }
            if (s && s->db)
                for (int i = 0; i < s->db->nroutines; i++)
                    if (!strcmp(s->db->routines[i]->name, nm)) found = true;
            free(nm);
            if (!found) ereport(s, "42883", "function \"%s\" does not exist", sig);
        }
        free(sig);
        return d_bool(true);
    }
    if (nargs >= 1 && nargs <= 2 &&
        (EQI("has_table_privilege") ||
         EQI("has_database_privilege") || EQI("has_column_privilege") ||
         EQI("has_sequence_privilege")))
        return d_bool(true);
    if ((nargs == 2 || nargs == 3) && EQI("has_schema_privilege")) {
        for (int i = 0; i < nargs; i++) if (args[i].isnull) return d_null(BOOLOID);
        char *ro = nargs == 3 ? as_cstr(s, args[0]) : xstrdup(session_role(s));
        char *sn = as_cstr(s, args[nargs - 2]);
        char *pv = as_cstr(s, args[nargs - 1]);
        if (strcmp(ro, "public") && !role_find(ro))
            { ereport(s, "42704", "role \"%s\" does not exist", ro); }
        Schema *sc = s && s->db ? sch_find(s->db, sn) : NULL;
        if (!sc) ereport(s, "3F000", "schema \"%s\" does not exist", sn);
        char want = 0;
        for (char *q = pv; *q; q++) *q = (char)toupper((unsigned char)*q);
        if (strstr(pv, "CREATE")) want = 'C';
        else if (strstr(pv, "USAGE")) want = 'U';
        else ereport(s, "22023", "unrecognized privilege type: \"%s\"", pv);
        Datum r = d_bool(schema_priv(s, sc, ro, want));
        free(ro); free(sn); free(pv);
        return r;
    }
    if (nargs == 3 && (EQI("has_table_privilege") ||
                       EQI("has_column_privilege") || EQI("has_database_privilege")))
        return d_bool(true);
    if (nargs == 1 && (EQI("to_regprocedure") || EQI("to_regproc"))) {
        if (args[0].isnull) return d_null(REGPROCOID);
        char *sig = as_cstr(s, args[0]);
        char *paren = strchr(sig, '(');
        char *nm = paren ? xstrndup(sig, (size_t)(paren - sig)) : xstrdup(sig);
        bool found = false;
        int nb = 0;
        const ProcDef *pd = builtin_procs(&nb);
        for (int i = 0; i < nb; i++) {
            if (pd[i].minvn && PGVC_VN < pd[i].minvn) continue;
            if (pd[i].maxvn && PGVC_VN > pd[i].maxvn) continue;
            if (!strcmp(pd[i].name, nm)) found = true;
        }
        if (s && s->db)
            for (int i = 0; i < s->db->nroutines; i++)
                if (!strcmp(s->db->routines[i]->name, nm)) found = true;
        Datum r = found ? reg_make(s, REGPROCOID, 0, sig) : d_null(REGPROCOID);
        free(nm); free(sig);
        return r;
    }
    if (nargs == 1 && (EQI("pg_table_is_visible") || EQI("pg_type_is_visible") ||
                       EQI("pg_function_is_visible")))
        return d_bool(true);
    if (nargs == 2 && EQI("obj_description")) return d_null(TEXTOID);
    if (nargs == 1 && EQI("obj_description")) return d_null(TEXTOID);
    if (nargs == 2 && EQI("col_description")) return d_null(TEXTOID);
    if (nargs == 2 && EQI("shobj_description")) return d_null(TEXTOID);
    if (nargs == 1 && EQI("pg_get_userbyid")) {
        if (args[0].isnull) return d_null(NAMEOID);
        Oid id = (Oid)coerce_to(s, args[0], OIDOID, -1, 1).v.i;
        RoleEntry *re = role_by_oid(id);
        if (re) return d_text(NAMEOID, re->name);
        char buf[32];
        snprintf(buf, sizeof buf, "unknown (OID=%u)", id);
        return d_text(NAMEOID, buf);
    }
    /* relpartbound and friends are stored as their source text here, so
     * pg_get_expr only has to hand it back. */
    if ((nargs == 2 || nargs == 3) && EQI("pg_get_expr")) {
        if (args[0].isnull) return d_null(TEXTOID);
        char *t = out_func(s, args[0]);
        Datum r = d_text(TEXTOID, t); free(t);
        return r;
    }
#if PGVC_VN >= 120
    if (nargs == 3 && EQI("date_trunc")) {
        /* date_trunc(field, timestamptz, zone) truncates the local time in
         * `zone` and maps the result back to an instant. */
        if (args[2].isnull) return d_null(TIMESTAMPTZOID);
        char *zn = as_cstr(s, args[2]);
        /* An IANA zone name is read from the operating system's tz files; a
         * fixed offset such as "+05:30" is not one, so fall back to the
         * built-in zone table, which also accepts a numeric displacement. */
        PgZone fz;
        bool iana = tz_known(zn);
        bool fixed = !iana && pgzone_resolve(zn, &fz);
        if (!iana && !fixed)
            ereport(s, "22023", "time zone \"%s\" not recognized", zn);
        if (args[0].isnull || args[1].isnull) { free(zn); return d_null(TIMESTAMPTZOID); }
        Datum v = args[1];
        if (v.type != TIMESTAMPTZOID) v = coerce_to(s, v, TIMESTAMPTZOID, -1, 1);
        int64 ts = v.v.i;
        int off = 0;
        if (iana) tz_offset_at(zn, ts / 1000000 + 946684800LL, &off, NULL, 0, NULL);
        else off = tz_offset_utc(&fz, ts);
        char *f = as_cstr(s, args[0]);
        bool ok = true;
        int64 cut = ts_trunc(s, f, ts + (int64)off * 1000000, &ok);
        if (!ok) {
            char *bad = f;
            free(zn);
            ereport(s, "22023", "unit \"%s\" not recognized for type %s",
                    bad, "timestamp with time zone");
        }
        free(f);
        /* Keep any sub-second part: zone offsets are whole seconds. */
        int64 secs = cut / 1000000, rem = cut - secs * 1000000;
        if (rem < 0) { secs -= 1; rem += 1000000; }
        int64 utc = secs + 946684800LL;
        if (iana) { if (!tz_local_to_utc(zn, utc, &utc)) utc = secs + 946684800LL; }
        else utc = secs + 946684800LL - tz_offset_local(&fz, cut);
        free(zn);
        return d_int(TIMESTAMPTZOID, (utc - 946684800LL) * 1000000 + rem);
    }
#endif
#if PGVC_VN >= 100
    if (nargs == 1 && EQI("pg_get_statisticsobjdef")) {
        if (args[0].isnull || !s || !s->db) return d_null(TEXTOID);
        Oid oid = (Oid)as_i8(s, args[0]);
        ExtStat *e = NULL;
        for (int i = 0; i < s->db->nstats; i++)
            if (s->db->stats[i]->oid == oid) e = s->db->stats[i];
        if (!e) return d_null(TEXTOID);
        Table *t = tbl_find_oid(s->db, e->table);
        Buf b; buf_init(&b);
        buf_printf(&b, "CREATE STATISTICS %s.%s", e->schema, e->name);
        bool d1 = strchr(e->kinds, 'd') != NULL;
        bool f1 = strchr(e->kinds, 'f') != NULL;
        bool m1 = strchr(e->kinds, 'm') != NULL;
        bool e1 = strchr(e->kinds, 'e') != NULL;
        bool all = d1 && f1 && (PGVC_VN < 120 || m1);
        /* an expression-only object lists no kinds at all */
        if (e1 && !d1 && !f1 && !m1) all = true;
        if (!all) {
            buf_adds(&b, " (");
            bool got = false;
            if (d1) { buf_adds(&b, "ndistinct"); got = true; }
            if (f1) { buf_printf(&b, "%sdependencies", got ? ", " : ""); got = true; }
            if (m1) { buf_printf(&b, "%smcv", got ? ", " : ""); }
            buf_addc(&b, ')');
        }
        buf_adds(&b, " ON ");
        int printed = 0;
        for (int i = 0; i < e->nkeys; i++) {
            if (printed) buf_adds(&b, ", ");
            if (t) buf_adds(&b, t->cols[e->keys[i]-1].name);
            printed++;
        }
        for (int i = 0; i < e->nexprs; i++) {
            if (printed) buf_adds(&b, ", ");
            char *d2 = deparse_expr_src(s, e->exprs[i], NULL, false);
            const char *x = d2 ? d2 : e->exprs[i];
            if (x[0] == '(') buf_adds(&b, x);
            else buf_printf(&b, "(%s)", x);
            free(d2);
            printed++;
        }
        buf_printf(&b, " FROM %s", t ? t->name : "?");
        return d_textbuf(TEXTOID, &b);
    }
#endif
    if ((nargs == 1 || nargs == 2) && EQI("pg_get_viewdef")) {
        if (args[0].isnull || !s || !s->db) return d_null(TEXTOID);
        Table *t = NULL;
        if (args[0].type == TEXTOID || args[0].type == UNKNOWNOID ||
            args[0].type == VARCHAROID || args[0].type == NAMEOID) {
            char *nm = as_cstr(s, args[0]);
            t = tbl_find(s->db, NULL, nm);
            free(nm);
        } else {
            Datum rc = coerce_to(s, args[0], REGCLASSOID, -1, 2);
            t = tbl_find_oid(s->db, (Oid)rc.v.i);
        }
        if (!t || (t->relkind != 'v' && t->relkind != 'm')) return d_null(TEXTOID);
        bool pretty = false;
        if (nargs == 2 && !args[1].isnull) {
            if (args[1].type == INT4OID || args[1].type == INT8OID)
                pretty = true;                 /* the wrap-column form */
            else pretty = as_bool(s, args[1]);
        }
        char *v = deparse_view(s, t, pretty);
        if (!v) {
            /* a shape the tree printer does not model falls back to the text
             * re-flow, which is what renders the SQL/JSON constructors */
            extern char *view_def_text(Session *s, const char *src, bool pretty);
            v = view_def_text(s, t->viewdef, pretty);
            if (!v) return d_text(TEXTOID, t->viewdef ? t->viewdef : "");
        }
        Datum r = d_text(TEXTOID, v);
        free(v);
        return r;
    }
    if (nargs == 1 && (EQI("pg_get_function_arguments") ||
                       EQI("pg_get_function_identity_arguments") ||
                       EQI("pg_get_function_result"))) {
        if (args[0].isnull) return d_null(TEXTOID);
        Oid oid = (Oid)as_i8(s, args[0]);
        SqlRoutine *p2 = NULL;
        if (s && s->db)
            for (int i = 0; i < s->db->nroutines; i++)
                if (s->db->routines[i]->oid == oid) p2 = s->db->routines[i];
        if (!p2) {
            /* not a CREATE FUNCTION routine: the builtin table carries the
             * signature PostgreSQL prints for a built-in oid */
            int nb = 0;
            const ProcDef *pd = builtin_procs(&nb);
            if (oid >= 1000 && (int)oid - 1000 < nb) {
                const ProcDef *d = &pd[oid - 1000];
                const char *tx = EQI("pg_get_function_result") ? d->result : d->args;
                return d_text(TEXTOID, tx ? tx : "");
            }
            return d_null(TEXTOID);
        }
        if (EQI("pg_get_function_result")) {
            char *t = format_type_str(p2->rettype, -1);
            Buf rb; buf_init(&rb);
            if (p2->retset) buf_adds(&rb, "SETOF ");
            buf_adds(&rb, t ? t : "void");
            free(t);
            return d_textbuf(TEXTOID, &rb);
        }
        bool ident = EQI("pg_get_function_identity_arguments");
        Buf b; buf_init(&b);
        int printed = 0;
        for (int i = 0; i < p2->nargs; i++) {
            char m = p2->argmodes ? p2->argmodes[i] : 0;
            if (ident && m == 'o') continue;
            if (printed) buf_adds(&b, ", ");
            if (!ident) {
                bool anymode = false;
                for (int k = 0; k < p2->nargs; k++)
                    if (p2->argmodes && p2->argmodes[k] && p2->argmodes[k] != 'i') anymode = true;
                if (anymode) buf_printf(&b, "%s ",
                                        m == 'o' ? "OUT" : m == 'b' ? "INOUT" :
                                        m == 'v' ? "VARIADIC" : "IN");
            }
            if (!ident && p2->argnames && p2->argnames[i]) buf_printf(&b, "%s ", p2->argnames[i]);
            char *t = format_type_str(p2->argtypes[i], -1);
            buf_adds(&b, t ? t : "unknown");
            free(t);
            printed++;
        }
        return d_textbuf(TEXTOID, &b);
    }
#if PGVC_VN >= 140
    if (nargs == 0 && EQI("pg_get_wal_replay_pause_state")) {
        err_hint(s, "Recovery control functions can only be executed during recovery.");
        ereport(s, "55000", "recovery is not in progress");
    }
#endif
    if (nargs == 1 && EQI("pg_get_partkeydef")) {
        if (args[0].isnull || !s || !s->db) return d_null(TEXTOID);
        Table *t = tbl_find_oid(s->db, (Oid)args[0].v.i);
        if (!t || !t->partstrat) return d_null(TEXTOID);
        Buf b; buf_init(&b);
        buf_adds(&b, t->partstrat == 'r' ? "RANGE (" :
                     t->partstrat == 'l' ? "LIST (" : "HASH (");
        for (int i = 0; i < t->npartkey; i++) {
            if (i) buf_adds(&b, ", ");
            if (t->partkeyexpr && t->partkeyexpr[i]) buf_printf(&b, "(%s)", t->partkeyexpr[i]);
            else buf_adds(&b, t->cols[t->partkey[i]-1].name);
        }
        buf_addc(&b, ')');
        char *x = buf_steal(&b);
        Datum r = d_text(TEXTOID, x); free(x);
        return r;
    }
    if (nargs == 1 && EQI("pg_get_partition_constraintdef")) {
        if (args[0].isnull || !s || !s->db) return d_null(TEXTOID);
        Table *t = tbl_find_oid(s->db, (Oid)args[0].v.i);
        if (!t || !t->parent) return d_null(TEXTOID);
        return d_text(TEXTOID, t->partbound ? t->partbound : "");
    }
    if (nargs == 2 && EQI("pg_get_serial_sequence")) {
        if (args[0].isnull || args[1].isnull || !s || !s->db) return d_null(TEXTOID);
        char *tn = out_func(s, args[0]), *cn = out_func(s, args[1]);
        char *bare = strrchr(tn, '.');
        Table *t = tbl_find(s->db, NULL, bare ? bare + 1 : tn);
        if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", tn);
        int ci = col_index(t, cn);
        if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", cn, t->name);
        Datum r = d_null(TEXTOID);
        if (t->cols[ci].identity_seq) {
            Buf b; buf_init(&b);
            buf_printf(&b, "%s.%s", t->schema, t->cols[ci].identity_seq);
            char *x = buf_steal(&b); r = d_text(TEXTOID, x); free(x);
        } else if (t->cols[ci].defexpr && strstr(t->cols[ci].defexpr, "nextval(")) {
            const char *p = strchr(t->cols[ci].defexpr, '\'');
            if (p) { const char *q = strchr(p + 1, '\'');
                     if (q) { char *nm = xstrndup(p + 1, (size_t)(q - p - 1));
                              const char *nb = strrchr(nm, '.');
                              Buf b; buf_init(&b);
                              buf_printf(&b, "%s.%s", t->schema, nb ? nb + 1 : nm);
                              char *x = buf_steal(&b); r = d_text(TEXTOID, x); free(x); free(nm); } }
        }
        free(tn); free(cn);
        return r;
    }
#if PGVC_VN >= 100
    if (nargs <= 1 && EQI("pg_current_logfile")) return d_null(TEXTOID);
#endif
#if PGVC_VN >= 120
    if (nargs == 1 && EQI("pg_partition_root")) {
        if (args[0].isnull || !s || !s->db) return d_null(REGCLASSOID);
        Datum rc = coerce_to(s, args[0], REGCLASSOID, -1, 2);
        Oid oid = (Oid)rc.v.i;
        Table *t = tbl_find_oid(s->db, oid);
        Index *ix = NULL;
        if (!t) for (int i = 0; i < s->db->nindexes; i++)
                    if (s->db->indexes[i]->oid == oid) ix = s->db->indexes[i];
        if (!t && !ix) return d_null(REGCLASSOID);
        if (t && !t->partstrat && !t->parent) return d_null(REGCLASSOID);
        if (ix && !ix->partitioned && !ix->parent_index) return d_null(REGCLASSOID);
        Oid cur = oid;
        for (;;) {
            Table *ct = tbl_find_oid(s->db, cur);
            Index *ci = NULL;
            if (!ct) for (int k = 0; k < s->db->nindexes; k++)
                         if (s->db->indexes[k]->oid == cur) ci = s->db->indexes[k];
            Oid up = ct ? ct->parent : (ci ? ci->parent_index : 0);
            if (!up) break;
            cur = up;
        }
        extern Datum reg_make(Session *s, Oid regtype, int64 oid, const char *name);
        return reg_make(s, REGCLASSOID, cur, NULL);
    }
#endif
#if PGVC_VN >= 110
    if (nargs >= 3 && EQI("satisfies_hash_partition")) {
        if (args[0].isnull || !s || !s->db) return d_null(BOOLOID);
        Table *t = tbl_find_oid(s->db, (Oid)args[0].v.i);
        if (!t || t->partstrat != 'h')
            ereport(s, "42809", "\"%s\" is not a hash partitioned table",
                    t ? t->name : "?");
        int64 modulus = coerce_to(s, args[1], INT8OID, -1, 1).v.i;
        int64 remainder = coerce_to(s, args[2], INT8OID, -1, 1).v.i;
        if (modulus <= 0)
            ereport(s, "22023", "modulus for hash partition must be an integer value greater than zero");
        if (remainder < 0 || remainder >= modulus)
            ereport(s, "22023", "remainder for hash partition must be less than modulus");
        extern uint64 part_hash_key(Session *s, Datum d, uint64 running);
        uint64 h = 0;
        for (int i = 3; i < nargs; i++) h = part_hash_key(s, args[i], h);
        return d_bool((int64)(h % (uint64)modulus) == remainder);
    }
#endif
    if (nargs == 1 && EQI("pg_encoding_to_char")) return d_text(NAMEOID, "UTF8");
    if (nargs == 0 && EQI("pg_client_encoding")) return d_text(NAMEOID, "UTF8");
    if (nargs == 1 && (EQI("to_regclass") || EQI("to_regtype"))) {
        Oid want = EQI("to_regclass") ? REGCLASSOID : REGTYPEOID;
        if (args[0].isnull) return d_null(want);
        char *t = out_func(s, args[0]);
        extern int64 reg_oid_for(Session *s, Oid regtype, const char *name);
        int64 oid = reg_oid_for(s, want, t);
        free(t);
        if (oid <= 0) return d_null(want);
        extern Datum reg_make(Session *s, Oid regtype, int64 oid, const char *name);
        return reg_make(s, want, oid, NULL);
    }
    if ((nargs == 1 || nargs == 2) && EQI("pg_get_constraintdef")) {
        if (args[0].isnull || !s || !s->db) return d_null(TEXTOID);
        Oid oid = (Oid)args[0].v.i;
        extern char *constraint_def_by_oid(Session *s, Oid oid);
        char *t = constraint_def_by_oid(s, oid);
        if (!t) return d_null(TEXTOID);
        Datum r = d_text(TEXTOID, t); free(t);
        return r;
    }
#if PGVC_VN >= 160
    if (nargs == 2 && EQI("pg_input_is_valid")) {
        Oid ty; int32 tm;
        pgvc_input_type(s, args[1], &ty, &tm);
        if (args[0].isnull) return d_null(BOOLOID);
        char *t = as_cstr(s, args[0]);
        bool ok = pgvc_try_input(s, ty, tm, t, NULL);
        free(t);
        return d_bool(ok);
    }
#endif
    if ((nargs >= 1 && nargs <= 5) &&
        (EQI("pg_create_logical_replication_slot") ||
         EQI("pg_create_physical_replication_slot"))) {
        bool logical = EQI("pg_create_logical_replication_slot");
        if (args[0].isnull) return d_null(RECORDOID);
        extern void repl_slot_create(Session *s, const char *name, const char *plugin,
                                     bool logical, bool temporary);
        char *sn = out_func(s, args[0]);
        char *pn = (logical && nargs > 1 && !args[1].isnull) ? out_func(s, args[1]) : NULL;
        bool temp = false;
        int tempidx = logical ? 2 : 1;
        if (nargs > tempidx && !args[tempidx].isnull) temp = as_bool(s, args[tempidx]);
        repl_slot_create(s, sn, pn, logical, temp);
        Buf b; buf_init(&b);
        buf_printf(&b, "(%s,0/0)", sn);
        free(sn); free(pn);
        return d_textbuf(RECORDOID, &b);
    }
    if (nargs == 1 && EQI("pg_get_functiondef")) return d_null(TEXTOID);
#if PGVC_VN >= 110
    /* The SHA-2 family became built-in in PostgreSQL 11; each takes bytea. */
    if (nargs == 1 && (EQI("sha224") || EQI("sha256") || EQI("sha384") || EQI("sha512"))) {
        if (args[0].type != BYTEAOID && args[0].type != UNKNOWNOID) { *found = false; return d_null(BYTEAOID); }
        if (args[0].isnull) return d_null(BYTEAOID);
        Datum a = coerce_to(s, args[0], BYTEAOID, -1, 2);
        unsigned char out[64];
        int n;
        const unsigned char *p = (const unsigned char *)a.v.s.p;
        size_t len = (size_t)a.v.s.len;
        if (EQI("sha224"))      { sha224(p, len, out); n = 28; }
        else if (EQI("sha256")) { sha256(p, len, out); n = 32; }
        else if (EQI("sha384")) { sha384(p, len, out); n = 48; }
        else                    { sha512(p, len, out); n = 64; }
        return d_textn(BYTEAOID, (const char *)out, n);
    }
#endif
    if (nargs == 1 && EQI("pg_drop_replication_slot")) {
        extern void repl_slot_drop(Session *s, const char *name);
        char *t = out_func(s, args[0]);
        repl_slot_drop(s, t);
        free(t);
        return d_null(VOIDOID);
    }

    /* --------------------------------------------- WAL / backup / replication */
#if PGVC_VN < 100
    if (nargs == 0 && EQI("pg_current_xlog_location")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_current_xlog_insert_location")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_switch_xlog")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_last_xlog_receive_location")) return d_null(TEXTOID);
    if (nargs == 0 && EQI("pg_last_xlog_replay_location")) return d_null(TEXTOID);
    if (nargs == 2 && EQI("pg_xlog_location_diff")) return d_num(num_from_int64(0));
    if (nargs == 1 && EQI("pg_xlogfile_name"))
        return d_text(TEXTOID, "000000010000000000000001");
#else
    if (nargs == 0 && EQI("pg_current_wal_lsn")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_current_wal_insert_lsn")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_current_wal_flush_lsn")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_switch_wal")) return d_text(TEXTOID, "0/1000000");
    if (nargs == 0 && EQI("pg_last_wal_receive_lsn")) return d_null(TEXTOID);
    if (nargs == 0 && EQI("pg_last_wal_replay_lsn")) return d_null(TEXTOID);
    if (nargs == 2 && EQI("pg_wal_lsn_diff")) {
        if (args[0].isnull || args[1].isnull) return d_null(NUMERICOID);
        Datum a = coerce_to(s, args[0], PGLSNOID, -1, 1);
        Datum b2 = coerce_to(s, args[1], PGLSNOID, -1, 1);
        return d_num(num_from_int64(a.v.i - b2.v.i));
    }
    if (nargs == 1 && EQI("pg_walfile_name"))
        return d_text(TEXTOID, "000000010000000000000001");
    if (nargs == 1 && EQI("pg_walfile_name_offset")) {
        Datum l = coerce_to(s, args[0], PGLSNOID, -1, 1);
        RecordVal *rv = xmalloc(sizeof *rv);
        rv->n = 2;
        rv->vals = xmalloc(2 * sizeof(Datum));
        rv->names = xmalloc(2 * sizeof(char *));
        rv->names[0] = xstrdup("file_name"); rv->names[1] = xstrdup("file_offset");
        rv->vals[0] = d_text(TEXTOID, "000000010000000000000001");
        rv->vals[1] = d_int(INT4OID, (int64)(l.v.i & 0x00FFFFFF));
        Datum r; memset(&r, 0, sizeof r);
        r.type = RECORDOID; r.v.rec = rv;
        return r;
    }
#endif
#if PGVC_VN < 150
    if ((nargs >= 1 && nargs <= 3) && EQI("pg_start_backup")) return d_text(TEXTOID, "0/1000000");
    if (nargs <= 2 && EQI("pg_stop_backup")) return d_text(TEXTOID, "0/1000000");
#else
    if ((nargs >= 1 && nargs <= 2) && EQI("pg_backup_start")) {
        if (nargs >= 2) (void) coerce_to(s, args[1], BOOLOID, -1, 1);
        return d_text(PGLSNOID, "0/1000000");
    }
    if (nargs <= 1 && EQI("pg_backup_stop")) {
        if (nargs == 1) {
            s->err.position = s->funcargpos;
            (void) coerce_to(s, args[0], BOOLOID, -1, 1);
            s->err.position = 0;
        }
        return d_text(TEXTOID, "0/1000000");
    }
#endif
    if (nargs == 0 && EQI("pg_is_in_recovery")) return d_bool(false);

    /* ------------------------------------------------------------------- uuid */
#if PGVC_VN >= 130
    if (nargs == 0 && EQI("gen_random_uuid")) {
        unsigned char b[16];
        rand_bytes(b, 16);
        b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
        b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
        return make_uuid_datum(b);
    }
#endif
#if PGVC_VN >= 180
    if (nargs == 0 && EQI("uuidv4")) {
        unsigned char b[16];
        rand_bytes(b, 16);
        b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
        b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
        return make_uuid_datum(b);
    }
    if ((nargs == 0 || nargs == 1) && EQI("uuidv7")) {
        unsigned char b[16];
        rand_bytes(b, 16);
        int64 ms = (clock_us() + 946684800000000LL) / 1000;
        if (nargs == 1 && !args[0].isnull && args[0].type == INTERVALOID)
            ms += args[0].v.iv.usec / 1000 + (int64)args[0].v.iv.day * 86400000LL;
        for (int i = 5; i >= 0; i--) { b[i] = (unsigned char)(ms & 0xff); ms >>= 8; }
        b[6] = (unsigned char)((b[6] & 0x0F) | 0x70);
        b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
        return make_uuid_datum(b);
    }
#endif
#if PGVC_VN >= 170
    if (nargs == 1 && EQI("uuid_extract_version")) {
        if (args[0].isnull) return d_null(INT2OID);
        char *t = as_cstr(s, args[0]);
        int v = 0;
        int seen = 0;
        for (char *q = t; *q; q++) {
            if (!isxdigit((unsigned char)*q)) continue;
            if (seen == 12) { v = isdigit((unsigned char)*q) ? *q - '0'
                                  : tolower((unsigned char)*q) - 'a' + 10; break; }
            seen++;
        }
        free(t);
        return d_int(INT2OID, v);
    }
    if (nargs == 1 && EQI("uuid_extract_timestamp")) {
        if (args[0].isnull) return d_null(TIMESTAMPTZOID);
        char *t = as_cstr(s, args[0]);
        char hx[33]; int o = 0;
        for (char *q = t; *q && o < 32; q++) if (isxdigit((unsigned char)*q)) hx[o++] = *q;
        hx[o] = 0;
        free(t);
        if (o < 32) return d_null(TIMESTAMPTZOID);
        int ver = isdigit((unsigned char)hx[12]) ? hx[12] - '0'
                                                 : tolower((unsigned char)hx[12]) - 'a' + 10;
        if (ver != 7) return d_null(TIMESTAMPTZOID);
        char tmp[13];
        memcpy(tmp, hx, 12); tmp[12] = 0;
        int64 ms = (int64)strtoll(tmp, NULL, 16);
        return d_int(TIMESTAMPTZOID, ms * 1000 - 946684800000000LL);
    }
#endif

#if PGVC_VN >= 110
    /* ---------------------------------------------------------- text search */
    if ((nargs == 1 || nargs == 2) &&
        (EQI("to_tsvector") || EQI("websearch_to_tsquery") ||
         EQI("plainto_tsquery") || EQI("to_tsquery"))) {
        Datum cfgd = nargs == 2 ? args[0] : d_text(REGCONFIGOID, "english");
        Datum vald = nargs == 2 ? args[1] : args[0];
        if (vald.isnull) return d_null(EQI("to_tsvector") ? TSVECTOROID : TSQUERYOID);
        char *cfg = as_cstr(s, cfgd);
        if (!ts_config_known(cfg)) {
            char *c = cfg;
            ereport(s, "3F000", "text search configuration \"%s\" does not exist", c);
        }
        if (EQI("to_tsvector")) {
            char *out;
            if (vald.type == JSONBOID || vald.type == JSONOID) {
                int np = 0; char **parts = jsonts_parts(s, vald, NULL, &np);
                out = ts_to_tsvector_parts(cfg, parts, np);
                for (int i = 0; i < np; i++) free(parts[i]);
                free(parts);
            } else {
                char *t = as_cstr(s, vald);
                out = ts_to_tsvector(cfg, t);
                free(t);
            }
            free(cfg);
            Datum r = d_text(TSVECTOROID, out);
            free(out);
            return r;
        }
        char *t = as_cstr(s, vald);
        int why = 0;
        char *out = ts_websearch_to_tsquery(cfg, t, &why);
        if (why == 1)
            notice(s, "00000", "text-search query doesn't contain lexemes: \"%s\"", t);
        else if (why == 2)
            notice(s, "00000", "text-search query contains only stop words or "
                              "doesn't contain lexemes, ignored");
        free(t); free(cfg);
        Datum r = d_text(TSQUERYOID, out);
        free(out);
        return r;
    }
    if (nargs == 3 && (EQI("jsonb_to_tsvector") || EQI("json_to_tsvector"))) {
        if (args[0].isnull || args[1].isnull || args[2].isnull) return d_null(TSVECTOROID);
        char *cfg = as_cstr(s, args[0]);
        if (!ts_config_known(cfg))
            ereport(s, "3F000", "text search configuration \"%s\" does not exist", cfg);
        int np = 0;
        char **parts = jsonts_parts(s, args[1], &args[2], &np);
        char *out = ts_to_tsvector_parts(cfg, parts, np);
        for (int i = 0; i < np; i++) free(parts[i]);
        free(parts); free(cfg);
        Datum r = d_text(TSVECTOROID, out);
        free(out);
        return r;
    }
#endif

    NOTFOUND();
}

/* ------------------------------------------------------------------ || */
static Datum concat_op(Session *s, Datum a, Datum b, bool *found)
{
    /* casting bpchar to text drops the blank padding */
    if (a.type == BPCHAROID && !a.isnull) a = coerce_to(s, a, TEXTOID, -1, 2);
    if (b.type == BPCHAROID && !b.isnull) b = coerce_to(s, b, TEXTOID, -1, 2);
    bool aa = type_is_array(a.type), ba = type_is_array(b.type);
    if (aa || ba) {
        if (a.isnull && b.isnull) return d_null(aa ? a.type : b.type);
        if (aa && ba) {
            if (a.isnull) return b;
            if (b.isnull) return a;
            Oid et = arr_elemtype(a);
            ArrayVal *r = arr_new(et);
            for (int k = 0; k < 2; k++) {
                ArrayVal *x = (k ? b : a).v.arr;
                for (int i = 0; x && i < x->nelem; i++) arr_push(r, x->elems[i]);
            }
            return d_arr(a.type, r);
        }
        if (aa) {
            if (a.isnull) { ArrayVal *r = arr_new(b.isnull ? TEXTOID : b.type);
                            arr_push(r, d_copy(b)); return d_arr(a.type, r); }
            Oid et = arr_elemtype(a);
            ArrayVal *r = arr_new(et);
            ArrayVal *x = a.v.arr;
            for (int i = 0; x && i < x->nelem; i++) arr_push(r, x->elems[i]);
            arr_push(r, b.isnull ? d_null(et) : coerce_to(s, b, et, -1, 1));
            return d_arr(a.type, r);
        }
        if (b.isnull) { ArrayVal *r = arr_new(a.isnull ? TEXTOID : a.type);
                        arr_push(r, d_copy(a)); return d_arr(b.type, r); }
        Oid et = arr_elemtype(b);
        ArrayVal *r = arr_new(et);
        arr_push(r, a.isnull ? d_null(et) : coerce_to(s, a, et, -1, 1));
        ArrayVal *x = b.v.arr;
        for (int i = 0; x && i < x->nelem; i++) arr_push(r, x->elems[i]);
        return d_arr(b.type, r);
    }
    if (a.type == JSONBOID && b.type == JSONBOID) {
        if (a.isnull || b.isnull) return d_null(JSONBOID);
        JN *x = jn_of(s, a), *y = jn_of(s, b);
        JN *r;
        if (x->kind == 5 && y->kind == 5) {
            r = jn_copy(x);
            for (int i = 0; i < y->n; i++) jn_put(r, y->keys[i], jn_copy(y->vals[i]));
        } else if (x->kind == 4 || y->kind == 4) {
            r = jn_arr();
            if (x->kind == 4) for (int i = 0; i < x->n; i++) jn_push(r, jn_copy(x->el[i]));
            else jn_push(r, jn_copy(x));
            if (y->kind == 4) for (int i = 0; i < y->n; i++) jn_push(r, jn_copy(y->el[i]));
            else jn_push(r, jn_copy(y));
        } else {
            r = jn_arr();
            jn_push(r, jn_copy(x)); jn_push(r, jn_copy(y));
        }
        return d_jsonb_node(s, r);
    }
    if (a.type == BYTEAOID && b.type == BYTEAOID) {
        if (a.isnull || b.isnull) return d_null(BYTEAOID);
        Buf bb; buf_init(&bb);
        buf_add(&bb, a.v.s.p, (size_t)a.v.s.len);
        buf_add(&bb, b.v.s.p, (size_t)b.v.s.len);
        return d_textbuf(BYTEAOID, &bb);
    }
    if (f_is_range(a.type) || f_is_mrange(a.type)) { *found = false; return d_null(TEXTOID); }
    if (a.isnull || b.isnull) return d_null(TEXTOID);
    char *x = as_cstr(s, a), *y = as_cstr(s, b);
    Buf bb; buf_init(&bb);
    buf_adds(&bb, x); buf_adds(&bb, y);
    free(x); free(y);
    return d_textbuf(TEXTOID, &bb);
}

/* ------------------------------------------------------------ jsonb ops */
static JN *jn_path_get(Session *s, JN *v, ArrayVal *path)
{
    for (int i = 0; v && path && i < path->nelem; i++) {
        if (path->elems[i].isnull) return NULL;
        char *k = as_cstr(s, path->elems[i]);
        if (v->kind == 5) v = jn_get(v, k);
        else if (v->kind == 4) {
            char *e; long idx = strtol(k, &e, 10);
            if (*e) v = NULL;
            else { if (idx < 0) idx += v->n; v = (idx >= 0 && idx < v->n) ? v->el[idx] : NULL; }
        } else v = NULL;
        free(k);
    }
    return v;
}

static Datum jsonb_op(Session *s, const char *name, Datum *args, int nargs, bool *found,
                      bool *handled)
{
    *handled = false;
    if (nargs != 2) return d_null(TEXTOID);
    Oid t0 = args[0].type;
    if (t0 != JSONBOID && t0 != JSONOID) return d_null(TEXTOID);
    bool jb = (t0 == JSONBOID);
    Oid rt = jb ? JSONBOID : JSONOID;
    Oid t1 = args[1].type;

    if (EQ("->") || EQ("->>")) {
        bool astext = EQ("->>");
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(astext ? TEXTOID : rt);
        JN *v = jn_of(s, args[0]);
        JN *r = NULL;
        if (f_is_int(t1)) {          /* jsonb -> integer; unknown resolves to text */
            long idx = (long)as_i8(s, args[1]);
            if (v->kind == 4) { if (idx < 0) idx += v->n;
                                r = (idx >= 0 && idx < v->n) ? v->el[idx] : NULL; }
        } else {
            char *k = as_cstr(s, args[1]);
            if (v->kind == 5) r = jn_get(v, k);
            free(k);
        }
        if (!r) return d_null(astext ? TEXTOID : rt);
        if (astext) { char *t = jn_astext(r);
                      if (!t) return d_null(TEXTOID);
                      Datum d = d_text(TEXTOID, t); free(t); return d; }
        if (jb) return d_jsonb_node(s, r);
        char *t = jn_text(r, 0);
        Datum d = d_text(JSONOID, t); free(t);
        return d;
    }
    if (EQ("#>") || EQ("#>>")) {
        bool astext = EQ("#>>");
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(astext ? TEXTOID : rt);
        JN *v = jn_of(s, args[0]);
        Datum pd = coerce_to(s, args[1], TEXTARRAYOID, -1, 1);
        JN *r = jn_path_get(s, v, pd.v.arr);
        if (!r) return d_null(astext ? TEXTOID : rt);
        if (astext) { char *t = jn_astext(r);
                      if (!t) return d_null(TEXTOID);
                      Datum d = d_text(TEXTOID, t); free(t); return d; }
        if (jb) return d_jsonb_node(s, r);
        char *t = jn_text(r, 0);
        Datum d = d_text(JSONOID, t); free(t);
        return d;
    }
    if (!jb) return d_null(TEXTOID);
    if ((EQ("@>") || EQ("<@")) && (t1 == JSONBOID || t1 == UNKNOWNOID)) {
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        /* an untyped literal on the right is read as jsonb */
        Datum b = (t1 == JSONBOID) ? args[1] : coerce_to(s, args[1], JSONBOID, -1, 1);
        return d_bool(EQ("@>") ? json_contains(args[0].v.jb, b.v.jb)
                               : json_contains(b.v.jb, args[0].v.jb));
    }
    if (EQ("?") && (f_is_str(t1))) {
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        bool f2;
        return call_builtin(s, "jsonb_exists", args, 2, &f2);
    }
    if ((EQ("?|") || EQ("?&")) ) {
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
        Datum ka = coerce_to(s, args[1], TEXTARRAYOID, -1, 1);
        ArrayVal *a = ka.v.arr;
        bool any = EQ("?|");
        bool acc = !any;
        for (int i = 0; a && i < a->nelem; i++) {
            if (a->elems[i].isnull) continue;
            Datum two[2] = { args[0], a->elems[i] };
            bool f2;
            bool hit = call_builtin(s, "jsonb_exists", two, 2, &f2).v.i != 0;
            if (any && hit) return d_bool(true);
            if (!any && !hit) return d_bool(false);
        }
        return d_bool(acc);
    }
    if (EQ("-")) {
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(JSONBOID);
        JN *v = jn_copy(jn_of(s, args[0]));
        if (type_is_array(t1)) {
            Datum ka = coerce_to(s, args[1], TEXTARRAYOID, -1, 1);
            ArrayVal *a = ka.v.arr;
            for (int i = 0; a && i < a->nelem; i++) {
                if (a->elems[i].isnull) continue;
                char *k = as_cstr(s, a->elems[i]);
                if (v->kind == 5) {
                    int w = 0;
                    for (int j = 0; j < v->n; j++) {
                        if (!strcmp(v->keys[j], k)) continue;
                        v->keys[w] = v->keys[j]; v->vals[w] = v->vals[j]; w++;
                    }
                    v->n = w;
                }
                free(k);
            }
        } else if (f_is_int(t1)) {
            long idx = (long)as_i8(s, args[1]);
            if (v->kind != 4)
                ereport(s, "22023", "cannot delete from object using integer index");
            if (idx < 0) idx += v->n;
            if (idx >= 0 && idx < v->n) {
                for (int j = (int)idx; j < v->n - 1; j++) v->el[j] = v->el[j+1];
                v->n--;
            }
        } else {
            char *k = as_cstr(s, args[1]);
            if (v->kind == 5) {
                int w = 0;
                for (int j = 0; j < v->n; j++) {
                    if (!strcmp(v->keys[j], k)) continue;
                    v->keys[w] = v->keys[j]; v->vals[w] = v->vals[j]; w++;
                }
                v->n = w;
            } else if (v->kind == 4) {
                int w = 0;
                for (int j = 0; j < v->n; j++) {
                    if (v->el[j]->kind == 3 && !strcmp(v->el[j]->str, k)) continue;
                    v->el[w++] = v->el[j];
                }
                v->n = w;
            } else
                ereport(s, "22023", "cannot delete from scalar");
            free(k);
        }
        return d_jsonb_node(s, v);
    }
    if (EQ("#-")) {
        *handled = true;
        if (args[0].isnull || args[1].isnull) return d_null(JSONBOID);
        JN *v = jn_copy(jn_of(s, args[0]));
        Datum pd = coerce_to(s, args[1], TEXTARRAYOID, -1, 1);
        ArrayVal *path = pd.v.arr;
        int np = path ? path->nelem : 0;
        if (np == 0) return d_jsonb_node(s, v);
        JN *cur = v;
        for (int i = 0; i < np - 1 && cur; i++) {
            char *k = as_cstr(s, path->elems[i]);
            JN *nx = NULL;
            if (cur->kind == 5) nx = jn_get(cur, k);
            else if (cur->kind == 4) { char *e; long idx = strtol(k, &e, 10);
                if (!*e) { if (idx < 0) idx += cur->n;
                           if (idx >= 0 && idx < cur->n) nx = cur->el[idx]; } }
            free(k);
            cur = nx;
        }
        if (cur) {
            char *k = as_cstr(s, path->elems[np-1]);
            if (cur->kind == 5) {
                int w = 0;
                for (int j = 0; j < cur->n; j++) {
                    if (!strcmp(cur->keys[j], k)) continue;
                    cur->keys[w] = cur->keys[j]; cur->vals[w] = cur->vals[j]; w++;
                }
                cur->n = w;
            } else if (cur->kind == 4) {
                char *e; long idx = strtol(k, &e, 10);
                if (!*e) { if (idx < 0) idx += cur->n;
                    if (idx >= 0 && idx < cur->n) {
                        for (int j = (int)idx; j < cur->n - 1; j++) cur->el[j] = cur->el[j+1];
                        cur->n--;
                    } }
            }
            free(k);
        }
        return d_jsonb_node(s, v);
    }
#if PGVC_VN >= 120
    {   /* @? and @@ are jsonb_path_exists/match with silent = true */
        Datum r = jsonpath_operator(s, name, args, nargs, handled);
        if (*handled) return r;
    }
#endif
    (void)found;
    return d_null(TEXTOID);
}

/* ------------------------------------------------------------ range ops */
static MRangeVal *mr_from_range(Oid mt, RangeVal *r)
{
    MRangeVal *m = mr_new(mt);
    mr_push(m, r);
    return m;
}

static Datum range_op(Session *s, const char *name, Datum *args, int nargs, bool *found,
                      bool *handled)
{
    *handled = false;
    (void)found;
    if (nargs != 2) return d_null(TEXTOID);
    Oid t0 = args[0].type, t1 = args[1].type;
    bool r0 = f_is_range(t0), m0 = f_is_mrange(t0);
    bool r1 = f_is_range(t1), m1 = f_is_mrange(t1);
    if (!r0 && !m0 && !r1 && !m1) return d_null(TEXTOID);
    bool isop = EQ("@>") || EQ("<@") || EQ("&&") || EQ("<<") || EQ(">>") ||
                EQ("&<") || EQ("&>") ||
                EQ("-|-") || EQ("+") || EQ("*") || EQ("-");
    if (!isop) return d_null(TEXTOID);
    /* union, intersection and difference are defined only between two values
     * of the same range or multirange type */
    if ((EQ("+") || EQ("*") || EQ("-")) && t0 != t1) return d_null(TEXTOID);
    *handled = true;
    if (args[0].isnull || args[1].isnull) {
        if (EQ("+") || EQ("*") || EQ("-")) return d_null(t0);
        return d_null(BOOLOID);
    }

    /* element containment */
    if (EQ("@>") && (r0 || m0) && !r1 && !m1) {
        Datum e = coerce_to(s, args[1], f_range_subtype(r0 ? t0 : f_range_of_mrange(t0)), -1, 1);
        if (r0) return d_bool(rng_contains_elem(s, args[0].v.rng, e));
        MRangeVal *m = args[0].v.mrng;
        for (int i = 0; i < m->n; i++) if (rng_contains_elem(s, m->ranges[i], e)) return d_bool(true);
        return d_bool(false);
    }
    if (EQ("<@") && !r0 && !m0 && (r1 || m1)) {
        Datum e = coerce_to(s, args[0], f_range_subtype(r1 ? t1 : f_range_of_mrange(t1)), -1, 1);
        if (r1) return d_bool(rng_contains_elem(s, args[1].v.rng, e));
        MRangeVal *m = args[1].v.mrng;
        for (int i = 0; i < m->n; i++) if (rng_contains_elem(s, m->ranges[i], e)) return d_bool(true);
        return d_bool(false);
    }

    /* normalise both sides to multiranges when either side is one */
    if (m0 || m1) {
        Oid mt = m0 ? t0 : t1;
        Oid rt = f_range_of_mrange(mt);
        MRangeVal *A = m0 ? args[0].v.mrng : mr_from_range(mt, args[0].v.rng);
        MRangeVal *B = m1 ? args[1].v.mrng : mr_from_range(mt, args[1].v.rng);
        if (EQ("@>") || EQ("<@")) {
            MRangeVal *big = EQ("@>") ? A : B, *small = EQ("@>") ? B : A;
            for (int i = 0; i < small->n; i++) {
                bool ok = false;
                for (int j = 0; j < big->n; j++)
                    if (rng_contains(s, big->ranges[j], small->ranges[i])) { ok = true; break; }
                if (!ok) return d_bool(false);
            }
            return d_bool(true);
        }
        if (EQ("&&")) {
            for (int i = 0; i < A->n; i++)
                for (int j = 0; j < B->n; j++)
                    if (rng_overlaps(s, A->ranges[i], B->ranges[j])) return d_bool(true);
            return d_bool(false);
        }
        if (EQ("<<")) {
            if (!A->n || !B->n) return d_bool(false);
            return d_bool(rng_before(s, A->ranges[A->n-1], B->ranges[0]));
        }
        if (EQ(">>")) {
            if (!A->n || !B->n) return d_bool(false);
            return d_bool(rng_before(s, B->ranges[B->n-1], A->ranges[0]));
        }
        if (EQ("-|-")) {
            if (!A->n || !B->n) return d_bool(false);
            return d_bool(rng_adjacent(s, A->ranges[A->n-1], B->ranges[0]) ||
                          rng_adjacent(s, B->ranges[B->n-1], A->ranges[0]));
        }
        if (EQ("&<")) {           /* does not extend to the right of */
            if (!A->n || !B->n) return d_bool(false);
            return d_bool(rb_cmp(s, rb_upper(A->ranges[A->n-1]),
                                    rb_upper(B->ranges[B->n-1])) <= 0);
        }
        if (EQ("&>")) {           /* does not extend to the left of */
            if (!A->n || !B->n) return d_bool(false);
            return d_bool(rb_cmp(s, rb_lower(A->ranges[0]), rb_lower(B->ranges[0])) >= 0);
        }
        MRangeVal *out = mr_new(mt);
        if (EQ("+")) {
            for (int i = 0; i < A->n; i++) mr_push(out, A->ranges[i]);
            for (int i = 0; i < B->n; i++) mr_push(out, B->ranges[i]);
        } else if (EQ("*")) {
            for (int i = 0; i < A->n; i++)
                for (int j = 0; j < B->n; j++) {
                    RangeVal *x = rng_intersect(s, A->ranges[i], B->ranges[j]);
                    if (!x->empty) mr_push(out, x);
                }
        } else {
            for (int i = 0; i < A->n; i++) {
                RangeVal *cur = A->ranges[i];
                for (int j = 0; j < B->n && cur && !cur->empty; j++) {
                    if (!rng_overlaps(s, cur, B->ranges[j])) continue;
                    bool lo_in = rb_cmp(s, rb_lower(B->ranges[j]), rb_lower(cur)) <= 0;
                    bool hi_in = rb_cmp(s, rb_upper(B->ranges[j]), rb_upper(cur)) >= 0;
                    if (lo_in && hi_in) { cur = rv_empty(rt); break; }
                    if (!lo_in && !hi_in) {
                        RangeVal *left = rv_copy2(cur);
                        RBound lb = rb_lower(B->ranges[j]);
                        left->upper_inf = lb.inf; left->upper = lb.val; left->upper_inc = !lb.inc;
                        mr_push(out, left);
                        RangeVal *right = rv_copy2(cur);
                        RBound ub = rb_upper(B->ranges[j]);
                        right->lower_inf = ub.inf; right->lower = ub.val; right->lower_inc = !ub.inc;
                        cur = right;
                        continue;
                    }
                    cur = rng_minus(s, cur, B->ranges[j]);
                }
                if (cur && !cur->empty) mr_push(out, cur);
            }
        }
        mr_normalize(s, out);
        return d_mrange(mt, out);
    }

    /* plain range op range */
    RangeVal *a = args[0].v.rng, *b = args[1].v.rng;
    if (EQ("@>")) return d_bool(rng_contains(s, a, b));
    if (EQ("<@")) return d_bool(rng_contains(s, b, a));
    if (EQ("&&")) return d_bool(rng_overlaps(s, a, b));
    if (EQ("<<")) return d_bool(rng_before(s, a, b));
    if (EQ(">>")) return d_bool(rng_before(s, b, a));
    if (EQ("-|-")) return d_bool(rng_adjacent(s, a, b));
    if (EQ("&<")) return d_bool(rb_cmp(s, rb_upper(a), rb_upper(b)) <= 0);
    if (EQ("&>")) return d_bool(rb_cmp(s, rb_lower(a), rb_lower(b)) >= 0);
    if (EQ("+")) return d_range(t0, rng_union(s, a, b));
    if (EQ("*")) return d_range(t0, rng_intersect(s, a, b));
    return d_range(t0, rng_minus(s, a, b));
}

/* ------------------------------------------------------- geometric ops
 * Only the containment operators are modelled; the values themselves stay in
 * their text form and are read back here. */
static int geo_nums(const char *t, double *out, int max)
{
    int n = 0;
    const char *p = t;
    while (*p && n < max) {
        if (*p == '-' || *p == '+' || *p == '.' || isdigit((unsigned char)*p)) {
            char *e;
            double v = strtod(p, &e);
            if (e == p) { p++; continue; }
            out[n++] = v;
            p = e;
        } else p++;
    }
    return n;
}

static Datum geo_op(Session *s, const char *name, Datum *args, int nargs, bool *handled)
{
    *handled = false;
    if (nargs != 2) return d_null(TEXTOID);
    Oid t0 = args[0].type, t1 = args[1].type;
    bool g0 = (t0 == BOXOID || t0 == POINTOID || t0 == CIRCLEOID);
    bool g1 = (t1 == BOXOID || t1 == POINTOID || t1 == CIRCLEOID);
    if (!g0 || !g1) return d_null(TEXTOID);
    if (!EQ("@>") && !EQ("<@")) return d_null(TEXTOID);
    Oid bt = EQ("@>") ? t0 : t1, st = EQ("@>") ? t1 : t0;
    if (bt == POINTOID) return d_null(TEXTOID);          /* a point contains nothing */
    if (bt == BOXOID && st != BOXOID && st != POINTOID) return d_null(TEXTOID);
    if (bt == CIRCLEOID && st != CIRCLEOID && st != POINTOID) return d_null(TEXTOID);
    *handled = true;
    if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
    Datum bd = EQ("@>") ? args[0] : args[1];
    Datum sd = EQ("@>") ? args[1] : args[0];
    double B[8], S[8];
    int nb = geo_nums(bd.v.s.p, B, 8), ns = geo_nums(sd.v.s.p, S, 8);
    if (bt == BOXOID) {
        if (nb < 4) return d_bool(false);
        double lx = B[0] < B[2] ? B[0] : B[2], hx = B[0] < B[2] ? B[2] : B[0];
        double ly = B[1] < B[3] ? B[1] : B[3], hy = B[1] < B[3] ? B[3] : B[1];
        if (st == POINTOID) {
            if (ns < 2) return d_bool(false);
            return d_bool(S[0] >= lx && S[0] <= hx && S[1] >= ly && S[1] <= hy);
        }
        if (ns < 4) return d_bool(false);
        double slx = S[0] < S[2] ? S[0] : S[2], shx = S[0] < S[2] ? S[2] : S[0];
        double sly = S[1] < S[3] ? S[1] : S[3], shy = S[1] < S[3] ? S[3] : S[1];
        return d_bool(slx >= lx && shx <= hx && sly >= ly && shy <= hy);
    }
    /* circle */
    if (nb < 3) return d_bool(false);
    double cx = B[0], cy = B[1], cr = B[2];
    if (st == POINTOID) {
        if (ns < 2) return d_bool(false);
        double dx = S[0] - cx, dy = S[1] - cy;
        return d_bool(dx*dx + dy*dy <= cr*cr);
    }
    if (ns < 3) return d_bool(false);
    double dx = S[0] - cx, dy = S[1] - cy;
    return d_bool(sqrt(dx*dx + dy*dy) + S[2] <= cr);
}

/* ------------------------------------------------------------ array ops */
static Datum array_op(Session *s, const char *name, Datum *args, int nargs, bool *found,
                      bool *handled)
{
    *handled = false;
    (void)found;
    if (nargs != 2) return d_null(TEXTOID);
    if (!type_is_array(args[0].type) || !type_is_array(args[1].type)) return d_null(TEXTOID);
    if (!(EQ("@>") || EQ("<@") || EQ("&&"))) return d_null(TEXTOID);
    *handled = true;
    if (args[0].isnull || args[1].isnull) return d_null(BOOLOID);
    ArrayVal *a = args[0].v.arr, *b = args[1].v.arr;
    if (!a) a = arr_new(TEXTOID);
    if (!b) b = arr_new(TEXTOID);
    if (EQ("@>")) return d_bool(arr_contains(s, a, b));
    if (EQ("<@")) return d_bool(arr_contains(s, b, a));
    return d_bool(arr_overlap(s, a, b));
}
