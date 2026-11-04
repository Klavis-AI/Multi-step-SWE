#define _GNU_SOURCE
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void mkpath(const char *p) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *c = tmp + 1; *c; c++) {
        if (*c == '/') { *c = 0; mkdir(tmp, 0755); *c = '/'; }
    }
    mkdir(tmp, 0755);
}

const char *store_now(char *b, size_t n) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    snprintf(b, n, "%04d-%02d-%02dT%02d:%02d:%02d.%03ld+00:00",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    return b;
}

void store_lock(store *s) { pthread_mutex_lock(&s->mu); }
void store_unlock(store *s) { pthread_mutex_unlock(&s->mu); }

static rec *rec_for(store *s, const char *type, const char *id, int create) {
    char key[512];
    snprintf(key, sizeof key, "%s/%s", type, id);
    rec *r = smap_get(&s->by_key, key);
    if (r || !create) return r;
    r = arena_calloc(&s->a, sizeof(rec));
    r->type = arena_strdup(&s->a, type);
    r->id = arena_strdup(&s->a, id);
    vec_init(&r->vers);
    smap_put(&s->by_key, arena_strdup(&s->a, key), r);
    vec_push(&s->all, r);
    return r;
}

rec *store_get(store *s, const char *type, const char *id) { return rec_for(s, type, id, 0); }

rver *store_version(rec *r, int vid) {
    if (!r) return NULL;
    for (size_t i = 0; i < r->vers.n; i++) {
        rver *v = r->vers.v[i];
        if (v->vid == vid) return v;
    }
    return NULL;
}

char *store_new_id(store *s) {
    /* RFC 4122 v4-shaped, seeded from a counter plus the clock so ids are unique
       across restarts without needing a random device. */
    static const char *hex = "0123456789abcdef";
    unsigned char b[16];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned long long v = (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
    unsigned long long c = (unsigned long long)(++s->next_id);
    for (int i = 0; i < 8; i++) b[i] = (unsigned char)(v >> (8 * i));
    for (int i = 0; i < 8; i++) b[8 + i] = (unsigned char)((c * 0x9E3779B97F4A7C15ull) >> (8 * i));
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    char *out = malloc(37);
    int o = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
        out[o++] = hex[b[i] >> 4];
        out[o++] = hex[b[i] & 15];
    }
    out[o] = 0;
    return out;
}

static void write_resource_file(store *s, const char *type, const char *id, jval *res, int deleted) {
    if (s->replaying) return;
    char dir[4096], path[4608];
    snprintf(dir, sizeof dir, "%s/%s", s->res_dir, type);
    mkpath(dir);
    snprintf(path, sizeof path, "%s/%s.json", dir, id);
    if (deleted) { unlink(path); return; }
    buf b; buf_init(&b);
    j_write(&b, res, 0);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(b.p, 1, b.n, f); fclose(f); }
    buf_free(&b);
}

static void tx_append(store *s, const char *op, const char *type, const char *id, rver *v) {
    if (s->replaying || !s->txlog) return;
    buf b; buf_init(&b);
    buf_printf(&b, "{\"op\":\"%s\",\"type\":\"%s\",\"id\":\"%s\",\"vid\":%d,\"lastUpdated\":\"%s\"",
               op, type, id, v->vid, v->last_updated);
    if (v->res) { buf_puts(&b, ",\"resource\":"); j_write(&b, v->res, 0); }
    buf_puts(&b, "}\n");
    fwrite(b.p, 1, b.n, s->txlog);
    fflush(s->txlog);
    buf_free(&b);
}

static void index_write(store *s) {
    if (s->replaying) return;
    char path[4096];
    snprintf(path, sizeof path, "%s/resources.idx", s->idx_dir);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (size_t i = 0; i < s->all.n; i++) {
        rec *r = s->all.v[i];
        fprintf(f, "%s\t%s\t%d\t%d\n", r->type, r->id, r->cur ? r->cur->vid : 0,
                r->cur ? r->cur->deleted : 1);
    }
    fclose(f);
}

