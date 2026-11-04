package lua;

import java.util.ArrayList;

/** Runtime operations: metamethod dispatch, arithmetic, comparison, calls, errors. */
final class Interp {
    static final Object[] EMPTY = new Object[0];

    static LuaTable globals = new LuaTable();
    static Upvalue envUpvalue;                // the _ENV of every chunk loaded here
    static LuaTable stringMeta;
    static LuaTable numberMeta, booleanMeta, nilMeta, functionMeta, threadMeta;
    static LuaTable registry = new LuaTable();
    static Coroutine main;
    static Coroutine current;
    static boolean pendingMethod;
    static final java.util.IdentityHashMap<Upvalue, Userdata> upvalIds = new java.util.IdentityHashMap<>();
    /** module-qualified names of the standard functions, as package.loaded holds them */
    static final java.util.IdentityHashMap<Object, String> globalNames = new java.util.IdentityHashMap<>();
    static String pendingName;

    // ------------------------------------------------------------ type names

    static String typeName(Object v) {
        if (v == null) return "nil";
        if (v instanceof Boolean) return "boolean";
        if (v instanceof Double) return "number";
        if (v instanceof LuaString) return "string";
        if (v instanceof LuaTable) return "table";
        if (v instanceof LuaFunction) return "function";
        if (v instanceof Coroutine) return "thread";
        if (v instanceof Userdata) return "userdata";
        return "userdata";
    }

    static boolean truthy(Object v) {
        return v != null && v != Boolean.FALSE;
    }

    // ---------------------------------------------------------------- errors

    static LuaError syntaxError(String msg) {
        return new LuaError(LuaString.of(msg));
    }

    /** The activation record `level` steps below the running one (0 = running). */
    static Frame frameAt(int level) {
        Frame f = current == null ? null : current.top;
        for (int i = 0; i < level && f != null; i++) f = f.prev;
        return f;
    }

    /** luaL_where: "chunk:line: ", empty for a library frame or past the bottom. */
    static String where(int level) {
        Frame f = frameAt(level);
        if (f == null || f.isC()) return "";
        return f.cl.p.shortSrc + ":" + f.line + ": ";
    }

    /** An error raised by the virtual machine: positioned at the running Lua function. */
    static LuaError runtimeError(String msg) {
        return raise(LuaString.of(where(0) + msg));
    }

    /** An error raised by a library function: positioned at its caller. */
    static LuaError libError(String msg) {
        return raise(LuaString.of(where(1) + msg));
    }

    static LuaError runtimeError0(String msg) {
        return raise(LuaString.of(msg));
    }

    /**
     * Builds the error object, running the active message handler first so that it
     * still sees the stack that raised the error, as luaG_errormsg does.
     */
    static LuaError raise(Object v) {
        Coroutine co = current;
        if (co != null && co.handler != null) {
            Object h = co.handler;
            // luaE_checkcstack: running the handler costs a C-stack slot. At
            // the limit the error becomes a stack overflow, which still goes
            // through the handler; far enough past it, handling has itself
            // failed and there is nothing left to report but that.
            co.nCcalls++;
            if (co.nCcalls >= Coroutine.MAXCCALLS / 10 * 11) {
                co.nCcalls--;
                LuaError e = new LuaError(LuaString.of("error in error handling"));
                e.handled = true;
                return e;
            }
            if (co.nCcalls >= Coroutine.MAXCCALLS) v = LuaString.of("C stack overflow");
            try {
                Object r = first(call(h, new Object[]{v}));
                LuaError e = new LuaError(r);
                e.handled = true;
                return e;
            } catch (LuaError inner) {
                // The handler raised: its own error has already been through
                // this path, so what it produced is the result.
                return inner;
            } catch (StackOverflowError inner) {
                LuaError e = new LuaError(LuaString.of("error in error handling"));
                e.handled = true;
                return e;
            } finally {
                co.nCcalls--;
            }
        }
        return new LuaError(v);
    }

    static LuaError argError(String fname, int n, String msg) {
        return libError("bad argument #" + n + " to '" + fname + "' (" + msg + ")");
    }

    static LuaError typeError(String op, Object v, String kind, String name) {
        String t = typeName(v);
        if (kind != null) {
            return runtimeError("attempt to " + op + " " + kind + " '" + name + "' (a " + t + " value)");
        }
        return runtimeError("attempt to " + op + " a " + t + " value");
    }

    // ----------------------------------------------------------- metatables

