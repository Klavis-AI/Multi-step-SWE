package java.lang;

/** java.lang.String. */
public final class String implements CharSequence, Comparable<String> {
    private final char[] value;
    private int hash;

    public String() { this.value = new char[0]; }
    public String(char[] v) { this.value = java.util.Arrays.copyOf(v, v.length); }
    public String(char[] v, int off, int count) {
        if (off < 0 || count < 0 || off + count > v.length) throw new StringIndexOutOfBoundsException("offset " + off + ", count " + count);
        char[] c = new char[count];
        System.arraycopy(v, off, c, 0, count);
        this.value = c;
    }
    public String(String original) { this.value = original.value; this.hash = original.hash; }
    public String(byte[] bytes) { this(bytes, 0, bytes.length); }
    public String(byte[] bytes, int off, int len) {
        this.value = decode(bytes, off, len, System.stdoutEncodingIsUTF8());
    }
    public String(byte[] bytes, int off, int len, String charsetName) throws java.io.UnsupportedEncodingException {
        boolean utf8 = charsetName.equalsIgnoreCase("UTF-8") || charsetName.equalsIgnoreCase("UTF8");
        if (!utf8 && !(charsetName.equalsIgnoreCase("US-ASCII") || charsetName.equalsIgnoreCase("ISO-8859-1")))
            throw new java.io.UnsupportedEncodingException(charsetName);
        this.value = decode(bytes, off, len, utf8);
    }
    public String(byte[] bytes, String charsetName) throws java.io.UnsupportedEncodingException { this(bytes, 0, bytes.length, charsetName); }

    /** VM-internal: adopt the array without copying. */
    String(char[] v, boolean share) { this.value = v; }

    private static char[] decode(byte[] b, int off, int len, boolean utf8) {
        char[] out = new char[len];
        int n = 0;
        int i = off, end = off + len;
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

    public int length() { return value.length; }
    public boolean isEmpty() { return value.length == 0; }
    public char charAt(int index) {
        if (index < 0 || index >= value.length) throw new StringIndexOutOfBoundsException("index " + index + ", length " + value.length);
        return value[index];
    }
    public int codePointAt(int index) {
        char c1 = charAt(index);
        if (Character.isHighSurrogate(c1) && index + 1 < value.length) {
            char c2 = value[index + 1];
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
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }
    public char[] toCharArray() { return java.util.Arrays.copyOf(value, value.length); }

    public byte[] getBytes() { return StringHelper.encode(value, System.stdoutEncodingIsUTF8()); }
    public byte[] getBytes(String charsetName) throws java.io.UnsupportedEncodingException {
        boolean utf8 = charsetName.equalsIgnoreCase("UTF-8") || charsetName.equalsIgnoreCase("UTF8");
        if (!utf8 && !(charsetName.equalsIgnoreCase("US-ASCII") || charsetName.equalsIgnoreCase("ISO-8859-1")))
            throw new java.io.UnsupportedEncodingException(charsetName);
        return StringHelper.encode(value, utf8);
    }
    public boolean equals(Object anObject) {
        if (this == anObject) return true;
        if (!(anObject instanceof String)) return false;
        String s = (String) anObject;
        if (s.value.length != value.length) return false;
        for (int i = 0; i < value.length; i++) if (value[i] != s.value[i]) return false;
        return true;
    }
    public boolean contentEquals(CharSequence cs) {
        if (cs.length() != value.length) return false;
        for (int i = 0; i < value.length; i++) if (value[i] != cs.charAt(i)) return false;
        return true;
    }
    public boolean equalsIgnoreCase(String s) {
        if (s == null || s.value.length != value.length) return false;
        for (int i = 0; i < value.length; i++)
            if (Character.toLowerCase(value[i]) != Character.toLowerCase(s.value[i])) return false;
        return true;
    }
    public int hashCode() {
        int h = hash;
        if (h == 0 && value.length > 0) {
            for (int i = 0; i < value.length; i++) h = 31 * h + value[i];
            hash = h;
        }
        return h;
    }
    public int compareTo(String anotherString) {
        int n = Math.min(value.length, anotherString.value.length);
        for (int i = 0; i < n; i++) {
            int d = value[i] - anotherString.value[i];
            if (d != 0) return d;
        }
        return value.length - anotherString.value.length;
    }
    public int compareToIgnoreCase(String s) { return toLowerCase().compareTo(s.toLowerCase()); }

    public boolean startsWith(String prefix) { return startsWith(prefix, 0); }
    public boolean startsWith(String prefix, int toffset) {
        if (toffset < 0 || toffset + prefix.value.length > value.length) return false;
        for (int i = 0; i < prefix.value.length; i++) if (value[toffset + i] != prefix.value[i]) return false;
        return true;
    }
    public boolean endsWith(String suffix) { return startsWith(suffix, value.length - suffix.value.length); }

    public int indexOf(int ch) { return indexOf(ch, 0); }
    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        for (int i = from; i < value.length; i++) if (value[i] == ch) return i;
        return -1;
    }
    public int lastIndexOf(int ch) {
        for (int i = value.length - 1; i >= 0; i--) if (value[i] == ch) return i;
        return -1;
    }
    public int indexOf(String str) { return indexOf(str, 0); }
    public int indexOf(String str, int from) {
        if (from < 0) from = 0;
        int n = str.value.length;
        for (int i = from; i + n <= value.length; i++) if (startsWith(str, i)) return i;
        return -1;
    }
    public int lastIndexOf(String str) {
        for (int i = value.length - str.value.length; i >= 0; i--) if (startsWith(str, i)) return i;
        return -1;
    }
    public boolean contains(CharSequence s) { return indexOf(s.toString()) >= 0; }

    public String substring(int beginIndex) { return substring(beginIndex, value.length); }
    public String substring(int beginIndex, int endIndex) {
        if (beginIndex < 0 || endIndex > value.length || beginIndex > endIndex)
            throw new StringIndexOutOfBoundsException("begin " + beginIndex + ", end " + endIndex + ", length " + value.length);
        char[] c = new char[endIndex - beginIndex];
        System.arraycopy(value, beginIndex, c, 0, c.length);
        return new String(c, true);
    }
    public CharSequence subSequence(int start, int end) { return substring(start, end); }

    public String concat(String str) {
        char[] c = new char[value.length + str.value.length];
        System.arraycopy(value, 0, c, 0, value.length);
        System.arraycopy(str.value, 0, c, value.length, str.value.length);
        return new String(c, true);
    }
    public String replace(char oldChar, char newChar) {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++) if (c[i] == oldChar) c[i] = newChar;
        return new String(c, true);
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
        return new String(c, true);
    }
    public String toLowerCase() {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++) c[i] = Character.toLowerCase(c[i]);
        return new String(c, true);
    }
    public String trim() {
        int st = 0, en = value.length;
        while (st < en && value[st] <= ' ') st++;
        while (st < en && value[en - 1] <= ' ') en--;
        return (st > 0 || en < value.length) ? substring(st, en) : this;
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
    public static String valueOf(char c) { return new String(new char[]{c}, true); }
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
