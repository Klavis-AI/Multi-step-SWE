package redis;

import java.util.*;

/** The command table: names, arity, flags and key specs, plus dispatch lookup. */
public final class CommandTable {
    public static final LinkedHashMap<String, Cmd> TABLE = new LinkedHashMap<>();
    /** container name -> subcommand name -> Cmd */
    public static final Map<String, Map<String, Cmd>> SUBS = new LinkedHashMap<>();
    private static boolean inited = false;

    public static void reg(String name, int arity, String flags, int fk, int lk, int step, Cmd.Handler h) {
        TABLE.put(name, new Cmd(name, name, arity, flags, fk, lk, step, h, 0));
    }

    public static void regSub(String container, String sub, int arity, String flags, Cmd.Handler h) {
        SUBS.computeIfAbsent(container, x -> new LinkedHashMap<>())
            .put(sub, new Cmd(sub, container + "|" + sub, arity, flags, 0, 0, 0, h, 0));
    }

    public static Cmd lookup(String name) { return TABLE.get(name); }

    public static synchronized void init() {
        if (inited) return;
        inited = true;
        StringCmds.register();
        ListCmds.register();
        HashCmds.register();
        SetCmds.register();
        ZSetCmds.register();
        GeoCmds.register();
        KeyCmds.register();
        ServerCmds.register();
        PubSubCmds.register();
        MultiCmds.register();
        ScriptCmds.register();
        BitCmds.register();
        ConnCmds.register();
        AclCmds.register();
        ReplCmds.register();
        HllCmds.register();
        ClusterCmds.register();
        StreamCmds.register();
        JsonCmds.register();
        ProbCmds.register();
        TsCmds.register();
        VectorCmds.register();
        ModTypes.init();
    }

    /** Dispatch helper for container commands (CONFIG, CLIENT, OBJECT, ...). */
    public static void dispatchSub(Client c, String container) {
        Map<String, Cmd> m = SUBS.get(container);
        if (m == null) throw new RedisError("ERR unknown command");
        if (c.argc < 2) throw RedisError.wrongArgs(container);
        String sub = c.argLower(1);
        Cmd cmd = m.get(sub);
        if (cmd == null) {
            // commandCheckExistence() in server.c
            throw new RedisError("ERR unknown subcommand '" + Server.trunc(c.argStr(1), 128)
                    + "'. Try " + container.toUpperCase(java.util.Locale.ROOT) + " HELP.");
        }
        if ((cmd.arity > 0 && cmd.arity != c.argc) || (c.argc < -cmd.arity)) {
            // commandCheckArity() in server.c
            throw new RedisError("ERR wrong number of arguments for '" + cmd.fullName + "' command");
        }
        cmd.handler.run(c);
    }
}
