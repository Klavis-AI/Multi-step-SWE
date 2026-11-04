package redis;

import java.util.Random;

/**
 * HyperLogLog commands: a port of redis 8.0.6 src/hyperloglog.c.
 *
 * PFADD / PFCOUNT / PFMERGE / PFSELFTEST / PFDEBUG.
 *
 * The value is a plain redis string whose layout is byte compatible with the
 * C implementation:
 *
 *   +------+---+-----+----------+
 *   | HYLL | E | N/U | Cardin.  |
 *   +------+---+-----+----------+
 *
 * followed either by 16384 6-bit registers (dense) or by a run length encoded
 * stream of ZERO / XZERO / VAL opcodes (sparse).  The cached cardinality is a
 * little endian 64 bit integer whose most significant bit, when set, marks the
 * cache as stale.
 */
public final class HllCmds {
    private HllCmds() {}

    /* ------------------------------------------------------------------ */
    /* constants (hyperloglog.c)                                          */
    /* ------------------------------------------------------------------ */
    static final int HLL_P = 14;
    static final int HLL_Q = 64 - HLL_P;                 /* 50 */
    static final int HLL_REGISTERS = 1 << HLL_P;         /* 16384 */
    static final int HLL_P_MASK = HLL_REGISTERS - 1;
    static final int HLL_BITS = 6;
    static final int HLL_REGISTER_MAX = (1 << HLL_BITS) - 1;
    static final int HLL_HDR_SIZE = 16;
    static final int HLL_DENSE_SIZE = HLL_HDR_SIZE + ((HLL_REGISTERS * HLL_BITS + 7) / 8);
    static final int HLL_DENSE = 0;
    static final int HLL_SPARSE = 1;
    static final int HLL_MAX_ENCODING = 1;

    static final int HLL_SPARSE_XZERO_BIT = 0x40;
    static final int HLL_SPARSE_VAL_BIT = 0x80;
    static final int HLL_SPARSE_VAL_MAX_VALUE = 32;
    static final int HLL_SPARSE_VAL_MAX_LEN = 4;
    static final int HLL_SPARSE_ZERO_MAX_LEN = 64;
    static final int HLL_SPARSE_XZERO_MAX_LEN = 16384;

    static final double HLL_ALPHA_INF = 0.721347520444481703680;

    /** invalid_hll_err: the message already carries its own error code. */
    static final String INVALID_HLL_ERR = "INVALIDOBJ Corrupted HLL object detected";
    static final String NOT_HLL_ERR = "WRONGTYPE Key is not a valid HyperLogLog string value.";

    private static RedisError invalidHll() { return new RedisError(INVALID_HLL_ERR); }

