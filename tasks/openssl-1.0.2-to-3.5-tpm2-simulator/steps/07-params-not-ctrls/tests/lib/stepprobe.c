/* stepprobe.c (step 07) - what the engine does at run time, counted.
 *
 * Verifier-owned: this is step 07's instrument and nobody else's.
 *
 * THIS IS STEP 06'S PROBE WITH A PARAMETER-LIST HALF ADDED
 *
 * WHAT IS NEW AT STEP 07
 *
 * Step 07's subject is a boundary crossing that leaves no trace in the answer.
 * Nine typed setters in CpriRSA.c and one hand-built EC_GROUP in CpriECC.c
 * reach the same provider code as the parameter lists that replace them and
 * produce byte-identical results, measured over 64 rounds per curve and
 * every digest the tree builds.  So there is no behaviour case that separates the two
 * trees, and a scan that looks for EVP_PKEY_encrypt and finds it is reading a
 * tree that changed nothing.  What separates them is which symbols ran, what
 * was in the lists, and one allocation nobody freed.
 *
 * The instrument for that is a block near the bottom of this file, added
 * whole, with its own header explaining what each counter is for and what was
 * measured before it was written.
 *
 * It is also the first step whose instrument needed the WORKLOAD to change,
 * and that is worth stating here rather than only beside the counters.  Steps
 * 05 and 06 moved ECDSA onto EVP, and the EVP route builds its key from a
 * parameter list without ever calling EccCurveInit -- so the hand-built
 * EC_GROUP this step is about was, on the tree this step starts from, reached
 * by nothing the workload drove, and EC gates would read ecg_gfp = 0 off a
 * tree that builds every group by hand.  asym_round therefore drives _cpri__EccPointMultiply, on counters of its own
 * so that no inherited gate's denominator moves; g_ecpm_try below has the
 * whole argument.  Everything else above the new block is step 06's, verbatim.
 *
 * WHAT IS NEW AT STEP 06
 *
 * Step 06 is the first step whose whole subject is an argument that has been
 * NULL in every tree the ladder has produced so far.  Steps 03 to 05 required
 * the engine to fetch its algorithms and build its keys from a library context
 * of its own; all nine of those call sites pass NULL as the property query,
 * because until a second provider is in the context there is nothing for a
 * property query to choose between.  Step 06 puts one there, and then the NULL
 * is not a default any more -- it is a choice, and on 3.1.8 it is the wrong
 * one.  Measured: with the FIPS provider in the context, a NULL or empty property query sends RSA below 2048
 * bits and every EC key built from explicit parameters to an implementation
 * that refuses them, which is five corpus families.
 *
 * Four things a scan cannot answer, all of them about arguments or about
 * whether a call actually ran:
 *
 *   - "the engine loads a configuration into its own library context" is a
 *     claim about OSSL_LIB_CTX_load_config's first argument and about its
 *     return value.  A tree that calls it on the process default, or that
 *     ignores a zero return, reads identically to a scan that looks for the
 *     symbol and finds it.
 *   - "and gets a self-tested FIPS provider out of it" is two claims.  The
 *     first is which provider: OSSL_PROVIDER_load's name argument is a string
 *     the scan sees as a string and cannot connect to the pointer that comes
 *     back.  The second is that OSSL_PROVIDER_self_test ran on THAT pointer.
 *   - "and refuses to start if the self-test fails" cannot be measured on a
 *     tree where it passes, so this probe can be told to make it fail.  With
 *     PROBE_FAIL_SELFTEST=1 in the environment, OSSL_PROVIDER_self_test
 *     returns 0 without being called through, and a tree that checks the
 *     result will not finish its own startup.  This is the synthetic failure
 *     fixture the clean control needs, built into the instrument rather than
 *     left to the author to remember.
 *   - "every fetch and every key operation names a property query" is a claim
 *     about the last argument of five different calls.  NULL and "" are the
 *     same thing to libcrypto and different things to a reader, so they are
 *     counted apart and gated together.
 *
 * WHAT IS GATED AND WHAT IS ONLY REPORTED
 *
 * The property query's TEXT is reported and not gated.  Which query a port
 * chooses is a decision about its own deployment -- "provider=default" keeps
 * every recorded answer where it is, "fips=yes" loses every 1024-bit RSA
 * encryption and every explicit-parameter EC key, and there is no third answer
 * this task can demand -- so what is gated is that a query was made and what
 * that query then DID (see the counterfactual context below), never what it
 * said, and the behaviour corpus prices the choice.  The
 * paths handed to OSSL_LIB_CTX_load_config are reported for the same reason:
 * the file that activates the FIPS provider on this machine is named in the
 * instruction, but a port that reaches it through a symlink or a relative path
 * has not done anything wrong.
 *
 * STEP 05'S FILE IS BELOW, VERBATIM
 *
 * for the reason it gives for carrying step 04's verbatim: an inherited gate
 * has to read exactly the number its author wrote it against, so the
 * instrumentation grows by addition and never by replacement.  Step 06
 * inherits twenty-six rgates -- step 05's four among them, demoted -- and
 * every counter any of them reads is still produced here by the same
 * interposer against the same workload.  What is new at step 06 is counters,
 * two interposers and one environment switch; no round is added, because the
 * subject is the arguments of the calls the existing rounds already make.
 *
 * WHAT IS NEW AT STEP 05
 *
 * Four questions about the asymmetric half that a scan cannot answer:
 *
 *   - "the key was built from the parameters the caller handed in" is a claim
 *     about EVP_PKEY_fromdata being the route.  An engine that keeps a key of
 *     its own, or that builds one through the deprecated RSA_set0_key /
 *     EVP_PKEY_assign_RSA pair, reads identically to a scan that looks for
 *     EVP_PKEY_sign and finds it.  The difference is which OpenSSL entry
 *     points actually ran.
 *   - "built in the engine's library context" is step 03's argument-identity
 *     question, asked of the first argument of EVP_PKEY_CTX_new_from_name
 *     instead of the first argument of EVP_MD_fetch.  Every presence fact can
 *     be true and the key can still come from the process default.
 *   - "one key per operation" is a rate, and it is the OPPOSITE of the rate
 *     steps 03 and 04 measure.  A digest and a cipher are fetched once and
 *     held, because neither depends on the caller's arguments; an asymmetric
 *     key has to be rebuilt on every call, because it is nothing but the
 *     caller's arguments.  A port whose fromdata count flattens across the
 *     long run has cached key material across calls -- a correctness bug the
 *     behaviour corpus would catch only by luck, since the corpus drives each
 *     case in a fresh process.  So this rate must scale, and the gate says so.
 *   - "the deprecated spellings are gone" has a run-time half no scan has, for
 *     the same reason step 04's four AES_ interposers exist: a call arriving
 *     through a function pointer, or through a translation unit the scan
 *     mis-attributes, is invisible to the scan and not to this.
 *
 * WHAT IS GATED AND WHAT IS ONLY REPORTED
 *
 * ECDSA_do_sign, ECDSA_do_verify, ECDSA_sign and ECDSA_verify are counted and
 * gated: no provider path inside 3.0.17 reaches them, they exist only for a
 * caller who already holds an EC_KEY, and step 04's ecdsa-sign-library and
 * ecdsa-verify-library rgates required the engine to IMPORT two of them.  Step
 * 05 retires those two gates and asserts the opposite, and this is where the
 * opposite is measured rather than read.
 *
 * EC_KEY_new, EC_KEY_new_by_curve_name and RSA_new are counted and NOT gated.
 * The default provider's own key management is built on those two types, and
 * while 3.0.17 reaches its internal constructors rather than these exported
 * ones, that is an implementation detail of a library the candidate does not
 * control; a gate resting on it would be a gate that OpenSSL could break.  The
 * numbers are written out because item 8 reads them, not because a threshold
 * does.
 *
 * THE VECTORS ARE CONSTANTS, NOT SOMETHING THE ENGINE MADE
 *
 * The verify half needs a signature that verifies, and the obvious way to get
 * one is to sign with the engine first.  That would make the instrument depend
 * on the thing it is measuring: a port that broke _cpri__SignEcc would stop
 * driving _cpri__ValidateSignatureEcc, and the verify counters would read zero
 * for a reason that has nothing to do with verification.  So the signatures
 * below are fixed constants, built by build/gates/05-asymvec.py from the same
 * keys.json the behaviour corpus draws its keys from, with no OpenSSL anywhere
 * in the path: ECDSA over that file's own curve arithmetic, EMSA-PKCS1-v1_5
 * and EMSA-PSS straight out of RFC 8017.  That script checks d*G == Q for all
 * three curves before printing anything, which is a check on keys.json as much
 * as on itself, and re-decodes every signature it prints -- the PSS one via a
 * re-implementation of the engine's own PssDecode (CpriRSA.c:1097), because
 * that function's salt handling is what the probe drives it with.
 *
 * ECDSA SIGNING IS NOT REPRODUCIBLE AND NO BYTE OF IT IS LOOKED AT
 *
 * _cpri__SignEcc dispatches ECDSA to SignEcdsa (CpriECC.c:1600), which ignores
 * kIn and draws its own nonce, so the signature is different every round.
 * Signing runs on NIST_P256 and BN_P256 only; SignEcdsa's pAssert at
 * CpriECC.c:1077 against MAX_ECC_PARAMETER_BYTES, which OsslCryptoEngine.h:83
 * sets to 32, makes a P-384 signature a fatal TPM failure in this engine, and
 * build/record/expected.txt records that as `eccsv/ok/007 tpmfail=4'.
 * This probe never compares those bytes to anything: it counts that the call
 * happened and which OpenSSL entry points it went through.  The bytes are the
 * behaviour corpus's business, and cpridrv.c:1244 records why the corpus does
 * not score them either.
 *
 * WHAT FOLLOWS IS STEP 04'S FILE
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

/* Step 07 reads the contents of the parameter lists the engine hands
 * OpenSSL, so it needs the list layout and the key names.  It declares them
 * here rather than including <openssl/params.h>: this file defines
 * OSSL_LIB_CTX_new and friends as interposers taking void *, and the real
 * headers declare them over OSSL_LIB_CTX *, so including them is a compile
 * error rather than a convenience.  The layout below is the public ABI --
 * struct ossl_param_st has been these five members, in this order, since
 * 3.0, and the key strings are the wire names a provider matches on, so
 * neither can move without breaking every provider ever built.
 *
 *   struct ossl_param_st          openssl/core.h
 *   OSSL_PARAM_INTEGER, ...       openssl/core.h
 *   the OSSL_*_PARAM_* strings    openssl/core_names.h
 *
 * The names below are spelled out to their string values because that is
 * what is being compared; core_names.h reaches most of them through two
 * levels of #define (OSSL_SIGNATURE_PARAM_DIGEST -> OSSL_PKEY_PARAM_DIGEST
 * -> OSSL_ALG_PARAM_DIGEST -> "digest"). */
typedef struct probe_ossl_param_st {
    const char  *key;
    unsigned int data_type;
    void        *data;
    size_t       data_size;
    size_t       return_size;
} OSSL_PARAM;

#define OSSL_PARAM_INTEGER                 1
#define OSSL_PARAM_UNSIGNED_INTEGER        2
#define OSSL_PARAM_UTF8_STRING             4
#define OSSL_PARAM_OCTET_STRING            5

#define OSSL_ASYM_CIPHER_PARAM_PAD_MODE    "pad-mode"
#define OSSL_ASYM_CIPHER_PARAM_OAEP_LABEL  "oaep-label"
#define OSSL_SIGNATURE_PARAM_PAD_MODE      "pad-mode"
#define OSSL_SIGNATURE_PARAM_DIGEST        "digest"
#define OSSL_SIGNATURE_PARAM_MGF1_DIGEST   "mgf1-digest"
#define OSSL_SIGNATURE_PARAM_PSS_SALTLEN   "saltlen"

#define OSSL_PKEY_RSA_PAD_MODE_OAEP        "oaep"
#define OSSL_PKEY_RSA_PAD_MODE_PSS         "pss"
#define OSSL_PKEY_RSA_PAD_MODE_PKCSV15     "pkcs1"
#define OSSL_PKEY_RSA_PSS_SALT_LEN_DIGEST  "digest"
#define OSSL_PKEY_RSA_PSS_SALT_LEN_MAX     "max"
#define OSSL_PKEY_RSA_PSS_SALT_LEN_AUTO    "auto"
#define OSSL_PKEY_RSA_PSS_SALT_LEN_AUTO_DIGEST_MAX   "auto-digestmax"

/* The same parameter takes the same four sentinels as a negative number.
 * rsa.h's values, spelled out here for the reason the strings above are:
 * this file cannot include the real headers. */
#define RSA_PSS_SALTLEN_DIGEST             (-1)
#define RSA_PSS_SALTLEN_AUTO               (-2)
#define RSA_PSS_SALTLEN_MAX                (-3)
#define RSA_PSS_SALTLEN_AUTO_DIGEST_MAX    (-4)

#define OSSL_PKEY_PARAM_EC_P               "p"
#define OSSL_PKEY_PARAM_EC_GENERATOR       "generator"
#define OSSL_PKEY_PARAM_EC_ORDER           "order"
#define OSSL_PKEY_PARAM_GROUP_NAME         "group"

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
 * are: providers-released asks whether the provider the engine loaded was
 * given back, and an unload that was written down is not an unload that ran.  A shutdown that
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
 * the retained digests-fetched-once gate wants to know is how many of the
 * references THIS engine took are still out, and a count of frees on its own
 * does not answer that.  A tree
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

/* And the same table read from the other side.  The retained digests-by-name
 * gate's sentence is "every digest the engine hashes with is obtained by
 * fetching it by name from the library context the engine made", and the
 * fetch counters above cannot say
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
     * every digest, fetched or not.  So the flag is reported and that gate
     * fails closed on it rather than reading a tautology as a clean tree. */
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

/* ------------------------------------ the asymmetric half's counters (step 05)
 *
 * Nothing above this line moves.  Every counter step 03's and step 04's gates
 * read keeps its name, its type and the workload that fills it.
 */

/* Asymmetric operations attempted, and of those, the ones that came back
 * CRYPT_SUCCESS.  Both are needed and neither is enough: a rate over "ops that
 * worked" reads clean on a tree where nothing works, and a rate over "ops
 * attempted" reads clean on a tree where nothing runs.  The gates require the
 * two to be equal in the long run and then divide by one of them. */
static int g_asym_try[PH_N];
static int g_asym_ops[PH_N];

