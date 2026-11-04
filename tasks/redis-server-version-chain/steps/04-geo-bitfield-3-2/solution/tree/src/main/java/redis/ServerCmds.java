package redis;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;

/**
 * Server administration commands: CONFIG, INFO, DBSIZE, LASTSAVE, SAVE, BGSAVE,
 * BGREWRITEAOF, SHUTDOWN, TIME, LOLWUT, COMMAND, DEBUG, MEMORY, SLOWLOG, LATENCY,
 * ACL and FAILOVER.
 *
 * Ported from redis 8.0.6: config.c, server.c (genRedisInfoString / COMMAND),
 * debug.c, slowlog.c, latency.c, object.c (MEMORY), acl.c, db.c and rdb.c.
 */
public final class ServerCmds {
    private ServerCmds() {}

    // ------------------------------------------------------------------
    // state
    // ------------------------------------------------------------------

    /** DEBUG SET-ACTIVE-EXPIRE toggle. */
    public static boolean activeExpireEnabled = true;
    /**
     * DEBUG LUA-ALWAYS-REPLICATE-COMMANDS.
     *
     * Redis 3.2 introduced this rung's opt-in model -- whole-script replication
     * is the default and a script calls redis.replicate_commands() to switch --
     * and defaults the toggle OFF. Scripting.RunCtx.replicateCommands reads it.
     * From 7.0 effects replication is unconditional and the toggle is gone, so
     * the later steps' copies of this tree default it on and ignore it.
     */
    public static boolean luaAlwaysReplicateCommands = false;
    /** DEBUG QUICKLIST-PACKED-THRESHOLD. */
    public static long quicklistPackedThreshold = 1L << 30;

    private static long peakMemory = 0;
    private static long instantaneousOps = 0;
    private static long lastOpsSampleMs = 0, lastOpsSampleCount = 0;

    // ---- slow log (slowlog.c) ----
    private static final int SLOWLOG_ENTRY_MAX_ARGC = 32;
    private static final int SLOWLOG_ENTRY_MAX_STRING = 128;

    private static final class SlowEntry {
        long id, time, durationUs;
        byte[][] argv;
        String peer, cname;
    }

    private static final LinkedList<SlowEntry> slowlog = new LinkedList<>();
    private static long slowlogNextId = 0;

    // ---- latency monitor (latency.c) ----
    private static final int LATENCY_TS_LEN = 160;

    private static final class LatSample {
        final long time, latency;
        LatSample(long t, long l) { time = t; latency = l; }
    }

    private static final class LatSeries {
        final ArrayDeque<LatSample> samples = new ArrayDeque<>();
        long max;
    }

    private static final LinkedHashMap<String, LatSeries> latencyEvents = new LinkedHashMap<>();

    // ---- per command latency histogram (nanoseconds, ~6% precision) ----
    private static final class LatHist {
        long count, usec;
        final TreeMap<Long, Long> buckets = new TreeMap<>();
    }

    private static final LinkedHashMap<String, LatHist> cmdLatency = new LinkedHashMap<>();

    /** debug.c server.pause_cron: DEBUG PAUSE-CRON stops serverCron() entirely. */
    public static boolean pauseCron = false;

    /** nanoTime at which a client blocked, so the slowlog can be filled on unblock. */
    private static final HashMap<Long, Long> blockStart = new HashMap<>();

    // ---- minimal ACL user table ----
    private static final LinkedHashMap<String, String> aclUsers = new LinkedHashMap<>();
    static { aclUsers.put("default", "on nopass sanitize-payload ~* &* +@all"); }

    // ------------------------------------------------------------------
    // config metadata that the Config table itself does not carry
    // ------------------------------------------------------------------
    private static final Set<String> IMMUTABLE = setOf(
        "rdbchecksum", "daemonize", "always-show-logo", "set-proc-title", "syslog-enabled",
        "cluster-enabled", "disable-thp", "lua-enable-deprecated-api", "aclfile", "unixsocket",
        "pidfile", "cluster-config-file", "syslog-ident", "appendfilename", "appenddirname",
        "server-cpulist", "bio-cpulist", "aof-rewrite-cpulist", "bgsave-cpulist", "logfile",
        "req-res-logfile", "supervised", "syslog-facility", "enable-protected-configs",
        "enable-debug-command", "enable-module-command", "databases", "io-threads",
        "tcp-backlog", "cluster-port", "unixsocketperm", "socket-mark-id",
        "client-default-resp", "replicaof");

    private static final Set<String> PROTECTED_CFG = setOf("dbfilename", "dir");

    private static final Set<String> SENSITIVE = setOf(
        "masteruser", "masterauth", "requirepass", "tls-key-file-pass", "tls-client-key-file-pass");

    private static final Set<String> MEMORY_CONFIG = setOf(
        "proto-max-bulk-len", "repl-backlog-size", "replica-full-sync-buffer-limit", "maxmemory",
        "cluster-link-sendbuf-limit", "active-defrag-ignore-bytes", "hash-max-listpack-value",
        "stream-node-max-bytes", "zset-max-listpack-value", "hll-sparse-max-bytes",
        "client-query-buffer-limit", "maxmemory-clients", "auto-aof-rewrite-min-size");

    private static final Set<String> PERCENT_CONFIG = setOf("maxmemory-clients");

    private static final Map<String, String[]> ENUMS = new HashMap<>();
    static {
        ENUMS.put("maxmemory-policy", new String[]{"volatile-lru", "volatile-lfu", "volatile-random",
                "volatile-ttl", "allkeys-lru", "allkeys-lfu", "allkeys-random", "noeviction"});
        ENUMS.put("appendfsync", new String[]{"always", "everysec", "no"});
        ENUMS.put("loglevel", new String[]{"debug", "verbose", "notice", "warning", "nothing"});
        ENUMS.put("repl-diskless-load", new String[]{"disabled", "on-empty-db", "swapdb"});
        ENUMS.put("acl-pubsub-default", new String[]{"allchannels", "resetchannels"});
        ENUMS.put("sanitize-dump-payload", new String[]{"no", "yes", "clients"});
        ENUMS.put("enable-protected-configs", new String[]{"no", "yes", "local"});
        ENUMS.put("enable-debug-command", new String[]{"no", "yes", "local"});
        ENUMS.put("enable-module-command", new String[]{"no", "yes", "local"});
        ENUMS.put("cluster-preferred-endpoint-type", new String[]{"ip", "hostname", "unknown-endpoint"});
        ENUMS.put("propagation-error-behavior", new String[]{"ignore", "panic", "panic-on-replicas"});
        ENUMS.put("tls-auth-clients", new String[]{"no", "yes", "optional"});
    }

    private static final String[] ACL_CATEGORIES = {
        "keyspace", "read", "write", "set", "sortedset", "list", "hash", "string", "bitmap",
        "hyperloglog", "geo", "stream", "pubsub", "admin", "fast", "slow", "blocking",
        "dangerous", "connection", "transaction", "scripting"};

    /** COMMAND INFO emits flags in this fixed order (server.c addReplyFlagsForCommand). */
    private static final String[] FLAG_ORDER = {
        "write", "readonly", "denyoom", "module", "admin", "pubsub", "noscript", "blocking",
        "loading", "stale", "skip_monitor", "skip_slowlog", "asking", "fast", "no_auth",
        "no_mandatory_keys", "no_async_loading", "no_multi", "movablekeys"};

    private static Set<String> setOf(String... v) {
        return new HashSet<>(Arrays.asList(v));
    }

    // ------------------------------------------------------------------
    // registration
    // ------------------------------------------------------------------
    public static void register() {
        // ---- CONFIG ----
        CommandTable.reg("config", -2, "", 0, 0, 0, c -> dispatch(c, "config"));
        CommandTable.regSub("config", "get", -3, "admin noscript loading stale", ServerCmds::configGet);
        CommandTable.regSub("config", "set", -4, "admin noscript loading stale", ServerCmds::configSet);
        CommandTable.regSub("config", "resetstat", 2, "admin noscript loading stale", ServerCmds::configResetStat);
        CommandTable.regSub("config", "rewrite", 2, "admin noscript loading stale", ServerCmds::configRewrite);
        CommandTable.regSub("config", "help", 2, "loading stale", c -> addHelp(c, CONFIG_HELP));

        // ---- INFO ----
        CommandTable.reg("info", -1, "loading stale", 0, 0, 0, ServerCmds::infoCommand);

        // ---- simple server commands ----
        CommandTable.reg("dbsize", 1, "readonly fast", 0, 0, 0, ServerCmds::dbsize);
        CommandTable.reg("lastsave", 1, "loading stale fast", 0, 0, 0, ServerCmds::lastsave);
        CommandTable.reg("save", 1, "admin noscript", 0, 0, 0, ServerCmds::save);
        CommandTable.reg("bgsave", -1, "admin noscript", 0, 0, 0, ServerCmds::bgsave);
        CommandTable.reg("bgrewriteaof", 1, "admin noscript", 0, 0, 0, ServerCmds::bgrewriteaof);
        CommandTable.reg("shutdown", -1, "admin noscript loading stale", 0, 0, 0, ServerCmds::shutdown);
        CommandTable.reg("time", 1, "loading stale fast", 0, 0, 0, ServerCmds::time);
        CommandTable.reg("lolwut", -1, "readonly fast", 0, 0, 0, ServerCmds::lolwut);
        CommandTable.reg("failover", -1, "admin noscript stale", 0, 0, 0, ServerCmds::failover);

        // ---- COMMAND ----
        CommandTable.reg("command", -1, "loading stale", 0, 0, 0, ServerCmds::commandCommand);
        CommandTable.regSub("command", "count", 2, "loading stale", c -> c.addInt(visibleCommands().size()));
        CommandTable.regSub("command", "docs", -2, "loading stale", ServerCmds::commandDocs);
        CommandTable.regSub("command", "info", -2, "loading stale", ServerCmds::commandInfo);
        CommandTable.regSub("command", "getkeys", -3, "loading stale", ServerCmds::commandGetKeys);
        CommandTable.regSub("command", "getkeysandflags", -3, "loading stale", ServerCmds::commandGetKeysAndFlags);
        CommandTable.regSub("command", "list", -2, "loading stale", ServerCmds::commandList);
        CommandTable.regSub("command", "help", 2, "loading stale", c -> addHelp(c, COMMAND_HELP));

        // ---- DEBUG ----
        CommandTable.reg("debug", -2, "admin noscript loading stale", 0, 0, 0, ServerCmds::debugDispatch);
        CommandTable.regSub("debug", "jmap", 2, "admin noscript loading stale", c -> c.addOk());
        CommandTable.regSub("debug", "pause-cron", 3, "admin noscript loading stale",
                c -> { pauseCron = c.argLong(2) != 0; c.addOk(); });
        CommandTable.regSub("debug", "set-active-expire", 3, "admin noscript loading stale", ServerCmds::debugSetActiveExpire);
        /* DEBUG LUA-ALWAYS-REPLICATE-COMMANDS <0|1> (redis 3.2 .. 6.2; 7.0 made
         * effects replication unconditional and dropped it). This port always
         * replicates a script by its effects, so the toggle is accepted and
         * recorded but cannot change anything: setting it to 1 is exactly what
         * the server already does, and 0 is a logged deviation. Accepting it
         * matters because the 3.2 .. 6.2 `unit/scripting` files call it OUTSIDE
         * any test block, where an error aborts the rest of the file. */
        CommandTable.regSub("debug", "lua-always-replicate-commands", 3,
                "admin noscript loading stale",
                c -> { luaAlwaysReplicateCommands = c.argLong(2) != 0; c.addOk(); });
        CommandTable.regSub("debug", "sleep", 3, "admin noscript loading stale", ServerCmds::debugSleep);
        CommandTable.regSub("debug", "stringmatch-len", 4, "admin noscript loading stale", ServerCmds::debugStringmatchLen);
        CommandTable.regSub("debug", "quicklist-packed-threshold", 3, "admin noscript loading stale",
                ServerCmds::debugQuicklistPackedThreshold);
        CommandTable.regSub("debug", "change-repl-id", 2, "admin noscript loading stale", ServerCmds::debugChangeReplId);
        CommandTable.regSub("debug", "object", 3, "admin noscript loading stale", ServerCmds::debugObject);
        CommandTable.regSub("debug", "reload", -2, "admin noscript loading stale", ServerCmds::debugReload);
        CommandTable.regSub("debug", "loadaof", 2, "admin noscript loading stale", ServerCmds::debugLoadAof);
        CommandTable.regSub("debug", "aof-flush-sleep", 3, "admin noscript loading stale", c -> {
            Server.I.aof.flushSleepUs = Math.max(0, c.argLong(2));
            c.addOk();
        });
        CommandTable.regSub("debug", "replybuffer", -3, "admin noscript loading stale", c -> {
            /* debug.c DEBUG REPLYBUFFER RESIZING <0|1> | PEAK-RESET-TIME <ms|never> */
            if (c.argc == 4 && c.argIs(2, "resizing")) {
                Client.replyBufferResizing = c.argLong(3) != 0;
                c.addOk();
            } else if (c.argc == 4 && c.argIs(2, "peak-reset-time")) {
                c.addOk();
            } else {
                throw RedisError.syntax();
            }
        });
        CommandTable.regSub("debug", "aof-stat", 2, "admin noscript loading stale", c -> {
            Server.I.logNotice("Current AOF state: state=" + Server.I.aof.state
                    + " buf=" + Server.I.aof.buf.len);
            c.addOk();
        });
        CommandTable.regSub("debug", "protocol", 3, "admin noscript loading stale", ServerCmds::debugProtocol);
        CommandTable.regSub("debug", "set-disable-deny-scripts", 3, "admin noscript loading stale",
                ServerCmds::debugSetDisableDenyScripts);
        CommandTable.regSub("debug", "digest", 2, "admin noscript loading stale", ServerCmds::debugDigest);
        CommandTable.regSub("debug", "populate", -3, "admin noscript loading stale", ServerCmds::debugPopulate);
        CommandTable.regSub("debug", "flushall", 2, "admin noscript loading stale", ServerCmds::debugFlushAll);
        CommandTable.regSub("debug", "stringmatch-test", 2, "admin noscript loading stale",
                c -> c.addSimple("Apparently Redis did not crash: test passed"));
        CommandTable.regSub("debug", "help", 2, "loading stale", c -> addHelp(c, DEBUG_HELP));

        // ---- MEMORY ----
        CommandTable.reg("memory", -2, "", 0, 0, 0, c -> dispatch(c, "memory"));
        CommandTable.regSub("memory", "usage", -3, "readonly", ServerCmds::memoryUsage);
        CommandTable.regSub("memory", "doctor", 2, "", ServerCmds::memoryDoctor);
        CommandTable.regSub("memory", "stats", 2, "", ServerCmds::memoryStats);
        CommandTable.regSub("memory", "purge", 2, "", c -> c.addOk());
        CommandTable.regSub("memory", "malloc-stats", 2, "",
                c -> c.addBulk("Stats not supported for the current allocator"));
        CommandTable.regSub("memory", "help", 2, "loading stale", c -> addHelp(c, MEMORY_HELP));

        // ---- SLOWLOG ----
        CommandTable.reg("slowlog", -2, "", 0, 0, 0, c -> dispatch(c, "slowlog"));
        CommandTable.regSub("slowlog", "get", -2, "admin loading stale", ServerCmds::slowlogGet);
        CommandTable.regSub("slowlog", "len", 2, "admin loading stale", c -> c.addInt(slowlog.size()));
        CommandTable.regSub("slowlog", "reset", 2, "admin loading stale", c -> { slowlog.clear(); c.addOk(); });
        CommandTable.regSub("slowlog", "help", 2, "loading stale", c -> addHelp(c, SLOWLOG_HELP));

        // ---- LATENCY ----
        CommandTable.reg("latency", -2, "", 0, 0, 0, c -> dispatch(c, "latency"));
        CommandTable.regSub("latency", "reset", -2, "admin noscript loading stale", ServerCmds::latencyReset);
        CommandTable.regSub("latency", "history", 3, "admin noscript loading stale", ServerCmds::latencyHistory);
        CommandTable.regSub("latency", "latest", 2, "admin noscript loading stale", ServerCmds::latencyLatest);
        CommandTable.regSub("latency", "doctor", 2, "admin noscript loading stale", ServerCmds::latencyDoctor);
        CommandTable.regSub("latency", "histogram", -2, "admin noscript loading stale", ServerCmds::latencyHistogram);
        CommandTable.regSub("latency", "help", 2, "loading stale", c -> addHelp(c, LATENCY_HELP));

        // ---- ACL: registered by AclCmds.register() ----

        ensureWrapped();
    }

