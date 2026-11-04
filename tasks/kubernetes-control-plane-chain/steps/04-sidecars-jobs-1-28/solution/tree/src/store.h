#ifndef KCP_STORE_H
#define KCP_STORE_H
#include "json.h"
#include "etcd.h"
#include <pthread.h>

typedef struct sobj {
    char *key;              /* full etcd key */
    json *obj;              /* the stored object (storage version) */
    long long mod;          /* etcd mod revision */
    struct sobj *next;      /* hash chain */
} sobj;

typedef struct {
    char type[12];          /* ADDED | MODIFIED | DELETED */
    char *key;
    json *obj;
    long long rv;
} sevent;

void store_init(etcd_t *e);
void store_load(void);
long long store_rv(void);

sobj *store_get(const char *key);
/* Iterate every object whose key starts with `prefix`; cb returns 0 to stop. */
void  store_each(const char *prefix, int (*cb)(sobj *, void *), void *ud);

/* All of these take ownership of `obj` on success and return the new revision,
 * or -1 on conflict. */
long long store_create(const char *key, json *obj);
long long store_update(const char *key, json *obj, long long expect_mod);
long long store_delete(const char *key);

/* watch */
typedef struct watcher watcher;
watcher *watch_start(long long from_rv);
/* Returns the next event with rv > last delivered, or NULL after timeout_ms. */
sevent *watch_next(watcher *w, int timeout_ms);
void watch_stop(watcher *w);
int  watch_history_has(long long rv);

void store_lock(void);
void store_unlock(void);
#endif