/* Point multiplications attempted, the ones that came back CRYPT_SUCCESS, and
 * the ones whose product point was the public half the vector table already
 * says it should be.  Kept apart from the two counters above rather than
 * folded into them: every gate from steps 01-06 that reads those was
 * calibrated against a workload that did not do this, and widening an
 * inherited denominator would change what those gates measure.
 *
 * WHY THE WORKLOAD DOES THIS AT ALL.  Steps 05 and 06 moved ECDSA signing and
 * verification onto EVP, and the EVP route builds its key from a parameter
 * list -- it never calls EccCurveInit.  Nothing else this workload drove did
 * either.  So on the tree this step starts from, a run that completes 198
 * ECDSA operations constructs exactly zero EC_GROUPs, and gates that read
 * ecg_gfp and ecg_setgen off that run would read zero on a tree that builds
 * every group by hand at CpriECC.c:234 -- a clean bill of health handed to
 * the one tree that certainly does not deserve it.  An instrument has to be
 * shown to reach what it measures, not only not to perturb it.
 *
 * _cpri__EccPointMultiply is the cheapest exported entry point that still goes
 * through EccCurveInit (CpriECC.c:458).  ECDAA, SM2 and the two key-exchange
 * entry points are the others and all four cost more for the same reading.
 *
 * With Qin and uIn NULL the engine multiplies the curve's OWN generator by d,
 * so R has to come out as the public half beside d in the table, and
 * ecpm_match says whether it did.
 *
 * ALL THREE ARE REPORTED AND NOT GATED.  ecpm_match does read on a generator
 * the port got wrong -- but it is not the only thing that does, and the other
 * thing costs more.  The frozen behaviour corpus multiplies the same scalars
 * by the same generators and compares against the same products: cases.txt
 * line 1191 is eccmul/ok/005 on NIST P256 with the scalar and the product
 * this probe's own vector table carries, line 1389 is the BN P256 row on the
 * scalar beside it, and
 * there are 58 eccmul rows beside the eccsign, eccgen and ecccommit families.
 * A tree that transposed or truncated a generator loses those rows, and
 * behaviour is 0.36 of the reward where this gate is 0.162 of it.  So the
 * workload stays -- without it nothing in this run reaches EccCurveInit at
 * all, which is why these counters were added -- and the three numbers are
 * printed in both of gate 3's sentences and gate neither. */
static int g_ecpm_try[PH_N];
static int g_ecpm_ok[PH_N];
static int g_ecpm_match[PH_N];

/* EVP_PKEY_CTX creation, and which library context it named.  The two _from_
 * spellings take that context as their first argument, which is the whole of
 * the question.  EVP_PKEY_CTX_new does not take one -- it inherits whatever
 * the EVP_PKEY was built in -- so it is counted apart and is evidence neither
 * way; a port that builds its key with fromdata against the engine's context
 * and then makes the operation context with EVP_PKEY_CTX_new has still done
 * the step, and the fromdata counters below are where that shows. */
#define MAXPKCTX 64
static void *g_pkctx[MAXPKCTX];        /* live contexts only: see pkctx_drop */
static char  g_pkctx_cls[MAXPKCTX];    /* 'e' engine, 'n' NULL, 'o' other */
static int   g_npkctx;
static int   g_pkctx_over;             /* more live at once than the table holds */
static int   g_pkctx_new[PH_N];        /* the two _from_ spellings */
static int   g_pkctx_ctx_engine;
static int   g_pkctx_ctx_null;
static int   g_pkctx_ctx_other;
static int   g_pkctx_inherit[PH_N];    /* EVP_PKEY_CTX_new, no context argument */
static int   g_pkctx_free[PH_N];

/* EVP_PKEY_fromdata, attributed through the EVP_PKEY_CTX it was called on.
 * "unknown" is a context this probe never watched being created -- one made
 * through a spelling not interposed here, or before the interposers were
 * reachable -- and is reported separately from "other" because the two are
 * different findings and only one of them is a wrong library context. */
static int   g_fromdata[PH_N];
static int   g_fromdata_ctx_engine;
static int   g_fromdata_ctx_null;
static int   g_fromdata_ctx_other;
static int   g_fromdata_ctx_unknown;

/* The three operations step 05 moves onto EVP, and the fourth it does not.
 * _cpri__DecryptRSA stays on RSADP at this step, and not only because the
 * instruction says so: consttime-private-exp, inherited from step 02, requires
 * RSADP to reach BN_set_flags, so a port that routes the private half through
 * EVP_PKEY_decrypt breaks an rgate.  Counting decrypt here is how that also
 * shows up as a measurement instead of only as a scan. */
static int   g_pk_sign[PH_N];
static int   g_pk_verify[PH_N];

/* The same verify calls, attributed to the engine operation that was in
 * flight when they arrived.  pk_verify counts every one and cannot say which
 * entry point made it, which is enough for a report and not enough for a
 * gate: a tree that runs RSA verification through EVP and checks ECDSA
 * signatures by hand produces the same total as one that does the opposite.
 * The marker is set by the workload around its own calls -- it is not read
 * out of the EVP_PKEY_CTX, because reading it out means asking libcrypto what
 * the key is, and a probe that introspects the library it is watching is
 * measuring that library rather than the tree.  A verify that arrives with no
 * marker set is attributed to neither and counted separately, so that the
 * three always add up to pk_verify and a gate can check that they do. */
static char  g_asym_in;                /* 'r' RSA, 'e' EC, 0 between calls */
static int   g_pk_ver_rsa[PH_N];
static int   g_pk_ver_ec[PH_N];
static int   g_pk_ver_none;
static int   g_pk_encrypt[PH_N];
static int   g_pk_decrypt[PH_N];
static int   g_pkey_free[PH_N];

/* The deprecated asymmetric API.  The four ECDSA_ spellings are gated on; the
 * three constructors are reported only.  The header says why. */
static int   g_low_ecdsa[PH_N];
static int   g_low_eckey[PH_N];
static int   g_low_rsanew[PH_N];

/* ------------------------------------- the FIPS half's counters (step 06)
 *
 * Nothing above this line moves.  Every counter step 03's, step 04's and step
 * 05's gates read keeps its name, its type and the workload that fills it.
 */

/* The configuration load.  OSSL_LIB_CTX_load_config takes the context as its
 * first argument and returns 1 on success, and both of those are the finding:
 * a call on NULL configures the process default, which is the thing step 03
 * made the engine stop using, and a call whose return is thrown away leaves a
 * tree that reports a working FIPS provider when it has none. */
static int   g_cfg[PH_N];
static int   g_cfg_engine;        /* into a context this probe saw created */
static int   g_cfg_null;          /* into the process default */
static int   g_cfg_other;
static int   g_cfg_ok;            /* of all of them, the ones that returned 1 */
static int   g_cfg_ok_engine;     /* returned 1 AND named the engine's context */
static char  g_cfg_path[96];      /* the first path, reported and not gated */

/* Providers by name.  note_prov above records the pointer, which is what the
 * unload side needs; the name is the other half and neither substitutes for
 * the other.  "loaded a provider" has been true since step 03; step 06 asks
 * which one, and the string is only in scope inside the two load interposers,
 * so the tally is kept there. */
#define PROVNAME 24
static char  g_prov_name[MAXPROV][PROVNAME];
static int   g_prov_fips;         /* loads naming "fips" */
static int   g_prov_fips_ok;      /* of those, the ones that returned non-NULL */
static int   g_prov_fips_engine;  /* of those, into a context the engine made */
static int   g_prov_fips_ok_engine;/* the conjunction: named fips, into the
                                   * engine's context, and came back non-NULL.
                                   * The two counters above are each half of
                                   * it and neither implies it; a gate whose
                                   * sentence is one clause reads one counter. */
static int   g_prov_dflt;         /* loads naming "default" */
static int   g_prov_base;         /* loads naming "base" */
static void *g_fips_prov[MAXPROV];/* the pointers the fips loads gave back */
static int   g_nfips;
static int   g_fips_gone[MAXPROV];/* which of those have been unloaded */
static int   g_fips_unload[PH_N]; /* unloads of one of those, by phase */

/* The self test.  OSSL_PROVIDER_self_test on a provider the engine loaded is
 * the call that turns "the module is present" into "the module answered", and
 * like the config load it has a return value that a tree can drop.
 *
 * g_selftest_forced is the fixture.  With PROBE_FAIL_SELFTEST=1 in the
 * environment this interposer returns 0 without calling through, which is the
 * only way to ask a passing tree what it would do if the module failed.  A
 * tree that checks the result stops there and takes the whole run down with
 * it; a tree that calls and ignores finishes exactly as it did before, and
 * the two runs are told apart by ok and tpmfail.  The switch is read once, in
 * .preinit_array -- before any shared object's constructor, this tree's
 * included -- and its bytes are erased there, so that a tree cannot be
 * affected by it and cannot see that it is being asked.  A constructor was
 * tried and was measurably not early enough; capture_fixture_from below is
 * where that is written down. */
static int   g_forced;            /* the fixture, captured before main */

static int   g_forced_done;       /* read once, and this is the once */

/* Before main, and before a line of the tree runs.  Taking the variable out of
 * the environment on the way past means the engine's own
 * getenv("PROBE_FAIL_SELFTEST") finds nothing: a tree cannot notice which of
 * the two runs it is in, and so cannot answer them differently.  Reading it
 * here also keeps the read-once property -- one answer for a run, whatever
 * the tree does mid-run.
 *
 * A CONSTRUCTOR IS NOT EARLY ENOUGH, AND THAT WAS MEASURED
 *
 * This probe is an -rdynamic EXECUTABLE, and an executable's constructors run
 * AFTER those of the shared objects it links against.  A tree that planted
 * __attribute__((constructor)) inside libCryptoEngine.so therefore runs
 * first, and with the capture in a constructor it reads the variable before
 * this file erases it.  A tree that can tell the two
 * runs apart can refuse to start in one and not the other, which passes gate 4
 * without ever reading a self test result.
 *
 * .preinit_array is the answer.  It exists only in the main executable, and
 * the loader runs it before the init_array of ANY shared object, this tree's
 * included.  There is no earlier place for the tree to stand.
 *
 * The scan is done by hand over the envp the loader passes rather than by
 * getenv/unsetenv, because at preinit time __environ has not necessarily been
 * pointed at that array yet and the library calls would have nothing to read.
 * Removing the entry by shifting the rest down is what unsetenv does, and it
 * works here for the same reason: __environ is pointed at THIS array, so the
 * hole is already in it by the time anything can look.
 *
 * AND THE POINTER IS NOT THE STRING
 *
 * Dropping the pointer closes getenv and closes a walk of `environ`.  It does
 * not close /proc/self/environ, which on Linux is not that array at all: it is
 * mm->env_start .. mm->env_end, the block of NUL-separated strings the kernel
 * laid down on the stack at execve, and editing a pointer array that happens
 * to point into it changes nothing there.  The fixture run is
 * `env PROBE_FAIL_SELFTEST=1 .../stepprobe`, so the name and the value are in
 * that block in the forced run and absent from it in the first, and a tree
 * that opens the file -- or that walks off the end of argv into the same
 * strings -- reads which run it is in and can refuse to start in one of them.
 * That passes the self-test gate without ever looking at a self-test result,
 * which is the one thing the gate exists to stop.
 *
 * So the whole KEY=VALUE string is overwritten with NULs IN PLACE before the
 * pointer is dropped.  The bytes are writable -- they are the process's own
 * stack -- and after this the block holds a run of empty strings where the
 * entry was, which is what a variable that was never set looks like. */
static void capture_fixture_from(char **envp)
{
    static const char key[] = "PROBE_FAIL_SELFTEST=";
    const size_t klen = sizeof key - 1;
    char **rd, **wr;

    if (envp == NULL) return;
    for (rd = wr = envp; *rd != NULL; rd++) {
        if (strncmp(*rd, key, klen) == 0) {
            char       *s = *rd;
            const char *v = s + klen;
            if (v[0] == '1' && v[1] == '\0') g_forced = 1;
            memset(s, 0, strlen(s));        /* the bytes, not just the pointer */
            continue;                       /* dropped, not copied down */
        }
        *wr++ = *rd;
    }
    *wr = NULL;
    g_forced_done = 1;
}

static void probe_preinit_fixture(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv;
    capture_fixture_from(envp);
}

__attribute__((section(".preinit_array"), used))
static void (*const probe_preinit[])(int, char **, char **) = {
    probe_preinit_fixture
};

/* The constructor stays as the second half of a belt and braces: if this ever
 * links somewhere .preinit_array is not honoured the capture still happens
 * before main, one stop later than it should.  unsetenv is called either way,
 * because by now libc is up and an environment this file has already emptied
 * costs nothing to empty again. */
__attribute__((constructor))
static void probe_capture_fixture(void)
{
    extern char **environ;

    if (!g_forced_done) capture_fixture_from(environ);
    unsetenv("PROBE_FAIL_SELFTEST");
}

static int   g_init_rc;           /* what _cpri__InitCryptoUnits returned */
static int   g_selftest[PH_N];
static int   g_selftest_known;    /* argument came back out of a load this saw */
static int   g_selftest_fips;     /* argument was one of the fips pointers */
static int   g_selftest_ok;       /* returned 1 */
static int   g_selftest_forced;   /* times this probe made it return 0 */

/* Property queries, counted at the five calls that take one.  NULL and "" are
 * the same thing to libcrypto -- both mean "no preference" -- and different
 * things to a reader, so they are counted apart and gated together.  What the
 * query SAYS is reported and not gated: see the header. */
static int   g_pq_md_null,  g_pq_md_empty,  g_pq_md_set;
static int   g_pq_ci_null,  g_pq_ci_empty,  g_pq_ci_set;
static int   g_pq_pkn_null, g_pq_pkn_empty, g_pq_pkn_set;
static int   g_pq_pkp_null, g_pq_pkp_empty, g_pq_pkp_set;
static int   g_pq_q_null,   g_pq_q_empty,   g_pq_q_set;
/* And the key-operation contexts built through a spelling that has nowhere to
 * put a query.  EVP_PKEY_CTX_new and EVP_PKEY_CTX_new_id take a key or a NID
 * and an ENGINE and nothing else, so a context built through either of them
 * has named no implementation -- which at the fetch is the same thing as a
 * NULL query, and is folded into the pq_null total for that reason.  Without
 * this they were a way to build a key-operation context that gate 3 could not
 * see: g_pkctx_inherit counted them and no gate read it. */
static int   g_pq_noq;
static char  g_pq_text[64];       /* the first non-empty one, reported */

/* A JSON string value, made safe without a JSON library.  The probe writes its
 * output with fprintf and has done since step 01; a path or a property query
 * is the first thing it has reported that it did not choose itself, so the
 * characters that would end the string early are the ones that are dropped.
 * Anything outside printable ASCII, and the two JSON metacharacters, become a
 * dot -- the field is for a human reading the log, and a gate that matched on
 * it would be gating the text, which the header says it does not. */
static void note_str(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (dst[0] != '\0' || src == NULL) return;   /* first one wins */
    for (i = 0; i + 1 < cap && src[i] != '\0'; i++)
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7f &&
                  src[i] != '"' && src[i] != '\\') ? src[i] : '.';
    dst[i] = '\0';
}

static int g_pq_empty_total(void)
{
    return g_pq_md_empty + g_pq_ci_empty + g_pq_pkn_empty + g_pq_pkp_empty
         + g_pq_q_empty;
}

static void note_pq(const char *pq, int *nul, int *empty, int *set)
{
    if (pq == NULL)     { (*nul)++;   return; }
    if (pq[0] == '\0')  { (*empty)++; return; }
    (*set)++;
    note_str(g_pq_text, sizeof g_pq_text, pq);
}

static int is_fips_prov(void *p)
{
    int i;
    if (p == NULL) return 0;
    for (i = 0; i < g_nfips; i++)
        if (g_fips_prov[i] == p) return 1;
    return 0;
}

/* Which module handles are still out.  By DISTINCT POINTER, not by loads minus
 * unloads: OSSL_PROVIDER_load of a name already loaded in the same context
 * hands back the same pointer with its reference count raised, so a tree that
 * loads "fips" twice and unloads it once has two loads, one unload, and no
 * handle this probe can still see.  Counting arithmetically would report one
 * live module and fail gate 2 on a tree that gave back every handle it was
 * given, which is the sentence gate 2 actually asks.
 *
 * What this deliberately does not measure is libcrypto's reference count --
 * the probe has no access to it, and a second load is a second reference.  A
 * tree in that shape is leaking a reference and this gate will not say so;
 * gate 2's sentence is about the handles the engine holds, and every one of
 * them has been given back. */
