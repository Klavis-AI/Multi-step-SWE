package lua;

import java.math.BigDecimal;
import java.math.MathContext;
import java.math.RoundingMode;

/**
 * C-library-compatible conversions between numbers and text: the "%.14g" that
 * `tostring` uses, the printf conversions that `string.format` needs, and the
 * `strtod`-compatible parser behind `tonumber` and the lexer.
 */
final class NumFmt {

    static final String NUMBER_FMT = "%.14g";

    /** tostring for a number, i.e. lua_number2str. */
    static String numToStr(double d) {
        return g(d, 14, false, false);
    }

    // ---------------------------------------------------------------- printf

    static String special(double d, boolean upper) {
        String s;
        if (Double.isNaN(d)) {
            s = (Double.doubleToRawLongBits(d) < 0 ? "-nan" : "nan");
        } else if (d > 0) {
            s = "inf";
        } else {
            s = "-inf";
        }
        return upper ? s.toUpperCase() : s;
    }

    /** %f / %F */
    static String f(double d, int prec, boolean upper) { return f(d, prec, upper, false); }

    /** %f / %F; with alt ('#') the decimal point stays even at precision 0. */
    static String f(double d, int prec, boolean upper, boolean alt) {
        if (!isFinite(d)) return special(d, upper);
        boolean neg = (Double.doubleToRawLongBits(d) < 0);
        BigDecimal bd = new BigDecimal(Math.abs(d)).setScale(prec, RoundingMode.HALF_EVEN);
        String s = bd.toPlainString();
        if (alt && prec == 0) s = s + ".";
        return neg ? "-" + s : s;
    }

    /** %e / %E */
    static String e(double d, int prec, boolean upper) {
        if (!isFinite(d)) return special(d, upper);
        boolean neg = (Double.doubleToRawLongBits(d) < 0);
        double a = Math.abs(d);
        String mant;
        int exp;
        if (a == 0.0) {
            exp = 0;
            mant = prec > 0 ? "0." + "0".repeat(prec) : "0";
        } else {
            BigDecimal bd = new BigDecimal(a).round(new MathContext(prec + 1, RoundingMode.HALF_EVEN));
            exp = bd.precision() - bd.scale() - 1;
            BigDecimal m = bd.movePointLeft(exp).setScale(prec, RoundingMode.HALF_EVEN);
            // rounding can push the mantissa to 10.000...
            if (m.compareTo(BigDecimal.TEN) >= 0) {
                m = m.movePointLeft(1).setScale(prec, RoundingMode.HALF_EVEN);
                exp++;
            }
            mant = m.toPlainString();
        }
        String es = (exp < 0 ? "-" : "+") + (Math.abs(exp) < 10 ? "0" : "") + Math.abs(exp);
        String s = mant + (upper ? "E" : "e") + es;
        return neg ? "-" + s : s;
    }

    /** %g / %G with the C rules, optionally keeping trailing zeros (# flag). */
    static String g(double d, int prec, boolean upper, boolean alt) {
        if (!isFinite(d)) return special(d, upper);
        int p = prec == 0 ? 1 : prec;
        boolean neg = (Double.doubleToRawLongBits(d) < 0);
        double a = Math.abs(d);
        int exp;
        if (a == 0.0) {
            exp = 0;
        } else {
            BigDecimal bd = new BigDecimal(a).round(new MathContext(p, RoundingMode.HALF_EVEN));
            exp = bd.precision() - bd.scale() - 1;
        }
        String body;
        if (exp < -4 || exp >= p) {
            body = e(a, p - 1, upper);
            if (!alt) body = stripExp(body, upper);
        } else {
            body = f(a, p - 1 - exp, upper);
            if (!alt) body = stripZeros(body);
        }
        return neg ? "-" + body : body;
    }

    /** %a / %A, glibc style: 0x1.<hex>p<+|-><dec exponent>. */
    static String a(double d, int prec, boolean upper, boolean alt) {
        if (!isFinite(d)) return special(d, upper);
        long bits = Double.doubleToRawLongBits(d);
        boolean neg = bits < 0;
        int be = (int) ((bits >>> 52) & 0x7ff);
        long mant = bits & 0x000fffffffffffffL;
        int exp;
        int lead;
        if (be == 0) {
            if (mant == 0) { lead = 0; exp = 0; }
            else { lead = 0; exp = -1022; }
        } else {
            lead = 1;
            exp = be - 1023;
        }
        StringBuilder hex = new StringBuilder();
        for (int i = 48; i >= 0; i -= 4) hex.append(Character.forDigit((int) ((mant >>> i) & 0xf), 16));
        String frac = hex.toString();
        if (prec >= 0) {
            // round the fraction to prec hex digits
            if (prec < 13) {
                java.math.BigInteger m = new java.math.BigInteger(frac, 16);
                int drop = 13 - prec;
                java.math.BigInteger div = java.math.BigInteger.ONE.shiftLeft(drop * 4);
                java.math.BigInteger q = m.divide(div), r = m.mod(div);
                java.math.BigInteger half = div.shiftRight(1);
                int c = r.compareTo(half);
                if (c > 0 || (c == 0 && q.testBit(0))) q = q.add(java.math.BigInteger.ONE);
                if (q.bitLength() > prec * 4) { lead++; q = q.subtract(java.math.BigInteger.ONE.shiftLeft(prec * 4)); }
                String qs = q.toString(16);
                while (qs.length() < prec) qs = "0" + qs;
                frac = qs;
            } else {
                StringBuilder sb = new StringBuilder(frac);
                while (sb.length() < prec) sb.append('0');
                frac = sb.toString();
            }
        } else {
            int end = frac.length();
            while (end > 0 && frac.charAt(end - 1) == '0') end--;
            frac = frac.substring(0, end);
        }
        StringBuilder sb = new StringBuilder();
        if (neg) sb.append('-');
        sb.append(upper ? "0X" : "0x").append(lead);
        if (!frac.isEmpty() || alt) sb.append('.').append(frac);
        sb.append(upper ? 'P' : 'p').append(exp < 0 ? '-' : '+').append(Math.abs(exp));
        if (upper) {
            // %A is the whole conversion in upper case, hex digits included.
            String body = sb.toString();
            sb.setLength(0);
            sb.append(body.toUpperCase(java.util.Locale.ROOT));
        }
        return sb.toString();
    }

