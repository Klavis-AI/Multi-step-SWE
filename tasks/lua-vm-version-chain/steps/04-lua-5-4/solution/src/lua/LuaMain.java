package lua;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

/** The standalone interpreter of the Lua 5.1 reference manual, section 7. */
public final class LuaMain {
    static final String PROGNAME = "lua";
    static final String VERSION = "Lua 5.4";
    static final String RELEASE = "Lua 5.4.8";
    static final String COPYRIGHT = RELEASE + "  Copyright (C) 1994-2025 Lua.org, PUC-Rio";

    static int exitCode = 0;

    public static void main(String[] args) throws Exception {
        Thread t = new Thread(null, () -> {
            run(args);
            // lua_close: every object still holding a finaliser is finalised
            // when the state goes away, in reverse order of marking.
            try { Gc.closeState(); } catch (LuaError e) { /* ignored at close */ }
        }, "lua-main", 1L << 30);
        t.start();
        t.join();
        LibIo.flushAll();
        System.exit(exitCode);
    }

    static void exitNow(int code) {
        LibIo.flushAll();
        System.exit(code);
    }

    /** The default warning function of the standalone interpreter. */
    static void warning(String msg) {
        LibIo.flushAll();
        System.err.println("Lua warning: " + msg);
        System.err.flush();
    }

    static void message(String msg) {
        LibIo.flushAll();
        System.err.println(PROGNAME + ": " + msg);
        System.err.flush();
    }

    static void report(LuaError e) {
        Object v = e.value;
        String msg;
        if (v instanceof LuaString || LuaNum.isNum(v)) msg = Interp.rawToString(v);
        else {
            Object h = Interp.metamethod(v, "__tostring");
            msg = h != null ? String.valueOf(Interp.tostr(v)) : "(error object is a " + Interp.typeName(v) + " value)";
        }
        LibIo.flushAll();
        System.err.println(PROGNAME + ": " + msg);
        System.err.flush();
        exitCode = 1;
    }

    static void run(String[] argv) {
        Interp.globals = new LuaTable();
        Interp.main = Coroutine.newMain();
        Interp.current = Interp.main;
        LuaTable g = Interp.globals;
        LibBase.open(g);
        LibString.open(g);
        LibTable.open(g);
        LibMath.open(g);
        LibIo.open(g);
        LibUtf8.open(g);
        LibCo.open(g);
        LibDebug.open(g);
        LibPackage.open(g);

        int scriptIdx = collectArgs(argv);
        if (scriptIdx == -2) { printUsage(badArg); exitCode = 1; return; }

        LuaTable argT = new LuaTable();
        argT.rawset(0.0, LuaString.of(scriptIdx >= 0 && scriptIdx < argv.length ? argv[scriptIdx] : PROGNAME));
        argT.rawset(-1.0, LuaString.of(PROGNAME));
        if (scriptIdx >= 0) {
            for (int i = scriptIdx + 1; i < argv.length; i++) argT.rawseti(i - scriptIdx, LuaString.of(argv[i]));
            for (int i = 0; i < scriptIdx; i++) argT.rawset((double) (i - scriptIdx - 1), LuaString.of(argv[i]));
        }
        g.rawset("arg", argT);

        try {
            if (!ignoreEnv) handleInit();
            boolean interactive = false;
            for (int i = 0; i < argv.length && (scriptIdx < 0 || i < scriptIdx); i++) {
                String a = argv[i];
                if (a.equals("--")) continue;
                if (a.equals("-v")) { printVersion(); continue; }
                if (a.equals("-E")) continue;
                if (a.equals("-W")) { LibBase.warnState = LibBase.WARN_ON; continue; }
                if (a.equals("-i")) { interactive = true; printVersion(); continue; }
                if (a.startsWith("-e")) {
                    String stat = a.length() > 2 ? a.substring(2) : argv[++i];
                    dostring(stat, "=(command line)");
                    continue;
                }
                if (a.startsWith("-l")) {
                    String name = a.length() > 2 ? a.substring(2) : argv[++i];
                    Interp.call(g.rawget("require"), new Object[]{LuaString.of(name)});
                    continue;
                }
            }
            if (scriptIdx >= 0) {
                String script = argv[scriptIdx];
                Object[] extra = new Object[Math.max(0, argv.length - scriptIdx - 1)];
                for (int i = 0; i < extra.length; i++) extra[i] = LuaString.of(argv[scriptIdx + 1 + i]);
                if (script.equals("-")) dofileStdin(extra);
                else dofile(script, extra);
            }
            if (interactive) repl();
            else if (scriptIdx < 0 && !hasE && !hasV) {
                printVersion();
                repl();
            }
        } catch (LuaError e) {
            report(e);
        } catch (StackOverflowError e) {
            report(new LuaError(LuaString.of("stack overflow")));
        }
    }

    static boolean hasE, hasV, ignoreEnv, warnOn;

    /** Returns the index of the script argument, -1 if none, -2 on a bad option. */
    static int collectArgs(String[] argv) {
        int i = 0;
        for (; i < argv.length; i++) {
            String a = argv[i];
            if (a.isEmpty() || a.charAt(0) != '-') return i;
            if (a.equals("-")) return i;
            if (a.equals("--")) return i + 1 < argv.length ? i + 1 : -1;
            if (a.equals("-i")) { hasV = true; continue; }
            if (a.equals("-v")) { hasV = true; continue; }
            if (a.equals("-E")) { ignoreEnv = true; continue; }
            if (a.equals("-W")) { warnOn = true; continue; }
            if (a.startsWith("-e")) {
                hasE = true;
                if (a.length() == 2) { if (++i >= argv.length) { badOption("-e"); return -2; } }
                continue;
            }
            if (a.startsWith("-l")) {
                if (a.length() == 2) { if (++i >= argv.length) { badOption("-l"); return -2; } }
                continue;
            }
            badArg = a;
            return -2;
        }
        return -1;
    }

