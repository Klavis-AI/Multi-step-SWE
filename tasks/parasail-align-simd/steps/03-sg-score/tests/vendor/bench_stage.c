/*
 * bench_stage -- the per-function harness for the parasail ladder.
 *
 * The library is loaded with dlopen() and every entry point is reached with
 * dlsym(), so the harness never has to be relinked against a tree the
 * candidate changed: it is compiled once, against a fixed header, and drives
 * whatever libparasail.so it is pointed at.  The library's own public
 * dispatcher entry points are what is called -- `parasail_sw_striped_16()` and
 * friends dereference the `parasail_sw_striped_16_pointer` the library
 * installed -- so a candidate that optimizes the portable C path is measured
 * exactly like one that writes a kernel.
 *
 *   bench_stage <lib.so> exists <name>
 *   bench_stage <lib.so> dump   <name> <seed> <n1> <n2>
 *   bench_stage <lib.so> bench  <name> <seed> <n1> <n2> <reps>
 *   bench_stage <lib.so> digest <listfile> <seed> <n1> <n2>
 *
 * `exists` prints 1 or 0.  `dump` prints `H <32 hex digits>`: a 128-bit
 * FNV-1a over the semantic content of the result -- score, end positions,
 * the class/kind/saturated bits of the flag word, and whichever of the
 * statistics, score table, last row/column or decoded CIGAR the result
 * carries.  The vectorization bits of the flag word (NOVEC / STRIPED / BITS_*
 * / LANES_*) are deliberately NOT part of it: the base answers from the
 * portable C reference and the candidate from a kernel, and those two are
 * supposed to differ there.  Everything a caller can observe is in.
 *
 * `bench` prints `NS <median nanoseconds>` over <reps> repetitions, each on a
 * freshly drawn sequence pair in freshly allocated buffers (see the comment in
 * the bench loop: reused buffers would make the draw's *contents* fresh but
 * its *addresses* constant, which a content-blind cache can exploit).
 * `digest` folds a whole list of entry points into one digest, which is the
 * library-wide consistency guard.
 *
 * Sequences are drawn inside this process from <seed>, so no workload file
 * ever reaches the candidate's tree and the draw is a property of the seed
 * the caller chose.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "parasail.h"

typedef parasail_result_t *(*pfn_t)(const char *const, const int,
                                    const char *const, const int,
                                    const int, const int,
                                    const parasail_matrix_t *);
typedef const parasail_matrix_t *(*mk_t)(const char *, int, int);
typedef parasail_cigar_t *(*get_cigar_t)(parasail_result_t *, const char *,
                                         const int, const char *, const int,
                                         const parasail_matrix_t *);
typedef char *(*decode_t)(parasail_cigar_t *);

static const int OPEN = 10;
static const int GAP = 1;
static const char *ALPHA = "ACDEFGHIKLMNPQRSTVWY";

/* ------------------------------------------------------------- hashing --- */

static uint64_t h1 = 0xcbf29ce484222325ULL;
static uint64_t h2 = 0x9e3779b97f4a7c15ULL;

static void h_init(void) {
    h1 = 0xcbf29ce484222325ULL;
    h2 = 0x9e3779b97f4a7c15ULL;
}

static void h_update(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h1 = (h1 ^ b[i]) * 0x100000001b3ULL;
        h2 = (h2 + b[i]) * 0xff51afd7ed558ccdULL;
        h2 ^= h2 >> 29;
    }
}

static void h_i32(int v) { h_update(&v, sizeof v); }
static void h_u32(uint32_t v) { h_update(&v, sizeof v); }

static void h_str(const char *s) {
    if (!s) { h_u32(0xffffffffu); return; }
    h_u32((uint32_t)strlen(s));
    h_update(s, strlen(s));
}

static void h_print(void) {
    printf("H %016llx%016llx\n", (unsigned long long)h1,
           (unsigned long long)h2);
}

/* -------------------------------------------------------------- inputs --- */

static uint64_t rs;

static uint32_t rnd(void) {
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return (uint32_t)(rs >> 32);
}

static char *gen(char *buf, int n) {
    int i;
    for (i = 0; i < n; i++) buf[i] = ALPHA[rnd() % 20u];
    buf[n] = '\0';
    return buf;
}

/* --------------------------------------------------------------- result --- */

/* The class and kind bits a caller can see; the vectorization bits are not
 * part of the comparison (see the file comment). */
