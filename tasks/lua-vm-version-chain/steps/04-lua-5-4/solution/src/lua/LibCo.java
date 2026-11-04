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
            if (!(v instanceof Coroutine)) {
                throw Lib.argError(0, "thread expected, got " + Lib.typeAt(a, 0));
            }
            Object[] args = new Object[Math.max(0, a.length - 1)];
            System.arraycopy(a, 1, args, 0, args.length);
            return Coroutine.resume((Coroutine) v, args);
        });

        Lib.reg(c, "yield", a -> Coroutine.yield(a));

        Lib.reg(c, "status", a -> {
            Object v = Lib.arg(a, 0);
            if (!(v instanceof Coroutine)) throw Lib.argError(0, "thread expected, got " + Lib.typeAt(a, 0));
            return Lib.ret(LuaString.of(((Coroutine) v).statusName()));
        });

        Lib.reg(c, "running", a -> {
            Coroutine cur = Interp.current;
            return Lib.ret(cur, cur.isMain);
        });

        // From 5.4 the coroutine to ask about is an optional argument.
        Lib.reg(c, "isyieldable", a -> {
            Object v = Lib.arg(a, 0);
            Coroutine co;
            if (v == null) co = Interp.current;
            else if (v instanceof Coroutine) co = (Coroutine) v;
            else throw Lib.argError(0, "coroutine expected, got " + Lib.typeAt(a, 0));
            return Lib.ret(!co.isMain
                    && (co != Interp.current || co.nonYieldable == 0));
        });

        Lib.reg(c, "close", a -> {
            Object v = Lib.arg(a, 0);
            if (!(v instanceof Coroutine)) throw Lib.argError(0, "thread expected, got " + Lib.typeAt(a, 0));
            Coroutine co = (Coroutine) v;
            if (co == Interp.current) throw Interp.libError("cannot close a running coroutine");
            if (co.status == Coroutine.NORMAL) {
                throw Interp.libError("cannot close a normal coroutine");
            }
            LuaError e = co.closeAll();
            if (e == null) return Lib.ret(Boolean.TRUE);
            return Lib.ret(Boolean.FALSE, e.value);
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
