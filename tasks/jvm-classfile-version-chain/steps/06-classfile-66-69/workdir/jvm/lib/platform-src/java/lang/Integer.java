package java.lang;
public final class Integer extends Number implements Comparable<Integer> {
    public static final int MIN_VALUE = 0x80000000;
    public static final int MAX_VALUE = 0x7fffffff;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    public static final Class<Integer> TYPE = (Class<Integer>) Class.getPrimitiveClass("int");

    private final int value;
    public Integer(int value) { this.value = value; }
    public Integer(String s) { this.value = parseInt(s); }

    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return (o instanceof Integer) && ((Integer) o).value == value; }
    public int compareTo(Integer other) { return compare(value, other.value); }

    public static int compare(int x, int y) { return x < y ? -1 : (x == y ? 0 : 1); }
    public static Integer valueOf(int i) { return (i >= -128 && i <= 127) ? IntegerCache.cache[i + 128] : new Integer(i); }
    public static Integer valueOf(String s) { return valueOf(parseInt(s)); }
    public static int parseInt(String s) { return parseInt(s, 10); }
    public static int parseInt(String s, int radix) {
        if (s == null) throw new NumberFormatException("Cannot parse null string");
        int len = s.length();
        if (len == 0) throw new NumberFormatException("For input string: " + Long.quote(s));
        boolean neg = false;
        int i = 0;
        char first = s.charAt(0);
        if (first == '-') { neg = true; i = 1; } else if (first == '+') { i = 1; }
        if (i == len) throw new NumberFormatException("For input string: " + Long.quote(s));
        long r = 0;
        for (; i < len; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException("For input string: " + Long.quote(s) + (radix == 10 ? "" : " under radix " + radix));
            r = r * radix + d;
            if (r > 2147483648L) throw new NumberFormatException("For input string: " + Long.quote(s));
        }
        long v = neg ? -r : r;
        if (v > MAX_VALUE || v < MIN_VALUE) throw new NumberFormatException("For input string: " + Long.quote(s));
        return (int) v;
    }
    public static String toString(int i) { return toString(i, 10); }
    public static String toString(int i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (i == 0) return "0";
        boolean neg = i < 0;
        char[] b = new char[33];
        int p = 33;
        long v = neg ? -(long) i : i;
        while (v > 0) { b[--p] = Character.forDigit((int) (v % radix), radix); v /= radix; }
        if (neg) b[--p] = '-';
        return new String(b, p, 33 - p);
    }
    public static String toHexString(int i) { return toUnsignedString0(i, 4); }
    public static String toOctalString(int i) { return toUnsignedString0(i, 3); }
    public static String toBinaryString(int i) { return toUnsignedString0(i, 1); }
    private static String toUnsignedString0(int val, int shift) {
        if (val == 0) return "0";
        int mask = (1 << shift) - 1;
        char[] b = new char[32];
        int p = 32;
        long v = val & 0xFFFFFFFFL;
        while (v != 0) { b[--p] = Character.forDigit((int) (v & mask), 1 << shift); v >>>= shift; }
        return new String(b, p, 32 - p);
    }
    public static int bitCount(int i) {
        int c = 0;
        for (int k = 0; k < 32; k++) if (((i >>> k) & 1) != 0) c++;
        return c;
    }
    public static int numberOfLeadingZeros(int i) {
        if (i == 0) return 32;
        int n = 0;
        while ((i & 0x80000000) == 0) { n++; i <<= 1; }
        return n;
    }
    public static int numberOfTrailingZeros(int i) {
        if (i == 0) return 32;
        int n = 0;
        while ((i & 1) == 0) { n++; i >>>= 1; }
        return n;
    }
    public static int highestOneBit(int i) { return i == 0 ? 0 : 1 << (31 - numberOfLeadingZeros(i)); }
    public static int reverse(int i) {
        int r = 0;
        for (int k = 0; k < 32; k++) { r = (r << 1) | (i & 1); i >>>= 1; }
        return r;
    }
    public static int signum(int i) { return (i >> 31) | (-i >>> 31); }
    public static int rotateLeft(int i, int d) { return (i << d) | (i >>> -d); }
    public static int rotateRight(int i, int d) { return (i >>> d) | (i << -d); }
    public static int sum(int a, int b) { return a + b; }
    public static int max(int a, int b) { return Math.max(a, b); }
    public static int min(int a, int b) { return Math.min(a, b); }
    public static long toUnsignedLong(int x) { return x & 0xFFFFFFFFL; }
    public static String toUnsignedString(int i) { return Long.toString(toUnsignedLong(i)); }
    public static int divideUnsigned(int dividend, int divisor) { return (int) (toUnsignedLong(dividend) / toUnsignedLong(divisor)); }
    public static int remainderUnsigned(int dividend, int divisor) { return (int) (toUnsignedLong(dividend) % toUnsignedLong(divisor)); }
    public static int compareUnsigned(int x, int y) { return Long.compare(toUnsignedLong(x), toUnsignedLong(y)); }

    private static final class IntegerCache {
        static final Integer[] cache = new Integer[256];
        static { for (int i = 0; i < 256; i++) cache[i] = new Integer(i - 128); }
    }
}
