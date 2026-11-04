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

    static store st;
    store_open(&st, rs, tx, idx);

    char *dir = pkg_dir("hl7.fhir.r4.core", "4.0.1");
    fhirdefs *d4 = defs_load(dir, "4.0.1", "R4", "baseR4");
    free(dir);
    if (!d4) { fprintf(stderr, "cannot load the R4 definitions\n"); return 70; }

    static fhirbase b_r4;
    b_r4.base_path = "baseR4";
    b_r4.defs = d4;
    b_r4.st = &st;

    static fhirbase *bases[4];
    bases[0] = &b_r4;
    s.bases = bases;
    s.nbase = 1;

    printf("serving FHIR R4 (4.0.1) at %s/baseR4, port %d\n", s.base_url, s.port);
    fflush(stdout);
    return http_serve("0.0.0.0", s.port, fhir_handle, &s);
}