    // ------------------------------------------------------------------
    // container dispatch with redis's exact "unknown subcommand" wording
    // ------------------------------------------------------------------
    private static void dispatch(Client c, String container) {
        ensureWrapped();
        CommandTable.dispatchSub(c, container);
    }

    /**
     * DEBUG is a single command in redis's table (it has no registered subcommands), so an
     * unrecognised subcommand goes through addReplySubcommandSyntaxError() instead of
     * commandCheckExistence().
     */
    private static void debugDispatch(Client c) {
        ensureWrapped();
        Map<String, Cmd> m = CommandTable.SUBS.get("debug");
        if (m == null || c.argc < 2) throw subcommandError(c, "debug");
        Cmd sub = m.get(c.argLower(1));
        if (sub == null || (sub.arity > 0 && sub.arity != c.argc) || (c.argc < -sub.arity))
            throw subcommandError(c, "debug");
        sub.handler.run(c);
    }

    private static RedisError subcommandError(Client c, String container) {
        String given = c.argStr(1);
        if (given.length() > 128) given = given.substring(0, 128);
        return new RedisError("ERR unknown subcommand or wrong number of arguments for '" + given
                + "'. Try " + container.toUpperCase(Locale.ROOT) + " HELP.");
    }

    // ------------------------------------------------------------------
    // command handler instrumentation: slowlog + latency monitoring
    // ------------------------------------------------------------------
    private static final Set<Object> wrapped =
            Collections.newSetFromMap(new IdentityHashMap<Object, Boolean>());
    private static int lastTableSize = -1, lastReadySize = -1, lastTimeoutSize = -1;

    /**
     * Wrap every registered command handler so that the slow log, the latency monitor and the
     * per command latency histograms see every command. Command registration all happens before
     * the event loop starts, so after the first scan the size guards make this a no-op.
     */
    static void ensureWrapped() {
        if (CommandTable.TABLE.size() != lastTableSize) {
            lastTableSize = CommandTable.TABLE.size();
            for (Map.Entry<String, Cmd> e : new ArrayList<>(CommandTable.TABLE.entrySet())) {
                final Cmd cmd = e.getValue();
                if (wrapped.contains(cmd)) continue;
                final Cmd.Handler inner = cmd.handler;
                final String full = cmd.fullName;
                final boolean fast = cmd.isFast();
                Cmd nc = new Cmd(cmd.name, cmd.fullName, cmd.arity, cmd.flags, cmd.firstKey,
                        cmd.lastKey, cmd.keyStep, cc -> timed(cc, full, fast, inner), cmd.sinceMajor);
                wrapped.add(nc);
                CommandTable.TABLE.put(e.getKey(), nc);
            }
        }
        if (Blocking.READY.size() != lastReadySize) {
            lastReadySize = Blocking.READY.size();
            for (Map.Entry<String, Blocking.Ready> e : new ArrayList<>(Blocking.READY.entrySet())) {
                final Blocking.Ready inner = e.getValue();
                if (wrapped.contains(inner)) continue;
                Blocking.Ready nr = (s, cl, key) -> {
                    boolean served = inner.tryServe(s, cl, key);
                    if (served) flushBlockedSlowlog(cl);
                    return served;
                };
                wrapped.add(nr);
                Blocking.READY.put(e.getKey(), nr);
            }
        }
        if (Blocking.TIMEOUT.size() != lastTimeoutSize) {
            lastTimeoutSize = Blocking.TIMEOUT.size();
            for (Map.Entry<String, Blocking.Timeout> e : new ArrayList<>(Blocking.TIMEOUT.entrySet())) {
                final Blocking.Timeout inner = e.getValue();
                if (wrapped.contains(inner)) continue;
                Blocking.Timeout nt = (s, cl) -> { inner.onTimeout(s, cl); flushBlockedSlowlog(cl); };
                wrapped.add(nt);
                Blocking.TIMEOUT.put(e.getKey(), nt);
            }
        }
    }

    private static void timed(Client c, String full, boolean fast, Cmd.Handler inner) {
        long start = System.nanoTime();
        byte[][] argv = c.argv;
        int argc = c.argc;
        try {
            inner.run(c);
        } finally {
            long nanos = System.nanoTime() - start;
            if (nanos < 0) nanos = 0;
            recordCmdLatency(resolveName(full, argv, argc), nanos);
            Server s = Server.I;
            if (s != null) {
                long thr = s.config.getLong("latency-monitor-threshold");
                long ms = nanos / 1000000L;
                if (thr > 0 && ms >= thr) latencyAddSample(fast ? "fast-command" : "command", ms);
                if (c.blocked) {
                    if (blockStart.size() > 1024) blockStart.clear();
                    blockStart.put(c.id, start);
                } else {
                    slowlogPush(c, argv, argc, nanos / 1000L, full);
                }
                sampleOps(s);
            }
        }
    }

    /** Container commands report their stats under "container|subcommand", like redis. */
    private static String resolveName(String full, byte[][] argv, int argc) {
        if (argc < 2 || argv == null || full.indexOf('|') >= 0) return full;
        Map<String, Cmd> subs = CommandTable.SUBS.get(full);
        if (subs == null) return full;
        Cmd sub = subs.get(Util.lower(argv[1]));
        return sub == null ? full : sub.fullName;
    }

    private static void flushBlockedSlowlog(Client c) {
        Long st = blockStart.remove(c.id);
        if (st == null) return;
        long us = (System.nanoTime() - st) / 1000L;
        slowlogPush(c, c.argv, c.argc, us, c.lastCmd);
    }

    private static void sampleOps(Server s) {
        long now = System.currentTimeMillis();
        if (lastOpsSampleMs == 0) {
            lastOpsSampleMs = now;
            lastOpsSampleCount = s.statNumCommands;
        } else if (now - lastOpsSampleMs >= 1000) {
            instantaneousOps = (s.statNumCommands - lastOpsSampleCount) * 1000L / (now - lastOpsSampleMs);
            lastOpsSampleMs = now;
            lastOpsSampleCount = s.statNumCommands;
        }
    }

    // ------------------------------------------------------------------
    // slow log
    // ------------------------------------------------------------------
    private static void slowlogPush(Client c, byte[][] argv, int argc, long durationUs, String cmdName) {
        Server s = Server.I;
        if (s == null || argv == null || argc <= 0) return;
        if ("exec".equals(cmdName)) return;   /* CMD_SKIP_SLOWLOG */
        long slower = s.config.getLong("slowlog-log-slower-than");
        long maxLen = s.config.getLong("slowlog-max-len");
        if (slower < 0 || maxLen == 0) return;
        if (durationUs < slower) return;

        SlowEntry e = new SlowEntry();
        e.id = slowlogNextId++;
        e.time = System.currentTimeMillis() / 1000L;
        e.durationUs = durationUs;
        e.argv = slowlogArgv(argv, argc);
        e.peer = c.addr == null ? "" : c.addr;
        e.cname = c.name == null ? "" : c.name;
        slowlog.addFirst(e);
        while (slowlog.size() > maxLen) slowlog.removeLast();
    }

    /** Redact sensitive arguments and trim over long argument vectors / strings. */
    private static byte[][] slowlogArgv(byte[][] argv, int argc) {
        boolean[] redact = new boolean[argc];
        markRedacted(argv, argc, redact);

        int slargc = Math.min(argc, SLOWLOG_ENTRY_MAX_ARGC);
        byte[][] out = new byte[slargc][];
        for (int j = 0; j < slargc; j++) {
            if (slargc != argc && j == slargc - 1) {
                out[j] = Util.b("... (" + (argc - slargc + 1) + " more arguments)");
            } else if (redact[j]) {
                out[j] = Util.b("(redacted)");
            } else if (argv[j].length > SLOWLOG_ENTRY_MAX_STRING) {
                byte[] head = Arrays.copyOf(argv[j], SLOWLOG_ENTRY_MAX_STRING);
                out[j] = Util.concat(head,
                        Util.b("... (" + (argv[j].length - SLOWLOG_ENTRY_MAX_STRING) + " more bytes)"));
            } else {
                out[j] = Util.copy(argv[j]);
            }
        }
        return out;
    }

    private static void markRedacted(byte[][] argv, int argc, boolean[] redact) {
        String cmd = Util.lower(argv[0]);
        String sub = argc > 1 ? Util.lower(argv[1]) : "";
        if (cmd.equals("auth")) {
            for (int j = 1; j < argc; j++) redact[j] = true;
        } else if (cmd.equals("hello")) {
            for (int j = 1; j < argc; j++) {
                if (Util.equalsIgnoreCase(argv[j], "auth")) {
                    if (j + 1 < argc) redact[j + 1] = true;
                    if (j + 2 < argc) redact[j + 2] = true;
                }
            }
        } else if (cmd.equals("config") && sub.equals("set")) {
            for (int j = 2; j + 1 < argc; j += 2) {
                if (SENSITIVE.contains(Util.lower(argv[j]))) redact[j + 1] = true;
            }
        } else if (cmd.equals("acl") && (sub.equals("setuser") || sub.equals("deluser"))) {
            for (int j = 2; j < argc; j++) redact[j] = true;
        } else if (cmd.equals("acl") && sub.equals("getuser")) {
            if (argc > 2) redact[2] = true;
        } else if (cmd.equals("migrate")) {
            for (int j = 1; j < argc; j++) {
                if (Util.equalsIgnoreCase(argv[j], "auth")) {
                    if (j + 1 < argc) redact[j + 1] = true;
                } else if (Util.equalsIgnoreCase(argv[j], "auth2")) {
                    if (j + 1 < argc) redact[j + 1] = true;
                    if (j + 2 < argc) redact[j + 2] = true;
                }
            }
        }
    }

    private static void slowlogGet(Client c) {
        if (c.argc > 3) throw subcommandError(c, "slowlog");
        long count = 10;
        if (c.argc == 3) {
            Long v = Util.string2ll(c.arg(2));
            if (v == null || v < -1)
                throw new RedisError("ERR count should be greater than or equal to -1");
            count = v;
            if (count == -1) count = slowlog.size();
        }
        if (count > slowlog.size()) count = slowlog.size();
        c.addArray((int) count);
        Iterator<SlowEntry> it = slowlog.iterator();
        for (long i = 0; i < count && it.hasNext(); i++) {
            SlowEntry e = it.next();
            /* slowlogCommand(): id, unix time, duration and the trimmed argv. */
            c.addArray(4);
            c.addInt(e.id);
            c.addInt(e.time);
            c.addInt(e.durationUs);
            c.addArray(e.argv.length);
            for (byte[] a : e.argv) c.addBulk(a);
        }
    }

    // ------------------------------------------------------------------
    // latency monitor
    // ------------------------------------------------------------------
    private static void latencyAddSample(String event, long latencyMs) {
        LatSeries ts = latencyEvents.computeIfAbsent(event, x -> new LatSeries());
        if (latencyMs > ts.max) ts.max = latencyMs;
        long now = System.currentTimeMillis() / 1000L;
        LatSample prev = ts.samples.peekLast();
        if (prev != null && prev.time == now) {
            if (latencyMs > prev.latency) {
                ts.samples.removeLast();
                ts.samples.addLast(new LatSample(now, latencyMs));
            }
            return;
        }
        ts.samples.addLast(new LatSample(now, latencyMs));
        while (ts.samples.size() > LATENCY_TS_LEN) ts.samples.removeFirst();
    }

    private static void latencyReset(Client c) {
        int resets = 0;
        if (c.argc == 2) {
            resets = latencyEvents.size();
            latencyEvents.clear();
        } else {
            for (int j = 2; j < c.argc; j++) {
                if (latencyEvents.remove(c.argStr(j)) != null) resets++;
            }
        }
        c.addInt(resets);
    }

    private static void latencyHistory(Client c) {
        LatSeries ts = latencyEvents.get(c.argStr(2));
        if (ts == null) { c.addEmptyArray(); return; }
        c.addArray(ts.samples.size());
        for (LatSample s : ts.samples) {
            c.addArray(2);
            c.addInt(s.time);
            c.addInt(s.latency);
        }
    }

    private static void latencyLatest(Client c) {
        c.addArray(latencyEvents.size());
        for (Map.Entry<String, LatSeries> e : latencyEvents.entrySet()) {
            LatSeries ts = e.getValue();
            LatSample last = ts.samples.peekLast();
            c.addArray(4);
            c.addBulk(e.getKey());
            c.addInt(last == null ? 0 : last.time);
            c.addInt(last == null ? 0 : last.latency);
            c.addInt(ts.max);
        }
    }

    private static void latencyDoctor(Client c) {
        String report;
        if (latencyEvents.isEmpty()) {
            report = "Dave, no latency spike was observed during the lifetime of this Redis instance, "
                   + "not in the slightest bit. I honestly think you ought to sit down calmly, "
                   + "take a stress pill, and think things over.\n";
        } else {
            StringBuilder sb = new StringBuilder();
            sb.append("Dave, I have observed latency spikes in this Redis instance. "
                    + "You don't mind talking about it, do you Dave?\n\n");
            for (Map.Entry<String, LatSeries> e : latencyEvents.entrySet()) {
                LatSeries ts = e.getValue();
                long sum = 0;
                for (LatSample s : ts.samples) sum += s.latency;
                long avg = ts.samples.isEmpty() ? 0 : sum / ts.samples.size();
                sb.append(e.getKey()).append(" - high ").append(e.getKey())
                  .append(" latency of ").append(ts.max).append(" ms (average ").append(avg)
                  .append(" ms, ").append(ts.samples.size()).append(" samples).\n");
            }
            sb.append("\nWhile there are latency events logged, I'm not able to suggest any easy fix. "
                    + "Please use the Redis community to get some help, providing this report in your "
                    + "help request.\n");
            report = sb.toString();
        }
        c.addVerbatim("txt", Util.b(report));
    }

    // ---- per command latency histograms ----
    private static void recordCmdLatency(String full, long nanos) {
        if (nanos < 1) nanos = 1;
        LatHist h = cmdLatency.computeIfAbsent(full, x -> new LatHist());
        h.count++;
        h.usec += nanos / 1000L;
        h.buckets.merge(highestEquivalent(nanos), 1L, Long::sum);
    }

    /** Highest value equivalent to v in a histogram keeping 4 significant binary digits. */
    private static long highestEquivalent(long v) {
        int exp = 63 - Long.numberOfLeadingZeros(v);
        if (exp < 4) return v;
        int shift = exp - 4;
        long bucket = v >>> shift;
        return ((bucket + 1) << shift) - 1;
    }

    private static long valueAtPercentile(LatHist h, double percentile) {
        if (h.count == 0) return 0;
        double p = percentile;
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        long target = (long) Math.ceil(p / 100.0 * h.count);
        if (target < 1) target = 1;
        long cum = 0, last = 0;
        for (Map.Entry<Long, Long> e : h.buckets.entrySet()) {
            cum += e.getValue();
            last = e.getKey();
            if (cum >= target) return e.getKey();
        }
        return last;
    }

    private static void latencyHistogram(Client c) {
        List<String> names = new ArrayList<>();
        if (c.argc == 2) {
            for (Map.Entry<String, LatHist> e : cmdLatency.entrySet())
                if (e.getValue().count > 0) names.add(e.getKey());
        } else {
            for (int j = 2; j < c.argc; j++) {
                String want = c.argLower(j);
                for (Map.Entry<String, LatHist> e : cmdLatency.entrySet()) {
                    if (e.getValue().count == 0) continue;
                    String n = e.getKey();
                    if ((n.equals(want) || n.startsWith(want + "|")) && !names.contains(n)) names.add(n);
                }
            }
        }
        c.addMap(names.size());
        for (String n : names) {
            c.addBulk(n);
            fillCommandCdf(c, cmdLatency.get(n));
        }
    }

