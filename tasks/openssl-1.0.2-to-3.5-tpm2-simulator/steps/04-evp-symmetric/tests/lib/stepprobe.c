/* stepprobe.c (step 04) - what the engine does at run time, counted.
 *
 * Verifier-owned: this is step 04's instrument and nobody else's.
 *
 * THIS IS STEP 03'S PROBE WITH A SYMMETRIC HALF ADDED
 *
 * Six of step 03's seven gates read run-time counters, and step 04 re-asks all
 * seven.  A step that re-asked them from the scans alone would be re-asking a
 * weaker question than the step that introduced them -- the argument-identity
 * half of "fetched from the engine's own context" would simply stop being
 * checked the moment step 03 was behind us.  So step 03's instrumentation is kept verbatim, its counters keep
 * their names, and the hash workload still runs; what is new is a symmetric
 * round beside it and the counters that round needs.  The ladder's probes grow
 * by addition, and an inherited gate reads exactly the number its author wrote
 * it against.
 *
 * WHAT IS NEW AT STEP 04
 *
 * The same two questions step 03 could not put to a scan, asked of ciphers:
 *
 *   - "fetched from the engine's library context" is a claim about the first
 *     argument of EVP_CIPHER_fetch.  A tree that owns a context (step 03 made
 *     it) and then fetches with NULL has every presence fact true and gets its
 *     algorithms from the process default, which is the thing step 03 already
 *     refused for digests.
 *   - "every symmetric operation gets its cipher from that fetch" is the
 *     OTHER end: the EVP_CIPHER * handed to any of the nine calls that start
 *     a cipher context -- _ex, _ex2 and the pre-_ex spelling of Encrypt,
 *     Decrypt and Cipher -- is checked against the pointers the fetches
 *     returned, because a tree can fetch its ciphers correctly, hold them,
 *     free them at shutdown, and encrypt with something else entirely.
 *   - "fetched once, not per operation" is the same rate, measured the same
 *     way: the difference between a short warm-up and a long run.
 *
 * and one question step 03 did not have at all:
 *
 *   - "the low-level API is gone" has a run-time half that no scan has.  The
 *     scan reads the sources the build compiles; it cannot see a call that
 *     arrives through a function pointer, a table, or a translation unit the
 *     scan mis-attributes.  AES_set_encrypt_key, AES_encrypt and their two
 *     decrypt spellings are interposed here and counted, so "no low-level
 *     symmetric call happened" is a measurement and not only a reading.
 *
 * THERE IS NO SM4 SYMBOL TO INTERPOSE
 *
 * Half of CpriSym.c spells SM4 and none of it runs.  CpriSym.c:61-65 is
 * `typedef AES_KEY SM4_KEY;` and four #defines mapping every SM4_ spelling
 * onto the AES_ one, so even where that half were compiled it would be AES
 * underneath; and it is not compiled, because Implementation.h:85 is
 * `#define ALG_SM4 ALG_NO`, TPM_ALG_SM4 is therefore never defined
 * (Implementation.h:364) and CpriSym.c:614-1113 never reaches the compiler.
 * At run time SM4_set_encrypt_key is not a symbol, it is a macro nothing
 * expands.  So the probe interposes the four AES_ names and no SM4_ name --
 * not as a simplification, but because the other four do not exist to
 * interpose.  The dead half is the AST gate's business, in the text, where it
 * is visible.
 *
 * ONE ENTRY POINT CANNOT BE DRIVEN FROM HERE
 *
 * Of the eight compiled symmetric entry points, seven have prototypes in
 * CpriSym_fp.h and two more names reach them through the DecryptCTR and
 * DecryptOFB #define aliases there, so nine names resolving to seven functions
 * are reachable by a consumer bound to CryptoEngine.h.  _cpri__AESDecryptCBC
 * is the eighth: compiled, called from CryptUtil.c:2009, and declared in no
 * installed header, so a consumer bound to CryptoEngine.h cannot reach it
 * and neither can this probe.
 * cpridrv.c:723-733 says the same thing from the other side: the driver is a
 * consumer bound to those same headers, so it cannot name that entry point
 * either -- "Undrivable from out of tree, not absent" -- and the e2e roundtrip
 * cases cover it through the TPM layer, as does the AST gate statically.  So
 * the probe drives nine names and says nine; it does not drive nine and
 * describe a surface of sixteen.
 *
 * WHAT FOLLOWS IS STEP 03'S FILE
 *
 * Three of step 03's six requirements are about *which object* a call was
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

/* The fetched-object table.  One question asked of every EVP_CIPHER the
 * engine gets hold of: did this pointer come out of a fetch against the
 * engine's own library context, and was it given back.  It is a struct rather
 * than a handful of loose counters because later steps fetch objects of their
 * own and ask the same three-part question -- from which context, used how
 * often, freed by whom -- and a struct is what they can copy. */
