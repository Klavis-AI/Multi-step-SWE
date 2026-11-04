#ifndef KCP_ETCD_H
#define KCP_ETCD_H
#include "json.h"
typedef struct { char *host; int port; } etcd_t;
int  etcd_init(etcd_t *e, const char *servers);        /* "http://127.0.0.1:2379" */
/* All revisions are etcd revisions; the API server uses them as resourceVersion. */
long long etcd_put(etcd_t *e, const char *key, const char *val);
long long etcd_put_cas(etcd_t *e, const char *key, const char *val, long long mod_rev);
long long etcd_create(etcd_t *e, const char *key, const char *val);  /* -1 if exists */
long long etcd_delete(etcd_t *e, const char *key, long long mod_rev); /* -1 on mismatch */
/* range: returns an array of {"key":..,"value":..,"mod":<rev>} ; *rev = header revision */
json *etcd_range(etcd_t *e, const char *prefix, long long *rev);
long long etcd_revision(etcd_t *e);
#endif