    private static void fillCommandCdf(Client c, LatHist h) {
        c.addMap(2);
        c.addBulk("calls");
        c.addInt(h.count);
        c.addBulk("histogram_usec");
        List<long[]> pairs = new ArrayList<>();
        long maxKey = h.buckets.isEmpty() ? 1024 : h.buckets.lastKey();
        long prev = 0;
        for (long step = 1024; step > 0 && step <= (1L << 62); step <<= 1) {
            long cum = 0;
            for (Map.Entry<Long, Long> e : h.buckets.entrySet()) {
                if (e.getKey() < step) cum += e.getValue();
                else break;
            }
            if (cum > prev) pairs.add(new long[]{(step - 1) / 1000, cum});
            prev = cum;
            if (step > maxKey) break;
        }
        c.addMap(pairs.size());
        for (long[] p : pairs) {
            c.addInt(p[0]);
            c.addInt(p[1]);
        }
    }

    // ------------------------------------------------------------------
    // CONFIG
    // ------------------------------------------------------------------
    private static void configGet(Client c) {
        Server s = Server.I;
        LinkedHashMap<String, Config.Param> matches = new LinkedHashMap<>();
        LinkedHashMap<String, String> spelling = new LinkedHashMap<>();
        for (int i = 2; i < c.argc; i++) {
            String name = c.argStr(i);
            if (name.indexOf('*') < 0 && name.indexOf('?') < 0 && name.indexOf('[') < 0) {
                String lower = name.toLowerCase(Locale.ROOT);
                if (matches.containsKey(lower)) continue;
                Config.Param p = s.config.find(lower);
                if (p != null) { matches.put(lower, p); spelling.put(lower, name); }
                continue;
            }
            byte[] pat = c.arg(i);
            for (Config.Param p : new ArrayList<>(s.config.params.values())) {
                if (!matches.containsKey(p.name) && Util.stringmatch(pat, Util.b(p.name), true)) {
                    matches.put(p.name, p);
                    spelling.put(p.name, p.name);
                }
                if (p.alias != null && !p.alias.isEmpty() && !matches.containsKey(p.alias)
                        && Util.stringmatch(pat, Util.b(p.alias), true)) {
                    matches.put(p.alias, p);
                    spelling.put(p.alias, p.alias);
                }
            }
        }
        c.addMap(matches.size());
        for (Map.Entry<String, Config.Param> e : matches.entrySet()) {
            c.addBulk(spelling.get(e.getKey()));
            c.addBulk(configValue(e.getValue()));
        }
    }

    private static String configValue(Config.Param p) {
        String v = p.value == null ? "" : p.value;
        if ("numeric".equals(p.kind)) {
            if (v.isEmpty()) return "0";
            if (PERCENT_CONFIG.contains(p.name) && v.endsWith("%")) return v;
            try { return Long.toString(Long.parseLong(v.trim())); } catch (NumberFormatException ignored) { }
            return Long.toString(Config.parseMemory(v));
        }
        if ("bool".equals(p.kind)) return yesno(v) ? "yes" : "no";
        return v;
    }

    private static boolean yesno(String v) {
        return "yes".equalsIgnoreCase(v) || "1".equals(v) || "true".equalsIgnoreCase(v);
    }

    private static void configSet(Client c) {
        Server s = Server.I;
        if ((c.argc & 1) != 0) throw RedisError.syntax();
        int n = (c.argc - 2) / 2;
        Config.Param[] ps = new Config.Param[n];
        String[] vals = new String[n];

        boolean invalid = false;
        String invalidName = null, errArg = null, errStr = null;
        for (int i = 0; i < n; i++) {
            String nm = c.argStr(2 + i * 2);
            Config.Param p = s.config.find(nm);
            if (p == null) {
                if (!invalid) { invalidName = nm; invalid = true; }
                continue;
            }
            if (invalid) continue;
            if (IMMUTABLE.contains(p.name)) {
                errStr = "can't set immutable config"; errArg = nm; invalid = true; continue;
            }
            if (PROTECTED_CFG.contains(p.name) && !protectedAllowed(s, c)) {
                errStr = "can't set protected config"; errArg = nm; invalid = true; continue;
            }
            boolean dup = false;
            for (int j = 0; j < i; j++) {
                if (ps[j] == p) {
                    errStr = "duplicate parameter"; errArg = nm; invalid = true; dup = true; break;
                }
            }
            if (dup) continue;
            ps[i] = p;
            vals[i] = c.argStr(2 + i * 2 + 1);
        }
        if (invalid) {
            if (invalidName != null)
                throw new RedisError("ERR Unknown option or number of arguments for CONFIG SET - '"
                        + invalidName + "'");
            throw new RedisError("ERR CONFIG SET failed (possibly related to argument '" + errArg
                    + "') - " + errStr);
        }

        /* Validate and normalise everything before applying anything: CONFIG SET is atomic. */
        String[] newVals = new String[n];
        for (int i = 0; i < n; i++) {
            String err = validate(ps[i], vals[i]);
            if (err != null)
                throw new RedisError("ERR CONFIG SET failed (possibly related to argument '"
                        + ps[i].name + "') - " + err);
            newVals[i] = normalise(ps[i], vals[i]);
        }
        for (int i = 0; i < n; i++) applyConfig(s, ps[i], newVals[i]);
        c.addOk();
    }

    private static boolean protectedAllowed(Server s, Client c) {
        String v = s.config.getOr("enable-protected-configs", "no");
        if ("yes".equalsIgnoreCase(v)) return true;
        if ("local".equalsIgnoreCase(v)) {
            if (c.unixSocket) return true;
            String a = c.addr == null ? "" : c.addr;
            return a.startsWith("127.") || a.startsWith("::1") || a.startsWith("[::1]");
        }
        return false;
    }

    private static String validate(Config.Param p, String v) {
        if ("bool".equals(p.kind)) {
            if (!"yes".equalsIgnoreCase(v) && !"no".equalsIgnoreCase(v))
                return "argument must be 'yes' or 'no'";
            return null;
        }
        if ("enum".equals(p.kind)) {
            String[] allowed = ENUMS.get(p.name);
            if (allowed == null) return null;
            for (String a : allowed) if (a.equalsIgnoreCase(v)) return null;
            StringBuilder sb = new StringBuilder("argument(s) must be one of the following: ");
            for (int i = 0; i < allowed.length; i++) {
                if (i > 0) sb.append(", ");
                sb.append(allowed[i]);
            }
            return sb.toString();
        }
        if ("numeric".equals(p.kind)) {
            String t = v.trim();
            if (PERCENT_CONFIG.contains(p.name) && t.length() > 1 && t.endsWith("%")) {
                Long pct = Util.string2ll(Util.b(t.substring(0, t.length() - 1)));
                if (pct == null || pct < 0) return "argument must be a memory or percent value";
                if (pct > 100) return "percentage argument must be less or equal to 100";
                return null;
            }
            if (MEMORY_CONFIG.contains(p.name)) {
                if (!looksLikeMemory(t))
                    return PERCENT_CONFIG.contains(p.name) ? "argument must be a memory or percent value"
                                                           : "argument must be a memory value";
                return null;
            }
            if (Util.string2ll(Util.b(t)) == null) return "argument couldn't be parsed into an integer";
            return null;
        }
        return null;
    }

    private static boolean looksLikeMemory(String s) {
        String low = s.trim().toLowerCase(Locale.ROOT);
        String[] units = {"kb", "mb", "gb", "k", "m", "g", "b"};
        for (String u : units) {
            if (low.endsWith(u)) {
                String num = low.substring(0, low.length() - u.length()).trim();
                if (num.isEmpty()) return false;
                try { Double.parseDouble(num); return true; } catch (NumberFormatException e) { return false; }
            }
        }
        return Util.string2ll(Util.b(low)) != null;
    }

    private static String normalise(Config.Param p, String v) {
        if ("bool".equals(p.kind)) return yesno(v) ? "yes" : "no";
        if ("enum".equals(p.kind)) return v.toLowerCase(Locale.ROOT);
        if ("numeric".equals(p.kind)) {
            String t = v.trim();
            if (PERCENT_CONFIG.contains(p.name) && t.length() > 1 && t.endsWith("%")) return t;
            return Long.toString(Config.parseMemory(t));
        }
        return v;
    }

    private static void applyConfig(Server s, Config.Param p, String v) {
        if (p.name.equals("save")) {
            s.config.saveParams.clear();
            String[] t = v.trim().isEmpty() ? new String[0] : v.trim().split("\\s+");
            for (int i = 0; i + 1 < t.length; i += 2) s.config.saveParams.add(t[i] + " " + t[i + 1]);
            s.config.set("save", String.join(" ", s.config.saveParams));
            return;
        }
        if (p.name.equals("dir")) {
            Path dir = Paths.get(v);
            if (!Files.isDirectory(dir))
                throw new RedisError("ERR CONFIG SET failed (possibly related to argument 'dir') - "
                        + "Changing directory: No such file or directory");
            s.baseDir = dir.toAbsolutePath().normalize();
            s.config.set("dir", s.baseDir.toString());
            return;
        }
        String oldValue = p.value;
        s.config.set(p.name, v);
        if (p.name.equals("port") || p.name.equals("tls-port")) {
            try {
                Tls.updateListeners(s);
            } catch (IOException e) {
                s.config.set(p.name, oldValue);
                throw new RedisError("ERR CONFIG SET failed (possibly related to argument '"
                        + p.name + "') - Unable to listen on this port: " + e.getMessage());
            }
        } else if (p.name.startsWith("tls-") && (s.tlsPort != 0
                || s.config.getBool("tls-replication") || s.config.getBool("tls-cluster"))) {
            /* config.c applyTlsCfg() */
            if (!Tls.buildContext(s)) {
                s.config.set(p.name, oldValue);
                Tls.buildContext(s);
                throw new RedisError("ERR CONFIG SET failed (possibly related to argument '"
                        + p.name + "') - Unable to update TLS configuration. Check server logs.");
            }
        }
        if (p.name.equals("requirepass")) {
            /* config.c: requirepass is the default user's password. */
            AclState.updateDefaultUserPassword(v);
        }
        if (p.name.equals("appendonly") && s.aof != null) {
            if (yesno(v)) {
                if (s.aof.state == Aof.OFF && !s.aof.startAppendOnly())
                    throw new RedisError("ERR Unable to turn on AOF. Check server logs.");
            } else if (s.aof.state != Aof.OFF) {
                s.aof.stopAppendOnly();
            }
        }
        /* config.c updateAofAutoGCEnabled(): re-enabling the gc collects now. */
        if (p.name.equals("aof-disable-auto-gc") && s.aof != null && !yesno(v)) {
            s.aof.delHistoryFiles();
        }
    }

    private static void configResetStat(Client c) {
        Server s = Server.I;
        s.statNumCommands = 0;
        s.statNumConnections = 0;
        s.statExpiredKeys = 0;
        /* resetServerStats(): the eviction counters are statistics too. */
        s.statEvictedKeys = 0;
        ConnCmds.evictedClients = 0;
        s.statKeyspaceHits = 0;
        s.statKeyspaceMisses = 0;
        s.statRejectedConn = 0;
        s.statTotalNetInput = 0;
        s.statTotalNetOutput = 0;
        s.commandStats.clear();
        s.errorStats.clear();
        cmdLatency.clear();
        instantaneousOps = 0;
        lastOpsSampleMs = 0;
        lastOpsSampleCount = 0;
        c.addOk();
    }

    private static void configRewrite(Client c) {
        Server s = Server.I;
        if (s.config.configFile == null)
            throw new RedisError("ERR The server is running without a config file");
        StringBuilder sb = new StringBuilder();
        sb.append("# Generated by CONFIG REWRITE\n");
        for (Config.Param p : new ArrayList<>(s.config.params.values())) {
            if (p.name.equals("save")) continue;
            String v = configValue(p);
            String def = p.def == null ? "" : p.def;
            if (v.equals(def)) continue;
            sb.append(p.name).append(' ').append(quoteConf(v)).append('\n');
        }
        /* config.c rewriteConfigUserOption(): ACL users live in the config file
         * unless an ACL file was configured. */
        String aclfile = s.config.getOr("aclfile", "");
        if (aclfile == null || aclfile.isEmpty()) {
            for (AclState.User u : AclState.USERS.values())
                sb.append("user ").append(u.name).append(' ')
                  .append(AclState.describeUser(u)).append('\n');
        }
        String save = s.config.getOr("save", "");
        if (save == null || save.trim().isEmpty()) {
            sb.append("save \"\"\n");
        } else {
            String[] t = save.trim().split("\\s+");
            for (int i = 0; i + 1 < t.length; i += 2)
                sb.append("save ").append(t[i]).append(' ').append(t[i + 1]).append('\n');
        }
        try {
            Files.write(Paths.get(s.config.configFile), Util.b(sb.toString()));
        } catch (IOException e) {
            throw new RedisError("ERR Rewriting config file: " + e.getMessage());
        }
        s.logNotice("CONFIG REWRITE executed with success.");
        c.addOk();
    }

    private static String quoteConf(String v) {
        if (v.isEmpty()) return "\"\"";
        boolean needQuote = false;
        for (int i = 0; i < v.length(); i++) {
            char ch = v.charAt(i);
            if (ch == '"' || ch == '\'' || ch == '\\' || ch == '#' || ch < 32 || ch > 126) {
                needQuote = true;
                break;
            }
        }
        if (!needQuote) return v;
        StringBuilder sb = new StringBuilder("\"");
        for (int i = 0; i < v.length(); i++) {
            char ch = v.charAt(i);
            switch (ch) {
                case '\\': sb.append("\\\\"); break;
                case '"': sb.append("\\\""); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (ch < 32 || ch > 126) sb.append(String.format("\\x%02x", (int) ch & 0xff));
                    else sb.append(ch);
            }
        }
        return sb.append('"').toString();
    }

    // ------------------------------------------------------------------
    // INFO
    // ------------------------------------------------------------------
    private static final String[] DEFAULT_SECTIONS = {
        "server", "clients", "memory", "persistence", "stats", "replication", "threads",
        "cpu", "module_list", "errorstats", "cluster", "keyspace"};

    private static void infoCommand(Client c) {
        ensureWrapped();
        Set<String> want = new HashSet<>();
        boolean all = false;
        if (c.argc == 1) {
            want.addAll(Arrays.asList(DEFAULT_SECTIONS));
        } else {
            for (int i = 1; i < c.argc; i++) {
                String sec = c.argLower(i);
                if (sec.equals("default")) want.addAll(Arrays.asList(DEFAULT_SECTIONS));
                else if (sec.equals("all") || sec.equals("everything")) all = true;
                else want.add(sec);
            }
        }
        c.addVerbatim("txt", Util.b(genInfo(want, all)));
    }

