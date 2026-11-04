/* Per-function timing and correctness harness for the StringZilla text-ingest
 * ladder.
 *
 *   usage:  measure <fnid>           > "<fnid> <median-us> <digest>"
 *           measure <fnid> dump      > the call's observable output
 *
 * Compiled against the tree under test and its own headers, with the ladder's
 * pinned capability envelope as the command line, so calling a public entry
 * point *is* calling the implementation that library installed: every entry
 * point selects its backend with `#if SZ_USE_<TIER>` at the point the header
 * is compiled, and the tier macros are the compiler's own target macros.
 * Two trees, one source file, identical inputs.
 *
 * The workload is generated in-process from a compiled-in profile, so it is not
 * a file the candidate can read, and it is regenerated for every repetition
 * from a different draw, so a result cannot be memoized behind the call.  The
 * last clause needs the second half of the guarantee: a fresh draw usually
 * lands on the address the previous one just freed, so a memo keyed on
 * (pointer, length) -- or on the pointer alone -- can answer repetitions 2..7
 * from a stale entry.  Every timed draw's observable output therefore joins one
 * digest, and `bench` prints it; the verifier compares the two trees' digests
 * on identical draws, so an answer that is not recomputed is a mismatch rather
 * than a speedup.
 *
 * `dump` is the correctness side: the observable output of one call, written
 * with the return value's own length.  `bench` is the timing side: REPS
 * repetitions, one fresh draw each, reported as the median, plus the digest of
 * all REPS outputs.  Each repetition's output is produced by an untimed second
 * call, so the digest never lands inside the measured region -- an output copy
 * there would tax a fast implementation more than a slow one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "stringzilla/stringzilla.h"

/* --- compiled-in workload profile ---------------------------------------- *
 * The practice profile.  This copy of the harness ships to the agent image as
 * /app/tools/measure.c, so it carries the practice draw and nothing else: no
 * seed, size, script mix or shape parameter of the scored profile is present
 * in this file, in the prebuilt binary, or in the binary `/app/tools/build.sh`
 * rebuilds from it.  The scored profile's values live in the verifier's own
 * copy of this harness, which is compiled from a different build context and
 * never ships.  Same operations and the same family of inputs as the scored
 * draw -- deliberately a disjoint draw of them.  `-DPRACTICE_PROFILE` is
 * accepted and selects these same values, so the shipped build command is
 * unchanged.
 *
 * The scored profile's *branch* is not compiled out of this copy -- it is
 * absent from it: there is no `#else` arm here, and the one place the harness
 * would branch on a wider script mix (see `w_word`) is reduced to this draw's
 * cycle, so nothing about the scored draw's shape can be recovered from this
 * file or from the binary it builds.
 */
#define W_SEED 0x5eed1234u
#define W_BYTES (256u * 1024u)
#define W_SCRIPTS 2
#define W_LINE_WORDS_MIN 6
#define W_LINE_WORDS_MAX 12

#define W_REPS 7

/* --- PRNG (xorshift64*) -------------------------------------------------- */
static sz_u64_t w_state;
static void w_seed(sz_u64_t seed) {
    w_state = seed | 1u;
    for (int i = 0; i < 8; ++i) {
        w_state ^= w_state >> 12;
        w_state ^= w_state << 25;
        w_state ^= w_state >> 27;
    }
}
static sz_u64_t w_next(void) {
    w_state ^= w_state >> 12;
    w_state ^= w_state << 25;
    w_state ^= w_state >> 27;
    return w_state * 2685821657736338717ull;
}
static sz_size_t w_below(sz_size_t bound) { return (sz_size_t)(w_next() % (bound ? bound : 1)); }

/* --- vocabulary ---------------------------------------------------------- *
 * Latin, Cyrillic and Greek words plus the ASCII punctuation, digits and
 * spacing that the Unicode segmentation properties care about.  The pieces of
 * a word are short so that the generator can assemble any line length.
 */