#define KIND_BITS (PARASAIL_FLAG_NW | PARASAIL_FLAG_SG | PARASAIL_FLAG_SW | \
                   PARASAIL_FLAG_SG_S1_BEG | PARASAIL_FLAG_SG_S1_END |      \
                   PARASAIL_FLAG_SG_S2_BEG | PARASAIL_FLAG_SG_S2_END |      \
                   PARASAIL_FLAG_SATURATED | PARASAIL_FLAG_STATS |          \
                   PARASAIL_FLAG_TABLE | PARASAIL_FLAG_ROWCOL |             \
                   PARASAIL_FLAG_TRACE)

static void h_result(parasail_result_t *r, const char *s1, int l1,
                     const char *s2, int l2, const parasail_matrix_t *m,
                     get_cigar_t get_cigar, decode_t decode) {
    if (!r) { h_u32(0xdeadbeefu); return; }
    h_i32(r->score);
    h_i32(r->end_query);
    h_i32(r->end_ref);
    h_u32((uint32_t)(r->flag & KIND_BITS));

    /* parasail's result carries ONE tagged union, and a statistics result
     * nests the table/rowcol payload under it (parasail_result_extra_stats_t
     * has its own union of tables/rowcols).  Reading the top-level member when
     * STATS is set walks into the wrong struct. */
    if (r->flag & PARASAIL_FLAG_STATS) {
        parasail_result_extra_stats_t *st = r->stats;
        if (!st) {
            h_u32(0x57f5beefu);
        } else {
            h_i32(st->matches); h_i32(st->similar); h_i32(st->length);
            if (r->flag & PARASAIL_FLAG_TABLE) {
                parasail_result_extra_stats_tables_t *t = st->tables;
                if (!t || !t->score_table) h_u32(0x0000beefu);
                else {
                    h_i32(l1); h_i32(l2);
                    h_update(t->score_table, (size_t)l1 * (size_t)l2 * sizeof(int));
                    h_update(t->matches_table, (size_t)l1 * (size_t)l2 * sizeof(int));
                    h_update(t->similar_table, (size_t)l1 * (size_t)l2 * sizeof(int));
                    h_update(t->length_table, (size_t)l1 * (size_t)l2 * sizeof(int));
                }
            } else {
                h_u32(0x22222222u);
            }
            if (r->flag & PARASAIL_FLAG_ROWCOL) {
                parasail_result_extra_stats_rowcols_t *rc = st->rowcols;
                if (!rc || !rc->score_row || !rc->score_col) h_u32(0x00beef00u);
                else {
                    h_i32(l1);
                    h_update(rc->score_col, (size_t)l1 * sizeof(int));
                    h_update(rc->matches_col, (size_t)l1 * sizeof(int));
                    h_update(rc->similar_col, (size_t)l1 * sizeof(int));
                    h_update(rc->length_col, (size_t)l1 * sizeof(int));
                    h_i32(l2);
                    h_update(rc->score_row, (size_t)l2 * sizeof(int));
                    h_update(rc->matches_row, (size_t)l2 * sizeof(int));
                    h_update(rc->similar_row, (size_t)l2 * sizeof(int));
                    h_update(rc->length_row, (size_t)l2 * sizeof(int));
                }
            } else {
                h_u32(0x33333333u);
            }
        }
    } else {
        h_u32(0x11111111u);
        if (r->flag & PARASAIL_FLAG_TABLE) {
            parasail_result_extra_tables_t *t = r->tables;
            if (!t || !t->score_table) h_u32(0x0000beefu);
            else { h_i32(l1); h_i32(l2);
                   h_update(t->score_table, (size_t)l1 * (size_t)l2 * sizeof(int)); }
        } else {
            h_u32(0x22222222u);
        }
        if (r->flag & PARASAIL_FLAG_ROWCOL) {
            parasail_result_extra_rowcols_t *rc = r->rowcols;
            if (!rc || !rc->score_row || !rc->score_col) h_u32(0x00beef00u);
            else { h_i32(l1); h_update(rc->score_col, (size_t)l1 * sizeof(int));
                   h_i32(l2); h_update(rc->score_row, (size_t)l2 * sizeof(int)); }
        } else {
            h_u32(0x33333333u);
        }
    }
    if (r->flag & PARASAIL_FLAG_TRACE) {
        parasail_cigar_t *cg = get_cigar ? get_cigar(r, s1, l1, s2, l2, m) : NULL;
        if (!cg) { h_u32(0x0badc0deu); }
        else {
            char *txt = decode ? decode(cg) : NULL;
            h_i32(cg->beg_query);
            h_i32(cg->beg_ref);
            h_str(txt);
        }
    } else {
        h_u32(0x44444444u);
    }
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    void *h;
    const parasail_matrix_t *m;
    mk_t mk;
    get_cigar_t get_cigar;
    decode_t decode;
    const char *mode;

    if (argc < 4) {
        fprintf(stderr, "usage: bench_stage <lib.so> exists|dump|bench|digest "
                        "<name> [seed n1 n2 reps]\n");
        return 2;
    }
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 3; }
    mk = (mk_t)dlsym(h, "parasail_matrix_create");
    if (!mk) { fprintf(stderr, "no parasail_matrix_create\n"); return 3; }
    m = mk("blosum62", OPEN, GAP);
    if (!m) { fprintf(stderr, "no blosum62\n"); return 3; }
    get_cigar = (get_cigar_t)dlsym(h, "parasail_result_get_cigar");
    decode = (decode_t)dlsym(h, "parasail_cigar_decode");

    mode = argv[2];
    if (strcmp(mode, "exists") == 0) {
        char nm[256];
        snprintf(nm, sizeof nm, "parasail_%s", argv[3]);
        printf("%d\n", dlsym(h, nm) != NULL);
        return 0;
    }
    if (strcmp(mode, "width") == 0 || strcmp(mode, "widths") == 0) {
        char nm[256];
        pfn_t f;
        parasail_result_t *r;
        char *s1, *s2;
        int n1, n2, w = 0;
        if (argc < 7) { fprintf(stderr, "width needs name seed n1 n2\n"); return 2; }
        rs = (uint64_t)strtoull(argv[4], NULL, 10) * 2654435761ULL + 1;
        n1 = atoi(argv[5]);
        n2 = atoi(argv[6]);
        s1 = malloc((size_t)n1 + 1);
        s2 = malloc((size_t)n2 + 1);
        gen(s1, n1); gen(s2, n2);
        snprintf(nm, sizeof nm, "parasail_%s", argv[3]);
        f = (pfn_t)dlsym(h, nm);
        if (!f) { printf("W -1\n"); return 0; }
        r = f(s1, n1, s2, n2, OPEN, GAP, m);
        if (!r) { printf("W -1\n"); return 0; }
        if (r->flag & PARASAIL_FLAG_BITS_8) w = 8;
        else if (r->flag & PARASAIL_FLAG_BITS_16) w = 16;
        else if (r->flag & PARASAIL_FLAG_BITS_32) w = 32;
        else if (r->flag & PARASAIL_FLAG_BITS_64) w = 64;
        printf("W %d\n", w);
        return 0;
    }
    if (strcmp(mode, "digest") == 0) {
        FILE *lf = fopen(argv[3], "r");
        char line[256];
        char *s1, *s2;
        int n1 = atoi(argv[5]), n2 = atoi(argv[6]);
        if (!lf) { fprintf(stderr, "no list %s\n", argv[3]); return 3; }
        rs = (uint64_t)strtoull(argv[4], NULL, 10) * 2654435761ULL + 1;
        s1 = malloc((size_t)n1 + 1);
        s2 = malloc((size_t)n2 + 1);
        gen(s1, n1); gen(s2, n2);
        h_init();
        while (fgets(line, sizeof line, lf)) {
            char nm[256];
            pfn_t f;
            parasail_result_t *r;
            size_t k = strlen(line);
            while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
            if (!k || line[0] == '#') continue;
            snprintf(nm, sizeof nm, "parasail_%s", line);
            f = (pfn_t)dlsym(h, nm);
            h_str(line);
            if (!f) { h_u32(0x4d495353u); continue; }
            r = f(s1, n1, s2, n2, OPEN, GAP, m);
            h_result(r, s1, n1, s2, n2, m, get_cigar, decode);
        }
        h_print();
        return 0;
    }
    if (strcmp(mode, "dump") == 0) {
        char nm[256];
        pfn_t f;
        parasail_result_t *r;
        char *s1, *s2;
        int n1, n2;
        if (argc < 7) { fprintf(stderr, "dump needs name seed n1 n2\n"); return 2; }
        rs = (uint64_t)strtoull(argv[4], NULL, 10) * 2654435761ULL + 1;
        n1 = atoi(argv[5]);
        n2 = atoi(argv[6]);
        s1 = malloc((size_t)n1 + 1);
        s2 = malloc((size_t)n2 + 1);
        gen(s1, n1); gen(s2, n2);
        snprintf(nm, sizeof nm, "parasail_%s", argv[3]);
        f = (pfn_t)dlsym(h, nm);
        if (!f) { printf("M MISSING\n"); return 0; }
        r = f(s1, n1, s2, n2, OPEN, GAP, m);
        h_init();
        h_result(r, s1, n1, s2, n2, m, get_cigar, decode);
        h_print();
        return 0;
    }
    if (strcmp(mode, "bench") == 0) {
        char nm[256];
        pfn_t f;
        int n1, n2, reps, i;
        uint64_t seed;
        double *t;
        if (argc < 8) { fprintf(stderr, "bench needs name seed n1 n2 reps\n"); return 2; }
        seed = (uint64_t)strtoull(argv[4], NULL, 10);
        n1 = atoi(argv[5]);
        n2 = atoi(argv[6]);
        reps = atoi(argv[7]);
        if (reps < 1) reps = 1;
        snprintf(nm, sizeof nm, "parasail_%s", argv[3]);
        f = (pfn_t)dlsym(h, nm);
        if (!f) { printf("MISSING\n"); return 0; }
        t = malloc((size_t)reps * sizeof(double));
        for (i = 0; i < reps; i++) {
            double t0, t1;
            parasail_result_t *r;
            char *b1, *b2;
            /* A fresh draw every repetition, in fresh buffers.  The contents
             * change with the draw either way, but reusing the buffers would
             * keep the SAME two addresses for every repetition, and a cache
             * keyed on (pointer, length) -- blind to the contents -- would
             * then answer repetitions 2..reps out of its own memory at ~0 ns
             * while every correctness gate still passed.  The allocation is
             * outside the timed region, so what is timed is still the entry
             * point's own work on a draw like any other. */
            rs = (seed + (uint64_t)i) * 2654435761ULL + 1;
            b1 = malloc((size_t)n1 + 1);
            b2 = malloc((size_t)n2 + 1);
            gen(b1, n1); gen(b2, n2);
            t0 = now();
            r = f(b1, n1, b2, n2, OPEN, GAP, m);
            t1 = now();
            (void)r;
            t[i] = (t1 - t0) * 1e9;
            free(b1); free(b2);
        }
        qsort(t, (size_t)reps, sizeof(double), cmp_double);
        printf("NS %.0f\n", t[reps / 2]);
        return 0;
    }
    if (strcmp(mode, "run") == 0) {
        /* One process, one line per entry -- the batch form the verifier uses.
         * `run <lib> run <listfile> <seed> <n1> <n2> <reps> <dump|bench>` */
        FILE *lf;
        char line[256];
        char *s1, *s2;
        int n1, n2, reps, i;
        uint64_t seed;
        int do_bench;   /* 1 bench, 0 dump, -1 widths */
        if (argc < 9) { fprintf(stderr, "run needs list seed n1 n2 reps dump|bench\n"); return 2; }
        lf = fopen(argv[3], "r");
        if (!lf) { fprintf(stderr, "no list %s\n", argv[3]); return 3; }
        seed = (uint64_t)strtoull(argv[4], NULL, 10);
        n1 = atoi(argv[5]);
        n2 = atoi(argv[6]);
        reps = atoi(argv[7]);
        if (reps < 1) reps = 1;
        if (strcmp(argv[8], "bench") == 0) do_bench = 1;
        else if (strcmp(argv[8], "widths") == 0) do_bench = -1;
        else if (strcmp(argv[8], "pipe") == 0) do_bench = 2;
        else do_bench = 0;
        s1 = malloc((size_t)n1 + 1);
        s2 = malloc((size_t)n2 + 1);
        while (fgets(line, sizeof line, lf)) {
            char nm[256];
            pfn_t f;
            size_t k = strlen(line);
            while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
            if (!k || line[0] == '#') continue;
            snprintf(nm, sizeof nm, "parasail_%s", line);
            f = (pfn_t)dlsym(h, nm);
            if (!f) { printf("%s MISSING\n", line); continue; }
            if (do_bench == 2) {
                /* `pipe` mode: one whole-library number -- the median over
                 * repetitions of the time to run EVERY entry point in the
                 * list once, on a draw of the shape the caller chose.  Used by
                 * the end-to-end non-regression gate, which gives it a
                 * different input shape from the scored draw. */
                double *t = malloc((size_t)reps * sizeof(double));
                double total = 0;
                int r;
                for (r = 0; r < reps; r++) {
                    double t0, t1;
                    size_t k = strlen(line);
                    char *b1, *b2;
                    while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
                    /* fresh buffers here too: same reason as the bench loop
                     * above -- a constant address pair across repetitions is
                     * what a content-blind cache exploits. */
                    rs = (seed + (uint64_t)r) * 2654435761ULL + 1;
                    b1 = malloc((size_t)n1 + 1);
                    b2 = malloc((size_t)n2 + 1);
                    gen(b1, n1); gen(b2, n2);
                    t0 = now();
                    {
                        FILE *lf2 = fopen(argv[3], "r");
                        char l2[256];
                        while (lf2 && fgets(l2, sizeof l2, lf2)) {
                            char nm2[256];
                            size_t k2 = strlen(l2);
                            pfn_t f2;
                            while (k2 && (l2[k2-1] == '\n' || l2[k2-1] == '\r')) l2[--k2] = 0;
                            if (!k2 || l2[0] == '#') continue;
                            snprintf(nm2, sizeof nm2, "parasail_%s", l2);
                            f2 = (pfn_t)dlsym(h, nm2);
                            if (f2) (void)f2(b1, n1, b2, n2, OPEN, GAP, m);
                        }
                        if (lf2) fclose(lf2);
                    }
                    t1 = now();
                    t[r] = (t1 - t0) * 1e9;
                    free(b1); free(b2);
                }
                qsort(t, (size_t)reps, sizeof(double), cmp_double);
                total = t[reps / 2];
                printf("PIPE %.0f\n", total);
                free(t);
                return 0;
            }
            if (do_bench < 0) {
                /* `widths` mode: the solution width the entry point reports.
                 * The lanes a kernel works in are part of what an entry point
                 * promises, so this is what the envelope check reads. */
                parasail_result_t *r;
                int w = 0;
                rs = seed * 2654435761ULL + 1;
                gen(s1, n1); gen(s2, n2);
                r = f(s1, n1, s2, n2, OPEN, GAP, m);
                if (!r) { printf("%s W -1\n", line); continue; }
                if (r->flag & PARASAIL_FLAG_BITS_8) w = 8;
                else if (r->flag & PARASAIL_FLAG_BITS_16) w = 16;
                else if (r->flag & PARASAIL_FLAG_BITS_32) w = 32;
                else if (r->flag & PARASAIL_FLAG_BITS_64) w = 64;
                printf("%s W %d\n", line, w);
                continue;
            }
            if (!do_bench) {
                parasail_result_t *r;
                rs = seed * 2654435761ULL + 1;
                gen(s1, n1); gen(s2, n2);
                r = f(s1, n1, s2, n2, OPEN, GAP, m);
                h_init();
                h_result(r, s1, n1, s2, n2, m, get_cigar, decode);
                printf("%s H %016llx%016llx\n", line, (unsigned long long)h1,
                       (unsigned long long)h2);
            } else {
                double *t = malloc((size_t)reps * sizeof(double));
                for (i = 0; i < reps; i++) {
                    double t0, t1;
                    parasail_result_t *r;
                    char *b1, *b2;
                    /* fresh buffers per repetition -- see the bench loop */
                    rs = (seed + (uint64_t)i) * 2654435761ULL + 1;
                    b1 = malloc((size_t)n1 + 1);
                    b2 = malloc((size_t)n2 + 1);
                    gen(b1, n1); gen(b2, n2);
                    t0 = now();
                    r = f(b1, n1, b2, n2, OPEN, GAP, m);
                    t1 = now();
                    (void)r;
                    t[i] = (t1 - t0) * 1e9;
                    free(b1); free(b2);
                }
                qsort(t, (size_t)reps, sizeof(double), cmp_double);
                printf("%s NS %.0f\n", line, t[reps / 2]);
                free(t);
            }
        }
        return 0;
    }
    fprintf(stderr, "unknown mode %s\n", mode);
    return 2;
}
