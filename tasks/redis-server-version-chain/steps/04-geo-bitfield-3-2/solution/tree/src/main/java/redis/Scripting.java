package redis;

import java.io.ByteArrayInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;

import org.luaj.vm2.Globals;
import org.luaj.vm2.LuaError;
import org.luaj.vm2.LuaNumber;
import org.luaj.vm2.LuaString;
import org.luaj.vm2.LuaTable;
import org.luaj.vm2.LuaValue;
import org.luaj.vm2.Varargs;
import org.luaj.vm2.compiler.LuaC;
import org.luaj.vm2.lib.CoroutineLib;
import org.luaj.vm2.lib.DebugLib;
import org.luaj.vm2.lib.OneArgFunction;
import org.luaj.vm2.lib.PackageLib;
import org.luaj.vm2.lib.StringLib;
import org.luaj.vm2.lib.TableLib;
import org.luaj.vm2.lib.ThreeArgFunction;
import org.luaj.vm2.lib.TwoArgFunction;
import org.luaj.vm2.lib.VarArgFunction;
import org.luaj.vm2.lib.ZeroArgFunction;
import org.luaj.vm2.lib.jse.JseBaseLib;
import org.luaj.vm2.lib.jse.JseMathLib;

/**
 * EVAL/EVALSHA/FUNCTION host.
 *
 * Port of src/script_lua.c, src/eval.c, src/function_lua.c, src/functions.c and
 * src/script.c from redis 8.0.6 on top of LuaJ 3.0.1.  LuaJ is Lua 5.2 flavoured
 * so the 5.1 compatibility shims (unpack, loadstring, table.getn, math.mod, ...)
 * are installed explicitly, and the cjson / struct / bit / cmsgpack libraries
 * redis ships in deps/lua are reimplemented here in Java.
 */
public final class Scripting {

    /** server.c createSharedObjects(): shared.noscripterr */
    public static final String NOSCRIPT_ERR = "NOSCRIPT No matching script. Please use EVAL.";

    /* script.h scripts_flags_def */
    public static final int SF_NO_WRITES = 1 << 0;
    public static final int SF_ALLOW_OOM = 1 << 1;
    public static final int SF_ALLOW_STALE = 1 << 2;
    public static final int SF_NO_CLUSTER = 1 << 3;
    public static final int SF_ALLOW_CROSS_SLOT = 1 << 4;
    public static final int SF_EVAL_COMPAT_MODE = 1 << 5;

    public static final int PROPAGATE_NONE = 0;
    public static final int PROPAGATE_AOF = 1;
    public static final int PROPAGATE_REPL = 2;

    /** how deep a returned Lua table may nest before we bail out like redis does */
    static final int MAX_REPLY_DEPTH = 600;
    /** functions.c LOAD_TIMEOUT_MS */
    static final long LOAD_TIMEOUT_MS = 500;

    public final Server s;

    public Scripting(Server s) { this.s = s; }

    /** DEBUG SET-DISABLE-DENY-SCRIPTS, mirrors server.script_disable_deny_script */
    public boolean disableDenyScript = false;

    /** script.c SCRIPT_TIMEDOUT: the running script overran busy-reply-threshold */
    public boolean timedout = false;
    /** script.c SCRIPT_KILLED: SCRIPT KILL / FUNCTION KILL asked for this script to die */
    public boolean killRequested = false;
    private boolean killLogged = false;
    /** throttles processEventsWhileBlocked() to at most one pass per millisecond */
    private long lastYield = 0;
    /** server.busy_reply_threshold, sampled once per run so the hook stays cheap */
    private long busyLimit = 0;
    /** the instruction hook armed for the duration of every EVAL/FCALL */
    TimeHook timeHook;

    // ------------------------------------------------------------------
    // script cache (eval.c lctx.lua_scripts): sha1 hex -> body
    // ------------------------------------------------------------------
    public final LinkedHashMap<String, byte[]> scripts = new LinkedHashMap<String, byte[]>();
    final LinkedHashMap<String, LuaValue> compiled = new LinkedHashMap<String, LuaValue>();
    final LinkedHashMap<String, Integer> scriptFlagsBySha = new LinkedHashMap<String, Integer>();
    /** eval.c lctx.lua_scripts_lru_list: EVAL-created shas, oldest first */
    final LinkedHashSet<String> scriptsLru = new LinkedHashSet<String>();
    /** server.stat_evictedscripts */
    public long statEvictedScripts = 0;
    /** eval.c LRU_LIST_LENGTH */
    static final int LRU_LIST_LENGTH = 500;

    // ------------------------------------------------------------------
    // functions (functions.c)
    // ------------------------------------------------------------------
    public static final class FuncInfo {
        public String name;
        public byte[] desc;
        public LuaValue callback;
        public int flags;
        public LibInfo lib;
    }

    public static final class LibInfo {
        public String name;
        public String engine = "LUA";
        public byte[] code;
        public final LinkedHashMap<String, FuncInfo> funcs = new LinkedHashMap<String, FuncInfo>();
    }

    /** library name (librariesDictType is case sensitive) */
    public final LinkedHashMap<String, LibInfo> libraries = new LinkedHashMap<String, LibInfo>();
    /** function name, lowercased (functionDictType is case insensitive) */
    public final LinkedHashMap<String, FuncInfo> functions = new LinkedHashMap<String, FuncInfo>();

    // ------------------------------------------------------------------
    // run context (script.c scriptRunCtx)
    // ------------------------------------------------------------------
    static final class RunCtx {
        Client caller;
        Client fake;
        String funcname = "";
        boolean evalMode;
        boolean readOnly;
        boolean writeDirty;
        int replFlags = PROPAGATE_AOF | PROPAGATE_REPL;
        long start;
        long dirtyStart;
        /**
         * scripting.c server.lua_replicate_commands, per run.
         *
         * At 3.2 whole-script replication is still the default and a script opts
         * into effects replication with redis.replicate_commands(); the switch is
         * refused once the script has written, and redis.set_repl() is an error
         * until it has been taken. Later releases made effects replication
         * unconditional and both rules went away, which is why only this rung's
         * copy of the tree carries them.
         */
        boolean replicateCommands;
    }

    RunCtx cur = null;

    public boolean isRunning() { return cur != null; }
    public boolean isEval() { return cur != null && cur.evalMode; }
    /** script.c SCRIPT_WRITE_DIRTY: the script already wrote, so it is unkillable */
    public boolean currWriteDirty() { return cur != null && cur.writeDirty; }
    public String currFunction() { return cur == null ? "" : cur.funcname; }
    public Client currCaller() { return cur == null ? null : cur.caller; }
    public long runDurationMs() { return cur == null ? 0 : (System.currentTimeMillis() - cur.start); }

    // ==================================================================
    // Lua state
    // ==================================================================
    Globals G;
    REnv evalEnv;       // globals of EVAL scripts (holds KEYS/ARGV)
    RTable fnGlobals;   // globals a registered function sees at call time
    REnv fnEnv;         // _ENV of library chunks and function callbacks
    RTable fnMeta;      // metatable of fnEnv, __index flips during FUNCTION LOAD
    RTable libApi;      // globals visible while a library body runs

    RTable evalRedis, fnRedis, libRedis;
    LuaTable stringLib, tableLib, mathLib, coroutineLib;
    RTable osLib;
    LuaValue unpackFn;
    LuaValue jsonNull;

    LibInfo loadingLib = null;
    long loadDeadline = 0;
    /** the instruction hook, only armed while a FUNCTION LOAD body runs */
    LoadHook hook;

    // cjson configuration (lua_cjson.c json_config_t)
    int jsonEncodeMaxDepth = 1000;
    int jsonDecodeMaxDepth = 1000;
    int jsonEncodeInvalidNumbers = 0;   // 0 = error, 1 = allow, 2 = as null
    int jsonDecodeInvalidNumbers = 1;
    boolean jsonKeepBuffer = true;
    int jsonPrecision = 14;
    int jsonSparseConvert = 0, jsonSparseRatio = 2, jsonSparseSafe = 10;

    // ------------------------------------------------------------------
    /** A LuaTable that refuses every mutation once locked. */
    static final class RTable extends LuaTable {
        boolean locked;

        void lock() { locked = true; }

        private void chk() {
            if (locked) throw new LuaError("Attempt to modify a readonly table");
        }

        @Override public void set(LuaValue key, LuaValue value) { chk(); super.set(key, value); }
        @Override public void set(int key, LuaValue value) { chk(); super.set(key, value); }
        @Override public void rawset(LuaValue key, LuaValue value) { chk(); super.rawset(key, value); }
        @Override public void rawset(int key, LuaValue value) { chk(); super.rawset(key, value); }
        @Override public LuaValue setmetatable(LuaValue mt) { chk(); return super.setmetatable(mt); }
        @Override public LuaValue remove(int pos) { chk(); return super.remove(pos); }
        @Override public void insert(int pos, LuaValue v) { chk(); super.insert(pos, v); }

        /** internal store, bypasses the lock */
        void force(String k, LuaValue v) { super.rawset(LuaValue.valueOf(k), v); }
        void force(LuaValue k, LuaValue v) { super.rawset(k, v); }
    }

    /**
     * The table handed to a chunk as its environment must be a Globals instance:
     * LuaClosure only keeps a globals reference (and therefore only runs the
     * instruction hook we use for the FUNCTION LOAD timeout) when it is one.
     */
    static final class REnv extends Globals {
        boolean locked;
        /**
         * scripting.c scriptingEnableGlobalsProtection(): the _G metatable turns a
         * store to a *new* name into "Script attempted to create global variable
         * '<n>'".  Overwriting a name that already exists stays a readonly-table
         * violation, which is what the newer suites check for (`redis = ...`).
         */
        boolean globalsProtection;

        void lock() { locked = true; }

        private void chk() {
            if (locked) throw new LuaError("Attempt to modify a readonly table");
        }

        private void chk(LuaValue key) {
            if (!locked) return;
            if (globalsProtection && key.isstring() && super.rawget(key).isnil())
                throw new LuaError("Script attempted to create global variable '"
                        + key.tojstring() + "'");
            throw new LuaError("Attempt to modify a readonly table");
        }

        @Override public void set(LuaValue key, LuaValue value) { chk(key); super.set(key, value); }
        @Override public void set(int key, LuaValue value) { chk(LuaValue.valueOf(key)); super.set(key, value); }
        @Override public void rawset(LuaValue key, LuaValue value) { chk(key); super.rawset(key, value); }
        @Override public void rawset(int key, LuaValue value) { chk(LuaValue.valueOf(key)); super.rawset(key, value); }
        @Override public LuaValue setmetatable(LuaValue mt) { chk(); return super.setmetatable(mt); }
        @Override public LuaValue remove(int pos) { chk(); return super.remove(pos); }
        @Override public void insert(int pos, LuaValue v) { chk(); super.insert(pos, v); }

        void force(String k, LuaValue v) { super.rawset(LuaValue.valueOf(k), v); }
        void force(LuaValue k, LuaValue v) { super.rawset(k, v); }

        /** share the interpreter services (and the running thread) with the real Globals */
        void adopt(Globals g) {
            compiler = g.compiler;
            loader = g.loader;
            undumper = g.undumper;
            baselib = g.baselib;
            package_ = g.package_;
            debuglib = g.debuglib;
            running = g.running;
            finder = g.finder;
            STDIN = g.STDIN;
            STDOUT = g.STDOUT;
            STDERR = g.STDERR;
        }
    }

    /** store into either flavour of protected table, bypassing the lock */
    static void put(LuaValue t, String k, LuaValue v) {
        if (t instanceof REnv) ((REnv) t).force(k, v);
        else ((RTable) t).force(k, v);
    }

    // ==================================================================
    // helpers: raw bytes <-> lua strings, number formatting
    // ==================================================================
    static byte[] lbytes(LuaValue v) {
        LuaString t = v.checkstring();
        byte[] r = new byte[t.m_length];
        System.arraycopy(t.m_bytes, t.m_offset, r, 0, t.m_length);
        return r;
    }

    static LuaString lstr(byte[] b) { return LuaString.valueOf(b); }

    /** ASCII-only literal -> LuaString (never UTF-8 re-encoded) */
    static LuaString ls(String v) { return LuaString.valueOf(Util.b(v)); }

    /**
     * script_lua.c luaArgsToRedisArgv(): print the integer form when double2ll()
     * succeeds, otherwise fpconv_dtoa()'s shortest round-tripping form.
     */
    public static String num2str(double d) {
        if (d >= -4.6116860184273879e18 && d <= 4.6116860184273879e18) {
            long ll = (long) d;
            if ((double) ll == d) return Long.toString(ll);
        }
        return fpconv(d);
    }

    /**
     * Lua 5.1's LUAI_NUMFFORMAT ("%.14g") number -> string conversion.  LuaJ uses
     * Java's Double.toString instead, which renders 0.0003 as "3.0E-4".
     */
    public static String luaNum2str(double d) {
        if (Double.isNaN(d)) return "-nan";
        if (Double.isInfinite(d)) return d > 0 ? "inf" : "-inf";
        return Util.fmtG(d, 14);
    }

    /** LuaJ reports the EOF token numerically; render it the way Lua does. */
    static String cleanCompileErr(String m) {
        int i = m.indexOf("unexpected symbol 286");
        if (i >= 0) return m.substring(0, i) + "unexpected symbol near '<eof>'";
        return m;
    }

    /** C "%.<prec>g" as used by cjson's number formatting */
    static String fmtG(double v, int prec) {
        if (Double.isNaN(v)) return "nan";
        if (Double.isInfinite(v)) return v > 0 ? "inf" : "-inf";
        if (prec <= 0) prec = 1;
        return Util.fmtG(v, prec);
    }

