package redis;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Byte-string helpers mirroring redis src/util.c semantics. */
public final class Util {
    private Util() {}

    public static final byte[] EMPTY = new byte[0];

    public static byte[] b(String s) { return s.getBytes(StandardCharsets.ISO_8859_1); }
    public static String s(byte[] b) { return new String(b, StandardCharsets.ISO_8859_1); }
    public static String s(byte[] b, int off, int len) { return new String(b, off, len, StandardCharsets.ISO_8859_1); }

    public static byte[] concat(byte[] a, byte[] bb) {
        byte[] r = new byte[a.length + bb.length];
        System.arraycopy(a, 0, r, 0, a.length);
        System.arraycopy(bb, 0, r, a.length, bb.length);
        return r;
    }

    /** redis string2ll: strict long long parsing, no leading spaces, no leading zeroes. */
    public static Long string2ll(byte[] p) {
        if (p == null) return null;
        return string2ll(p, 0, p.length);
    }

    public static Long string2ll(byte[] p, int off, int slen) {
        int i = off;
        int end = off + slen;
        if (slen == 0 || slen >= 21) return null;
        if (slen == 1 && p[i] >= '0' && p[i] <= '9') return (long) (p[i] - '0');
        boolean negative = false;
        if (p[i] == '-') { negative = true; i++; if (i == end) return null; }
        else if (p[i] == '+') { return null; }
        long v;
        if (p[i] >= '1' && p[i] <= '9') { v = p[i] - '0'; i++; }
        else return null;
        while (i < end && p[i] >= '0' && p[i] <= '9') {
            if (v > Long.MAX_VALUE / 10) return null;
            v *= 10;
            int d = p[i] - '0';
            if (v > Long.MAX_VALUE - d) {
                if (negative && v == Long.MAX_VALUE - d + 1 && i + 1 == end) {
                    // exactly LLONG_MIN
                    return Long.MIN_VALUE;
                }
                return null;
            }
            v += d;
            i++;
        }
        if (i != end) return null;
        return negative ? -v : v;
    }

    /** redis ll2string */
    public static byte[] ll2string(long v) { return b(Long.toString(v)); }

