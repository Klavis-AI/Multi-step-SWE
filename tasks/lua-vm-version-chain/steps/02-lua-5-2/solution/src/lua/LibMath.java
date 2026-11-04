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

        Lib.reg(m, "abs", a -> Lib.ret(Math.abs(Lib.checkNumber(a, 0))));
        Lib.reg(m, "sqrt", a -> Lib.ret(Math.sqrt(Lib.checkNumber(a, 0))));
        Lib.reg(m, "sin", a -> Lib.ret(Math.sin(Lib.checkNumber(a, 0))));
        Lib.reg(m, "cos", a -> Lib.ret(Math.cos(Lib.checkNumber(a, 0))));
        Lib.reg(m, "tan", a -> Lib.ret(Math.tan(Lib.checkNumber(a, 0))));
        Lib.reg(m, "asin", a -> Lib.ret(Math.asin(Lib.checkNumber(a, 0))));
        Lib.reg(m, "acos", a -> Lib.ret(Math.acos(Lib.checkNumber(a, 0))));
        Lib.reg(m, "atan", a -> Lib.ret(Math.atan(Lib.checkNumber(a, 0))));
        Lib.reg(m, "atan2", a -> Lib.ret(Math.atan2(Lib.checkNumber(a, 0), Lib.checkNumber(a, 1))));
        Lib.reg(m, "sinh", a -> Lib.ret(Math.sinh(Lib.checkNumber(a, 0))));
        Lib.reg(m, "cosh", a -> Lib.ret(Math.cosh(Lib.checkNumber(a, 0))));
        Lib.reg(m, "tanh", a -> Lib.ret(Math.tanh(Lib.checkNumber(a, 0))));
        Lib.reg(m, "exp", a -> Lib.ret(Math.exp(Lib.checkNumber(a, 0))));
        Lib.reg(m, "deg", a -> Lib.ret(Math.toDegrees(Lib.checkNumber(a, 0))));
        Lib.reg(m, "rad", a -> Lib.ret(Math.toRadians(Lib.checkNumber(a, 0))));
        Lib.reg(m, "ceil", a -> Lib.ret(Math.ceil(Lib.checkNumber(a, 0))));
        Lib.reg(m, "floor", a -> Lib.ret(Math.floor(Lib.checkNumber(a, 0))));
        Lib.reg(m, "pow", a -> Lib.ret(Math.pow(Lib.checkNumber(a, 0), Lib.checkNumber(a, 1))));

        Lib.reg(m, "log", a -> {
            double x = Lib.checkNumber(a, 0);
            if (a.length < 2 || a[1] == null) return Lib.ret(Math.log(x));
            double b = Lib.checkNumber(a, 1);
            if (b == 10.0) return Lib.ret(Math.log10(x));
            return Lib.ret(Math.log(x) / Math.log(b));
        });
        Lib.reg(m, "log10", a -> Lib.ret(Math.log10(Lib.checkNumber(a, 0))));

        Lib.reg(m, "fmod", a -> Lib.ret(cfmod(Lib.checkNumber(a, 0), Lib.checkNumber(a, 1))));

        Lib.reg(m, "modf", a -> {
            double d = Lib.checkNumber(a, 0);
            double ip = d >= 0 ? Math.floor(d) : Math.ceil(d);
            double fp = Double.isInfinite(d) ? 0.0 : d - ip;
            return Lib.ret(ip, fp);
        });

        Lib.reg(m, "frexp", a -> {
            double d = Lib.checkNumber(a, 0);
            if (d == 0 || Double.isNaN(d) || Double.isInfinite(d)) return Lib.ret(d, 0.0);
            int e = Math.getExponent(d);
            if (e == -1023) {                        // subnormal: probe a scaled copy
                e = Math.getExponent(d * 0x1p54) - 54;
            }
            e = e + 1;
            double mant = Math.scalb(d, -e);
            return Lib.ret(mant, (double) e);
        });

        Lib.reg(m, "ldexp", a -> Lib.ret(Math.scalb(Lib.checkNumber(a, 0), (int) Lib.checkInt(a, 1))));

        Lib.reg(m, "max", a -> {
            double r = Lib.checkNumber(a, 0);
            for (int i = 1; i < a.length; i++) r = Math.max(r, Lib.checkNumber(a, i));
            return Lib.ret(r);
        });

        Lib.reg(m, "min", a -> {
            double r = Lib.checkNumber(a, 0);
            for (int i = 1; i < a.length; i++) r = Math.min(r, Lib.checkNumber(a, i));
            return Lib.ret(r);
        });

        Lib.reg(m, "random", a -> {
            if (a.length > 2) throw Interp.libError("wrong number of arguments");
            double r = rng.nextDouble();
            switch (a.length) {
                case 0: return Lib.ret(r);
                case 1: {
                    long u = Lib.checkInt(a, 0);
                    if (1 > u) throw Lib.argError(0, "interval is empty");
                    return Lib.ret(Math.floor(r * u) + 1);
                }
                default: {
                    long lo = Lib.checkInt(a, 0), up = Lib.checkInt(a, 1);
                    if (lo > up) throw Lib.argError(1, "interval is empty");
                    return Lib.ret(Math.floor(r * (up - lo + 1)) + lo);
                }
            }
        });

        Lib.reg(m, "randomseed", a -> {
            rng = new Random((long) Lib.checkNumber(a, 0));
            return Lib.NONE;
        });
    }

    static double cfmod(double x, double y) {
        double r = x % y;
        return r;
    }
}
