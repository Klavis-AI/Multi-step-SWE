package redis;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * EVAL / EVALSHA / EVAL_RO / EVALSHA_RO, the SCRIPT and FUNCTION containers and
 * FCALL / FCALL_RO.  Ported from src/eval.c and src/functions.c (redis 8.0.6).
 */
public final class ScriptCmds {

    private ScriptCmds() {}

    static Scripting sc() { return Server.I.scripting; }

    public static void register() {
        CommandTable.reg("eval", -3, "noscript stale movablekeys", 0, 0, 0,
                c -> sc().evalCommand(c, false, false));
        CommandTable.reg("evalsha", -3, "noscript stale movablekeys", 0, 0, 0,
                c -> sc().evalCommand(c, true, false));
        CommandTable.reg("eval_ro", -3, "noscript stale readonly movablekeys", 0, 0, 0,
                c -> sc().evalCommand(c, false, true));
        CommandTable.reg("evalsha_ro", -3, "noscript stale readonly movablekeys", 0, 0, 0,
                c -> sc().evalCommand(c, true, true));
        CommandTable.reg("fcall", -3, "noscript stale movablekeys", 0, 0, 0,
                c -> sc().fcallCommand(c, false));
        CommandTable.reg("fcall_ro", -3, "noscript stale readonly movablekeys", 0, 0, 0,
                c -> sc().fcallCommand(c, true));

        CommandTable.reg("script", -2, "", 0, 0, 0, c -> CommandTable.dispatchSub(c, "script"));
        CommandTable.regSub("script", "load", 3, "noscript stale", ScriptCmds::scriptLoad);
        CommandTable.regSub("script", "exists", -3, "noscript", ScriptCmds::scriptExists);
        CommandTable.regSub("script", "flush", -2, "noscript", ScriptCmds::scriptFlush);
        CommandTable.regSub("script", "kill", 2, "noscript", ScriptCmds::scriptKill);
        CommandTable.regSub("script", "debug", 3, "noscript", ScriptCmds::scriptDebug);
        CommandTable.regSub("script", "help", 2, "loading stale", ScriptCmds::scriptHelp);

        CommandTable.reg("function", -2, "", 0, 0, 0, c -> CommandTable.dispatchSub(c, "function"));
        CommandTable.regSub("function", "load", -3, "noscript write denyoom", ScriptCmds::functionLoad);
        CommandTable.regSub("function", "delete", 3, "noscript write", ScriptCmds::functionDelete);
        CommandTable.regSub("function", "flush", -2, "noscript write", ScriptCmds::functionFlush);
        CommandTable.regSub("function", "list", -2, "noscript", ScriptCmds::functionList);
        CommandTable.regSub("function", "dump", 2, "noscript", ScriptCmds::functionDump);
        CommandTable.regSub("function", "restore", -3, "noscript write denyoom", ScriptCmds::functionRestore);
        CommandTable.regSub("function", "stats", 2, "noscript", ScriptCmds::functionStats);
        CommandTable.regSub("function", "kill", 2, "noscript", ScriptCmds::functionKill);
        CommandTable.regSub("function", "help", 2, "loading stale", ScriptCmds::functionHelp);
    }

    // ==================================================================
    // SCRIPT
    // ==================================================================
    static void scriptLoad(Client c) {
        String sha = sc().createFunction(c.arg(2), true);
        c.addBulk(Util.b(sha));
    }

    static void scriptExists(Client c) {
        Scripting s = sc();
        s.ensureInit();
        c.addArray(c.argc - 2);
        for (int j = 2; j < c.argc; j++) {
            String sha = c.argStr(j).toLowerCase(Locale.US);
            c.addInt(s.scripts.containsKey(sha) ? 1 : 0);
        }
    }

    static void scriptFlush(Client c) {
        if (c.argc == 3) {
            if (!c.argIs(2, "sync") && !c.argIs(2, "async"))
                throw new RedisError("ERR SCRIPT FLUSH only support SYNC|ASYNC option");
        } else if (c.argc != 2) {
            throw new RedisError("ERR SCRIPT FLUSH only support SYNC|ASYNC option");
        }
        sc().scriptFlush();
        c.addOk();
    }

    static void scriptKill(Client c) {
        Scripting s = sc();
        if (!s.isRunning()) throw new RedisError("NOTBUSY No scripts in execution right now.");
        if (!s.isEval())
            throw new RedisError("BUSY Redis is busy running a script. You can only call FUNCTION KILL or SHUTDOWN NOSAVE.");
        if (s.currWriteDirty())
            throw new RedisError("UNKILLABLE Sorry the script already executed write commands against the dataset."
                    + " You can either wait the script termination or kill the server in a hard way using the SHUTDOWN NOSAVE command.");
        s.killRequested = true;
        c.addOk();
    }

