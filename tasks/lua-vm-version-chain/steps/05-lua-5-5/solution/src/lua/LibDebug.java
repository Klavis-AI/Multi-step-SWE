package lua;

/** The portable part of the debug library. */
final class LibDebug {
    static void open(LuaTable g) {
        LuaTable d = new LuaTable();
        g.rawset("debug", d);

        Lib.reg(d, "traceback", a -> {
            Object msg = Lib.arg(a, 0);
            if (msg != null && !(msg instanceof LuaString) && !LuaNum.isNum(msg)) return Lib.ret(msg);
            String m = msg == null ? null : Interp.rawToString(msg);
            int level = (int) Lib.optInt(a, 1, 1);
            return Lib.ret(LuaString.of(Interp.traceback(m, level)));
        });

        Lib.reg(d, "getinfo", a -> {
            int i = 0;
            if (Lib.arg(a, 0) instanceof Coroutine) i = 1;
            Object what = Lib.arg(a, i);
            // db_getinfo fills only the fields the option string asks for.
            String opts = Lib.optString(a, i + 1, "flnSrtu").toString();
            LuaTable r = new LuaTable();
            Frame f = null;
            LuaClosure cl = null;
            Object cfunc = null;
            if (what instanceof Long) {
                int level = (int) (long) (Long) what;
                f = Interp.frameAt(level);
                if (f == null) return Lib.ret((Object) null);
                if (f.isC()) cfunc = (f.callee != null) ? f.callee : Boolean.TRUE;
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
                r.rawset("linedefined", isC ? -1L : (long) p.lineDefined);
                r.rawset("lastlinedefined", isC ? -1L : (long) p.lastLineDefined);
            }
            if (opts.indexOf('l') >= 0) {
                r.rawset("currentline", (!isC && f != null) ? (long) f.line : -1L);
            }
            if (opts.indexOf('u') >= 0) {
                r.rawset("nups", isC ? 0L : (long) cl.upvals.length);
                r.rawset("nparams", isC ? 0L : (long) p.numParams);
                r.rawset("isvararg", isC ? true : p.isVararg);
            }
            if (opts.indexOf('t') >= 0) {
                r.rawset("istailcall", f != null && f.tailcalls > 0);
                // extraargs: the number of '__call' objects this call went through.
                r.rawset("extraargs", (f != null) ? (long) f.callHops : 0L);
            }
            if (opts.indexOf('r') >= 0) {
                r.rawset("ftransfer", 0L);
                r.rawset("ntransfer", 0L);
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
                    for (int ln = p.lineDefined; ln <= p.lastLineDefined; ln++) al.rawset((long) ln, Boolean.TRUE);
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


        Lib.reg(d, "getuservalue", a -> {
            Object v = Lib.arg(a, 0);
            if (!(v instanceof Userdata)) return Lib.ret((Object) null);
            return Lib.ret(((Userdata) v).uservalue);
        });

        Lib.reg(d, "setuservalue", a -> {
            Object v = Lib.checkAny(a, 0);
            if (!(v instanceof Userdata)) {
                throw Lib.argError(0, "full userdata expected, got " + Interp.typeName(v));
            }
            if (((Userdata) v).light) {
                throw Lib.argError(0, "full userdata expected, got light userdata");
            }
            Object nv = Lib.arg(a, 1);
            if (nv != null && !(nv instanceof LuaTable)) throw Lib.argError(1, "table expected");
            ((Userdata) v).uservalue = (LuaTable) nv;
            return Lib.ret(v);
        });

        Lib.reg(d, "upvalueid", a -> {
            Object f = Lib.checkAny(a, 0);
            int n = (int) Lib.checkInt(a, 1);
            if (!(f instanceof LuaFunction)) throw Lib.argError(0, "function expected, got " + Lib.typeAt(a, 0));
            // From 5.4 an index past the last upvalue is nil, not an error, and
            // a C function simply has none.
            if (!(f instanceof LuaClosure)) return Lib.ret((Object) null);
            LuaClosure c = (LuaClosure) f;
            if (n < 1 || n > c.upvals.length) return Lib.ret((Object) null);
            Userdata u = Interp.upvalIds.get(c.upvals[n - 1]);
            if (u == null) {
                u = new Userdata(c.upvals[n - 1]);
                u.light = true;
                Interp.upvalIds.put(c.upvals[n - 1], u);
            }
            return Lib.ret(u);
        });

        Lib.reg(d, "upvaluejoin", a -> {
            Object f1 = Lib.checkAny(a, 0);
            int n1 = (int) Lib.checkInt(a, 1);
            Object f2 = Lib.checkAny(a, 2);
            int n2 = (int) Lib.checkInt(a, 3);
            if (!(f1 instanceof LuaClosure) || !(f2 instanceof LuaClosure)) {
                throw Lib.argError(0, "Lua function expected");
            }
            LuaClosure c1 = (LuaClosure) f1, c2 = (LuaClosure) f2;
            if (n1 < 1 || n1 > c1.upvals.length) throw Lib.argError(1, "invalid upvalue index");
            if (n2 < 1 || n2 > c2.upvals.length) throw Lib.argError(3, "invalid upvalue index");
            c1.upvals[n1 - 1] = c2.upvals[n2 - 1];
            return Lib.NONE;
        });

        Lib.reg(d, "getregistry", a -> Lib.ret(Interp.registry));
        Lib.reg(d, "sethook", a -> Lib.NONE);
        Lib.reg(d, "gethook", a -> Lib.ret((Object) null));
        Lib.reg(d, "debug", a -> Lib.NONE);
    }
}
