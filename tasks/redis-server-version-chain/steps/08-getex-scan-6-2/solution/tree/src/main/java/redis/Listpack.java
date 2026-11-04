package redis;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The four packed on-disk container formats redis stores inside RDB strings:
 * listpack (src/listpack.c), ziplist (src/ziplist.c), intset (src/intset.c) and
 * zipmap (src/zipmap.c). Everything here is little endian, as redis writes it on
 * the platforms it supports.
 */
public final class Listpack {
    private Listpack() {}

    /* ---------------------------------------------------------------- */
    /* listpack                                                          */
    /* ---------------------------------------------------------------- */

    public static final int LP_HDR_SIZE = 6;
    private static final int LP_HDR_NUMELE_UNKNOWN = 65535;
    private static final int LP_EOF = 0xFF;

    /** Number of bytes lpEncodeBacklen() needs for a backlen of `l`. */
    static int backlenBytes(long l) {
        if (l <= 127) return 1;
        if (l < 16383) return 2;
        if (l < 2097151) return 3;
        if (l < 268435455) return 4;
        return 5;
    }

    /** Number of bytes the integer encoding of `v` occupies (lpEncodeIntegerGetType). */
    private static int intEncLen(long v) {
        if (v >= 0 && v <= 127) return 1;
        if (v >= -4096 && v <= 4095) return 2;
        if (v >= -32768 && v <= 32767) return 3;
        if (v >= -8388608 && v <= 8388607) return 4;
        if (v >= -2147483648L && v <= 2147483647L) return 5;
        return 9;
    }

    private static int writeIntEnc(byte[] b, int p, long v) {
        if (v >= 0 && v <= 127) {
            b[p] = (byte) v;
            return 1;
        } else if (v >= -4096 && v <= 4095) {
            long x = v < 0 ? (1L << 13) + v : v;
            b[p] = (byte) ((x >> 8) | 0xC0);
            b[p + 1] = (byte) (x & 0xff);
            return 2;
        } else if (v >= -32768 && v <= 32767) {
            long x = v < 0 ? (1L << 16) + v : v;
            b[p] = (byte) 0xF1;
            b[p + 1] = (byte) (x & 0xff);
            b[p + 2] = (byte) (x >> 8);
            return 3;
        } else if (v >= -8388608 && v <= 8388607) {
            long x = v < 0 ? (1L << 24) + v : v;
            b[p] = (byte) 0xF2;
            b[p + 1] = (byte) (x & 0xff);
            b[p + 2] = (byte) ((x >> 8) & 0xff);
            b[p + 3] = (byte) (x >> 16);
            return 4;
        } else if (v >= -2147483648L && v <= 2147483647L) {
            long x = v < 0 ? (1L << 32) + v : v;
            b[p] = (byte) 0xF3;
            b[p + 1] = (byte) (x & 0xff);
            b[p + 2] = (byte) ((x >> 8) & 0xff);
            b[p + 3] = (byte) ((x >> 16) & 0xff);
            b[p + 4] = (byte) (x >> 24);
            return 5;
        } else {
            b[p] = (byte) 0xF4;
            for (int i = 0; i < 8; i++) b[p + 1 + i] = (byte) (v >>> (8 * i));
            return 9;
        }
    }

    private static int strEncLen(int size) {
        if (size < 64) return 1 + size;
        if (size < 4096) return 2 + size;
        return 5 + size;
    }

    private static int writeStrEnc(byte[] b, int p, byte[] s) {
        int len = s.length;
        if (len < 64) {
            b[p] = (byte) (len | 0x80);
            System.arraycopy(s, 0, b, p + 1, len);
            return 1 + len;
        } else if (len < 4096) {
            b[p] = (byte) ((len >> 8) | 0xE0);
            b[p + 1] = (byte) (len & 0xff);
            System.arraycopy(s, 0, b, p + 2, len);
            return 2 + len;
        } else {
            b[p] = (byte) 0xF0;
            b[p + 1] = (byte) (len & 0xff);
            b[p + 2] = (byte) ((len >> 8) & 0xff);
            b[p + 3] = (byte) ((len >> 16) & 0xff);
            b[p + 4] = (byte) ((len >>> 24) & 0xff);
            System.arraycopy(s, 0, b, p + 5, len);
            return 5 + len;
        }
    }

