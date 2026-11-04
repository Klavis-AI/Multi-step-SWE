/* fuzzdrv.c (step 10) - the loop the candidate does not get to write.
 *
 * Verifier-owned: this is step 10's instrument and nobody else's.
 *
 * WHY THIS FILE EXISTS AT ALL
 *
 * The migration line's tenth step asks for a "fuzzing harness".  A harness is
 * two halves -- an entry point that turns bytes into a call on the code under
 * test, and a loop that produces the bytes -- and only the first half is the
 * candidate's business.  A step that asked for both would be a step whose
 * substantive claim ("it found nothing") is measured by a loop the candidate
 * wrote, and a loop that runs the seed corpus twice and stops finds nothing on
 * every tree there is.  So the entry point is asked for, by its standard name,
 * and the loop is here.
 *
 *     int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
 *
 * That is libFuzzer's signature, and it is used for the reason a standard name
 * is ever used: it is the one a port would write anyway, it is documented
 * outside this package, and a harness written against it is worth something to
 * the repository after this benchmark is done with it.  What it is NOT is
 * libFuzzer: `-fsanitize=fuzzer' is a clang feature and this image has gcc, so
 * there is no coverage-guided mutation here and no claim of any.  What this
 * driver does is stated exactly:
 *
 *   - every seed, once, in a defined order;
 *   - then ITERS further inputs, each one a mutation of a seed or of an earlier
 *     mutant, from a PRNG whose seed is a constant in this file.
 *
 * DETERMINISM IS THE POINT.  Nothing here reads a clock, an
 * environment variable, /dev/urandom, or an address.  Two runs of this driver
 * over the same corpus with the same ITERS make the same calls in the same
 * order with the same bytes, so "the harness found nothing" is a fact that can
 * be re-checked, and a fixture that reintroduces a defect fails reproducibly
 * rather than usually.  The iteration count is a count and not a time budget
 * for the same reason: a gate that stops when the clock says so is a gate whose
 * verdict depends on what else the machine was doing.
 *
 * HOW A FINDING IS REPORTED.  It is not.  The build this driver is compiled
 * into carries -fsanitize=address,undefined -fno-sanitize-recover=all, so a
 * sanitizer report ends the process; and a harness that crashes on its own ends
 * it too.  Either way the exit status is non-zero and the last `exec' line this
 * driver printed names the input that did it.  There is no catch, no continue
 * and no "N crashes found" summary, because a driver that survives a finding is
 * a driver that can lose one.
 */

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The candidate's half.  Declared, never defined here; the link fails if the
 * tree does not supply it, which is requirement 1 of the gate and is reported
 * as "no harness compiles" rather than as a driver fault. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* libFuzzer's optional one-time hook.  Weak, so a harness that does not want
 * one links without writing an empty function; called before any input if it
 * is there.  argc/argv are this driver's, which is what libFuzzer passes too.
 */
__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);

#define MAX_SEEDS   512         /* files read out of the corpus directory  */
#define MAX_LEN     65536       /* bytes of any one input, seed or mutant  */
#define PRNG_SEED   0x9E3779B97F4A7C15ULL

struct input {
    uint8_t *buf;
    size_t   len;
};

static struct input g_seed[MAX_SEEDS];
static int          g_seeds;

/* xorshift64*.  Chosen because it is four lines, has no state to initialise
 * beyond a constant, and is identical on every target this will ever run on.
 * It is not a cryptographic generator and does not need to be: its whole job is
 * to be the same sequence twice. */
static uint64_t g_state = PRNG_SEED;

static uint64_t rnd(void)
{
    g_state ^= g_state >> 12;
    g_state ^= g_state << 25;
    g_state ^= g_state >> 27;
    return g_state * 0x2545F4914F6CDD1DULL;
}

static size_t rnd_below(size_t n)      /* 0 .. n-1, and 0 when n is 0 */
{
    return n ? (size_t)(rnd() % (uint64_t)n) : 0;
}

/* --- the corpus ---------------------------------------------------------- */

