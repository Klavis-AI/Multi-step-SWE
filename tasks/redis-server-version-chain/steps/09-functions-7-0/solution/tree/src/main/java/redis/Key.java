package redis;

import java.util.Arrays;

/** Binary-safe map key wrapping a byte[] with a cached hash. */
public final class Key implements Comparable<Key> {
    public final byte[] b;
    private final int hash;

    public Key(byte[] b) { this.b = b; this.hash = Arrays.hashCode(b); }

    public static Key of(byte[] b) { return new Key(b); }
    public static Key of(String s) { return new Key(Util.b(s)); }

    @Override public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Key)) return false;
        return Arrays.equals(b, ((Key) o).b);
    }
    @Override public int hashCode() { return hash; }
    @Override public String toString() { return Util.s(b); }
    public int length() { return b.length; }

    @Override public int compareTo(Key o) {
        int n = Math.min(b.length, o.b.length);
        for (int i = 0; i < n; i++) {
            int c = (b[i] & 0xff) - (o.b[i] & 0xff);
            if (c != 0) return c;
        }
        return b.length - o.b.length;
    }
}
