package java.lang;
public final class Double extends Number implements Comparable<Double> {
    public static final double POSITIVE_INFINITY = 1.0d / 0.0;
    public static final double NEGATIVE_INFINITY = -1.0d / 0.0;
    public static final double NaN = 0.0d / 0.0;
    public static final double MAX_VALUE = 1.7976931348623157e308;
    public static final double MIN_VALUE = 4.9e-324;
    public static final double MIN_NORMAL = 2.2250738585072014E-308;
    public static final int SIZE = 64;
    public static final int BYTES = 8;
    public static final Class<Double> TYPE = (Class<Double>) Class.getPrimitiveClass("double");
    private final double value;
    public Double(double value) { this.value = value; }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return (float) value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { long b = doubleToLongBits(value); return (int) (b ^ (b >>> 32)); }
    public boolean equals(Object o) { return (o instanceof Double) && doubleToLongBits(((Double) o).value) == doubleToLongBits(value); }
    public int compareTo(Double d) { return compare(value, d.value); }
    public boolean isNaN() { return isNaN(value); }
    public static boolean isNaN(double v) { return v != v; }
    public static boolean isInfinite(double v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
    public static boolean isFinite(double v) { return !isNaN(v) && !isInfinite(v); }
    public static Double valueOf(double d) { return new Double(d); }
    public static Double valueOf(String s) { return new Double(parseDouble(s)); }
    public static native double parseDouble(String s);
    public static native String toString(double d);
    public static native long doubleToRawLongBits(double value);
    public static native double longBitsToDouble(long bits);
    public static long doubleToLongBits(double value) { return isNaN(value) ? 0x7ff8000000000000L : doubleToRawLongBits(value); }
    public static int compare(double d1, double d2) {
        if (d1 < d2) return -1;
        if (d1 > d2) return 1;
        long a = doubleToLongBits(d1), b = doubleToLongBits(d2);
        return (a == b ? 0 : (a < b ? -1 : 1));
    }
    public static double sum(double a, double b) { return a + b; }
    public static double max(double a, double b) { return Math.max(a, b); }
    public static double min(double a, double b) { return Math.min(a, b); }
}
