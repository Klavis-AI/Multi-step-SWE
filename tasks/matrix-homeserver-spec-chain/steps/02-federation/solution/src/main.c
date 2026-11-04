#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <yaml.h>

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxfed.h"
#include "mxhttp.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxutil.h"

/* ---------------------------------------------------------------- config */
/* Reads the subset of Dendrite's dendrite.yaml that the server needs.  Keys it
 * does not know are ignored, which is what lets a full Dendrite configuration
 * file be used unchanged. */
static json *yaml_to_json(yaml_parser_t *p);

static json *yaml_node(yaml_parser_t *p, yaml_event_t *ev) {
    switch (ev->type) {
    case YAML_SCALAR_EVENT: {
        const char *v = (const char *)ev->data.scalar.value;
        size_t len = ev->data.scalar.length;
        bool quoted = ev->data.scalar.style == YAML_SINGLE_QUOTED_SCALAR_STYLE ||
                      ev->data.scalar.style == YAML_DOUBLE_QUOTED_SCALAR_STYLE;
        if (!quoted) {
            if (!strcmp(v, "true")) return j_bool(true);
            if (!strcmp(v, "false")) return j_bool(false);
            if (!strcmp(v, "null") || !strcmp(v, "~") || len == 0) return j_null();
            char *end = NULL;
            long long n = strtoll(v, &end, 10);
            if (end && *end == 0 && len > 0) return j_int((int64_t)n);
        }
        return j_strn(v, len);
    }
    case YAML_SEQUENCE_START_EVENT: {
        json *arr = j_arr();
        for (;;) {
            yaml_event_t e;
            if (!yaml_parser_parse(p, &e)) return arr;
            if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); break; }
            json *v = yaml_node(p, &e);
            yaml_event_delete(&e);
            if (v) j_push(arr, v);
        }
        return arr;
    }
    case YAML_MAPPING_START_EVENT: {
        json *obj = j_obj();
        for (;;) {
            yaml_event_t k;
            if (!yaml_parser_parse(p, &k)) return obj;
            if (k.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&k); break; }
            char key[256];
            snprintf(key, sizeof key, "%s", (const char *)k.data.scalar.value);
            yaml_event_delete(&k);
            yaml_event_t v;
            if (!yaml_parser_parse(p, &v)) return obj;
            json *val = yaml_node(p, &v);
            yaml_event_delete(&v);
            if (val) j_set(obj, key, val);
        }
        return obj;
    }
    default:
        return NULL;
    }
}

static json *yaml_to_json(yaml_parser_t *p) {
    json *root = NULL;
    for (;;) {
        yaml_event_t ev;
        if (!yaml_parser_parse(p, &ev)) break;
        if (ev.type == YAML_STREAM_END_EVENT) { yaml_event_delete(&ev); break; }
        if (ev.type == YAML_MAPPING_START_EVENT ||
            ev.type == YAML_SEQUENCE_START_EVENT) {
            root = yaml_node(p, &ev);
            yaml_event_delete(&ev);
            break;
        }
        yaml_event_delete(&ev);
    }
    return root;
}

static json *load_yaml(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    yaml_parser_t p;
    yaml_parser_initialize(&p);
    yaml_parser_set_input_file(&p, f);
    json *root = yaml_to_json(&p);
    yaml_parser_delete(&p);
    fclose(f);
    return root;
}

/* Dendrite's matrix_key.pem: a PEM block of type MATRIX PRIVATE KEY carrying a
 * Key-ID header and the base64 of the 32-byte Ed25519 seed. */
static bool load_signing_key(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open private_key %s\n", path); return false; }
    char line[512];
    buf body = {0};
    bool in_body = false;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "BEGIN MATRIX PRIVATE KEY")) { in_body = true; continue; }
        if (strstr(line, "END MATRIX PRIVATE KEY")) break;
        if (!in_body) continue;
        if (strstr(line, "Key-ID:")) {
            char *v = strchr(line, ':');
            /* "Key-ID: ed25519:abcdef" — take everything after the first colon */
            if (v) {
                v++;
                while (*v == ' ') v++;
                char *nl = strpbrk(v, "\r\n");
                if (nl) *nl = 0;
                CFG.key_id = xstrdup_(v);
            }
            continue;
        }
        if (line[0] == '\r' || line[0] == '\n') continue;
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        buf_puts(&body, line);
    }
    fclose(f);
    if (!body.b) { buf_free(&body); return false; }
    size_t n = 0;
    unsigned char *raw = b64_decode(body.b, &n);
    buf_free(&body);
    if (!raw || n < 32) { free(raw); return false; }
    memcpy(CFG.key_seed, raw, 32);
    free(raw);
    if (!CFG.key_id) CFG.key_id = xstrdup_("ed25519:auto");
    return true;
}

static void mkdirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static volatile sig_atomic_t g_stop;
static http_server *g_server;
static void on_signal(int sig) { (void)sig; g_stop = 1; if (g_server) http_server_stop(g_server); }

