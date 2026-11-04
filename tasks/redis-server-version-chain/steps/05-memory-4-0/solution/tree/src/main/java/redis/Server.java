package redis;

import java.io.*;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.nio.file.*;
import java.util.*;

/** Server globals, the event loop, command dispatch and the keyspace helpers. */
public final class Server {
    public static Server I;

    public static final String VERSION = "8.0.6";

    public Config config = new Config();
    public Db[] dbs;
    public final LinkedHashMap<Long, Client> clients = new LinkedHashMap<>();
    public final List<Client> replicas = new ArrayList<>();
    public final List<Client> monitors = new ArrayList<>();
    public long nextClientId = 1;
    public long dirty = 0;
    public long startTime = System.currentTimeMillis();
    public String runId = randomHex(40);
    public String replId = randomHex(40);
    public String replId2 = "0000000000000000000000000000000000000000";
    public long masterReplOffset = 0;
    public long secondReplOffset = -1;
    public volatile boolean shutdownRequested = false;
    public int port;
    public int tlsPort;
    public String unixsocketPath;
    public PrintStream logStream = System.out;
    public long lastSave = System.currentTimeMillis() / 1000;
    public boolean loading = false;
    public long statNumCommands = 0, statNumConnections = 0, statExpiredKeys = 0,
                 statKeyspaceHits = 0, statKeyspaceMisses = 0, statRejectedConn = 0,
                 statTotalNetInput = 0, statTotalNetOutput = 0;
    public final Map<String, long[]> commandStats = new HashMap<>();
    public final Map<String, Long> errorStats = new HashMap<>();

    public Selector selector;
    public ServerSocketChannel tcpServer;
    public ServerSocketChannel unixServer;
    public ServerSocketChannel tlsServer;
    public Path unixPath;

    public final PubSub pubsub = new PubSub();
    public Aof aof;
    public Repl repl;
    public Scripting scripting;
    /** redis.c checkForSentinelMode(): the server runs as Redis Sentinel. */
    public boolean sentinelMode = false;
    public Sentinel sentinel;
    /** cluster.c: the Redis Cluster state, null unless cluster-enabled. */
    public Cluster cluster;
    public final List<Client> clientsToClose = new ArrayList<>();

    /** cumulative command call depth; >0 means we are inside EXEC/Lua */
    public int execDepth = 0;

    public static String randomHex(int n) {
        Random r = new Random();
        StringBuilder sb = new StringBuilder(n);
        String h = "0123456789abcdef";
        for (int i = 0; i < n; i++) sb.append(h.charAt(r.nextInt(16)));
        return sb.toString();
    }

    public long protoMaxBulkLen() { return Math.max(1024 * 1024, config.getLong("proto-max-bulk-len")); }

    public Db db(int i) { return dbs[i]; }

    // ------------------------------------------------------------------
    // logging
    // ------------------------------------------------------------------
    public void log(char level, String msg) {
        long pid = ProcessHandle.current().pid();
        String role = sentinelMode ? "X"
                : ((repl != null && repl.masterHost != null) ? "S" : "M");
        java.text.SimpleDateFormat f = new java.text.SimpleDateFormat("dd MMM yyyy HH:mm:ss.SSS", Locale.US);
        logStream.println(pid + ":" + role + " " + f.format(new Date()) + " " + level + " " + msg);
        logStream.flush();
    }
    public void logNotice(String m) { log('*', m); }
    public void logWarn(String m) { log('#', m); }

