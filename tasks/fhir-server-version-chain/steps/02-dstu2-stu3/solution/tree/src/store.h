/* store.h -- versioned resource store with history and on-disk persistence. */
#ifndef FHIR_STORE_H
#define FHIR_STORE_H

#include "json.h"
#include <pthread.h>
#include <stdio.h>

typedef struct rver {
    int          vid;
    char        *last_updated;     /* instant with timezone */
    jval        *res;              /* NULL when deleted     */
    int          deleted;
    struct rver *prev;
} rver;

typedef struct {
    const char *type;
    const char *id;
    rver       *cur;
    vec         vers;              /* rver*, oldest first */
} rec;

typedef struct {
    arena           a;
    pthread_mutex_t mu;
    smap            by_key;        /* "Patient/id" -> rec* */
    vec             all;           /* rec* */
    char           *res_dir, *tx_dir, *idx_dir;
    FILE           *txlog;
    long long       next_id;
    int             replaying;
} store;

int   store_open(store *s, const char *res_dir, const char *tx_dir, const char *idx_dir);
rec  *store_get(store *s, const char *type, const char *id);
rver *store_version(rec *r, int vid);
/* Writes a new version.  `res` must already carry resourceType/id.  Returns the
   stored version (with meta filled in).  Caller holds the store lock. */
rver *store_put(store *s, const char *type, const char *id, jval *res);
rver *store_delete(store *s, const char *type, const char *id);
char *store_new_id(store *s);
void  store_lock(store *s);
void  store_unlock(store *s);
void  store_flush(store *s);
/* All records of one type (or every record when type is NULL), newest first. */
void  store_scan(store *s, const char *type, vec *out);
const char *store_now(char *buf, size_t n);

#endif