static void note_fips_unload(void *p)
{
    int i;
    for (i = 0; i < g_nfips; i++)
        if (g_fips_prov[i] == p) { g_fips_gone[i] = 1; return; }
}

static int fips_live(void)
{
    int i, n = 0;
    for (i = 0; i < g_nfips; i++)
        if (!g_fips_gone[i]) n++;
    return n;
}

/* Called from both load spellings, after the call, with everything in scope:
 * which context was asked, what name was asked for, and what came back. */
static void note_prov_named(void *ctx, const char *name, void *p)
{
    int i;

    if (name == NULL) name = "";
    if (strcmp(name, "fips") == 0) {
        g_prov_fips++;
        if (is_engine_ctx(ctx)) g_prov_fips_engine++;
        if (p != NULL) {
            g_prov_fips_ok++;
            if (is_engine_ctx(ctx)) g_prov_fips_ok_engine++;
            for (i = 0; i < g_nfips; i++)
                if (g_fips_prov[i] == p) break;
            if (i == g_nfips && g_nfips < MAXPROV) g_fips_prov[g_nfips++] = p;
        }
    } else if (strcmp(name, "default") == 0) {
        g_prov_dflt++;
    } else if (strcmp(name, "base") == 0) {
        g_prov_base++;
    }
    if (p == NULL) return;
    for (i = 0; i < g_nprov; i++)
        if (g_prov[i] == p) {
            note_str(g_prov_name[i], PROVNAME, name);
            return;
        }
}

/* --------------------------------------------------------- the interposers */

/* Declared here rather than included from OpenSSL's own headers: the probe needs the
 * ABI of the forty-eight functions it interposes, not OpenSSL's type system,
 * and a header that changed across the ladder would make this file a step-05
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
typedef int   (*fn_loadcfg)(void *, const char *);
typedef int   (*fn_ip)(void *);
typedef int   (*fn_init3)(void *, void *, void *);
typedef int   (*fn_init2)(void *, void *);
typedef int   (*fn_oneshot)(const void *, size_t, void *, void *, void *,
                            void *);
typedef int   (*fn_qdigest)(void *, const char *, const char *, const void *,
                            size_t, void *, void *);

/* The asymmetric shapes.  EVP_PKEY_CTX_new_from_name is not listed: its ABI is
 * context, name, property query, which is fn_fetch above, and it shares that
 * typedef for that reason and no other. */
typedef void *(*fn_ctxfrompkey)(void *, void *, const char *);
typedef void *(*fn_ctxnew)(void *, void *);
typedef void *(*fn_ctxnewid)(int, void *);
typedef int   (*fn_fromdata)(void *, void **, int, void *);
/* sign, encrypt and decrypt: context, out, out length by reference, in, in
 * length by value.  verify differs because the signature is an input, so both
 * of its lengths are by value. */
typedef int   (*fn_pkop)(void *, unsigned char *, size_t *,
                         const unsigned char *, size_t);
typedef int   (*fn_pkverify)(void *, const unsigned char *, size_t,
                             const unsigned char *, size_t);
typedef void *(*fn_dosign)(const unsigned char *, int, void *);
typedef int   (*fn_doverify)(const unsigned char *, int, const void *, void *);
typedef int   (*fn_sigsign)(int, const unsigned char *, int, unsigned char *,
                            unsigned int *, void *);
typedef int   (*fn_sigverify)(int, const unsigned char *, int,
                              const unsigned char *, int, void *);
typedef void *(*fn_newcurve)(int);

#define NEXT(var, type, name)                                   \
    static type var;                                            \
    if (var == NULL) {                                          \
        var = (type)dlsym(RTLD_NEXT, name);                     \
        if (var == NULL) {                                      \
            fprintf(stderr, "probe: no next %s\n", name);       \
            abort();                                            \
        }                                                       \
    }

/* ---- the counterfactual context ----------------------------------------
 *
 * Gate 3 asks two questions and this is the second: not "was a query passed"
 * but "did the query decide anything".  A tree that writes a non-empty string
 * no implementation defines -- "?x=1" -- passes presence at every call site
 * and then resolves exactly as a NULL query does, because OpenSSL drops an
 * optional clause that matches nothing and falls straight back to the order
 * the providers were loaded in.  On the load order this step's golden writes,
 * that tree computes every recorded answer correctly, so nothing the corpus
 * measures can tell it from a tree that named its implementation.
 *
 * So the probe builds a second OSSL_LIB_CTX of its own, loads the same
 * configuration into it, and loads the same two providers into it in the
 * REVERSED order -- "fips" first, "default" second.  Every digest fetch, every
 * cipher fetch and every key-operation context the tree makes is repeated
 * there, with the tree's own query string.  A query that names an
 * implementation lands on the same provider in both contexts: a pin.  A query
 * that names nothing follows the load order and lands on the other one: a
 * drift.
 *
 * The key-operation contexts are repeated as EVP_KEYMGMT_fetch, which is the
 * call their query is really for; shadow_keymgmt below is that, and says why
 * it is the half that matters most here.
 *
 * The tree cannot see this context.  It is never the engine's, it is never
 * passed to the tree, and the only input the tree has to it is the query
 * string it was going to pass anyway.  Nor can it be written against: the one
 * way to make every fetch pin is to name an implementation, which is the
 * requirement.
 *
 * AND IT MUST NOT MOVE A COUNTER ANY OTHER GATE READS.  Gates 1, 2 and 4 are
 * arithmetic over configuration loads, provider loads, unloads and self tests;
 * gate 3's own presence half is arithmetic over the calls watched here.  A
 * shadow load that came back through this file's OSSL_PROVIDER_load would add
 * a load the tree never made and would break all four of them silently.  Two
 * things stop it:
 *
 *   Every call the shadow makes goes through a pointer from
 *   dlsym(RTLD_NEXT, ...), so it enters libcrypto directly and never passes
 *   through an interposer in this file.  That is the mechanism.
 *
 *   g_shadow_busy is set for the whole of the shadow's work, and every
 *   interposer whose counters gates 1 to 4 read returns straight through while
 *   it is set, counting nothing.  That is the belt to the mechanism's braces
 *   -- and g_shadow_reenter counts the times it fired, so "the guard was never
 *   needed" is a number in probe.json rather than a hope in a comment.  On
 *   this libcrypto it is 0: internal fetches resolve without coming back out
 *   through the exported entry points this file defines.  It is reported and
 *   NOT gated, in either direction: gating it to 0 would be a conjunct that
 *   has never been anything else, and a non-zero value is the guard working,
 *   not a fault.
 *
 * Errors the shadow's own fetches raise are bracketed by ERR_set_mark and
 * ERR_pop_to_mark, so a failed shadow fetch cannot leave anything on the
 * thread's error queue for the tree to find.
 *
 * The shadow context is deliberately never freed.  It is built at the first
 * fetch and wanted until the last; freeing it would mean unloading two
 * providers, and an unload is exactly the event gate 2 counts.  The process
 * ends a few milliseconds later.
 */

typedef const char *(*fn_provname)(void *);
typedef int   (*fn_mark)(void);

#define SHADOW_OFF   0
#define SHADOW_READY 1
#define SHADOW_DEAD  2

/* The three kinds of implementation a comparison can be about, and the three
 * libcrypto calls each kind needs.  A digest, a cipher and a key management
 * implementation are fetched by name with a query and carry a provider, and
 * nothing else about the comparison differs between them -- so the kind is an
 * index into these tables and not three copies of the function below.
 *
 * SK_KEYMGMT is the one the tree never fetches for itself: see shadow_keymgmt.
 */
#define SK_MD      0
#define SK_CIPHER  1
#define SK_KEYMGMT 2
#define SK_N       3

static const char *const sk_fetch[SK_N] = {
    "EVP_MD_fetch", "EVP_CIPHER_fetch", "EVP_KEYMGMT_fetch"
};
static const char *const sk_free[SK_N] = {
    "EVP_MD_free", "EVP_CIPHER_free", "EVP_KEYMGMT_free"
};
static const char *const sk_prov[SK_N] = {
    "EVP_MD_get0_provider", "EVP_CIPHER_get0_provider",
    "EVP_KEYMGMT_get0_provider"
};

static int   g_shadow_state;
static void *g_shadow_ctx;
static int   g_shadow_busy;       /* the guard */
static int   g_shadow_reenter;    /* times the guard fired: expected 0 */
static char  g_cfg_path_raw[512]; /* first path that loaded, unsanitised */
static int   g_pq_pin;            /* fetch resolved the same in both contexts */
static int   g_pq_drift;          /* it did not */
static int   g_pq_nocmp;          /* nothing to compare: the fetch failed */
static char  g_pq_drift_at[80];   /* the first drift, reported */

static void *shadow_sym(const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (p == NULL) {
        fprintf(stderr, "probe: no next %s\n", name);
        abort();
    }
    return p;
}

/* Built once, at the first fetch, because that is the first moment the tree's
 * own configuration path is known -- the engine loads its configuration before
 * it fetches anything, and a tree that does not is already failing gate 1 for
 * that.  If the tree's path does not work here, the image's own is used, so
 * that declining to configure anything is not a way to switch this half of
 * gate 3 off. */
static void shadow_build(void)
{
    static const char *fallback = "/usr/local/ssl/fips.cnf";
    fn_void    ctxnew  = (fn_void)   shadow_sym("OSSL_LIB_CTX_new");
    fn_vp      ctxfree = (fn_vp)     shadow_sym("OSSL_LIB_CTX_free");
    fn_loadcfg loadcfg = (fn_loadcfg)shadow_sym("OSSL_LIB_CTX_load_config");
    fn_load    load    = (fn_load)   shadow_sym("OSSL_PROVIDER_load");
    fn_mark    setmark = (fn_mark)   shadow_sym("ERR_set_mark");
    fn_mark    popmark = (fn_mark)   shadow_sym("ERR_pop_to_mark");
    int i;

    g_shadow_state = SHADOW_DEAD;
    setmark();
    for (i = 0; i < 2; i++) {
        const char *path = (i == 0) ? g_cfg_path_raw : fallback;
        void *c;

        if (path[0] == '\0') continue;
        c = ctxnew();
        if (c == NULL) continue;
        /* "fips" FIRST.  That reversal is the whole point of this context. */
        if (loadcfg(c, path) == 1
                && load(c, "fips") != NULL && load(c, "default") != NULL) {
            g_shadow_ctx   = c;
            g_shadow_state = SHADOW_READY;
            break;
        }
        ctxfree(c);
    }
    popmark();
}

/* One comparison.  `real` is what the tree's own fetch returned; NULL means it
 * failed and there is nothing to compare, counted apart so that the accounting
 * still balances against the watched-call totals.  `kind` picks which trio of
 * libcrypto calls to use, and nothing else differs between the two sides --
 * same algorithm name, same query string.
 *
 * The symbols are resolved once per kind rather than once per call.  At 18
 * fetches a dlsym apiece did not matter; the key-operation half below is 1716
 * calls, and six dlsyms on each of them is work this instrument does not need
 * to do.  The memo is the same idiom the NEXT macro uses for the interposers.
 */
static void shadow_check(int kind, void *real, const char *alg,
                         const char *pq)
{
    static void *m_fetch[SK_N], *m_free[SK_N], *m_prov[SK_N];
    static fn_provname provname;
    static fn_mark     setmark, popmark;
    fn_fetch    fetch;
    fn_vp       objfree;
    fn_pp       getprov;
    void *shadow;

    if (g_shadow_state == SHADOW_OFF) {
        g_shadow_busy = 1;
        shadow_build();
        g_shadow_busy = 0;
    }
    if (g_shadow_state != SHADOW_READY) { g_pq_nocmp++; return; }
    if (real == NULL)                   { g_pq_nocmp++; return; }

    if (m_fetch[kind] == NULL) {
        m_fetch[kind] = shadow_sym(sk_fetch[kind]);
        m_free[kind]  = shadow_sym(sk_free[kind]);
        m_prov[kind]  = shadow_sym(sk_prov[kind]);
    }
    if (provname == NULL) {
        provname = (fn_provname)shadow_sym("OSSL_PROVIDER_get0_name");
        setmark  = (fn_mark)    shadow_sym("ERR_set_mark");
        popmark  = (fn_mark)    shadow_sym("ERR_pop_to_mark");
    }
    fetch   = (fn_fetch)m_fetch[kind];
    objfree = (fn_vp)   m_free[kind];
    getprov = (fn_pp)   m_prov[kind];

    g_shadow_busy = 1;
    setmark();
    shadow = fetch(g_shadow_ctx, alg, pq);
    {
        void *rp = getprov(real);
        void *sp = (shadow != NULL) ? getprov(shadow) : NULL;
        const char *rn = (rp != NULL) ? provname(rp) : NULL;
        const char *sn = (sp != NULL) ? provname(sp) : NULL;

        if (rn != NULL && sn != NULL && strcmp(rn, sn) == 0) {
            g_pq_pin++;
        } else {
            char at[80];
            g_pq_drift++;
            snprintf(at, sizeof at, "%s: %s vs %s", alg,
                     rn != NULL ? rn : "(none)", sn != NULL ? sn : "(none)");
            note_str(g_pq_drift_at, sizeof g_pq_drift_at, at);
        }
    }
    if (shadow != NULL) objfree(shadow);
    popmark();
    g_shadow_busy = 0;
}

/* EVP_Q_digest resolves its digest inside libcrypto, so there is no object for
 * the caller to compare.  The probe fetches it itself, on the library context
 * the call named and with the call's own query, and hands that to the
 * comparison above.  Neither fetch is visible to any counter in this file.
 * This engine has no EVP_Q_digest call site; the spelling is closed anyway,
 * because a port that moved its hashing to it would otherwise have moved that
 * hashing out of gate 3's reach. */
static void shadow_qdigest(void *libctx, const char *alg, const char *pq)
{
    fn_fetch fetch   = (fn_fetch)shadow_sym("EVP_MD_fetch");
    fn_vp    objfree = (fn_vp)   shadow_sym("EVP_MD_free");
    fn_mark  setmark = (fn_mark) shadow_sym("ERR_set_mark");
    fn_mark  popmark = (fn_mark) shadow_sym("ERR_pop_to_mark");
    void *real;

    g_shadow_busy = 1;
    setmark();
    real = fetch(libctx, alg, pq);
    popmark();
    g_shadow_busy = 0;

    shadow_check(SK_MD, real, alg, pq);

    if (real != NULL) {
        g_shadow_busy = 1;
        objfree(real);
        g_shadow_busy = 0;
    }
}

/* The key-operation half, and the half whose query text actually moves this
 * corpus.
 *
 * EVP_PKEY_CTX_new_from_name and EVP_PKEY_CTX_new_from_pkey hand back a
 * CONTEXT rather than an implementation, so there is no object of the tree's
 * for the comparison above to read a provider name off.  What those calls do
 * take is a property query, and the query they take is the query
 * EVP_KEYMGMT_fetch takes: building the context is what selects the key
 * management implementation the whole operation then runs on.  So the probe
 * fetches that keymgmt itself -- on the library context the call named, with
 * the call's own query -- and hands it to the comparison, exactly as
 * shadow_qdigest does for the digest EVP_Q_digest resolves internally.
 * Neither fetch moves a counter in this file.
 *
 * WHY THIS HALF AND NOT ONLY THE FETCHES
 *
 * A digest and a cipher compute the same bytes whichever provider answers;
 * what the FIPS module changes about them on this tree is nothing.  The
 * keymgmt is what decides whether a 1024-bit RSA key may be encrypted with and
 * whether an explicit-parameter EC curve is accepted at all, and those
 * decisions land at fromdata and encrypt_init, not at SHA256.  With the comparison on the fetches alone, a
 * tree that wrote "provider=default" on the two startup fetch tables and a
 * dummy on all 1716 of these pinned 18, drifted 0, named 1734 and passed --
 * with every key operation still decided by load order.
 *
 * `name` NULL is one outcome like any other: counted as not-comparable, so
 * that one outcome per watched call stays an equality the gate can check.
 */
