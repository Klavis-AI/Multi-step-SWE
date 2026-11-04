/* smart.c -- SMART on FHIR authorization.
 *
 * The server is open by default and enforces only what a token asks it to: a
 * request with no Authorization header is answered normally, and a request with
 * one is held to the scopes inside it.  That is what makes the scope grammar
 * testable without standing up an authorization server, and it is what
 * `scopes-and-launch-context.html` describes once the token is in hand.
 */
#include "smart.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>

#define JWKS_PATH "/app/testdata/smart/jwks.json"

static jval *load_jwks(arena *a) {
    const char *p = getenv("SMART_JWKS");
    if (!p) p = JWKS_PATH;
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    buf b;
    buf_init(&b);
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) buf_put(&b, chunk, n);
    fclose(f);
    char err[128];
    jval *d = j_parse(a, b.p ? b.p : "", b.n, err, sizeof err);
    buf_free(&b);
    return d;
}

/* The `oct` key whose `kid` this header names, decoded from base64url. */
static long key_for(arena *a, jval *jwks, const char *kid, uint8_t **out) {
    jval *keys = j_get(jwks, "keys");
    for (int i = 0; i < j_len(keys); i++) {
        jval *k = j_at(keys, i);
        if (kid && j_gets(k, "kid") && !str_eq(j_gets(k, "kid"), kid)) continue;
        const char *kty = j_gets(k, "kty");
        const char *val = j_gets(k, "k");
        if (!val || (kty && !str_eq(kty, "oct"))) continue;
        size_t l = strlen(val);
        uint8_t *raw = arena_alloc(a, l + 4);
        long n = b64url_decode(val, l, raw);
        if (n <= 0) continue;
        *out = raw;
        return n;
    }
    return -1;
}

void smart_verify(arena *a, const char *authz, const char *issuer, smart_token *out) {
    memset(out, 0, sizeof *out);
    vec_init(&out->scopes);
    if (!authz) return;
    while (*authz == ' ') authz++;
    if (strncasecmp(authz, "bearer", 6) != 0) return;
    out->present = 1;
    const char *tok = authz + 6;
    while (*tok == ' ') tok++;
    const char *d1 = strchr(tok, '.');
    const char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    if (!d1 || !d2) { out->reason = "the bearer token is not a JWS"; return; }

    uint8_t *hdr = arena_alloc(a, (size_t)(d1 - tok) + 4);
    long hn = b64url_decode(tok, (size_t)(d1 - tok), hdr);
    uint8_t *pay = arena_alloc(a, (size_t)(d2 - d1) + 4);
    long pn = b64url_decode(d1 + 1, (size_t)(d2 - d1 - 1), pay);
    if (hn <= 0 || pn <= 0) { out->reason = "the token is not base64url"; return; }
    char err[128];
    jval *h = j_parse(a, (const char *)hdr, (size_t)hn, err, sizeof err);
    jval *p = j_parse(a, (const char *)pay, (size_t)pn, err, sizeof err);
    if (!h || !p) { out->reason = "the token's header or payload is not JSON"; return; }
    const char *alg = j_gets(h, "alg");
    if (!alg || !str_eq(alg, "HS256")) { out->reason = "the token is not HS256"; return; }

    jval *jwks = load_jwks(a);
    uint8_t *key = NULL;
    long klen = jwks ? key_for(a, jwks, j_gets(h, "kid"), &key) : -1;
    if (klen <= 0) { out->reason = "no key with that kid"; return; }

    uint8_t mac[32];
    hmac_sha256(key, (size_t)klen, tok, (size_t)(d2 - tok), mac);
    char want[64];
    b64url_encode(mac, 32, want);
    if (!str_eq(want, d2 + 1)) { out->reason = "the signature does not verify"; return; }

    jval *exp = j_get(p, "exp");
    if (exp && j_num_of(exp) && (long long)time(NULL) > atoll(j_num_of(exp))) {
        out->reason = "the token has expired";
        return;
    }
    const char *iss = j_gets(p, "iss");
    if (iss && issuer && !str_eq(iss, issuer)) {
        out->reason = "the token was issued for another server";
        return;
    }
    const char *scope = j_gets(p, "scope");
    if (scope) {
        char *s = arena_strdup(a, scope);
        for (char *t = strtok(s, " \t"); t; t = strtok(NULL, " \t"))
            vec_push(&out->scopes, t);
    }
    out->patient = j_gets(p, "patient");
    out->ok = 1;
}

