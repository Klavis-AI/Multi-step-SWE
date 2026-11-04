/* tzdata.c - the IANA time zone files the operating system already ships.
 *
 * PostgreSQL resolves a zone name such as "Australia/Sydney" into the UTC
 * offset, abbreviation and daylight flag that applied at one instant.  It can
 * be built against its own copy of the zone database or against the one the
 * operating system installs; this server always reads the installed one, in
 * the compiled "TZif" layout of RFC 8536, and keeps no copy of its own.
 *
 * Only what the rest of the server needs is decoded: the transition list and
 * the local time types.  Leap seconds, the trailing POSIX rule string and the
 * standard/wall indicators are skipped.
 */
#include "engine.h"
#include <dirent.h>
#include <stdio.h>

#ifndef PGVC_TZDIR
#define PGVC_TZDIR "/usr/share/zoneinfo"
#endif

typedef struct TzType {
    int32 gmtoff;
    bool  isdst;
    char  abbr[16];
} TzType;

typedef struct TzZone {
    int64   *trans; unsigned char *idx; int ntrans;
    TzType  *types; int ntypes;
} TzZone;

static uint32 rd32(const unsigned char *p)
{
    return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
}

static int64 rd64(const unsigned char *p)
{
    uint64 v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return (int64)v;
}

/* One TZif header: the magic, the version byte and the six counts. */
static bool tz_hdr(const unsigned char *buf, size_t len, size_t *off,
                   uint32 cnt[6], char *ver)
{
    if (*off + 44 > len) return false;
    const unsigned char *p = buf + *off;
    if (memcmp(p, "TZif", 4)) return false;
    *ver = (char)p[4];
    for (int i = 0; i < 6; i++) cnt[i] = rd32(p + 20 + i * 4);
    *off += 44;
    return true;
}

static void tz_free(TzZone *z)
{
    free(z->trans); free(z->idx); free(z->types);
    memset(z, 0, sizeof *z);
}

static bool tz_parse(const unsigned char *buf, size_t len, TzZone *z)
{
    size_t off = 0;
    uint32 c[6];
    char ver = 0;
    int tsz = 4;

    memset(z, 0, sizeof *z);
    if (!tz_hdr(buf, len, &off, c, &ver)) return false;
    if (ver >= '2') {
        /* Skip the 32-bit block; the 64-bit one behind it is authoritative. */
        size_t skip = (size_t)c[3] * 4 + (size_t)c[3] + (size_t)c[4] * 6 +
                      (size_t)c[5] + (size_t)c[2] * 8 + (size_t)c[1] + (size_t)c[0];
        if (off + skip > len) return false;
        off += skip;
        if (!tz_hdr(buf, len, &off, c, &ver)) return false;
        tsz = 8;
    }

    int ntrans = (int)c[3], ntypes = (int)c[4], nchars = (int)c[5];
    if (ntypes <= 0 || ntrans < 0 || nchars < 0) return false;
    size_t need = (size_t)ntrans * (size_t)tsz + (size_t)ntrans +
                  (size_t)ntypes * 6 + (size_t)nchars;
    if (off + need > len) return false;

    z->ntrans = ntrans;
    z->ntypes = ntypes;
    z->trans = xmalloc(sizeof(int64) * (size_t)(ntrans ? ntrans : 1));
    for (int i = 0; i < ntrans; i++)
        z->trans[i] = tsz == 8 ? rd64(buf + off + (size_t)i * 8)
                               : (int64)(int32)rd32(buf + off + (size_t)i * 4);
    off += (size_t)ntrans * (size_t)tsz;

    z->idx = xmalloc((size_t)(ntrans ? ntrans : 1));
    if (ntrans) memcpy(z->idx, buf + off, (size_t)ntrans);
    off += (size_t)ntrans;

    const unsigned char *tt = buf + off;
    off += (size_t)ntypes * 6;
    const char *chars = (const char *)(buf + off);

    z->types = xmalloc(sizeof(TzType) * (size_t)ntypes);
    for (int i = 0; i < ntypes; i++) {
        z->types[i].gmtoff = (int32)rd32(tt + (size_t)i * 6);
        z->types[i].isdst = tt[(size_t)i * 6 + 4] != 0;
        unsigned ai = tt[(size_t)i * 6 + 5];
        z->types[i].abbr[0] = '\0';
        if (ai < (unsigned)nchars) {
            size_t k = 0;
            while (ai + k < (unsigned)nchars && chars[ai + k] &&
                   k + 1 < sizeof z->types[i].abbr) {
                z->types[i].abbr[k] = chars[ai + k];
                k++;
            }
            z->types[i].abbr[k] = '\0';
        }
    }
    return true;
}