static void shadow_keymgmt(void *libctx, const char *name, const char *pq)
{
    static fn_fetch fetch;
    static fn_vp    objfree;
    static fn_mark  setmark, popmark;
    void *real;

    if (name == NULL) { g_pq_nocmp++; return; }
    if (fetch == NULL) {
        fetch   = (fn_fetch)shadow_sym("EVP_KEYMGMT_fetch");
        objfree = (fn_vp)   shadow_sym("EVP_KEYMGMT_free");
        setmark = (fn_mark) shadow_sym("ERR_set_mark");
        popmark = (fn_mark) shadow_sym("ERR_pop_to_mark");
    }

    g_shadow_busy = 1;
    setmark();
    real = fetch(libctx, name, pq);
    popmark();
    g_shadow_busy = 0;

    shadow_check(SK_KEYMGMT, real, name, pq);

    if (real != NULL) {
        g_shadow_busy = 1;
        objfree(real);
        g_shadow_busy = 0;
    }
}

/* What _from_pkey named, asked of the key it was handed.  That call takes a
 * key and not a name, and the name the keymgmt fetch above needs is the key's
 * own type name -- EVP_PKEY_get0_type_name, published at 3.1.8 in
 * include/openssl/evp.h:1685, which returns the name of the key management
 * implementation the key is already on ("RSA", "EC").  A NULL key, or a key
 * whose type name libcrypto will not give, returns NULL and is counted as not
 * comparable rather than guessed at. */
static const char *pkey_type_name(void *pkey)
{
    static fn_provname typename_;
    const char *r;

    if (pkey == NULL) return NULL;
    if (typename_ == NULL)
        typename_ = (fn_provname)shadow_sym("EVP_PKEY_get0_type_name");
    g_shadow_busy = 1;
    r = typename_(pkey);
    g_shadow_busy = 0;
    return r;
}

void *OSSL_LIB_CTX_new(void)
{
    void *r;
    NEXT(next, fn_void, "OSSL_LIB_CTX_new")
    if (g_shadow_busy) { g_shadow_reenter++; return next(); }
    r = next();
    g_ctx_new[g_phase]++;
    if (r != NULL && g_nctx < MAXCTX) g_ctx[g_nctx++] = r;
    return r;
}

void OSSL_LIB_CTX_free(void *ctx)
{
    NEXT(next, fn_vp, "OSSL_LIB_CTX_free")
    if (g_shadow_busy) { g_shadow_reenter++; next(ctx); return; }
    g_ctx_free[g_phase]++;
    if (is_engine_ctx(ctx)) g_ctx_free_known++;
    next(ctx);
}

/* The configuration load.  3.0 gives a library context a configuration only
 * when it is asked to: OSSL_LIB_CTX_new hands back a context with nothing in
 * it, and the FIPS provider is handed its integrity MAC as configuration
 * data, so a context that never loads a file cannot load that provider no
 * matter how correct the file on disk is.  Which context and what the call
 * returned are both counted; the path is reported and not gated, because a
 * port that reaches the same file through a symlink or a relative path has
 * not done anything wrong. */
int OSSL_LIB_CTX_load_config(void *ctx, const char *config_file)
{
    int r;
    NEXT(next, fn_loadcfg, "OSSL_LIB_CTX_load_config")
    if (g_shadow_busy) { g_shadow_reenter++; return next(ctx, config_file); }
    g_cfg[g_phase]++;
    if (ctx == NULL)             g_cfg_null++;
    else if (is_engine_ctx(ctx)) g_cfg_engine++;
    else                         g_cfg_other++;
    note_str(g_cfg_path, sizeof g_cfg_path, config_file);
    r = next(ctx, config_file);
    if (r == 1) {
        g_cfg_ok++;
        if (is_engine_ctx(ctx)) g_cfg_ok_engine++;
        /* The same path again, unsanitised and untruncated, for the
         * counterfactual context to load.  g_cfg_path above is a report and
         * is squashed to printable ASCII for the JSON; this one has to still
         * open a file.  First one that worked wins, as there. */
        if (g_cfg_path_raw[0] == '\0' && config_file != NULL
                && strlen(config_file) < sizeof g_cfg_path_raw)
            strcpy(g_cfg_path_raw, config_file);
    }
    return r;
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
    if (g_shadow_busy) { g_shadow_reenter++; return next(ctx, name); }
    note_load_ctx(ctx);
    r = next(ctx, name);
    note_prov(r);
    note_prov_named(ctx, name, r);
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
    if (g_shadow_busy) { g_shadow_reenter++; return next(ctx, name, retain); }
    note_load_ctx(ctx);
    g_prov_try++;
    r = next(ctx, name, retain);
    note_prov(r);
    note_prov_named(ctx, name, r);
    return r;
}

int OSSL_PROVIDER_unload(void *prov)
{
    NEXT(next, fn_ip, "OSSL_PROVIDER_unload")
    if (g_shadow_busy) { g_shadow_reenter++; return next(prov); }
    if (prov == NULL) {
        g_prov_unload_null++;
    } else {
        g_prov_unload[g_phase]++;
        if (is_loaded_prov(prov)) g_prov_unload_known++;
        if (is_fips_prov(prov)) {
            g_fips_unload[g_phase]++;
            note_fips_unload(prov);
        }
    }
    return next(prov);
}

/* The self test, and the one place this probe lies.
 *
 * Everything else here counts and calls through.  This one consults the
 * fixture .preinit_array captured above, and if it was set to 1 it returns 0
 * without calling through -- the module is fine, the answer is not.
 * That is the only way to ask a tree whose self test passes what it would do
 * if the self test failed, and "what it would do" is the whole content of
 * "refuses to start on a module it cannot trust": on a tree that checks, the
 * run ends in on_tpm_fail and this probe reports ok 0; on a tree that calls
 * and ignores, the run finishes exactly as it did without the variable, and
 * the two are told apart without reading a line of the tree.
 *
 * Read once, in .preinit_array, rather than per call: the switch cannot
 * change under a tree mid-run -- there is nothing left to read by the time the
 * tree runs -- and a tree that self-tests twice gets one answer. */
int OSSL_PROVIDER_self_test(void *prov)
{
    int r;
    NEXT(next, fn_ip, "OSSL_PROVIDER_self_test")
    if (g_shadow_busy) { g_shadow_reenter++; return next(prov); }
    g_selftest[g_phase]++;
    if (is_loaded_prov(prov)) g_selftest_known++;
    if (is_fips_prov(prov))   g_selftest_fips++;
    if (g_forced) {
        g_selftest_forced++;
        return 0;
    }
    r = next(prov);
    if (r == 1) g_selftest_ok++;
    return r;
}

void *EVP_MD_fetch(void *ctx, const char *algorithm, const char *properties)
{
    void *r;
    NEXT(next, fn_fetch, "EVP_MD_fetch")
    if (g_shadow_busy) { g_shadow_reenter++; return next(ctx, algorithm, properties); }
    g_fetch[g_phase]++;
    if (ctx == NULL)             g_fetch_ctx_null++;
    else if (is_engine_ctx(ctx)) g_fetch_ctx_engine++;
    else                         g_fetch_ctx_other++;
    note_pq(properties, &g_pq_md_null, &g_pq_md_empty, &g_pq_md_set);
    r = next(ctx, algorithm, properties);
    note_md(r);
    shadow_check(SK_MD, r, algorithm, properties);
    return r;
}

/* The four ways libcrypto's EVP digest API is handed a digest to hash with.
 * EVP_DigestInit_ex is what the reference tree uses; _ex2 is the same call
 * with a parameter list instead of an ENGINE and is what a port written
 * against 3.0's own documentation is likely to use; EVP_DigestInit is the
 * two-argument spelling; EVP_Digest is the one-shot.  A port that hashes
 * through none of them is not hashing with an EVP_MD at all -- it is on the
 * low-level API, which is the retained no-state-peek gate's sentence and is
 * priced there. */

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
 * the retained digests-by-name gate's sentence forbids, written as one call.
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
    if (g_shadow_busy) { g_shadow_reenter++; return next(libctx, name, propq, data, datalen, md, mdlen); }
    g_qdigest++;
    shadow_qdigest(libctx, name, propq);
    note_pq(propq, &g_pq_q_null, &g_pq_q_empty, &g_pq_q_set);
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
    if (g_shadow_busy) { g_shadow_reenter++; next(md); return; }
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
    if (g_shadow_busy) { g_shadow_reenter++; return next(ctx, algorithm, properties); }
    obj_fetch(&g_ciph, ctx);
    note_pq(properties, &g_pq_ci_null, &g_pq_ci_empty, &g_pq_ci_set);
    r = next(ctx, algorithm, properties);
    obj_note(&g_ciph, r);
    shadow_check(SK_CIPHER, r, algorithm, properties);
    return r;
}

