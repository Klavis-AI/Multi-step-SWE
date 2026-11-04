package lua;

import java.io.ByteArrayOutputStream;

/** The utf8 library of Lua 5.3. */
final class LibUtf8 {

    static void open(LuaTable g) {
        LuaTable u = new LuaTable();
        g.rawset("utf8", u);
        byte[] pat = new byte[]{'[', 0, '-', 0x7F, (byte) 0xC2, '-', (byte) 0xFD, ']',
                                '[', (byte) 0x80, '-', (byte) 0xBF, ']', '*'};
        u.rawset("charpattern", LuaString.of(pat));

        Lib.reg(u, "char", a -> {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            for (int i = 0; i < a.length; i++) {
                long c = Lib.checkInt(a, i);
                if (c < 0 || c > 0x7FFFFFFFL) throw Lib.argError(i, "value out of range");
                byte[] e = Utf8.encode(c);
                out.write(e, 0, e.length);
            }
            return Lib.ret(LuaString.of(out.toByteArray()));
        });

        Lib.reg(u, "codepoint", a -> {
            LuaString s = Lib.checkString(a, 0);
            long i = posrelat(Lib.optInt(a, 1, 1), s.length());
            long j = posrelat(Lib.optInt(a, 2, i), s.length());
            if (i < 1) throw Lib.argError(1, "out of bounds");
            if (j > s.length()) throw Lib.argError(2, "out of bounds");
            boolean strict = !Interp.truthy(Lib.arg(a, 3));
            java.util.ArrayList<Object> out = new java.util.ArrayList<>();
            int p = (int) i - 1;
            while (p < j) {
                long[] d = Utf8.decode(s.b, p, strict);
                if (d == null) throw Interp.libError("invalid UTF-8 code");
                out.add(d[0]);
                p = (int) d[1];
            }
            return out.toArray();
        });

        Lib.reg(u, "len", a -> {
            LuaString s = Lib.checkString(a, 0);
            long i = posrelat(Lib.optInt(a, 1, 1), s.length());
            long j = posrelat(Lib.optInt(a, 2, -1), s.length());
            if (!(1 <= i && i <= s.length() + 1)) throw Lib.argError(1, "initial position out of bounds");
            if (!(j <= s.length())) throw Lib.argError(2, "final position out of bounds");
            boolean strict = !Interp.truthy(Lib.arg(a, 3));
            long n = 0;
            int p = (int) i - 1;
            while (p <= j - 1) {
                long[] d = Utf8.decode(s.b, p, strict);
                if (d == null) return Lib.ret(null, (long) (p + 1));
                p = (int) d[1];
                n++;
            }
            return Lib.ret(n);
        });

        // byteoffset: 5.5 returns the first and the last byte of the
        // character, and refuses a position that lands on a continuation byte.
        Lib.reg(u, "offset", a -> {
            LuaString s = Lib.checkString(a, 0);
            long n = Lib.checkInt(a, 1);
            long posi = posrelat(Lib.optInt(a, 2, n >= 0 ? 1 : s.length() + 1), s.length());
            if (!(1 <= posi && posi - 1 <= s.length())) throw Lib.argError(2, "position out of bounds");
            int p = (int) posi - 1;
            if (n == 0) {
                while (p > 0 && iscont(s, p)) p--;
            } else {
                if (iscont(s, p)) throw Interp.libError("initial position is a continuation byte");
                if (n < 0) {
                    while (n < 0 && p > 0) {
                        do { p--; } while (p > 0 && iscont(s, p));
                        n++;
                    }
                } else {
                    n--;
                    while (n > 0 && p < s.length()) {
                        do { p++; } while (iscont(s, p));
                        n--;
                    }
                }
            }
            if (n != 0) return Lib.ret((Object) null);
            long first = p + 1;
            if (p < s.length() && (s.byteAt(p) & 0x80) != 0) {
                if ((s.byteAt(p) & 0xC0) == 0x80) {
                    throw Interp.libError("initial position is a continuation byte");
                }
                while (iscont(s, p + 1)) p++;
            }
            return Lib.ret(first, (long) (p + 1));
        });

        Lib.Fn iter = new Lib.Fn("codes iterator", a -> {
            LuaString s = Lib.checkString(a, 0);
            // iter_aux: the position is unsigned, so a negative one is past
            // the end and simply ends the iteration.
            long i = Lib.checkInt(a, 1);
            if (i < 0 || i >= s.length()) return Lib.NONE;
            int p = (int) i;
            while (p < s.length() && iscont(s, p)) p++;
            if (p >= s.length()) return Lib.NONE;
            long[] d = Utf8.decode(s.b, p, true);
            // iter_aux also refuses a continuation byte right after a sequence.
            if (d == null || iscont(s, (int) d[1])) throw Interp.libError("invalid UTF-8 code");
            return Lib.ret((long) (p + 1), d[0]);
        });

        Lib.Fn iterLax = new Lib.Fn("codes iterator", a -> {
            LuaString s = Lib.checkString(a, 0);
            // iter_aux: the position is unsigned, so a negative one is past
            // the end and simply ends the iteration.
            long i = Lib.checkInt(a, 1);
            if (i < 0 || i >= s.length()) return Lib.NONE;
            int p = (int) i;
            while (p < s.length() && iscont(s, p)) p++;
            if (p >= s.length()) return Lib.NONE;
            long[] d = Utf8.decode(s.b, p, false);
            if (d == null || iscont(s, (int) d[1])) throw Interp.libError("invalid UTF-8 code");
            return Lib.ret((long) (p + 1), d[0]);
        });

        Lib.reg(u, "codes", a -> {
            LuaString s = Lib.checkString(a, 0);
            if (s.length() > 0 && iscont(s, 0)) throw Lib.argError(0, "invalid UTF-8 code");
            return Lib.ret(Interp.truthy(Lib.arg(a, 1)) ? iterLax : iter, s, 0L);
        });
    }

    private static boolean iscont(LuaString s, int p) {
        return p >= 0 && p < s.length() && (s.byteAt(p) & 0xC0) == 0x80;
    }

    static long posrelat(long pos, int len) {
        if (pos >= 0) return pos;
        if (-pos > len) return 0;
        return len + pos + 1;
    }
}
