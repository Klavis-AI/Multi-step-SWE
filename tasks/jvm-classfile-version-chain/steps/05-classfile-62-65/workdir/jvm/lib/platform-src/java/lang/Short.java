package java.lang;
public final class Short extends Number implements Comparable<Short> {
    public static final short MIN_VALUE = -32768;
    public static final short MAX_VALUE = 32767;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    public static final Class<Short> TYPE = (Class<Short>) Class.getPrimitiveClass("short");
    private final short value;
    public Short(short value) { this.value = value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return Integer.toString(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return (o instanceof Short) && ((Short) o).value == value; }
    public int compareTo(Short o) { return value - o.value; }
    public static Short valueOf(short s) { return new Short(s); }
    public static short parseShort(String s) { return (short) Integer.parseInt(s); }
    public static String toString(short s) { return Integer.toString(s); }
    public static int compare(short x, short y) { return x - y; }
}
