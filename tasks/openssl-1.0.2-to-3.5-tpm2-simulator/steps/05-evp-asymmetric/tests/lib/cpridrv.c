/* cpridrv.c - behavior-suite driver for the TPM CryptoEngine public API.
 *
 * This is author tooling.  It never ships to the agent: it lives in each
 * step's verifier image, is built against the tree the agent produced, and
 * replays a case file whose expected answers were recorded from the untouched
 * baseline.
 *
 * It binds to the library exactly the way the TPM layer does: one
 * #include "CryptoEngine.h", the include path of the public headers, and
 * -lCryptoEngine -lplatform.  Nothing internal to OsslCryptoEngine is touched,
 * so the driver keeps compiling across every step of the migration as long as
 * the public interface is preserved -- which is itself part of what is scored.
 *
 *   usage: cpridrv <cases> <out> [first-case-index]
 *
 * Reads <cases>, skips the first [first-case-index] cases, writes one result
 * line per case to <out>, flushing after each.  A case that puts the engine
 * into failure mode (pAssert / FAIL) is caught and reported as "tpmfail"; a
 * case that takes the process down is reported by the runner, which restarts
 * the driver just past the line that died.
 *
 * Case line:   <id> <op> <arg>...
 * Result line: <id> <field>=<value>...
 *
 * Argument conventions, uniform across every op:
 *   "NULL" -> a null pointer            "-" -> an empty (size 0) buffer
 *   otherwise a lowercase hex string, or a decimal/0x integer, or a symbolic
 *   TPM_ALG_/TPM_ECC_ name without the prefix ("SHA256", "NIST_P256").
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#include "CryptoEngine.h"

#define MAXB      4096
#define MAXTOK    64

typedef union {
    struct {
        UINT16 size;
        BYTE   buffer[MAXB];
    } t;
    TPM2B b;
} B2;

static FILE        *g_out;
static sigjmp_buf   g_jmp;
static volatile int g_armed;
static volatile int g_failcode;
static volatile int g_signal;

/* ---------------------------------------------------------------- output */

static char g_line[65536];
static int  g_len;
static int  g_trunc;   /* a case-file argument was longer than its buffer */

static void emit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_len += vsnprintf(g_line + g_len, sizeof(g_line) - g_len, fmt, ap);
    va_end(ap);
    if (g_len > (int)sizeof(g_line) - 1)
        g_len = (int)sizeof(g_line) - 1;
}

static void emith(const char *name, const void *p, int n)
{
    const BYTE *b = (const BYTE *)p;
    int i;

    if (p == NULL) {
        emit(" %s=NULL", name);
        return;
    }
    if (n <= 0) {
        emit(" %s=-", name);
        return;
    }
    emit(" %s=", name);
    for (i = 0; i < n; i++)
        emit("%02x", b[i]);
}

static void emit2b(const char *name, const TPM2B *b)
{
    if (b == NULL) {
        emit(" %s=NULL", name);
        return;
    }
    emith(name, b->buffer, b->size);
}

/* -------------------------------------------------------------- failures */

static void on_tpm_fail(const char *function, int line, int code)
{
    /* The function name and line number move whenever the engine is
     * refactored, so only the error code is recorded: it is the part that
     * says what the library decided about the input. */
    (void)function;
    (void)line;
    g_failcode = code;
    if (g_armed)
        siglongjmp(g_jmp, 1);
    abort();
}

static void on_signal(int sig)
{
    g_signal = sig;
    if (g_armed)
        siglongjmp(g_jmp, 2);
    _exit(128 + sig);
}

/* ------------------------------------------------------------- arguments */

static char       *g_tok[MAXTOK];
static int         g_ntok;

static const char *arg(int i)
{
    return (i >= 0 && i < g_ntok) ? g_tok[i] : NULL;
}