    /** redis string2d / strtod with full-string validation. */
    public static Double string2d(byte[] p) {
        if (p == null || p.length == 0) return null;
        String t = s(p);
        // strtod skips leading whitespace; redis rejects it explicitly (isspace(buf[0]))
        char c0 = t.charAt(0);
        if (c0 == ' ' || c0 == '\t' || c0 == '\n' || c0 == '\r' || c0 == '\f' || c0 == 0x0b) return null;
        String lower = t.toLowerCase();
        if (lower.equals("inf") || lower.equals("+inf") || lower.equals("infinity") || lower.equals("+infinity"))
            return Double.POSITIVE_INFINITY;
        if (lower.equals("-inf") || lower.equals("-infinity")) return Double.NEGATIVE_INFINITY;
        if (lower.equals("nan") || lower.equals("-nan") || lower.equals("+nan")) return null;
        // Java's valueOf accepts trailing 'd'/'f' and hex floats; reject those explicitly.
        for (int i = 0; i < t.length(); i++) {
            char c = t.charAt(i);
            if (c == 'd' || c == 'D' || c == 'f' || c == 'F' || c == 'x' || c == 'X') return null;
        }
        try {
            double d = Double.parseDouble(t);
            if (Double.isNaN(d)) return null;
            return d;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /** redis d2string: integers print as integers, otherwise shortest round-tripping %g form. */
    public static byte[] d2string(double value) { return b(d2str(value)); }

    /**
     * redis 2.8 d2string() (util.c): an exact integer in the +-2^52 range prints
     * through ll2string, everything else through "%.17g".
     */
    public static String d2str(double value) {
        if (Double.isNaN(value)) return "nan";
        if (Double.isInfinite(value)) return value < 0 ? "-inf" : "inf";
        if (value == 0) return (1.0 / value < 0) ? "-0" : "0";
        double min = -4503599627370495.0; /* (2^52)-1 */
        double max = 4503599627370496.0;
        if (value > min && value < max && value == (double) (long) value)
            return Long.toString((long) value);
        return fmtG(value, 17);
    }

    /** fpconv_dtoa's emit_digits(): value == digits * 10^k. */
    public static String emitDigits(String digits, int k, boolean neg) {
        StringBuilder sb = new StringBuilder();
        if (neg) sb.append('-');
        int ndigits = digits.length();
        int exp = Math.abs(k + ndigits - 1);

        if (k >= 0 && exp < ndigits + 7) {
            sb.append(digits);
            for (int i = 0; i < k; i++) sb.append('0');
            return sb.toString();
        }
        if (k < 0 && (k > -7 || exp < 4)) {
            int offset = ndigits - Math.abs(k);
            if (offset <= 0) {
                offset = -offset;
                sb.append("0.");
                for (int i = 0; i < offset; i++) sb.append('0');
                sb.append(digits);
            } else {
                sb.append(digits, 0, offset).append('.').append(digits, offset, ndigits);
            }
            return sb.toString();
        }
        ndigits = Math.min(ndigits, 18 - (neg ? 1 : 0));
        sb.append(digits.charAt(0));
        if (ndigits > 1) sb.append('.').append(digits, 1, ndigits);
        sb.append('e').append(k + ndigits - 1 < 0 ? '-' : '+');
        int cent = 0;
        if (exp > 99) { cent = exp / 100; sb.append((char) ('0' + cent)); exp -= cent * 100; }
        if (exp > 9) { int dec = exp / 10; sb.append((char) ('0' + dec)); exp -= dec * 10; }
        else if (cent != 0) sb.append('0');
        sb.append((char) ('0' + exp % 10));
        return sb.toString();
    }

    /**
     * C's "%.<prec>g".  Java's own %g rounds the shortest decimal form to the
     * requested precision, while C rounds the exact binary value, so the digits
     * are taken from BigDecimal(double) (which is exact) instead.
     */
    public static String fmtG(double v, int prec) {
        if (Double.isNaN(v)) return "nan";
        if (Double.isInfinite(v)) return v > 0 ? "inf" : "-inf";
        if (prec <= 0) prec = 1;
        if (v == 0) return (1.0 / v < 0) ? "-0" : "0";
        java.math.BigDecimal r = new java.math.BigDecimal(v)
                .round(new java.math.MathContext(prec, java.math.RoundingMode.HALF_EVEN));
        int exp = r.precision() - r.scale() - 1;   /* decimal exponent of the first digit */
        if (exp < -4 || exp >= prec) {
            String mant = trimZeros(r.movePointLeft(exp).toPlainString());
            int a = Math.abs(exp);
            StringBuilder sb = new StringBuilder(mant);
            sb.append('e').append(exp < 0 ? '-' : '+');
            if (a < 10) sb.append('0');
            sb.append(a);
            return sb.toString();
        }
        String out = r.toPlainString();
        if (out.indexOf('.') >= 0) out = trimZeros(out);
        return out;
    }

    private static String trimZeros(String x) {
        int i = x.length();
        while (i > 0 && x.charAt(i - 1) == '0') i--;
        if (i > 0 && x.charAt(i - 1) == '.') i--;
        return x.substring(0, i);
    }

    /** Redis long double formatting (addReplyHumanLongDouble / %.17Lg with trailing zero removal). */
    public static String ld2str(double value) {
        if (Double.isNaN(value)) return "nan";
        if (Double.isInfinite(value)) return value > 0 ? "inf" : "-inf";
        String out = String.format("%.17f", value);
        if (out.indexOf('.') >= 0) out = trimZeros(out);
        if (out.equals("-0")) out = "0";
        return out;
    }

    /** redis stringmatchlen(): glob-style matching on raw bytes (port of util.c). */
    public static boolean stringmatch(byte[] pattern, byte[] string, boolean nocase) {
        boolean[] skip = new boolean[1];
        return smatch(pattern, 0, pattern.length, string, 0, string.length, nocase, skip, 0);
    }

    private static boolean smatch(byte[] p, int pi, int plen, byte[] st, int si, int slen,
                                  boolean nocase, boolean[] skipLonger, int depth) {
        if (depth > 1000) return false;
        while (plen > 0 && slen > 0) {
            switch (p[pi]) {
                case '*': {
                    while (plen > 1 && p[pi + 1] == '*') { pi++; plen--; }
                    if (plen == 1) return true;
                    while (slen > 0) {
                        if (smatch(p, pi + 1, plen - 1, st, si, slen, nocase, skipLonger, depth + 1)) return true;
                        if (skipLonger[0]) return false;
                        si++; slen--;
                    }
                    skipLonger[0] = true;
                    return false;
                }
                case '?':
                    si++; slen--;
                    break;
                case '[': {
                    pi++; plen--;
                    boolean not = plen > 0 && p[pi] == '^';
                    if (not) { pi++; plen--; }
                    boolean m = false;
                    while (true) {
                        if (plen == 0) { pi--; plen++; break; }
                        if (p[pi] == '\\' && plen >= 2) {
                            pi++; plen--;
                            if (p[pi] == st[si]) m = true;
                        } else if (p[pi] == ']') {
                            break;
                        } else if (plen >= 3 && p[pi + 1] == '-') {
                            int start = p[pi] & 0xff, end = p[pi + 2] & 0xff, c = st[si] & 0xff;
                            if (start > end) { int t = start; start = end; end = t; }
                            if (nocase) {
                                start = Character.toLowerCase((char) start);
                                end = Character.toLowerCase((char) end);
                                c = Character.toLowerCase((char) c);
                            }
                            pi += 2; plen -= 2;
                            if (c >= start && c <= end) m = true;
                        } else {
                            if (!nocase) { if (p[pi] == st[si]) m = true; }
                            else if (Character.toLowerCase((char) (p[pi] & 0xff))
                                    == Character.toLowerCase((char) (st[si] & 0xff))) m = true;
                        }
                        pi++; plen--;
                    }
                    if (not) m = !m;
                    if (!m) return false;
                    si++; slen--;
                    break;
                }
                case '\\':
                    if (plen >= 2) { pi++; plen--; }
                    if (!nocase) { if (p[pi] != st[si]) return false; }
                    else if (Character.toLowerCase((char) (p[pi] & 0xff))
                            != Character.toLowerCase((char) (st[si] & 0xff))) return false;
                    si++; slen--;
                    break;
                default:
                    if (!nocase) { if (p[pi] != st[si]) return false; }
                    else if (Character.toLowerCase((char) (p[pi] & 0xff))
                            != Character.toLowerCase((char) (st[si] & 0xff))) return false;
                    si++; slen--;
                    break;
            }
            pi++; plen--;
            if (slen == 0) {
                while (plen > 0 && p[pi] == '*') { pi++; plen--; }
                break;
            }
        }
        return plen == 0 && slen == 0;
    }

    public static boolean equalsIgnoreCase(byte[] a, String b) {
        if (a.length != b.length()) return false;
        for (int i = 0; i < a.length; i++) {
            if (Character.toLowerCase((char) (a[i] & 0xff)) != Character.toLowerCase(b.charAt(i))) return false;
        }
        return true;
    }

    public static String lower(byte[] a) { return s(a).toLowerCase(); }

    public static long mstime() { return System.currentTimeMillis(); }
    public static long ustime() { return System.currentTimeMillis() * 1000L; }

    public static byte[] sha1hex(byte[] data) {
        try {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-1");
            byte[] d = md.digest(data);
            StringBuilder sb = new StringBuilder(40);
            for (byte x : d) sb.append(Character.forDigit((x >> 4) & 0xf, 16)).append(Character.forDigit(x & 0xf, 16));
            return b(sb.toString());
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    public static byte[] copy(byte[] a) { return Arrays.copyOf(a, a.length); }
}
