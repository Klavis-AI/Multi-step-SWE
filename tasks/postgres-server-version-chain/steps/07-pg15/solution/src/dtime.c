/*-------------------------------------------------------------------------
 * dtime.c - date/time support for the PostgreSQL-compatible server.
 *
 * Faithful re-implementation of PostgreSQL's datetime machinery for the
 * single configuration this server supports:
 *
 *      timezone = UTC,  DateStyle = 'ISO, MDY',  IntervalStyle = postgres
 *
 * Internal representations are PostgreSQL's own:
 *      date        int32  days since 2000-01-01
 *      time        int64  microseconds since midnight
 *      timestamp   int64  microseconds since 2000-01-01 00:00:00 (UTC for tz)
 *      interval    PgInterval { usec, day, month } - three independent fields
 *
 * Version-sensitive behaviour is gated on PGVC_MAJOR:
 *      < 10   to_date()/to_timestamp() do not range-check their fields, so
 *             out-of-range values roll over ('2020-13-01' -> 2021-01-01)
 *      < 15   extract()/date_part() unit errors use the old message wording
 *             (`timestamp units "x" not supported`)
 *      >= 18  extract(week from interval) is supported (day / 7)
 *
 * libc only.  Compile with:  gcc -std=gnu11 -O2 -Wall -c dtime.c
 *-------------------------------------------------------------------------
 */
#include "pgvc.h"

/* PGVC_MAJOR is spelled the way PostgreSQL spells the release, so it does not
 * order numerically; PGVC_VERNUM does (96 < 110 < 112). */
#ifndef PGVC_VERNUM
#if PGVC_MAJOR == 96
#define PGVC_VERNUM 96
#else
#define PGVC_VERNUM (PGVC_MAJOR + 100)
#endif
#endif
#include <limits.h>
#include <inttypes.h>

/*
 * PGVC_MAJOR is spelled 96 for PostgreSQL 9.6 and 10..18 for the rest, so it
 * cannot be compared numerically.  PGVC_VERNUM is monotonic: 96 < 110 < 118.
 */
#if PGVC_MAJOR == 96
#define PGVC_VERNUM 96
#else
#define PGVC_VERNUM (PGVC_MAJOR + 100)
#endif

/* ------------------------------------------------------------------ *
 *  Constants (mirrors of PostgreSQL's datatype/timestamp.h)
 * ------------------------------------------------------------------ */
#define SECS_PER_DAY      86400
#define SECS_PER_HOUR     3600
#define SECS_PER_MINUTE   60
#define MINS_PER_HOUR     60
#define HOURS_PER_DAY     24
#define MONTHS_PER_YEAR   12
#define DAYS_PER_MONTH    30        /* assumed by interval arithmetic */
#define SECS_PER_YEAR     (36525 * 864)   /* 31557600 */

#define USECS_PER_DAY     INT64_C(86400000000)
#define USECS_PER_HOUR    INT64_C(3600000000)
#define USECS_PER_MINUTE  INT64_C(60000000)
#define USECS_PER_SEC     INT64_C(1000000)

#define POSTGRES_EPOCH_JDATE  2451545   /* == date2j(2000, 1, 1) */
#define UNIX_EPOCH_JDATE      2440588   /* == date2j(1970, 1, 1) */

#define JULIAN_MINYEAR   (-4713)
#define JULIAN_MINMONTH  (11)
#define JULIAN_MAXYEAR   (5874898)
#define JULIAN_MAXMONTH  (6)

#define IS_VALID_JULIAN(y,m,d)                                          \
    (((y) > JULIAN_MINYEAR ||                                           \
      ((y) == JULIAN_MINYEAR && ((m) >= JULIAN_MINMONTH))) &&           \
     ((y) < JULIAN_MAXYEAR ||                                           \
      ((y) == JULIAN_MAXYEAR && ((m) < JULIAN_MAXMONTH))))

#define DATETIME_MIN_JULIAN   (0)
#define DATE_END_JULIAN       (2147483494)   /* == date2j(JULIAN_MAXYEAR, 1, 1) */

#define IS_VALID_DATE(d)                                                \
    ((DATETIME_MIN_JULIAN - POSTGRES_EPOCH_JDATE) <= (d) &&             \
     (d) < (DATE_END_JULIAN - POSTGRES_EPOCH_JDATE))

#define MIN_TIMESTAMP   INT64_C(-211813488000000000)
#define END_TIMESTAMP   INT64_C(9223371331200000000)
#define IS_VALID_TIMESTAMP(t)  (MIN_TIMESTAMP <= (t) && (t) < END_TIMESTAMP)

#define DT_NOBEGIN      INT64_MIN
#define DT_NOEND        INT64_MAX

#define MAX_TZDISP_HOUR 15

#define TIMESTAMP_IS_NOBEGIN(j)  ((j) == DT_NOBEGIN)
#define TIMESTAMP_IS_NOEND(j)    ((j) == DT_NOEND)
#define TIMESTAMP_NOT_FINITE(j)  (TIMESTAMP_IS_NOBEGIN(j) || TIMESTAMP_IS_NOEND(j))

/* field-content identifiers produced by the tokenizer */
#define DTK_NUMBER      0
#define DTK_STRING      1
#define DTK_DATE        2
#define DTK_TIME        3
#define DTK_TZ          4
#define DTK_AGO         5

#define DTK_SPECIAL     6
#define DTK_EARLY       9
#define DTK_LATE        10
#define DTK_EPOCH       11
#define DTK_NOW         12
#define DTK_YESTERDAY   13
#define DTK_TODAY       14
#define DTK_TOMORROW    15
#define DTK_ZULU        16

#define DTK_DELTA       17
#define DTK_SECOND      18
#define DTK_MINUTE      19
#define DTK_HOUR        20
#define DTK_DAY         21
#define DTK_WEEK        22
#define DTK_MONTH       23
#define DTK_QUARTER     24
#define DTK_YEAR        25
#define DTK_DECADE      26
#define DTK_CENTURY     27
#define DTK_MILLENNIUM  28
#define DTK_MILLISEC    29
#define DTK_MICROSEC    30
#define DTK_JULIAN      31

#define DTK_DOW         32
#define DTK_DOY         33
#define DTK_TZ_HOUR     34
#define DTK_TZ_MINUTE   35
#define DTK_ISOYEAR     36
#define DTK_ISODOW      37

/* mask bit positions (all < 32) */
#define RESERV      0
#define MONTH       1
#define YEAR        2
#define DAY         3
#define JULIAN      4
#define TZ          5
#define DTZ         6
#define DYNTZ       7
#define IGNORE_DTF  8
#define AMPM        9
#define HOUR        10
#define MINUTE      11
#define SECOND      12
#define MILLISECOND 13
#define MICROSECOND 14
#define DOY         15
#define DOW         16
#define UNITS       17
#define ADBC        18
#define AGO         19
#define ABS_BEFORE  20
#define ABS_AFTER   21
#define ISODATE     22
#define ISOTIME     23
#define WEEK        24
#define DECADE      25
#define CENTURY     26
#define MILLENNIUM  27
#define DTZMOD      28
#define UNKNOWN_FIELD 31

/* field mask bits */
#define DTK_M(t)        (0x01U << (t))
#define DTK_ALL_SECS_M  (DTK_M(SECOND) | DTK_M(MILLISECOND) | DTK_M(MICROSECOND))
#define DTK_DATE_M      (DTK_M(YEAR) | DTK_M(MONTH) | DTK_M(DAY))
#define DTK_TIME_M      (DTK_M(HOUR) | DTK_M(MINUTE) | DTK_M(SECOND))

#define AM  0
#define PM  1
#define HR24 2
#define AD  0
#define BC  1

/* error codes, same meanings as PostgreSQL's DTERR_* */
#define DTERR_BAD_FORMAT        (-1)
#define DTERR_FIELD_OVERFLOW    (-2)
#define DTERR_MD_FIELD_OVERFLOW (-3)
#define DTERR_INTERVAL_OVERFLOW (-4)
#define DTERR_TZDISP_OVERFLOW   (-5)
/* not one of PostgreSQL's: it reports the bad zone name from inside the
 * decoder, which cannot be done here without a channel back to the caller. */
#define DTERR_TZ_UNKNOWN        (-6)
static char dt_bad_tz[80];

#define MAXDATEFIELDS  25
#define MAXDATELEN     128
#define TOKMAXLEN      10

struct pg_tm
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;                 /* origin 1, not 0 */
    int tm_year;                /* absolute year; 1 BC == 0, 2 BC == -1 */
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

typedef int64 fsec_t;

static const int day_tab[2][13] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0}
};

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

/* ------------------------------------------------------------------ *
 *  Error reporting
 *
 *  The public API hands back `const char *` messages, so the text lives in a
 *  per-thread buffer owned by this module.  Callers must copy it if it has
 *  to outlive the next dt_* call on the same thread.
 * ------------------------------------------------------------------ */
static __thread char dt_errbuf[MAXDATELEN + 160];

static const char *
dt_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dt_errbuf, sizeof(dt_errbuf), fmt, ap);
    va_end(ap);
    return dt_errbuf;
}

const char *
dt_last_error(void)
{
    return dt_errbuf;
}

static int dt_errcode;

/* The DTERR_* code behind the last dt_* parse failure, so the caller can tell
 * a month/day overflow (which earns the datestyle hint) from the rest. */
int
dt_last_error_kind(void)
{
    return dt_errcode;
}

/* PostgreSQL's DateTimeParseError(), primary messages only */
static const char *
dt_parse_error(int dterr, const char *str, const char *datatype)
{
    dt_errcode = dterr;
    switch (dterr)
    {
        case DTERR_TZ_UNKNOWN:
            return dt_err("time zone \"%s\" not recognized", dt_bad_tz);
        case DTERR_FIELD_OVERFLOW:
        case DTERR_MD_FIELD_OVERFLOW:
            return dt_err("date/time field value out of range: \"%s\"", str);
        case DTERR_INTERVAL_OVERFLOW:
            return dt_err("interval field value out of range: \"%s\"", str);
        case DTERR_TZDISP_OVERFLOW:
            return dt_err("time zone displacement out of range: \"%s\"", str);
        case DTERR_BAD_FORMAT:
        default:
            return dt_err("invalid input syntax for type %s: \"%s\"",
                          datatype, str);
    }
}

/* ------------------------------------------------------------------ *
 *  Small utilities
 * ------------------------------------------------------------------ */
static void *
dt_alloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL)
    {
        fprintf(stderr, "dtime: out of memory\n");
        abort();
    }
    return p;
}

static char *
dt_strdup_n(const char *s, size_t n)
{
    char *p = dt_alloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static int
dt_tolower(int c)
{
    return tolower((unsigned char) c);
}

/* strtol into an int, flagging ERANGE for out-of-int values */
static int
dt_strtoint(const char *s, char **endptr, int base)
{
    long val;

    errno = 0;
    val = strtol(s, endptr, base);
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN)
    {
        errno = ERANGE;
        return val > 0 ? INT_MAX : INT_MIN;
    }
    return (int) val;
}