/* Sorted by name, because readdir() order is a fact about the filesystem and
 * this driver is not allowed to have any of those.  strcmp and not strcoll: a
 * locale is another thing that would have to be pinned. */
static int by_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int read_corpus(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char *names[MAX_SEEDS];
    int n = 0, i;

    if (d == NULL) {
        fprintf(stderr, "fuzzdrv: corpus directory %s cannot be opened\n", dir);
        return -1;
    }
    while ((e = readdir(d)) != NULL && n < MAX_SEEDS) {
        char path[4096];
        struct stat st;
        if (e->d_name[0] == '.')
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name)
                >= (int)sizeof(path))
            continue;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        names[n] = strdup(e->d_name);
        if (names[n] == NULL)
            break;
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), by_name);

    for (i = 0; i < n; i++) {
        char path[4096];
        FILE *fh;
        long len;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        fh = fopen(path, "rb");
        if (fh == NULL) {
            free(names[i]);
            continue;
        }
        fseek(fh, 0, SEEK_END);
        len = ftell(fh);
        fseek(fh, 0, SEEK_SET);
        if (len < 0)
            len = 0;
        if (len > MAX_LEN)
            len = MAX_LEN;
        /* Allocated at exactly the length read, deliberately: an input handed
         * to the harness in a buffer larger than its size is an input whose
         * overrun ASan cannot see, and the one thing this driver must never do
         * is hide the class of defect it exists to find. */
        g_seed[g_seeds].buf = (uint8_t *)malloc((size_t)len ? (size_t)len : 1);
        if (g_seed[g_seeds].buf == NULL) {
            fclose(fh);
            free(names[i]);
            continue;
        }
        if (len > 0 && fread(g_seed[g_seeds].buf, 1, (size_t)len, fh)
                != (size_t)len) {
            free(g_seed[g_seeds].buf);
            fclose(fh);
            free(names[i]);
            continue;
        }
        g_seed[g_seeds].len = (size_t)len;
        g_seeds++;
        fclose(fh);
        free(names[i]);
    }
    return 0;
}

/* --- mutation ------------------------------------------------------------ */

/* Seven operators, all length-aware, none of them clever.  The point of the
 * set is coverage of SHAPES -- shorter, longer, one bit different, a piece of
 * another input spliced in -- rather than of formats: this driver knows nothing
 * about what the harness parses, and a mutator that did would be a second
 * place for the step's content to live. */
static size_t mutate(uint8_t *out, const uint8_t *in, size_t len)
{
    size_t n = len > MAX_LEN ? MAX_LEN : len;
    int rounds = 1 + (int)rnd_below(4);
    int r;

    if (n)
        memcpy(out, in, n);

    for (r = 0; r < rounds; r++) {
        switch (rnd() % 7) {
        case 0:                                     /* flip one bit */
            if (n)
                out[rnd_below(n)] ^= (uint8_t)(1u << (rnd() % 8));
            break;
        case 1:                                     /* set one byte */
            if (n)
                out[rnd_below(n)] = (uint8_t)(rnd() & 0xff);
            break;
        case 2: {                                   /* insert one byte */
            size_t at;
            if (n >= MAX_LEN)
                break;
            at = rnd_below(n + 1);
            memmove(out + at + 1, out + at, n - at);
            out[at] = (uint8_t)(rnd() & 0xff);
            n++;
            break;
        }
        case 3: {                                   /* erase one byte */
            size_t at;
            if (n == 0)
                break;
            at = rnd_below(n);
            memmove(out + at, out + at + 1, n - at - 1);
            n--;
            break;
        }
        case 4:                                     /* truncate */
            if (n)
                n = rnd_below(n);
            break;
        case 5: {                                   /* extend with a run */
            size_t add = 1 + rnd_below(32);
            uint8_t fill = (uint8_t)(rnd() & 0xff);
            if (n + add > MAX_LEN)
                add = MAX_LEN - n;
            memset(out + n, fill, add);
            n += add;
            break;
        }
        case 6: {                                   /* splice from a seed */
            const struct input *s;
            size_t take, at;
            if (g_seeds == 0)
                break;
            s = &g_seed[rnd_below((size_t)g_seeds)];
            if (s->len == 0)
                break;
            take = 1 + rnd_below(s->len);
            if (take > MAX_LEN - n)
                take = MAX_LEN - n;
            if (take == 0)
                break;
            at = rnd_below(s->len - take + 1);
            memcpy(out + n, s->buf + at, take);
            n += take;
            break;
        }
        }
    }
    return n;
}

