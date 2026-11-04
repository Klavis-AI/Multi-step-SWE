// End-to-end driver: a composite bit-vector workload through the public API.
//
//   e2e_bench bench  [seed]  -> milliseconds for the whole composite, one number
//   e2e_bench digest [seed]  -> a hex digest of everything the composite produced
//
// `bench` is the non-regression guard: it asks whether the real pipeline got
// slower, which no per-function microbenchmark can answer.  `digest` is the
// end-to-end correctness gate: it folds every vector the composite produced
// into one number, so a change that is bit-exact per function but wrong in the
// glue still shows up.  Both read the same corpus, drawn from the same seed.
//
// The corpus is generated here, from a seed the verifier owns -- never read
// from /app, which the candidate controlled.

#include "bm.h"
#include "bmserial.h"
#include "bmaggregator.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

typedef bm::bvector<> bvect;

static inline uint64_t sm64(uint64_t& s)
{
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static const unsigned NVEC  = 12;
static const unsigned NBLK  = 256;         // 256 * 65536 bits ~ 2.1M bits/vector
static const unsigned BLKBITS = 65536;

// Every vector is built from a run-length draw, so it has a mix of dense,
// sparse and run-structured regions and the GAP/bitmap decision is exercised.
static void make_vect(bvect& bv, uint64_t& s, unsigned density)
{
    unsigned pos = 0;
    const unsigned total = NBLK * BLKBITS;
    while (pos < total)
    {
        unsigned skip = (unsigned)(sm64(s) % density) + 1;
        unsigned run  = (unsigned)(sm64(s) % density) + 1;
        pos += skip;
        if (pos >= total) break;
        if (pos + run > total) run = total - pos;
        bv.set_range(pos, pos + run - 1);
        pos += run;
    }
}

struct Corpus
{
    std::vector<bvect> v;
    void build(uint64_t seed)
    {
        uint64_t s = seed ^ 0xDEADBEEFCAFEF00Dull;
        v.resize(NVEC);
        for (unsigned i = 0; i < NVEC; ++i)
        {
            unsigned density = 4u << (i % 5);       // 4 .. 64
            make_vect(v[i], s, density);
        }
    }
};

static uint64_t fnv1a(uint64_t h, const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}

// The composite.  Returns a digest of everything it computed.
static uint64_t composite(Corpus& c, unsigned long long* ops)
{
    const unsigned n = NVEC;
    std::vector<bvect> w(n);
    uint64_t h = 0xCBF29CE484222325ull;
    unsigned long long nops = 0;

    for (unsigned i = 0; i < n; ++i) { w[i] = c.v[i]; ++nops; }
    for (unsigned i = 0; i < n; ++i) { w[i].bit_and(c.v[(i + 1) % n]); ++nops; }
    for (unsigned i = 0; i < n; ++i) { w[i].bit_or (c.v[(i + 3) % n]); ++nops; }
    for (unsigned i = 0; i < n; ++i) { w[i].bit_xor(c.v[(i + 5) % n]); ++nops; }
    for (unsigned i = 0; i < n; ++i) { w[i].bit_sub(c.v[(i + 7) % n]); ++nops; }
    for (unsigned i = 0; i < n; ++i)
    {
        bm::bvector<>::size_type cnt = w[i].count();
        h = fnv1a(h, &cnt, sizeof(cnt)); ++nops;
    }
    for (unsigned i = 0; i < n; ++i) { w[i].optimize(); ++nops; }
    for (unsigned i = 0; i < n; ++i)
    {
        // XOR-compression path: bmxor's block differencing and change counting.
        bvect t; t.bit_xor(w[i], c.v[i]); ++nops;
        bm::bvector<>::size_type cnt = t.count();
        h = fnv1a(h, &cnt, sizeof(cnt));
        bool eq = t.equal(c.v[(i + 1) % n]);
        h = fnv1a(h, &eq, sizeof(eq));
        bvect::size_type idx = 0;
        t.find(idx);                       // first set bit at or after 0
        h = fnv1a(h, &idx, sizeof(idx));
    }
    // The aggregator drives the fused multi-way block forms the ladder scores
    // (and/or/sub 2-, 3- and 5-way) rather than the plain binary ones.
    {
        bm::aggregator<bvect> agg;
        for (unsigned i = 0; i < n; ++i) agg.add(&c.v[i], i & 1u);
        bvect a, o;
        agg.combine_and(a);
        agg.combine_or(o);
        bvect::size_type ca = a.count(), co = o.count();
        h = fnv1a(h, &ca, sizeof(ca)); h = fnv1a(h, &co, sizeof(co));
        agg.reset();
        agg.set_optimization();
        for (unsigned i = 0; i < n; ++i) agg.add(&c.v[i]);
        bvect s2;
        agg.combine_and_sub(s2);
        bvect::size_type cs = s2.count();
        h = fnv1a(h, &cs, sizeof(cs));
        ++nops;
    }
    for (unsigned i = 0; i < n; ++i)
    {
        // Representation-sensitive scalar paths: set_bit_conditional and the
        // range predicates behave differently on bitmap and GAP blocks.
        bvect x(c.v[i]);
        x.optimize();                       // force the bitmap / GAP decision
        bool cond = x.set_bit_conditional(12345u, true, true);
        x.set_bit_conditional(54321u, false, cond);
        unsigned long long nb = (unsigned long long)x.count();
        bvect::size_type cr = x.count_range(1000, 9000000);
        bvect::size_type fp = 0;
        x.find(fp);                         // first set bit at or after 0
        h = fnv1a(h, &cond, sizeof(cond)); h = fnv1a(h, &nb, sizeof(nb));
        h = fnv1a(h, &cr, sizeof(cr)); h = fnv1a(h, &fp, sizeof(fp));
        ++nops;
    }
    for (unsigned i = 0; i < n; ++i)
    {
        // Serialization depends on representation choice, digests and GAP
        // encoding, so it is a sharp end-to-end check on the block layer.
        bm::serializer<bvect> ser;
        bm::serializer<bvect>::buffer sbuf;
        ser.serialize(w[i], sbuf);
        h = fnv1a(h, sbuf.data(), sbuf.size());
        bvect back;
        bm::deserialize(back, sbuf.data());
        bm::bvector<>::size_type cnt = back.count();
        h = fnv1a(h, &cnt, sizeof(cnt));
        ++nops;
    }
    if (ops) *ops = nops;
    return h;
}

int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "bench";
    uint64_t seed = (argc > 2) ? std::strtoull(argv[2], 0, 10) : 20260923ull;

    Corpus c;
    c.build(seed);

    if (std::strcmp(mode, "digest") == 0)
    {
        unsigned long long ops = 0;
        uint64_t h = composite(c, &ops);
        // The op count is printed beside the digest so a reader -- and the
        // verifier -- can see the digest came from a corpus of known size and
        // was not the digest of an empty run.
        std::printf("%016llx %llu\n", (unsigned long long)h, ops);
        return 0;
    }

    // Best of 3, so a scheduling hiccup on a host shared with other sessions
    // does not read as a regression.  Best-of-N is the wrong aggregator for a
    // ratio, but this is whole-program wall clock for a pass/fail guard, where
    // the noise is one-sided and only a real slowdown matters.
    double best = 1e30;
    unsigned long long ops = 0;
    for (int r = 0; r < 3; ++r)
    {
        Corpus cc; cc.build(seed);
        auto t0 = std::chrono::steady_clock::now();
        composite(cc, &ops);
        auto t1 = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> d = t1 - t0;
        if (d.count() < best) best = d.count();
    }
    std::printf("%.3f\n", best);
    (void)ops;
    return 0;
}