/* zero-padded unsigned decimal, PostgreSQL's pg_ultostr_zeropad */
static char *
dt_ultostr_zeropad(char *str, unsigned long value, int minwidth)
{
    char tmp[32];
    int len = 0;

    if (value == 0)
        tmp[len++] = '0';
    while (value > 0)
    {
        tmp[len++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (len < minwidth)
        tmp[len++] = '0';
    while (len > 0)
        *str++ = tmp[--len];
    return str;
}

/* ------------------------------------------------------------------ *
 *  Julian day conversions - PostgreSQL's algorithms verbatim
 * ------------------------------------------------------------------ */
static int
date2j(int y, int m, int d)
{
    int julian;
    int century;

    if (m > 2)
    {
        m += 1;
        y += 4800;
    }
    else
    {
        m += 13;
        y += 4799;
    }

    century = y / 100;
    julian = y * 365 - 32167;
    julian += y / 4 - century + century / 4;
    julian += 7834 * m / 256 + d;

    return julian;
}

static void
j2date(int jd, int *year, int *month, int *day)
{
    unsigned int julian;
    unsigned int quad;
    unsigned int extra;
    int y;

    julian = (unsigned int) jd;
    julian += 32044;
    quad = julian / 146097;
    extra = (julian - quad * 146097) * 4 + 3;
    julian += 60 + quad * 3 + extra / 146097;
    quad = julian / 1461;
    julian -= quad * 1461;
    y = julian * 4 / 1461;
    julian = ((y != 0) ? ((julian + 305) % 365) : ((julian + 306) % 366)) + 123;
    y += quad * 4;
    *year = y - 4800;
    quad = julian * 2141 / 65536;
    *day = julian - 7834 * quad / 256;
    *month = (quad + 10) % MONTHS_PER_YEAR + 1;
}

/* 0 == Sunday */
static int
j2day(int date)
{
    date += 1;
    date %= 7;
    if (date < 0)
        date += 7;
    return date;
}

void
dt_date_to_ymd(int32 day, int *y, int *m, int *d)
{
    j2date((int) day + POSTGRES_EPOCH_JDATE, y, m, d);
}

int32
dt_ymd_to_date(int y, int m, int d)
{
    return (int32) (date2j(y, m, d) - POSTGRES_EPOCH_JDATE);
}

/* ------------------------------------------------------------------ *
 *  tm <-> internal conversions
 * ------------------------------------------------------------------ */
static void
ClearPgTm(struct pg_tm *tm, fsec_t *fsec)
{
    tm->tm_year = 0;
    tm->tm_mon = 0;
    tm->tm_mday = 0;
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    tm->tm_yday = 0;
    tm->tm_wday = 0;
    tm->tm_isdst = -1;
    *fsec = 0;
}

static int64
time2t(const int hour, const int min, const int sec, const fsec_t fsec)
{
    return (((((hour * INT64_C(60)) + min) * INT64_C(60)) + sec) * USECS_PER_SEC) + fsec;
}

static void
dt2time(int64 jd, int *hour, int *min, int *sec, fsec_t *fsec)
{
    int64 time = jd;

    *hour = (int) (time / USECS_PER_HOUR);
    time -= (*hour) * USECS_PER_HOUR;
    *min = (int) (time / USECS_PER_MINUTE);
    time -= (*min) * USECS_PER_MINUTE;
    *sec = (int) (time / USECS_PER_SEC);
    *fsec = time - (*sec * USECS_PER_SEC);
}

/* returns 0 on success, -1 on out-of-range */
static int
timestamp2tm(int64 dt, int *tzp, struct pg_tm *tm, fsec_t *fsec)
{
    int64 date, time;

    time = dt;
    date = time / USECS_PER_DAY;
    time -= date * USECS_PER_DAY;
    if (time < INT64_C(0))
    {
        time += USECS_PER_DAY;
        date -= 1;
    }

    date += POSTGRES_EPOCH_JDATE;

    if (date < 0 || date > (int64) INT_MAX)
        return -1;

    j2date((int) date, &tm->tm_year, &tm->tm_mon, &tm->tm_mday);
    dt2time(time, &tm->tm_hour, &tm->tm_min, &tm->tm_sec, fsec);
    tm->tm_isdst = 0;

    if (tzp != NULL)
        *tzp = 0;               /* UTC only */
    return 0;
}

/* returns 0 on success, -1 on overflow */
static int
tm2timestamp(struct pg_tm *tm, fsec_t fsec, int *tzp, int64 *result)
{
    int64 date;
    int64 time;

    if (!IS_VALID_JULIAN(tm->tm_year, tm->tm_mon, tm->tm_mday))
    {
        *result = 0;
        return -1;
    }

    date = date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) - POSTGRES_EPOCH_JDATE;
    time = time2t(tm->tm_hour, tm->tm_min, tm->tm_sec, fsec);

    *result = date * USECS_PER_DAY + time;
    if ((*result - time) / USECS_PER_DAY != date)
    {
        *result = 0;
        return -1;
    }
    if ((*result < 0 && date > 0) || (*result > 0 && date < -1))
    {
        *result = 0;
        return -1;
    }
    if (tzp != NULL)
        *result -= (int64) (-(*tzp)) * USECS_PER_SEC;

    if (!IS_VALID_TIMESTAMP(*result))
    {
        *result = 0;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 *  Keyword tables (must stay sorted; lookups are binary searches on the
 *  first TOKMAXLEN characters, exactly like PostgreSQL)
 * ------------------------------------------------------------------ */
typedef struct
{
    const char *token;
    int type;
    int value;
} DtToken;

/* PostgreSQL's datetktbl, trimmed to the tokens meaningful under the fixed
 * UTC / 'ISO, MDY' configuration. */
static const DtToken datetktbl[] = {
    {"-infinity", RESERV, DTK_EARLY},
    {"ad", ADBC, AD},
    {"allballs", RESERV, DTK_ZULU},
    {"am", AMPM, AM},
    {"apr", MONTH, 4},
    {"april", MONTH, 4},
    {"at", IGNORE_DTF, 0},
    {"aug", MONTH, 8},
    {"august", MONTH, 8},
    {"bc", ADBC, BC},
    {"dec", MONTH, 12},
    {"december", MONTH, 12},
    {"epoch", RESERV, DTK_EPOCH},
    {"feb", MONTH, 2},
    {"february", MONTH, 2},
    {"fri", DOW, 5},
    {"friday", DOW, 5},
    {"gmt", TZ, 0},
    {"infinity", RESERV, DTK_LATE},
    {"jan", MONTH, 1},
    {"january", MONTH, 1},
    {"jul", MONTH, 7},
    {"july", MONTH, 7},
    {"jun", MONTH, 6},
    {"june", MONTH, 6},
    {"mar", MONTH, 3},
    {"march", MONTH, 3},
    {"may", MONTH, 5},
    {"mon", DOW, 1},
    {"monday", DOW, 1},
    {"nov", MONTH, 11},
    {"november", MONTH, 11},
    {"oct", MONTH, 10},
    {"october", MONTH, 10},
    {"on", IGNORE_DTF, 0},
    {"pm", AMPM, PM},
    {"sat", DOW, 6},
    {"saturday", DOW, 6},
    {"sep", MONTH, 9},
    {"sept", MONTH, 9},
    {"september", MONTH, 9},
    {"sun", DOW, 0},
    {"sunday", DOW, 0},
    {"t", ISOTIME, DTK_TIME},
    {"thu", DOW, 4},
    {"thur", DOW, 4},
    {"thurs", DOW, 4},
    {"thursday", DOW, 4},
    {"tue", DOW, 2},
    {"tues", DOW, 2},
    {"tuesday", DOW, 2},
    {"ut", TZ, 0},
    {"utc", TZ, 0},
    {"wed", DOW, 3},
    {"wednesday", DOW, 3},
    {"weds", DOW, 3},
    {"z", TZ, 0},
    {"zulu", TZ, 0},
};

/* PostgreSQL's deltatktbl: units recognised inside interval literals and by
 * extract()/date_part()/date_trunc(). */
static const DtToken deltatktbl[] = {
    {"@", IGNORE_DTF, 0},
    {"ago", AGO, 0},
    {"c", UNITS, DTK_CENTURY},
    {"cent", UNITS, DTK_CENTURY},
    {"centuries", UNITS, DTK_CENTURY},
    {"century", UNITS, DTK_CENTURY},
    {"d", UNITS, DTK_DAY},
    {"day", UNITS, DTK_DAY},
    {"days", UNITS, DTK_DAY},
    {"dec", UNITS, DTK_DECADE},
    {"decade", UNITS, DTK_DECADE},
    {"decades", UNITS, DTK_DECADE},
    {"decs", UNITS, DTK_DECADE},
    {"h", UNITS, DTK_HOUR},
    {"hour", UNITS, DTK_HOUR},
    {"hours", UNITS, DTK_HOUR},
    {"hr", UNITS, DTK_HOUR},
    {"hrs", UNITS, DTK_HOUR},
    {"m", UNITS, DTK_MINUTE},
    {"microsecon", UNITS, DTK_MICROSEC},
    {"mil", UNITS, DTK_MILLENNIUM},
    {"millennia", UNITS, DTK_MILLENNIUM},
    {"millennium", UNITS, DTK_MILLENNIUM},
    {"millisecon", UNITS, DTK_MILLISEC},
    {"mils", UNITS, DTK_MILLENNIUM},
    {"min", UNITS, DTK_MINUTE},
    {"mins", UNITS, DTK_MINUTE},
    {"minute", UNITS, DTK_MINUTE},
    {"minutes", UNITS, DTK_MINUTE},
    {"mon", UNITS, DTK_MONTH},
    {"mons", UNITS, DTK_MONTH},
    {"month", UNITS, DTK_MONTH},
    {"months", UNITS, DTK_MONTH},
    {"ms", UNITS, DTK_MILLISEC},
    {"msec", UNITS, DTK_MILLISEC},
    {"msecond", UNITS, DTK_MILLISEC},
    {"mseconds", UNITS, DTK_MILLISEC},
    {"msecs", UNITS, DTK_MILLISEC},
    {"qtr", UNITS, DTK_QUARTER},
    {"quarter", UNITS, DTK_QUARTER},
    {"s", UNITS, DTK_SECOND},
    {"sec", UNITS, DTK_SECOND},
    {"second", UNITS, DTK_SECOND},
    {"seconds", UNITS, DTK_SECOND},
    {"secs", UNITS, DTK_SECOND},
    {"timezone", UNITS, DTK_TZ},
    {"timezone_h", UNITS, DTK_TZ_HOUR},
    {"timezone_m", UNITS, DTK_TZ_MINUTE},
    {"us", UNITS, DTK_MICROSEC},
    {"usec", UNITS, DTK_MICROSEC},
    {"usecond", UNITS, DTK_MICROSEC},
    {"useconds", UNITS, DTK_MICROSEC},
    {"usecs", UNITS, DTK_MICROSEC},
    {"w", UNITS, DTK_WEEK},
    {"week", UNITS, DTK_WEEK},
    {"weeks", UNITS, DTK_WEEK},
    {"y", UNITS, DTK_YEAR},
    {"year", UNITS, DTK_YEAR},
    {"years", UNITS, DTK_YEAR},
    {"yr", UNITS, DTK_YEAR},
    {"yrs", UNITS, DTK_YEAR},
};

/* Unit names extract()/date_part()/date_trunc() additionally recognise: they
 * consult datetktbl too, which is where these live in PostgreSQL. */
static const DtToken extratktbl[] = {
    {"dow", UNITS, DTK_DOW},
    {"doy", UNITS, DTK_DOY},
    {"epoch", UNITS, DTK_EPOCH},
    {"isodow", UNITS, DTK_ISODOW},
    {"isoyear", UNITS, DTK_ISOYEAR},
    {"jd", UNITS, DTK_JULIAN},
    {"julian", UNITS, DTK_JULIAN},
};

static int
dt_tokcmp(const char *key, const char *tok)
{
    return strncmp(key, tok, TOKMAXLEN);
}

static int
datebsearch(const char *key, const DtToken *base, int nel, int *value)
{
    int lo = 0, hi = nel - 1;

    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        int cmp = dt_tokcmp(key, base[mid].token);

        if (cmp == 0)
        {
            *value = base[mid].value;
            return base[mid].type;
        }
        if (cmp < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return UNKNOWN_FIELD;
}

static int
DecodeSpecial(const char *lowtoken, int *val)
{
    return datebsearch(lowtoken, datetktbl,
                       (int) (sizeof(datetktbl) / sizeof(datetktbl[0])), val);
}

static int
DecodeUnits(const char *lowtoken, int *val)
{
    return datebsearch(lowtoken, deltatktbl,
                       (int) (sizeof(deltatktbl) / sizeof(deltatktbl[0])), val);
}

/* unit lookup used by extract()/date_part()/date_trunc() */
static int
DecodeAnyUnits(const char *lowtoken, int *val)
{
    int type = DecodeUnits(lowtoken, val);
    int nel = (int) (sizeof(extratktbl) / sizeof(extratktbl[0]));

    if (type != UNKNOWN_FIELD)
        return type;
    return datebsearch(lowtoken, extratktbl, nel, val);
}

/* ------------------------------------------------------------------ *
 *  ParseDateTime - split the input into typed fields
 * ------------------------------------------------------------------ */
static int
ParseDateTime(const char *timestr, char *workbuf, size_t buflen,
              char **field, int *ftype, int maxfields, int *numfields)
{
    int nf = 0;
    const char *cp = timestr;
    char *bufp = workbuf;
    const char *bufend = workbuf + buflen;

#define APPEND_CHAR(bp, be, c)                  \
    do {                                        \
        if (((bp) + 1) >= (be))                 \
            return DTERR_BAD_FORMAT;            \
        *(bp)++ = (char) (c);                   \
    } while (0)

    while (*cp != '\0')
    {
        if (isspace((unsigned char) *cp))
        {
            cp++;
            continue;
        }

        if (nf >= maxfields)
            return DTERR_BAD_FORMAT;
        field[nf] = bufp;

        if (isdigit((unsigned char) *cp))
        {
            APPEND_CHAR(bufp, bufend, *cp++);
            while (isdigit((unsigned char) *cp))
                APPEND_CHAR(bufp, bufend, *cp++);

            if (*cp == ':')
            {
                ftype[nf] = DTK_TIME;
                do
                {
                    APPEND_CHAR(bufp, bufend, *cp++);
                } while (*cp == ':' || *cp == '.' || isdigit((unsigned char) *cp));
            }
            else if (*cp == '-' || *cp == '/' || *cp == '.')
            {
                char delim = *cp;

                APPEND_CHAR(bufp, bufend, *cp++);
                if (isdigit((unsigned char) *cp))
                {
                    ftype[nf] = ((delim == '.') ? DTK_NUMBER : DTK_DATE);
                    while (isdigit((unsigned char) *cp))
                        APPEND_CHAR(bufp, bufend, *cp++);

                    /* delimiters must match to make a three-field date */
                    if (*cp == delim)
                    {
                        ftype[nf] = DTK_DATE;
                        APPEND_CHAR(bufp, bufend, *cp++);
                        while (isdigit((unsigned char) *cp) || *cp == delim)
                            APPEND_CHAR(bufp, bufend, *cp++);
                    }
                }
                else
                {
                    ftype[nf] = DTK_DATE;
                    while (isalnum((unsigned char) *cp) || *cp == delim)
                        APPEND_CHAR(bufp, bufend, dt_tolower(*cp++));
                }
            }
            else
                ftype[nf] = DTK_NUMBER;
        }
        else if (*cp == '.')
        {
            APPEND_CHAR(bufp, bufend, *cp++);
            while (isdigit((unsigned char) *cp))
                APPEND_CHAR(bufp, bufend, *cp++);
            ftype[nf] = DTK_NUMBER;
        }
        else if (isalpha((unsigned char) *cp))
        {
            bool is_date;

            ftype[nf] = DTK_STRING;
            APPEND_CHAR(bufp, bufend, dt_tolower(*cp++));
            while (isalpha((unsigned char) *cp))
                APPEND_CHAR(bufp, bufend, dt_tolower(*cp++));

            /*
             * Dates can have embedded '-', '/' or '.' separators.  If the
             * next character is a digit or '+', check whether what we have so
             * far is a recognised keyword; if it is, this is not a date.
             */
            is_date = false;
            if (*cp == '-' || *cp == '/' || *cp == '.')
                is_date = true;
            else if (*cp == '+' || isdigit((unsigned char) *cp))
            {
                int val;

                if (bufp >= bufend)
                    return DTERR_BAD_FORMAT;
                *bufp = '\0';
                if (DecodeSpecial(field[nf], &val) == UNKNOWN_FIELD)
                    is_date = true;
            }
            if (is_date)
            {
                ftype[nf] = DTK_DATE;
                do
                {
                    APPEND_CHAR(bufp, bufend, dt_tolower(*cp++));
                } while (*cp == '+' || *cp == '-' || *cp == '/' || *cp == '_' ||
                         *cp == '.' || *cp == ':' || isalnum((unsigned char) *cp));
            }
        }
        else if (*cp == '+' || *cp == '-')
        {
            APPEND_CHAR(bufp, bufend, *cp++);
            while (isspace((unsigned char) *cp))
                cp++;
            if (isdigit((unsigned char) *cp))
            {
                ftype[nf] = DTK_TZ;
                APPEND_CHAR(bufp, bufend, *cp++);
                while (isdigit((unsigned char) *cp) || *cp == ':' ||
                       *cp == '.' || *cp == '-')
                    APPEND_CHAR(bufp, bufend, *cp++);
            }
            else if (isalpha((unsigned char) *cp))
            {
                ftype[nf] = DTK_SPECIAL;
                APPEND_CHAR(bufp, bufend, dt_tolower(*cp++));
                while (isalpha((unsigned char) *cp))
                    APPEND_CHAR(bufp, bufend, dt_tolower(*cp++));
            }
            else
                return DTERR_BAD_FORMAT;
        }
        else if (ispunct((unsigned char) *cp))
        {
            cp++;
            continue;
        }
        else
            return DTERR_BAD_FORMAT;

        APPEND_CHAR(bufp, bufend, '\0');
        nf++;
    }
#undef APPEND_CHAR

    *numfields = nf;
    return 0;
}

/* ------------------------------------------------------------------ *
 *  Numeric helpers used by the decoders
 * ------------------------------------------------------------------ */
static int
ParseFraction(char *cp, double *frac)
{
    errno = 0;
    *frac = strtod(cp, &cp);
    if (*cp != '\0' || errno != 0)
        return DTERR_BAD_FORMAT;
    return 0;
}

static int
ParseFractionalSecond(char *cp, fsec_t *fsec)
{
    double frac;
    int dterr = ParseFraction(cp, &frac);

    if (dterr)
        return dterr;
    *fsec = (fsec_t) rint(frac * 1000000);
    return 0;
}

static int
DecodeTimezone(const char *str, int *tzp)
{
    int tz;
    int hr, min, sec = 0;
    char *cp;

    if (*str != '+' && *str != '-')
        return DTERR_BAD_FORMAT;

    errno = 0;
    hr = dt_strtoint(str + 1, &cp, 10);
    if (errno == ERANGE)
        return DTERR_TZDISP_OVERFLOW;

    if (*cp == ':')
    {
        errno = 0;
        min = dt_strtoint(cp + 1, &cp, 10);
        if (errno == ERANGE)
            return DTERR_TZDISP_OVERFLOW;
        if (*cp == ':')
        {
            errno = 0;
            sec = dt_strtoint(cp + 1, &cp, 10);
            if (errno == ERANGE)
                return DTERR_TZDISP_OVERFLOW;
        }
    }
    else if (*cp == '\0' && strlen(str) > 3)
    {
        min = hr % 100;
        hr = hr / 100;
    }
    else
        min = 0;

    if (hr < 0 || hr > MAX_TZDISP_HOUR)
        return DTERR_TZDISP_OVERFLOW;
    if (min < 0 || min >= MINS_PER_HOUR)
        return DTERR_TZDISP_OVERFLOW;
    if (sec < 0 || sec >= SECS_PER_MINUTE)
        return DTERR_TZDISP_OVERFLOW;

    tz = (hr * MINS_PER_HOUR + min) * SECS_PER_MINUTE + sec;
    if (*str == '-')
        tz = -tz;

    *tzp = -tz;

    if (*cp != '\0')
        return DTERR_BAD_FORMAT;
    return 0;
}

/*
 * Shared body of DecodeTime()/DecodeTimeForInterval().  tm_hour is int64 here
 * because an interval's hour field is unbounded.
 */
static int
DecodeTimeCommon(char *str, unsigned int fmask, int range, unsigned int *tmask,
                 int64 *out_hour, int *out_min, int *out_sec, fsec_t *out_fsec)
{
    char *cp;
    int dterr;
    fsec_t fsec = 0;
    int64 hour;
    int min, sec;

    (void) fmask;
    (void) range;
    *tmask = DTK_TIME_M;

    errno = 0;
    hour = strtoll(str, &cp, 10);
    if (errno == ERANGE)
        return DTERR_FIELD_OVERFLOW;
    if (*cp != ':')
        return DTERR_BAD_FORMAT;
    errno = 0;
    min = dt_strtoint(cp + 1, &cp, 10);
    if (errno == ERANGE)
        return DTERR_FIELD_OVERFLOW;
    if (*cp == '\0')
        sec = 0;
    else if (*cp == '.')
    {
        /* always assume mm:ss.sss is MINUTE TO SECOND */
        dterr = ParseFractionalSecond(cp, &fsec);
        if (dterr)
            return dterr;
        if (hour > INT_MAX || hour < INT_MIN)
            return DTERR_FIELD_OVERFLOW;
        sec = min;
        min = (int) hour;
        hour = 0;
    }
    else if (*cp == ':')
    {
        errno = 0;
        sec = dt_strtoint(cp + 1, &cp, 10);
        if (errno == ERANGE)
            return DTERR_FIELD_OVERFLOW;
        if (*cp == '.')
        {
            dterr = ParseFractionalSecond(cp, &fsec);
            if (dterr)
                return dterr;
        }
        else if (*cp != '\0')
            return DTERR_BAD_FORMAT;
    }
    else
        return DTERR_BAD_FORMAT;

    /* sanity check; the caller must check the range of tm_hour */
    if (hour < 0 || min < 0 || min > MINS_PER_HOUR - 1 ||
        sec < 0 || sec > SECS_PER_MINUTE ||
        fsec < 0 || fsec > USECS_PER_SEC)
        return DTERR_FIELD_OVERFLOW;

    *out_hour = hour;
    *out_min = min;
    *out_sec = sec;
    *out_fsec = fsec;
    return 0;
}

static int
DecodeTime(char *str, unsigned int fmask, int range,
           unsigned int *tmask, struct pg_tm *tm, fsec_t *fsec)
{
    int64 hour;
    int dterr = DecodeTimeCommon(str, fmask, range, tmask,
                                 &hour, &tm->tm_min, &tm->tm_sec, fsec);

    if (dterr)
        return dterr;
    if (hour > INT_MAX)
        return DTERR_FIELD_OVERFLOW;
    tm->tm_hour = (int) hour;
    return 0;
}

/* interval time fields have no 24-hour limit; result is total microseconds */
static int
DecodeTimeForInterval(char *str, unsigned int fmask, int range,
                      unsigned int *tmask, int64 *usec)
{
    int64 hour;
    int min, sec;
    fsec_t fsec;
    int dterr = DecodeTimeCommon(str, fmask, range, tmask,
                                 &hour, &min, &sec, &fsec);

    if (dterr)
        return dterr;
    if (hour > INT64_MAX / USECS_PER_HOUR || hour < INT64_MIN / USECS_PER_HOUR)
        return DTERR_FIELD_OVERFLOW;
    *usec = hour * USECS_PER_HOUR + (int64) min * USECS_PER_MINUTE +
            (int64) sec * USECS_PER_SEC + fsec;
    return 0;
}

/*
 * PostgreSQL's time_overflows(): 24:00:00 exactly is legal, anything past it
 * is not, and the individual fields have their own limits.
 */
static bool
time_overflows(int hour, int min, int sec, fsec_t fsec)
{
    if (hour < 0 || hour > HOURS_PER_DAY ||
        min < 0 || min >= MINS_PER_HOUR ||
        sec < 0 || sec > SECS_PER_MINUTE ||
        fsec < 0 || fsec > USECS_PER_SEC)
        return true;

    if ((((((int64) hour * MINS_PER_HOUR + min) * SECS_PER_MINUTE) + sec)
         * USECS_PER_SEC) + fsec > USECS_PER_DAY)
        return true;

    return false;
}

static int
DecodeNumberField(int len, char *str, unsigned int fmask,
                  unsigned int *tmask, struct pg_tm *tm, fsec_t *fsec,
                  bool *is2digits)
{
    char *cp;

    if ((cp = strchr(str, '.')) != NULL)
    {
        double frac;

        errno = 0;
        frac = strtod(cp, NULL);
        if (errno != 0)
            return DTERR_BAD_FORMAT;
        *fsec = (fsec_t) rint(frac * 1000000);
        *cp = '\0';
        len = (int) strlen(str);
    }
    else if ((fmask & DTK_DATE_M) != DTK_DATE_M)
    {
        if (len >= 6)
        {
            *tmask = DTK_DATE_M;

            tm->tm_mday = atoi(str + (len - 2));
            *(str + (len - 2)) = '\0';
            tm->tm_mon = atoi(str + (len - 4));
            *(str + (len - 4)) = '\0';
            tm->tm_year = atoi(str);
            if ((len - 4) == 2)
                *is2digits = true;

            return DTK_DATE;
        }
    }

    if ((fmask & DTK_TIME_M) != DTK_TIME_M)
    {
        if (len == 6)
        {
            *tmask = DTK_TIME_M;
            tm->tm_sec = atoi(str + 4);
            *(str + 4) = '\0';
            tm->tm_min = atoi(str + 2);
            *(str + 2) = '\0';
            tm->tm_hour = atoi(str);
            return DTK_TIME;
        }
        else if (len == 4)
        {
            *tmask = DTK_TIME_M;
            tm->tm_sec = 0;
            tm->tm_min = atoi(str + 2);
            *(str + 2) = '\0';
            tm->tm_hour = atoi(str);
            return DTK_TIME;
        }
    }

    return DTERR_BAD_FORMAT;
}

static int
DecodeNumber(int flen, char *str, bool haveTextMonth, unsigned int fmask,
             unsigned int *tmask, struct pg_tm *tm, fsec_t *fsec,
             bool *is2digits)
{
    int val;
    char *cp;
    int dterr;

    *tmask = 0;

    errno = 0;
    val = dt_strtoint(str, &cp, 10);
    if (errno == ERANGE)
        return DTERR_FIELD_OVERFLOW;
    if (cp == str)
        return DTERR_BAD_FORMAT;

    if (*cp == '.')
    {
        /*
         * More than two digits before the decimal point?  Then it could be a
         * date or a run-together time: 2001.360  20011225  040506.789
         */
        if (cp - str > 2)
        {
            dterr = DecodeNumberField(flen, str, (fmask | DTK_DATE_M),
                                      tmask, tm, fsec, is2digits);
            if (dterr < 0)
                return dterr;
            return 0;
        }

        dterr = ParseFractionalSecond(cp, fsec);
        if (dterr)
            return dterr;
    }
    else if (*cp != '\0')
        return DTERR_BAD_FORMAT;

    /* Special case for day of year */
    if (flen == 3 && (fmask & DTK_DATE_M) == DTK_M(YEAR) &&
        val >= 1 && val <= 366)
    {
        *tmask = (DTK_M(DOY) | DTK_M(MONTH) | DTK_M(DAY));
        tm->tm_yday = val;
        return 0;
    }

    /* DateOrder is always MDY for this server */
    switch (fmask & DTK_DATE_M)
    {
        case 0:
            if (flen >= 3)
            {
                *tmask = DTK_M(YEAR);
                tm->tm_year = val;
            }
            else
            {
                *tmask = DTK_M(MONTH);
                tm->tm_mon = val;
            }
            break;

        case (DTK_M(YEAR)):
            *tmask = DTK_M(MONTH);
            tm->tm_mon = val;
            break;

        case (DTK_M(MONTH)):
            if (haveTextMonth)
            {
                if (flen >= 3)
                {
                    *tmask = DTK_M(YEAR);
                    tm->tm_year = val;
                }
                else
                {
                    *tmask = DTK_M(DAY);
                    tm->tm_mday = val;
                }
                break;
            }
            *tmask = DTK_M(DAY);
            tm->tm_mday = val;
            break;

        case (DTK_M(YEAR) | DTK_M(MONTH)):
            if (haveTextMonth)
            {
                if (flen >= 3 && *is2digits)
                {
                    /* the guess that the first numeric field was the day was wrong */
                    *tmask = DTK_M(DAY);
                    tm->tm_mday = tm->tm_year;
                    tm->tm_year = val;
                    *is2digits = false;
                }
                else
                {
                    *tmask = DTK_M(DAY);
                    tm->tm_mday = val;
                }
                break;
            }
            *tmask = DTK_M(DAY);
            tm->tm_mday = val;
            break;

        case (DTK_M(DAY)):
            *tmask = DTK_M(MONTH);
            tm->tm_mon = val;
            break;

        case (DTK_M(MONTH) | DTK_M(DAY)):
            *tmask = DTK_M(YEAR);
            tm->tm_year = val;
            break;

        case (DTK_M(YEAR) | DTK_M(MONTH) | DTK_M(DAY)):
            /* we have the whole date, so this must be a time field */
            dterr = DecodeNumberField(flen, str, fmask, tmask, tm, fsec,
                                      is2digits);
            if (dterr < 0)
                return dterr;
            return 0;

        default:
            return DTERR_BAD_FORMAT;
    }

    if (*tmask == DTK_M(YEAR))
        *is2digits = (flen <= 2);

    return 0;
}

static int
DecodeDate(char *str, unsigned int fmask, unsigned int *tmask,
           bool *is2digits, struct pg_tm *tm)
{
    fsec_t fsec;
    int nf = 0;
    int i, len;
    int dterr;
    bool haveTextMonth = false;
    int type, val;
    unsigned int dmask = 0;
    char *field[MAXDATEFIELDS];

    *tmask = 0;

    while (*str != '\0' && nf < MAXDATEFIELDS)
    {
        while (*str != '\0' && !isalnum((unsigned char) *str))
            str++;
        if (*str == '\0')
            return DTERR_BAD_FORMAT;

        field[nf] = str;
        if (isdigit((unsigned char) *str))
        {
            while (isdigit((unsigned char) *str))
                str++;
        }
        else if (isalpha((unsigned char) *str))
        {
            while (isalpha((unsigned char) *str))
                str++;
        }

        if (*str != '\0')
            *str++ = '\0';
        nf++;
    }

    /* look first for text fields, since that is an unambiguous month */
    for (i = 0; i < nf; i++)
    {
        if (isalpha((unsigned char) *field[i]))
        {
            type = DecodeSpecial(field[i], &val);
            if (type == IGNORE_DTF)
                continue;

            dmask = DTK_M(type);
            switch (type)
            {
                case MONTH:
                    tm->tm_mon = val;
                    haveTextMonth = true;
                    break;
                default:
                    return DTERR_BAD_FORMAT;
            }
            if (fmask & dmask)
                return DTERR_BAD_FORMAT;

            fmask |= dmask;
            *tmask |= dmask;
            field[i] = NULL;
        }
    }

    for (i = 0; i < nf; i++)
    {
        if (field[i] == NULL)
            continue;
        if ((len = (int) strlen(field[i])) <= 0)
            return DTERR_BAD_FORMAT;

        dterr = DecodeNumber(len, field[i], haveTextMonth, fmask,
                             &dmask, tm, &fsec, is2digits);
        if (dterr)
            return dterr;

        if (fmask & dmask)
            return DTERR_BAD_FORMAT;
        fmask |= dmask;
        *tmask |= dmask;
    }

    if ((fmask & ~(DTK_M(DOY) | DTK_M(TZ))) != DTK_DATE_M)
        return DTERR_BAD_FORMAT;

    return 0;
}

static int
ValidateDate(unsigned int fmask, bool isjulian, bool is2digits, bool bc,
             struct pg_tm *tm)
{
    if (fmask & DTK_M(YEAR))
    {
        if (isjulian)
        {
            /* tm_year is already correct */
        }
        else if (bc)
        {
            if (tm->tm_year <= 0)
                return DTERR_FIELD_OVERFLOW;
            /* internally 1 BC is year zero, 2 BC is -1, and so on */
            tm->tm_year = -(tm->tm_year - 1);
        }
        else if (is2digits)
        {
            if (tm->tm_year < 0)
                return DTERR_FIELD_OVERFLOW;
            if (tm->tm_year < 70)
                tm->tm_year += 2000;
            else if (tm->tm_year < 100)
                tm->tm_year += 1900;
        }
        else
        {
            if (tm->tm_year <= 0)
                return DTERR_FIELD_OVERFLOW;
        }
    }

    if (fmask & DTK_M(DOY))
        j2date(date2j(tm->tm_year, 1, 1) + tm->tm_yday - 1,
               &tm->tm_year, &tm->tm_mon, &tm->tm_mday);

    if (fmask & DTK_M(MONTH))
    {
        if (tm->tm_mon < 1 || tm->tm_mon > MONTHS_PER_YEAR)
            return DTERR_MD_FIELD_OVERFLOW;
    }

    if (fmask & DTK_M(DAY))
    {
        if (tm->tm_mday < 1 || tm->tm_mday > 31)
            return DTERR_MD_FIELD_OVERFLOW;
    }

    if ((fmask & DTK_DATE_M) == DTK_DATE_M)
    {
        if (tm->tm_mday > day_tab[isleap(tm->tm_year)][tm->tm_mon - 1])
            return DTERR_FIELD_OVERFLOW;
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 *  DecodeDateTime - the date / timestamp decoder
 * ------------------------------------------------------------------ */
static int
DecodeDateTime(char **field, int *ftype, int nf,
               int *dtype, struct pg_tm *tm, fsec_t *fsec, int *tzp)
{
    unsigned int fmask = 0, tmask;
    int type;
    int ptype = 0;
    int i;
    int val;
    int dterr;
    int mer = HR24;
    bool haveTextMonth = false;
    bool isjulian = false;
    bool is2digits = false;
    bool bc = false;

    *dtype = DTK_DATE;
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    tm->tm_year = 0;
    tm->tm_mon = 0;
    tm->tm_mday = 0;
    tm->tm_yday = 0;
    tm->tm_wday = 0;
    *fsec = 0;
    tm->tm_isdst = -1;
    if (tzp != NULL)
        *tzp = 0;

    for (i = 0; i < nf; i++)
    {
        tmask = 0;
        switch (ftype[i])
        {
            case DTK_DATE:
                /*
                 * Already have a date?  Then this has to be a time zone name
                 * with embedded punctuation.  This server carries no zone
                 * database, so every such name is reported the way PostgreSQL
                 * reports one it cannot find.
                 */
                if (ptype != 0 ||
                    ((fmask & (DTK_M(MONTH) | DTK_M(DAY))) ==
                     (DTK_M(MONTH) | DTK_M(DAY))))
                {
                    if (tzp != NULL && ptype == 0 &&
                        isalpha((unsigned char) field[i][0]))
                    {
                        snprintf(dt_bad_tz, sizeof dt_bad_tz, "%s", field[i]);
                        return DTERR_TZ_UNKNOWN;
                    }
                    return DTERR_BAD_FORMAT;
                }

                dterr = DecodeDate(field[i], fmask, &tmask, &is2digits, tm);
                if (dterr)
                    return dterr;
                break;

            case DTK_TIME:
                /* may follow an ISO "t" filler field */
                if (ptype != 0)
                {
                    if (ptype != DTK_TIME)
                        return DTERR_BAD_FORMAT;
                    ptype = 0;
                }
                dterr = DecodeTime(field[i], (fmask | DTK_DATE_M), 0,
                                   &tmask, tm, fsec);
                if (dterr)
                    return dterr;

                /* check for time overflow */
                if (time_overflows(tm->tm_hour, tm->tm_min, tm->tm_sec, *fsec))
                    return DTERR_FIELD_OVERFLOW;
                break;

            case DTK_TZ:
            {
                int tz;

                if (tzp == NULL)
                    return DTERR_BAD_FORMAT;

                dterr = DecodeTimezone(field[i], &tz);
                if (dterr)
                    return dterr;
                *tzp = tz;
                tmask = DTK_M(TZ);
                break;
            }

            case DTK_NUMBER:
                if (ptype != 0)
                {
                    char *cp;
                    int value;

                    errno = 0;
                    value = dt_strtoint(field[i], &cp, 10);
                    if (errno == ERANGE)
                        return DTERR_FIELD_OVERFLOW;
                    if (*cp == '.')
                    {
                        switch (ptype)
                        {
                            case DTK_JULIAN:
                            case DTK_TIME:
                            case DTK_SECOND:
                                break;
                            default:
                                return DTERR_BAD_FORMAT;
                        }
                    }
                    else if (*cp != '\0')
                        return DTERR_BAD_FORMAT;

                    switch (ptype)
                    {
                        case DTK_YEAR:
                            tm->tm_year = value;
                            tmask = DTK_M(YEAR);
                            break;
                        case DTK_MONTH:
                            tm->tm_mon = value;
                            tmask = DTK_M(MONTH);
                            break;
                        case DTK_DAY:
                            tm->tm_mday = value;
                            tmask = DTK_M(DAY);
                            break;
                        case DTK_HOUR:
                            tm->tm_hour = value;
                            tmask = DTK_M(HOUR);
                            break;
                        case DTK_MINUTE:
                            tm->tm_min = value;
                            tmask = DTK_M(MINUTE);
                            break;
                        case DTK_SECOND:
                            tm->tm_sec = value;
                            tmask = DTK_M(SECOND);
                            if (*cp == '.')
                            {
                                dterr = ParseFractionalSecond(cp, fsec);
                                if (dterr)
                                    return dterr;
                                tmask = DTK_ALL_SECS_M;
                            }
                            break;
                        case DTK_TZ:
                            tmask = DTK_M(TZ);
                            if (tzp == NULL)
                                return DTERR_BAD_FORMAT;
                            dterr = DecodeTimezone(field[i], tzp);
                            if (dterr)
                                return dterr;
                            break;
                        case DTK_JULIAN:
                            if (value < 0)
                                return DTERR_FIELD_OVERFLOW;
                            tmask = DTK_DATE_M;
                            j2date(value, &tm->tm_year, &tm->tm_mon, &tm->tm_mday);
                            isjulian = true;
                            if (*cp == '.')
                            {
                                double time;

                                dterr = ParseFraction(cp, &time);
                                if (dterr)
                                    return dterr;
                                time *= USECS_PER_DAY;
                                dt2time((int64) time, &tm->tm_hour, &tm->tm_min,
                                        &tm->tm_sec, fsec);
                                tmask |= DTK_TIME_M;
                            }
                            break;
                        case DTK_TIME:
                            dterr = DecodeNumberField((int) strlen(field[i]),
                                                      field[i],
                                                      (fmask | DTK_DATE_M),
                                                      &tmask, tm, fsec, &is2digits);
                            if (dterr < 0)
                                return dterr;
                            if (tmask != DTK_TIME_M)
                                return DTERR_BAD_FORMAT;
                            break;
                        default:
                            return DTERR_BAD_FORMAT;
                    }

                    ptype = 0;
                    *dtype = DTK_DATE;
                }
                else
                {
                    char *cp;
                    int flen;

                    flen = (int) strlen(field[i]);
                    cp = strchr(field[i], '.');

                    /* embedded decimal and no date yet? */
                    if (cp != NULL && !(fmask & DTK_DATE_M))
                    {
                        dterr = DecodeDate(field[i], fmask, &tmask, &is2digits, tm);
                        if (dterr)
                            return dterr;
                    }
                    /* embedded decimal and several digits before? */
                    else if (cp != NULL && flen - (int) strlen(cp) > 2)
                    {
                        dterr = DecodeNumberField(flen, field[i], fmask,
                                                  &tmask, tm, fsec, &is2digits);
                        if (dterr < 0)
                            return dterr;
                    }
                    else if (flen > 4)
                    {
                        dterr = DecodeNumberField(flen, field[i], fmask,
                                                  &tmask, tm, fsec, &is2digits);
                        if (dterr < 0)
                            return dterr;
                    }
                    else
                    {
                        dterr = DecodeNumber(flen, field[i], haveTextMonth, fmask,
                                             &tmask, tm, fsec, &is2digits);
                        if (dterr)
                            return dterr;
                    }
                }
                break;

            case DTK_STRING:
            case DTK_SPECIAL:
                type = DecodeSpecial(field[i], &val);
                if (type == IGNORE_DTF)
                    continue;

                tmask = DTK_M(type);
                switch (type)
                {
                    case RESERV:
                        switch (val)
                        {
                            case DTK_EARLY:
                            case DTK_LATE:
                                tmask = DTK_DATE_M | DTK_TIME_M;
                                *dtype = val;
                                break;
                            case DTK_EPOCH:
                                tmask = DTK_DATE_M | DTK_TIME_M;
                                *dtype = DTK_DATE;
                                tm->tm_year = 1970;
                                tm->tm_mon = 1;
                                tm->tm_mday = 1;
                                tm->tm_hour = 0;
                                tm->tm_min = 0;
                                tm->tm_sec = 0;
                                *fsec = 0;
                                isjulian = true;
                                break;
                            case DTK_ZULU:
                                tmask = (DTK_TIME_M | DTK_M(TZ));
                                *dtype = DTK_DATE;
                                tm->tm_hour = 0;
                                tm->tm_min = 0;
                                tm->tm_sec = 0;
                                if (tzp != NULL)
                                    *tzp = 0;
                                break;
                            default:
                                return DTERR_BAD_FORMAT;
                        }
                        break;

                    case MONTH:
                        /*
                         * Already have a (numeric) month?  Then see whether
                         * we can substitute.
                         */
                        if ((fmask & DTK_M(MONTH)) && !haveTextMonth &&
                            !(fmask & DTK_M(DAY)) && tm->tm_mon >= 1 &&
                            tm->tm_mon <= 31)
                        {
                            tm->tm_mday = tm->tm_mon;
                            tmask = DTK_M(DAY);
                        }
                        haveTextMonth = true;
                        tm->tm_mon = val;
                        break;

                    case DTZ:
                    case TZ:
                        if (tzp == NULL)
                            return DTERR_BAD_FORMAT;
                        *tzp = val;
                        tmask = DTK_M(TZ);
                        break;

                    case IGNORE_DTF:
                        break;

                    case AMPM:
                        mer = val;
                        break;

                    case ADBC:
                        bc = (val == BC);
                        break;

                    case DOW:
                        tm->tm_wday = val;
                        break;

                    case UNITS:
                        tmask = 0;
                        ptype = val;
                        break;

                    case ISOTIME:
                        /*
                         * Filler field "t" indicating that the next field is a
                         * time.  Verify that this is sensible.
                         */
                        tmask = 0;
                        if ((fmask & DTK_DATE_M) != DTK_DATE_M)
                            return DTERR_BAD_FORMAT;
                        if (i >= nf - 1 ||
                            (ftype[i + 1] != DTK_NUMBER &&
                             ftype[i + 1] != DTK_TIME &&
                             ftype[i + 1] != DTK_DATE))
                            return DTERR_BAD_FORMAT;
                        ptype = val;
                        break;

                    default:
                        return DTERR_BAD_FORMAT;
                }
                break;

            default:
                return DTERR_BAD_FORMAT;
        }

        if (tmask & fmask)
            return DTERR_BAD_FORMAT;
        fmask |= tmask;
    }

    if (ptype != 0)
        return DTERR_BAD_FORMAT;

    /* final checking and adjustment of Y/M/D fields */
    dterr = ValidateDate(fmask, isjulian, is2digits, bc, tm);
    if (dterr)
        return dterr;

    /* handle AM/PM */
    if (mer != HR24 && tm->tm_hour > HOURS_PER_DAY / 2)
        return DTERR_FIELD_OVERFLOW;
    if (mer == AM && tm->tm_hour == HOURS_PER_DAY / 2)
        tm->tm_hour = 0;
    else if (mer == PM && tm->tm_hour != HOURS_PER_DAY / 2)
        tm->tm_hour += HOURS_PER_DAY / 2;

    if (*dtype == DTK_DATE && (fmask & DTK_DATE_M) != DTK_DATE_M)
        return DTERR_BAD_FORMAT;

    return 0;
}

/* ------------------------------------------------------------------ *
 *  DecodeTimeOnly
 * ------------------------------------------------------------------ */
static int
DecodeTimeOnly(char **field, int *ftype, int nf,
               int *dtype, struct pg_tm *tm, fsec_t *fsec, int *tzp)
{
    unsigned int fmask = 0, tmask;
    int type;
    int ptype = 0;
    int i;
    int val;
    int dterr;
    bool isjulian = false;
    bool is2digits = false;
    bool bc = false;
    int mer = HR24;

    *dtype = DTK_TIME;
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    tm->tm_year = 0;
    tm->tm_mon = 0;
    tm->tm_mday = 0;
    tm->tm_yday = 0;
    tm->tm_wday = 0;
    *fsec = 0;
    tm->tm_isdst = -1;
    if (tzp != NULL)
        *tzp = 0;

    for (i = 0; i < nf; i++)
    {
        tmask = 0;
        switch (ftype[i])
        {
            case DTK_DATE:
                /* Under limited circumstances, we accept a leading date. */
                if (tzp == NULL)
                    return DTERR_BAD_FORMAT;
                if (i == 0 && nf >= 2 &&
                    (ftype[nf - 1] == DTK_DATE || ftype[1] == DTK_TIME))
                {
                    dterr = DecodeDate(field[i], fmask, &tmask, &is2digits, tm);
                    if (dterr)
                        return dterr;
                    break;
                }
                return DTERR_BAD_FORMAT;

            case DTK_TIME:
                dterr = DecodeTime(field[i], (fmask | DTK_DATE_M), 0,
                                   &tmask, tm, fsec);
                if (dterr)
                    return dterr;
                break;

            case DTK_TZ:
            {
                int tz;

                if (tzp == NULL)
                    return DTERR_BAD_FORMAT;
                dterr = DecodeTimezone(field[i], &tz);
                if (dterr)
                    return dterr;
                *tzp = tz;
                tmask = DTK_M(TZ);
                break;
            }

            case DTK_NUMBER:
                if (ptype != 0)
                {
                    char *cp;
                    int value;

                    errno = 0;
                    value = dt_strtoint(field[i], &cp, 10);
                    if (errno == ERANGE)
                        return DTERR_FIELD_OVERFLOW;
                    if (*cp != '.' && *cp != '\0')
                        return DTERR_BAD_FORMAT;

                    switch (ptype)
                    {
                        case DTK_HOUR:
                            tm->tm_hour = value;
                            tmask = DTK_M(HOUR);
                            break;
                        case DTK_MINUTE:
                            tm->tm_min = value;
                            tmask = DTK_M(MINUTE);
                            break;
                        case DTK_SECOND:
                            tm->tm_sec = value;
                            tmask = DTK_M(SECOND);
                            if (*cp == '.')
                            {
                                dterr = ParseFractionalSecond(cp, fsec);
                                if (dterr)
                                    return dterr;
                                tmask = DTK_ALL_SECS_M;
                            }
                            break;
                        case DTK_TZ:
                            tmask = DTK_M(TZ);
                            if (tzp == NULL)
                                return DTERR_BAD_FORMAT;
                            dterr = DecodeTimezone(field[i], tzp);
                            if (dterr)
                                return dterr;
                            break;
                        case DTK_TIME:
                            dterr = DecodeNumberField((int) strlen(field[i]),
                                                      field[i],
                                                      (fmask | DTK_DATE_M),
                                                      &tmask, tm, fsec, &is2digits);
                            if (dterr < 0)
                                return dterr;
                            break;
                        default:
                            return DTERR_BAD_FORMAT;
                    }
                    ptype = 0;
                    *dtype = DTK_DATE;
                }
                else
                {
                    char *cp;
                    int flen;

                    flen = (int) strlen(field[i]);
                    cp = strchr(field[i], '.');

                    if (cp != NULL)
                    {
                        /* under limited circumstances we accept a date */
                        if (i == 0 && nf >= 2 && ftype[nf - 1] == DTK_DATE)
                        {
                            dterr = DecodeDate(field[i], fmask, &tmask,
                                               &is2digits, tm);
                            if (dterr)
                                return dterr;
                        }
                        /* embedded decimal and several digits before? */
                        else if (flen - (int) strlen(cp) > 2)
                        {
                            dterr = DecodeNumberField(flen, field[i],
                                                      (fmask | DTK_DATE_M),
                                                      &tmask, tm, fsec, &is2digits);
                            if (dterr < 0)
                                return dterr;
                        }
                        else
                            return DTERR_BAD_FORMAT;
                    }
                    else if (flen > 4)
                    {
                        dterr = DecodeNumberField(flen, field[i],
                                                  (fmask | DTK_DATE_M),
                                                  &tmask, tm, fsec, &is2digits);
                        if (dterr < 0)
                            return dterr;
                    }
                    /* otherwise it is a single date/time field */
                    else
                    {
                        dterr = DecodeNumber(flen, field[i], false,
                                             (fmask | DTK_DATE_M),
                                             &tmask, tm, fsec, &is2digits);
                        if (dterr)
                            return dterr;
                    }
                }
                break;

            case DTK_STRING:
            case DTK_SPECIAL:
                type = DecodeSpecial(field[i], &val);
                if (type == IGNORE_DTF)
                    continue;

                tmask = DTK_M(type);
                switch (type)
                {
                    case RESERV:
                        switch (val)
                        {
                            case DTK_ZULU:
                                tmask = (DTK_TIME_M | DTK_M(TZ));
                                *dtype = DTK_TIME;
                                tm->tm_hour = 0;
                                tm->tm_min = 0;
                                tm->tm_sec = 0;
                                if (tzp != NULL)
                                    *tzp = 0;
                                break;
                            default:
                                return DTERR_BAD_FORMAT;
                        }
                        break;

                    case DTZ:
                    case TZ:
                        if (tzp == NULL)
                            return DTERR_BAD_FORMAT;
                        *tzp = val;
                        tmask = DTK_M(TZ);
                        break;

                    case IGNORE_DTF:
                        break;

                    case AMPM:
                        mer = val;
                        break;

                    case ADBC:
                        bc = (val == BC);
                        break;

                    case MONTH:
                        tm->tm_mon = val;
                        break;

                    case DOW:
                        tm->tm_wday = val;
                        break;

                    case UNITS:
                    case ISOTIME:
                        tmask = 0;
                        ptype = val;
                        break;

                    default:
                        return DTERR_BAD_FORMAT;
                }
                break;

            default:
                return DTERR_BAD_FORMAT;
        }

        if (tmask & fmask)
            return DTERR_BAD_FORMAT;
        fmask |= tmask;
    }

    /* reject if a prefix type appeared and was never handled */
    if (ptype != 0)
        return DTERR_BAD_FORMAT;

    /* do final checking/adjustment of Y/M/D fields */
    dterr = ValidateDate(fmask, isjulian, is2digits, bc, tm);
    if (dterr)
        return dterr;

    /* handle AM/PM */
    if (mer != HR24 && tm->tm_hour > HOURS_PER_DAY / 2)
        return DTERR_FIELD_OVERFLOW;
    if (mer == AM && tm->tm_hour == HOURS_PER_DAY / 2)
        tm->tm_hour = 0;
    else if (mer == PM && tm->tm_hour != HOURS_PER_DAY / 2)
        tm->tm_hour += HOURS_PER_DAY / 2;

    /* check for time overflow */
    if (time_overflows(tm->tm_hour, tm->tm_min, tm->tm_sec, *fsec))
        return DTERR_FIELD_OVERFLOW;

    if ((fmask & DTK_TIME_M) != DTK_TIME_M)
        return DTERR_BAD_FORMAT;

    return 0;
}

/* ------------------------------------------------------------------ *
 *  Public parse entry points for date / time / timestamp
 * ------------------------------------------------------------------ */
bool
dt_parse_date(const char *s, int32 *out_day, const char **err)
{
    fsec_t fsec;
    struct pg_tm tt, *tm = &tt;
    int tzp;
    int dtype;
    int nf;
    int dterr;
    char *field[MAXDATEFIELDS];
    int ftype[MAXDATEFIELDS];
    char workbuf[MAXDATELEN + MAXDATEFIELDS];
    int32 date;

    if (s == NULL)
    {
        if (err)
            *err = dt_err("invalid input syntax for type date: \"\"");
        return false;
    }

    dterr = ParseDateTime(s, workbuf, sizeof(workbuf),
                          field, ftype, MAXDATEFIELDS, &nf);
    if (dterr == 0)
        dterr = DecodeDateTime(field, ftype, nf, &dtype, tm, &fsec, &tzp);
    if (dterr != 0)
    {
        if (err)
            *err = dt_parse_error(dterr, s, "date");
        return false;
    }

    switch (dtype)
    {
        case DTK_DATE:
            break;
        case DTK_LATE:
            *out_day = INT32_MAX;
            return true;
        case DTK_EARLY:
            *out_day = INT32_MIN;
            return true;
        default:
            if (err)
                *err = dt_parse_error(DTERR_BAD_FORMAT, s, "date");
            return false;
    }

    if (!IS_VALID_JULIAN(tm->tm_year, tm->tm_mon, tm->tm_mday))
    {
        if (err)
            *err = dt_err("date out of range: \"%s\"", s);
        return false;
    }

    date = (int32) (date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) -
                    POSTGRES_EPOCH_JDATE);
    if (!IS_VALID_DATE(date))
    {
        if (err)
            *err = dt_err("date out of range: \"%s\"", s);
        return false;
    }

    *out_day = date;
    return true;
}

bool
dt_parse_time(const char *s, int64 *out_usec, const char **err)
{
    fsec_t fsec;
    struct pg_tm tt, *tm = &tt;
    int tzp;
    int dtype;
    int nf;
    int dterr;
    char *field[MAXDATEFIELDS];
    int ftype[MAXDATEFIELDS];
    char workbuf[MAXDATELEN + MAXDATEFIELDS];

    if (s == NULL)
    {
        if (err)
            *err = dt_err("invalid input syntax for type time: \"\"");
        return false;
    }

    dterr = ParseDateTime(s, workbuf, sizeof(workbuf),
                          field, ftype, MAXDATEFIELDS, &nf);
    if (dterr == 0)
        dterr = DecodeTimeOnly(field, ftype, nf, &dtype, tm, &fsec, &tzp);
    if (dterr != 0)
    {
        if (err)
            *err = dt_parse_error(dterr, s, "time");
        return false;
    }

    *out_usec = time2t(tm->tm_hour, tm->tm_min, tm->tm_sec, fsec);
    return true;
}

bool
dt_parse_timestamp(const char *s, int64 *out_usec, bool with_tz,
                   const char **err)
{
    fsec_t fsec;
    struct pg_tm tt, *tm = &tt;
    int tz;
    int dtype;
    int nf;
    int dterr;
    char *field[MAXDATEFIELDS];
    int ftype[MAXDATEFIELDS];
    char workbuf[MAXDATELEN + MAXDATEFIELDS];
    const char *tname = with_tz ? "timestamp with time zone" : "timestamp";
    int64 result;

    if (s == NULL)
    {
        if (err)
            *err = dt_err("invalid input syntax for type %s: \"\"", tname);
        return false;
    }

    dterr = ParseDateTime(s, workbuf, sizeof(workbuf),
                          field, ftype, MAXDATEFIELDS, &nf);
    if (dterr == 0)
        dterr = DecodeDateTime(field, ftype, nf, &dtype, tm, &fsec, &tz);
    if (dterr != 0)
    {
        if (err)
            *err = dt_parse_error(dterr, s, tname);
        return false;
    }

    switch (dtype)
    {
        case DTK_DATE:
            if (tm2timestamp(tm, fsec, with_tz ? &tz : NULL, &result) != 0)
            {
                if (err)
                    *err = dt_err("timestamp out of range: \"%s\"", s);
                return false;
            }
            break;
        case DTK_LATE:
            result = DT_NOEND;
            break;
        case DTK_EARLY:
            result = DT_NOBEGIN;
            break;
        default:
            if (err)
                *err = dt_parse_error(DTERR_BAD_FORMAT, s, tname);
            return false;
    }

    *out_usec = result;
    return true;
}

/* ------------------------------------------------------------------ *
 *  Interval decoding
 * ------------------------------------------------------------------ */
/*
 * Accumulator for interval decoding.  PostgreSQL 15 replaced the old
 * struct pg_tm + fsec_t pair with exactly this shape (struct pg_itm_in),
 * accumulating microseconds directly so that hour and second fields are not
 * limited to what an int can hold.
 */
typedef struct
{
    int64 usec;
    int32 mday;
    int32 mon;
    int32 year;
} ItmIn;

static void
ClearItmIn(ItmIn *itm)
{
    itm->usec = 0;
    itm->mday = 0;
    itm->mon = 0;
    itm->year = 0;
}

static int
itmin2interval(const ItmIn *itm, PgInterval *span)
{
    int64 total_months = (int64) itm->year * MONTHS_PER_YEAR + itm->mon;

    if (total_months > INT32_MAX || total_months < INT32_MIN)
        return -1;
    span->month = (int32) total_months;
    span->day = itm->mday;
    span->usec = itm->usec;
    return 0;
}

/* PostgreSQL's tm2interval(); used by age() */
static int
tm2interval(struct pg_tm *tm, fsec_t fsec, PgInterval *span)
{
    double total_months = (double) tm->tm_year * MONTHS_PER_YEAR + tm->tm_mon;

    if (total_months > INT32_MAX || total_months < INT32_MIN)
        return -1;
    span->month = (int32) total_months;
    span->day = tm->tm_mday;
    span->usec = (((((tm->tm_hour * INT64_C(60)) + tm->tm_min) * INT64_C(60)) +
                   tm->tm_sec) * USECS_PER_SEC) + fsec;
    return 0;
}

static void
interval2tm(PgInterval span, struct pg_tm *tm, fsec_t *fsec)
{
    int64 time;
    int64 tfrac;

    tm->tm_year = span.month / MONTHS_PER_YEAR;
    tm->tm_mon = span.month % MONTHS_PER_YEAR;
    tm->tm_mday = span.day;
    time = span.usec;

    tfrac = time / USECS_PER_HOUR;
    time -= tfrac * USECS_PER_HOUR;
    tm->tm_hour = (int) tfrac;
    tfrac = time / USECS_PER_MINUTE;
    time -= tfrac * USECS_PER_MINUTE;
    tm->tm_min = (int) tfrac;
    tfrac = time / USECS_PER_SEC;
    *fsec = time - (tfrac * USECS_PER_SEC);
    tm->tm_sec = (int) tfrac;
}

/* ---- fractional-field accumulation -------------------------------- *
 * PostgreSQL 15 changed how the fractional part of an interval field is
 * turned into microseconds: before 15 the whole seconds were split off and
 * the remainder was rint()ed; from 15 on the product is truncated and only
 * bumped when the leftover exceeds half a microsecond.  So
 * `interval '0.9999995 seconds'` is 00:00:01 on <= 14 and 00:00:00.999999
 * on >= 15.  The same release also widened field values from int to int64
 * and started rint()ing fractional years instead of truncating them.
 */
static bool
AdjustFractUsec(double frac, int64 scale, ItmIn *itm)
{
    int64 usec;

    if (frac == 0)
        return true;

#if PGVC_VERNUM >= 115
    frac *= (double) scale;
    usec = (int64) frac;
    frac -= (double) usec;
    if (frac > 0.5)
        usec++;
    else if (frac < -0.5)
        usec--;
#else
    {
        double secs = frac * ((double) scale / 1000000.0);
        int isec = (int) secs;

        secs -= isec;
        usec = (int64) isec * USECS_PER_SEC + (int64) rint(secs * 1000000.0);
    }
#endif

    itm->usec += usec;
    return true;
}

static bool
AdjustUsec(int64 val, double fval, int64 scale, ItmIn *itm)
{
    if (val != 0)
    {
        if (val > INT64_MAX / scale || val < INT64_MIN / scale)
            return false;
        itm->usec += val * scale;
    }
    return AdjustFractUsec(fval, scale, itm);
}

static bool
AdjustDaysN(int64 val, int scale, ItmIn *itm)
{
    int64 days;

    if (val < INT32_MIN || val > INT32_MAX)
        return false;
    days = val * scale;
    if (days < INT32_MIN || days > INT32_MAX)
        return false;
    days += itm->mday;
    if (days < INT32_MIN || days > INT32_MAX)
        return false;
    itm->mday = (int32) days;
    return true;
}

static bool
AdjustMonthsN(int64 val, ItmIn *itm)
{
    int64 mon;

    if (val < INT32_MIN || val > INT32_MAX)
        return false;
    mon = (int64) itm->mon + val;
    if (mon < INT32_MIN || mon > INT32_MAX)
        return false;
    itm->mon = (int32) mon;
    return true;
}

static bool
AdjustYearsN(int64 val, int scale, ItmIn *itm)
{
    int64 years;

    if (val < INT32_MIN || val > INT32_MAX)
        return false;
    years = val * scale;
    if (years < INT32_MIN || years > INT32_MAX)
        return false;
    years += itm->year;
    if (years < INT32_MIN || years > INT32_MAX)
        return false;
    itm->year = (int32) years;
    return true;
}

static bool
AdjustFractDaysN(double frac, int scale, ItmIn *itm)
{
    int extra_days;

    if (frac == 0)
        return true;
    frac *= scale;
    extra_days = (int) frac;
    if (!AdjustDaysN(extra_days, 1, itm))
        return false;
    frac -= extra_days;
    return AdjustFractUsec(frac, USECS_PER_DAY, itm);
}

static bool
AdjustFractYearsN(double frac, int scale, ItmIn *itm)
{
    int extra_months;

    if (frac == 0)
        return true;
#if PGVC_VERNUM >= 115
    extra_months = (int) rint(frac * scale * MONTHS_PER_YEAR);
#else
    extra_months = (int) (frac * MONTHS_PER_YEAR * scale);
#endif
    return AdjustMonthsN(extra_months, itm);
}

static int
DecodeInterval(char **field, int *ftype, int nf, int *dtype, ItmIn *itm)
{
    bool is_before = false;
    bool parsing_unit_val = false;
    char *cp;
    unsigned int fmask = 0, tmask;
    int type;
    int i;
    int dterr;
    int64 val;
    double fval;

    *dtype = DTK_DELTA;
    type = IGNORE_DTF;
    ClearItmIn(itm);

    /* read the list backwards to pick up units before values */
    for (i = nf - 1; i >= 0; i--)
    {
        switch (ftype[i])
        {
            case DTK_TIME:
            {
                int64 usec;

                dterr = DecodeTimeForInterval(field[i], fmask, 0, &tmask, &usec);
                if (dterr)
                    return dterr;
                itm->usec = usec;
                type = DTK_DAY;
                parsing_unit_val = false;
                break;
            }

            case DTK_TZ:

                /*
                 * A "timezone" here is just a token with a leading sign and
                 * at least one digit.  A signed hh:mm[:ss] behaves exactly
                 * like DTK_TIME with the sign applied; anything else falls
                 * through to the number code, which handles signed values
                 * and the SQL year-month form.
                 */
                if (strchr(field[i] + 1, ':') != NULL)
                {
                    int64 usec;
                    unsigned int ttmask;

                    if (DecodeTimeForInterval(field[i] + 1, fmask, 0,
                                              &ttmask, &usec) == 0)
                    {
                        if (*field[i] == '-')
                            usec = -usec;
                        itm->usec = usec;
                        tmask = ttmask;

                        /*
                         * If no unit follows, the next field to the left is a
                         * day; this is what makes '1 +02:03' work.
                         */
                        type = DTK_DAY;
                        parsing_unit_val = false;
                        break;
                    }
                }
                /* FALLTHROUGH */
            case DTK_DATE:
            case DTK_NUMBER:
                if (type == IGNORE_DTF)
                    type = DTK_SECOND;

                errno = 0;
                val = strtoll(field[i], &cp, 10);
                if (errno == ERANGE)
                    return DTERR_FIELD_OVERFLOW;
#if PGVC_VERNUM < 115
                /* before 15 an interval field value had to fit in an int */
                if (val > INT32_MAX || val < INT32_MIN)
                    return DTERR_FIELD_OVERFLOW;
#endif

                if (*cp == '-')
                {
                    /* SQL "years-months" syntax */
                    int val2;

                    errno = 0;
                    val2 = dt_strtoint(cp + 1, &cp, 10);
                    if (errno == ERANGE || val2 < 0 || val2 >= MONTHS_PER_YEAR)
                        return DTERR_FIELD_OVERFLOW;
                    if (*cp != '\0')
                        return DTERR_BAD_FORMAT;
                    type = DTK_MONTH;
                    if (*field[i] == '-')
                        val2 = -val2;
                    if (val > INT64_MAX / MONTHS_PER_YEAR ||
                        val < INT64_MIN / MONTHS_PER_YEAR)
                        return DTERR_FIELD_OVERFLOW;
                    val = val * MONTHS_PER_YEAR + val2;
                    fval = 0;
                }
                else if (*cp == '.')
                {
                    dterr = ParseFraction(cp, &fval);
                    if (dterr)
                        return dterr;
                    if (*field[i] == '-')
                        fval = -fval;
                }
                else if (*cp == '\0')
                    fval = 0;
                else
                    return DTERR_BAD_FORMAT;

                tmask = 0;

                switch (type)
                {
                    case DTK_MICROSEC:
#if PGVC_VERNUM >= 115
                        if (!AdjustUsec(val, fval, 1, itm))
                            return DTERR_FIELD_OVERFLOW;
#else
                        itm->usec += (int64) rint((double) val + fval);
#endif
                        tmask = DTK_M(MICROSECOND);
                        break;

                    case DTK_MILLISEC:
#if PGVC_VERNUM >= 115
                        if (!AdjustUsec(val, fval, 1000, itm))
                            return DTERR_FIELD_OVERFLOW;
#else
                        /* avoid overflowing the old fsec field */
                        itm->usec += (val / 1000) * USECS_PER_SEC;
                        itm->usec += (int64) rint(((double) (val - (val / 1000) * 1000) + fval) * 1000);
#endif
                        tmask = DTK_M(MILLISECOND);
                        break;

                    case DTK_SECOND:
                        if (!AdjustUsec(val, fval, USECS_PER_SEC, itm))
                            return DTERR_FIELD_OVERFLOW;
                        /*
                         * If subseconds were given, treat this as microsecond
                         * and millisecond input as well.
                         */
                        if (fval == 0)
                            tmask = DTK_M(SECOND);
                        else
                            tmask = DTK_ALL_SECS_M;
                        break;

                    case DTK_MINUTE:
                        if (!AdjustUsec(val, fval, USECS_PER_MINUTE, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(MINUTE);
                        break;

                    case DTK_HOUR:
                        if (!AdjustUsec(val, fval, USECS_PER_HOUR, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(HOUR);
                        type = DTK_DAY; /* set for the next field */
                        break;

                    case DTK_DAY:
                        if (!AdjustDaysN(val, 1, itm) ||
                            !AdjustFractUsec(fval, USECS_PER_DAY, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(DAY);
                        break;

                    case DTK_WEEK:
                        if (!AdjustDaysN(val, 7, itm) ||
                            !AdjustFractDaysN(fval, 7, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(WEEK);
                        break;

                    case DTK_MONTH:
                        if (!AdjustMonthsN(val, itm) ||
                            !AdjustFractDaysN(fval, DAYS_PER_MONTH, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(MONTH);
                        break;

                    case DTK_YEAR:
                        if (!AdjustYearsN(val, 1, itm) ||
                            !AdjustFractYearsN(fval, 1, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(YEAR);
                        break;

                    case DTK_DECADE:
                        if (!AdjustYearsN(val, 10, itm) ||
                            !AdjustFractYearsN(fval, 10, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(DECADE);
                        break;

                    case DTK_CENTURY:
                        if (!AdjustYearsN(val, 100, itm) ||
                            !AdjustFractYearsN(fval, 100, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(CENTURY);
                        break;

                    case DTK_MILLENNIUM:
                        if (!AdjustYearsN(val, 1000, itm) ||
                            !AdjustFractYearsN(fval, 1000, itm))
                            return DTERR_FIELD_OVERFLOW;
                        tmask = DTK_M(MILLENNIUM);
                        break;

                    default:
                        return DTERR_BAD_FORMAT;
                }
                parsing_unit_val = false;
                break;

            case DTK_STRING:
            case DTK_SPECIAL:
            {
                int uval;

                /* reject consecutive unhandled units */
                if (parsing_unit_val)
                    return DTERR_BAD_FORMAT;
                type = DecodeUnits(field[i], &uval);
                if (type == UNKNOWN_FIELD)
                    type = DecodeSpecial(field[i], &uval);
                if (type == IGNORE_DTF)
                    continue;

                tmask = 0;
                switch (type)
                {
                    case UNITS:
                        type = uval;
                        parsing_unit_val = true;
                        break;

                    case AGO:
                        /* "ago" is only allowed at the end of the interval */
                        if (i != nf - 1)
                            return DTERR_BAD_FORMAT;
                        is_before = true;
                        type = uval;
                        break;

                    case RESERV:
                        tmask = (DTK_DATE_M | DTK_TIME_M);
                        /* only the infinite intervals are accepted */
                        if (uval != DTK_LATE && uval != DTK_EARLY)
                            return DTERR_BAD_FORMAT;
                        if (i != nf - 1)
                            return DTERR_BAD_FORMAT;
                        *dtype = uval;
                        break;

                    default:
                        return DTERR_BAD_FORMAT;
                }
                break;
            }

            default:
                return DTERR_BAD_FORMAT;
        }

        if (tmask & fmask)
            return DTERR_BAD_FORMAT;
        fmask |= tmask;
    }

    /* at least one time field must have been found */
    if (fmask == 0)
        return DTERR_BAD_FORMAT;

    /* reject if a unit appeared and was never handled */
    if (parsing_unit_val)
        return DTERR_BAD_FORMAT;

    /* finally, AGO negates everything */
    if (is_before)
    {
        if (itm->usec == INT64_MIN || itm->mday == INT32_MIN ||
            itm->mon == INT32_MIN || itm->year == INT32_MIN)
            return DTERR_FIELD_OVERFLOW;
        itm->usec = -itm->usec;
        itm->mday = -itm->mday;
        itm->mon = -itm->mon;
        itm->year = -itm->year;
    }

    return 0;
}

/* ------------- ISO 8601 interval forms ------------- */
static int
ParseISO8601Number(char *str, char **endptr, int64 *ipart, double *fpart)
{
    double val;

    if (!(isdigit((unsigned char) *str) || *str == '-' || *str == '.'))
        return DTERR_BAD_FORMAT;
    errno = 0;
    val = strtod(str, endptr);
    if (errno != 0 || *endptr == str)
        return DTERR_BAD_FORMAT;
    if (*endptr == str + 1 && (*str == '-' || *str == '.'))
        return DTERR_BAD_FORMAT;
    if (val < -1.0e15 || val > 1.0e15)
        return DTERR_FIELD_OVERFLOW;
    *ipart = (int64) val;
    *fpart = val - (double) *ipart;
    return 0;
}

static int
ISO8601IntegerWidth(const char *fieldstart)
{
    if (*fieldstart == '-')
        fieldstart++;
    return (int) strspn(fieldstart, "0123456789");
}

static int
DecodeISO8601Interval(const char *instr, int *dtype, ItmIn *itm)
{
    bool datepart = true;
    bool havefield = false;
    char buf[MAXDATELEN + 1];
    char *str;

    *dtype = DTK_DELTA;
    ClearItmIn(itm);

    if (strlen(instr) < 2 || instr[0] != 'P')
        return DTERR_BAD_FORMAT;
    if (strlen(instr) > MAXDATELEN)
        return DTERR_BAD_FORMAT;
    strcpy(buf, instr);
    str = buf + 1;

    while (*str)
    {
        char *fieldstart;
        int64 val;
        double fval;
        char unit;
        int dterr;

        if (*str == 'T')
        {
            datepart = false;
            havefield = false;
            str++;
            continue;
        }

        fieldstart = str;
        dterr = ParseISO8601Number(str, &str, &val, &fval);
        if (dterr)
            return dterr;

        unit = *str++;

        if (datepart)
        {
            switch (unit)
            {
                case 'Y':
                    if (!AdjustYearsN(val, 1, itm) || !AdjustFractYearsN(fval, 1, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case 'M':
                    if (!AdjustMonthsN(val, itm) ||
                        !AdjustFractDaysN(fval, DAYS_PER_MONTH, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case 'W':
                    if (!AdjustDaysN(val, 7, itm) || !AdjustFractDaysN(fval, 7, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case 'D':
                    if (!AdjustDaysN(val, 1, itm) ||
                        !AdjustFractUsec(fval, USECS_PER_DAY, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case 'T':
                case '\0':
                    if (ISO8601IntegerWidth(fieldstart) == 8 && !havefield)
                    {
                        if (!AdjustYearsN(val / 10000, 1, itm) ||
                            !AdjustMonthsN((val / 100) % 100, itm) ||
                            !AdjustDaysN(val % 100, 1, itm) ||
                            !AdjustFractUsec(fval, USECS_PER_DAY, itm))
                            return DTERR_FIELD_OVERFLOW;
                        if (unit == '\0')
                            return 0;
                        datepart = false;
                        havefield = false;
                        continue;
                    }
                    /* FALLTHROUGH */
                case '-':
                    if (havefield)
                        return DTERR_BAD_FORMAT;

                    if (!AdjustYearsN(val, 1, itm) || !AdjustFractYearsN(fval, 1, itm))
                        return DTERR_FIELD_OVERFLOW;
                    if (unit == '\0')
                        return 0;
                    if (unit == 'T')
                    {
                        datepart = false;
                        havefield = false;
                        continue;
                    }

                    dterr = ParseISO8601Number(str, &str, &val, &fval);
                    if (dterr)
                        return dterr;
                    if (!AdjustMonthsN(val, itm) ||
                        !AdjustFractDaysN(fval, DAYS_PER_MONTH, itm))
                        return DTERR_FIELD_OVERFLOW;
                    if (*str == '\0')
                        return 0;
                    if (*str == 'T')
                    {
                        datepart = false;
                        havefield = false;
                        str++;
                        continue;
                    }
                    if (*str != '-')
                        return DTERR_BAD_FORMAT;
                    str++;

                    dterr = ParseISO8601Number(str, &str, &val, &fval);
                    if (dterr)
                        return dterr;
                    if (!AdjustDaysN(val, 1, itm) ||
                        !AdjustFractUsec(fval, USECS_PER_DAY, itm))
                        return DTERR_FIELD_OVERFLOW;
                    if (*str == '\0')
                        return 0;
                    if (*str == 'T')
                    {
                        datepart = false;
                        havefield = false;
                        str++;
                        continue;
                    }
                    return DTERR_BAD_FORMAT;
                default:
                    return DTERR_BAD_FORMAT;
            }
        }
        else
        {
            switch (unit)
            {
                case 'H':
                    if (!AdjustUsec(val, fval, USECS_PER_HOUR, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case 'M':
                    if (!AdjustUsec(val, fval, USECS_PER_MINUTE, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case 'S':
                    if (!AdjustUsec(val, fval, USECS_PER_SEC, itm))
                        return DTERR_FIELD_OVERFLOW;
                    break;
                case '\0':
                    if (ISO8601IntegerWidth(fieldstart) == 6 && !havefield)
                    {
                        if (!AdjustUsec(val / 10000, 0, USECS_PER_HOUR, itm) ||
                            !AdjustUsec((val / 100) % 100, 0, USECS_PER_MINUTE, itm) ||
                            !AdjustUsec(val % 100, fval, USECS_PER_SEC, itm))
                            return DTERR_FIELD_OVERFLOW;
                        return 0;
                    }
                    /* FALLTHROUGH */
                case ':':
                    if (havefield)
                        return DTERR_BAD_FORMAT;
                    if (!AdjustUsec(val, fval, USECS_PER_HOUR, itm))
                        return DTERR_FIELD_OVERFLOW;
                    if (unit == '\0')
                        return 0;

                    dterr = ParseISO8601Number(str, &str, &val, &fval);
                    if (dterr)
                        return dterr;
                    if (!AdjustUsec(val, fval, USECS_PER_MINUTE, itm))
                        return DTERR_FIELD_OVERFLOW;
                    if (*str == '\0')
                        return 0;
                    if (*str != ':')
                        return DTERR_BAD_FORMAT;
                    str++;

                    dterr = ParseISO8601Number(str, &str, &val, &fval);
                    if (dterr)
                        return dterr;
                    if (!AdjustUsec(val, fval, USECS_PER_SEC, itm))
                        return DTERR_FIELD_OVERFLOW;
                    if (*str == '\0')
                        return 0;
                    return DTERR_BAD_FORMAT;
                default:
                    return DTERR_BAD_FORMAT;
            }
        }

        havefield = true;
    }

    return 0;
}

bool
dt_parse_interval(const char *s, PgInterval *out, const char **err)
{
    ItmIn itm;
    int dtype;
    int nf;
    int dterr;
    char *field[MAXDATEFIELDS];
    int ftype[MAXDATEFIELDS];
    char workbuf[256];

    if (s == NULL)
    {
        if (err)
            *err = dt_err("invalid input syntax for type interval: \"\"");
        return false;
    }

    dterr = ParseDateTime(s, workbuf, sizeof(workbuf),
                          field, ftype, MAXDATEFIELDS, &nf);
    if (dterr == 0)
        dterr = DecodeInterval(field, ftype, nf, &dtype, &itm);

    /* if that looks like a bad format, try ISO 8601 style */
    if (dterr == DTERR_BAD_FORMAT)
        dterr = DecodeISO8601Interval(s, &dtype, &itm);

    if (dterr != 0)
    {
        if (dterr == DTERR_FIELD_OVERFLOW)
            dterr = DTERR_INTERVAL_OVERFLOW;
        if (err)
            *err = dt_parse_error(dterr, s, "interval");
        return false;
    }

    switch (dtype)
    {
        case DTK_DELTA:
            if (itmin2interval(&itm, out) != 0)
            {
                if (err)
                    *err = dt_err("interval out of range");
                return false;
            }
            break;
        default:
            if (err)
                *err = dt_parse_error(DTERR_BAD_FORMAT, s, "interval");
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ *
 *  Output formatting (DateStyle ISO, IntervalStyle postgres)
 * ------------------------------------------------------------------ */
#define MAX_TIMESTAMP_PRECISION 6
#define MAX_INTERVAL_PRECISION  6

/* Append "ss" or "ss.ffffff" with trailing fractional zeros stripped. */
static char *
AppendSeconds(char *cp, int sec, fsec_t fsec, int precision, bool fillzeros)
{
    int64 a = (sec < 0) ? -(int64) sec : (int64) sec;

    if (fillzeros)
        cp = dt_ultostr_zeropad(cp, (unsigned long) a, 2);
    else
        cp = dt_ultostr_zeropad(cp, (unsigned long) a, 1);

    if (fsec != 0)
    {
        int64 value = (fsec < 0) ? -fsec : fsec;
        char *end = &cp[precision + 1];
        bool gotnonzero = false;
        int p = precision;

        *cp++ = '.';

        while (p--)
        {
            int64 oldval = value;
            int64 remainder;

            value /= 10;
            remainder = oldval - value * 10;

            if (remainder)
                gotnonzero = true;

            if (gotnonzero)
                cp[p] = (char) ('0' + remainder);
            else
                end = &cp[p];
        }

        return end;
    }
    return cp;
}

static char *
AppendTimestampSeconds(char *cp, struct pg_tm *tm, fsec_t fsec)
{
    cp = dt_ultostr_zeropad(cp, (unsigned long) tm->tm_hour, 2);
    *cp++ = ':';
    cp = dt_ultostr_zeropad(cp, (unsigned long) tm->tm_min, 2);
    *cp++ = ':';
    return AppendSeconds(cp, tm->tm_sec, fsec, MAX_TIMESTAMP_PRECISION, true);
}

static char *
EncodeTimezone(char *str, int tz)
{
    int hour, min, sec;

    sec = (tz < 0) ? -tz : tz;
    min = sec / SECS_PER_MINUTE;
    sec -= min * SECS_PER_MINUTE;
    hour = min / MINS_PER_HOUR;
    min -= hour * MINS_PER_HOUR;

    /* TZ is negated relative to the sign we wish to display */
    *str++ = (tz <= 0 ? '+' : '-');

    if (sec != 0)
    {
        str = dt_ultostr_zeropad(str, (unsigned long) hour, 2);
        *str++ = ':';
        str = dt_ultostr_zeropad(str, (unsigned long) min, 2);
        *str++ = ':';
        str = dt_ultostr_zeropad(str, (unsigned long) sec, 2);
    }
    else if (min != 0)
    {
        str = dt_ultostr_zeropad(str, (unsigned long) hour, 2);
        *str++ = ':';
        str = dt_ultostr_zeropad(str, (unsigned long) min, 2);
    }
    else
        str = dt_ultostr_zeropad(str, (unsigned long) hour, 2);
    return str;
}

static void
EncodeDateOnly(struct pg_tm *tm, char *str)
{
    str = dt_ultostr_zeropad(str,
                             (unsigned long) ((tm->tm_year > 0)
                                              ? tm->tm_year
                                              : -(tm->tm_year - 1)), 4);
    *str++ = '-';
    str = dt_ultostr_zeropad(str, (unsigned long) tm->tm_mon, 2);
    *str++ = '-';
    str = dt_ultostr_zeropad(str, (unsigned long) tm->tm_mday, 2);

    if (tm->tm_year <= 0)
    {
        memcpy(str, " BC", 3);
        str += 3;
    }
    *str = '\0';
}

static void
EncodeTimeOnly(struct pg_tm *tm, fsec_t fsec, bool print_tz, int tz, char *str)
{
    str = dt_ultostr_zeropad(str, (unsigned long) tm->tm_hour, 2);
    *str++ = ':';
    str = dt_ultostr_zeropad(str, (unsigned long) tm->tm_min, 2);
    *str++ = ':';
    str = AppendSeconds(str, tm->tm_sec, fsec, MAX_TIMESTAMP_PRECISION, true);
    if (print_tz)
        str = EncodeTimezone(str, tz);
    *str = '\0';
}

static void
EncodeDateTime(struct pg_tm *tm, fsec_t fsec, bool print_tz, int tz, char *str)
{
    str = dt_ultostr_zeropad(str,
                             (unsigned long) ((tm->tm_year > 0)
                                              ? tm->tm_year
                                              : -(tm->tm_year - 1)), 4);
    *str++ = '-';
    str = dt_ultostr_zeropad(str, (unsigned long) tm->tm_mon, 2);
    *str++ = '-';
    str = dt_ultostr_zeropad(str, (unsigned long) tm->tm_mday, 2);
    *str++ = ' ';
    str = AppendTimestampSeconds(str, tm, fsec);
    if (print_tz)
        str = EncodeTimezone(str, tz);

    if (tm->tm_year <= 0)
    {
        memcpy(str, " BC", 3);
        str += 3;
    }
    *str = '\0';
}

char *
dt_format_date(int32 day)
{
    char buf[MAXDATELEN + 1];
    struct pg_tm tt, *tm = &tt;

    if (day == INT32_MAX)
        return dt_strdup_n("infinity", 8);
    if (day == INT32_MIN)
        return dt_strdup_n("-infinity", 9);

    j2date((int) day + POSTGRES_EPOCH_JDATE,
           &tm->tm_year, &tm->tm_mon, &tm->tm_mday);
    EncodeDateOnly(tm, buf);
    return dt_strdup_n(buf, strlen(buf));
}

char *
dt_format_time(int64 usec)
{
    char buf[MAXDATELEN + 1];
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec;

    dt2time(usec, &tm->tm_hour, &tm->tm_min, &tm->tm_sec, &fsec);
    EncodeTimeOnly(tm, fsec, false, 0, buf);
    return dt_strdup_n(buf, strlen(buf));
}

char *
dt_format_timestamp(int64 usec, bool with_tz)
{
    char buf[MAXDATELEN + 1];
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec;
    int tz = 0;

    if (TIMESTAMP_IS_NOBEGIN(usec))
        return dt_strdup_n("-infinity", 9);
    if (TIMESTAMP_IS_NOEND(usec))
        return dt_strdup_n("infinity", 8);

    if (timestamp2tm(usec, with_tz ? &tz : NULL, tm, &fsec) != 0)
        return dt_strdup_n("", 0);

    EncodeDateTime(tm, fsec, with_tz, tz, buf);
    return dt_strdup_n(buf, strlen(buf));
}

/*
 * IntervalStyle = postgres.  Each nonzero integer field sets "is_before" for
 * the *next* field only -- a quirk preserved from PostgreSQL for
 * compatibility.
 */
static char *
AddPostgresIntPart(char *cp, int value, const char *units,
                   bool *is_zero, bool *is_before)
{
    if (value == 0)
        return cp;
    sprintf(cp, "%s%s%d %s%s",
            (!*is_zero) ? " " : "",
            (*is_before && value > 0) ? "+" : "",
            value,
            units,
            (value != 1) ? "s" : "");

    *is_before = (value < 0);
    *is_zero = false;
    return cp + strlen(cp);
}

char *
dt_format_interval(const PgInterval *iv)
{
    char buf[MAXDATELEN + 1];
    char *cp = buf;
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec;
    int year, mon, mday, hour, min, sec;
    bool is_zero = true;
    bool is_before = false;

    interval2tm(*iv, tm, &fsec);
    year = tm->tm_year;
    mon = tm->tm_mon;
    mday = tm->tm_mday;
    hour = tm->tm_hour;
    min = tm->tm_min;
    sec = tm->tm_sec;

    cp = AddPostgresIntPart(cp, year, "year", &is_zero, &is_before);
    cp = AddPostgresIntPart(cp, mon, "mon", &is_zero, &is_before);
    cp = AddPostgresIntPart(cp, mday, "day", &is_zero, &is_before);
    if (is_zero || hour != 0 || min != 0 || sec != 0 || fsec != 0)
    {
        bool minus = (hour < 0 || min < 0 || sec < 0 || fsec < 0);

        sprintf(cp, "%s%s%02d:%02d:",
                is_zero ? "" : " ",
                (minus ? "-" : (is_before ? "+" : "")),
                abs(hour), abs(min));
        cp += strlen(cp);
        cp = AppendSeconds(cp, sec, fsec, MAX_INTERVAL_PRECISION, true);
        *cp = '\0';
    }
    return dt_strdup_n(buf, strlen(buf));
}

/* ------------------------------------------------------------------ *
 *  ISO week / ISO year
 * ------------------------------------------------------------------ */
static int
date2isoweek(int year, int mon, int mday)
{
    int result;
    int day0, day4, dayn;

    dayn = date2j(year, mon, mday);
    day4 = date2j(year, 1, 4);
    day0 = j2day(day4 - 1);

    if (dayn < day4 - day0)
    {
        day4 = date2j(year - 1, 1, 4);
        day0 = j2day(day4 - 1);
    }

    result = (dayn - (day4 - day0)) / 7 + 1;

    if (result >= 52)
    {
        day4 = date2j(year + 1, 1, 4);
        day0 = j2day(day4 - 1);
        if (dayn >= day4 - day0)
            result = (dayn - (day4 - day0)) / 7 + 1;
    }

    return result;
}

static int
date2isoyear(int year, int mon, int mday)
{
    int result;
    int day0, day4, dayn;

    dayn = date2j(year, mon, mday);
    day4 = date2j(year, 1, 4);
    day0 = j2day(day4 - 1);

    if (dayn < day4 - day0)
    {
        day4 = date2j(year - 1, 1, 4);
        day0 = j2day(day4 - 1);
        year--;
    }

    result = (dayn - (day4 - day0)) / 7 + 1;

    if (result >= 52)
    {
        day4 = date2j(year + 1, 1, 4);
        day0 = j2day(day4 - 1);
        if (dayn >= day4 - day0)
            year++;
    }

    return year;
}

/* ------------------------------------------------------------------ *
 *  Decimal string helpers for extract()'s numeric results
 * ------------------------------------------------------------------ */
static char *
num_int64_str(int64 v)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%" PRId64, v);
    return dt_strdup_n(buf, strlen(buf));
}

/*
 * Render `v / 10^scale` with exactly `scale` digits after the point, which is
 * what PostgreSQL's int64_div_fast_to_numeric() produces.
 */
static char *
num_scaled_str(int64 v, int scale)
{
    char buf[64];
    char digits[32];
    bool neg = (v < 0);
    uint64 a = neg ? (uint64) (-(v + 1)) + 1 : (uint64) v;
    int n = 0, i, pos = 0;

    if (scale <= 0)
        return num_int64_str(v);

    do
    {
        digits[n++] = (char) ('0' + (a % 10));
        a /= 10;
    } while (a > 0);
    while (n <= scale)
        digits[n++] = '0';

    if (neg)
        buf[pos++] = '-';
    for (i = n - 1; i >= scale; i--)
        buf[pos++] = digits[i];
    buf[pos++] = '.';
    for (; i >= 0; i--)
        buf[pos++] = digits[i];
    buf[pos] = '\0';
    return dt_strdup_n(buf, (size_t) pos);
}

/*
 * extract(julian from timestamp) on PostgreSQL 14+ is
 *     date2j(...) + usec_of_day / 86400000000
 * computed in numeric.  Reproduce both the value and numeric's chosen result
 * scale (select_div_scale(): enough digits for 16 significant ones).
 */
static char *
num_julian_str(int64 jd, int64 usec_of_day)
{
    const int64 den = USECS_PER_DAY;      /* 86400000000 */
    int64 tmp;
    int ndigits = 0;
    int firstdigit1 = 0;
    int weight1 = 0;
    int qweight, rscale;
    char frac[1024];
    int i;
    int64 rem;
    char buf[1100];
    int pos = 0;
    char intpart[32];

    /* base-10000 shape of the numerator, as numeric would store it */
    tmp = usec_of_day;
    while (tmp > 0)
    {
        firstdigit1 = (int) (tmp % 10000);
        tmp /= 10000;
        ndigits++;
    }
    if (ndigits > 0)
        weight1 = ndigits - 1;
    else
        firstdigit1 = 0;

    /* denominator 86400000000 == {864, 0, 0} base 10000 */
    qweight = weight1 - 2;
    if (firstdigit1 <= 864)
        qweight--;

    rscale = 16 - qweight * 4;
    if (rscale < 0)
        rscale = 0;
    if (rscale > 1000)
        rscale = 1000;

    rem = usec_of_day;
    for (i = 0; i < rscale; i++)
    {
        rem *= 10;
        frac[i] = (char) ('0' + (rem / den));
        rem %= den;
    }
    frac[rscale] = '\0';

    /* round half away from zero */
    if (rem * 2 >= den)
    {
        int k = rscale - 1;

        while (k >= 0)
        {
            if (frac[k] != '9')
            {
                frac[k]++;
                break;
            }
            frac[k] = '0';
            k--;
        }
        if (k < 0)
            jd++;
    }

    snprintf(intpart, sizeof(intpart), "%" PRId64, jd);
    pos = (int) strlen(intpart);
    memcpy(buf, intpart, (size_t) pos);
    if (rscale > 0)
    {
        buf[pos++] = '.';
        memcpy(buf + pos, frac, (size_t) rscale);
        pos += rscale;
    }
    buf[pos] = '\0';
    return dt_strdup_n(buf, (size_t) pos);
}

/* ------------------------------------------------------------------ *
 *  extract() / date_part()
 * ------------------------------------------------------------------ */
__attribute__((unused)) static const char *
dt_typename(Oid type)
{
    switch (type)
    {
        case DATEOID:        return "date";
        case TIMEOID:        return "time without time zone";
        case TIMESTAMPOID:   return "timestamp without time zone";
        case TIMESTAMPTZOID: return "timestamp with time zone";
        case INTERVALOID:    return "interval";
        default:             return "timestamp without time zone";
    }
}

/* the short names PostgreSQL <= 14 used in these messages */
__attribute__((unused)) static const char *
dt_oldtypename(Oid type)
{
    switch (type)
    {
        case DATEOID:        return "date";
        case TIMEOID:        return "\"time\"";
        case TIMESTAMPOID:   return "timestamp";
        case TIMESTAMPTZOID: return "timestamp with time zone";
        case INTERVALOID:    return "interval";
        default:             return "timestamp";
    }
}

static const char *
dt_unit_error(Oid type, const char *lowunits, bool recognized)
{
#if PGVC_VERNUM >= 115
    return dt_err("unit \"%s\" %s for type %s", lowunits,
                  recognized ? "not supported" : "not recognized",
                  dt_typename(type));
#else
    /*
     * Before 15, time's messages always said "not recognized", even for units
     * that exist but are meaningless for a time value.
     */
    if (type == TIMEOID)
        recognized = false;
    return dt_err("%s units \"%s\" %s", dt_oldtypename(type), lowunits,
                  recognized ? "not supported" : "not recognized");
#endif
}

static void
dt_lowerunits(const char *field, char *buf, size_t buflen)
{
    size_t i;

    for (i = 0; i + 1 < buflen && field[i] != '\0'; i++)
        buf[i] = (char) dt_tolower(field[i]);
    buf[i] = '\0';
}

/* The set of units each type supports, and the integer result for the
 * "simple" ones.  Returns:
 *    1 - handled, *intresult is the answer (an exact integer)
 *    0 - unit is a scaled/odd one; caller handles it via `special`
 *   -1 - unit not supported for this type
 *   -2 - unit not recognized at all
 */
#define DTX_MICROSEC 1
#define DTX_MILLISEC 2
#define DTX_SECOND   3
#define DTX_EPOCH    4
#define DTX_JULIAN   5

static int
extract_timestamp_common(int val, struct pg_tm *tm, fsec_t fsec, int tz,
                         Oid type, int64 *intresult, int *special)
{
    (void) fsec;
    *special = 0;
    switch (val)
    {
        case DTK_MICROSEC:
            *special = DTX_MICROSEC;
            return 0;
        case DTK_MILLISEC:
            *special = DTX_MILLISEC;
            return 0;
        case DTK_SECOND:
            *special = DTX_SECOND;
            return 0;
        case DTK_EPOCH:
            *special = DTX_EPOCH;
            return 0;
        case DTK_JULIAN:
            *special = DTX_JULIAN;
            return 0;
        case DTK_MINUTE:
            *intresult = tm->tm_min;
            return 1;
        case DTK_HOUR:
            *intresult = tm->tm_hour;
            return 1;
        case DTK_DAY:
            *intresult = tm->tm_mday;
            return 1;
        case DTK_MONTH:
            *intresult = tm->tm_mon;
            return 1;
        case DTK_QUARTER:
            *intresult = (tm->tm_mon - 1) / 3 + 1;
            return 1;
        case DTK_WEEK:
            *intresult = date2isoweek(tm->tm_year, tm->tm_mon, tm->tm_mday);
            return 1;
        case DTK_YEAR:
            /* there is no year 0 */
            *intresult = (tm->tm_year > 0) ? tm->tm_year : (tm->tm_year - 1);
            return 1;
        case DTK_DECADE:
            if (tm->tm_year >= 0)
                *intresult = tm->tm_year / 10;
            else
                *intresult = -((8 - (tm->tm_year - 1)) / 10);
            return 1;
        case DTK_CENTURY:
            if (tm->tm_year > 0)
                *intresult = (tm->tm_year + 99) / 100;
            else
                *intresult = -((99 - (tm->tm_year - 1)) / 100);
            return 1;
        case DTK_MILLENNIUM:
            if (tm->tm_year > 0)
                *intresult = (tm->tm_year + 999) / 1000;
            else
                *intresult = -((999 - (tm->tm_year - 1)) / 1000);
            return 1;
        case DTK_ISOYEAR:
            *intresult = date2isoyear(tm->tm_year, tm->tm_mon, tm->tm_mday);
            if (*intresult <= 0)
                *intresult -= 1;
            return 1;
        case DTK_DOW:
        case DTK_ISODOW:
            *intresult = j2day(date2j(tm->tm_year, tm->tm_mon, tm->tm_mday));
            if (val == DTK_ISODOW && *intresult == 0)
                *intresult = 7;
            return 1;
        case DTK_DOY:
            *intresult = (date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) -
                          date2j(tm->tm_year, 1, 1) + 1);
            return 1;
        case DTK_TZ:
            if (type != TIMESTAMPTZOID)
                return -1;
            *intresult = -tz;
            return 1;
        case DTK_TZ_MINUTE:
            if (type != TIMESTAMPTZOID)
                return -1;
            *intresult = (-tz / SECS_PER_MINUTE) % MINS_PER_HOUR;
            return 1;
        case DTK_TZ_HOUR:
            if (type != TIMESTAMPTZOID)
                return -1;
            *intresult = -tz / SECS_PER_HOUR;
            return 1;
        default:
            return -1;
    }
}

static int
extract_interval_common(int val, struct pg_tm *tm, fsec_t fsec,
                        const PgInterval *iv, int64 *intresult, int *special)
{
    (void) fsec;
    (void) iv;
    *special = 0;
    switch (val)
    {
        case DTK_MICROSEC:
            *special = DTX_MICROSEC;
            return 0;
        case DTK_MILLISEC:
            *special = DTX_MILLISEC;
            return 0;
        case DTK_SECOND:
            *special = DTX_SECOND;
            return 0;
        case DTK_EPOCH:
            *special = DTX_EPOCH;
            return 0;
        case DTK_MINUTE:
            *intresult = tm->tm_min;
            return 1;
        case DTK_HOUR:
            *intresult = tm->tm_hour;
            return 1;
        case DTK_DAY:
            *intresult = tm->tm_mday;
            return 1;
        case DTK_MONTH:
            *intresult = tm->tm_mon;
            return 1;
        case DTK_QUARTER:
#if PGVC_VERNUM >= 118
            /*
             * PostgreSQL 18 made a field of a negative interval the negation
             * of the field of the sign-reversed interval.
             */
            if (iv->month >= 0)
                *intresult = (tm->tm_mon / 3) + 1;
            else
                *intresult = -(((-iv->month % MONTHS_PER_YEAR) / 3) + 1);
#else
            *intresult = (tm->tm_mon / 3) + 1;
#endif
            return 1;
        case DTK_YEAR:
            *intresult = tm->tm_year;
            return 1;
        case DTK_DECADE:
            *intresult = tm->tm_year / 10;
            return 1;
        case DTK_CENTURY:
            *intresult = tm->tm_year / 100;
            return 1;
        case DTK_MILLENNIUM:
            *intresult = tm->tm_year / 1000;
            return 1;
#if PGVC_VERNUM >= 118
        case DTK_WEEK:
            *intresult = iv->day / 7;
            return 1;
#endif
        default:
            return -1;
    }
}

static int64
interval_epoch_usec(const PgInterval *iv)
{
    int64 secs_from_day_month;

    secs_from_day_month = ((int64) SECS_PER_YEAR * (iv->month / MONTHS_PER_YEAR) +
                           (int64) SECS_PER_DAY * DAYS_PER_MONTH *
                           (iv->month % MONTHS_PER_YEAR) +
                           (int64) SECS_PER_DAY * iv->day);
    return iv->usec + secs_from_day_month * USECS_PER_SEC;
}

/*
 * Shared body of dt_extract()/dt_extract_numeric().
 *
 * `numeric_semantics` selects PostgreSQL 14+ extract() behaviour (in
 * particular, a date argument is handled natively rather than by casting to
 * timestamp).  Returns false and leaves the message in dt_errbuf on error.
 */
static bool
dt_extract_body(const char *field, Oid type, int64 val, const PgInterval *iv,
                bool numeric_semantics, double *dres, char **sres)
{
    char lowunits[TOKMAXLEN + 32];
    int utype, uval;
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec = 0;
    int tz = 0;
    int64 intresult = 0;
    int special = 0;
    int rc;
    Oid errtype = type;

    /*
     * Before 14, date_part(text, date) was implemented by casting the date to
     * a timestamp, so its errors name "timestamp", not "date".
     */
    if (type == DATEOID && !numeric_semantics)
        errtype = TIMESTAMPOID;

    dt_lowerunits(field, lowunits, sizeof(lowunits));
    utype = DecodeAnyUnits(lowunits, &uval);
    if (utype != UNITS && !(utype == RESERV && uval == DTK_EPOCH))
    {
        dt_unit_error(errtype, lowunits, false);
        return false;
    }
    if (utype == RESERV)
        uval = DTK_EPOCH;

    if (type == INTERVALOID)
    {
        PgInterval zero = {0, 0, 0};
        const PgInterval *sp = iv ? iv : &zero;

        interval2tm(*sp, tm, &fsec);
        rc = extract_interval_common(uval, tm, fsec, sp, &intresult, &special);
        if (rc < 0)
        {
            dt_unit_error(INTERVALOID, lowunits, true);
            return false;
        }
        if (special == DTX_EPOCH)
        {
            int64 eus = interval_epoch_usec(sp);

            if (sres)
                *sres = num_scaled_str(eus, 6);
            if (dres)
                *dres = (double) eus / 1000000.0;
            return true;
        }
    }
    else if (type == TIMEOID)
    {
        dt2time(val, &tm->tm_hour, &tm->tm_min, &tm->tm_sec, &fsec);
        switch (uval)
        {
            case DTK_MICROSEC: special = DTX_MICROSEC; break;
            case DTK_MILLISEC: special = DTX_MILLISEC; break;
            case DTK_SECOND:   special = DTX_SECOND; break;
            case DTK_MINUTE:   intresult = tm->tm_min; break;
            case DTK_HOUR:     intresult = tm->tm_hour; break;
            case DTK_EPOCH:
                if (sres)
                    *sres = num_scaled_str(val, 6);
                if (dres)
                    *dres = (double) val / 1000000.0;
                return true;
            default:
                dt_unit_error(TIMEOID, lowunits, true);
                return false;
        }
    }
    else if (type == DATEOID && numeric_semantics)
    {
        /* PostgreSQL 14+: extract() has a native date implementation */
        int year, mon, mday;
        int32 date = (int32) val;

        j2date((int) date + POSTGRES_EPOCH_JDATE, &year, &mon, &mday);
        switch (uval)
        {
            case DTK_DAY:    intresult = mday; break;
            case DTK_MONTH:  intresult = mon; break;
            case DTK_QUARTER: intresult = (mon - 1) / 3 + 1; break;
            case DTK_WEEK:   intresult = date2isoweek(year, mon, mday); break;
            case DTK_YEAR:   intresult = (year > 0) ? year : (year - 1); break;
            case DTK_DECADE:
                if (year >= 0)
                    intresult = year / 10;
                else
                    intresult = -((8 - (year - 1)) / 10);
                break;
            case DTK_CENTURY:
                if (year > 0)
                    intresult = (year + 99) / 100;
                else
                    intresult = -((99 - (year - 1)) / 100);
                break;
            case DTK_MILLENNIUM:
                if (year > 0)
                    intresult = (year + 999) / 1000;
                else
                    intresult = -((999 - (year - 1)) / 1000);
                break;
            case DTK_JULIAN:
                intresult = (int64) date + POSTGRES_EPOCH_JDATE;
                break;
            case DTK_ISOYEAR:
                intresult = date2isoyear(year, mon, mday);
                if (intresult <= 0)
                    intresult -= 1;
                break;
            case DTK_DOW:
            case DTK_ISODOW:
                intresult = j2day((int) date + POSTGRES_EPOCH_JDATE);
                if (uval == DTK_ISODOW && intresult == 0)
                    intresult = 7;
                break;
            case DTK_DOY:
                intresult = date2j(year, mon, mday) - date2j(year, 1, 1) + 1;
                break;
            case DTK_EPOCH:
                intresult = ((int64) date + POSTGRES_EPOCH_JDATE -
                             UNIX_EPOCH_JDATE) * SECS_PER_DAY;
                break;
            default:
                dt_unit_error(DATEOID, lowunits, true);
                return false;
        }
        special = 0;
    }
    else
    {
        /*
         * timestamp / timestamptz, and (pre-14 date_part semantics) date,
         * which PostgreSQL handled by casting the date to a timestamp - which
         * is also why the error messages name "timestamp".
         */
        int64 ts;

        if (type == DATEOID)
            ts = (int64) ((int32) val) * USECS_PER_DAY;
        else
            ts = val;

        if (timestamp2tm(ts, (type == TIMESTAMPTZOID) ? &tz : NULL, tm, &fsec) != 0)
        {
            dt_err("timestamp out of range");
            return false;
        }

        rc = extract_timestamp_common(uval, tm, fsec, tz, errtype,
                                      &intresult, &special);
        if (rc < 0)
        {
            dt_unit_error(errtype, lowunits, true);
            return false;
        }

        if (special == DTX_EPOCH)
        {
            int64 eus = ts - (int64) (UNIX_EPOCH_JDATE - POSTGRES_EPOCH_JDATE) *
                        USECS_PER_DAY;

            if (sres)
                *sres = num_scaled_str(eus, 6);
            if (dres)
                *dres = (double) eus / 1000000.0;
            return true;
        }
        if (special == DTX_JULIAN)
        {
            int jd = date2j(tm->tm_year, tm->tm_mon, tm->tm_mday);
            int64 usec_of_day = time2t(tm->tm_hour, tm->tm_min, tm->tm_sec, fsec);

            if (sres)
                *sres = num_julian_str(jd, usec_of_day);
            if (dres)
                *dres = (double) jd +
                    ((((tm->tm_hour * MINS_PER_HOUR) + tm->tm_min) * SECS_PER_MINUTE) +
                     tm->tm_sec + (fsec / 1000000.0)) / (double) SECS_PER_DAY;
            return true;
        }
    }

    switch (special)
    {
        case DTX_MICROSEC:
            intresult = tm->tm_sec * INT64_C(1000000) + fsec;
            if (sres)
                *sres = num_int64_str(intresult);
            if (dres)
                *dres = (double) tm->tm_sec * 1000000.0 + (double) fsec;
            return true;
        case DTX_MILLISEC:
            intresult = tm->tm_sec * INT64_C(1000000) + fsec;
            if (sres)
                *sres = num_scaled_str(intresult, 3);
            if (dres)
                *dres = (double) tm->tm_sec * 1000.0 + (double) fsec / 1000.0;
            return true;
        case DTX_SECOND:
            intresult = tm->tm_sec * INT64_C(1000000) + fsec;
            if (sres)
                *sres = num_scaled_str(intresult, 6);
            if (dres)
                *dres = (double) tm->tm_sec + (double) fsec / 1000000.0;
            return true;
        default:
            break;
    }

    if (sres)
        *sres = num_int64_str(intresult);
    if (dres)
        *dres = (double) intresult;
    return true;
}

double
dt_extract(const char *field, Oid type, int64 val, const PgInterval *iv,
           bool *ok)
{
    double d = 0.0;
    bool res = dt_extract_body(field, type, val, iv, false, &d, NULL);

    if (ok)
        *ok = res;
    return res ? d : 0.0;
}

char *
dt_extract_numeric(const char *field, Oid type, int64 val, const PgInterval *iv,
                   bool *ok)
{
    char *s = NULL;
    bool res = dt_extract_body(field, type, val, iv, true, NULL, &s);

    if (ok)
        *ok = res;
    if (!res)
        return NULL;
    return s;
}

/* ------------------------------------------------------------------ *
 *  Arithmetic helpers used by the execution engine
 * ------------------------------------------------------------------ */
#define TSROUND(j)  (rint(((double) (j)) * 1000000.0) / 1000000.0)

bool
dt_date_to_timestamp(int32 day, int64 *out, const char **err)
{
    if (day == INT32_MIN)
    {
        *out = DT_NOBEGIN;
        return true;
    }
    if (day == INT32_MAX)
    {
        *out = DT_NOEND;
        return true;
    }
    /* date is always in range for a timestamp? no - check it */
    if (day < (int32) (MIN_TIMESTAMP / USECS_PER_DAY) ||
        day > (int32) (END_TIMESTAMP / USECS_PER_DAY))
    {
        if (err)
            *err = dt_err("date out of range for timestamp");
        return false;
    }
    *out = (int64) day * USECS_PER_DAY;
    return true;
}

bool
dt_date_add_days(int32 day, int32 ndays, int32 *out, const char **err)
{
    int64 result = (int64) day + (int64) ndays;

    if (result > INT32_MAX || result < INT32_MIN || !IS_VALID_DATE((int32) result))
    {
        if (err)
            *err = dt_err("date out of range");
        return false;
    }
    *out = (int32) result;
    return true;
}

int32
dt_date_sub_date(int32 a, int32 b)
{
    return (int32) ((int64) a - (int64) b);
}

bool
dt_timestamp_add_interval(int64 ts, const PgInterval *span, int64 *out,
                          const char **err)
{
    int64 timestamp = ts;

    if (TIMESTAMP_NOT_FINITE(timestamp))
    {
        *out = timestamp;
        return true;
    }

    if (span->month != 0)
    {
        struct pg_tm tt, *tm = &tt;
        fsec_t fsec;

        if (timestamp2tm(timestamp, NULL, tm, &fsec) != 0)
            goto range;

        tm->tm_mon += span->month;
        if (tm->tm_mon > MONTHS_PER_YEAR)
        {
            tm->tm_year += (tm->tm_mon - 1) / MONTHS_PER_YEAR;
            tm->tm_mon = ((tm->tm_mon - 1) % MONTHS_PER_YEAR) + 1;
        }
        else if (tm->tm_mon < 1)
        {
            tm->tm_year += tm->tm_mon / MONTHS_PER_YEAR - 1;
            tm->tm_mon = tm->tm_mon % MONTHS_PER_YEAR + MONTHS_PER_YEAR;
        }

        /* adjust for end-of-month boundary problems */
        if (tm->tm_mday > day_tab[isleap(tm->tm_year)][tm->tm_mon - 1])
            tm->tm_mday = day_tab[isleap(tm->tm_year)][tm->tm_mon - 1];

        if (tm2timestamp(tm, fsec, NULL, &timestamp) != 0)
            goto range;
    }

    if (span->day != 0)
    {
        struct pg_tm tt, *tm = &tt;
        fsec_t fsec;
        int julian;

        if (timestamp2tm(timestamp, NULL, tm, &fsec) != 0)
            goto range;

        julian = date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) + span->day;
        j2date(julian, &tm->tm_year, &tm->tm_mon, &tm->tm_mday);

        if (tm2timestamp(tm, fsec, NULL, &timestamp) != 0)
            goto range;
    }

    timestamp += span->usec;
    if (!IS_VALID_TIMESTAMP(timestamp))
        goto range;

    *out = timestamp;
    return true;

range:
    if (err)
        *err = dt_err("timestamp out of range");
    return false;
}

bool
dt_timestamp_sub_interval(int64 ts, const PgInterval *span, int64 *out,
                          const char **err)
{
    PgInterval tspan;

    tspan.month = -span->month;
    tspan.day = -span->day;
    tspan.usec = -span->usec;

    return dt_timestamp_add_interval(ts, &tspan, out, err);
}

void
dt_interval_justify_hours(const PgInterval *span, PgInterval *result)
{
    int64 wholeday;

    result->month = span->month;
    result->day = span->day;
    result->usec = span->usec;

    wholeday = result->usec / USECS_PER_DAY;
    result->usec -= wholeday * USECS_PER_DAY;
    result->day += (int32) wholeday;

    if (result->day > 0 && result->usec < 0)
    {
        result->usec += USECS_PER_DAY;
        result->day--;
    }
    else if (result->day < 0 && result->usec > 0)
    {
        result->usec -= USECS_PER_DAY;
        result->day++;
    }
}

void
dt_interval_justify_days(const PgInterval *span, PgInterval *result)
{
    int32 wholemonth;

    result->month = span->month;
    result->day = span->day;
    result->usec = span->usec;

    wholemonth = result->day / DAYS_PER_MONTH;
    result->day -= wholemonth * DAYS_PER_MONTH;
    result->month += wholemonth;

    if (result->month > 0 && result->day < 0)
    {
        result->day += DAYS_PER_MONTH;
        result->month--;
    }
    else if (result->month < 0 && result->day > 0)
    {
        result->day -= DAYS_PER_MONTH;
        result->month++;
    }
}

void
dt_interval_justify_interval(const PgInterval *span, PgInterval *result)
{
    int64 wholeday;
    int32 wholemonth;

    result->month = span->month;
    result->day = span->day;
    result->usec = span->usec;

    wholeday = result->usec / USECS_PER_DAY;
    result->usec -= wholeday * USECS_PER_DAY;
    result->day += (int32) wholeday;

    wholemonth = result->day / DAYS_PER_MONTH;
    result->day -= wholemonth * DAYS_PER_MONTH;
    result->month += wholemonth;

    if (result->month > 0 && (result->day < 0 || (result->day == 0 && result->usec < 0)))
    {
        result->day += DAYS_PER_MONTH;
        result->month--;
    }
    else if (result->month < 0 && (result->day > 0 || (result->day == 0 && result->usec > 0)))
    {
        result->day -= DAYS_PER_MONTH;
        result->month++;
    }

    if (result->day > 0 && result->usec < 0)
    {
        result->usec += USECS_PER_DAY;
        result->day--;
    }
    else if (result->day < 0 && result->usec > 0)
    {
        result->usec -= USECS_PER_DAY;
        result->day++;
    }
}

bool
dt_timestamp_sub_timestamp(int64 a, int64 b, PgInterval *out, const char **err)
{
    PgInterval tmp;

    if (TIMESTAMP_NOT_FINITE(a) || TIMESTAMP_NOT_FINITE(b))
    {
        if (err)
            *err = dt_err("cannot subtract infinite timestamps");
        return false;
    }

    tmp.usec = a - b;
    tmp.month = 0;
    tmp.day = 0;

    /*
     * This is arguably wrong, but PostgreSQL has always justified the hours
     * here and changing it would break compatibility.
     */
    dt_interval_justify_hours(&tmp, out);
    return true;
}

bool
dt_interval_add(const PgInterval *a, const PgInterval *b, PgInterval *out,
                const char **err)
{
    int64 month = (int64) a->month + (int64) b->month;
    int64 day = (int64) a->day + (int64) b->day;

    if (month > INT32_MAX || month < INT32_MIN ||
        day > INT32_MAX || day < INT32_MIN)
    {
        if (err)
            *err = dt_err("interval out of range");
        return false;
    }
    out->month = (int32) month;
    out->day = (int32) day;
    out->usec = a->usec + b->usec;
    return true;
}

bool
dt_interval_sub(const PgInterval *a, const PgInterval *b, PgInterval *out,
                const char **err)
{
    PgInterval nb;

    nb.month = -b->month;
    nb.day = -b->day;
    nb.usec = -b->usec;
    return dt_interval_add(a, &nb, out, err);
}

void
dt_interval_negate(const PgInterval *a, PgInterval *out)
{
    out->month = -a->month;
    out->day = -a->day;
    out->usec = -a->usec;
}

bool
dt_interval_mul(const PgInterval *span, double factor, PgInterval *result,
                const char **err)
{
    double month_remainder_days, sec_remainder, result_double;
    int32 orig_month = span->month, orig_day = span->day;

    result_double = orig_month * factor;
    if (isnan(result_double) ||
        result_double > INT32_MAX || result_double < INT32_MIN)
        goto range;
    result->month = (int32) result_double;

    result_double = orig_day * factor;
    if (isnan(result_double) ||
        result_double > INT32_MAX || result_double < INT32_MIN)
        goto range;
    result->day = (int32) result_double;

    /*
     * The above handles the whole-number parts.  Cascade the fractional parts
     * down to lower units using DAYS_PER_MONTH and SECS_PER_DAY; never up.
     */
    month_remainder_days = (orig_month * factor - result->month) * DAYS_PER_MONTH;
    month_remainder_days = TSROUND(month_remainder_days);
    sec_remainder = (orig_day * factor - result->day +
                     month_remainder_days - (int) month_remainder_days) * SECS_PER_DAY;
    sec_remainder = TSROUND(sec_remainder);

    if (fabs(sec_remainder) >= SECS_PER_DAY)
    {
        result->day += (int) (sec_remainder / SECS_PER_DAY);
        sec_remainder -= (int) (sec_remainder / SECS_PER_DAY) * SECS_PER_DAY;
    }

    /* cascade units down */
    result->day += (int32) month_remainder_days;
    result_double = rint(span->usec * factor + sec_remainder * 1000000.0);
    if (isnan(result_double) ||
        result_double > (double) INT64_MAX || result_double < (double) INT64_MIN)
        goto range;
    result->usec = (int64) result_double;

    return true;

range:
    if (err)
        *err = dt_err("interval out of range");
    return false;
}

bool
dt_interval_div(const PgInterval *span, double factor, PgInterval *result,
                const char **err)
{
    double month_remainder_days, sec_remainder;
    int32 orig_month = span->month, orig_day = span->day;

    if (factor == 0.0)
    {
        if (err)
            *err = dt_err("division by zero");
        return false;
    }

    result->month = (int32) (orig_month / factor);
    result->day = (int32) (orig_day / factor);

    month_remainder_days = (orig_month / factor - result->month) * DAYS_PER_MONTH;
    month_remainder_days = TSROUND(month_remainder_days);
    sec_remainder = (orig_day / factor - result->day +
                     month_remainder_days - (int) month_remainder_days) * SECS_PER_DAY;
    sec_remainder = TSROUND(sec_remainder);
    if (fabs(sec_remainder) >= SECS_PER_DAY)
    {
        result->day += (int) (sec_remainder / SECS_PER_DAY);
        sec_remainder -= (int) (sec_remainder / SECS_PER_DAY) * SECS_PER_DAY;
    }

    result->day += (int32) month_remainder_days;
    result->usec = (int64) rint(span->usec / factor + sec_remainder * 1000000.0);

    return true;
}

bool
dt_date_add_interval(int32 day, const PgInterval *iv, int64 *out_ts,
                     const char **err)
{
    int64 ts;

    if (!dt_date_to_timestamp(day, &ts, err))
        return false;
    return dt_timestamp_add_interval(ts, iv, out_ts, err);
}

bool
dt_date_sub_interval(int32 day, const PgInterval *iv, int64 *out_ts,
                     const char **err)
{
    int64 ts;

    if (!dt_date_to_timestamp(day, &ts, err))
        return false;
    return dt_timestamp_sub_interval(ts, iv, out_ts, err);
}

/* ------------------------------------------------------------------ *
 *  age()
 * ------------------------------------------------------------------ */
bool
dt_age(int64 dt1, int64 dt2, PgInterval *out, const char **err)
{
    fsec_t fsec, fsec1, fsec2;
    struct pg_tm tt, *tm = &tt;
    struct pg_tm tt1, *tm1 = &tt1;
    struct pg_tm tt2, *tm2 = &tt2;

    if (TIMESTAMP_NOT_FINITE(dt1) || TIMESTAMP_NOT_FINITE(dt2))
    {
        if (err)
            *err = dt_err("cannot take age of infinite timestamps");
        return false;
    }

    if (timestamp2tm(dt1, NULL, tm1, &fsec1) != 0 ||
        timestamp2tm(dt2, NULL, tm2, &fsec2) != 0)
    {
        if (err)
            *err = dt_err("timestamp out of range");
        return false;
    }

    /* form the symbolic difference */
    fsec = fsec1 - fsec2;
    tm->tm_sec = tm1->tm_sec - tm2->tm_sec;
    tm->tm_min = tm1->tm_min - tm2->tm_min;
    tm->tm_hour = tm1->tm_hour - tm2->tm_hour;
    tm->tm_mday = tm1->tm_mday - tm2->tm_mday;
    tm->tm_mon = tm1->tm_mon - tm2->tm_mon;
    tm->tm_year = tm1->tm_year - tm2->tm_year;

    /* flip sign if necessary */
    if (dt1 < dt2)
    {
        fsec = -fsec;
        tm->tm_sec = -tm->tm_sec;
        tm->tm_min = -tm->tm_min;
        tm->tm_hour = -tm->tm_hour;
        tm->tm_mday = -tm->tm_mday;
        tm->tm_mon = -tm->tm_mon;
        tm->tm_year = -tm->tm_year;
    }

    /* propagate any negative fields into the next higher field */
    while (fsec < 0)
    {
        fsec += USECS_PER_SEC;
        tm->tm_sec--;
    }
    while (tm->tm_sec < 0)
    {
        tm->tm_sec += SECS_PER_MINUTE;
        tm->tm_min--;
    }
    while (tm->tm_min < 0)
    {
        tm->tm_min += MINS_PER_HOUR;
        tm->tm_hour--;
    }
    while (tm->tm_hour < 0)
    {
        tm->tm_hour += HOURS_PER_DAY;
        tm->tm_mday--;
    }
    while (tm->tm_mday < 0)
    {
        if (dt1 < dt2)
        {
            tm->tm_mday += day_tab[isleap(tm1->tm_year)][tm1->tm_mon - 1];
            tm->tm_mon--;
        }
        else
        {
            tm->tm_mday += day_tab[isleap(tm2->tm_year)][tm2->tm_mon - 1];
            tm->tm_mon--;
        }
    }
    while (tm->tm_mon < 0)
    {
        tm->tm_mon += MONTHS_PER_YEAR;
        tm->tm_year--;
    }

    /* recover sign if necessary */
    if (dt1 < dt2)
    {
        fsec = -fsec;
        tm->tm_sec = -tm->tm_sec;
        tm->tm_min = -tm->tm_min;
        tm->tm_hour = -tm->tm_hour;
        tm->tm_mday = -tm->tm_mday;
        tm->tm_mon = -tm->tm_mon;
        tm->tm_year = -tm->tm_year;
    }

    if (tm2interval(tm, fsec, out) != 0)
    {
        if (err)
            *err = dt_err("interval out of range");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ *
 *  date_trunc()
 * ------------------------------------------------------------------ */
bool
dt_date_trunc(const char *field, int64 ts, bool with_tz, int64 *out,
              const char **err)
{
    char lowunits[TOKMAXLEN + 32];
    int type, val;
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec;
    int64 result;
    Oid etype = with_tz ? TIMESTAMPTZOID : TIMESTAMPOID;

    if (TIMESTAMP_NOT_FINITE(ts))
    {
        *out = ts;
        return true;
    }

    dt_lowerunits(field, lowunits, sizeof(lowunits));
    type = DecodeAnyUnits(lowunits, &val);
    if (type != UNITS)
    {
        if (err)
            *err = dt_unit_error(etype, lowunits, false);
        return false;
    }

    if (timestamp2tm(ts, NULL, tm, &fsec) != 0)
    {
        if (err)
            *err = dt_err("timestamp out of range");
        return false;
    }

    switch (val)
    {
        case DTK_WEEK:
        {
            int woy = j2day(date2j(tm->tm_year, tm->tm_mon, tm->tm_mday));
            int back = (woy == 0) ? 6 : (woy - 1);

            j2date(date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) - back,
                   &tm->tm_year, &tm->tm_mon, &tm->tm_mday);
            tm->tm_hour = 0;
            tm->tm_min = 0;
            tm->tm_sec = 0;
            fsec = 0;
            break;
        }
        case DTK_MILLENNIUM:
            if (tm->tm_year > 0)
                tm->tm_year = ((tm->tm_year + 999) / 1000) * 1000 - 999;
            else
                tm->tm_year = -((999 - (tm->tm_year - 1)) / 1000) * 1000 + 1;
            /* FALLTHROUGH */
        case DTK_CENTURY:
            if (val == DTK_CENTURY)
            {
                if (tm->tm_year > 0)
                    tm->tm_year = ((tm->tm_year + 99) / 100) * 100 - 99;
                else
                    tm->tm_year = -((99 - (tm->tm_year - 1)) / 100) * 100 + 1;
            }
            /* FALLTHROUGH */
        case DTK_DECADE:
            if (val == DTK_DECADE)
            {
                if (tm->tm_year > 0)
                    tm->tm_year = (tm->tm_year / 10) * 10;
                else
                    tm->tm_year = -((8 - (tm->tm_year - 1)) / 10) * 10;
            }
            /* FALLTHROUGH */
        case DTK_YEAR:
            tm->tm_mon = 1;
            /* FALLTHROUGH */
        case DTK_QUARTER:
            if (val == DTK_QUARTER)
                tm->tm_mon = (3 * ((tm->tm_mon - 1) / 3)) + 1;
            /* FALLTHROUGH */
        case DTK_MONTH:
            tm->tm_mday = 1;
            /* FALLTHROUGH */
        case DTK_DAY:
            tm->tm_hour = 0;
            /* FALLTHROUGH */
        case DTK_HOUR:
            tm->tm_min = 0;
            /* FALLTHROUGH */
        case DTK_MINUTE:
            tm->tm_sec = 0;
            /* FALLTHROUGH */
        case DTK_SECOND:
            fsec = 0;
            break;
        case DTK_MILLISEC:
            fsec = (fsec / 1000) * 1000;
            break;
        case DTK_MICROSEC:
            break;
        default:
            if (err)
                *err = dt_unit_error(etype, lowunits, true);
            return false;
    }

    if (tm2timestamp(tm, fsec, NULL, &result) != 0)
    {
        if (err)
            *err = dt_err("timestamp out of range");
        return false;
    }
    *out = result;
    return true;
}

/* ------------------------------------------------------------------ *
 *  to_date() / to_timestamp()
 *
 *  Only the numeric patterns YYYY MM DD HH24 MI SS are supported, which is
 *  all the engine needs.  Anything else in the format string is treated as
 *  literal text, exactly as PostgreSQL treats unrecognised characters.
 *
 *  PostgreSQL 10 added range checking of the decoded fields; 9.6 silently
 *  rolled values over ('2020-13-01' -> 2021-01-01), so the pre-10 behaviour
 *  is kept behind #if PGVC_MAJOR < 10.
 * ------------------------------------------------------------------ */
typedef struct
{
    const char *name;           /* keyword, as PostgreSQL spells it */
    int klen;                   /* length of the keyword */
    int dlen;                   /* number of input digits it consumes */
    int key;
} DchKey;

#define DCH_YYYY 1
#define DCH_MM   2
#define DCH_DD   3
#define DCH_HH24 4
#define DCH_MI   5
#define DCH_SS   6
#define DCH_HH   7              /* the 12-hour clock: HH and HH12 */

/* longest keyword first, so HH24 is preferred over HH */
static const DchKey dch_keys[] = {
    {"HH24", 4, 2, DCH_HH24},   /* DCH_from_char passes an explicit len of 2 */
    {"HH12", 4, 2, DCH_HH},
    {"YYYY", 4, 4, DCH_YYYY},
    {"MM",   2, 2, DCH_MM},
    {"DD",   2, 2, DCH_DD},
    {"HH",   2, 2, DCH_HH},
    {"MI",   2, 2, DCH_MI},
    {"SS",   2, 2, DCH_SS},
};

typedef struct
{
    int key;                    /* 0 == literal character, -1 == end */
    const char *name;
    char ch;
    int dlen;
} DchNode;

static int
dch_parse_format(const char *fmt, DchNode *nodes, int maxnodes)
{
    int n = 0;
    const char *p = fmt;

    while (*p && n < maxnodes - 1)
    {
        size_t i;
        bool matched = false;

        for (i = 0; i < sizeof(dch_keys) / sizeof(dch_keys[0]); i++)
        {
            const DchKey *k = &dch_keys[i];
            int j;

            for (j = 0; j < k->klen; j++)
            {
                if (toupper((unsigned char) p[j]) != k->name[j])
                    break;
            }
            if (j == k->klen)
            {
                nodes[n].key = k->key;
                nodes[n].name = k->name;
                nodes[n].ch = '\0';
                nodes[n].dlen = k->dlen;
                n++;
                p += k->klen;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            nodes[n].key = 0;
            nodes[n].name = NULL;
            nodes[n].ch = *p++;
            nodes[n].dlen = 0;
            n++;
        }
    }
    nodes[n].key = -1;
    nodes[n].name = NULL;
    nodes[n].ch = '\0';
    nodes[n].dlen = 0;
    return n;
}

/*
 * PostgreSQL's is_next_separator(): may the current field slurp greedily?
 * The end of the format string counts as a separator.
 */
static bool
dch_is_next_separator(const DchNode *nodes, int i)
{
    const DchNode *n = &nodes[i + 1];

    if (n->key == -1)
        return true;
    if (n->key > 0)             /* another numeric field */
        return false;
    if (n->ch && isdigit((unsigned char) n->ch))
        return false;
    return true;
}

/* PostgreSQL's from_char_parse_int_len() */
static bool
dch_parse_int(const char **src, const DchNode *nodes, int i, int *dest,
              const char **err)
{
    const DchNode *node = &nodes[i];
    const char *init = *src;
    const char *s = *src;
    char copy[32];
    long result = 0;
    int used;
    int len = node->dlen;

    while (*s == ' ')
        s++;

    used = 0;
    while (used < len && s[used] != '\0')
    {
        copy[used] = s[used];
        used++;
    }
    copy[used] = '\0';

    if (dch_is_next_separator(nodes, i))
    {
        char *endptr;

        errno = 0;
        result = strtol(init, &endptr, 10);
        s = endptr;
    }
    else
    {
        char *last;

        if (used < len)
        {
            if (err)
                *err = dt_err("source string too short for \"%s\" formatting field",
                              node->name);
            return false;
        }
        errno = 0;
        result = strtol(copy, &last, 10);
        used = (int) (last - copy);
        if (used > 0 && used < len)
        {
            if (err)
                *err = dt_err("invalid value \"%s\" for \"%s\"", copy, node->name);
            return false;
        }
        s += used;
    }

    if (s == init)
    {
        if (err)
            *err = dt_err("invalid value \"%s\" for \"%s\"", copy, node->name);
        return false;
    }

    if (errno == ERANGE || result < INT_MIN || result > INT_MAX)
    {
        if (err)
            *err = dt_err("value for \"%s\" in source string is out of range",
                          node->name);
        return false;
    }

    *dest = (int) result;
    *src = s;
    return true;
}

static bool
dch_is_separator(char c)
{
    return !isalnum((unsigned char) c);
}

/*
 * PostgreSQL's DCH_from_char() restricted to the numeric patterns, then the
 * field-assembly half of do_to_timestamp().  Note the outer loop stops as
 * soon as the input is exhausted, which is why to_date('', 'YYYY-MM-DD')
 * yields the all-defaults date rather than an error.
 */
static bool
do_to_timestamp(const char *s, const char *fmt, struct pg_tm *tm, fsec_t *fsec,
                unsigned int *fmaskp, const char **err)
{
    DchNode nodes[512];
    int nnodes;
    int i;
    const char *cp = s;
    unsigned int fmask = 0;
    int year = 0, mm = 0, dd = 0, hh = 0, mi = 0, ss = 0;
    bool hh12 = false;

    nnodes = dch_parse_format(fmt, nodes, 512);

    /* ZERO_tm(): everything zero except month and day, which default to 1 */
    ClearPgTm(tm, fsec);
    tm->tm_mon = 1;
    tm->tm_mday = 1;

    for (i = 0; i < nnodes && *cp != '\0'; i++)
    {
        if (nodes[i].key > 0)
        {
            /* skip spaces before a field (non-FX mode) */
            while (*cp != '\0' && isspace((unsigned char) *cp))
                cp++;
        }

        if (nodes[i].key == 0)
        {
            if (isspace((unsigned char) nodes[i].ch) || dch_is_separator(nodes[i].ch))
            {
                /*
                 * One format space or separator matches at most one space or
                 * separator in the input.
                 */
                if (*cp && (isspace((unsigned char) *cp) || dch_is_separator(*cp)))
                    cp++;
            }
            else if (*cp)
                cp++;
            continue;
        }

        switch (nodes[i].key)
        {
            case DCH_YYYY:
                if (!dch_parse_int(&cp, nodes, i, &year, err))
                    return false;
                break;
            case DCH_MM:
                if (!dch_parse_int(&cp, nodes, i, &mm, err))
                    return false;
                break;
            case DCH_DD:
                if (!dch_parse_int(&cp, nodes, i, &dd, err))
                    return false;
                break;
            case DCH_HH24:
                if (!dch_parse_int(&cp, nodes, i, &hh, err))
                    return false;
                break;
            case DCH_HH:
                if (!dch_parse_int(&cp, nodes, i, &hh, err))
                    return false;
                hh12 = true;
                break;
            case DCH_MI:
                if (!dch_parse_int(&cp, nodes, i, &mi, err))
                    return false;
                break;
            case DCH_SS:
                if (!dch_parse_int(&cp, nodes, i, &ss, err))
                    return false;
                break;
            default:
                break;
        }
    }

    if (hh12 && (hh < 1 || hh > 12))
    {
        if (err)
            *err = dt_err("hour \"%d\" is invalid for the 12-hour clock", hh);
        return false;
    }
    if (ss)
        tm->tm_sec = ss;
    if (mi)
        tm->tm_min = mi;
    if (hh)
        tm->tm_hour = (hh12 && hh == 12) ? 0 : hh;

    /*
     * PostgreSQL only records a year when it is nonzero, which is why
     * to_date('0000-01-01','YYYY-MM-DD') comes out as 0001-01-01 BC.
     */
    if (year)
    {
        tm->tm_year = year;
        fmask |= DTK_M(YEAR);
    }
    if (dd)
    {
        tm->tm_mday = dd;
        fmask |= DTK_M(DAY);
    }
    if (mm)
    {
        tm->tm_mon = mm;
        fmask |= DTK_M(MONTH);
    }

    *fmaskp = fmask;
    return true;
}

static bool
to_datetime_common(const char *s, const char *fmt, struct pg_tm *tm,
                   fsec_t *fsec, const char *tname, const char **err)
{
    unsigned int fmask = 0;

    if (!do_to_timestamp(s, fmt, tm, fsec, &fmask, err))
        return false;

#if PGVC_VERNUM >= 110
    /* Range-check the decoded values (added in PostgreSQL 10) */
    if (fmask != 0)
    {
        /* AD/BC has already been dealt with, so pass isjulian = true */
        if (ValidateDate(fmask, true, false, false, tm) != 0)
        {
            if (err)
                *err = dt_err("date/time field value out of range: \"%s\"", s);
            return false;
        }
    }

    if (tm->tm_hour < 0 || tm->tm_hour >= HOURS_PER_DAY ||
        tm->tm_min < 0 || tm->tm_min >= MINS_PER_HOUR ||
        tm->tm_sec < 0 || tm->tm_sec >= SECS_PER_MINUTE ||
        *fsec < INT64_C(0) || *fsec >= USECS_PER_SEC)
    {
        if (err)
            *err = dt_err("date/time field value out of range: \"%s\"", s);
        return false;
    }
#else
    /*
     * PostgreSQL 9.6 and earlier did no range checking here at all, so
     * out-of-range values simply roll over into the next unit.
     */
    (void) fmask;
#endif
    (void) tname;
    return true;
}

bool
dt_to_date(const char *s, const char *fmt, int32 *out, const char **err)
{
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec;
    int32 date;

    if (!to_datetime_common(s, fmt, tm, &fsec, "date", err))
        return false;

    if (!IS_VALID_JULIAN(tm->tm_year, tm->tm_mon, tm->tm_mday))
    {
        if (err)
            *err = dt_err("date out of range: \"%s\"", s);
        return false;
    }

    date = (int32) (date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) -
                    POSTGRES_EPOCH_JDATE);
    if (!IS_VALID_DATE(date))
    {
        if (err)
            *err = dt_err("date out of range: \"%s\"", s);
        return false;
    }
    *out = date;
    return true;
}

bool
dt_to_timestamp(const char *s, const char *fmt, int64 *out, const char **err)
{
    struct pg_tm tt, *tm = &tt;
    fsec_t fsec;
    int64 result;

    if (!to_datetime_common(s, fmt, tm, &fsec, "timestamp", err))
        return false;

    if (tm2timestamp(tm, fsec, NULL, &result) != 0)
    {
        if (err)
            *err = dt_err("timestamp out of range: \"%s\"", s);
        return false;
    }
    *out = result;
    return true;
}
