package redis;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The four RedisBloom value types, ported from
 * modsrc/RedisBloom/{deps/bloom/bloom.c, src/sb.c, src/cuckoo.c, src/cms.c,
 * src/topk.c} so that sizes, capacities and membership answers agree with the
 * module bundled in redis 8.0.
 *
 * The hashes matter: a bloom chain hashes with MurmurHash64A (seed
 * 0xc6a4a7935bd1e995, then the first result as the seed of a second pass), a
 * cuckoo filter with MurmurHash64A seeded 0, and CMS / TopK with the 32 bit
 * MurmurHash2 seeded by the row index.
 */
public final class RBloom {
    private RBloom() {}

    // ==================================================================
    // MurmurHash2
    // ==================================================================

    public static long murmur64(byte[] key, long seed) {
        final long m = 0xc6a4a7935bd1e995L;
        final int r = 47;
        int len = key.length;
        long h = seed ^ (len * m);
        int n = len / 8;
        int p = 0;
        for (int i = 0; i < n; i++) {
            long k = 0;
            for (int j = 7; j >= 0; j--) k = (k << 8) | (key[p + j] & 0xffL);   /* little endian */
            p += 8;
            k *= m;
            k ^= k >>> r;
            k *= m;
            h ^= k;
            h *= m;
        }
        switch (len & 7) {
            case 7: h ^= (key[p + 6] & 0xffL) << 48;
            case 6: h ^= (key[p + 5] & 0xffL) << 40;
            case 5: h ^= (key[p + 4] & 0xffL) << 32;
            case 4: h ^= (key[p + 3] & 0xffL) << 24;
            case 3: h ^= (key[p + 2] & 0xffL) << 16;
            case 2: h ^= (key[p + 1] & 0xffL) << 8;
            case 1: h ^= (key[p] & 0xffL); h *= m;
            default:
        }
        h ^= h >>> r;
        h *= m;
        h ^= h >>> r;
        return h;
    }

    public static int murmur32(byte[] key, int seed) {
        final int m = 0x5bd1e995;
        final int r = 24;
        int len = key.length;
        int h = seed ^ len;
        int p = 0;
        while (len >= 4) {
            int k = (key[p] & 0xff) | ((key[p + 1] & 0xff) << 8)
                  | ((key[p + 2] & 0xff) << 16) | ((key[p + 3] & 0xff) << 24);
            k *= m;
            k ^= k >>> r;
            k *= m;
            h *= m;
            h ^= k;
            p += 4;
            len -= 4;
        }
        switch (len) {
            case 3: h ^= (key[p + 2] & 0xff) << 16;
            case 2: h ^= (key[p + 1] & 0xff) << 8;
            case 1: h ^= (key[p] & 0xff); h *= m;
            default:
        }
        h ^= h >>> 13;
        h *= m;
        h ^= h >>> 15;
        return h;
    }

    /** Unsigned remainder for a 64 bit hash. */
    static long umod(long a, long b) { return Long.remainderUnsigned(a, b); }

    // ==================================================================
    // scalable bloom filter (sb.c + bloom.c)
    // ==================================================================

    static final double LN2SQUARED = 0.480453013918201;
    static final double LN2 = 0.693147180559945;
    static final double TIGHTENING = 0.5;

    /** One sub-filter: bloom_init() with BLOOM_OPT_NOROUND | BLOOM_OPT_FORCE64. */
    public static final class Link {
        public long entries;
        public double error;
        public double bpe;
        public long bits;
        public long bytes;
        public int hashes;
        public byte[] bf;
        public long size;          // items in this link

        Link() {}

        static Link create(long entries, double error) {
            Link l = new Link();
            l.entries = entries;
            l.error = error;
            l.bpe = Math.abs(-(Math.log(error) / LN2SQUARED));
            long bits = (long) (entries * l.bpe);
            if (bits == 0) bits = 1;
            l.bytes = (bits % 64 != 0) ? ((bits / 64) + 1) * 8 : bits / 8;
            l.bits = l.bytes * 8;
            l.hashes = (int) Math.ceil(LN2 * l.bpe);
            l.bf = new byte[(int) l.bytes];
            return l;
        }

        private boolean testSet(long x, boolean write) {
            int byteIx = (int) (x >>> 3);
            int mask = 1 << (int) (x & 7);
            int c = bf[byteIx] & 0xff;
            if ((c & mask) != 0) return true;
            if (write) bf[byteIx] = (byte) (c | mask);
            return false;
        }