    static void scriptDebug(Client c) {
        if (c.argIs(2, "no") || c.argIs(2, "yes") || c.argIs(2, "sync")) c.addOk();
        else throw new RedisError("ERR Use SCRIPT DEBUG YES/SYNC/NO");
    }

    static final String[] SCRIPT_HELP = {
        "DEBUG (YES|SYNC|NO)",
        "    Set the debug mode for subsequent scripts executed.",
        "EXISTS <sha1> [<sha1> ...]",
        "    Return information about the existence of the scripts in the script cache.",
        "FLUSH [ASYNC|SYNC]",
        "    Flush the Lua scripts cache. Very dangerous on replicas.",
        "    When called without the optional mode argument, the behavior is determined by the",
        "    lazyfree-lazy-user-flush configuration directive. Valid modes are:",
        "    * ASYNC: Asynchronously flush the scripts cache.",
        "    * SYNC: Synchronously flush the scripts cache.",
        "KILL",
        "    Kill the currently executing Lua script.",
        "LOAD <script>",
        "    Load a script into the scripts cache without executing it.",
    };

    static void scriptHelp(Client c) { addReplyHelp(c, "SCRIPT", SCRIPT_HELP); }

    static void addReplyHelp(Client c, String cmd, String[] help) {
        c.addArray(help.length + 3);
        c.addSimple(cmd + " <subcommand> [<arg> [value] [opt] ...]. Subcommands are:");
        for (int i = 0; i < help.length; i++) c.addSimple(help[i]);
        c.addSimple("HELP");
        c.addSimple("    Print this help.");
    }

    // ==================================================================
    // FUNCTION
    // ==================================================================
    static void functionLoad(Client c) {
        boolean replace = false;
        int pos = 2;
        while (pos < c.argc - 1) {
            if (c.argIs(pos, "replace")) { replace = true; pos++; continue; }
            throw Scripting.rerr("ERR Unknown option given: " + c.argStr(pos));
        }
        if (pos >= c.argc) throw new RedisError("ERR Function code is missing");
        String name = sc().functionLoad(c.arg(pos), replace);
        Server.I.dirty++;
        c.addBulk(Util.b(name));
    }

    static void functionDelete(Client c) {
        sc().functionDelete(c.argStr(2));
        Server.I.dirty++;
        c.addOk();
    }

    static void functionFlush(Client c) {
        if (c.argc > 3) throw subcommandSyntaxError(c, "function");
        if (c.argc == 3 && !c.argIs(2, "sync") && !c.argIs(2, "async"))
            throw new RedisError("ERR FUNCTION FLUSH only supports SYNC|ASYNC option");
        sc().functionFlush();
        Server.I.dirty++;
        c.addOk();
    }

    /** networking.c addReplySubcommandSyntaxError() */
    static RedisError subcommandSyntaxError(Client c, String container) {
        String sub = c.argStr(1);
        if (sub.length() > 128) sub = sub.substring(0, 128);
        return Scripting.rerr("ERR unknown subcommand or wrong number of arguments for '"
                + sub + "'. Try " + container.toUpperCase(Locale.US) + " HELP.");
    }

    static void functionList(Client c) {
        boolean withCode = false;
        byte[] libName = null;
        for (int i = 2; i < c.argc; i++) {
            if (!withCode && c.argIs(i, "withcode")) { withCode = true; continue; }
            if (libName == null && c.argIs(i, "libraryname")) {
                if (i >= c.argc - 1) throw new RedisError("ERR library name argument was not given");
                libName = c.arg(++i);
                continue;
            }
            throw Scripting.rerr("ERR Unknown argument " + c.argStr(i));
        }
        Scripting s = sc();
        List<Scripting.LibInfo> libs = new ArrayList<Scripting.LibInfo>();
        for (Scripting.LibInfo li : s.libraries.values()) {
            if (libName != null && !Util.stringmatch(libName, Util.b(li.name), true)) continue;
            libs.add(li);
        }
        c.addArray(libs.size());
        for (int i = 0; i < libs.size(); i++) {
            Scripting.LibInfo li = libs.get(i);
            c.addMap(withCode ? 4 : 3);
            c.addBulk("library_name");
            c.addBulk(Util.b(li.name));
            c.addBulk("engine");
            c.addBulk(Util.b(li.engine));
            c.addBulk("functions");
            c.addArray(li.funcs.size());
            for (Scripting.FuncInfo fi : li.funcs.values()) {
                c.addMap(3);
                c.addBulk("name");
                c.addBulk(Util.b(fi.name));
                c.addBulk("description");
                if (fi.desc != null) c.addBulk(fi.desc); else c.addNil();
                c.addBulk("flags");
                int n = 0;
                for (int f = 0; f < Scripting.ALL_FLAGS.length; f++)
                    if ((fi.flags & Scripting.ALL_FLAGS[f]) != 0) n++;
                c.addSet(n);
                for (int f = 0; f < Scripting.ALL_FLAGS.length; f++)
                    if ((fi.flags & Scripting.ALL_FLAGS[f]) != 0)
                        c.addSimple(Scripting.flagName(Scripting.ALL_FLAGS[f]));
            }
            if (withCode) {
                c.addBulk("library_code");
                c.addBulk(li.code);
            }
        }
    }

