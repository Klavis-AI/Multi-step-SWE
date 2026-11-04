package lua;

/**
 * The Lua 5.3 number model: two subtypes, integer (a 64-bit signed integer with
 * wrap-around arithmetic) and float, with the manual's exact conversion and
 * comparison rules between them.
 */
final class LuaNum {

    static final double TWO63 = 9.223372036854775807E18;   // 2^63 as a double
    private static final long MAXINTFITSF = 1L << 53;

    static boolean isNum(Object v) { return v instanceof Long || v instanceof Double; }

    static boolean isInt(Object v) { return v instanceof Long; }

    static double dbl(Object v) {
        return (v instanceof Long) ? (double) (Long) v : (Double) v;
    }

    /** luaV_tointeger with F2Ieq: a float converts only when it is exactly an integer. */
    static Long exact(Object v) {
        if (v instanceof Long) return (Long) v;
        if (v instanceof Double) {
            double d = (Double) v;
            if (d >= -TWO63 && d < TWO63) {
                long l = (long) d;
                if ((double) l == d) return l;
            }
        }
        return null;
    }

    // ------------------------------------------------------------- arithmetic

    static long add(long a, long b) { return a + b; }

    /** luaV_idiv: floor division, with 0x8000...//-1 wrapping instead of trapping. */
    static long idiv(long m, long n) {
        if (n == 0) throw Interp.runtimeError("attempt to divide by zero");
        if (n == -1) return -m;
        long q = m / n;
        if ((m ^ n) < 0 && m % n != 0) q -= 1;
        return q;
    }

    /** luaV_mod: the remainder has the sign of the divisor. */
    static long imod(long m, long n) {
        if (n == 0) throw Interp.runtimeError("attempt to perform 'n%0'");
        if (n == -1) return 0;
        long r = m % n;
        if (r != 0 && (r ^ n) < 0) r += n;
        return r;
    }

    /** luai_nummod of 5.3: fmod, corrected when the signs disagree. */
    static double fmod(double a, double b) {
        double m = a % b;
        if (m * b < 0) m += b;
        return m;
    }

    static double idivf(double a, double b) { return Math.floor(a / b); }

    /** luaV_shiftl: a negative count shifts the other way, |count| >= 64 gives 0. */
    static long shiftl(long x, long n) {
        if (n < 0) {
            if (n <= -64) return 0;
            return x >>> -n;
        }
        if (n >= 64) return 0;
        return x << n;
    }

    // ------------------------------------------------------------- comparison

    private static boolean fitsFloat(long i) {
        return -MAXINTFITSF <= i && i <= MAXINTFITSF;
    }

    static boolean ltIntFloat(long i, double f) {
        if (fitsFloat(i)) return (double) i < f;
        double c = Math.ceil(f);
        if (c >= -TWO63 && c < TWO63) return i < (long) c;
        return f > 0;
    }

    static boolean leIntFloat(long i, double f) {
        if (fitsFloat(i)) return (double) i <= f;
        double fl = Math.floor(f);
        if (fl >= -TWO63 && fl < TWO63) return i <= (long) fl;
        return f > 0;
    }

    static boolean ltFloatInt(double f, long i) {
        if (fitsFloat(i)) return f < (double) i;
        double fl = Math.floor(f);
        if (fl >= -TWO63 && fl < TWO63) return (long) fl < i;
        return f < 0;
    }

    static boolean leFloatInt(double f, long i) {
        if (fitsFloat(i)) return f <= (double) i;
        double c = Math.ceil(f);
        if (c >= -TWO63 && c < TWO63) return (long) c <= i;
        return f < 0;
    }

    static boolean lt(Object a, Object b) {
        if (a instanceof Long) {
            long l = (Long) a;
            if (b instanceof Long) return l < (Long) b;
            return ltIntFloat(l, (Double) b);
        }
        double f = (Double) a;
        if (b instanceof Double) return f < (Double) b;
        return ltFloatInt(f, (Long) b);
    }

    static boolean le(Object a, Object b) {
        if (a instanceof Long) {
            long l = (Long) a;
            if (b instanceof Long) return l <= (Long) b;
            return leIntFloat(l, (Double) b);
        }
        double f = (Double) a;
        if (b instanceof Double) return f <= (Double) b;
        return leFloatInt(f, (Long) b);
    }

    /** Two numbers of different subtypes are equal when they denote the same value. */
    static boolean eq(Object a, Object b) {
        if (a instanceof Long && b instanceof Long) return ((Long) a).longValue() == (Long) b;
        if (a instanceof Double && b instanceof Double) {
            return ((Double) a).doubleValue() == (Double) b;
        }
        Long x = exact(a), y = exact(b);
        return x != null && y != null && x.longValue() == y.longValue();
    }
}