int main(int argc, char **argv) {
    const char *config_path = "dendrite.yaml";
    const char *http_bind = ":8008";
    const char *https_bind = ":8448";
    const char *tls_cert = NULL, *tls_key = NULL;
    bool open_reg = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] == '-') a++;          /* --flag == -flag */
        const char *val = NULL;
        char name[64];
        const char *eq = strchr(a, '=');
        if (eq) {
            size_t len = (size_t)(eq - a - 1);
            if (len >= sizeof name) len = sizeof name - 1;
            memcpy(name, a + 1, len);
            name[len] = 0;
            val = eq + 1;
        } else {
            snprintf(name, sizeof name, "%s", a + 1);
            if (i + 1 < argc) val = argv[i + 1];
        }
        if (!strcmp(name, "config")) { config_path = val; if (!eq) i++; }
        else if (!strcmp(name, "http-bind-address")) { http_bind = val; if (!eq) i++; }
        else if (!strcmp(name, "https-bind-address")) { https_bind = val; if (!eq) i++; }
        else if (!strcmp(name, "tls-cert")) { tls_cert = val; if (!eq) i++; }
        else if (!strcmp(name, "tls-key")) { tls_key = val; if (!eq) i++; }
        else if (!strcmp(name, "really-enable-open-registration")) open_reg = true;
        else if (!strcmp(name, "version")) { printf("dendrite 0.13.8\n"); return 0; }
    }

    json *cfg = load_yaml(config_path);
    if (!cfg) { fprintf(stderr, "Invalid config file: %s\n", config_path); return 1; }
    json *g = j_geto(cfg, "global");
    if (!g) { fprintf(stderr, "Configuration error: missing global section\n"); return 1; }

    CFG.server_name = xstrdup_(j_gets(g, "server_name", "localhost"));
    CFG.private_key_path = xstrdup_(j_gets(g, "private_key", "matrix_key.pem"));
    json *dbsec = j_geto(g, "database");
    CFG.db_path = xstrdup_(dbsec ? j_gets(dbsec, "connection_string", "dendrite.db")
                                 : "dendrite.db");
    CFG.disable_federation = j_getb(g, "disable_federation", false);
    json *pres = j_geto(g, "presence");
    CFG.presence_in = pres ? j_getb(pres, "enable_inbound", false) : false;
    CFG.presence_out = pres ? j_getb(pres, "enable_outbound", false) : false;

    json *capi = j_geto(cfg, "client_api");
    CFG.registration_disabled = capi ? j_getb(capi, "registration_disabled", true) : true;
    CFG.guests_disabled = capi ? j_getb(capi, "guests_disabled", true) : true;
    CFG.registration_shared_secret =
        xstrdup_(capi ? j_gets(capi, "registration_shared_secret", "") : "");
    CFG.open_registration = open_reg ||
        (capi ? j_getb(capi, "open_registration_without_verification_enabled", false)
              : false);

    json *media = j_geto(cfg, "media_api");
    CFG.media_base_path = xstrdup_(media ? j_gets(media, "base_path", "./media_store")
                                         : "./media_store");
    CFG.max_upload_bytes = media ? j_geti(media, "max_file_size_bytes", 10485760)
                                 : 10485760;
    CFG.tls_cert = tls_cert ? xstrdup_(tls_cert) : NULL;
    CFG.tls_key = tls_key ? xstrdup_(tls_key) : NULL;
    CFG.key_validity_ms = 7LL * 24 * 3600 * 1000;

    if (!load_signing_key(CFG.private_key_path)) {
        fprintf(stderr, "Configuration error: cannot read private_key\n");
        return 1;
    }
    mkdirs(CFG.media_base_path);
    if (!db_open(CFG.db_path)) return 1;
    j_free(cfg);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    http_server *s = http_server_new(NULL);
    g_server = s;
    api_register_account(s);
    api_register_rooms(s);
    api_register_sync(s);
    api_register_media(s);
    api_register_misc(s);
    api_register_federation(s);

    char host[128] = "0.0.0.0";
    int port = 8008;
    const char *colon = strrchr(http_bind, ':');
    if (colon) {
        size_t hl = (size_t)(colon - http_bind);
        if (hl && hl < sizeof host) { memcpy(host, http_bind, hl); host[hl] = 0; }
        port = atoi(colon + 1);
    }
    if (!http_listen(s, host, port, NULL, NULL)) {
        fprintf(stderr, "cannot listen on %s\n", http_bind);
        return 1;
    }
    fprintf(stderr, "dendrite listening on %s:%d (server_name=%s)\n",
            host, port, CFG.server_name);

    if (tls_cert && tls_key && https_bind) {
        char shost[128] = "0.0.0.0";
        int sport = 8448;
        const char *c2 = strrchr(https_bind, ':');
        if (c2) {
            size_t hl = (size_t)(c2 - https_bind);
            if (hl && hl < sizeof shost) { memcpy(shost, https_bind, hl); shost[hl] = 0; }
            sport = atoi(c2 + 1);
        }
        if (!http_listen(s, shost, sport, tls_cert, tls_key))
            fprintf(stderr, "warning: cannot listen on %s\n", https_bind);
        else
            fprintf(stderr, "federation listening on %s:%d\n", shost, sport);
    }

    http_server_run(s);
    fed_shutdown();
    db_close();
    return 0;
}