#define MAXOBJ 64

typedef struct {
    void *p[MAXOBJ];
    int   n;
    int   over;            /* more distinct objects than the table holds */
    int   fetch[PH_N];
    int   ctx_engine;
    int   ctx_null;
    int   ctx_other;
    int   freed[PH_N];     /* free with a non-NULL argument */
    int   freed_null;
    int   freed_known;     /* frees of something this probe watched fetched */
    int   used[PH_N];      /* an operation that named one of these objects */
    int   used_known[PH_N];/* of those, the object came back from a fetch */
} objtab;

static objtab g_ciph;

/* Saying "yes" when the table has overflowed is the honest answer on the free
 * side -- fetched-minus-freed must not run backwards because the probe stopped
 * watching -- and the wrong answer on the use side, where it would make "used
 * what it fetched" true of everything.  So the flag is reported and the gates
 * fail closed on it rather than reading a tautology as a clean tree. */
static int obj_known(objtab *t, void *p)
{
    int i;
    if (p == NULL) return 0;
    if (t->over) return 1;
    for (i = 0; i < t->n; i++)
        if (t->p[i] == p) return 1;
    return 0;
}

static void obj_note(objtab *t, void *p)
{
    int i;
    if (p == NULL) return;
    for (i = 0; i < t->n; i++)
        if (t->p[i] == p) return;
    if (t->n < MAXOBJ) t->p[t->n++] = p;
    else               t->over = 1;
}

static void obj_fetch(objtab *t, void *ctx)
{
    t->fetch[g_phase]++;
    if (ctx == NULL)             t->ctx_null++;
    else if (is_engine_ctx(ctx)) t->ctx_engine++;
    else                         t->ctx_other++;
}

static void obj_free(objtab *t, void *p)
{
    if (p == NULL) { t->freed_null++; return; }
    t->freed[g_phase]++;
    if (obj_known(t, p)) t->freed_known++;
}

static void obj_use(objtab *t, void *p)
{
    if (p == NULL) return;          /* a re-init against what the ctx holds */
    t->used[g_phase]++;
    if (obj_known(t, p)) t->used_known[g_phase]++;
}

/* Every symmetric operation that actually went through EVP, counted where it
 * cannot be avoided.  The init calls above are not enough on their own: a port
 * that keeps one EVP_CIPHER_CTX per mode and re-keys it with
 * EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) names no cipher after the first
 * time, so "used a cipher it fetched" stops being evidence that the work
 * happened.  Update is the call no EVP-based port can skip: bytes go in
 * through one of the four spellings below or they do not go through EVP at
 * all.  Only calls that carry bytes are counted -- see the note there. */
static int g_evp_update[PH_N];

/* The low-level symmetric API, counted rather than routed.  There is nothing
 * to route: these calls take a key schedule the caller owns, so the only
 * question is whether they happened at all. */
static int g_low_setkey[PH_N];
static int g_low_block[PH_N];

/* --------------------------------------------------------- the interposers */

/* Declared here rather than included from OpenSSL's own headers: the probe needs the
 * ABI of the thirty-one functions it interposes, not OpenSSL's type system,
 * and a header that changed across the ladder would make this file a step-04
 * problem.  Every argument of every one of them is a pointer or an int,
 * except the two one-shot digest lengths -- EVP_Digest's count and
 * EVP_Q_digest's -- which are size_t, and EVP_Cipher's, which is unsigned
 * int; all three are spelled here the way the ABI spells them, which is the
 * same reason: the ABI, not the type system.  The typedefs below are shapes
 * and are fewer than the functions, because spellings that differ only in a
 * name share one. */