    static String badArg;

    static void badOption(String o) { badArg = o; }

    static void printUsage(String badOption) {
        LibIo.flushAll();
        if (badOption != null) {
            if (badOption.length() > 1 && (badOption.charAt(1) == 'e' || badOption.charAt(1) == 'l')) {
                System.err.println(PROGNAME + ": '" + badOption + "' needs argument");
            } else {
                System.err.println(PROGNAME + ": unrecognized option '" + badOption + "'");
            }
        }
        System.err.println("usage: " + PROGNAME + " [options] [script [args]]\n"
                + "Available options are:\n"
                + "  -e stat   execute string " + q("stat") + "\n"
                + "  -i        enter interactive mode after executing " + q("script") + "\n"
                + "  -l mod    require library " + q("mod") + " into global " + q("mod") + "\n"
                + "  -l g=mod  require library " + q("mod") + " into global " + q("g") + "\n"
                + "  -v        show version information\n"
                + "  -E        ignore environment variables\n"
                + "  -W        turn warnings on\n"
                + "  --        stop handling options\n"
                + "  -         stop handling options and execute stdin");
        System.err.flush();
    }

    static String q(String s) { return "'" + s + "'"; }

    /** From 5.2 the banner goes to standard output. */
    static void printVersion() {
        LibIo.writeStdout((COPYRIGHT + "\n").getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));
    }

    static void handleInit() {
        String init = System.getenv("LUA_INIT_5_4");
        if (init == null) init = System.getenv("LUA_INIT");
        if (init == null) return;
        if (init.startsWith("@")) dofile(init.substring(1), Interp.EMPTY);
        else dostring(init, "=LUA_INIT");
    }

    static void dostring(String s, String name) {
        byte[] b = s.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
        Object[] r = LibBase.doLoad(b, name);
        if (r[0] == null) throw new LuaError(r[1]);
        docall((LuaFunction) r[0], Interp.EMPTY);
    }

    static void dofile(String path, Object[] args) {
        byte[] b;
        try {
            b = Files.readAllBytes(Paths.get(path));
        } catch (IOException e) {
            throw new LuaError(LuaString.of("cannot open " + path + ": No such file or directory"));
        }
        Object[] r = LibBase.doLoad(LibBase.skipShebang(b), "@" + path);
        if (r[0] == null) throw new LuaError(r[1]);
        docall((LuaFunction) r[0], args);
    }

    static void dofileStdin(Object[] args) {
        try {
            byte[] b = LibBase.skipShebang(LibBase.readAll(System.in));
            Object[] r = LibBase.doLoad(b, "=stdin");
            if (r[0] == null) throw new LuaError(r[1]);
            docall((LuaFunction) r[0], args);
        } catch (IOException e) {
            throw new LuaError(LuaString.of("cannot read stdin"));
        }
    }

    static void docall(LuaFunction f, Object[] args) {
        Coroutine co = Interp.current;
        Object saved = co.handler;
        co.handler = new Lib.Fn("traceback", a -> {
            Object m = a.length > 0 ? a[0] : null;
            String msg = (m instanceof LuaString || LuaNum.isNum(m)) ? Interp.rawToString(m) : null;
            if (msg == null && m != null && Interp.metamethod(m, "__tostring") == null) return Lib.ret(m);
            if (msg == null) msg = Interp.tostring(m).toString();
            return Lib.ret(LuaString.of(Interp.traceback(msg, 1)));
        });
        try {
            Interp.call(f, args);
        } finally {
            co.handler = saved;
        }
    }

    static void repl() {
        java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(System.in,
                java.nio.charset.StandardCharsets.ISO_8859_1));
        StringBuilder acc = new StringBuilder();
        for (;;) {
            LibIo.writeStdout((acc.length() == 0 ? "> " : ">> ").getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));
            LibIo.flushAll();
            String line;
            try { line = in.readLine(); } catch (IOException e) { break; }
            if (line == null) break;
            if (acc.length() == 0 && line.startsWith("=")) line = "return " + line.substring(1);
            acc.append(line);
            String src = acc.toString();
            Object[] r = LibBase.doLoad(src.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1), "=stdin");
            if (r[0] == null) {
                String msg = String.valueOf(r[1]);
                if (msg.endsWith("'<eof>'")) { acc.append("\n"); continue; }
                message(msg);
                acc.setLength(0);
                continue;
            }
            acc.setLength(0);
            try {
                Object[] res = Interp.call(r[0], Interp.EMPTY);
                if (res.length > 0) {
                    Object[] pargs = res;
                    Interp.call(Interp.globals.rawget("print"), pargs);
                }
            } catch (LuaError e) {
                Object v = e.value;
                message(v instanceof LuaString || LuaNum.isNum(v) ? Interp.rawToString(v)
                        : "(error object is a " + Interp.typeName(v) + " value)");
            }
            LibIo.flushAll();
        }
        LibIo.writeStdout("\n".getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));
    }
}
