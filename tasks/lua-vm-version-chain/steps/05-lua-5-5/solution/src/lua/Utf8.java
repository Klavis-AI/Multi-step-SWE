package lua;

/** UTF-8 encoding as the utf8 library and the \\u escape define it (up to 6 bytes). */
final class Utf8 {
    static byte[] encode(long x) {
        if (x < 0x80) return new byte[]{(byte) x};
        int n = 1;
        long mfb = 0x3f;
        byte[] buff = new byte[8];
        do {
            buff[8 - (n++)] = (byte) (0x80 | (x & 0x3f));
            x >>= 6;
            mfb >>= 1;
        } while (x > mfb);
        buff[8 - n] = (byte) ((~mfb << 1) | x);
        byte[] r = new byte[n];
        System.arraycopy(buff, 8 - n, r, 0, n);
        return r;
    }

    /** Decodes one sequence at i; returns {codepoint, nextIndex} or null when invalid. */
    static long[] decode(byte[] s, int i, boolean strict) {
        if (i < 0 || i >= s.length) return null;
        int c = s[i] & 0xff;
        if (c < 0x80) return new long[]{c, i + 1};
        if (c < 0xc0) return null;
        int count = 0;
        long res = 0;
        while ((c & 0x40) != 0) {
            count++;
            if (i + count >= s.length) return null;
            int cc = s[i + count] & 0xff;
            if ((cc & 0xc0) != 0x80) return null;
            res = (res << 6) | (cc & 0x3f);
            c <<= 1;
        }
        res |= ((long) (c & 0x7f) << (count * 5));
        // limits[]: a sequence longer than the value needs is not valid UTF-8.
        final long[] limits = {0, 0x80, 0x800, 0x10000L, 0x200000L, 0x4000000L};
        if (count > 5 || res > 0x7FFFFFFFL || res < limits[count]) return null;
        if (strict && (res > 0x10FFFFL || (res >= 0xD800 && res <= 0xDFFF))) return null;
        return new long[]{res, i + count + 1};
    }
}
