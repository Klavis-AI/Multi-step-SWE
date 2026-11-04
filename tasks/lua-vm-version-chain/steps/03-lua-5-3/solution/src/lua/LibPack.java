package lua;

import java.io.ByteArrayOutputStream;

/** string.pack, string.unpack and string.packsize, following lstrlib.c. */
final class LibPack {

    private static final int MAXINTSIZE = 16;
    private static final int NB = 8;              // bits per byte
    private static final int SZINT = 8;           // sizeof(lua_Integer)

    private static final class State {
        boolean little = true;                    // native byte order on x86-64
        int maxalign = 1;
    }

    private static final class Opt {
        int kind;                                 // see K* below
        int size;
        int ntoalign;
    }

    private static final int Kint = 0, Kuint = 1, Kfloat = 2, Kchar = 3, Kstring = 4,
            Kzstr = 5, Kpadding = 6, Kpaddalign = 7, Knop = 8, Kdouble = 9;

    private static final class Fmt {
        final LuaString s;
        int pos;
        Fmt(LuaString s) { this.s = s; }
        boolean more() { return pos < s.length(); }
        int next() { return s.byteAt(pos++); }
        int peek() { return pos < s.length() ? s.byteAt(pos) : -1; }
    }

    private static int digit(int c) { return (c >= '0' && c <= '9') ? c - '0' : -1; }

    private static int getnum(Fmt f, int def) {
        if (digit(f.peek()) < 0) return def;
        int a = 0;
        do {
            a = a * 10 + digit(f.next());
        } while (digit(f.peek()) >= 0 && a <= (Integer.MAX_VALUE - 9) / 10);
        return a;
    }

    private static int getnumlimit(Fmt f, int def) {
        int sz = getnum(f, def);
        if (sz > MAXINTSIZE || sz <= 0) {
            throw Interp.libError("integral size (" + sz + ") out of limits [1," + MAXINTSIZE + "]");
        }
        return sz;
    }

    private static Opt getoption(State h, Fmt f) {
        Opt o = new Opt();
        int c = f.next();
        o.size = 0;
        switch (c) {
            case 'b': o.kind = Kint; o.size = 1; break;
            case 'B': o.kind = Kuint; o.size = 1; break;
            case 'h': o.kind = Kint; o.size = 2; break;
            case 'H': o.kind = Kuint; o.size = 2; break;
            case 'l': case 'j': o.kind = Kint; o.size = 8; break;
            case 'L': case 'J': case 'T': o.kind = Kuint; o.size = 8; break;
            case 'i': o.kind = Kint; o.size = getnumlimit(f, 4); break;
            case 'I': o.kind = Kuint; o.size = getnumlimit(f, 4); break;
            case 'f': o.kind = Kfloat; o.size = 4; break;
            case 'd': case 'n': o.kind = Kdouble; o.size = 8; break;
            case 's': o.kind = Kstring; o.size = getnumlimit(f, 8); break;
            case 'z': o.kind = Kzstr; o.size = 0; break;
            case 'c':
                o.size = getnum(f, -1);
                if (o.size == -1) throw Interp.libError("missing size for format option 'c'");
                o.kind = Kchar;
                break;
            case 'x': o.kind = Kpadding; o.size = 1; break;
            case 'X': o.kind = Kpaddalign; o.size = 0; break;
            case ' ': o.kind = Knop; o.size = 0; break;
            case '<': h.little = true; o.kind = Knop; break;
            case '>': h.little = false; o.kind = Knop; break;
            case '=': h.little = true; o.kind = Knop; break;
            case '!': h.maxalign = getnumlimit(f, 8); o.kind = Knop; break;
            default:
                throw Interp.libError("invalid format option '" + (char) c + "'");
        }
        return o;
    }

