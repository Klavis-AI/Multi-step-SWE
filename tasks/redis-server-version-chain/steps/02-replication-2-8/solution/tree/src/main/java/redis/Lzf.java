package redis;

import java.util.Arrays;

/**
 * LibLZF 3.6 as vendored by redis (src/lzf_c.c, src/lzf_d.c, src/lzfP.h).
 *
 * Configuration taken from lzfP.h: HLOG = 16, VERY_FAST = 1, ULTRA_FAST = 0,
 * INIT_HTAB = 0, LZF_USE_OFFSETS = 0.
 *
 * Compressed format:
 *   000LLLLL &lt;L+1&gt;              literal run, L+1 = 1..32 octets
 *   LLLooooo oooooooo           backref, L+2 octets, o+1 = 1..8192 offset
 *   111ooooo LLLLLLLL oooooooo  backref, L+7+2 octets, o+1 = 1..8192 offset
 */
public final class Lzf {
    private Lzf() {}

    private static final int HLOG = 16;
    private static final int HSIZE = 1 << HLOG;
    private static final int MAX_LIT = 1 << 5;
    private static final int MAX_OFF = 1 << 13;
    private static final int MAX_REF = (1 << 8) + (1 << 3);

    /* IDX(h) for VERY_FAST && !ULTRA_FAST: (((h >> (24-HLOG)) - h*5) & (HSIZE-1)) */
    private static int idx(int hval) {
        return (((hval >>> (24 - HLOG)) - hval * 5)) & (HSIZE - 1);
    }

    /**
     * Compress `in` the way rdbSaveLzfStringObject() does: the output buffer is
     * in.length-4 bytes, so anything that does not shrink by at least four bytes
     * is rejected. Returns null when the data does not compress.
     */
    public static byte[] compress(byte[] in) {
        if (in.length <= 4) return null;
        return compress(in, in.length - 4);
    }

    /** Compress into a buffer of `outLen` bytes; null when it does not fit. */
    public static byte[] compress(byte[] in, int outLen) {
        int inLen = in.length;
        if (inLen == 0 || outLen <= 0) return null;

        /* The C code mallocs outLen+1 and may write one byte past out_end in the
         * trailing literal run; keep a little slack and validate at the end. */
        byte[] out = new byte[outLen + 8];
        int[] htab = new int[HSIZE];   // stores (index+1); 0 means "empty"

        int ip = 0;                    // input cursor
        int op = 0;                    // output cursor
        int inEnd = inLen;
        int outEnd = outLen;

        int lit = 0;
        op++;                          // start run

        int hval = ((in[ip] & 0xff) << 8) | (in[ip + 1] & 0xff);   // FRST(ip)

        while (ip < inEnd - 2) {
            hval = (hval << 8) | (in[ip + 2] & 0xff);              // NEXT(hval, ip)
            int slot = idx(hval);
            int refPlus1 = htab[slot];
            int ref = refPlus1 - 1;                                // -1 when empty
            htab[slot] = ip + 1;

            long off;
            if (refPlus1 != 0
                    && (off = (long) ip - ref - 1) < MAX_OFF
                    && off >= 0
                    && ref > 0
                    && in[ref + 2] == in[ip + 2]
                    && in[ref] == in[ip]
                    && in[ref + 1] == in[ip + 1]) {
                /* match found at ref */
                int len = 2;
                int maxlen = inEnd - ip - len;
                if (maxlen > MAX_REF) maxlen = MAX_REF;

                if (op + 3 + 1 >= outEnd) {
                    if (op - (lit != 0 ? 0 : 1) + 3 + 1 >= outEnd) return null;
                }

                out[op - lit - 1] = (byte) (lit - 1);   // stop run
                if (lit == 0) op--;                     // undo run if length is zero

                matchLoop:
                for (;;) {
                    if (maxlen > 16) {
                        for (int k = 0; k < 16; k++) {
                            len++;
                            if (in[ref + len] != in[ip + len]) break matchLoop;
                        }
                    }
                    do {
                        len++;
                    } while (len < maxlen && in[ref + len] == in[ip + len]);
                    break;
                }

                len -= 2;   // len is now #octets - 1
                ip++;

                if (len < 7) {
                    out[op++] = (byte) ((off >> 8) + (len << 5));
                } else {
                    out[op++] = (byte) ((off >> 8) + (7 << 5));
                    out[op++] = (byte) (len - 7);
                }
                out[op++] = (byte) off;

                lit = 0;
                op++;                                    // start run

                ip += len + 1;
                if (ip >= inEnd - 2) break;

                /* VERY_FAST && !ULTRA_FAST: back up two positions and re-hash. */
                ip -= 2;
                hval = ((in[ip] & 0xff) << 8) | (in[ip + 1] & 0xff);
                hval = (hval << 8) | (in[ip + 2] & 0xff);
                htab[idx(hval)] = ip + 1;
                ip++;
                hval = (hval << 8) | (in[ip + 2] & 0xff);
                htab[idx(hval)] = ip + 1;
                ip++;
            } else {
                /* one more literal byte we must copy */
                if (op >= outEnd) return null;
                lit++;
                out[op++] = in[ip++];
                if (lit == MAX_LIT) {
                    out[op - lit - 1] = (byte) (lit - 1);   // stop run
                    lit = 0;
                    op++;                                    // start run
                }
            }
        }

        if (op + 3 > outEnd) return null;   // at most 3 bytes can be missing here

        while (ip < inEnd) {
            lit++;
            out[op++] = in[ip++];
            if (lit == MAX_LIT) {
                out[op - lit - 1] = (byte) (lit - 1);
                lit = 0;
                op++;
            }
        }

        out[op - lit - 1] = (byte) (lit - 1);   // end run
        if (lit == 0) op--;                     // undo run if length is zero

        if (op <= 0 || op > outLen + 1) return null;
        return Arrays.copyOf(out, op);
    }

    /**
     * Decompress `in` into exactly `outLen` bytes. Returns null when the input is
     * corrupt (the caller reports "Invalid LZF compressed string").
     */
    public static byte[] decompress(byte[] in, int outLen) {
        return decompress(in, 0, in.length, outLen);
    }

    public static byte[] decompress(byte[] in, int inOff, int inLen, int outLen) {
        byte[] out = new byte[outLen];
        int ip = inOff;
        int inEnd = inOff + inLen;
        int op = 0;

        while (ip < inEnd) {
            int ctrl = in[ip++] & 0xff;

            if (ctrl < (1 << 5)) {
                /* literal run */
                ctrl++;
                if (op + ctrl > outLen) return null;
                if (ip + ctrl > inEnd) return null;
                System.arraycopy(in, ip, out, op, ctrl);
                ip += ctrl;
                op += ctrl;
            } else {
                /* back reference */
                int len = ctrl >> 5;
                int ref = op - ((ctrl & 0x1f) << 8) - 1;

                if (ip >= inEnd) return null;
                if (len == 7) {
                    len += in[ip++] & 0xff;
                    if (ip >= inEnd) return null;
                }
                ref -= in[ip++] & 0xff;

                if (op + len + 2 > outLen) return null;
                if (ref < 0) return null;

                len += 2;
                /* Overlapping copies must be byte by byte. */
                for (int k = 0; k < len; k++) out[op++] = out[ref++];
            }
        }
        if (op != outLen) return null;
        return out;
    }
}