    private static int writeBacklen(byte[] b, int p, long l) {
        if (l <= 127) {
            b[p] = (byte) l;
            return 1;
        } else if (l < 16383) {
            b[p] = (byte) (l >> 7);
            b[p + 1] = (byte) ((l & 127) | 128);
            return 2;
        } else if (l < 2097151) {
            b[p] = (byte) (l >> 14);
            b[p + 1] = (byte) (((l >> 7) & 127) | 128);
            b[p + 2] = (byte) ((l & 127) | 128);
            return 3;
        } else if (l < 268435455) {
            b[p] = (byte) (l >> 21);
            b[p + 1] = (byte) (((l >> 14) & 127) | 128);
            b[p + 2] = (byte) (((l >> 7) & 127) | 128);
            b[p + 3] = (byte) ((l & 127) | 128);
            return 4;
        } else {
            b[p] = (byte) (l >> 28);
            b[p + 1] = (byte) (((l >> 21) & 127) | 128);
            b[p + 2] = (byte) (((l >> 14) & 127) | 128);
            b[p + 3] = (byte) (((l >> 7) & 127) | 128);
            b[p + 4] = (byte) ((l & 127) | 128);
            return 5;
        }
    }

    /** Build a listpack holding `items` in order. */
    public static byte[] encode(List<byte[]> items) {
        long total = LP_HDR_SIZE + 1;
        int n = items.size();
        long[] ival = new long[n];
        boolean[] isInt = new boolean[n];
        for (int i = 0; i < n; i++) {
            byte[] e = items.get(i);
            Long v = Util.string2ll(e);
            long l;
            if (v != null) { isInt[i] = true; ival[i] = v; l = intEncLen(v); }
            else l = strEncLen(e.length);
            total += l + backlenBytes(l);
        }
        if (total > Integer.MAX_VALUE) throw new RedisError("ERR listpack too big");
        byte[] lp = new byte[(int) total];
        int p = LP_HDR_SIZE;
        for (int i = 0; i < n; i++) {
            int w = isInt[i] ? writeIntEnc(lp, p, ival[i]) : writeStrEnc(lp, p, items.get(i));
            p += w;
            p += writeBacklen(lp, p, w);
        }
        lp[p] = (byte) LP_EOF;
        setU32(lp, 0, total);
        int numele = Math.min(n, LP_HDR_NUMELE_UNKNOWN);
        lp[4] = (byte) (numele & 0xff);
        lp[5] = (byte) ((numele >> 8) & 0xff);
        return lp;
    }

    /** Total bytes as recorded in the listpack header. */
    public static long bytes(byte[] lp) { return getU32(lp, 0); }

    /** Number of entries, scanning when the header says "unknown". */
    public static int count(byte[] lp) {
        int n = (lp[4] & 0xff) | ((lp[5] & 0xff) << 8);
        if (n != LP_HDR_NUMELE_UNKNOWN) return n;
        return decode(lp).size();
    }

    /** Decode every entry of a listpack; integers come back as decimal strings. */
    public static List<byte[]> decode(byte[] lp) {
        List<byte[]> out = new ArrayList<>();
        if (lp.length < LP_HDR_SIZE + 1) throw corrupt();
        int p = LP_HDR_SIZE;
        while (true) {
            if (p >= lp.length) throw corrupt();
            int b0 = lp[p] & 0xff;
            if (b0 == LP_EOF) break;
            int enclen = entryEncodedSize(lp, p);
            if (enclen <= 0 || p + enclen > lp.length) throw corrupt();
            out.add(entryValue(lp, p));
            p += enclen + backlenBytes(enclen);
        }
        return out;
    }

