package redis;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Random;

/**
 * Redis Sentinel (src/sentinel.c): monitoring, failure detection, the hello
 * channel, leader election and the automatic failover state machine.
 *
 * The instance graph, the timers and the state transitions are a direct port of
 * redis 2.8's sentinel.c; the asynchronous hiredis links are replaced by a
 * small non blocking RESP client ({@link Link}) driven by the server's own
 * selector.
 */
public final class Sentinel {

    /* ----- sentinelRedisInstance flags (SRI_*) ----- */
    public static final int SRI_MASTER = 1 << 0;
    public static final int SRI_SLAVE = 1 << 1;
    public static final int SRI_SENTINEL = 1 << 2;
    public static final int SRI_DISCONNECTED = 1 << 3;
    public static final int SRI_S_DOWN = 1 << 4;
    public static final int SRI_O_DOWN = 1 << 5;
    public static final int SRI_MASTER_DOWN = 1 << 6;
    public static final int SRI_FAILOVER_IN_PROGRESS = 1 << 7;
    public static final int SRI_PROMOTED = 1 << 8;
    public static final int SRI_RECONF_SENT = 1 << 9;
    public static final int SRI_RECONF_INPROG = 1 << 10;
    public static final int SRI_RECONF_DONE = 1 << 11;
    public static final int SRI_FORCE_FAILOVER = 1 << 12;
    public static final int SRI_SCRIPT_KILL_SENT = 1 << 13;

    /* ----- times, in milliseconds ----- */
    public static final int INFO_PERIOD = 10000;
    public static final int PING_PERIOD = 1000;
    public static final int ASK_PERIOD = 1000;
    public static final int PUBLISH_PERIOD = 2000;
    public static final int DEFAULT_DOWN_AFTER = 30000;
    public static final String HELLO_CHANNEL = "__sentinel__:hello";
    public static final int TILT_TRIGGER = 2000;
    public static final int TILT_PERIOD = PING_PERIOD * 30;
    public static final int DEFAULT_SLAVE_PRIORITY = 100;
    public static final int SLAVE_RECONF_TIMEOUT = 10000;
    public static final int DEFAULT_PARALLEL_SYNCS = 1;
    public static final int MIN_LINK_RECONNECT_PERIOD = 15000;
    public static final int DEFAULT_FAILOVER_TIMEOUT = 60 * 3 * 1000;
    public static final int MAX_PENDING_COMMANDS = 100;
    public static final int ELECTION_TIMEOUT = 10000;
    public static final int MAX_DESYNC = 1000;

    /* ----- failover states ----- */
    public static final int FS_NONE = 0;
    public static final int FS_WAIT_START = 1;
    public static final int FS_SELECT_SLAVE = 2;
    public static final int FS_SEND_SLAVEOF_NOONE = 3;
    public static final int FS_WAIT_PROMOTION = 4;
    public static final int FS_RECONF_SLAVES = 5;
    public static final int FS_UPDATE_CONFIG = 6;

    public static final int LINK_UP = 0;
    public static final int LINK_DOWN = 1;

    /* log levels, redis.h REDIS_DEBUG..REDIS_WARNING */
    public static final int DEBUG = 0, VERBOSE = 1, NOTICE = 2, WARNING = 3;

    /* generic flags */
    public static final int GENERATE_EVENT = 1 << 16;
    public static final int LEADER = 1 << 17;
    public static final int OBSERVER = 1 << 18;
    public static final int RESET_NO_SENTINELS = 1 << 0;

    // ------------------------------------------------------------------
    // global state (struct sentinelState)
    // ------------------------------------------------------------------
    public final Server s;
    public long currentEpoch = 0;
    public final LinkedHashMap<String, Instance> masters = new LinkedHashMap<>();
    public boolean tilt = false;
    public int runningScripts = 0;
    public long tiltStartTime = 0;
    public long previousTime = 0;
    public String announceIp = null;
    public int announcePort = 0;
    private final Random rnd = new Random();
    /** SENTINEL SIMULATE-FAILURE flags */
    public boolean simFailCrashAfterElection = false, simFailCrashAfterPromotion = false;

    public Sentinel(Server s) {
        this.s = s;
        this.previousTime = mstime();
    }

    public static long mstime() { return System.currentTimeMillis(); }

    public String myid() { return s.runId; }

    /** redis.c checkForSentinelMode(): argv[0] basename or a --sentinel argument. */
    public static boolean checkForSentinelMode(String[] args) {
        for (String a : args) if (a.equals("--sentinel")) return true;
        return false;
    }

    // ==================================================================
    // addresses
    // ==================================================================
    public static final class Addr {
        public String ip;
        public int port;
        Addr(String ip, int port) { this.ip = ip; this.port = port; }
        public boolean eq(Addr o) { return o != null && port == o.port && ip.equalsIgnoreCase(o.ip); }
        Addr dup() { return new Addr(ip, port); }
    }

    /** createSentinelAddr(): resolves the hostname, null when it can't. */
    static Addr createAddr(String hostname, int port) {
        if (port <= 0 || port > 65535) return null;
        try { return new Addr(InetAddress.getByName(hostname).getHostAddress(), port); }
        catch (Exception e) { return null; }
    }

    // ==================================================================
    // instances
    // ==================================================================
    public static final class Instance {
        public int flags;
        public String name;
        public String runid;
        public long configEpoch;
        public Addr addr;
        public Link cc, pc;
        public int pendingCommands;
        public long ccConnTime, pcConnTime, pcLastActivity;
        public long lastAvailTime, lastPingTime, lastPongTime, lastPubTime, lastHelloTime;
        public long lastMasterDownReplyTime;
        public long sDownSinceTime, oDownSinceTime;
        public long downAfterPeriod;
        public long infoRefresh;
        /** last INFO output, kept for SENTINEL INFO-CACHE */
        public String info;

        public int roleReported;
        public long roleReportedTime, slaveConfChangeTime;

        /* master only */
        public final LinkedHashMap<String, Instance> sentinels = new LinkedHashMap<>();
        public final LinkedHashMap<String, Instance> slaves = new LinkedHashMap<>();
        public int quorum;
        public int parallelSyncs;
        public String authPass;

        /* slave only */
        public long masterLinkDownTime;
        public int slavePriority;
        public long slaveReconfSentTime;
        public Instance master;
        public String slaveMasterHost;
        public int slaveMasterPort;
        public int slaveMasterLinkStatus = LINK_DOWN;
        public long slaveReplOffset;

        /* failover */
        public String leader;
        public long leaderEpoch, failoverEpoch;
        public int failoverState;
        public long failoverStateChangeTime, failoverStartTime, failoverTimeout, failoverDelayLogged;
        public Instance promotedSlave;
        public String notificationScript, clientReconfigScript;

        public boolean isMaster() { return (flags & SRI_MASTER) != 0; }
        public boolean isSlave() { return (flags & SRI_SLAVE) != 0; }
        public boolean isSentinel() { return (flags & SRI_SENTINEL) != 0; }
    }

    public static String typeStr(Instance ri) {
        if ((ri.flags & SRI_MASTER) != 0) return "master";
        if ((ri.flags & SRI_SLAVE) != 0) return "slave";
        if ((ri.flags & SRI_SENTINEL) != 0) return "sentinel";
        return "unknown";
    }

    static String instanceName(String hostname, int port) {
        return (hostname.indexOf(':') >= 0 ? "[" + hostname + "]:" : hostname + ":") + port;
    }

    /** createSentinelRedisInstance(); returns null on duplicate or bad address. */
    public Instance createInstance(String name, int flags, String hostname, int port,
                                   int quorum, Instance master) {
        Addr addr = createAddr(hostname, port);
        if (addr == null) return null;
        if ((flags & (SRI_SLAVE | SRI_SENTINEL)) != 0) name = instanceName(addr.ip, port);

        Map<String, Instance> table;
        if ((flags & SRI_MASTER) != 0) table = masters;
        else if ((flags & SRI_SLAVE) != 0) table = master.slaves;
        else table = master.sentinels;
        if (table.containsKey(name)) return null;

        Instance ri = new Instance();
        ri.flags = flags | SRI_DISCONNECTED;
        ri.name = name;
        ri.runid = null;
        ri.configEpoch = 0;
        ri.addr = addr;
        ri.pendingCommands = 0;
        long now = mstime();
        ri.lastPingTime = now;
        ri.lastAvailTime = now;
        ri.lastPongTime = now;
        ri.lastPubTime = now;
        ri.lastHelloTime = now;
        ri.lastMasterDownReplyTime = now;
        ri.sDownSinceTime = 0;
        ri.oDownSinceTime = 0;
        ri.downAfterPeriod = master != null ? master.downAfterPeriod : DEFAULT_DOWN_AFTER;
        ri.masterLinkDownTime = 0;
        ri.authPass = null;
        ri.slavePriority = DEFAULT_SLAVE_PRIORITY;
        ri.slaveReconfSentTime = 0;
        ri.slaveMasterHost = null;
        ri.slaveMasterPort = 0;
        ri.slaveMasterLinkStatus = LINK_DOWN;
        ri.slaveReplOffset = 0;
        ri.quorum = quorum;
        ri.parallelSyncs = DEFAULT_PARALLEL_SYNCS;
        ri.master = master;
        ri.infoRefresh = 0;

        ri.leader = null;
        ri.leaderEpoch = 0;
        ri.failoverEpoch = 0;
        ri.failoverState = FS_NONE;
        ri.failoverStateChangeTime = 0;
        ri.failoverStartTime = 0;
        ri.failoverTimeout = DEFAULT_FAILOVER_TIMEOUT;
        ri.failoverDelayLogged = 0;
        ri.promotedSlave = null;

        ri.roleReported = ri.flags & (SRI_MASTER | SRI_SLAVE);
        ri.roleReportedTime = now;
        ri.slaveConfChangeTime = now;

        table.put(ri.name, ri);
        return ri;
    }