        /** CHECK_ADD_FUNC with the compat (non power of two) modulus. */
        boolean checkAdd(long a, long b, boolean write) {
            boolean foundUnset = false;
            for (int i = 0; i < hashes; i++) {
                long x = umod(a + (long) i * b, bits);
                if (!testSet(x, write)) {
                    if (!write) return false;
                    foundUnset = true;
                }
            }
            if (!write) return true;
            return foundUnset;
        }
    }

    /** SBChain. */
    public static final class Bloom implements ModVal {
        public final List<Link> filters = new ArrayList<>();
        public long size;              // total items
        public boolean nonScaling;
        public int growth;

        @Override public String moduleTypeName() { return "MBbloom--"; }
        @Override public int moduleId() { return ModTypes.ID_BLOOM; }

        public static Bloom create(long capacity, double error, int growth, boolean nonScaling) {
            Bloom b = new Bloom();
            b.growth = growth;
            b.nonScaling = nonScaling;
            b.filters.add(Link.create(capacity, error * (nonScaling ? 1.0 : TIGHTENING)));
            return b;
        }

        private long[] hash(byte[] item) {
            long a = murmur64(item, 0xc6a4a7935bd1e995L);
            long b = murmur64(item, a);
            return new long[]{a, b};
        }

        /** SBChain_Add: 1 when newly added, 0 when present, -2 when full. */
        public int add(byte[] item) {
            long[] h = hash(item);
            for (int i = filters.size() - 1; i >= 0; i--) {
                if (filters.get(i).checkAdd(h[0], h[1], false)) return 0;
            }
            Link cur = filters.get(filters.size() - 1);
            if (cur.size >= cur.entries) {
                if (nonScaling) return -2;
                if (growth == 0) return -1;
                cur = Link.create(cur.entries * growth, cur.error * TIGHTENING);
                filters.add(cur);
            }
            /* bloom_add_h() returns "was already there"; checkAdd() reports
             * whether any bit had to be set, which is the same signal negated. */
            boolean wasNew = cur.checkAdd(h[0], h[1], true);
            if (wasNew) { cur.size++; size++; return 1; }
            return 0;
        }

        public boolean check(byte[] item) {
            long[] h = hash(item);
            for (int i = filters.size() - 1; i >= 0; i--) {
                if (filters.get(i).checkAdd(h[0], h[1], false)) return true;
            }
            return false;
        }

        public long capacity() {
            long c = 0;
            for (Link l : filters) c += l.entries;
            return c;
        }

        /** BFMemUsage(): sizeof(SBChain) + nfilters * sizeof(SBLink) + the bit arrays. */
        public long memUsage() {
            long n = 32 + 64L * filters.size();
            for (Link l : filters) n += l.bytes;
            return n;
        }

        @Override public byte[] moduleSave() {
            ModTypes.Out o = new ModTypes.Out();
            o.u8(1);                     // format version
            o.i64(size);
            o.u8(nonScaling ? 1 : 0);
            o.i32(growth);
            o.i32(filters.size());
            for (Link l : filters) {
                o.i64(l.entries);
                o.dbl(l.error);
                o.i64(l.size);
                o.blob(l.bf);
            }
            return o.bytes();
        }
    }

    public static Object loadBloom(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        Bloom b = new Bloom();
        b.size = in.i64();
        b.nonScaling = in.u8() != 0;
        b.growth = in.i32();
        int n = in.i32();
        for (int i = 0; i < n; i++) {
            long entries = in.i64();
            double error = in.dbl();
            Link l = Link.create(entries, error);
            l.size = in.i64();
            byte[] bits = in.blob();
            System.arraycopy(bits, 0, l.bf, 0, Math.min(bits.length, l.bf.length));
            b.filters.add(l);
        }
        return b;
    }

    // ==================================================================
    // cuckoo filter (cuckoo.c)
    // ==================================================================

    public static final class SubCf {
        public long numBuckets;
        public int bucketSize;
        public byte[] data;
    }

    public static final class Cuckoo implements ModVal {
        public long numBuckets;
        public long numItems;
        public long numDeletes;
        public int bucketSize;
        public int maxIterations;
        public int expansion;
        public final List<SubCf> filters = new ArrayList<>();

