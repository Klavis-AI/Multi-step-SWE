package lua;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;

/** The basic library of Lua 5.1. */
final class LibBase {

    static void open(LuaTable g) {
        g.rawset("_G", g);
        g.rawset("_VERSION", LuaString.of("Lua 5.1"));

        Lib.reg(g, "assert", a -> {
            Object v = Lib.checkAny(a, 0);
            if (!Interp.truthy(v)) {
                // luaL_optstring(L, 2, "assertion failed!"): the message must
                // be a string, and nil falls back to the default.
                LuaString m = Lib.optString(a, 1, "assertion failed!");
                throw Interp.libError(m.toString());
            }
            return a;
        });

        Lib.reg(g, "collectgarbage", a -> {
            String opt = Lib.optString(a, 0, "collect").toString();
            switch (opt) {
                case "collect":
                    Gc.collect();
                    return Lib.ret(0.0);
                case "step": {
                    // The collector here always runs a complete cycle, so the
                    // only thing a step size changes is the answer to "did this
                    // step finish one?": the default 0 never does, as it does
                    // not in the reference collector, and any explicit size does.
                    Gc.collect();
                    return Lib.ret(Lib.optNumber(a, 1, 0) != 0 ? Boolean.TRUE : Boolean.FALSE);
                }
                case "count":
                    return Lib.ret((double) Gc.count(), 0.0);
                case "stop": Gc.running = false; return Lib.ret(0.0);
                case "restart": Gc.running = true; return Lib.ret(0.0);
                case "setpause": case "setstepmul": return Lib.ret(200.0);
                default:
                    throw Lib.argError(0, "invalid option '" + opt + "'");
            }
        });

        Lib.reg(g, "error", a -> {
            Object msg = Lib.arg(a, 0);
            long level = Lib.optInt(a, 1, 1);
            if (msg instanceof LuaString && level > 0) {
                msg = LuaString.of(Interp.where((int) level) + msg);
            }
            throw Interp.raise(msg);
        });

        Lib.reg(g, "getfenv", a -> {
            Object v = Lib.arg(a, 0);
            LuaTable env = envOf(v, 1);
            return Lib.ret(env);
        });

        Lib.reg(g, "setfenv", a -> {
            LuaTable t = Lib.checkTable(a, 1);
            Object v = Lib.arg(a, 0);
            if (v instanceof Double) {
                int level = (int) (double) (Double) v;
                if (level == 0) { Interp.current.env = t; return Lib.NONE; }
                Frame f = Interp.frameAt(level);
                if (f == null) throw Lib.argError(0, "invalid level");
                if (f.isC()) throw Interp.libError("'setfenv' cannot change environment of given object");
                f.cl.env = t;
                return Lib.ret(f.cl);
            }
            if (v instanceof LuaClosure) { ((LuaClosure) v).env = t; return Lib.ret(v); }
            if (v instanceof Builtin) throw Interp.libError("'setfenv' cannot change environment of given object");
            throw Lib.typeError(a, 0, "function");
        });

        Lib.reg(g, "getmetatable", a -> {
            Object v = Lib.checkAny(a, 0);
            LuaTable mt = Interp.getmetatable(v);
            if (mt == null) return Lib.ret((Object) null);
            Object p = mt.rawget("__metatable");
            return Lib.ret(p != null ? p : mt);
        });

        Lib.reg(g, "setmetatable", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            Object m = Lib.arg(a, 1);
            if (!(m == null || m instanceof LuaTable)) throw Lib.argError(1, "nil or table expected");
            if (t.metatable != null && t.metatable.rawget("__metatable") != null) {
                throw Interp.libError("cannot change a protected metatable");
            }
            t.metatable = (LuaTable) m;
            if (m != null) {
                if (((LuaTable) m).rawget("__mode") != null) Gc.registerWeak(t);
            }
            return Lib.ret(t);
        });

        Lib.reg(g, "rawequal", a -> {
            Object x = Lib.checkAny(a, 0), y = Lib.checkAny(a, 1);
            return Lib.ret(Interp.rawEquals(x, y));
        });