    static LuaTable getmetatable(Object v) {
        if (v instanceof LuaTable) return ((LuaTable) v).metatable;
        if (v instanceof LuaString) return stringMeta;
        if (v instanceof Userdata) return ((Userdata) v).metatable;
        if (v instanceof Double) return numberMeta;
        if (v instanceof Boolean) return booleanMeta;
        if (v == null) return nilMeta;
        if (v instanceof LuaFunction) return functionMeta;
        if (v instanceof Coroutine) return threadMeta;
        return null;
    }

    static void setTypeMeta(Object v, LuaTable mt) {
        if (v instanceof LuaTable) ((LuaTable) v).metatable = mt;
        else if (v instanceof Userdata) ((Userdata) v).metatable = mt;
        else if (v instanceof Double) numberMeta = mt;
        else if (v instanceof Boolean) booleanMeta = mt;
        else if (v == null) nilMeta = mt;
        else if (v instanceof LuaFunction) functionMeta = mt;
        else if (v instanceof Coroutine) threadMeta = mt;
    }

    static Object metamethod(Object v, String event) {
        LuaTable mt = getmetatable(v);
        if (mt == null) return null;
        return mt.rawget(event);
    }

    // ---------------------------------------------------------------- index

    static Object index(Object t, Object k, String kind, String name) {
        for (int loop = 0; loop < 100; loop++) {
            Object h;
            if (t instanceof LuaTable) {
                Object v = ((LuaTable) t).rawget(k);
                if (v != null) return v;
                LuaTable mt = ((LuaTable) t).metatable;
                if (mt == null) return null;
                h = mt.rawget("__index");
                if (h == null) return null;
            } else {
                h = metamethod(t, "__index");
                if (h == null) throw typeError("index", t, kind, name);
            }
            if (h instanceof LuaFunction) return first(call(h, new Object[]{t, k}));
            t = h;
            kind = null; name = null;
        }
        throw runtimeError("'__index' chain too long; possible loop");
    }

    static Object index(Object t, Object k) { return index(t, k, null, null); }

    static void setindex(Object t, Object k, Object v, String kind, String name) {
        for (int loop = 0; loop < 100; loop++) {
            Object h;
            if (t instanceof LuaTable) {
                LuaTable tt = (LuaTable) t;
                Object old = tt.rawget(k);
                if (old != null || tt.metatable == null
                        || (h = tt.metatable.rawget("__newindex")) == null) {
                    if (k == null) throw runtimeError("table index is nil");
                    if (k instanceof Double && ((Double) k).isNaN()) throw runtimeError("table index is NaN");
                    tt.rawset(k, v);
                    return;
                }
            } else {
                h = metamethod(t, "__newindex");
                if (h == null) throw typeError("index", t, kind, name);
            }
            if (h instanceof LuaFunction) { call(h, new Object[]{t, k, v}); return; }
            t = h;
            kind = null; name = null;
        }
        throw runtimeError("'__newindex' chain too long; possible loop");
    }

    // ----------------------------------------------------------------- calls

    static Object[] call(Object f, Object[] args) {
        return call(f, args, null, null);
    }

    /** A call from a source-level call expression: a coroutine may yield through it. */
    static Object[] callDirect(Object f, Object[] args, String kind, String name) {
        if (f instanceof LuaFunction) {
            pendingMethod = "method".equals(kind);
            pendingName = name;
            return ((LuaFunction) f).call(args);
        }
        // tryfuncTM: before 5.4 the '__call' value itself had to be a
        // function, so a chain of callable tables is not callable.
        Object h = metamethod(f, "__call");
        if (!(h instanceof LuaFunction)) throw typeError("call", f, kind, name);
        Object[] a2 = new Object[args.length + 1];
        a2[0] = f;
        System.arraycopy(args, 0, a2, 1, args.length);
        return callDirect(h, a2, null, name);
    }

    /**
     * Every other call — metamethods, for-loop iterators, library callbacks — goes
     * through a C frame, which in Lua 5.1 is a barrier for coroutine.yield.
     */
    static Object[] call(Object f, Object[] args, String kind, String name) {
        return callDirect(f, args, kind, name);
    }

    /** A call a coroutine may not yield through: a sort comparator, a gsub handler. */
    static Object[] callBarrier(Object f, Object[] args) {
        Coroutine co = current;
        co.nonYieldable++;
        try {
            return callDirect(f, args, null, null);
        } finally {
            co.nonYieldable--;
        }
    }

