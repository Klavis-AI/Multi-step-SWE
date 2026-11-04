package lua;

import java.io.ByteArrayOutputStream;

/** string.format: C's printf over Lua values. */
final class Fmt {

    static LuaString format(Object[] args, String fname) {
        LuaString fmt = Lib.checkString(args, 0, fname);
        byte[] f = fmt.b;
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        int arg = 0;
        int i = 0;
        while (i < f.length) {
            int c = f[i] & 0xff;
            if (c != '%') { out.write(c); i++; continue; }
            i++;
            if (i < f.length && f[i] == '%') { out.write('%'); i++; continue; }
            // flags
            StringBuilder flags = new StringBuilder();
            while (i < f.length && "-+ #0".indexOf((char) (f[i] & 0xff)) >= 0) {
                flags.append((char) (f[i] & 0xff));
                i++;
                if (flags.length() > 5) throw Interp.libError("invalid format (repeated flags)");
            }
            int width = -1;
            int wd = 0;
            while (i < f.length && f[i] >= '0' && f[i] <= '9') {
                if (width < 0) width = 0;
                width = width * 10 + (f[i] - '0');
                i++; wd++;
            }
            if (wd > 2) throw Interp.libError("invalid format (width or precision too long)");
            int prec = -1;
            if (i < f.length && f[i] == '.') {
                i++;
                prec = 0;
                int pd = 0;
                while (i < f.length && f[i] >= '0' && f[i] <= '9') {
                    prec = prec * 10 + (f[i] - '0');
                    i++; pd++;
                }
                if (pd > 2) throw Interp.libError("invalid format (width or precision too long)");
            }
            if (i >= f.length) throw Interp.libError("invalid option '%' to 'format'");
            char spec = (char) (f[i] & 0xff);
            i++;
            arg++;
            if (spec != '%' && arg >= args.length) throw Lib.argError(arg, "no value");
            String body;
            boolean numeric = true;
            switch (spec) {
                case 'c': {
                    long v = (long) Lib.checkNumber(args, arg, fname);
                    body = String.valueOf((char) (v & 0xff));
                    numeric = false;
                    break;
                }
                case 'd': case 'i': {
                    double d = Lib.checkNumber(args, arg, fname);
                    long v = (long) d;
                    // Long.MIN_VALUE has no positive counterpart: take the digits
                    // from the unsigned rendering of its negation.
                    String digits = (v == Long.MIN_VALUE)
                            ? Long.toUnsignedString(v) : Long.toString(Math.abs(v));
                    body = intBody(digits, v < 0, flags, prec);
                    break;
                }
                case 'u': {
                    long v = toInteger(Lib.checkNumber(args, arg, fname));
                    body = intBody(Long.toUnsignedString(v), false, flags, prec);
                    break;
                }
                case 'o': {
                    long v = toInteger(Lib.checkNumber(args, arg, fname));
                    String o = Long.toOctalString(v);
                    // '#' makes an octal numeral start with a zero.
                    if (flags.indexOf("#") >= 0 && !o.startsWith("0")) o = "0" + o;
                    body = intBody(o, false, flags, prec);
                    break;
                }
                case 'x': case 'X': {
                    long v = toInteger(Lib.checkNumber(args, arg, fname));
                    String h = Long.toHexString(v);
                    if (spec == 'X') h = h.toUpperCase();
                    body = intBody(h, false, flags, prec);
                    if (flags.indexOf("#") >= 0 && v != 0) body = (spec == 'x' ? "0x" : "0X") + body;
                    break;
                }
                case 'e': case 'E':
                    body = signed(NumFmt.e(Lib.checkNumber(args, arg, fname), prec < 0 ? 6 : prec, spec == 'E'), flags);
                    break;
                case 'f':
                    body = signed(NumFmt.f(Lib.checkNumber(args, arg, fname), prec < 0 ? 6 : prec,
                            false, flags.indexOf("#") >= 0), flags);
                    break;
                case 'g': case 'G':
                    body = signed(NumFmt.g(Lib.checkNumber(args, arg, fname), prec < 0 ? 6 : prec,
                            spec == 'G', flags.indexOf("#") >= 0), flags);
                    break;
                case 'q': {
                    LuaString q = Lib.checkString(args, arg, fname);
                    writeQ(out, q);
                    continue;
                }
                case 's': {
                    Object v = arg < args.length ? args[arg] : null;
                    LuaString ls = Interp.tolstring(v);
                    // A NULL string: glibc prints "(null)" unless a precision
                    // too small to hold it says otherwise.
                    if (ls == null) ls = LuaString.of(prec >= 0 && prec < 6 ? "" : "(null)");
                    byte[] sb = ls.b;
                    int len = sb.length;
                    if (prec >= 0 && prec < len) len = prec;
                    String pad = "";
                    if (width > len) {
                        StringBuilder p = new StringBuilder();
                        for (int k = len; k < width; k++) p.append(' ');
                        pad = p.toString();
                    }
                    if (flags.indexOf("-") >= 0) {
                        out.write(sb, 0, len);
                        writeAscii(out, pad);
                    } else {
                        writeAscii(out, pad);
                        out.write(sb, 0, len);
                    }
                    continue;
                }
                default:
                    throw Interp.libError("invalid option '%" + spec + "' to 'format'");
            }
            writeAscii(out, pad(body, width, flags, numeric));
        }
        return LuaString.of(out.toByteArray());
    }

