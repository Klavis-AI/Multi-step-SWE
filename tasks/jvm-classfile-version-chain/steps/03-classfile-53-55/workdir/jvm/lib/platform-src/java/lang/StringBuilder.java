package java.lang;

public final class StringBuilder implements CharSequence, Appendable {
    private char[] buf;
    private int count;

    public StringBuilder() { buf = new char[16]; }
    public StringBuilder(int capacity) {
        if (capacity < 0) throw new NegativeArraySizeException(Integer.toString(capacity));
        buf = new char[capacity];
    }
    public StringBuilder(String str) { buf = new char[str.length() + 16]; append(str); }
    public StringBuilder(CharSequence seq) { buf = new char[seq.length() + 16]; append(seq); }

    private void ensure(int n) {
        if (count + n > buf.length) {
            int cap = buf.length * 2 + 2;
            if (cap < count + n) cap = count + n;
            buf = java.util.Arrays.copyOf(buf, cap);
        }
    }
    public int length() { return count; }
    public char charAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ", length " + count);
        return buf[index];
    }
    public void setCharAt(int index, char ch) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ", length " + count);
        buf[index] = ch;
    }
    public void setLength(int newLength) {
        if (newLength < 0) throw new StringIndexOutOfBoundsException("newLength " + newLength);
        ensure(newLength - count);
        for (int i = count; i < newLength; i++) buf[i] = '\0';
        count = newLength;
    }
    public CharSequence subSequence(int s, int e) { return substring(s, e); }
    public String substring(int start) { return substring(start, count); }
    public String substring(int start, int end) {
        if (start < 0 || end > count || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end + ", length " + count);
        char[] c = new char[end - start];
        System.arraycopy(buf, start, c, 0, c.length);
        return new String(c);
    }
    public StringBuilder append(String str) {
        if (str == null) str = "null";
        int n = str.length();
        ensure(n);
        str.getChars(0, n, buf, count);
        count += n;
        return this;
    }
    public StringBuilder append(CharSequence s) { return append(s == null ? "null" : s.toString()); }
    public StringBuilder append(CharSequence s, int start, int end) { return append(s.subSequence(start, end).toString()); }
    public StringBuilder append(Object obj) { return append(String.valueOf(obj)); }
    public StringBuilder append(StringBuilder sb) { return append(sb == null ? "null" : sb.toString()); }
    public StringBuilder append(char c) { ensure(1); buf[count++] = c; return this; }
    public StringBuilder append(char[] str) { return append(new String(str)); }
    public StringBuilder append(char[] str, int off, int len) { return append(new String(str, off, len)); }
    public StringBuilder append(boolean b) { return append(String.valueOf(b)); }
    public StringBuilder append(int i) { return append(Integer.toString(i)); }
    public StringBuilder append(long l) { return append(Long.toString(l)); }
    public StringBuilder append(float f) { return append(Float.toString(f)); }
    public StringBuilder append(double d) { return append(Double.toString(d)); }
    public StringBuilder insert(int offset, String str) {
        if (offset < 0 || offset > count) throw new StringIndexOutOfBoundsException("offset " + offset);
        if (str == null) str = "null";
        int n = str.length();
        ensure(n);
        System.arraycopy(buf, offset, buf, offset + n, count - offset);
        str.getChars(0, n, buf, offset);
        count += n;
        return this;
    }
    public StringBuilder insert(int offset, Object obj) { return insert(offset, String.valueOf(obj)); }
    public StringBuilder insert(int offset, char c) { return insert(offset, String.valueOf(c)); }
    public StringBuilder insert(int offset, int i) { return insert(offset, Integer.toString(i)); }
    public StringBuilder deleteCharAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ", length " + count);
        System.arraycopy(buf, index + 1, buf, index, count - index - 1);
        count--;
        return this;
    }
    public StringBuilder delete(int start, int end) {
        if (end > count) end = count;
        if (start < 0 || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end + ", length " + count);
        System.arraycopy(buf, end, buf, start, count - end);
        count -= end - start;
        return this;
    }
    public StringBuilder reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
        return this;
    }
    public int indexOf(String str) { return toString().indexOf(str); }
    public String toString() { char[] c = new char[count]; System.arraycopy(buf, 0, c, 0, count); return new String(c); }
}
