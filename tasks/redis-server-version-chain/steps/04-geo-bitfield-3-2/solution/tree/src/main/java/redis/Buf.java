package redis;

import java.util.Arrays;

/** Growable byte buffer used for client output and generic serialization. */
public final class Buf {
    public byte[] a;
    public int len;

    public Buf() { this(256); }
    public Buf(int cap) { a = new byte[Math.max(16, cap)]; }

    public void ensure(int extra) {
        if (len + extra <= a.length) return;
        int cap = a.length;
        while (cap < len + extra) cap = cap < 1024 * 1024 ? cap * 2 : cap + 1024 * 1024;
        a = Arrays.copyOf(a, cap);
    }
    public Buf put(byte b) { ensure(1); a[len++] = b; return this; }
    public Buf put(byte[] b) { ensure(b.length); System.arraycopy(b, 0, a, len, b.length); len += b.length; return this; }
    public Buf put(byte[] b, int off, int n) { ensure(n); System.arraycopy(b, off, a, len, n); len += n; return this; }
    public Buf put(String s) { return put(Util.b(s)); }
    public Buf putLong(long v) { return put(Long.toString(v)); }
    public Buf crlf() { ensure(2); a[len++] = '\r'; a[len++] = '\n'; return this; }
    public void clear() { len = 0; }
    public byte[] toBytes() { return Arrays.copyOf(a, len); }
    public boolean isEmpty() { return len == 0; }
    public void consume(int n) {
        if (n >= len) { len = 0; return; }
        System.arraycopy(a, n, a, 0, len - n);
        len -= n;
    }
}