        Lib.reg(g, "rawget", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            return Lib.ret(t.rawget(Lib.checkAny(a, 1)));
        });

        Lib.reg(g, "rawset", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            Object k = Lib.checkAny(a, 1);
            if (k == null) throw Interp.libError("table index is nil");
            if (k instanceof Double && ((Double) k).isNaN()) throw Interp.libError("table index is NaN");
            t.rawset(k, Lib.arg(a, 2));
            return Lib.ret(t);
        });

        Lib.reg(g, "next", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            Object[] kv = t.next(Lib.arg(a, 1));
            if (kv == null) return Lib.ret((Object) null);
            return kv;
        });

        final Builtin next = (Builtin) g.rawget("next");

        Lib.reg(g, "pairs", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            return Lib.ret(next, t, null);
        });

        Lib.Fn inext = new Lib.Fn("inext", a -> {
            LuaTable t = (LuaTable) a[0];
            int i = (int) (double) (Double) a[1] + 1;
            Object v = t.rawgeti(i);
            if (v == null) return Lib.ret((Object) null);
            return Lib.ret((double) i, v);
        });

        Lib.reg(g, "ipairs", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            return Lib.ret(inext, t, 0.0);
        });

        Lib.reg(g, "type", a -> Lib.ret(LuaString.of(Interp.typeName(Lib.checkAny(a, 0)))));

        Lib.reg(g, "tostring", a -> Lib.ret(Interp.tostring(Lib.checkAny(a, 0))));


        Lib.reg(g, "tonumber", a -> {
            if (a.length >= 2 && a[1] != null) {
                long base = Lib.checkInt(a, 1);
                if (base == 10) {
                    Object v = Lib.checkAny(a, 0);
                    return Lib.ret(Interp.toNumber(v));
                }
                if (base < 2 || base > 36) throw Lib.argError(1, "base out of range");
                // C strtoul: unsigned accumulation, so a negative numeral wraps
                LuaString s = Lib.checkString(a, 0);
                String t = s.toString();
                int i = 0;
                while (i < t.length() && NumFmt.isSpace(t.charAt(i))) i++;
                boolean neg = false;
                if (i < t.length() && (t.charAt(i) == '-' || t.charAt(i) == '+')) {
                    neg = t.charAt(i) == '-';
                    i++;
                }
                int start = i;
                long n = 0;
                while (i < t.length() && Character.digit(t.charAt(i), (int) base) >= 0) {
                    n = n * base + Character.digit(t.charAt(i), (int) base);
                    i++;
                }
                if (i == start) return Lib.ret((Object) null);
                while (i < t.length() && NumFmt.isSpace(t.charAt(i))) i++;
                if (i != t.length()) return Lib.ret((Object) null);
                if (neg) n = -n;
                double d = (n < 0) ? (double) (n >>> 1) * 2.0 + (n & 1) : (double) n;
                return Lib.ret(d);
            }
            Object v = Lib.checkAny(a, 0);
            if (v instanceof Double) return Lib.ret(v);
            if (v instanceof LuaString) return Lib.ret(NumFmt.str2num((LuaString) v));
            return Lib.ret((Object) null);
        });

        Lib.reg(g, "print", a -> {
            StringBuilder sb = new StringBuilder();
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            // luaB_print calls the *global* 'tostring', so replacing it changes
            // what print produces -- and removing it makes print fail.
            Object ts = Interp.globals.rawget("tostring");
            for (int i = 0; i < a.length; i++) {
                if (i > 0) out.write('\t');
                Object r = Interp.first(Interp.call(ts, new Object[]{a[i]}));
                if (!(r instanceof LuaString || r instanceof Double)) {
                    throw Interp.libError("'tostring' must return a string to 'print'");
                }
                LuaString s = Interp.toStr(r);
                out.write(s.b, 0, s.b.length);
            }
            out.write('\n');
            LibIo.writeStdout(out.toByteArray());
            return Lib.NONE;
        });

        Lib.reg(g, "select", a -> {
            Object v = Lib.checkAny(a, 0);
            if (v instanceof LuaString && v.toString().equals("#")) {
                return Lib.ret((double) (a.length - 1));
            }
            long n = Lib.checkInt(a, 0);
            if (n < 0) n = a.length + n;
            else if (n > a.length) n = a.length;
            if (n < 1) throw Lib.argError(0, "index out of range");
            Object[] r = new Object[(int) (a.length - n)];
            System.arraycopy(a, (int) n, r, 0, r.length);
            return r;
        });

        Lib.reg(g, "unpack", a -> {
            LuaTable t = Lib.checkTable(a, 0);
            long i = Lib.optInt(a, 1, 1);
            long j = a.length > 2 && a[2] != null ? Lib.checkInt(a, 2) : t.length();
            if (i > j) return Lib.NONE;
            long n = j - i + 1;
            if (n <= 0 || n > 1000000) throw Interp.libError("too many results to unpack");
            Object[] r = new Object[(int) n];
            for (int k = 0; k < n; k++) r[k] = t.rawgeti((int) (i + k));
            return r;
        });

        Lib.reg(g, "pcall", a -> {
            Object f = Lib.checkAny(a, 0);
            Object[] args = new Object[Math.max(0, a.length - 1)];
            System.arraycopy(a, 1, args, 0, args.length);
            Coroutine co = Interp.current;
            // luaE_checkcstack: a protected call costs a C-stack slot.
            if (++co.nCcalls >= Coroutine.MAXCCALLS) {
                co.nCcalls--;
                throw Interp.runtimeError0("C stack overflow");
            }
            Frame saved = co.top;
            Object savedHandler = co.handler;
            co.handler = null;
            co.nonYieldable++;
            try {
                Object[] r = Interp.call(f, args);
                Object[] out = new Object[r.length + 1];
                out[0] = Boolean.TRUE;
                System.arraycopy(r, 0, out, 1, r.length);
                return out;
            } catch (LuaError e) {
                co.top = saved;
                return Lib.ret(Boolean.FALSE, e.value);
            } catch (StackOverflowError e) {
                co.top = saved;
                return Lib.ret(Boolean.FALSE, LuaString.of("stack overflow"));
            } finally {
                co.nCcalls--;
                co.handler = savedHandler;
                co.nonYieldable--;
            }
        });

        Lib.reg(g, "xpcall", a -> {
            Object f = Lib.checkAny(a, 0);
            Object h = Lib.checkAny(a, 1);
            Coroutine co = Interp.current;
            // luaE_checkcstack: a protected call costs a C-stack slot.
            if (++co.nCcalls >= Coroutine.MAXCCALLS) {
                co.nCcalls--;
                throw Interp.runtimeError0("C stack overflow");
            }
            Frame saved = co.top;
            Object savedHandler = co.handler;
            co.handler = h;
            co.nonYieldable++;
            try {
                Object[] r = Interp.call(f, Interp.EMPTY);
                Object[] out = new Object[r.length + 1];
                out[0] = Boolean.TRUE;
                System.arraycopy(r, 0, out, 1, r.length);
                return out;
            } catch (LuaError e) {
                Object v = e.value;
                if (!e.handled) {
                    co.handler = null;
                    try {
                        v = Interp.first(Interp.call(h, new Object[]{v}));
                    } catch (LuaError e2) {
                        v = LuaString.of("error in error handling");
                    }
                }
                co.top = saved;
                return Lib.ret(Boolean.FALSE, v);
            } catch (StackOverflowError e) {
                co.top = saved;
                co.handler = null;
                Object hr = Interp.first(Interp.call(h, new Object[]{LuaString.of("stack overflow")}));
                return Lib.ret(Boolean.FALSE, hr);
            } finally {
                co.nCcalls--;
                co.handler = savedHandler;
                co.nonYieldable--;
            }
        });

        Lib.reg(g, "loadstring", a -> {
            LuaString s = Lib.checkString(a, 0);
            String name = a.length > 1 && a[1] != null ? Lib.checkString(a, 1).toString() : s.toString();
            return doLoad(s.b, name);
        });

        Lib.reg(g, "load", a -> {
            LuaFunction f = Lib.checkFunction(a, 0);
            String name = a.length > 1 && a[1] != null ? Lib.checkString(a, 1).toString() : "=(load)";
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            try {
            for (;;) {
                Object piece = Interp.first(Interp.call(f, Interp.EMPTY));
                if (piece == null) break;
                if (!(piece instanceof LuaString)) {
                    if (piece instanceof Double) piece = Interp.toStr(piece);
                    else return Lib.ret(null, LuaString.of("reader function must return a string"));
                }
                LuaString ls = (LuaString) piece;
                if (ls.length() == 0) break;
                out.write(ls.b, 0, ls.b.length);
            }
            } catch (LuaError e) {
                return Lib.ret(null, e.value);
            }
            return doLoad(out.toByteArray(), name);
        });

        Lib.reg(g, "loadfile", a -> {
            LuaString fn = Lib.optString(a, 0, null);
            try {
                byte[] data = skipShebang(fn == null ? readAll(System.in) : Files.readAllBytes(Paths.get(fn.toString())));
                return doLoad(data, fn == null ? "=stdin" : "@" + fn);
            } catch (IOException e) {
                return Lib.ret(null, LuaString.of("cannot open " + fn + ": No such file or directory"));
            }
        });

        Lib.reg(g, "dofile", a -> {
            LuaString fn = Lib.optString(a, 0, null);
            byte[] data;
            try {
                data = skipShebang(fn == null ? readAll(System.in) : Files.readAllBytes(Paths.get(fn.toString())));
            } catch (IOException e) {
                throw Interp.libError("cannot open " + fn + ": No such file or directory");
            }
            Object[] r = doLoad(data, fn == null ? "=stdin" : "@" + fn);
            if (r[0] == null) throw new LuaError(r[1]);
            return Interp.call(r[0], Interp.EMPTY);
        });

        Lib.reg(g, "gcinfo", a -> Lib.ret((double) Gc.count()));

        Lib.reg(g, "newproxy", a -> {
            Object v = Lib.arg(a, 0);
            Userdata u = new Userdata(null);
            if (v instanceof Boolean && (Boolean) v) u.metatable = new LuaTable();
            else if (v instanceof Userdata) u.metatable = ((Userdata) v).metatable;
            else if (v != null && v != Boolean.FALSE) throw Lib.argError(0, "boolean or proxy expected");
            if (u.metatable != null) Gc.markFinalizable(u);
            return Lib.ret(u);
        });
    }

    static Object[] doLoad(byte[] data, String name) {
        try {
            LuaClosure c = Parser.load(data, name, displayName(name), Interp.current.env);
            return Lib.ret(c);
        } catch (LuaError e) {
            return Lib.ret(null, e.value);
        }
    }

    /** luaO_chunkid. LUA_IDSIZE (60) for runtime positions, MAXSRC (80) for lexer errors. */
    static String displayName(String n) { return displayName(n, 60); }

    static String displayName(String n, int bufflen) {
        if (n.startsWith("=")) {
            String t = n.substring(1);
            return t.length() > bufflen - 1 ? t.substring(0, bufflen - 1) : t;
        }
        if (n.startsWith("@")) {
            if (n.length() <= bufflen) return n.substring(1);
            return "..." + n.substring(n.length() - bufflen);
        }
        int len = n.length();
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            if (c == 0x0a || c == 0x0d) { len = i; break; }
        }
        int max = bufflen - " [string \"...\"] ".length() - 1;
        boolean trunc = len > max;
        if (trunc) len = max;
        String body = n.substring(0, len);
        if (len < n.length()) return "[string \"" + body + "...\"]";
        return "[string \"" + body + "\"]";
    }

    /**
     * luaL_loadfilex's skipcomment: a leading '#' line is dropped and replaced by a
     * single newline, so that line numbers still match the file.
     */
    static byte[] skipShebang(byte[] data) {
        if (data.length == 0 || data[0] != '#') return data;
        int i = 0;
        while (i < data.length && data[i] != '\n') i++;
        if (i < data.length) i++;
        byte[] r = new byte[data.length - i + 1];
        r[0] = '\n';
        System.arraycopy(data, i, r, 1, data.length - i);
        return r;
    }

    static byte[] readAll(java.io.InputStream in) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }

    static LuaTable envOf(Object v, int defLevel) {
        if (v == null || (v instanceof Double)) {
            int level = v == null ? defLevel : (int) (double) (Double) v;
            if (level < 0) throw Lib.argError(0, "level must be non-negative");
            if (level == 0) return Interp.current.env;
            Frame f = Interp.frameAt(level);
            if (f == null) throw Lib.argError(0, "invalid level");
            return f.isC() ? Interp.globals : f.cl.env;
        }
        if (v instanceof LuaClosure) return ((LuaClosure) v).env;
        if (v instanceof Builtin) return Interp.globals;
        throw Lib.argError(0, "function expected");
    }
}