    /** The C conversion of a double to a 64-bit integer, wrapping above 2^63. */
    static long toInteger(double d) {
        if (d >= 9.223372036854775807E18) return (long) (d - 1.8446744073709552E19);
        return (long) d;
    }

    private static String intBody(String digits, boolean neg, StringBuilder flags, int prec) {
        StringBuilder sb = new StringBuilder();
        // C: a zero with an explicit precision of zero prints nothing at all.
        if (prec == 0 && digits.equals("0")) digits = "";
        if (prec >= 0) while (digits.length() < prec) digits = "0" + digits;
        if (neg) sb.append('-');
        else if (flags.indexOf("+") >= 0) sb.append('+');
        else if (flags.indexOf(" ") >= 0) sb.append(' ');
        sb.append(digits);
        return sb.toString();
    }

    private static String signed(String body, StringBuilder flags) {
        if (body.startsWith("-")) return body;
        if (flags.indexOf("+") >= 0) return "+" + body;
        if (flags.indexOf(" ") >= 0) return " " + body;
        return body;
    }

    private static String pad(String body, int width, StringBuilder flags, boolean numeric) {
        if (width <= body.length()) return body;
        int n = width - body.length();
        StringBuilder p = new StringBuilder();
        if (flags.indexOf("-") >= 0) {
            for (int i = 0; i < n; i++) p.append(' ');
            return body + p;
        }
        if (numeric && flags.indexOf("0") >= 0) {
            String sign = "";
            if (!body.isEmpty() && (body.charAt(0) == '-' || body.charAt(0) == '+' || body.charAt(0) == ' ')) {
                sign = body.substring(0, 1);
                body = body.substring(1);
            }
            for (int i = 0; i < n; i++) p.append('0');
            return sign + p + body;
        }
        for (int i = 0; i < n; i++) p.append(' ');
        return p + body;
    }

    private static void writeAscii(ByteArrayOutputStream out, String s) {
        for (int i = 0; i < s.length(); i++) out.write(s.charAt(i) & 0xff);
    }

    /** %q as Lua 5.2 writes it: control characters become decimal escapes. */
    private static final String BSLASH = String.valueOf((char) 92);
    private static void writeQ(ByteArrayOutputStream out, LuaString s) {
        out.write('"');
        for (int i = 0; i < s.b.length; i++) {
            int c = s.b[i] & 0xff;
            if (c == '"' || c == 92 || c == 10) {
                out.write(92);
                out.write(c);
            } else if (c < 32 || c == 127) {
                int next = (i + 1 < s.b.length) ? (s.b[i + 1] & 0xff) : -1;
                boolean digit = next >= '0' && next <= '9';
                writeAscii(out, digit ? (BSLASH + String.format("%03d", c)) : (BSLASH + c));
            } else {
                out.write(c);
            }
        }
        out.write('"');
    }
}