        @Override public String moduleTypeName() { return "MBbloomCF"; }
        @Override public int moduleId() { return ModTypes.ID_CUCKOO; }

        static long nextPow2(long n) {
            n--;
            n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16; n |= n >> 32;
            return n + 1;
        }

        public static Cuckoo create(long capacity, int bucketSize, int maxIterations, int expansion) {
            Cuckoo c = new Cuckoo();
            c.expansion = (int) nextPow2(expansion);
            if (expansion == 0) c.expansion = 0;
            c.bucketSize = bucketSize;
            c.maxIterations = maxIterations;
            c.numBuckets = nextPow2(capacity / bucketSize);
            if (c.numBuckets == 0) c.numBuckets = 1;
            c.grow();
            return c;
        }

        boolean grow() {
            SubCf f = new SubCf();
            f.bucketSize = bucketSize;
            long growth = (long) Math.pow(Math.max(expansion, 0), filters.size());
            if (filters.isEmpty()) growth = 1;
            f.numBuckets = numBuckets * growth;
            long cells = f.numBuckets * bucketSize;
            if (cells > (1 << 28)) return false;
            f.data = new byte[(int) cells];
            filters.add(f);
            return true;
        }

        static long altHash(int fp, long index) { return index ^ ((fp & 0xffffffffL) * 0x5bd1e995L); }

        /** getLookupParams(): fingerprint, primary and alternate bucket hash. */
        static long[] params(byte[] item) {
            long hash = murmur64(item, 0);
            int fp = (int) (umod(hash, 255) + 1);
            return new long[]{fp, hash, altHash(fp, hash)};
        }

        private static int index(SubCf f, long hash) {
            return (int) (umod(hash, f.numBuckets) * f.bucketSize);
        }

        private static boolean bucketHas(byte[] d, int at, int n, int fp) {
            for (int i = 0; i < n; i++) if ((d[at + i] & 0xff) == fp) return true;
            return false;
        }

        private static int bucketCount(byte[] d, int at, int n, int fp) {
            int r = 0;
            for (int i = 0; i < n; i++) if ((d[at + i] & 0xff) == fp) r++;
            return r;
        }

        private static int findFree(byte[] d, int at, int n) {
            for (int i = 0; i < n; i++) if (d[at + i] == 0) return at + i;
            return -1;
        }

        public boolean check(byte[] item) {
            long[] p = params(item);
            return checkFp(p);
        }

        private boolean checkFp(long[] p) {
            int fp = (int) p[0];
            for (SubCf f : filters) {
                if (bucketHas(f.data, index(f, p[1]), f.bucketSize, fp)
                        || bucketHas(f.data, index(f, p[2]), f.bucketSize, fp)) return true;
            }
            return false;
        }

        public long count(byte[] item) {
            long[] p = params(item);
            int fp = (int) p[0];
            long r = 0;
            for (SubCf f : filters) {
                r += bucketCount(f.data, index(f, p[1]), f.bucketSize, fp)
                   + bucketCount(f.data, index(f, p[2]), f.bucketSize, fp);
            }
            return r;
        }

        public boolean delete(byte[] item) {
            long[] p = params(item);
            int fp = (int) p[0];
            for (int i = filters.size() - 1; i >= 0; i--) {
                SubCf f = filters.get(i);
                if (bucketDelete(f.data, index(f, p[1]), f.bucketSize, fp)
                        || bucketDelete(f.data, index(f, p[2]), f.bucketSize, fp)) {
                    numItems--;
                    numDeletes++;
                    if (filters.size() > 1 && numDeletes > (double) numItems * 0.10) compact(false);
                    return true;
                }
            }
            return false;
        }

        private static boolean bucketDelete(byte[] d, int at, int n, int fp) {
            for (int i = 0; i < n; i++) {
                if ((d[at + i] & 0xff) == fp) { d[at + i] = 0; return true; }
            }
            return false;
        }

        /** 1 inserted, 0 exists, -1 no space. */
        public int insert(byte[] item, boolean unique) {
            long[] p = params(item);
            if (unique && checkFp(p)) return 0;
            return insertFp(p, 0);
        }