static int isnull(const char *s)
{
    return s == NULL || strcmp(s, "NULL") == 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Returns the byte count, or -1 for a null pointer request.
 *
 * The whole buffer is cleared, not just the bytes that were decoded.  Several
 * engine entry points read past the size they were handed -- _math__Comp reads
 * a[0] before it looks at aSize -- and reading uninitialised stack would make
 * the recorded expectation depend on whatever the previous case left behind. */
static int unhex(const char *s, BYTE *o, int max)
{
    int n, i;

    memset(o, 0, (size_t)max);
    if (isnull(s)) return -1;
    if (strcmp(s, "-") == 0) return 0;
    n = (int)strlen(s) / 2;
    /* Silent truncation would turn a case-file mistake into a plausible-looking
     * expectation, so it is reported in the result line instead. */
    if (n > max) { n = max; g_trunc = 1; }
    for (i = 0; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        o[i] = (BYTE)((hi << 4) | lo);
    }
    return n;
}

static TPM2B *arg2b(B2 *b, int i)
{
    int n = unhex(arg(i), b->t.buffer, MAXB);
    if (n < 0) return NULL;
    b->t.size = (UINT16)n;
    return &b->b;
}

static TPM2B_ECC_PARAMETER *argecc(TPM2B_ECC_PARAMETER *p, int i)
{
    int n = unhex(arg(i), p->t.buffer, MAX_ECC_KEY_BYTES);
    if (n < 0) return NULL;
    p->t.size = (UINT16)n;
    return p;
}

static TPMS_ECC_POINT *argpt(TPMS_ECC_POINT *q, int i)
{
    if (isnull(arg(i))) return NULL;
    memset(q, 0, sizeof(*q));
    argecc(&q->x, i);
    argecc(&q->y, i + 1);
    return q;
}

static const char *arglabel(int i)
{
    const char *s = arg(i);
    return isnull(s) ? NULL : s;
}

/* The names carry the TCG-assigned numeric values rather than the build's
 * macros, because this build enables only a subset of the spec (no SHA-512,
 * SM3, SM4, Camellia, ECMQV or KDF2) and the suite has to be able to *feed*
 * a disabled algorithm to see it refused.  The values are fixed by TPM 2.0
 * Part 2 Table 9 and Part 2 Table 10; the assertions below pin the ones this
 * build does enable to those literals, so a renumbering could not slip past. */
static const struct {
    const char *name;
    int         value;
} g_names[] = {
    { "ERROR",          0x0000 },
    { "RSA",            0x0001 },
    { "SHA1",           0x0004 },
    { "HMAC",           0x0005 },
    { "AES",            0x0006 },
    { "MGF1",           0x0007 },
    { "KEYEDHASH",      0x0008 },
    { "XOR",            0x000A },
    { "SHA256",         0x000B },
    { "SHA384",         0x000C },
    { "SHA512",         0x000D },
    { "NULL_ALG",       0x0010 },
    { "SM3_256",        0x0012 },
    { "SM4",            0x0013 },
    { "RSASSA",         0x0014 },
    { "RSAES",          0x0015 },
    { "RSAPSS",         0x0016 },
    { "OAEP",           0x0017 },
    { "ECDSA",          0x0018 },
    { "ECDH",           0x0019 },
    { "ECDAA",          0x001A },
    { "SM2",            0x001B },
    { "ECSCHNORR",      0x001C },
    { "ECMQV",          0x001D },
    { "KDF1_SP800_56A", 0x0020 },
    { "KDF2",           0x0021 },
    { "KDF1_SP800_108", 0x0022 },
    { "ECC",            0x0023 },
    { "SYMCIPHER",      0x0025 },
    { "CAMELLIA",       0x0026 },
    { "CTR",            0x0040 },
    { "OFB",            0x0041 },
    { "CBC",            0x0042 },
    { "CFB",            0x0043 },
    { "ECB",            0x0044 },
    { "NONE",           0x0000 },
    { "NIST_P192",      0x0001 },
    { "NIST_P224",      0x0002 },
    { "NIST_P256",      0x0003 },
    { "NIST_P384",      0x0004 },
    { "NIST_P521",      0x0005 },
    { "BN_P256",        0x0010 },
    { "BN_P638",        0x0011 },
    { "SM2_P256",       0x0020 },
    { NULL,             0 },
};

#define CHECK_ID(m, v) \
    typedef char assert_##m[(m) == (v) ? 1 : -1]
CHECK_ID(TPM_ALG_RSA,              0x0001);
CHECK_ID(TPM_ALG_SHA1,             0x0004);
CHECK_ID(TPM_ALG_HMAC,             0x0005);
CHECK_ID(TPM_ALG_AES,              0x0006);
CHECK_ID(TPM_ALG_MGF1,             0x0007);
CHECK_ID(TPM_ALG_XOR,              0x000A);
CHECK_ID(TPM_ALG_SHA256,           0x000B);
CHECK_ID(TPM_ALG_SHA384,           0x000C);
CHECK_ID(TPM_ALG_NULL,             0x0010);
CHECK_ID(TPM_ALG_RSASSA,           0x0014);
CHECK_ID(TPM_ALG_RSAES,            0x0015);
CHECK_ID(TPM_ALG_RSAPSS,           0x0016);
CHECK_ID(TPM_ALG_OAEP,             0x0017);
CHECK_ID(TPM_ALG_ECDSA,            0x0018);
CHECK_ID(TPM_ALG_ECDH,             0x0019);
CHECK_ID(TPM_ALG_ECDAA,            0x001A);
CHECK_ID(TPM_ALG_SM2,              0x001B);
CHECK_ID(TPM_ALG_ECSCHNORR,        0x001C);
CHECK_ID(TPM_ALG_KDF1_SP800_56A,   0x0020);
CHECK_ID(TPM_ALG_KDF1_SP800_108,   0x0022);
CHECK_ID(TPM_ALG_ECC,              0x0023);
CHECK_ID(TPM_ALG_CTR,              0x0040);
CHECK_ID(TPM_ALG_OFB,              0x0041);
CHECK_ID(TPM_ALG_CBC,              0x0042);
CHECK_ID(TPM_ALG_CFB,              0x0043);
CHECK_ID(TPM_ALG_ECB,              0x0044);
CHECK_ID(TPM_ECC_NIST_P256,        0x0003);
CHECK_ID(TPM_ECC_NIST_P384,        0x0004);
CHECK_ID(TPM_ECC_BN_P256,          0x0010);
CHECK_ID(MAX_ECC_KEY_BYTES,        48);
CHECK_ID(MAX_RSA_KEY_BYTES,        256);

static long argnum(int i)
{
    const char *s = arg(i);
    int         k;

    if (s == NULL) return 0;
    for (k = 0; g_names[k].name != NULL; k++)
        if (strcmp(s, g_names[k].name) == 0)
            return g_names[k].value;
    return strtol(s, NULL, 0);
}

/* ------------------------------------------------------------- hash ops */

static void do_hash(void)
{
    /* hash <alg> <sequence> <outsize> <chunk>... */
    CPRI_HASH_STATE st;
    BYTE            buf[MAXB];
    BYTE            dig[MAX_DIGEST_SIZE * 2];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    BOOL            seq = (BOOL)argnum(3);
    UINT32          osz = (UINT32)argnum(4);
    UINT16          started, done;
    int             i;

    memset(&st, 0, sizeof(st));
    memset(dig, 0, sizeof(dig));
    started = _cpri__StartHash(alg, seq, &st);
    emit(" start=%u ctxalg=0x%04x", started, _cpri__GetContextAlg(&st));
    for (i = 5; i < g_ntok; i++) {
        int n = unhex(arg(i), buf, MAXB);
        _cpri__UpdateHash(&st, n < 0 ? 0 : (UINT32)n, n < 0 ? NULL : buf);
    }
    done = _cpri__CompleteHash(&st, osz, dig);
    emit(" done=%u", done);
    emith("digest", dig, done > sizeof(dig) ? (int)sizeof(dig) : (int)done);
}

static void do_hashafter(void)
{
    /* hashafter <alg> <sequence> <chunk> - complete twice, update after
     * completion: the second round must report the emptied state. */
    CPRI_HASH_STATE st;
    BYTE            buf[MAXB];
    BYTE            dig[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    BOOL            seq = (BOOL)argnum(3);
    int             n   = unhex(arg(4), buf, MAXB);
    UINT16          d1, d2;

    memset(&st, 0, sizeof(st));
    memset(dig, 0, sizeof(dig));
    _cpri__StartHash(alg, seq, &st);
    _cpri__UpdateHash(&st, n < 0 ? 0 : (UINT32)n, n < 0 ? NULL : buf);
    d1 = _cpri__CompleteHash(&st, sizeof(dig), dig);
    emit(" first=%u", d1);
    emith("digest1", dig, (int)d1);
    /* The state is spent now.  Both calls must be inert. */
    _cpri__UpdateHash(&st, n < 0 ? 0 : (UINT32)n, n < 0 ? NULL : buf);
    memset(dig, 0, sizeof(dig));
    d2 = _cpri__CompleteHash(&st, sizeof(dig), dig);
    emit(" second=%u", d2);
    emith("digest2", dig, (int)d2);
}

static void do_hashcopy(void)
{
    /* hashcopy <alg> <sequence> <chunkA> <chunkB> <chunkC>
     * A is fed to the original, the state is cloned, then B goes to the
     * original and C to the clone: the two digests prove independence.
     *
     * Only <sequence>=0 is drivable.  A sequence state holds a marshalled
     * byte image rather than a live context, and _cpri__CopyHashState
     * reinterprets the buffer as an EVP_MD_CTX unconditionally, so cloning a
     * sequence state dereferences a bogus digest pointer and takes the
     * process down.  The TPM layer never does it, so the baseline never
     * trips over it.
     *
     * The return value (sizeof(CPRI_HASH_STATE)) is reported only as
     * zero/non-zero: the exact struct size is sizing detail that a faithful
     * port to opaque contexts is entitled to change. */
    CPRI_HASH_STATE a, b;
    BYTE            bufa[MAXB], bufb[MAXB], bufc[MAXB];
    BYTE            da[MAX_DIGEST_SIZE], db[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    BOOL            seq = (BOOL)argnum(3);
    int             na  = unhex(arg(4), bufa, MAXB);
    int             nb  = unhex(arg(5), bufb, MAXB);
    int             nc  = unhex(arg(6), bufc, MAXB);
    UINT16          copied, ra, rb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(da, 0, sizeof(da));
    memset(db, 0, sizeof(db));
    _cpri__StartHash(alg, seq, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    copied = _cpri__CopyHashState(&b, &a);
    emit(" copied=%d clonealg=0x%04x", copied != 0, _cpri__GetContextAlg(&b));
    _cpri__UpdateHash(&a, nb < 0 ? 0 : (UINT32)nb, bufb);
    _cpri__UpdateHash(&b, nc < 0 ? 0 : (UINT32)nc, bufc);
    ra = _cpri__CompleteHash(&a, sizeof(da), da);
    rb = _cpri__CompleteHash(&b, sizeof(db), db);
    emit(" ra=%u rb=%u", ra, rb);
    emith("origin", da, (int)ra);
    emith("clone", db, (int)rb);
}

static void do_hashmove(void)
{
    /* hashmove <alg> <chunkA> <chunkB> <chunkC> - a sequence state relocated
     * by a bitwise copy of its container, which is what ContextSave does to
     * the OBJECT that holds it. */
    CPRI_HASH_STATE a, b;
    BYTE            bufa[MAXB], bufb[MAXB], bufc[MAXB];
    BYTE            da[MAX_DIGEST_SIZE], db[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    int             na  = unhex(arg(3), bufa, MAXB);
    int             nb  = unhex(arg(4), bufb, MAXB);
    int             nc  = unhex(arg(5), bufc, MAXB);
    UINT16          ra, rb;

    memset(&a, 0, sizeof(a));
    memset(da, 0, sizeof(da));
    memset(db, 0, sizeof(db));
    _cpri__StartHash(alg, TRUE, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    memcpy(&b, &a, sizeof(b));
    _cpri__UpdateHash(&a, nb < 0 ? 0 : (UINT32)nb, bufb);
    _cpri__UpdateHash(&b, nc < 0 ? 0 : (UINT32)nc, bufc);
    ra = _cpri__CompleteHash(&a, sizeof(da), da);
    rb = _cpri__CompleteHash(&b, sizeof(db), db);
    emit(" ra=%u rb=%u", ra, rb);
    emith("origin", da, (int)ra);
    emith("moved", db, (int)rb);
}

static void do_hashmove2(void)
{
    /* hashmove2 <alg> <chunkA> <chunkB> <chunkC> - a state relocated twice.
     * ContextLoad puts the OBJECT back in a slot, and the slot can be saved
     * again, so a sequence's state is copied an unbounded number of times
     * over its life.  All three of the states this leaves behind are fed
     * different bytes and completed: a relocation that hands out a second
     * name for one state rather than a second state shows up here as two
     * digests that agree, or as a completion that destroys another state. */
    CPRI_HASH_STATE a, b, c;
    BYTE            bufa[MAXB], bufb[MAXB], bufc[MAXB];
    BYTE            da[MAX_DIGEST_SIZE], db[MAX_DIGEST_SIZE];
    BYTE            dc[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    int             na  = unhex(arg(3), bufa, MAXB);
    int             nb  = unhex(arg(4), bufb, MAXB);
    int             nc  = unhex(arg(5), bufc, MAXB);
    UINT16          ra, rb, rc;

    memset(&a, 0, sizeof(a));
    memset(da, 0, sizeof(da));
    memset(db, 0, sizeof(db));
    memset(dc, 0, sizeof(dc));
    _cpri__StartHash(alg, TRUE, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    memcpy(&b, &a, sizeof(b));
    _cpri__UpdateHash(&b, nb < 0 ? 0 : (UINT32)nb, bufb);
    memcpy(&c, &b, sizeof(c));
    _cpri__UpdateHash(&c, nc < 0 ? 0 : (UINT32)nc, bufc);
    rc = _cpri__CompleteHash(&c, sizeof(dc), dc);
    rb = _cpri__CompleteHash(&b, sizeof(db), db);
    ra = _cpri__CompleteHash(&a, sizeof(da), da);
    emit(" ra=%u rb=%u rc=%u", ra, rb, rc);
    emith("origin", da, (int)ra);
    emith("once", db, (int)rb);
    emith("twice", dc, (int)rc);
}

static void do_hashmove3(void)
{
    /* hashmove3 <alg> <chunkA> <chunkB> <chunkC> <chunkD> - one state
     * relocated into two places at once, which is what a context blob loaded
     * twice produces.  Each of the three gets its own tail. */
    CPRI_HASH_STATE a, b, c;
    BYTE            bufa[MAXB], bufb[MAXB], bufc[MAXB], bufd[MAXB];
    BYTE            da[MAX_DIGEST_SIZE], db[MAX_DIGEST_SIZE];
    BYTE            dc[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    int             na  = unhex(arg(3), bufa, MAXB);
    int             nb  = unhex(arg(4), bufb, MAXB);
    int             nc  = unhex(arg(5), bufc, MAXB);
    int             nd  = unhex(arg(6), bufd, MAXB);
    UINT16          ra, rb, rc;

    memset(&a, 0, sizeof(a));
    memset(da, 0, sizeof(da));
    memset(db, 0, sizeof(db));
    memset(dc, 0, sizeof(dc));
    _cpri__StartHash(alg, TRUE, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    memcpy(&b, &a, sizeof(b));
    memcpy(&c, &a, sizeof(c));
    _cpri__UpdateHash(&a, nb < 0 ? 0 : (UINT32)nb, bufb);
    _cpri__UpdateHash(&b, nc < 0 ? 0 : (UINT32)nc, bufc);
    _cpri__UpdateHash(&c, nd < 0 ? 0 : (UINT32)nd, bufd);
    ra = _cpri__CompleteHash(&a, sizeof(da), da);
    rb = _cpri__CompleteHash(&b, sizeof(db), db);
    rc = _cpri__CompleteHash(&c, sizeof(dc), dc);
    emit(" ra=%u rb=%u rc=%u", ra, rb, rc);
    emith("origin", da, (int)ra);
    emith("fork1", db, (int)rb);
    emith("fork2", dc, (int)rc);
}

static void do_hashmovedrop(void)
{
    /* hashmovedrop <alg> <chunkA> <chunkB> - relocate, then abandon the
     * original without completing it, which is the ContextSave-then-
     * FlushContext path.  The copy is the only surviving state and has to
     * finish on its own. */
    CPRI_HASH_STATE a, b;
    BYTE            bufa[MAXB], bufb[MAXB];
    BYTE            db[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    int             na  = unhex(arg(3), bufa, MAXB);
    int             nb  = unhex(arg(4), bufb, MAXB);
    UINT16          rb;

    memset(&a, 0, sizeof(a));
    memset(db, 0, sizeof(db));
    _cpri__StartHash(alg, TRUE, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    memcpy(&b, &a, sizeof(b));
    memset(&a, 0, sizeof(a));      /* the slot the original lived in is reused */
    _cpri__UpdateHash(&b, nb < 0 ? 0 : (UINT32)nb, bufb);
    rb = _cpri__CompleteHash(&b, sizeof(db), db);
    emit(" rb=%u", rb);
    emith("moved", db, (int)rb);
}

static void do_hashmoveafter(void)
{
    /* hashmoveafter <alg> <chunkA> <chunkB> - relocate, complete the
     * ORIGINAL, then finish the copy with the same tail.  Both digests cover
     * the same bytes, so they have to agree; a copy that shares the
     * original's private state instead of owning one answers differently, or
     * not at all. */
    CPRI_HASH_STATE a, b;
    BYTE            bufa[MAXB], bufb[MAXB];
    BYTE            da[MAX_DIGEST_SIZE], db[MAX_DIGEST_SIZE];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    int             na  = unhex(arg(3), bufa, MAXB);
    int             nb  = unhex(arg(4), bufb, MAXB);
    UINT16          ra, rb;

    memset(&a, 0, sizeof(a));
    memset(da, 0, sizeof(da));
    memset(db, 0, sizeof(db));
    _cpri__StartHash(alg, TRUE, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    memcpy(&b, &a, sizeof(b));
    _cpri__UpdateHash(&a, nb < 0 ? 0 : (UINT32)nb, bufb);
    ra = _cpri__CompleteHash(&a, sizeof(da), da);
    _cpri__UpdateHash(&b, nb < 0 ? 0 : (UINT32)nb, bufb);
    rb = _cpri__CompleteHash(&b, sizeof(db), db);
    emit(" ra=%u rb=%u", ra, rb);
    emith("origin", da, (int)ra);
    emith("moved", db, (int)rb);
}

static void do_hashexport(void)
{
    /* hashexport <alg> <chunkA> <chunkB> - export the sequence state, then
     * finish the original: exporting must not disturb it.
     *
     * What the external buffer ends up holding is deliberately not reported.
     * _cpri__ImportExportHashState is a bare `return;` in this revision (the
     * copy is #if 0'd out), so there is no export format to be faithful to --
     * only the guarantee that the source state survives the call. */
    CPRI_HASH_STATE   a;
    EXPORT_HASH_STATE x;
    BYTE              bufa[MAXB], bufb[MAXB];
    BYTE              dig[MAX_DIGEST_SIZE];
    TPM_ALG_ID        alg = (TPM_ALG_ID)argnum(2);
    int               na  = unhex(arg(3), bufa, MAXB);
    int               nb  = unhex(arg(4), bufb, MAXB);
    UINT16            r;

    memset(&a, 0, sizeof(a));
    memset(&x, 0, sizeof(x));
    memset(dig, 0, sizeof(dig));
    _cpri__StartHash(alg, TRUE, &a);
    _cpri__UpdateHash(&a, na < 0 ? 0 : (UINT32)na, bufa);
    _cpri__ImportExportHashState(&a, &x, EXPORT_STATE);
    _cpri__UpdateHash(&a, nb < 0 ? 0 : (UINT32)nb, bufb);
    r = _cpri__CompleteHash(&a, sizeof(dig), dig);
    emit(" r=%u", r);
    emith("digest", dig, (int)r);
}

static void do_hashblock(void)
{
    /* hashblock <alg> <outsize> <data> */
    BYTE       buf[MAXB];
    BYTE       dig[MAX_DIGEST_SIZE * 2];
    TPM_ALG_ID alg = (TPM_ALG_ID)argnum(2);
    UINT32     osz = (UINT32)argnum(3);
    int        n   = unhex(arg(4), buf, MAXB);
    UINT16     r;

    memset(dig, 0, sizeof(dig));
    r = _cpri__HashBlock(alg, n < 0 ? 0 : (UINT32)n, n < 0 ? NULL : buf,
                         osz, dig);
    emit(" r=%u", r);
    emith("digest", dig, r > sizeof(dig) ? (int)sizeof(dig) : (int)r);
}

static void do_hmac(void)
{
    /* hmac <alg> <sequence> <outsize> <key> <chunk>... */
    CPRI_HASH_STATE st;
    B2              opad;
    BYTE            key[MAXB], buf[MAXB];
    BYTE            mac[MAX_DIGEST_SIZE * 2];
    TPM_ALG_ID      alg = (TPM_ALG_ID)argnum(2);
    BOOL            seq = (BOOL)argnum(3);
    UINT32          osz = (UINT32)argnum(4);
    int             nk  = unhex(arg(5), key, MAXB);
    UINT16          started, done;
    int             i;

    memset(&st, 0, sizeof(st));
    memset(&opad, 0, sizeof(opad));
    memset(mac, 0, sizeof(mac));
    opad.t.size = 0;
    started = _cpri__StartHMAC(alg, seq, &st, (UINT16)(nk < 0 ? 0 : nk),
                               nk < 0 ? NULL : key, &opad.b);
    emit(" start=%u opadsize=%u", started, opad.t.size);
    for (i = 6; i < g_ntok; i++) {
        int n = unhex(arg(i), buf, MAXB);
        _cpri__UpdateHash(&st, n < 0 ? 0 : (UINT32)n, buf);
    }
    done = _cpri__CompleteHMAC(&st, &opad.b, osz, mac);
    emit(" done=%u", done);
    emith("mac", mac, done > sizeof(mac) ? (int)sizeof(mac) : (int)done);
}

static void do_mgf1(void)
{
    /* mgf1 <alg> <masksize> <seed> */
    BYTE         mask[MAXB], seed[MAXB];
    TPM_ALG_ID   alg = (TPM_ALG_ID)argnum(2);
    UINT32       msz = (UINT32)argnum(3);
    int          ns  = unhex(arg(4), seed, MAXB);
    CRYPT_RESULT r;

    if (msz > MAXB) msz = MAXB;
    memset(mask, 0, sizeof(mask));
    r = _cpri__MGF1(msz, mask, alg, ns < 0 ? 0 : (UINT32)ns,
                    ns < 0 ? NULL : seed);
    emit(" r=%d", (int)r);
    emith("mask", mask, r > 0 ? (int)r : 0);
}

static void do_kdfa(void)
{
    /* kdfa <alg> <key> <label> <contextU> <contextV> <bits> <once> <counter> */
    B2         key, cu, cv;
    BYTE       ks[MAXB];
    TPM_ALG_ID alg  = (TPM_ALG_ID)argnum(2);
    TPM2B     *pkey = arg2b(&key, 3);
    const char *lab = arglabel(4);
    TPM2B     *pcu  = arg2b(&cu, 5);
    TPM2B     *pcv  = arg2b(&cv, 6);
    UINT32     bits = (UINT32)argnum(7);
    BOOL       once = (BOOL)argnum(8);
    UINT32     ctr  = (UINT32)argnum(9);
    int        usectr = !isnull(arg(9));
    UINT16     r;

    memset(ks, 0, sizeof(ks));
    r = _cpri__KDFa(alg, pkey, lab, pcu, pcv, bits, ks,
                    usectr ? &ctr : NULL, once);
    emit(" r=%u", r);
    if (usectr) emit(" counter=%u", ctr);
    emith("stream", ks, r > MAXB ? MAXB : (int)r);
}

static void do_kdfe(void)
{
    /* kdfe <alg> <z> <label> <partyU> <partyV> <bits> */
    B2         z, pu, pv;
    BYTE       ks[MAXB];
    TPM_ALG_ID alg  = (TPM_ALG_ID)argnum(2);
    TPM2B     *pz   = arg2b(&z, 3);
    const char *lab = arglabel(4);
    TPM2B     *ppu  = arg2b(&pu, 5);
    TPM2B     *ppv  = arg2b(&pv, 6);
    UINT32     bits = (UINT32)argnum(7);
    UINT16     r;

    memset(ks, 0, sizeof(ks));
    r = _cpri__KDFe(alg, pz, lab, ppu, ppv, bits, ks);
    emit(" r=%u", r);
    emith("stream", ks, r > MAXB ? MAXB : (int)r);
}

static void do_hashinfo(void)
{
    /* hashinfo <alg> */
    TPM_ALG_ID  alg = (TPM_ALG_ID)argnum(2);
    const BYTE *der = NULL;
    UINT16      dsz;

    emit(" digestsize=%u blocksize=%u",
         _cpri__GetDigestSize(alg), _cpri__GetHashBlockSize(alg));
    dsz = _cpri__GetHashDER(alg, &der);
    emit(" dersize=%u", dsz);
    emith("der", der, (int)dsz);
}

static void do_hashindex(void)
{
    /* hashindex <i> */
    emit(" alg=0x%04x", _cpri__GetHashAlgByIndex((UINT32)argnum(2)));
}

/* ------------------------------------------------------------ symmetric */

static void do_sym(void)
{
    /* sym <mode> <e|d> <keybits> <key> <iv> <data>
     *
     * AES is the only block cipher this build enables (SM4 and Camellia are
     * ALG_NO).  AES-CBC decrypt exists -- _cpri__AESDecryptCBC is defined in
     * CpriSym.c, exported by libCryptoEngine.so and called from CryptUtil.c --
     * but it is declared in no installed header, so a consumer bound to the
     * public headers, which is what this driver is, cannot name it.  Undrivable
     * from out of tree, not absent: the e2e half reaches it through the TPM
     * layer, in e2esym roundtrip-128-CBC and roundtrip-256-CBC.
     * OFB and CTR are self-inverse, so the engine ships only the encrypt entry
     * point for them and the decrypt direction routes to it. */
    BYTE         key[MAXB], iv[MAXB], in[MAXB], outb[MAXB];
    TPM_ALG_ID   mode = (TPM_ALG_ID)argnum(2);
    int          dec  = arg(3) != NULL && arg(3)[0] == 'd';
    UINT32       bits = (UINT32)argnum(4);
    int          nk   = unhex(arg(5), key, MAXB);
    int          niv  = unhex(arg(6), iv, MAXB);
    int          nin  = unhex(arg(7), in, MAXB);
    CRYPT_RESULT r    = CRYPT_FAIL;
    BYTE        *pk   = nk < 0 ? NULL : key;
    BYTE        *pv   = niv < 0 ? NULL : iv;
    BYTE        *pin  = nin < 0 ? NULL : in;
    UINT32       nn   = nin < 0 ? 0 : (UINT32)nin;

    memset(outb, 0, sizeof(outb));
    switch (mode) {
    case TPM_ALG_ECB:
        r = dec ? _cpri__AESDecryptECB(outb, bits, pk, nn, pin)
                : _cpri__AESEncryptECB(outb, bits, pk, nn, pin);
        break;
    case TPM_ALG_CBC:
        if (dec) { emit(" undrivable=1"); return; }
        r = _cpri__AESEncryptCBC(outb, bits, pk, pv, nn, pin);
        break;
    case TPM_ALG_CFB:
        r = dec ? _cpri__AESDecryptCFB(outb, bits, pk, pv, nn, pin)
                : _cpri__AESEncryptCFB(outb, bits, pk, pv, nn, pin);
        break;
    case TPM_ALG_OFB:
        r = _cpri__AESEncryptOFB(outb, bits, pk, pv, nn, pin);
        break;
    case TPM_ALG_CTR:
        r = _cpri__AESEncryptCTR(outb, bits, pk, pv, nn, pin);
        break;
    default:
        emit(" undrivable=1");
        return;
    }
    emit(" r=%d", (int)r);
    emith("out", outb, (int)nn);
    /* CBC/CFB/OFB/CTR chain through the IV buffer in place, so the IV the
     * engine leaves behind is part of the observable result. */
    emith("iv", pv, niv < 0 ? 0 : niv);
}

static void do_symroundtrip(void)
{
    /* symroundtrip <mode> <keybits> <key> <iv> <data> - the modes whose
     * decrypt direction exists, checked end to end so the in-place IV update
     * cannot be papered over. */
    BYTE       key[MAXB], iv[MAXB], iv2[MAXB], in[MAXB], ct[MAXB], pt[MAXB];
    TPM_ALG_ID mode = (TPM_ALG_ID)argnum(2);
    UINT32     bits = (UINT32)argnum(3);
    int        nk   = unhex(arg(4), key, MAXB);
    int        niv  = unhex(arg(5), iv, MAXB);
    int        nin  = unhex(arg(6), in, MAXB);
    CRYPT_RESULT re = CRYPT_FAIL, rd = CRYPT_FAIL;

    if (nk < 0 || nin < 0) { emit(" undrivable=1"); return; }
    if (niv < 0) niv = 0;
    memcpy(iv2, iv, (size_t)niv);
    memset(ct, 0, sizeof(ct));
    memset(pt, 0, sizeof(pt));
    switch (mode) {
    case TPM_ALG_ECB:
        re = _cpri__AESEncryptECB(ct, bits, key, (UINT32)nin, in);
        rd = _cpri__AESDecryptECB(pt, bits, key, (UINT32)nin, ct);
        break;
    case TPM_ALG_CFB:
        re = _cpri__AESEncryptCFB(ct, bits, key, iv, (UINT32)nin, in);
        rd = _cpri__AESDecryptCFB(pt, bits, key, iv2, (UINT32)nin, ct);
        break;
    case TPM_ALG_OFB:
        re = _cpri__AESEncryptOFB(ct, bits, key, iv, (UINT32)nin, in);
        rd = _cpri__AESEncryptOFB(pt, bits, key, iv2, (UINT32)nin, ct);
        break;
    case TPM_ALG_CTR:
        re = _cpri__AESEncryptCTR(ct, bits, key, iv, (UINT32)nin, in);
        rd = _cpri__AESEncryptCTR(pt, bits, key, iv2, (UINT32)nin, ct);
        break;
    default:
        emit(" undrivable=1");
        return;
    }
    emit(" enc=%d dec=%d recovered=%d", (int)re, (int)rd,
         (re == CRYPT_SUCCESS && rd == CRYPT_SUCCESS)
             ? (memcmp(pt, in, (size_t)nin) == 0) : -1);
    emith("ct", ct, re == CRYPT_SUCCESS ? nin : 0);
    emith("iv", iv, niv);
    emith("iv2", iv2, niv);
}

static void do_symblock(void)
{
    /* symblock <alg> <keybits> */
    emit(" size=%d", (int)_cpri__GetSymmetricBlockSize(
                          (TPM_ALG_ID)argnum(2), (UINT16)argnum(3)));
}

/* ------------------------------------------------------------------ rsa */

#ifdef TPM_ALG_RSA
static void do_rsatest(void)
{
    /* rsatest <exponent> <modulus> <prime1> <prime2> */
    B2           d, n, p, q;
    TPM2B       *pn, *pp, *pq;
    CRYPT_RESULT r;

    memset(&d, 0, sizeof(d));
    d.t.size = MAX_RSA_KEY_BYTES / 2;
    pn = arg2b(&n, 3);
    pp = arg2b(&p, 4);
    pq = arg2b(&q, 5);
    r = _cpri__TestKeyRSA(&d.b, (UINT32)argnum(2), pn, pp, pq);
    emit(" r=%d", (int)r);
    emit2b("d", &d.b);
    if (pn != NULL) emit2b("n", pn);
}

static void do_rsaenc(void)
{
    /* rsaenc <padding> <hashAlg> <label> <exponent> <modulus> <data> */
    RSA_KEY      key;
    B2           pub, priv;
    BYTE         in[MAXB], ct[MAXB];
    UINT32       csz = MAXB;
    int          nin;
    CRYPT_RESULT r;

    memset(&priv, 0, sizeof(priv));
    priv.t.size = 0;
    key.exponent   = (UINT32)argnum(5);
    key.publicKey  = arg2b(&pub, 6);
    key.privateKey = &priv.b;
    nin = unhex(arg(7), in, MAXB);
    memset(ct, 0, sizeof(ct));
    r = _cpri__EncryptRSA(&csz, ct, &key, (TPM_ALG_ID)argnum(2),
                          nin < 0 ? 0 : (UINT32)nin, nin < 0 ? NULL : in,
                          (TPM_ALG_ID)argnum(3), arglabel(4));
    emit(" r=%d size=%u", (int)r, csz);
    emith("ct", ct, r == CRYPT_SUCCESS ? (int)csz : 0);
}

static void do_rsadec(void)
{
    /* rsadec <padding> <hashAlg> <label> <exponent> <modulus> <prime> <ct> */
    RSA_KEY      key;
    B2           pub, priv;
    BYTE         ct[MAXB], pt[MAXB];
    UINT32       psz = MAXB;
    int          nct;
    CRYPT_RESULT r;

    key.exponent   = (UINT32)argnum(5);
    key.publicKey  = arg2b(&pub, 6);
    key.privateKey = arg2b(&priv, 7);
    nct = unhex(arg(8), ct, MAXB);
    memset(pt, 0, sizeof(pt));
    r = _cpri__DecryptRSA(&psz, pt, &key, (TPM_ALG_ID)argnum(2),
                          nct < 0 ? 0 : (UINT32)nct, nct < 0 ? NULL : ct,
                          (TPM_ALG_ID)argnum(3), arglabel(4));
    emit(" r=%d size=%u", (int)r, psz);
    emith("pt", pt, r == CRYPT_SUCCESS ? (int)psz : 0);
}

static void do_rsaroundtrip(void)
{
    /* rsaroundtrip <padding> <hashAlg> <label> <exponent> <modulus> <prime>
     *              <data>
     * Padding schemes that draw random bytes cannot be scored on the
     * ciphertext, so the recovered plaintext is what is compared. */
    RSA_KEY      key;
    B2           pub, priv;
    BYTE         in[MAXB], ct[MAXB], pt[MAXB];
    UINT32       csz = MAXB, psz = MAXB;
    TPM_ALG_ID   pad = (TPM_ALG_ID)argnum(2);
    TPM_ALG_ID   hsh = (TPM_ALG_ID)argnum(3);
    const char  *lab = arglabel(4);
    int          nin;
    CRYPT_RESULT re, rd;

    key.exponent   = (UINT32)argnum(5);
    key.publicKey  = arg2b(&pub, 6);
    key.privateKey = arg2b(&priv, 7);
    nin = unhex(arg(8), in, MAXB);
    memset(ct, 0, sizeof(ct));
    memset(pt, 0, sizeof(pt));
    re = _cpri__EncryptRSA(&csz, ct, &key, pad, nin < 0 ? 0 : (UINT32)nin,
                           nin < 0 ? NULL : in, hsh, lab);
    emit(" enc=%d csize=%u", (int)re, csz);
    if (re != CRYPT_SUCCESS) return;
    rd = _cpri__DecryptRSA(&psz, pt, &key, pad, csz, ct, hsh, lab);
    emit(" dec=%d psize=%u", (int)rd, psz);
    emith("pt", pt, rd == CRYPT_SUCCESS ? (int)psz : 0);
}

static void do_rsasign(void)
{
    /* rsasign <scheme> <hashAlg> <exponent> <modulus> <prime> <digest> */
    RSA_KEY      key;
    B2           pub, priv;
    BYTE         dg[MAXB], sig[MAXB];
    UINT32       ssz = MAXB;
    int          nd;
    CRYPT_RESULT r;

    key.exponent   = (UINT32)argnum(4);
    key.publicKey  = arg2b(&pub, 5);
    key.privateKey = arg2b(&priv, 6);
    nd = unhex(arg(7), dg, MAXB);
    memset(sig, 0, sizeof(sig));
    r = _cpri__SignRSA(&ssz, sig, &key, (TPM_ALG_ID)argnum(2),
                       (TPM_ALG_ID)argnum(3), nd < 0 ? 0 : (UINT32)nd,
                       nd < 0 ? NULL : dg);
    emit(" r=%d size=%u", (int)r, ssz);
    emith("sig", sig, r == CRYPT_SUCCESS ? (int)ssz : 0);
}

static void do_rsasignverify(void)
{
    /* rsasignverify <scheme> <hashAlg> <exponent> <modulus> <prime> <digest>
     *               <saltsize>
     * PSS salts at random, so the pair (sign result, verify result) is the
     * stable observation. */
    RSA_KEY      key;
    B2           pub, priv;
    BYTE         dg[MAXB], sig[MAXB];
    UINT32       ssz = MAXB;
    TPM_ALG_ID   sch = (TPM_ALG_ID)argnum(2);
    TPM_ALG_ID   hsh = (TPM_ALG_ID)argnum(3);
    int          nd;
    CRYPT_RESULT rs, rv;

    key.exponent   = (UINT32)argnum(4);
    key.publicKey  = arg2b(&pub, 5);
    key.privateKey = arg2b(&priv, 6);
    nd = unhex(arg(7), dg, MAXB);
    memset(sig, 0, sizeof(sig));
    rs = _cpri__SignRSA(&ssz, sig, &key, sch, hsh, nd < 0 ? 0 : (UINT32)nd,
                        nd < 0 ? NULL : dg);
    emit(" sign=%d size=%u", (int)rs, ssz);
    if (rs != CRYPT_SUCCESS) return;
    rv = _cpri__ValidateSignatureRSA(&key, sch, hsh, nd < 0 ? 0 : (UINT32)nd,
                                     nd < 0 ? NULL : dg, ssz, sig,
                                     (UINT16)argnum(8));
    emit(" verify=%d", (int)rv);
}

static void do_rsaverify(void)
{
    /* rsaverify <scheme> <hashAlg> <exponent> <modulus> <digest> <sig>
     *           <saltsize> */
    RSA_KEY      key;
    B2           pub, priv;
    BYTE         dg[MAXB], sig[MAXB];
    int          nd, ns;
    CRYPT_RESULT r;

    memset(&priv, 0, sizeof(priv));
    priv.t.size = 0;
    key.exponent   = (UINT32)argnum(4);
    key.publicKey  = arg2b(&pub, 5);
    key.privateKey = &priv.b;
    nd = unhex(arg(6), dg, MAXB);
    ns = unhex(arg(7), sig, MAXB);
    r = _cpri__ValidateSignatureRSA(&key, (TPM_ALG_ID)argnum(2),
                                    (TPM_ALG_ID)argnum(3),
                                    nd < 0 ? 0 : (UINT32)nd,
                                    nd < 0 ? NULL : dg,
                                    ns < 0 ? 0 : (UINT32)ns,
                                    ns < 0 ? NULL : sig,
                                    (UINT16)argnum(8));
    emit(" r=%d", (int)r);
}

#ifndef RSA_KEY_SIEVE
static void do_rsagen(void)
{
    /* rsagen <keybits> <e> <hashAlg> <seed> <label> <extra> <counter> */
    B2           n, p, seed, extra;
    TPM2B       *pseed, *pextra;
    UINT32       ctr;
    int          usectr = !isnull(arg(8));
    CRYPT_RESULT r;

    memset(&n, 0, sizeof(n));
    memset(&p, 0, sizeof(p));
    n.t.size = MAX_RSA_KEY_BYTES;
    p.t.size = MAX_RSA_KEY_BYTES / 2;
    pseed  = arg2b(&seed, 5);
    pextra = arg2b(&extra, 7);
    ctr    = (UINT32)argnum(8);
    r = _cpri__GenerateKeyRSA(&n.b, &p.b, (UINT16)argnum(2),
                              (UINT32)argnum(3), (TPM_ALG_ID)argnum(4),
                              pseed, arglabel(6), pextra,
                              usectr ? &ctr : NULL);
    emit(" r=%d", (int)r);
    if (usectr) emit(" counter=%u", ctr);
    emit2b("n", &n.b);
    emit2b("p", &p.b);
}
#endif
#endif /* TPM_ALG_RSA */

/* ------------------------------------------------------------------ ecc */

#ifdef TPM_ALG_ECC
static void do_eccinfo(void)
{
    /* eccinfo <curve> */
    const ECC_CURVE *c = _cpri__EccGetParametersByCurveId(
                             (TPM_ECC_CURVE)argnum(2));

    if (c == NULL) {
        emit(" found=0");
        return;
    }
    emit(" found=1 curve=0x%04x bits=%u kdf=0x%04x kdfhash=0x%04x"
         " sign=0x%04x",
         c->curveId, c->keySizeBits, c->kdf.scheme,
         c->kdf.details.mgf1.hashAlg, c->sign.scheme);
    emit2b("p", c->curveData->p);
    emit2b("a", c->curveData->a);
    emit2b("b", c->curveData->b);
    emit2b("gx", c->curveData->x);
    emit2b("gy", c->curveData->y);
    emit2b("n", c->curveData->n);
    emit2b("h", c->curveData->h);
}

static void do_ecccurves(void)
{
    /* ecccurves */
    UINT32 count = _cpri__EccGetCurveCount();
    UINT32 i;

    emit(" count=%u", count);
    for (i = 0; i < count && i < 32; i++)
        emit(" c%u=0x%04x", i, _cpri__GetCurveIdByIndex((UINT16)i));
}

static void do_curvebyindex(void)
{
    /* curvebyindex <i> - the index form, including indices past the end of
     * the table, which the ecccurves sweep cannot reach. */
    emit(" curve=0x%04x", _cpri__GetCurveIdByIndex((UINT16)argnum(2)));
}

static void do_eccmul(void)
{
    /* eccmul <curve> <d> <Qx> <Qy> <u> */
    TPMS_ECC_POINT      r, q;
    TPM2B_ECC_PARAMETER d, u;
    TPM2B_ECC_PARAMETER *pd, *pu;
    TPMS_ECC_POINT      *pq;
    CRYPT_RESULT        rc;

    memset(&r, 0, sizeof(r));
    pd = argecc(&d, 3);
    pq = argpt(&q, 4);
    pu = argecc(&u, 6);
    rc = _cpri__EccPointMultiply(&r, (TPM_ECC_CURVE)argnum(2), pd, pq, pu);
    emit(" r=%d", (int)rc);
    emit2b("x", &r.x.b);
    emit2b("y", &r.y.b);
}

static void do_eccpoint(void)
{
    /* eccpoint <curve> <Qx> <Qy> */
    TPMS_ECC_POINT q;
    TPMS_ECC_POINT *pq = argpt(&q, 3);

    emit(" on=%d", (int)_cpri__EccIsPointOnCurve((TPM_ECC_CURVE)argnum(2), pq));
}

static void do_eccgen(void)
{
    /* eccgen <curve> <hashAlg> <seed> <label> <extra> <counter> */
    TPMS_ECC_POINT      q;
    TPM2B_ECC_PARAMETER d;
    B2                  seed, extra;
    TPM2B              *pseed, *pextra;
    UINT32              ctr;
    int                 usectr = !isnull(arg(7));
    CRYPT_RESULT        rc;

    memset(&q, 0, sizeof(q));
    memset(&d, 0, sizeof(d));
    pseed  = arg2b(&seed, 4);
    pextra = arg2b(&extra, 6);
    ctr    = (UINT32)argnum(7);
    rc = _cpri__GenerateKeyEcc(&q, &d, (TPM_ECC_CURVE)argnum(2),
                               (TPM_ALG_ID)argnum(3), pseed, arglabel(5),
                               pextra, usectr ? &ctr : NULL);
    emit(" r=%d", (int)rc);
    if (usectr) emit(" counter=%u", ctr);
    emit2b("x", &q.x.b);
    emit2b("y", &q.y.b);
    emit2b("d", &d.b);
    emit(" on=%d", (int)_cpri__EccIsPointOnCurve((TPM_ECC_CURVE)argnum(2), &q));
}

static void do_eccephemeral(void)
{
    /* eccephemeral <curve> - the key is random, so only its shape and the
     * fact that it lands on the curve can be scored. */
    TPMS_ECC_POINT      q;
    TPM2B_ECC_PARAMETER d;
    TPM_ECC_CURVE       cv = (TPM_ECC_CURVE)argnum(2);
    CRYPT_RESULT        rc;

    memset(&q, 0, sizeof(q));
    memset(&d, 0, sizeof(d));
    rc = _cpri__GetEphemeralEcc(&q, &d, cv);
    emit(" r=%d xsize=%u ysize=%u dsize=%u on=%d", (int)rc, q.x.t.size,
         q.y.t.size, d.t.size,
         rc == CRYPT_SUCCESS ? (int)_cpri__EccIsPointOnCurve(cv, &q) : -1);
}

/* Is this a legal signature component for this curve, i.e. is it in [1, n-1]?
 * Every correct ECDSA signature satisfies that whatever nonce the library drew
 * for itself, so it is a function of the inputs where the components are not.
 * Returns -1 for a curve the engine does not know -- defensive only, since
 * _cpri__EccGetParametersByCurveId asserts on an unknown curve rather than
 * returning NULL, which is why the bad-curve cases record tpmfail.  Written out
 * rather than called through _math__uComp so the check does not depend on the
 * engine half being ported to answer a question asked about the engine half. */
static int ecc_inrange(const TPM2B *v, TPM_ECC_CURVE cv)
{
    const ECC_CURVE *c = _cpri__EccGetParametersByCurveId(cv);
    const TPM2B     *n;
    UINT16           vi, ni;

    if (c == NULL) return -1;
    n = c->curveData->n;
    for (vi = 0; vi < v->size && v->buffer[vi] == 0; vi++);
    for (ni = 0; ni < n->size && n->buffer[ni] == 0; ni++);
    if (vi == v->size) return 0;                        /* zero */
    if (v->size - vi != n->size - ni)
        return (v->size - vi) < (n->size - ni);
    for (; vi < v->size; vi++, ni++)
        if (v->buffer[vi] != n->buffer[ni])
            return v->buffer[vi] < n->buffer[ni];
    return 0;                                           /* exactly n */
}

static void do_eccrange(void)
{
    /* eccrange <curve> <v> - ecc_inrange() against values whose answers are
     * known before the run, so the instrument the eccsv cases lean on is
     * rehearsed rather than assumed. */
    TPM2B_ECC_PARAMETER v;

    if (argecc(&v, 3) == NULL) {
        emit(" inrange=-2");
        return;
    }
    emit(" inrange=%d", ecc_inrange(&v.b, (TPM_ECC_CURVE)argnum(2)));
}

static void do_eccsign(void)
{
    /* eccsign <scheme> <hashAlg> <curve> <d> <digest> <k> */
    TPM2B_ECC_PARAMETER r, s, dk, k;
    TPM2B_ECC_PARAMETER *pd, *pk;
    B2                   dg;
    TPM2B               *pdg;
    CRYPT_RESULT         rc;

    memset(&r, 0, sizeof(r));
    memset(&s, 0, sizeof(s));
    pd  = argecc(&dk, 5);
    pdg = arg2b(&dg, 6);
    pk  = argecc(&k, 7);
    rc = _cpri__SignEcc(&r, &s, (TPM_ALG_ID)argnum(2), (TPM_ALG_ID)argnum(3),
                        (TPM_ECC_CURVE)argnum(4), pd, pdg, pk);
    emit(" r=%d", (int)rc);
    emit2b("sigr", &r.b);
    emit2b("sigs", &s.b);
}

static void do_eccsignverify(void)
{
    /* eccsignverify <scheme> <hashAlg> <curve> <d> <Qx> <Qy> <digest> <k> */
    TPM2B_ECC_PARAMETER  sr, ss, dk, k;
    TPM2B_ECC_PARAMETER *pd, *pk;
    TPMS_ECC_POINT       q;
    TPMS_ECC_POINT      *pq;
    B2                   dg;
    TPM2B               *pdg;
    TPM_ALG_ID           sch = (TPM_ALG_ID)argnum(2);
    TPM_ALG_ID           hsh = (TPM_ALG_ID)argnum(3);
    TPM_ECC_CURVE        cv  = (TPM_ECC_CURVE)argnum(4);
    CRYPT_RESULT         rs, rv;

    memset(&sr, 0, sizeof(sr));
    memset(&ss, 0, sizeof(ss));
    pd  = argecc(&dk, 5);
    pq  = argpt(&q, 6);
    pdg = arg2b(&dg, 8);
    pk  = argecc(&k, 9);
    rs = _cpri__SignEcc(&sr, &ss, sch, hsh, cv, pd, pdg, pk);
    emit(" sign=%d", (int)rs);
    if (rs != CRYPT_SUCCESS) return;

    /* Well-formedness holds for every signature this can produce, so it is
     * scored for every scheme. */
    emit(" rrange=%d srange=%d", ecc_inrange(&sr.b, cv), ecc_inrange(&ss.b, cv));

    /* The round trip is only scored where the signature is a function of the
     * inputs.  _cpri__SignEcc drops kIn for ECDSA (CpriECC.c:1776) and draws its
     * own nonce, and the verifier truncates the digest to the length of r
     * (CpriECC.c:1909), so an ECDSA signature whose r happens to lead with a
     * zero byte -- once in 256 -- is rejected by the very code that made it.
     * Recording a coin flip here would cost a correct port a point once in 256
     * runs; the defect itself is recorded deterministically instead, by the
     * eccverify cases that hand a fixed short-r signature to the verifier.
     * Schemes that take the nonce keep the round trip, which is where it says
     * something about the port. */
    if (sch == TPM_ALG_ECDSA) return;
    emit(" rsize=%u ssize=%u", sr.t.size, ss.t.size);
    rv = _cpri__ValidateSignatureEcc(&sr, &ss, sch, hsh, cv, pq, pdg);
    emit(" verify=%d", (int)rv);
}

static void do_eccverify(void)
{
    /* eccverify <scheme> <hashAlg> <curve> <Qx> <Qy> <digest> <r> <s> */
    TPM2B_ECC_PARAMETER  sr, ss;
    TPM2B_ECC_PARAMETER *psr, *pss;
    TPMS_ECC_POINT       q;
    TPMS_ECC_POINT      *pq;
    B2                   dg;
    TPM2B               *pdg;
    CRYPT_RESULT         rc;

    pq  = argpt(&q, 5);
    pdg = arg2b(&dg, 7);
    psr = argecc(&sr, 8);
    pss = argecc(&ss, 9);
    rc = _cpri__ValidateSignatureEcc(psr, pss, (TPM_ALG_ID)argnum(2),
                                     (TPM_ALG_ID)argnum(3),
                                     (TPM_ECC_CURVE)argnum(4), pq, pdg);
    emit(" r=%d", (int)rc);
}

static void do_ecccommit(void)
{
    /* ecccommit <curve> <Mx> <My> <Bx> <By> <d> <r> */
    TPMS_ECC_POINT       k, l, e, m, b;
    TPMS_ECC_POINT      *pm, *pb;
    TPM2B_ECC_PARAMETER  d, r;
    CRYPT_RESULT         rc;

    memset(&k, 0, sizeof(k));
    memset(&l, 0, sizeof(l));
    memset(&e, 0, sizeof(e));
    pm = argpt(&m, 3);
    pb = argpt(&b, 5);
    argecc(&d, 7);
    argecc(&r, 8);
    rc = _cpri__EccCommitCompute(&k, &l, &e, (TPM_ECC_CURVE)argnum(2),
                                 pm, pb, &d, &r);
    emit(" r=%d", (int)rc);
    emit2b("kx", &k.x.b);
    emit2b("ky", &k.y.b);
    emit2b("lx", &l.x.b);
    emit2b("ly", &l.y.b);
    emit2b("ex", &e.x.b);
    emit2b("ey", &e.y.b);
}

#if CC_ZGen_2Phase == YES
static void do_ecc22(void)
{
    /* ecc22 <curve> <scheme> <dsA> <deA> <QsBx> <QsBy> <QeBx> <QeBy> */
    TPMS_ECC_POINT       z1, z2, qs, qe;
    TPM2B_ECC_PARAMETER  ds, de;
    CRYPT_RESULT         rc;

    memset(&z1, 0, sizeof(z1));
    memset(&z2, 0, sizeof(z2));
    argecc(&ds, 4);
    argecc(&de, 5);
    argpt(&qs, 6);
    argpt(&qe, 8);
    rc = _cpri__C_2_2_KeyExchange(&z1, &z2, (TPM_ECC_CURVE)argnum(2),
                                  (TPM_ALG_ID)argnum(3), &ds, &de, &qs, &qe);
    emit(" r=%d", (int)rc);
    emit2b("z1x", &z1.x.b);
    emit2b("z1y", &z1.y.b);
    emit2b("z2x", &z2.x.b);
    emit2b("z2y", &z2.y.b);
}
#endif
#endif /* TPM_ALG_ECC */

/* ----------------------------------------------------------------- math */

static void do_modexp(void)
{
    /* modexp <csize> <m> <e> <n> */
    BYTE         c[MAXB], m[MAXB], e[MAXB], n[MAXB];
    UINT32       csz = (UINT32)argnum(2);
    int          nm  = unhex(arg(3), m, MAXB);
    int          ne  = unhex(arg(4), e, MAXB);
    int          nn  = unhex(arg(5), n, MAXB);
    CRYPT_RESULT r;

    if (csz > MAXB) csz = MAXB;
    memset(c, 0, sizeof(c));
    r = _math__ModExp(csz, c, nm < 0 ? 0 : (UINT32)nm, nm < 0 ? NULL : m,
                      ne < 0 ? 0 : (UINT32)ne, ne < 0 ? NULL : e,
                      nn < 0 ? 0 : (UINT32)nn, nn < 0 ? NULL : n);
    emit(" r=%d", (int)r);
    emith("c", c, r > 0 ? (int)r : (int)csz);
}

static void do_div(void)
{
    /* div <numerator> <denominator> <qsize> <rsize> */
    B2           n, d, q, r;
    TPM2B       *pn, *pd;
    CRYPT_RESULT rc;

    memset(&q, 0, sizeof(q));
    memset(&r, 0, sizeof(r));
    pn = arg2b(&n, 2);
    pd = arg2b(&d, 3);
    q.t.size = (UINT16)argnum(4);
    r.t.size = (UINT16)argnum(5);
    rc = _math__Div(pn, pd, &q.b, &r.b);
    emit(" rc=%d", (int)rc);
    emit2b("q", &q.b);
    emit2b("r", &r.b);
}

static void do_mul(void)
{
    /* mul <a> <b> <psize>
     *
     * psize is the caller's declared room for the product and is an input the
     * engine checks twice: below aSize+bSize it returns CRYPT_PARAMETER, and
     * above MAX_2B_BYTES (256 here) it trips a pAssert.  Passing it from the
     * case file is what makes both refusals reachable.  "NULL" asks for the
     * pSize==NULL call. */
    BYTE   a[MAXB], b[MAXB], p[MAXB];
    int    na  = unhex(arg(2), a, MAXB);
    int    nb  = unhex(arg(3), b, MAXB);
    int    use = !isnull(arg(4));
    UINT32 psz = (UINT32)argnum(4);
    int    r;

    memset(p, 0, sizeof(p));
    r = _math__Mul(na < 0 ? 0 : (UINT32)na, na < 0 ? NULL : a,
                   nb < 0 ? 0 : (UINT32)nb, nb < 0 ? NULL : b,
                   use ? &psz : NULL, p);
    emit(" r=%d", r);
    if (use) emit(" size=%u", psz);
    emith("p", p, (na > 0 && nb > 0 && na + nb <= MAXB) ? na + nb : 0);
}

static void do_sub(void)
{
    /* sub <a> <b> */
    BYTE   a[MAXB], b[MAXB], c[MAXB];
    int    na = unhex(arg(2), a, MAXB);
    int    nb = unhex(arg(3), b, MAXB);
    UINT16 csz = 0;
    int    r;

    memset(c, 0, sizeof(c));
    r = _math__sub(na < 0 ? 0 : (UINT32)na, na < 0 ? NULL : a,
                   nb < 0 ? 0 : (UINT32)nb, nb < 0 ? NULL : b, &csz, c);
    emit(" r=%d size=%u", r, csz);
    emith("c", c, (int)csz);
}

static void do_dec(void)
{
    /* dec <a> */
    BYTE a[MAXB];
    int  na = unhex(arg(2), a, MAXB);

    if (na <= 0) {
        emit(" skipped=1");
        return;
    }
    _math__Dec((UINT32)na, a);
    emith("a", a, na);
}

static void do_comp(void)
{
    /* comp <a> <b> */
    BYTE a[MAXB], b[MAXB];
    int  na = unhex(arg(2), a, MAXB);
    int  nb = unhex(arg(3), b, MAXB);

    emit(" ucomp=%d comp=%d",
         _math__uComp(na < 0 ? 0 : (UINT32)na, na < 0 ? NULL : a,
                      nb < 0 ? 0 : (UINT32)nb, nb < 0 ? NULL : b),
         _math__Comp(na < 0 ? 0 : (UINT32)na, na < 0 ? NULL : a,
                     nb < 0 ? 0 : (UINT32)nb, nb < 0 ? NULL : b));
}

static void do_isprime(void)
{
    /* isprime <n> */
    emit(" prime=%d", (int)_math__IsPrime((UINT32)argnum(2)));
}

static void do_norm(void)
{
    /* norm <b> <denormsize> */
    B2     b;
    TPM2B *pb = arg2b(&b, 2);
    UINT16 n;
    BOOL   ok;

    n = _math__Normalize2B(pb);
    emit(" normalized=%u", n);
    emit2b("nb", pb);
    ok = _math__Denormalize2B(pb, (UINT32)argnum(3));
    emit(" denorm=%d", (int)ok);
    emit2b("db", pb);
}

/* ------------------------------------------------------------------ rng */

static void do_seededrandom(void)
{
    /* seededrandom <size> <hashAlg> <seed> <label> <partyU> <partyV> */
    BYTE   r[MAXB];
    B2     seed, pu, pv;
    TPM2B *pseed, *ppu, *ppv;
    INT32  size = (INT32)argnum(2);
    UINT16 n;

    if (size > MAXB) size = MAXB;
    memset(r, 0, sizeof(r));
    pseed = arg2b(&seed, 4);
    ppu   = arg2b(&pu, 6);
    ppv   = arg2b(&pv, 7);
    n = _cpri__GenerateSeededRandom(size, r, (TPM_ALG_ID)argnum(3), pseed,
                                    arglabel(5), ppu, ppv);
    emit(" n=%u", n);
    emith("bytes", r, n > MAXB ? MAXB : (int)n);
}

static void do_random(void)
{
    /* random <size> - the bytes are random, so only the reported count is
     * stable, and -- for draws wide enough that a collision is not ordinary
     * luck -- the fact that two draws differ.  Two independent draws of one
     * byte are equal once in 256, which is a fact about the size asked for and
     * not about the port, so the narrow sizes report the count alone.  The
     * shape of the line is fixed by <size>, which is an input. */
    BYTE   a[MAXB], b[MAXB];
    INT32  size = (INT32)argnum(2);
    UINT16 na, nb;

    if (size > MAXB) size = MAXB;
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    na = _cpri__GenerateRandom(size, a);
    nb = _cpri__GenerateRandom(size, b);
    emit(" na=%u nb=%u", na, nb);
    if (size >= 16)
        emit(" differ=%d", (na > 0 && na == nb) ? (memcmp(a, b, na) != 0) : -1);
}

static void do_stir(void)
{
    /* stir <entropy> */
    BYTE e[MAXB];
    int  n = unhex(arg(2), e, MAXB);

    emit(" r=%d", (int)_cpri__StirRandom(n < 0 ? 0 : (INT32)n,
                                         n < 0 ? NULL : e));
}

static void do_drbgstate(void)
{
    /* drbgstate <GET|PUT> <buffer> */
    BYTE b[MAXB];
    int  n   = unhex(arg(3), b, MAXB);
    int  put = arg(2) != NULL && arg(2)[0] == 'P';

    emit(" r=%d", (int)_cpri__DrbgGetPutState(put ? PUT_STATE : GET_STATE,
                                              n < 0 ? 0 : n,
                                              n < 0 ? NULL : b));
}

static void do_startup(void)
{
    /* startup - one call to _cpri__Startup() is not a function of its inputs.
     * _cpri__RngStartup collects MAX_RNG_ENTROPY_SIZE bytes four at a time and
     * fails if two consecutive words are equal, and the Linux rand_s shim fills
     * only the low byte of each word (platform/Entropy.c), so the 15 comparisons
     * repeat once in about twenty runs.  Attempts are independent -- entry to
     * _cpri__RngStartup clears s_entropyFailure and _plat__GetEntropy(NULL, 0)
     * resets the previous-value comparison -- so a disjunction over eight of
     * them is 1 unless startup is genuinely broken, and the whole startup path
     * (hash, RNG, RSA, ECC, symmetric) stays scored.  A port that fails startup
     * outright still reports 0. */
    int i, ok = 0;

    for (i = 0; i < 8 && !ok; i++)
        ok = ((int)_cpri__Startup() != 0);
    emit(" startup=%d", ok);
}

/* ------------------------------------------------------------- dispatch */

static void dispatch(void)
{
    const char *op = arg(1);

    if (op == NULL) { emit(" empty=1"); return; }

    if      (!strcmp(op, "hashinfo"))      do_hashinfo();
    else if (!strcmp(op, "hashindex"))     do_hashindex();
    else if (!strcmp(op, "hash"))          do_hash();
    else if (!strcmp(op, "hashafter"))     do_hashafter();
    else if (!strcmp(op, "hashcopy"))      do_hashcopy();
    else if (!strcmp(op, "hashmove"))      do_hashmove();
    else if (!strcmp(op, "hashmove2"))     do_hashmove2();
    else if (!strcmp(op, "hashmove3"))     do_hashmove3();
    else if (!strcmp(op, "hashmovedrop"))  do_hashmovedrop();
    else if (!strcmp(op, "hashmoveafter")) do_hashmoveafter();
    else if (!strcmp(op, "hashexport"))    do_hashexport();
    else if (!strcmp(op, "hashblock"))     do_hashblock();
    else if (!strcmp(op, "hmac"))          do_hmac();
    else if (!strcmp(op, "mgf1"))          do_mgf1();
    else if (!strcmp(op, "kdfa"))          do_kdfa();
    else if (!strcmp(op, "kdfe"))          do_kdfe();
    else if (!strcmp(op, "sym"))           do_sym();
    else if (!strcmp(op, "symroundtrip"))  do_symroundtrip();
    else if (!strcmp(op, "symblock"))      do_symblock();
    else if (!strcmp(op, "modexp"))        do_modexp();
    else if (!strcmp(op, "div"))           do_div();
    else if (!strcmp(op, "mul"))           do_mul();
    else if (!strcmp(op, "sub"))           do_sub();
    else if (!strcmp(op, "dec"))           do_dec();
    else if (!strcmp(op, "comp"))          do_comp();
    else if (!strcmp(op, "isprime"))       do_isprime();
    else if (!strcmp(op, "norm"))          do_norm();
    else if (!strcmp(op, "seededrandom"))  do_seededrandom();
    else if (!strcmp(op, "random"))        do_random();
    else if (!strcmp(op, "stir"))          do_stir();
    else if (!strcmp(op, "drbgstate"))     do_drbgstate();
    else if (!strcmp(op, "startup"))       do_startup();
#ifdef TPM_ALG_RSA
    else if (!strcmp(op, "rsatest"))       do_rsatest();
    else if (!strcmp(op, "rsaenc"))        do_rsaenc();
    else if (!strcmp(op, "rsadec"))        do_rsadec();
    else if (!strcmp(op, "rsaroundtrip"))  do_rsaroundtrip();
    else if (!strcmp(op, "rsasign"))       do_rsasign();
    else if (!strcmp(op, "rsasignverify")) do_rsasignverify();
    else if (!strcmp(op, "rsaverify"))     do_rsaverify();
#ifndef RSA_KEY_SIEVE
    else if (!strcmp(op, "rsagen"))        do_rsagen();
#endif
#endif
#ifdef TPM_ALG_ECC
    else if (!strcmp(op, "eccinfo"))       do_eccinfo();
    else if (!strcmp(op, "ecccurves"))     do_ecccurves();
    else if (!strcmp(op, "curvebyindex"))  do_curvebyindex();
    else if (!strcmp(op, "eccmul"))        do_eccmul();
    else if (!strcmp(op, "eccpoint"))      do_eccpoint();
    else if (!strcmp(op, "eccgen"))        do_eccgen();
    else if (!strcmp(op, "eccephemeral"))  do_eccephemeral();
    else if (!strcmp(op, "eccrange"))      do_eccrange();
    else if (!strcmp(op, "eccsign"))       do_eccsign();
    else if (!strcmp(op, "eccsignverify")) do_eccsignverify();
    else if (!strcmp(op, "eccverify"))     do_eccverify();
    else if (!strcmp(op, "ecccommit"))     do_ecccommit();
#if CC_ZGen_2Phase == YES
    else if (!strcmp(op, "ecc22"))         do_ecc22();
#endif
#endif
    else emit(" unknownop=1");
}

/* Set from CPRIDRV_MARK.  A sanitizer report arrives on stderr, which is a
 * stream with no case in it; with the marker on, the screen can name the case
 * that provoked each one.  Off in every scored run -- no verifier sets it --
 * so the driver a candidate is graded with prints exactly what it always did.
 */
static int g_mark = 0;

int main(int argc, char **argv)
{
    static char buf[65536];
    FILE       *in;
    long        first = 0, index = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <cases> <out> [first]\n", argv[0]);
        return 2;
    }
    if (argc > 3) first = strtol(argv[3], NULL, 10);
    g_mark = getenv("CPRIDRV_MARK") != NULL;

    in = fopen(argv[1], "r");
    if (in == NULL) { perror(argv[1]); return 2; }
    g_out = fopen(argv[2], first > 0 ? "a" : "w");
    if (g_out == NULL) { perror(argv[2]); return 2; }

    signal(SIGSEGV, on_signal);
    signal(SIGBUS, on_signal);
    signal(SIGFPE, on_signal);
    signal(SIGABRT, on_signal);
    signal(SIGILL, on_signal);

    _cpri__InitCryptoUnits(&on_tpm_fail);

    while (fgets(buf, sizeof(buf), in) != NULL) {
        char *p = buf, *save = NULL;
        int   jumped;

        if (index++ < first) continue;
        g_ntok = 0;
        for (p = strtok_r(buf, " \t\r\n", &save);
             p != NULL && g_ntok < MAXTOK;
             p = strtok_r(NULL, " \t\r\n", &save))
            g_tok[g_ntok++] = p;
        if (g_ntok == 0 || g_tok[0][0] == '#') continue;

        g_len = 0;
        g_line[0] = 0;
        emit("%s", g_tok[0]);
        if (g_mark) { fprintf(stderr, "## %s\n", g_tok[0]); fflush(stderr); }
        g_failcode = 0;
        g_signal = 0;
        g_trunc = 0;

        jumped = sigsetjmp(g_jmp, 1);
        g_armed = 1;
        if (jumped == 0)
            dispatch();
        g_armed = 0;

        if (jumped == 1)
            emit(" tpmfail=%d", g_failcode);
        else if (jumped == 2)
            emit(" signal=%d", g_signal);
        if (g_trunc)
            emit(" argtrunc=1");
        fprintf(g_out, "%s\n", g_line);
        fflush(g_out);
    }
    fclose(g_out);
    fclose(in);
    return 0;
}