    /** releaseSentinelRedisInstance(): drop the links of an instance and its children. */
    void releaseInstance(Instance ri) {
        for (Instance x : new ArrayList<>(ri.sentinels.values())) releaseInstance(x);
        for (Instance x : new ArrayList<>(ri.slaves.values())) releaseInstance(x);
        ri.sentinels.clear();
        ri.slaves.clear();
        if (ri.cc != null) killLink(ri, ri.cc);
        if (ri.pc != null) killLink(ri, ri.pc);
        if ((ri.flags & SRI_SLAVE) != 0 && (ri.flags & SRI_PROMOTED) != 0 && ri.master != null)
            ri.master.promotedSlave = null;
    }

    public Instance lookupSlave(Instance ri, String ip, int port) {
        return ri.slaves.get(instanceName(ip, port));
    }

    public Instance getMasterByName(String name) { return masters.get(name); }

    /** removeMatchingSentinelsFromMaster() */
    int removeMatchingSentinels(Instance master, String ip, int port, String runid) {
        int removed = 0;
        for (Instance ri : new ArrayList<>(master.sentinels.values())) {
            if ((ri.runid != null && runid != null && ri.runid.equals(runid))
                    || (ip != null && ri.addr.ip.equals(ip) && port == ri.addr.port)) {
                master.sentinels.remove(ri.name);
                releaseInstance(ri);
                removed++;
            }
        }
        return removed;
    }

    /** getSentinelRedisInstanceByAddrAndRunID() */
    static Instance byAddrAndRunID(Map<String, Instance> instances, String ip, int port, String runid) {
        for (Instance ri : instances.values()) {
            if (runid != null && ri.runid == null) continue;
            if ((runid == null || ri.runid.equals(runid))
                    && (ip == null || (ri.addr.ip.equals(ip) && ri.addr.port == port)))
                return ri;
        }
        return null;
    }

    // ==================================================================
    // events
    // ==================================================================
    public void event(int level, String type, Instance ri, String fmt, Object... args) {
        String msg;
        if (fmt.startsWith("%@")) {
            Instance master = (ri.flags & SRI_MASTER) != 0 ? null : ri.master;
            if (master != null) {
                msg = String.format(Locale.US, "%s %s %s %d @ %s %s %d", typeStr(ri), ri.name,
                        ri.addr.ip, ri.addr.port, master.name, master.addr.ip, master.addr.port);
            } else {
                msg = String.format(Locale.US, "%s %s %s %d", typeStr(ri), ri.name,
                        ri.addr.ip, ri.addr.port);
            }
            String rest = fmt.substring(2);
            if (!rest.isEmpty()) msg = msg + String.format(Locale.US, rest, args);
        } else {
            msg = args.length == 0 ? fmt : String.format(Locale.US, fmt, args);
        }
        if (level >= NOTICE) s.log(level >= WARNING ? '#' : '*', type + " " + msg);
        if (level != DEBUG) {
            s.pubsub.publishMessage(s, Util.b(type), Util.b(msg), false);
        }
    }

    void generateInitialMonitorEvents() {
        for (Instance ri : masters.values())
            event(WARNING, "+monitor", ri, "%@ quorum %d", ri.quorum);
    }

    // ==================================================================
    // master reset / address change
    // ==================================================================
    public void resetMaster(Instance ri, int flags) {
        for (Instance x : new ArrayList<>(ri.slaves.values())) releaseInstance(x);
        ri.slaves.clear();
        if ((flags & RESET_NO_SENTINELS) == 0) {
            for (Instance x : new ArrayList<>(ri.sentinels.values())) releaseInstance(x);
            ri.sentinels.clear();
        }
        if (ri.cc != null) killLink(ri, ri.cc);
        if (ri.pc != null) killLink(ri, ri.pc);
        ri.flags &= SRI_MASTER | SRI_DISCONNECTED;
        ri.leader = null;
        ri.failoverState = FS_NONE;
        ri.failoverStateChangeTime = 0;
        ri.failoverStartTime = 0;
        ri.promotedSlave = null;
        ri.runid = null;
        ri.slaveMasterHost = null;
        long now = mstime();
        ri.lastPingTime = now;
        ri.lastAvailTime = now;
        ri.lastPongTime = now;
        ri.roleReportedTime = now;
        ri.roleReported = SRI_MASTER;
        if ((flags & GENERATE_EVENT) != 0) event(WARNING, "+reset-master", ri, "%@");
    }

    public int resetMastersByPattern(byte[] pattern, int flags) {
        int reset = 0;
        for (Instance ri : new ArrayList<>(masters.values())) {
            if (ri.name != null && Util.stringmatch(pattern, Util.b(ri.name), false)) {
                resetMaster(ri, flags);
                reset++;
            }
        }
        return reset;
    }

    /** sentinelResetMasterAndChangeAddress() */
    public boolean resetMasterAndChangeAddress(Instance master, String ip, int port) {
        Addr newaddr = createAddr(ip, port);
        if (newaddr == null) return false;

        List<Addr> slaves = new ArrayList<>();
        for (Instance slave : master.slaves.values()) {
            if (slave.addr.eq(newaddr)) continue;
            slaves.add(slave.addr.dup());
        }
        if (!newaddr.eq(master.addr)) slaves.add(master.addr.dup());

        resetMaster(master, RESET_NO_SENTINELS);
        master.addr = newaddr;
        master.oDownSinceTime = 0;
        master.sDownSinceTime = 0;

        for (Addr a : slaves) {
            Instance slave = createInstance(null, SRI_SLAVE, a.ip, a.port, master.quorum, master);
            if (slave != null) event(NOTICE, "+slave", slave, "%@");
        }
        flushConfig();
        return true;
    }

    /** sentinelRedisInstanceNoDownFor() */
    static boolean noDownFor(Instance ri, long ms) {
        long mostRecent = Math.max(ri.sDownSinceTime, ri.oDownSinceTime);
        return mostRecent == 0 || (mstime() - mostRecent) > ms;
    }

    /** sentinelGetCurrentMasterAddress() */
    public static Addr currentMasterAddr(Instance master) {
        if ((master.flags & SRI_FAILOVER_IN_PROGRESS) != 0 && master.promotedSlave != null
                && master.failoverState >= FS_RECONF_SLAVES) {
            return master.promotedSlave.addr;
        }
        return master.addr;
    }

    public void propagateDownAfterPeriod(Instance master) {
        for (Instance ri : master.slaves.values()) ri.downAfterPeriod = master.downAfterPeriod;
        for (Instance ri : master.sentinels.values()) ri.downAfterPeriod = master.downAfterPeriod;
    }