static char const *const w_latin[] = {
    "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog", "data", "systems", "search", "index",
    "corpus", "token", "phrase", "segment", "unicode", "boundary", "folding", "hash", "throughput",
    "scalar", "vector", "kernel", "instruction", "cache", "latency", "bandwidth", "aligned", "profile",
};
static char const *const w_cyrillic[] = {
    "привет", "мир", "данные", "поиск", "система", "корпус", "токен", "сегмент", "юникод", "граница",
    "свёртка", "хэш", "выравнивание", "производительность", "скалярный", "векторный", "ядро", "память",
};
static char const *const w_greek[] = {
    "γειά", "κόσμος", "δεδομένα", "αναζήτηση", "σύστημα", "σώμα", "τμήμα", "ενιαίο", "όριο", "δίπλωση",
    "κατακερματισμός", "απόδοση", "μνήμη", "πυρήνας", "διάνυσμα",
};
static char const *const w_cjk[] = {"東京", "文字", "検索", "系統", "資料", "分割", "日本語", "漢字"};

static char const *w_word(void) {
    sz_u64_t r = w_next();
    /* This draw's word cycle. */
    switch (r % 4) {
    case 0:
    case 1:
    case 2:
    case 3: return w_latin[r % (sizeof(w_latin) / sizeof(w_latin[0]))];
    case 4: return w_cyrillic[r % (sizeof(w_cyrillic) / sizeof(w_cyrillic[0]))];
    case 5: return w_greek[r % (sizeof(w_greek) / sizeof(w_greek[0]))];
    case 6: return w_cjk[r % (sizeof(w_cjk) / sizeof(w_cjk[0]))];
    default: return w_latin[r % (sizeof(w_latin) / sizeof(w_latin[0]))];
    }
}

/* Punctuation that separates, terminates or attaches: exactly the characters
 * UAX-14 and UAX-29 classify separately. */
static char const *w_punct(void) {
    static char const *const p[] = {".", ",", ";", ":", "!", "?", "'", "\"", "-", "(", ")", "/", "…", "—"};
    return p[w_next() % (sizeof(p) / sizeof(p[0]))];
}

static void w_append(char *dst, sz_size_t *len, char const *s) {
    sz_size_t n = strlen(s);
    memcpy(dst + *len, s, n);
    *len += n;
}

/* Build a text of about W_BYTES bytes: lines of words, punctuation inside
 * them, sentence terminators at their ends, blank lines and a few markup
 * runs.  Returns the buffer and its length. */
static char *w_build(sz_size_t *out_length) {
    char *text = (char *)malloc(W_BYTES + 4096);
    sz_size_t len = 0;
    while (len < W_BYTES) {
        sz_size_t words = W_LINE_WORDS_MIN + w_below(W_LINE_WORDS_MAX - W_LINE_WORDS_MIN + 1);
        for (sz_size_t i = 0; i < words && len < W_BYTES; ++i) {
            w_append(text, &len, w_word());
            if ((w_next() & 7) == 0) w_append(text, &len, w_punct());
            if (i + 1 < words) {
                if ((w_next() & 15) == 0) w_append(text, &len, ", ");
                else w_append(text, &len, " ");
            }
        }
        if ((w_next() & 7) == 0) {
            w_append(text, &len, " (");
            char digits[8];
            snprintf(digits, sizeof(digits), "%u", (unsigned)(w_next() % 1000));
            w_append(text, &len, digits);
            w_append(text, &len, ")");
        }
        if ((w_next() & 31) == 0) w_append(text, &len, "\n\n");
        else if ((w_next() & 15) == 0) w_append(text, &len, w_punct()), w_append(text, &len, "\n");
        else w_append(text, &len, ".\n");
    }
    text[len] = '\0';
    *out_length = len;
    return text;
}

/* A needle for the search-shaped entry points: a token that occurs exactly
 * once in the text, planted in its last sixth, so that answering the query
 * means walking most of the haystack rather than hitting the first occurrence
 * of a common word.  This is the shape the project's own search benchmark
 * measures: a rare token against the whole corpus.
 *
 * The token is planted in lower case and handed to the search in upper case,
 * which is what makes it a *case-insensitive* search rather than a strstr.
 */
#define W_NEEDLE_LEN 12
static char w_needle_upper[W_NEEDLE_LEN + 1];
static char w_needle_lower[W_NEEDLE_LEN + 1];