    private static String genInfo(Set<String> want, boolean all) {
        Server s = Server.I;
        StringBuilder info = new StringBuilder();
        int[] sections = {0};
        long uptime = (System.currentTimeMillis() - s.startTime) / 1000L;

        if (all || want.contains("server")) {
            sep(info, sections);
            info.append("# Server\r\n");
            String mode = s.config.getBool("cluster-enabled") ? "cluster" : "standalone";
            long hz = s.config.getLong("hz");
            if (hz <= 0) hz = 10;   /* CONFIG_DEFAULT_HZ */
            kv(info, "redis_version", Server.VERSION);
            kv(info, "redis_git_sha1", "00000000");
            kv(info, "redis_git_dirty", "0");
            kv(info, "redis_build_id", "0000000000000000");
            kv(info, "redis_mode", mode);
            kv(info, "os", osString());
            kv(info, "arch_bits", "64");
            kv(info, "monotonic_clock", "POSIX clock_gettime");
            kv(info, "multiplexing_api", "epoll");
            kv(info, "atomicvar_api", "c11-builtin");
            kv(info, "gcc_version", "0.0.0");
            kv(info, "process_id", Long.toString(ProcessHandle.current().pid()));
            kv(info, "process_supervised", "no");
            kv(info, "run_id", s.runId);
            kv(info, "tcp_port", Integer.toString(s.port != 0 ? s.port : s.tlsPort));
            kv(info, "server_time_usec", Long.toString(System.currentTimeMillis() * 1000L));
            kv(info, "uptime_in_seconds", Long.toString(uptime));
            kv(info, "uptime_in_days", Long.toString(uptime / (3600 * 24)));
            kv(info, "hz", Long.toString(hz));
            kv(info, "configured_hz", Long.toString(hz));
            kv(info, "lru_clock", Long.toString((System.currentTimeMillis() / 1000L) & 0xffffffL));
            kv(info, "executable", executable());
            kv(info, "config_file", s.config.configFile == null ? "" : s.config.configFile);
            kv(info, "io_threads_active", "0");
            kv(info, "listener0", "name=tcp,bind=" + s.config.getOr("bind", "*")
                    + ",port=" + (s.port != 0 ? s.port : s.tlsPort));
        }

        if (all || want.contains("clients")) {
            sep(info, sections);
            info.append("# Clients\r\n");
            int blocked = 0, tracking = 0, pubsubClients = 0, watching = 0;
            for (Client cl : new ArrayList<>(s.clients.values())) {
                if (cl.blocked) blocked++;
                if (cl.tracking) tracking++;
                if (!cl.subChannels.isEmpty() || !cl.subPatterns.isEmpty() || !cl.subShard.isEmpty())
                    pubsubClients++;
                if (!cl.watched.isEmpty()) watching++;
            }
            long watchedKeys = 0, blockingKeys = 0, blockingKeysOnNokey = 0;
            for (Db d : s.dbs) {
                watchedKeys += d.watchedKeys.size();
                blockingKeys += d.blockingKeys.size();
                for (List<Client> waiting : d.blockingKeys.values()) {
                    for (Client bc : waiting) {
                        if (bc.blocked && StreamCmds.blocksOnNokey(bc)) { blockingKeysOnNokey++; break; }
                    }
                }
            }
            kv(info, "connected_clients", Integer.toString(Math.max(0, s.clients.size() - s.replicas.size())));
            kv(info, "cluster_connections", "0");
            kv(info, "maxclients", Long.toString(s.config.getLong("maxclients")));
            kv(info, "client_recent_max_input_buffer", "0");
            kv(info, "client_recent_max_output_buffer", "0");
            kv(info, "blocked_clients", Integer.toString(blocked));
            kv(info, "tracking_clients", Integer.toString(tracking));
            kv(info, "pubsub_clients", Integer.toString(pubsubClients));
            kv(info, "watching_clients", Integer.toString(watching));
            kv(info, "clients_in_timeout_table", Integer.toString(blocked));
            kv(info, "total_watched_keys", Long.toString(watchedKeys));
            kv(info, "total_blocking_keys", Long.toString(blockingKeys));
            kv(info, "total_blocking_keys_on_nokey", Long.toString(blockingKeysOnNokey));
        }

        if (all || want.contains("memory")) {
            sep(info, sections);
            info.append("# Memory\r\n");
            Runtime rt = Runtime.getRuntime();
            long used = s.usedMemory();
            long rss = used + 1024 * 1024;
            if (used > peakMemory) peakMemory = used;
            if (Mem.peak > peakMemory) peakMemory = Mem.peak;
            long maxmemory = s.config.getLong("maxmemory");
            long totalSystem = rt.maxMemory();
            double frag = used == 0 ? 0.0 : (double) rss / (double) used;
            long memClientsNormal = 0, memClientsSlaves = 0;
            for (Client cc : s.clients.values()) {
                if (s.replicas.contains(cc)) memClientsSlaves += Mem.clientMemUsage(cc);
                else memClientsNormal += Mem.clientMemUsage(cc);
            }
            long memStartup = Mem.STARTUP;
            long memOverhead = memStartup + Mem.keyspaceOverhead(s) + memClientsNormal + memClientsSlaves;
            long memDataset = Math.max(0, used - memOverhead);
            kv(info, "used_memory", Long.toString(used));
            kv(info, "used_memory_human", bytesToHuman(used));
            kv(info, "used_memory_rss", Long.toString(rss));
            kv(info, "used_memory_rss_human", bytesToHuman(rss));
            kv(info, "used_memory_peak", Long.toString(peakMemory));
            kv(info, "used_memory_peak_human", bytesToHuman(peakMemory));
            kv(info, "used_memory_peak_perc", fmt2(peakMemory == 0 ? 100.0 : 100.0 * used / peakMemory) + "%");
            kv(info, "used_memory_overhead", Long.toString(memOverhead));
            kv(info, "used_memory_startup", Long.toString(memStartup));
            kv(info, "used_memory_dataset", Long.toString(memDataset));
            kv(info, "used_memory_dataset_perc", fmt2(used <= memStartup ? 100.0
                    : 100.0 * memDataset / (used - memStartup)) + "%");
            kv(info, "allocator_allocated", Long.toString(used));
            kv(info, "allocator_active", Long.toString(rss));
            kv(info, "allocator_resident", Long.toString(rss));
            kv(info, "allocator_muzzy", "0");
            kv(info, "total_system_memory", Long.toString(totalSystem));
            kv(info, "total_system_memory_human", bytesToHuman(totalSystem));
            kv(info, "used_memory_lua", "0");
            kv(info, "used_memory_vm_eval", "0");
            kv(info, "used_memory_lua_human", bytesToHuman(0));
            kv(info, "used_memory_scripts_eval", "0");
            kv(info, "number_of_cached_scripts",
                    Integer.toString(s.scripting == null ? 0 : s.scripting.scripts.size()));
            kv(info, "number_of_functions",
                    Integer.toString(s.scripting == null ? 0 : s.scripting.functions.size()));
            kv(info, "number_of_libraries",
                    Integer.toString(s.scripting == null ? 0 : s.scripting.libraries.size()));
            kv(info, "used_memory_vm_functions", "0");
            kv(info, "used_memory_vm_total", "0");
            kv(info, "used_memory_vm_total_human", bytesToHuman(0));
            kv(info, "used_memory_functions", "0");
            kv(info, "used_memory_scripts", "0");
            kv(info, "used_memory_scripts_human", bytesToHuman(0));
            kv(info, "maxmemory", Long.toString(maxmemory));
            kv(info, "maxmemory_human", bytesToHuman(maxmemory));
            kv(info, "maxmemory_policy", s.config.getOr("maxmemory-policy", "noeviction"));
            kv(info, "allocator_frag_ratio", fmt2(1.0));
            kv(info, "allocator_frag_bytes", "0");
            kv(info, "allocator_rss_ratio", fmt2(1.0));
            kv(info, "allocator_rss_bytes", "0");
            kv(info, "rss_overhead_ratio", fmt2(1.0));
            kv(info, "rss_overhead_bytes", "0");
            kv(info, "mem_fragmentation_ratio", fmt2(frag));
            kv(info, "mem_fragmentation_bytes", Long.toString(rss - used));
            kv(info, "mem_not_counted_for_evict", Long.toString(Mem.notCountedForEvict(s)));
            kv(info, "mem_replication_backlog", "0");
            kv(info, "mem_total_replication_buffers", Long.toString(memClientsSlaves));
            kv(info, "mem_replica_full_sync_buffer", "0");
            kv(info, "mem_clients_slaves", Long.toString(memClientsSlaves));
            kv(info, "mem_clients_normal", Long.toString(memClientsNormal));
            kv(info, "mem_cluster_links", "0");
            kv(info, "mem_aof_buffer", Long.toString(s.aof == null ? 0 : s.aof.buf.len));
            kv(info, "mem_allocator", "libc");
            kv(info, "mem_overhead_db_hashtable_rehashing", "0");
            kv(info, "active_defrag_running", "0");
            kv(info, "lazyfree_pending_objects", "0");
            kv(info, "lazyfreed_objects", "0");
        }

        if (all || want.contains("persistence")) {
            sep(info, sections);
            info.append("# Persistence\r\n");
            boolean aofOn = s.aof != null && s.aof.isEnabled();
            kv(info, "loading", s.loading ? "1" : "0");
            kv(info, "async_loading", "0");
            kv(info, "current_cow_peak", "0");
            kv(info, "current_cow_size", "0");
            kv(info, "current_cow_size_age", "0");
            kv(info, "current_fork_perc", fmt2(0.0));
            kv(info, "current_save_keys_processed", "0");
            kv(info, "current_save_keys_total", "0");
            kv(info, "rdb_changes_since_last_save", Long.toString(Math.max(0, s.dirty - Persist.lastSaveDirty)));
            kv(info, "rdb_bgsave_in_progress", Persist.bgsaveInProgress ? "1" : "0");
            kv(info, "rdb_last_save_time", Long.toString(s.lastSave));
            kv(info, "rdb_last_bgsave_status", Persist.lastBgsaveStatus);
            kv(info, "rdb_last_bgsave_time_sec", "0");
            kv(info, "rdb_current_bgsave_time_sec", "-1");
            kv(info, "rdb_saves", Long.toString(Persist.rdbSaves));
            kv(info, "rdb_last_cow_size", "0");
            kv(info, "rdb_last_load_keys_expired", "0");
            kv(info, "rdb_last_load_keys_loaded", "0");
            kv(info, "aof_enabled", aofOn ? "1" : "0");
            kv(info, "aof_rewrite_in_progress", (s.aof != null && s.aof.childPid != -1) ? "1" : "0");
            kv(info, "aof_rewrite_scheduled", (s.aof != null && s.aof.rewriteScheduled) ? "1" : "0");
            kv(info, "aof_last_rewrite_time_sec",
                    Long.toString(s.aof == null ? -1 : s.aof.lastRewriteTimeSec));
            kv(info, "aof_current_rewrite_time_sec",
                    (s.aof != null && s.aof.rewriteTimeStart != -1)
                        ? Long.toString((System.currentTimeMillis() - s.aof.rewriteTimeStart) / 1000) : "-1");
            kv(info, "aof_last_bgrewrite_status", s.aof == null ? "ok" : s.aof.lastBgrewriteStatus);
            kv(info, "aof_rewrites", Long.toString(s.aof == null ? 0 : s.aof.statRewrites));
            kv(info, "aof_rewrites_consecutive_failures",
                    Long.toString(s.aof == null ? 0 : s.aof.consecutiveFailures));
            kv(info, "aof_last_write_status", s.aof == null ? "ok" : s.aof.lastWriteStatus);
            kv(info, "aof_last_cow_size", "0");
            kv(info, "module_fork_in_progress", "0");
            kv(info, "module_fork_last_cow_size", "0");
            if (aofOn) {
                kv(info, "aof_current_size", Long.toString(s.aof.currentSize));
                kv(info, "aof_base_size", Long.toString(s.aof.baseSize));
                kv(info, "aof_last_incr_size", Long.toString(s.aof.lastIncrSize));
                kv(info, "aof_last_incr_fsync_offset",
                        Long.toString(s.aof.lastIncrFsyncOffset));
                kv(info, "aof_pending_rewrite", s.aof.rewriteScheduled ? "1" : "0");
                kv(info, "aof_buffer_length", Long.toString(s.aof.buf.len));
                kv(info, "aof_rewrite_buffer_length", "0");
                kv(info, "aof_pending_bio_fsync", "0");
                kv(info, "aof_delayed_fsync", "0");
            }
        }

        if (all || want.contains("threads")) {
            sep(info, sections);
            info.append("# Threads\r\n");
            info.append("io_thread_0:clients=").append(s.clients.size())
                .append(",reads=").append(s.statNumCommands)
                .append(",writes=").append(s.statNumCommands).append("\r\n");
        }

        if (all || want.contains("stats")) {
            sep(info, sections);
            info.append("# Stats\r\n");
            long totalErrors = 0;
            for (Long v : s.errorStats.values()) totalErrors += v;
            kv(info, "total_connections_received", Long.toString(s.statNumConnections));
            kv(info, "total_commands_processed", Long.toString(s.statNumCommands));
            kv(info, "instantaneous_ops_per_sec", Long.toString(instantaneousOps));
            kv(info, "total_net_input_bytes", Long.toString(s.statTotalNetInput));
            kv(info, "total_net_output_bytes", Long.toString(s.statTotalNetOutput));
            kv(info, "total_net_repl_input_bytes", "0");
            kv(info, "total_net_repl_output_bytes", "0");
            kv(info, "instantaneous_input_kbps", fmt2(0.0));
            kv(info, "instantaneous_output_kbps", fmt2(0.0));
            kv(info, "instantaneous_input_repl_kbps", fmt2(0.0));
            kv(info, "instantaneous_output_repl_kbps", fmt2(0.0));
            kv(info, "rejected_connections", Long.toString(s.statRejectedConn));
            kv(info, "sync_full", Long.toString(s.repl == null ? 0 : s.repl.statSyncFull));
            kv(info, "sync_partial_ok", Long.toString(s.repl == null ? 0 : s.repl.statSyncPartialOk));
            kv(info, "sync_partial_err", Long.toString(s.repl == null ? 0 : s.repl.statSyncPartialErr));
            kv(info, "expired_subkeys", "0");
            kv(info, "expired_keys", Long.toString(s.statExpiredKeys));
            kv(info, "expired_stale_perc", fmt2(0.0));
            kv(info, "expired_time_cap_reached_count", "0");
            kv(info, "expire_cycle_cpu_milliseconds", "0");
            kv(info, "evicted_keys", Long.toString(s.statEvictedKeys));
            kv(info, "evicted_clients", Long.toString(ConnCmds.evictedClients));
            kv(info, "evicted_scripts",
                    Long.toString(s.scripting == null ? 0L : s.scripting.statEvictedScripts));
            kv(info, "total_eviction_exceeded_time", "0");
            kv(info, "current_eviction_exceeded_time", "0");
            kv(info, "keyspace_hits", Long.toString(s.statKeyspaceHits));
            kv(info, "keyspace_misses", Long.toString(s.statKeyspaceMisses));
            kv(info, "pubsub_channels", Integer.toString(s.pubsub.channels.size()));
            kv(info, "pubsub_patterns", Integer.toString(s.pubsub.patterns.size()));
            kv(info, "pubsubshard_channels", Integer.toString(s.pubsub.shard.size()));
            kv(info, "latest_fork_usec", "0");
            kv(info, "total_forks", Long.toString(Persist.totalForks));
            kv(info, "migrate_cached_sockets", "0");
            kv(info, "slave_expires_tracked_keys", "0");
            kv(info, "active_defrag_hits", "0");
            kv(info, "active_defrag_misses", "0");
            kv(info, "active_defrag_key_hits", "0");
            kv(info, "active_defrag_key_misses", "0");
            kv(info, "total_active_defrag_time", "0");
            kv(info, "current_active_defrag_time", "0");
            kv(info, "tracking_total_keys", Long.toString(Tracking.totalKeys()));
            kv(info, "tracking_total_items", Long.toString(Tracking.totalItems));
            kv(info, "tracking_total_prefixes", Long.toString(Tracking.totalPrefixes()));
            kv(info, "unexpected_error_replies", "0");
            kv(info, "total_error_replies", Long.toString(totalErrors));
            kv(info, "dump_payload_sanitizations", "0");
            kv(info, "total_reads_processed", Long.toString(s.statNumCommands));
            kv(info, "total_writes_processed", Long.toString(s.statNumCommands));
            kv(info, "io_threaded_reads_processed", "0");
            kv(info, "io_threaded_writes_processed", "0");
            kv(info, "client_query_buffer_limit_disconnections", "0");
            kv(info, "client_output_buffer_limit_disconnections", "0");
            kv(info, "reply_buffer_shrinks", "0");
            kv(info, "reply_buffer_expands", "0");
            kv(info, "eventloop_cycles", "0");
            kv(info, "eventloop_duration_sum", "0");
            kv(info, "eventloop_duration_cmd_sum", "0");
            kv(info, "instantaneous_eventloop_cycles_per_sec", "0");
            kv(info, "instantaneous_eventloop_duration_usec", "0");
            kv(info, "acl_access_denied_auth", Long.toString(AclState.deniedAuth));
            kv(info, "acl_access_denied_cmd", Long.toString(AclState.deniedCmd));
            kv(info, "acl_access_denied_key", Long.toString(AclState.deniedKey));
            kv(info, "acl_access_denied_channel", Long.toString(AclState.deniedChannel));
        }

        if (all || want.contains("replication")) {
            sep(info, sections);
            info.append("# Replication\r\n");
            if (s.repl != null) {
                info.append(s.repl.infoBody());
            } else {
                /* Repl does not expose the fields yet: emit the neutral master values. */
                kv(info, "role", "master");
                kv(info, "connected_slaves", "0");
                kv(info, "master_failover_state", "no-failover");
                kv(info, "master_replid", s.replId);
                kv(info, "master_replid2", s.replId2);
                kv(info, "master_repl_offset", Long.toString(s.masterReplOffset));
                kv(info, "second_repl_offset", Long.toString(s.secondReplOffset));
                kv(info, "repl_backlog_active", "0");
                kv(info, "repl_backlog_size", Long.toString(s.config.getLong("repl-backlog-size")));
                kv(info, "repl_backlog_first_byte_offset", "0");
                kv(info, "repl_backlog_histlen", "0");
            }
        }

        if (all || want.contains("cpu")) {
            sep(info, sections);
            info.append("# CPU\r\n");
            long[] cpu = procCpu();
            kv(info, "used_cpu_sys", secUsec(cpu[1]));
            kv(info, "used_cpu_user", secUsec(cpu[0]));
            kv(info, "used_cpu_sys_children", secUsec(0));
            kv(info, "used_cpu_user_children", secUsec(0));
            kv(info, "used_cpu_sys_main_thread", secUsec(cpu[1]));
            kv(info, "used_cpu_user_main_thread", secUsec(cpu[0]));
        }

        if (all || want.contains("module_list") || want.contains("modules")) {
            sep(info, sections);
            info.append("# Modules\r\n");
        }

        if (all || want.contains("commandstats")) {
            sep(info, sections);
            info.append("# Commandstats\r\n");
            Map<String, long[]> snap = new LinkedHashMap<String, long[]>(s.commandStats);
            for (Map.Entry<String, long[]> e : snap.entrySet()) {
                long calls = e.getValue()[0];
                long usec = e.getValue()[1];
                long failed = e.getValue().length > 2 ? e.getValue()[2] : 0;
                /* Containers have no stats of their own: redis reports their
                 * subcommands, which we keep in the per command tally below. */
                if (calls <= 0 || CommandTable.SUBS.containsKey(e.getKey())) continue;
                cmdStatLine(info, e.getKey(), calls, usec, failed);
            }
            Map<String, LatHist> subs = new LinkedHashMap<String, LatHist>(cmdLatency);
            for (Map.Entry<String, LatHist> e : subs.entrySet()) {
                if (e.getKey().indexOf('|') < 0 || e.getValue().count == 0) continue;
                cmdStatLine(info, e.getKey(), e.getValue().count, e.getValue().usec, 0);
            }
        }

        if (all || want.contains("errorstats")) {
            sep(info, sections);
            info.append("# Errorstats\r\n");
            List<String> codes = new ArrayList<>(s.errorStats.keySet());
            Collections.sort(codes);
            for (String code : codes)
                info.append("errorstat_").append(code).append(":count=")
                    .append(s.errorStats.get(code)).append("\r\n");
        }

        if (all || want.contains("latencystats")) {
            sep(info, sections);
            info.append("# Latencystats\r\n");
            if (s.config.getBool("latency-tracking")) {
                double[] pcts = trackingPercentiles(s);
                Map<String, LatHist> snap = new LinkedHashMap<String, LatHist>(cmdLatency);
                for (Map.Entry<String, LatHist> e : snap.entrySet()) {
                    LatHist h = e.getValue();
                    if (h.count == 0) continue;
                    info.append("latency_percentiles_usec_").append(e.getKey()).append(':');
                    for (int i = 0; i < pcts.length; i++) {
                        if (i > 0) info.append(',');
                        info.append('p').append(trimDouble(pcts[i])).append('=')
                            .append(fmt3(valueAtPercentile(h, pcts[i]) / 1000.0));
                    }
                    info.append("\r\n");
                }
            }
        }

        if (all || want.contains("cluster")) {
            sep(info, sections);
            info.append("# Cluster\r\n");
            kv(info, "cluster_enabled", s.config.getBool("cluster-enabled") ? "1" : "0");
        }

        if (all || want.contains("keyspace")) {
            sep(info, sections);
            info.append("# Keyspace\r\n");
            for (Db d : s.dbs) {
                int keys = d.dict.size();
                int expires = d.expires.size();
                if (keys == 0 && expires == 0) continue;
                info.append("db").append(d.id).append(":keys=").append(keys)
                    .append(",expires=").append(expires).append(",avg_ttl=0\r\n");
            }
        }

        return info.toString();
    }

