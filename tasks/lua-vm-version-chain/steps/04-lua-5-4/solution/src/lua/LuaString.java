package lua;

import java.util.Arrays;

/** An 8-bit clean immutable Lua string. */
public final class LuaString {
    public final byte[] b;
    private int hash;

    private LuaString(byte[] b) { this.b = b; Gc.alloc(24 + b.length); }

    /** LUAI_MAXSHORTLEN: at or below this length a string is interned. */
    private static final int MAXSHORTLEN = 40;
    private static final java.util.WeakHashMap<LuaString, java.lang.ref.WeakReference<LuaString>>
            SHORT = new java.util.WeakHashMap<>();

    private static synchronized LuaString intern(LuaString s) {
        if (s.b.length > MAXSHORTLEN) return s;
        java.lang.ref.WeakReference<LuaString> r = SHORT.get(s);
        LuaString old = (r == null) ? null : r.get();
        if (old != null) return old;
        SHORT.put(s, new java.lang.ref.WeakReference<>(s));
        return s;
    }

    public static LuaString of(byte[] b) { return intern(new LuaString(b)); }

    public static LuaString of(byte[] b, int off, int len) {
        return intern(new LuaString(Arrays.copyOfRange(b, off, off + len)));
    }

    /** Latin-1 view: every char of s must be < 256. Used for interpreter-generated text. */
    public static LuaString of(String s) {
        int n = s.length();
        byte[] r = new byte[n];
        for (int i = 0; i < n; i++) r[i] = (byte) s.charAt(i);
        return intern(new LuaString(r));
    }

    public int length() { return b.length; }

    public int byteAt(int i) { return b[i] & 0xff; }

    /** [from,to) */
    public LuaString sub(int from, int to) { return new LuaString(Arrays.copyOfRange(b, from, to)); }

    public LuaString concat(LuaString o) {
        byte[] r = new byte[b.length + o.b.length];
        System.arraycopy(b, 0, r, 0, b.length);
        System.arraycopy(o.b, 0, r, b.length, o.b.length);
        return intern(new LuaString(r));
    }

    @Override public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof LuaString)) return false;
        return Arrays.equals(b, ((LuaString) o).b);
    }

    @Override public int hashCode() {
        int h = hash;
        if (h == 0 && b.length > 0) {
            h = b.length;
            for (byte x : b) h = h * 31 + x;
            if (h == 0) h = 1;
            hash = h;
        }
        return h;
    }

    /** Latin-1 decoding: byte i becomes char i. Never lossy. */
    @Override public String toString() {
        char[] c = new char[b.length];
        for (int i = 0; i < b.length; i++) c[i] = (char) (b[i] & 0xff);
        return new String(c);
    }

    public int compareTo(LuaString o) {
        int n = Math.min(b.length, o.b.length);
        for (int i = 0; i < n; i++) {
            int d = (b[i] & 0xff) - (o.b[i] & 0xff);
            if (d != 0) return d;
        }
        return b.length - o.b.length;
    }
}