    static void functionStats(Client c) {
        Scripting s = sc();
        if (s.isRunning() && s.isEval())
            throw new RedisError("BUSY Redis is busy running a script. You can only call FUNCTION KILL or SHUTDOWN NOSAVE.");
        c.addMap(2);
        c.addBulk("running_script");
        if (!s.isRunning()) {
            c.addNil();
        } else {
            c.addMap(3);
            c.addBulk("name");
            c.addBulk(Util.b(s.currFunction()));
            c.addBulk("command");
            Client caller = s.currCaller();
            c.addArray(caller == null ? 0 : caller.argc);
            if (caller != null) for (int i = 0; i < caller.argc; i++) c.addBulk(caller.argv[i]);
            c.addBulk("duration_ms");
            c.addInt(s.runDurationMs());
        }
        c.addBulk("engines");
        c.addMap(1);
        c.addBulk("LUA");
        c.addMap(2);
        c.addBulk("libraries_count");
        c.addInt(s.libraries.size());
        c.addBulk("functions_count");
        c.addInt(s.functions.size());
    }

    static void functionKill(Client c) {
        Scripting s = sc();
        if (!s.isRunning()) throw new RedisError("NOTBUSY No scripts in execution right now.");
        if (s.isEval())
            throw new RedisError("BUSY Redis is busy running a script. You can only call SCRIPT KILL or SHUTDOWN NOSAVE.");
        if (s.currWriteDirty())
            throw new RedisError("UNKILLABLE Sorry the script already executed write commands against the dataset."
                    + " You can either wait the script termination or kill the server in a hard way using the SHUTDOWN NOSAVE command.");
        s.killRequested = true;
        c.addOk();
    }

    static final String[] FUNCTION_HELP = {
        "LOAD [REPLACE] <FUNCTION CODE>",
        "    Create a new library with the given library name and code.",
        "DELETE <LIBRARY NAME>",
        "    Delete the given library.",
        "LIST [LIBRARYNAME PATTERN] [WITHCODE]",
        "    Return general information on all the libraries:",
        "    * Library name",
        "    * The engine used to run the Library",
        "    * Functions list",
        "    * Library code (if WITHCODE is given)",
        "    It also possible to get only function that matches a pattern using LIBRARYNAME argument.",
        "STATS",
        "    Return information about the current function running:",
        "    * Function name",
        "    * Command used to run the function",
        "    * Duration in MS that the function is running",
        "    If no function is running, return nil",
        "    In addition, returns a list of available engines.",
        "KILL",
        "    Kill the current running function.",
        "FLUSH [ASYNC|SYNC]",
        "    Delete all the libraries.",
        "    When called without the optional mode argument, the behavior is determined by the",
        "    lazyfree-lazy-user-flush configuration directive. Valid modes are:",
        "    * ASYNC: Asynchronously flush the libraries.",
        "    * SYNC: Synchronously flush the libraries.",
        "DUMP",
        "    Return a serialized payload representing the current libraries, can be restored using FUNCTION RESTORE command",
        "RESTORE <PAYLOAD> [FLUSH|APPEND|REPLACE]",
        "    Restore the libraries represented by the given payload, it is possible to give a restore policy to",
        "    control how to handle existing libraries (default APPEND):",
        "    * FLUSH: delete all existing libraries.",
        "    * APPEND: appends the restored libraries to the existing libraries. On collision, abort.",
        "    * REPLACE: appends the restored libraries to the existing libraries, On collision, replace the old",
        "      libraries with the new libraries (notice that even on this option there is a chance of failure",
        "      in case of functions name collision with another library).",
    };

    static void functionHelp(Client c) { addReplyHelp(c, "FUNCTION", FUNCTION_HELP); }