        private int insertFp(long[] p, int depth) {
            if (depth > 64) return -1;
            int fp = (int) p[0];
            for (int i = filters.size() - 1; i >= 0; i--) {
                SubCf f = filters.get(i);
                int slot = findFree(f.data, index(f, p[1]), f.bucketSize);
                if (slot < 0) slot = findFree(f.data, index(f, p[2]), f.bucketSize);
                if (slot >= 0) { f.data[slot] = (byte) fp; numItems++; return 1; }
            }
            if (koInsert(filters.get(filters.size() - 1), p)) { numItems++; return 1; }
            if (expansion == 0) return -1;
            if (!grow()) return -1;
            return insertFp(p, depth + 1);
        }

        /** Filter_KOInsert(): evict along the cuckoo path, rolling back on failure. */
        private boolean koInsert(SubCf f, long[] p) {
            int fp = (int) p[0];
            long numB = f.numBuckets;
            int bs = f.bucketSize;
            int counter = 0;
            int victimIx = 0;
            long ii = umod(p[1], numB);
            while (counter++ < maxIterations) {
                int bucket = (int) (ii * bs);
                int tmp = f.data[bucket + victimIx] & 0xff;
                f.data[bucket + victimIx] = (byte) fp;
                fp = tmp;
                ii = umod(altHash(fp, ii), numB);
                int empty = findFree(f.data, (int) (ii * bs), bs);
                if (empty >= 0) { f.data[empty] = (byte) fp; return true; }
                victimIx = (victimIx + 1) % bs;
            }
            counter = 0;
            while (counter++ < maxIterations) {
                victimIx = (victimIx + bs - 1) % bs;
                ii = umod(altHash(fp, ii), numB);
                int bucket = (int) (ii * bs);
                int tmp = f.data[bucket + victimIx] & 0xff;
                f.data[bucket + victimIx] = (byte) fp;
                fp = tmp;
            }
            return false;
        }

        /** CuckooFilter_Compact(): push entries down into the older sub-filters. */
        public void compact(boolean cont) {
            for (int ii = filters.size() - 1; ii > 0; ii--) {
                if (!compactSingle(ii) && !cont) break;
            }
            numDeletes = 0;
        }

        private boolean compactSingle(int filterIx) {
            SubCf cur = filters.get(filterIx);
            boolean ok = true;
            for (long bucketIx = 0; bucketIx < cur.numBuckets; bucketIx++) {
                int at = (int) (bucketIx * cur.bucketSize);
                for (int slotIx = 0; slotIx < cur.bucketSize; slotIx++) {
                    int fp = cur.data[at + slotIx] & 0xff;
                    if (fp == 0) continue;
                    long h1 = bucketIx;
                    long h2 = altHash(fp, bucketIx);
                    boolean moved = false;
                    for (int i = 0; i < filterIx && !moved; i++) {
                        SubCf f = filters.get(i);
                        int slot = findFree(f.data, index(f, h1), f.bucketSize);
                        if (slot < 0) slot = findFree(f.data, index(f, h2), f.bucketSize);
                        if (slot >= 0) {
                            f.data[slot] = (byte) fp;
                            cur.data[at + slotIx] = 0;
                            moved = true;
                        }
                    }
                    if (!moved) ok = false;
                }
            }
            if (ok && filterIx == filters.size() - 1) filters.remove(filterIx);
            return ok;
        }

        /** CFMemUsage(). */
        public long memUsage() {
            long n = 40 + 16L * filters.size();
            for (SubCf f : filters) n += (long) f.bucketSize * f.numBuckets;
            return n;
        }

        @Override public byte[] moduleSave() {
            ModTypes.Out o = new ModTypes.Out();
            o.u8(1);
            o.i64(numBuckets); o.i64(numItems); o.i64(numDeletes);
            o.i32(bucketSize); o.i32(maxIterations); o.i32(expansion);
            o.i32(filters.size());
            for (SubCf f : filters) { o.i64(f.numBuckets); o.i32(f.bucketSize); o.blob(f.data); }
            return o.bytes();
        }
    }

    public static Object loadCuckoo(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        Cuckoo c = new Cuckoo();
        c.numBuckets = in.i64(); c.numItems = in.i64(); c.numDeletes = in.i64();
        c.bucketSize = in.i32(); c.maxIterations = in.i32(); c.expansion = in.i32();
        int n = in.i32();
        for (int i = 0; i < n; i++) {
            SubCf f = new SubCf();
            f.numBuckets = in.i64();
            f.bucketSize = in.i32();
            f.data = in.blob();
            c.filters.add(f);
        }
        return c;
    }

