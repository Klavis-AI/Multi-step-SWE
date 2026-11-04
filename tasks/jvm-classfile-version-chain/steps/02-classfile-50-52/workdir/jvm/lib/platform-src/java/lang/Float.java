package java.lang;
public final class Float extends Number implements Comparable<Float> {
    public static final float POSITIVE_INFINITY = 1.0f / 0.0f;
    public static final float NEGATIVE_INFINITY = -1.0f / 0.0f;
    public static final float NaN = 0.0f / 0.0f;
    public static final float MAX_VALUE = 3.4028235e38f;
    public static final float MIN_VALUE = 1.4e-45f;
    public static final float MIN_NORMAL = 1.17549435E-38f;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    public static final Class<Float> TYPE = (Class<Float>) Class.getPrimitiveClass("float");
    private final float value;
    public Float(float value) { this.value = value; }
    public Float(double value) { this.value = (float) value; }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return floatToIntBits(value); }
    public boolean equals(Object o) { return (o instanceof Float) && floatToIntBits(((Float) o).value) == floatToIntBits(value); }
    public int compareTo(Float f) { return compare(value, f.value); }
    public boolean isNaN() { return isNaN(value); }
    public static boolean isNaN(float v) { return v != v; }
    public static boolean isInfinite(float v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
    public static boolean isFinite(float v) { return !isNaN(v) && !isInfinite(v); }
    public static Float valueOf(float f) { return new Float(f); }
    public static Float valueOf(String s) { return new Float(parseFloat(s)); }
    public static native float parseFloat(String s);
    public static native String toString(float f);
    public static native int floatToRawIntBits(float value);
    public static native float intBitsToFloat(int bits);
    public static int floatToIntBits(float value) { return isNaN(value) ? 0x7fc00000 : floatToRawIntBits(value); }
    public static int compare(float f1, float f2) {
        if (f1 < f2) return -1;
        if (f1 > f2) return 1;
        int a = floatToIntBits(f1), b = floatToIntBits(f2);
        return (a == b ? 0 : (a < b ? -1 : 1));
    }
}