/* A zone name is a path below the zone directory, so it must not escape it. */
static bool tz_name_ok(const char *zone)
{
    if (!zone || !*zone || *zone == '/' || strlen(zone) > 200) return false;
    for (const char *p = zone; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '/' || *p == '_' ||
              *p == '-' || *p == '+' || *p == '.'))
            return false;
    for (const char *p = zone; p; ) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && !memcmp(p, "..", 2)))
            return false;
        p = e ? e + 1 : NULL;
    }
    return true;
}

/* PostgreSQL matches zone names without regard to case, so when the exact
 * spelling is not on disk each path component is looked up again ignoring
 * case. */
static char *tz_resolve(const char *zone)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", PGVC_TZDIR, zone);
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return xstrdup(path); }

    Buf b; buf_init(&b);
    buf_adds(&b, PGVC_TZDIR);
    const char *p = zone;
    while (p) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char comp[128];
        if (n >= sizeof comp) { free(buf_steal(&b)); return NULL; }
        memcpy(comp, p, n); comp[n] = '\0';

        DIR *d = opendir(b.data ? b.data : PGVC_TZDIR);
        if (!d) { free(buf_steal(&b)); return NULL; }
        struct dirent *de;
        char *hit = NULL;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            if (!strcasecmp(de->d_name, comp)) { hit = xstrdup(de->d_name); break; }
        }
        closedir(d);
        if (!hit) { free(buf_steal(&b)); return NULL; }
        buf_addc(&b, '/');
        buf_adds(&b, hit);
        free(hit);
        p = e ? e + 1 : NULL;
    }
    char *res = buf_steal(&b);
    FILE *g = fopen(res, "rb");
    if (!g) { free(res); return NULL; }
    fclose(g);
    return res;
}

static bool tz_load(const char *zone, TzZone *z)
{
    memset(z, 0, sizeof *z);
    if (!tz_name_ok(zone)) return false;
    char *path = tz_resolve(zone);
    if (!path) return false;
    FILE *f = fopen(path, "rb");
    free(path);
    if (!f) return false;
    unsigned char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 8192;
            if (cap > ((size_t)4 << 20)) { free(buf); fclose(f); return false; }
            buf = xrealloc(buf, cap);
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) break;
    }
    fclose(f);
    bool ok = tz_parse(buf, len, z);
    free(buf);
    return ok;
}

static const TzType *tz_type_at(const TzZone *z, int64 t)
{
    if (z->ntrans == 0 || t < z->trans[0]) {
        for (int i = 0; i < z->ntypes; i++)
            if (!z->types[i].isdst) return &z->types[i];
        return &z->types[0];
    }
    int lo = 0, hi = z->ntrans - 1, k = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (z->trans[mid] <= t) { k = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    unsigned ti = z->idx[k];
    if (ti >= (unsigned)z->ntypes) ti = 0;
    return &z->types[ti];
}

/* Is this a zone name the server knows? */
bool tz_known(const char *zone)
{
    TzZone z;
    if (!tz_load(zone, &z)) return false;
    tz_free(&z);
    return true;
}

/* The offset in effect in `zone` at the instant `utc` (seconds since the Unix
 * epoch).  abbr and isdst may be NULL. */
bool tz_offset_at(const char *zone, int64 utc, int *gmtoff,
                  char *abbr, int abbrlen, bool *isdst)
{
    TzZone z;
    if (!tz_load(zone, &z)) return false;
    const TzType *t = tz_type_at(&z, utc);
    if (gmtoff) *gmtoff = (int)t->gmtoff;
    if (isdst) *isdst = t->isdst;
    if (abbr && abbrlen > 0) snprintf(abbr, (size_t)abbrlen, "%s", t->abbr);
    tz_free(&z);
    return true;
}

/* The instant at which `zone` reads `local` (seconds since the Unix epoch as
 * if the local wall clock were UTC).  Where a wall clock time occurs twice the
 * earlier instant wins, which is what PostgreSQL does as well. */
bool tz_local_to_utc(const char *zone, int64 local, int64 *utc)
{
    TzZone z;
    if (!tz_load(zone, &z)) return false;
    int64 guess = local - tz_type_at(&z, local)->gmtoff;
    for (int i = 0; i < 3; i++) {
        int64 next = local - tz_type_at(&z, guess)->gmtoff;
        if (next == guess) break;
        guess = next;
    }
    *utc = guess;
    tz_free(&z);
    return true;
}
