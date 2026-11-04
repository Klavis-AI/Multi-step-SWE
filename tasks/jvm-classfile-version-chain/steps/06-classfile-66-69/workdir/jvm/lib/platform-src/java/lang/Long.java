package java.lang;
public final class Long extends Number implements Comparable<Long> {
    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    public static final int SIZE = 64;
    public static final int BYTES = 8;
    public static final Class<Long> TYPE = (Class<Long>) Class.getPrimitiveClass("long");
    private final long value;
    public Long(long value) { this.value = value; }
    public Long(String s) { this.value = parseLong(s); }
    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return (int) (value ^ (value >>> 32)); }
    public boolean equals(Object o) { return (o instanceof Long) && ((Long) o).value == value; }
    public int compareTo(Long other) { return compare(value, other.value); }
    public static int compare(long x, long y) { return x < y ? -1 : (x == y ? 0 : 1); }
    public static Long valueOf(long l) { return (l >= -128 && l <= 127) ? LongCache.cache[(int) l + 128] : new Long(l); }
    public static Long valueOf(String s) { return valueOf(parseLong(s)); }
    public static long parseLong(String s) { return parseLong(s, 10); }
    public static long parseLong(String s, int radix) {
        if (s == null) throw new NumberFormatException("Cannot parse null string");
        int len = s.length();
        if (len == 0) throw new NumberFormatException("For input string: " + quote(s));
        boolean neg = false;
        int i = 0;
        char first = s.charAt(0);
        if (first == '-') { neg = true; i = 1; } else if (first == '+') { i = 1; }
        if (i == len) throw new NumberFormatException("For input string: " + quote(s));
        long r = 0;
        long limit = neg ? MIN_VALUE : -MAX_VALUE;
        long mult = limit / radix;
        for (; i < len; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0 || r < mult) throw new NumberFormatException("For input string: " + quote(s));
            r *= radix;
            if (r < limit + d) throw new NumberFormatException("For input string: " + quote(s));
            r -= d;
        }
        return neg ? r : -r;
    }
    static String quote(String s) { return (char) 34 + s + (char) 34; }
    public static String toString(long i) { return toString(i, 10); }
    public static String toString(long i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (i == 0) return "0";
        boolean neg = i < 0;
        char[] b = new char[65];
        int p = 65;
        if (neg) {
            while (i != 0) { b[--p] = Character.forDigit((int) -(i % radix), radix); i /= radix; }
            b[--p] = '-';
        } else {
            while (i != 0) { b[--p] = Character.forDigit((int) (i % radix), radix); i /= radix; }
        }
        return new String(b, p, 65 - p);
    }
    public static String toHexString(long i) { return toUnsignedString0(i, 4); }
    public static String toOctalString(long i) { return toUnsignedString0(i, 3); }
    public static String toBinaryString(long i) { return toUnsignedString0(i, 1); }
    private static String toUnsignedString0(long val, int shift) {
        if (val == 0) return "0";
        int mask = (1 << shift) - 1;
        char[] b = new char[64];
        int p = 64;
        while (val != 0) { b[--p] = Character.forDigit((int) (val & mask), 1 << shift); val >>>= shift; }
        return new String(b, p, 64 - p);
    }
    public static int bitCount(long i) {
        int c = 0;
        for (int k = 0; k < 64; k++) if (((i >>> k) & 1) != 0) c++;
        return c;
    }
    public static int numberOfLeadingZeros(long i) {
        if (i == 0) return 64;
        int n = 0;
        while ((i & 0x8000000000000000L) == 0) { n++; i <<= 1; }
        return n;
    }
    public static int numberOfTrailingZeros(long i) {
        if (i == 0) return 64;
        int n = 0;
        while ((i & 1) == 0) { n++; i >>>= 1; }
        return n;
    }
    public static long highestOneBit(long i) { return i == 0 ? 0 : 1L << (63 - numberOfLeadingZeros(i)); }
    public static long rotateLeft(long i, int d) { return (i << d) | (i >>> -d); }
    public static long sum(long a, long b) { return a + b; }
    public static long max(long a, long b) { return Math.max(a, b); }
    public static long min(long a, long b) { return Math.min(a, b); }
    public static int signum(long i) { return (int) ((i >> 63) | (-i >>> 63)); }
    private static final class LongCache {
        static final Long[] cache = new Long[256];
        static { for (int i = 0; i < 256; i++) cache[i] = new Long(i - 128); }
    }
}