    // ==================================================================
    // count-min sketch (cms.c)
    // ==================================================================

    public static final class Cms implements ModVal {
        public long width, depth;
        public long counter;
        public long[] array;           // cellSize is always 4 bytes for new sketches

        @Override public String moduleTypeName() { return "CMSk-TYPE"; }
        @Override public int moduleId() { return ModTypes.ID_CMS; }

        public static final long CELL_MAX = 0xFFFFFFFFL;   /* CMS_CELL_MAX(4) */

        public static Cms create(long width, long depth) {
            Cms c = new Cms();
            c.width = width;
            c.depth = depth;
            c.array = new long[(int) (width * depth)];
            return c;
        }

        /** CMS_DimFromProb(). */
        public static long[] dimFromProb(double overEst, double prob) {
            long w = (long) Math.ceil(2 / overEst);
            long d = (long) Math.ceil(Math.log10((float) prob) / Math.log10(0.5f));
            return new long[]{w, d};
        }

        private int loc(byte[] item, int row) {
            long h = murmur32(item, row) & 0xffffffffL;
            return (int) (h % width + (long) row * width);
        }

        /**
         * CMS_IncrBy as the RedisBloom bundled with redis 8.0 implements it: a
         * cell saturates at UINT32_MAX, the sketch counter always takes the
         * whole increment, and the command reports an overflow when the
         * resulting estimate is the saturated value.
         *
         * Returns 0 on success and 1 on overflow; the estimate lands in out[0]
         * either way.
         */
        public int incrBy(byte[] item, long value, long[] out) {
            long minCount = Long.MAX_VALUE;
            for (int i = 0; i < depth; i++) {
                int l = loc(item, i);
                long cell = array[l];
                long updated = (CELL_MAX - cell < value) ? CELL_MAX : cell + value;
                array[l] = updated;
                minCount = Math.min(minCount, updated);
            }
            counter += value;
            out[0] = minCount;
            return minCount == CELL_MAX ? 1 : 0;
        }

        public long query(byte[] item) {
            long min = Long.MAX_VALUE;
            for (int i = 0; i < depth; i++) min = Math.min(min, array[loc(item, i)]);
            return min;
        }

        /** CMS_Merge(); false when the result would overflow a cell or the counter. */
        public boolean merge(List<Cms> src, long[] weights) {
            int n = src.size();
            for (int i = 0; i < depth; i++) {
                for (int j = 0; j < width; j++) {
                    long itemCount = 0;
                    for (int k = 0; k < n; k++) itemCount += src.get(k).array[(int) (i * width + j)] * weights[k];
                    if (itemCount < 0 || itemCount > CELL_MAX) return false;
                }
            }
            long cmsCount = 0;
            for (int i = 0; i < n; i++) cmsCount += src.get(i).counter * weights[i];
            if (cmsCount < 0) return false;
            for (int i = 0; i < depth; i++) {
                for (int j = 0; j < width; j++) {
                    long itemCount = 0;
                    for (int k = 0; k < n; k++) itemCount += src.get(k).array[(int) (i * width + j)] * weights[k];
                    array[(int) (i * width + j)] = itemCount;
                }
            }
            counter = cmsCount;
            return true;
        }

        @Override public byte[] moduleSave() {
            ModTypes.Out o = new ModTypes.Out();
            o.u8(1);
            o.i64(width); o.i64(depth); o.i64(counter);
            o.i32(array.length);
            for (long v : array) o.i32((int) v);
            return o.bytes();
        }
    }

    public static Object loadCms(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        Cms c = new Cms();
        c.width = in.i64(); c.depth = in.i64(); c.counter = in.i64();
        int n = in.i32();
        c.array = new long[n];
        for (int i = 0; i < n; i++) c.array[i] = in.i32() & 0xffffffffL;
        return c;
    }

    // ==================================================================
    // top-k (topk.c)
    // ==================================================================

    public static final class Topk implements ModVal {
        public static final int DECAY_LOOKUP_TABLE = 256;
        public static final int GA = 1919;

        public int k, width, depth;
        public double decay;
        public int[] bucketFp;
        public long[] bucketCount;
        public int[] heapFp;
        public long[] heapCount;
        public byte[][] heapItem;
        private double[] lookup;

        @Override public String moduleTypeName() { return "TopK-TYPE"; }
        @Override public int moduleId() { return ModTypes.ID_TOPK; }

