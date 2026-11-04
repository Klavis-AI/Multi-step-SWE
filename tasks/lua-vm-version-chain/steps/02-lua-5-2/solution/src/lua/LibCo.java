package lua;

/** The coroutine library of Lua 5.1. */
final class LibCo {
    static void open(LuaTable g) {
        LuaTable c = new LuaTable();
        g.rawset("coroutine", c);

        Lib.reg(c, "create", a -> {
            Object f = Lib.arg(a, 0);
            // A coroutine body may be any function, C ones included.
            if (!(f instanceof LuaFunction)) throw Lib.argError(0, "function expected, got " + Lib.typeAt(a, 0));
            return Lib.ret(new Coroutine((LuaFunction) f));
        });

        Lib.reg(c, "resume", a -> {
            Object v = Lib.arg(a, 0);
            if (!(v instanceof Coroutine)) throw Lib.argError(0, "coroutine expected");
            Object[] args = new Object[Math.max(0, a.length - 1)];
            System.arraycopy(a, 1, args, 0, args.length);
            return Coroutine.resume((Coroutine) v, args);
        });

        Lib.reg(c, "yield", a -> Coroutine.yield(a));

        Lib.reg(c, "status", a -> {
            Object v = Lib.arg(a, 0);
            if (!(v instanceof Coroutine)) throw Lib.argError(0, "coroutine expected");
            return Lib.ret(LuaString.of(((Coroutine) v).statusName()));
        });

        Lib.reg(c, "running", a -> {
            Coroutine cur = Interp.current;
            return Lib.ret(cur, cur.isMain);
        });

        Lib.reg(c, "wrap", a -> {
            Object f = Lib.arg(a, 0);
            // A coroutine body may be any function, C ones included.
            if (!(f instanceof LuaFunction)) throw Lib.argError(0, "function expected, got " + Lib.typeAt(a, 0));
            Coroutine co = new Coroutine((LuaFunction) f);
            return Lib.ret(new Lib.Fn("wrap", b -> {
                Object[] r = Coroutine.resume(co, b);
                if (r[0] == Boolean.FALSE) {
                    Object e = r[1];
                    if (e instanceof LuaString) {
                        throw Interp.libError(e.toString());
                    }
                    throw Interp.raise(e);
                }
                Object[] out = new Object[r.length - 1];
                System.arraycopy(r, 1, out, 0, out.length);
                return out;
            }));
        });
    }
}