    private static String stripZeros(String s) {
        if (s.indexOf('.') < 0) return s;
        int end = s.length();
        while (end > 0 && s.charAt(end - 1) == '0') end--;
        if (end > 0 && s.charAt(end - 1) == '.') end--;
        return s.substring(0, end);
    }

    private static String stripExp(String s, boolean upper) {
        int i = s.indexOf(upper ? 'E' : 'e');
        if (i < 0) return s;
        return stripZeros(s.substring(0, i)) + s.substring(i);
    }

    static boolean isFinite(double d) {
        return !Double.isNaN(d) && !Double.isInfinite(d);
    }

    // ------------------------------------------------------------- str2number

    /**
     * The conversion behind `tonumber(s)` and string→number coercion: leading and
     * trailing spaces are skipped, then the whole remainder must be a C `strtod`
     * numeral (which accepts hexadecimal numerals and hexadecimal floats), or a
     * hexadecimal integer.
     */
    static Double str2num(LuaString ls) {
        String s = ls.toString();
        int n = s.length();
        int i = 0, j = n;
        while (i < j && isSpace(s.charAt(i))) i++;
        while (j > i && isSpace(s.charAt(j - 1))) j--;
        if (i >= j) return null;
        String t = s.substring(i, j);
        if (t.indexOf('\0') >= 0) return null;
        return strtod(t);
    }

    static boolean isSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b;
    }

    /** Full-string C strtod. Returns null when the text is not entirely a numeral. */
    static Double strtod(String t) {
        int n = t.length();
        if (n == 0) return null;
        int i = 0;
        boolean neg = false;
        if (t.charAt(i) == '+' || t.charAt(i) == '-') { neg = t.charAt(i) == '-'; i++; }
        if (i >= n) return null;
        String rest = t.substring(i);
        String low = rest.toLowerCase();
        if (low.startsWith("0x")) {
            Double v = hexNumeral(rest.substring(2));
            if (v == null) return null;
            return neg ? -v : v;
        }
        if (low.equals("inf") || low.equals("infinity")) {
            return neg ? Double.NEGATIVE_INFINITY : Double.POSITIVE_INFINITY;
        }
        if (low.equals("nan") || low.startsWith("nan(") && low.endsWith(")")) {
            return neg ? Double.longBitsToDouble(0xfff8000000000000L) : Double.NaN;
        }
        // plain decimal numeral: digits [. digits] [e[+-]digits]
        int k = 0, m = rest.length();
        int digits = 0;
        while (k < m && isDigit(rest.charAt(k))) { k++; digits++; }
        if (k < m && rest.charAt(k) == '.') {
            k++;
            while (k < m && isDigit(rest.charAt(k))) { k++; digits++; }
        }
        if (digits == 0) return null;
        if (k < m && (rest.charAt(k) == 'e' || rest.charAt(k) == 'E')) {
            int save = k;
            k++;
            if (k < m && (rest.charAt(k) == '+' || rest.charAt(k) == '-')) k++;
            int ed = 0;
            while (k < m && isDigit(rest.charAt(k))) { k++; ed++; }
            if (ed == 0) { k = save; return null; }
        }
        if (k != m) return null;
        double d;
        try {
            d = Double.parseDouble(rest);
        } catch (NumberFormatException ex) {
            return null;
        }
        return neg ? -d : d;
    }

    /** The part of a hexadecimal numeral after 0x, C99 style (allows a p exponent). */
    static Double hexNumeral(String s) {
        int n = s.length();
        int i = 0;
        double mant = 0;
        int digits = 0, fracDigits = 0;
        while (i < n && isHex(s.charAt(i))) { mant = mant * 16 + hexVal(s.charAt(i)); i++; digits++; }
        if (i < n && s.charAt(i) == '.') {
            i++;
            while (i < n && isHex(s.charAt(i))) {
                mant = mant * 16 + hexVal(s.charAt(i));
                i++; digits++; fracDigits++;
            }
        }
        if (digits == 0) return null;
        int exp = -4 * fracDigits;
        if (i < n && (s.charAt(i) == 'p' || s.charAt(i) == 'P')) {
            i++;
            boolean eneg = false;
            if (i < n && (s.charAt(i) == '+' || s.charAt(i) == '-')) { eneg = s.charAt(i) == '-'; i++; }
            if (i >= n || !isDigit(s.charAt(i))) return null;
            long e = 0;
            while (i < n && isDigit(s.charAt(i))) { e = Math.min(e * 10 + (s.charAt(i) - '0'), 1 << 20); i++; }
            exp += (int) (eneg ? -e : e);
        }
        if (i != n) return null;
        return mant * Math.pow(2, exp);
    }

    static boolean isDigit(char c) { return c >= '0' && c <= '9'; }

    static boolean isHex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static int hexVal(char c) {
        if (c <= '9') return c - '0';
        return (Character.toLowerCase(c) - 'a') + 10;
    }
}
