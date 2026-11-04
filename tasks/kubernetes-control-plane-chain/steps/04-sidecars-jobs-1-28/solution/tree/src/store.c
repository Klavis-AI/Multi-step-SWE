#define _GNU_SOURCE
#include "store.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define NBUCK 4096
#define RING  8192

static etcd_t *E;
static sobj *buckets[NBUCK];
static long long g_rv;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;

static sevent ring[RING];
static long long ring_head;      /* number of events ever appended */
static long long ring_first_rv;  /* rv of the oldest retained event */

struct watcher { long long last; };

void store_lock(void){ pthread_mutex_lock(&mu); }
void store_unlock(void){ pthread_mutex_unlock(&mu); }

static unsigned hashs(const char *s){
    unsigned h = 2166136261u;
    for(; *s; s++){ h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

void store_init(etcd_t *e){ E = e; }
long long store_rv(void){ return g_rv; }

static void idx_put(const char *key, json *obj, long long mod){
    unsigned b = hashs(key) % NBUCK;
    for(sobj *o = buckets[b]; o; o = o->next)
        if(strcmp(o->key, key) == 0){ j_free(o->obj); o->obj = obj; o->mod = mod; return; }
    sobj *o = calloc(1, sizeof(sobj));
    o->key = xstrdup2(key); o->obj = obj; o->mod = mod;
    o->next = buckets[b]; buckets[b] = o;
}
static void idx_del(const char *key){
    unsigned b = hashs(key) % NBUCK;
    sobj **pp = &buckets[b];
    while(*pp){
        if(strcmp((*pp)->key, key) == 0){
            sobj *d = *pp; *pp = d->next;
            free(d->key); j_free(d->obj); free(d);
            return;
        }
        pp = &(*pp)->next;
    }
}

sobj *store_get(const char *key){
    unsigned b = hashs(key) % NBUCK;
    for(sobj *o = buckets[b]; o; o = o->next) if(strcmp(o->key, key) == 0) return o;
    return NULL;
}

void store_each(const char *prefix, int (*cb)(sobj *, void *), void *ud){
    size_t n = strlen(prefix);
    for(int b = 0; b < NBUCK; b++)
        for(sobj *o = buckets[b]; o; o = o->next)
            if(strncmp(o->key, prefix, n) == 0)
                if(!cb(o, ud)) return;
}

static void emit(const char *type, const char *key, json *obj, long long rv){
    sevent *e = &ring[ring_head % RING];
    if(ring_head >= RING){ free(e->key); j_free(e->obj); }
    snprintf(e->type, sizeof e->type, "%s", type);
    e->key = xstrdup2(key);
    e->obj = j_clone(obj);
    e->rv = rv;
    ring_head++;
    if(ring_head > RING) ring_first_rv = ring[(ring_head - RING) % RING].rv;
    g_rv = rv;
    pthread_cond_broadcast(&cv);
}

void store_load(void){
    long long rev = 0;
    json *kvs = etcd_range(E, "/registry/", &rev);
    if(kvs){
        for(size_t i = 0; i < kvs->nitems; i++){
            json *kv = kvs->items[i];
            const char *k = j_gets(kv, "key");
            const char *v = j_gets(kv, "value");
            if(!k || !v) continue;
            json *o = j_parse0(v);
            if(o) idx_put(k, o, j_geti(kv, "mod", 0));
        }
        j_free(kvs);
    }
    if(rev > 0) g_rv = rev;
    ring_first_rv = g_rv;
}

long long store_create(const char *key, json *obj){
    char *text = j_dump(obj);
    long long rv = etcd_create(E, key, text);
    free(text);
    if(rv < 0){ j_free(obj); return -1; }
    idx_put(key, obj, rv);
    emit("ADDED", key, obj, rv);
    return rv;
}

long long store_update(const char *key, json *obj, long long expect_mod){
    char *text = j_dump(obj);
    long long rv = expect_mod > 0 ? etcd_put_cas(E, key, text, expect_mod)
                                  : etcd_put(E, key, text);
    free(text);
    if(rv < 0){ j_free(obj); return -1; }
    idx_put(key, obj, rv);
    emit("MODIFIED", key, obj, rv);
    return rv;
}

long long store_delete(const char *key){
    sobj *o = store_get(key);
    if(!o) return -1;
    json *copy = j_clone(o->obj);
    long long rv = etcd_delete(E, key, 0);
    if(rv < 0){ j_free(copy); return -1; }
    idx_del(key);
    emit("DELETED", key, copy, rv);
    j_free(copy);
    return rv;
}

watcher *watch_start(long long from){
    watcher *w = calloc(1, sizeof(watcher));
    w->last = from;
    return w;
}

int watch_history_has(long long rv){
    if(ring_head == 0) return rv <= g_rv;
    return rv >= ring_first_rv;
}

sevent *watch_next(watcher *w, int timeout_ms){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if(ts.tv_nsec >= 1000000000L){ ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    for(;;){
        long long start = ring_head > RING ? ring_head - RING : 0;
        for(long long i = start; i < ring_head; i++){
            sevent *e = &ring[i % RING];
            if(e->rv > w->last){ w->last = e->rv; return e; }
        }
        int rc = pthread_cond_timedwait(&cv, &mu, &ts);
        if(rc == ETIMEDOUT) return NULL;
    }
}

void watch_stop(watcher *w){ free(w); }
