package lua;

import java.io.ByteArrayOutputStream;

/** The table library of Lua 5.1, including PUC's own quicksort. */
final class LibTable {

    static void open(LuaTable g) {
        LuaTable t = new LuaTable();
        g.rawset("table", t);

        Lib.reg(t, "insert", a -> {
            Object tt = checkTab(a, 0, TAB_RW);
            long n = len(tt);
            switch (a.length) {
                case 2: seti(tt, n + 1, a[1]); return Lib.NONE;
                case 3: {
                    long pos = Lib.checkInt(a, 1);
                    if (pos < 1 || pos > n + 1) throw Lib.argError(1, "position out of bounds");
                    for (long i = n + 1; i > pos; i--) seti(tt, i, geti(tt, i - 1));
                    seti(tt, pos, a[2]);
                    return Lib.NONE;
                }
                default: throw Interp.libError("wrong number of arguments to 'insert'");
            }
        });

        Lib.reg(t, "remove", a -> {
            Object tt = checkTab(a, 0, TAB_RW);
            long n = len(tt);
            long pos = Lib.optInt(a, 1, n);
            if (pos != n) {
                if (pos < 1 || pos > n + 1) throw Lib.argError(1, "position out of bounds");
            }
            Object v = geti(tt, pos);
            for (long i = pos; i < n; i++) seti(tt, i, geti(tt, i + 1));
            if (pos <= n) seti(tt, Math.max(pos, n), null);
            return Lib.ret(v);
        });

        Lib.reg(t, "concat", a -> {
            Object tt = checkTab(a, 0, TAB_R);
            LuaString sep = Lib.optString(a, 1, "");
            long i = Lib.optInt(a, 2, 1);
            long last = a.length > 3 && a[3] != null ? Lib.checkInt(a, 3) : len(tt);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            // tconcat's loop runs while i < last and then appends the last
            // element, so it cannot overflow when last is the largest integer.
            for (; i < last; i++) {
                appendConcat(out, geti(tt, i), i);
                out.write(sep.b, 0, sep.b.length);
            }
            if (i == last) appendConcat(out, geti(tt, i), i);
            return Lib.ret(LuaString.of(out.toByteArray()));
        });

        Lib.reg(t, "pack", a -> {
            LuaTable r = new LuaTable(a.length, 1);
            for (int i = 0; i < a.length; i++) r.rawseti(i + 1, a[i]);
            r.rawset("n", (long) a.length);
            return Lib.ret(r);
        });

        // table.create pre-sizes a table; the sizes are only a hint, so what
        // is observable is the argument checking and an empty table.
        Lib.reg(t, "create", a -> {
            long seq = Lib.checkInt(a, 0);
            long rest = Lib.optInt(a, 1, 0);
            if (seq < 0 || seq > Integer.MAX_VALUE) throw Lib.argError(0, "out of range");
            if (rest < 0 || rest > Integer.MAX_VALUE) throw Lib.argError(1, "out of range");
            // The sizes are a hint, but the memory they reserve is observable
            // through collectgarbage("count"). A request larger than this
            // implementation can hold is a table overflow, as it is in the
            // reference; the check is up front because the heap ceiling ends
            // the process rather than raising something catchable.
            if (8L * seq + 24L * rest > (256L << 20)) throw Interp.libError("table overflow");
            return Lib.ret(new LuaTable((int) seq, (int) rest));
        });

        Lib.reg(t, "unpack", a -> {
            Object tt = checkTab(a, 0, TAB_R);
            long i = Lib.optInt(a, 1, 1);
            long j = a.length > 2 && a[2] != null ? Lib.checkInt(a, 2) : len(tt);
            if (i > j) return Lib.NONE;
            long n = j - i + 1;
            if (n <= 0 || n > 1000000) throw Interp.libError("too many results to unpack");
            Object[] r = new Object[(int) n];
            for (int k = 0; k < n; k++) r[k] = geti(tt, i + k);
            return r;
        });

        Lib.reg(t, "move", a -> {
            Object a1 = checkTab(a, 0, TAB_R);
            long f = Lib.checkInt(a, 1), e = Lib.checkInt(a, 2), tpos = Lib.checkInt(a, 3);
            Object a2 = (a.length > 4 && a[4] != null) ? checkTab(a, 4, TAB_W) : a1;
            // Overlapping ranges in the same table copy back to front when the
            // destination is above the source, so a source element is read
            // before it is overwritten.
            if (e >= f) {
                if (!(f > 0 || e < Long.MAX_VALUE + f)) {
                    throw Lib.argError(2, "too many elements to move");
                }
                long count = e - f + 1;
                if (!(tpos <= Long.MAX_VALUE - count + 1)) {
                    throw Lib.argError(3, "destination wrap around");
                }
                if (a1 == a2 && tpos > f && tpos <= e) {
                    for (long i = e - f; i >= 0; i--) seti(a2, tpos + i, geti(a1, f + i));
                } else {
                    for (long i = 0; i <= e - f; i++) seti(a2, tpos + i, geti(a1, f + i));
                }
            }
            return Lib.ret(a2);
        });

        Lib.reg(t, "sort", a -> {
            Object tt = checkTab(a, 0, TAB_RW);
            long ln = len(tt);
            if (ln >= Integer.MAX_VALUE) throw Lib.argError(0, "array too big");
            int n = (int) ln;
            Object cmp = a.length > 1 && a[1] != null ? Lib.checkFunction(a, 1) : null;
            Coroutine co = Interp.current;
            co.nonYieldable++;
            try {
                auxsort(tt, 1, n, cmp);
            } finally {
                co.nonYieldable--;
            }
            return Lib.NONE;
        });
    }