    /** lpCurrentEncodedSizeUnsafe(): encoding byte + length bytes + payload. */
    private static int entryEncodedSize(byte[] lp, int p) {
        int b0 = lp[p] & 0xff;
        if ((b0 & 0x80) == 0) return 1;                                 // 7 bit uint
        if ((b0 & 0xC0) == 0x80) return 1 + (b0 & 0x3F);                // 6 bit string
        if ((b0 & 0xE0) == 0xC0) return 2;                              // 13 bit int
        if (b0 == 0xF1) return 3;
        if (b0 == 0xF2) return 4;
        if (b0 == 0xF3) return 5;
        if (b0 == 0xF4) return 9;
        if ((b0 & 0xF0) == 0xE0) {                                      // 12 bit string
            if (p + 1 >= lp.length) throw corrupt();
            return 2 + (((b0 & 0xF) << 8) | (lp[p + 1] & 0xff));
        }
        if (b0 == 0xF0) {                                               // 32 bit string
            if (p + 4 >= lp.length) throw corrupt();
            long l = (lp[p + 1] & 0xffL) | ((lp[p + 2] & 0xffL) << 8)
                   | ((lp[p + 3] & 0xffL) << 16) | ((lp[p + 4] & 0xffL) << 24);
            if (l > Integer.MAX_VALUE - 5L) throw corrupt();
            return (int) (5 + l);
        }
        if (b0 == LP_EOF) return 1;
        throw corrupt();
    }

    /** The value of the entry at p, as bytes (integers rendered in decimal). */
    private static byte[] entryValue(byte[] lp, int p) {
        int b0 = lp[p] & 0xff;
        long uval, negstart, negmax;
        if ((b0 & 0x80) == 0) {
            return Util.ll2string(b0 & 0x7f);
        } else if ((b0 & 0xC0) == 0x80) {
            int len = b0 & 0x3F;
            return Arrays.copyOfRange(lp, p + 1, p + 1 + len);
        } else if ((b0 & 0xE0) == 0xC0) {
            uval = ((b0 & 0x1f) << 8) | (lp[p + 1] & 0xff);
            negstart = 1L << 12;
            negmax = 8191;
        } else if (b0 == 0xF1) {
            uval = (lp[p + 1] & 0xffL) | ((lp[p + 2] & 0xffL) << 8);
            negstart = 1L << 15;
            negmax = 65535;
        } else if (b0 == 0xF2) {
            uval = (lp[p + 1] & 0xffL) | ((lp[p + 2] & 0xffL) << 8) | ((lp[p + 3] & 0xffL) << 16);
            negstart = 1L << 23;
            negmax = 16777215;
        } else if (b0 == 0xF3) {
            uval = (lp[p + 1] & 0xffL) | ((lp[p + 2] & 0xffL) << 8)
                 | ((lp[p + 3] & 0xffL) << 16) | ((lp[p + 4] & 0xffL) << 24);
            negstart = 1L << 31;
            negmax = 4294967295L;
        } else if (b0 == 0xF4) {
            long v = 0;
            for (int i = 0; i < 8; i++) v |= (lp[p + 1 + i] & 0xffL) << (8 * i);
            return Util.ll2string(v);   // full 64 bit range: two's complement already
        } else if ((b0 & 0xF0) == 0xE0) {
            int len = ((b0 & 0xF) << 8) | (lp[p + 1] & 0xff);
            return Arrays.copyOfRange(lp, p + 2, p + 2 + len);
        } else if (b0 == 0xF0) {
            long l = (lp[p + 1] & 0xffL) | ((lp[p + 2] & 0xffL) << 8)
                   | ((lp[p + 3] & 0xffL) << 16) | ((lp[p + 4] & 0xffL) << 24);
            return Arrays.copyOfRange(lp, p + 5, (int) (p + 5 + l));
        } else {
            throw corrupt();
        }
        long val;
        if (Long.compareUnsigned(uval, negstart) >= 0) {
            uval = negmax - uval;
            val = -uval - 1;
        } else {
            val = uval;
        }
        return Util.ll2string(val);
    }

    /* ---------------------------------------------------------------- */
    /* ziplist                                                           */
    /* ---------------------------------------------------------------- */

    private static final int ZIP_END = 255;
    private static final int ZIP_BIG_PREVLEN = 254;
    private static final int ZIP_STR_MASK = 0xc0;
    private static final int ZIP_STR_06B = 0;
    private static final int ZIP_STR_14B = 1 << 6;
    private static final int ZIP_STR_32B = 2 << 6;
    private static final int ZIP_INT_16B = 0xc0;
    private static final int ZIP_INT_32B = 0xc0 | (1 << 4);
    private static final int ZIP_INT_64B = 0xc0 | (2 << 4);
    private static final int ZIP_INT_24B = 0xc0 | (3 << 4);
    private static final int ZIP_INT_8B = 0xfe;
    private static final int ZIP_INT_IMM_MIN = 0xf1;
    private static final int ZIP_INT_IMM_MAX = 0xfd;

