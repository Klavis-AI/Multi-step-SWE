package java.lang;
public final class Character implements Comparable<Character> {
    public static final char MIN_VALUE = (char) 0;
    public static final char MAX_VALUE = (char) 0xffff;
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    public static final char MIN_HIGH_SURROGATE = (char) 0xD800;
    public static final char MAX_HIGH_SURROGATE = (char) 0xDBFF;
    public static final char MIN_LOW_SURROGATE = (char) 0xDC00;
    public static final char MAX_LOW_SURROGATE = (char) 0xDFFF;
    public static final int MIN_CODE_POINT = 0x000000;
    public static final int MAX_CODE_POINT = 0X10FFFF;
    public static final int MIN_SUPPLEMENTARY_CODE_POINT = 0x010000;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    public static final Class<Character> TYPE = (Class<Character>) Class.getPrimitiveClass("char");
    private final char value;
    public Character(char value) { this.value = value; }
    public char charValue() { return value; }
    public String toString() { return String.valueOf(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return (o instanceof Character) && ((Character) o).value == value; }
    public int compareTo(Character c) { return value - c.value; }
    public static int compare(char x, char y) { return x - y; }
    public static Character valueOf(char c) { return c <= 127 ? cache[c] : new Character(c); }
    public static String toString(char c) { return String.valueOf(c); }
    public static boolean isDigit(char c) { return c >= 48 && c <= 57; }
    public static boolean isLetter(char c) { return (c >= 97 && c <= 122) || (c >= 65 && c <= 90) || c >= 0x00C0; }
    public static boolean isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
    public static boolean isUpperCase(char c) { return c >= 65 && c <= 90; }
    public static boolean isLowerCase(char c) { return c >= 97 && c <= 122; }
    public static boolean isWhitespace(char c) { return c == 32 || c == 9 || c == 10 || c == 13 || c == 12; }
    public static boolean isSpaceChar(char c) { return c == 32; }
    public static boolean isAlphabetic(int c) { return isLetter((char) c); }
    public static char toUpperCase(char c) { return isLowerCase(c) ? (char) (c - 32) : c; }
    public static char toLowerCase(char c) { return isUpperCase(c) ? (char) (c + 32) : c; }
    public static int digit(char c, int radix) {
        int d = -1;
        if (c >= 48 && c <= 57) d = c - 48;
        else if (c >= 97 && c <= 122) d = c - 97 + 10;
        else if (c >= 65 && c <= 90) d = c - 65 + 10;
        return (d >= 0 && d < radix) ? d : -1;
    }
    public static char forDigit(int digit, int radix) {
        if (digit < 0 || digit >= radix) return (char) 0;
        return (char) (digit < 10 ? 48 + digit : 97 + digit - 10);
    }
    public static int getNumericValue(char c) { return digit(c, 36); }
    public static boolean isHighSurrogate(char ch) { return ch >= MIN_HIGH_SURROGATE && ch <= MAX_HIGH_SURROGATE; }
    public static boolean isLowSurrogate(char ch) { return ch >= MIN_LOW_SURROGATE && ch <= MAX_LOW_SURROGATE; }
    public static boolean isSurrogate(char ch) { return isHighSurrogate(ch) || isLowSurrogate(ch); }
    public static int toCodePoint(char high, char low) {
        return ((high - MIN_HIGH_SURROGATE) << 10) + (low - MIN_LOW_SURROGATE) + MIN_SUPPLEMENTARY_CODE_POINT;
    }
    public static int charCount(int codePoint) { return codePoint >= MIN_SUPPLEMENTARY_CODE_POINT ? 2 : 1; }
    public static char[] toChars(int codePoint) {
        if (codePoint < MIN_SUPPLEMENTARY_CODE_POINT) return new char[] { (char) codePoint };
        int cp = codePoint - MIN_SUPPLEMENTARY_CODE_POINT;
        return new char[] { (char) ((cp >> 10) + MIN_HIGH_SURROGATE), (char) ((cp & 0x3FF) + MIN_LOW_SURROGATE) };
    }
    private static final Character[] cache = new Character[128];
    static { for (int i = 0; i < 128; i++) cache[i] = new Character((char) i); }
}