    // ==================================================================
    // configuration
    // ==================================================================
    /** sentinelHandleConfiguration(): returns an error string or null. */
    public String handleConfiguration(List<String> argv) {
        int argc = argv.size();
        String o = argv.get(0);
        Instance ri;
        if (o.equalsIgnoreCase("monitor") && argc == 5) {
            int quorum = atoi(argv.get(4));
            if (quorum <= 0) return "Quorum must be 1 or greater.";
            if (createInstance(argv.get(1), SRI_MASTER, argv.get(2), atoi(argv.get(3)), quorum, null) == null)
                return "Can't resolve master instance hostname.";
        } else if (o.equalsIgnoreCase("down-after-milliseconds") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.downAfterPeriod = atoi(argv.get(2));
            if (ri.downAfterPeriod <= 0) return "negative or zero time parameter.";
            propagateDownAfterPeriod(ri);
        } else if (o.equalsIgnoreCase("failover-timeout") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.failoverTimeout = atoi(argv.get(2));
            if (ri.failoverTimeout <= 0) return "negative or zero time parameter.";
        } else if (o.equalsIgnoreCase("parallel-syncs") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.parallelSyncs = atoi(argv.get(2));
        } else if (o.equalsIgnoreCase("notification-script") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.notificationScript = argv.get(2);
        } else if (o.equalsIgnoreCase("client-reconfig-script") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.clientReconfigScript = argv.get(2);
        } else if (o.equalsIgnoreCase("auth-pass") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.authPass = argv.get(2);
        } else if (o.equalsIgnoreCase("current-epoch") && argc == 2) {
            long e = atol(argv.get(1));
            if (e > currentEpoch) currentEpoch = e;
        } else if (o.equalsIgnoreCase("myid") && argc == 2) {
            s.runId = argv.get(1);
        } else if (o.equalsIgnoreCase("config-epoch") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.configEpoch = atol(argv.get(2));
            if (ri.configEpoch > currentEpoch) currentEpoch = ri.configEpoch;
        } else if (o.equalsIgnoreCase("leader-epoch") && argc == 3) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            ri.leaderEpoch = atol(argv.get(2));
        } else if ((o.equalsIgnoreCase("known-slave") || o.equalsIgnoreCase("known-replica")) && argc == 4) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            if (createInstance(null, SRI_SLAVE, argv.get(2), atoi(argv.get(3)), ri.quorum, ri) == null)
                return "Wrong hostname or port for slave.";
        } else if (o.equalsIgnoreCase("known-sentinel") && (argc == 4 || argc == 5)) {
            ri = getMasterByName(argv.get(1));
            if (ri == null) return "No such master with specified name.";
            Instance si = createInstance(null, SRI_SENTINEL, argv.get(2), atoi(argv.get(3)), ri.quorum, ri);
            if (si == null) return "Wrong hostname or port for sentinel.";
            if (argc == 5) si.runid = argv.get(4);
        } else if (o.equalsIgnoreCase("announce-ip") && argc == 2) {
            if (!argv.get(1).isEmpty()) announceIp = argv.get(1);
        } else if (o.equalsIgnoreCase("announce-port") && argc == 2) {
            announcePort = atoi(argv.get(1));
        } else {
            return "Unrecognized sentinel configuration statement.";
        }
        return null;
    }

    static int atoi(String x) { try { return (int) Long.parseLong(x.trim()); } catch (Exception e) { return 0; } }
    static long atol(String x) { try { return Long.parseLong(x.trim()); } catch (Exception e) { return 0; } }

    /** rewriteConfigSentinelOption(): the "sentinel ..." lines of the config file. */
    public List<String> configLines() {
        List<String> out = new ArrayList<>();
        for (Instance master : masters.values()) {
            Addr ma = currentMasterAddr(master);
            out.add("sentinel monitor " + master.name + " " + ma.ip + " " + ma.port + " " + master.quorum);
            if (master.downAfterPeriod != DEFAULT_DOWN_AFTER)
                out.add("sentinel down-after-milliseconds " + master.name + " " + master.downAfterPeriod);
            if (master.failoverTimeout != DEFAULT_FAILOVER_TIMEOUT)
                out.add("sentinel failover-timeout " + master.name + " " + master.failoverTimeout);
            if (master.parallelSyncs != DEFAULT_PARALLEL_SYNCS)
                out.add("sentinel parallel-syncs " + master.name + " " + master.parallelSyncs);
            if (master.notificationScript != null)
                out.add("sentinel notification-script " + master.name + " " + master.notificationScript);
            if (master.clientReconfigScript != null)
                out.add("sentinel client-reconfig-script " + master.name + " " + master.clientReconfigScript);
            if (master.authPass != null)
                out.add("sentinel auth-pass " + master.name + " " + master.authPass);
            out.add("sentinel config-epoch " + master.name + " " + master.configEpoch);
            out.add("sentinel leader-epoch " + master.name + " " + master.leaderEpoch);
            for (Instance ri : master.slaves.values()) {
                Addr sa = ri.addr;
                if (sa.eq(ma)) sa = master.addr;
                out.add("sentinel known-slave " + master.name + " " + sa.ip + " " + sa.port);
            }
            for (Instance ri : master.sentinels.values()) {
                out.add("sentinel known-sentinel " + master.name + " " + ri.addr.ip + " " + ri.addr.port
                        + (ri.runid != null ? " " + ri.runid : ""));
            }
        }
        out.add("sentinel current-epoch " + currentEpoch);
        if (announceIp != null) out.add("sentinel announce-ip " + announceIp);
        if (announcePort != 0) out.add("sentinel announce-port " + announcePort);
        return out;
    }

    /** sentinelFlushConfig(): persist the state into the config file. */
    public void flushConfig() {
        String path = s.config.configFile;
        if (path == null) return;
        List<String> keep = new ArrayList<>();
        try {
            for (String line : Files.readAllLines(Paths.get(path), StandardCharsets.ISO_8859_1)) {
                String t = line.trim();
                if (!t.isEmpty() && t.charAt(0) != '#') {
                    List<String> a;
                    try { a = Config.splitArgs(t); } catch (RuntimeException e) { a = null; }
                    if (a != null && !a.isEmpty() && a.get(0).equalsIgnoreCase("sentinel")) continue;
                }
                keep.add(line);
            }
        } catch (IOException e) {
            // the file may have gone away; we recreate it below
        }
        while (!keep.isEmpty() && keep.get(keep.size() - 1).trim().isEmpty()) keep.remove(keep.size() - 1);
        StringBuilder sb = new StringBuilder();
        for (String l : keep) sb.append(l).append('\n');
        for (String l : configLines()) sb.append(l).append('\n');
        try {
            Path p = Paths.get(path);
            Path tmp = Paths.get(path + ".tmp");
            Files.write(tmp, sb.toString().getBytes(StandardCharsets.ISO_8859_1));
            try { Files.move(tmp, p, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
            catch (IOException e) { Files.move(tmp, p, StandardCopyOption.REPLACE_EXISTING); }
        } catch (IOException e) {
            s.logWarn("WARNING: Sentinel was not able to save the new configuration on disk!!!: "
                    + e.getMessage());
        }
    }

    // ==================================================================
    // asynchronous links
    // ==================================================================
    /** A reply callback; the argument is the parsed reply (see Resp). */
    public interface Cb { void run(Object reply); }

    /** A RESP status reply (+...). */
    public static final class Status {
        public final String v;
        Status(String v) { this.v = v; }
        public String toString() { return v; }
    }

    /** A RESP error reply (-...). */
    public static final class Err {
        public final String v;
        Err(String v) { this.v = v; }
        public String toString() { return v; }
    }

    private static final Object INCOMPLETE = new Object();

    /**
     * A non blocking connection to a monitored instance, registered on the
     * server's selector; replaces the hiredis async context of sentinel.c.
     */
    public static final class Link {
        final Sentinel sen;
        public Instance ri;
        public final boolean pubsub;
        SocketChannel ch;
        SelectionKey key;
        final Buf out = new Buf(1024);
        final Buf in = new Buf(4096);
        final ArrayDeque<Cb> cbs = new ArrayDeque<>();
        Cb sticky;
        boolean connected;
        boolean dead;

        Link(Sentinel sen, Instance ri, boolean pubsub) {
            this.sen = sen;
            this.ri = ri;
            this.pubsub = pubsub;
        }

        boolean connect(String ip, int port) {
            try {
                ch = SocketChannel.open();
                ch.configureBlocking(false);
                boolean now = ch.connect(new InetSocketAddress(InetAddress.getByName(ip), port));
                connected = now;
                key = ch.register(sen.s.selector,
                        now ? SelectionKey.OP_READ : SelectionKey.OP_CONNECT, this);
                return true;
            } catch (Exception e) {
                close();
                return false;
            }
        }

        void close() {
            dead = true;
            try { if (key != null) key.cancel(); } catch (Exception ignored) {}
            try { if (ch != null) ch.close(); } catch (IOException ignored) {}
            ch = null;
        }

        /** Queue a command (array of bulk strings) and try to push it out. */
        public void call(Cb cb, String... argv) {
            if (dead) return;
            out.put((byte) '*').putLong(argv.length).crlf();
            for (String a : argv) {
                byte[] b = Util.b(a);
                out.put((byte) '$').putLong(b.length).crlf().put(b).crlf();
            }
            /* hiredis keeps the callback of a subscribe command around for every
             * later push: we model that with the sticky handler, so a null cb
             * simply means "this reply belongs to the sticky handler". */
            if (cb != null) cbs.add(cb);
            flush();
        }

        void flush() {
            if (dead || !connected || out.len == 0) return;
            try {
                ByteBuffer bb = ByteBuffer.wrap(out.a, 0, out.len);
                int w = ch.write(bb);
                if (w > 0) out.consume(w);
            } catch (IOException e) {
                sen.disconnectFromContext(this);
                close();
                return;
            }
            updateInterest();
        }

        void updateInterest() {
            if (dead || key == null || !key.isValid()) return;
            int ops = connected ? SelectionKey.OP_READ : SelectionKey.OP_CONNECT;
            if (connected && out.len > 0) ops |= SelectionKey.OP_WRITE;
            if (key.interestOps() != ops) key.interestOps(ops);
        }

        public void onEvent(SelectionKey k) {
            if (dead || !k.isValid()) return;
            try {
                if (k.isConnectable()) {
                    boolean ok;
                    try { ok = ch.finishConnect(); }
                    catch (IOException e) { sen.disconnectFromContext(this); close(); return; }
                    if (!ok) return;
                    connected = true;
                    updateInterest();
                    flush();
                    return;
                }
                if (k.isValid() && k.isWritable()) flush();
                if (k.isValid() && !dead && k.isReadable()) read();
            } catch (Exception e) {
                sen.disconnectFromContext(this);
                close();
            }
        }

        private final byte[] rbuf = new byte[16 * 1024];

        void read() {
            int n;
            try { n = ch.read(ByteBuffer.wrap(rbuf)); }
            catch (IOException e) { sen.disconnectFromContext(this); close(); return; }
            if (n < 0) { sen.disconnectFromContext(this); close(); return; }
            if (n == 0) return;
            in.put(rbuf, 0, n);
            while (!dead) {
                int[] pos = new int[]{0};
                Object o = parse(in.a, in.len, pos);
                if (o == INCOMPLETE) break;
                in.consume(pos[0]);
                dispatch(o);
            }
        }

        @SuppressWarnings("unchecked")
        void dispatch(Object reply) {
            if (pubsub && reply instanceof List && ((List<Object>) reply).size() == 3) {
                Object first = ((List<Object>) reply).get(0);
                if (first instanceof byte[] && Util.equalsIgnoreCase((byte[]) first, "message")) {
                    if (sticky != null) sticky.run(reply);
                    return;
                }
            }
            Cb cb = cbs.poll();
            if (cb == null) cb = sticky;
            if (cb != null) cb.run(reply);
        }
    }

    /** Parse one RESP reply out of a[0..len); pos[0] receives the bytes used. */
    static Object parse(byte[] a, int len, int[] pos) {
        int p = pos[0];
        if (p >= len) return INCOMPLETE;
        int nl = -1;
        for (int i = p; i + 1 < len; i++) if (a[i] == '\r' && a[i + 1] == '\n') { nl = i; break; }
        if (nl < 0) return INCOMPLETE;
        byte t = a[p];
        String head = Util.s(a, p + 1, nl - p - 1);
        switch (t) {
            case '+': pos[0] = nl + 2; return new Status(head);
            case '-': pos[0] = nl + 2; return new Err(head);
            case ':': pos[0] = nl + 2; try { return Long.parseLong(head); } catch (Exception e) { return 0L; }
            case '$': {
                long n;
                try { n = Long.parseLong(head); } catch (Exception e) { pos[0] = nl + 2; return null; }
                if (n < 0) { pos[0] = nl + 2; return null; }
                if (nl + 2 + n + 2 > len) return INCOMPLETE;
                byte[] b = Arrays.copyOfRange(a, nl + 2, (int) (nl + 2 + n));
                pos[0] = (int) (nl + 2 + n + 2);
                return b;
            }
            case '*': {
                long n;
                try { n = Long.parseLong(head); } catch (Exception e) { pos[0] = nl + 2; return null; }
                if (n < 0) { pos[0] = nl + 2; return null; }
                List<Object> l = new ArrayList<>((int) Math.min(n, 1024));
                int cur = nl + 2;
                for (long i = 0; i < n; i++) {
                    int[] sub = new int[]{cur};
                    Object o = parse(a, len, sub);
                    if (o == INCOMPLETE) return INCOMPLETE;
                    l.add(o);
                    cur = sub[0];
                }
                pos[0] = cur;
                return l;
            }
            default:
                /* inline / unexpected: swallow the line */
                pos[0] = nl + 2;
                return new Status(Util.s(a, p, nl - p));
        }
    }

    /** sentinelKillLink() */
    void killLink(Instance ri, Link l) {
        if (l == null) return;
        if (ri.cc == l) { ri.cc = null; ri.pendingCommands = 0; }
        if (ri.pc == l) ri.pc = null;
        l.ri = null;
        ri.flags |= SRI_DISCONNECTED;
        l.close();
    }

    /** sentinelDisconnectInstanceFromContext() */
    void disconnectFromContext(Link l) {
        Instance ri = l.ri;
        if (ri == null) return;
        boolean pubsub = (ri.pc == l);
        event(DEBUG, pubsub ? "-pubsub-link" : "-cmd-link", ri, "%@");
        if (pubsub) ri.pc = null; else { ri.cc = null; ri.pendingCommands = 0; }
        ri.flags |= SRI_DISCONNECTED;
    }

    void sendAuthIfNeeded(Instance ri, Link l) {
        String authPass = (ri.flags & SRI_MASTER) != 0 ? ri.authPass
                        : (ri.master != null ? ri.master.authPass : null);
        if (authPass != null) {
            ri.pendingCommands++;
            l.call(r -> discard(ri), "AUTH", authPass);
        }
    }

    void setClientName(Instance ri, Link l, String type) {
        String name = "sentinel-" + (myid().length() >= 8 ? myid().substring(0, 8) : myid()) + "-" + type;
        ri.pendingCommands++;
        l.call(r -> discard(ri), "CLIENT", "SETNAME", name);
    }

    void discard(Instance ri) { if (ri.pendingCommands > 0) ri.pendingCommands--; }

    /** sentinelReconnectInstance() */
    void reconnectInstance(Instance ri) {
        if ((ri.flags & SRI_DISCONNECTED) == 0) return;

        if (ri.cc == null) {
            Link l = new Link(this, ri, false);
            if (!l.connect(ri.addr.ip, ri.addr.port)) {
                event(DEBUG, "-cmd-link-reconnection", ri, "%@");
            } else {
                ri.cc = l;
                ri.ccConnTime = mstime();
                sendAuthIfNeeded(ri, l);
                setClientName(ri, l, "cmd");
                sendPing(ri);
            }
        }
        if ((ri.flags & (SRI_MASTER | SRI_SLAVE)) != 0 && ri.pc == null) {
            Link l = new Link(this, ri, true);
            if (!l.connect(ri.addr.ip, ri.addr.port)) {
                event(DEBUG, "-pubsub-link-reconnection", ri, "%@");
            } else {
                ri.pc = l;
                ri.pcConnTime = mstime();
                ri.pcLastActivity = mstime();
                sendAuthIfNeeded(ri, l);
                setClientName(ri, l, "pubsub");
                l.sticky = r -> receiveHelloMessages(l, r);
                l.call(null, "SUBSCRIBE", HELLO_CHANNEL);
            }
        }
        if (ri.cc != null && !ri.cc.dead
                && (((ri.flags & SRI_SENTINEL) != 0) || (ri.pc != null && !ri.pc.dead)))
            ri.flags &= ~SRI_DISCONNECTED;
    }

    // ==================================================================
    // INFO refresh
    // ==================================================================
    /** sentinelMasterLooksSane() */
    boolean masterLooksSane(Instance master) {
        return (master.flags & SRI_MASTER) != 0
                && master.roleReported == SRI_MASTER
                && (master.flags & (SRI_S_DOWN | SRI_O_DOWN)) == 0
                && (mstime() - master.infoRefresh) < INFO_PERIOD * 2;
    }

    /** sentinelRefreshInstanceInfo() */
    void refreshInstanceInfo(Instance ri, String info) {
        int role = 0;
        ri.masterLinkDownTime = 0;
        ri.info = info;

        for (String l : info.split("\r\n|\n")) {
            if (l.length() >= 47 && l.startsWith("run_id:")) {
                String rid = l.substring(7, 47);
                if (ri.runid == null) {
                    ri.runid = rid;
                } else if (!ri.runid.equals(rid)) {
                    event(NOTICE, "+reboot", ri, "%@");
                    ri.runid = rid;
                }
            }

            if ((ri.flags & SRI_MASTER) != 0 && l.length() >= 7
                    && l.startsWith("slave") && Character.isDigit(l.charAt(5))) {
                String ip, port;
                int ipx = l.indexOf("ip=");
                if (ipx < 0) {
                    int c1 = l.indexOf(':');
                    if (c1 < 0) continue;
                    int c2 = l.indexOf(',', c1 + 1);
                    if (c2 < 0) continue;
                    int c3 = l.indexOf(',', c2 + 1);
                    if (c3 < 0) continue;
                    ip = l.substring(c1 + 1, c2);
                    port = l.substring(c2 + 1, c3);
                } else {
                    int px = l.indexOf("port=");
                    if (px < 0) continue;
                    ip = l.substring(ipx + 3);
                    int e = ip.indexOf(',');
                    if (e >= 0) ip = ip.substring(0, e);
                    port = l.substring(px + 5);
                    e = port.indexOf(',');
                    if (e >= 0) port = port.substring(0, e);
                }
                if (lookupSlave(ri, ip, atoi(port)) == null) {
                    Instance slave = createInstance(null, SRI_SLAVE, ip, atoi(port), ri.quorum, ri);
                    if (slave != null) {
                        event(NOTICE, "+slave", slave, "%@");
                        flushConfig();
                    }
                }
            }

            if (l.length() >= 32 && l.startsWith("master_link_down_since_seconds"))
                ri.masterLinkDownTime = atol(l.substring(31)) * 1000;

            if (l.startsWith("role:master")) role = SRI_MASTER;
            else if (l.startsWith("role:slave")) role = SRI_SLAVE;

            if (role == SRI_SLAVE) {
                if (l.length() >= 12 && l.startsWith("master_host:")) {
                    String h = l.substring(12);
                    if (ri.slaveMasterHost == null || !h.equalsIgnoreCase(ri.slaveMasterHost)) {
                        ri.slaveMasterHost = h;
                        ri.slaveConfChangeTime = mstime();
                    }
                }
                if (l.length() >= 12 && l.startsWith("master_port:")) {
                    int p = atoi(l.substring(12));
                    if (ri.slaveMasterPort != p) {
                        ri.slaveMasterPort = p;
                        ri.slaveConfChangeTime = mstime();
                    }
                }
                if (l.length() >= 19 && l.startsWith("master_link_status:")) {
                    ri.slaveMasterLinkStatus =
                            l.substring(19).equalsIgnoreCase("up") ? LINK_UP : LINK_DOWN;
                }
                if (l.length() >= 15 && l.startsWith("slave_priority:"))
                    ri.slavePriority = atoi(l.substring(15));
                if (l.length() >= 18 && l.startsWith("slave_repl_offset:"))
                    ri.slaveReplOffset = atol(l.substring(18));
            }
        }
        ri.infoRefresh = mstime();

        /* ------------------------- acting half ------------------------- */
        if (role != ri.roleReported) {
            ri.roleReportedTime = mstime();
            ri.roleReported = role;
            if (role == SRI_SLAVE) ri.slaveConfChangeTime = mstime();
            event(VERBOSE, ((ri.flags & (SRI_MASTER | SRI_SLAVE)) == role) ? "+role-change" : "-role-change",
                    ri, "%@ new reported role is %s", role == SRI_MASTER ? "master" : "slave");
        }

        if (tilt) return;

        /* slave -> master role switch */
        if ((ri.flags & SRI_SLAVE) != 0 && role == SRI_MASTER) {
            if ((ri.flags & SRI_PROMOTED) != 0
                    && (ri.master.flags & SRI_FAILOVER_IN_PROGRESS) != 0
                    && ri.master.failoverState == FS_WAIT_PROMOTION) {
                ri.master.configEpoch = ri.master.failoverEpoch;
                ri.master.failoverState = FS_RECONF_SLAVES;
                ri.master.failoverStateChangeTime = mstime();
                flushConfig();
                event(WARNING, "+promoted-slave", ri, "%@");
                event(WARNING, "+failover-state-reconf-slaves", ri.master, "%@");
                forceHelloUpdateForMaster(ri.master);
                if (simFailCrashAfterPromotion) simulateFailureCrash();
            } else {
                long waitTime = PUBLISH_PERIOD * 4;
                if ((ri.flags & SRI_PROMOTED) == 0 && masterLooksSane(ri.master)
                        && noDownFor(ri, waitTime)
                        && mstime() - ri.roleReportedTime > waitTime) {
                    if (sendSlaveOf(ri, ri.master.addr.ip, ri.master.addr.port))
                        event(NOTICE, "+convert-to-slave", ri, "%@");
                }
            }
        }

        /* slaves replicating to a different master address */
        if ((ri.flags & SRI_SLAVE) != 0 && role == SRI_SLAVE
                && (ri.slaveMasterPort != ri.master.addr.port
                    || ri.slaveMasterHost == null
                    || !ri.slaveMasterHost.equalsIgnoreCase(ri.master.addr.ip))) {
            long waitTime = ri.master.failoverTimeout;
            if (masterLooksSane(ri.master) && noDownFor(ri, waitTime)
                    && mstime() - ri.slaveConfChangeTime > waitTime) {
                if (sendSlaveOf(ri, ri.master.addr.ip, ri.master.addr.port))
                    event(NOTICE, "+fix-slave-config", ri, "%@");
            }
        }

        /* slave being reconfigured changed state */
        if ((ri.flags & SRI_SLAVE) != 0 && role == SRI_SLAVE
                && (ri.flags & (SRI_RECONF_SENT | SRI_RECONF_INPROG)) != 0) {
            if ((ri.flags & SRI_RECONF_SENT) != 0 && ri.slaveMasterHost != null
                    && ri.master.promotedSlave != null
                    && ri.slaveMasterHost.equals(ri.master.promotedSlave.addr.ip)
                    && ri.slaveMasterPort == ri.master.promotedSlave.addr.port) {
                ri.flags &= ~SRI_RECONF_SENT;
                ri.flags |= SRI_RECONF_INPROG;
                event(NOTICE, "+slave-reconf-inprog", ri, "%@");
            }
            if ((ri.flags & SRI_RECONF_INPROG) != 0 && ri.slaveMasterLinkStatus == LINK_UP) {
                ri.flags &= ~SRI_RECONF_INPROG;
                ri.flags |= SRI_RECONF_DONE;
                event(NOTICE, "+slave-reconf-done", ri, "%@");
            }
        }
    }

    /** sentinelSimFailureCrash() */
    void simulateFailureCrash() {
        s.logWarn("Sentinel CRASH because of SENTINEL simulate-failure");
        s.logStream.flush();
        Runtime.getRuntime().halt(99);
    }

    // ==================================================================
    // reply callbacks
    // ==================================================================
    void infoReply(Instance ri, Object reply) {
        discard(ri);
        if (reply instanceof byte[]) refreshInstanceInfo(ri, Util.s((byte[]) reply));
    }

    void pingReply(Instance ri, Object reply) {
        discard(ri);
        String str = null;
        if (reply instanceof Status) str = ((Status) reply).v;
        else if (reply instanceof Err) str = ((Err) reply).v;
        if (str != null) {
            if (str.startsWith("PONG") || str.startsWith("LOADING") || str.startsWith("MASTERDOWN")) {
                ri.lastAvailTime = mstime();
                ri.lastPingTime = 0;
            } else if (str.startsWith("BUSY") && (ri.flags & SRI_S_DOWN) != 0
                    && (ri.flags & SRI_SCRIPT_KILL_SENT) == 0) {
                if (ri.cc != null) {
                    ri.pendingCommands++;
                    ri.cc.call(r -> discard(ri), "SCRIPT", "KILL");
                }
                ri.flags |= SRI_SCRIPT_KILL_SENT;
            }
        }
        ri.lastPongTime = mstime();
    }

    void publishReply(Instance ri, Object reply) {
        discard(ri);
        if (!(reply instanceof Err)) ri.lastPubTime = mstime();
    }

    // ==================================================================
    // hello messages
    // ==================================================================
    /** sentinelProcessHelloMessage() */
    public void processHelloMessage(String hello) {
        String[] token = hello.split(",", -1);
        if (token.length != 8) return;
        Instance master = getMasterByName(token[4]);
        if (master == null) return;

        int port = atoi(token[1]);
        int masterPort = atoi(token[6]);
        Instance si = byAddrAndRunID(master.sentinels, token[0], port, token[2]);
        long helloEpoch = atol(token[3]);
        long masterConfigEpoch = atol(token[7]);

        if (si == null) {
            int removed = removeMatchingSentinels(master, token[0], port, token[2]);
            if (removed > 0) {
                event(NOTICE, "-dup-sentinel", master, "%@ #duplicate of %s:%d or %s",
                        token[0], port, token[2]);
            }
            si = createInstance(null, SRI_SENTINEL, token[0], port, master.quorum, master);
            if (si != null) {
                event(NOTICE, "+sentinel", si, "%@");
                si.runid = token[2];
                flushConfig();
            }
        }

        if (helloEpoch > currentEpoch) {
            currentEpoch = helloEpoch;
            flushConfig();
            event(WARNING, "+new-epoch", master, "%d", currentEpoch);
        }

        if (master.configEpoch < masterConfigEpoch) {
            master.configEpoch = masterConfigEpoch;
            if (masterPort != master.addr.port || !master.addr.ip.equals(token[5])) {
                if (si != null) event(WARNING, "+config-update-from", si, "%@");
                event(WARNING, "+switch-master", master, "%s %s %d %s %d",
                        master.name, master.addr.ip, master.addr.port, token[5], masterPort);
                resetMasterAndChangeAddress(master, token[5], masterPort);
            }
        }

        if (si != null) si.lastHelloTime = mstime();
    }

    @SuppressWarnings("unchecked")
    void receiveHelloMessages(Link l, Object reply) {
        Instance ri = l.ri;
        if (ri == null) return;
        ri.pcLastActivity = mstime();
        if (!(reply instanceof List)) return;
        List<Object> r = (List<Object>) reply;
        if (r.size() != 3) return;
        if (!(r.get(0) instanceof byte[]) || !(r.get(1) instanceof byte[]) || !(r.get(2) instanceof byte[]))
            return;
        if (!Util.equalsIgnoreCase((byte[]) r.get(0), "message")) return;
        String payload = Util.s((byte[]) r.get(2));
        if (payload.contains(myid())) return;    /* not interested in meeting ourselves */
        processHelloMessage(payload);
    }

    /** sentinelSendHello() */
    boolean sendHello(Instance ri) {
        Instance master = (ri.flags & SRI_MASTER) != 0 ? ri : ri.master;
        Addr masterAddr = currentMasterAddr(master);
        if ((ri.flags & SRI_DISCONNECTED) != 0) return false;
        if (ri.cc == null || ri.cc.dead || ri.cc.ch == null) return false;

        String ip;
        if (announceIp != null) {
            ip = announceIp;
        } else {
            try {
                ip = ((InetSocketAddress) ri.cc.ch.getLocalAddress()).getAddress().getHostAddress();
            } catch (Exception e) { return false; }
        }
        int port = announcePort != 0 ? announcePort : s.port;

        String payload = ip + "," + port + "," + myid() + "," + currentEpoch + ","
                + master.name + "," + masterAddr.ip + "," + masterAddr.port + "," + master.configEpoch;
        ri.pendingCommands++;
        ri.cc.call(r -> publishReply(ri, r), "PUBLISH", HELLO_CHANNEL, payload);
        return true;
    }

    static void forceHelloUpdateDict(Map<String, Instance> instances) {
        for (Instance ri : instances.values())
            if (ri.lastPubTime >= (PUBLISH_PERIOD + 1)) ri.lastPubTime -= (PUBLISH_PERIOD + 1);
    }

    public boolean forceHelloUpdateForMaster(Instance master) {
        if ((master.flags & SRI_MASTER) == 0) return false;
        if (master.lastPubTime >= (PUBLISH_PERIOD + 1)) master.lastPubTime -= (PUBLISH_PERIOD + 1);
        forceHelloUpdateDict(master.sentinels);
        forceHelloUpdateDict(master.slaves);
        return true;
    }

    /** sentinelSendPing() */
    boolean sendPing(Instance ri) {
        if (ri.cc == null || ri.cc.dead) return false;
        ri.pendingCommands++;
        ri.cc.call(r -> pingReply(ri, r), "PING");
        if (ri.lastPingTime == 0) ri.lastPingTime = mstime();
        return true;
    }

    /** sentinelSendPeriodicCommands() */
    void sendPeriodicCommands(Instance ri) {
        long now = mstime();
        if ((ri.flags & SRI_DISCONNECTED) != 0) return;
        if (ri.pendingCommands >= MAX_PENDING_COMMANDS) return;

        long infoPeriod;
        if ((ri.flags & SRI_SLAVE) != 0
                && (ri.master.flags & (SRI_O_DOWN | SRI_FAILOVER_IN_PROGRESS)) != 0) {
            infoPeriod = 1000;
        } else {
            infoPeriod = INFO_PERIOD;
        }
        long pingPeriod = Math.min(ri.downAfterPeriod, PING_PERIOD);

        if (ri.cc == null || ri.cc.dead) return;

        if ((ri.flags & SRI_SENTINEL) == 0
                && (ri.infoRefresh == 0 || (now - ri.infoRefresh) > infoPeriod)) {
            ri.pendingCommands++;
            ri.cc.call(r -> infoReply(ri, r), "INFO");
        } else if ((now - ri.lastPongTime) > pingPeriod) {
            sendPing(ri);
        } else if ((now - ri.lastPubTime) > PUBLISH_PERIOD) {
            sendHello(ri);
        }
    }

    // ==================================================================
    // availability checks
    // ==================================================================
    void checkSubjectivelyDown(Instance ri) {
        long elapsed = 0;
        if (ri.lastPingTime != 0) elapsed = mstime() - ri.lastPingTime;

        if (ri.cc != null && (mstime() - ri.ccConnTime) > MIN_LINK_RECONNECT_PERIOD
                && ri.lastPingTime != 0
                && (mstime() - ri.lastPingTime) > (ri.downAfterPeriod / 2)
                && (mstime() - ri.lastPongTime) > (ri.downAfterPeriod / 2)) {
            killLink(ri, ri.cc);
        }

        if (ri.pc != null && (mstime() - ri.pcConnTime) > MIN_LINK_RECONNECT_PERIOD
                && (mstime() - ri.pcLastActivity) > (PUBLISH_PERIOD * 3)) {
            killLink(ri, ri.pc);
        }

        if (elapsed > ri.downAfterPeriod
                || ((ri.flags & SRI_MASTER) != 0 && ri.roleReported == SRI_SLAVE
                    && mstime() - ri.roleReportedTime > (ri.downAfterPeriod + INFO_PERIOD * 2))) {
            if ((ri.flags & SRI_S_DOWN) == 0) {
                event(WARNING, "+sdown", ri, "%@");
                ri.sDownSinceTime = mstime();
                ri.flags |= SRI_S_DOWN;
            }
        } else {
            if ((ri.flags & SRI_S_DOWN) != 0) {
                event(WARNING, "-sdown", ri, "%@");
                ri.flags &= ~(SRI_S_DOWN | SRI_SCRIPT_KILL_SENT);
            }
        }
    }

    void checkObjectivelyDown(Instance master) {
        int quorum = 0;
        boolean odown = false;
        if ((master.flags & SRI_S_DOWN) != 0) {
            quorum = 1;
            for (Instance ri : master.sentinels.values())
                if ((ri.flags & SRI_MASTER_DOWN) != 0) quorum++;
            if (quorum >= master.quorum) odown = true;
        }
        if (odown) {
            if ((master.flags & SRI_O_DOWN) == 0) {
                event(WARNING, "+odown", master, "%@ #quorum %d/%d", quorum, master.quorum);
                master.flags |= SRI_O_DOWN;
                master.oDownSinceTime = mstime();
            }
        } else {
            if ((master.flags & SRI_O_DOWN) != 0) {
                event(WARNING, "-odown", master, "%@");
                master.flags &= ~SRI_O_DOWN;
            }
        }
    }

    @SuppressWarnings("unchecked")
    void receiveIsMasterDownReply(Instance ri, Object reply) {
        discard(ri);
        if (!(reply instanceof List)) return;
        List<Object> r = (List<Object>) reply;
        if (r.size() != 3) return;
        if (!(r.get(0) instanceof Long) || !(r.get(1) instanceof byte[]) || !(r.get(2) instanceof Long))
            return;
        ri.lastMasterDownReplyTime = mstime();
        if ((Long) r.get(0) == 1L) ri.flags |= SRI_MASTER_DOWN;
        else ri.flags &= ~SRI_MASTER_DOWN;
        String leader = Util.s((byte[]) r.get(1));
        if (!leader.equals("*")) {
            long epoch = (Long) r.get(2);
            if (ri.leaderEpoch != epoch)
                s.logWarn(ri.name + " voted for " + leader + " " + epoch);
            ri.leader = leader;
            ri.leaderEpoch = epoch;
        }
    }

    /** sentinelAskMasterStateToOtherSentinels() */
    void askMasterStateToOtherSentinels(Instance master, boolean forced) {
        for (Instance ri : new ArrayList<>(master.sentinels.values())) {
            long elapsed = mstime() - ri.lastMasterDownReplyTime;
            if (elapsed > ASK_PERIOD * 5) {
                ri.flags &= ~SRI_MASTER_DOWN;
                ri.leader = null;
            }
            if ((master.flags & SRI_S_DOWN) == 0) continue;
            if ((ri.flags & SRI_DISCONNECTED) != 0) continue;
            if (!forced && mstime() - ri.lastMasterDownReplyTime < ASK_PERIOD) continue;
            if (ri.cc == null || ri.cc.dead) continue;
            ri.pendingCommands++;
            ri.cc.call(r -> receiveIsMasterDownReply(ri, r),
                    "SENTINEL", "is-master-down-by-addr",
                    master.addr.ip, Integer.toString(master.addr.port),
                    Long.toString(currentEpoch),
                    master.failoverState > FS_NONE ? myid() : "*");
        }
    }

    // ==================================================================
    // failover
    // ==================================================================
    /** sentinelVoteLeader() */
    public String voteLeader(Instance master, long reqEpoch, String reqRunid, long[] leaderEpochOut) {
        if (reqEpoch > currentEpoch) {
            currentEpoch = reqEpoch;
            flushConfig();
            event(WARNING, "+new-epoch", master, "%d", currentEpoch);
        }
        if (master.leaderEpoch < reqEpoch && currentEpoch <= reqEpoch) {
            master.leader = reqRunid;
            master.leaderEpoch = currentEpoch;
            flushConfig();
            event(WARNING, "+vote-for-leader", master, "%s %d", master.leader, master.leaderEpoch);
            if (!master.leader.equalsIgnoreCase(myid()))
                master.failoverStartTime = mstime() + rnd.nextInt(MAX_DESYNC);
        }
        leaderEpochOut[0] = master.leaderEpoch;
        return master.leader;
    }

    /** sentinelGetLeader() */
    String getLeader(Instance master, long epoch) {
        Map<String, Long> counters = new LinkedHashMap<>();
        int voters = master.sentinels.size() + 1;
        String winner = null;
        long maxVotes = 0;

        for (Instance ri : master.sentinels.values())
            if (ri.leader != null && ri.leaderEpoch == currentEpoch)
                counters.merge(ri.leader, 1L, Long::sum);

        for (Map.Entry<String, Long> e : counters.entrySet()) {
            if (e.getValue() > maxVotes) { maxVotes = e.getValue(); winner = e.getKey(); }
        }

        long[] leaderEpoch = new long[1];
        String myvote = voteLeader(master, epoch, winner != null ? winner : myid(), leaderEpoch);

        if (myvote != null && leaderEpoch[0] == epoch) {
            long votes = counters.merge(myvote, 1L, Long::sum);
            if (votes > maxVotes) { maxVotes = votes; winner = myvote; }
        }

        int votersQuorum = voters / 2 + 1;
        if (winner != null && (maxVotes < votersQuorum || maxVotes < master.quorum)) winner = null;
        return winner;
    }

    /** sentinelSendSlaveOf(): MULTI / SLAVEOF / CONFIG REWRITE / CLIENT KILL / EXEC. */
    boolean sendSlaveOf(Instance ri, String host, int port) {
        if (ri.cc == null || ri.cc.dead) return false;
        String h = host, p = Integer.toString(port);
        if (host == null) { h = "NO"; p = "ONE"; }
        ri.pendingCommands++;
        ri.cc.call(r -> discard(ri), "MULTI");
        ri.pendingCommands++;
        ri.cc.call(r -> discard(ri), "SLAVEOF", h, p);
        ri.pendingCommands++;
        ri.cc.call(r -> discard(ri), "CONFIG", "REWRITE");
        ri.pendingCommands++;
        ri.cc.call(r -> discard(ri), "CLIENT", "KILL", "TYPE", "normal");
        ri.pendingCommands++;
        ri.cc.call(r -> discard(ri), "EXEC");
        return true;
    }

    /** sentinelStartFailover() */
    public void startFailover(Instance master) {
        master.failoverState = FS_WAIT_START;
        master.flags |= SRI_FAILOVER_IN_PROGRESS;
        master.failoverEpoch = ++currentEpoch;
        event(WARNING, "+new-epoch", master, "%d", currentEpoch);
        event(WARNING, "+try-failover", master, "%@");
        master.failoverStartTime = mstime() + rnd.nextInt(MAX_DESYNC);
        master.failoverStateChangeTime = mstime();
    }

    boolean startFailoverIfNeeded(Instance master) {
        if ((master.flags & SRI_O_DOWN) == 0) return false;
        if ((master.flags & SRI_FAILOVER_IN_PROGRESS) != 0) return false;
        if (mstime() - master.failoverStartTime < master.failoverTimeout * 2) {
            if (master.failoverDelayLogged != master.failoverStartTime) {
                long clock = (master.failoverStartTime + master.failoverTimeout * 2) / 1000;
                master.failoverDelayLogged = master.failoverStartTime;
                s.logWarn("Next failover delay: I will not start a failover before "
                        + new java.util.Date(clock * 1000).toString());
            }
            return false;
        }
        startFailover(master);
        return true;
    }

    /** compareSlavesForPromotion() */
    static int compareSlaves(Instance a, Instance b) {
        if (a.slavePriority != b.slavePriority) return Integer.compare(a.slavePriority, b.slavePriority);
        if (a.slaveReplOffset > b.slaveReplOffset) return -1;
        if (a.slaveReplOffset < b.slaveReplOffset) return 1;
        if (a.runid == null && b.runid == null) return 0;
        if (a.runid == null) return 1;
        if (b.runid == null) return -1;
        return a.runid.compareToIgnoreCase(b.runid);
    }

    /** sentinelSelectSlave() */
    public Instance selectSlave(Instance master) {
        List<Instance> instances = new ArrayList<>();
        long maxMasterDownTime = 0;
        if ((master.flags & SRI_S_DOWN) != 0) maxMasterDownTime += mstime() - master.sDownSinceTime;
        maxMasterDownTime += master.downAfterPeriod * 10;

        for (Instance slave : master.slaves.values()) {
            if ((slave.flags & (SRI_S_DOWN | SRI_O_DOWN | SRI_DISCONNECTED)) != 0) continue;
            if (mstime() - slave.lastAvailTime > PING_PERIOD * 5) continue;
            if (slave.slavePriority == 0) continue;
            long infoValidityTime = (master.flags & SRI_S_DOWN) != 0
                    ? PING_PERIOD * 5 : INFO_PERIOD * 3;
            if (mstime() - slave.infoRefresh > infoValidityTime) continue;
            if (slave.masterLinkDownTime > maxMasterDownTime) continue;
            instances.add(slave);
        }
        if (instances.isEmpty()) return null;
        instances.sort(Sentinel::compareSlaves);
        return instances.get(0);
    }

    void failoverWaitStart(Instance ri) {
        String leader = getLeader(ri, ri.failoverEpoch);
        boolean isleader = leader != null && leader.equalsIgnoreCase(myid());

        if (!isleader && (ri.flags & SRI_FORCE_FAILOVER) == 0) {
            long electionTimeout = Math.min(ELECTION_TIMEOUT, ri.failoverTimeout);
            if (mstime() - ri.failoverStartTime > electionTimeout) {
                event(WARNING, "-failover-abort-not-elected", ri, "%@");
                abortFailover(ri);
            }
            return;
        }
        event(WARNING, "+elected-leader", ri, "%@");
        if (simFailCrashAfterElection) simulateFailureCrash();
        ri.failoverState = FS_SELECT_SLAVE;
        ri.failoverStateChangeTime = mstime();
        event(WARNING, "+failover-state-select-slave", ri, "%@");
    }

    void failoverSelectSlave(Instance ri) {
        Instance slave = selectSlave(ri);
        if (slave == null) {
            event(WARNING, "-failover-abort-no-good-slave", ri, "%@");
            abortFailover(ri);
        } else {
            event(WARNING, "+selected-slave", slave, "%@");
            slave.flags |= SRI_PROMOTED;
            ri.promotedSlave = slave;
            ri.failoverState = FS_SEND_SLAVEOF_NOONE;
            ri.failoverStateChangeTime = mstime();
            event(NOTICE, "+failover-state-send-slaveof-noone", slave, "%@");
        }
    }

    void failoverSendSlaveOfNoOne(Instance ri) {
        if ((ri.promotedSlave.flags & SRI_DISCONNECTED) != 0) {
            if (mstime() - ri.failoverStateChangeTime > ri.failoverTimeout) {
                event(WARNING, "-failover-abort-slave-timeout", ri, "%@");
                abortFailover(ri);
            }
            return;
        }
        if (!sendSlaveOf(ri.promotedSlave, null, 0)) return;
        event(NOTICE, "+failover-state-wait-promotion", ri.promotedSlave, "%@");
        ri.failoverState = FS_WAIT_PROMOTION;
        ri.failoverStateChangeTime = mstime();
    }

    void failoverWaitPromotion(Instance ri) {
        if (mstime() - ri.failoverStateChangeTime > ri.failoverTimeout) {
            event(WARNING, "-failover-abort-slave-timeout", ri, "%@");
            abortFailover(ri);
        }
    }

    void failoverDetectEnd(Instance master) {
        int notReconfigured = 0;
        boolean timeout = false;
        long elapsed = mstime() - master.failoverStateChangeTime;

        if (master.promotedSlave == null || (master.promotedSlave.flags & SRI_S_DOWN) != 0) return;

        for (Instance slave : master.slaves.values()) {
            if ((slave.flags & (SRI_PROMOTED | SRI_RECONF_DONE)) != 0) continue;
            if ((slave.flags & SRI_S_DOWN) != 0) continue;
            notReconfigured++;
        }

        if (elapsed > master.failoverTimeout) {
            notReconfigured = 0;
            timeout = true;
            event(WARNING, "+failover-end-for-timeout", master, "%@");
        }

        if (notReconfigured == 0) {
            event(WARNING, "+failover-end", master, "%@");
            master.failoverState = FS_UPDATE_CONFIG;
            master.failoverStateChangeTime = mstime();
        }

        if (timeout) {
            for (Instance slave : master.slaves.values()) {
                if ((slave.flags & (SRI_RECONF_DONE | SRI_RECONF_SENT | SRI_DISCONNECTED)) != 0) continue;
                if (sendSlaveOf(slave, master.promotedSlave.addr.ip, master.promotedSlave.addr.port)) {
                    event(NOTICE, "+slave-reconf-sent-be", slave, "%@");
                    slave.flags |= SRI_RECONF_SENT;
                }
            }
        }
    }

    void failoverReconfNextSlave(Instance master) {
        int inProgress = 0;
        for (Instance slave : master.slaves.values())
            if ((slave.flags & (SRI_RECONF_SENT | SRI_RECONF_INPROG)) != 0) inProgress++;

        for (Instance slave : new ArrayList<>(master.slaves.values())) {
            if (inProgress >= master.parallelSyncs) break;
            if ((slave.flags & (SRI_PROMOTED | SRI_RECONF_DONE)) != 0) continue;

            if ((slave.flags & SRI_RECONF_SENT) != 0
                    && (mstime() - slave.slaveReconfSentTime) > SLAVE_RECONF_TIMEOUT) {
                event(NOTICE, "-slave-reconf-sent-timeout", slave, "%@");
                slave.flags &= ~SRI_RECONF_SENT;
                slave.flags |= SRI_RECONF_DONE;
            }

            if ((slave.flags & (SRI_DISCONNECTED | SRI_RECONF_SENT | SRI_RECONF_INPROG)) != 0) continue;

            if (sendSlaveOf(slave, master.promotedSlave.addr.ip, master.promotedSlave.addr.port)) {
                slave.flags |= SRI_RECONF_SENT;
                slave.slaveReconfSentTime = mstime();
                event(NOTICE, "+slave-reconf-sent", slave, "%@");
                inProgress++;
            }
        }
        failoverDetectEnd(master);
    }

    void failoverSwitchToPromotedSlave(Instance master) {
        Instance ref = master.promotedSlave != null ? master.promotedSlave : master;
        event(WARNING, "+switch-master", master, "%s %s %d %s %d",
                master.name, master.addr.ip, master.addr.port, ref.addr.ip, ref.addr.port);
        resetMasterAndChangeAddress(master, ref.addr.ip, ref.addr.port);
    }

    void failoverStateMachine(Instance ri) {
        if ((ri.flags & SRI_FAILOVER_IN_PROGRESS) == 0) return;
        switch (ri.failoverState) {
            case FS_WAIT_START: failoverWaitStart(ri); break;
            case FS_SELECT_SLAVE: failoverSelectSlave(ri); break;
            case FS_SEND_SLAVEOF_NOONE: failoverSendSlaveOfNoOne(ri); break;
            case FS_WAIT_PROMOTION: failoverWaitPromotion(ri); break;
            case FS_RECONF_SLAVES: failoverReconfNextSlave(ri); break;
            default: break;
        }
    }

    public void abortFailover(Instance ri) {
        ri.flags &= ~(SRI_FAILOVER_IN_PROGRESS | SRI_FORCE_FAILOVER);
        ri.failoverState = FS_NONE;
        ri.failoverStateChangeTime = mstime();
        if (ri.promotedSlave != null) {
            ri.promotedSlave.flags &= ~SRI_PROMOTED;
            ri.promotedSlave = null;
        }
    }

    // ==================================================================
    // timer
    // ==================================================================
    void handleRedisInstance(Instance ri) {
        reconnectInstance(ri);
        sendPeriodicCommands(ri);

        if (tilt) {
            if (mstime() - tiltStartTime < TILT_PERIOD) return;
            tilt = false;
            event(WARNING, "-tilt", null, "#tilt mode exited");
        }

        checkSubjectivelyDown(ri);

        if ((ri.flags & SRI_MASTER) != 0) {
            checkObjectivelyDown(ri);
            if (startFailoverIfNeeded(ri)) askMasterStateToOtherSentinels(ri, true);
            failoverStateMachine(ri);
            askMasterStateToOtherSentinels(ri, false);
        }
    }

    void handleDictOfRedisInstances(Map<String, Instance> instances) {
        Instance switchToPromoted = null;
        for (Instance ri : new ArrayList<>(instances.values())) {
            handleRedisInstance(ri);
            if ((ri.flags & SRI_MASTER) != 0) {
                handleDictOfRedisInstances(ri.slaves);
                handleDictOfRedisInstances(ri.sentinels);
                if (ri.failoverState == FS_UPDATE_CONFIG) switchToPromoted = ri;
            }
        }
        if (switchToPromoted != null) failoverSwitchToPromotedSlave(switchToPromoted);
    }

    void checkTiltCondition() {
        long now = mstime();
        long delta = now - previousTime;
        if (delta < 0 || delta > TILT_TRIGGER) {
            tilt = true;
            tiltStartTime = mstime();
            event(WARNING, "+tilt", null, "#tilt mode entered");
        }
        previousTime = mstime();
    }

    private String lastTimerError = null;

    public void timer() {
        checkTiltCondition();
        /* A Sentinel is a monitoring daemon: an unexpected failure while
         * handling one instance must not take the whole process down, or the
         * masters it watches lose their monitor. Log the first occurrence of
         * each distinct failure and keep going. */
        try {
            handleDictOfRedisInstances(masters);
        } catch (RuntimeException | StackOverflowError e) {
            String msg = String.valueOf(e);
            if (!msg.equals(lastTimerError)) {
                lastTimerError = msg;
                s.logWarn("Error while handling monitored instances: " + msg);
            }
        }

        /* We continuously change the frequency of the Redis "timer interrupt"
         * in order to desynchronize every Sentinel from every other. This
         * non-determinism avoids that Sentinels started at the same time exactly
         * continue to stay synchronized asking to be voted at the same time
         * again and again (resulting in nobody likely winning the election
         * because of split brain voting). */
        s.cronPeriodMs = 1000 / (REDIS_DEFAULT_HZ + rnd.nextInt(REDIS_DEFAULT_HZ));
    }

    /** redis.h REDIS_DEFAULT_HZ */
    static final int REDIS_DEFAULT_HZ = 10;

    // ==================================================================
    // startup
    // ==================================================================
    /** sentinelIsRunning() */
    public void isRunning() {
        s.logWarn("Sentinel runid is " + s.runId);
        if (s.config.configFile == null) {
            s.logWarn("Sentinel started without a config file. Exiting...");
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        } else if (!Files.isWritable(Paths.get(s.config.configFile))) {
            s.logWarn("Sentinel config file " + s.config.configFile
                    + " is not writable: Permission denied. Exiting...");
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        generateInitialMonitorEvents();
    }

    /** Apply the "sentinel ..." directives collected while parsing the config. */
    public void loadConfig(List<List<String>> directives) {
        for (List<String> d : directives) {
            if (d.isEmpty()) continue;
            String err = handleConfiguration(d);
            if (err != null) {
                s.logWarn("\n*** FATAL CONFIG FILE ERROR ***");
                s.logWarn("Reading the configuration file, at line: sentinel " + String.join(" ", d));
                s.logWarn(">>> '" + String.join(" ", d) + "'");
                s.logWarn(err);
                s.logStream.flush();
                Runtime.getRuntime().halt(1);
            }
        }
    }
}