typedef void *(*fn_pp)(void *);
typedef void  (*fn_vp)(void *);
typedef void *(*fn_void)(void);
typedef void *(*fn_fetch)(void *, const char *, const char *);
/* The two EVP cipher init shapes differ in more than argument order: _ex takes
 * an ENGINE * in the third slot and _ex2 an OSSL_PARAM * in the fifth, and the
 * const-ness of key and iv moves with them.  One typedef for both would
 * compile with a warning and hand the wrong pointer's qualifiers to dlsym's
 * result, so there are two. */
typedef int   (*fn_init_ex)(void *, void *, void *, const void *, const void *);
typedef int   (*fn_init_ex2)(void *, void *, const void *, const void *, void *);
typedef int   (*fn_cipherinit6)(void *, void *, void *, const void *,
                                const void *, int);
/* The pre-_ex spellings drop the ENGINE * and the OSSL_PARAM * and keep
 * everything else, so neither of the two above fits them. */
typedef int   (*fn_init4)(void *, void *, const void *, const void *);
typedef int   (*fn_cipherinit5)(void *, void *, const void *, const void *,
                                int);
typedef int   (*fn_aes_setkey)(const unsigned char *, const int, void *);
typedef void  (*fn_aes_block)(const unsigned char *, unsigned char *,
                              const void *);
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

/* ---- ciphers */

void *EVP_CIPHER_fetch(void *ctx, const char *algorithm, const char *properties)
{
    void *r;
    NEXT(next, fn_fetch, "EVP_CIPHER_fetch")
    obj_fetch(&g_ciph, ctx);
    r = next(ctx, algorithm, properties);
    obj_note(&g_ciph, r);
    return r;
}

void EVP_CIPHER_free(void *c)
{
    NEXT(next, fn_vp, "EVP_CIPHER_free")
    obj_free(&g_ciph, c);
    next(c);
}