static void w_plant_needle(char *text, sz_size_t length) {
    static char const alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    for (int i = 0; i < W_NEEDLE_LEN; ++i) {
        char c = alphabet[w_next() % 26];
        w_needle_lower[i] = c;
        w_needle_upper[i] = (char)(c - 'a' + 'A');
    }
    w_needle_lower[W_NEEDLE_LEN] = '\0';
    w_needle_upper[W_NEEDLE_LEN] = '\0';
    if (length > 4 * W_NEEDLE_LEN) {
        sz_size_t at = length - length / 6 + w_below(length / 12 + 1);
        memcpy(text + at, w_needle_lower, W_NEEDLE_LEN);
    }
}

static void w_needle(char const *text, sz_size_t length, sz_size_t variant, char const **needle,
                     sz_size_t *nlen) {
    (void)text;
    (void)length;
    (void)variant;
    *needle = w_needle_upper;
    *nlen = W_NEEDLE_LEN;
}

/* Big enough for the largest observable output any entry point can produce:
 * a decode can unpack almost one u32 per input byte, and a segmentation can
 * produce a boundary per byte, so this cap is generous by design.  A dump that
 * would exceed it is refused rather than overrunning the buffer. */
#define OUT_CAP (96u * 1024u * 1024u)
/* --- output accumulation ------------------------------------------------- */
static void w_out(void const *bytes, sz_size_t n) {
    if (n) fwrite(bytes, 1, n, stdout);
}

/* Every dump path funnels through here: an output larger than the buffer is a
 * harness bug, and it must say so rather than corrupt memory. */
static int w_guard(sz_size_t n) {
    if (n > OUT_CAP) {
        fprintf(stderr, "dump of %zu bytes exceeds the %u-byte buffer\n",
                (size_t)n, (unsigned)OUT_CAP);
        return 0;
    }
    return 1;
}
static volatile sz_u64_t sink;
static void w_sink_u64(sz_u64_t v) { sink ^= v; }

/* --- output digest ------------------------------------------------------- *
 * FNV-1a over every byte of every timed draw's observable output.  Local on
 * purpose: the digest must not depend on the candidate's own hashing code,
 * which the tree under test is free to change.
 */
static sz_u64_t w_digest_fold(sz_u64_t digest, unsigned char const *bytes, sz_size_t n) {
    for (sz_size_t i = 0; i < n; ++i) {
        digest ^= (sz_u64_t)bytes[i];
        digest *= 0x100000001b3ull;
    }
    return digest;
}

/* --- pre-timer preparation ----------------------------------------------- *
 * Work an entry point needs per draw that is not itself the work being timed:
 * it is done once, before the clock starts, identically on both trees.  Two
 * cases need it, and in both the entry point's own timing is otherwise diluted
 * or shortcut rather than measured.
 */
static sz_size_t seek_total = 0;
static sz_size_t prep_utf8_seek(char *text, sz_size_t length) {
    /* The seek index is derived from the text's rune count, and that count is
     * `sz_utf8_count` -- another entry point this harness can time.  Calling
     * it inside the timed region would report a count speedup as a seek
     * speedup, so it is taken here, outside the clock. */
    seek_total = sz_utf8_count(text, length);
    return seek_total;
}

