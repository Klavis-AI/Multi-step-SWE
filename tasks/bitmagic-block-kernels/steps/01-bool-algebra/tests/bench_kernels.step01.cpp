// Per-function harness for the BitMagic block-kernel ladder -- 01-bool-algebra.
//
//   bench_kernels <entry-id> <seed> <variant> bench [reps]   -> nanoseconds per call
//   bench_kernels <entry-id> <seed> <variant> dump            -> raw outputs of one call
//   bench_kernels <entry-id> <seed> <variant> trace [reps]    -> outputs of the timed sequence
//   bench_kernels list                                       -> the entries below
//
// Build:
//   g++ -std=c++17 -O2 -DNDEBUG -march=skylake -mavx2 -mpopcnt -DBMAVX2OPT \
//       -I /app/bitmagic/src -o bench_kernels bench_kernels.cpp
//
// It calls the NAMED entry points in src/bmfunc.h and src/bmxor.h that the
// library's own dispatch macros sit inside, so whichever implementation the tree
// installs for an entry point is what gets measured -- the hand-written AVX2
// kernel, the portable C++ body, or anything else that ends up there.
//
// Timing: KBLK back-to-back calls, each on its own destination block, with the
// restore of those blocks done outside the timed window.  Nothing but the call
// is charged to the number: no per-call clock overhead and no subtraction of two
// noisy quantities.  The reported figure is the median of `reps` batches.
//
// Inputs: every batch is timed on inputs drawn for it, freshly, after the last
// batch finished -- each of the KBLK calls in a batch reads its own block of a
// window drawn for that batch, and its own freshly restored destination, so no
// input is ever timed twice and a cache keyed on what the call reads has no
// repeat to answer from.  The redraw happens outside the timed window, and it is
// what puts the batch's inputs in cache before they are timed, exactly as a
// single pre-drawn input did before: the figure stays a kernel measurement, not
// a memory measurement.  The window is KBLK blocks, so the working set is the
// same few blocks per call it always was.
//
// Correctness: `dump` writes the destination block plus every other observable
// output of one call.  `trace` replays the SAME call sequence the timing is made
// of -- same draws, same order -- and writes every observable output of every
// call in it.  Two builds must produce byte-identical output for both: the calls
// that are timed are checked, not only the single call `dump` prints.

#include "bm.h"
#include "bmxor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef BENCH_SEED
#define BENCH_SEED 987654321
#endif