/* The nine EVP calls that hand libcrypto's cipher API a cipher to work with
 * and can be reached from this engine's interface.
 * _ex is what a port written against 3.0's own examples uses; _ex2 is the
 * parameterised spelling; the pre-_ex forms are the oldest spelling, still
 * published and not deprecated, and the one a port that was reading the
 * reference tree's own 1.0.2 calls is likeliest to reach for; the Cipher*
 * triple is the direction-flag form the reference tree's mode loops would
 * translate into most directly.  A port that encrypts through none of them is
 * not on EVP at all, which is the low-level counters' business below.  This
 * mirrors what the digest side already does, where EVP_DigestInit sits beside
 * _ex and _ex2 for the same reason.
 *
 * They are the whole of the Init family 3.0.17 publishes -- EVP_*Init, _ex and
 * _ex2 for Encrypt, Decrypt and Cipher, include/openssl/evp.h:751-794, none of
 * them deprecated -- and they are not every function in this libcrypto that
 * hands a cipher to a context.  The paragraph headed NINE IS THE INIT FAMILY
 * below says which the others are and why that is not a hole.
 *
 * All nine account identically: a non-NULL cipher is recorded as a use, and a
 * NULL one -- a re-key against whatever the context already holds -- is not,
 * because it names nothing.
 *
 * Two of them nest in libcrypto's own source: 3.0 writes EVP_EncryptInit and
 * EVP_DecryptInit as calls to EVP_CipherInit (crypto/evp/evp_enc.c:481, :501).
 * Whether that inner call leaves libcrypto and is seen here a second time is a
 * property of how libcrypto was linked rather than of this file, and it does
 * not change what the gate reads either way, because both counters move
 * together on every one of the nine.  Measured on this image it does not: a
 * tree that inits through the pre-_ex spelling reports the same 1188 calls as
 * one that inits through _ex, one per symmetric operation.
 *
 * NINE IS THE INIT FAMILY, NOT A COMPLETENESS CLAIM -- AND THE GATE DOES NOT
 * WANT ONE
 *
 * Three other 3.0.17 functions take an EVP_CIPHER * and start a context on it:
 * EVP_OpenInit (evp.h:844), EVP_SealInit (evp.h:849) and BIO_set_cipher
 * (evp.h:895).  Each reaches the context by calling one of the nine from
 * inside libcrypto -- EVP_DecryptInit_ex at p_open.c:29, EVP_EncryptInit_ex at
 * p_seal.c:34, EVP_CipherInit_ex at bio_enc.c:438 -- where, for the reason in
 * the paragraph above, this file does not see it; a tree that did its
 * symmetric work through one of them would read zero uses here.  None is
 * interposed, and nothing on this bench can reach them: EVP_OpenInit and
 * EVP_SealInit take an EVP_PKEY and unwrap or manufacture the symmetric key
 * themselves, so neither can implement a _cpri__AES* entry point, whose key
 * arrives as caller-supplied bytes; BIO_set_cipher wants a BIO chain this
 * engine's interface has nowhere to put; the reference tree mentions none of
 * the three outside the vendored 1.0.2 evp.h that declares them, the golden
 * tree calls none of them, and none of them is among the 86 OpenSSL symbols
 * the golden engine asks the loader for.
 *
 * Which is the harmless direction, and the reason a complete list is not what
 * the gate is after.  What it asks is whether the cipher-initialising calls
 * THIS engine made were handed something it had fetched, not whether some
 * other function exists somewhere: a spelling nobody interposed can only cost
 * a correct port a count it needs, and cannot let through a tree that encrypts
 * with something else, because the cipher such a tree would have to come by is
 * what the same gate's other conjuncts refuse -- by import, by call, by the
 * name written in the text, and by loader lookup.
 *
 * FORWARD OBLIGATION, for whoever writes the first step whose libcrypto is 3.5
 * -- on this ladder that is step 9, the opaque-symmetric-keys rung, and not
 * one rung earlier: 3.5.4's evp.h publishes three cipher-init entry points
 * 3.0.17 does not, and the 3.1.8, 3.2.5 and 3.4.2 images of steps 6, 7 and 8
 * publish none of them either, so there is nothing here for those steps to do.
 * As openssl-3.5.4/include/openssl/evp.h declares them (__owur elided for
 * width, its line numbers at the right):
 *
 *   int EVP_CipherInit_SKEY(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
 *                           EVP_SKEY *skey, const unsigned char *iv,
 *                           size_t iv_len, int enc,
 *                           const OSSL_PARAM params[]);               :823
 *   int EVP_CipherPipelineEncryptInit(EVP_CIPHER_CTX *ctx,
 *                           const EVP_CIPHER *cipher,
 *                           const unsigned char *key, size_t keylen,
 *                           size_t numpipes,
 *                           const unsigned char **iv, size_t ivlen);  :833
 *   int EVP_CipherPipelineDecryptInit(EVP_CIPHER_CTX *ctx,
 *                           const EVP_CIPHER *cipher,
 *                           const unsigned char *key, size_t keylen,
 *                           size_t numpipes,
 *                           const unsigned char **iv, size_t ivlen);  :838
 *
 * None of the three is interposed here, deliberately: the libcrypto this
 * step's image links against publishes none of them, so an interposer would be
 * a definition nothing can reach and evidence of nothing.  On the day they can
 * be reached, each must record its cipher into this table exactly as the nine
 * below do, or a correct port that inits through any one of them reads zero
 * uses and fails the by-name gate for no fault of its own.
 *
 * And at that step the obligation is larger than interposing three more
 * functions.  Under SKEY the key is no longer bytes the caller passes but an
 * object it holds, obtained from
 *
 *   EVP_SKEY *EVP_SKEY_import(OSSL_LIB_CTX *libctx, const char *skeymgmtname,
 *                             const char *propquery, int selection,
 *                             const OSSL_PARAM *params);            :2288
 *
 * which takes a library context of its own.  So the question this table exists
 * to answer -- did the object this operation worked with come from a fetch
 * against the context the engine made -- may have to follow the key object as
 * well as the EVP_CIPHER *, and a gate phrased around the cipher pointer alone
 * will not ask it.  The same note sits at that gate in the test script. */

int EVP_EncryptInit_ex(void *ctx, void *type, void *impl,
                       const void *key, const void *iv)
{
    NEXT(next, fn_init_ex, "EVP_EncryptInit_ex")
    obj_use(&g_ciph, type);
    return next(ctx, type, impl, key, iv);
}

int EVP_DecryptInit_ex(void *ctx, void *type, void *impl,
                       const void *key, const void *iv)
{
    NEXT(next, fn_init_ex, "EVP_DecryptInit_ex")
    obj_use(&g_ciph, type);
    return next(ctx, type, impl, key, iv);
}