    /** Decode every entry of a ziplist; integers come back as decimal strings. */
    public static List<byte[]> decodeZiplist(byte[] zl) {
        List<byte[]> out = new ArrayList<>();
        if (zl.length < 11) throw corrupt();
        int p = 10;   // zlbytes(4) + zltail(4) + zllen(2)
        while (true) {
            if (p >= zl.length) throw corrupt();
            int b = zl[p] & 0xff;
            if (b == ZIP_END) break;
            int prevlensize = (b < ZIP_BIG_PREVLEN) ? 1 : 5;
            int q = p + prevlensize;
            if (q >= zl.length) throw corrupt();
            int enc = zl[q] & 0xff;
            if (enc < ZIP_STR_MASK) enc &= ZIP_STR_MASK;
            int lensize, len;
            if (enc < ZIP_STR_MASK) {
                if (enc == ZIP_STR_06B) {
                    lensize = 1;
                    len = zl[q] & 0x3f;
                } else if (enc == ZIP_STR_14B) {
                    lensize = 2;
                    len = ((zl[q] & 0x3f) << 8) | (zl[q + 1] & 0xff);
                } else if (enc == ZIP_STR_32B) {
                    lensize = 5;
                    len = ((zl[q + 1] & 0xff) << 24) | ((zl[q + 2] & 0xff) << 16)
                        | ((zl[q + 3] & 0xff) << 8) | (zl[q + 4] & 0xff);
                } else {
                    throw corrupt();
                }
                if (len < 0 || q + lensize + len > zl.length) throw corrupt();
                out.add(Arrays.copyOfRange(zl, q + lensize, q + lensize + len));
                p = q + lensize + len;
            } else {
                lensize = 1;
                if (enc == ZIP_INT_8B) len = 1;
                else if (enc == ZIP_INT_16B) len = 2;
                else if (enc == ZIP_INT_24B) len = 3;
                else if (enc == ZIP_INT_32B) len = 4;
                else if (enc == ZIP_INT_64B) len = 8;
                else if (enc >= ZIP_INT_IMM_MIN && enc <= ZIP_INT_IMM_MAX) len = 0;
                else throw corrupt();
                if (q + lensize + len > zl.length) throw corrupt();
                out.add(Util.ll2string(zipLoadInteger(zl, q + lensize, enc)));
                p = q + lensize + len;
            }
        }
        return out;
    }

    private static long zipLoadInteger(byte[] b, int p, int enc) {
        switch (enc) {
            case ZIP_INT_8B: return b[p];
            case ZIP_INT_16B: return (short) ((b[p] & 0xff) | ((b[p + 1] & 0xff) << 8));
            case ZIP_INT_24B: {
                int v = ((b[p] & 0xff) << 8) | ((b[p + 1] & 0xff) << 16) | ((b[p + 2] & 0xff) << 24);
                return v >> 8;
            }
            case ZIP_INT_32B:
                return (int) ((b[p] & 0xff) | ((b[p + 1] & 0xff) << 8)
                            | ((b[p + 2] & 0xff) << 16) | ((b[p + 3] & 0xff) << 24));
            case ZIP_INT_64B: {
                long v = 0;
                for (int i = 0; i < 8; i++) v |= (b[p + i] & 0xffL) << (8 * i);
                return v;
            }
            default:
                if (enc >= ZIP_INT_IMM_MIN && enc <= ZIP_INT_IMM_MAX) return (enc & 0x0f) - 1;
                throw corrupt();
        }
    }

