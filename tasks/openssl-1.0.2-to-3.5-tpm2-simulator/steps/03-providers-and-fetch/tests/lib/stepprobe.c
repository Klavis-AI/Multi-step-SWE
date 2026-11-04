/* stepprobe.c (step 03) - what the engine does at run time, counted.
 *
 * Verifier-owned: this is step 03's instrument and nobody else's.
 *
 * WHY THIS EXISTS
 *
 * Three of this step's six requirements are about *which object* a call was
 * made against and *how often* a call was made, and neither question survives
 * translation into a scan:
 *
 *   - "fetched from the engine's library context" is a claim about the first
 *     argument of EVP_MD_fetch.  A tree that creates a context, loads a
 *     provider into it, and then fetches with NULL has three true presence
 *     facts and has not done the step -- its digests come from the process's
 *     default context, provided by whatever loaded itself there.
 *   - "fetches each digest once and holds it" is a claim about a rate.  The
 *     AST gate asks the question backwards (EVP_MD_free must not be reached
 *     from the hash entry points) because asking it forwards would fail the
 *     lazy cache, which is a correct shape.  Backwards, it is passed by a tree
 *     that fetches per operation and leaks, with one EVP_MD_free(NULL) put
 *     somewhere to satisfy the "is called" half.
 *   - "a provider loaded into it" is the same argument-identity question as
 *     the fetch.
 *   - "every digest the engine hashes with" is an argument-identity question
 *     about the OTHER end of the fetch.  A tree that fetches three digests by
 *     name from its own context, holds them, gives them back at shutdown and
 *     then hashes with an EVP_MD it assembled itself has done the whole of the
 *     bookkeeping and none of the sentence; every count on the fetching side
 *     reads correct.  So the digest handed to each hashing call is checked
 *     against the pointers the fetches returned.
 *
 * So: link the candidate's engine, interpose the eleven OpenSSL entry points
 * those claims are about, and drive a fixed workload through the engine's own
 * public hash API.  Interposition rather than tracing because the executable's
 * dynamic symbols are searched before any shared object's: a definition here
 * captures libCryptoEngine.so's calls without the engine being rebuilt, and
 * dlsym(RTLD_NEXT) hands each one straight on to libcrypto, so the engine
 * computes exactly what it would have computed unobserved.
 *
 * THE RATE IS MEASURED AS A DIFFERENCE, NOT A TOTAL
 *
 * The workload runs twice: a short warm-up that touches every algorithm in the
 * engine's table through every hash entry point, then a long run that touches
 * them again.  A cache -- eager in startup or lazy in the lookup -- does all
 * of its fetching in the warm-up and none in the long run.  A fetch per
 * operation does it in proportion to the work.  The difference is therefore
 * the measurement, and it is immune to a fixed overhead: whatever libcrypto
 * itself fetches internally, it fetches in both halves.
 *
 *   usage: stepprobe <out.json>
 *
 * Exit status is 0 on a completed run and non-zero if the engine could not be
 * driven at all; the JSON is written either way when it can be.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>

#include "CryptoEngine.h"

/* ------------------------------------------------------------ the counters */

/* Phases.  Every counter is kept per phase so that "during the long run" is a
 * number and not an inference. */
enum { PH_PRE = 0, PH_INIT, PH_WARM, PH_LONG, PH_STOP, PH_N };
static int g_phase = PH_PRE;

#define MAXCTX 16

static void *g_ctx[MAXCTX];     /* contexts OSSL_LIB_CTX_new handed back */
static int   g_nctx;
static int   g_ctx_new[PH_N];
static int   g_ctx_free[PH_N];
static int   g_ctx_free_known;  /* frees whose argument the engine had created */
static int   g_ctx_live;        /* created minus freed, at the end */

/* Providers are tracked by pointer for the same reason contexts and digests
 * are: gate 3 asks whether the provider the engine loaded was given back, and
 * an unload that was written down is not an unload that ran.  A shutdown that
 * reads `if (s_prov) OSSL_PROVIDER_unload(s_prov)` with s_prov never set
 * satisfies every scan on the page and calls nothing. */
#define MAXPROV 16
static void *g_prov[MAXPROV];    /* providers a load of either spelling gave */
static int   g_nprov;
static int   g_prov_over;        /* more distinct providers than the table holds */
static int   g_prov_load[PH_N];
static int   g_prov_unload[PH_N];  /* unload with a non-NULL argument */
static int   g_prov_unload_null;
static int   g_prov_unload_known;  /* unloads of a provider this probe watched
                                    * come out of a load */