    static Object[] callNoBarrier(Object f, Object[] args) {
        return callDirect(f, args, null, null);
    }

    static Object first(Object[] r) { return r.length == 0 ? null : r[0]; }

    static Object[] callClosure(LuaClosure clArg, Object[] args) {
        LuaClosure cl = clArg;
        Proto p = cl.p;
        Frame f = new Frame(cl);
        int np = p.numParams;
        for (int i = 0; i < np; i++) {
            Object v = i < args.length ? args[i] : null;
            f.slots[i] = p.boxed[i] ? new Upvalue(v) : v;
        }
        if (p.isVararg && args.length > np) {
            Object[] va = new Object[args.length - np];
            System.arraycopy(args, np, va, 0, va.length);
            f.varargs = va;
        }
        if (p.needsArg && p.argSlot >= 0) {
            LuaTable at = new LuaTable(f.varargs.length, 1);
            for (int i = 0; i < f.varargs.length; i++) at.rawseti(i + 1, f.varargs[i]);
            at.rawset("n", (double) f.varargs.length);
            f.slots[p.argSlot] = p.boxed[p.argSlot] ? new Upvalue(at) : at;
        }
        Coroutine co = current;
        f.callName = pendingName;
        pendingName = null;
        f.prev = co.top;
        co.top = f;
        co.depth++;
        if (co.depth > 200000) { co.top = f.prev; co.depth--; throw runtimeError("stack overflow"); }
        try {
            for (;;) {
                int sig = p.body.run(f);
                if (sig == Stat.RETURN && f.hasTail) {
                    Object func = f.tailFunc;
                    Object[] targs = f.tailArgs;
                    String tname = f.tailName;
                    String tkind = f.tailKind;
                    f.hasTail = false;
                    f.tailFunc = null;
                    f.tailArgs = null;
                    while (!(func instanceof LuaFunction)) {
                        Object h = metamethod(func, "__call");
                        if (!(h instanceof LuaFunction)) throw typeError("call", func, tkind, tname);
                        Object[] a2 = new Object[targs.length + 1];
                        a2[0] = func;
                        System.arraycopy(targs, 0, a2, 1, targs.length);
                        func = h;
                        targs = a2;
                    }
                    if (!(func instanceof LuaClosure)) {
                        pendingName = tname;
                        pendingMethod = "method".equals(tkind);
                        return ((LuaFunction) func).call(targs);
                    }
                    cl = (LuaClosure) func;
                    p = cl.p;
                    f.tailcalls++;
                    f.cl = cl;
                    f.callName = tname;
                    f.slots = new Object[p.numSlots];
                    f.varargs = EMPTY;
                    f.ret = null;
                    f.temps = null;
                    int np2 = p.numParams;
                    for (int i = 0; i < np2; i++) {
                        Object v = i < targs.length ? targs[i] : null;
                        f.slots[i] = p.boxed[i] ? new Upvalue(v) : v;
                    }
                    if (p.isVararg && targs.length > np2) {
                        Object[] va = new Object[targs.length - np2];
                        System.arraycopy(targs, np2, va, 0, va.length);
                        f.varargs = va;
                    }
                    if (p.needsArg && p.argSlot >= 0) {
                        LuaTable at = new LuaTable(f.varargs.length, 1);
                        for (int i = 0; i < f.varargs.length; i++) at.rawseti(i + 1, f.varargs[i]);
                        at.rawset("n", (double) f.varargs.length);
                        f.slots[p.argSlot] = p.boxed[p.argSlot] ? new Upvalue(at) : at;
                    }
                    continue;
                }
                return sig == Stat.RETURN && f.ret != null ? f.ret : EMPTY;
            }
        } catch (StackOverflowError e) {
            co.top = f;
            throw runtimeError("stack overflow");
        } finally {
            co.top = f.prev;
            co.depth--;
        }
    }

    // ------------------------------------------------------------ arithmetic

    static Double toNumber(Object v) {
        if (v instanceof Double) return (Double) v;
        if (v instanceof LuaString) return NumFmt.str2num((LuaString) v);
        return null;
    }

    static double checkArith(Object v, Object other, String event) {
        Double d = toNumber(v);
        if (d != null) return d;
        return Double.NaN;
    }

