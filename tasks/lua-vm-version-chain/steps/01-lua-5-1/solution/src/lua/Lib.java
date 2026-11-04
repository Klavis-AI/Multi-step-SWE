package lua;

/** Argument checking shared by the standard libraries, mirroring lauxlib.c. */
class Lib {
    static String cur = "?";
    static boolean method;

    interface Impl { Object[] run(Object[] a); }

    static final class Fn extends Builtin {
        final Impl impl;
        Fn(String name, Impl impl) { super(name); this.impl = impl; }
        @Override Object[] call(Object[] a) {
            String savedName = cur;
            boolean savedMethod = method;
            cur = name;
            method = Interp.pendingMethod;
            Interp.pendingMethod = false;
            Coroutine co = Interp.current;
            Frame fr = new Frame(name);
            fr.callName = Interp.pendingName;
            Interp.pendingName = null;
            fr.prev = co.top;
            co.top = fr;
            try {
                return impl.run(a);
            } finally {
                co.top = fr.prev;
                cur = savedName;
                method = savedMethod;
            }
        }
    }

    static void reg(LuaTable t, String name, Impl impl) {
        t.rawset(name, new Fn(name, impl));
    }

    static Object[] ret(Object... vs) { return vs; }

    static final Object[] NONE = Interp.EMPTY;

    static Object arg(Object[] a, int i) { return i < a.length ? a[i] : null; }

    static boolean has(Object[] a, int i) { return i < a.length; }

    static String typeAt(Object[] a, int i) {
        return i < a.length ? Interp.typeName(a[i]) : "no value";
    }

    /** luaL_argerror names the function the way the calling instruction did. */
    static String callerName() {
        Frame f = Interp.frameAt(0);
        String n = (f != null && f.isC()) ? f.callName : null;
        return n == null ? "?" : n;
    }

    static LuaError argError(int i, String msg) {
        String cur = callerName();
        int n = i + 1;
        if (method) {
            n--;
            if (n == 0) {
                return Interp.libError("calling '" + cur + "' on bad self (" + msg + ")");
            }
        }
        return Interp.libError("bad argument #" + n + " to '" + cur + "' (" + msg + ")");
    }

    static LuaError typeError(Object[] a, int i, String want) {
        return argError(i, want + " expected, got " + typeAt(a, i));
    }

    static LuaString checkString(Object[] a, int i) {
        Object v = arg(a, i);
        if (v instanceof LuaString) return (LuaString) v;
        if (v instanceof Double) return LuaString.of(NumFmt.numToStr((Double) v));
        throw typeError(a, i, "string");
    }

    static LuaString checkString(Object[] a, int i, String fname) { return checkString(a, i); }

    static LuaString optString(Object[] a, int i, String def) {
        Object v = arg(a, i);
        if (v == null) return def == null ? null : LuaString.of(def);
        return checkString(a, i);
    }

    static double checkNumber(Object[] a, int i) {
        Object v = arg(a, i);
        Double d = Interp.toNumber(v);
        if (d == null) throw typeError(a, i, "number");
        return d;
    }

    static double checkNumber(Object[] a, int i, String fname) { return checkNumber(a, i); }

    static double optNumber(Object[] a, int i, double def) {
        Object v = arg(a, i);
        if (v == null) return def;
        return checkNumber(a, i);
    }

    static long checkInt(Object[] a, int i) {
        double d = checkNumber(a, i);
        return (long) d;
    }

    static long optInt(Object[] a, int i, long def) {
        Object v = arg(a, i);
        if (v == null) return def;
        return checkInt(a, i);
    }

    static LuaTable checkTable(Object[] a, int i) {
        Object v = arg(a, i);
        if (v instanceof LuaTable) return (LuaTable) v;
        throw typeError(a, i, "table");
    }

    static LuaFunction checkFunction(Object[] a, int i) {
        Object v = arg(a, i);
        if (v instanceof LuaFunction) return (LuaFunction) v;
        throw typeError(a, i, "function");
    }

    static Object checkAny(Object[] a, int i) {
        if (i >= a.length) throw argError(i, "value expected");
        return a[i];
    }

    static boolean optBool(Object[] a, int i, boolean def) {
        Object v = arg(a, i);
        if (v == null) return def;
        return Interp.truthy(v);
    }

    /** luaL_checkoption */
    static int checkOption(Object[] a, int i, String def, String[] opts) {
        LuaString s = def == null ? checkString(a, i) : optString(a, i, def);
        String t = s.toString();
        for (int k = 0; k < opts.length; k++) if (opts[k].equals(t)) return k;
        throw argError(i, "invalid option '" + t + "'");
    }
}
