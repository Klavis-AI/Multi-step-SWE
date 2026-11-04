package java.lang;

/** java.lang.String with the compact representation of JEP 254: a byte[] that
 *  holds Latin-1 when every character fits, UTF-16 otherwise. */
public final class String implements CharSequence, Comparable<String> {
    static final byte LATIN1 = 0;
    static final byte UTF16 = 1;

    private final byte[] value;
    private final byte coder;
    private int hash;

    public String() { value = new byte[0]; coder = LATIN1; }

    String(byte[] v, byte c) { value = v; coder = c; }

    public String(char[] v) { this(v, 0, v.length); }
    public String(char[] v, int off, int count) {
        if (off < 0 || count < 0 || off + count > v.length)
            throw new StringIndexOutOfBoundsException("offset " + off + ", count " + count);
        boolean latin = true;
        for (int i = 0; i < count; i++) if (v[off + i] > 0xFF) { latin = false; break; }
        if (latin) {
            byte[] b = new byte[count];
            for (int i = 0; i < count; i++) b[i] = (byte) v[off + i];
            value = b;
            coder = LATIN1;
        } else {
            byte[] b = new byte[count * 2];
            for (int i = 0; i < count; i++) { b[i * 2] = (byte) (v[off + i] >> 8); b[i * 2 + 1] = (byte) v[off + i]; }
            value = b;
            coder = UTF16;
        }
    }
    public String(String original) { value = original.value; coder = original.coder; hash = original.hash; }
    public String(byte[] bytes) { this(bytes, 0, bytes.length); }
    public String(byte[] bytes, int off, int len) { this(decode(bytes, off, len, System.stdoutEncodingIsUTF8()), 0, decode(bytes, off, len, System.stdoutEncodingIsUTF8()).length); }
    public String(byte[] bytes, int off, int len, String cs) throws java.io.UnsupportedEncodingException {
        this(fromBytes(bytes, off, len, cs), 0, fromBytes(bytes, off, len, cs).length);
    }
    private static char[] fromBytes(byte[] b, int off, int len, String cs) throws java.io.UnsupportedEncodingException {
        return decode(b, off, len, checkCharset(cs));
    }
    public String(byte[] bytes, String cs) throws java.io.UnsupportedEncodingException { this(bytes, 0, bytes.length, cs); }

    private static boolean checkCharset(String cs) throws java.io.UnsupportedEncodingException {
        boolean utf8 = cs.equalsIgnoreCase("UTF-8") || cs.equalsIgnoreCase("UTF8");
        if (!utf8 && !(cs.equalsIgnoreCase("US-ASCII") || cs.equalsIgnoreCase("ISO-8859-1")))
            throw new java.io.UnsupportedEncodingException(cs);
        return utf8;
    }

    private static char[] decode(byte[] b, int off, int len, boolean utf8) {
        char[] out = new char[len];
        int n = 0, i = off, end = off + len;
        while (i < end) {
            int c = b[i] & 0xff;
            if (!utf8 || c < 0x80) { out[n++] = (char) (utf8 ? c : (c < 0x80 ? c : 0xFFFD)); i++; }
            else if ((c & 0xE0) == 0xC0 && i + 1 < end) { out[n++] = (char) (((c & 0x1F) << 6) | (b[i+1] & 0x3F)); i += 2; }
            else if ((c & 0xF0) == 0xE0 && i + 2 < end) { out[n++] = (char) (((c & 0x0F) << 12) | ((b[i+1] & 0x3F) << 6) | (b[i+2] & 0x3F)); i += 3; }
            else if ((c & 0xF8) == 0xF0 && i + 3 < end) {
                int cp = ((c & 0x07) << 18) | ((b[i+1] & 0x3F) << 12) | ((b[i+2] & 0x3F) << 6) | (b[i+3] & 0x3F);
                cp -= 0x10000;
                out[n++] = (char) (0xD800 + (cp >> 10));
                out[n++] = (char) (0xDC00 + (cp & 0x3FF));
                i += 4;
            } else { out[n++] = 0xFFFD; i++; }
        }
        return java.util.Arrays.copyOf(out, n);
    }