    // ------------------------------------------------------------------
    // FUNCTION DUMP / RESTORE -- the RDB function payload (rdb.c)
    // ------------------------------------------------------------------
    static final int RDB_VERSION = 12;
    static final int RDB_OPCODE_FUNCTION2 = 245;
    static final int RDB_OPCODE_FUNCTION_PRE_GA = 246;

    static void functionDump(Client c) {
        Scripting s = sc();
        s.ensureInit();
        Buf b = new Buf(256);
        for (Scripting.LibInfo li : s.libraries.values()) {
            b.put((byte) RDB_OPCODE_FUNCTION2);
            saveRawString(b, li.code);
        }
        b.put((byte) (RDB_VERSION & 0xff));
        b.put((byte) ((RDB_VERSION >> 8) & 0xff));
        long crc = crc64(0, b.a, 0, b.len);
        for (int i = 0; i < 8; i++) b.put((byte) (crc >>> (8 * i)));
        c.addBulk(b.toBytes());
    }

    static void functionRestore(Client c) {
        if (c.argc > 4) throw subcommandSyntaxError(c, "function");
        int policy = 0;   // 0 append, 1 replace, 2 flush
        if (c.argc == 4) {
            if (c.argIs(3, "append")) policy = 0;
            else if (c.argIs(3, "replace")) policy = 1;
            else if (c.argIs(3, "flush")) policy = 2;
            else throw new RedisError("ERR Wrong restore policy given, value should be either FLUSH, APPEND or REPLACE.");
        }
        byte[] data = c.arg(2);
        if (!verifyDumpPayload(data))
            throw new RedisError("ERR DUMP payload version or checksum are wrong");

        Scripting s = sc();
        s.ensureInit();
        List<byte[]> codes = new ArrayList<byte[]>();
        int[] pos = {0};
        while (data.length - pos[0] > 10) {
            int type = data[pos[0]++] & 0xff;
            if (type == RDB_OPCODE_FUNCTION_PRE_GA)
                throw new RedisError("ERR Pre-GA function format not supported");
            if (type != RDB_OPCODE_FUNCTION2)
                throw new RedisError("ERR given type is not a function");
            byte[] code = loadRawString(data, pos);
            if (code == null) throw new RedisError("ERR failed loading the given functions payload");
            codes.add(code);
        }

        // snapshot so a failure leaves the current libraries untouched
        java.util.LinkedHashMap<String, Scripting.LibInfo> savedLibs =
                new java.util.LinkedHashMap<String, Scripting.LibInfo>(s.libraries);
        java.util.LinkedHashMap<String, Scripting.FuncInfo> savedFuncs =
                new java.util.LinkedHashMap<String, Scripting.FuncInfo>(s.functions);
        if (policy == 2) { s.libraries.clear(); s.functions.clear(); }
        try {
            for (int i = 0; i < codes.size(); i++) s.functionLoad(codes.get(i), policy != 0, true);
        } catch (RedisError e) {
            s.libraries.clear();
            s.libraries.putAll(savedLibs);
            s.functions.clear();
            s.functions.putAll(savedFuncs);
            throw e;
        }
        Server.I.dirty++;
        c.addOk();
    }

    static boolean verifyDumpPayload(byte[] p) {
        if (p.length < 10) return false;
        int foot = p.length - 10;
        int rdbver = ((p[foot + 1] & 0xff) << 8) | (p[foot] & 0xff);
        if (rdbver > RDB_VERSION) return false;
        long crc = crc64(0, p, 0, p.length - 8);
        for (int i = 0; i < 8; i++) {
            if ((byte) (crc >>> (8 * i)) != p[p.length - 8 + i]) return false;
        }
        return true;
    }

    // ---- rdb string / length codec ----
    static void saveLen(Buf b, long len) {
        if (len < (1 << 6)) {
            b.put((byte) (len & 0x3F));
        } else if (len < (1 << 14)) {
            b.put((byte) (((len >>> 8) & 0x3F) | 0x40));
            b.put((byte) (len & 0xFF));
        } else if (len <= 0xFFFFFFFFL) {
            b.put((byte) 0x80);
            for (int i = 3; i >= 0; i--) b.put((byte) (len >>> (8 * i)));
        } else {
            b.put((byte) 0x81);
            for (int i = 7; i >= 0; i--) b.put((byte) (len >>> (8 * i)));
        }
    }

    static void saveRawString(Buf b, byte[] v) {
        saveLen(b, v.length);
        b.put(v);
    }