namespace {

inline uint64_t sm64(uint64_t& s)
{
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

const unsigned W  = bm::set_block_size;              // words per bit block
const unsigned NB = W * sizeof(bm::word_t);          // bytes per bit block
const unsigned KBLK = 32;                            // calls per timed batch

inline void* xalloc(size_t bytes)
{
    void* p = 0;
    if (::posix_memalign(&p, 64, bytes) != 0) { std::fprintf(stderr, "oom\n"); std::exit(3); }
    std::memset(p, 0, bytes);
    return p;
}

bm::word_t*    dstK;    // the destinations a call may write, one per call
bm::word_t*    dst;     // the block the current call writes
bm::word_t*    srcK;    // this batch's inputs: KBLK drawn blocks
bm::word_t*    src1;    // the block the current call reads as src1 (src2..src4
bm::word_t*    src2;    // are the blocks that follow it in the window, wrapped)
bm::word_t*    src3;
bm::word_t*    src4;
bm::word_t*    maskb;
bm::id64_t*    digestK;             // one digest per call, redrawn with the inputs
bm::gap_word_t* gapbuf;
unsigned*      idxarr;

bm::id64_t g_ret_d;
bool       g_ret_b;
unsigned   g_ret_u;
bm::word_t g_ret_w;
bm::word_t g_empty_acc;
bm::word_t g_co_flag;
bm::id64_t g_digest;
unsigned   g_gc, g_bc, g_pos, g_start, g_stop, g_count;

uint64_t g_draw_s;                  // the input draw stream, owned by the harness

std::vector<unsigned char> sink;

// variant 0 dense random, 1 sparse, 2 word runs
void fill(bm::word_t* b, uint64_t& s, int variant)
{
    for (unsigned i = 0; i < W; ++i)
    {
        uint64_t r = sm64(s);
        switch (variant)
        {
        case 1:  b[i] = (r & 0x3F) ? 0u : (bm::word_t)r; break;
        case 2:  b[i] = (r & 1) ? 0xFFFFFFFFu : (bm::word_t)(r & 0xFF00FF00u); break;
        default: b[i] = (bm::word_t)r; break;
        }
    }
}

// A GAP block built straight from a run-length draw, with no library call, so
// the input never depends on the tree under test.  Layout is BitMagic's:
//   buf[0] = ((len-1) << 3) | (level << 1) | last_bit_value
//   buf[i] = last position (inclusive) of run i-1, for i = 1..len-1
// and the value of run k is first_bit_value ^ (k & 1).
//
// The run lengths are drawn so the block stays inside BitMagic's own GAP
// capacity for the level it declares: `bm::gap_max_buff_len` words at level 3.
// A block with more runs than that is not a GAP block the library can produce.
void make_gap(bm::gap_word_t* buf, uint64_t seed)
{
    uint64_t s = seed ^ 0xA5A5A5A5A5A5A5A5ull;
    const unsigned TOTAL = 65536;
    unsigned pos = 0, len = 1;
    pos += (unsigned)(sm64(s) % 40u) + 1u;
    buf[len++] = (bm::gap_word_t)(pos - 1);
    while (pos < TOTAL)
    {
        unsigned r = (unsigned)((sm64(s) % 40u) + 40u);
        if (pos + r > TOTAL) r = TOTAL - pos;
        pos += r;
        buf[len++] = (bm::gap_word_t)(pos - 1);
    }
    if (len > bm::gap_max_buff_len)
    {
        std::fprintf(stderr, "make_gap: %u words exceeds gap_max_buff_len %u\n",
                     len, (unsigned)bm::gap_max_buff_len);
        std::exit(4);
    }
    unsigned last_run = len - 2;
    buf[0] = (bm::gap_word_t)(((len - 1) << 3) | (3u << 1) | (0u ^ (last_run & 1u)));
}

// Sorted, strictly increasing 32-bit indices inside one block.
const unsigned IDXGAP = 8;   // index density the scatter entry is measured at
void make_idx(unsigned* a, uint64_t seed, unsigned count)
{
    uint64_t s = seed ^ 0x123456789ABCDEFull;
    a[0] = (unsigned)(sm64(s) % 32u);
    for (unsigned i = 1; i < count; ++i)
        a[i] = a[i - 1] + 1u + (unsigned)(sm64(s) % IDXGAP);
}

// A 64-bit block digest drawn per (seed, variant).  Variant 0 names every wave,
// which is what a dense block's digest looks like and the cost the timing should
// represent; variants 1 and 2 name a sparse and a dense-but-partial subset, so a
// body that ignores the digest and always touches the whole block is caught by
// the correctness comparison instead of passing.
bm::id64_t make_digest(uint64_t seed, int v)
{
    if (v == 0) return ~(bm::id64_t)0;
    uint64_t s = seed ^ 0xD16E57ull;
    bm::id64_t d = 0;
    unsigned waves = (v == 1) ? 8u : 48u;
    while (bm::word_bitcount64(d) < waves)
    {
        unsigned w = (unsigned)(sm64(s) % 64u);
        d |= (bm::id64_t(1) << w);
    }
    return d;
}

// Point every global a call can read at this batch's input for that call: its
// own block of the window as src1 (and the three that follow it as src2..src4), and its own
// destination.  A call's input tuple is a function of k alone, and the window is
// redrawn before every batch, so no two calls in the timed sequence share one.
void select_call(unsigned k)
{
    src1 = srcK + (size_t)k * W;
    src2 = srcK + (size_t)((k + 1) % KBLK) * W;
    src3 = srcK + (size_t)((k + 2) % KBLK) * W;
    src4 = srcK + (size_t)((k + 3) % KBLK) * W;
    dst  = dstK + (size_t)k * W;
    g_digest = digestK[k];
}

// Draw this batch's KBLK inputs (KBLK blocks from the per-word draw, one digest
// each).  Outside the timed
// window, from a stream that never repeats a draw.
void redraw_inputs(int v)
{
    for (unsigned k = 0; k < KBLK; ++k)
    {
        fill(srcK + (size_t)k * W, g_draw_s, v);
        digestK[k] = make_digest(sm64(g_draw_s), v);
    }
    select_call(0);
}

// Restore the destination blocks this batch writes from the window: each call's
// destination starts as a copy of its own source, the relation the single
// pre-drawn input had, and it is restored outside the timed window.
void reset_dst()
{
    std::memcpy(dstK, srcK, (size_t)NB * KBLK);
}

void init_buffers()
{
    dstK   = (bm::word_t*)xalloc((size_t)NB * KBLK);
    srcK   = (bm::word_t*)xalloc((size_t)NB * KBLK);
    digestK = (bm::id64_t*)xalloc((size_t)KBLK * sizeof(bm::id64_t));
    maskb  = (bm::word_t*)xalloc(NB);
    gapbuf = (bm::gap_word_t*)xalloc((65536 + 8) * sizeof(bm::gap_word_t));
    idxarr = (unsigned*)xalloc(NB + 64);
    dst = dstK;
}

typedef void (*setup_fn)(uint64_t, int);
typedef void (*call_fn)();

struct Entry
{
    const char* id;
    setup_fn    setup;
    call_fn     call;
    const char* kind;
};


static void setup_bool(uint64_t seed, int v)
{
    g_draw_s = seed;
    redraw_inputs(v);
    reset_dst();
}

// ---- the entry points scoped to this milestone ----
void call_and() { g_ret_d = bm::bit_block_and(dst, src1); }

void call_and_digest() { g_ret_d = bm::bit_block_and(dst, src1, g_digest); }

void call_and_2way() { g_ret_d = bm::bit_block_and_2way(dst, src1, src2, g_digest); }

void call_and_3way() { g_ret_d = bm::bit_block_and_3way(dst, src1, src2, g_digest); }

void call_and_5way() { g_ret_d = bm::bit_block_and_5way(dst, src1, src2, src3, src4, g_digest); }

void call_init_and_2way() { g_ret_d = bm::bit_block_init_and_2way(dst, src1, src2, g_digest); }

void call_xor() { g_ret_d = bm::bit_block_xor(dst, src1); }

void call_xor_2way() { g_ret_d = bm::bit_block_xor_2way(dst, src1, src2); }

void call_sub() { g_ret_d = bm::bit_block_sub(dst, src1); }

void call_sub_digest() { g_ret_d = bm::bit_block_sub(dst, src1, g_digest); }

void call_sub_2way() { g_ret_d = bm::bit_block_sub_2way(dst, src1, src2, g_digest); }

void call_sub_3way() { g_ret_d = bm::bit_block_sub_3way(dst, src1, src2, g_digest); }

void call_sub_5way() { g_ret_d = bm::bit_block_sub_5way(dst, src1, src2, src3, src4, g_digest); }

void call_or_2way() { g_ret_b = bm::bit_block_or_2way(dst, src1, src2); }

void call_or_3way() { g_ret_b = bm::bit_block_or_3way(dst, src1, src2); }

void call_block_init_digest0() { bm::block_init_digest0(dst, g_digest); }

void call_update_block_digest0() { g_ret_d = bm::update_block_digest0(src1, g_digest); }

const Entry ENTRIES[] = {
    { "and", setup_bool, call_and, "V" },
    { "and_digest", setup_bool, call_and_digest, "B" },
    { "and_2way", setup_bool, call_and_2way, "B" },
    { "and_3way", setup_bool, call_and_3way, "B" },
    { "and_5way", setup_bool, call_and_5way, "B" },
    { "init_and_2way", setup_bool, call_init_and_2way, "B" },
    { "xor", setup_bool, call_xor, "B" },
    { "xor_2way", setup_bool, call_xor_2way, "V" },
    { "sub", setup_bool, call_sub, "B" },
    { "sub_digest", setup_bool, call_sub_digest, "B" },
    { "sub_2way", setup_bool, call_sub_2way, "B" },
    { "sub_3way", setup_bool, call_sub_3way, "B" },
    { "sub_5way", setup_bool, call_sub_5way, "B" },
    { "or_2way", setup_bool, call_or_2way, "B" },
    { "or_3way", setup_bool, call_or_3way, "B" },
    { "block_init_digest0", setup_bool, call_block_init_digest0, "B" },
    { "update_block_digest0", setup_bool, call_update_block_digest0, "B" },
};
const unsigned NENTRIES = 17;

// The observable outputs of one call, appended after it returns: exactly the
// fields the dump path prints for this step's kind, and no more -- two
// implementations may legitimately differ in what the dump does not print (the
// raw value of a predicate they agree on zero / non-zero, say), and this
// comparison is byte-identical, so it folds the same set the dump prints.
void fold_call(const Entry* e, std::vector<unsigned char>& o)
{
    switch (e->kind[0] ? e->kind[0] : 'B')
    {
    case 'B': { o.insert(o.end(), (unsigned char*)&g_ret_d, (unsigned char*)&g_ret_d + 8);
    o.insert(o.end(), (unsigned char*)&g_ret_b, (unsigned char*)&g_ret_b + 1);
        break; }
    case 'V': { unsigned char vd = g_ret_d ? 1 : 0, vb = g_ret_b ? 1 : 0;
    o.insert(o.end(), &vd, &vd + 1);
    o.insert(o.end(), &vb, &vb + 1);
        break; }
    default: break;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "list") == 0)
    {
        for (unsigned i = 0; i < NENTRIES; ++i)
            std::printf("%s\n", ENTRIES[i].id);
        return 0;
    }
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <entry-id> <seed> <variant> bench|dump|trace [reps]\n", argv[0]);
        return 2;
    }
    const Entry* e = 0;
    for (unsigned i = 0; i < NENTRIES; ++i)
        if (std::strcmp(ENTRIES[i].id, argv[1]) == 0) { e = &ENTRIES[i]; break; }
    if (!e) { std::fprintf(stderr, "unknown entry '%s'\n", argv[1]); return 2; }