static int   g_prov_ctx_engine;  /* loads into a context the engine created */
static int   g_prov_ctx_null;    /* loads into the process default */
static int   g_prov_ctx_other;
static int   g_prov_try;         /* of the loads, how many were try_load */

static int   g_fetch[PH_N];
static int   g_fetch_ctx_engine;
static int   g_fetch_ctx_null;
static int   g_fetch_ctx_other;

/* Digests are tracked by pointer, like contexts, and for the same reason: what
 * gate 5 wants to know is how many of the references THIS engine took are
 * still out, and a count of frees on its own does not answer that.  A tree
 * that hands EVP_MD_free a statically defined EVP_MD -- a documented no-op at
 * 3.0, and what a port that swapped the fetch for the legacy lookup leaves
 * behind -- would otherwise drive fetched-minus-freed negative and read as a
 * leak running backwards. */
#define MAXMD 64
static void *g_md[MAXMD];        /* digests EVP_MD_fetch handed back */
static int   g_nmd;
static int   g_md_over;          /* more distinct digests than the table holds */
static int   g_mdfree[PH_N];     /* EVP_MD_free with a non-NULL argument */
static int   g_mdfree_null;
static int   g_mdfree_known;     /* frees of a digest this probe watched fetched */

/* And the same table read from the other side.  Gate 4's sentence is "every
 * digest the engine hashes with is obtained by fetching it by name from the
 * library context the engine made", and the fetch counters above cannot say
 * that: they say what the fetches were, not what the hashing used.  So every
 * EVP call that takes an EVP_MD * and starts hashing with it reports the
 * pointer it was handed, and it is looked up in g_md.  A call that passes NULL
 * re-runs the digest the context already holds and names nothing new, so it is
 * counted apart. */
static int   g_init[PH_N];       /* a hashing call that named a digest */
static int   g_init_known[PH_N]; /* of those, the digest came back from a fetch */
static int   g_init_null;        /* re-init against the context's own digest */
static int   g_qdigest;          /* one-shots that fetch and hash in one call */

static int is_engine_ctx(void *c)
{
    int i;
    if (c == NULL) return 0;
    for (i = 0; i < g_nctx; i++)
        if (g_ctx[i] == c) return 1;
    return 0;
}

static int is_loaded_prov(void *p)
{
    int i;
    if (p == NULL) return 0;
    if (g_prov_over) return 1;
    for (i = 0; i < g_nprov; i++)
        if (g_prov[i] == p) return 1;
    return 0;
}

static void note_prov(void *p)
{
    int i;
    if (p == NULL) return;
    for (i = 0; i < g_nprov; i++)
        if (g_prov[i] == p) return;
    if (g_nprov < MAXPROV) g_prov[g_nprov++] = p;
    else                   g_prov_over = 1;
}

static int is_fetched_md(void *m)
{
    int i;
    if (m == NULL) return 0;
    /* The table lost track.  Saying "yes" here is the honest answer for the
     * free side -- fetched-minus-freed must not run backwards because the
     * probe stopped watching -- but it is the wrong answer for the hashing
     * side, where it would make "hashed with a digest it fetched" true of
     * every digest, fetched or not.  So the flag is reported and gate 4 fails
     * closed on it rather than reading a tautology as a clean tree. */
    if (g_md_over) return 1;
    for (i = 0; i < g_nmd; i++)
        if (g_md[i] == m) return 1;
    return 0;
}

static void note_init(void *md)
{
    if (md == NULL) { g_init_null++; return; }
    g_init[g_phase]++;
    if (is_fetched_md(md)) g_init_known[g_phase]++;
}

static void note_md(void *m)
{
    int i;
    if (m == NULL) return;
    for (i = 0; i < g_nmd; i++)
        if (g_md[i] == m) return;
    if (g_nmd < MAXMD) g_md[g_nmd++] = m;
    else               g_md_over = 1;
}

/* --------------------------------------------------------- the interposers */

/* Declared here rather than included from OpenSSL's own headers: the probe needs the
 * ABI of twelve functions, not OpenSSL's type system, and a header that
 * changed across the ladder would make this file a step-04 problem.  Every
 * argument of every one of them is a pointer or an int except EVP_Digest's
 * length, which is spelled size_t here for the same reason: the ABI, not the
 * type system. */

