package lua;

import java.util.Random;

/** The mathematical library of Lua 5.1. */
final class LibMath {
    static Random rng = new Random(0);

    static void open(LuaTable g) {
        LuaTable m = new LuaTable();
        g.rawset("math", m);
        m.rawset("pi", Math.PI);
        m.rawset("huge", Double.POSITIVE_INFINITY);

        m.rawset("maxinteger", Long.MAX_VALUE);
        m.rawset("mininteger", Long.MIN_VALUE);

        Lib.reg(m, "abs", a -> {
            Object v = Lib.checkNum(a, 0);
            if (v instanceof Long) { long l = (Long) v; return Lib.ret(l < 0 ? -l : l); }
            return Lib.ret(Math.abs((Double) v));
        });

        Lib.reg(m, "type", a -> {
            Object v = Lib.checkAny(a, 0);
            if (v instanceof Long) return Lib.ret(LuaString.of("integer"));
            if (v instanceof Double) return Lib.ret(LuaString.of("float"));
            return Lib.ret((Object) null);
        });

        Lib.reg(m, "tointeger", a -> {
            // lua_tointegerx also converts a string that reads as an integer.
            Object v = Lib.arg(a, 0);
            Lib.checkAny(a, 0);
            Object n = Interp.toNumber(v);
            if (n == null) return Lib.ret((Object) null);
            return Lib.ret(LuaNum.exact(n));
        });

        Lib.reg(m, "ult", a -> Lib.ret(Long.compareUnsigned(Lib.checkInt(a, 0), Lib.checkInt(a, 1)) < 0));
        Lib.reg(m, "sqrt", a -> Lib.ret(Math.sqrt(Lib.checkNumber(a, 0))));
        Lib.reg(m, "sin", a -> Lib.ret(Math.sin(Lib.checkNumber(a, 0))));
        Lib.reg(m, "cos", a -> Lib.ret(Math.cos(Lib.checkNumber(a, 0))));
        Lib.reg(m, "tan", a -> Lib.ret(Math.tan(Lib.checkNumber(a, 0))));
        Lib.reg(m, "asin", a -> Lib.ret(Math.asin(Lib.checkNumber(a, 0))));
        Lib.reg(m, "acos", a -> Lib.ret(Math.acos(Lib.checkNumber(a, 0))));
        // From 5.3 math.atan takes the second operand of atan2, defaulting to 1.
        Lib.reg(m, "atan", a -> Lib.ret(
                Math.atan2(Lib.checkNumber(a, 0), Lib.optNumber(a, 1, 1.0))));
        if (Compat.MATHLIB) Lib.reg(m, "atan2", a -> Lib.ret(Math.atan2(Lib.checkNumber(a, 0), Lib.checkNumber(a, 1))));
        if (Compat.MATHLIB) Lib.reg(m, "sinh", a -> Lib.ret(Math.sinh(Lib.checkNumber(a, 0))));
        if (Compat.MATHLIB) Lib.reg(m, "cosh", a -> Lib.ret(Math.cosh(Lib.checkNumber(a, 0))));
        if (Compat.MATHLIB) Lib.reg(m, "tanh", a -> Lib.ret(Math.tanh(Lib.checkNumber(a, 0))));
        Lib.reg(m, "exp", a -> Lib.ret(Math.exp(Lib.checkNumber(a, 0))));
        Lib.reg(m, "deg", a -> Lib.ret(Math.toDegrees(Lib.checkNumber(a, 0))));
        Lib.reg(m, "rad", a -> Lib.ret(Math.toRadians(Lib.checkNumber(a, 0))));
        Lib.reg(m, "ceil", a -> Lib.ret(toint(Math.ceil(Lib.checkNumber(a, 0)), Lib.arg(a, 0))));
        Lib.reg(m, "floor", a -> Lib.ret(toint(Math.floor(Lib.checkNumber(a, 0)), Lib.arg(a, 0))));
        if (Compat.MATHLIB) Lib.reg(m, "pow", a -> Lib.ret(Math.pow(Lib.checkNumber(a, 0), Lib.checkNumber(a, 1))));

        Lib.reg(m, "log", a -> {
            double x = Lib.checkNumber(a, 0);
            if (a.length < 2 || a[1] == null) return Lib.ret(Math.log(x));
            double b = Lib.checkNumber(a, 1);
            if (b == 10.0) return Lib.ret(Math.log10(x));
            return Lib.ret(Math.log(x) / Math.log(b));
        });
        if (Compat.MATHLIB) Lib.reg(m, "log10", a -> Lib.ret(Math.log10(Lib.checkNumber(a, 0))));

        Lib.reg(m, "fmod", a -> {
            Object x = Lib.checkNum(a, 0), y = Lib.checkNum(a, 1);
            if (x instanceof Long && y instanceof Long) {
                long d = (Long) y;
                if (Long.compareUnsigned(d + 1L, 1L) <= 0) {          // 0 or -1
                    if (d == 0) throw Lib.argError(1, "zero");
                    return Lib.ret(0L);
                }
                return Lib.ret((Long) x % d);
            }
            return Lib.ret(cfmod(LuaNum.dbl(x), LuaNum.dbl(y)));
        });

        Lib.reg(m, "modf", a -> {
            Object v = Lib.checkNum(a, 0);
            if (v instanceof Long) return Lib.ret(v, 0.0);
            double d = (Double) v;
            double ip = d >= 0 ? Math.floor(d) : Math.ceil(d);
            double fp = (d == ip) ? 0.0 : d - ip;
            return Lib.ret(pushnumint(ip), fp);
        });

        if (Compat.MATHLIB) {
            Lib.reg(m, "frexp", a -> {
                double d = Lib.checkNumber(a, 0);
                if (d == 0 || Double.isNaN(d) || Double.isInfinite(d)) return Lib.ret(d, 0L);   // the exponent is an integer
                int e = Math.getExponent(d);
                if (e == -1023) {                    // subnormal: probe a scaled copy
                    e = Math.getExponent(d * 0x1p54) - 54;
                }
                e = e + 1;
                double mant = Math.scalb(d, -e);
                return Lib.ret(mant, (long) e);
            });
        }

        if (Compat.MATHLIB) Lib.reg(m, "ldexp", a -> Lib.ret(Math.scalb(Lib.checkNumber(a, 0), (int) Lib.checkInt(a, 1))));

        Lib.reg(m, "max", a -> {
            if (a.length < 1) throw Lib.argError(0, "value expected");
            Object r = Lib.checkNum(a, 0);
            for (int i = 1; i < a.length; i++) {
                Object v = Lib.checkNum(a, i);
                if (LuaNum.lt(r, v)) r = v;
            }
            return Lib.ret(r);
        });

        Lib.reg(m, "min", a -> {
            if (a.length < 1) throw Lib.argError(0, "value expected");
            Object r = Lib.checkNum(a, 0);
            for (int i = 1; i < a.length; i++) {
                Object v = Lib.checkNum(a, i);
                if (LuaNum.lt(v, r)) r = v;
            }
            return Lib.ret(r);
        });

        Lib.reg(m, "random", a -> {
            if (a.length > 2) throw Interp.libError("wrong number of arguments");
            switch (a.length) {
                case 0: return Lib.ret(rng.nextDouble());
                case 1: {
                    long u = Lib.checkInt(a, 0);
                    if (1 > u) throw Lib.argError(0, "interval is empty");
                    return Lib.ret(project(0, u - 1) + 1L);
                }
                default: {
                    long lo = Lib.checkInt(a, 0), up = Lib.checkInt(a, 1);
                    if (lo > up) throw Lib.argError(1, "interval is empty");
                    if (!(lo >= 0 || up <= Long.MAX_VALUE + lo)) {
                        throw Lib.argError(0, "interval too large");
                    }
                    return Lib.ret(lo + project(0, up - lo));
                }
            }
        });

        Lib.reg(m, "randomseed", a -> {
            rng = new Random((long) Lib.checkNumber(a, 0));
            return Lib.NONE;
        });
    }

    /** pushnumint: a float result becomes an integer when it has one exactly. */
    static Object pushnumint(double d) {
        Long l = LuaNum.exact(d);
        return (l != null) ? (Object) l : (Object) d;
    }

    /** floor/ceil give an integer when the result fits. */
    private static Object toint(double d, Object orig) {
        if (orig instanceof Long) return orig;
        return pushnumint(d);
    }

    static double cfmod(double x, double y) {
        double r = x % y;
        return r;
    }

    /**
     * project: a value uniform over [0, n] taken from whole random words, so
     * that the whole integer range is reachable when the interval is that wide.
     */
    private static long project(long lo, long n) {
        if (n == -1L) return rng.nextLong();          // the full 64-bit range
        long lim = n;
        // round up to the next 2^k - 1
        lim |= lim >>> 1; lim |= lim >>> 2; lim |= lim >>> 4;
        lim |= lim >>> 8; lim |= lim >>> 16; lim |= lim >>> 32;
        long r;
        do {
            r = rng.nextLong() & lim;
        } while (Long.compareUnsigned(r, n) > 0);
        return lo + r;
    }
}