    /** deps/fpconv fpconv_dtoa() */
    public static String fpconv(double v) {
        if (Double.isNaN(v)) return "nan";
        if (Double.isInfinite(v)) return v > 0 ? "inf" : "-inf";
        boolean neg = (v < 0) || (v == 0.0 && 1 / v < 0);
        double a = Math.abs(v);
        if (a == 0.0) return neg ? "-0" : "0";
        String digits = null;
        int k = 0;
        for (int p = 0; p <= 17; p++) {
            String cand = String.format(Locale.US, "%." + p + "e", a);
            if (Double.parseDouble(cand) == a) {
                int e = cand.indexOf('e');
                String mant = cand.substring(0, e);
                int exp = Integer.parseInt(cand.substring(e + 1));
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < mant.length(); i++) {
                    char c = mant.charAt(i);
                    if (c >= '0' && c <= '9') sb.append(c);
                }
                digits = sb.toString();
                k = exp - p;
                break;
            }
        }
        if (digits == null) return Double.toString(v);
        int nd = digits.length();
        while (nd > 1 && digits.charAt(nd - 1) == '0') { nd--; k++; }
        digits = digits.substring(0, nd);
        return (neg ? "-" : "") + emitDigits(digits, k);
    }

    /** fpconv.c emit_digits() */
    private static String emitDigits(String digits, int k) {
        int nd = digits.length();
        int exp = Math.abs(k + nd - 1);
        StringBuilder d = new StringBuilder();
        if (k >= 0 && exp < nd + 7) {
            d.append(digits);
            for (int i = 0; i < k; i++) d.append('0');
            return d.toString();
        }
        if (k < 0 && (k > -7 || exp < 4)) {
            int offset = nd - Math.abs(k);
            if (offset <= 0) {
                offset = -offset;
                d.append("0.");
                for (int i = 0; i < offset; i++) d.append('0');
                d.append(digits);
            } else {
                d.append(digits, 0, offset).append('.').append(digits, offset, nd);
            }
            return d.toString();
        }
        d.append(digits.charAt(0));
        if (nd > 1) d.append('.').append(digits, 1, nd);
        d.append('e');
        d.append(k + nd - 1 < 0 ? '-' : '+');
        int e = exp;
        int cent = 0;
        if (e > 99) { cent = e / 100; d.append((char) ('0' + cent)); e -= cent * 100; }
        if (e > 9) { int dec = e / 10; d.append((char) ('0' + dec)); e -= dec * 10; }
        else if (cent != 0) d.append('0');
        d.append((char) ('0' + e % 10));
        return d.toString();
    }

    // ==================================================================
    // error message normalisation
    // ==================================================================
    /**
     * LuaJ renders a runtime error as "@chunk:LINE message" (plus a traceback when
     * the debug library is loaded); real Lua produces "chunk:LINE: message".
     */
    static String normalizeErr(String m) {
        if (m == null) return "";
        int tb = m.indexOf("\nstack traceback:");
        if (tb >= 0) m = m.substring(0, tb);
        if (m.startsWith("@")) {
            int c1 = m.indexOf(':');
            if (c1 > 0) {
                int i = c1 + 1, st = i;
                while (i < m.length() && m.charAt(i) >= '0' && m.charAt(i) <= '9') i++;
                if (i > st && i < m.length() && m.charAt(i) == ' ')
                    return m.substring(1, i) + ": " + m.substring(i + 1);
            }
            return m.substring(1);
        }
        return m;
    }

    static int errLine(String m) {
        if (m == null || !m.startsWith("@")) return -1;
        int c1 = m.indexOf(':');
        if (c1 < 0) return -1;
        int i = c1 + 1, st = i;
        while (i < m.length() && m.charAt(i) >= '0' && m.charAt(i) <= '9') i++;
        if (i == st) return -1;
        try { return Integer.parseInt(m.substring(st, i)); } catch (NumberFormatException e) { return -1; }
    }

    static String errSource(String m) {
        if (m == null || !m.startsWith("@")) return null;
        int c1 = m.indexOf(':');
        if (c1 < 0) return null;
        return m.substring(1, c1);
    }

    /** script_lua.c luaPushErrorBuff(): derive the error code from the message. */
    static LuaTable errorTable(String errBuffer) {
        String msg, code;
        if (errBuffer.startsWith("-")) {
            int sp = errBuffer.indexOf(' ');
            if (sp < 0) { msg = errBuffer.substring(1); code = "ERR"; }
            else { code = errBuffer.substring(1, sp); msg = errBuffer.substring(sp + 1); }
        } else {
            msg = errBuffer;
            code = "ERR";
        }
        int b = 0, e = msg.length();
        while (b < e && (msg.charAt(b) == '\r' || msg.charAt(b) == '\n')) b++;
        while (e > b && (msg.charAt(e - 1) == '\r' || msg.charAt(e - 1) == '\n')) e--;
        msg = msg.substring(b, e);
        LuaTable t = new LuaTable();
        t.rawset(ls("err"), ls(code + " " + msg));
        return t;
    }

    static LuaError pushError(String error) { return new LuaError(errorTable(error)); }

    /**
     * networking.c addReplyErrorFormatInternal(): trim the newlines at the ends
     * and map any CRLF left in the middle to spaces, so user supplied text can
     * never break the protocol.
     */
    public static RedisError rerr(String msg) {
        StringBuilder sb = new StringBuilder(msg);
        int e = sb.length();
        while (e > 0 && (sb.charAt(e - 1) == '\r' || sb.charAt(e - 1) == '\n')) e--;
        sb.setLength(e);
        int b = 0;
        while (b < sb.length() && (sb.charAt(b) == '\r' || sb.charAt(b) == '\n')) b++;
        if (b > 0) sb.delete(0, b);
        for (int i = 0; i < sb.length(); i++) {
            char c = sb.charAt(i);
            if (c == '\r' || c == '\n') sb.setCharAt(i, ' ');
        }
        return new RedisError(sb.toString());
    }

    // ==================================================================
    // sandbox construction
    // ==================================================================
    public void ensureInit() {
        if (G != null) return;
        Globals g = new Globals();
        g.load(new JseBaseLib());
        g.load(new PackageLib());   /* TableLib/StringLib register into package.loaded */
        g.load(new TableLib());
        g.load(new StringLib());
        g.load(new CoroutineLib());
        g.load(new JseMathLib());
        /* Both hooks must be load()ed so LuaJ hands them their Globals: DebugLib's
         * onCall/onReturn bookkeeping dereferences it.  LoadHook is loaded last so
         * that it stays the base globals' debug library, as before. */
        timeHook = new TimeHook();
        g.load(timeHook);
        hook = new LoadHook();
        g.load(hook);
        LuaC.install(g);
        G = g;

        stringLib = g.get("string").checktable();
        tableLib = g.get("table").checktable();
        mathLib = g.get("math").checktable();
        coroutineLib = g.get("coroutine").checktable();

        RTable errMeta = new RTable();
        errMeta.force("__index", new TwoArgFunction() {
            @Override public LuaValue call(LuaValue t, LuaValue k) {
                throw new LuaError("Script attempted to access nonexistent global variable '"
                        + k.tojstring() + "'");
            }
        });
        errMeta.lock();

        RTable str = protect(stringLib);
        str.force("gfind", stringLib.get("gmatch"));
        str.lock();
        RTable tbl = protect(tableLib);
        installTableCompat(tbl);
        tbl.lock();
        RTable mth = protect(mathLib);
        installMathCompat(mth);
        mth.lock();
        RTable cor = protect(coroutineLib);
        cor.lock();
        osLib = buildOs();
        RTable cjson = Json.build(this);
        RTable bitlib = Bit.build();
        RTable structlib = Struct.build();
        RTable msgpack = MsgPack.build(this);

        evalRedis = buildRedis(true);
        fnRedis = buildRedis(false);
        evalRedis.lock();
        fnRedis.lock();

        libRedis = new RTable();
        libRedis.force("register_function", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return registerFunction(a); }
        });
        addLogApi(libRedis);
        addVersionApi(libRedis);
        libRedis.setmetatable(errMeta);
        libRedis.lock();

        evalEnv = new REnv();
        evalEnv.adopt(g);
        evalEnv.debuglib = null;   /* no per-instruction hook for EVAL */
        fnGlobals = new RTable();
        installBase(evalEnv, g);
        installBase(fnGlobals, g);
        LuaValue[] both = new LuaValue[]{evalEnv, fnGlobals};
        for (int i = 0; i < both.length; i++) {
            LuaValue e = both[i];
            put(e, "string", str);
            put(e, "table", tbl);
            put(e, "math", mth);
            put(e, "coroutine", cor);
            put(e, "os", osLib);
            put(e, "cjson", cjson);
            put(e, "bit", bitlib);
            put(e, "struct", structlib);
            put(e, "cmsgpack", msgpack);
        }
        evalEnv.force("redis", evalRedis);
        evalEnv.force("server", evalRedis);
        fnGlobals.force("redis", fnRedis);
        fnGlobals.force("server", fnRedis);
        evalEnv.force("KEYS", new LuaTable());
        evalEnv.force("ARGV", new LuaTable());
        evalEnv.force("_G", evalEnv);
        evalEnv.setmetatable(errMeta);
        evalEnv.globalsProtection = true;
        evalEnv.lock();

        fnEnv = new REnv();
        fnEnv.adopt(g);
        fnEnv.debuglib = null;
        fnGlobals.force("_G", fnEnv);
        fnGlobals.setmetatable(errMeta);
        fnGlobals.lock();

        libApi = new RTable();
        libApi.force("redis", libRedis);
        libApi.force("server", libRedis);
        libApi.setmetatable(errMeta);
        libApi.lock();

        fnMeta = new RTable();
        fnMeta.force("__index", fnGlobals);
        fnEnv.setmetatable(fnMeta);
        fnEnv.lock();

        // numbers: give them a readonly metatable whose __tostring formats like Lua
        RTable nmt = new RTable();
        nmt.force("__tostring", new OneArgFunction() {
            @Override public LuaValue call(LuaValue v) { return lstr(Util.b(luaNum2str(v.todouble()))); }
        });
        nmt.lock();
        LuaNumber.s_metatable = nmt;

        // the string metatable must be readonly as well
        LuaValue sm = LuaString.s_metatable;
        RTable smt = new RTable();
        if (sm != null && sm.istable()) copyInto(smt, sm.checktable());
        else smt.force(LuaValue.INDEX, str);
        smt.lock();
        LuaString.s_metatable = smt;
    }

    private static void copyInto(RTable dst, LuaTable src) {
        LuaValue k = LuaValue.NIL;
        while (true) {
            Varargs n = src.next(k);
            k = n.arg1();
            if (k.isnil()) break;
            dst.force(k, n.arg(2));
        }
    }

    private static RTable protect(LuaTable t) {
        RTable r = new RTable();
        copyInto(r, t);
        return r;
    }

    /** the Lua 5.1 table.* entries LuaJ's 5.2 table library dropped */
    private void installTableCompat(RTable t) {
        t.force("getn", new OneArgFunction() {
            @Override public LuaValue call(LuaValue a) { return LuaValue.valueOf(a.checktable().length()); }
        });
        t.force("setn", new TwoArgFunction() {
            @Override public LuaValue call(LuaValue a, LuaValue b) { throw new LuaError("'setn' is obsolete"); }
        });
        t.force("maxn", new OneArgFunction() {
            @Override public LuaValue call(LuaValue a) {
                LuaTable tt = a.checktable();
                double max = 0;
                LuaValue k = LuaValue.NIL;
                while (true) {
                    Varargs n = tt.next(k);
                    k = n.arg1();
                    if (k.isnil()) break;
                    if (k.isnumber() && k.todouble() > max) max = k.todouble();
                }
                return LuaValue.valueOf(max);
            }
        });
        t.force("foreach", new TwoArgFunction() {
            @Override public LuaValue call(LuaValue a, LuaValue f) {
                LuaTable tt = a.checktable();
                LuaValue k = LuaValue.NIL;
                while (true) {
                    Varargs n = tt.next(k);
                    k = n.arg1();
                    if (k.isnil()) break;
                    LuaValue r = f.call(k, n.arg(2));
                    if (!r.isnil()) return r;
                }
                return LuaValue.NIL;
            }
        });
        t.force("foreachi", new TwoArgFunction() {
            @Override public LuaValue call(LuaValue a, LuaValue f) {
                LuaTable tt = a.checktable();
                int n = tt.length();
                for (int i = 1; i <= n; i++) {
                    LuaValue r = f.call(LuaValue.valueOf(i), tt.get(i));
                    if (!r.isnil()) return r;
                }
                return LuaValue.NIL;
            }
        });
        t.force("concat", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                LuaTable tt = a.arg1().checktable();
                byte[] sep = a.narg() >= 2 && !a.arg(2).isnil() ? lbytes(a.arg(2)) : Util.EMPTY;
                int i = a.narg() >= 3 && !a.arg(3).isnil() ? (int) a.arg(3).checkdouble() : 1;
                int j = a.narg() >= 4 && !a.arg(4).isnil() ? (int) a.arg(4).checkdouble() : tt.length();
                Buf b = new Buf(32);
                for (int x = i; x <= j; x++) {
                    if (x > i) b.put(sep);
                    LuaValue v = tt.get(x);
                    if (v.type() == LuaValue.TNUMBER) b.put(Util.b(luaNum2str(v.todouble())));
                    else if (v.type() == LuaValue.TSTRING) b.put(lbytes(v));
                    else throw new LuaError("invalid value (at index " + x + ") in table for 'concat'");
                }
                return lstr(b.toBytes());
            }
        });
        t.force("pack", LuaValue.NIL);
        t.force("unpack", LuaValue.NIL);
        unpackFn = new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                LuaTable tt = a.arg1().checktable();
                long i = a.narg() >= 2 && !a.arg(2).isnil() ? (long) a.arg(2).checkdouble() : 1;
                long j = a.narg() >= 3 && !a.arg(3).isnil() ? (long) a.arg(3).checkdouble() : tt.length();
                if (i > j) return LuaValue.NONE;
                long n = j - i + 1;
                if (n <= 0 || n > 1000000) throw new LuaError("too many results to unpack");
                LuaValue[] out = new LuaValue[(int) n];
                for (int x = 0; x < n; x++) {
                    long idx = i + x;
                    out[x] = (idx >= Integer.MIN_VALUE && idx <= Integer.MAX_VALUE)
                            ? tt.get(LuaValue.valueOf((double) idx)) : LuaValue.NIL;
                }
                return LuaValue.varargsOf(out);
            }
        };
    }

    private void installMathCompat(RTable m) {
        m.force("log10", new OneArgFunction() {
            @Override public LuaValue call(LuaValue a) { return LuaValue.valueOf(Math.log10(a.checkdouble())); }
        });
        m.force("mod", m.get("fmod"));
        m.force("random", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return mathRandom(a); }
        });
        m.force("randomseed", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                srand48((int) a.arg1().checkdouble());
                return LuaValue.NONE;
            }
        });
    }

    /** deps/lua loslib.c sandbox_syslib: only os.clock survives */
    private RTable buildOs() {
        RTable os = new RTable();
        os.force("clock", new ZeroArgFunction() {
            @Override public LuaValue call() { return LuaValue.valueOf(System.nanoTime() / 1.0e9); }
        });
        RTable meta = new RTable();
        meta.force("__index", new TwoArgFunction() {
            @Override public LuaValue call(LuaValue t, LuaValue k) {
                /* deps/lua loslib.c only registers os.clock; reaching for one of
                 * the removed entries must look like Lua calling a nil field. */
                throw new LuaError("attempt to call field '" + k.tojstring() + "' (a nil value)");
            }
        });
        meta.lock();
        os.setmetatable(meta);
        os.lock();
        return os;
    }

    /** copy the sandboxed subset of the Lua base library into the given globals */
    private void installBase(LuaValue e, Globals g) {
        String[] keep = {"assert", "collectgarbage", "error", "getmetatable", "ipairs", "next",
                "pairs", "rawequal", "rawget", "select", "tonumber", "tostring", "type", "xpcall"};
        for (int i = 0; i < keep.length; i++) {
            LuaValue v = g.rawget(keep[i]);
            if (!v.isnil()) put(e, keep[i], v);
        }
        put(e, "_VERSION", ls("Lua 5.1"));
        put(e, "gcinfo", new ZeroArgFunction() {
            @Override public LuaValue call() { return LuaValue.valueOf(0); }
        });
        put(e, "unpack", unpackFn);
        put(e, "setmetatable", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                LuaValue t = a.arg1();
                if (!t.istable()) throw new LuaError("bad argument #1 to 'setmetatable' (table expected)");
                LuaValue mt = a.arg(2);
                if (!mt.isnil() && !mt.istable())
                    throw new LuaError("bad argument #2 to 'setmetatable' (nil or table expected)");
                t.setmetatable(mt.isnil() ? null : mt);
                return t;
            }
        });
        put(e, "rawset", new ThreeArgFunction() {
            @Override public LuaValue call(LuaValue t, LuaValue k, LuaValue v) {
                t.checktable().rawset(k, v);
                return t;
            }
        });
        put(e, "pcall", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return redisPcall(a); }
        });
        LuaValue loader = new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return luaLoad(a); }
        };
        put(e, "load", loader);
        put(e, "loadstring", loader);
        put(e, "__redis__err__handler", new OneArgFunction() {
            @Override public LuaValue call(LuaValue err) {
                if (err.istable()) return err;
                LuaTable t = new LuaTable();
                t.rawset(ls("err"), ls("ERR " + normalizeErr(err.tojstring())));
                return t;
            }
        });
    }

    /** script_lua.c luaRedisPcall(): unwrap an error table into its message */
    Varargs redisPcall(Varargs a) {
        LuaValue f = a.arg1();
        try {
            return LuaValue.varargsOf(LuaValue.TRUE, f.invoke(a.subargs(2)).eval());
        } catch (LuaError le) {
            LuaValue o = le.getMessageObject();
            if (o == null) o = ls(normalizeErr(le.getMessage()));
            else if (o.istable()) {
                LuaValue er = o.get(ls("err"));
                if (er.isstring()) o = er;
            } else if (o.isstring()) {
                o = ls(normalizeErr(o.tojstring()));
            }
            return LuaValue.varargsOf(LuaValue.FALSE, o);
        } catch (StackOverflowError so) {
            return LuaValue.varargsOf(LuaValue.FALSE, ls("stack overflow"));
        }
    }

    /** load()/loadstring(): text chunks only, binary chunks yield nil like redis */
    Varargs luaLoad(Varargs a) {
        LuaValue src = a.arg1();
        byte[] body;
        if (src.isfunction()) {
            Buf b = new Buf(64);
            while (true) {
                LuaValue piece = src.call();
                if (!piece.isstring()) break;
                byte[] p = lbytes(piece);
                if (p.length == 0) break;
                b.put(p);
            }
            body = b.toBytes();
        } else if (src.isstring()) {
            body = lbytes(src);
        } else {
            return LuaValue.varargsOf(LuaValue.NIL, ls("bad argument"));
        }
        String name = a.narg() >= 2 && a.arg(2).isstring() ? a.arg(2).tojstring() : "(load)";
        if (body.length > 0 && body[0] == 27)
            return LuaValue.varargsOf(LuaValue.NIL, ls("attempt to load a binary chunk"));
        LuaValue envTable = (cur != null && !cur.evalMode) || loadingLib != null ? fnEnv : evalEnv;
        try {
            return G.load(new ByteArrayInputStream(body), "@" + name, "t", envTable);
        } catch (LuaError e) {
            return LuaValue.varargsOf(LuaValue.NIL, ls(normalizeErr(e.getMessage())));
        } catch (RuntimeException e) {
            return LuaValue.varargsOf(LuaValue.NIL, ls(String.valueOf(e.getMessage())));
        }
    }

    // ------------------------------------------------------------------
    // the redis.* table (script_lua.c luaRegisterRedisAPI)
    // ------------------------------------------------------------------
    private RTable buildRedis(boolean eval) {
        RTable r = new RTable();
        r.force("call", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return redisGenericCommand(a, true); }
        });
        r.force("pcall", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return redisGenericCommand(a, false); }
        });
        addLogApi(r);
        addVersionApi(r);
        r.force("setresp", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                if (a.narg() != 1) throw pushError("redis.setresp() requires one argument.");
                int resp = (int) a.arg1().todouble();
                if (resp != 2 && resp != 3) throw pushError("RESP version must be 2 or 3.");
                if (cur != null) cur.fake.resp = resp;
                return LuaValue.NONE;
            }
        });
        r.force("sha1hex", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                if (a.narg() != 1) throw pushError("wrong number of arguments");
                LuaValue v = a.arg1();
                byte[] in = v.isstring() ? lbytes(v) : Util.EMPTY;
                return lstr(Util.sha1hex(in));
            }
        });
        r.force("error_reply", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                if (a.narg() != 1 || a.arg1().type() != LuaValue.TSTRING)
                    return errorTable("wrong number or type of arguments");
                String err = Util.s(lbytes(a.arg1()));
                return errorTable(err.startsWith("-") ? err : "-" + err);
            }
        });
        r.force("status_reply", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                if (a.narg() != 1 || a.arg1().type() != LuaValue.TSTRING)
                    return errorTable("wrong number or type of arguments");
                LuaTable t = new LuaTable();
                t.rawset(ls("ok"), a.arg1());
                return t;
            }
        });
        r.force("set_repl", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) {
                /* luaRedisSetReplCommand() checks this first, before the arity
                 * and before the flags. Only EVAL is gated: redis.replicate_commands()
                 * is an eval-only API, so a function has nothing to turn on. */
                if (cur != null && cur.evalMode && !cur.replicateCommands)
                    throw pushError("You can set the replication behavior only after turning on "
                            + "single commands replication with redis.replicate_commands().");
                if (a.narg() != 1) throw pushError("redis.set_repl() requires one argument.");
                int flags = (int) a.arg1().todouble();
                if ((flags & ~(PROPAGATE_AOF | PROPAGATE_REPL)) != 0)
                    throw pushError("Invalid replication flags. Use REPL_AOF, REPL_REPLICA, REPL_ALL or REPL_NONE.");
                if (cur != null) cur.replFlags = flags;
                return LuaValue.NONE;
            }
        });
        r.force("REPL_NONE", LuaValue.valueOf(PROPAGATE_NONE));
        r.force("REPL_AOF", LuaValue.valueOf(PROPAGATE_AOF));
        r.force("REPL_SLAVE", LuaValue.valueOf(PROPAGATE_REPL));
        r.force("REPL_REPLICA", LuaValue.valueOf(PROPAGATE_REPL));
        r.force("REPL_ALL", LuaValue.valueOf(PROPAGATE_AOF | PROPAGATE_REPL));
        r.force("acl_check_cmd", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return aclCheckCmd(a); }
        });
        if (eval) {
            r.force("breakpoint", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return LuaValue.FALSE; }
            });
            r.force("debug", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return LuaValue.NONE; }
            });
            r.force("replicate_commands", new VarArgFunction() {
                /** scripting.c scriptingEnableReplicateCommands(). */
                @Override public Varargs invoke(Varargs a) {
                    if (cur == null || cur.replicateCommands) return LuaValue.TRUE;
                    /* "Can't switch after a write": the script has already made
                     * the dirty counter move, so the whole-script form is
                     * already committed and the switch is refused. */
                    if (s.dirty != cur.dirtyStart) return LuaValue.FALSE;
                    cur.replicateCommands = true;
                    /* switching to effect replication frees the PRNG from the
                     * per-call reseed, so give it a real seed. */
                    srand48(SEED_SOURCE.nextInt());
                    return LuaValue.TRUE;
                }
            });
        }
        return r;
    }

    private void addLogApi(RTable r) {
        r.force("log", new VarArgFunction() {
            @Override public Varargs invoke(Varargs a) { return luaLog(a); }
        });
        r.force("LOG_DEBUG", LuaValue.valueOf(0));
        r.force("LOG_VERBOSE", LuaValue.valueOf(1));
        r.force("LOG_NOTICE", LuaValue.valueOf(2));
        r.force("LOG_WARNING", LuaValue.valueOf(3));
    }

    private void addVersionApi(RTable r) {
        String v = Server.VERSION;
        int num = 0;
        try {
            String[] p = v.split("\\.");
            num = (Integer.parseInt(p[0]) << 16) | (Integer.parseInt(p[1]) << 8) | Integer.parseInt(p[2]);
        } catch (RuntimeException ignored) {
            num = 0;
        }
        r.force("REDIS_VERSION_NUM", LuaValue.valueOf(num));
        r.force("REDIS_VERSION", ls(v));
    }

    private Varargs luaLog(Varargs a) {
        int argc = a.narg();
        if (argc < 2) throw pushError("redis.log() requires two arguments or more.");
        if (!a.arg1().isnumber()) throw pushError("First argument must be a number (log level).");
        int level = (int) a.arg1().todouble();
        if (level < 0 || level > 3) throw pushError("Invalid log level.");
        if (level < verbosity()) return LuaValue.NONE;
        StringBuilder sb = new StringBuilder();
        for (int j = 2; j <= argc; j++) {
            LuaValue v = a.arg(j);
            if (!v.isstring()) continue;
            if (sb.length() > 0) sb.append(' ');
            sb.append(Util.s(lbytes(v)));
        }
        s.log(".-*#".charAt(level), sb.toString());
        return LuaValue.NONE;
    }

    private int verbosity() {
        String lv = s.config.getOr("loglevel", "notice");
        if (lv == null) return 2;
        String l = lv.toLowerCase(Locale.US);
        if (l.equals("debug")) return 0;
        if (l.equals("verbose")) return 1;
        if (l.equals("warning")) return 3;
        if (l.equals("nothing")) return 4;
        return 2;
    }

    private Varargs aclCheckCmd(Varargs a) {
        int n = a.narg();
        if (n == 0) throw pushError("Please specify at least one argument for this redis lib call");
        byte[][] argv = new byte[n][];
        for (int i = 1; i <= n; i++) {
            LuaValue v = a.arg(i);
            if (v.type() == LuaValue.TNUMBER) argv[i - 1] = Util.b(num2str(v.todouble()));
            else if (v.type() == LuaValue.TSTRING) argv[i - 1] = lbytes(v);
            else throw pushError("Lua redis lib command arguments must be strings or integers");
        }
        Cmd cmd = CommandTable.lookup(Util.lower(argv[0]));
        if (cmd == null) throw pushError("Invalid command passed to redis.acl_check_cmd()");
        if (!Acl.enabled(s) || cur == null) return LuaValue.TRUE;
        Client probe = new Client();
        probe.fake = true;
        probe.db = cur.caller.db;
        probe.user = cur.caller.user;
        probe.authenticated = true;
        probe.argv = argv;
        probe.argc = argv.length;
        String err;
        try { err = Acl.check(s, probe, cmd); }
        catch (RuntimeException t) { err = null; }
        return LuaValue.valueOf(err == null);
    }

    // ==================================================================
    // math.random: redis' own lrand48 (rand.c) so the sequence is portable
    // ==================================================================
    private long rx = (0x1234L << 32) | (0xABCDL << 16) | 0x330EL;

    /** rand.c redisSrand48(rand()) used by scripting.c luaRedisReplicateCommandsCommand() */
    private static final java.util.Random SEED_SOURCE = new java.util.Random();

    private void srand48(int seed) {
        long lo = seed & 0xFFFFL;
        long hi = (seed >> 16) & 0xFFFFL;
        rx = (hi << 32) | (lo << 16) | 0x330EL;
    }

    private int lrand48() {
        rx = (0x5DEECE66DL * rx + 0xBL) & 0xFFFFFFFFFFFFL;
        return (int) ((rx >>> 17) & 0x7FFFFFFFL);
    }

    private Varargs mathRandom(Varargs a) {
        double r = (double) (lrand48() % Integer.MAX_VALUE) / (double) Integer.MAX_VALUE;
        switch (a.narg()) {
            case 0:
                return LuaValue.valueOf(r);
            case 1: {
                int u = (int) a.arg1().checkdouble();
                if (u < 1) throw new LuaError("bad argument #1 to 'random' (interval is empty)");
                return LuaValue.valueOf(Math.floor(r * u) + 1);
            }
            case 2: {
                int l = (int) a.arg1().checkdouble();
                int u = (int) a.arg(2).checkdouble();
                if (l > u) throw new LuaError("bad argument #2 to 'random' (interval is empty)");
                return LuaValue.valueOf(Math.floor(r * ((double) u - l + 1)) + l);
            }
            default:
                throw new LuaError("wrong number of arguments");
        }
    }

    // ==================================================================
    // slow script detection (scripting.c luaMaskCountHook / script.c scriptInterrupt)
    // ==================================================================
    final class TimeHook extends DebugLib {
        private int n;

        @Override public void onInstruction(int pc, Varargs v, int top) {
            /* script_lua.c: once the script is killed the hook is rearmed to fire
             * on every line, "so the user will not be able to catch the error with
             * pcall and invoke pcall again" -- the error has to be raised at an
             * instruction that is not inside the protected call. */
            if (!killRequested && (++n & 0x3ff) != 0) return;
            interrupt();
        }
    }

    /**
     * script.c scriptInterrupt(): once the script is over the busy threshold we
     * reenter the event loop so that other clients can still be served (they get
     * -BUSY back) and can ask for SCRIPT KILL / SHUTDOWN NOSAVE.
     */
    void interrupt() {
        RunCtx r = cur;
        if (r == null) return;
        if (!timedout) {
            if (busyLimit <= 0) return;
            long elapsed = System.currentTimeMillis() - r.start;
            if (elapsed < busyLimit) return;
            s.logWarn("Slow script detected: still in execution after " + elapsed
                    + " milliseconds. You can try killing the script using the SCRIPT KILL"
                    + " command. Script name is: " + r.funcname + ".");
            timedout = true;
        }
        long now = System.currentTimeMillis();
        if (now != lastYield) {
            lastYield = now;
            try { s.processEvents(0, false); } catch (java.io.IOException ignored) {}
            if (s.shutdownRequested) s.doShutdown(true);
        }
        /* Do not clear the flag: a script that wraps everything in pcall() would
         * otherwise swallow the error and keep running (script_lua.c re-arms the
         * hook on every line for the same reason). */
        if (killRequested) {
            if (!killLogged) { killLogged = true; s.logWarn("Lua script killed by user with SCRIPT KILL."); }
            throw pushError("Script killed by user with SCRIPT KILL...");
        }
    }

    // ==================================================================
    // FUNCTION LOAD instruction hook (function_lua.c luaEngineLoadHook)
    // ==================================================================
    final class LoadHook extends DebugLib {
        private int n;

        @Override public void onInstruction(int pc, Varargs v, int top) {
            super.onInstruction(pc, v, top);
            if (loadDeadline != 0 && ((++n & 0x1fff) == 0) && System.currentTimeMillis() > loadDeadline) {
                loadDeadline = 0;
                throw pushError("FUNCTION LOAD timeout");
            }
        }
    }

    // ==================================================================
    // redis.call / redis.pcall (script_lua.c luaRedisGenericCommand)
    // ==================================================================
    Varargs redisGenericCommand(Varargs a, boolean raiseError) {
        RunCtx r = cur;
        if (r == null) throw pushError("redis.call/pcall can only be called inside a script invocation");
        int n = a.narg();
        if (n == 0) {
            LuaTable t = errorTable("Please specify at least one argument for this redis lib call");
            if (raiseError) throw new LuaError(t);
            return t;
        }
        byte[][] argv = new byte[n][];
        for (int i = 1; i <= n; i++) {
            LuaValue v = a.arg(i);
            if (v.type() == LuaValue.TNUMBER) {
                argv[i - 1] = Util.b(num2str(v.todouble()));
            } else if (v.type() == LuaValue.TSTRING) {
                argv[i - 1] = lbytes(v);
            } else {
                LuaTable t = errorTable("Lua redis lib command arguments must be strings or integers");
                if (raiseError) throw new LuaError(t);
                return t;
            }
        }

        Client c = r.fake;
        c.argv = argv;
        c.argc = argv.length;
        c.out.clear();
        String name = Util.lower(argv[0]);
        Cmd cmd = CommandTable.lookup(name);
        Cmd effective = cmd;
        if (cmd != null && CommandTable.SUBS.containsKey(name) && argv.length >= 2) {
            Cmd sub = CommandTable.SUBS.get(name).get(Util.lower(argv[1]));
            if (sub != null) effective = sub;
        }

        String err = null;
        if (cmd == null) {
            err = "Unknown Redis command called from script";
        } else if ((cmd.arity > 0 && cmd.arity != c.argc) || (c.argc < -cmd.arity)) {
            err = "Wrong number of args calling Redis command from script";
        } else if (!disableDenyScript && effective.isNoScript()) {
            err = "This Redis command is not allowed from script";
        } else if (r.readOnly && (effective.isWrite() || effective.has("may_replicate"))) {
            err = "Write commands are not allowed from read-only scripts.";
        } else if (effective.isWrite() && s.repl.masterHost != null
                && s.config.getBool("replica-read-only") && !s.repl.isMasterClient(r.caller)) {
            err = "-READONLY You can't write against a read only replica.";
        } else if (Acl.enabled(s)) {
            /* script.c scriptVerifyACL() */
            err = AclState.checkScript(s, c, r.caller, cmd);
        }

        if (err != null) {
            c.argv = null;
            c.argc = 0;
            LuaTable t = errorTable(err);
            t.rawset(ls("ignore_error_stats_update"), LuaValue.TRUE);
            s.errorStats.merge(errCode(err), 1L, Long::sum);
            if (raiseError) throw new LuaError(t);
            return t;
        }

        if (effective.isWrite()) r.writeDirty = true;

        int savedDepth = s.execDepth;
        s.replOverride = null;
        long dirtyBefore = s.dirty;
        /* redis.set_repl(): script.c passes the propagation flags down to call().
         * Suppress Server.call()'s own propagation and feed the selected sinks. */
        if (r.replFlags != (PROPAGATE_AOF | PROPAGATE_REPL)) s.replOverride = new byte[0][];
        try {
            s.execDepth++;
            s.call(c, cmd);
        } catch (RedisError e) {
            c.addErr(e.msg);
        } catch (StackOverflowError e) {
            c.addError("ERR stack overflow");
        } catch (RuntimeException e) {
            c.addError("ERR internal error: " + e);
        } finally {
            s.execDepth = savedDepth;
            s.replOverride = null;
        }
        if (r.replFlags != (PROPAGATE_AOF | PROPAGATE_REPL) && s.dirty != dirtyBefore) {
            byte[][] verbatim = Arrays.copyOf(argv, argv.length);
            if ((r.replFlags & PROPAGATE_AOF) != 0) s.aof.feed(c.db, verbatim);
            if ((r.replFlags & PROPAGATE_REPL) != 0) s.repl.feed(c.db, verbatim);
        }
        if (c.blocked) {
            /* Scripts never block (CLIENT_DENY_BLOCKING): behave as if the
             * blocking command timed out immediately. */
            if (c.out.len == 0) {
                Blocking.Timeout t = Blocking.TIMEOUT.get(c.blockType);
                if (t != null) t.onTimeout(s, c);
                else c.addNilArray();
            }
            s.unblockClient(c);
        }
        byte[] reply = c.out.toBytes();
        c.out.clear();
        c.argv = null;
        c.argc = 0;

        boolean isErr = reply.length > 0 && reply[0] == '-';
        LuaValue v = protocolToLua(reply);
        /* scripting.c luaRedisGenericCommand(): "Sort the output array if needed,
         * assuming it is a non-null multi bulk reply as expected." */
        if (r.evalMode && reply.length > 1 && reply[0] == '*' && reply[1] != '-'
                && SORT_FOR_SCRIPT.contains(name) && v.istable())
            luaSortArray((LuaTable) v);
        if (raiseError && isErr) throw new LuaError(v);
        return v;
    }

    /**
     * redis.c command table flag 'S' (CMD_SORT_FOR_SCRIPT / "to-sort"): the reply
     * of these commands has no deterministic order, so a script sees it sorted.
     */
    static final java.util.Set<String> SORT_FOR_SCRIPT = new java.util.HashSet<String>(
            Arrays.asList("sinter", "sunion", "sdiff", "smembers", "hkeys", "hvals", "keys"));

    /** scripting.c luaSortArray(): table.sort(array, __redis__compare_helper) */
    static void luaSortArray(LuaTable t) {
        int n = t.length();
        if (n < 2) return;
        LuaValue[] a = new LuaValue[n];
        for (int i = 0; i < n; i++) a[i] = t.rawget(i + 1);
        Arrays.sort(a, new java.util.Comparator<LuaValue>() {
            @Override public int compare(LuaValue x, LuaValue y) { return compareHelper(x, y); }
        });
        for (int i = 0; i < n; i++) t.rawset(i + 1, a[i]);
    }

    /** the __redis__compare_helper Lua function: false sorts as the empty string */
    static int compareHelper(LuaValue x, LuaValue y) {
        if (x.type() == LuaValue.TNUMBER && y.type() == LuaValue.TNUMBER)
            return Double.compare(x.todouble(), y.todouble());
        byte[] a = sortKey(x), b = sortKey(y);
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) {
            int d = (a[i] & 0xFF) - (b[i] & 0xFF);
            if (d != 0) return d;
        }
        return a.length - b.length;
    }

    static byte[] sortKey(LuaValue v) {
        if (v.type() == LuaValue.TSTRING) return lbytes(v);
        if (v.type() == LuaValue.TNUMBER) return Util.b(luaNum2str(v.todouble()));
        return Util.EMPTY;
    }

    static String errCode(String msg) {
        String m = msg.startsWith("-") ? msg.substring(1) : msg;
        int sp = m.indexOf(' ');
        String code = sp < 0 ? m : m.substring(0, sp);
        if (code.isEmpty()) return "ERR";
        for (int i = 0; i < code.length(); i++) {
            char ch = code.charAt(i);
            if (ch < 'A' || ch > 'Z') return "ERR";
        }
        return code;
    }

    // ==================================================================
    // RESP -> Lua (script_lua.c redisProtocolToLuaType_*)
    // ==================================================================
    LuaValue protocolToLua(byte[] p) {
        if (p.length == 0) return LuaValue.NIL;
        return parseReply(p, new int[]{0});
    }

    private static int findCrlf(byte[] p, int from) {
        for (int i = from; i + 1 < p.length; i++) if (p[i] == '\r' && p[i + 1] == '\n') return i;
        return p.length;
    }

    private LuaValue parseReply(byte[] p, int[] pos) {
        if (pos[0] >= p.length) return LuaValue.NIL;
        int type = p[pos[0]];
        int start = pos[0] + 1;
        int eol = findCrlf(p, start);
        if (eol < start) eol = start;
        String head = Util.s(p, start, eol - start);
        pos[0] = Math.min(eol + 2, p.length);
        switch (type) {
            case '+': {
                LuaTable t = new LuaTable();
                t.rawset(ls("ok"), lstr(Arrays.copyOfRange(p, start, eol)));
                return t;
            }
            case '-': {
                LuaTable t = errorTable("-" + head);
                t.rawset(ls("ignore_error_stats_update"), LuaValue.TRUE);
                return t;
            }
            case ':': {
                long v;
                try { v = Long.parseLong(head.trim()); } catch (NumberFormatException e) { v = 0; }
                return LuaValue.valueOf((double) v);
            }
            case ',': {
                double d;
                if (head.equals("inf")) d = Double.POSITIVE_INFINITY;
                else if (head.equals("-inf")) d = Double.NEGATIVE_INFINITY;
                else if (head.equals("nan")) d = Double.NaN;
                else {
                    try { d = Double.parseDouble(head); } catch (NumberFormatException e) { d = 0; }
                }
                LuaTable t = new LuaTable();
                t.rawset(ls("double"), LuaValue.valueOf(d));
                return t;
            }
            case '#':
                return LuaValue.valueOf(head.equals("t"));
            case '_':
                return LuaValue.NIL;
            case '(': {
                LuaTable t = new LuaTable();
                t.rawset(ls("big_number"), lstr(Arrays.copyOfRange(p, start, eol)));
                return t;
            }
            case '$': {
                long len = parseLen(head);
                if (len < 0) return LuaValue.FALSE;
                int b = pos[0];
                int e = (int) Math.min((long) b + len, p.length);
                pos[0] = Math.min(e + 2, p.length);
                return lstr(Arrays.copyOfRange(p, b, e));
            }
            case '=': {
                long len = parseLen(head);
                if (len < 0) return LuaValue.FALSE;
                int b = pos[0];
                int e = (int) Math.min((long) b + len, p.length);
                pos[0] = Math.min(e + 2, p.length);
                String fmt = (e - b >= 4) ? Util.s(p, b, 3) : "txt";
                LuaTable inner = new LuaTable();
                inner.rawset(ls("string"), lstr(Arrays.copyOfRange(p, Math.min(b + 4, e), e)));
                inner.rawset(ls("format"), ls(fmt));
                LuaTable t = new LuaTable();
                t.rawset(ls("verbatim_string"), inner);
                return t;
            }
            case '*':
            case '>': {
                long n = parseLen(head);
                if (n < 0) return LuaValue.FALSE;
                LuaTable t = new LuaTable();
                for (long i = 0; i < n; i++) t.rawset((int) (i + 1), parseReply(p, pos));
                return t;
            }
            case '~': {
                long n = parseLen(head);
                if (n < 0) return LuaValue.FALSE;
                LuaTable inner = new LuaTable();
                for (long i = 0; i < n; i++) {
                    LuaValue k = parseReply(p, pos);
                    if (!k.isnil()) inner.rawset(k, LuaValue.TRUE);
                }
                LuaTable t = new LuaTable();
                t.rawset(ls("set"), inner);
                return t;
            }
            case '%': {
                long n = parseLen(head);
                if (n < 0) return LuaValue.FALSE;
                LuaTable inner = new LuaTable();
                for (long i = 0; i < n; i++) {
                    LuaValue k = parseReply(p, pos);
                    LuaValue v = parseReply(p, pos);
                    if (!k.isnil()) inner.rawset(k, v);
                }
                LuaTable t = new LuaTable();
                t.rawset(ls("map"), inner);
                return t;
            }
            case '|': {
                long n = parseLen(head);
                for (long i = 0; i < n; i++) { parseReply(p, pos); parseReply(p, pos); }
                return parseReply(p, pos);
            }
            default:
                return LuaValue.NIL;
        }
    }

    private static long parseLen(String h) {
        try { return Long.parseLong(h.trim()); } catch (NumberFormatException e) { return -1; }
    }

    // ==================================================================
    // Lua -> RESP (script_lua.c luaReplyToRedisReply)
    // ==================================================================
    void luaReplyToRedis(Client c, int scriptResp, LuaValue v, int depth) {
        if (depth > MAX_REPLY_DEPTH) {
            c.addError("ERR reached lua stack limit");
            return;
        }
        switch (v.type()) {
            case LuaValue.TSTRING:
                c.addBulk(lbytes(v));
                return;
            case LuaValue.TBOOLEAN:
                if (scriptResp == 2) {
                    if (v.toboolean()) c.addInt(1);
                    else c.addNil();
                } else {
                    c.addBool(v.toboolean());
                }
                return;
            case LuaValue.TNUMBER:
                c.addInt((long) v.todouble());
                return;
            case LuaValue.TTABLE:
                break;
            default:
                c.addNil();
                return;
        }

        LuaValue f = v.rawget(ls("err"));
        if (f.type() == LuaValue.TSTRING) {
            String msg = normalizeErr(Util.s(lbytes(f)));
            byte[] out = Util.b(msg.startsWith("-") ? msg.substring(1) : msg);
            for (int i = 0; i < out.length; i++) if (out[i] == '\r' || out[i] == '\n') out[i] = ' ';
            c.addErr(out);
            if (!v.rawget(ls("ignore_error_stats_update")).toboolean())
                s.errorStats.merge(errCode(Util.s(out)), 1L, Long::sum);
            return;
        }
        f = v.rawget(ls("ok"));
        if (f.type() == LuaValue.TSTRING) {
            byte[] out = lbytes(f);
            for (int i = 0; i < out.length; i++) if (out[i] == '\r' || out[i] == '\n') out[i] = ' ';
            c.addSimple(Util.s(out));
            return;
        }
        f = v.rawget(ls("double"));
        if (f.type() == LuaValue.TNUMBER) { c.addDouble(f.todouble()); return; }
        f = v.rawget(ls("big_number"));
        if (f.type() == LuaValue.TSTRING) {
            byte[] out = lbytes(f);
            for (int i = 0; i < out.length; i++) if (out[i] == '\r' || out[i] == '\n') out[i] = ' ';
            c.addBigNumber(Util.s(out));
            return;
        }
        f = v.rawget(ls("verbatim_string"));
        if (f.istable()) {
            LuaValue fmt = f.rawget(ls("format"));
            LuaValue str = f.rawget(ls("string"));
            if (fmt.type() == LuaValue.TSTRING && str.type() == LuaValue.TSTRING) {
                String sf = Util.s(lbytes(fmt));
                if (sf.length() < 3) sf = (sf + "txt").substring(0, 3);
                c.addVerbatim(sf, lbytes(str));
                return;
            }
        }
        f = v.rawget(ls("map"));
        if (f.istable()) {
            LuaTable m = f.checktable();
            List<LuaValue[]> items = new ArrayList<LuaValue[]>();
            LuaValue k = LuaValue.NIL;
            while (true) {
                Varargs n = m.next(k);
                k = n.arg1();
                if (k.isnil()) break;
                items.add(new LuaValue[]{k, n.arg(2)});
            }
            c.addMap(items.size());
            for (int i = 0; i < items.size(); i++) {
                luaReplyToRedis(c, scriptResp, items.get(i)[0], depth + 1);
                luaReplyToRedis(c, scriptResp, items.get(i)[1], depth + 1);
            }
            return;
        }
        f = v.rawget(ls("set"));
        if (f.istable()) {
            LuaTable m = f.checktable();
            List<LuaValue> items = new ArrayList<LuaValue>();
            LuaValue k = LuaValue.NIL;
            while (true) {
                Varargs n = m.next(k);
                k = n.arg1();
                if (k.isnil()) break;
                items.add(k);
            }
            c.addSet(items.size());
            for (int i = 0; i < items.size(); i++) luaReplyToRedis(c, scriptResp, items.get(i), depth + 1);
            return;
        }
        int n = 0;
        while (!v.rawget(n + 1).isnil()) n++;
        c.addArray(n);
        for (int i = 1; i <= n; i++) luaReplyToRedis(c, scriptResp, v.rawget(i), depth + 1);
    }

    // ==================================================================
    // shebang parsing (eval.c evalExtractShebangFlags)
    // ==================================================================
    static final class Shebang {
        int flags = SF_EVAL_COMPAT_MODE;
        int len = 0;
    }

    static Shebang extractShebang(byte[] body) {
        Shebang sb = new Shebang();
        if (body.length < 2 || body[0] != '#' || body[1] != '!') return sb;
        int nl = -1;
        for (int i = 0; i < body.length; i++) if (body[i] == '\n') { nl = i; break; }
        if (nl < 0) throw new RedisError("ERR Invalid script shebang");
        sb.len = nl;
        List<String> parts;
        try {
            parts = Config.splitArgs(Util.s(body, 0, nl));
        } catch (RuntimeException e) {
            throw new RedisError("ERR Invalid engine in script shebang");
        }
        if (parts.isEmpty()) throw new RedisError("ERR Invalid engine in script shebang");
        if (!parts.get(0).equals("#!lua"))
            throw rerr("ERR Unexpected engine in script shebang: " + parts.get(0));
        sb.flags &= ~SF_EVAL_COMPAT_MODE;
        for (int j = 1; j < parts.size(); j++) {
            String part = parts.get(j);
            if (part.startsWith("flags=")) {
                String[] fl = part.substring(6).split(",", -1);
                for (int x = 0; x < fl.length; x++) {
                    int f = flagByName(fl[x]);
                    if (f == 0) throw rerr("ERR Unexpected flag in script shebang: " + fl[x]);
                    sb.flags |= f;
                }
            } else {
                throw rerr("ERR Unknown lua shebang option: " + part);
            }
        }
        return sb;
    }

    static int flagByName(String f) {
        if (f.equals("no-writes")) return SF_NO_WRITES;
        if (f.equals("allow-oom")) return SF_ALLOW_OOM;
        if (f.equals("allow-stale")) return SF_ALLOW_STALE;
        if (f.equals("no-cluster")) return SF_NO_CLUSTER;
        if (f.equals("allow-cross-slot-keys")) return SF_ALLOW_CROSS_SLOT;
        return 0;
    }

    static String flagName(int f) {
        if (f == SF_NO_WRITES) return "no-writes";
        if (f == SF_ALLOW_OOM) return "allow-oom";
        if (f == SF_ALLOW_STALE) return "allow-stale";
        if (f == SF_NO_CLUSTER) return "no-cluster";
        if (f == SF_ALLOW_CROSS_SLOT) return "allow-cross-slot-keys";
        return "";
    }

    static final int[] ALL_FLAGS = {SF_NO_WRITES, SF_ALLOW_OOM, SF_ALLOW_STALE, SF_NO_CLUSTER, SF_ALLOW_CROSS_SLOT};

    // ==================================================================
    // EVAL / EVALSHA
    // ==================================================================
    public String createFunction(byte[] body) { return createFunction(body, false); }

    /**
     * eval.c luaCreateFunction(): compile, cache and return the sha1.  Scripts
     * born from EVAL take part in the 500 entry LRU, SCRIPT LOAD ones do not.
     */
    public String createFunction(byte[] body, boolean scriptLoad) {
        ensureInit();
        String sha = Util.s(Util.sha1hex(body));
        if (scripts.containsKey(sha)) return sha;
        Shebang sb = extractShebang(body);
        byte[] src = sb.len == 0 ? body : Arrays.copyOfRange(body, sb.len, body.length);
        LuaValue fn;
        try {
            fn = G.load(new ByteArrayInputStream(src), "@user_script", "t", evalEnv);
        } catch (LuaError e) {
            throw rerr("ERR Error compiling script (new function): "
                    + cleanCompileErr(normalizeErr(e.getMessage())));
        } catch (RuntimeException e) {
            throw rerr("ERR Error compiling script (new function): " + e.getMessage());
        }
        if (!scriptLoad) {
            /* Redis 2.8 never evicts from server.lua_scripts: the LRU of EVAL
             * born scripts only exists from 7.0 on. */
            scriptsLru.add(sha);
        }
        scripts.put(sha, Util.copy(body));
        compiled.put(sha, fn);
        scriptFlagsBySha.put(sha, sb.flags);
        return sha;
    }

    public void evalCommand(Client c, boolean evalsha, boolean ro) {
        ensureInit();
        if (evalsha && c.argv[1].length != 40) throw new RedisError(NOSCRIPT_ERR);

        Long nk = Util.string2ll(c.argv[2]);
        if (nk == null) throw RedisError.notInt();
        long numkeys = nk.longValue();
        if (numkeys > c.argc - 3)
            throw new RedisError("ERR Number of keys can't be greater than number of args");
        if (numkeys < 0)
            throw new RedisError("ERR Number of keys can't be negative");

        String sha;
        if (evalsha) {
            sha = Util.s(c.argv[1]).toLowerCase(Locale.US);
            if (!scripts.containsKey(sha)) throw new RedisError(NOSCRIPT_ERR);
        } else {
            sha = createFunction(c.argv[1]);
        }
        LuaValue fn = compiled.get(sha);
        if (fn == null) throw new RedisError(NOSCRIPT_ERR);
        Integer fl = scriptFlagsBySha.get(sha);
        int flags = fl == null ? SF_EVAL_COMPAT_MODE : fl.intValue();
        if (scriptsLru.remove(sha)) scriptsLru.add(sha);   /* move to the LRU tail */

        byte[][] keys = new byte[(int) numkeys][];
        for (int i = 0; i < numkeys; i++) keys[i] = c.argv[3 + i];
        int nargs = (int) (c.argc - 3 - numkeys);
        byte[][] args = new byte[nargs][];
        for (int i = 0; i < nargs; i++) args[i] = c.argv[(int) (3 + numkeys) + i];

        if (!prepareForRun(c, sha, flags, ro, true)) return;
        try {
            evalEnv.force("KEYS", arrayTable(keys));
            evalEnv.force("ARGV", arrayTable(args));
            callAndReply(c, fn, null, null);
        } finally {
            resetRun();
        }
    }

    static LuaTable arrayTable(byte[][] a) {
        LuaTable t = new LuaTable(a.length, 0);
        for (int i = 0; i < a.length; i++) t.rawset(i + 1, lstr(a[i]));
        return t;
    }

    /** script.c scriptPrepareForRun() */
    boolean prepareForRun(Client caller, String funcname, int flags, boolean ro, boolean evalMode) {
        if ((flags & SF_EVAL_COMPAT_MODE) == 0 && (flags & SF_NO_WRITES) == 0) {
            if (s.repl.masterHost != null && s.config.getBool("replica-read-only")
                    && !s.repl.isMasterClient(caller)) {
                caller.addError("READONLY Can not run script with write flag on readonly replica");
                return false;
            }
            if (ro) {
                caller.addError("ERR Can not execute a script with write flag using *_ro command.");
                return false;
            }
        }
        if (evalMode) {
            /* scripting.c evalGenericCommand(): "We want the same PRNG sequence at
             * every call so that our PRNG is not affected by external state." */
            srand48(0);
        }
        RunCtx r = new RunCtx();
        r.caller = caller;
        r.funcname = funcname;
        r.evalMode = evalMode;
        r.readOnly = ro || ((flags & SF_EVAL_COMPAT_MODE) == 0 && (flags & SF_NO_WRITES) != 0);
        r.start = System.currentTimeMillis();
        r.dirtyStart = s.dirty;
        /* scripting.c: with lua-always-replicate-commands on, the server calls
         * scriptingEnableReplicateCommands() for the script rather than waiting
         * for it to ask -- including that call's PRNG reseed. */
        if (ServerCmds.luaAlwaysReplicateCommands) {
            r.replicateCommands = true;
            srand48(SEED_SOURCE.nextInt());
        }
        r.fake = new Client();
        r.fake.fake = true;
        r.fake.db = caller.db;
        r.fake.resp = 2;
        r.fake.user = caller.user;
        r.fake.authenticated = true;
        cur = r;
        busyLimit = s.config.getLong("lua-time-limit");
        if (evalMode) evalEnv.debuglib = timeHook; else fnEnv.debuglib = timeHook;
        return true;
    }

    /** script.c scriptResetRun(): preventCommandPropagation(run_ctx->original_client) */
    void resetRun() {
        RunCtx r = cur;
        cur = null;
        evalEnv.debuglib = null;
        fnEnv.debuglib = null;
        timedout = false;
        killRequested = false;
        killLogged = false;
        /* The effects were already propagated one by one, so the EVAL/FCALL
         * itself must not be replicated verbatim.  Server.call() only calls
         * propagate() when the dirty counter moved, so only arm the override
         * in that case (otherwise it would leak into the next command). */
        s.replOverride = (r != null && s.dirty != r.dirtyStart) ? new byte[0][] : null;
    }

    /** script_lua.c luaCallFunction() */
    void callAndReply(Client c, LuaValue fn, LuaTable keys, LuaTable args) {
        try {
            LuaValue res = (keys == null) ? fn.call() : fn.call(keys, args);
            luaReplyToRedis(c, cur.fake.resp, res, 0);
        } catch (LuaError e) {
            replyError(c, e);
        } catch (StackOverflowError e) {
            c.addError("ERR reached lua stack limit");
        } catch (RedisError e) {
            c.addErr(e.msg);
        } catch (RuntimeException e) {
            c.addError("ERR Error running script " + (cur == null ? "" : cur.funcname) + ", " + e);
        }
    }

    private void replyError(Client c, LuaError e) {
        LuaValue obj = e.getMessageObject();
        String raw = e.getMessage();
        String msg;
        boolean ignoreStats = false;
        if (obj != null && obj.istable()) {
            LuaValue er = obj.rawget(ls("err"));
            msg = er.isstring() ? normalizeErr(Util.s(lbytes(er))) : "ERR unknown error";
            ignoreStats = obj.rawget(ls("ignore_error_stats_update")).toboolean();
        } else {
            msg = "ERR " + normalizeErr(raw);
        }
        if (msg.startsWith("-")) msg = msg.substring(1);
        int line = errLine(raw);
        String source = errSource(raw);
        StringBuilder sb = new StringBuilder(msg);
        if (line > 0 && source != null) {
            sb.append(" script: ").append(cur == null ? "" : cur.funcname)
              .append(", on @").append(source).append(':').append(line).append('.');
        }
        byte[] out = Util.b(sb.toString());
        for (int i = 0; i < out.length; i++) if (out[i] == '\r' || out[i] == '\n') out[i] = ' ';
        c.addErr(out);
        if (!ignoreStats) s.errorStats.merge(errCode(Util.s(out)), 1L, Long::sum);
    }

    public void scriptFlush() {
        ensureInit();
        scripts.clear();
        compiled.clear();
        scriptFlagsBySha.clear();
        scriptsLru.clear();
    }

    // ==================================================================
    // FUNCTION
    // ==================================================================
    /** functions.c functionsVerifyName() */
    static boolean verifyName(byte[] name) {
        if (name.length == 0) return false;
        for (int i = 0; i < name.length; i++) {
            byte b = name[i];
            if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_') continue;
            return false;
        }
        return true;
    }

    static final String BAD_NAME =
            "Library names can only contain letters, numbers, or underscores(_) and must be at least one character long";

    public String functionLoad(byte[] payload, boolean replace) {
        return functionLoad(payload, replace, false);
    }

    /**
     * functions.c functionExtractLibMetaData() + functionsCreateWithLibraryCtx().
     * FUNCTION RESTORE goes through libraryJoin(), which words the collision
     * error differently ("Library %s" instead of "Library '%S'").
     */
    public String functionLoad(byte[] payload, boolean replace, boolean restore) {
        ensureInit();
        if (payload.length < 2 || payload[0] != '#' || payload[1] != '!')
            throw new RedisError("ERR Missing library metadata");
        int nl = -1;
        for (int i = 0; i < payload.length; i++) if (payload[i] == '\n') { nl = i; break; }
        if (nl < 0) throw new RedisError("ERR Invalid library metadata");
        List<String> parts;
        try {
            parts = Config.splitArgs(Util.s(payload, 0, nl));
        } catch (RuntimeException e) {
            throw new RedisError("ERR Invalid library metadata");
        }
        if (parts.isEmpty()) throw new RedisError("ERR Invalid library metadata");
        String engine = parts.get(0).substring(2);
        String name = null;
        for (int i = 1; i < parts.size(); i++) {
            String part = parts.get(i);
            if (part.length() >= 5 && part.substring(0, 5).equalsIgnoreCase("name=")) {
                if (name != null)
                    throw new RedisError("ERR Invalid metadata value, name argument was given multiple times");
                name = part.substring(5);
                continue;
            }
            throw rerr("ERR Invalid metadata value given: " + part);
        }
        if (name == null) throw new RedisError("ERR Library name was not given");
        if (!verifyName(Util.b(name))) throw new RedisError("ERR " + BAD_NAME);
        if (!engine.equalsIgnoreCase("LUA")) throw rerr("ERR Engine '" + engine + "' not found");

        LibInfo old = libraries.get(name);
        if (old != null && !replace)
            throw rerr(restore ? "ERR Library " + name + " already exists"
                               : "ERR Library '" + name + "' already exists");

        LibInfo li = new LibInfo();
        li.name = name;
        li.code = Util.copy(payload);
        byte[] src = Arrays.copyOfRange(payload, nl, payload.length);

        LuaValue chunk;
        try {
            chunk = G.load(new ByteArrayInputStream(src), "@user_function", "t", fnEnv);
        } catch (LuaError e) {
            throw rerr("ERR Error compiling function: "
                    + cleanCompileErr(normalizeErr(e.getMessage())));
        } catch (RuntimeException e) {
            throw rerr("ERR Error compiling function: " + e.getMessage());
        }

        LibInfo savedLib = loadingLib;
        long savedDeadline = loadDeadline;
        loadingLib = li;
        loadDeadline = System.currentTimeMillis() + LOAD_TIMEOUT_MS;
        fnMeta.force("__index", libApi);
        fnEnv.debuglib = hook;
        try {
            chunk.call();
        } catch (LuaError e) {
            LuaValue o = e.getMessageObject();
            String m;
            if (o != null && o.istable()) {
                LuaValue er = o.rawget(ls("err"));
                m = er.isstring() ? normalizeErr(Util.s(lbytes(er))) : "ERR unknown error";
            } else {
                m = "ERR " + normalizeErr(e.getMessage());
            }
            throw rerr("ERR Error registering functions: " + m);
        } catch (StackOverflowError e) {
            throw new RedisError("ERR Error registering functions: ERR stack overflow");
        } finally {
            fnEnv.debuglib = null;
            fnMeta.force("__index", fnGlobals);
            loadingLib = savedLib;
            loadDeadline = savedDeadline;
        }

        if (li.funcs.isEmpty()) throw new RedisError("ERR No functions registered");

        for (FuncInfo fi : li.funcs.values()) {
            FuncInfo clash = functions.get(fi.name.toLowerCase(Locale.US));
            if (clash != null && (old == null || clash.lib != old))
                throw rerr("ERR Function " + fi.name + " already exists");
        }

        if (old != null) {
            for (FuncInfo fi : old.funcs.values()) functions.remove(fi.name.toLowerCase(Locale.US));
            libraries.remove(old.name);
        }
        libraries.put(name, li);
        for (FuncInfo fi : li.funcs.values()) functions.put(fi.name.toLowerCase(Locale.US), fi);
        return name;
    }

    /** function_lua.c luaRegisterFunction() */
    Varargs registerFunction(Varargs a) {
        if (loadingLib == null)
            throw pushError("redis.register_function can only be called on FUNCTION LOAD command");
        int argc = a.narg();
        if (argc < 1 || argc > 2) throw pushError("wrong number of arguments to redis.register_function");

        String name;
        byte[] desc = null;
        LuaValue cb;
        int flags = 0;

        if (argc == 1) {
            LuaValue t = a.arg1();
            if (!t.istable())
                throw pushError("calling redis.register_function with a single argument is only applicable to Lua table (representing named arguments).");
            LuaTable tt = t.checktable();
            String fname = null;
            LuaValue callback = null;
            LuaValue k = LuaValue.NIL;
            while (true) {
                Varargs n = tt.next(k);
                k = n.arg1();
                if (k.isnil()) break;
                LuaValue v = n.arg(2);
                if (!k.isstring())
                    throw pushError("named argument key given to redis.register_function is not a string");
                String key = k.tojstring();
                if (key.equalsIgnoreCase("function_name")) {
                    if (v.type() != LuaValue.TSTRING)
                        throw pushError("function_name argument given to redis.register_function must be a string");
                    fname = Util.s(lbytes(v));
                } else if (key.equalsIgnoreCase("description")) {
                    if (v.type() != LuaValue.TSTRING)
                        throw pushError("description argument given to redis.register_function must be a string");
                    desc = lbytes(v);
                } else if (key.equalsIgnoreCase("callback")) {
                    if (!v.isfunction())
                        throw pushError("callback argument given to redis.register_function must be a function");
                    callback = v;
                } else if (key.equalsIgnoreCase("flags")) {
                    if (!v.istable())
                        throw pushError("flags argument to redis.register_function must be a table representing function flags");
                    flags = readFlags(v.checktable());
                } else {
                    throw pushError("unknown argument given to redis.register_function");
                }
            }
            if (fname == null) throw pushError("redis.register_function must get a function name argument");
            if (callback == null) throw pushError("redis.register_function must get a callback argument");
            name = fname;
            cb = callback;
        } else {
            LuaValue nv = a.arg1();
            if (nv.type() != LuaValue.TSTRING)
                throw pushError("first argument to redis.register_function must be a string");
            if (!a.arg(2).isfunction())
                throw pushError("second argument to redis.register_function must be a function");
            name = Util.s(lbytes(nv));
            cb = a.arg(2);
        }

        if (!verifyName(Util.b(name))) throw pushError(BAD_NAME);
        LibInfo li = loadingLib;
        if (li.funcs.containsKey(name)) throw pushError("Function already exists in the library");
        FuncInfo fi = new FuncInfo();
        fi.name = name;
        fi.desc = desc;
        fi.callback = cb;
        fi.flags = flags;
        fi.lib = li;
        li.funcs.put(name, fi);
        return LuaValue.NONE;
    }

    private static int readFlags(LuaTable t) {
        int f = 0;
        int j = 1;
        while (true) {
            LuaValue v = t.get(j++);
            if (v.isnil()) break;
            if (!v.isstring()) throw pushError("unknown flag given");
            String sv = v.tojstring();
            int one = 0;
            for (int i = 0; i < ALL_FLAGS.length; i++) {
                if (sv.equalsIgnoreCase(flagName(ALL_FLAGS[i]))) { one = ALL_FLAGS[i]; break; }
            }
            if (one == 0) throw pushError("unknown flag given");
            f |= one;
        }
        return f;
    }

    public void fcallCommand(Client c, boolean ro) {
        ensureInit();
        FuncInfo fi = functions.get(Util.lower(c.argv[1]));
        if (fi == null) throw new RedisError("ERR Function not found");
        Long nk = Util.string2ll(c.argv[2]);
        if (nk == null) throw new RedisError("ERR Bad number of keys provided");
        long numkeys = nk.longValue();
        if (numkeys > c.argc - 3)
            throw new RedisError("ERR Number of keys can't be greater than number of args");
        if (numkeys < 0)
            throw new RedisError("ERR Number of keys can't be negative");

        byte[][] keys = new byte[(int) numkeys][];
        for (int i = 0; i < numkeys; i++) keys[i] = c.argv[3 + i];
        int nargs = (int) (c.argc - 3 - numkeys);
        byte[][] args = new byte[nargs][];
        for (int i = 0; i < nargs; i++) args[i] = c.argv[(int) (3 + numkeys) + i];

        int flags = fi.flags == 0 ? 0 : fi.flags;   /* functions never run in compat mode */
        if (!prepareForRun(c, fi.name, flags, ro, false)) return;
        try {
            callAndReply(c, fi.callback, arrayTable(keys), arrayTable(args));
        } finally {
            resetRun();
        }
    }

    public void functionFlush() {
        libraries.clear();
        functions.clear();
    }

    public void functionDelete(String name) {
        LibInfo li = libraries.remove(name);
        if (li == null) throw new RedisError("ERR Library not found");
        for (FuncInfo fi : li.funcs.values()) functions.remove(fi.name.toLowerCase(Locale.US));
    }

    // ==================================================================
    // cjson (deps/lua/src/lua_cjson.c)
    // ==================================================================
    static final class Json {
        /* lua_cjson.c char2escape */
        static final String[] ESC = new String[256];
        static {
            for (int i = 0; i < 0x20; i++) ESC[i] = String.format("\\u%04x", i);
            ESC[8] = "\\b"; ESC[9] = "\\t"; ESC[10] = "\\n"; ESC[12] = "\\f"; ESC[13] = "\\r";
            ESC['"'] = "\\\"";
            ESC['/'] = "\\/";
            ESC['\\'] = "\\\\";
            ESC[0x7f] = "\\u007f";
        }

        static RTable build(final Scripting sc) {
            final RTable t = new RTable();
            sc.jsonNull = LuaValue.userdataOf(new Object() {
                @Override public String toString() { return "userdata: (nil)"; }
            });
            t.force("null", sc.jsonNull);
            t.force("_NAME", ls("cjson"));
            t.force("_VERSION", ls("2.1.0"));
            t.force("encode", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    if (a.narg() != 1)
                        throw new LuaError("bad argument #1 to 'encode' (expected 1 argument)");
                    Buf b = new Buf(64);
                    appendData(sc, b, a.arg1(), 0);
                    return lstr(b.toBytes());
                }
            });
            t.force("decode", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    LuaValue v = a.arg1();
                    if (v.type() != LuaValue.TSTRING && v.type() != LuaValue.TNUMBER)
                        throw new LuaError("bad argument #1 to 'decode' (string expected)");
                    return decode(sc, lbytes(v));
                }
            });
            t.force("encode_keep_buffer", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    boolean old = sc.jsonKeepBuffer;
                    if (a.narg() >= 1 && !a.arg1().isnil()) sc.jsonKeepBuffer = a.arg1().toboolean();
                    return LuaValue.valueOf(old);
                }
            });
            t.force("encode_max_depth", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int old = sc.jsonEncodeMaxDepth;
                    if (a.narg() >= 1 && !a.arg1().isnil()) sc.jsonEncodeMaxDepth = (int) a.arg1().checkdouble();
                    return LuaValue.valueOf(old);
                }
            });
            t.force("decode_max_depth", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int old = sc.jsonDecodeMaxDepth;
                    if (a.narg() >= 1 && !a.arg1().isnil()) sc.jsonDecodeMaxDepth = (int) a.arg1().checkdouble();
                    return LuaValue.valueOf(old);
                }
            });
            t.force("encode_number_precision", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int old = sc.jsonPrecision;
                    if (a.narg() >= 1 && !a.arg1().isnil()) {
                        int p = (int) a.arg1().checkdouble();
                        if (p < 1 || p > 14) throw new LuaError("bad argument #1 to 'encode_number_precision' (expected integer between 1 and 14)");
                        sc.jsonPrecision = p;
                    }
                    return LuaValue.valueOf(old);
                }
            });
            t.force("encode_invalid_numbers", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int old = sc.jsonEncodeInvalidNumbers;
                    if (a.narg() >= 1 && !a.arg1().isnil()) sc.jsonEncodeInvalidNumbers = optNanInf(a.arg1());
                    return old == 2 ? (Varargs) ls("null") : (Varargs) LuaValue.valueOf(old == 1);
                }
            });
            t.force("decode_invalid_numbers", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int old = sc.jsonDecodeInvalidNumbers;
                    if (a.narg() >= 1 && !a.arg1().isnil()) sc.jsonDecodeInvalidNumbers = optNanInf(a.arg1());
                    return LuaValue.valueOf(old != 0);
                }
            });
            t.force("encode_sparse_array", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int oc = sc.jsonSparseConvert, or = sc.jsonSparseRatio, os = sc.jsonSparseSafe;
                    if (a.narg() >= 1 && !a.arg1().isnil()) sc.jsonSparseConvert = a.arg1().toboolean() ? 1 : 0;
                    if (a.narg() >= 2 && !a.arg(2).isnil()) sc.jsonSparseRatio = (int) a.arg(2).checkdouble();
                    if (a.narg() >= 3 && !a.arg(3).isnil()) sc.jsonSparseSafe = (int) a.arg(3).checkdouble();
                    return LuaValue.varargsOf(new LuaValue[]{
                            LuaValue.valueOf(oc != 0), LuaValue.valueOf(or), LuaValue.valueOf(os)});
                }
            });
            t.force("new", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return t; }
            });
            t.lock();
            return t;
        }

        static int optNanInf(LuaValue v) {
            if (v.type() == LuaValue.TSTRING) {
                String sv = v.tojstring();
                if (sv.equals("off")) return 0;
                if (sv.equals("on")) return 1;
                if (sv.equals("null")) return 2;
                throw new LuaError("bad argument (invalid option '" + sv + "')");
            }
            return v.toboolean() ? 1 : 0;
        }

        static void appendString(Buf out, byte[] v) {
            out.put((byte) '"');
            for (int i = 0; i < v.length; i++) {
                String e = ESC[v[i] & 0xff];
                if (e != null) out.put(e);
                else out.put(v[i]);
            }
            out.put((byte) '"');
        }

        static void appendNumber(Scripting sc, Buf out, double num) {
            if (sc.jsonEncodeInvalidNumbers == 0) {
                if (Double.isInfinite(num) || Double.isNaN(num))
                    throw new LuaError("Cannot serialise number: must not be NaN or Inf");
            } else if (sc.jsonEncodeInvalidNumbers == 1) {
                if (Double.isNaN(num)) { out.put("nan"); return; }
            } else {
                if (Double.isInfinite(num) || Double.isNaN(num)) { out.put("null"); return; }
            }
            out.put(fmtG(num, sc.jsonPrecision));
        }

        /** lua_cjson.c lua_array_length() */
        static int arrayLength(Scripting sc, LuaTable t) {
            int max = 0, items = 0;
            LuaValue k = LuaValue.NIL;
            while (true) {
                Varargs n = t.next(k);
                k = n.arg1();
                if (k.isnil()) break;
                if (k.type() == LuaValue.TNUMBER) {
                    double d = k.todouble();
                    if (Math.floor(d) == d && d >= 1) {
                        if (d > max) max = (int) d;
                        items++;
                        continue;
                    }
                }
                return -1;
            }
            if (sc.jsonSparseRatio > 0 && max > items * sc.jsonSparseRatio && max > sc.jsonSparseSafe) {
                if (sc.jsonSparseConvert == 0)
                    throw new LuaError("Cannot serialise table: excessively sparse array");
                return -1;
            }
            return max;
        }

        static void appendData(Scripting sc, Buf out, LuaValue v, int depth) {
            switch (v.type()) {
                case LuaValue.TSTRING:
                    appendString(out, lbytes(v));
                    return;
                case LuaValue.TNUMBER:
                    appendNumber(sc, out, v.todouble());
                    return;
                case LuaValue.TBOOLEAN:
                    out.put(v.toboolean() ? "true" : "false");
                    return;
                case LuaValue.TNIL:
                    out.put("null");
                    return;
                case LuaValue.TTABLE: {
                    depth++;
                    if (depth > sc.jsonEncodeMaxDepth)
                        throw new LuaError("Cannot serialise, excessive nesting (" + depth + ")");
                    LuaTable t = v.checktable();
                    int len = arrayLength(sc, t);
                    if (len > 0) {
                        out.put((byte) '[');
                        for (int i = 1; i <= len; i++) {
                            if (i > 1) out.put((byte) ',');
                            appendData(sc, out, t.get(i), depth);
                        }
                        out.put((byte) ']');
                    } else {
                        out.put((byte) '{');
                        boolean first = true;
                        LuaValue k = LuaValue.NIL;
                        while (true) {
                            Varargs n = t.next(k);
                            k = n.arg1();
                            if (k.isnil()) break;
                            if (!first) out.put((byte) ',');
                            first = false;
                            if (k.type() == LuaValue.TNUMBER) {
                                out.put((byte) '"');
                                appendNumber(sc, out, k.todouble());
                                out.put("\":");
                            } else if (k.type() == LuaValue.TSTRING) {
                                appendString(out, lbytes(k));
                                out.put((byte) ':');
                            } else {
                                throw new LuaError("Cannot serialise " + k.typename()
                                        + ": table key must be a number or string");
                            }
                            appendData(sc, out, n.arg(2), depth);
                        }
                        out.put((byte) '}');
                    }
                    return;
                }
                default:
                    if (v == sc.jsonNull) { out.put("null"); return; }
                    throw new LuaError("Cannot serialise " + v.typename() + ": type not supported");
            }
        }

        // ---------------- decoding ----------------
        static LuaValue decode(Scripting sc, byte[] j) {
            int[] p = {0};
            LuaValue v = parseValue(sc, j, p, 1);
            skipWs(j, p);
            if (p[0] != j.length)
                throw new LuaError("Expected the end but found invalid token at character " + (p[0] + 1));
            return v;
        }

        static void skipWs(byte[] j, int[] p) {
            while (p[0] < j.length) {
                byte c = j[p[0]];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p[0]++;
                else break;
            }
        }

        static LuaValue parseValue(Scripting sc, byte[] j, int[] p, int depth) {
            if (depth > sc.jsonDecodeMaxDepth)
                throw new LuaError("Found too many nested data structures (" + depth + ") at character " + (p[0] + 1));
            skipWs(j, p);
            if (p[0] >= j.length)
                throw new LuaError("Expected value but found T_END at character " + (p[0] + 1));
            byte c = j[p[0]];
            switch (c) {
                case '{': {
                    p[0]++;
                    LuaTable t = new LuaTable();
                    skipWs(j, p);
                    if (p[0] < j.length && j[p[0]] == '}') { p[0]++; return t; }
                    while (true) {
                        skipWs(j, p);
                        if (p[0] >= j.length || j[p[0]] != '"')
                            throw new LuaError("Expected object key string but found invalid token at character " + (p[0] + 1));
                        LuaValue k = parseString(j, p);
                        skipWs(j, p);
                        if (p[0] >= j.length || j[p[0]] != ':')
                            throw new LuaError("Expected colon but found invalid token at character " + (p[0] + 1));
                        p[0]++;
                        LuaValue v = parseValue(sc, j, p, depth + 1);
                        t.rawset(k, v);
                        skipWs(j, p);
                        if (p[0] < j.length && j[p[0]] == ',') { p[0]++; continue; }
                        if (p[0] < j.length && j[p[0]] == '}') { p[0]++; return t; }
                        throw new LuaError("Expected comma or object end but found invalid token at character " + (p[0] + 1));
                    }
                }
                case '[': {
                    p[0]++;
                    LuaTable t = new LuaTable();
                    skipWs(j, p);
                    if (p[0] < j.length && j[p[0]] == ']') { p[0]++; return t; }
                    int idx = 1;
                    while (true) {
                        LuaValue v = parseValue(sc, j, p, depth + 1);
                        t.rawset(idx++, v);
                        skipWs(j, p);
                        if (p[0] < j.length && j[p[0]] == ',') { p[0]++; continue; }
                        if (p[0] < j.length && j[p[0]] == ']') { p[0]++; return t; }
                        throw new LuaError("Expected comma or array end but found invalid token at character " + (p[0] + 1));
                    }
                }
                case '"':
                    return parseString(j, p);
                default: {
                    if (match(j, p, "true")) return LuaValue.TRUE;
                    if (match(j, p, "false")) return LuaValue.FALSE;
                    if (match(j, p, "null")) return sc.jsonNull;
                    if (sc.jsonDecodeInvalidNumbers != 0) {
                        if (match(j, p, "nan")) return LuaValue.valueOf(Double.NaN);
                        if (match(j, p, "-nan")) return LuaValue.valueOf(Double.NaN);
                        if (match(j, p, "inf")) return LuaValue.valueOf(Double.POSITIVE_INFINITY);
                        if (match(j, p, "-inf")) return LuaValue.valueOf(Double.NEGATIVE_INFINITY);
                        if (match(j, p, "Infinity")) return LuaValue.valueOf(Double.POSITIVE_INFINITY);
                        if (match(j, p, "-Infinity")) return LuaValue.valueOf(Double.NEGATIVE_INFINITY);
                    }
                    return parseNumber(j, p);
                }
            }
        }

        static boolean match(byte[] j, int[] p, String w) {
            if (p[0] + w.length() > j.length) return false;
            for (int i = 0; i < w.length(); i++) if (j[p[0] + i] != w.charAt(i)) return false;
            p[0] += w.length();
            return true;
        }

        static LuaValue parseNumber(byte[] j, int[] p) {
            int st = p[0];
            int i = p[0];
            if (i < j.length && (j[i] == '-' || j[i] == '+')) i++;
            boolean any = false;
            while (i < j.length && j[i] >= '0' && j[i] <= '9') { i++; any = true; }
            if (i < j.length && j[i] == '.') {
                i++;
                while (i < j.length && j[i] >= '0' && j[i] <= '9') { i++; any = true; }
            }
            if (any && i < j.length && (j[i] == 'e' || j[i] == 'E')) {
                int save = i;
                i++;
                if (i < j.length && (j[i] == '-' || j[i] == '+')) i++;
                boolean e = false;
                while (i < j.length && j[i] >= '0' && j[i] <= '9') { i++; e = true; }
                if (!e) i = save;
            }
            if (!any)
                throw new LuaError("Expected value but found invalid token at character " + (st + 1));
            String num = Util.s(j, st, i - st);
            p[0] = i;
            try {
                return LuaValue.valueOf(Double.parseDouble(num));
            } catch (NumberFormatException e) {
                throw new LuaError("Expected value but found invalid number '" + num
                        + "' at character " + (st + 1));
            }
        }

        static LuaValue parseString(byte[] j, int[] p) {
            int st = p[0];
            p[0]++;  // opening quote
            Buf out = new Buf(16);
            while (true) {
                if (p[0] >= j.length)
                    throw new LuaError("Expected value but found unterminated string at character " + (st + 1));
                byte c = j[p[0]++];
                if (c == '"') break;
                if (c != '\\') { out.put(c); continue; }
                if (p[0] >= j.length)
                    throw new LuaError("Expected value but found unterminated string at character " + (st + 1));
                byte e = j[p[0]++];
                switch (e) {
                    case '"': out.put((byte) '"'); break;
                    case '\\': out.put((byte) '\\'); break;
                    case '/': out.put((byte) '/'); break;
                    case 'b': out.put((byte) 8); break;
                    case 'f': out.put((byte) 12); break;
                    case 'n': out.put((byte) 10); break;
                    case 'r': out.put((byte) 13); break;
                    case 't': out.put((byte) 9); break;
                    case 'u': {
                        int cp = hex4(j, p, st);
                        if (cp >= 0xD800 && cp <= 0xDBFF && p[0] + 1 < j.length
                                && j[p[0]] == '\\' && j[p[0] + 1] == 'u') {
                            int save = p[0];
                            p[0] += 2;
                            int lo = hex4(j, p, st);
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else p[0] = save;
                        }
                        utf8(out, cp);
                        break;
                    }
                    default:
                        throw new LuaError("Expected value but found invalid escape code at character " + p[0]);
                }
            }
            return lstr(out.toBytes());
        }

        static int hex4(byte[] j, int[] p, int st) {
            if (p[0] + 4 > j.length)
                throw new LuaError("Expected value but found invalid unicode escape code at character " + (st + 1));
            int v = 0;
            for (int i = 0; i < 4; i++) {
                int d = Character.digit((char) (j[p[0] + i] & 0xff), 16);
                if (d < 0)
                    throw new LuaError("Expected value but found invalid unicode escape code at character " + (st + 1));
                v = v * 16 + d;
            }
            p[0] += 4;
            return v;
        }

        static void utf8(Buf out, int cp) {
            if (cp < 0x80) {
                out.put((byte) cp);
            } else if (cp < 0x800) {
                out.put((byte) (0xC0 | (cp >> 6)));
                out.put((byte) (0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.put((byte) (0xE0 | (cp >> 12)));
                out.put((byte) (0x80 | ((cp >> 6) & 0x3F)));
                out.put((byte) (0x80 | (cp & 0x3F)));
            } else {
                out.put((byte) (0xF0 | (cp >> 18)));
                out.put((byte) (0x80 | ((cp >> 12) & 0x3F)));
                out.put((byte) (0x80 | ((cp >> 6) & 0x3F)));
                out.put((byte) (0x80 | (cp & 0x3F)));
            }
        }
    }

    // ==================================================================
    // bit (deps/lua/src/lua_bit.c -- Lua BitOp 1.0.2)
    // ==================================================================
    static final class Bit {
        /** barg(): modular conversion of a lua_Number to uint32 */
        static int barg(LuaValue v) {
            double n = v.checkdouble();
            double d = n + 6755399441055744.0;  /* 2^52+2^51 */
            return (int) Double.doubleToRawLongBits(d);
        }

        static LuaValue bret(int b) { return LuaValue.valueOf((double) b); }

        static RTable build() {
            RTable t = new RTable();
            t.force("tobit", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return bret(barg(a.arg1())); }
            });
            t.force("bnot", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return bret(~barg(a.arg1())); }
            });
            t.force("band", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int b = barg(a.arg1());
                    for (int i = a.narg(); i > 1; i--) b &= barg(a.arg(i));
                    return bret(b);
                }
            });
            t.force("bor", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int b = barg(a.arg1());
                    for (int i = a.narg(); i > 1; i--) b |= barg(a.arg(i));
                    return bret(b);
                }
            });
            t.force("bxor", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int b = barg(a.arg1());
                    for (int i = a.narg(); i > 1; i--) b ^= barg(a.arg(i));
                    return bret(b);
                }
            });
            t.force("lshift", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    return bret(barg(a.arg1()) << (barg(a.arg(2)) & 31));
                }
            });
            t.force("rshift", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    return bret(barg(a.arg1()) >>> (barg(a.arg(2)) & 31));
                }
            });
            t.force("arshift", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    return bret(barg(a.arg1()) >> (barg(a.arg(2)) & 31));
                }
            });
            t.force("rol", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    return bret(Integer.rotateLeft(barg(a.arg1()), barg(a.arg(2)) & 31));
                }
            });
            t.force("ror", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    return bret(Integer.rotateRight(barg(a.arg1()), barg(a.arg(2)) & 31));
                }
            });
            t.force("bswap", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    return bret(Integer.reverseBytes(barg(a.arg1())));
                }
            });
            t.force("tohex", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int b = barg(a.arg1());
                    int n = a.narg() < 2 || a.arg(2).isnil() ? 8 : barg(a.arg(2));
                    String hex = "0123456789abcdef";
                    if (n == Integer.MIN_VALUE) n = Integer.MIN_VALUE + 1;
                    if (n < 0) { n = -n; hex = "0123456789ABCDEF"; }
                    if (n > 8) n = 8;
                    char[] buf = new char[n];
                    for (int i = n; --i >= 0; ) { buf[i] = hex.charAt(b & 15); b >>>= 4; }
                    return lstr(Util.b(new String(buf)));
                }
            });
            t.lock();
            return t;
        }
    }

    // ==================================================================
    // struct (deps/lua/src/lua_struct.c)
    // ==================================================================
    static final class Struct {
        static final int MAXALIGN = 8;   /* max(sizeof(struct cD)-sizeof(double), sizeof(int)) on x86-64 */

        static final class Hdr {
            boolean little = true;   /* native endianness on x86-64 */
            int align = 1;
        }

        static int getnum(String fmt, int[] i, int df) {
            if (i[0] >= fmt.length() || !Character.isDigit(fmt.charAt(i[0]))) return df;
            int a = 0;
            while (i[0] < fmt.length() && Character.isDigit(fmt.charAt(i[0]))) {
                a = a * 10 + (fmt.charAt(i[0]++) - '0');
                if (a > (Integer.MAX_VALUE / 10)) throw new LuaError("integral size overflow");
            }
            return a;
        }

        static int optsize(char opt, String fmt, int[] i) {
            switch (opt) {
                case 'B': case 'b': return 1;
                case 'H': case 'h': return 2;
                case 'L': case 'l': return 8;
                case 'T': return 8;
                case 'f': return 4;
                case 'd': return 8;
                case 'x': return 1;
                case 'c': return getnum(fmt, i, 1);
                case 'i': case 'I': {
                    int sz = getnum(fmt, i, 4);
                    if (sz > 32) throw new LuaError("integral size " + sz + " is larger than limit of 32");
                    return sz;
                }
                default: return 0;
            }
        }

        static int gettoalign(int len, Hdr h, char opt, int size) {
            if (size == 0 || opt == 'c') return 0;
            if (size > h.align) size = h.align;
            return (size - (len & (size - 1))) & (size - 1);
        }

        static void controloptions(char opt, String fmt, int[] i, Hdr h) {
            switch (opt) {
                case ' ': return;
                case '>': h.little = false; return;
                case '<': h.little = true; return;
                case '!': {
                    int a = getnum(fmt, i, MAXALIGN);
                    if (!(a > 0 && (a & (a - 1)) == 0))
                        throw new LuaError("alignment " + a + " is not a power of 2");
                    h.align = a;
                    return;
                }
                default:
                    throw new LuaError("bad argument #1 to 'pack' (invalid format option '" + opt + "')");
            }
        }

        static void putinteger(Buf b, double n, boolean little, int size) {
            long value = (long) n;
            byte[] buf = new byte[size];
            if (little) {
                for (int i = 0; i < size; i++) { buf[i] = (byte) (value & 0xff); value >>>= 8; }
            } else {
                for (int i = size - 1; i >= 0; i--) { buf[i] = (byte) (value & 0xff); value >>>= 8; }
            }
            b.put(buf);
        }

        static double getinteger(byte[] data, int pos, boolean little, boolean signed, int size) {
            long l = 0;
            if (!little) {
                for (int i = 0; i < size; i++) l = (l << 8) | (data[pos + i] & 0xffL);
            } else {
                for (int i = size - 1; i >= 0; i--) l = (l << 8) | (data[pos + i] & 0xffL);
            }
            if (!signed) {
                if (size >= 8) return (double) l;      /* unsigned 64 bit wraps like C does */
                return (double) l;
            }
            if (size < 8) {
                long mask = -1L << (size * 8 - 1);
                if ((l & mask) != 0) l |= mask;
            }
            return (double) l;
        }

        static RTable build() {
            RTable t = new RTable();
            t.force("pack", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return pack(a); }
            });
            t.force("unpack", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return unpack(a); }
            });
            t.force("size", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return size(a); }
            });
            t.lock();
            return t;
        }

        static Varargs pack(Varargs a) {
            String fmt = Util.s(lbytes(a.arg1()));
            Hdr h = new Hdr();
            Buf b = new Buf(32);
            int arg = 2;
            int totalsize = 0;
            int[] i = {0};
            while (i[0] < fmt.length()) {
                char opt = fmt.charAt(i[0]++);
                int size = optsize(opt, fmt, i);
                int toalign = gettoalign(totalsize, h, opt, size);
                totalsize += toalign;
                while (toalign-- > 0) b.put((byte) 0);
                switch (opt) {
                    case 'b': case 'B': case 'h': case 'H':
                    case 'l': case 'L': case 'T': case 'i': case 'I':
                        putinteger(b, a.arg(arg++).checkdouble(), h.little, size);
                        break;
                    case 'x':
                        b.put((byte) 0);
                        break;
                    case 'f': {
                        int bits = Float.floatToRawIntBits((float) a.arg(arg++).checkdouble());
                        for (int k = 0; k < 4; k++) {
                            int shift = h.little ? (k * 8) : ((3 - k) * 8);
                            b.put((byte) ((bits >>> shift) & 0xff));
                        }
                        break;
                    }
                    case 'd': {
                        long bits = Double.doubleToRawLongBits(a.arg(arg++).checkdouble());
                        for (int k = 0; k < 8; k++) {
                            int shift = h.little ? (k * 8) : ((7 - k) * 8);
                            b.put((byte) ((bits >>> shift) & 0xff));
                        }
                        break;
                    }
                    case 'c': case 's': {
                        byte[] sv = lbytes(a.arg(arg++));
                        if (size == 0) size = sv.length;
                        if (sv.length < size) throw new LuaError("bad argument #" + (arg - 1) + " to 'pack' (string too short)");
                        b.put(sv, 0, size);
                        if (opt == 's') { b.put((byte) 0); size++; }
                        break;
                    }
                    default:
                        controloptions(opt, fmt, i, h);
                }
                totalsize += size;
            }
            return lstr(b.toBytes());
        }

        static Varargs unpack(Varargs a) {
            String fmt = Util.s(lbytes(a.arg1()));
            byte[] data = lbytes(a.arg(2));
            int pos = a.narg() >= 3 && !a.arg(3).isnil() ? (int) a.arg(3).checkdouble() : 1;
            if (pos <= 0) throw new LuaError("bad argument #3 to 'unpack' (offset must be 1 or greater)");
            pos--;
            Hdr h = new Hdr();
            List<LuaValue> out = new ArrayList<LuaValue>();
            int[] i = {0};
            while (i[0] < fmt.length()) {
                char opt = fmt.charAt(i[0]++);
                int size = optsize(opt, fmt, i);
                pos += gettoalign(pos, h, opt, size);
                if (size > data.length || pos > data.length - size)
                    throw new LuaError("bad argument #2 to 'unpack' (data string too short)");
                switch (opt) {
                    case 'b': case 'B': case 'h': case 'H':
                    case 'l': case 'L': case 'T': case 'i': case 'I': {
                        boolean signed = Character.isLowerCase(opt);
                        out.add(LuaValue.valueOf(getinteger(data, pos, h.little, signed, size)));
                        break;
                    }
                    case 'x':
                        break;
                    case 'f': {
                        int bits = 0;
                        for (int k = 0; k < 4; k++) {
                            int shift = h.little ? (k * 8) : ((3 - k) * 8);
                            bits |= (data[pos + k] & 0xff) << shift;
                        }
                        out.add(LuaValue.valueOf(Float.intBitsToFloat(bits)));
                        break;
                    }
                    case 'd': {
                        long bits = 0;
                        for (int k = 0; k < 8; k++) {
                            int shift = h.little ? (k * 8) : ((7 - k) * 8);
                            bits |= (data[pos + k] & 0xffL) << shift;
                        }
                        out.add(LuaValue.valueOf(Double.longBitsToDouble(bits)));
                        break;
                    }
                    case 'c': {
                        if (size == 0) {
                            if (out.isEmpty() || out.get(out.size() - 1).type() != LuaValue.TNUMBER)
                                throw new LuaError("format 'c0' needs a previous size");
                            size = (int) out.remove(out.size() - 1).todouble();
                            if (size > data.length || pos > data.length - size)
                                throw new LuaError("bad argument #2 to 'unpack' (data string too short)");
                        }
                        out.add(lstr(Arrays.copyOfRange(data, pos, pos + size)));
                        break;
                    }
                    case 's': {
                        int e = pos;
                        while (e < data.length && data[e] != 0) e++;
                        if (e >= data.length) throw new LuaError("unfinished string in data");
                        out.add(lstr(Arrays.copyOfRange(data, pos, e)));
                        size = e - pos + 1;
                        break;
                    }
                    default:
                        controloptions(opt, fmt, i, h);
                }
                pos += size;
            }
            out.add(LuaValue.valueOf(pos + 1));
            return LuaValue.varargsOf(out.toArray(new LuaValue[0]));
        }

        static Varargs size(Varargs a) {
            String fmt = Util.s(lbytes(a.arg1()));
            Hdr h = new Hdr();
            int pos = 0;
            int[] i = {0};
            while (i[0] < fmt.length()) {
                char opt = fmt.charAt(i[0]++);
                int size = optsize(opt, fmt, i);
                pos += gettoalign(pos, h, opt, size);
                if (opt == 's')
                    throw new LuaError("bad argument #1 to 'size' (option 's' has no fixed size)");
                if (opt == 'c' && size == 0)
                    throw new LuaError("bad argument #1 to 'size' (option 'c0' has no fixed size)");
                if (!Character.isLetterOrDigit(opt)) controloptions(opt, fmt, i, h);
                pos += size;
            }
            return LuaValue.valueOf(pos);
        }
    }

    // ==================================================================
    // cmsgpack (deps/lua/src/lua_cmsgpack.c)
    // ==================================================================
    static final class MsgPack {
        static final int MAX_NESTING = 16;

        static RTable build(final Scripting sc) {
            RTable t = new RTable();
            t.force("_NAME", ls("cmsgpack"));
            t.force("_VERSION", ls("0.4.0"));
            t.force("_DESCRIPTION", ls("MessagePack C implementation for Lua"));
            t.force("_COPYRIGHT", ls("Copyright(c) 2012 Salvatore Sanfilippo"));
            t.force("pack", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    int nargs = a.narg();
                    if (nargs == 0) throw new LuaError("bad argument #0 to 'pack' (MessagePack pack needs input.)");
                    Buf b = new Buf(32);
                    for (int i = 1; i <= nargs; i++) encode(b, a.arg(i), 0);
                    return lstr(b.toBytes());
                }
            });
            t.force("unpack", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) { return unpackFull(a, 0, 0); }
            });
            t.force("unpack_one", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    long off = a.narg() >= 2 && !a.arg(2).isnil() ? (long) a.arg(2).checkdouble() : 0;
                    return unpackFull(a, 1, off);
                }
            });
            t.force("unpack_limit", new VarArgFunction() {
                @Override public Varargs invoke(Varargs a) {
                    long lim = (long) a.arg(2).checkdouble();
                    long off = a.narg() >= 3 && !a.arg(3).isnil() ? (long) a.arg(3).checkdouble() : 0;
                    return unpackFull(a, lim, off);
                }
            });
            t.lock();
            return t;
        }

        static void encodeBytes(Buf b, byte[] s) {
            int len = s.length;
            if (len < 32) {
                b.put((byte) (0xa0 | len));
            } else if (len <= 0xff) {
                b.put((byte) 0xd9).put((byte) len);
            } else if (len <= 0xffff) {
                b.put((byte) 0xda).put((byte) (len >> 8)).put((byte) len);
            } else {
                b.put((byte) 0xdb).put((byte) (len >>> 24)).put((byte) (len >>> 16))
                 .put((byte) (len >>> 8)).put((byte) len);
            }
            b.put(s);
        }

        static void encodeInt(Buf b, long n) {
            if (n >= 0) {
                if (n <= 127) b.put((byte) (n & 0x7f));
                else if (n <= 0xffL) b.put((byte) 0xcc).put((byte) n);
                else if (n <= 0xffffL) b.put((byte) 0xcd).put((byte) (n >> 8)).put((byte) n);
                else if (n <= 0xffffffffL) {
                    b.put((byte) 0xce).put((byte) (n >> 24)).put((byte) (n >> 16))
                     .put((byte) (n >> 8)).put((byte) n);
                } else {
                    b.put((byte) 0xcf);
                    for (int i = 56; i >= 0; i -= 8) b.put((byte) (n >> i));
                }
            } else {
                if (n >= -32) b.put((byte) n);
                else if (n >= -128) b.put((byte) 0xd0).put((byte) n);
                else if (n >= -32768) b.put((byte) 0xd1).put((byte) (n >> 8)).put((byte) n);
                else if (n >= -2147483648L) {
                    b.put((byte) 0xd2).put((byte) (n >> 24)).put((byte) (n >> 16))
                     .put((byte) (n >> 8)).put((byte) n);
                } else {
                    b.put((byte) 0xd3);
                    for (int i = 56; i >= 0; i -= 8) b.put((byte) (n >> i));
                }
            }
        }

        static void encodeDouble(Buf b, double d) {
            float f = (float) d;
            if (d == (double) f) {
                int bits = Float.floatToRawIntBits(f);
                b.put((byte) 0xca);
                for (int i = 24; i >= 0; i -= 8) b.put((byte) (bits >>> i));
            } else {
                long bits = Double.doubleToRawLongBits(d);
                b.put((byte) 0xcb);
                for (int i = 56; i >= 0; i -= 8) b.put((byte) (bits >>> i));
            }
        }

        static void encodeArrayHdr(Buf b, int n) {
            if (n <= 15) b.put((byte) (0x90 | n));
            else if (n <= 65535) b.put((byte) 0xdc).put((byte) (n >> 8)).put((byte) n);
            else b.put((byte) 0xdd).put((byte) (n >>> 24)).put((byte) (n >>> 16)).put((byte) (n >>> 8)).put((byte) n);
        }

        static void encodeMapHdr(Buf b, int n) {
            if (n <= 15) b.put((byte) (0x80 | n));
            else if (n <= 65535) b.put((byte) 0xde).put((byte) (n >> 8)).put((byte) n);
            else b.put((byte) 0xdf).put((byte) (n >>> 24)).put((byte) (n >>> 16)).put((byte) (n >>> 8)).put((byte) n);
        }

        /** lua_cmsgpack.c table_is_an_array() */
        static boolean isArray(LuaTable t) {
            int count = 0;
            double max = 0;
            LuaValue k = LuaValue.NIL;
            while (true) {
                Varargs nx = t.next(k);
                k = nx.arg1();
                if (k.isnil()) break;
                if (k.type() != LuaValue.TNUMBER) return false;
                double n = k.todouble();
                if (n <= 0 || Double.isInfinite(n) || (int) n != n) return false;
                if (n > max) max = n;
                count++;
            }
            return max == count;
        }

        static void encode(Buf b, LuaValue v, int level) {
            int t = v.type();
            if (t == LuaValue.TTABLE && level == MAX_NESTING) t = LuaValue.TNIL;
            switch (t) {
                case LuaValue.TSTRING:
                    encodeBytes(b, lbytes(v));
                    return;
                case LuaValue.TBOOLEAN:
                    b.put((byte) (v.toboolean() ? 0xc3 : 0xc2));
                    return;
                case LuaValue.TNUMBER: {
                    double d = v.todouble();
                    if (!Double.isInfinite(d) && !Double.isNaN(d) && (double) (long) d == d) encodeInt(b, (long) d);
                    else encodeDouble(b, d);
                    return;
                }
                case LuaValue.TTABLE: {
                    LuaTable tt = v.checktable();
                    if (isArray(tt)) {
                        int len = tt.length();
                        encodeArrayHdr(b, len);
                        for (int j = 1; j <= len; j++) encode(b, tt.get(j), level + 1);
                    } else {
                        int len = 0;
                        LuaValue k = LuaValue.NIL;
                        while (true) {
                            Varargs nx = tt.next(k);
                            k = nx.arg1();
                            if (k.isnil()) break;
                            len++;
                        }
                        encodeMapHdr(b, len);
                        k = LuaValue.NIL;
                        while (true) {
                            Varargs nx = tt.next(k);
                            k = nx.arg1();
                            if (k.isnil()) break;
                            encode(b, k, level + 1);
                            encode(b, nx.arg(2), level + 1);
                        }
                    }
                    return;
                }
                default:
                    b.put((byte) 0xc0);
            }
        }

        static final class Cur {
            byte[] p;
            int pos;
            Cur(byte[] p, int pos) { this.p = p; this.pos = pos; }
            int left() { return p.length - pos; }
            void need(int n) { if (left() < n) throw new LuaError("Missing bytes in input."); }
        }

        static LuaValue decode(Cur c) {
            c.need(1);
            int b0 = c.p[c.pos] & 0xff;
            switch (b0) {
                case 0xcc: c.need(2); { int v = c.p[c.pos + 1] & 0xff; c.pos += 2; return LuaValue.valueOf((double) v); }
                case 0xd0: c.need(2); { int v = c.p[c.pos + 1]; c.pos += 2; return LuaValue.valueOf((double) v); }
                case 0xcd: c.need(3); { int v = ((c.p[c.pos + 1] & 0xff) << 8) | (c.p[c.pos + 2] & 0xff); c.pos += 3; return LuaValue.valueOf((double) v); }
                case 0xd1: c.need(3); { int v = (short) (((c.p[c.pos + 1] & 0xff) << 8) | (c.p[c.pos + 2] & 0xff)); c.pos += 3; return LuaValue.valueOf((double) v); }
                case 0xce: c.need(5); { long v = readU(c.p, c.pos + 1, 4); c.pos += 5; return LuaValue.valueOf((double) v); }
                case 0xd2: c.need(5); { int v = (int) readU(c.p, c.pos + 1, 4); c.pos += 5; return LuaValue.valueOf((double) v); }
                case 0xcf: c.need(9); { long v = readU(c.p, c.pos + 1, 8); c.pos += 9; return LuaValue.valueOf((double) v); }
                case 0xd3: c.need(9); { long v = readU(c.p, c.pos + 1, 8); c.pos += 9; return LuaValue.valueOf((double) v); }
                case 0xc0: c.pos += 1; return LuaValue.NIL;
                case 0xc3: c.pos += 1; return LuaValue.TRUE;
                case 0xc2: c.pos += 1; return LuaValue.FALSE;
                case 0xca: c.need(5); {
                    int bits = (int) readU(c.p, c.pos + 1, 4);
                    c.pos += 5;
                    return LuaValue.valueOf((double) Float.intBitsToFloat(bits));
                }
                case 0xcb: c.need(9); {
                    long bits = readU(c.p, c.pos + 1, 8);
                    c.pos += 9;
                    return LuaValue.valueOf(Double.longBitsToDouble(bits));
                }
                case 0xd9: c.need(2); {
                    int l = c.p[c.pos + 1] & 0xff;
                    c.need(2 + l);
                    LuaValue r = lstr(Arrays.copyOfRange(c.p, c.pos + 2, c.pos + 2 + l));
                    c.pos += 2 + l;
                    return r;
                }
                case 0xda: c.need(3); {
                    int l = ((c.p[c.pos + 1] & 0xff) << 8) | (c.p[c.pos + 2] & 0xff);
                    c.need(3 + l);
                    LuaValue r = lstr(Arrays.copyOfRange(c.p, c.pos + 3, c.pos + 3 + l));
                    c.pos += 3 + l;
                    return r;
                }
                case 0xdb: c.need(5); {
                    int l = (int) readU(c.p, c.pos + 1, 4);
                    c.pos += 5;
                    c.need(l);
                    LuaValue r = lstr(Arrays.copyOfRange(c.p, c.pos, c.pos + l));
                    c.pos += l;
                    return r;
                }
                case 0xdc: c.need(3); {
                    int l = ((c.p[c.pos + 1] & 0xff) << 8) | (c.p[c.pos + 2] & 0xff);
                    c.pos += 3;
                    return decodeArray(c, l);
                }
                case 0xdd: c.need(5); {
                    int l = (int) readU(c.p, c.pos + 1, 4);
                    c.pos += 5;
                    return decodeArray(c, l);
                }
                case 0xde: c.need(3); {
                    int l = ((c.p[c.pos + 1] & 0xff) << 8) | (c.p[c.pos + 2] & 0xff);
                    c.pos += 3;
                    return decodeMap(c, l);
                }
                case 0xdf: c.need(5); {
                    int l = (int) readU(c.p, c.pos + 1, 4);
                    c.pos += 5;
                    return decodeMap(c, l);
                }
                default:
                    if ((b0 & 0x80) == 0) { c.pos += 1; return LuaValue.valueOf((double) b0); }
                    if ((b0 & 0xe0) == 0xe0) { c.pos += 1; return LuaValue.valueOf((double) (byte) b0); }
                    if ((b0 & 0xe0) == 0xa0) {
                        int l = b0 & 0x1f;
                        c.need(1 + l);
                        LuaValue r = lstr(Arrays.copyOfRange(c.p, c.pos + 1, c.pos + 1 + l));
                        c.pos += 1 + l;
                        return r;
                    }
                    if ((b0 & 0xf0) == 0x90) { int l = b0 & 0xf; c.pos += 1; return decodeArray(c, l); }
                    if ((b0 & 0xf0) == 0x80) { int l = b0 & 0xf; c.pos += 1; return decodeMap(c, l); }
                    throw new LuaError("Bad data format in input.");
            }
        }

        static long readU(byte[] p, int off, int n) {
            long v = 0;
            for (int i = 0; i < n; i++) v = (v << 8) | (p[off + i] & 0xffL);
            return v;
        }

        static LuaValue decodeArray(Cur c, int len) {
            LuaTable t = new LuaTable();
            for (int i = 1; i <= len; i++) t.rawset(i, decode(c));
            return t;
        }

        static LuaValue decodeMap(Cur c, int len) {
            LuaTable t = new LuaTable();
            for (int i = 0; i < len; i++) {
                LuaValue k = decode(c);
                LuaValue v = decode(c);
                if (!k.isnil()) t.rawset(k, v);
            }
            return t;
        }

        static Varargs unpackFull(Varargs a, long limit, long offset) {
            byte[] data = lbytes(a.arg1());
            boolean decodeAll = (limit == 0 && offset == 0);
            if (offset < 0 || limit < 0)
                throw new LuaError("Invalid request to unpack with offset of " + offset
                        + " and limit of " + data.length + ".");
            if (offset > data.length)
                throw new LuaError("Start offset " + offset + " greater than input length " + data.length + ".");
            if (decodeAll) limit = Integer.MAX_VALUE;
            Cur c = new Cur(data, (int) offset);
            List<LuaValue> out = new ArrayList<LuaValue>();
            int cnt = 0;
            while (c.left() > 0 && cnt < limit) {
                out.add(decode(c));
                cnt++;
            }
            if (!decodeAll) {
                long newOffset = data.length - c.left();
                out.add(0, LuaValue.valueOf(c.left() == 0 ? -1.0 : (double) newOffset));
            }
            return LuaValue.varargsOf(out.toArray(new LuaValue[0]));
        }
    }
}
