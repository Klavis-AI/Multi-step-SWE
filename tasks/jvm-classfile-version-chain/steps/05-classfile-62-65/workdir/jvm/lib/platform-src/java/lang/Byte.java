package java.lang;
public final class Byte extends Number implements Comparable<Byte> {
    public static final byte MIN_VALUE = -128;
    public static final byte MAX_VALUE = 127;
    public static final int SIZE = 8;
    public static final int BYTES = 1;
    public static final Class<Byte> TYPE = (Class<Byte>) Class.getPrimitiveClass("byte");
    private final byte value;
    public Byte(byte value) { this.value = value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return Integer.toString(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return (o instanceof Byte) && ((Byte) o).value == value; }
    public int compareTo(Byte o) { return value - o.value; }
    public static Byte valueOf(byte b) { return cache[b + 128]; }
    public static byte parseByte(String s) { return (byte) Integer.parseInt(s); }
    public static String toString(byte b) { return Integer.toString(b); }
    public static int compare(byte x, byte y) { return x - y; }
    private static final Byte[] cache = new Byte[256];
    static { for (int i = 0; i < 256; i++) cache[i] = new Byte((byte) (i - 128)); }
}
