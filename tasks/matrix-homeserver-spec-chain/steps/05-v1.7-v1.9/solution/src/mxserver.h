/* Process-wide server configuration and shared state. */
#ifndef MXSERVER_H
#define MXSERVER_H

#include <stdbool.h>
#include <stdint.h>
#include "mxjson.h"

typedef struct {
    char *server_name;
    char *private_key_path;
    unsigned char key_seed[32];
    char *key_id;
    char *db_path;
    char *media_base_path;
    int64_t max_upload_bytes;
    bool registration_disabled;
    bool open_registration;
    bool guests_disabled;
    bool disable_federation;
    bool presence_in;
    bool presence_out;
    char *registration_shared_secret;
    char *tls_cert;
    char *tls_key;
    int64_t key_validity_ms;
} hs_config;

extern hs_config CFG;

/* One monotonic counter orders every stream a /sync token has to cover. */
int64_t stream_next(void);
int64_t stream_current(void);
void stream_notify(void);
/* Blocks until stream_current() > after, or `timeout_ms` elapses. */
void stream_wait(int64_t after, int timeout_ms);

/* Serialises every write; readers do not take it. */
void hs_lock(void);
void hs_unlock(void);

#endif