    public int length() { return coder == LATIN1 ? value.length : value.length >> 1; }
    public boolean isEmpty() { return value.length == 0; }
    public char charAt(int index) {
        if (index < 0 || index >= length())
            throw new StringIndexOutOfBoundsException("index " + index + ", length " + length());
        if (coder == LATIN1) return (char) (value[index] & 0xff);
        return (char) (((value[index * 2] & 0xff) << 8) | (value[index * 2 + 1] & 0xff));
    }
    public int codePointAt(int index) {
        char c1 = charAt(index);
        if (Character.isHighSurrogate(c1) && index + 1 < length()) {
            char c2 = charAt(index + 1);
            if (Character.isLowSurrogate(c2)) return Character.toCodePoint(c1, c2);
        }
        return c1;
    }
    public int codePointCount(int begin, int end) {
        int n = 0;
        for (int i = begin; i < end; ) { i += Character.charCount(codePointAt(i)); n++; }
        return n;
    }
    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        for (int i = srcBegin; i < srcEnd; i++) dst[dstBegin + i - srcBegin] = charAt(i);
    }
    public char[] toCharArray() {
        char[] c = new char[length()];
        getChars(0, c.length, c, 0);
        return c;
    }
    public byte[] getBytes() { return encodeTo(System.stdoutEncodingIsUTF8()); }
    public byte[] getBytes(String cs) throws java.io.UnsupportedEncodingException { return encodeTo(checkCharset(cs)); }
    private byte[] encodeTo(boolean utf8) { return StringHelper.encode(toCharArray(), utf8); }

    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof String)) return false;
        String s = (String) o;
        if (s.coder != coder || s.value.length != value.length) return false;
        for (int i = 0; i < value.length; i++) if (value[i] != s.value[i]) return false;
        return true;
    }
    public boolean contentEquals(CharSequence cs) {
        if (cs.length() != length()) return false;
        for (int i = 0; i < length(); i++) if (charAt(i) != cs.charAt(i)) return false;
        return true;
    }
    public boolean equalsIgnoreCase(String s) {
        if (s == null || s.length() != length()) return false;
        for (int i = 0; i < length(); i++)
            if (Character.toLowerCase(charAt(i)) != Character.toLowerCase(s.charAt(i))) return false;
        return true;
    }
    public int hashCode() {
        int h = hash;
        if (h == 0 && length() > 0) {
            for (int i = 0; i < length(); i++) h = 31 * h + charAt(i);
            hash = h;
        }
        return h;
    }
    public int compareTo(String other) {
        int n = Math.min(length(), other.length());
        for (int i = 0; i < n; i++) {
            int d = charAt(i) - other.charAt(i);
            if (d != 0) return d;
        }
        return length() - other.length();
    }
    public int compareToIgnoreCase(String s) { return toLowerCase().compareTo(s.toLowerCase()); }
    public boolean startsWith(String prefix) { return startsWith(prefix, 0); }
    public boolean startsWith(String prefix, int toffset) {
        if (toffset < 0 || toffset + prefix.length() > length()) return false;
        for (int i = 0; i < prefix.length(); i++) if (charAt(toffset + i) != prefix.charAt(i)) return false;
        return true;
    }
    public boolean endsWith(String suffix) { return startsWith(suffix, length() - suffix.length()); }
    public int indexOf(int ch) { return indexOf(ch, 0); }
    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        for (int i = from; i < length(); i++) if (charAt(i) == ch) return i;
        return -1;
    }
    public int lastIndexOf(int ch) {
        for (int i = length() - 1; i >= 0; i--) if (charAt(i) == ch) return i;
        return -1;
    }
    public int indexOf(String str) { return indexOf(str, 0); }
    public int indexOf(String str, int from) {
        if (from < 0) from = 0;
        for (int i = from; i + str.length() <= length(); i++) if (startsWith(str, i)) return i;
        return -1;
    }
    public int lastIndexOf(String str) {
        for (int i = length() - str.length(); i >= 0; i--) if (startsWith(str, i)) return i;
        return -1;
    }
    public boolean contains(CharSequence s) { return indexOf(s.toString()) >= 0; }
    public String substring(int beginIndex) { return substring(beginIndex, length()); }
    public String substring(int beginIndex, int endIndex) {
        if (beginIndex < 0 || endIndex > length() || beginIndex > endIndex)
            throw new StringIndexOutOfBoundsException("begin " + beginIndex + ", end " + endIndex + ", length " + length());
        char[] c = new char[endIndex - beginIndex];
        getChars(beginIndex, endIndex, c, 0);
        return new String(c, 0, c.length);
    }
    public CharSequence subSequence(int start, int end) { return substring(start, end); }
    public String concat(String str) {
        char[] c = new char[length() + str.length()];
        getChars(0, length(), c, 0);
        str.getChars(0, str.length(), c, length());
        return new String(c, 0, c.length);
    }
    public String replace(char oldChar, char newChar) {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++) if (c[i] == oldChar) c[i] = newChar;
        return new String(c, 0, c.length);
    }
    public String replace(CharSequence target, CharSequence replacement) {
        String t = target.toString(), r = replacement.toString();
        StringBuilder sb = new StringBuilder();
        int i = 0;
        while (true) {
            int j = indexOf(t, i);
            if (j < 0 || t.isEmpty()) break;
            sb.append(substring(i, j)).append(r);
            i = j + t.length();
        }
        sb.append(substring(i));
        return sb.toString();
    }
    public String toUpperCase() {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++) c[i] = Character.toUpperCase(c[i]);
        return new String(c, 0, c.length);
    }
    public String toLowerCase() {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++) c[i] = Character.toLowerCase(c[i]);
        return new String(c, 0, c.length);
    }
    public String trim() {
        int st = 0, en = length();
        while (st < en && charAt(st) <= ' ') st++;
        while (st < en && charAt(en - 1) <= ' ') en--;
        return (st > 0 || en < length()) ? substring(st, en) : this;
    }
    public String strip() { return trim(); }
    public boolean isBlank() { return trim().isEmpty(); }
    public String repeat(int count) {
        if (count < 0) throw new IllegalArgumentException("count is negative: " + count);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < count; i++) sb.append(this);
        return sb.toString();
    }
    public String toString() { return this; }
    public native String intern();

    public static String valueOf(Object obj) { return obj == null ? "null" : obj.toString(); }
    public static String valueOf(char[] data) { return new String(data); }
    public static String valueOf(boolean b) { return b ? "true" : "false"; }
    public static String valueOf(char c) { return new String(new char[]{c}, 0, 1); }
    public static String valueOf(int i) { return Integer.toString(i); }
    public static String valueOf(long l) { return Long.toString(l); }
    public static String valueOf(float f) { return Float.toString(f); }
    public static String valueOf(double d) { return Double.toString(d); }
    public static String copyValueOf(char[] data) { return new String(data); }
    public static String join(CharSequence delim, CharSequence... elements) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < elements.length; i++) { if (i > 0) sb.append(delim); sb.append(elements[i]); }
        return sb.toString();
    }
}