    private static Opt getdetails(State h, int totalsize, Fmt f) {
        Opt o = getoption(h, f);
        int align = o.size;
        if (o.kind == Kpaddalign) {
            if (!f.more()) throw Lib.argError(0, "invalid next option for option 'X'");
            Opt n = getoption(h, f);
            align = n.size;
            if (n.kind == Kchar || align == 0) {
                throw Lib.argError(0, "invalid next option for option 'X'");
            }
        }
        if (align <= 1 || o.kind == Kchar) {
            o.ntoalign = 0;
        } else {
            if (align > h.maxalign) align = h.maxalign;
            if ((align & (align - 1)) != 0) {
                throw Lib.argError(0, "format asks for alignment not power of 2");
            }
            o.ntoalign = (align - (totalsize & (align - 1))) & (align - 1);
        }
        return o;
    }

    private static void packint(ByteArrayOutputStream out, long n, boolean little, int size,
                                boolean neg) {
        byte[] buff = new byte[size];
        buff[little ? 0 : size - 1] = (byte) (n & 0xff);
        for (int i = 1; i < size; i++) {
            n >>= NB;
            buff[little ? i : size - 1 - i] = (byte) (n & 0xff);
        }
        if (neg && size > SZINT) {
            for (int i = SZINT; i < size; i++) buff[little ? i : size - 1 - i] = (byte) 0xff;
        }
        out.write(buff, 0, size);
    }

