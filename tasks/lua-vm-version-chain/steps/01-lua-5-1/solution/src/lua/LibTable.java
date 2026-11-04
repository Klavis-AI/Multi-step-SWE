package lua;

import java.io.ByteArrayOutputStream;

/** The table library of Lua 5.1, including PUC's own quicksort. */
final class LibTable {

    static void open(LuaTable g) {
        LuaTable t = new LuaTable();
        g.rawset("table", t);

        Lib.reg(t, "getn", a -> Lib.ret((double) Lib.checkTable(a, 0).length()));

        Lib.reg(t, "setn", a -> { throw Interp.libError("'setn' is obsolete"); });

        Lib.reg(t, "maxn", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            double max = 0;
            Object[] kv = tt.next(null);
            while (kv != null) {
                Object k = kv[0];
                if (k instanceof Double && (Double) k > max) max = (Double) k;
                else if (k instanceof Integer && (Integer) k > max) max = (Integer) k;
                kv = tt.next(k);
            }
            return Lib.ret(max);
        });

        Lib.reg(t, "insert", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            int n = tt.length();
            switch (a.length) {
                case 2: tt.rawseti(n + 1, a[1]); return Lib.NONE;
                case 3: {
                    long pos = Lib.checkInt(a, 1);
                    long e = n + 1;
                    if (pos > e) e = pos;                    // 5.1 grows the array instead
                    for (long i = e; i > pos; i--) tt.rawseti((int) i, tt.rawgeti((int) i - 1));
                    tt.rawseti((int) pos, a[2]);
                    return Lib.NONE;
                }
                default: throw Interp.libError("wrong number of arguments to 'insert'");
            }
        });

        Lib.reg(t, "remove", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            int n = tt.length();
            long pos = Lib.optInt(a, 1, n);
            if (!(1 <= pos && pos <= n)) return Lib.NONE;     // nothing to remove
            Object v = tt.rawgeti((int) pos);
            for (long i = pos; i < n; i++) tt.rawseti((int) i, tt.rawgeti((int) i + 1));
            tt.rawseti(n, null);
            return Lib.ret(v);
        });

        Lib.reg(t, "concat", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            LuaString sep = Lib.optString(a, 1, "");
            long i = Lib.optInt(a, 2, 1);
            long last = a.length > 3 && a[3] != null ? Lib.checkInt(a, 3) : tt.length();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            for (; i <= last; i++) {
                Object v = tt.rawgeti((int) i);
                if (!(v instanceof LuaString || v instanceof Double)) {
                    throw Interp.libError("invalid value (" + Interp.typeName(v)
                            + ") at index " + i + " in table for 'concat'");
                }
                LuaString vs = Interp.toStr(v);
                out.write(vs.b, 0, vs.b.length);
                if (i != last) out.write(sep.b, 0, sep.b.length);
            }
            return Lib.ret(LuaString.of(out.toByteArray()));
        });

        Lib.reg(t, "foreach", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            Object f = Lib.checkFunction(a, 1);
            Object[] kv = tt.next(null);
            while (kv != null) {
                Object r = Interp.first(Interp.call(f, new Object[]{kv[0], kv[1]}));
                if (r != null) return Lib.ret(r);
                kv = tt.next(kv[0]);
            }
            return Lib.NONE;
        });

        Lib.reg(t, "foreachi", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            Object f = Lib.checkFunction(a, 1);
            int n = tt.length();
            for (int i = 1; i <= n; i++) {
                Object r = Interp.first(Interp.call(f, new Object[]{(double) i, tt.rawgeti(i)}));
                if (r != null) return Lib.ret(r);
            }
            return Lib.NONE;
        });

        Lib.reg(t, "sort", a -> {
            LuaTable tt = Lib.checkTable(a, 0);
            int n = tt.length();
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
        return Interp.truthy(Interp.first(Interp.call(cmp, new Object[]{x, y})));
    }

    private static void swap(LuaTable t, int i, int j) {
        Object a = t.rawgeti(i), b = t.rawgeti(j);
        t.rawseti(i, b);
        t.rawseti(j, a);
    }

    /** ltablib.c auxsort, so that comparison order and error cases match. */
    private static void auxsort(LuaTable t, int l, int u, Object cmp) {
        while (l < u) {
            if (lt(cmp, t.rawgeti(u), t.rawgeti(l))) swap(t, l, u);
            if (u - l == 1) break;
            int i = (l + u) / 2;
            if (lt(cmp, t.rawgeti(i), t.rawgeti(l))) swap(t, i, l);
            else if (lt(cmp, t.rawgeti(u), t.rawgeti(i))) swap(t, i, u);
            if (u - l == 2) break;
            Object p = t.rawgeti(i);
            swap(t, i, u - 1);
            i = l;
            int j = u - 1;
            for (;;) {
                while (lt(cmp, t.rawgeti(++i), p)) {
                    if (i > u) throw Interp.libError("invalid order function for sorting");
                }
                while (lt(cmp, p, t.rawgeti(--j))) {
                    if (j < l) throw Interp.libError("invalid order function for sorting");
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
}