static rver *push_version(store *s, rec *r, jval *res, int deleted, int vid, const char *lu) {
    rver *v = arena_calloc(&s->a, sizeof(rver));
    v->vid = vid ? vid : (r->cur ? r->cur->vid + 1 : 1);
    char nowbuf[64];
    v->last_updated = arena_strdup(&s->a, lu ? lu : store_now(nowbuf, sizeof nowbuf));
    v->deleted = deleted;
    v->res = res;
    v->prev = r->cur;
    vec_push(&r->vers, v);
    r->cur = v;
    return v;
}

rver *store_put(store *s, const char *type, const char *id, jval *res) {
    /* The caller's resource lives in a per-request arena that is released when
       the request ends, so the store keeps its own deep copy. */
    res = j_clone(&s->a, res);
    rec *r = rec_for(s, type, id, 1);
    rver *v = push_version(s, r, res, 0, 0, NULL);
    /* stamp meta */
    jval *meta = j_get(res, "meta");
    if (!meta) { meta = j_obj(&s->a); }
    j_set(&s->a, meta, "versionId", j_str(&s->a, arena_printf(&s->a, "%d", v->vid)));
    j_set(&s->a, meta, "lastUpdated", j_str(&s->a, v->last_updated));
    /* meta must be the second member, right after id, per the JSON representation */
    j_del(res, "meta");
    jval *reordered = j_obj(&s->a);
    j_set(&s->a, reordered, "resourceType", j_get(res, "resourceType"));
    if (j_get(res, "id")) j_set(&s->a, reordered, "id", j_get(res, "id"));
    j_set(&s->a, reordered, "meta", meta);
    for (jmem *m = res->u.obj.first; m; m = m->next) {
        if (str_eq(m->key, "resourceType") || str_eq(m->key, "id") || str_eq(m->key, "meta")) continue;
        j_set(&s->a, reordered, m->key, m->val);
    }
    v->res = reordered;
    tx_append(s, "put", type, id, v);
    write_resource_file(s, type, id, reordered, 0);
    index_write(s);
    return v;
}

rver *store_delete(store *s, const char *type, const char *id) {
    rec *r = rec_for(s, type, id, 0);
    if (!r) return NULL;
    rver *v = push_version(s, r, NULL, 1, 0, NULL);
    tx_append(s, "delete", type, id, v);
    write_resource_file(s, type, id, NULL, 1);
    index_write(s);
    return v;
}

void store_scan(store *s, const char *type, vec *out) {
    for (size_t i = 0; i < s->all.n; i++) {
        rec *r = s->all.v[i];
        if (type && !str_eq(r->type, type)) continue;
        vec_push(out, r);
    }
}

static void replay(store *s) {
    char path[4096];
    snprintf(path, sizeof path, "%s/transactions.ndjson", s->tx_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    s->replaying = 1;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, f)) > 0) {
        if (n <= 1) continue;
        char err[256];
        jval *e = j_parse(&s->a, line, (size_t)n, err, sizeof err);
        if (!e) continue;
        const char *op = j_gets(e, "op");
        const char *type = j_gets(e, "type");
        const char *id = j_gets(e, "id");
        const char *lu = j_gets(e, "lastUpdated");
        jval *vidv = j_get(e, "vid");
        int vid = vidv && vidv->kind == J_NUM ? atoi(j_num_of(vidv)) : 0;
        if (!op || !type || !id) continue;
        rec *r = rec_for(s, type, id, 1);
        if (str_eq(op, "delete")) push_version(s, r, NULL, 1, vid, lu);
        else push_version(s, r, j_get(e, "resource"), 0, vid, lu);
    }
    free(line);
    fclose(f);
    s->replaying = 0;
}

int store_open(store *s, const char *res_dir, const char *tx_dir, const char *idx_dir) {
    memset(s, 0, sizeof *s);
    arena_init(&s->a);
    pthread_mutex_init(&s->mu, NULL);
    smap_init(&s->by_key, 4096);
    vec_init(&s->all);
    s->res_dir = str_dup(res_dir);
    s->tx_dir = str_dup(tx_dir);
    s->idx_dir = str_dup(idx_dir);
    mkpath(res_dir); mkpath(tx_dir); mkpath(idx_dir);
    replay(s);
    char path[4096];
    snprintf(path, sizeof path, "%s/transactions.ndjson", s->tx_dir);
    s->txlog = fopen(path, "ab");
    index_write(s);
    return 0;
}

void store_flush(store *s) { if (s->txlog) fflush(s->txlog); }