    static void open(LuaTable str) {
        Lib.reg(str, "pack", a -> {
            State h = new State();
            Fmt f = new Fmt(Lib.checkString(a, 0));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            int arg = 0;
            int total = 0;
            while (f.more()) {
                Opt o = getdetails(h, total, f);
                total += o.ntoalign + o.size;
                for (int i = 0; i < o.ntoalign; i++) out.write(0);
                switch (o.kind) {
                    case Kint: {
                        arg++;
                        long n = Lib.checkInt(a, arg);
                        if (o.size < SZINT) {
                            long lim = 1L << (o.size * NB - 1);
                            if (!(-lim <= n && n < lim)) {
                                throw Lib.argError(arg, "integer overflow");
                            }
                        }
                        packint(out, n, h.little, o.size, n < 0);
                        break;
                    }
                    case Kuint: {
                        arg++;
                        long n = Lib.checkInt(a, arg);
                        if (o.size < SZINT) {
                            if (Long.compareUnsigned(n, 1L << (o.size * NB)) >= 0) {
                                throw Lib.argError(arg, "unsigned overflow");
                            }
                        }
                        packint(out, n, h.little, o.size, false);
                        break;
                    }
                    case Kfloat: {
                        arg++;
                        float v = (float) Lib.checkNumber(a, arg);
                        packint(out, Float.floatToRawIntBits(v) & 0xffffffffL, h.little, 4, false);
                        break;
                    }
                    case Kdouble: {
                        arg++;
                        double v = Lib.checkNumber(a, arg);
                        packint(out, Double.doubleToRawLongBits(v), h.little, 8, false);
                        break;
                    }
                    case Kchar: {
                        arg++;
                        LuaString cs = Lib.checkString(a, arg);
                        if (cs.length() > o.size) {
                            throw Lib.argError(arg, "string longer than given size");
                        }
                        out.write(cs.b, 0, cs.length());
                        for (int k = cs.length(); k < o.size; k++) out.write(0);
                        break;
                    }
                    case Kstring: {
                        arg++;
                        LuaString s = Lib.checkString(a, arg);
                        int len = s.length();
                        if (o.size < SZINT && Long.compareUnsigned(len, 1L << (o.size * NB)) >= 0) {
                            throw Lib.argError(arg, "string length does not fit in given size");
                        }
                        packint(out, len, h.little, o.size, false);
                        out.write(s.b, 0, len);
                        total += len;
                        break;
                    }
                    case Kzstr: {
                        arg++;
                        LuaString s = Lib.checkString(a, arg);
                        for (int i = 0; i < s.length(); i++) {
                            if (s.byteAt(i) == 0) throw Lib.argError(arg, "string contains zeros");
                        }
                        out.write(s.b, 0, s.length());
                        out.write(0);
                        total += s.length() + 1;
                        break;
                    }
                    case Kpadding: out.write(0); break;
                    default: break;
                }
            }
            return Lib.ret(LuaString.of(out.toByteArray()));
        });

        Lib.reg(str, "packsize", a -> {
            State h = new State();
            Fmt f = new Fmt(Lib.checkString(a, 0));
            long total = 0;
            while (f.more()) {
                Opt o = getdetails(h, (int) total, f);
                if (o.kind == Kstring || o.kind == Kzstr) {
                    throw Lib.argError(0, "variable-length format");
                }
                total += o.ntoalign + o.size;
                if (total > Integer.MAX_VALUE) {
                    throw Lib.argError(0, "format result too large");
                }
            }
            return Lib.ret(total);
        });

        Lib.reg(str, "unpack", a -> {
            State h = new State();
            Fmt f = new Fmt(Lib.checkString(a, 0));
            LuaString data = Lib.checkString(a, 1);
            int pos = (int) LibUtf8.posrelat(Lib.optInt(a, 2, 1), data.length()) - 1;
            if (pos < 0 || pos > data.length()) throw Lib.argError(2, "initial position out of string");
            java.util.ArrayList<Object> res = new java.util.ArrayList<>();
            while (f.more()) {
                Opt o = getdetails(h, pos, f);
                if (o.ntoalign + o.size > data.length() - pos) {
                    throw Lib.argError(1, "data string too short");
                }
                pos += o.ntoalign;
                switch (o.kind) {
                    case Kint: case Kuint: {
                        // unpackint: only the low SZINT bytes fit; the rest have
                        // to be the sign extension, or the value is out of range.
                        int limit = Math.min(o.size, SZINT);
                        long r = 0;
                        for (int i = limit - 1; i >= 0; i--) {
                            r <<= NB;
                            r |= data.byteAt(pos + (h.little ? i : o.size - 1 - i));
                        }
                        boolean signed = (o.kind == Kint);
                        if (o.size < SZINT) {
                            if (signed) {
                                long mask = 1L << (o.size * NB - 1);
                                r = ((r ^ mask) - mask);
                            }
                        } else if (o.size > SZINT) {
                            int mask = (!signed || r >= 0) ? 0 : 0xFF;
                            for (int i = limit; i < o.size; i++) {
                                if (data.byteAt(pos + (h.little ? i : o.size - 1 - i)) != mask) {
                                    throw Interp.libError(o.size
                                            + "-byte integer does not fit into Lua Integer");
                                }
                            }
                        }
                        res.add(r);
                        break;
                    }
                    case Kfloat: {
                        int bits = 0;
                        for (int i = 3; i >= 0; i--) {
                            bits <<= NB;
                            bits |= data.byteAt(pos + (h.little ? i : 3 - i));
                        }
                        res.add((double) Float.intBitsToFloat(bits));
                        break;
                    }
                    case Kdouble: {
                        long bits = 0;
                        for (int i = 7; i >= 0; i--) {
                            bits <<= NB;
                            bits |= data.byteAt(pos + (h.little ? i : 7 - i));
                        }
                        res.add(Double.longBitsToDouble(bits));
                        break;
                    }
                    case Kchar: {
                        res.add(LuaString.of(data.b, pos, o.size));
                        break;
                    }
                    case Kstring: {
                        long len = 0;
                        for (int i = o.size - 1; i >= 0; i--) {
                            len <<= NB;
                            len |= data.byteAt(pos + (h.little ? i : o.size - 1 - i));
                        }
                        if (len > data.length() - pos - o.size) {
                            throw Lib.argError(1, "data string too short");
                        }
                        res.add(LuaString.of(data.b, pos + o.size, (int) len));
                        pos += len;
                        break;
                    }
                    case Kzstr: {
                        int end = pos;
                        while (end < data.length() && data.byteAt(end) != 0) end++;
                        if (end >= data.length()) throw Lib.argError(1, "unfinished string for format 'z'");
                        res.add(LuaString.of(data.b, pos, end - pos));
                        pos = end + 1 - o.size;
                        break;
                    }
                    default: break;
                }
                pos += o.size;
            }
            res.add((long) (pos + 1));
            return res.toArray();
        });
    }
}