    private static double[] trackingPercentiles(Server s) {
        String v = s.config.getOr("latency-tracking-info-percentiles", "");
        if (v == null || v.trim().isEmpty()) v = "50 99 99.9";
        String[] t = v.trim().split("[,\\s]+");
        List<Double> out = new ArrayList<>();
        for (String x : t) {
            try { out.add(Double.parseDouble(x)); } catch (NumberFormatException ignored) { }
        }
        if (out.isEmpty()) out.add(50.0);
        double[] r = new double[out.size()];
        for (int i = 0; i < r.length; i++) r[i] = out.get(i);
        return r;
    }

    private static void cmdStatLine(StringBuilder info, String name, long calls, long usec, long failed) {
        info.append("cmdstat_").append(name)
            .append(":calls=").append(calls)
            .append(",usec=").append(usec)
            .append(",usec_per_call=").append(fmt2(calls == 0 ? 0 : (double) usec / calls))
            .append(",rejected_calls=0,failed_calls=").append(failed).append("\r\n");
    }

    private static void sep(StringBuilder info, int[] sections) {
        if (sections[0]++ > 0) info.append("\r\n");
    }

    private static void kv(StringBuilder sb, String k, String v) {
        sb.append(k).append(':').append(v).append("\r\n");
    }

    private static String fmt2(double d) {
        if (Double.isNaN(d) || Double.isInfinite(d)) d = 0;
        return String.format(Locale.US, "%.2f", d);
    }

    private static String fmt3(double d) {
        if (Double.isNaN(d) || Double.isInfinite(d)) d = 0;
        return String.format(Locale.US, "%.3f", d);
    }

    /** Redis trims the trailing zeroes of the percentile label ("50.000000" -> "50"). */
    private static String trimDouble(double d) {
        String s = String.format(Locale.US, "%f", d);
        if (s.indexOf('.') >= 0) {
            int end = s.length();
            while (end > 0 && s.charAt(end - 1) == '0') end--;
            if (end > 0 && s.charAt(end - 1) == '.') end--;
            s = s.substring(0, end);
        }
        return s;
    }

    private static String bytesToHuman(long n) {
        if (n < 1024) return n + "B";
        if (n < 1024L * 1024) return fmt2((double) n / 1024.0) + "K";
        if (n < 1024L * 1024 * 1024) return fmt2((double) n / (1024.0 * 1024)) + "M";
        if (n < 1024L * 1024 * 1024 * 1024) return fmt2((double) n / (1024.0 * 1024 * 1024)) + "G";
        return fmt2((double) n / (1024.0 * 1024 * 1024 * 1024)) + "T";
    }

    private static String osString() {
        String name = System.getProperty("os.name", "Linux");
        String rel = System.getProperty("os.version", "0");
        String arch = System.getProperty("os.arch", "x86_64");
        if (arch.equals("amd64")) arch = "x86_64";
        return name + " " + rel + " " + arch;
    }

    private static String executable() {
        try {
            return ProcessHandle.current().info().command().orElse("");
        } catch (Throwable t) {
            return "";
        }
    }

    /** {utime_usec, stime_usec} read from /proc/self/stat, zeroes when unavailable. */
    private static long[] procCpu() {
        try {
            String stat = new String(Files.readAllBytes(Paths.get("/proc/self/stat")),
                    java.nio.charset.StandardCharsets.ISO_8859_1);
            int close = stat.lastIndexOf(')');
            String[] f = stat.substring(close + 2).trim().split("\\s+");
            long utime = Long.parseLong(f[11]);
            long stime = Long.parseLong(f[12]);
            return new long[]{utime * 10000L, stime * 10000L};
        } catch (Throwable t) {
            return new long[]{0, 0};
        }
    }

    private static String secUsec(long usec) {
        return (usec / 1000000L) + "." + String.format(Locale.US, "%06d", usec % 1000000L);
    }

    // ------------------------------------------------------------------
    // simple server commands
    // ------------------------------------------------------------------
    private static void dbsize(Client c) {
        ensureWrapped();
        c.addInt(Server.I.dbs[c.db].size());
    }

    private static void lastsave(Client c) {
        ensureWrapped();
        c.addInt(Server.I.lastSave);
    }

    private static void save(Client c) {
        ensureWrapped();
        Server s = Server.I;
        try {
            Rdb.saveFile(s, s.rdbPath());
        } catch (Exception e) {
            Persist.lastBgsaveStatus = "err";
            s.logWarn("Error saving DB on disk: " + e);
            throw new RedisError("ERR");
        }
        Persist.lastSaveDirty = s.dirty;
        Persist.lastSaveTime = System.currentTimeMillis();
        Persist.lastBgsaveStatus = "ok";
        s.lastSave = System.currentTimeMillis() / 1000L;
        c.addOk();
    }

    /**
     * rdb.c bgsaveCommand(). The JVM cannot fork, so the save is performed by
     * the next cron tick while rdb_bgsave_in_progress reports the "child".
     */
    private static void bgsave(Client c) {
        ensureWrapped();
        Server s = Server.I;
        if (c.argc > 1 && !(c.argc == 2 && c.argIs(1, "schedule"))) throw RedisError.syntax();
        if (Persist.bgsaveInProgress) {
            c.addError("ERR Background save already in progress");
            return;
        }
        if (s.aof.childPid != -1) {
            if (c.argc == 2 && c.argIs(1, "schedule")) {
                Persist.bgsaveScheduled = true;
                c.addSimple("Background saving scheduled");
                return;
            }
            c.addError("ERR An AOF log rewriting in progress: can't BGSAVE right now. "
                    + "Use BGSAVE SCHEDULE in order to schedule a BGSAVE whenever possible.");
            return;
        }
        Persist.startBgSave(s);
        c.addSimple("Background saving started");
    }

    /** aof.c bgrewriteaofCommand(). */
    private static void bgrewriteaof(Client c) {
        ensureWrapped();
        Server s = Server.I;
        if (s.aof.childPid != -1) {
            c.addError("ERR Background append only file rewriting already in progress");
        } else if (Persist.bgsaveInProgress) {
            /* aof.c bgrewriteaofCommand() also postpones inside MULTI since 7.0,
             * but the 2.8..6.2 suites still expect the rewrite (and its child) to
             * start from inside EXEC, and nothing in 7.x depends on the newer
             * behaviour, so the pre-7.0 rule is kept here. */
            s.aof.rewriteScheduled = true;
            /* A manually triggered AOFRW resets the failure count so that it
             * can be executed immediately. */
            s.aof.consecutiveFailures = 0;
            c.addSimple("Background append only file rewriting scheduled");
        } else if (s.aof.startBackgroundRewrite()) {
            c.addSimple("Background append only file rewriting started");
        } else {
            c.addError("ERR Can't execute an AOF background rewriting. "
                    + "Please check the server logs for more information.");
        }
    }

    private static void shutdown(Client c) {
        ensureWrapped();
        Server s = Server.I;
        boolean nosave = false, dosave = false, abort = false;
        for (int i = 1; i < c.argc; i++) {
            if (c.argIs(i, "nosave")) nosave = true;
            else if (c.argIs(i, "save")) dosave = true;
            else if (c.argIs(i, "now")) { /* accepted: we never delay a shutdown */ }
            else if (c.argIs(i, "force")) { /* accepted */ }
            else if (c.argIs(i, "abort")) abort = true;
            else throw RedisError.syntax();
        }
        if ((abort && (nosave || dosave)) || (nosave && dosave)) throw RedisError.syntax();
        if (abort) throw new RedisError("ERR No shutdown in progress.");
        if (dosave) {
            try {
                Rdb.saveFile(s, s.rdbPath());
            } catch (Exception e) {
                s.logWarn("Error trying to save the DB: " + e);
            }
            s.doShutdown(false);
        } else {
            s.doShutdown(!nosave);
        }
    }

    private static void time(Client c) {
        ensureWrapped();
        long ms = System.currentTimeMillis();
        c.addArray(2);
        c.addBulk(Long.toString(ms / 1000L));
        c.addBulk(Long.toString((ms % 1000L) * 1000L));
    }

    private static void lolwut(Client c) {
        ensureWrapped();
        if (c.argc >= 3 && c.argIs(1, "version")) {
            if (Util.string2ll(c.arg(2)) == null) throw RedisError.notInt();
        }
        c.addVerbatim("txt", Util.b("Redis ver. " + Server.VERSION + "\n"));
    }

    private static void failover(Client c) {
        ensureWrapped();
        if (c.argc == 2 && c.argIs(1, "abort")) throw new RedisError("ERR No failover in progress.");
        throw new RedisError("ERR FAILOVER requires connected replicas.");
    }

    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // COMMAND
    //
    // Everything this section reports about a command -- ACL categories,
    // command tips, key specifications, the legacy first/last/step range and
    // the documentation fields -- comes from CommandMeta, generated by
    // tools_gencmdmeta.py out of redis's own src/commands/*.json. Commands
    // this server adds on top of redis's table (the module-provided ones) have
    // no entry there and fall back to what CommandTable.reg() declared.
    // ------------------------------------------------------------------
    private static List<Cmd> visibleCommands() {
        return new ArrayList<>(CommandTable.TABLE.values());
    }

    private static void commandCommand(Client c) {
        ensureWrapped();
        if (c.argc > 1) { dispatch(c, "command"); return; }
        List<Cmd> cmds = visibleCommands();
        c.addArray(cmds.size());
        for (Cmd cmd : cmds) addCommandInfo(c, cmd);
    }

    /** server.c addReplyCommandInfo() */
    private static void addCommandInfo(Client c, Cmd cmd) {
        if (cmd == null) { c.addNil(); return; }
        CommandMeta.Entry m = CommandMeta.get(cmd.fullName);
        c.addArray(10);
        c.addBulk(cmd.fullName);
        c.addInt(m == null ? cmd.arity : m.arity);
        if (m == null) {
            addCommandFlags(c, cmd);
            c.addInt(cmd.firstKey);
            c.addInt(cmd.lastKey);
            c.addInt(cmd.keyStep);
            c.addSet(0);                    // ACL categories
            c.addSet(0);                    // command tips
            c.addSet(0);                    // key specs
        } else {
            c.addSet(m.flags.length);
            for (String f : m.flags) c.addSimple(f);
            c.addInt(m.firstKey);
            c.addInt(m.lastKey);
            c.addInt(m.keyStep);
            c.addSet(m.categories.length);
            for (String cat : m.categories) c.addSimple(cat);
            c.addSet(m.tips.length);
            for (String t : m.tips) c.addBulk(t);
            addKeySpecs(c, m.specs);
        }
        Map<String, Cmd> subs = topLevelSubs(cmd);
        if (subs == null) {
            c.addSet(0);
        } else {
            List<Cmd> list = new ArrayList<>(subs.values());
            c.addArray(list.size());
            for (Cmd sub : list) addCommandInfo(c, sub);
        }
    }

    /** server.c addReplyCommandKeySpecs() */
    private static void addKeySpecs(Client c, CommandMeta.Spec[] specs) {
        c.addSet(specs.length);
        for (CommandMeta.Spec sp : specs) {
            c.addMap(sp.notes == null ? 3 : 4);
            if (sp.notes != null) { c.addBulk("notes"); c.addBulk(sp.notes); }
            c.addBulk("flags");
            List<String> names = CommandMeta.keyFlagNames(sp.flags);
            c.addSet(names.size());
            for (String n : names) c.addSimple(n);
            c.addBulk("begin_search");
            c.addMap(2);
            c.addBulk("type");
            if (sp.bsType == CommandMeta.BS_INDEX) {
                c.addBulk("index");
                c.addBulk("spec");
                c.addMap(1);
                c.addBulk("index"); c.addInt(sp.bsIndex);
            } else if (sp.bsType == CommandMeta.BS_KEYWORD) {
                c.addBulk("keyword");
                c.addBulk("spec");
                c.addMap(2);
                c.addBulk("keyword"); c.addBulk(sp.bsKeyword);
                c.addBulk("startfrom"); c.addInt(sp.bsStartFrom);
            } else {
                c.addBulk("unknown");
                c.addBulk("spec");
                c.addMap(0);
            }
            c.addBulk("find_keys");
            c.addMap(2);
            c.addBulk("type");
            if (sp.fkType == CommandMeta.FK_RANGE) {
                c.addBulk("range");
                c.addBulk("spec");
                c.addMap(3);
                c.addBulk("lastkey"); c.addInt(sp.lastkey);
                c.addBulk("keystep"); c.addInt(sp.keystep);
                c.addBulk("limit"); c.addInt(sp.limit);
            } else if (sp.fkType == CommandMeta.FK_KEYNUM) {
                c.addBulk("keynum");
                c.addBulk("spec");
                c.addMap(3);
                c.addBulk("keynumidx"); c.addInt(sp.keynumidx);
                c.addBulk("firstkey"); c.addInt(sp.firstkey);
                c.addBulk("keystep"); c.addInt(sp.keystep);
            } else {
                c.addBulk("unknown");
                c.addBulk("spec");
                c.addMap(0);
            }
        }
    }