    // ------------------------------------------------------------------
    // command table
    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("pfadd", -2, "write denyoom fast", 1, 1, 1, HllCmds::pfadd);
        CommandTable.reg("pfcount", -2, "readonly", 1, -1, 1, HllCmds::pfcount);
        CommandTable.reg("pfmerge", -2, "write denyoom", 1, -1, 1, HllCmds::pfmerge);
        CommandTable.reg("pfselftest", 1, "admin", 0, 0, 0, HllCmds::pfselftest);
        CommandTable.reg("pfdebug", 3, "write denyoom admin", 2, 2, 1, HllCmds::pfdebug);
    }

    // ==================================================================
    // low level bit macros
    // ==================================================================

    /** HLL_DENSE_GET_REGISTER(): read the 6-bit register 'regnum'. */
    static int denseGet(byte[] p, int off, int regnum) {
        int bytepos = off + regnum * HLL_BITS / 8;
        int fb = (regnum * HLL_BITS) & 7;
        int fb8 = 8 - fb;
        int b0 = p[bytepos] & 0xff;
        /* sds strings are implicitly NUL terminated, so the C code may read one
         * byte past the registers; the bits it picks up are always masked out. */
        int b1 = (bytepos + 1 < p.length) ? (p[bytepos + 1] & 0xff) : 0;
        return ((b0 >>> fb) | (b1 << fb8)) & HLL_REGISTER_MAX;
    }

    /** HLL_DENSE_SET_REGISTER(). */
    static void denseSetRegister(byte[] p, int off, int regnum, int val) {
        int bytepos = off + regnum * HLL_BITS / 8;
        int fb = (regnum * HLL_BITS) & 7;
        int fb8 = 8 - fb;
        p[bytepos] &= (byte) ~(HLL_REGISTER_MAX << fb);
        p[bytepos] |= (byte) (val << fb);
        if (bytepos + 1 < p.length) {
            p[bytepos + 1] &= (byte) ~(HLL_REGISTER_MAX >>> fb8);
            p[bytepos + 1] |= (byte) (val >>> fb8);
        }
    }

    /* Sparse opcode accessors; 'p' is an absolute index into 'b'. */
    static boolean sparseIsZero(byte[] b, int p) { return (b[p] & 0xc0) == 0; }
    static boolean sparseIsXzero(byte[] b, int p) { return (b[p] & 0xc0) == HLL_SPARSE_XZERO_BIT; }
    static boolean sparseIsVal(byte[] b, int p) { return (b[p] & HLL_SPARSE_VAL_BIT) != 0; }
    static int sparseZeroLen(byte[] b, int p) { return (b[p] & 0x3f) + 1; }
    static int sparseXzeroLen(byte[] b, int p) {
        int lo = (p + 1 < b.length) ? (b[p + 1] & 0xff) : 0;
        return (((b[p] & 0x3f) << 8) | lo) + 1;
    }
    static int sparseValValue(byte[] b, int p) { return (((b[p] & 0xff) >>> 2) & 0x1f) + 1; }
    static int sparseValLen(byte[] b, int p) { return (b[p] & 0x3) + 1; }

    static void sparseValSet(byte[] b, int p, int val, int len) {
        b[p] = (byte) ((((val - 1) << 2) | (len - 1)) | HLL_SPARSE_VAL_BIT);
    }
    static void sparseZeroSet(byte[] b, int p, int len) { b[p] = (byte) (len - 1); }
    static void sparseXzeroSet(byte[] b, int p, int len) {
        int l = len - 1;
        b[p] = (byte) ((l >>> 8) | HLL_SPARSE_XZERO_BIT);
        b[p + 1] = (byte) (l & 0xff);
    }

    /* ---- cached cardinality --------------------------------------- */
    static boolean validCache(byte[] b) { return (b[15] & (1 << 7)) == 0; }
    static void invalidateCache(byte[] b) { b[15] |= (byte) (1 << 7); }

    static long readCard(byte[] b) {
        long card = 0;
        for (int i = 0; i < 8; i++) card |= ((long) (b[8 + i] & 0xff)) << (8 * i);
        return card;
    }

    static void writeCard(byte[] b, long card) {
        for (int i = 0; i < 8; i++) b[8 + i] = (byte) ((card >>> (8 * i)) & 0xff);
    }

    // ==================================================================
    // hashing
    // ==================================================================

    /** MurmurHash2, 64 bit, endian neutral, exactly as redis computes it. */
    static long murmurHash64A(byte[] key, int len, long seed) {
        final long m = 0xc6a4a7935bd1e995L;
        final int r = 47;
        long h = seed ^ (len * m);
        int end = len - (len & 7);
        int i = 0;
        while (i != end) {
            long k = ((long) (key[i] & 0xff))
                    | ((long) (key[i + 1] & 0xff) << 8)
                    | ((long) (key[i + 2] & 0xff) << 16)
                    | ((long) (key[i + 3] & 0xff) << 24)
                    | ((long) (key[i + 4] & 0xff) << 32)
                    | ((long) (key[i + 5] & 0xff) << 40)
                    | ((long) (key[i + 6] & 0xff) << 48)
                    | ((long) (key[i + 7] & 0xff) << 56);
            k *= m;
            k ^= k >>> r;
            k *= m;
            h ^= k;
            h *= m;
            i += 8;
        }
        switch (len & 7) {
            case 7: h ^= ((long) (key[i + 6] & 0xff)) << 48; /* fall through */
            case 6: h ^= ((long) (key[i + 5] & 0xff)) << 40; /* fall through */
            case 5: h ^= ((long) (key[i + 4] & 0xff)) << 32; /* fall through */
            case 4: h ^= ((long) (key[i + 3] & 0xff)) << 24; /* fall through */
            case 3: h ^= ((long) (key[i + 2] & 0xff)) << 16; /* fall through */
            case 2: h ^= ((long) (key[i + 1] & 0xff)) << 8;  /* fall through */
            case 1: h ^= ((long) (key[i] & 0xff));
                    h *= m;
                    break;
            default: break;
        }
        h ^= h >>> r;
        h *= m;
        h ^= h >>> r;
        return h;
    }

    /**
     * hllPatLen(): the register index and the length of the 000..1 pattern,
     * packed as (count &lt;&lt; 16) | index so no output parameter is needed.
     */
    static int hllPatLen(byte[] ele, int elesize) {
        long hash = murmurHash64A(ele, elesize, 0xadc83b19L);
        int index = (int) (hash & HLL_P_MASK);
        hash >>>= HLL_P;
        hash |= 1L << HLL_Q;
        int count = Long.numberOfTrailingZeros(hash) + 1;
        return (count << 16) | index;
    }

    // ==================================================================
    // dense representation
    // ==================================================================

    /** hllDenseSet(): returns true when the register grew. */
    static boolean hllDenseSet(byte[] b, int index, int count) {
        int oldcount = denseGet(b, HLL_HDR_SIZE, index);
        if (count > oldcount) {
            denseSetRegister(b, HLL_HDR_SIZE, index, count);
            return true;
        }
        return false;
    }

    /** hllDenseRegHisto(): the unrolled 16-registers-per-12-bytes loop. */
    static void hllDenseRegHisto(byte[] b, int off, int[] reghisto) {
        int r = off;
        for (int j = 0; j < 1024; j++) {
            int c0 = b[r] & 0xff, c1 = b[r + 1] & 0xff, c2 = b[r + 2] & 0xff;
            int c3 = b[r + 3] & 0xff, c4 = b[r + 4] & 0xff, c5 = b[r + 5] & 0xff;
            int c6 = b[r + 6] & 0xff, c7 = b[r + 7] & 0xff, c8 = b[r + 8] & 0xff;
            int c9 = b[r + 9] & 0xff, c10 = b[r + 10] & 0xff, c11 = b[r + 11] & 0xff;

            reghisto[c0 & 63]++;
            reghisto[(c0 >>> 6 | c1 << 2) & 63]++;
            reghisto[(c1 >>> 4 | c2 << 4) & 63]++;
            reghisto[(c2 >>> 2) & 63]++;
            reghisto[c3 & 63]++;
            reghisto[(c3 >>> 6 | c4 << 2) & 63]++;
            reghisto[(c4 >>> 4 | c5 << 4) & 63]++;
            reghisto[(c5 >>> 2) & 63]++;
            reghisto[c6 & 63]++;
            reghisto[(c6 >>> 6 | c7 << 2) & 63]++;
            reghisto[(c7 >>> 4 | c8 << 4) & 63]++;
            reghisto[(c8 >>> 2) & 63]++;
            reghisto[c9 & 63]++;
            reghisto[(c9 >>> 6 | c10 << 2) & 63]++;
            reghisto[(c10 >>> 4 | c11 << 4) & 63]++;
            reghisto[(c11 >>> 2) & 63]++;
            r += 12;
        }
    }

    /** hllMergeDense(): max[i] = MAX(max[i], dense[i]). */
    static void hllMergeDense(byte[] max, byte[] b, int off) {
        int r = off, i = 0;
        for (int j = 0; j < 1024; j++) {
            int c0 = b[r] & 0xff, c1 = b[r + 1] & 0xff, c2 = b[r + 2] & 0xff;
            int c3 = b[r + 3] & 0xff, c4 = b[r + 4] & 0xff, c5 = b[r + 5] & 0xff;
            int c6 = b[r + 6] & 0xff, c7 = b[r + 7] & 0xff, c8 = b[r + 8] & 0xff;
            int c9 = b[r + 9] & 0xff, c10 = b[r + 10] & 0xff, c11 = b[r + 11] & 0xff;

            i = mergeOne(max, i, c0 & 63);
            i = mergeOne(max, i, (c0 >>> 6 | c1 << 2) & 63);
            i = mergeOne(max, i, (c1 >>> 4 | c2 << 4) & 63);
            i = mergeOne(max, i, (c2 >>> 2) & 63);
            i = mergeOne(max, i, c3 & 63);
            i = mergeOne(max, i, (c3 >>> 6 | c4 << 2) & 63);
            i = mergeOne(max, i, (c4 >>> 4 | c5 << 4) & 63);
            i = mergeOne(max, i, (c5 >>> 2) & 63);
            i = mergeOne(max, i, c6 & 63);
            i = mergeOne(max, i, (c6 >>> 6 | c7 << 2) & 63);
            i = mergeOne(max, i, (c7 >>> 4 | c8 << 4) & 63);
            i = mergeOne(max, i, (c8 >>> 2) & 63);
            i = mergeOne(max, i, c9 & 63);
            i = mergeOne(max, i, (c9 >>> 6 | c10 << 2) & 63);
            i = mergeOne(max, i, (c10 >>> 4 | c11 << 4) & 63);
            i = mergeOne(max, i, (c11 >>> 2) & 63);
            r += 12;
        }
    }

    private static int mergeOne(byte[] max, int i, int val) {
        if (val > (max[i] & 0xff)) max[i] = (byte) val;
        return i + 1;
    }

    /** hllDenseCompress(): write the raw register array into the dense encoding. */
    static void hllDenseCompress(byte[] b, int off, byte[] raw) {
        for (int i = 0; i < HLL_REGISTERS; i++) denseSetRegister(b, off, i, raw[i] & 0xff);
    }

    // ==================================================================
    // sparse representation
    // ==================================================================

    /**
     * hllSparseToDense(): rewrite 'o' using the dense representation.
     * Returns false (C_ERR) when the sparse representation was corrupted.
     */
    static boolean hllSparseToDense(RObj o) {
        byte[] sparse = (byte[]) o.ptr;
        if ((sparse[4] & 0xff) == HLL_DENSE) return true;

        byte[] dense = new byte[HLL_DENSE_SIZE];
        System.arraycopy(sparse, 0, dense, 0, HLL_HDR_SIZE);  /* magic + cached card */
        dense[4] = HLL_DENSE;

        int idx = 0, p = HLL_HDR_SIZE, end = sparse.length;
        boolean valid = true;
        while (p < end) {
            if (sparseIsZero(sparse, p)) {
                int runlen = sparseZeroLen(sparse, p);
                if ((runlen + idx) > HLL_REGISTERS) { valid = false; break; }
                idx += runlen;
                p++;
            } else if (sparseIsXzero(sparse, p)) {
                int runlen = sparseXzeroLen(sparse, p);
                if ((runlen + idx) > HLL_REGISTERS) { valid = false; break; }
                idx += runlen;
                p += 2;
            } else {
                int runlen = sparseValLen(sparse, p);
                int regval = sparseValValue(sparse, p);
                if ((runlen + idx) > HLL_REGISTERS) { valid = false; break; }
                while (runlen-- > 0) {
                    denseSetRegister(dense, HLL_HDR_SIZE, idx, regval);
                    idx++;
                }
                p++;
            }
        }
        if (!valid || idx != HLL_REGISTERS) return false;
        o.ptr = dense;
        return true;
    }

    /**
     * hllSparseSet(): 1 when the cardinality changed, 0 when nothing was
     * updated, -1 when the representation is invalid.  May promote 'o' to the
     * dense representation as a side effect.
     */
    static int hllSparseSet(RObj o, int index, int count) {
        if (count > HLL_SPARSE_VAL_MAX_VALUE) return sparsePromote(o, index, count);

        byte[] buf = (byte[]) o.ptr;
        int end = buf.length;

        /* Step 1: locate the opcode covering 'index'. */
        int p = HLL_HDR_SIZE;
        int first = 0, span = 0, prev = -1;
        while (p < end) {
            int oplen = 1;
            if (sparseIsZero(buf, p)) {
                span = sparseZeroLen(buf, p);
            } else if (sparseIsVal(buf, p)) {
                span = sparseValLen(buf, p);
            } else {
                span = sparseXzeroLen(buf, p);
                oplen = 2;
            }
            if (index <= first + span - 1) break;
            prev = p;
            p += oplen;
            first += span;
        }
        if (span == 0 || p >= end) return -1;   /* Invalid format. */

        boolean isZero = false, isXzero = false, isVal = false;
        int runlen;
        if (sparseIsZero(buf, p)) {
            isZero = true;
            runlen = sparseZeroLen(buf, p);
        } else if (sparseIsXzero(buf, p)) {
            isXzero = true;
            runlen = sparseXzeroLen(buf, p);
        } else {
            isVal = true;
            runlen = sparseValLen(buf, p);
        }

        boolean updatedInPlace = false;

        /* Case A / B: the opcode is already a VAL. */
        if (isVal) {
            int oldcount = sparseValValue(buf, p);
            if (oldcount >= count) return 0;                       /* Case A. */
            if (runlen == 1) { sparseValSet(buf, p, count, 1); updatedInPlace = true; }  /* Case B. */
        }

        /* Case C: a ZERO of length one becomes a VAL of length one. */
        if (!updatedInPlace && isZero && runlen == 1) {
            sparseValSet(buf, p, count, 1);
            updatedInPlace = true;
        }

        if (!updatedInPlace) {
            /* Case D: split the opcode. */
            byte[] seq = new byte[5];
            int n = 0;
            int last = first + span - 1;
            int len;

            if (isZero || isXzero) {
                if (index != first) {
                    len = index - first;
                    if (len > HLL_SPARSE_ZERO_MAX_LEN) { sparseXzeroSet(seq, n, len); n += 2; }
                    else { sparseZeroSet(seq, n, len); n++; }
                }
                sparseValSet(seq, n, count, 1);
                n++;
                if (index != last) {
                    len = last - index;
                    if (len > HLL_SPARSE_ZERO_MAX_LEN) { sparseXzeroSet(seq, n, len); n += 2; }
                    else { sparseZeroSet(seq, n, len); n++; }
                }
            } else {
                int curval = sparseValValue(buf, p);
                if (index != first) {
                    len = index - first;
                    sparseValSet(seq, n, curval, len);
                    n++;
                }
                sparseValSet(seq, n, count, 1);
                n++;
                if (index != last) {
                    len = last - index;
                    sparseValSet(seq, n, curval, len);
                    n++;
                }
            }

            /* Step 3: substitute the new sequence for the old opcode. */
            int seqlen = n;
            int oldlen = isXzero ? 2 : 1;
            int deltalen = seqlen - oldlen;

            if (deltalen > 0 && end + deltalen > sparseMaxBytes())
                return sparsePromote(o, index, count);

            if (deltalen != 0) {
                byte[] nb = new byte[end + deltalen];
                System.arraycopy(buf, 0, nb, 0, p);
                System.arraycopy(seq, 0, nb, p, seqlen);
                System.arraycopy(buf, p + oldlen, nb, p + seqlen, end - (p + oldlen));
                buf = nb;
                o.ptr = nb;
                end = nb.length;
            } else {
                System.arraycopy(seq, 0, buf, p, seqlen);
            }
        }

        /* Step 4: merge adjacent VAL opcodes when possible. */
        p = (prev >= 0) ? prev : HLL_HDR_SIZE;
        int scanlen = 5;
        while (p < end && scanlen-- > 0) {
            if (sparseIsXzero(buf, p)) { p += 2; continue; }
            if (sparseIsZero(buf, p)) { p++; continue; }
            if (p + 1 < end && sparseIsVal(buf, p + 1)) {
                int v1 = sparseValValue(buf, p);
                int v2 = sparseValValue(buf, p + 1);
                if (v1 == v2) {
                    int len = sparseValLen(buf, p) + sparseValLen(buf, p + 1);
                    if (len <= HLL_SPARSE_VAL_MAX_LEN) {
                        sparseValSet(buf, p + 1, v1, len);
                        System.arraycopy(buf, p + 1, buf, p, end - p - 1);
                        end--;
                        continue;
                    }
                }
            }
            p++;
        }
        if (end != buf.length) {
            byte[] nb = new byte[end];
            System.arraycopy(buf, 0, nb, 0, end);
            buf = nb;
            o.ptr = nb;
        }

        invalidateCache(buf);
        return 1;
    }

    /** The 'promote' label of hllSparseSet(). */
    private static int sparsePromote(RObj o, int index, int count) {
        if (!hllSparseToDense(o)) return -1;   /* Corrupted HLL. */
        byte[] b = (byte[]) o.ptr;
        return hllDenseSet(b, index, count) ? 1 : 0;
    }

    static int sparseMaxBytes() {
        long v = Server.I.config.getLong("hll-sparse-max-bytes");
        if (v < 0) v = 0;
        if (v > Integer.MAX_VALUE) v = Integer.MAX_VALUE;
        return (int) v;
    }

    /** hllSparseRegHisto(): returns true when the representation is corrupted. */
    static boolean hllSparseRegHisto(byte[] b, int[] reghisto) {
        int idx = 0, p = HLL_HDR_SIZE, end = b.length;
        boolean valid = true;
        while (p < end) {
            if (sparseIsZero(b, p)) {
                int runlen = sparseZeroLen(b, p);
                if ((runlen + idx) > HLL_REGISTERS) { valid = false; break; }
                idx += runlen;
                reghisto[0] += runlen;
                p++;
            } else if (sparseIsXzero(b, p)) {
                int runlen = sparseXzeroLen(b, p);
                if ((runlen + idx) > HLL_REGISTERS) { valid = false; break; }
                idx += runlen;
                reghisto[0] += runlen;
                p += 2;
            } else {
                int runlen = sparseValLen(b, p);
                int regval = sparseValValue(b, p);
                if ((runlen + idx) > HLL_REGISTERS) { valid = false; break; }
                idx += runlen;
                reghisto[regval] += runlen;
                p++;
            }
        }
        return !valid || idx != HLL_REGISTERS;
    }

    // ==================================================================
    // cardinality estimation (the Ertl "new estimator", redis >= 4.0)
    // ==================================================================

    /** hllSigma(), as defined in Otmar Ertl, arXiv:1702.01284. */
    static double hllSigma(double x) {
        if (x == 1.0) return Double.POSITIVE_INFINITY;
        double zPrime;
        double y = 1;
        double z = x;
        do {
            x *= x;
            zPrime = z;
            z += x * y;
            y += y;
        } while (zPrime != z);
        return z;
    }

    /** hllTau(), as defined in Otmar Ertl, arXiv:1702.01284. */
    static double hllTau(double x) {
        if (x == 0.0 || x == 1.0) return 0.0;
        double zPrime;
        double y = 1.0;
        double z = 1 - x;
        do {
            x = Math.sqrt(x);
            zPrime = z;
            y *= 0.5;
            z -= Math.pow(1 - x, 2) * y;
        } while (zPrime != z);
        return z / 3;
    }

    /** The tail of hllCount(): turn a register histogram into a cardinality. */
    static long hllCountFromHisto(int[] reghisto) {
        double m = HLL_REGISTERS;
        double z = m * hllTau((m - reghisto[HLL_Q + 1]) / m);
        for (int j = HLL_Q; j >= 1; --j) {
            z += reghisto[j];
            z *= 0.5;
        }
        z += m * hllSigma(reghisto[0] / m);
        return Math.round(HLL_ALPHA_INF * m * m / z);
    }

    /** hllCount() over a stored HLL string; sets invalid[0] on a bad sparse HLL. */
    static long hllCount(byte[] b, boolean[] invalid) {
        int[] reghisto = new int[64];
        if ((b[4] & 0xff) == HLL_DENSE) {
            hllDenseRegHisto(b, HLL_HDR_SIZE, reghisto);
        } else {
            if (hllSparseRegHisto(b, reghisto) && invalid != null) invalid[0] = true;
        }
        return hllCountFromHisto(reghisto);
    }

    /** hllCount() over the internal HLL_RAW representation. */
    static long hllCountRaw(byte[] raw) {
        int[] reghisto = new int[64];
        for (int i = 0; i < HLL_REGISTERS; i++) reghisto[raw[i] & 0xff]++;
        return hllCountFromHisto(reghisto);
    }

    /** hllAdd(): 1 when a register changed, 0 when not, -1 on a bad encoding. */
    static int hllAdd(RObj o, byte[] ele) {
        byte[] b = (byte[]) o.ptr;
        int enc = b[4] & 0xff;
        if (enc == HLL_DENSE) {
            int pat = hllPatLen(ele, ele.length);
            return hllDenseSet(b, pat & 0xffff, pat >>> 16) ? 1 : 0;
        } else if (enc == HLL_SPARSE) {
            int pat = hllPatLen(ele, ele.length);
            return hllSparseSet(o, pat & 0xffff, pat >>> 16);
        }
        return -1;
    }

    /** hllMerge(): max[i] = MAX(max[i], hll[i]); false when the HLL is corrupted. */
    static boolean hllMerge(byte[] max, RObj hll) {
        byte[] b = (byte[]) hll.ptr;
        if ((b[4] & 0xff) == HLL_DENSE) {
            hllMergeDense(max, b, HLL_HDR_SIZE);
            return true;
        }
        int p = HLL_HDR_SIZE, end = b.length, i = 0;
        boolean valid = true;
        while (p < end) {
            if (sparseIsZero(b, p)) {
                int runlen = sparseZeroLen(b, p);
                if ((runlen + i) > HLL_REGISTERS) { valid = false; break; }
                i += runlen;
                p++;
            } else if (sparseIsXzero(b, p)) {
                int runlen = sparseXzeroLen(b, p);
                if ((runlen + i) > HLL_REGISTERS) { valid = false; break; }
                i += runlen;
                p += 2;
            } else {
                int runlen = sparseValLen(b, p);
                int regval = sparseValValue(b, p);
                if ((runlen + i) > HLL_REGISTERS) { valid = false; break; }
                while (runlen-- > 0) {
                    if (regval > (max[i] & 0xff)) max[i] = (byte) regval;
                    i++;
                }
                p++;
            }
        }
        return valid && i == HLL_REGISTERS;
    }

    // ==================================================================
    // object helpers
    // ==================================================================

    /** createHLLObject(): a sparse HLL made of XZERO opcodes only. */
    static RObj createHLLObject() {
        int sparselen = HLL_HDR_SIZE
                + ((HLL_REGISTERS + (HLL_SPARSE_XZERO_MAX_LEN - 1)) / HLL_SPARSE_XZERO_MAX_LEN) * 2;
        byte[] s = new byte[sparselen];
        int aux = HLL_REGISTERS;
        int p = HLL_HDR_SIZE;
        while (aux > 0) {
            int xzero = Math.min(HLL_SPARSE_XZERO_MAX_LEN, aux);
            sparseXzeroSet(s, p, xzero);
            p += 2;
            aux -= xzero;
        }
        s[0] = 'H'; s[1] = 'Y'; s[2] = 'L'; s[3] = 'L';
        s[4] = HLL_SPARSE;
        return RObj.rawString(s);
    }

    /** isHLLObjectOrReply(): throws the exact error redis replies with. */
    static void checkHLLObject(RObj o) {
        if (o.type != RObj.STRING) throw RedisError.wrongType();
        if (o.encoding == RObj.ENC_INT) throw new RedisError(NOT_HLL_ERR);
        byte[] b = (byte[]) o.ptr;
        if (b.length < HLL_HDR_SIZE) throw new RedisError(NOT_HLL_ERR);
        if (b[0] != 'H' || b[1] != 'Y' || b[2] != 'L' || b[3] != 'L')
            throw new RedisError(NOT_HLL_ERR);
        if ((b[4] & 0xff) > HLL_MAX_ENCODING) throw new RedisError(NOT_HLL_ERR);
        if ((b[4] & 0xff) == HLL_DENSE && b.length != HLL_DENSE_SIZE)
            throw new RedisError(NOT_HLL_ERR);
    }

    /** dbUnshareStringValue(): give the object a private, mutable raw buffer. */
    static byte[] unshare(RObj o) {
        return StringCmds.unshareRaw(o);
    }

    // ==================================================================
    // commands
    // ==================================================================

    /** PFADD var ele ele ele ... ele =&gt; :0 or :1 */
    static void pfadd(Client c) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        int updated = 0;

        if (o == null) {
            o = createHLLObject();
            s.dbAdd(c.db, key, o);
            updated++;
        } else {
            checkHLLObject(o);
            unshare(o);
        }

        for (int j = 2; j < c.argc; j++) {
            int retval = hllAdd(o, c.arg(j));
            if (retval == 1) updated++;
            else if (retval == -1) throw invalidHll();
        }

        if (updated > 0) {
            invalidateCache((byte[]) o.ptr);
            s.signalModifiedKey(c.db, new Key(key));
            s.notifyKeyspaceEvent('$', "pfadd", key, c.db);
            s.dirty += updated;
        }
        c.addInt(updated > 0 ? 1 : 0);
    }

    /** PFCOUNT var -&gt; approximated cardinality of set. */
    static void pfcount(Client c) {
        Server s = Server.I;

        /* Case 1: cardinality of the union of many keys. */
        if (c.argc > 2) {
            byte[] max = new byte[HLL_REGISTERS];
            for (int j = 1; j < c.argc; j++) {
                RObj o = s.lookupKeyRead(c, c.arg(j));
                if (o == null) continue;
                checkHLLObject(o);
                if (!hllMerge(max, o)) throw invalidHll();
            }
            c.addInt(hllCountRaw(max));
            return;
        }

        /* Case 2: the cached or recomputed cardinality of a single HLL. */
        byte[] key = c.arg(1);
        RObj o = s.lookupKeyRead(c, key);
        if (o == null) { c.addInt(0); return; }
        checkHLLObject(o);
        byte[] b = unshare(o);

        long card;
        if (validCache(b)) {
            card = readCard(b);
        } else {
            boolean[] invalid = new boolean[1];
            card = hllCount(b, invalid);
            if (invalid[0]) throw invalidHll();
            writeCard(b, card);
            /* Updating the cache modifies the string: propagate the change. */
            s.signalModifiedKey(c.db, new Key(key));
            s.dirty++;
        }
        c.addInt(card);
    }

    /** PFMERGE dest src1 src2 src3 ... srcN =&gt; OK */
    static void pfmerge(Client c) {
        Server s = Server.I;
        byte[] max = new byte[HLL_REGISTERS];
        boolean useDense = false;

        for (int j = 1; j < c.argc; j++) {
            RObj src = s.lookupKeyRead(c, c.arg(j));
            if (src == null) continue;
            checkHLLObject(src);
            if ((((byte[]) src.ptr)[4] & 0xff) == HLL_DENSE) useDense = true;
            if (!hllMerge(max, src)) throw invalidHll();
        }

        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) {
            o = createHLLObject();
            s.dbAdd(c.db, key, o);
        } else {
            unshare(o);
        }

        if (useDense && !hllSparseToDense(o)) throw invalidHll();

        if (useDense) {
            hllDenseCompress((byte[]) o.ptr, HLL_HDR_SIZE, max);
        } else {
            for (int j = 0; j < HLL_REGISTERS; j++) {
                if (max[j] == 0) continue;
                byte[] b = (byte[]) o.ptr;
                if ((b[4] & 0xff) == HLL_DENSE) hllDenseSet(b, j, max[j] & 0xff);
                else hllSparseSet(o, j, max[j] & 0xff);
            }
        }
        invalidateCache((byte[]) o.ptr);

        s.signalModifiedKey(c.db, new Key(key));
        /* PFMERGE fires a "pfadd" event: semantically it is a mass add. */
        s.notifyKeyspaceEvent('$', "pfadd", key, c.db);
        s.dirty++;
        c.addOk();
    }

    // ------------------------------------------------------------------
    // testing / debugging
    // ------------------------------------------------------------------
    private static final int HLL_TEST_CYCLES = 1000;

    /** PFSELFTEST */
    static void pfselftest(Client c) {
        Random rnd = new Random();
        byte[] bitcounters = new byte[HLL_DENSE_SIZE];
        int[] bytecounters = new int[HLL_REGISTERS];

        /* Test 1: registers are independently addressable. */
        for (int j = 0; j < HLL_TEST_CYCLES; j++) {
            for (int i = 0; i < HLL_REGISTERS; i++) {
                int r = rnd.nextInt() & HLL_REGISTER_MAX;
                bytecounters[i] = r;
                denseSetRegister(bitcounters, HLL_HDR_SIZE, i, r);
            }
            for (int i = 0; i < HLL_REGISTERS; i++) {
                int val = denseGet(bitcounters, HLL_HDR_SIZE, i);
                if (val != bytecounters[i]) {
                    c.addError("ERR TESTFAILED Register " + i + " should be "
                            + bytecounters[i] + " but is " + val);
                    return;
                }
            }
        }

        /* Test 2: the approximation error stays inside the expected bounds. */
        java.util.Arrays.fill(bitcounters, HLL_HDR_SIZE, HLL_DENSE_SIZE, (byte) 0);
        RObj o = createHLLObject();
        double relerr = 1.04 / Math.sqrt(HLL_REGISTERS);
        long checkpoint = 1;
        long seed = ((long) rnd.nextInt() & 0xffffffffL) | ((long) rnd.nextInt() << 32);
        byte[] ele = new byte[8];
        int sparseMax = sparseMaxBytes();

        for (long j = 1; j <= 10000000L; j++) {
            long e = j ^ seed;
            for (int i = 0; i < 8; i++) ele[i] = (byte) ((e >>> (8 * i)) & 0xff);

            int pat = hllPatLen(ele, 8);
            hllDenseSet(bitcounters, pat & 0xffff, pat >>> 16);
            hllAdd(o, ele);

            if (j == checkpoint) {
                if (j < sparseMax / 2 && (((byte[]) o.ptr)[4] & 0xff) != HLL_SPARSE) {
                    c.addError("ERR TESTFAILED sparse encoding not used");
                    return;
                }
                if (hllCount(bitcounters, null) != hllCount((byte[]) o.ptr, null)) {
                    c.addError("ERR TESTFAILED dense/sparse disagree");
                    return;
                }
                long abserr = checkpoint - hllCount(bitcounters, null);
                long maxerr = (long) Math.ceil(relerr * 6 * checkpoint);
                if (j == 10) maxerr = 1;
                if (abserr < 0) abserr = -abserr;
                if (abserr > maxerr) {
                    c.addError("ERR TESTFAILED Too big error. card:" + checkpoint
                            + " abserr:" + abserr);
                    return;
                }
                checkpoint *= 10;
            }
        }
        c.addOk();
    }

    /**
     * PFDEBUG GETREG|DECODE|ENCODING|TODENSE &lt;key&gt;
     * PFDEBUG SIMD (ON|OFF)
     */
    static void pfdebug(Client c) {
        Server s = Server.I;
        String cmd = c.argLower(1);

        if (cmd.equals("simd")) {
            if (!(c.argIs(2, "on") || c.argIs(2, "off")))
                c.addError("ERR Argument must be ON or OFF");
            else
                /* No AVX2 path here, so the vectorised merge is always off. */
                c.addSimple("disabled");
            return;
        }

        byte[] key = c.arg(2);
        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) throw new RedisError("ERR The specified key does not exist");
        checkHLLObject(o);
        byte[] b = unshare(o);

        if (cmd.equals("getreg")) {
            if ((b[4] & 0xff) == HLL_SPARSE) {
                if (!hllSparseToDense(o)) throw invalidHll();
                s.dirty++;   /* Force propagation on encoding change. */
                b = (byte[]) o.ptr;
            }
            c.addArray(HLL_REGISTERS);
            for (int j = 0; j < HLL_REGISTERS; j++) c.addInt(denseGet(b, HLL_HDR_SIZE, j));
        } else if (cmd.equals("decode")) {
            if ((b[4] & 0xff) != HLL_SPARSE)
                throw new RedisError("ERR HLL encoding is not sparse");
            StringBuilder sb = new StringBuilder();
            int p = HLL_HDR_SIZE, end = b.length;
            while (p < end) {
                if (sparseIsZero(b, p)) {
                    sb.append("z:").append(sparseZeroLen(b, p)).append(' ');
                    p++;
                } else if (sparseIsXzero(b, p)) {
                    sb.append("Z:").append(sparseXzeroLen(b, p)).append(' ');
                    p += 2;
                } else {
                    sb.append("v:").append(sparseValValue(b, p)).append(',')
                      .append(sparseValLen(b, p)).append(' ');
                    p++;
                }
            }
            String decoded = sb.toString();
            int st = 0, en = decoded.length();
            while (st < en && decoded.charAt(st) == ' ') st++;
            while (en > st && decoded.charAt(en - 1) == ' ') en--;
            c.addBulk(Util.b(decoded.substring(st, en)));
        } else if (cmd.equals("encoding")) {
            c.addSimple((b[4] & 0xff) == HLL_DENSE ? "dense" : "sparse");
        } else if (cmd.equals("todense")) {
            boolean conv = false;
            if ((b[4] & 0xff) == HLL_SPARSE) {
                if (!hllSparseToDense(o)) throw invalidHll();
                conv = true;
                s.dirty++;   /* Force propagation on encoding change. */
            }
            c.addInt(conv ? 1 : 0);
        } else {
            throw new RedisError("ERR Unknown PFDEBUG subcommand '" + c.argStr(1) + "'");
        }
    }
}
