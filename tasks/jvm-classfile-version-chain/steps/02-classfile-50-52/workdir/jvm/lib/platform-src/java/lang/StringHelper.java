package java.lang;
/** Character encoding shared by String and the streams. */
public final class StringHelper {
    private StringHelper() {}
    public static byte[] encode(char[] v, boolean utf8) {
        int n = 0;
        for (int i = 0; i < v.length; i++) {
            int c = v[i];
            if (!utf8) n += 1;
            else if (c < 0x80) n += 1;
            else if (c < 0x800) n += 2;
            else if (Character.isHighSurrogate((char) c) && i + 1 < v.length && Character.isLowSurrogate(v[i+1])) { n += 4; i++; }
            else n += 3;
        }
        byte[] out = new byte[n];
        int p = 0;
        for (int i = 0; i < v.length; i++) {
            int c = v[i];
            if (!utf8) { out[p++] = (byte) (c < 0x80 ? c : '?'); }
            else if (c < 0x80) out[p++] = (byte) c;
            else if (c < 0x800) { out[p++] = (byte) (0xC0 | (c >> 6)); out[p++] = (byte) (0x80 | (c & 0x3F)); }
            else if (Character.isHighSurrogate((char) c) && i + 1 < v.length && Character.isLowSurrogate(v[i+1])) {
                int cp = Character.toCodePoint((char) c, v[++i]);
                out[p++] = (byte) (0xF0 | (cp >> 18));
                out[p++] = (byte) (0x80 | ((cp >> 12) & 0x3F));
                out[p++] = (byte) (0x80 | ((cp >> 6) & 0x3F));
                out[p++] = (byte) (0x80 | (cp & 0x3F));
            } else { out[p++] = (byte) (0xE0 | (c >> 12)); out[p++] = (byte) (0x80 | ((c >> 6) & 0x3F)); out[p++] = (byte) (0x80 | (c & 0x3F)); }
        }
        return out;
    }
}
