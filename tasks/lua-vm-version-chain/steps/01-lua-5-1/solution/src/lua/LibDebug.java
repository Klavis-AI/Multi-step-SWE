package lua;

/** The portable part of the debug library. */
final class LibDebug {
    static void open(LuaTable g) {
        LuaTable d = new LuaTable();
        g.rawset("debug", d);

        Lib.reg(d, "traceback", a -> {
            Object msg = Lib.arg(a, 0);
            if (msg != null && !(msg instanceof LuaString) && !(msg instanceof Double)) return Lib.ret(msg);
            String m = msg == null ? null : Interp.rawToString(msg);
            int level = (int) Lib.optInt(a, 1, 1);
            return Lib.ret(LuaString.of(Interp.traceback(m, level)));
        });

        Lib.reg(d, "getinfo", a -> {
            int i = 0;
            if (Lib.arg(a, 0) instanceof Coroutine) i = 1;
            Object what = Lib.arg(a, i);
            // db_getinfo fills only the fields the option string asks for.
            String opts = Lib.optString(a, i + 1, "flnSu").toString();
            LuaTable r = new LuaTable();
            Frame f = null;
            LuaClosure cl = null;
            Object cfunc = null;
            if (what instanceof Double) {
                int level = (int) (double) (Double) what;
                f = Interp.frameAt(level);
                if (f == null) return Lib.ret((Object) null);
                if (f.isC()) cfunc = Boolean.TRUE;
                else cl = f.cl;
            } else if (what instanceof LuaClosure) {
                cl = (LuaClosure) what;
            } else if (what instanceof Builtin) {
                cfunc = what;
            } else {
                throw Lib.argError(i, "function or level expected");
            }
            boolean isC = (cl == null);
            Proto p = isC ? null : cl.p;
            if (opts.indexOf('S') >= 0) {
                r.rawset("source", LuaString.of(isC ? "=[C]" : p.source));
                r.rawset("short_src", LuaString.of(isC ? "[C]" : p.shortSrc));
                r.rawset("what", LuaString.of(isC ? "C" : (p.lineDefined == 0 ? "main" : "Lua")));
                r.rawset("linedefined", isC ? (Object) (-1.0) : (Object) (double) p.lineDefined);
                r.rawset("lastlinedefined", isC ? (Object) (-1.0) : (Object) (double) p.lastLineDefined);
            }
            if (opts.indexOf('l') >= 0) {
                r.rawset("currentline", (!isC && f != null) ? (Object) (double) f.line : (Object) (-1.0));
            }
            if (opts.indexOf('u') >= 0) {
                r.rawset("nups", isC ? (Object) 0.0 : (Object) (double) cl.upvals.length);
            }
            if (opts.indexOf('n') >= 0) {
                if (f != null && f.callName != null && f.tailcalls == 0) {
                    r.rawset("name", LuaString.of(f.callName));
                    r.rawset("namewhat", LuaString.of("global"));
                } else {
                    r.rawset("namewhat", LuaString.of(""));
                }
            }
            if (opts.indexOf('f') >= 0) {
                r.rawset("func", isC ? (cfunc == Boolean.TRUE ? null : cfunc) : cl);
            }
            if (opts.indexOf('L') >= 0) {
                LuaTable al = new LuaTable();
                if (!isC) {
                    for (int ln = p.lineDefined; ln <= p.lastLineDefined; ln++) al.rawset((double) ln, Boolean.TRUE);
                }
                r.rawset("activelines", al);
            }
            return Lib.ret(r);
        });

        Lib.reg(d, "getmetatable", a -> Lib.ret(Interp.getmetatable(Lib.checkAny(a, 0))));

        Lib.reg(d, "setmetatable", a -> {
            Object v = Lib.checkAny(a, 0);
            Object m = Lib.arg(a, 1);
            Interp.setTypeMeta(v, (LuaTable) m);
            return Lib.ret(v);
        });

        Lib.reg(d, "getfenv", a -> {
            Object v = Lib.checkAny(a, 0);
            if (v instanceof Coroutine) return Lib.ret(((Coroutine) v).env);
            return Lib.ret(LibBase.envOf(v, 1));
        });

        Lib.reg(d, "setfenv", a -> {
            Object v = Lib.checkAny(a, 0);
            LuaTable t = Lib.checkTable(a, 1);
            if (v instanceof LuaClosure) ((LuaClosure) v).env = t;
            else if (v instanceof Coroutine) ((Coroutine) v).env = t;
            return Lib.ret(v);
        });

        Lib.reg(d, "getupvalue", a -> {
            Object f = Lib.checkAny(a, 0);
            int n = (int) Lib.checkInt(a, 1);
            if (!(f instanceof LuaClosure)) return Lib.NONE;
            LuaClosure c = (LuaClosure) f;
            if (n < 1 || n > c.upvals.length) return Lib.NONE;
            return Lib.ret(LuaString.of(c.p.upvals[n - 1].name), c.upvals[n - 1].v);
        });

        Lib.reg(d, "setupvalue", a -> {
            Object f = Lib.checkAny(a, 0);
            int n = (int) Lib.checkInt(a, 1);
            Object v = Lib.arg(a, 2);
            if (!(f instanceof LuaClosure)) return Lib.NONE;
            LuaClosure c = (LuaClosure) f;
            if (n < 1 || n > c.upvals.length) return Lib.NONE;
            c.upvals[n - 1].v = v;
            return Lib.ret(LuaString.of(c.p.upvals[n - 1].name));
        });

        Lib.reg(d, "getlocal", a -> {
            int i = 0;
            if (Lib.arg(a, 0) instanceof Coroutine) i = 1;
            int level = (int) Lib.checkInt(a, i);
            int n = (int) Lib.checkInt(a, i + 1);
            Frame f = Interp.frameAt(level);
            if (f == null) throw Lib.argError(i, "level out of range");
            if (f.isC()) return Lib.ret((Object) null);
            if (n < 1 || n > f.slots.length || f.cl.p.slotNames[n - 1] == null) return Lib.ret((Object) null);
            Object v = f.slots[n - 1];
            if (v instanceof Upvalue) v = ((Upvalue) v).v;
            return Lib.ret(LuaString.of(f.cl.p.slotNames[n - 1]), v);
        });

        Lib.reg(d, "setlocal", a -> {
            int i = 0;
            if (Lib.arg(a, 0) instanceof Coroutine) i = 1;
            int level = (int) Lib.checkInt(a, i);
            int n = (int) Lib.checkInt(a, i + 1);
            Object nv = Lib.arg(a, i + 2);
            Frame f = Interp.frameAt(level);
            if (f == null) throw Lib.argError(i, "level out of range");
            if (f.isC() || n < 1 || n > f.slots.length || f.cl.p.slotNames[n - 1] == null) {
                return Lib.ret((Object) null);
            }
            if (f.slots[n - 1] instanceof Upvalue) ((Upvalue) f.slots[n - 1]).v = nv;
            else f.slots[n - 1] = nv;
            return Lib.ret(LuaString.of(f.cl.p.slotNames[n - 1]));
        });


        Lib.reg(d, "getregistry", a -> Lib.ret(Interp.registry));
        Lib.reg(d, "sethook", a -> Lib.NONE);
        Lib.reg(d, "gethook", a -> Lib.ret((Object) null));
        Lib.reg(d, "debug", a -> Lib.NONE);
    }
}
