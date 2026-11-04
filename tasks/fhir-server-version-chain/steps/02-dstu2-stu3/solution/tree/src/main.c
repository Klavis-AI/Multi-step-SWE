#define _GNU_SOURCE
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *envd(const char *k, const char *dflt) {
    const char *v = getenv(k);
    return (v && *v) ? v : dflt;
}

static char *pkg_dir(const char *name, const char *ver) {
    char *buf = malloc(1024);
    const char *cache = envd("FHIR_PACKAGE_CACHE", "/root/.fhir/packages");
    snprintf(buf, 1024, "%s/%s#%s/package", cache, name, ver);
    return buf;
}

static char *sub(const char *dir, const char *leaf) {
    char *b = malloc(strlen(dir) + strlen(leaf) + 2);
    sprintf(b, "%s/%s", dir, leaf);
    return b;
}

/* The FHIR versions this server publishes, each at the version-named service
   base the HAPI FHIR public test server uses. */
static const struct {
    const char *pkg, *ver, *code, *base;
} VERSIONS[] = {
    { "hl7.fhir.r2.core", "1.0.2", "DSTU2", "baseDstu2" },
    { "hl7.fhir.r3.core", "3.0.2", "STU3",  "baseDstu3" },
    { "hl7.fhir.r4.core", "4.0.1", "R4",    "baseR4"    },
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    static server s;
    memset(&s, 0, sizeof s);
    const char *base_url = envd("BASE_URL", "http://localhost:8080");
    char *bu = str_dup(base_url);
    size_t bl = strlen(bu);
    while (bl && bu[bl - 1] == '/') bu[--bl] = 0;
    s.base_url = bu;
    s.port = atoi(envd("SERVER_PORT", "8080"));

    const char *idx = envd("INDEX_DB_DIR", "/app/data/index");
    const char *tx = envd("TRANSACTION_DB_DIR", "/app/data/transaction");
    const char *rs = envd("RESOURCE_DB_DIR", "/app/data/resource");

    size_t nv = sizeof VERSIONS / sizeof VERSIONS[0];
    static fhirbase *bases[16];
    for (size_t i = 0; i < nv; i++) {
        char *dir = pkg_dir(VERSIONS[i].pkg, VERSIONS[i].ver);
        fhirdefs *d = defs_load(dir, VERSIONS[i].ver, VERSIONS[i].code, VERSIONS[i].base);
        free(dir);
        if (!d) {
            fprintf(stderr, "cannot load the %s definitions\n", VERSIONS[i].code);
            return 70;
        }
        store *st = calloc(1, sizeof(store));
        char *a1 = sub(rs, VERSIONS[i].base), *a2 = sub(tx, VERSIONS[i].base), *a3 = sub(idx, VERSIONS[i].base);
        store_open(st, a1, a2, a3);
        free(a1); free(a2); free(a3);
        fhirbase *b = calloc(1, sizeof(fhirbase));
        b->base_path = VERSIONS[i].base;
        b->defs = d;
        b->st = st;
        bases[i] = b;
        printf("serving FHIR %s (%s) at %s/%s\n", VERSIONS[i].code, VERSIONS[i].ver,
               s.base_url, VERSIONS[i].base);
    }
    s.bases = bases;
    s.nbase = nv;

    printf("blaze listening on port %d\n", s.port);
    fflush(stdout);
    return http_serve("0.0.0.0", s.port, fhir_handle, &s);
}