/* --- the loop ------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *dir;
    long iters;
    long i;
    uint8_t *work, *mutant;
    size_t wlen;

    if (argc < 3) {
        fprintf(stderr, "usage: fuzzdrv <corpus-dir> <iterations>\n");
        return 2;
    }
    dir = argv[1];
    iters = strtol(argv[2], NULL, 10);
    if (iters < 0)
        iters = 0;

    if (read_corpus(dir) != 0)
        return 2;
    printf("fuzzdrv: seeds=%d dir=%s iters=%ld\n", g_seeds, dir, iters);
    fflush(stdout);

    if (LLVMFuzzerInitialize)
        LLVMFuzzerInitialize(&argc, &argv);

    /* Both buffers are MAX_LEN because mutate() grows into them; the call
     * itself is always made with the exact length, never with the capacity. */
    work = (uint8_t *)malloc(MAX_LEN);
    mutant = (uint8_t *)malloc(MAX_LEN);
    if (work == NULL || mutant == NULL) {
        fprintf(stderr, "fuzzdrv: out of memory\n");
        return 2;
    }

    /* Pass one: every seed, once, in name order.  A harness that cannot
     * survive its own corpus is a finding, and it is the first one made. */
    for (i = 0; i < g_seeds; i++) {
        printf("fuzzdrv: exec seed %ld len %zu\n", i, g_seed[i].len);
        fflush(stdout);
        LLVMFuzzerTestOneInput(g_seed[i].buf, g_seed[i].len);
    }

    /* Pass two: mutants.  The base alternates between a seed and the previous
     * mutant, so the walk neither stays next to the corpus forever nor drifts
     * away from it and never comes back. */
    wlen = 0;
    if (g_seeds > 0) {
        wlen = g_seed[0].len;
        if (wlen)
            memcpy(work, g_seed[0].buf, wlen);
    }
    for (i = 0; i < iters; i++) {
        size_t mlen;
        if (g_seeds > 0 && (i & 1) == 0) {
            const struct input *s = &g_seed[rnd_below((size_t)g_seeds)];
            wlen = s->len;
            if (wlen)
                memcpy(work, s->buf, wlen);
        }
        mlen = mutate(mutant, work, wlen);
        printf("fuzzdrv: exec mutant %ld len %zu\n", i, mlen);
        fflush(stdout);
        {
            /* Handed over at exactly its length, like a seed: from the
             * MAX_LEN buffer, a harness that reads past mlen reads the
             * buffer's own tail and AddressSanitizer has nothing to say. */
            uint8_t *exact = (uint8_t *)malloc(mlen ? mlen : 1);
            if (exact == NULL) {
                fprintf(stderr, "fuzzdrv: out of memory\n");
                return 2;
            }
            if (mlen)
                memcpy(exact, mutant, mlen);
            LLVMFuzzerTestOneInput(exact, mlen);
            free(exact);
        }
        memcpy(work, mutant, mlen);
        wlen = mlen;
    }

    /* Everything this driver allocated, given back.  Not tidiness: the build
     * this is compiled into runs LeakSanitizer, a leak in the HARNESS is one of
     * the findings this gate exists to make, and a driver that leaks its own
     * two buffers makes that finding on every tree there is.  Measured, not
     * assumed -- the first build of this file reported
     * "Direct leak of 65536 byte(s) in 1 object(s) ... in main" and exited 1
     * before any harness had done anything at all.  Verify the instrument
     * before the finding. */
    free(work);
    free(mutant);
    for (i = 0; i < g_seeds; i++)
        free(g_seed[i].buf);

    printf("fuzzdrv: done seeds=%d execs=%ld\n", g_seeds,
           (long)g_seeds + iters);
    fflush(stdout);
    return 0;
}