    static Object arith(String op, String event, Object a, Object b,
                        Expr ea, Expr eb) {
        Double x = toNumber(a), y = toNumber(b);
        if (x != null && y != null) return rawArith(op, x, y);
        Object h = metamethod(a, event);
        if (h == null) h = metamethod(b, event);
        if (h != null) return first(call(h, new Object[]{a, b}));
        Object bad = (x == null) ? a : b;
        Expr be = (x == null) ? ea : eb;
        throw typeError("perform arithmetic on", bad, be == null ? null : be.kind(), be == null ? null : be.kindName());
    }

    static Object rawArith(String op, double x, double y) {
        switch (op) {
            case "+": return x + y;
            case "-": return x - y;
            case "*": return x * y;
            case "/": return x / y;
            case "%": return luaMod(x, y);
            case "^": return Math.pow(x, y);
            default: throw new IllegalStateException(op);
        }
    }

    /** luai_nummod of 5.1 and 5.2: a - floor(a/b)*b. */
    static double luaMod(double x, double y) {
        return x - Math.floor(x / y) * y;
    }

    static Object unm(Object a, Expr ea) {
        Double x = toNumber(a);
        if (x != null) return -x;
        Object h = metamethod(a, "__unm");
        if (h != null) return first(call(h, new Object[]{a, a}));
        throw typeError("perform arithmetic on", a, ea == null ? null : ea.kind(), ea == null ? null : ea.kindName());
    }

    static Object len(Object a, Expr ea) {
        if (a instanceof LuaString) return (double) ((LuaString) a).length();
        if (a instanceof LuaTable) {
            Object h = metamethod(a, "__len");
            if (h != null) return first(call(h, new Object[]{a}));
            return (double) ((LuaTable) a).length();
        }
        Object h = metamethod(a, "__len");
        if (h != null) return first(call(h, new Object[]{a}));
        throw typeError("get length of", a, ea == null ? null : ea.kind(), ea == null ? null : ea.kindName());
    }

    static Object concat(Object a, Object b, Expr ea, Expr eb) {
        if ((a instanceof LuaString || a instanceof Double)
                && (b instanceof LuaString || b instanceof Double)) {
            return toStr(a).concat(toStr(b));
        }
        Object h = metamethod(a, "__concat");
        if (h == null) h = metamethod(b, "__concat");
        if (h != null) return first(call(h, new Object[]{a, b}));
        boolean aBad = !(a instanceof LuaString || a instanceof Double);
        Object bad = aBad ? a : b;
        Expr be = aBad ? ea : eb;
        throw typeError("concatenate", bad, be == null ? null : be.kind(), be == null ? null : be.kindName());
    }

    static LuaString toStr(Object v) {
        if (v instanceof LuaString) return (LuaString) v;
        return LuaString.of(NumFmt.numToStr((Double) v));
    }

    // ----------------------------------------------------------- comparison

    static boolean rawEquals(Object a, Object b) {
        if (a == null) return b == null;
        if (a instanceof Double && b instanceof Double) {
            return ((Double) a).doubleValue() == ((Double) b).doubleValue();
        }
        return a.equals(b);
    }

    static boolean equals(Object a, Object b) {
        if (a == null || b == null) return a == b;
        if (a instanceof Double || b instanceof Double) {
            if (!(a instanceof Double && b instanceof Double)) return false;
            return ((Double) a).doubleValue() == ((Double) b).doubleValue();
        }
        if (a.equals(b)) return true;
        if (a.getClass() != b.getClass()) {
            if (!(a instanceof LuaTable && b instanceof LuaTable)
                    && !(a instanceof Userdata && b instanceof Userdata)) return false;
        }
        if (!((a instanceof LuaTable && b instanceof LuaTable)
                || (a instanceof Userdata && b instanceof Userdata))) return false;
        Object h = sameMeta(a, b, "__eq");
        if (h == null) return false;
        return truthy(first(call(h, new Object[]{a, b})));
    }

    static boolean lessThan(Object a, Object b) {
        if (a instanceof Double && b instanceof Double) return (Double) a < (Double) b;
        if (a instanceof LuaString && b instanceof LuaString) {
            return ((LuaString) a).compareTo((LuaString) b) < 0;
        }
        Object h = metamethod(a, "__lt");
        if (h == null) h = metamethod(b, "__lt");
        if (h != null) return truthy(first(call(h, new Object[]{a, b})));
        throw cmpError(a, b);
    }