typedef void *(*fn_pp)(void *);
typedef void  (*fn_vp)(void *);
typedef void *(*fn_void)(void);
typedef void *(*fn_fetch)(void *, const char *, const char *);
typedef void *(*fn_load)(void *, const char *);
typedef void *(*fn_tryload)(void *, const char *, int);
typedef int   (*fn_ip)(void *);
typedef int   (*fn_init3)(void *, void *, void *);
typedef int   (*fn_init2)(void *, void *);
typedef int   (*fn_oneshot)(const void *, size_t, void *, void *, void *,
                            void *);
typedef int   (*fn_qdigest)(void *, const char *, const char *, const void *,
                            size_t, void *, void *);

#define NEXT(var, type, name)                                   \
    static type var;                                            \
    if (var == NULL) {                                          \
        var = (type)dlsym(RTLD_NEXT, name);                     \
        if (var == NULL) {                                      \
            fprintf(stderr, "probe: no next %s\n", name);       \
            abort();                                            \
        }                                                       \
    }

void *OSSL_LIB_CTX_new(void)
{
    void *r;
    NEXT(next, fn_void, "OSSL_LIB_CTX_new")
    r = next();
    g_ctx_new[g_phase]++;
    if (r != NULL && g_nctx < MAXCTX) g_ctx[g_nctx++] = r;
    return r;
}

void OSSL_LIB_CTX_free(void *ctx)
{
    NEXT(next, fn_vp, "OSSL_LIB_CTX_free")
    g_ctx_free[g_phase]++;
    if (is_engine_ctx(ctx)) g_ctx_free_known++;
    next(ctx);
}

static void note_load_ctx(void *ctx)
{
    g_prov_load[g_phase]++;
    if (ctx == NULL)               g_prov_ctx_null++;
    else if (is_engine_ctx(ctx))   g_prov_ctx_engine++;
    else                           g_prov_ctx_other++;
}

void *OSSL_PROVIDER_load(void *ctx, const char *name)
{
    void *r;
    NEXT(next, fn_load, "OSSL_PROVIDER_load")
    note_load_ctx(ctx);
    r = next(ctx, name);
    note_prov(r);
    return r;
}

/* The same operation under its other published name.  3.0 spells it both
 * ways and implements the first as the second with retain = 0, so a port that
 * loads its provider with try_load has made this step's decision and must
 * count the same.  The two counters do not overlap: libcrypto's own call from
 * OSSL_PROVIDER_load into OSSL_PROVIDER_try_load stays inside libcrypto and
 * does not come back out through this interposer.  g_prov_try is reported so
 * the log can say which spelling a tree used; no gate reads it. */
void *OSSL_PROVIDER_try_load(void *ctx, const char *name, int retain)
{
    void *r;
    NEXT(next, fn_tryload, "OSSL_PROVIDER_try_load")
    note_load_ctx(ctx);
    g_prov_try++;
    r = next(ctx, name, retain);
    note_prov(r);
    return r;
}

int OSSL_PROVIDER_unload(void *prov)
{
    NEXT(next, fn_ip, "OSSL_PROVIDER_unload")
    if (prov == NULL) {
        g_prov_unload_null++;
    } else {
        g_prov_unload[g_phase]++;
        if (is_loaded_prov(prov)) g_prov_unload_known++;
    }
    return next(prov);
}

void *EVP_MD_fetch(void *ctx, const char *algorithm, const char *properties)
{
    void *r;
    NEXT(next, fn_fetch, "EVP_MD_fetch")
    g_fetch[g_phase]++;
    if (ctx == NULL)             g_fetch_ctx_null++;
    else if (is_engine_ctx(ctx)) g_fetch_ctx_engine++;
    else                         g_fetch_ctx_other++;
    r = next(ctx, algorithm, properties);
    note_md(r);
    return r;
}

/* The four ways libcrypto's EVP digest API is handed a digest to hash with.
 * EVP_DigestInit_ex is what the reference tree uses; _ex2 is the same call
 * with a parameter list instead of an ENGINE and is what a port written
 * against 3.0's own documentation is likely to use; EVP_DigestInit is the
 * two-argument spelling; EVP_Digest is the one-shot.  A port that hashes
 * through none of them is not hashing with an EVP_MD at all -- it is on the
 * low-level API, which is gate 6's sentence and is priced there. */

int EVP_DigestInit_ex(void *ctx, void *type, void *impl)
{
    NEXT(next, fn_init3, "EVP_DigestInit_ex")
    note_init(type);
    return next(ctx, type, impl);
}