    const char* mode = (argc >= 5) ? argv[4] : "bench";
    uint64_t seed = std::strtoull(argv[2], 0, 10);
    if (seed == 0) seed = (uint64_t)BENCH_SEED;
    int variant = std::atoi(argv[3]);
    const bool trace = (std::strcmp(mode, "trace") == 0);

    if (std::strcmp(mode, "dump") == 0)
    {
        init_buffers();
        e->setup(seed, variant);
        e->call();
        std::vector<unsigned char> o;
        o.insert(o.end(), (unsigned char*)dst, (unsigned char*)dst + NB);
        switch (e->kind[0] ? e->kind[0] : 'B')
        {
        case 'B': { o.insert(o.end(), (unsigned char*)&g_ret_d, (unsigned char*)&g_ret_d + 8);
        o.insert(o.end(), (unsigned char*)&g_ret_b, (unsigned char*)&g_ret_b + 1);
            break; }
        case 'V': { unsigned char vd = g_ret_d ? 1 : 0, vb = g_ret_b ? 1 : 0;
        o.insert(o.end(), &vd, &vd + 1);
        o.insert(o.end(), &vb, &vb + 1);
            break; }
        default: break;
        }
        std::fwrite(o.data(), 1, o.size(), stdout);
        std::fflush(stdout);
        return 0;
    }