    /** Build a ziplist (only needed to write pre-7.0 format files). */
    public static byte[] encodeZiplist(List<byte[]> items) {
        Buf b = new Buf(64);
        b.put(new byte[10]);   // header, patched below
        int prevlen = 0;
        int tailOffset = 10;
        for (byte[] e : items) {
            tailOffset = b.len;
            if (prevlen < ZIP_BIG_PREVLEN) {
                b.put((byte) prevlen);
            } else {
                b.put((byte) ZIP_BIG_PREVLEN);
                b.put((byte) (prevlen & 0xff));
                b.put((byte) ((prevlen >> 8) & 0xff));
                b.put((byte) ((prevlen >> 16) & 0xff));
                b.put((byte) ((prevlen >>> 24) & 0xff));
            }
            Long v = Util.string2ll(e);
            if (v != null) {
                long x = v;
                if (x >= 0 && x <= 12) {
                    b.put((byte) (0xf1 + x));
                } else if (x >= Byte.MIN_VALUE && x <= Byte.MAX_VALUE) {
                    b.put((byte) ZIP_INT_8B);
                    b.put((byte) x);
                } else if (x >= Short.MIN_VALUE && x <= Short.MAX_VALUE) {
                    b.put((byte) ZIP_INT_16B);
                    b.put((byte) (x & 0xff));
                    b.put((byte) ((x >> 8) & 0xff));
                } else if (x >= -8388608L && x <= 8388607L) {
                    b.put((byte) ZIP_INT_24B);
                    b.put((byte) (x & 0xff));
                    b.put((byte) ((x >> 8) & 0xff));
                    b.put((byte) ((x >> 16) & 0xff));
                } else if (x >= Integer.MIN_VALUE && x <= Integer.MAX_VALUE) {
                    b.put((byte) ZIP_INT_32B);
                    for (int i = 0; i < 4; i++) b.put((byte) (x >> (8 * i)));
                } else {
                    b.put((byte) ZIP_INT_64B);
                    for (int i = 0; i < 8; i++) b.put((byte) (x >> (8 * i)));
                }
            } else {
                int len = e.length;
                if (len <= 0x3f) {
                    b.put((byte) (ZIP_STR_06B | len));
                } else if (len <= 0x3fff) {
                    b.put((byte) (ZIP_STR_14B | ((len >> 8) & 0x3f)));
                    b.put((byte) (len & 0xff));
                } else {
                    b.put((byte) ZIP_STR_32B);
                    b.put((byte) ((len >>> 24) & 0xff));
                    b.put((byte) ((len >> 16) & 0xff));
                    b.put((byte) ((len >> 8) & 0xff));
                    b.put((byte) (len & 0xff));
                }
                b.put(e);
            }
            prevlen = b.len - tailOffset;
        }
        b.put((byte) ZIP_END);
        byte[] zl = b.toBytes();
        setU32(zl, 0, zl.length);
        setU32(zl, 4, items.isEmpty() ? 10 : tailOffset);
        int n = Math.min(items.size(), 65535);
        zl[8] = (byte) (n & 0xff);
        zl[9] = (byte) ((n >> 8) & 0xff);
        return zl;
    }

    /* ---------------------------------------------------------------- */
    /* intset                                                            */
    /* ---------------------------------------------------------------- */

    /** Decode an intset into its members, in the stored (ascending) order. */
    public static List<byte[]> decodeIntset(byte[] is) {
        if (is.length < 8) throw corrupt();
        long enc = getU32(is, 0);
        long len = getU32(is, 4);
        if (enc != 2 && enc != 4 && enc != 8) throw corrupt();
        if (len < 0 || 8 + len * enc != is.length) throw corrupt();
        List<byte[]> out = new ArrayList<>((int) len);
        for (int i = 0; i < len; i++) {
            int p = (int) (8 + i * enc);
            long v;
            if (enc == 2) v = (short) ((is[p] & 0xff) | ((is[p + 1] & 0xff) << 8));
            else if (enc == 4) v = (int) ((is[p] & 0xff) | ((is[p + 1] & 0xff) << 8)
                    | ((is[p + 2] & 0xff) << 16) | ((is[p + 3] & 0xff) << 24));
            else {
                v = 0;
                for (int k = 0; k < 8; k++) v |= (is[p + k] & 0xffL) << (8 * k);
            }
            out.add(Util.ll2string(v));
        }
        return out;
    }