/* The SMART v2 scope grammar: a context (patient, user or system), a resource
   type or a wildcard, and the interaction letters c r u d s.  The v1 spellings
   are read as their v2 equivalents -- `.read` is `rs`, `.write` is `cud`. */
static int scope_grants(const char *scope, const char *type, char op,
                        const char **compartment, const char *patient) {
    const char *slash = strchr(scope, '/');
    if (!slash) return 0;
    size_t cl = (size_t)(slash - scope);
    int is_patient = (cl == 7 && strncmp(scope, "patient", 7) == 0);
    if (!is_patient && !(cl == 4 && strncmp(scope, "user", 4) == 0) &&
        !(cl == 6 && strncmp(scope, "system", 6) == 0))
        return 0;
    const char *rest = slash + 1;
    const char *dot = strrchr(rest, '.');
    if (!dot) return 0;
    size_t tl = (size_t)(dot - rest);
    if (!(tl == 1 && rest[0] == '*') &&
        !(strlen(type) == tl && strncmp(rest, type, tl) == 0))
        return 0;
    const char *acts = dot + 1;
    int ok = 0;
    if (str_eq(acts, "read")) ok = (op == 'r' || op == 's');
    else if (str_eq(acts, "write")) ok = (op == 'c' || op == 'u' || op == 'd');
    else if (str_eq(acts, "*")) ok = 1;
    else ok = strchr(acts, op) != NULL;
    if (ok && is_patient && compartment) *compartment = patient;
    return ok;
}

int smart_allows(const smart_token *t, const char *type, char op,
                 const char **compartment_patient) {
    if (compartment_patient) *compartment_patient = NULL;
    if (!t->present) return 1;          /* open by default */
    if (!t->ok) return 0;
    for (size_t i = 0; i < t->scopes.n; i++)
        if (scope_grants((const char *)t->scopes.v[i], type, op,
                         compartment_patient, t->patient))
            return 1;
    return 0;
}

jval *smart_configuration(arena *a, const char *base_url) {
    jval *d = j_obj(a);
    j_set(a, d, "issuer", j_str(a, base_url));
    j_set(a, d, "jwks_uri", j_str(a, arena_printf(a, "%s/.well-known/jwks.json", base_url)));
    j_set(a, d, "authorization_endpoint", j_str(a, arena_printf(a, "%s/auth/authorize", base_url)));
    j_set(a, d, "token_endpoint", j_str(a, arena_printf(a, "%s/auth/token", base_url)));
    jval *tem = j_arr(a);
    j_push(a, tem, j_str(a, "client_secret_basic"));
    j_push(a, tem, j_str(a, "private_key_jwt"));
    j_set(a, d, "token_endpoint_auth_methods_supported", tem);
    jval *ccm = j_arr(a);
    j_push(a, ccm, j_str(a, "S256"));
    j_set(a, d, "code_challenge_methods_supported", ccm);
    jval *gt = j_arr(a);
    j_push(a, gt, j_str(a, "authorization_code"));
    j_push(a, gt, j_str(a, "client_credentials"));
    j_set(a, d, "grant_types_supported", gt);
    jval *caps = j_arr(a);
    const char *c[] = {"launch-standalone", "client-public", "client-confidential-symmetric",
                       "context-standalone-patient", "permission-patient", "permission-user",
                       "permission-v1", "permission-v2", "sso-openid-connect", NULL};
    for (int i = 0; c[i]; i++) j_push(a, caps, j_str(a, c[i]));
    j_set(a, d, "capabilities", caps);
    jval *sc = j_arr(a);
    const char *s[] = {"patient/*.rs", "user/*.cruds", "system/*.rs", "openid", "fhirUser",
                       "launch/patient", "offline_access", NULL};
    for (int i = 0; s[i]; i++) j_push(a, sc, j_str(a, s[i]));
    j_set(a, d, "scopes_supported", sc);
    return d;
}