    private static Map<String, Cmd> topLevelSubs(Cmd cmd) {
        if (!cmd.name.equals(cmd.fullName)) return null;
        Map<String, Cmd> subs = CommandTable.SUBS.get(cmd.name);
        return (subs == null || subs.isEmpty()) ? null : subs;
    }

    private static void addCommandFlags(Client c, Cmd cmd) {
        List<String> flags = new ArrayList<>();
        for (String f : FLAG_ORDER) if (cmd.has(f)) flags.add(f);
        c.addSet(flags.size());
        for (String f : flags) c.addSimple(f);
    }

    /** server.c addReplyCommandDocs() */
    private static void addCommandDocs(Client c, Cmd cmd) {
        Map<String, Cmd> subs = topLevelSubs(cmd);
        CommandMeta.Entry m = CommandMeta.get(cmd.fullName);
        if (m == null) {
            c.addMap(subs == null ? 2 : 3);
            c.addBulk("summary");
            c.addBulk(cmd.fullName);
            c.addBulk("group");
            c.addBulk("server");
            if (subs != null) addDocsSubcommands(c, subs);
            return;
        }
        int n = 1;                                      // "group" is always there
        if (!m.summary.isEmpty()) n++;
        if (!m.since.isEmpty()) n++;
        if (!m.complexity.isEmpty()) n++;
        if (m.docFlags.length > 0) n++;
        if (m.deprecatedSince != null) n++;
        if (m.replacedBy != null) n++;
        if (m.history.length > 0) n++;
        if (m.args.length > 0) n++;
        if (subs != null) n++;
        c.addMap(n);
        if (!m.summary.isEmpty()) { c.addBulk("summary"); c.addBulk(m.summary); }
        if (!m.since.isEmpty()) { c.addBulk("since"); c.addBulk(m.since); }
        c.addBulk("group");
        c.addBulk(m.group);
        if (!m.complexity.isEmpty()) { c.addBulk("complexity"); c.addBulk(m.complexity); }
        if (m.docFlags.length > 0) {
            c.addBulk("doc_flags");
            c.addSet(m.docFlags.length);
            for (String f : m.docFlags) c.addSimple(f);
        }
        if (m.deprecatedSince != null) { c.addBulk("deprecated_since"); c.addBulk(m.deprecatedSince); }
        if (m.replacedBy != null) { c.addBulk("replaced_by"); c.addBulk(m.replacedBy); }
        if (m.history.length > 0) {
            c.addBulk("history");
            c.addSet(m.history.length);
            for (String[] h : m.history) {
                c.addArray(2);
                c.addBulk(h[0]);
                c.addBulk(h[1]);
            }
        }
        if (m.args.length > 0) { c.addBulk("arguments"); addArgList(c, m.args); }
        if (subs != null) addDocsSubcommands(c, subs);
    }

    private static void addDocsSubcommands(Client c, Map<String, Cmd> subs) {
        c.addBulk("subcommands");
        List<Cmd> list = new ArrayList<>(subs.values());
        c.addMap(list.size());
        for (Cmd sub : list) {
            c.addBulk(sub.fullName);
            addCommandDocs(c, sub);
        }
    }

    /** server.c addReplyCommandArgList() */
    private static void addArgList(Client c, CommandMeta.Arg[] args) {
        c.addArray(args.length);
        for (CommandMeta.Arg a : args) {
            boolean block = a.type.equals("oneof") || a.type.equals("block");
            int n = 2;
            if (!block) n++;                            // display_text
            if (a.keySpecIndex != -1) n++;
            if (a.token != null) n++;
            if (a.summary != null) n++;
            if (a.since != null) n++;
            if (a.deprecatedSince != null) n++;
            if (a.flags.length > 0) n++;
            if (block) n++;                             // nested arguments
            c.addMap(n);
            c.addBulk("name"); c.addBulk(a.name);
            c.addBulk("type"); c.addBulk(a.type);
            if (!block) {
                c.addBulk("display_text");
                c.addBulk(a.display == null ? a.name : a.display);
            }
            if (a.keySpecIndex != -1) { c.addBulk("key_spec_index"); c.addInt(a.keySpecIndex); }
            if (a.token != null) { c.addBulk("token"); c.addBulk(a.token); }
            if (a.summary != null) { c.addBulk("summary"); c.addBulk(a.summary); }
            if (a.since != null) { c.addBulk("since"); c.addBulk(a.since); }
            if (a.deprecatedSince != null) { c.addBulk("deprecated_since"); c.addBulk(a.deprecatedSince); }
            if (a.flags.length > 0) {
                c.addBulk("flags");
                c.addSet(a.flags.length);
                for (String f : a.flags) c.addSimple(f);
            }
            if (block) { c.addBulk("arguments"); addArgList(c, a.sub); }
        }
    }