    // ------------------------------------------------------------------
    // startup
    // ------------------------------------------------------------------
    public void start(String[] args) throws Exception {
        I = this;
        /* redis.c main(): sentinel mode is decided before the configuration is
         * parsed, because parsing it populates the sentinel data structures. */
        sentinelMode = Sentinel.checkForSentinelMode(args);
        if (sentinelMode) config.set("port", "26379");   // initSentinelConfig()
        List<String> rest = new ArrayList<>();
        String confFile = null;
        int i = 0;
        if (args.length > 0 && !args[0].startsWith("--")) { confFile = args[0]; i = 1; }
        for (; i < args.length; i++) rest.add(args[i]);
        if (confFile != null) config.loadFile(confFile);
        config.loadArgs(rest);

        String dir = config.getOr("dir", ".");
        if (dir != null && !dir.isEmpty() && !dir.equals(".")) {
            Path p = Paths.get(dir);
            if (!Files.isDirectory(p)) { System.err.println("Can't chdir to '" + dir + "'"); System.exit(1); }
            System.setProperty("user.dir", p.toAbsolutePath().toString());
            baseDir = p.toAbsolutePath();
        } else {
            baseDir = Paths.get(".").toAbsolutePath().normalize();
        }

        String lf = config.getOr("logfile", "");
        if (lf != null && !lf.isEmpty()) {
            logStream = new PrintStream(new FileOutputStream(resolve(lf).toFile(), true), true);
        }

        int dbnum = sentinelMode ? 1 : Math.max(1, config.getInt("databases"));
        dbs = new Db[dbnum];
        for (int k = 0; k < dbnum; k++) dbs[k] = new Db(k);

        port = config.getInt("port");
        tlsPort = config.getInt("tls-port");
        unixsocketPath = config.getOr("unixsocket", "");

        CommandTable.init();
        Acl.loadUsersAtStartup(this);
        aof = new Aof(this);
        repl = new Repl(this);
        scripting = new Scripting(this);
        if (sentinelMode) {
            SentinelCmds.initSentinelTable();
            sentinel = new Sentinel(this);
            sentinel.loadConfig(config.sentinelDirectives);
        }

        long pid = ProcessHandle.current().pid();
        String pf = config.getOr("pidfile", "");
        if (pf != null && !pf.isEmpty()) {
            try { Files.write(resolve(pf), (pid + "\n").getBytes()); } catch (IOException ignored) {}
        }

        logNotice("oO0OoO0OoO0Oo Redis is starting oO0OoO0OoO0Oo");
        logNotice("Redis version=" + VERSION + ", bits=64, commit=00000000, modified=0, pid=" + pid + ", just started");
        logNotice("Configuration loaded");

        selector = Selector.open();
        if (port != 0) {
            try {
                tcpServer = ServerSocketChannel.open();
                tcpServer.configureBlocking(false);
                tcpServer.socket().setReuseAddress(true);
                String bindAddr = firstBindAddr();
                tcpServer.bind(bindAddr == null ? new InetSocketAddress(port)
                                                : new InetSocketAddress(bindAddr, port), 511);
                tcpServer.register(selector, SelectionKey.OP_ACCEPT);
            } catch (IOException e) {
                logWarn("Warning: Could not create server TCP listening socket *:" + port + ": " + e.getMessage());
                logWarn("Failed listening on port " + port + " (tcp), aborting.");
                System.exit(1);
            }
        }
        if (tlsPort != 0) {
            if (!Tls.buildContext(this)) {
                logWarn("Failed to configure TLS. Check logs for more info. " + Tls.lastError);
                System.exit(1);
            }
            try {
                tlsServer = ServerSocketChannel.open();
                tlsServer.configureBlocking(false);
                tlsServer.socket().setReuseAddress(true);
                String bindAddr = firstBindAddr();
                tlsServer.bind(bindAddr == null ? new InetSocketAddress(tlsPort)
                                                : new InetSocketAddress(bindAddr, tlsPort), 511);
                tlsServer.register(selector, SelectionKey.OP_ACCEPT);
            } catch (IOException e) {
                logWarn("Warning: Could not create server TCP listening socket *:" + tlsPort
                        + ": " + e.getMessage());
                logWarn("Failed listening on port " + tlsPort + " (tls), aborting.");
                System.exit(1);
            }
        }
        if (unixsocketPath != null && !unixsocketPath.isEmpty()) {
            try {
                unixPath = Paths.get(unixsocketPath);
                Files.deleteIfExists(unixPath);
                unixServer = ServerSocketChannel.open(StandardProtocolFamily.UNIX);
                unixServer.configureBlocking(false);
                unixServer.bind(UnixDomainSocketAddress.of(unixPath), 511);
                unixServer.register(selector, SelectionKey.OP_ACCEPT);
            } catch (IOException e) {
                logWarn("Failed listening on unix socket " + unixsocketPath + ": " + e.getMessage());
                System.exit(1);
            }
        }

        /* server.c redisAsciiArt() runs after initServer(), so the banner (and
         * the "PID:" line the test harness looks for) only appears once the
         * listening sockets are really ours. */
        printAsciiArt(pid);

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            try { if (unixPath != null) Files.deleteIfExists(unixPath); } catch (IOException ignored) {}
        }));
        installSignalHandlers();

        /* server.c initServer(): clusterInit() runs once the event loop and
         * the listening sockets exist. */
        if (!sentinelMode && config.getBool("cluster-enabled")) {
            cluster = new Cluster(this);
            cluster.init();
        }

        logNotice("Server initialized");

        // load persisted data (server.c loadDataFromDisk()); not in sentinel mode
        if (sentinelMode) {
            // nothing to load: a Sentinel has no dataset
        } else {
            /* main(): the AOF manifest is read before the dataset, whatever the
             * AOF state is, so that a later "CONFIG SET appendonly yes" keeps
             * numbering its files where the previous run left off. */
            aof.loadManifestFromDisk();
            if (config.getBool("appendonly")) aof.loadOnStartup();
            else Rdb.loadOnStartup(this, rdbPath());
        }
        if (!sentinelMode) { aof.init(); aof.delHistoryFiles(); }
        if (cluster != null && !cluster.verifyConfigWithData()) {
            logWarn("You can't have keys in a DB different than DB 0 when in Cluster mode. Exiting.");
            logStream.flush();
            Runtime.getRuntime().halt(1);
        }

        String repl0 = sentinelMode ? "" : config.getOr("replicaof", "");
        if (repl0 != null && !repl0.trim().isEmpty()) {
            String[] hp = repl0.trim().split("\\s+");
            if (hp.length >= 2 && !hp[0].equalsIgnoreCase("no")) repl.setMaster(hp[0], Integer.parseInt(hp[1]));
        }

        // Redis's own "the server is up" log lines. The 2.8-era harness greps
        // the lowercase sentence, every harness from 6.0 counts the modern one.
        if (sentinelMode) sentinel.isRunning();
        logNotice("The server is now ready to accept connections on port " + (port != 0 ? port : tlsPort));
        logNotice("Ready to accept connections tcp");
        eventLoop();
    }

    public Path baseDir = Paths.get(".").toAbsolutePath().normalize();

    public Path resolve(String name) {
        Path p = Paths.get(name);
        return p.isAbsolute() ? p : baseDir.resolve(p);
    }

    public Path rdbPath() { return resolve(config.getOr("dbfilename", "dump.rdb")); }

    private String firstBindAddr() {
        String b = config.getOr("bind", "");
        if (b == null || b.trim().isEmpty()) return null;
        for (String tok : b.trim().split("\\s+")) {
            if (tok.startsWith("-")) continue;
            if (tok.equals("*")) return null;
            return tok;
        }
        return null;
    }

    private void printAsciiArt(long pid) {
        String mode = sentinelMode ? "sentinel"
                : (config.getBool("cluster-enabled") ? "cluster" : "standalone");
        int p = port != 0 ? port : tlsPort;
        String[] logo = {
            "                _._                                                  ",
            "           _.-``__ ''-._                                             ",
            "      _.-``    `.  `_.  ''-._           Redis Community Edition      ",
            "  .-`` .-```.  ```\\/    _.,_ ''-._     " + VERSION + " (00000000/0) 64 bit",
            " (    '      ,       .-`  | `,    )     Running in " + mode + " mode",
            " |`-._`-...-` __...-.``-._|'` _.-'|     Port: " + p,
            " |    `-._   `._    /     _.-'    |     PID: " + pid,
            "  `-._    `-._  `-./  _.-'    _.-'                                   ",
            " |`-._`-._    `-.__.-'    _.-'_.-'|                                  ",
            " |    `-._`-._        _.-'_.-'    |           https://redis.io       ",
            "  `-._    `-._`-.__.-'_.-'    _.-'                                   ",
            " |`-._`-._    `-.__.-'    _.-'_.-'|                                  ",
            " |    `-._`-._        _.-'_.-'    |                                  ",
            "  `-._    `-._`-.__.-'_.-'    _.-'                                   ",
            "      `-._    `-.__.-'    _.-'                                       ",
            "          `-._        _.-'                                           ",
            "              `-.__.-'                                               ",
        };
        for (String l : logo) logStream.println(l);
        logStream.flush();
    }

    private void installSignalHandlers() {
        try {
            Class<?> sig = Class.forName("sun.misc.Signal");
            Class<?> handler = Class.forName("sun.misc.SignalHandler");
            Object h = java.lang.reflect.Proxy.newProxyInstance(handler.getClassLoader(),
                    new Class<?>[]{handler}, (proxy, method, margs) -> {
                        if (method.getName().equals("handle")) {
                            shutdownRequested = true;
                            selector.wakeup();
                        }
                        return null;
                    });
            java.lang.reflect.Method h1 = sig.getMethod("handle", sig, handler);
            h1.invoke(null, sig.getConstructor(String.class).newInstance("TERM"), h);
            h1.invoke(null, sig.getConstructor(String.class).newInstance("INT"), h);
        } catch (Throwable ignored) {}
    }

    public void doShutdown(boolean save) {
        logWarn("Received SIGTERM scheduling shutdown...");
        logWarn("User requested shutdown...");
        try {
            if (aof != null) aof.flushAndClose();
            if (save && !sentinelMode && !config.getOr("save", "").trim().isEmpty()) {
                logNotice("Saving the final RDB snapshot before exiting.");
                Rdb.saveFile(this, rdbPath());
            }
        } catch (Exception e) {
            logWarn("Error trying to save the DB: " + e);
        }
        try { if (unixPath != null) Files.deleteIfExists(unixPath); } catch (IOException ignored) {}
        logWarn("Redis is now ready to exit, bye bye...");
        logStream.flush();
        Runtime.getRuntime().halt(0);
    }

    // ------------------------------------------------------------------
    // event loop
    // ------------------------------------------------------------------
    private long lastCron = 0;
    /** serverCron() period, 1000/server.hz. Sentinel keeps randomizing it. */
    public int cronPeriodMs = 100;
    private static final SelectionKey[] NO_KEYS = new SelectionKey[0];

    /** Stable dispatch order for one pass of the event loop: see processEvents(). */
    private static int compareReady(SelectionKey x, SelectionKey y) {
        Object ax = x.attachment(), ay = y.attachment();
        boolean cx = ax instanceof Client, cy = ay instanceof Client;
        if (cx != cy) return cx ? 1 : -1;
        if (!cx) return 0;
        return Long.compare(((Client) ax).id, ((Client) ay).id);
    }

    public void eventLoop() throws IOException {
        while (true) {
            if (shutdownRequested) doShutdown(true);
            processEvents(20, true);
        }
    }

    /**
     * ae.c aeProcessEvents(): one pass over the ready file events.  Called with
     * timeMs 0 and withCron false from networking.c processEventsWhileBlocked(),
     * i.e. from the Lua hook once a script overran the busy-reply threshold.
     */
    public void processEvents(long timeMs, boolean withCron) throws IOException {
        /* selectedKeys() is a hash set, so two clients that became readable in the
         * same pass would be served in an arbitrary order.  epoll hands redis its
         * descriptors in the order they became ready instead, and that order is
         * observable: two clients blocking on the same key in the same pass must
         * be woken in the order they blocked (t_list.c handleClientsBlockedOnKeys
         * serves the list in order), and a command sent on one connection before a
         * command on another must be executed first (a MONITOR has to see the very
         * next command).  The Consumer form of select() reports the keys straight
         * from the poll array, which is exactly that order.
         *
         * The list is also a snapshot: a script over the busy threshold reenters
         * this method from its Lua hook, and the nested select() would otherwise
         * mutate the collection we are iterating. */
        List<SelectionKey> readyList = new ArrayList<>(8);
        if (timeMs <= 0) selector.selectNow(readyList::add);
        else selector.select(readyList::add, timeMs);
        selector.selectedKeys().clear();
        SelectionKey[] ready = readyList.isEmpty() ? NO_KEYS
                : readyList.toArray(new SelectionKey[readyList.size()]);
        for (int ki = 0; ki < ready.length; ki++) {
            SelectionKey k = ready[ki];
            try {
                if (!k.isValid()) continue;
                if (k.isAcceptable()) {
                    if (k.attachment() instanceof Cluster) ((Cluster) k.attachment()).acceptBus(k);
                    else accept(k);
                }
                else {
                    Object att = k.attachment();
                    /* scripting.c: the caller of a timed out script is masked from
                     * the event loop, so that it cannot disconnect (or pipeline a
                     * second command) while its EVAL is still running. */
                    if (scripting != null && scripting.timedout && att == scripting.currCaller()) continue;
                    if (att instanceof Client) {
                        Client c = (Client) att;
                        if (k.isReadable()) readFromClient(c);
                        if (k.isValid() && k.isWritable()) {
                            c.write();
                            updateInterest(c);
                        }
                    } else if (att instanceof Repl.MasterLink) {
                        ((Repl.MasterLink) att).onEvent(k);
                    } else if (att instanceof Sentinel.Link) {
                        ((Sentinel.Link) att).onEvent(k);
                    } else if (att instanceof Cluster.Link) {
                        ((Cluster.Link) att).onEvent(k);
                    }
                }
            } catch (CancelledKeyException e) {
                // client went away
            } catch (IOException e) {
                Object att = k.attachment();
                if (att instanceof Client) freeClient((Client) att);
            }
        }
        if (withCron) {
            long now = System.currentTimeMillis();
            if (now - lastCron >= cronPeriodMs) { lastCron = now; cron(); }
        }
        if (aof != null) aof.beforeSleep();
        ConnCmds.evictClients(this);
        Tracking.beforeSleep(this);
        if (cluster != null) cluster.beforeSleep();
        flushAll();
        /* evict.c evictionTimeProc(): keep freeing memory outside the command
         * path as well, so that lowering maxmemory evicts straight away
         * instead of waiting for the next write.  This has to run *after* the
         * replies have been written out: the pending reply bytes are part of
         * used_memory, and evicting against them would make every reply free
         * keys, which is a feedback loop, not an eviction policy. */
        if (config.getLong("maxmemory") > 0) freeMemoryIfNeeded();
    }

    private void accept(SelectionKey k) throws IOException {
        ServerSocketChannel ss = (ServerSocketChannel) k.channel();
        SocketChannel ch;
        while ((ch = ss.accept()) != null) {
            ch.configureBlocking(false);
            try { ch.setOption(StandardSocketOptions.TCP_NODELAY, true); } catch (Exception ignored) {}
            /* acceptCommonHandler(): refuse the connection when we already serve
             * 'maxclients' clients. The error is best effort, then we hang up. */
            if (clients.size() >= config.getLong("maxclients")) {
                try {
                    ch.write(java.nio.ByteBuffer.wrap(
                            Util.b("-ERR max number of clients reached\r\n")));
                } catch (IOException ignored) {
                    // best effort
                }
                statRejectedConn++;
                try { ch.close(); } catch (IOException ignored) {}
                continue;
            }
            Client c = new Client();
            c.chan = ch;
            if (ss == tlsServer) {
                c.tls = Tls.newConn(this);
                if (c.tls == null) { try { ch.close(); } catch (IOException ignored) {} continue; }
            }
            c.id = nextClientId++;
            c.unixSocket = (ss == unixServer);
            if (c.unixSocket) {
                c.addr = unixsocketPath + ":0";
                c.laddr = unixsocketPath + ":0";
            } else {
                InetSocketAddress ra = (InetSocketAddress) ch.getRemoteAddress();
                InetSocketAddress la = (InetSocketAddress) ch.getLocalAddress();
                c.addr = ra.getAddress().getHostAddress() + ":" + ra.getPort();
                c.laddr = la.getAddress().getHostAddress() + ":" + la.getPort();
            }
            c.fd = (int) (c.id + 7);
            c.skey = ch.register(selector, SelectionKey.OP_READ, c);
            c.resp = config.getInt("client-default-resp") >= 3 ? 3 : 2;
            Acl.initClient(this, c);
            clients.put(c.id, c);
            statNumConnections++;
        }
    }

    private final ByteBuffer readBuf = ByteBuffer.allocate(64 * 1024);

    private void readFromClient(Client c) throws IOException {
        if (c.tls != null) { Tls.read(this, c); return; }
        readBuf.clear();
        int n;
        try { n = c.chan.read(readBuf); }
        catch (IOException e) { freeClient(c); return; }
        if (n < 0) { freeClient(c); return; }
        if (n == 0) return;
        statTotalNetInput += n;
        c.query.put(readBuf.array(), 0, n);
        c.lastInteraction = System.currentTimeMillis();
        processInput(c);
    }

    public void processInput(Client c) {
        while (!c.closeAfterReply && c.query.len > 0 && !c.blocked) {
            /* networking.c processInputBuffer(): paused clients are frozen. */
            if (Cluster.pausedNow(c)) break;
            boolean got;
            try { got = c.parseNext(); }
            catch (Exception e) { c.protocolError("invalid request"); break; }
            if (!got) break;
            if (c.argc == 0) continue;
            boolean connected = c.chan != null;
            try {
                processCommand(c);
            } catch (RedisError e) {
                c.addErr(e.msg);
            } catch (Exception e) {
                c.addError("ERR internal error: " + e);
            }
            /* The command freed this connection (client eviction): stop. */
            if (connected && c.chan == null) break;
        }
    }

    // ------------------------------------------------------------------
    // dispatch
    // ------------------------------------------------------------------
    public void processCommand(Client c) {
        String name = c.cmdName();
        Cmd cmd = CommandTable.lookup(name);
        c.lastCmd = cmd == null ? name : cmd.fullName;

        if (cmd == null) {
            // commandCheckExistence(): container commands report the unknown
            // subcommand, everything else the unknown command plus its args.
            String msg;
            if (CommandTable.SUBS.containsKey(name) && c.argc > 1) {
                msg = "ERR unknown subcommand '" + trunc(Util.s(c.argv[1]), 128)
                        + "'. Try " + name.toUpperCase(java.util.Locale.ROOT) + " HELP.";
            } else {
                StringBuilder args = new StringBuilder();
                for (int i = 1; i < c.argc && args.length() < 128; i++) {
                    args.append("'").append(trunc(Util.s(c.argv[i]), 128 - args.length())).append("' ");
                }
                msg = "ERR unknown command '" + trunc(Util.s(c.argv[0]), 128)
                        + "', with args beginning with: " + args;
            }
            msg = msg.replace('\r', ' ').replace('\n', ' ');
            if (c.inMulti) { c.multiError = true; }
            c.addError(msg);
            return;
        }
        if ((cmd.arity > 0 && cmd.arity != c.argc) || (c.argc < -cmd.arity)) {
            if (c.inMulti) c.multiError = true;
            c.addError("ERR wrong number of arguments for '" + cmd.name + "' command");
            return;
        }
        if (Acl.authRequired(this, c) && !(name.equals("auth") || name.equals("hello")
                || name.equals("quit") || name.equals("reset"))) {
            c.addError("NOAUTH Authentication required.");
            return;
        }
        if (Acl.enabled(this)) {
            String err = Acl.check(this, c, cmd);
            if (err != null) {
                if (c.inMulti) c.multiError = true;
                c.addError(err);
                return;
            }
        }
        /* If cluster is enabled perform the cluster redirection here. */
        if (cluster != null && !cluster.redirectCheck(c, cmd)) return;
        // subscriber mode restrictions (RESP2 only)
        if (c.resp == 2 && (!c.subChannels.isEmpty() || !c.subPatterns.isEmpty() || !c.subShard.isEmpty())) {
            if (!cmd.isPubsub() && !name.equals("subscribe") && !name.equals("unsubscribe")
                    && !name.equals("psubscribe") && !name.equals("punsubscribe")
                    && !name.equals("ssubscribe") && !name.equals("sunsubscribe")
                    && !name.equals("ping") && !name.equals("quit") && !name.equals("reset")
                    && !name.equals("shutdown")) {
                c.addError("ERR Can't execute '" + cmd.fullName
                        + "': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in this context");
                return;
            }
        }
        /* server.c processCommand(): "Loading DB? Return an error if the command
         * has not the CMD_LOADING flag."  This is checked before the busy script
         * rule, and is observable because a script running from the AOF loader
         * pumps the event loop from its busy hook. */
        if (loading && !c.fake && !cmd.isLoading()) {
            if (c.inMulti) c.multiError = true;
            c.addError("LOADING Redis is loading the dataset in memory");
            return;
        }
        /* server.c: "Lua script too slow?" -- only a few commands are allowed
         * while a script is past the busy-reply threshold. */
        if (scripting != null && scripting.timedout && !c.fake && !allowedWhileBusy(c, name)) {
            if (c.inMulti) c.multiError = true;
            c.addError(scripting.isEval()
                    ? "BUSY Redis is busy running a script. You can only call SCRIPT KILL or SHUTDOWN NOSAVE."
                    : "BUSY Redis is busy running a script. You can only call FUNCTION KILL or SHUTDOWN NOSAVE.");
            return;
        }
        /* server.c processCommand(): "Disconnect some clients if total clients
         * memory is too high. We do this before key eviction, after the last
         * command was executed and consumed some client output buffer memory."
         * Evicting only in beforeSleep let one event-loop pass that reads many
         * commands push used_memory past maxmemory on client buffers alone, so
         * keys went before any client did. If this client was the one evicted,
         * the command is not executed. */
        if (!c.fake && c.chan != null) {
            ConnCmds.evictClients(this);
            if (c.chan == null) return;
        }
        /* Handle the maxmemory directive (server.c processCommand()). */
        if (config.getLong("maxmemory") > 0) {
            boolean freed = freeMemoryIfNeeded();
            if (!freed && cmd.isDenyOom()) {
                if (c.inMulti) c.multiError = true;
                c.addError("OOM command not allowed when used memory > 'maxmemory'.");
                return;
            }
        }
        /* Make sure to use a reasonable amount of memory for client side
         * caching metadata (server.c processCommand). */
        Tracking.limitUsedSlots(this);

        /* Don't accept writes without enough good replicas. */
        if (repl.masterHost == null && cmd.isWrite() && !c.fake
                && config.getLong("min-replicas-to-write") != 0
                && config.getLong("min-replicas-max-lag") != 0
                && repl.goodReplicasCount() < config.getLong("min-replicas-to-write")) {
            if (c.inMulti) c.multiError = true;
            c.addError("NOREPLICAS Not enough good slaves to write.");
            return;
        }
        if (repl.masterHost != null && cmd.isWrite() && !c.fake
                && config.getBool("replica-read-only") && !repl.isMasterClient(c)) {
            if (c.inMulti) c.multiError = true;
            c.addError("READONLY You can't write against a read only replica.");
            return;
        }
        if (c.inMulti && !(name.equals("exec") || name.equals("discard")
                || name.equals("multi") || name.equals("watch") || name.equals("quit")
                || name.equals("reset"))) {
            c.multiQueue.add(c.argv);
            c.addSimple("QUEUED");
            return;
        }
        if (repl.masterHost != null && !repl.masterLinkUp
                && !config.getBool("replica-serve-stale-data") && !cmd.isStale()) {
            c.addError("MASTERDOWN Link with MASTER is down and replica-serve-stale-data is set to 'no'.");
            return;
        }
        call(c, cmd);
        /* networking.c resetClient(): CLIENT REPLY SKIP and CLIENT CACHING both
         * affect only the next command. */
        c.replySkip = c.replySkipNext;
        c.replySkipNext = false;
        if (!c.inMulti && !name.equals("client")) c.trackingCachingOn = false;
    }

    /** the CMD_ALLOW_BUSY commands (commands.def) */
    private static boolean allowedWhileBusy(Client c, String name) {
        if (name.equals("auth") || name.equals("hello") || name.equals("replconf")
                || name.equals("reset") || name.equals("quit") || name.equals("multi")
                || name.equals("discard") || name.equals("watch") || name.equals("unwatch")
                || name.equals("shutdown")) return true;
        if ((name.equals("script") || name.equals("function")) && c.argc >= 2) {
            String sub = Util.lower(c.argv[1]);
            return sub.equals("kill") || (name.equals("function") && sub.equals("stats"));
        }
        return false;
    }

    static String trunc(String s, int n) {
        if (n <= 0) return "";
        return s.length() <= n ? s : s.substring(0, n);
    }

    public void call(Client c, Cmd cmd) {
        long dirtyBefore = dirty;
        int outBefore = c.out.len;
        c.pushing = false;
        long start = System.nanoTime();
        if (!monitors.isEmpty() && !cmd.isAdmin() && !c.isMonitor) feedMonitors(c);
        Tracking.beforeCall(this, c);
        boolean failed = false;
        try {
            try {
                cmd.handler.run(c);
            } catch (RedisError e) {
                c.addErr(e.msg);
                failed = true;
                String code = Util.s(e.msg).split(" ")[0];
                errorStats.merge(code, 1L, Long::sum);
            }
            /* networking.c prepareClientToWrite(): CLIENT REPLY OFF/SKIP suppress
             * the command reply, but never the out of band push messages, which
             * are emitted after this point. */
            if ((c.replyOff || c.replySkip) && !c.pushing && c.out.len > outBefore)
                c.out.len = outBefore;
        } finally {
            Tracking.afterCall(this, c, cmd);
        }
        long dur = (System.nanoTime() - start) / 1000;
        long[] st = commandStats.computeIfAbsent(cmd.fullName, x -> new long[3]);
        st[0]++; st[1] += dur;
        if (failed) st[2]++;
        statNumCommands++;
        long offBefore = masterReplOffset;
        /* server.c call(): propagation is driven by a *positive* dirty delta.
         * DEBUG LOADAOF / DEBUG RELOAD reset server.dirty to zero on purpose,
         * and a negative delta must never propagate the command (that would
         * append DEBUG LOADAOF to the AOF and replay it on every load). */
        if (dirty > dirtyBefore) {
            Mem.touchCommandKeys(this, c, cmd);
            propagate(c.db, c.argv, c.argc);
        }
        /* server.c call(): remember the replication offset right after the last
         * command of this client that resulted in propagation (WAIT/WAITAOF). */
        if (masterReplOffset != offBefore) c.woff = masterReplOffset;
        handleReadyKeys();
    }

    /** Commands may replace what gets replicated (e.g. SPOP -> SREM). */
    public byte[][] replOverride = null;

    public void propagate(int dbid, byte[][] argv, int argc) {
        byte[][] a = argv;
        int n = argc;
        if (replOverride != null) { a = replOverride; n = a.length; replOverride = null; if (n == 0) return; }
        byte[][] cmdArr = Arrays.copyOf(a, n);
        aof.feed(dbid, cmdArr);
        repl.feed(dbid, cmdArr);
    }

    public void propagateExtra(int dbid, byte[][] cmdArr) {
        aof.feed(dbid, cmdArr);
        repl.feed(dbid, cmdArr);
    }

    private void feedMonitors(Client c) {
        StringBuilder sb = new StringBuilder();
        sb.append(String.format(Locale.US, "%d.%06d", System.currentTimeMillis() / 1000,
                (System.currentTimeMillis() % 1000) * 1000));
        sb.append(" [").append(c.db).append(' ').append(c.fake ? "lua" : c.addr).append(']');
        for (int i = 0; i < c.argc; i++) {
            sb.append(" \"");
            for (byte b : c.argv[i]) {
                char ch = (char) (b & 0xff);
                if (ch == '\\' || ch == '"') sb.append('\\').append(ch);
                else if (ch == '\n') sb.append("\\n");
                else if (ch == '\r') sb.append("\\r");
                else if (ch < 32 || ch > 126) sb.append(String.format("\\x%02x", b & 0xff));
                else sb.append(ch);
            }
            sb.append('"');
        }
        for (Client m : monitors) Reply.simple(m.out, sb.toString());
    }

    // ------------------------------------------------------------------
    // keyspace helpers
    // ------------------------------------------------------------------
    public RObj lookupKeyRead(Client c, byte[] key) { return lookupKey(c.db, key, true); }
    public RObj lookupKeyWrite(Client c, byte[] key) { return lookupKey(c.db, key, false); }

    /** lookupKeyReadWithFlags(LOOKUP_NOTOUCH): TYPE, TTL, OBJECT and friends. */
    public RObj lookupKeyReadNoTouch(Client c, byte[] key) {
        Db d = dbs[c.db];
        Key k = new Key(key);
        expireIfNeeded(d, k);
        RObj o = d.dict.get(k);
        if (o == null) statKeyspaceMisses++; else statKeyspaceHits++;
        return o;
    }

    public RObj lookupKey(int dbid, byte[] key, boolean read) {
        Db d = dbs[dbid];
        Key k = new Key(key);
        expireIfNeeded(d, k);
        RObj o = d.dict.get(k);
        if (read) { if (o == null) statKeyspaceMisses++; else statKeyspaceHits++; }
        /* lookupKey(): refresh the LRU clock or bump the LFU counter. */
        if (o != null && !loading) Mem.updateOnAccess(this, o);
        return o;
    }

    public RObj lookupType(Client c, byte[] key, int type) {
        RObj o = lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != type) throw RedisError.wrongType();
        return o;
    }

    public RObj lookupTypeWrite(Client c, byte[] key, int type) {
        RObj o = lookupKeyWrite(c, key);
        if (o == null) return null;
        if (o.type != type) throw RedisError.wrongType();
        return o;
    }

    public void setKey(Client c, byte[] key, RObj o) {
        Db d = dbs[c.db];
        Key k = new Key(key);
        d.dict.put(k, o);
        d.expires.remove(k);
        signalModifiedKey(c.db, k);
    }

    public void dbAdd(int dbid, byte[] key, RObj o) {
        Key k = new Key(key);
        dbs[dbid].dict.put(k, o);
        /* A hash that arrives already carrying field TTLs -- RENAME, MOVE, COPY,
         * RESTORE -- has to join db->hexpires under its new name, or the active
         * cycle would never look at it again. */
        if (o != null && o.type == RObj.HASH && ((RHash) o.ptr).hasTtls()) dbs[dbid].hexpires.add(k);
    }

    public boolean dbDelete(int dbid, byte[] key) {
        Key k = new Key(key);
        boolean r = dbs[dbid].delete(k);
        if (r) signalModifiedKey(dbid, k);
        return r;
    }

    public boolean expireIfNeeded(Db d, Key k) {
        Long when = d.expires.get(k);
        if (when == null) return false;
        if (loading) return false;
        long now = mstime();
        if (now < when) return false;
        if (repl.masterHost != null) {
            // replicas do not expire keys themselves; they wait for the DEL
            return false;
        }
        d.dict.remove(k);
        d.expires.remove(k);
        statExpiredKeys++;
        byte[][] del = new byte[][]{Util.b(config.getBool("lazyfree-lazy-expire") ? "UNLINK" : "DEL"), k.b};
        propagateExtra(d.id, del);
        repl.incrOffsetForPropagation();
        Tracking.invalidateKey(this, null, k.b, true);
        notifyKeyspaceEvent('g', "expired", k.b, d.id);
        /* Redis 2.8 does not signal the key as modified here: a key that simply
         * expired must not invalidate a WATCH (see multi.c / db.c). */
        return true;
    }

    public boolean keyIsExpired(Db d, Key k) {
        Long when = d.expires.get(k);
        return when != null && mstime() >= when;
    }

    public long mstime() { return System.currentTimeMillis(); }

    public long statEvictedKeys = 0;

    public long usedMemory() { return Mem.usedMemory(this); }

    /**
     * evict.c performEvictions(): evict until the dataset fits in maxmemory.
     * Returns false when the limit is still exceeded, which makes every
     * denyoom command reply with the OOM error.
     */
    public boolean freeMemoryIfNeeded() { return Mem.performEvictions(this); }

    public void activeExpireCycle() {
        if (repl.masterHost != null) return;
        if (!ServerCmds.activeExpireEnabled) return;
        for (Db d : dbs) {
            if (d.expires.isEmpty()) continue;
            List<Key> toDel = new ArrayList<>();
            int checked = 0;
            for (Map.Entry<Key, Long> e : d.expires.entrySet()) {
                if (++checked > 200) break;
                if (mstime() >= e.getValue()) toDel.add(e.getKey());
            }
            for (Key k : toDel) expireIfNeeded(d, k);
        }
        /* expire.c activeExpireHashFieldCycle(): the same cycle also collects the
         * hash fields whose TTL has passed. Without it a field only disappears
         * when something reads it, and the `hexpired` keyspace event -- which is
         * fired by the *active* path -- would never be delivered. */
        for (Db d : dbs) {
            if (d.hexpires.isEmpty()) continue;
            int checked = 0;
            for (Key k : new ArrayList<>(d.hexpires)) {
                if (++checked > 200) break;
                if (!HashCmds.activeExpireHashFields(this, d, k)) d.hexpires.remove(k);
            }
        }
    }

    // ------------------------------------------------------------------
    // WATCH support
    // ------------------------------------------------------------------
    public void signalModifiedKey(int dbid, Key k) {
        /* db.c signalDeletedKeyAsReady(): a key that was replaced or removed
         * must also wake the clients that block until it disappears (the
         * XREADGROUP "unblock on nokey" case). The per type ready handler
         * decides whether the client can really be served. */
        if (dbs[dbid].blockingKeys.containsKey(k)) dbs[dbid].readyKeys.add(k);
        Tracking.invalidateKey(this, Tracking.current, k.b, true);
        List<Client> l = dbs[dbid].watchedKeys.get(k);
        if (l == null) return;
        for (Client wc : l) wc.dirtyCas = true;
    }

    public void touchAllWatchedKeys(int dbid) {
        for (List<Client> l : dbs[dbid].watchedKeys.values()) for (Client wc : l) wc.dirtyCas = true;
    }

    /**
     * multi.c touchWatchedKeysOnFlush(): only the watched keys that actually
     * exist in the database being emptied invalidate the transaction. `dbid`
     * is -1 for FLUSHALL.
     */
    public void touchWatchedKeysOnFlush(int dbid) {
        Tracking.invalidateKeysOnFlush(this);
        for (Client c : clients.values()) {
            for (int i = 0; i < c.watched.size(); i++) {
                int wdb = c.watchedDbs.get(i);
                if (dbid != -1 && wdb != dbid) continue;
                if (dbs[wdb].dict.containsKey(c.watched.get(i))) { c.dirtyCas = true; break; }
            }
        }
    }

    // ------------------------------------------------------------------
    // keyspace notifications
    // ------------------------------------------------------------------
    public void notifyKeyspaceEvent(char cls, String event, byte[] key, int dbid) {
        String flags = config.getOr("notify-keyspace-events", "");
        if (flags == null || flags.isEmpty()) return;
        boolean all = flags.indexOf('A') >= 0;
        if (!all && flags.indexOf(cls) < 0) return;
        if (flags.indexOf('K') >= 0) {
            byte[] chan = Util.b("__keyspace@" + dbid + "__:" + Util.s(key));
            pubsub.publishMessage(this, chan, Util.b(event), false);
        }
        if (flags.indexOf('E') >= 0) {
            byte[] chan = Util.b("__keyevent@" + dbid + "__:" + event);
            pubsub.publishMessage(this, chan, key, false);
        }
    }

    // ------------------------------------------------------------------
    // blocking list support
    // ------------------------------------------------------------------
    public void signalKeyAsReady(int dbid, byte[] key) {
        Db d = dbs[dbid];
        Key k = new Key(key);
        if (d.blockingKeys.containsKey(k)) d.readyKeys.add(k);
    }

    public void handleReadyKeys() {
        for (Db d : dbs) {
            if (d.readyKeys.isEmpty()) continue;
            List<Key> keys = new ArrayList<>(d.readyKeys);
            d.readyKeys.clear();
            for (Key k : keys) Blocking.serveClientsBlockedOnKey(this, d, k);
        }
    }

    public void unblockClient(Client c) {
        if (!c.blocked) return;
        c.blocked = false;
        for (Key k : c.blockKeys) {
            List<Client> l = dbs[c.db].blockingKeys.get(k);
            if (l != null) { l.remove(c); if (l.isEmpty()) dbs[c.db].blockingKeys.remove(k); }
        }
        c.blockKeys.clear();
        c.blockType = "";
        c.blockState = null;
    }

    // ------------------------------------------------------------------
    // cron and client teardown
    // ------------------------------------------------------------------
    public void cron() {
        /* debug.c: DEBUG PAUSE-CRON stops serverCron() entirely. */
        if (ServerCmds.pauseCron) return;
        if (sentinelMode) { sentinel.timer(); return; }   // sentinel.c sentinelTimer()
        activeExpireCycle();
        Tracking.limitUsedSlots(this);
        Blocking.checkTimeouts(this);
        handleReadyKeys();
        repl.cron();
        aof.cron();
        Persist.cron(this);
        aof.rewriteCron();
        aof.autoRewriteCron();
        /* server.c clientsCron(): give oversized query buffers back. */
        for (Client c : clients.values()) c.resizeQueryBuffer();
        long timeout = config.getLong("timeout");
        if (timeout > 0) {
            long now = System.currentTimeMillis();
            for (Client c : new ArrayList<>(clients.values())) {
                if (c.isReplica || c.isMonitor || c.blocked) continue;
                if (repl != null && repl.masterClient == c) continue;
                if (!c.subChannels.isEmpty() || !c.subPatterns.isEmpty()) continue;
                if (now - c.lastInteraction > timeout * 1000) freeClient(c);
            }
        }
        Persist.maybeBgSave(this);
        Cluster.pauseCron(this);
        if (cluster != null) cluster.cron();
    }

    public void flushAll() {
        for (Client c : new ArrayList<>(clients.values())) {
            if (c.out.len > 0 || Tls.wantWrite(c)) {
                try { c.write(); } catch (IOException e) { clientsToClose.add(c); continue; }
            }
            if (c.out.len == 0 && c.closeAfterReply) clientsToClose.add(c);
            else updateInterest(c);
        }
        if (!clientsToClose.isEmpty()) {
            for (Client c : new ArrayList<>(clientsToClose)) freeClient(c);
            clientsToClose.clear();
        }
    }

    public void updateInterest(Client c) {
        if (c.skey == null || !c.skey.isValid()) return;
        int ops = SelectionKey.OP_READ;
        if (c.out.len > 0 || Tls.wantWrite(c)) ops |= SelectionKey.OP_WRITE;
        if (c.skey.interestOps() != ops) c.skey.interestOps(ops);
    }

    public void freeClient(Client c) {
        if (repl != null && repl.masterClient == c) {
            /* Dropping the master link caches its state for a partial resync. */
            clients.remove(c.id);
            repl.masterLinkLost();
            return;
        }
        if (c.chan == null && !c.fake) return;
        Tracking.onClientFree(this, c);
        unblockClient(c);
        pubsub.unsubscribeAll(this, c);
        Multi.unwatchAll(this, c);
        replicas.remove(c);
        monitors.remove(c);
        clients.remove(c.id);
        try { if (c.skey != null) c.skey.cancel(); } catch (Exception ignored) {}
        try { if (c.chan != null) c.chan.close(); } catch (IOException ignored) {}
        c.chan = null;
    }
}