static char *order_copy = 0;
static sz_size_t order_n = 0;
static sz_size_t prep_uncased_order(char *text, sz_size_t length) {
    /* A private copy of the slice whose case differs from the original's.  The
     * ordering call compares the slice against this copy, so both pointers
     * differ and the bytes differ at every letter: there is no pointer-identity
     * and no memcmp short cut, and the comparison must fold its way to the end.
     * The copy is built here, before the clock starts, so it is not charged to
     * the timed call -- and it is deliberately not a second draw, which would
     * measure the corpus generator rather than the comparison. */
    if (!order_copy) order_copy = (char *)malloc(W_BYTES + 4096);
    order_n = length / 8;
    if (order_n < 4096) order_n = length;
    for (sz_size_t i = 0; i < order_n; ++i) {
        char c = text[i];
        order_copy[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    return order_n;
}

/* --- the functions ------------------------------------------------------- *
 * One case per entry point.  Each returns the number of bytes it wrote into
 * `out` (dump mode) and does its work once (both modes).  `prep`, when
 * non-zero, runs once per draw before the timer starts (see above).
 */
static unsigned char *out_buf;

typedef struct {
    char const *fnid;
    sz_size_t (*prep)(char *text, sz_size_t length);
    sz_size_t (*run)(char *text, sz_size_t length, sz_size_t variant, int dump);
} entry_t;

static sz_size_t run_bytesum(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    sz_u64_t r = sz_bytesum(text, length);
    w_sink_u64(r);
    if (dump) {
        memcpy(out_buf, &r, 8);
        return 8;
    }
    return 0;
}

static sz_size_t run_hash(char *text, sz_size_t length, sz_size_t v, int dump) {
    sz_u64_t seed = 0x1234567u + v;
    sz_u64_t r = sz_hash(text, length, seed);
    w_sink_u64(r);
    if (dump) {
        memcpy(out_buf, &r, 8);
        return 8;
    }
    return 0;
}

static sz_size_t run_hash_multiseed(char *text, sz_size_t length, sz_size_t v, int dump) {
    sz_u64_t seeds[8];
    sz_u64_t hashes[8];
    for (int i = 0; i < 8; ++i) seeds[i] = (sz_u64_t)(0x9e37u + i * 2654435761u + v);
    sz_hash_multiseed(text, length, seeds, 8, hashes);
    for (int i = 0; i < 8; ++i) w_sink_u64(hashes[i]);
    if (dump) {
        memcpy(out_buf, hashes, sizeof(hashes));
        return sizeof(hashes);
    }
    return 0;
}

static sz_size_t run_hash_state(char *text, sz_size_t length, sz_size_t v, int dump) {
    sz_hash_state_t state;
    sz_size_t chunk = length / 8 + 1;
    sz_hash_state_init(&state, 0xabcdefu + v);
    for (sz_size_t at = 0; at < length; at += chunk) {
        sz_size_t n = (at + chunk <= length) ? chunk : length - at;
        sz_hash_state_update(&state, text + at, n);
    }
    sz_u64_t r = sz_hash_state_digest(&state);
    w_sink_u64(r);
    if (dump) {
        memcpy(out_buf, &r, 8);
        return 8;
    }
    return 0;
}

/* The multi-state SHA-256 path takes a sequence of separate strings, so the
 * text is cut into SEQ_LANES equal chunks and a sequence is built over them:
 * hashing many independent documents in parallel is what the kernel is for. */
#define SEQ_LANES 16
static char *seq_storage;
static sz_size_t seq_lens[SEQ_LANES];

static sz_size_t run_sha256_multistate(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    if (!seq_storage) seq_storage = (char *)malloc(W_BYTES + SEQ_LANES * 2);
    sz_cptr_t starts[SEQ_LANES];
    sz_size_t chunk = length / SEQ_LANES;
    if (chunk < 1) chunk = 1;
    sz_size_t count = 0, used = 0;
    for (sz_size_t at = 0; at < length && count < SEQ_LANES; at += chunk) {
        sz_size_t n = (at + chunk <= length) ? chunk : length - at;
        memcpy(seq_storage + used, text + at, n);
        seq_storage[used + n] = '\0';
        starts[count] = seq_storage + used;
        seq_lens[count] = n;
        used += n + 1;
        ++count;
    }
    if (count == 0) {
        seq_storage[0] = '\0';
        starts[0] = seq_storage;
        seq_lens[0] = 0;
        count = 1;
    }

    sz_sequence_t sequence;
    sz_sequence_from_null_terminated_strings(starts, count, &sequence);

    static sz_sha256_state_t states[SEQ_LANES];
    for (sz_size_t i = 0; i < count; ++i) sz_sha256_state_init(&states[i]);
    sz_sha256_multistate_update(states, &sequence);
    static sz_u8_t digests[SEQ_LANES * 32];
    sz_sha256_multistate_digest(states, count, digests);
    w_sink_u64((sz_u64_t)digests[0]);
    if (dump) {
        memcpy(out_buf, digests, count * 32);
        return count * 32;
    }
    return 0;
}

static sz_size_t run_utf8_count(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    sz_size_t r = sz_utf8_count(text, length);
    w_sink_u64((sz_u64_t)r);
    if (dump) {
        memcpy(out_buf, &r, sizeof(r));
        return sizeof(r);
    }
    return 0;
}

static sz_size_t run_utf8_seek(char *text, sz_size_t length, sz_size_t v, int dump) {
    sz_size_t total = seek_total; /* taken by prep_utf8_seek, before the clock */
    if (!total) return 0;
    /* The dump draw is variant 0: without the +1 it would ask for rune 0, which
     * both trees answer by returning `text` -- a byte-compare that cannot fail.
     * Every variant therefore seeks to a non-zero rune. */
    sz_size_t n = (sz_size_t)((1u + v * 7919u) % total);
    sz_cptr_t p = sz_utf8_seek(text, length, n);
    sz_size_t off = p ? (sz_size_t)(p - text) : (sz_size_t)-1;
    w_sink_u64((sz_u64_t)off);
    if (dump) {
        memcpy(out_buf, &off, sizeof(off));
        return sizeof(off);
    }
    return 0;
}

static sz_size_t run_utf8_decode(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    static sz_rune_t *runes;
    if (!runes) runes = (sz_rune_t *)malloc((W_BYTES + 64) * sizeof(sz_rune_t));
    sz_size_t unpacked = 0;
    sz_cptr_t end = sz_utf8_decode(text, length, runes, W_BYTES + 64, &unpacked);
    sz_size_t consumed = end ? (sz_size_t)(end - text) : 0;
    w_sink_u64((sz_u64_t)unpacked);
    w_sink_u64((sz_u64_t)runes[unpacked ? unpacked - 1 : 0]);
    if (dump) {
        sz_size_t total = 2 * sizeof(consumed) + unpacked * sizeof(sz_rune_t);
        if (!w_guard(total)) return 0;
        memcpy(out_buf, &consumed, sizeof(consumed));
        memcpy(out_buf + sizeof(consumed), &unpacked, sizeof(unpacked));
        memcpy(out_buf + 2 * sizeof(consumed), runes, unpacked * sizeof(sz_rune_t));
        return total;
    }
    return 0;
}

static sz_size_t run_segment(char *text, sz_size_t length, sz_size_t v, int dump, int sentences) {
    (void)v;
    static sz_size_t *starts, *lens;
    sz_size_t capacity = W_BYTES / 2 + 64;
    if (!starts) {
        starts = (sz_size_t *)malloc(capacity * sizeof(sz_size_t));
        lens = (sz_size_t *)malloc(capacity * sizeof(sz_size_t));
    }
    sz_size_t consumed = 0;
    sz_size_t r = sentences ? sz_utf8_sentences(text, length, starts, lens, capacity, &consumed)
                            : sz_utf8_linebreaks(text, length, starts, lens, capacity, &consumed);
    w_sink_u64((sz_u64_t)r);
    w_sink_u64((sz_u64_t)consumed);
    if (dump) {
        sz_size_t total = 2 * sizeof(r) + 2 * r * sizeof(sz_size_t);
        if (!w_guard(total)) return 0;
        memcpy(out_buf, &r, sizeof(r));
        memcpy(out_buf + sizeof(r), &consumed, sizeof(consumed));
        if (r) {
            memcpy(out_buf + 2 * sizeof(r), starts, r * sizeof(sz_size_t));
            memcpy(out_buf + 2 * sizeof(r) + r * sizeof(sz_size_t), lens, r * sizeof(sz_size_t));
        }
        return total;
    }
    return 0;
}

static sz_size_t run_linebreaks(char *text, sz_size_t length, sz_size_t v, int dump) {
    return run_segment(text, length, v, dump, 0);
}
static sz_size_t run_sentences(char *text, sz_size_t length, sz_size_t v, int dump) {
    return run_segment(text, length, v, dump, 1);
}

static sz_size_t run_uncased_fold(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    static char *folded;
    if (!folded) folded = (char *)malloc(W_BYTES * 4 + 64);
    sz_size_t r = sz_utf8_uncased_fold(text, length, folded);
    w_sink_u64((sz_u64_t)r);
    if (dump) {
        if (!w_guard(sizeof(r) + r)) return 0;
        memcpy(out_buf, &r, sizeof(r));
        memcpy(out_buf + sizeof(r), folded, r);
        return sizeof(r) + r;
    }
    return 0;
}

static sz_size_t run_uncased_search(char *text, sz_size_t length, sz_size_t v, int dump) {
    char const *needle = 0;
    sz_size_t nlen = 0;
    w_needle(text, length, v, &needle, &nlen);
    sz_utf8_uncased_needle_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    sz_size_t matched = 0;
    sz_cptr_t p = sz_utf8_uncased_search(text, length, needle, nlen, &meta, &matched);
    sz_size_t off = p ? (sz_size_t)(p - text) : (sz_size_t)-1;
    w_sink_u64((sz_u64_t)off);
    if (dump) {
        memcpy(out_buf, &off, sizeof(off));
        memcpy(out_buf + sizeof(off), &matched, sizeof(matched));
        return sizeof(off) + sizeof(matched);
    }
    return 0;
}

static sz_size_t run_uncased_order(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    /* A long slice against a private case-differing copy of itself: the
     * comparison must walk the whole slice case-insensitively -- the work being
     * timed -- and one slice against a one-byte-shorter view of the copy, which
     * must order them.  The copy comes from prep_uncased_order. */
    if (!order_copy) return 0; /* main always preps; a missing copy must not dump as agreement */
    sz_size_t n = order_n;
    sz_ordering_t a = sz_utf8_uncased_order(text, n, order_copy, n);
    sz_ordering_t b = sz_utf8_uncased_order(text, n, order_copy, n > 1 ? n - 1 : n);
    w_sink_u64((sz_u64_t)a);
    w_sink_u64((sz_u64_t)b);
    if (dump) {
        memcpy(out_buf, &a, sizeof(a));
        memcpy(out_buf + sizeof(a), &b, sizeof(b));
        return sizeof(a) + sizeof(b);
    }
    return 0;
}

static sz_size_t run_find_cased(char *text, sz_size_t length, sz_size_t v, int dump) {
    (void)v;
    sz_cptr_t p = sz_utf8_find_cased(text, length);
    sz_size_t off = p ? (sz_size_t)(p - text) : (sz_size_t)-1;
    w_sink_u64((sz_u64_t)off);
    if (dump) {
        memcpy(out_buf, &off, sizeof(off));
        return sizeof(off);
    }
    return 0;
}

/* The whole ingest pass, in one measurement: index the UTF-8, segment it into
 * lines and sentences, case-fold it, search it, hash it.  This is the figure of
 * merit the ladder is a decomposition of, and the end-to-end non-regression
 * gate compares it against the L0 floor -- the tree with every one of this
 * task's optimizations absent, a tree that has done nothing -- rather than
 * against the tree the candidate inherited: a no-op tree IS that floor, so
 * baselining the guard on the inherited tree would report a correct no-op as
 * catastrophically slow.
 */
static sz_size_t run_ingest(char *text, sz_size_t length, sz_size_t v, int dump) {
    static sz_rune_t *runes;
    static sz_size_t *starts, *lens;
    static char *folded;
    if (!runes) {
        runes = (sz_rune_t *)malloc((W_BYTES + 64) * sizeof(sz_rune_t));
        starts = (sz_size_t *)malloc((W_BYTES / 2 + 64) * sizeof(sz_size_t));
        lens = (sz_size_t *)malloc((W_BYTES / 2 + 64) * sizeof(sz_size_t));
        folded = (char *)malloc(W_BYTES * 4 + 64);
    }
    sz_u64_t acc = 0;

    acc += (sz_u64_t)sz_utf8_count(text, length);

    sz_size_t unpacked = 0;
    sz_cptr_t decoded_end = sz_utf8_decode(text, length, runes, W_BYTES + 64, &unpacked);
    acc += (sz_u64_t)unpacked + (sz_u64_t)(decoded_end - text);

    sz_size_t consumed = 0;
    sz_size_t lines = sz_utf8_linebreaks(text, length, starts, lens, W_BYTES / 2 + 64, &consumed);
    acc += (sz_u64_t)lines + (sz_u64_t)consumed;

    sz_size_t sentences = sz_utf8_sentences(text, length, starts, lens, W_BYTES / 2 + 64, &consumed);
    acc += (sz_u64_t)sentences + (sz_u64_t)consumed;

    sz_size_t folded_length = sz_utf8_uncased_fold(text, length, folded);
    acc += (sz_u64_t)folded_length;

    sz_utf8_uncased_needle_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    sz_size_t matched = 0;
    sz_cptr_t found = sz_utf8_uncased_search(text, length, w_needle_upper, W_NEEDLE_LEN, &meta, &matched);
    acc += (sz_u64_t)(found ? (found - text) : (sz_size_t)-1);

    acc ^= sz_hash(text, length, 0x5eedu + (sz_u64_t)v);
    w_sink_u64(acc);

    if (dump) {
        memcpy(out_buf, &acc, sizeof(acc));
        return sizeof(acc);
    }
    return 0;
}

static entry_t const entries[] = {
    {"sz_bytesum", 0, run_bytesum},
    {"sz_hash", 0, run_hash},
    {"sz_hash_multiseed", 0, run_hash_multiseed},
    {"sz_hash_state", 0, run_hash_state},
    {"sz_sha256_multistate", 0, run_sha256_multistate},
    {"sz_utf8_count", 0, run_utf8_count},
    {"sz_utf8_seek", prep_utf8_seek, run_utf8_seek},
    {"sz_utf8_decode", 0, run_utf8_decode},
    {"sz_utf8_linebreaks", 0, run_linebreaks},
    {"sz_utf8_sentences", 0, run_sentences},
    {"sz_utf8_uncased_fold", 0, run_uncased_fold},
    {"sz_utf8_uncased_search", 0, run_uncased_search},
    {"sz_utf8_uncased_order", prep_uncased_order, run_uncased_order},
    {"sz_utf8_find_cased", 0, run_find_cased},
    {"ingest", 0, run_ingest},
};

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(void const *a, void const *b) {
    double x = *(double const *)a, y = *(double const *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fnid> [dump|bench]   (bench is the default)\n", argv[0]);
        return 2;
    }
    char const *fnid = argv[1];
    int dump = (argc > 2 && !strcmp(argv[2], "dump"));
    if (argc > 2 && strcmp(argv[2], "dump") && strcmp(argv[2], "bench")) {
        fprintf(stderr, "unknown mode '%s'\n", argv[2]);
        return 2;
    }

    entry_t const *entry = 0;
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        if (!strcmp(entries[i].fnid, fnid)) entry = &entries[i];
    if (!entry) {
        fprintf(stderr, "unknown fnid '%s'\n", fnid);
        return 2;
    }

    out_buf = (unsigned char *)malloc(OUT_CAP);
    if (!out_buf) return 2;

    if (dump) {
        sz_size_t length = 0;
        w_seed(W_SEED);
        char *text = w_build(&length);
        w_plant_needle(text, length);
        if (entry->prep) entry->prep(text, length);
        sz_size_t n = entry->run(text, length, 0, 1);
        w_out(out_buf, n);
        free(text);
        fflush(stdout);
        return 0;
    }

    double samples[W_REPS];
    sz_u64_t digest = 14695981039346656037ull; /* FNV-1a offset basis */
    for (int r = 0; r < W_REPS; ++r) {
        sz_size_t length = 0;
        w_seed(W_SEED + (sz_u64_t)r * 0x100000001b3ull);
        char *text = w_build(&length);
        w_plant_needle(text, length);
        if (entry->prep) entry->prep(text, length);
        double t0 = now_ns();
        entry->run(text, length, (sz_size_t)r, 0);
        double t1 = now_ns();
        samples[r] = (t1 - t0) / 1000.0; /* microseconds */
        /* This draw's observable output joins the digest.  Untimed, and after
         * the clock has stopped: an output copy inside the measured region
         * would tax a fast implementation more than a slow one. */
        sz_size_t n = entry->run(text, length, (sz_size_t)r, 1);
        digest = w_digest_fold(digest, out_buf, n);
        free(text);
    }
    qsort(samples, W_REPS, sizeof(double), cmp_double);
    printf("%s %.3f %016llx\n", fnid, samples[W_REPS / 2], (unsigned long long)digest);
    return 0;
}