    init_buffers();
    long reps = (argc > 5) ? std::strtol(argv[5], 0, 10) : 7;
    if (reps < 3) reps = 3;
    std::vector<double> runs;
    runs.reserve((size_t)reps);
    std::vector<unsigned char> out;

    e->setup(seed, variant);
    for (int i = 0; i < 2; ++i) { reset_dst(); select_call(0); e->call(); }
    sink.push_back((unsigned char)(g_ret_d & 1));

    for (long r = 0; r < reps; ++r)
    {
        redraw_inputs(variant);         // this batch's inputs, redrawn outside the window
        reset_dst();
        auto t0 = std::chrono::steady_clock::now();
        for (unsigned k = 0; k < KBLK; ++k)
        {
            select_call(k);
            e->call();
            if (trace) fold_call(e, out);
        }
        auto t1 = std::chrono::steady_clock::now();
        if (trace)
        {
            // every destination block this batch wrote, byte for byte
            out.insert(out.end(), (unsigned char*)dstK,
                       (unsigned char*)(dstK + (size_t)KBLK * W));
            continue;
        }
        sink.push_back((unsigned char)(g_ret_d & 1));
        std::chrono::duration<double> d = t1 - t0;
        runs.push_back(d.count() / KBLK);
    }
    if (trace)
    {
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
        return 0;
    }
    std::sort(runs.begin(), runs.end());
    double ns = runs[runs.size() / 2] * 1e9;
    if (ns < 0.0) ns = 0.0;
    std::printf("%.3f\n", ns);
    return 0;
}