int EVP_EncryptInit_ex2(void *ctx, void *type, const void *key,
                        const void *iv, void *params)
{
    NEXT(next, fn_init_ex2, "EVP_EncryptInit_ex2")
    obj_use(&g_ciph, type);
    return next(ctx, type, key, iv, params);
}

int EVP_DecryptInit_ex2(void *ctx, void *type, const void *key,
                        const void *iv, void *params)
{
    NEXT(next, fn_init_ex2, "EVP_DecryptInit_ex2")
    obj_use(&g_ciph, type);
    return next(ctx, type, key, iv, params);
}

int EVP_EncryptInit(void *ctx, void *type, const void *key, const void *iv)
{
    NEXT(next, fn_init4, "EVP_EncryptInit")
    obj_use(&g_ciph, type);
    return next(ctx, type, key, iv);
}

int EVP_DecryptInit(void *ctx, void *type, const void *key, const void *iv)
{
    NEXT(next, fn_init4, "EVP_DecryptInit")
    obj_use(&g_ciph, type);
    return next(ctx, type, key, iv);
}

int EVP_CipherInit(void *ctx, void *type, const void *key, const void *iv,
                   int enc)
{
    NEXT(next, fn_cipherinit5, "EVP_CipherInit")
    obj_use(&g_ciph, type);
    return next(ctx, type, key, iv, enc);
}

int EVP_CipherInit_ex(void *ctx, void *type, void *impl,
                      const void *key, const void *iv, int enc)
{
    NEXT(next, fn_cipherinit6, "EVP_CipherInit_ex")
    obj_use(&g_ciph, type);
    return next(ctx, type, impl, key, iv, enc);
}

int EVP_CipherInit_ex2(void *ctx, void *type, const void *key,
                       const void *iv, int enc, void *params)
{
    typedef int (*fn6)(void *, void *, const void *, const void *, int, void *);
    NEXT(next, fn6, "EVP_CipherInit_ex2")
    obj_use(&g_ciph, type);
    return next(ctx, type, key, iv, enc, params);
}

/* The three update spellings.  Counted and not inspected except for the one
 * thing that decides whether the call is evidence at all: an update handed no
 * input moves no bytes, so it does not count.  Without that, a tree that did
 * its symmetric work by hand and issued one empty update per operation would
 * read as though every operation had gone through EVP, which is the whole of
 * what the gate below asks.  The probe's own workload never drives an empty
 * one -- every symmetric operation it performs passes four blocks of data --
 * so refusing them cannot cost a correct port a count it needs against the
 * operations it is measured on. */

int EVP_EncryptUpdate(void *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl)
{
    typedef int (*fn5)(void *, unsigned char *, int *, const unsigned char *, int);
    NEXT(next, fn5, "EVP_EncryptUpdate")
    if (inl > 0) g_evp_update[g_phase]++;
    return next(ctx, out, outl, in, inl);
}

int EVP_DecryptUpdate(void *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl)
{
    typedef int (*fn5)(void *, unsigned char *, int *, const unsigned char *, int);
    NEXT(next, fn5, "EVP_DecryptUpdate")
    if (inl > 0) g_evp_update[g_phase]++;
    return next(ctx, out, outl, in, inl);
}

int EVP_CipherUpdate(void *ctx, unsigned char *out, int *outl,
                     const unsigned char *in, int inl)
{
    typedef int (*fn5)(void *, unsigned char *, int *, const unsigned char *, int);
    NEXT(next, fn5, "EVP_CipherUpdate")
    if (inl > 0) g_evp_update[g_phase]++;
    return next(ctx, out, outl, in, inl);
}

/* EVP_Cipher is the fourth spelling and the one a port is least likely to
 * reach for, but it is a documented public entry point that moves bytes
 * through a cipher context, so a gate that counted only the other three would
 * read zero on a correct port that used it. */

int EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in,
               unsigned int inl)
{
    typedef int (*fn4)(void *, unsigned char *, const unsigned char *,
                       unsigned int);
    NEXT(next, fn4, "EVP_Cipher")
    if (inl > 0) g_evp_update[g_phase]++;
    return next(ctx, out, in, inl);
}

/* ---- the low-level symmetric API, which must not run at all */

int AES_set_encrypt_key(const unsigned char *userKey, const int bits, void *key)
{
    NEXT(next, fn_aes_setkey, "AES_set_encrypt_key")
    g_low_setkey[g_phase]++;
    return next(userKey, bits, key);
}