    static boolean lessEq(Object a, Object b) {
        if (a instanceof Double && b instanceof Double) return (Double) a <= (Double) b;
        if (a instanceof LuaString && b instanceof LuaString) {
            return ((LuaString) a).compareTo((LuaString) b) <= 0;
        }
        Object h = metamethod(a, "__le");
        if (h == null) h = metamethod(b, "__le");
        if (h != null) return truthy(first(call(h, new Object[]{a, b})));
        h = metamethod(a, "__lt");
        if (h == null) h = metamethod(b, "__lt");
        if (h != null) return !truthy(first(call(h, new Object[]{b, a})));
        throw cmpError(a, b);
    }

    /** Lua 5.1 uses an order or equality metamethod only when both operands share it. */
    static Object sameMeta(Object a, Object b, String event) {
        Object h1 = metamethod(a, event);
        if (h1 == null) return null;
        Object h2 = metamethod(b, event);
        if (h2 == null) return null;
        return rawEquals(h1, h2) ? h1 : null;
    }

    static LuaError cmpError(Object a, Object b) {
        String ta = typeName(a), tb = typeName(b);
        if (ta.equals(tb)) return runtimeError("attempt to compare two " + ta + " values");
        return runtimeError("attempt to compare " + ta + " with " + tb);
    }

    // ------------------------------------------------------------- tostring

    static Object tostring(Object v) {
        Object h = metamethod(v, "__tostring");
        if (h != null) return first(call(h, new Object[]{v}));
        return LuaString.of(rawToString(v));
    }

    /**
     * luaL_tolstring.  5.2 does not check what '__tostring' returned, so a
     * non-string leaves lua_tolstring returning NULL; null stands for that
     * here.
     */
    static LuaString tolstring(Object v) {
        Object h = metamethod(v, "__tostring");
        if (h != null) {
            Object r = first(call(h, new Object[]{v}));
            if (r instanceof LuaString) return (LuaString) r;
            if (r instanceof Double) return toStr(r);
            return null;
        }
        return LuaString.of(rawToString(v));
    }

    /** tostring coerced to a string, or null when the metamethod produced something else. */
    static LuaString tostr(Object v) {
        Object r = tostring(v);
        if (r instanceof LuaString) return (LuaString) r;
        if (r instanceof Double) return toStr(r);
        return null;
    }

    static String rawToString(Object v) {
        if (v == null) return "nil";
        if (v instanceof Boolean) return ((Boolean) v) ? "true" : "false";
        if (v instanceof Double) return NumFmt.numToStr((Double) v);
        if (v instanceof LuaString) return v.toString();
        return typeName(v) + ": 0x" + String.format("%014x", addressOf(v));
    }

    private static final java.util.IdentityHashMap<Object, Long> ADDR = new java.util.IdentityHashMap<>();
    private static long nextAddr = 0x55a0000000L;

    static synchronized long addressOf(Object v) {
        Long a = ADDR.get(v);
        if (a == null) { a = (nextAddr += 0x30); ADDR.put(v, a); }
        return a;
    }

    // ------------------------------------------------------------- traceback

    static final int LEVELS1 = 10, LEVELS2 = 11;
    static final Frame HOST = new Frame("?");

    static String traceback(String msg, int level) {
        StringBuilder sb = new StringBuilder();
        if (msg != null) sb.append(msg).append("\n");
        sb.append("stack traceback:");
        java.util.ArrayList<Frame> fs = new java.util.ArrayList<>();
        for (Frame f = current.top; f != null; f = f.prev) fs.add(f);
        fs.add(HOST);                     // the C entry point, as lua_getstack reports it
        int last = fs.size() - 1;
        int n1 = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
        int i = level;
        while (i <= last) {
            if (n1-- == 0) {
                sb.append("\n\t...");
                i = last - LEVELS2 + 1;
                continue;
            }
            Frame f = fs.get(i);
            sb.append("\n\t");
            if (f.isC()) {
                sb.append("[C]: in ");
                String q = (f.callName != null) ? f.callName
                        : (f.callee != null ? globalNames.get(f.callee) : null);
                if (q != null) sb.append("function '").append(q).append("'");
                else sb.append("?");
            } else {
                sb.append(f.cl.p.shortSrc).append(":").append(f.line).append(": in ");
                if (f.callName != null && f.tailcalls == 0) {
                    sb.append("function '").append(f.callName).append("'");
                } else if (f.cl.p.lineDefined == 0) {
                    sb.append("main chunk");
                } else {
                    sb.append("function <").append(f.cl.p.shortSrc).append(":")
                            .append(f.cl.p.lineDefined).append(">");
                }
                if (f.tailcalls > 0) sb.append("\n\t(...tail calls...)");
            }
            i++;
        }
        return sb.toString();
    }
}
