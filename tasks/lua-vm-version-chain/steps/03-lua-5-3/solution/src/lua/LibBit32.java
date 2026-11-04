package lua;

/** The bit32 library of Lua 5.2: operations on 32-bit unsigned integers. */
final class LibBit32 {

    private static long arg(Object[] a, int i) {
        double d = Lib.checkNumber(a, i);
        double f = Math.floor(d);
        if (f != d || Double.isInfinite(d) || Double.isNaN(d)) {
            throw Lib.argError(i, "number has no integer representation");
        }
        long l = (long) (f % 4294967296.0);
        if (l < 0) l += 4294967296L;
        return l & 0xffffffffL;
    }

    private static Object u(long v) { return v & 0xffffffffL; }   // 5.3 bit32 yields integers

    private static int shiftArg(Object[] a, int i) {
        double d = Lib.checkNumber(a, i);
        if (Math.floor(d) != d) throw Lib.argError(i, "number has no integer representation");
        return (int) Math.max(-64, Math.min(64, d));
    }

    static void open(LuaTable g) {
        LuaTable b = new LuaTable();
        g.rawset("bit32", b);

        Lib.reg(b, "band", a -> {
            long r = 0xffffffffL;
            for (int i = 0; i < a.length; i++) r &= arg(a, i);
            return Lib.ret(u(r));
        });
        Lib.reg(b, "bor", a -> {
            long r = 0;
            for (int i = 0; i < a.length; i++) r |= arg(a, i);
            return Lib.ret(u(r));
        });
        Lib.reg(b, "bxor", a -> {
            long r = 0;
            for (int i = 0; i < a.length; i++) r ^= arg(a, i);
            return Lib.ret(u(r));
        });
        Lib.reg(b, "btest", a -> {
            long r = 0xffffffffL;
            for (int i = 0; i < a.length; i++) r &= arg(a, i);
            return Lib.ret((r & 0xffffffffL) != 0);
        });
        Lib.reg(b, "bnot", a -> Lib.ret(u(~arg(a, 0))));

        Lib.reg(b, "lshift", a -> {
            long v = arg(a, 0);
            int n = shiftArg(a, 1);
            if (n <= -32 || n >= 32) return Lib.ret(u(0));
            return Lib.ret(u(n >= 0 ? (v << n) : (v >>> -n)));
        });
        Lib.reg(b, "rshift", a -> {
            long v = arg(a, 0);
            int n = shiftArg(a, 1);
            if (n <= -32 || n >= 32) return Lib.ret(u(0));
            return Lib.ret(u(n >= 0 ? (v >>> n) : (v << -n)));
        });
        Lib.reg(b, "arshift", a -> {
            long v = arg(a, 0);
            int n = shiftArg(a, 1);
            if (n < 0 || (v & 0x80000000L) == 0) {
                if (n <= -32 || n >= 32) return Lib.ret(u(0));
                return Lib.ret(u(n >= 0 ? (v >>> n) : (v << -n)));
            }
            if (n >= 32) return Lib.ret(u(0xffffffffL));
            return Lib.ret(u((v >>> n) | ~(0xffffffffL >>> n)));
        });
        Lib.reg(b, "lrotate", a -> {
            long v = arg(a, 0);
            int n = ((int) Lib.checkInt(a, 1)) & 31;
            return Lib.ret(u((v << n) | (v >>> (32 - n) % 32)));
        });
        Lib.reg(b, "rrotate", a -> {
            long v = arg(a, 0);
            int n = (-(int) Lib.checkInt(a, 1)) & 31;
            return Lib.ret(u((v << n) | (v >>> (32 - n) % 32)));
        });

        Lib.reg(b, "extract", a -> {
            long v = arg(a, 0);
            int f = (int) Lib.checkInt(a, 1);
            int w = (int) Lib.optInt(a, 2, 1);
            checkField(f, w);
            return Lib.ret(u((v >>> f) & mask(w)));
        });
        Lib.reg(b, "replace", a -> {
            long v = arg(a, 0);
            long nv = arg(a, 1);
            int f = (int) Lib.checkInt(a, 2);
            int w = (int) Lib.optInt(a, 3, 1);
            checkField(f, w);
            long m = mask(w);
            return Lib.ret(u((v & ~(m << f)) | ((nv & m) << f)));
        });
    }

    private static long mask(int w) { return (w >= 32) ? 0xffffffffL : ((1L << w) - 1); }

    private static void checkField(int f, int w) {
        if (f < 0) throw Lib.argError(1, "field cannot be negative");
        if (w <= 0) throw Lib.argError(3, "width must be positive");
        if (f + w > 32) throw Interp.libError("trying to access non-existent bits");
    }
}