int EVP_DigestInit_ex2(void *ctx, void *type, void *params)
{
    NEXT(next, fn_init3, "EVP_DigestInit_ex2")
    note_init(type);
    return next(ctx, type, params);
}

int EVP_DigestInit(void *ctx, void *type)
{
    NEXT(next, fn_init2, "EVP_DigestInit")
    note_init(type);
    return next(ctx, type);
}

int EVP_Digest(const void *data, size_t count, void *md, void *size,
               void *type, void *impl)
{
    NEXT(next, fn_oneshot, "EVP_Digest")
    note_init(type);
    return next(data, count, md, size, type, impl);
}

/* The fifth way, and the one that does not look like either half.  3.0's
 * EVP_Q_digest(libctx, "SHA256", propq, ...) fetches the digest by name from
 * the library context it is handed, hashes with it and frees it, all inside
 * libcrypto -- so EVP_MD_fetch is never called out here, no EVP_MD * ever
 * crosses this boundary, and a port that one-shots through it reads zero on
 * every counter above.  Against the process default it is exactly the thing
 * gate 4's sentence forbids, written as one call.
 *
 * It is counted as both halves of what it does: a fetch, routed by the context
 * like any other, and a hashing call that named a digest.  The hashing half is
 * "known" only when the context is the engine's -- which is the same rule
 * is_fetched_md applies to a pointer, asked of a name the caller never sees a
 * pointer for. */
int EVP_Q_digest(void *libctx, const char *name, const char *propq,
                 const void *data, size_t datalen, void *md, void *mdlen)
{
    NEXT(next, fn_qdigest, "EVP_Q_digest")
    g_qdigest++;
    g_fetch[g_phase]++;
    g_init[g_phase]++;
    if (libctx == NULL) {
        g_fetch_ctx_null++;
    } else if (is_engine_ctx(libctx)) {
        g_fetch_ctx_engine++;
        g_init_known[g_phase]++;
    } else {
        g_fetch_ctx_other++;
    }
    return next(libctx, name, propq, data, datalen, md, mdlen);
}

void EVP_MD_free(void *md)
{
    NEXT(next, fn_vp, "EVP_MD_free")
    if (md == NULL) {
        g_mdfree_null++;
    } else {
        g_mdfree[g_phase]++;
        if (is_fetched_md(md)) g_mdfree_known++;
    }
    next(md);
}

/* ------------------------------------------------------------- the workload */

static sigjmp_buf   g_jmp;
static volatile int g_armed;
static volatile int g_tpmfail;
static volatile int g_crash;

static void on_tpm_fail(const char *function, int line, int code)
{
    (void)function; (void)line;
    g_tpmfail = code ? code : 1;
    if (g_armed) siglongjmp(g_jmp, 1);
    abort();
}

static void on_signal(int sig)
{
    g_crash = sig;
    if (g_armed) siglongjmp(g_jmp, 2);
    _exit(3);
}

/* The algorithms the reference tree's table can serve.  An engine that does
 * not serve one of these answers 0 from _cpri__StartHash and the round is not
 * counted -- the probe measures what the engine does do, and a missing
 * algorithm is the behaviour corpus's finding, not this one's. */
static const TPM_ALG_ID g_algs[] = {
#ifdef TPM_ALG_SHA1
    TPM_ALG_SHA1,
#endif
#ifdef TPM_ALG_SHA256
    TPM_ALG_SHA256,
#endif
#ifdef TPM_ALG_SHA384
    TPM_ALG_SHA384,
#endif
#ifdef TPM_ALG_SHA512
    TPM_ALG_SHA512,
#endif
};
#define NALG ((int)(sizeof(g_algs) / sizeof(g_algs[0])))

/* Operations the engine actually performed, per phase.  Per phase because the
 * rate this probe exists to measure is a ratio of two numbers from the same
 * half of the run: a fetch count over an operation count nobody checked
 * measures nothing. */
static int g_ops[PH_N];

/* The engine's TPM2B convention, spelled locally for the same reason cpridrv
 * spells it locally: a TPM2B is a size and a flexible byte array, and a caller
 * has to supply the storage behind it. */
typedef union {
    struct { UINT16 size; BYTE buffer[256]; } t;
    TPM2B b;
} PB2B;

/* One round: every hash entry point the instruction's "no fetch per
 * operation" sentence is about, for one algorithm.  Kept deliberately close to
 * what the corpus already drives, so that a tree which survives the corpus
 * survives this. */