        public static Topk create(int k, int width, int depth, double decay) {
            Topk t = new Topk();
            t.k = k; t.width = width; t.depth = depth; t.decay = decay;
            t.bucketFp = new int[width * depth];
            t.bucketCount = new long[width * depth];
            t.heapFp = new int[k];
            t.heapCount = new long[k];
            t.heapItem = new byte[k][];
            t.initLookup();
            return t;
        }

        private void initLookup() {
            lookup = new double[DECAY_LOOKUP_TABLE];
            for (int i = 0; i < DECAY_LOOKUP_TABLE; i++) lookup[i] = Math.pow(decay, i);
        }

        private int fp(byte[] item) { return murmur32(item, GA); }

        private int heapIndexOf(byte[] item) {
            int f = fp(item);
            for (int i = k - 1; i >= 0; i--) {
                if (heapFp[i] == f && heapItem[i] != null && Arrays.equals(heapItem[i], item)) return i;
            }
            return -1;
        }

        /** heapifyDown() from topk.c: a min-heap keyed on count. */
        private void heapifyDown(int start) {
            int len = k;
            int child = start;
            if (len < 2 || (len - 2) / 2 < child) return;
            child = 2 * child + 1;
            if ((child + 1) < len && heapCount[child] > heapCount[child + 1]) child++;
            if (heapCount[child] > heapCount[start]) return;
            int topFp = heapFp[start];
            long topCount = heapCount[start];
            byte[] topItem = heapItem[start];
            do {
                heapFp[start] = heapFp[child];
                heapCount[start] = heapCount[child];
                heapItem[start] = heapItem[child];
                start = child;
                if ((len - 2) / 2 < child) break;
                child = 2 * child + 1;
                if ((child + 1) < len && heapCount[child] > heapCount[child + 1]) child++;
            } while (heapCount[child] < topCount);
            heapFp[start] = topFp;
            heapCount[start] = topCount;
            heapItem[start] = topItem;
        }

        /** TopK_Add(): returns the expelled item, or null. */
        public byte[] add(byte[] item, long increment, java.util.Random rnd) {
            int f = fp(item);
            long maxCount = 0;
            long heapMin = heapCount[0];
            for (int i = 0; i < depth; i++) {
                int loc = (int) ((murmur32(item, i) & 0xffffffffL) % width) + i * width;
                if (bucketCount[loc] == 0) {
                    bucketFp[loc] = f;
                    bucketCount[loc] = increment;
                    maxCount = Math.max(maxCount, bucketCount[loc]);
                } else if (bucketFp[loc] == f) {
                    bucketCount[loc] += increment;
                    maxCount = Math.max(maxCount, bucketCount[loc]);
                } else {
                    for (long localIncr = increment; localIncr > 0; localIncr--) {
                        long cnt = bucketCount[loc];
                        double d;
                        if (cnt < DECAY_LOOKUP_TABLE) {
                            d = lookup[(int) cnt];
                        } else {
                            d = Math.pow(lookup[DECAY_LOOKUP_TABLE - 1], cnt / (DECAY_LOOKUP_TABLE - 1))
                                    * lookup[(int) (cnt % (DECAY_LOOKUP_TABLE - 1))];
                        }
                        if (rnd.nextDouble() < d) {
                            bucketCount[loc]--;
                            if (bucketCount[loc] == 0) {
                                bucketFp[loc] = f;
                                bucketCount[loc] = localIncr;
                                maxCount = Math.max(maxCount, bucketCount[loc]);
                                break;
                            }
                        }
                    }
                }
            }
            if (maxCount >= heapMin) {
                int at = heapIndexOf(item);
                if (at >= 0) {
                    heapCount[at] = maxCount;
                    heapifyDown(at);
                } else {
                    byte[] expelled = heapItem[0];
                    heapCount[0] = maxCount;
                    heapFp[0] = f;
                    heapItem[0] = item.clone();
                    heapifyDown(0);
                    return expelled;
                }
            }
            return null;
        }

        public boolean query(byte[] item) { return heapIndexOf(item) >= 0; }

        public long count(byte[] item) {
            int f = fp(item);
            long heapMin = heapCount[0];
            boolean inHeap = heapIndexOf(item) >= 0;
            long res = 0;
            for (int i = 0; i < depth; i++) {
                int loc = (int) ((murmur32(item, i) & 0xffffffffL) % width) + i * width;
                if (bucketFp[loc] == f && (!inHeap || bucketCount[loc] >= heapMin))
                    res = Math.max(res, bucketCount[loc]);
            }
            return res;
        }