    private static boolean lt(Object cmp, Object x, Object y) {
        if (cmp == null) return Interp.lessThan(x, y);
        return Interp.truthy(Interp.first(Interp.callBarrier(cmp, new Object[]{x, y})));
    }

    private static void swap(Object t, int i, int j) {
        Object a = geti(t, i), b = geti(t, j);
        seti(t, i, b);
        seti(t, j, a);
    }

    /** ltablib.c auxsort, so that comparison order and error cases match. */
    private static void auxsort(Object t, int l, int u, Object cmp) {
        while (l < u) {
            if (lt(cmp, geti(t, u), geti(t, l))) swap(t, l, u);
            if (u - l == 1) break;
            int i = (l + u) / 2;
            if (lt(cmp, geti(t, i), geti(t, l))) swap(t, i, l);
            else if (lt(cmp, geti(t, u), geti(t, i))) swap(t, i, u);
            if (u - l == 2) break;
            Object p = geti(t, i);
            swap(t, i, u - 1);
            i = l;
            int j = u - 1;
            for (;;) {
                while (lt(cmp, geti(t, ++i), p)) {
                    if (i >= u) throw Interp.libError("invalid order function for sorting");
                }
                while (lt(cmp, p, geti(t, --j))) {
                    if (j <= l) throw Interp.libError("invalid order function for sorting");
                }
                if (i >= j) break;
                swap(t, i, j);
            }
            swap(t, u - 1, i);
            int lo, up;
            if (i - l < u - i) { lo = l; up = i - 1; l = i + 1; }
            else { lo = i + 1; up = u; u = i - 1; }
            auxsort(t, lo, up, cmp);
        }
    }

    // From 5.3 the table library goes through the metatable: 'lua_geti',
    // 'lua_seti' and 'luaL_len' respect '__index', '__newindex' and '__len',
    // so a proxy table behaves like the table it stands for.
    private static Object geti(Object t, long i) {
        return Interp.index(t, i, null, null);
    }

    private static void seti(Object t, long i, Object v) {
        Interp.setindex(t, i, v, null, null);
    }

    private static long len(Object t) {
        Object n = Interp.len(t, null);
        Long l = Interp.toInteger(n);
        if (l == null) throw Interp.libError("object length is not an integer");
        return l;
    }

    private static final int TAB_R = 1, TAB_W = 2, TAB_RW = 3;

    /**
     * checktab: a plain table, or one whose metatable supplies exactly the
     * access this function needs -- reading, writing or both.
     */
    private static Object checkTab(Object[] a, int i, int what) {
        Object v = Lib.arg(a, i);
        if (v instanceof LuaTable) return v;
        boolean ok = Interp.metamethod(v, "__len") != null;
        if ((what & TAB_R) != 0) ok = ok && Interp.metamethod(v, "__index") != null;
        if ((what & TAB_W) != 0) ok = ok && Interp.metamethod(v, "__newindex") != null;
        if (ok) return v;
        return Lib.checkTable(a, i);
    }

    private static void appendConcat(ByteArrayOutputStream out, Object v, long i) {
        if (!(v instanceof LuaString || LuaNum.isNum(v))) {
            throw Interp.libError("invalid value (" + Interp.typeName(v)
                    + ") at index " + i + " in table for 'concat'");
        }
        LuaString vs = Interp.toStr(v);
        out.write(vs.b, 0, vs.b.length);
    }
}