static void round_for(TPM_ALG_ID alg, int salt)
{
    CPRI_HASH_STATE st, cp;
    PB2B            opad;
    BYTE            in[64], dig[MAX_DIGEST_SIZE * 2], mask[128];
    BYTE            key[32];
    UINT16          started;
    int             i;

    for (i = 0; i < (int)sizeof(in); i++)  in[i]  = (BYTE)(i + salt);
    for (i = 0; i < (int)sizeof(key); i++) key[i] = (BYTE)(i * 3 + salt);

    /* a plain hash: start, feed, finish */
    memset(&st, 0, sizeof(st));
    started = _cpri__StartHash(alg, FALSE, &st);
    if (started == 0) return;
    _cpri__UpdateHash(&st, (UINT32)sizeof(in), in);
    _cpri__UpdateHash(&st, 16, in);
    _cpri__CompleteHash(&st, started, dig);
    g_ops[g_phase]++;

    /* a sequence, relocated by a bitwise copy of its container and then run on
     * in both places -- the shape the corpus's hashmove family is about,
     * repeated here because a port that fetches per operation tends to fetch
     * in the update path too, and because a sequence is the only hash state
     * this engine's callers relocate.  A non-sequence state is a local of the
     * layer that started it and is not copied: the instruction's "a hash
     * sequence state is a value" is about the saved kind, and so is this. */
    memset(&st, 0, sizeof(st));
    if (_cpri__StartHash(alg, TRUE, &st) != 0) {
        _cpri__UpdateHash(&st, (UINT32)sizeof(in), in);
        memcpy(&cp, &st, sizeof(cp));
        _cpri__UpdateHash(&st, 16, in);
        _cpri__UpdateHash(&cp, 8, in);
        _cpri__CompleteHash(&st, started, dig);
        _cpri__CompleteHash(&cp, started, dig);
        g_ops[g_phase] += 2;
    }

    /* one shot */
    _cpri__HashBlock(alg, (UINT32)sizeof(in), in, started, dig);
    g_ops[g_phase]++;

    /* mask generation, which loops the hash internally */
    _cpri__MGF1((UINT32)sizeof(mask), mask, alg, (UINT32)sizeof(in), in);
    g_ops[g_phase]++;

    /* HMAC, which takes two hash states of its own */
    memset(&st, 0, sizeof(st));
    memset(&opad, 0, sizeof(opad));
    opad.t.size = 0;
    if (_cpri__StartHMAC(alg, FALSE, &st, (UINT16)sizeof(key), key,
                         &opad.b) != 0) {
        _cpri__UpdateHash(&st, (UINT32)sizeof(in), in);
        _cpri__CompleteHMAC(&st, &opad.b, started, dig);
        g_ops[g_phase]++;
    }

    /* The two key derivations.  They are hash work done on a caller's behalf
     * exactly as the five above are -- KDFa runs an HMAC per output block and
     * KDFe a plain hash per output block, both in a loop the caller does not
     * see -- and they are on the AST gate's list of entry points for that
     * reason.  Driving them here is what makes the rate half ask the same
     * question of the same places: a fetch inside a derivation loop is a
     * fetch per operation, and without these calls the long half would never
     * enter one. */
    {
        PB2B    kb, cu, cv;
        BYTE    stream[64];

        memset(&kb, 0, sizeof(kb));
        memset(&cu, 0, sizeof(cu));
        memset(&cv, 0, sizeof(cv));
        kb.t.size = (UINT16)sizeof(key);
        memcpy(kb.t.buffer, key, sizeof(key));
        cu.t.size = 8;
        memcpy(cu.t.buffer, in, 8);
        cv.t.size = 8;
        memcpy(cv.t.buffer, &in[8], 8);

        if (_cpri__KDFa(alg, &kb.b, "probe", &cu.b, &cv.b, 256, stream,
                        NULL, FALSE) != 0)
            g_ops[g_phase]++;
        if (_cpri__KDFe(alg, &kb.b, "probe", &cu.b, &cv.b, 256, stream) != 0)
            g_ops[g_phase]++;
    }
}

static void workload(int rounds)
{
    int r, a;
    for (r = 0; r < rounds; r++)
        for (a = 0; a < NALG; a++)
            round_for(g_algs[a], r + a);
}

/* ------------------------------------------------------------------- output */

static int sum(const int *v)
{
    int i, t = 0;
    for (i = 0; i < PH_N; i++) t += v[i];
    return t;
}

#define WARM_ROUNDS 2
#define LONG_ROUNDS 64