    /** returns {len, isEncoded} */
    static long[] loadLen(byte[] p, int[] pos) {
        if (pos[0] >= p.length) return null;
        int b0 = p[pos[0]++] & 0xff;
        int type = (b0 & 0xC0) >> 6;
        if (type == 3) return new long[]{b0 & 0x3F, 1};
        if (type == 0) return new long[]{b0 & 0x3F, 0};
        if (type == 1) {
            if (pos[0] >= p.length) return null;
            long len = ((long) (b0 & 0x3F) << 8) | (p[pos[0]++] & 0xff);
            return new long[]{len, 0};
        }
        if (b0 == 0x80) {
            if (pos[0] + 4 > p.length) return null;
            long len = 0;
            for (int i = 0; i < 4; i++) len = (len << 8) | (p[pos[0]++] & 0xff);
            return new long[]{len, 0};
        }
        if (b0 == 0x81) {
            if (pos[0] + 8 > p.length) return null;
            long len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | (p[pos[0]++] & 0xff);
            return new long[]{len, 0};
        }
        return null;
    }

    static byte[] loadRawString(byte[] p, int[] pos) {
        long[] l = loadLen(p, pos);
        if (l == null) return null;
        if (l[1] == 1) {
            int enc = (int) l[0];
            switch (enc) {
                case 0: {   // RDB_ENC_INT8
                    if (pos[0] + 1 > p.length) return null;
                    return Util.b(Integer.toString(p[pos[0]++]));
                }
                case 1: {   // RDB_ENC_INT16
                    if (pos[0] + 2 > p.length) return null;
                    int v = (p[pos[0]] & 0xff) | ((p[pos[0] + 1] & 0xff) << 8);
                    pos[0] += 2;
                    return Util.b(Integer.toString((short) v));
                }
                case 2: {   // RDB_ENC_INT32
                    if (pos[0] + 4 > p.length) return null;
                    int v = 0;
                    for (int i = 3; i >= 0; i--) v = (v << 8) | (p[pos[0] + i] & 0xff);
                    pos[0] += 4;
                    return Util.b(Integer.toString(v));
                }
                case 3: {   // RDB_ENC_LZF
                    long[] clen = loadLen(p, pos);
                    long[] ulen = loadLen(p, pos);
                    if (clen == null || ulen == null) return null;
                    if (pos[0] + clen[0] > p.length) return null;
                    byte[] out = lzfDecompress(p, pos[0], (int) clen[0], (int) ulen[0]);
                    pos[0] += (int) clen[0];
                    return out;
                }
                default:
                    return null;
            }
        }
        int len = (int) l[0];
        if (pos[0] + len > p.length) return null;
        byte[] out = Arrays.copyOfRange(p, pos[0], pos[0] + len);
        pos[0] += len;
        return out;
    }

    /** deps/lzf_d.c lzf_decompress() */
    static byte[] lzfDecompress(byte[] in, int off, int inLen, int outLen) {
        byte[] out = new byte[outLen];
        int ip = off, op = 0;
        int end = off + inLen;
        while (ip < end) {
            int ctrl = in[ip++] & 0xff;
            if (ctrl < 32) {
                ctrl++;
                if (op + ctrl > outLen || ip + ctrl > end) return null;
                System.arraycopy(in, ip, out, op, ctrl);
                ip += ctrl;
                op += ctrl;
            } else {
                int len = ctrl >> 5;
                int ref = op - ((ctrl & 0x1f) << 8) - 1;
                if (ip >= end) return null;
                if (len == 7) {
                    len += in[ip++] & 0xff;
                    if (ip >= end) return null;
                }
                ref -= in[ip++] & 0xff;
                if (op + len + 2 > outLen || ref < 0) return null;
                out[op++] = out[ref++];
                out[op++] = out[ref++];
                while (len-- > 0) out[op++] = out[ref++];
            }
        }
        return out;
    }

    // ---- crc64 (Jones polynomial, reflected -- src/crc64.c) ----
    static final long CRC64_POLY_REV = Long.reverse(0xad93d23594c935a9L);
    static final long[] CRC64_TABLE = new long[256];
    static {
        for (int i = 0; i < 256; i++) {
            long crc = i;
            for (int j = 0; j < 8; j++)
                crc = ((crc & 1) != 0) ? ((crc >>> 1) ^ CRC64_POLY_REV) : (crc >>> 1);
            CRC64_TABLE[i] = crc;
        }
    }

    static long crc64(long crc, byte[] data, int off, int len) {
        for (int i = 0; i < len; i++)
            crc = CRC64_TABLE[(int) ((crc ^ data[off + i]) & 0xff)] ^ (crc >>> 8);
        return crc;
    }
}
