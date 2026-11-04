package lua;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;

/** The basic library of Lua 5.1. */
final class LibBase {

    static final int WARN_OFF = 0, WARN_ON = 1;
    static int warnState = WARN_OFF;
    private static final StringBuilder warnBuf = new StringBuilder();


    static void open(LuaTable g) {
        g.rawset("_G", g);
        g.rawset("_VERSION", LuaString.of("Lua 5.5"));

        Lib.reg(g, "assert", a -> {
            Object v = Lib.checkAny(a, 0);
            if (!Interp.truthy(v)) {
                // luaB_assert hands the message to 'error' at level 1, so a
                // string one gets a position and any other value is raised as is.
                Object msg = (a.length > 1) ? a[1] : LuaString.of("assertion failed!");
                if (msg instanceof LuaString) {
                    throw Interp.raise(LuaString.of(Interp.where(1) + msg));
                }
                throw Interp.raise(msg);
            }
            return a;
        });

        Lib.reg(g, "warn", a -> {
            int n = a.length;
            if (n < 1) throw Lib.argError(0, "string expected, got no value");
            for (int i = 0; i < n; i++) Lib.checkString(a, i);
            for (int i = 0; i < n; i++) {
                warnBuf.append(Lib.checkString(a, i).toString());
                if (i < n - 1) continue;
                String msg = warnBuf.toString();
                warnBuf.setLength(0);
                if (n == 1 && msg.startsWith("@")) {          // a control message
                    if (msg.equals("@on")) warnState = WARN_ON;
                    else if (msg.equals("@off")) warnState = WARN_OFF;
                    return Lib.NONE;
                }
                if (warnState == WARN_ON) LuaMain.warning(msg);
            }
            return Lib.NONE;
        });

        Lib.reg(g, "collectgarbage", a -> {
            String opt = Lib.optString(a, 0, "collect").toString();
            switch (opt) {
                case "collect":
                    // lua_gc refuses while the collector is running, which is
                    // what a call from inside a finaliser sees.
                    if (Gc.collecting) return Lib.ret((Object) null);
                    Gc.collect();
                    return Lib.ret(0L);
                case "step": {
                    // The collector here always runs a complete cycle, so the
                    // only thing a step size changes is the answer to "did this
                    // step finish one?": the default 0 never does, as it does
                    // not in the reference collector, and any explicit size does.
                    Gc.collect();
                    return Lib.ret(Lib.optNumber(a, 1, 0) != 0 ? Boolean.TRUE : Boolean.FALSE);
                }
                case "count":
                    return Lib.ret((double) Gc.count());
                case "stop": Gc.running = false; return Lib.ret(0L);
                case "restart": Gc.running = true; return Lib.ret(0L);
                case "isrunning": return Lib.ret(Gc.running);
                case "generational": case "incremental": {
                    String prev = Gc.mode;
                    Gc.mode = opt;
                    return Lib.ret(LuaString.of(prev));
                }
                case "param": {
                    // 5.5 replaced the per-mode numeric options with a single
                    // "param" option naming the parameter to read or set.
                    String name = Lib.checkString(a, 1).toString();
                    Long cur = Gc.params.get(name);
                    if (cur == null) throw Lib.argError(1, "invalid option '" + name + "'");
                    if (Lib.arg(a, 2) != null) Gc.params.put(name, Lib.checkInt(a, 2));
                    return Lib.ret(cur);
                }
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
            if (!(m == null || m instanceof LuaTable)) {
                throw Lib.argError(1, "nil or table expected, got " + Lib.typeAt(a, 1));
            }
            if (t.metatable != null && t.metatable.rawget("__metatable") != null) {
                throw Interp.libError("cannot change a protected metatable");
            }
            t.metatable = (LuaTable) m;
            if (m != null) {
                if (((LuaTable) m).rawget("__mode") != null) Gc.registerWeak(t);
                // a finaliser is attached when the metatable is set, not later
                if (((LuaTable) m).rawget("__gc") != null) Gc.markFinalizable(t);
            }
            return Lib.ret(t);
        });

        Lib.reg(g, "rawlen", a -> {
            Object v = Lib.arg(a, 0);
            if (v instanceof LuaTable) return Lib.ret((long) ((LuaTable) v).length());
            if (v instanceof LuaString) return Lib.ret((long) ((LuaString) v).length());
            throw Lib.argError(0, "table or string expected, got " + Lib.typeAt(a, 0));
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
            Object v = Lib.checkAny(a, 0);
            Object h = Interp.metamethod(v, "__pairs");
            if (h != null) {
                // 5.5 takes four values, the fourth being the loop's
                // to-be-closed variable.
                Object[] r = Interp.call(h, new Object[]{v});
                Object[] out = new Object[4];
                for (int i = 0; i < 4; i++) out[i] = i < r.length ? r[i] : null;
                return out;
            }
            Lib.checkTable(a, 0);
            return Lib.ret(next, v, null, null);
        });

        Lib.Fn inext = new Lib.Fn("inext", a -> {
            Object t = a[0];
            long i = (Long) a[1] + 1;
            Object v = Interp.index(t, i);   // 5.3: ipairs respects metamethods
            if (v == null) return Lib.ret((Object) null);
            return Lib.ret(i, v);
        });

        Lib.reg(g, "ipairs", a -> {
            Object v = Lib.checkAny(a, 0);
            Lib.checkTable(a, 0);
            return Lib.ret(inext, v, 0L);
        });

        Lib.reg(g, "type", a -> Lib.ret(LuaString.of(Interp.typeName(Lib.checkAny(a, 0)))));

        // luaL_tolstring: '__tostring' has to produce a string.
        Lib.reg(g, "tostring", a -> Lib.ret(Interp.tolstring(Lib.checkAny(a, 0))));


        Lib.reg(g, "tonumber", a -> {
            if (a.length >= 2 && a[1] != null) {
                long base = Lib.checkInt(a, 1);
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
                return Lib.ret(neg ? -n : n);
            }
            Object v = Lib.checkAny(a, 0);
            if (LuaNum.isNum(v)) return Lib.ret(v);
            if (v instanceof LuaString) return Lib.ret(NumFmt.str2num((LuaString) v));
            return Lib.ret((Object) null);
        });

        Lib.reg(g, "print", a -> {
            StringBuilder sb = new StringBuilder();
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            for (int i = 0; i < a.length; i++) {
                if (i > 0) out.write('\t');
                LuaString s = Interp.tostr(a[i]);
                if (s == null) throw Interp.libError("'tostring' must return a string to 'print'");
                out.write(s.b, 0, s.b.length);
            }
            out.write('\n');
            LibIo.writeStdout(out.toByteArray());
            return Lib.NONE;
        });

        Lib.reg(g, "select", a -> {
            Object v = Lib.checkAny(a, 0);
            if (v instanceof LuaString && v.toString().equals("#")) {
                return Lib.ret((long) (a.length - 1));
            }
            long n = Lib.checkInt(a, 0);
            if (n < 0) n = a.length + n;
            else if (n > a.length) n = a.length;
            if (n < 1) throw Lib.argError(0, "index out of range");
            Object[] r = new Object[(int) (a.length - n)];
            System.arraycopy(a, (int) n, r, 0, r.length);
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
            }
        });

        Lib.reg(g, "xpcall", a -> {
            Object f = Lib.checkAny(a, 0);
            Object h = Lib.checkAny(a, 1);
            Object[] xargs = new Object[Math.max(0, a.length - 2)];
            System.arraycopy(a, 2, xargs, 0, xargs.length);
            Coroutine co = Interp.current;
            // luaE_checkcstack: a protected call costs a C-stack slot.
            if (++co.nCcalls >= Coroutine.MAXCCALLS) {
                co.nCcalls--;
                throw Interp.runtimeError0("C stack overflow");
            }
            Frame saved = co.top;
            Object savedHandler = co.handler;
            co.handler = h;
            try {
                Object[] r = Interp.call(f, xargs);
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
            }
        });

        Lib.reg(g, "load", a -> {
            Object chunk = Lib.arg(a, 0);
            Object env = a.length > 3 ? a[3] : Interp.globals;
            String mode = (a.length > 2 && a[2] != null) ? Lib.checkString(a, 2).toString() : "bt";
            if (chunk instanceof LuaString || LuaNum.isNum(chunk)) {
                LuaString ls = Interp.toStr(chunk);
                String name = a.length > 1 && a[1] != null ? Lib.checkString(a, 1).toString()
                                                           : "=(load)";
                if (a.length > 1 && a[1] != null) name = Lib.checkString(a, 1).toString();
                else name = ls.toString();
                Object[] bad = checkMode(ls.b, mode);
                if (bad != null) return bad;
                return doLoad(ls.b, name, env);
            }
            LuaFunction f = Lib.checkFunction(a, 0);
            String name = a.length > 1 && a[1] != null ? Lib.checkString(a, 1).toString()
                                                       : "=(load)";
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            try {
                for (;;) {
                    Object piece = Interp.first(Interp.call(f, Interp.EMPTY));
                    if (piece == null) break;
                    if (!(piece instanceof LuaString)) {
                        if (LuaNum.isNum(piece)) piece = Interp.toStr(piece);
                        else return Lib.ret(null, LuaString.of("reader function must return a string"));
                    }
                    LuaString ls = (LuaString) piece;
                    if (ls.length() == 0) break;
                    out.write(ls.b, 0, ls.b.length);
                }
            } catch (LuaError e) {
                return Lib.ret(null, e.value);
            }
            byte[] data = out.toByteArray();
            Object[] bad = checkMode(data, mode);
            if (bad != null) return bad;
            return doLoad(data, name, env);
        });

        Lib.reg(g, "loadfile", a -> {
            LuaString fn = Lib.optString(a, 0, null);
            String mode = Lib.optString(a, 1, "bt").toString();
            Object env = a.length > 2 ? a[2] : Interp.globals;
            try {
                byte[] data = skipShebang(fn == null ? readAll(System.in) : Files.readAllBytes(Paths.get(fn.toString())));
                Object[] bad = checkMode(data, mode);
                if (bad != null) return bad;
                return doLoad(data, fn == null ? "=stdin" : "@" + fn, env);
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

    }

    /** checkmode: a binary chunk starts with the signature byte, and mode says what is allowed. */
    static Object[] checkMode(byte[] data, String mode) {
        for (int k = 0; k < mode.length(); k++) {
            if ("bt".indexOf(mode.charAt(k)) < 0) throw Lib.argError(2, "invalid mode");
        }
        boolean binary = data.length > 0 && (data[0] & 0xff) == 0x1b;
        String want = binary ? "b" : "t";
        if (mode.indexOf(want.charAt(0)) < 0) {
            return Lib.ret(null, LuaString.of("attempt to load a " + (binary ? "binary" : "text")
                    + " chunk (mode is '" + mode + "')"));
        }
        if (binary) {
            return Lib.ret(null, LuaString.of("attempt to load a binary chunk"));
        }
        return null;
    }

    static Object[] doLoad(byte[] data, String name) { return doLoad(data, name, Interp.globals); }

    static Object[] doLoad(byte[] data, String name, Object env) {
        try {
            LuaClosure c = Parser.load(data, name, displayName(name), env);
            return Lib.ret(c);
        } catch (LuaError e) {
            return Lib.ret(null, e.value);
        }
    }

    /** luaO_chunkid with LUA_IDSIZE = 60, the 5.2 form. */
    static String displayName(String n) { return displayName(n, 60); }

    static String displayName(String n, int bufflen) {
        if (n.startsWith("=")) {
            String t = n.substring(1);
            return t.length() <= bufflen - 1 ? t : t.substring(0, bufflen - 1);
        }
        if (n.startsWith("@")) {
            String t = n.substring(1);
            if (t.length() <= bufflen - 1) return t;
            return "..." + t.substring(t.length() - (bufflen - 1 - 3));
        }
        int nl = -1;
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            if (c == 0x0a || c == 0x0d) { nl = i; break; }
        }
        int room = bufflen - " [string \"...\"] ".length() + 2 - 1;   // 60 - 14 - 1
        int l = n.length();
        if (l < room && nl < 0) return "[string \"" + n + "\"]";
        if (nl >= 0) l = nl;
        if (l > room) l = room;
        return "[string \"" + n.substring(0, l) + "...\"]";
    }

    /**
     * luaL_loadfilex's skipcomment: a leading '#' line is dropped and replaced by a
     * single newline, so that line numbers still match the file.
     */
    static byte[] skipShebang(byte[] data) {
        // skipcomment: a UTF-8 byte-order mark goes first, then a first line
        // that starts with '#' (so a Unix executable script loads).
        if (data.length >= 3 && (data[0] & 0xFF) == 0xEF && (data[1] & 0xFF) == 0xBB
                && (data[2] & 0xFF) == 0xBF) {
            byte[] t = new byte[data.length - 3];
            System.arraycopy(data, 3, t, 0, t.length);
            data = t;
        }
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

}