int main(int argc, char **argv)
{
    FILE *out;
    int   jumped, ok = 1;

    if (argc < 2) { fprintf(stderr, "usage: %s <out.json>\n", argv[0]); return 2; }

    signal(SIGSEGV, on_signal);
    signal(SIGBUS,  on_signal);
    signal(SIGFPE,  on_signal);
    signal(SIGABRT, on_signal);
    signal(SIGILL,  on_signal);

    jumped = sigsetjmp(g_jmp, 1);
    g_armed = 1;
    if (jumped == 0) {
        g_phase = PH_INIT;  _cpri__InitCryptoUnits(&on_tpm_fail);
        g_phase = PH_WARM;  workload(WARM_ROUNDS);
        g_phase = PH_LONG;  workload(LONG_ROUNDS);
        g_phase = PH_STOP;  _cpri__StopCryptoUnits();
    } else {
        ok = 0;
    }
    g_armed = 0;

    g_ctx_live = sum(g_ctx_new) - sum(g_ctx_free);

    out = fopen(argv[1], "w");
    if (out == NULL) { perror(argv[1]); return 2; }
    fprintf(out,
        "{\n"
        "  \"ok\": %d,\n"
        "  \"tpmfail\": %d,\n"
        "  \"signal\": %d,\n"
        "  \"ops\": %d,\n"
        "  \"ops_long\": %d,\n"
        "  \"warm_rounds\": %d,\n"
        "  \"long_rounds\": %d,\n"
        "  \"ctx_new\": %d,\n"
        "  \"ctx_new_at_init\": %d,\n"
        "  \"ctx_free\": %d,\n"
        "  \"ctx_free_at_stop\": %d,\n"
        "  \"ctx_free_known\": %d,\n"
        "  \"ctx_live\": %d,\n"
        "  \"prov_load\": %d,\n"
        "  \"prov_load_at_init\": %d,\n"
        "  \"prov_unload\": %d,\n"
        "  \"prov_unload_at_stop\": %d,\n"
        "  \"prov_unload_null\": %d,\n"
        "  \"prov_unload_known\": %d,\n"
        "  \"prov_ctx_engine\": %d,\n"
        "  \"prov_ctx_null\": %d,\n"
        "  \"prov_ctx_other\": %d,\n"
        "  \"prov_try_load\": %d,\n"
        "  \"fetch\": %d,\n"
        "  \"fetch_at_init\": %d,\n"
        "  \"fetch_warm\": %d,\n"
        "  \"fetch_long\": %d,\n"
        "  \"fetch_ctx_engine\": %d,\n"
        "  \"fetch_ctx_null\": %d,\n"
        "  \"fetch_ctx_other\": %d,\n"
        "  \"md_free\": %d,\n"
        "  \"md_free_at_stop\": %d,\n"
        "  \"md_free_null\": %d,\n"
        "  \"md_free_known\": %d,\n"
        "  \"md_live\": %d,\n"
        "  \"init\": %d,\n"
        "  \"init_known\": %d,\n"
        "  \"init_null\": %d,\n"
        "  \"qdigest\": %d,\n"
        "  \"md_distinct\": %d,\n"
        "  \"md_over\": %d,\n"
        "  \"prov_distinct\": %d,\n"
        "  \"prov_over\": %d\n"
        "}\n",
        ok, g_tpmfail, g_crash, sum(g_ops), g_ops[PH_LONG],
        WARM_ROUNDS, LONG_ROUNDS,
        sum(g_ctx_new), g_ctx_new[PH_INIT],
        sum(g_ctx_free), g_ctx_free[PH_STOP], g_ctx_free_known, g_ctx_live,
        sum(g_prov_load), g_prov_load[PH_INIT], sum(g_prov_unload),
        g_prov_unload[PH_STOP], g_prov_unload_null, g_prov_unload_known,
        g_prov_ctx_engine, g_prov_ctx_null, g_prov_ctx_other, g_prov_try,
        sum(g_fetch), g_fetch[PH_INIT], g_fetch[PH_WARM], g_fetch[PH_LONG],
        g_fetch_ctx_engine, g_fetch_ctx_null, g_fetch_ctx_other,
        sum(g_mdfree), g_mdfree[PH_STOP], g_mdfree_null, g_mdfree_known,
        sum(g_fetch) - g_mdfree_known,
        sum(g_init), sum(g_init_known), g_init_null,
        g_qdigest, g_nmd, g_md_over, g_nprov, g_prov_over);
    fclose(out);
    return ok ? 0 : 1;
}