    private static void commandInfo(Client c) {
        if (c.argc == 2) {
            List<Cmd> cmds = visibleCommands();
            c.addArray(cmds.size());
            for (Cmd cmd : cmds) addCommandInfo(c, cmd);
            return;
        }
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) addCommandInfo(c, lookupByName(c.argLower(i)));
    }

    private static void commandDocs(Client c) {
        if (c.argc == 2) {
            List<Cmd> cmds = visibleCommands();
            c.addMap(cmds.size());
            for (Cmd cmd : cmds) {
                c.addBulk(cmd.fullName);
                addCommandDocs(c, cmd);
            }
            return;
        }
        List<Cmd> found = new ArrayList<>();
        for (int i = 2; i < c.argc; i++) {
            Cmd cmd = lookupByName(c.argLower(i));
            if (cmd != null) found.add(cmd);
        }
        c.addMap(found.size());
        for (Cmd cmd : found) {
            c.addBulk(cmd.fullName);
            addCommandDocs(c, cmd);
        }
    }

    /** server.c commandListCommand() */
    private static void commandList(Client c) {
        String pattern = null, aclcat = null;
        boolean matchNothing = false;
        for (int i = 2; i < c.argc; i++) {
            int moreargs = (c.argc - 1) - i;
            if (c.argIs(i, "filterby") && moreargs == 2) {
                String type = c.argLower(i + 1);
                if (type.equals("pattern")) pattern = c.argStr(i + 2);
                else if (type.equals("aclcat")) aclcat = c.argLower(i + 2);
                else if (type.equals("module")) matchNothing = true;   // no modules are loaded
                else throw RedisError.syntax();
                i += 2;
            } else {
                throw RedisError.syntax();
            }
        }
        List<String> names = new ArrayList<>();
        if (!matchNothing) {
            for (Cmd cmd : visibleCommands()) collectNames(cmd, pattern, aclcat, names);
        }
        c.addArray(names.size());
        for (String n : names) c.addBulk(n);
    }

    private static void collectNames(Cmd cmd, String pattern, String aclcat, List<String> out) {
        if (listed(cmd, pattern, aclcat)) out.add(cmd.fullName);
        Map<String, Cmd> subs = topLevelSubs(cmd);
        if (subs == null) return;
        for (Cmd sub : new ArrayList<>(subs.values()))
            if (listed(sub, pattern, aclcat)) out.add(sub.fullName);
    }

    /** server.c shouldFilterFromCommandList(): an unknown ACL category matches nothing. */
    private static boolean listed(Cmd cmd, String pattern, String aclcat) {
        if (pattern != null)
            return Util.stringmatch(Util.b(pattern), Util.b(cmd.fullName), true);
        if (aclcat != null) {
            CommandMeta.Entry m = CommandMeta.get(cmd.fullName);
            if (m == null) return false;
            for (String cat : m.categories) if (cat.substring(1).equals(aclcat)) return true;
            return false;
        }
        return true;
    }

    private static Cmd lookupByName(String name) {
        Cmd cmd = CommandTable.TABLE.get(name);
        if (cmd != null) return cmd;
        int bar = name.indexOf('|');
        if (bar > 0) {
            Map<String, Cmd> subs = CommandTable.SUBS.get(name.substring(0, bar));
            if (subs != null) return subs.get(name.substring(bar + 1));
        }
        return null;
    }

    private static void commandGetKeys(Client c) { getKeysImpl(c, false); }

    private static void commandGetKeysAndFlags(Client c) { getKeysImpl(c, true); }

    /** server.c getKeysSubcommandImpl() */
    private static void getKeysImpl(Client c, boolean withFlags) {
        int argc = c.argc - 2;
        byte[][] argv = new byte[argc][];
        for (int i = 0; i < argc; i++) argv[i] = c.arg(i + 2);
        Cmd cmd = lookupCommandFully(argv, argc);
        if (cmd == null) throw new RedisError("ERR Invalid command specified");
        CommandMeta.Entry m = CommandMeta.get(cmd.fullName);
        if (m == null) { getKeysFromRegistration(c, cmd, argv, argc, withFlags); return; }
        if (!m.hasKeys()) throw new RedisError("ERR The command has no key arguments");
        if ((m.arity > 0 && m.arity != argc) || (argc < -m.arity))
            throw new RedisError("ERR Invalid number of arguments specified for command");
        List<int[]> keys = getKeysWithFlags(m, argv, argc);
        if (keys.isEmpty()) {
            if (hasFlag(m, "no_mandatory_keys")) { c.addEmptyArray(); return; }
            throw new RedisError("ERR Invalid arguments specified for command");
        }
        c.addArray(keys.size());
        for (int[] k : keys) {
            if (!withFlags) {
                c.addBulk(argv[k[0]]);
            } else {
                c.addArray(2);
                c.addBulk(argv[k[0]]);
                List<String> names = CommandMeta.keyFlagNames(k[1]);
                c.addSet(names.size());
                for (String n : names) c.addSimple(n);
            }
        }
    }

    /**
     * The commands this server adds on top of redis's table are registered with
     * a plain first/last/step range and have no key spec to report flags from,
     * so they keep answering the way they did before the metadata table existed.
     */
    private static void getKeysFromRegistration(Client c, Cmd cmd, byte[][] argv, int argc,
                                                boolean withFlags) {
        if (cmd.firstKey == 0) throw new RedisError("ERR The command has no key arguments");
        if ((cmd.arity > 0 && cmd.arity != argc) || (argc < -cmd.arity))
            throw new RedisError("ERR Invalid number of arguments specified for command");
        List<byte[]> keys = new ArrayList<>();
        int last = cmd.lastKey < 0 ? argc + cmd.lastKey : cmd.lastKey;
        int step = cmd.keyStep <= 0 ? 1 : cmd.keyStep;
        for (int i = cmd.firstKey; i <= last && i < argc; i += step) keys.add(argv[i]);
        if (keys.isEmpty()) throw new RedisError("ERR Invalid arguments specified for command");
        c.addArray(keys.size());
        for (byte[] k : keys) {
            if (!withFlags) {
                c.addBulk(k);
            } else {
                c.addArray(2);
                c.addBulk(k);
                c.addSet(2);
                c.addSimple(cmd.isWrite() ? "RW" : "RO");
                c.addSimple(cmd.isWrite() ? "update" : "access");
            }
        }
    }

    private static boolean hasFlag(CommandMeta.Entry m, String flag) {
        for (String f : m.flags) if (f.equals(flag)) return true;
        return false;
    }

    /** server.c lookupCommand(): resolves "<container> <sub>" as well. */
    private static Cmd lookupCommandFully(byte[][] argv, int argc) {
        Cmd base = CommandTable.TABLE.get(Util.lower(argv[0]));
        if (base == null) return null;
        Map<String, Cmd> subs = CommandTable.SUBS.get(base.name);
        if (argc == 1 || subs == null || subs.isEmpty()) return base;
        return subs.get(Util.lower(argv[1]));
    }

    /**
     * db.c getKeysFromCommandWithSpecs(): prefer the key specs, whose flags are
     * reliable, and fall back to the hand written extractors otherwise. Each
     * entry of the result is {argv position, key flags}.
     */
    private static List<int[]> getKeysWithFlags(CommandMeta.Entry m, byte[][] argv, int argc) {
        boolean hasKeySpec = false, hasVarFlags = false;
        for (CommandMeta.Spec sp : m.specs) {
            if ((sp.flags & CommandMeta.K_NOT_KEY) == 0) hasKeySpec = true;
            if ((sp.flags & CommandMeta.K_VARIABLE_FLAGS) != 0) hasVarFlags = true;
        }
        if (hasKeySpec && !hasVarFlags) {
            List<int[]> out = keysUsingSpecs(m, argv, argc);
            if (out != null) return out;
        }
        return keysUsingProc(m, argv, argc);
    }

    /** db.c getKeysUsingKeySpecs(); null when a spec could not be resolved. */
    private static List<int[]> keysUsingSpecs(CommandMeta.Entry m, byte[][] argv, int argc) {
        List<int[]> out = new ArrayList<>();
        for (CommandMeta.Spec spec : m.specs) {
            if ((spec.flags & CommandMeta.K_NOT_KEY) != 0) continue;
            long first = 0, last, step;
            if (spec.bsType == CommandMeta.BS_INDEX) {
                first = spec.bsIndex;
            } else if (spec.bsType == CommandMeta.BS_KEYWORD) {
                int start = spec.bsStartFrom > 0 ? spec.bsStartFrom : argc + spec.bsStartFrom;
                int end = spec.bsStartFrom > 0 ? argc - 1 : 1;
                for (int i = start; i != end; i = start <= end ? i + 1 : i - 1) {
                    if (i >= argc || i < 1) break;
                    if (Util.equalsIgnoreCase(argv[i], spec.bsKeyword)) { first = i + 1; break; }
                }
                if (first == 0) continue;               // keyword not found
            } else {
                return null;
            }
            if (spec.fkType == CommandMeta.FK_RANGE) {
                step = spec.keystep;
                if (spec.lastkey >= 0) last = first + spec.lastkey;
                else if (spec.limit == 0) last = argc + spec.lastkey;
                else last = first + ((argc - first) / spec.limit + spec.lastkey);
            } else if (spec.fkType == CommandMeta.FK_KEYNUM) {
                step = spec.keystep;
                if (spec.keynumidx >= argc || first + spec.keynumidx >= argc) return null;
                Long n = Util.string2ll(argv[(int) (first + spec.keynumidx)]);
                if (n == null || n < 0) return null;
                first += spec.firstkey;
                last = first + n - 1;
            } else {
                return null;
            }
            if (last >= argc || last < first || first >= argc) return null;
            for (long i = first; i <= last; i += step) {
                /* A negative arity means the dispatcher did not check the
                 * argument count, so a short command lands here instead of
                 * being rejected; report no keys and let the command itself
                 * raise the arity or syntax error. */
                if (i >= argc || i < first) { if (m.arity < 0) continue; else break; }
                out.add(new int[]{(int) i, spec.flags});
            }
            if ((spec.flags & CommandMeta.K_INCOMPLETE) != 0) return null;
        }
        return out;
    }

    /** db.c genericGetKeys(): the commands using it have a fully defined key spec. */
    private static void genericGetKeys(int storeKeyOfs, int keyCountOfs, int firstKeyOfs, int keyStep,
                                       byte[][] argv, int argc, List<int[]> out) {
        Long n = Util.string2ll(argv[keyCountOfs]);
        int num = n == null ? 0 : (int) Math.max(Math.min(n, Integer.MAX_VALUE), Integer.MIN_VALUE);
        if (num < 1 || num > (argc - firstKeyOfs) / keyStep) return;
        for (int i = 0; i < num; i++) out.add(new int[]{firstKeyOfs + i * keyStep, 0});
        if (storeKeyOfs != 0) out.add(new int[]{storeKeyOfs, 0});
    }

    /** db.c: the per command getkeys_proc callbacks. */
    private static List<int[]> keysUsingProc(CommandMeta.Entry m, byte[][] argv, int argc) {
        List<int[]> out = new ArrayList<>();
        String proc = m.getKeysProc;
        if (proc == null) return out;
        int ro = CommandMeta.K_RO, rw = CommandMeta.K_RW, ow = CommandMeta.K_OW;
        int access = CommandMeta.K_ACCESS, update = CommandMeta.K_UPDATE, delete = CommandMeta.K_DELETE;
        switch (proc) {
            case "setGetKeys": {
                int flags = ow | update;
                for (int i = 3; i < argc; i++)
                    if (argv[i].length == 3 && Util.equalsIgnoreCase(argv[i], "get")) {
                        flags = rw | access | update;
                        break;
                    }
                out.add(new int[]{1, flags});
                break;
            }
            case "bitfieldGetKeys": {
                boolean readonly = true;
                for (int i = 2; i < argc; i++) {
                    int rem = argc - i - 1;
                    if (Util.equalsIgnoreCase(argv[i], "get") && rem >= 2) i += 2;
                    else if ((Util.equalsIgnoreCase(argv[i], "set")
                            || Util.equalsIgnoreCase(argv[i], "incrby")) && rem >= 3) {
                        readonly = false; break;
                    } else if (Util.equalsIgnoreCase(argv[i], "overflow") && rem >= 1) i += 1;
                    else { readonly = false; break; }
                }
                out.add(new int[]{1, readonly ? (ro | access) : (rw | access | update)});
                break;
            }
            case "sortROGetKeys":
                out.add(new int[]{1, ro | access});
                break;
            case "sortGetKeys": {
                out.add(new int[]{1, ro | access});
                int storePos = -1;
                String[] skipName = {"limit", "get", "by"};
                int[] skipArgs = {2, 1, 1};
                for (int i = 2; i < argc; i++) {
                    for (int j = 0; j < skipName.length; j++) {
                        if (Util.equalsIgnoreCase(argv[i], skipName[j])) { i += skipArgs[j]; break; }
                        else if (Util.equalsIgnoreCase(argv[i], "store") && i + 1 < argc) {
                            storePos = i + 1; break;    // the *last* STORE wins, like SORT itself
                        }
                    }
                }
                if (storePos >= 0) out.add(new int[]{storePos, ow | update});
                break;
            }
            case "migrateGetKeys": {
                int first = 3, num = 1;
                String[] skipName = {"copy", "replace", "auth", "auth2"};
                int[] skipArgs = {0, 0, 1, 2};
                if (argc > 6) {
                    for (int i = 6; i < argc; i++) {
                        if (Util.equalsIgnoreCase(argv[i], "keys")) {
                            if (argv[3].length > 0) num = 0;    // syntax error, MIGRATE reports it
                            else { first = i + 1; num = argc - first; }
                            break;
                        }
                        for (int j = 0; j < skipName.length; j++)
                            if (Util.equalsIgnoreCase(argv[i], skipName[j])) { i += skipArgs[j]; break; }
                    }
                }
                for (int i = 0; i < num; i++) out.add(new int[]{first + i, rw | access | delete});
                break;
            }
            case "georadiusGetKeys": {
                int stored = -1;
                for (int i = 5; i < argc; i++) {
                    if ((Util.equalsIgnoreCase(argv[i], "store")
                            || Util.equalsIgnoreCase(argv[i], "storedist")) && i + 1 < argc) {
                        stored = i + 1; i++;
                    }
                }
                out.add(new int[]{1, 0});
                if (stored != -1) out.add(new int[]{stored, 0});
                break;
            }
            case "xreadGetKeys": {
                int streams = -1;
                for (int i = 1; i < argc; i++) {
                    if (Util.equalsIgnoreCase(argv[i], "block")) i++;
                    else if (Util.equalsIgnoreCase(argv[i], "count")) i++;
                    else if (Util.equalsIgnoreCase(argv[i], "group")) i += 2;
                    else if (Util.equalsIgnoreCase(argv[i], "noack")) { /* no argument */ }
                    else if (Util.equalsIgnoreCase(argv[i], "streams")) { streams = i; break; }
                    else break;                         // syntax error
                }
                int num = streams == -1 ? 0 : argc - streams - 1;
                if (streams == -1 || num == 0 || num % 2 != 0) break;
                num /= 2;                               // half the arguments are IDs
                for (int i = streams + 1; i < argc - num; i++) out.add(new int[]{i, 0});
                break;
            }
            case "evalGetKeys": case "functionGetKeys": case "blmpopGetKeys": case "bzmpopGetKeys":
                genericGetKeys(0, 2, 3, 1, argv, argc, out); break;
            case "zunionInterDiffGetKeys": case "sintercardGetKeys":
            case "lmpopGetKeys": case "zmpopGetKeys":
                genericGetKeys(0, 1, 2, 1, argv, argc, out); break;
            case "zunionInterDiffStoreGetKeys":
                genericGetKeys(1, 2, 3, 1, argv, argc, out); break;
            default:
                break;
        }
        return out;
    }

    // ------------------------------------------------------------------
    // DEBUG
    // ------------------------------------------------------------------
    private static void debugSetActiveExpire(Client c) {
        Long v = Util.string2ll(c.arg(2));
        activeExpireEnabled = (v == null || v != 0);
        c.addOk();
    }

    private static void debugSleep(Client c) {
        Double d = Util.string2d(c.arg(2));
        double secs = d == null ? 0 : d;
        if (secs > 0) {
            long deadline = System.nanoTime() + (long) (secs * 1e9);
            while (System.nanoTime() - deadline < 0) {
                // busy wait: DEBUG SLEEP has to block the whole server, exactly like redis does
            }
        }
        c.addOk();
    }

    private static void debugStringmatchLen(Client c) {
        c.addInt(Util.stringmatch(c.arg(2), c.arg(3), false) ? 1 : 0);
    }

    private static void debugQuicklistPackedThreshold(Client c) {
        String v = c.argStr(2);
        if (!looksLikeMemory(v))
            throw new RedisError("ERR argument must be a memory value bigger than 1 and smaller than 4gb");
        long sz = Config.parseMemory(v);
        if (sz == 0) {
            sz = 1L << 30;
        } else if (sz < 1 || sz > (1L << 32)) {
            throw new RedisError("ERR argument must be a memory value bigger than 1 and smaller than 4gb");
        }
        quicklistPackedThreshold = sz;
        c.addOk();
    }

    private static void debugChangeReplId(Client c) {
        Server s = Server.I;
        s.logNotice("Changing replication IDs after receiving DEBUG change-repl-id");
        if (s.repl != null) {
            s.repl.changeReplicationId();
            s.repl.clearReplicationId2();
        } else {
            s.replId = Server.randomHex(40);
            s.replId2 = "0000000000000000000000000000000000000000";
            s.secondReplOffset = -1;
        }
        c.addOk();
    }

    private static void debugObject(Client c) {
        Server s = Server.I;
        Db d = s.dbs[c.db];
        Key k = new Key(c.arg(2));
        s.expireIfNeeded(d, k);
        RObj o = d.dict.get(k);
        if (o == null) throw RedisError.noSuchKey();
        StringBuilder sb = new StringBuilder();
        sb.append("Value at:0 refcount:1 encoding:").append(o.encodingName())
          .append(" serializedlength:").append(serializedLength(o))
          .append(" lru:0 lru_seconds_idle:0");
        if (o.encoding == RObj.ENC_QUICKLIST && o.ptr instanceof RList) {
            RList l = (RList) o.ptr;
            long bytes = 0;
            for (byte[] e : l.l) bytes += e.length + 11;
            sb.append(" ql_nodes:1 ql_avg_node:").append(fmt2(l.size()))
              .append(" ql_listpack_max:").append(s.config.getLong("list-max-listpack-size"))
              .append(" ql_compressed:0 ql_uncompressed_size:").append(bytes);
        }
        c.addSimple(sb.toString());
    }

    private static void debugReload(Client c) {
        Server s = Server.I;
        boolean flush = true, doSave = true;
        for (int j = 2; j < c.argc; j++) {
            if (c.argIs(j, "merge")) { /* accepted: conflicting keys are taken from the RDB */ }
            else if (c.argIs(j, "noflush")) flush = false;
            else if (c.argIs(j, "nosave")) doSave = false;
            else throw new RedisError("ERR DEBUG RELOAD only supports the MERGE, NOFLUSH and NOSAVE options.");
        }
        if (doSave) {
            try {
                Rdb.saveFile(s, s.rdbPath());
            } catch (Exception e) {
                s.logWarn("DEBUG RELOAD failed to save the RDB: " + e);
                throw new RedisError("ERR");
            }
        }
        if (flush) for (Db d : s.dbs) d.clear();
        try {
            Rdb.loadFile(s, s.rdbPath());
        } catch (Exception e) {
            s.logWarn("DEBUG RELOAD failed to load the RDB: " + e);
            throw new RedisError("ERR Error trying to load the RDB dump, check server logs.");
        }
        s.logNotice("DB reloaded by DEBUG RELOAD");
        c.addOk();
    }

    /** debug.c: server.script_disable_deny_script, lets a script call noscript commands. */
    private static void debugSetDisableDenyScripts(Client c) {
        Long v = Util.string2ll(c.arg(2));
        Server.I.scripting.disableDenyScript = v != null && v.longValue() != 0;
        c.addOk();
    }

    /**
     * debug.c DEBUG PROTOCOL [string|integer|double|bignum|null|array|set|map|
     *                         attrib|push|verbatim|true|false]
     */
    private static void debugProtocol(Client c) {
        String name = c.argLower(2);
        if (name.equals("string")) {
            c.addBulk("Hello World");
        } else if (name.equals("integer")) {
            c.addInt(12345);
        } else if (name.equals("double")) {
            c.addDouble(3.141);
        } else if (name.equals("bignum")) {
            c.addBigNumber("1234567999999999999999999999999999999");
        } else if (name.equals("null")) {
            c.addNil();
        } else if (name.equals("array")) {
            c.addArray(3);
            for (int j = 0; j < 3; j++) c.addInt(j);
        } else if (name.equals("set")) {
            c.addSet(3);
            for (int j = 0; j < 3; j++) c.addInt(j);
        } else if (name.equals("map")) {
            c.addMap(3);
            for (int j = 0; j < 3; j++) { c.addInt(j); c.addBool(j == 1); }
        } else if (name.equals("attrib")) {
            if (c.resp >= 3) {
                c.addRaw(Util.b("|1\r\n"));
                c.addBulk("key-popularity");
                c.addArray(2);
                c.addBulk("key:123");
                c.addInt(90);
            }
            /* Attributes are not real replies, so a well formed reply should
             * also have a normal reply type after the attribute. */
            c.addBulk("Some real reply following the attribute");
        } else if (name.equals("push")) {
            if (c.resp < 3) throw new RedisError("ERR RESP2 is not supported by this command");
            c.addPush(2);
            c.addBulk("server-cpu-usage");
            c.addInt(42);
            /* Push replies are not synchronous replies, so we emit also a
             * normal reply in order for blocking clients just discarding the
             * push reply, to actually consume the reply and continue. */
            c.addBulk("Some real reply following the push reply");
        } else if (name.equals("true")) {
            c.addBool(true);
        } else if (name.equals("false")) {
            c.addBool(false);
        } else if (name.equals("verbatim")) {
            c.addVerbatim("txt", Util.b("This is a verbatim\nstring"));
        } else {
            throw new RedisError("ERR Wrong protocol type name. Please use one of the following: "
                    + "string|integer|double|bignum|null|array|set|map|attrib|push|verbatim|true|false");
        }
    }

    /** debug.c DEBUG LOADAOF: flush the buffer, empty the dataset, replay the AOF. */
    private static void debugLoadAof(Client c) {
        Server s = Server.I;
        if (s.aof != null && s.aof.isEnabled()) s.aof.flush();
        for (Db d : s.dbs) d.clear();
        try {
            if (s.aof != null) s.aof.load();
        } catch (Exception e) {
            s.logWarn("DEBUG LOADAOF failed: " + e);
            throw new RedisError("ERR Error trying to load the AOF files, check server logs.");
        }
        if (s.aof != null) s.aof.updateCurrentSize();
        s.dirty = 0; /* Prevent AOF / replication */
        s.logNotice("Append Only File loaded by DEBUG LOADAOF");
        c.addOk();
    }

    /** debug.c DEBUG DIGEST: the SHA1 fingerprint of the whole dataset. */
    private static void debugDigest(Client c) {
        c.addSimple(Digest.hex(Digest.dataset(Server.I)));
    }

    /** debug.c DEBUG POPULATE <count> [prefix]. */
    private static void debugPopulate(Client c) {
        Server s = Server.I;
        if (c.argc != 3 && c.argc != 4) throw subcommandError(c, "debug");
        Long n = Util.string2ll(c.arg(2));
        if (n == null) throw RedisError.notInt();
        String prefix = c.argc == 4 ? c.argStr(3) : "key";
        Db d = s.dbs[c.db];
        for (long j = 0; j < n; j++) {
            Key k = new Key(Util.b(prefix + ":" + j));
            if (d.dict.containsKey(k)) continue;
            d.dict.put(k, RObj.string(Util.b("value:" + j)));
        }
        c.addOk();
    }

    /** debug.c DEBUG FLUSHALL: drop everything without touching the AOF. */
    private static void debugFlushAll(Client c) {
        Server s = Server.I;
        for (Db d : s.dbs) d.clear();
        c.addOk();
    }

    // ------------------------------------------------------------------
    // MEMORY
    // ------------------------------------------------------------------
    private static void memoryUsage(Client c) {
        Server s = Server.I;
        long samples = Mem.DEF_SAMPLES;
        for (int j = 3; j < c.argc; j++) {
            if (c.argIs(j, "samples") && j + 1 < c.argc) {
                Long v = Util.string2ll(c.arg(j + 1));
                if (v == null) throw RedisError.notInt();
                if (v < 0) throw RedisError.syntax();
                samples = v == 0 ? Long.MAX_VALUE : v;
                j++;
            } else {
                throw RedisError.syntax();
            }
        }
        Db d = s.dbs[c.db];
        Key k = new Key(c.arg(2));
        s.expireIfNeeded(d, k);
        RObj o = d.dict.get(k);
        if (o == null) { c.addNil(); return; }
        c.addInt(Mem.memoryUsage(k, o, samples));
    }

    private static void memoryDoctor(Client c) {
        long used = Server.I.usedMemory();
        String report;
        if (used < 5 * 1024 * 1024) {
            report = "Hi Sam, this instance is empty or is using very little memory, "
                   + "my issues detector can't be used in these conditions. "
                   + "Please, leave this server alone, I can't assist you.";
        } else {
            report = "Hi Sam, I can't find any memory issue in your instance. "
                   + "I can only account for what occurs on this base.";
        }
        c.addVerbatim("txt", Util.b(report));
    }

    private static void memoryStats(Client c) {
        Server s = Server.I;
        long used = s.usedMemory();
        if (used > peakMemory) peakMemory = used;
        if (Mem.peak > peakMemory) peakMemory = Mem.peak;
        List<Db> nonEmpty = new ArrayList<>();
        long totalKeys = 0;
        for (Db d : s.dbs) {
            if (d.dict.isEmpty()) continue;
            nonEmpty.add(d);
            totalKeys += d.dict.size();
        }
        long clientsMem = Mem.clientsMemory(s);
        long startup = Mem.STARTUP;
        long overhead = startup + Mem.keyspaceOverhead(s) + clientsMem;
        long dataset = Math.max(0, used - overhead);

        c.addMap(32 + nonEmpty.size());
        mapEntry(c, "peak.allocated", peakMemory);
        mapEntry(c, "total.allocated", used);
        mapEntry(c, "startup.allocated", startup);
        mapEntry(c, "replication.backlog", 0);
        mapEntry(c, "clients.slaves", 0);
        mapEntry(c, "clients.normal", 0);
        mapEntry(c, "cluster.links", 0);
        mapEntry(c, "aof.buffer", s.aof == null ? 0 : s.aof.buf.len);
        mapEntry(c, "lua.caches", 0);
        mapEntry(c, "functions.caches", 0);
        mapEntry(c, "script.VMs", 0);
        for (Db d : nonEmpty) {
            c.addBulk("db." + d.id);
            c.addMap(2);
            c.addBulk("overhead.hashtable.main");
            c.addInt(56 + (long) d.dict.size() * 32);
            c.addBulk("overhead.hashtable.expires");
            c.addInt(d.expires.isEmpty() ? 0 : 56 + (long) d.expires.size() * 32);
        }
        mapEntry(c, "overhead.db.hashtable.lut", totalKeys * 8);
        mapEntry(c, "overhead.db.hashtable.rehashing", 0);
        mapEntry(c, "overhead.total", overhead);
        mapEntry(c, "db.dict.rehashing.count", 0);
        mapEntry(c, "keys.count", totalKeys);
        mapEntry(c, "keys.bytes-per-key", totalKeys == 0 ? 0 : dataset / totalKeys);
        mapEntry(c, "dataset.bytes", dataset);
        c.addBulk("dataset.percentage");
        c.addDouble(used == 0 ? 100.0 : 100.0 * dataset / used);
        c.addBulk("peak.percentage");
        c.addDouble(peakMemory == 0 ? 100.0 : 100.0 * used / peakMemory);
        mapEntry(c, "allocator.allocated", used);
        mapEntry(c, "allocator.active", used + 1024 * 1024);
        mapEntry(c, "allocator.resident", used + 1024 * 1024);
        mapEntry(c, "allocator.muzzy", 0);
        c.addBulk("allocator-fragmentation.ratio");
        c.addDouble(1.0);
        mapEntry(c, "allocator-fragmentation.bytes", 0);
        c.addBulk("allocator-rss.ratio");
        c.addDouble(1.0);
        mapEntry(c, "allocator-rss.bytes", 0);
        c.addBulk("rss-overhead.ratio");
        c.addDouble(1.0);
        mapEntry(c, "rss-overhead.bytes", 0);
        c.addBulk("fragmentation");
        c.addDouble(used == 0 ? 1.0 : (double) (used + 1024 * 1024) / used);
        mapEntry(c, "fragmentation.bytes", 1024 * 1024);
    }

    private static void mapEntry(Client c, String k, long v) {
        c.addBulk(k);
        c.addInt(v);
    }

    // ---- size estimation shared by DEBUG OBJECT and MEMORY USAGE ----
    private static long lenPrefix(long n) {
        if (n < 64) return 1;
        if (n < 16384) return 2;
        if (n <= 0xFFFFFFFFL) return 5;
        return 9;
    }

    private static long stringLen(byte[] b) {
        Long v = Util.string2ll(b);
        if (v != null && b.length <= 11) {
            if (v >= -128 && v <= 127) return 2;
            if (v >= -32768 && v <= 32767) return 3;
            if (v >= Integer.MIN_VALUE && v <= Integer.MAX_VALUE) return 5;
        }
        return lenPrefix(b.length) + b.length;
    }

    /** A deterministic stand-in for rdbSavedObjectLen(). */
    static long serializedLength(RObj o) {
        switch (o.type) {
            case RObj.STRING:
                return stringLen(o.str());
            case RObj.LIST: {
                RList l = (RList) o.ptr;
                long n = lenPrefix(l.size());
                for (byte[] e : l.l) n += stringLen(e);
                return n;
            }
            case RObj.SET: {
                RSet st = (RSet) o.ptr;
                long n = lenPrefix(st.size());
                for (Key m : st.set) n += stringLen(m.b);
                return n;
            }
            case RObj.ZSET: {
                RZSet z = (RZSet) o.ptr;
                long n = lenPrefix(z.size());
                for (Key m : z.dict.keySet()) n += stringLen(m.b) + 9;
                return n;
            }
            case RObj.HASH: {
                RHash h = (RHash) o.ptr;
                long n = lenPrefix(h.size());
                for (Map.Entry<Key, byte[]> e : h.map.entrySet())
                    n += stringLen(e.getKey().b) + stringLen(e.getValue());
                return n;
            }
            default:
                return 1;
        }
    }

    private static long sdsAllocSize(int len) {
        if (len < 256) return len + 4;
        if (len < 65536) return len + 6;
        return len + 10;
    }

    /** Rough but deterministic in-memory footprint of a value, mirroring objectComputeSize(). */
    private static long objectMemUsage(RObj o) {
        switch (o.type) {
            case RObj.STRING:
                if (o.encoding == RObj.ENC_INT) return 16;
                return 16 + sdsAllocSize(o.strlen());
            case RObj.LIST: {
                RList l = (RList) o.ptr;
                long n = 16 + 32;
                for (byte[] e : l.l) n += sdsAllocSize(e.length) + 24;
                return n;
            }
            case RObj.SET: {
                RSet st = (RSet) o.ptr;
                long n = 16 + 48;
                for (Key m : st.set) n += sdsAllocSize(m.b.length) + 48;
                return n;
            }
            case RObj.ZSET: {
                RZSet z = (RZSet) o.ptr;
                long n = 16 + 64;
                for (Key m : z.dict.keySet()) n += sdsAllocSize(m.b.length) + 80;
                return n;
            }
            case RObj.HASH: {
                RHash h = (RHash) o.ptr;
                long n = 16 + 48;
                for (Map.Entry<Key, byte[]> e : h.map.entrySet())
                    n += sdsAllocSize(e.getKey().b.length) + sdsAllocSize(e.getValue().length) + 48;
                return n;
            }
            default:
                return 16;
        }
    }

    // ------------------------------------------------------------------
    // ACL (minimal but well formed: a single "default" user by default)
    // ------------------------------------------------------------------
    private static void aclList(Client c) {
        c.addArray(aclUsers.size());
        for (Map.Entry<String, String> e : aclUsers.entrySet())
            c.addBulk("user " + e.getKey() + " " + e.getValue());
    }

    private static void aclUsersCmd(Client c) {
        c.addArray(aclUsers.size());
        for (String n : aclUsers.keySet()) c.addBulk(n);
    }

    private static void aclCat(Client c) {
        if (c.argc == 2) {
            c.addArray(ACL_CATEGORIES.length);
            for (String cat : ACL_CATEGORIES) c.addBulk(cat);
            return;
        }
        if (c.argc != 3) throw subcommandError(c, "acl");
        String want = c.argLower(2);
        boolean known = false;
        for (String cat : ACL_CATEGORIES) if (cat.equals(want)) { known = true; break; }
        if (!known) {
            String given = c.argStr(2);
            if (given.length() > 128) given = given.substring(0, 128);
            throw new RedisError("ERR Unknown category '" + given + "'");
        }
        List<String> names = new ArrayList<>();
        for (Cmd cmd : visibleCommands()) if (cmd.has(want)) names.add(cmd.fullName);
        c.addArray(names.size());
        for (String n : names) c.addBulk(n);
    }

    private static void aclGetUser(Client c) {
        String rules = aclUsers.get(c.argStr(2));
        if (rules == null) { c.addNil(); return; }
        List<String> flags = new ArrayList<>();
        String commands = "-@all";
        StringBuilder keyPats = new StringBuilder(), chanPats = new StringBuilder();
        boolean on = false;
        for (String tok : rules.trim().split("\\s+")) {
            if (tok.isEmpty()) continue;
            if (tok.equals("on")) on = true;
            else if (tok.equals("off")) on = false;
            else if (tok.equals("nopass")) flags.add("nopass");
            else if (tok.equals("sanitize-payload")) flags.add("sanitize-payload");
            else if (tok.equals("skip-sanitize-payload")) flags.add("skip-sanitize-payload");
            else if (tok.startsWith("~") || tok.startsWith("%")) {
                if (keyPats.length() > 0) keyPats.append(' ');
                keyPats.append(tok);
            } else if (tok.startsWith("&")) {
                if (chanPats.length() > 0) chanPats.append(' ');
                chanPats.append(tok);
            } else if (tok.startsWith("+") || tok.startsWith("-")) {
                if (tok.equals("+@all")) commands = "+@all";
                else if (tok.equals("-@all")) commands = "-@all";
                else commands = commands + " " + tok;
            }
        }
        flags.add(0, on ? "on" : "off");

        c.addMap(6);
        c.addBulk("flags");
        c.addSet(flags.size());
        for (String f : flags) c.addBulk(f);
        c.addBulk("passwords");
        c.addEmptyArray();
        c.addBulk("commands");
        c.addBulk(commands);
        c.addBulk("keys");
        c.addBulk(keyPats.toString());
        c.addBulk("channels");
        c.addBulk(chanPats.toString());
        c.addBulk("selectors");
        c.addEmptyArray();
    }

    private static void aclSetUser(Client c) {
        String name = c.argStr(2);
        if (name.indexOf(' ') >= 0 || name.indexOf((char) 0) >= 0)
            throw new RedisError("ERR Usernames can't contain spaces or null characters");
        List<String> toks = new ArrayList<>();
        String existing = aclUsers.get(name);
        if (existing != null && !existing.trim().isEmpty())
            toks.addAll(Arrays.asList(existing.trim().split("\\s+")));
        else toks.add("off");
        for (int j = 3; j < c.argc; j++) {
            String tok = c.argStr(j);
            String low = tok.toLowerCase(Locale.ROOT);
            if (low.equals("reset")) { toks.clear(); toks.add("off"); continue; }
            if (low.equals("on") || low.equals("off")) {
                toks.remove("on");
                toks.remove("off");
                toks.add(0, low);
                continue;
            }
            if (low.equals("allkeys")) tok = "~*";
            else if (low.equals("allchannels")) tok = "&*";
            else if (low.equals("allcommands")) tok = "+@all";
            else if (low.equals("nocommands")) tok = "-@all";
            else if (low.startsWith(">") || low.startsWith("<") || low.startsWith("#")
                    || low.startsWith("!")) continue;   // passwords are not stored
            if (!toks.contains(tok)) toks.add(tok);
        }
        aclUsers.put(name, String.join(" ", toks));
        c.addOk();
    }

    private static void aclDelUser(Client c) {
        for (int j = 2; j < c.argc; j++) {
            if (c.argStr(j).equals("default"))
                throw new RedisError("ERR The 'default' user cannot be removed");
        }
        int deleted = 0;
        for (int j = 2; j < c.argc; j++) if (aclUsers.remove(c.argStr(j)) != null) deleted++;
        c.addInt(deleted);
    }

    private static void aclLog(Client c) {
        if (c.argc == 3) {
            if (c.argIs(2, "reset")) { c.addOk(); return; }
            if (Util.string2ll(c.arg(2)) == null) throw RedisError.notInt();
        } else if (c.argc > 3) {
            throw subcommandError(c, "acl");
        }
        c.addEmptyArray();
    }

    private static void aclGenPass(Client c) {
        long bits = 256;
        if (c.argc == 3) {
            Long v = Util.string2ll(c.arg(2));
            if (v == null) throw RedisError.notInt();
            bits = v;
        } else if (c.argc > 3) {
            throw subcommandError(c, "acl");
        }
        if (bits <= 0 || bits > 4096)
            throw new RedisError("ERR ACL GENPASS argument must be the number of bits for the "
                    + "output password, a positive number up to 4096");
        c.addBulk(Server.randomHex((int) ((bits + 3) / 4)));
    }

    // ------------------------------------------------------------------
    // HELP replies (networking.c addReplyHelp)
    // ------------------------------------------------------------------
    private static void addHelp(Client c, String[] help) {
        String container = c.argStr(0).toUpperCase(Locale.ROOT);
        c.addArray(help.length + 3);
        c.addSimple(container + " <subcommand> [<arg> [value] [opt] ...]. Subcommands are:");
        for (String h : help) c.addSimple(h);
        c.addSimple("HELP");
        c.addSimple("    Print this help.");
    }

    private static final String[] CONFIG_HELP = {
        "GET <pattern>",
        "    Return parameters matching the glob-like <pattern> and their values.",
        "SET <directive> <value>",
        "    Set the configuration <directive> to <value>.",
        "RESETSTAT",
        "    Reset statistics reported by the INFO command.",
        "REWRITE",
        "    Rewrite the configuration file.",
    };

    private static final String[] COMMAND_HELP = {
        "(no subcommand)",
        "    Return details about all Redis commands.",
        "COUNT",
        "    Return the total number of commands in this Redis server.",
        "LIST",
        "    Return a list of all commands in this Redis server.",
        "INFO [<command-name> ...]",
        "    Return details about multiple Redis commands.",
        "    If no command names are given, documentation details for all",
        "    commands are returned.",
        "DOCS [<command-name> ...]",
        "    Return documentation details about multiple Redis commands.",
        "    If no command names are given, documentation details for all",
        "    commands are returned.",
        "GETKEYS <full-command>",
        "    Return the keys from a full Redis command.",
        "GETKEYSANDFLAGS <full-command>",
        "    Return the keys and the access flags from a full Redis command.",
    };

    private static final String[] DEBUG_HELP = {
        "CHANGE-REPL-ID",
        "    Change the replication IDs of the instance.",
        "    Dangerous: should be used only for testing the replication subsystem.",
        "JMAP",
        "    Trigger a heap bookkeeping pass.",
        "OBJECT <key>",
        "    Show low level info about `key` and associated value.",
        "QUICKLIST-PACKED-THRESHOLD <size>",
        "    Sets the threshold for elements to be inserted as plain vs packed nodes",
        "    Default value is 1GB, allows values up to 4GB. Setting to 0 restores to default.",
        "RELOAD [option ...]",
        "    Save the RDB on disk and reload it back to memory. Valid <option> values:",
        "    * MERGE: conflicting keys will be loaded from RDB.",
        "    * NOFLUSH: the existing database will not be removed before load, but",
        "      conflicting keys will generate an exception and kill the server.",
        "    * NOSAVE: the database will be loaded from an existing RDB file.",
        "SET-ACTIVE-EXPIRE <0|1>",
        "    Setting it to 0 disables expiring keys in background when they are not",
        "    accessed (otherwise the Redis behavior). Setting it to 1 reenables back the",
        "    default.",
        "SLEEP <seconds>",
        "    Stop the server for <seconds>. Decimals allowed.",
        "STRINGMATCH-LEN <pattern> <string>",
        "    Run the stringmatchlen() function against the two arguments.",
        "STRINGMATCH-TEST",
        "    Run a fuzz tester against the stringmatchlen() function.",
    };

    private static final String[] MEMORY_HELP = {
        "DOCTOR",
        "    Return memory problems reports.",
        "MALLOC-STATS",
        "    Return internal statistics report from the memory allocator.",
        "PURGE",
        "    Attempt to purge dirty pages for reclamation by the allocator.",
        "STATS",
        "    Return information about the memory usage of the server.",
        "USAGE <key> [SAMPLES <count>]",
        "    Return memory in bytes used by <key> and its value. Nested values are",
        "    sampled up to <count> times (default: 5, 0 means sample all).",
    };

    private static final String[] SLOWLOG_HELP = {
        "GET [<count>]",
        "    Return top <count> entries from the slowlog (default: 10, -1 mean all).",
        "    Entries are made of:",
        "    id, timestamp, time in microseconds, arguments array, client IP and port,",
        "    client name",
        "LEN",
        "    Return the length of the slowlog.",
        "RESET",
        "    Reset the slowlog.",
    };

    private static final String[] LATENCY_HELP = {
        "DOCTOR",
        "    Return a human readable latency analysis report.",
        "GRAPH <event>",
        "    Return an ASCII latency graph for the <event> class.",
        "HISTORY <event>",
        "    Return time-latency samples for the <event> class.",
        "LATEST",
        "    Return the latest latency samples for all events.",
        "RESET [<event> ...]",
        "    Reset latency data of one or more <event> classes.",
        "    (default: reset all data for all event classes)",
        "HISTOGRAM [COMMAND ...]",
        "    Return a cumulative distribution of latencies in the format of a histogram for the specified command names.",
        "    If no commands are specified then all histograms are replied.",
    };

    private static final String[] ACL_HELP = {
        "CAT [<category>]",
        "    List all commands that belong to <category>, or all command categories",
        "    when no category is specified.",
        "DELUSER <username> [<username> ...]",
        "    Delete a list of users.",
        "GETUSER <username>",
        "    Get the user's details.",
        "GENPASS [<bits>]",
        "    Generate a secure 256-bit user password. The optional `bits` argument can",
        "    be used to specify a different size.",
        "LIST",
        "    Show users details in config file format.",
        "LOG [<count> | RESET]",
        "    Show the ACL log entries.",
        "SETUSER <username> [attribs ...]",
        "    Create or modify a user with the specified attributes.",
        "USERS",
        "    List all the registered usernames.",
        "WHOAMI",
        "    Return the current connection username.",
    };
}