void EVP_CIPHER_free(void *c)
{
    NEXT(next, fn_vp, "EVP_CIPHER_free")
    if (g_shadow_busy) { g_shadow_reenter++; next(c); return; }
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


/* ------------------------------------ the interposers, step 05's share of them */

/* The live-context table is small on purpose.  Unlike a digest or a cipher, an
 * EVP_PKEY_CTX is created and destroyed per operation, so a table that only
 * grew would overflow in the first few rounds and every attribution after that
 * would be a guess.  Dropping the entry on free keeps it bounded by how many
 * the engine holds at once -- which is one, in any shape that is not leaking --
 * and it also stops a freed pointer's class from being read off a later
 * context that the allocator handed the same address to. */
static int pkctx_class(void *ctx)
{
    int i;
    if (ctx == NULL) return 'u';
    for (i = 0; i < g_npkctx; i++)
        if (g_pkctx[i] == ctx) return g_pkctx_cls[i];
    return 'u';
}

static void pkctx_note(void *ctx, void *libctx)
{
    char cls;

    /* A constructor that returned NULL made no context.  Nothing was
     * allocated, nothing can be freed, and nothing can be attributed to a
     * library context -- so the failed call is not counted as a construction
     * either, or a tree whose allocation failed would read as a tree that
     * leaked. */
    if (ctx == NULL) return;
    if (libctx == NULL)             { cls = 'n'; g_pkctx_ctx_null++;   }
    else if (is_engine_ctx(libctx)) { cls = 'e'; g_pkctx_ctx_engine++; }
    else                            { cls = 'o'; g_pkctx_ctx_other++;  }
    g_pkctx_new[g_phase]++;
    if (g_npkctx < MAXPKCTX) {
        g_pkctx[g_npkctx]       = ctx;
        g_pkctx_cls[g_npkctx++] = cls;
    } else {
        g_pkctx_over = 1;
    }
}

static void pkctx_drop(void *ctx)
{
    int i;
    if (ctx == NULL) return;
    for (i = 0; i < g_npkctx; i++)
        if (g_pkctx[i] == ctx) {
            g_pkctx[i]     = g_pkctx[g_npkctx - 1];
            g_pkctx_cls[i] = g_pkctx_cls[g_npkctx - 1];
            g_npkctx--;
            return;
        }
}

void *EVP_PKEY_CTX_new_from_name(void *libctx, const char *name,
                                 const char *propq)
{
    void *r;
    NEXT(next, fn_fetch, "EVP_PKEY_CTX_new_from_name")
    if (g_shadow_busy) { g_shadow_reenter++; return next(libctx, name, propq); }
    note_pq(propq, &g_pq_pkn_null, &g_pq_pkn_empty, &g_pq_pkn_set);
    r = next(libctx, name, propq);
    pkctx_note(r, libctx);
    /* One comparison per pkctx_note, unconditionally, so that pin + drift +
     * nocmp still equals the calls this probe watched. */
    shadow_keymgmt(libctx, name, propq);
    return r;
}

void *EVP_PKEY_CTX_new_from_pkey(void *libctx, void *pkey, const char *propq)
{
    void *r;
    NEXT(next, fn_ctxfrompkey, "EVP_PKEY_CTX_new_from_pkey")
    if (g_shadow_busy) { g_shadow_reenter++; return next(libctx, pkey, propq); }
    note_pq(propq, &g_pq_pkp_null, &g_pq_pkp_empty, &g_pq_pkp_set);
    r = next(libctx, pkey, propq);
    pkctx_note(r, libctx);
    shadow_keymgmt(libctx, pkey_type_name(pkey), propq);
    return r;
}

void *EVP_PKEY_CTX_new(void *pkey, void *e)
{
    NEXT(next, fn_ctxnew, "EVP_PKEY_CTX_new")
    g_pkctx_inherit[g_phase]++;
    g_pq_noq++;
    return next(pkey, e);
}

/* The same context, built from a NID instead of a key.  It is the pre-3.0
 * spelling, it takes no library context and no property query either, and it
 * counts as the one above does -- same counter, because it is the same event:
 * a key-operation context built by a spelling that cannot name an
 * implementation.  Gate 3's presence half is what reads that counter, and it
 * is the only instrument for these two.  The effect half leaves them alone on
 * purpose: with no query to repeat, both sides of the counterfactual would
 * resolve by load order and both would pin, which would read as a pass and
 * mean the opposite. */
void *EVP_PKEY_CTX_new_id(int id, void *e)
{
    NEXT(next, fn_ctxnewid, "EVP_PKEY_CTX_new_id")
    g_pkctx_inherit[g_phase]++;
    g_pq_noq++;
    return next(id, e);
}

void EVP_PKEY_CTX_free(void *ctx)
{
    NEXT(next, fn_vp, "EVP_PKEY_CTX_free")
    if (ctx != NULL) g_pkctx_free[g_phase]++;
    pkctx_drop(ctx);
    next(ctx);
}

int EVP_PKEY_fromdata(void *ctx, void **ppkey, int selection, void *params)
{
    int r;
    NEXT(next, fn_fromdata, "EVP_PKEY_fromdata")
    r = next(ctx, ppkey, selection, params);
    g_fromdata[g_phase]++;
    switch (pkctx_class(ctx)) {
    case 'e': g_fromdata_ctx_engine++;  break;
    case 'n': g_fromdata_ctx_null++;    break;
    case 'o': g_fromdata_ctx_other++;   break;
    default:  g_fromdata_ctx_unknown++; break;
    }
    return r;
}

void EVP_PKEY_free(void *pkey)
{
    NEXT(next, fn_vp, "EVP_PKEY_free")
    if (pkey != NULL) g_pkey_free[g_phase]++;
    next(pkey);
}

/* Only the call that carries an output buffer is counted.  The usual EVP idiom
 * calls sign twice -- once with a NULL buffer to be told the length, once to do
 * the work -- and a port that sizes its buffer from the key calls it once;
 * counting the buffered call makes both idioms produce one per operation, which
 * is what the rate gates divide by.  Verify has no such idiom and every call is
 * counted. */
int EVP_PKEY_sign(void *ctx, unsigned char *sig, size_t *siglen,
                  const unsigned char *tbs, size_t tbslen)
{
    NEXT(next, fn_pkop, "EVP_PKEY_sign")
    if (sig != NULL) g_pk_sign[g_phase]++;
    return next(ctx, sig, siglen, tbs, tbslen);
}

int EVP_PKEY_verify(void *ctx, const unsigned char *sig, size_t siglen,
                    const unsigned char *tbs, size_t tbslen)
{
    NEXT(next, fn_pkverify, "EVP_PKEY_verify")
    g_pk_verify[g_phase]++;
    if (g_asym_in == 'r')      g_pk_ver_rsa[g_phase]++;
    else if (g_asym_in == 'e') g_pk_ver_ec[g_phase]++;
    else                       g_pk_ver_none++;
    return next(ctx, sig, siglen, tbs, tbslen);
}

int EVP_PKEY_encrypt(void *ctx, unsigned char *out, size_t *outlen,
                     const unsigned char *in, size_t inlen)
{
    NEXT(next, fn_pkop, "EVP_PKEY_encrypt")
    if (out != NULL) g_pk_encrypt[g_phase]++;
    return next(ctx, out, outlen, in, inlen);
}

int EVP_PKEY_decrypt(void *ctx, unsigned char *out, size_t *outlen,
                     const unsigned char *in, size_t inlen)
{
    NEXT(next, fn_pkop, "EVP_PKEY_decrypt")
    if (out != NULL) g_pk_decrypt[g_phase]++;
    return next(ctx, out, outlen, in, inlen);
}

void *ECDSA_do_sign(const unsigned char *dgst, int dgstlen, void *eckey)
{
    NEXT(next, fn_dosign, "ECDSA_do_sign")
    g_low_ecdsa[g_phase]++;
    return next(dgst, dgstlen, eckey);
}

int ECDSA_do_verify(const unsigned char *dgst, int dgstlen, const void *sig,
                    void *eckey)
{
    NEXT(next, fn_doverify, "ECDSA_do_verify")
    g_low_ecdsa[g_phase]++;
    return next(dgst, dgstlen, sig, eckey);
}

int ECDSA_sign(int type, const unsigned char *dgst, int dgstlen,
               unsigned char *sig, unsigned int *siglen, void *eckey)
{
    NEXT(next, fn_sigsign, "ECDSA_sign")
    g_low_ecdsa[g_phase]++;
    return next(type, dgst, dgstlen, sig, siglen, eckey);
}

int ECDSA_verify(int type, const unsigned char *dgst, int dgstlen,
                 const unsigned char *sig, int siglen, void *eckey)
{
    NEXT(next, fn_sigverify, "ECDSA_verify")
    g_low_ecdsa[g_phase]++;
    return next(type, dgst, dgstlen, sig, siglen, eckey);
}

/* Reported, not gated -- see the header.  EC_KEY_new_by_curve_name calls
 * EC_KEY_new on its way through, so one construction can land in this counter
 * twice; the number is a signal for item 8, not a threshold for a gate. */
void *EC_KEY_new(void)
{
    NEXT(next, fn_void, "EC_KEY_new")
    g_low_eckey[g_phase]++;
    return next();
}

void *EC_KEY_new_by_curve_name(int nid)
{
    NEXT(next, fn_newcurve, "EC_KEY_new_by_curve_name")
    g_low_eckey[g_phase]++;
    return next(nid);
}

void *RSA_new(void)
{
    NEXT(next, fn_void, "RSA_new")
    g_low_rsanew[g_phase]++;
    return next();
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


/* --------------------------------------------------- the asymmetric workload */

/* The vectors.  Printed by build/gates/05-asymvec.py, which is kept beside this
 * file so that "not produced by the engine under test" is checkable; the five
 * comment lines below are that script's own self-checks, pasted with the
 * constants they certify.  The keys are build/suite/keys.json's NIST_P256_1,
 * BN_P256_1, NIST_P384_1, k1024e65537 and k2048e65537, so a vector here and a
 * case in the behaviour corpus are about the same key. */

/* NIST_P256  d*G == Q  ok, ecdsa verify ok, |r|=|s|=32 */
/* BN_P256    d*G == Q  ok, ecdsa verify ok, |r|=|s|=32 */
/* NIST_P384  d*G == Q  ok, ecdsa verify ok, |r|=|s|=48 */
static const char g_ec_p256_d[] =
    "c648aa1e5438f88b5c36dfaa064c12a7f23a7440ce2390028fad0fbd3ae086b7";

static const char g_ec_p256_qx[] =
    "82fb6f925b54aa3237dbfe8cabc004859ece413e49d0329f2999222876c93579";

static const char g_ec_p256_qy[] =
    "4903a59d2629d679d049b12a61ab51a34f2a9bab585e6f4ee7cdb96b9d0350b2";

static const char g_ec_p256_r[] =
    "ec156a4eac88fd09c35e9992d75f672640fe9caf3be2f082909147166cb1c7cb";

static const char g_ec_p256_s[] =
    "feed4bb6069fa385d06296bf3cf4f563140c0ab5e757398d7835c7faeba4e248";

static const char g_ec_bn256_d[] =
    "f91adf23315bd0cfd670a298743bc2fa322c5705ceb28e34aacdc4652338c438";

static const char g_ec_bn256_qx[] =
    "bdf86d7634afcfba05fae7c424f7a5d1152eef20c8fcd0f6d4c7a9eb16811a04";

static const char g_ec_bn256_qy[] =
    "0a47028810b86b04dd8813b7c61aab6bdcb4079f35458b78ccd7c94dc45ffe49";

static const char g_ec_bn256_r[] =
    "d2ea3e9504be890d915c9e7c3297243efbc9d536ef1cffd9d768233169fe7415";

static const char g_ec_bn256_s[] =
    "f98b8807fde24d350e47933f281f3ac535b129cb3ab5f003e941c26d985a7d5d";

static const char g_ec_p384_d[] =
    "8fcfa8cbf5f3473070339c537310563bf1f3a5a589755e4bd6cc95e0ecab215b"
    "8b88e5834b2e4e5190e2bf6f8f2ad357";

static const char g_ec_p384_qx[] =
    "5b98a82129a4e931457f2fc2f261881c515cd8395496c6d88c822d8bf7f419a6"
    "e7973a63e22dc2a4d03d39f0f0aa7415";

static const char g_ec_p384_qy[] =
    "1646d84613a9697f84fe2c64bad1e75fa4221562ce477d607b6f81356e9a480a"
    "f32dfb831e739ec5ec4803081a09c15c";

static const char g_ec_p384_r[] =
    "b0c032ec60dd9d5cff1bb205928c9971b7fc57d9233a9ff1fe6907450b74c27c"
    "9a41f620713a4b6fdf5c8cf41c292425";

static const char g_ec_p384_s[] =
    "531a21acdbfb1b2d04d3850f9921204ae34f7ad3ed8e6eb015ab1c2475b5c8d0"
    "09430417cf3a6ed1cb801b5ef3ea3848";

/* k1024e65537  n=128 bytes, e=65537, RSASSA ok, RSAPSS ok (salt 32) */
static const char g_rsa1024_n[] =
    "c4e18748bd7edce0bfac181b43f22b8ebc41d2c303c10ee28ad7940713ae23da"
    "d4b6784bbebb8ed4685292daceea1d16f7b0c9224396b2ad3214ca08d9e990f3"
    "e1e8facde1726d52c73ac60d810bab5893dd56b4cfd333fa8667629d5439bffc"
    "6bbbba27a6823d69fe77015cf34251cbf2d422a7b372474167f32a3959a13aaf";

static const char g_rsa1024_ssa[] =
    "0366f9176aa2b1e29919d0951db26e8fdce39e8779bccb988be0177dcc097058"
    "71ef18e706ec67d08d6868faad429f368b008deabd54b7941d917f59653c0da7"
    "40fb40d9ca3d8d71dd90d1bf2afad147dcbd3e88f0bd65309149395e2b53ccda"
    "ec5c61f6d5a93622f0598ed3504386d9d316e9db5f03fe879d3d1c3fd3a4aa71";

static const char g_rsa1024_pss[] =
    "822377a5075ff3e179dffe90c4d617e8dc533d2cda16315f2ea70a6b62237dc6"
    "188c87582ab3d16afa97ec23ac1262746367bf05341c66c11ff442d9bd6bcc83"
    "af25aea0f7a2061f0c9353fd85269b10541dc1888cf0b7a3de7aa26c93b0d5ac"
    "18040b6581b4c733f65614bf4648b2fb4f843582bab933981b4459d15794b51e";

/* k2048e65537  n=256 bytes, e=65537, RSASSA ok, RSAPSS ok (salt 32) */
static const char g_rsa2048_n[] =
    "a0d0b6bafbefe13fb5668856b56a6027432b0fc5b8e1dec2d23ba68e2d93ed70"
    "7f2a3930cf7e6b981a182c558ac161e8667fd7d1130d8d4a1d9b959709bbced3"
    "4bd122fc60cff92440a2bee703ea8efa9b7002c6dd6e224335eb3376d323ac60"
    "8502a64bab5910d113103fba465925c026f086e1472a21b730bac1aa2e946e6b"
    "24876a86e2ecdd2602c439b070d598d020be75243774e68a534621826b8735a7"
    "90249c3a9d31bd870835ccc6b0d44ede44f30a2fda5d27643d523fa11a36f613"
    "fad2c91ee20dd7c67af1b142911e8bffc0b743a71d6748f31077cea5324e7c45"
    "c0cd83cf89675d279adb66715572e2f4138fb5fb653c2baa9fb81288d8514fdf";

static const char g_rsa2048_ssa[] =
    "32e40857b9bb55ff0eb00a1058da4c096f429df89785533083188d9515c24cee"
    "dd2d994ce1f73200c60663a90c8b8a7dce9cc861d074352da50d056d1d761063"
    "cba9d6a1c975676c7907d8b3ce2e8be84b9389019f00140ac830b358088c8c89"
    "ab6fcebb2e1b6383a5206b5ff49cbe52f1097f7b1487ad79dac20721748978fb"
    "b531aba8d8bd1eb2203bd895f575fc7370ed9ee022cc08ec4688dfb5a055e125"
    "4cae93187ceaf10ab6d942ba1a7acc7661e4e3ee41c843779c77d295f654128f"
    "3f97cedb7b122198366e4ef011112a0c1963976fc0ba5f981ac60d92a7b748a6"
    "9030a4e9295909517ef56102c13edcc638ea7bf5a2da5db27ca19484657b57d7";

static const char g_rsa2048_pss[] =
    "51bc94379386ae4e551dfb7d9c3485ad51d90aaa90a001c8d87ce89643204573"
    "6ce5049f284ac67e0ab5ec21e0e1b9d7b4cfbf91c56cf942a885e6cb4cadfed1"
    "a6412b023510708d07bc1d7846e9ccaaae6a748d65a6b3034d19caef7f58534f"
    "ca618400e6c2ba1f80493cb2398c117f8f5eb88a317695e176f505260e2deba4"
    "010fee00a04cc6d9252d8a738fc1be3ca51024a4b6afda1d15ab132eb073afe3"
    "c6ff09b513d4390e6a0234c598b967023bc9b0d4ea992e54807c263bf581b9b8"
    "70ecc715e6bca5199aeeace0a48ca4a590e39321fcac872646c5aa825f849b40"
    "2bfcf9809600937f2732e1df3914b6ac9a85fce52702fa93ef16ff7ed960dba0";

static const char g_dig256[] =
    "030e19242f3a45505b66717c87929da8b3bec9d4dfeaf5000b16212c37424d58";

static const char g_dig384[] =
    "05121f2c394653606d7a8794a1aebbc8d5e2effc091623303d4a5764717e8b98"
    "a5b2bfccd9e6f3000d1a2734414e5b68";


/* An RSA modulus does not fit in PB2B, whose 256 bytes are the TPM2B the hash
 * half needs; MAX_RSA_KEY_BYTES is 256 exactly, so this one is spelled at the
 * size the header names rather than at a size that happens to be enough. */
typedef union {
    struct { UINT16 size; BYTE buffer[MAX_RSA_KEY_BYTES]; } t;
    TPM2B b;
} RB2B;

static int unhex(const char *h, BYTE *out, int max)
{
    unsigned v;
    int      n = 0;

    for (; h[0] != '\0' && h[1] != '\0' && n < max; h += 2) {
        if (sscanf(h, "%2x", &v) != 1) return -1;
        out[n++] = (BYTE)v;
    }
    return n;
}

#ifdef TPM_ALG_ECDSA
static void ecc_set(TPM2B_ECC_PARAMETER *p, const char *hex)
{
    int n = unhex(hex, p->t.buffer, (int)sizeof(p->t.buffer));
    p->t.size = (UINT16)(n < 0 ? 0 : n);
}
#endif

/* One asymmetric round: the four entry points step 05 moves onto EVP, thirteen
 * operations in all -- two ECDSA signatures, three ECDSA verifications, and
 * four RSA operations on each of two keys.
 *
 * Signing gets a digest that varies with the round and verification gets the
 * exact digest its signature was made over.  That asymmetry is deliberate: a
 * fixed sign input would let a port answer the second round from whatever it
 * worked out in the first, and a varying verify input would not verify.
 *
 * hashAlg is TPM_ALG_SHA256 for every curve and is not a mistake.  Neither
 * SignEcdsa nor ValidateSignatureEcdsa is given it -- _cpri__SignEcc's ECDSA
 * case calls SignEcdsa(rOut, sOut, curveId, dIn, digest) at CpriECC.c:1600 and
 * the validator dispatches the same five arguments -- so what selects the
 * arithmetic is the digest's length against the curve's order, which is why
 * the P-384 vector carries a 48-byte digest. */
static void asym_round(int salt)
{
    PB2B dg;

#ifdef TPM_ALG_ECDSA
    {
        /* R = [d]G on each of the three curves.  This is the only thing in the
         * workload that reaches EccCurveInit; see the note beside g_ecpm_try.
         *
         * BN_P256 is the one that matters most.  It has no NID at all, so a
         * group built from its parameters comes back with no curve name --
         * which is how ecg_unnamed tells a port that built the curve from its
         * parameters apart from one that looked a name up and pushed that. */
        static const struct {
            TPM_ECC_CURVE curve;
            const char   *d, *qx, *qy;
        } pm[] = {
            { TPM_ECC_NIST_P256, g_ec_p256_d,  g_ec_p256_qx,  g_ec_p256_qy  },
            { TPM_ECC_BN_P256,   g_ec_bn256_d, g_ec_bn256_qx, g_ec_bn256_qy },
            { TPM_ECC_NIST_P384, g_ec_p384_d,  g_ec_p384_qx,  g_ec_p384_qy  },
        };
        TPM2B_ECC_PARAMETER d;
        TPMS_ECC_POINT      R, q;
        int                 i;

        for (i = 0; i < (int)(sizeof(pm) / sizeof(pm[0])); i++) {
            ecc_set(&d, pm[i].d);
            memset(&q, 0, sizeof(q));
            ecc_set(&q.x, pm[i].qx);
            ecc_set(&q.y, pm[i].qy);
            memset(&R, 0, sizeof(R));
            g_ecpm_try[g_phase]++;
            if (_cpri__EccPointMultiply(&R, pm[i].curve, &d, NULL, NULL)
                    == CRYPT_SUCCESS) {
                g_ecpm_ok[g_phase]++;
                if (R.x.t.size == q.x.t.size && R.y.t.size == q.y.t.size
                        && memcmp(R.x.t.buffer, q.x.t.buffer,
                                  q.x.t.size) == 0
                        && memcmp(R.y.t.buffer, q.y.t.buffer,
                                  q.y.t.size) == 0)
                    g_ecpm_match[g_phase]++;
            }
        }
    }
#endif

#ifdef TPM_ALG_ECDSA
    {
        /* Three curves are verified against and only two are signed with,
         * and the missing one is recorded behaviour rather than taste.
         * SignEcdsa pAsserts curveData->n->size <= MAX_ECC_PARAMETER_BYTES
         * (CpriECC.c:1077) and OsslCryptoEngine.h:83 makes that bound 32, so
         * asking this engine for a P-384 signature is a fatal TPM failure and
         * always has been: the frozen corpus writes it down itself, as
         * `eccsv/ok/007 tpmfail=4' in build/record/expected.txt, and
         * build/suite/cases.txt has no eccsign ECDSA case on any curve.
         * ValidateSignatureEcdsa carries no such bound -- the corpus scores
         * six eccverify ECDSA NIST_P384 cases -- so P-384 stays on the verify
         * side, where its 48-byte r and s are exactly what a port that sized a
         * buffer at 32 bytes will trip over.
         *
         * BN_P256 is here because it has no OpenSSL NID at all.  EccCurveInit
         * (CpriECC.c:181) builds every group from explicit p, a, b, G, n and h
         * through EC_GROUP_new_curve_GFp, and names no NID anywhere; a port
         * that quietly rewrote that table as a set of NIDs would still answer
         * for the two NIST curves and could not answer for this one. */
        static const struct {
            TPM_ECC_CURVE curve;
            const char   *d, *qx, *qy, *r, *s, *dig;
            int           sign;
        } ec[] = {
            { TPM_ECC_NIST_P256, g_ec_p256_d, g_ec_p256_qx, g_ec_p256_qy,
              g_ec_p256_r, g_ec_p256_s, g_dig256, 1 },
            { TPM_ECC_BN_P256, g_ec_bn256_d, g_ec_bn256_qx, g_ec_bn256_qy,
              g_ec_bn256_r, g_ec_bn256_s, g_dig256, 1 },
            { TPM_ECC_NIST_P384, g_ec_p384_d, g_ec_p384_qx, g_ec_p384_qy,
              g_ec_p384_r, g_ec_p384_s, g_dig384, 0 },
        };
        TPM2B_ECC_PARAMETER d, r, s, ro, so;
        TPMS_ECC_POINT      q;
        int                 i, n;

        for (i = 0; i < (int)(sizeof(ec) / sizeof(ec[0])); i++) {
            if (ec[i].sign) {
                memset(&dg, 0, sizeof(dg));
                n = unhex(ec[i].dig, dg.t.buffer, (int)sizeof(dg.t.buffer));
                dg.t.size      = (UINT16)(n < 0 ? 0 : n);
                dg.t.buffer[0] = (BYTE)(dg.t.buffer[0] ^ (BYTE)salt);
                ecc_set(&d, ec[i].d);
                memset(&ro, 0, sizeof(ro));
                memset(&so, 0, sizeof(so));
                g_asym_try[g_phase]++;
                g_asym_in = 'e';
                if (_cpri__SignEcc(&ro, &so, TPM_ALG_ECDSA, TPM_ALG_SHA256,
                                   ec[i].curve, &d, &dg.b, NULL)
                        == CRYPT_SUCCESS)
                    g_asym_ops[g_phase]++;
                g_asym_in = 0;
            }

            memset(&dg, 0, sizeof(dg));
            n = unhex(ec[i].dig, dg.t.buffer, (int)sizeof(dg.t.buffer));
            dg.t.size = (UINT16)(n < 0 ? 0 : n);
            ecc_set(&r, ec[i].r);
            ecc_set(&s, ec[i].s);
            memset(&q, 0, sizeof(q));
            ecc_set(&q.x, ec[i].qx);
            ecc_set(&q.y, ec[i].qy);
            g_asym_try[g_phase]++;
            g_asym_in = 'e';
            if (_cpri__ValidateSignatureEcc(&r, &s, TPM_ALG_ECDSA,
                                            TPM_ALG_SHA256, ec[i].curve,
                                            &q, &dg.b) == CRYPT_SUCCESS)
                g_asym_ops[g_phase]++;
            g_asym_in = 0;
        }
    }
#endif

#ifdef TPM_ALG_RSA
    {
        static const struct {
            const char *n, *ssa, *pss;
        } rk[] = {
            { g_rsa1024_n, g_rsa1024_ssa, g_rsa1024_pss },
            { g_rsa2048_n, g_rsa2048_ssa, g_rsa2048_pss },
        };
        RSA_KEY key;
        RB2B    pub, priv;
        BYTE    sig[MAX_RSA_KEY_BYTES], ct[MAX_RSA_KEY_BYTES], msg[32];
        UINT32  csz;
        int     i, k, n;

        memset(&dg, 0, sizeof(dg));
        n = unhex(g_dig256, dg.t.buffer, (int)sizeof(dg.t.buffer));
        dg.t.size = (UINT16)(n < 0 ? 0 : n);
        for (i = 0; i < (int)sizeof(msg); i++) msg[i] = (BYTE)(i * 5 + salt);

        for (k = 0; k < (int)(sizeof(rk) / sizeof(rk[0])); k++) {
            /* The public half only.  _cpri__ValidateSignatureRSA and
             * _cpri__EncryptRSA are both given a zero-size private exponent by
             * cpridrv (cpridrv.c:853, :982) because neither needs one, and a
             * probe that handed them one would be driving a different call
             * than the corpus does. */
            memset(&priv, 0, sizeof(priv));
            memset(&pub, 0, sizeof(pub));
            n = unhex(rk[k].n, pub.t.buffer, (int)sizeof(pub.t.buffer));
            pub.t.size     = (UINT16)(n < 0 ? 0 : n);
            priv.t.size    = 0;
            key.exponent   = 65537;
            key.publicKey  = &pub.b;
            key.privateKey = &priv.b;

            n = unhex(rk[k].ssa, sig, (int)sizeof(sig));
            g_asym_try[g_phase]++;
            g_asym_in = 'r';
            if (_cpri__ValidateSignatureRSA(&key, TPM_ALG_RSASSA,
                                            TPM_ALG_SHA256, dg.t.size,
                                            dg.t.buffer, (UINT32)n, sig, 0)
                    == CRYPT_SUCCESS)
                g_asym_ops[g_phase]++;
            g_asym_in = 0;

            /* saltSize 0 is "whatever the signer used", which is what
             * PssDecode's own comment calls a variable size salt
             * (CpriRSA.c:1171 skips the length check when it is zero).  The
             * vector was made with a 32-byte salt and re-decoded under exactly
             * this convention before it was written down. */
            n = unhex(rk[k].pss, sig, (int)sizeof(sig));
            g_asym_try[g_phase]++;
            g_asym_in = 'r';
            if (_cpri__ValidateSignatureRSA(&key, TPM_ALG_RSAPSS,
                                            TPM_ALG_SHA256, dg.t.size,
                                            dg.t.buffer, (UINT32)n, sig, 0)
                    == CRYPT_SUCCESS)
                g_asym_ops[g_phase]++;
            g_asym_in = 0;

            /* OAEP carries a label, and the label is the detail this call is
             * here for: OaepEncode hashes strlen(label) + 1 bytes
             * (CpriRSA.c:727), the terminating NUL included, so a port that
             * hands OpenSSL strlen(label) produces a ciphertext the engine's
             * own decoder will not take back. */
            csz = (UINT32)sizeof(ct);
            g_asym_try[g_phase]++;
            g_asym_in = 'r';
            if (_cpri__EncryptRSA(&csz, ct, &key, TPM_ALG_OAEP,
                                  (UINT32)sizeof(msg), msg, TPM_ALG_SHA256,
                                  "probe") == CRYPT_SUCCESS)
                g_asym_ops[g_phase]++;
            g_asym_in = 0;

            csz = (UINT32)sizeof(ct);
            g_asym_try[g_phase]++;
            g_asym_in = 'r';
            if (_cpri__EncryptRSA(&csz, ct, &key, TPM_ALG_RSAES,
                                  (UINT32)sizeof(msg), msg, TPM_ALG_SHA256,
                                  NULL) == CRYPT_SUCCESS)
                g_asym_ops[g_phase]++;
            g_asym_in = 0;
        }
    }
#endif
}

static void workload(int rounds)
{
    int r, a;
    for (r = 0; r < rounds; r++) {
        for (a = 0; a < NALG; a++)
            round_for(g_algs[a], r + a);
        sym_round(r);
        asym_round(r);
    }
}

/* ==================================== step 07's instruments ============== *
 *
 * Step 07 asks for two things and both of them are invisible to a scan for
 * exactly the same reason: the old spelling and the new spelling reach the
 * same provider code, produce the same bytes, and differ only in what crossed
 * the boundary on the way.  A tree that kept every typed setter and a tree
 * that replaced all nine with parameter lists pass every behaviour case in
 * this corpus identically.  So the instrument has to count calls, read the
 * lists, and watch one allocation.
 *
 * WHAT THE INTERPOSER CAN SEE, AND WHY IT CAN
 *
 * This was measured before the gates were written, because on 3.x the typed setters are implemented ON the parameter machinery
 * and it is not obvious that the two routes stay apart at a public symbol.
 * They do.  EVP_PKEY_CTX_set_rsa_padding() does not reach the public
 * EVP_PKEY_CTX_ctrl() or the public EVP_PKEY_CTX_set_params() through the PLT;
 * libcrypto binds its internal calls locally.  So typed_total below is a count
 * of the tree's own calls and of nothing else, and the same measurement says
 * EC_GROUP_new_from_params() contributes nothing to new_curve_GFp or to
 * set_generator.
 *
 * THE PARAMETER ROUTE HAS TWO SPELLINGS AND BOTH ARE RIGHT
 *
 * EVP_PKEY_encrypt_init_ex(ctx, params) and EVP_PKEY_encrypt_init(ctx)
 * followed by EVP_PKEY_CTX_set_params(ctx, params) are equivalent -- section
 * 11.1.6 measured every ctrl against both -- so they are counted apart and
 * added wherever a gate reads them.  A gate that picked one would be gating a
 * preference.
 *
 * WHY THE LISTS ARE READ AND NOT JUST COUNTED
 *
 * "The parameters cross as a list" is satisfied by an empty list.  What the
 * step actually requires is that the list says what the nine ctrl calls said,
 * so every list handed to any of the three entry points is walked and its keys
 * are tallied.  Two of the values matter as well as the keys:
 *
 *   - saltlen.  The parameter takes the sentinel as a number or as a string
 *     and the strings are not synonyms.  Measured on this image inside
 *     m30v:step07env -- sign a SHA-256 digest with a digest-length salt, then
 *     verify it four times through the parameter route: "auto" 1, "digest" 1,
 *     "auto-digestmax" 1, "max" 0.  This engine's PssEncode clamps the salt to
 *     the digest's length, so "max" (the longest the modulus leaves room for)
 *     rejects this engine's own signatures outright, and "digest" accepts
 *     those while rejecting every other salt length it verifies today.
 *     sl_digest and sl_max count those two apart, sl_auto counts the two
 *     spellings that do mean the sentinel, and sl_other counts anything else
 *     so that a value nobody anticipated is visible rather than read as AUTO
 *     because it was not "digest".
 *
 *   - the EC parameter list's shape.  A port can hand
 *     EC_GROUP_new_from_params() a group NAME instead of the curve's
 *     parameters, which works for the two NIST curves and fails for BN_P256 --
 *     libcrypto has no NID for it, which is the fact EccKeyFromData's own
 *     comment already records.  ecg_byname and ecg_explicit tell those apart,
 *     and ecg_ok against ecg_call catches the failure without having to guess
 *     which curve it was.
 *
 * THE ALLOCATION
 *
 * EVP_PKEY_CTX_set0_rsa_oaep_label() takes the caller's buffer.  A parameter
 * list COPIES it -- measured by freeing the buffer before
 * EVP_PKEY_encrypt and getting the same ciphertext.  So the memdup at
 * CpriRSA.c:1199 is load-bearing for the ctrl and is dead weight for the
 * parameter list, and a rewrite that keeps it without adding a free leaks once
 * per OAEP encryption -- a defect no behaviour case can see, because the
 * ciphertext is right.
 *
 * dup_live is that leak.  It is stated as a leak and not as a matching free on
 * purpose: the best answer to the ownership flip is to stop allocating at all
 * and push the caller's label straight into the list, and a gate that demanded
 * a free would fail the best answer.  Section 11.2.3 measured that libcrypto's
 * own allocations do not cross the PLT, so this table sees only allocations
 * libCryptoEngine.so asked for itself.
 *
 * Five entry points feed it: CRYPTO_memdup, which is what this tree writes
 * today; CRYPTO_malloc and CRYPTO_zalloc, which are what a port reaches for
 * once it has to produce a buffer of its own -- OPENSSL_malloc + memcpy is
 * the obvious rewrite of a memdup; and CRYPTO_strdup and CRYPTO_strndup,
 * which are that same copy spelled for a C string.  The label IS one, and
 * both the old body and the golden size it strlen(label) + 1, so
 * OPENSSL_strdup(label) hands back the very buffer OPENSSL_memdup(label,
 * lSize) does.  A counter that watched one of the five would be pricing a
 * spelling and not the leak.
 *
 * None of them can count an allocation twice.  Disassembled on this release,
 * CRYPTO_memdup, CRYPTO_strdup, CRYPTO_strndup and CRYPTO_zalloc each reach
 * CRYPTO_malloc by a direct call inside libcrypto.so -- call 22a8f0
 * <CRYPTO_malloc>, not a PLT slot -- so the inner call never leaves the
 * library and never arrives here.  That is the same fact from the other side:
 * it is why interposing CRYPTO_malloc alone did not see a strdup.
 *
 * CRYPTO_realloc is deliberately NOT interposed: the hash sequence table
 * grows through it and is designed to stay live across calls, so counting it
 * would fail the tree for something this step does not ask about.  Its first
 * allocation is an OPENSSL_realloc(NULL, ...) and does not cross the malloc
 * interposer either.
 */

typedef void *(*fn_ecgfromp)(const void *, void *, const char *);
typedef void *(*fn_ecgfp)(const void *, const void *, const void *, void *);
typedef int   (*fn_ecgsetgen)(void *, const void *, const void *, const void *);
typedef void *(*fn_ecgtop)(const void *, void *, const char *, void *);
typedef int   (*fn_ecgname)(const void *);
typedef int   (*fn_pkparams)(void *, const void *);
typedef int   (*fn_pksetint)(void *, int);
typedef int   (*fn_pksetptr)(void *, const void *);
typedef int   (*fn_pklabel)(void *, void *, int);
typedef void *(*fn_memdup)(const void *, size_t, const char *, int);
typedef void *(*fn_cmalloc)(size_t, const char *, int);
typedef char *(*fn_cstrdup)(const char *, const char *, int);
typedef char *(*fn_cstrndup)(const char *, size_t, const char *, int);
typedef void  (*fn_cfree)(void *, const char *, int);
typedef void  (*fn_cclrfree)(void *, size_t, const char *, int);

/* ---- the EC group half --------------------------------------------------
 *
 * ecg_ok_engine is the conjunction -- a group came back, AND the context it
 * was asked for came from this engine -- for the reason cfg_ok_engine is one
 * at step 06: two counters a gate has to hope are about the same call are not
 * the same thing as one counter that says they were.
 *
 * The library context matters here in a way step 06's gate cannot cover.
 * Section 11.1.4 measured EC_GROUP_new_from_params() to be pure libcrypto: it
 * succeeds identically on a context with no provider in it at all.  So this
 * call will not fail for a tree that passes NULL, and nothing downstream will
 * either -- the group is a plain structure.  Step 06's property-query gate
 * predates the symbol and does not know to look at it.  If step 07 does not
 * gate the argument, nothing does, and the engine acquires a call into
 * libcrypto that quietly does not belong to its context.
 */
static int   g_ecg_call;        /* EC_GROUP_new_from_params, total */
static int   g_ecg_ok;          /* of those, returned a group */
static int   g_ecg_engine;      /* on a library context this engine made */
static int   g_ecg_null;        /* on the process default */
static int   g_ecg_other;       /* on some third context -- reported only */
static int   g_ecg_ok_engine;   /* returned a group AND on this engine's ctx */
static int   g_ecg_pq_null;
static int   g_ecg_pq_empty;
static int   g_ecg_pq_set;
static char  g_ecg_pq_text[64]; /* the first non-empty one, reported */
static int   g_ecg_explicit;    /* the list gave p, a, b, generator and order */
static int   g_ecg_byname;      /* the list gave a group name */
static int   g_ecg_named;       /* the group that came back has a curve name */
static int   g_ecg_unnamed;     /* ... and the ones that do not: BN_P256 */
static int   g_ecg_gfp;         /* EC_GROUP_new_curve_GFp -- the hand route */
static int   g_ecg_setgen;      /* EC_GROUP_set_generator -- the hand route */
static int   g_ecg_topar;       /* EC_GROUP_to_params -- reported, not gated */

/* ---- the RSA parameter half --------------------------------------------- */
static int   g_rsa_pad;         /* EVP_PKEY_CTX_set_rsa_padding */
static int   g_rsa_oaepmd;      /* EVP_PKEY_CTX_set_rsa_oaep_md */
static int   g_rsa_mgf1md;      /* EVP_PKEY_CTX_set_rsa_mgf1_md */
static int   g_rsa_label;       /* EVP_PKEY_CTX_set0_rsa_oaep_label */
static int   g_rsa_saltlen;     /* EVP_PKEY_CTX_set_rsa_pss_saltlen */
static int   g_rsa_sigmd;       /* EVP_PKEY_CTX_set_signature_md */
static int   g_pl_setparams;    /* EVP_PKEY_CTX_set_params */
static int   g_pl_encinit;      /* EVP_PKEY_encrypt_init_ex */
static int   g_pl_vfyinit;      /* EVP_PKEY_verify_init_ex */
static int   g_pl_empty;        /* a list entry point reached with NULL or {} */
static int   g_pl_keys;         /* parameters seen across every list */

/* keys, tallied across every list handed to any of the three entry points */
static int   g_pk_pad;
static int   g_pk_digest;
static int   g_pk_mgf1;
static int   g_pk_label;
static int   g_pk_saltlen;
static int   g_pk_other;        /* anything else -- reported, never gated */
/* values that decide something */
static int   g_pv_oaep;         /* pad-mode "oaep" or RSA_PKCS1_OAEP_PADDING */
static int   g_pv_pss;
static int   g_pv_pkcs1;
static int   g_pv_sl_int;       /* saltlen as a length: a number >= 0 */
static int   g_pv_sl_auto;      /* AUTO: -2, -4, "auto", "auto-digestmax" */
static int   g_pv_sl_digest;    /* DIGEST: -1 or "digest" -- a different one */
static int   g_pv_sl_max;       /* MAX: -3 or "max" -- a different one again */
static int   g_pv_sl_other;     /* anything else -- reported, never gated */
static char  g_pv_pad_text[32]; /* the first pad-mode string, reported */

/* ---- the allocation ----------------------------------------------------- */
#define MAXDUP 64
static void *g_dup[MAXDUP];
static int   g_ndup;            /* live entries in the table */
static int   g_dup_calls;       /* allocations from this engine, all five ways */
static int   g_dup_freed;       /* frees of a pointer this table was holding */
static int   g_dup_over;        /* more live at once than the table holds */

static void dup_note(void *p)
{
    if (p == NULL) return;
    g_dup_calls++;
    if (g_ndup >= MAXDUP) { g_dup_over++; return; }
    g_dup[g_ndup++] = p;
}

static void dup_drop(void *p)
{
    int i;
    if (p == NULL) return;
    for (i = 0; i < g_ndup; i++)
        if (g_dup[i] == p) {
            g_dup[i] = g_dup[--g_ndup];
            g_dup_freed++;
            return;
        }
}

/* Walks one list.  Everything here is a read of the caller's own array; the
 * probe never rewrites a parameter, because a gate that changed what the tree
 * asked for would be measuring itself. */
static void note_list(const void *v)
{
    const OSSL_PARAM *p = (const OSSL_PARAM *)v;
    if (p == NULL || p->key == NULL) { g_pl_empty++; return; }
    for (; p->key != NULL; p++) {
        g_pl_keys++;
        if (strcmp(p->key, OSSL_ASYM_CIPHER_PARAM_PAD_MODE) == 0) {
            g_pk_pad++;
            if (p->data_type == OSSL_PARAM_UTF8_STRING && p->data != NULL) {
                const char *s = (const char *)p->data;
                if (g_pv_pad_text[0] == '\0')
                    snprintf(g_pv_pad_text, sizeof(g_pv_pad_text), "%s", s);
                if (strcmp(s, OSSL_PKEY_RSA_PAD_MODE_OAEP) == 0)    g_pv_oaep++;
                else if (strcmp(s, OSSL_PKEY_RSA_PAD_MODE_PSS) == 0) g_pv_pss++;
                else if (strcmp(s, OSSL_PKEY_RSA_PAD_MODE_PKCSV15) == 0)
                    g_pv_pkcs1++;
            } else if ((p->data_type == OSSL_PARAM_INTEGER
                            || p->data_type == OSSL_PARAM_UNSIGNED_INTEGER)
                           && p->data != NULL && p->data_size >= sizeof(int)) {
                int n = *(const int *)p->data;
                if (n == 4)      g_pv_oaep++;   /* RSA_PKCS1_OAEP_PADDING */
                else if (n == 6) g_pv_pss++;    /* RSA_PKCS1_PSS_PADDING  */
                else if (n == 1) g_pv_pkcs1++;  /* RSA_PKCS1_PADDING      */
            }
        } else if (strcmp(p->key, OSSL_ASYM_CIPHER_PARAM_OAEP_LABEL) == 0) {
            g_pk_label++;
        } else if (strcmp(p->key, OSSL_SIGNATURE_PARAM_MGF1_DIGEST) == 0) {
            g_pk_mgf1++;
        } else if (strcmp(p->key, OSSL_SIGNATURE_PARAM_PSS_SALTLEN) == 0) {
            g_pk_saltlen++;
            /* Four sentinels share this parameter and only two spellings of
             * one of them mean what the ctrl this step removes meant.  Each
             * is classified by what it MEANS, in both the string form and
             * the number form, and a value that is none of them lands in
             * sl_other rather than being read as AUTO by default. */
            if (p->data_type == OSSL_PARAM_UTF8_STRING && p->data != NULL) {
                const char *s = (const char *)p->data;
                if (strcmp(s, OSSL_PKEY_RSA_PSS_SALT_LEN_DIGEST) == 0)
                    g_pv_sl_digest++;
                else if (strcmp(s, OSSL_PKEY_RSA_PSS_SALT_LEN_MAX) == 0)
                    g_pv_sl_max++;
                else if (strcmp(s, OSSL_PKEY_RSA_PSS_SALT_LEN_AUTO) == 0
                             || strcmp(s,
                                  OSSL_PKEY_RSA_PSS_SALT_LEN_AUTO_DIGEST_MAX)
                                == 0)
                    g_pv_sl_auto++;
                else
                    g_pv_sl_other++;
            } else if ((p->data_type == OSSL_PARAM_INTEGER
                            || p->data_type == OSSL_PARAM_UNSIGNED_INTEGER)
                           && p->data != NULL && p->data_size >= sizeof(int)) {
                int n = *(const int *)p->data;
                if (n >= 0)                                   g_pv_sl_int++;
                else if (n == RSA_PSS_SALTLEN_DIGEST)         g_pv_sl_digest++;
                else if (n == RSA_PSS_SALTLEN_AUTO)           g_pv_sl_auto++;
                else if (n == RSA_PSS_SALTLEN_MAX)            g_pv_sl_max++;
                else if (n == RSA_PSS_SALTLEN_AUTO_DIGEST_MAX) g_pv_sl_auto++;
                else                                          g_pv_sl_other++;
            } else {
                g_pv_sl_other++;
            }
        } else if (strcmp(p->key, OSSL_SIGNATURE_PARAM_DIGEST) == 0) {
            g_pk_digest++;
        } else {
            g_pk_other++;
        }
    }
}

/* The EC list is read for its shape only: whether the curve arrived as its
 * parameters or as a name.  Which name, and which parameters, is what
 * ecg_named and the behaviour corpus answer between them. */
static void note_ec_list(const void *v)
{
    const OSSL_PARAM *p = (const OSSL_PARAM *)v;
    int p_seen = 0, gen_seen = 0, ord_seen = 0, name_seen = 0;
    if (p == NULL) return;
    for (; p->key != NULL; p++) {
        if (strcmp(p->key, OSSL_PKEY_PARAM_EC_P) == 0)              p_seen = 1;
        else if (strcmp(p->key, OSSL_PKEY_PARAM_EC_GENERATOR) == 0) gen_seen = 1;
        else if (strcmp(p->key, OSSL_PKEY_PARAM_EC_ORDER) == 0)     ord_seen = 1;
        else if (strcmp(p->key, OSSL_PKEY_PARAM_GROUP_NAME) == 0)   name_seen = 1;
    }
    if (p_seen && gen_seen && ord_seen) g_ecg_explicit++;
    if (name_seen)                      g_ecg_byname++;
}

void *EC_GROUP_new_from_params(const void *params, void *libctx,
                               const char *propq)
{
    void *g;
    NEXT(next, fn_ecgfromp, "EC_GROUP_new_from_params")
    if (g_shadow_busy) return next(params, libctx, propq);
    g_ecg_call++;
    if (libctx == NULL)             g_ecg_null++;
    else if (is_engine_ctx(libctx)) g_ecg_engine++;
    else                            g_ecg_other++;
    if (propq == NULL)       g_ecg_pq_null++;
    else if (*propq == '\0') g_ecg_pq_empty++;
    else {
        g_ecg_pq_set++;
        if (g_ecg_pq_text[0] == '\0')
            snprintf(g_ecg_pq_text, sizeof(g_ecg_pq_text), "%s", propq);
    }
    note_ec_list(params);
    g = next(params, libctx, propq);
    if (g != NULL) {
        g_ecg_ok++;
        if (is_engine_ctx(libctx)) g_ecg_ok_engine++;
        {
            NEXT(name, fn_ecgname, "EC_GROUP_get_curve_name")
            if (name(g) != 0) g_ecg_named++;   /* NID_undef is 0 */
            else              g_ecg_unnamed++;
        }
    }
    return g;
}

void *EC_GROUP_new_curve_GFp(const void *p, const void *a, const void *b,
                             void *ctx)
{
    NEXT(next, fn_ecgfp, "EC_GROUP_new_curve_GFp")
    if (!g_shadow_busy) g_ecg_gfp++;
    return next(p, a, b, ctx);
}

int EC_GROUP_set_generator(void *g, const void *P, const void *n,
                           const void *h)
{
    NEXT(next, fn_ecgsetgen, "EC_GROUP_set_generator")
    if (!g_shadow_busy) g_ecg_setgen++;
    return next(g, P, n, h);
}

void *EC_GROUP_to_params(const void *g, void *libctx, const char *propq,
                         void *bnctx)
{
    NEXT(next, fn_ecgtop, "EC_GROUP_to_params")
    if (!g_shadow_busy) g_ecg_topar++;
    return next(g, libctx, propq, bnctx);
}

int EVP_PKEY_CTX_set_params(void *ctx, const void *params)
{
    NEXT(next, fn_pkparams, "EVP_PKEY_CTX_set_params")
    if (g_shadow_busy) return next(ctx, params);
    g_pl_setparams++;
    note_list(params);
    return next(ctx, params);
}

int EVP_PKEY_encrypt_init_ex(void *ctx, const void *params)
{
    NEXT(next, fn_pkparams, "EVP_PKEY_encrypt_init_ex")
    if (g_shadow_busy) return next(ctx, params);
    g_pl_encinit++;
    note_list(params);
    return next(ctx, params);
}

int EVP_PKEY_verify_init_ex(void *ctx, const void *params)
{
    NEXT(next, fn_pkparams, "EVP_PKEY_verify_init_ex")
    if (g_shadow_busy) return next(ctx, params);
    g_pl_vfyinit++;
    note_list(params);
    return next(ctx, params);
}

int EVP_PKEY_CTX_set_rsa_padding(void *ctx, int pad)
{
    NEXT(next, fn_pksetint, "EVP_PKEY_CTX_set_rsa_padding")
    if (!g_shadow_busy) g_rsa_pad++;
    return next(ctx, pad);
}

int EVP_PKEY_CTX_set_rsa_pss_saltlen(void *ctx, int len)
{
    NEXT(next, fn_pksetint, "EVP_PKEY_CTX_set_rsa_pss_saltlen")
    if (!g_shadow_busy) g_rsa_saltlen++;
    return next(ctx, len);
}

int EVP_PKEY_CTX_set_rsa_oaep_md(void *ctx, const void *md)
{
    NEXT(next, fn_pksetptr, "EVP_PKEY_CTX_set_rsa_oaep_md")
    if (!g_shadow_busy) g_rsa_oaepmd++;
    return next(ctx, md);
}

int EVP_PKEY_CTX_set_rsa_mgf1_md(void *ctx, const void *md)
{
    NEXT(next, fn_pksetptr, "EVP_PKEY_CTX_set_rsa_mgf1_md")
    if (!g_shadow_busy) g_rsa_mgf1md++;
    return next(ctx, md);
}

int EVP_PKEY_CTX_set_signature_md(void *ctx, const void *md)
{
    NEXT(next, fn_pksetptr, "EVP_PKEY_CTX_set_signature_md")
    if (!g_shadow_busy) g_rsa_sigmd++;
    return next(ctx, md);
}

int EVP_PKEY_CTX_set0_rsa_oaep_label(void *ctx, void *label, int len)
{
    int r;
    NEXT(next, fn_pklabel, "EVP_PKEY_CTX_set0_rsa_oaep_label")
    if (g_shadow_busy) return next(ctx, label, len);
    g_rsa_label++;
    r = next(ctx, label, len);
    /* set0 takes the buffer on success, so a pointer this probe was holding
     * stops being the engine's to free.  Dropping it here is what keeps the
     * leak counter a statement about the tree and not about the API: a tree
     * that still uses the ctrl leaks nothing, and should read dup_live 0. */
    if (r > 0) dup_drop(label);
    return r;
}

void *CRYPTO_memdup(const void *data, size_t siz, const char *file, int line)
{
    void *p;
    NEXT(next, fn_memdup, "CRYPTO_memdup")
    p = next(data, siz, file, line);
    if (!g_shadow_busy) dup_note(p);
    return p;
}

/* The same table, reached through the two plain allocators.  OPENSSL_malloc +
 * memcpy is what a memdup becomes when somebody rewrites it by hand, and a
 * leak counter that watched memdup alone would be counting a spelling. */
void *CRYPTO_malloc(size_t siz, const char *file, int line)
{
    void *p;
    NEXT(next, fn_cmalloc, "CRYPTO_malloc")
    p = next(siz, file, line);
    if (!g_shadow_busy) dup_note(p);
    return p;
}

void *CRYPTO_zalloc(size_t siz, const char *file, int line)
{
    void *p;
    NEXT(next, fn_cmalloc, "CRYPTO_zalloc")
    p = next(siz, file, line);
    if (!g_shadow_busy) dup_note(p);
    return p;
}

/* And the same table again, reached the way a C string asks for it.  The OAEP
 * label is NUL-terminated and is copied strlen(label) + 1 bytes at a time, so
 * OPENSSL_strdup(label) is the same buffer as OPENSSL_memdup(label, lSize) and
 * is the more natural thing to write.  Neither of these forwards through the
 * CRYPTO_malloc interposer above -- libcrypto calls its own malloc directly --
 * so this is the only place the copy can be seen, and it is counted once. */
char *CRYPTO_strdup(const char *str, const char *file, int line)
{
    char *p;
    NEXT(next, fn_cstrdup, "CRYPTO_strdup")
    p = next(str, file, line);
    if (!g_shadow_busy) dup_note(p);
    return p;
}

char *CRYPTO_strndup(const char *str, size_t siz, const char *file, int line)
{
    char *p;
    NEXT(next, fn_cstrndup, "CRYPTO_strndup")
    p = next(str, siz, file, line);
    if (!g_shadow_busy) dup_note(p);
    return p;
}

void CRYPTO_free(void *p, const char *file, int line)
{
    NEXT(next, fn_cfree, "CRYPTO_free")
    if (!g_shadow_busy) dup_drop(p);
    next(p, file, line);
}

void CRYPTO_clear_free(void *p, size_t siz, const char *file, int line)
{
    NEXT(next, fn_cclrfree, "CRYPTO_clear_free")
    if (!g_shadow_busy) dup_drop(p);
    next(p, siz, file, line);
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
        g_phase = PH_INIT;  g_init_rc = _cpri__InitCryptoUnits(&on_tpm_fail);
        /* A startup that reports failure is a startup that did not happen, and
         * driving a workload through an engine that has just said so measures
         * nothing.  Every tree on this ladder returns CRYPT_SUCCESS here, so
         * this branch changes no number any inherited gate reads; it exists
         * because step 06 asks what a tree does when a self test fails, and
         * FAIL() is one of the two honest answers to that -- refusing at the
         * return is the other, and an instrument that only recognised the
         * first would be gating a spelling. */
        if (g_init_rc != CRYPT_SUCCESS) {
            ok = 0;
        } else {
            g_phase = PH_WARM;  workload(WARM_ROUNDS);
            g_phase = PH_LONG;  workload(LONG_ROUNDS);
            g_phase = PH_STOP;  _cpri__StopCryptoUnits();
        }
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
    fprintf(out,
        "  \"asym_try\": %d,\n"
        "  \"asym_try_long\": %d,\n"
        "  \"asym_ops\": %d,\n"
        "  \"asym_ops_warm\": %d,\n"
        "  \"asym_ops_long\": %d,\n"
        "  \"ecpm_try\": %d,\n"
        "  \"ecpm_ok\": %d,\n"
        "  \"ecpm_match\": %d,\n"
        "  \"pkctx_new\": %d,\n"
        "  \"pkctx_new_long\": %d,\n"
        "  \"pkctx_ctx_engine\": %d,\n"
        "  \"pkctx_ctx_null\": %d,\n"
        "  \"pkctx_ctx_other\": %d,\n"
        "  \"pkctx_inherit\": %d,\n"
        "  \"pkctx_free\": %d,\n"
        "  \"pkctx_free_long\": %d,\n"
        "  \"pkctx_live\": %d,\n"
        "  \"pkctx_over\": %d,\n"
        "  \"fromdata\": %d,\n"
        "  \"fromdata_warm\": %d,\n"
        "  \"fromdata_long\": %d,\n"
        "  \"fromdata_ctx_engine\": %d,\n"
        "  \"fromdata_ctx_null\": %d,\n"
        "  \"fromdata_ctx_other\": %d,\n"
        "  \"fromdata_ctx_unknown\": %d,\n"
        "  \"pkey_free\": %d,\n"
        "  \"pk_sign\": %d,\n"
        "  \"pk_sign_long\": %d,\n"
        "  \"pk_verify\": %d,\n"
        "  \"pk_verify_long\": %d,\n"
        "  \"pk_verify_rsa\": %d,\n"
        "  \"pk_verify_rsa_long\": %d,\n"
        "  \"pk_verify_ec\": %d,\n"
        "  \"pk_verify_ec_long\": %d,\n"
        "  \"pk_verify_none\": %d,\n"
        "  \"pk_encrypt\": %d,\n"
        "  \"pk_encrypt_long\": %d,\n"
        "  \"pk_decrypt\": %d,\n"
        "  \"pk_decrypt_long\": %d,\n"
        "  \"low_ecdsa\": %d,\n"
        "  \"low_ecdsa_warm\": %d,\n"
        "  \"low_ecdsa_long\": %d,\n"
        "  \"low_eckey\": %d,\n"
        "  \"low_rsanew\": %d,\n",
        sum(g_asym_try), g_asym_try[PH_LONG],
        sum(g_asym_ops), g_asym_ops[PH_WARM], g_asym_ops[PH_LONG],
        sum(g_ecpm_try), sum(g_ecpm_ok), sum(g_ecpm_match),
        sum(g_pkctx_new), g_pkctx_new[PH_LONG],
        g_pkctx_ctx_engine, g_pkctx_ctx_null, g_pkctx_ctx_other,
        sum(g_pkctx_inherit), sum(g_pkctx_free), g_pkctx_free[PH_LONG],
        sum(g_pkctx_new) + sum(g_pkctx_inherit) - sum(g_pkctx_free),
        g_pkctx_over,
        sum(g_fromdata), g_fromdata[PH_WARM], g_fromdata[PH_LONG],
        g_fromdata_ctx_engine, g_fromdata_ctx_null, g_fromdata_ctx_other,
        g_fromdata_ctx_unknown, sum(g_pkey_free),
        sum(g_pk_sign), g_pk_sign[PH_LONG],
        sum(g_pk_verify), g_pk_verify[PH_LONG],
        sum(g_pk_ver_rsa), g_pk_ver_rsa[PH_LONG],
        sum(g_pk_ver_ec), g_pk_ver_ec[PH_LONG], g_pk_ver_none,
        sum(g_pk_encrypt), g_pk_encrypt[PH_LONG],
        sum(g_pk_decrypt), g_pk_decrypt[PH_LONG],
        sum(g_low_ecdsa), g_low_ecdsa[PH_WARM], g_low_ecdsa[PH_LONG],
        sum(g_low_eckey), sum(g_low_rsanew));
    fprintf(out,
        "  \"cfg_load\": %d,\n"
        "  \"cfg_load_at_init\": %d,\n"
        "  \"cfg_ctx_engine\": %d,\n"
        "  \"cfg_ctx_null\": %d,\n"
        "  \"cfg_ctx_other\": %d,\n"
        "  \"cfg_ok\": %d,\n"
        "  \"cfg_ok_engine\": %d,\n"
        "  \"cfg_path\": \"%s\",\n"
        "  \"prov_fips\": %d,\n"
        "  \"prov_fips_ok\": %d,\n"
        "  \"prov_fips_engine\": %d,\n"
        "  \"prov_fips_ok_engine\": %d,\n"
        "  \"prov_default\": %d,\n"
        "  \"prov_base\": %d,\n"
        "  \"fips_distinct\": %d,\n"
        "  \"fips_unload\": %d,\n"
        "  \"fips_unload_at_stop\": %d,\n"
        "  \"fips_unload_before_stop\": %d,\n"
        "  \"fips_live\": %d,\n"
        "  \"init_rc\": %d,\n"
        "  \"selftest\": %d,\n"
        "  \"selftest_at_init\": %d,\n"
        "  \"selftest_known\": %d,\n"
        "  \"selftest_fips\": %d,\n"
        "  \"selftest_ok\": %d,\n"
        "  \"selftest_forced\": %d,\n"
        "  \"pq_md_null\": %d,\n"
        "  \"pq_md_empty\": %d,\n"
        "  \"pq_md_set\": %d,\n"
        "  \"pq_cipher_null\": %d,\n"
        "  \"pq_cipher_empty\": %d,\n"
        "  \"pq_cipher_set\": %d,\n"
        "  \"pq_pkey_name_null\": %d,\n"
        "  \"pq_pkey_name_empty\": %d,\n"
        "  \"pq_pkey_name_set\": %d,\n"
        "  \"pq_pkey_pkey_null\": %d,\n"
        "  \"pq_pkey_pkey_empty\": %d,\n"
        "  \"pq_pkey_pkey_set\": %d,\n"
        "  \"pq_qdigest_null\": %d,\n"
        "  \"pq_qdigest_empty\": %d,\n"
        "  \"pq_qdigest_set\": %d,\n"
        "  \"pq_noquery\": %d,\n"
        "  \"pq_null\": %d,\n"
        "  \"pq_empty\": %d,\n"
        "  \"pq_set\": %d,\n"
        "  \"pq_text\": \"%s\",\n"
        "  \"pq_shadow\": %d,\n"
        "  \"pq_pin\": %d,\n"
        "  \"pq_drift\": %d,\n"
        "  \"pq_nocmp\": %d,\n"
        "  \"pq_reenter\": %d,\n"
        "  \"pq_drift_at\": \"%s\",\n",
        sum(g_cfg), g_cfg[PH_INIT],
        g_cfg_engine, g_cfg_null, g_cfg_other, g_cfg_ok, g_cfg_ok_engine,
        g_cfg_path,
        g_prov_fips, g_prov_fips_ok, g_prov_fips_engine, g_prov_fips_ok_engine,
        g_prov_dflt, g_prov_base, g_nfips,
        sum(g_fips_unload), g_fips_unload[PH_STOP],
        sum(g_fips_unload) - g_fips_unload[PH_STOP],
        fips_live(),
        g_init_rc,
        sum(g_selftest), g_selftest[PH_INIT], g_selftest_known,
        g_selftest_fips, g_selftest_ok, g_selftest_forced,
        g_pq_md_null,  g_pq_md_empty,  g_pq_md_set,
        g_pq_ci_null,  g_pq_ci_empty,  g_pq_ci_set,
        g_pq_pkn_null, g_pq_pkn_empty, g_pq_pkn_set,
        g_pq_pkp_null, g_pq_pkp_empty, g_pq_pkp_set,
        g_pq_q_null,   g_pq_q_empty,   g_pq_q_set,
        g_pq_noq,
        /* A key-operation context built through a spelling with nowhere to
         * put a query has named nothing, which at the fetch is what a NULL
         * query is, so it is folded in here rather than counted beside. */
        g_pq_md_null + g_pq_ci_null + g_pq_pkn_null + g_pq_pkp_null
            + g_pq_q_null + g_pq_noq,
        g_pq_empty_total(),
        g_pq_md_set + g_pq_ci_set + g_pq_pkn_set + g_pq_pkp_set + g_pq_q_set,
        g_pq_text,
        g_shadow_state == SHADOW_READY,
        g_pq_pin, g_pq_drift, g_pq_nocmp, g_shadow_reenter, g_pq_drift_at);
    fprintf(out,
        "  \"ecg_call\": %d,\n"
        "  \"ecg_ok\": %d,\n"
        "  \"ecg_engine\": %d,\n"
        "  \"ecg_null\": %d,\n"
        "  \"ecg_other\": %d,\n"
        "  \"ecg_ok_engine\": %d,\n"
        "  \"ecg_pq_null\": %d,\n"
        "  \"ecg_pq_empty\": %d,\n"
        "  \"ecg_pq_set\": %d,\n"
        "  \"ecg_pq_text\": \"%s\",\n"
        "  \"ecg_explicit\": %d,\n"
        "  \"ecg_byname\": %d,\n"
        "  \"ecg_named\": %d,\n"
        "  \"ecg_unnamed\": %d,\n"
        "  \"ecg_gfp\": %d,\n"
        "  \"ecg_setgen\": %d,\n"
        "  \"ecg_topar\": %d,\n"
        "  \"rsa_typed_pad\": %d,\n"
        "  \"rsa_typed_oaepmd\": %d,\n"
        "  \"rsa_typed_mgf1md\": %d,\n"
        "  \"rsa_typed_label\": %d,\n"
        "  \"rsa_typed_saltlen\": %d,\n"
        "  \"rsa_typed_sigmd\": %d,\n"
        "  \"rsa_typed\": %d,\n"
        "  \"pl_setparams\": %d,\n"
        "  \"pl_encinit\": %d,\n"
        "  \"pl_vfyinit\": %d,\n"
        "  \"pl_total\": %d,\n"
        "  \"pl_empty\": %d,\n"
        "  \"pl_keys\": %d,\n"
        "  \"pk_pad\": %d,\n"
        "  \"pk_digest\": %d,\n"
        "  \"pk_mgf1\": %d,\n"
        "  \"pk_label\": %d,\n"
        "  \"pk_saltlen\": %d,\n"
        "  \"pk_other\": %d,\n"
        "  \"pv_oaep\": %d,\n"
        "  \"pv_pss\": %d,\n"
        "  \"pv_pkcs1\": %d,\n"
        "  \"pv_pad_text\": \"%s\",\n"
        "  \"pv_sl_int\": %d,\n"
        "  \"pv_sl_auto\": %d,\n"
        "  \"pv_sl_digest\": %d,\n"
        "  \"pv_sl_max\": %d,\n"
        "  \"pv_sl_other\": %d,\n"
        "  \"dup_calls\": %d,\n"
        "  \"dup_freed\": %d,\n"
        "  \"dup_live\": %d,\n"
        "  \"dup_over\": %d,\n",
        g_ecg_call, g_ecg_ok, g_ecg_engine, g_ecg_null, g_ecg_other,
        g_ecg_ok_engine,
        g_ecg_pq_null, g_ecg_pq_empty, g_ecg_pq_set, g_ecg_pq_text,
        g_ecg_explicit, g_ecg_byname, g_ecg_named, g_ecg_unnamed,
        g_ecg_gfp, g_ecg_setgen, g_ecg_topar,
        g_rsa_pad, g_rsa_oaepmd, g_rsa_mgf1md, g_rsa_label, g_rsa_saltlen,
        g_rsa_sigmd,
        g_rsa_pad + g_rsa_oaepmd + g_rsa_mgf1md + g_rsa_label + g_rsa_saltlen
            + g_rsa_sigmd,
        g_pl_setparams, g_pl_encinit, g_pl_vfyinit,
        g_pl_setparams + g_pl_encinit + g_pl_vfyinit,
        g_pl_empty, g_pl_keys,
        g_pk_pad, g_pk_digest, g_pk_mgf1, g_pk_label, g_pk_saltlen, g_pk_other,
        g_pv_oaep, g_pv_pss, g_pv_pkcs1, g_pv_pad_text,
        g_pv_sl_int, g_pv_sl_auto, g_pv_sl_digest, g_pv_sl_max,
        g_pv_sl_other,
        g_dup_calls, g_dup_freed, g_ndup, g_dup_over);
    emit(out, "ciph", &g_ciph);
    /* Last, so that no key above it needs a trailing comma decision. */
    fprintf(out, "  \"nbits\": %d\n}\n", NBITS);
    fclose(out);
    return ok ? 0 : 1;
}