    /** Build an intset from the given values (they are sorted here). */
    public static byte[] encodeIntset(long[] vals) {
        long[] v = vals.clone();
        Arrays.sort(v);
        int enc = 2;
        for (long x : v) {
            if (x < Integer.MIN_VALUE || x > Integer.MAX_VALUE) { enc = 8; break; }
            else if (x < Short.MIN_VALUE || x > Short.MAX_VALUE) enc = Math.max(enc, 4);
        }
        byte[] out = new byte[8 + v.length * enc];
        setU32(out, 0, enc);
        setU32(out, 4, v.length);
        for (int i = 0; i < v.length; i++) {
            int p = 8 + i * enc;
            for (int k = 0; k < enc; k++) out[p + k] = (byte) (v[i] >> (8 * k));
        }
        return out;
    }

    /* ---------------------------------------------------------------- */
    /* zipmap (redis <= 2.4 hashes)                                      */
    /* ---------------------------------------------------------------- */

    private static final int ZIPMAP_BIGLEN = 254;
    private static final int ZIPMAP_END = 255;

    /** Decode a zipmap into a flat [field, value, field, value, ...] list. */
    public static List<byte[]> decodeZipmap(byte[] zm) {
        List<byte[]> out = new ArrayList<>();
        if (zm.length < 2) throw corrupt();
        int p = 1;   // skip <zmlen>
        while (true) {
            if (p >= zm.length) throw corrupt();
            if ((zm[p] & 0xff) == ZIPMAP_END) break;
            int klen = zipmapDecodeLength(zm, p);
            p += zipmapLenBytes(zm, p);
            if (klen < 0 || p + klen > zm.length) throw corrupt();
            byte[] field = Arrays.copyOfRange(zm, p, p + klen);
            p += klen;
            if (p >= zm.length) throw corrupt();
            int vlen = zipmapDecodeLength(zm, p);
            p += zipmapLenBytes(zm, p);
            if (p >= zm.length) throw corrupt();
            int free = zm[p] & 0xff;
            p += 1;
            if (vlen < 0 || p + vlen + free > zm.length) throw corrupt();
            byte[] value = Arrays.copyOfRange(zm, p, p + vlen);
            p += vlen + free;
            out.add(field);
            out.add(value);
        }
        return out;
    }

    private static int zipmapLenBytes(byte[] zm, int p) {
        return (zm[p] & 0xff) < ZIPMAP_BIGLEN ? 1 : 5;
    }

    private static int zipmapDecodeLength(byte[] zm, int p) {
        int l = zm[p] & 0xff;
        if (l < ZIPMAP_BIGLEN) return l;
        if (p + 5 > zm.length) throw corrupt();
        return (int) getU32(zm, p + 1);
    }

    /** Build a zipmap from a flat [field, value, ...] list. */
    public static byte[] encodeZipmap(List<byte[]> fv) {
        Buf b = new Buf(64);
        int pairs = fv.size() / 2;
        b.put((byte) (pairs < ZIPMAP_BIGLEN ? pairs : ZIPMAP_BIGLEN));
        for (int i = 0; i + 1 < fv.size(); i += 2) {
            zipmapPutLen(b, fv.get(i).length);
            b.put(fv.get(i));
            zipmapPutLen(b, fv.get(i + 1).length);
            b.put((byte) 0);    // <free>
            b.put(fv.get(i + 1));
        }
        b.put((byte) ZIPMAP_END);
        return b.toBytes();
    }

    private static void zipmapPutLen(Buf b, int l) {
        if (l < ZIPMAP_BIGLEN) {
            b.put((byte) l);
        } else {
            b.put((byte) ZIPMAP_BIGLEN);
            for (int i = 0; i < 4; i++) b.put((byte) (l >> (8 * i)));
        }
    }

    /* ---------------------------------------------------------------- */

    static long getU32(byte[] b, int p) {
        return (b[p] & 0xffL) | ((b[p + 1] & 0xffL) << 8)
             | ((b[p + 2] & 0xffL) << 16) | ((b[p + 3] & 0xffL) << 24);
    }

    static void setU32(byte[] b, int p, long v) {
        b[p] = (byte) (v & 0xff);
        b[p + 1] = (byte) ((v >> 8) & 0xff);
        b[p + 2] = (byte) ((v >> 16) & 0xff);
        b[p + 3] = (byte) ((v >> 24) & 0xff);
    }

    static RedisError corrupt() { return new RedisError("ERR Bad data format"); }
}
