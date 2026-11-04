package lua;

import java.io.ByteArrayOutputStream;

/** The string library of Lua 5.1. */
final class LibString {

    /** MAXSSIZE: the largest string this implementation can build. */
    private static final long MAXSSIZE = Integer.MAX_VALUE - 16;


    static void open(LuaTable g) {
        LuaTable s = new LuaTable();
        g.rawset("string", s);
        LuaTable mt = new LuaTable();
        mt.rawset("__index", s);
        Interp.stringMeta = mt;

        Lib.reg(s, "len", a -> Lib.ret((long) Lib.checkString(a, 0).length()));

        Lib.reg(s, "sub", a -> {
            LuaString str = Lib.checkString(a, 0);
            int l = str.length();
            long i = Lib.optInt(a, 1, 1), j = Lib.optInt(a, 2, -1);
            if (i < 0) i = Math.max(l + i + 1, 1);
            else if (i == 0) i = 1;
            if (j < 0) j = l + j + 1;
            else if (j > l) j = l;
            if (i > j) return Lib.ret(LuaString.of(""));
            return Lib.ret(str.sub((int) i - 1, (int) j));
        });

        Lib.reg(s, "upper", a -> {
            LuaString str = Lib.checkString(a, 0);
            byte[] b = str.b.clone();
            for (int i = 0; i < b.length; i++) {
                int c = b[i] & 0xff;
                if (c >= 'a' && c <= 'z') b[i] = (byte) (c - 32);
            }
            return Lib.ret(LuaString.of(b));
        });

        Lib.reg(s, "lower", a -> {
            LuaString str = Lib.checkString(a, 0);
            byte[] b = str.b.clone();
            for (int i = 0; i < b.length; i++) {
                int c = b[i] & 0xff;
                if (c >= 'A' && c <= 'Z') b[i] = (byte) (c + 32);
            }
            return Lib.ret(LuaString.of(b));
        });

        Lib.reg(s, "rep", a -> {
            LuaString str = Lib.checkString(a, 0);
            long n = Lib.checkInt(a, 1);
            LuaString sep = (a.length > 2 && a[2] != null) ? Lib.checkString(a, 2) : LuaString.of("");
            if (n <= 0) return Lib.ret(LuaString.of(""));
            // str_rep's own overflow-safe test: the product is never formed,
            // because for a large count it would wrap round.
            long unit = (long) str.length() + sep.length();
            if (unit > 0 && unit > (MAXSSIZE - sep.length()) / n) {
                throw Interp.libError("resulting string too large");
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            for (long i = 0; i < n; i++) {
                if (i > 0) out.write(sep.b, 0, sep.b.length);
                out.write(str.b, 0, str.b.length);
            }
            return Lib.ret(LuaString.of(out.toByteArray()));
        });

        Lib.reg(s, "reverse", a -> {
            LuaString str = Lib.checkString(a, 0);
            byte[] b = new byte[str.length()];
            for (int i = 0; i < b.length; i++) b[i] = str.b[b.length - 1 - i];
            return Lib.ret(LuaString.of(b));
        });

        Lib.reg(s, "byte", a -> {
            LuaString str = Lib.checkString(a, 0);
            int l = str.length();
            long i = Lib.optInt(a, 1, 1);
            long j = Lib.optInt(a, 2, i);
            if (i < 0) i = l + i + 1;
            if (j < 0) j = l + j + 1;
            if (i < 1) i = 1;
            if (j > l) j = l;
            if (i > j) return Lib.NONE;
            Object[] r = new Object[(int) (j - i + 1)];
            for (int k = 0; k < r.length; k++) r[k] = (long) str.byteAt((int) i - 1 + k);
            return r;
        });

        Lib.reg(s, "char", a -> {
            byte[] b = new byte[a.length];
            for (int i = 0; i < a.length; i++) {
                long v = Lib.checkInt(a, i);
                if (v < 0 || v > 255) throw Lib.argError(i, "value out of range");
                b[i] = (byte) v;
            }
            return Lib.ret(LuaString.of(b));
        });

        Lib.reg(s, "format", a -> Lib.ret(Fmt.format(a, "format")));

        Lib.reg(s, "dump", a -> {
            Lib.checkFunction(a, 0);
            throw Interp.libError("unable to dump given function");
        });

        LibPack.open(s);

        Lib.reg(s, "find", a -> strFind(a, true));
        Lib.reg(s, "match", a -> strFind(a, false));

        Lib.reg(s, "gmatch", a -> {
            LuaString src = Lib.checkString(a, 0);
            LuaString pat = Lib.checkString(a, 1);
            // From 5.4 gmatch takes an initial position (posrelatI: negative
            // counts from the end, anything before the start is the start).
            // A position past the end leaves nothing at all to match, not even
            // the empty string.
            long ip = Lib.optInt(a, 2, 1);
            long init = (ip > 0) ? ip
                      : (ip == 0 || -ip > src.length()) ? 1 : src.length() + ip + 1;
            init -= 1;
            if (init > src.length()) init = src.length() + 1;
            final int[] pos = {(int) init};
            final int[] lastMatch = {-1};
            return Lib.ret(new Lib.Fn("gmatch iterator", b -> {
                while (pos[0] <= src.length()) {
                    LuaPattern m = new LuaPattern(src.b, pat.b);
                    m.reset();
                    int e = m.match(pos[0], 0);
                    if (e >= 0 && e != lastMatch[0]) {
                        int start = pos[0];
                        pos[0] = lastMatch[0] = e;
                        return m.captures(start, e);
                    }
                    pos[0]++;
                }
                return Lib.ret((Object) null);
            }));
        });


        Lib.reg(s, "gsub", a -> {
            LuaString src = Lib.checkString(a, 0);
            LuaString pat = Lib.checkString(a, 1);
            Object repl = Lib.arg(a, 2);
            if (!(repl == null || repl instanceof LuaString || LuaNum.isNum(repl)
                    || repl instanceof LuaTable || repl instanceof LuaFunction)) {
                throw Lib.argError(2, "string/function/table expected");
            }
            long maxN = a.length > 3 && a[3] != null ? Lib.checkInt(a, 3) : Long.MAX_VALUE;
            boolean anchor = pat.length() > 0 && pat.byteAt(0) == '^';
            int pstart = anchor ? 1 : 0;
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            int pos = 0;
            int lastMatch = -1;
            long count = 0;
            boolean changed = false;
            while (count < maxN) {
                LuaPattern m = new LuaPattern(src.b, pat.b);
                m.reset();
                int e = m.match(pos, pstart);
                if (e >= 0 && e != lastMatch) {
                    count++;
                    changed |= addValue(out, m, src, pos, e, repl);
                    pos = lastMatch = e;
                } else if (pos < src.length()) {
                    out.write(src.byteAt(pos++));
                } else {
                    break;
                }
                if (anchor) break;
            }
            while (pos < src.length()) out.write(src.byteAt(pos++));
            // str_gsub returns the subject itself when nothing changed, which a
            // program can see through string.format("%p", ...).
            // str_gsub returns the subject itself when nothing was replaced,
            // which a program can see through string.format("%p", ...).
            if (!changed) return Lib.ret(src, count);
            if (!changed) return Lib.ret(src, count);
            return Lib.ret(LuaString.of(out.toByteArray()), count);
        });
    }

    /** add_value: true when a real substitution went into the buffer. */
    private static boolean addValue(ByteArrayOutputStream out, LuaPattern m, LuaString src,
                                 int start, int end, Object repl) {
        Object v;
        if (repl instanceof LuaString || LuaNum.isNum(repl)) {
            LuaString rs = Interp.toStr(repl);
            for (int i = 0; i < rs.length(); i++) {
                int c = rs.byteAt(i);
                if (c != '%') { out.write(c); continue; }
                i++;
                if (i >= rs.length()) throw Interp.libError("invalid use of '%' in replacement string");
                int d = rs.byteAt(i);
                if (d == '%') { out.write('%'); continue; }
                if (d < '0' || d > '9') throw Interp.libError("invalid use of '%' in replacement string");
                if (d == '0') {
                    out.write(src.b, start, end - start);
                } else {
                    Object cap = m.oneCapture(d - '1', start, end);
                    LuaString cs = cap instanceof LuaString ? (LuaString) cap : Interp.toStr(cap);
                    out.write(cs.b, 0, cs.b.length);
                }
            }
            return true;
        }
        Object[] caps = m.captures(start, end);
        if (repl instanceof LuaTable) {
            v = Interp.index(repl, caps[0]);
        } else {
            v = Interp.first(Interp.callBarrier(repl, caps));
        }
        if (v == null || v == Boolean.FALSE) {
            out.write(src.b, start, end - start);
            return false;                       // kept the original text
        } else if (v instanceof LuaString || LuaNum.isNum(v)) {
            LuaString vs = Interp.toStr(v);
            out.write(vs.b, 0, vs.b.length);
        } else {
            throw Interp.libError("invalid replacement value (a " + Interp.typeName(v) + ")");
        }
        return true;
    }

    private static Object[] strFind(Object[] a, boolean find) {
        LuaString src = Lib.checkString(a, 0);
        LuaString pat = Lib.checkString(a, 1);
        int l = src.length();
        long init = Lib.optInt(a, 2, 1);
        if (init < 0) init = l + init + 1;
        if (init < 1) init = 1;
        if (init > l + 1) return Lib.ret((Object) null);
        boolean plain = find && Lib.optBool(a, 3, false);
        if (plain || (find && noSpecials(pat))) {
            int idx = indexOf(src.b, pat.b, (int) init - 1);
            if (idx < 0) return Lib.ret((Object) null);
            return Lib.ret((long) (idx + 1), (long) (idx + pat.length()));
        }
        boolean anchor = pat.length() > 0 && pat.byteAt(0) == '^';
        int pstart = anchor ? 1 : 0;
        int pos = (int) init - 1;
        do {
            LuaPattern m = new LuaPattern(src.b, pat.b);
            m.reset();
            int e = m.match(pos, pstart);
            if (e >= 0) {
                if (find) {
                    Object[] caps = m.level == 0 ? Interp.EMPTY : m.captures(pos, e);
                    Object[] r = new Object[2 + caps.length];
                    r[0] = (long) (pos + 1);
                    r[1] = (long) e;
                    System.arraycopy(caps, 0, r, 2, caps.length);
                    return r;
                }
                return m.captures(pos, e);
            }
            pos++;
        } while (pos <= src.length() && !anchor);
        return Lib.ret((Object) null);
    }

    private static boolean noSpecials(LuaString p) {
        for (int i = 0; i < p.length(); i++) {
            if ("^$*+?.([%-".indexOf((char) p.byteAt(i)) >= 0) return false;
        }
        return true;
    }

    private static int indexOf(byte[] hay, byte[] needle, int from) {
        if (needle.length == 0) return Math.min(from, hay.length);
        outer:
        for (int i = from; i + needle.length <= hay.length; i++) {
            for (int j = 0; j < needle.length; j++) if (hay[i + j] != needle[j]) continue outer;
            return i;
        }
        return -1;
    }
}