int AES_set_decrypt_key(const unsigned char *userKey, const int bits, void *key)
{
    NEXT(next, fn_aes_setkey, "AES_set_decrypt_key")
    g_low_setkey[g_phase]++;
    return next(userKey, bits, key);
}

void AES_encrypt(const unsigned char *in, unsigned char *out, const void *key)
{
    NEXT(next, fn_aes_block, "AES_encrypt")
    g_low_block[g_phase]++;
    next(in, out, key);
}

void AES_decrypt(const unsigned char *in, unsigned char *out, const void *key)
{
    NEXT(next, fn_aes_block, "AES_decrypt")
    g_low_block[g_phase]++;
    next(in, out, key);
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

/* And the symmetric half's, kept separate rather than added in.  Step 03's
 * gates divide fetches by g_ops; folding two workloads into one counter would
 * silently move every one of those ratios. */
static int g_sym_ops[PH_N];

/* The engine's TPM2B convention, spelled locally for the same reason cpridrv
 * spells it locally: a TPM2B is a size and a flexible byte array, and a caller
 * has to supply the storage behind it. */
typedef union {
    struct { UINT16 size; BYTE buffer[256]; } t;
    TPM2B b;
} PB2B;

/* The key sizes gencases.py:253 drives, and for the same reason it drives
 * them: 128 and 256 are the two this corpus has keys for, MAX_AES_KEY_BITS is
 * 256, and a probe that drove a size the recording never recorded would be
 * asking the engine a question no golden answer exists for. */
static const UINT32 g_bits[] = { 128, 256 };
#define NBITS ((int)(sizeof(g_bits) / sizeof(g_bits[0])))

static const BYTE g_key256[32] = {
    0x60,0x3d,0xeb,0x10,0x15,0xca,0x71,0xbe,0x2b,0x73,0xae,0xf0,0x85,0x7d,0x77,0x81,
    0x1f,0x35,0x2c,0x07,0x3b,0x61,0x08,0xd7,0x2d,0x98,0x10,0xa3,0x09,0x14,0xdf,0xf4
};

/* One symmetric round: every publicly reachable cipher entry point, at both
 * key sizes.  Nine calls per key size, eighteen per round.
 *
 * The IV is reloaded before each call that takes one because the engine's IV
 * argument is IN/OUT -- it writes the chaining state back -- so a shared
 * buffer would make each call depend on the one before it and the round would
 * stop being a fixed workload.  The data is 64 bytes, four blocks, which every
 * mode here accepts: CBC and ECB require a whole number of blocks and the
 * three stream modes do not care. */
static void sym_round(int salt)
{
    BYTE   in[64], out[64], back[64];
    BYTE   key[32], iv[16];
    UINT32 bits;
    int    i, b;

    for (i = 0; i < (int)sizeof(in); i++) in[i] = (BYTE)(i * 7 + salt);

    for (b = 0; b < NBITS; b++) {
        bits = g_bits[b];
        memcpy(key, g_key256, sizeof(key));
        key[0] = (BYTE)(key[0] ^ (BYTE)salt);

        /* ECB: the one mode with no iv argument at all. */
        _cpri__AESEncryptECB(out, bits, key, sizeof(in), in);
        g_sym_ops[g_phase]++;
        _cpri__AESDecryptECB(back, bits, key, sizeof(out), out);
        g_sym_ops[g_phase]++;

        /* CBC, encrypt only.  _cpri__AESDecryptCBC exists and is not declared
         * (see the header comment); the e2e half reaches it, this cannot. */
        for (i = 0; i < 16; i++) iv[i] = (BYTE)i;
        _cpri__AESEncryptCBC(out, bits, key, iv, sizeof(in), in);
        g_sym_ops[g_phase]++;

        for (i = 0; i < 16; i++) iv[i] = (BYTE)i;
        _cpri__AESEncryptCFB(out, bits, key, iv, sizeof(in), in);
        g_sym_ops[g_phase]++;
        for (i = 0; i < 16; i++) iv[i] = (BYTE)i;
        _cpri__AESDecryptCFB(back, bits, key, iv, sizeof(out), out);
        g_sym_ops[g_phase]++;

        for (i = 0; i < 16; i++) iv[i] = (BYTE)(0xf0 + i);
        _cpri__AESEncryptCTR(out, bits, key, iv, sizeof(in), in);
        g_sym_ops[g_phase]++;
        for (i = 0; i < 16; i++) iv[i] = (BYTE)(0xf0 + i);
        _cpri__AESDecryptCTR(back, bits, key, iv, sizeof(out), out);
        g_sym_ops[g_phase]++;

        for (i = 0; i < 16; i++) iv[i] = (BYTE)i;
        _cpri__AESEncryptOFB(out, bits, key, iv, sizeof(in), in);
        g_sym_ops[g_phase]++;
        for (i = 0; i < 16; i++) iv[i] = (BYTE)i;
        _cpri__AESDecryptOFB(back, bits, key, iv, sizeof(out), out);
        g_sym_ops[g_phase]++;

        /* Not an operation and not counted as one: it allocates nothing and
         * fetches nothing.  It is here because a port that moves the mode
         * table to EVP tends to touch this function too, and a crash in it
         * should land in the probe rather than only in the corpus. */
        (void)_cpri__GetSymmetricBlockSize(TPM_ALG_AES, (UINT16)bits);
    }
}

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
    for (r = 0; r < rounds; r++) {
        for (a = 0; a < NALG; a++)
            round_for(g_algs[a], r + a);
        sym_round(r);
    }
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

static void emit(FILE *out, const char *tag, objtab *t)
{
    fprintf(out,
        "  \"%s_fetch\": %d,\n"
        "  \"%s_fetch_at_init\": %d,\n"
        "  \"%s_fetch_warm\": %d,\n"
        "  \"%s_fetch_long\": %d,\n"
        "  \"%s_ctx_engine\": %d,\n"
        "  \"%s_ctx_null\": %d,\n"
        "  \"%s_ctx_other\": %d,\n"
        "  \"%s_free\": %d,\n"
        "  \"%s_free_at_stop\": %d,\n"
        "  \"%s_free_null\": %d,\n"
        "  \"%s_free_known\": %d,\n"
        "  \"%s_live\": %d,\n"
        "  \"%s_use\": %d,\n"
        "  \"%s_use_known\": %d,\n"
        "  \"%s_use_long\": %d,\n"
        "  \"%s_distinct\": %d,\n"
        "  \"%s_over\": %d,\n",
        tag, sum(t->fetch), tag, t->fetch[PH_INIT], tag, t->fetch[PH_WARM],
        tag, t->fetch[PH_LONG],
        tag, t->ctx_engine, tag, t->ctx_null, tag, t->ctx_other,
        tag, sum(t->freed), tag, t->freed[PH_STOP], tag, t->freed_null,
        tag, t->freed_known, tag, sum(t->fetch) - t->freed_known,
        tag, sum(t->used), tag, sum(t->used_known), tag, t->used[PH_LONG],
        tag, t->n, tag, t->over);
}

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
        "  \"prov_over\": %d,\n"
        "  \"sym_ops\": %d,\n"
        "  \"sym_ops_warm\": %d,\n"
        "  \"sym_ops_long\": %d,\n"
        "  \"sym_per_round\": %d,\n"
        "  \"low_setkey\": %d,\n"
        "  \"low_setkey_warm\": %d,\n"
        "  \"low_setkey_long\": %d,\n"
        "  \"low_block\": %d,\n"
        "  \"low_block_warm\": %d,\n"
        "  \"low_block_long\": %d,\n"
        "  \"evp_update\": %d,\n"
        "  \"evp_update_warm\": %d,\n"
        "  \"evp_update_long\": %d,\n",
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
        g_qdigest, g_nmd, g_md_over, g_nprov, g_prov_over,
        sum(g_sym_ops), g_sym_ops[PH_WARM], g_sym_ops[PH_LONG], NBITS * 9,
        sum(g_low_setkey), g_low_setkey[PH_WARM], g_low_setkey[PH_LONG],
        sum(g_low_block), g_low_block[PH_WARM], g_low_block[PH_LONG],
        sum(g_evp_update), g_evp_update[PH_WARM], g_evp_update[PH_LONG]);
    emit(out, "ciph", &g_ciph);
    /* Last, so that no key above it needs a trailing comma decision. */
    fprintf(out, "  \"nbits\": %d\n}\n", NBITS);
    fclose(out);
    return ok ? 0 : 1;
}