        /** TopK_List(): a stable sort of the heap by descending count. */
        public int[] listOrder() {
            Integer[] ix = new Integer[k];
            for (int i = 0; i < k; i++) ix[i] = i;
            Arrays.sort(ix, (a, b) -> Long.compare(heapCount[b], heapCount[a]));
            int[] r = new int[k];
            for (int i = 0; i < k; i++) r[i] = ix[i];
            return r;
        }

        @Override public byte[] moduleSave() {
            ModTypes.Out o = new ModTypes.Out();
            o.u8(1);
            o.i32(k); o.i32(width); o.i32(depth); o.dbl(decay);
            for (int i = 0; i < bucketFp.length; i++) { o.i32(bucketFp[i]); o.i64(bucketCount[i]); }
            for (int i = 0; i < k; i++) {
                o.i32(heapFp[i]);
                o.i64(heapCount[i]);
                o.u8(heapItem[i] == null ? 0 : 1);
                if (heapItem[i] != null) o.blob(heapItem[i]);
            }
            return o.bytes();
        }
    }

    public static Object loadTopk(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        int k = in.i32(), width = in.i32(), depth = in.i32();
        double decay = in.dbl();
        Topk t = Topk.create(k, width, depth, decay);
        for (int i = 0; i < t.bucketFp.length; i++) { t.bucketFp[i] = in.i32(); t.bucketCount[i] = in.i64(); }
        for (int i = 0; i < k; i++) {
            t.heapFp[i] = in.i32();
            t.heapCount[i] = in.i64();
            t.heapItem[i] = in.u8() != 0 ? in.blob() : null;
        }
        return t;
    }

    // ==================================================================
    // t-digest (a plain sorted sample set; only the reported statistics
    // have to agree with the module)
    // ==================================================================

    public static final class TDigest implements ModVal {
        public long compression = 100;
        public final List<double[]> centroids = new ArrayList<>();   // {mean, weight}
        public double min = Double.NaN, max = Double.NaN;
        public double totalWeight = 0;
        public long observations = 0;
        public long compressions = 0;
        public long mergedCentroids = 0;

        @Override public String moduleTypeName() { return "TDIS-TYPE"; }
        @Override public int moduleId() { return ModTypes.ID_TDIGEST; }

        public void reset() {
            centroids.clear();
            min = Double.NaN;
            max = Double.NaN;
            totalWeight = 0;
            observations = 0;
            compressions = 0;
            mergedCentroids = 0;
        }

        /**
         * Folding the unmerged buffer into the sorted digest. Every read does
         * it, which is why TDIGEST.INFO reports a compression after the first
         * query but not straight after a TDIGEST.ADD.
         */
        public void compress() {
            if (centroids.size() > mergedCentroids) {
                mergedCentroids = centroids.size();
                compressions++;
            }
        }

        public void add(double v, double w) {
            int lo = 0, hi = centroids.size();
            while (lo < hi) {
                int mid = (lo + hi) >>> 1;
                if (centroids.get(mid)[0] < v) lo = mid + 1; else hi = mid;
            }
            centroids.add(lo, new double[]{v, w});
            totalWeight += w;
            observations++;
            if (Double.isNaN(min) || v < min) min = v;
            if (Double.isNaN(max) || v > max) max = v;
        }

        @Override public byte[] moduleSave() {
            ModTypes.Out o = new ModTypes.Out();
            o.u8(1);
            o.i64(compression);
            o.dbl(min); o.dbl(max); o.dbl(totalWeight); o.i64(observations);
            o.i64(compressions); o.i64(mergedCentroids);
            o.i32(centroids.size());
            for (double[] c : centroids) { o.dbl(c[0]); o.dbl(c[1]); }
            return o.bytes();
        }
    }

    public static Object loadTDigest(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        TDigest t = new TDigest();
        t.compression = in.i64();
        t.min = in.dbl(); t.max = in.dbl(); t.totalWeight = in.dbl(); t.observations = in.i64();
        t.compressions = in.i64(); t.mergedCentroids = in.i64();
        int n = in.i32();
        for (int i = 0; i < n; i++) t.centroids.add(new double[]{in.dbl(), in.dbl()});
        return t;
    }
}
