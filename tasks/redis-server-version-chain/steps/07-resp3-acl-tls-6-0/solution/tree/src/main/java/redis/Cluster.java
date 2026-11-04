package redis;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.StandardSocketOptions;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.*;

/**
 * Redis Cluster: the slot map, the node table, the binary cluster bus, gossip,
 * failure detection, config epochs and the replica failover election.
 *
 * Ported from redis 3.0.7 src/cluster.c + cluster.h. The wire format of the
 * cluster bus is the x86-64 memory layout of `clusterMsg`, so a node of this
 * implementation interoperates with a real redis 3.0 node.
 */
public final class Cluster {

    // ------------------------------------------------------------------
    // constants (cluster.h)
    // ------------------------------------------------------------------
    public static final int SLOTS = 16384;
    public static final int NAMELEN = 40;
    public static final int PORT_INCR = 10000;

    public static final int STATE_OK = 0;
    public static final int STATE_FAIL = 1;

    public static final int DEFAULT_NODE_TIMEOUT = 15000;
    static final int FAIL_REPORT_VALIDITY_MULT = 2;
    static final int FAIL_UNDO_TIME_MULT = 2;
    static final int MF_TIMEOUT = 5000;
    static final int MF_PAUSE_MULT = 2;
    static final int SLAVE_MIGRATION_DELAY = 5000;
    static final int BLACKLIST_TTL = 60;

    /* node flags */
    public static final int F_MASTER = 1;
    public static final int F_SLAVE = 2;
    public static final int F_PFAIL = 4;
    public static final int F_FAIL = 8;
    public static final int F_MYSELF = 16;
    public static final int F_HANDSHAKE = 32;
    public static final int F_NOADDR = 64;
    public static final int F_MEET = 128;
    public static final int F_MIGRATE_TO = 256;

    /* redirection error codes */
    public static final int REDIR_NONE = 0;
    public static final int REDIR_CROSS_SLOT = 1;
    public static final int REDIR_UNSTABLE = 2;
    public static final int REDIR_ASK = 3;
    public static final int REDIR_MOVED = 4;
    public static final int REDIR_DOWN_STATE = 5;
    public static final int REDIR_DOWN_UNBOUND = 6;

    /* message types */
    static final int MSG_PING = 0;
    static final int MSG_PONG = 1;
    static final int MSG_MEET = 2;
    static final int MSG_FAIL = 3;
    static final int MSG_PUBLISH = 4;
    static final int MSG_FAILOVER_AUTH_REQUEST = 5;
    static final int MSG_FAILOVER_AUTH_ACK = 6;
    static final int MSG_UPDATE = 7;
    static final int MSG_MFSTART = 8;
    static final String[] MSG_NAMES = {"ping", "pong", "meet", "fail", "publish",
            "auth-req", "auth-ack", "update", "mfstart"};

    static final int MFLAG0_PAUSED = 1;
    static final int MFLAG0_FORCEACK = 2;

    /* clusterMsg byte layout, x86-64 (see cluster.h) */
    static final int HDR_LEN = 2208;      /* CLUSTERMSG_MIN_LEN */
    static final int GOSSIP_LEN = 104;
    static final int UPDATE_LEN = 2096;
    static final int FAIL_LEN = 40;
    static final int PUBLISH_LEN = 16;    /* sizeof(clusterMsgDataPublish) */
    static final int IP_STR_LEN = 46;

    static final int O_TOTLEN = 4, O_VER = 8, O_TYPE = 12, O_COUNT = 14,
            O_CUREPOCH = 16, O_CFGEPOCH = 24, O_OFFSET = 32, O_SENDER = 40,
            O_MYSLOTS = 80, O_SLAVEOF = 2128, O_PORT = 2200, O_FLAGS = 2202,
            O_STATE = 2204, O_MFLAGS = 2205, O_DATA = 2208;
    /* gossip entry field offsets */
    static final int G_NAME = 0, G_PING = 40, G_PONG = 44, G_IP = 48, G_PORT = 94, G_FLAGS = 96;
    /* clusterMsgDataUpdate field offsets */
    static final int U_EPOCH = 0, U_NAME = 8, U_SLOTS = 48;

    /* cant-failover reasons */
    static final int CANT_NONE = 0, CANT_DATA_AGE = 1, CANT_WAITING_DELAY = 2,
            CANT_EXPIRED = 3, CANT_WAITING_VOTES = 4;
    static final int CANT_RELOG_PERIOD = 60 * 5;

    static final int TODO_HANDLE_FAILOVER = 1;
    static final int TODO_UPDATE_STATE = 2;
    static final int TODO_SAVE_CONFIG = 4;
    static final int TODO_FSYNC_CONFIG = 8;

    // ------------------------------------------------------------------
    // CRC16-CCITT (XMODEM) -- crc16.c
    // ------------------------------------------------------------------
    private static final int[] CRC16TAB = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
        0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
        0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
        0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
        0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
        0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
        0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
        0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
        0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
        0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
        0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
        0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
        0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
        0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
        0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
        0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
        0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
        0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
        0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
        0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
        0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
        0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
        0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
        0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
        0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
        0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
        0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
        0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
        0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
        0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
        0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
    };

    public static int crc16(byte[] b, int off, int len) {
        int crc = 0;
        for (int i = 0; i < len; i++)
            crc = ((crc << 8) ^ CRC16TAB[((crc >> 8) ^ (b[off + i] & 0xff)) & 0xff]) & 0xffff;
        return crc;
    }

    /** cluster.c keyHashSlot(): crc16 of the key (or of the {...} hash tag) and 16383. */
    public static int keyHashSlot(byte[] key) {
        int keylen = key.length;
        int s;
        for (s = 0; s < keylen; s++) if (key[s] == '{') break;
        if (s == keylen) return crc16(key, 0, keylen) & 0x3FFF;
        int e;
        for (e = s + 1; e < keylen; e++) if (key[e] == '}') break;
        if (e == keylen || e == s + 1) return crc16(key, 0, keylen) & 0x3FFF;
        return crc16(key, s + 1, e - s - 1) & 0x3FFF;
    }

    // ------------------------------------------------------------------
    // data model
    // ------------------------------------------------------------------
    public static final class FailReport {
        Node node;
        long time;
        FailReport(Node n, long t) { node = n; time = t; }
    }

    public static final class Node {
        public long ctime;
        public String name;
        public int flags;
        public long configEpoch;
        public final byte[] slots = new byte[SLOTS / 8];
        public int numslots;
        public final List<Node> slaves = new ArrayList<>();
        public Node slaveof;
        public long pingSent, pongReceived, failTime, votedTime, replOffsetTime, orphanedTime;
        public long replOffset;
        public String ip = "";
        public int port;
        public Link link;
        public final List<FailReport> failReports = new ArrayList<>();

        Node(String nodename, int flags) {
            this.name = nodename != null ? nodename : Server.randomHex(NAMELEN);
            this.ctime = System.currentTimeMillis();
            this.flags = flags;
        }

        public boolean isMaster() { return (flags & F_MASTER) != 0; }
        public boolean isSlave() { return (flags & F_SLAVE) != 0; }
        public boolean inHandshake() { return (flags & F_HANDSHAKE) != 0; }
        public boolean hasAddr() { return (flags & F_NOADDR) == 0; }
        public boolean timedOut() { return (flags & F_PFAIL) != 0; }
        public boolean failed() { return (flags & F_FAIL) != 0; }
    }

    /** clusterLink: one TCP connection of the cluster bus. */
    public final class Link {
        public SocketChannel ch;
        public SelectionKey key;
        public Node node;
        public boolean inbound;
        public long ctime = System.currentTimeMillis();
        final Buf snd = new Buf(1024);
        final Buf rcv = new Buf(HDR_LEN + 256);
        boolean connecting;
        boolean freed;

        public void onEvent(SelectionKey k) {
            try {
                if (k.isValid() && k.isConnectable()) {
                    if (!ch.finishConnect()) return;
                    connecting = false;
                    updateInterest();
                }
                if (!freed && k.isValid() && k.isWritable()) writeHandler();
                if (!freed && k.isValid() && k.isReadable()) readHandler();
            } catch (IOException e) {
                freeLink(this);
            } catch (RuntimeException e) {
                s.logWarn("Cluster bus error: " + e);
                freeLink(this);
            }
            if (!freed) updateInterest();
        }

        void updateInterest() {
            if (key == null || !key.isValid()) return;
            int ops = connecting ? SelectionKey.OP_CONNECT : SelectionKey.OP_READ;
            if (!connecting && snd.len > 0) ops |= SelectionKey.OP_WRITE;
            if (key.interestOps() != ops) key.interestOps(ops);
        }

        void writeHandler() throws IOException {
            if (snd.len == 0) return;
            int w = ch.write(ByteBuffer.wrap(snd.a, 0, snd.len));
            if (w > 0) snd.consume(w);
        }

        void readHandler() throws IOException {
            byte[] tmp = new byte[16 * 1024];
            while (true) {
                int readlen;
                if (rcv.len < 8) {
                    readlen = 8 - rcv.len;
                } else {
                    long totlen = u32(rcv.a, O_TOTLEN);
                    if (rcv.len == 8) {
                        if (rcv.a[0] != 'R' || rcv.a[1] != 'C' || rcv.a[2] != 'm' || rcv.a[3] != 'b'
                                || totlen < HDR_LEN) {
                            s.logWarn("Bad message length or signature received from Cluster bus.");
                            freeLink(this);
                            return;
                        }
                    }
                    readlen = (int) (totlen - rcv.len);
                    if (readlen > tmp.length) readlen = tmp.length;
                }
                ByteBuffer bb = ByteBuffer.wrap(tmp, 0, readlen);
                int n = ch.read(bb);
                if (n < 0) { freeLink(this); return; }
                if (n == 0) return;
                rcv.put(tmp, 0, n);
                if (rcv.len >= 8 && rcv.len == u32(rcv.a, O_TOTLEN)) {
                    byte[] pkt = rcv.toBytes();
                    rcv.clear();
                    if (!processPacket(this, pkt)) return;   /* link no longer valid */
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // cluster state
    // ------------------------------------------------------------------
    public final Server s;
    public Node myself;
    public long currentEpoch;
    public int state = STATE_FAIL;
    public int size = 1;
    public final LinkedHashMap<String, Node> nodes = new LinkedHashMap<>();
    public final HashMap<String, Long> blacklist = new HashMap<>();
    public final Node[] slots = new Node[SLOTS];
    public final Node[] migratingSlotsTo = new Node[SLOTS];
    public final Node[] importingSlotsFrom = new Node[SLOTS];

    long failoverAuthTime;
    int failoverAuthCount;
    boolean failoverAuthSent;
    int failoverAuthRank;
    long failoverAuthEpoch;
    int cantFailoverReason = CANT_NONE;

    long mfEnd;
    Node mfSlave;
    long mfMasterOffset;
    boolean mfCanStart;

    long lastVoteEpoch;
    int todoBeforeSleep;
    public final long[] statsSent = new long[MSG_NAMES.length];
    public final long[] statsReceived = new long[MSG_NAMES.length];

    ServerSocketChannel busListener;
    public String configFile;
    private final Random rnd = new Random();
    private long iteration = 0;
    private long amongMinorityTime = 0;
    private long firstCallTime = 0;
    private long lastLogTime = 0;

    /** CLIENT PAUSE / manual failover client pause (networking.c pauseClients()). */
    public static long clientsPauseEnd = 0;

    public Cluster(Server s) { this.s = s; }

    public int nodeTimeout() {
        long t = s.config.getLong("cluster-node-timeout");
        return (int) (t <= 0 ? DEFAULT_NODE_TIMEOUT : t);
    }

    static long now() { return System.currentTimeMillis(); }

    // ------------------------------------------------------------------
    // initialization (clusterInit)
    // ------------------------------------------------------------------
    public void init() {
        String cf = s.config.getOr("cluster-config-file", "nodes.conf");
        if (cf == null || cf.isEmpty()) cf = "nodes.conf";
        configFile = s.resolve(cf).toString();

        boolean saveconf = false;
        if (!loadConfig(configFile)) {
            myself = new Node(null, F_MYSELF | F_MASTER);
            s.logNotice("No cluster configuration found, I'm " + myself.name);
            addNode(myself);
            saveconf = true;
        }
        if (saveconf) saveConfigOrDie(true);

        if (s.port > 65535 - PORT_INCR) {
            s.logWarn("Redis port number too high. Cluster communication port is 10,000 port "
                    + "numbers higher than your Redis port. Your Redis port number must be "
                    + "lower than 55535.");
            System.exit(1);
        }
        int busPort = busPort(s.port);
        try {
            busListener = ServerSocketChannel.open();
            busListener.configureBlocking(false);
            busListener.socket().setReuseAddress(true);
            busListener.bind(new InetSocketAddress(busPort), 511);
            busListener.register(s.selector, SelectionKey.OP_ACCEPT, this);
        } catch (IOException e) {
            s.logWarn("Warning: Could not create server TCP listening socket *:" + busPort
                    + ": " + e.getMessage());
            System.exit(1);
        }
        myself.port = s.port;
        mfEnd = 0;
        resetManualFailover();
    }

    public int busPort(int port) {
        long declared = s.config.getLong("cluster-port");
        if (declared > 0) return (int) declared;
        long off = s.config.getLong("cluster-port-offset");
        return port + (int) (off > 0 ? off : PORT_INCR);
    }

    // ------------------------------------------------------------------
    // nodes.conf (clusterLoadConfig / clusterSaveConfig)
    // ------------------------------------------------------------------
    private boolean loadConfig(String filename) {
        Path p = java.nio.file.Paths.get(filename);
        List<String> lines;
        try {
            if (!Files.exists(p) || Files.size(p) == 0) return false;
            lines = Files.readAllLines(p, StandardCharsets.ISO_8859_1);
        } catch (IOException e) {
            s.logWarn("Loading the cluster node config from " + filename + ": " + e.getMessage());
            System.exit(1);
            return false;
        }
        for (String line : lines) {
            if (line.isEmpty()) continue;
            List<String> argv;
            try { argv = Config.splitArgs(line); }
            catch (RuntimeException e) { fmterr(); return false; }
            if (argv.isEmpty()) continue;
            if (argv.get(0).equalsIgnoreCase("vars")) {
                for (int j = 1; j + 1 < argv.size(); j += 2) {
                    if (argv.get(j).equalsIgnoreCase("currentEpoch"))
                        currentEpoch = parseUlong(argv.get(j + 1));
                    else if (argv.get(j).equalsIgnoreCase("lastVoteEpoch"))
                        lastVoteEpoch = parseUlong(argv.get(j + 1));
                    else
                        s.logWarn("Skipping unknown cluster config variable '" + argv.get(j) + "'");
                }
                continue;
            }
            if (argv.size() < 8) { fmterr(); return false; }
            Node n = lookupNode(argv.get(0));
            if (n == null) { n = new Node(argv.get(0), 0); addNode(n); }
            String addr = argv.get(1);
            int colon = addr.lastIndexOf(':');
            if (colon < 0) { fmterr(); return false; }
            n.ip = addr.substring(0, colon);
            String portPart = addr.substring(colon + 1);
            int at = portPart.indexOf('@');           /* redis >= 4 "port@cport" */
            if (at >= 0) portPart = portPart.substring(0, at);
            n.port = atoi(portPart);

            for (String f : argv.get(2).split(",")) {
                if (f.equalsIgnoreCase("myself")) { myself = n; n.flags |= F_MYSELF; }
                else if (f.equalsIgnoreCase("master")) n.flags |= F_MASTER;
                else if (f.equalsIgnoreCase("slave")) n.flags |= F_SLAVE;
                else if (f.equalsIgnoreCase("fail?")) n.flags |= F_PFAIL;
                else if (f.equalsIgnoreCase("fail")) { n.flags |= F_FAIL; n.failTime = now(); }
                else if (f.equalsIgnoreCase("handshake")) n.flags |= F_HANDSHAKE;
                else if (f.equalsIgnoreCase("noaddr")) n.flags |= F_NOADDR;
            }
            if (!argv.get(3).startsWith("-")) {
                Node master = lookupNode(argv.get(3));
                if (master == null) { master = new Node(argv.get(3), 0); addNode(master); }
                n.slaveof = master;
                nodeAddSlave(master, n);
            }
            if (atoi(argv.get(4)) != 0) n.pingSent = now();
            if (atoi(argv.get(5)) != 0) n.pongReceived = now();
            n.configEpoch = parseUlong(argv.get(6));

            for (int j = 8; j < argv.size(); j++) {
                String tok = argv.get(j);
                if (tok.startsWith("[")) {
                    int dash = tok.indexOf('-');
                    if (dash < 0) continue;
                    char direction = tok.charAt(dash + 1);
                    int slot = atoi(tok.substring(1, dash));
                    String other = tok.substring(dash + 3);
                    if (other.endsWith("]")) other = other.substring(0, other.length() - 1);
                    Node cn = lookupNode(other);
                    if (cn == null) { cn = new Node(other, 0); addNode(cn); }
                    if (slot < 0 || slot >= SLOTS) continue;
                    if (direction == '>') migratingSlotsTo[slot] = cn;
                    else importingSlotsFrom[slot] = cn;
                    continue;
                }
                int start, stop;
                int dash = tok.indexOf('-');
                if (dash > 0) {
                    start = atoi(tok.substring(0, dash));
                    stop = atoi(tok.substring(dash + 1));
                } else {
                    start = stop = atoi(tok);
                }
                while (start <= stop) { addSlot(n, start); start++; }
            }
        }
        if (myself == null) { fmterr(); return false; }
        s.logNotice("Node configuration loaded, I'm " + myself.name);
        if (Long.compareUnsigned(getMaxEpoch(), currentEpoch) > 0) currentEpoch = getMaxEpoch();
        return true;
    }

    private void fmterr() {
        s.logWarn("Unrecoverable error: corrupted cluster config file.");
        s.logStream.flush();
        Runtime.getRuntime().halt(1);
    }

    static long parseUlong(String v) {
        try { return Long.parseUnsignedLong(v.trim()); } catch (RuntimeException e) { return 0; }
    }

    static int atoi(String v) {
        v = v.trim();
        int i = 0, sign = 1;
        if (i < v.length() && (v.charAt(i) == '-' || v.charAt(i) == '+')) {
            sign = v.charAt(i) == '-' ? -1 : 1;
            i++;
        }
        long r = 0;
        for (; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c < '0' || c > '9') break;
            r = r * 10 + (c - '0');
            if (r > Integer.MAX_VALUE) return sign * Integer.MAX_VALUE;
        }
        return (int) (sign * r);
    }

    public int saveConfig(boolean doFsync) {
        todoBeforeSleep &= ~TODO_SAVE_CONFIG;
        StringBuilder ci = genNodesDescription(F_HANDSHAKE);
        ci.append("vars currentEpoch ").append(Long.toUnsignedString(currentEpoch))
          .append(" lastVoteEpoch ").append(Long.toUnsignedString(lastVoteEpoch)).append("\n");
        try {
            byte[] data = ci.toString().getBytes(StandardCharsets.ISO_8859_1);
            Path p = java.nio.file.Paths.get(configFile);
            Files.write(p, data, StandardOpenOption.CREATE, StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING);
            if (doFsync) todoBeforeSleep &= ~TODO_FSYNC_CONFIG;
            return 0;
        } catch (IOException e) {
            return -1;
        }
    }

    public void saveConfigOrDie(boolean doFsync) {
        if (saveConfig(doFsync) == -1) {
            s.logWarn("Fatal: can't update cluster config file.");
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
    }

    // ------------------------------------------------------------------
    // node table
    // ------------------------------------------------------------------
    public Node lookupNode(String name) {
        if (name == null || name.length() != NAMELEN) return null;
        return nodes.get(name);
    }

    void addNode(Node n) { nodes.put(n.name, n); }

    void renameNode(Node n, String newname) {
        nodes.remove(n.name);
        n.name = newname;
        addNode(n);
    }

    static int nodeAddSlave(Node master, Node slave) {
        for (Node x : master.slaves) if (x == slave) return -1;
        master.slaves.add(slave);
        master.flags |= F_MIGRATE_TO;
        return 0;
    }

    static int nodeRemoveSlave(Node master, Node slave) {
        if (master.slaves.remove(slave)) {
            if (master.slaves.isEmpty()) master.flags &= ~F_MIGRATE_TO;
            return 0;
        }
        return -1;
    }

    static int countNonFailingSlaves(Node n) {
        int ok = 0;
        for (Node sl : n.slaves) if (!sl.failed()) ok++;
        return ok;
    }

    public void delNode(Node delnode) {
        for (int j = 0; j < SLOTS; j++) {
            if (importingSlotsFrom[j] == delnode) importingSlotsFrom[j] = null;
            if (migratingSlotsTo[j] == delnode) migratingSlotsTo[j] = null;
            if (slots[j] == delnode) delSlot(j);
        }
        for (Node n : new ArrayList<>(nodes.values())) {
            if (n == delnode) continue;
            nodeDelFailureReport(n, delnode);
        }
        /* freeClusterNode() */
        for (Node sl : delnode.slaves) sl.slaveof = null;
        delnode.slaves.clear();
        if (delnode.isSlave() && delnode.slaveof != null) nodeRemoveSlave(delnode.slaveof, delnode);
        nodes.remove(delnode.name);
        if (delnode.link != null) freeLink(delnode.link);
    }

    // ------------------------------------------------------------------
    // failure reports
    // ------------------------------------------------------------------
    boolean nodeAddFailureReport(Node failing, Node sender) {
        for (FailReport fr : failing.failReports) {
            if (fr.node == sender) { fr.time = now(); return false; }
        }
        failing.failReports.add(new FailReport(sender, now()));
        return true;
    }

    void nodeCleanupFailureReports(Node node) {
        long maxtime = (long) nodeTimeout() * FAIL_REPORT_VALIDITY_MULT;
        long n = now();
        node.failReports.removeIf(fr -> n - fr.time > maxtime);
    }

    boolean nodeDelFailureReport(Node node, Node sender) {
        FailReport found = null;
        for (FailReport fr : node.failReports) if (fr.node == sender) { found = fr; break; }
        if (found == null) return false;
        node.failReports.remove(found);
        nodeCleanupFailureReports(node);
        return true;
    }

    public int nodeFailureReportsCount(Node node) {
        nodeCleanupFailureReports(node);
        return node.failReports.size();
    }

    // ------------------------------------------------------------------
    // epochs
    // ------------------------------------------------------------------
    public long getMaxEpoch() {
        long max = 0;
        for (Node n : nodes.values())
            if (Long.compareUnsigned(n.configEpoch, max) > 0) max = n.configEpoch;
        if (Long.compareUnsigned(max, currentEpoch) < 0) max = currentEpoch;
        return max;
    }

    public boolean bumpConfigEpochWithoutConsensus() {
        long maxEpoch = getMaxEpoch();
        if (myself.configEpoch == 0 || myself.configEpoch != maxEpoch) {
            currentEpoch++;
            myself.configEpoch = currentEpoch;
            doBeforeSleep(TODO_SAVE_CONFIG | TODO_FSYNC_CONFIG);
            s.logWarn("New configEpoch set to " + Long.toUnsignedString(myself.configEpoch));
            return true;
        }
        return false;
    }

    void handleConfigEpochCollision(Node sender) {
        if (sender.configEpoch != myself.configEpoch || !sender.isMaster() || !myself.isMaster()) return;
        if (sender.name.compareTo(myself.name) <= 0) return;
        currentEpoch++;
        myself.configEpoch = currentEpoch;
        saveConfigOrDie(true);
        s.logNotice("WARNING: configEpoch collision with node " + sender.name
                + ". configEpoch set to " + Long.toUnsignedString(myself.configEpoch));
    }

    // ------------------------------------------------------------------
    // blacklist
    // ------------------------------------------------------------------
    void blacklistCleanup() {
        long nowSec = now() / 1000;
        blacklist.entrySet().removeIf(e -> e.getValue() < nowSec);
    }

    void blacklistAddNode(Node node) {
        blacklistCleanup();
        blacklist.put(node.name, now() / 1000 + BLACKLIST_TTL);
    }

    boolean blacklistExists(String nodeid) {
        blacklistCleanup();
        return blacklist.containsKey(nodeid);
    }

    // ------------------------------------------------------------------
    // failure detection
    // ------------------------------------------------------------------
    void markNodeAsFailingIfNeeded(Node node) {
        int neededQuorum = (size / 2) + 1;
        if (!node.timedOut()) return;
        if (node.failed()) return;
        int failures = nodeFailureReportsCount(node);
        if (myself.isMaster()) failures++;
        if (failures < neededQuorum) return;

        s.logNotice("Marking node " + node.name + " as failing (quorum reached).");
        node.flags &= ~F_PFAIL;
        node.flags |= F_FAIL;
        node.failTime = now();
        if (myself.isMaster()) sendFail(node.name);
        doBeforeSleep(TODO_UPDATE_STATE | TODO_SAVE_CONFIG);
    }

    void clearNodeFailureIfNeeded(Node node) {
        long n = now();
        if (node.isSlave() || node.numslots == 0) {
            s.logNotice("Clear FAIL state for node " + node.name + ": "
                    + (node.isSlave() ? "slave" : "master without slots") + " is reachable again.");
            node.flags &= ~F_FAIL;
            doBeforeSleep(TODO_UPDATE_STATE | TODO_SAVE_CONFIG);
        }
        if (node.isMaster() && node.numslots > 0
                && (n - node.failTime) > (long) nodeTimeout() * FAIL_UNDO_TIME_MULT) {
            s.logNotice("Clear FAIL state for node " + node.name
                    + ": is reachable again and nobody is serving its slots after some time.");
            node.flags &= ~F_FAIL;
            doBeforeSleep(TODO_UPDATE_STATE | TODO_SAVE_CONFIG);
        }
    }

    // ------------------------------------------------------------------
    // handshake
    // ------------------------------------------------------------------
    boolean handshakeInProgress(String ip, int port) {
        for (Node n : nodes.values()) {
            if (!n.inHandshake()) continue;
            if (n.ip.equalsIgnoreCase(ip) && n.port == port) return true;
        }
        return false;
    }

    public static final int HS_OK = 0, HS_EAGAIN = 1, HS_EINVAL = 2;

    public int startHandshake(String ip, int port) {
        String norm;
        if (!isLiteralAddress(ip)) return HS_EINVAL;
        try {
            norm = java.net.InetAddress.getByName(ip).getHostAddress();
        } catch (Exception e) {
            return HS_EINVAL;
        }
        if (port <= 0 || port > 65535 - PORT_INCR) return HS_EINVAL;
        if (handshakeInProgress(norm, port)) return HS_EAGAIN;
        Node n = new Node(null, F_HANDSHAKE | F_MEET);
        n.ip = norm;
        n.port = port;
        addNode(n);
        return HS_OK;
    }

    /** inet_pton() semantics: only numeric addresses are accepted. */
    static boolean isLiteralAddress(String ip) {
        if (ip == null || ip.isEmpty()) return false;
        if (ip.indexOf(':') >= 0) {
            for (int i = 0; i < ip.length(); i++) {
                char c = ip.charAt(i);
                boolean ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                        || (c >= 'A' && c <= 'F') || c == ':' || c == '.' || c == '%';
                if (!ok) return false;
            }
            return true;
        }
        String[] parts = ip.split("\\.", -1);
        if (parts.length != 4) return false;
        for (String p : parts) {
            if (p.isEmpty() || p.length() > 3) return false;
            for (int i = 0; i < p.length(); i++)
                if (p.charAt(i) < '0' || p.charAt(i) > '9') return false;
            if (Integer.parseInt(p) > 255) return false;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // slots
    // ------------------------------------------------------------------
    static boolean bitmapTestBit(byte[] bitmap, int pos) {
        return (bitmap[pos / 8] & (1 << (pos & 7))) != 0;
    }

    static void bitmapSetBit(byte[] bitmap, int pos) { bitmap[pos / 8] |= (byte) (1 << (pos & 7)); }

    static void bitmapClearBit(byte[] bitmap, int pos) { bitmap[pos / 8] &= (byte) ~(1 << (pos & 7)); }

    static boolean nodeSetSlotBit(Node n, int slot) {
        boolean old = bitmapTestBit(n.slots, slot);
        bitmapSetBit(n.slots, slot);
        if (!old) n.numslots++;
        return old;
    }

    static boolean nodeClearSlotBit(Node n, int slot) {
        boolean old = bitmapTestBit(n.slots, slot);
        bitmapClearBit(n.slots, slot);
        if (old) n.numslots--;
        return old;
    }

    public static boolean nodeGetSlotBit(Node n, int slot) { return bitmapTestBit(n.slots, slot); }

    public boolean addSlot(Node n, int slot) {
        if (slots[slot] != null) return false;
        nodeSetSlotBit(n, slot);
        slots[slot] = n;
        return true;
    }

    public boolean delSlot(int slot) {
        Node n = slots[slot];
        if (n == null) return false;
        nodeClearSlotBit(n, slot);
        slots[slot] = null;
        return true;
    }

    public int delNodeSlots(Node node) {
        int deleted = 0;
        for (int j = 0; j < SLOTS; j++) if (nodeGetSlotBit(node, j)) { delSlot(j); deleted++; }
        return deleted;
    }

    public void closeAllSlots() {
        Arrays.fill(migratingSlotsTo, null);
        Arrays.fill(importingSlotsFrom, null);
    }

    // ------------------------------------------------------------------
    // slot -> keys (db.c). Recomputed on demand: the Java keyspace has no
    // slot index and every caller is on a cold path.
    // ------------------------------------------------------------------
    public int countKeysInSlot(int slot) {
        int n = 0;
        for (Key k : s.dbs[0].dict.keySet()) if (keyHashSlot(k.b) == slot) n++;
        return n;
    }

    /** One pass over db0 producing the per-slot key counts. */
    public int[] slotKeyCounts() {
        int[] counts = new int[SLOTS];
        for (Key k : s.dbs[0].dict.keySet()) counts[keyHashSlot(k.b)]++;
        return counts;
    }

    public List<byte[]> getKeysInSlot(int slot, int count) {
        List<byte[]> out = new ArrayList<>();
        if (count <= 0) return out;
        for (Key k : s.dbs[0].dict.keySet()) {
            if (keyHashSlot(k.b) != slot) continue;
            out.add(k.b);
            if (out.size() >= count) break;
        }
        return out;
    }

    public int delKeysInSlot(int slot) {
        List<Key> victims = new ArrayList<>();
        for (Key k : s.dbs[0].dict.keySet()) if (keyHashSlot(k.b) == slot) victims.add(k);
        for (Key k : victims) { s.dbs[0].delete(k); s.signalModifiedKey(0, k); }
        return victims.size();
    }

    // ------------------------------------------------------------------
    // cluster state evaluation (clusterUpdateState)
    // ------------------------------------------------------------------
    static final int MAX_REJOIN_DELAY = 5000;
    static final int MIN_REJOIN_DELAY = 500;
    static final int WRITABLE_DELAY = 2000;

    public void updateState() {
        todoBeforeSleep &= ~TODO_UPDATE_STATE;
        if (firstCallTime == 0) firstCallTime = now();
        if (myself.isMaster() && state == STATE_FAIL && now() - firstCallTime < WRITABLE_DELAY) return;

        int newState = STATE_OK;
        if (s.config.getBool("cluster-require-full-coverage")) {
            for (int j = 0; j < SLOTS; j++) {
                if (slots[j] == null || (slots[j].flags & F_FAIL) != 0) { newState = STATE_FAIL; break; }
            }
        }

        int reachableMasters = 0;
        size = 0;
        for (Node node : nodes.values()) {
            if (node.isMaster() && node.numslots > 0) {
                size++;
                if ((node.flags & (F_FAIL | F_PFAIL)) == 0) reachableMasters++;
            }
        }

        int neededQuorum = (size / 2) + 1;
        if (reachableMasters < neededQuorum) {
            newState = STATE_FAIL;
            amongMinorityTime = now();
        }

        if (newState != state) {
            long rejoinDelay = nodeTimeout();
            if (rejoinDelay > MAX_REJOIN_DELAY) rejoinDelay = MAX_REJOIN_DELAY;
            if (rejoinDelay < MIN_REJOIN_DELAY) rejoinDelay = MIN_REJOIN_DELAY;
            if (newState == STATE_OK && myself.isMaster() && now() - amongMinorityTime < rejoinDelay)
                return;
            s.logWarn("Cluster state changed: " + (newState == STATE_OK ? "ok" : "fail"));
            state = newState;
        }
    }

    /** verifyClusterConfigWithData(): reconcile the loaded keyspace with the slot map. */
    public boolean verifyConfigWithData() {
        if (myself.isSlave()) return true;
        for (int j = 1; j < s.dbs.length; j++) if (s.dbs[j].size() != 0) return false;
        int updateConfig = 0;
        int[] counts = slotKeyCounts();
        for (int j = 0; j < SLOTS; j++) {
            if (counts[j] == 0) continue;
            if (slots[j] == myself || importingSlotsFrom[j] != null) continue;
            updateConfig++;
            if (slots[j] == null) {
                s.logWarn("I have keys for unassigned slot " + j + ". Taking responsibility for it.");
                addSlot(myself, j);
            } else {
                s.logWarn("I have keys for slot " + j + ", but the slot is assigned to another node. "
                        + "Setting it to importing state.");
                importingSlotsFrom[j] = slots[j];
            }
        }
        if (updateConfig > 0) saveConfigOrDie(true);
        return true;
    }

    // ------------------------------------------------------------------
    // role changes
    // ------------------------------------------------------------------
    public void setNodeAsMaster(Node n) {
        if (n.isMaster()) return;
        if (n.slaveof != null) {
            nodeRemoveSlave(n.slaveof, n);
            if (n != myself) n.flags |= F_MIGRATE_TO;
        }
        n.flags &= ~F_SLAVE;
        n.flags |= F_MASTER;
        n.slaveof = null;
        doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE);
    }

    public void setMaster(Node n) {
        if (n == myself) return;
        if (myself.isMaster()) {
            myself.flags &= ~(F_MASTER | F_MIGRATE_TO);
            myself.flags |= F_SLAVE;
            closeAllSlots();
        } else if (myself.slaveof != null) {
            nodeRemoveSlave(myself.slaveof, myself);
        }
        myself.slaveof = n;
        nodeAddSlave(n, myself);
        s.repl.setMaster(n.ip, n.port);
        resetManualFailover();
    }

    /** clusterReset(): forget everything, optionally with a new node ID. */
    public void reset(boolean hard) {
        if (myself.isSlave()) {
            setNodeAsMaster(myself);
            s.repl.unsetMaster();
            for (Db d : s.dbs) d.clear();
        }
        closeAllSlots();
        resetManualFailover();
        for (int j = 0; j < SLOTS; j++) delSlot(j);
        for (Node node : new ArrayList<>(nodes.values())) {
            if (node == myself) continue;
            delNode(node);
        }
        if (hard) {
            currentEpoch = 0;
            lastVoteEpoch = 0;
            myself.configEpoch = 0;
            s.logWarn("configEpoch set to 0 via CLUSTER RESET HARD");
            nodes.remove(myself.name);
            myself.name = Server.randomHex(NAMELEN);
            addNode(myself);
        }
        doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE | TODO_FSYNC_CONFIG);
    }

    // ------------------------------------------------------------------
    // node description (CLUSTER NODES / nodes.conf)
    // ------------------------------------------------------------------
    static final int[] FLAG_VALUES = {F_MYSELF, F_MASTER, F_SLAVE, F_PFAIL, F_FAIL, F_HANDSHAKE, F_NOADDR};
    static final String[] FLAG_NAMES = {"myself,", "master,", "slave,", "fail?,", "fail,",
            "handshake,", "noaddr,"};

    public static String representNodeFlags(int flags) {
        StringBuilder sb = new StringBuilder();
        if ((flags & (F_MYSELF | F_MASTER | F_SLAVE | F_PFAIL | F_FAIL | F_HANDSHAKE | F_NOADDR)) == 0) {
            sb.append("noflags,");
        } else {
            for (int i = 0; i < FLAG_VALUES.length; i++)
                if ((flags & FLAG_VALUES[i]) != 0) sb.append(FLAG_NAMES[i]);
        }
        sb.setLength(sb.length() - 1);
        return sb.toString();
    }

    public String genNodeDescription(Node node) {
        StringBuilder ci = new StringBuilder(128);
        ci.append(node.name).append(' ').append(node.ip).append(':').append(node.port).append(' ');
        ci.append(representNodeFlags(node.flags));
        if (node.slaveof != null) ci.append(' ').append(node.slaveof.name).append(' ');
        else ci.append(" - ");
        ci.append(node.pingSent).append(' ').append(node.pongReceived).append(' ')
          .append(Long.toUnsignedString(node.configEpoch)).append(' ')
          .append((node.link != null || (node.flags & F_MYSELF) != 0) ? "connected" : "disconnected");

        int start = -1;
        for (int j = 0; j < SLOTS; j++) {
            boolean bit = nodeGetSlotBit(node, j);
            if (bit && start == -1) start = j;
            if (start != -1 && (!bit || j == SLOTS - 1)) {
                int end = j;
                if (bit && j == SLOTS - 1) end = j + 1;
                if (start == end - 1) ci.append(' ').append(start);
                else ci.append(' ').append(start).append('-').append(end - 1);
                start = -1;
            }
        }
        if ((node.flags & F_MYSELF) != 0) {
            for (int j = 0; j < SLOTS; j++) {
                if (migratingSlotsTo[j] != null)
                    ci.append(" [").append(j).append("->-").append(migratingSlotsTo[j].name).append(']');
                else if (importingSlotsFrom[j] != null)
                    ci.append(" [").append(j).append("-<-").append(importingSlotsFrom[j].name).append(']');
            }
        }
        return ci.toString();
    }

    public StringBuilder genNodesDescription(int filter) {
        StringBuilder ci = new StringBuilder(1024);
        for (Node node : new ArrayList<>(nodes.values())) {
            if ((node.flags & filter) != 0) continue;
            ci.append(genNodeDescription(node)).append('\n');
        }
        return ci;
    }

    // ------------------------------------------------------------------
    // cluster bus: links
    // ------------------------------------------------------------------
    public void acceptBus(SelectionKey k) {
        ServerSocketChannel ss = (ServerSocketChannel) k.channel();
        /* clusterAcceptHandler(): don't accept cluster connections while loading. */
        if (s.repl.masterHost == null && s.loading) return;
        try {
            SocketChannel ch;
            while ((ch = ss.accept()) != null) {
                ch.configureBlocking(false);
                try { ch.setOption(StandardSocketOptions.TCP_NODELAY, true); } catch (Exception ignored) {}
                Link link = new Link();
                link.ch = ch;
                link.inbound = true;
                link.key = ch.register(s.selector, SelectionKey.OP_READ, link);
            }
        } catch (IOException e) {
            // best effort, like anetTcpAccept()
        }
    }

    public void freeLink(Link link) {
        if (link.freed) return;
        link.freed = true;
        if (link.key != null) { try { link.key.cancel(); } catch (Exception ignored) {} }
        if (link.ch != null) { try { link.ch.close(); } catch (IOException ignored) {} }
        if (link.node != null && link.node.link == link) link.node.link = null;
        link.node = null;
    }

    void sendMessage(Link link, byte[] msg, int len) {
        if (link == null || link.freed) return;
        link.snd.put(msg, 0, len);
        link.updateInterest();
        int t = u16(msg, O_TYPE);
        if (t >= 0 && t < statsSent.length) statsSent[t]++;
    }

    void broadcastMessage(byte[] buf, int len) {
        for (Node node : new ArrayList<>(nodes.values())) {
            if (node.link == null) continue;
            if ((node.flags & (F_MYSELF | F_HANDSHAKE)) != 0) continue;
            sendMessage(node.link, buf, len);
        }
    }

    // ------------------------------------------------------------------
    // binary helpers (network byte order)
    // ------------------------------------------------------------------
    static void p16(byte[] b, int off, int v) { b[off] = (byte) (v >> 8); b[off + 1] = (byte) v; }

    static void p32(byte[] b, int off, long v) {
        b[off] = (byte) (v >> 24); b[off + 1] = (byte) (v >> 16);
        b[off + 2] = (byte) (v >> 8); b[off + 3] = (byte) v;
    }

    static void p64(byte[] b, int off, long v) {
        for (int i = 0; i < 8; i++) b[off + i] = (byte) (v >>> (56 - 8 * i));
    }

    static int u16(byte[] b, int off) { return ((b[off] & 0xff) << 8) | (b[off + 1] & 0xff); }

    static long u32(byte[] b, int off) {
        return ((long) (b[off] & 0xff) << 24) | ((long) (b[off + 1] & 0xff) << 16)
                | ((long) (b[off + 2] & 0xff) << 8) | (b[off + 3] & 0xff);
    }

    static long u64(byte[] b, int off) {
        long v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | (b[off + i] & 0xff);
        return v;
    }

    static void pstr(byte[] b, int off, int max, String v) {
        byte[] x = v.getBytes(StandardCharsets.ISO_8859_1);
        int n = Math.min(max, x.length);
        System.arraycopy(x, 0, b, off, n);
    }

    static String gstr(byte[] b, int off, int max) {
        int n = 0;
        while (n < max && off + n < b.length && b[off + n] != 0) n++;
        return new String(b, off, n, StandardCharsets.ISO_8859_1);
    }

    static String gname(byte[] b, int off) {
        if (off + NAMELEN > b.length) return "";
        return new String(b, off, NAMELEN, StandardCharsets.ISO_8859_1);
    }

    // ------------------------------------------------------------------
    // message building (clusterBuildMessageHdr)
    // ------------------------------------------------------------------
    byte[] buildMessageHdr(int type, int extra) {
        byte[] buf = new byte[Math.max(HDR_LEN + extra, HDR_LEN)];
        Node master = (myself.isSlave() && myself.slaveof != null) ? myself.slaveof : myself;
        buf[0] = 'R'; buf[1] = 'C'; buf[2] = 'm'; buf[3] = 'b';
        p16(buf, O_VER, 0);
        p16(buf, O_TYPE, type);
        pstr(buf, O_SENDER, NAMELEN, myself.name);
        System.arraycopy(master.slots, 0, buf, O_MYSLOTS, SLOTS / 8);
        if (myself.slaveof != null) pstr(buf, O_SLAVEOF, NAMELEN, myself.slaveof.name);
        p16(buf, O_PORT, s.port);
        p16(buf, O_FLAGS, myself.flags & 0xffff);
        buf[O_STATE] = (byte) state;
        p64(buf, O_CUREPOCH, currentEpoch);
        p64(buf, O_CFGEPOCH, master.configEpoch);
        long offset = myself.isSlave() ? replicationGetSlaveOffset() : s.masterReplOffset;
        p64(buf, O_OFFSET, offset);
        if (myself.isMaster() && mfEnd != 0) buf[O_MFLAGS] |= MFLAG0_PAUSED;
        int totlen = 0;
        if (type == MSG_FAIL) totlen = HDR_LEN + FAIL_LEN;
        else if (type == MSG_UPDATE) totlen = HDR_LEN + UPDATE_LEN;
        p32(buf, O_TOTLEN, totlen);
        return buf;
    }

    long replicationGetSlaveOffset() {
        if (s.repl.masterHost == null) return 0;
        long off = s.repl.replOffset;
        return off < 0 ? 0 : off;
    }

    // ------------------------------------------------------------------
    // PING / PONG / MEET (clusterSendPing)
    // ------------------------------------------------------------------
    public void sendPing(Link link, int type) {
        if (link == null || link.freed) return;
        int gossipcount = 0;
        int freshnodes = nodes.size() - 2;
        int wanted = nodes.size() / 10;
        if (wanted < 3) wanted = 3;
        if (wanted > freshnodes) wanted = freshnodes;
        if (wanted < 0) wanted = 0;

        if (link.node != null && type == MSG_PING) link.node.pingSent = now();
        byte[] buf = buildMessageHdr(type, GOSSIP_LEN * wanted);

        List<Node> all = new ArrayList<>(nodes.values());
        int maxiterations = wanted * 3;
        String[] added = new String[Math.max(wanted, 1)];
        while (freshnodes > 0 && gossipcount < wanted && maxiterations-- > 0) {
            Node t = all.get(rnd.nextInt(all.size()));
            if (t == myself) continue;
            if (maxiterations > wanted * 2 && (t.flags & (F_PFAIL | F_FAIL)) == 0) continue;
            if ((t.flags & (F_HANDSHAKE | F_NOADDR)) != 0 || (t.link == null && t.numslots == 0)) {
                freshnodes--;
                continue;
            }
            boolean dup = false;
            for (int j = 0; j < gossipcount; j++) if (added[j].equals(t.name)) { dup = true; break; }
            if (dup) continue;
            freshnodes--;
            int go = O_DATA + gossipcount * GOSSIP_LEN;
            pstr(buf, go + G_NAME, NAMELEN, t.name);
            p32(buf, go + G_PING, (int) t.pingSent);
            p32(buf, go + G_PONG, (int) t.pongReceived);
            pstr(buf, go + G_IP, IP_STR_LEN, t.ip);
            p16(buf, go + G_PORT, t.port);
            p16(buf, go + G_FLAGS, t.flags & 0xffff);
            added[gossipcount] = t.name;
            gossipcount++;
        }
        int totlen = HDR_LEN + GOSSIP_LEN * gossipcount;
        p16(buf, O_COUNT, gossipcount);
        p32(buf, O_TOTLEN, totlen);
        sendMessage(link, buf, totlen);
    }

    static final int BROADCAST_ALL = 0;
    static final int BROADCAST_LOCAL_SLAVES = 1;

    void broadcastPong(int target) {
        for (Node node : new ArrayList<>(nodes.values())) {
            if (node.link == null) continue;
            if (node == myself || node.inHandshake()) continue;
            if (target == BROADCAST_LOCAL_SLAVES) {
                boolean localSlave = node.isSlave() && node.slaveof != null
                        && (node.slaveof == myself || node.slaveof == myself.slaveof);
                if (!localSlave) continue;
            }
            sendPing(node.link, MSG_PONG);
        }
    }

    void sendFail(String nodename) {
        byte[] buf = buildMessageHdr(MSG_FAIL, FAIL_LEN);
        pstr(buf, O_DATA, NAMELEN, nodename);
        broadcastMessage(buf, HDR_LEN + FAIL_LEN);
    }

    void sendUpdate(Link link, Node node) {
        if (link == null) return;
        byte[] buf = buildMessageHdr(MSG_UPDATE, UPDATE_LEN);
        pstr(buf, O_DATA + U_NAME, NAMELEN, node.name);
        p64(buf, O_DATA + U_EPOCH, node.configEpoch);
        System.arraycopy(node.slots, 0, buf, O_DATA + U_SLOTS, SLOTS / 8);
        sendMessage(link, buf, HDR_LEN + UPDATE_LEN);
    }

    public void propagatePublish(byte[] channel, byte[] message) {
        int totlen = HDR_LEN + PUBLISH_LEN - 8 + channel.length + message.length;
        byte[] buf = buildMessageHdr(MSG_PUBLISH, totlen - HDR_LEN);
        p32(buf, O_DATA, channel.length);
        p32(buf, O_DATA + 4, message.length);
        System.arraycopy(channel, 0, buf, O_DATA + 8, channel.length);
        System.arraycopy(message, 0, buf, O_DATA + 8 + channel.length, message.length);
        p32(buf, O_TOTLEN, totlen);
        broadcastMessage(buf, totlen);
    }

    void requestFailoverAuth() {
        byte[] buf = buildMessageHdr(MSG_FAILOVER_AUTH_REQUEST, 0);
        if (mfEnd != 0) buf[O_MFLAGS] |= MFLAG0_FORCEACK;
        p32(buf, O_TOTLEN, HDR_LEN);
        broadcastMessage(buf, HDR_LEN);
    }

    void sendFailoverAuth(Node node) {
        if (node.link == null) return;
        byte[] buf = buildMessageHdr(MSG_FAILOVER_AUTH_ACK, 0);
        p32(buf, O_TOTLEN, HDR_LEN);
        sendMessage(node.link, buf, HDR_LEN);
    }

    void sendMFStart(Node node) {
        if (node.link == null) return;
        byte[] buf = buildMessageHdr(MSG_MFSTART, 0);
        p32(buf, O_TOTLEN, HDR_LEN);
        sendMessage(node.link, buf, HDR_LEN);
    }

    // ------------------------------------------------------------------
    // gossip processing
    // ------------------------------------------------------------------
    void processGossipSection(byte[] hdr, Link link) {
        int count = u16(hdr, O_COUNT);
        Node sender = link.node != null ? link.node : lookupNode(gname(hdr, O_SENDER));
        for (int i = 0; i < count; i++) {
            int g = O_DATA + i * GOSSIP_LEN;
            if (g + GOSSIP_LEN > hdr.length) break;
            int flags = u16(hdr, g + G_FLAGS);
            String gn = gname(hdr, g + G_NAME);
            String gip = gstr(hdr, g + G_IP, IP_STR_LEN);
            int gport = u16(hdr, g + G_PORT);

            Node node = lookupNode(gn);
            if (node != null) {
                if (sender != null && sender.isMaster() && node != myself) {
                    if ((flags & (F_FAIL | F_PFAIL)) != 0) {
                        if (nodeAddFailureReport(node, sender)) {
                            s.logNotice("Node " + sender.name + " reported node " + node.name
                                    + " as not reachable.");
                        }
                        markNodeAsFailingIfNeeded(node);
                    } else {
                        if (nodeDelFailureReport(node, sender)) {
                            s.logNotice("Node " + sender.name + " reported node " + node.name
                                    + " is back online.");
                        }
                    }
                }
                if ((node.flags & (F_FAIL | F_PFAIL)) != 0
                        && (!node.ip.equalsIgnoreCase(gip) || node.port != gport)) {
                    startHandshake(gip, gport);
                }
            } else {
                if (sender != null && (flags & F_NOADDR) == 0 && !blacklistExists(gn)) {
                    startHandshake(gip, gport);
                }
            }
        }
    }

    static String peerIp(Link link) {
        try {
            InetSocketAddress a = (InetSocketAddress) link.ch.getRemoteAddress();
            return a.getAddress().getHostAddress();
        } catch (Exception e) {
            return "";
        }
    }

    static String sockIp(Link link) {
        try {
            InetSocketAddress a = (InetSocketAddress) link.ch.getLocalAddress();
            return a.getAddress().getHostAddress();
        } catch (Exception e) {
            return "";
        }
    }

    boolean nodeUpdateAddressIfNeeded(Node node, Link link, int port) {
        if (link == node.link) return false;
        String ip = peerIp(link);
        if (node.port == port && ip.equals(node.ip)) return false;
        node.ip = ip;
        node.port = port;
        if (node.link != null) freeLink(node.link);
        node.flags &= ~F_NOADDR;
        s.logWarn("Address updated for node " + node.name + ", now " + node.ip + ":" + node.port);
        if (myself.isSlave() && myself.slaveof == node) s.repl.setMaster(node.ip, node.port);
        return true;
    }

    // ------------------------------------------------------------------
    // clusterUpdateSlotsConfigWith
    // ------------------------------------------------------------------
    void updateSlotsConfigWith(Node sender, long senderConfigEpoch, byte[] bitmap, int bitmapOff) {
        Node curmaster = myself.isMaster() ? myself : myself.slaveof;
        Node newmaster = null;
        int[] dirtySlots = new int[SLOTS];
        int dirtyCount = 0;
        int[] keyCounts = null;

        if (sender == myself) {
            s.logWarn("Discarding UPDATE message about myself.");
            return;
        }
        for (int j = 0; j < SLOTS; j++) {
            if (!bitmapTestBit2(bitmap, bitmapOff, j)) continue;
            if (slots[j] == sender) continue;
            if (importingSlotsFrom[j] != null) continue;
            if (slots[j] == null || Long.compareUnsigned(slots[j].configEpoch, senderConfigEpoch) < 0) {
                if (slots[j] == myself) {
                    if (keyCounts == null) keyCounts = slotKeyCounts();
                    if (keyCounts[j] != 0) dirtySlots[dirtyCount++] = j;
                }
                if (slots[j] == curmaster) newmaster = sender;
                delSlot(j);
                addSlot(sender, j);
                doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE | TODO_FSYNC_CONFIG);
            }
        }
        if (newmaster != null && curmaster != null && curmaster.numslots == 0) {
            s.logWarn("Configuration change detected. Reconfiguring myself as a replica of "
                    + sender.name);
            setMaster(sender);
            doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE | TODO_FSYNC_CONFIG);
        } else if (dirtyCount > 0) {
            for (int j = 0; j < dirtyCount; j++) delKeysInSlot(dirtySlots[j]);
        }
    }

    static boolean bitmapTestBit2(byte[] b, int off, int pos) {
        return (b[off + pos / 8] & (1 << (pos & 7))) != 0;
    }

    // ------------------------------------------------------------------
    // packet processing (clusterProcessPacket)
    // ------------------------------------------------------------------
    boolean processPacket(Link link, byte[] hdr) {
        long totlen = u32(hdr, O_TOTLEN);
        int type = u16(hdr, O_TYPE);
        if (type >= 0 && type < statsReceived.length) statsReceived[type]++;

        if (totlen < 16) return true;
        if (totlen > hdr.length) return true;
        if (u16(hdr, O_VER) != 0) return true;

        int flags = u16(hdr, O_FLAGS);
        long senderCurrentEpoch = 0, senderConfigEpoch = 0;

        if (type == MSG_PING || type == MSG_PONG || type == MSG_MEET) {
            int count = u16(hdr, O_COUNT);
            long explen = HDR_LEN + (long) GOSSIP_LEN * count;
            if (totlen != explen) return true;
        } else if (type == MSG_FAIL) {
            if (totlen != HDR_LEN + FAIL_LEN) return true;
        } else if (type == MSG_PUBLISH) {
            long explen = HDR_LEN + PUBLISH_LEN - 8 + u32(hdr, O_DATA) + u32(hdr, O_DATA + 4);
            if (totlen != explen) return true;
        } else if (type == MSG_FAILOVER_AUTH_REQUEST || type == MSG_FAILOVER_AUTH_ACK
                || type == MSG_MFSTART) {
            if (totlen != HDR_LEN) return true;
        } else if (type == MSG_UPDATE) {
            if (totlen != HDR_LEN + UPDATE_LEN) return true;
        }

        Node sender = lookupNode(gname(hdr, O_SENDER));
        if (sender != null && !sender.inHandshake()) {
            senderCurrentEpoch = u64(hdr, O_CUREPOCH);
            senderConfigEpoch = u64(hdr, O_CFGEPOCH);
            if (Long.compareUnsigned(senderCurrentEpoch, currentEpoch) > 0)
                currentEpoch = senderCurrentEpoch;
            if (Long.compareUnsigned(senderConfigEpoch, sender.configEpoch) > 0) {
                sender.configEpoch = senderConfigEpoch;
                doBeforeSleep(TODO_SAVE_CONFIG | TODO_FSYNC_CONFIG);
            }
            sender.replOffset = u64(hdr, O_OFFSET);
            sender.replOffsetTime = now();
            if (mfEnd != 0 && myself.isSlave() && myself.slaveof == sender
                    && (hdr[O_MFLAGS] & MFLAG0_PAUSED) != 0 && mfMasterOffset == 0) {
                mfMasterOffset = sender.replOffset;
                s.logWarn("Received replication offset for paused master manual failover: "
                        + mfMasterOffset);
            }
        }

        if (type == MSG_PING || type == MSG_MEET) {
            if (type == MSG_MEET || myself.ip.isEmpty()) {
                String ip = sockIp(link);
                if (!ip.isEmpty() && !ip.equals(myself.ip)) {
                    myself.ip = ip;
                    s.logWarn("IP address for this node updated to " + myself.ip);
                    doBeforeSleep(TODO_SAVE_CONFIG);
                }
            }
            if (sender == null && type == MSG_MEET) {
                Node node = new Node(null, F_HANDSHAKE);
                node.ip = peerIp(link);
                node.port = u16(hdr, O_PORT);
                addNode(node);
                doBeforeSleep(TODO_SAVE_CONFIG);
            }
            if (sender == null && type == MSG_MEET) processGossipSection(hdr, link);
            sendPing(link, MSG_PONG);
        }

        if (type == MSG_PING || type == MSG_PONG || type == MSG_MEET) {
            if (link.node != null) {
                if (link.node.inHandshake()) {
                    if (sender != null) {
                        if (nodeUpdateAddressIfNeeded(sender, link, u16(hdr, O_PORT)))
                            doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE);
                        delNode(link.node);
                        return false;
                    }
                    renameNode(link.node, gname(hdr, O_SENDER));
                    link.node.flags &= ~F_HANDSHAKE;
                    link.node.flags |= flags & (F_MASTER | F_SLAVE);
                    doBeforeSleep(TODO_SAVE_CONFIG);
                } else if (!link.node.name.equals(gname(hdr, O_SENDER))) {
                    link.node.flags |= F_NOADDR;
                    link.node.ip = "";
                    link.node.port = 0;
                    freeLink(link);
                    doBeforeSleep(TODO_SAVE_CONFIG);
                    return false;
                }
            }

            if (sender != null && type == MSG_PING && !sender.inHandshake()
                    && nodeUpdateAddressIfNeeded(sender, link, u16(hdr, O_PORT))) {
                doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE);
            }

            if (link.node != null && type == MSG_PONG) {
                link.node.pongReceived = now();
                link.node.pingSent = 0;
                if (link.node.timedOut()) {
                    link.node.flags &= ~F_PFAIL;
                    doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE);
                } else if (link.node.failed()) {
                    clearNodeFailureIfNeeded(link.node);
                }
            }

            if (sender != null) {
                if (isNullName(hdr, O_SLAVEOF)) {
                    setNodeAsMaster(sender);
                } else {
                    Node master = lookupNode(gname(hdr, O_SLAVEOF));
                    if (sender.isMaster()) {
                        delNodeSlots(sender);
                        sender.flags &= ~(F_MASTER | F_MIGRATE_TO);
                        sender.flags |= F_SLAVE;
                        doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE);
                    }
                    if (master != null && sender.slaveof != master) {
                        if (sender.slaveof != null) nodeRemoveSlave(sender.slaveof, sender);
                        nodeAddSlave(master, sender);
                        sender.slaveof = master;
                        doBeforeSleep(TODO_SAVE_CONFIG);
                    }
                }
            }

            Node senderMaster = null;
            boolean dirtySlots = false;
            if (sender != null) {
                senderMaster = sender.isMaster() ? sender : sender.slaveof;
                if (senderMaster != null) dirtySlots = !equalSlots(senderMaster.slots, hdr, O_MYSLOTS);
            }

            if (sender != null && sender.isMaster() && dirtySlots)
                updateSlotsConfigWith(sender, senderConfigEpoch, hdr, O_MYSLOTS);

            if (sender != null && dirtySlots) {
                for (int j = 0; j < SLOTS; j++) {
                    if (bitmapTestBit2(hdr, O_MYSLOTS, j)) {
                        if (slots[j] == sender || slots[j] == null) continue;
                        if (Long.compareUnsigned(slots[j].configEpoch, senderConfigEpoch) > 0) {
                            s.logNotice("Node " + sender.name + " has old slots configuration, "
                                    + "sending an UPDATE message about " + slots[j].name);
                            sendUpdate(sender.link, slots[j]);
                            break;
                        }
                    }
                }
            }

            if (sender != null && myself.isMaster() && sender.isMaster()
                    && senderConfigEpoch == myself.configEpoch) {
                handleConfigEpochCollision(sender);
            }

            if (sender != null) processGossipSection(hdr, link);
        } else if (type == MSG_FAIL) {
            if (sender != null) {
                String about = gname(hdr, O_DATA);
                Node failing = lookupNode(about);
                if (failing != null && (failing.flags & (F_FAIL | F_MYSELF)) == 0) {
                    s.logNotice("FAIL message received from " + gname(hdr, O_SENDER)
                            + " about " + about);
                    failing.flags |= F_FAIL;
                    failing.failTime = now();
                    failing.flags &= ~F_PFAIL;
                    doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE);
                }
            } else {
                s.logNotice("Ignoring FAIL message from unknown node " + gname(hdr, O_SENDER)
                        + " about " + gname(hdr, O_DATA));
            }
        } else if (type == MSG_PUBLISH) {
            if (!s.pubsub.channels.isEmpty() || !s.pubsub.patterns.isEmpty()) {
                int channelLen = (int) u32(hdr, O_DATA);
                int messageLen = (int) u32(hdr, O_DATA + 4);
                byte[] channel = Arrays.copyOfRange(hdr, O_DATA + 8, O_DATA + 8 + channelLen);
                byte[] message = Arrays.copyOfRange(hdr, O_DATA + 8 + channelLen,
                        O_DATA + 8 + channelLen + messageLen);
                s.pubsub.publishMessage(s, channel, message, false);
            }
        } else if (type == MSG_FAILOVER_AUTH_REQUEST) {
            if (sender == null) return true;
            sendFailoverAuthIfNeeded(sender, hdr);
        } else if (type == MSG_FAILOVER_AUTH_ACK) {
            if (sender == null) return true;
            if (sender.isMaster() && sender.numslots > 0
                    && Long.compareUnsigned(senderCurrentEpoch, failoverAuthEpoch) >= 0) {
                failoverAuthCount++;
                doBeforeSleep(TODO_HANDLE_FAILOVER);
            }
        } else if (type == MSG_MFSTART) {
            if (sender == null || sender.slaveof != myself) return true;
            resetManualFailover();
            mfEnd = now() + MF_TIMEOUT;
            mfSlave = sender;
            pauseClients(now() + (long) MF_TIMEOUT * MF_PAUSE_MULT);
            s.logWarn("Manual failover requested by slave " + sender.name + ".");
        } else if (type == MSG_UPDATE) {
            long reportedConfigEpoch = u64(hdr, O_DATA + U_EPOCH);
            if (sender == null) return true;
            Node n = lookupNode(gname(hdr, O_DATA + U_NAME));
            if (n == null) return true;
            if (Long.compareUnsigned(n.configEpoch, reportedConfigEpoch) >= 0) return true;
            if (n.isSlave()) setNodeAsMaster(n);
            n.configEpoch = reportedConfigEpoch;
            doBeforeSleep(TODO_SAVE_CONFIG | TODO_FSYNC_CONFIG);
            updateSlotsConfigWith(n, reportedConfigEpoch, hdr, O_DATA + U_SLOTS);
        } else {
            s.logWarn("Received unknown packet type: " + type);
        }
        return true;
    }

    static boolean isNullName(byte[] b, int off) {
        for (int i = 0; i < NAMELEN; i++) if (b[off + i] != 0) return false;
        return true;
    }

    static boolean equalSlots(byte[] a, byte[] b, int off) {
        for (int i = 0; i < SLOTS / 8; i++) if (a[i] != b[off + i]) return false;
        return true;
    }

    // ------------------------------------------------------------------
    // voting
    // ------------------------------------------------------------------
    void sendFailoverAuthIfNeeded(Node node, byte[] request) {
        Node master = node.slaveof;
        long requestCurrentEpoch = u64(request, O_CUREPOCH);
        long requestConfigEpoch = u64(request, O_CFGEPOCH);
        boolean forceAck = (request[O_MFLAGS] & MFLAG0_FORCEACK) != 0;

        if (myself.isSlave() || myself.numslots == 0) return;
        if (Long.compareUnsigned(requestCurrentEpoch, currentEpoch) < 0) {
            s.logWarn("Failover auth denied to " + node.name + ": reqEpoch ("
                    + Long.toUnsignedString(requestCurrentEpoch) + ") < curEpoch("
                    + Long.toUnsignedString(currentEpoch) + ")");
            return;
        }
        if (lastVoteEpoch == currentEpoch) {
            s.logWarn("Failover auth denied to " + node.name + ": already voted for epoch "
                    + Long.toUnsignedString(currentEpoch));
            return;
        }
        if (node.isMaster() || master == null || (!master.failed() && !forceAck)) {
            if (node.isMaster())
                s.logWarn("Failover auth denied to " + node.name + ": it is a master node");
            else if (master == null)
                s.logWarn("Failover auth denied to " + node.name + ": I don't know its master");
            else
                s.logWarn("Failover auth denied to " + node.name + ": its master is up");
            return;
        }
        if (now() - master.votedTime < (long) nodeTimeout() * 2) {
            s.logWarn("Failover auth denied to " + node.name
                    + ": can't vote about this master before "
                    + ((long) nodeTimeout() * 2 - (now() - master.votedTime)) + " milliseconds");
            return;
        }
        for (int j = 0; j < SLOTS; j++) {
            if (!bitmapTestBit2(request, O_MYSLOTS, j)) continue;
            if (slots[j] == null || Long.compareUnsigned(slots[j].configEpoch, requestConfigEpoch) <= 0)
                continue;
            s.logWarn("Failover auth denied to " + node.name + ": slot " + j + " epoch ("
                    + Long.toUnsignedString(slots[j].configEpoch) + ") > reqEpoch ("
                    + Long.toUnsignedString(requestConfigEpoch) + ")");
            return;
        }
        sendFailoverAuth(node);
        lastVoteEpoch = currentEpoch;
        master.votedTime = now();
        s.logWarn("Failover auth granted to " + node.name + " for epoch "
                + Long.toUnsignedString(currentEpoch));
    }

    int getSlaveRank() {
        Node master = myself.slaveof;
        if (master == null) return 0;
        long myoffset = replicationGetSlaveOffset();
        int rank = 0;
        for (Node sl : master.slaves) if (sl != myself && sl.replOffset > myoffset) rank++;
        return rank;
    }

    void logCantFailover(int reason) {
        long nologFailTime = nodeTimeout() + 5000;
        if (reason == cantFailoverReason && now() / 1000 - lastLogTime < CANT_RELOG_PERIOD) return;
        cantFailoverReason = reason;
        if (myself.slaveof != null && myself.slaveof.failed()
                && (now() - myself.slaveof.failTime) < nologFailTime) return;
        String msg;
        switch (reason) {
            case CANT_DATA_AGE:
                msg = "Disconnected from master for longer than allowed. "
                        + "Please check the 'cluster-slave-validity-factor' configuration option.";
                break;
            case CANT_WAITING_DELAY:
                msg = "Waiting the delay before I can start a new failover."; break;
            case CANT_EXPIRED: msg = "Failover attempt expired."; break;
            case CANT_WAITING_VOTES: msg = "Waiting for votes, but majority still not reached."; break;
            default: msg = "Unknown reason code."; break;
        }
        lastLogTime = now() / 1000;
        s.logWarn("Currently unable to failover: " + msg);
    }

    /** clusterFailoverReplaceYourMaster(): grab the master's slots and announce. */
    public void failoverReplaceYourMaster() {
        Node oldmaster = myself.slaveof;
        if (myself.isMaster() || oldmaster == null) return;
        setNodeAsMaster(myself);
        s.repl.unsetMaster();
        for (int j = 0; j < SLOTS; j++) {
            if (nodeGetSlotBit(oldmaster, j)) { delSlot(j); addSlot(myself, j); }
        }
        updateState();
        saveConfigOrDie(true);
        broadcastPong(BROADCAST_ALL);
        resetManualFailover();
    }

    void handleSlaveFailover() {
        long authAge = now() - failoverAuthTime;
        int neededQuorum = (size / 2) + 1;
        boolean manualFailover = mfEnd != 0 && mfCanStart;
        todoBeforeSleep &= ~TODO_HANDLE_FAILOVER;

        long authTimeout = (long) nodeTimeout() * 2;
        if (authTimeout < 2000) authTimeout = 2000;
        long authRetryTime = authTimeout * 2;

        if (myself.isMaster() || myself.slaveof == null
                || (!myself.slaveof.failed() && !manualFailover)
                || myself.slaveof.numslots == 0) {
            cantFailoverReason = CANT_NONE;
            return;
        }

        long dataAge;
        if (s.repl.state == Repl.ST_CONNECTED && s.repl.masterClient != null) {
            dataAge = now() - s.repl.masterClient.lastInteraction;
        } else {
            dataAge = s.repl.downSince == 0 ? 0 : now() - s.repl.downSince;
        }
        if (dataAge > nodeTimeout()) dataAge -= nodeTimeout();

        long validityFactor = s.config.getLong("cluster-slave-validity-factor");
        if (validityFactor != 0
                && dataAge > (s.config.getLong("repl-ping-replica-period") * 1000
                              + (long) nodeTimeout() * validityFactor)) {
            if (!manualFailover) {
                logCantFailover(CANT_DATA_AGE);
                return;
            }
        }

        if (authAge > authRetryTime) {
            failoverAuthTime = now() + 500 + rnd.nextInt(500);
            failoverAuthCount = 0;
            failoverAuthSent = false;
            failoverAuthRank = getSlaveRank();
            failoverAuthTime += (long) failoverAuthRank * 1000;
            if (mfEnd != 0) {
                failoverAuthTime = now();
                failoverAuthRank = 0;
            }
            s.logWarn("Start of election delayed for " + (failoverAuthTime - now())
                    + " milliseconds (rank #" + failoverAuthRank + ", offset "
                    + replicationGetSlaveOffset() + ").");
            broadcastPong(BROADCAST_LOCAL_SLAVES);
            return;
        }

        if (!failoverAuthSent && mfEnd == 0) {
            int newrank = getSlaveRank();
            if (newrank > failoverAuthRank) {
                long addedDelay = (long) (newrank - failoverAuthRank) * 1000;
                failoverAuthTime += addedDelay;
                failoverAuthRank = newrank;
                s.logWarn("Slave rank updated to #" + newrank + ", added " + addedDelay
                        + " milliseconds of delay.");
            }
        }

        if (now() < failoverAuthTime) { logCantFailover(CANT_WAITING_DELAY); return; }
        if (authAge > authTimeout) { logCantFailover(CANT_EXPIRED); return; }

        if (!failoverAuthSent) {
            currentEpoch++;
            failoverAuthEpoch = currentEpoch;
            s.logWarn("Starting a failover election for epoch "
                    + Long.toUnsignedString(currentEpoch) + ".");
            requestFailoverAuth();
            failoverAuthSent = true;
            doBeforeSleep(TODO_SAVE_CONFIG | TODO_UPDATE_STATE | TODO_FSYNC_CONFIG);
            return;
        }

        if (failoverAuthCount >= neededQuorum) {
            s.logWarn("Failover election won: I'm the new master.");
            if (Long.compareUnsigned(myself.configEpoch, failoverAuthEpoch) < 0) {
                myself.configEpoch = failoverAuthEpoch;
                s.logWarn("configEpoch set to " + Long.toUnsignedString(myself.configEpoch)
                        + " after successful failover");
            }
            failoverReplaceYourMaster();
        } else {
            logCantFailover(CANT_WAITING_VOTES);
        }
    }

    // ------------------------------------------------------------------
    // replica migration
    // ------------------------------------------------------------------
    void handleSlaveMigration(int maxSlaves) {
        if (state != STATE_OK) return;
        Node mymaster = myself.slaveof;
        if (mymaster == null) return;
        int okslaves = 0;
        for (Node sl : mymaster.slaves) if (!sl.failed() && !sl.timedOut()) okslaves++;
        if (okslaves <= s.config.getLong("cluster-migration-barrier")) return;

        Node target = null, candidate = myself;
        for (Node node : new ArrayList<>(nodes.values())) {
            int nodeOkSlaves = 0;
            boolean isOrphaned = true;
            if (node.isSlave() || node.failed()) isOrphaned = false;
            if ((node.flags & F_MIGRATE_TO) == 0) isOrphaned = false;
            if (node.isMaster()) nodeOkSlaves = countNonFailingSlaves(node);
            if (nodeOkSlaves > 0) isOrphaned = false;
            if (isOrphaned) {
                if (target == null && node.numslots > 0) target = node;
                if (node.orphanedTime == 0) node.orphanedTime = now();
            } else {
                node.orphanedTime = 0;
            }
            if (nodeOkSlaves == maxSlaves) {
                for (Node sl : node.slaves)
                    if (sl.name.compareTo(candidate.name) < 0) candidate = sl;
            }
        }
        if (target != null && candidate == myself
                && (now() - target.orphanedTime) > SLAVE_MIGRATION_DELAY) {
            s.logWarn("Migrating to orphaned master " + target.name);
            setMaster(target);
        }
    }

    // ------------------------------------------------------------------
    // manual failover
    // ------------------------------------------------------------------
    public void resetManualFailover() {
        if (mfEnd != 0 && clientsArePausedNow()) unpauseClients(s);
        mfEnd = 0;
        mfCanStart = false;
        mfSlave = null;
        mfMasterOffset = 0;
    }

    void manualFailoverCheckTimeout() {
        if (mfEnd != 0 && mfEnd < now()) {
            s.logWarn("Manual failover timed out.");
            resetManualFailover();
        }
    }

    void handleManualFailover() {
        if (mfEnd == 0) return;
        if (mfCanStart) return;
        if (mfMasterOffset == 0) return;
        if (mfMasterOffset == replicationGetSlaveOffset()) {
            mfCanStart = true;
            s.logWarn("All master replication stream processed, manual failover can start.");
        }
    }

    // ------------------------------------------------------------------
    // client pause (networking.c)
    // ------------------------------------------------------------------
    public static void pauseClients(long end) {
        if (clientsPauseEnd == 0 || end > clientsPauseEnd) clientsPauseEnd = end;
    }

    static boolean clientsArePausedNow() {
        return clientsPauseEnd != 0 && clientsPauseEnd >= System.currentTimeMillis();
    }

    /** Called from Server.processInput(): is this client currently frozen? */
    public static boolean pausedNow(Client c) {
        return !c.isReplica && clientsArePausedNow();
    }

    /**
     * networking.c clientsArePaused() side effect: end the pause and push every
     * client that has a half-read command back through the input path, so that
     * requests received while frozen are answered without waiting for more data.
     */
    private static boolean unpausing = false;

    public static void unpauseClients(Server s) {
        if (clientsPauseEnd == 0 || unpausing) return;
        clientsPauseEnd = 0;
        unpausing = true;
        try {
            for (Client c : new ArrayList<>(s.clients.values())) {
                if (c.isReplica || c.blocked) continue;
                if (c.hasPendingInput()) s.processInput(c);
            }
        } finally {
            unpausing = false;
        }
    }

    /** Called from Server.cron(): release the pause once it expired. */
    public static void pauseCron(Server s) {
        if (clientsPauseEnd == 0) return;
        if (clientsPauseEnd >= System.currentTimeMillis()) return;
        unpauseClients(s);
    }

    // ------------------------------------------------------------------
    // cron (clusterCron) -- called every 100 ms
    // ------------------------------------------------------------------
    public void cron() {
        boolean updateStateFlag = false;
        long nowMs = now();
        iteration++;
        redirectBlockedClients();

        long handshakeTimeout = nodeTimeout();
        if (handshakeTimeout < 1000) handshakeTimeout = 1000;

        for (Node node : new ArrayList<>(nodes.values())) {
            if ((node.flags & (F_MYSELF | F_NOADDR)) != 0) continue;
            if (node.inHandshake() && nowMs - node.ctime > handshakeTimeout) {
                delNode(node);
                continue;
            }
            if (node.link == null) {
                Link link = connectTo(node);
                if (link == null) {
                    if (node.pingSent == 0) node.pingSent = now();
                    continue;
                }
                node.link = link;
                long oldPingSent = node.pingSent;
                sendPing(link, (node.flags & F_MEET) != 0 ? MSG_MEET : MSG_PING);
                if (oldPingSent != 0) node.pingSent = oldPingSent;
                node.flags &= ~F_MEET;
            }
        }

        if (iteration % 10 == 0) {
            Node minPongNode = null;
            long minPong = 0;
            List<Node> all = new ArrayList<>(nodes.values());
            for (int j = 0; j < 5 && !all.isEmpty(); j++) {
                Node t = all.get(rnd.nextInt(all.size()));
                if (t.link == null || t.pingSent != 0) continue;
                if ((t.flags & (F_MYSELF | F_HANDSHAKE)) != 0) continue;
                if (minPongNode == null || minPong > t.pongReceived) {
                    minPongNode = t;
                    minPong = t.pongReceived;
                }
            }
            if (minPongNode != null) sendPing(minPongNode.link, MSG_PING);
        }

        int orphanedMasters = 0, maxSlaves = 0, thisSlaves = 0;
        for (Node node : new ArrayList<>(nodes.values())) {
            nowMs = now();
            if ((node.flags & (F_MYSELF | F_NOADDR | F_HANDSHAKE)) != 0) continue;

            if (myself.isSlave() && node.isMaster() && !node.failed()) {
                int okslaves = countNonFailingSlaves(node);
                if (okslaves == 0 && node.numslots > 0 && (node.flags & F_MIGRATE_TO) != 0)
                    orphanedMasters++;
                if (okslaves > maxSlaves) maxSlaves = okslaves;
                if (myself.slaveof == node) thisSlaves = okslaves;
            }

            if (node.link != null && nowMs - node.link.ctime > nodeTimeout()
                    && node.pingSent != 0 && node.pongReceived < node.pingSent
                    && nowMs - node.pingSent > nodeTimeout() / 2) {
                freeLink(node.link);
            }

            if (node.link != null && node.pingSent == 0
                    && (nowMs - node.pongReceived) > nodeTimeout() / 2) {
                sendPing(node.link, MSG_PING);
                continue;
            }

            if (mfEnd != 0 && myself.isMaster() && mfSlave == node && node.link != null) {
                sendPing(node.link, MSG_PING);
                continue;
            }

            if (node.pingSent == 0) continue;
            long delay = nowMs - node.pingSent;
            if (delay > nodeTimeout()) {
                if ((node.flags & (F_PFAIL | F_FAIL)) == 0) {
                    s.logNotice("*** NODE " + node.name + " possibly failing");
                    node.flags |= F_PFAIL;
                    updateStateFlag = true;
                }
            }
        }

        if (myself.isSlave() && s.repl.masterHost == null && myself.slaveof != null
                && myself.slaveof.hasAddr()) {
            s.repl.setMaster(myself.slaveof.ip, myself.slaveof.port);
        }

        manualFailoverCheckTimeout();

        if (myself.isSlave()) {
            handleManualFailover();
            handleSlaveFailover();
            if (orphanedMasters > 0 && maxSlaves >= 2 && thisSlaves == maxSlaves)
                handleSlaveMigration(maxSlaves);
        }

        if (updateStateFlag || state == STATE_FAIL) updateState();
    }

    /**
     * cluster.c clusterRedirectBlockedClientIfNeeded(): a client blocked on a
     * key of a hash slot this node no longer serves would wait forever, so it
     * is woken with the redirection error it would have got for the command.
     */
    void redirectBlockedClients() {
        for (Client c : new ArrayList<>(s.clients.values())) {
            if (!c.blocked || c.blockKeys.isEmpty()) continue;
            if (state == STATE_FAIL) {
                redirectClient(c, null, 0, REDIR_DOWN_STATE);
                s.unblockClient(c);
                s.updateInterest(c);
                continue;
            }
            for (Key k : new ArrayList<>(c.blockKeys)) {
                int slot = keyHashSlot(k.b);
                Node node = slots[slot];
                if (node == myself || importingSlotsFrom[slot] != null) continue;
                if (node == null) redirectClient(c, null, 0, REDIR_DOWN_UNBOUND);
                else redirectClient(c, node, slot, REDIR_MOVED);
                s.unblockClient(c);
                s.updateInterest(c);
                break;
            }
        }
    }

    Link connectTo(Node node) {
        if (node.ip == null || node.ip.isEmpty() || node.port == 0) return null;
        try {
            SocketChannel ch = SocketChannel.open();
            ch.configureBlocking(false);
            try { ch.setOption(StandardSocketOptions.TCP_NODELAY, true); } catch (Exception ignored) {}
            Link link = new Link();
            link.ch = ch;
            link.node = node;
            boolean done;
            try {
                done = ch.connect(new InetSocketAddress(node.ip, busPort(node.port)));
            } catch (Exception e) {
                try { ch.close(); } catch (IOException ignored) {}
                return null;
            }
            link.connecting = !done;
            link.key = ch.register(s.selector,
                    done ? SelectionKey.OP_READ : SelectionKey.OP_CONNECT, link);
            return link;
        } catch (IOException e) {
            return null;
        }
    }

    public void beforeSleep() {
        if ((todoBeforeSleep & TODO_HANDLE_FAILOVER) != 0) handleSlaveFailover();
        if ((todoBeforeSleep & TODO_UPDATE_STATE) != 0) updateState();
        if ((todoBeforeSleep & TODO_SAVE_CONFIG) != 0) {
            boolean fsync = (todoBeforeSleep & TODO_FSYNC_CONFIG) != 0;
            saveConfigOrDie(fsync);
        }
        todoBeforeSleep = 0;
    }

    public void doBeforeSleep(int flags) { todoBeforeSleep |= flags; }

    // ------------------------------------------------------------------
    // serving / redirecting clients
    // ------------------------------------------------------------------
    public static final class Redirect {
        public int slot;
        public int code = REDIR_NONE;
    }

    /** cluster.c getNodeByQuery(). */
    public Node getNodeByQuery(Client c, Cmd cmd, byte[][] argv, int argc, Redirect out) {
        Node n = null;
        byte[] firstkey = null;
        boolean multipleKeys = false;
        int slot = 0;
        boolean migratingSlot = false, importingSlot = false;
        int missingKeys = 0;
        out.code = REDIR_NONE;

        List<byte[][]> commands = new ArrayList<>();
        List<Cmd> cmds = new ArrayList<>();
        if (cmd.name.equals("exec")) {
            if (!c.inMulti) return myself;
            for (byte[][] q : c.multiQueue) {
                if (q.length == 0) continue;
                Cmd qc = CommandTable.lookup(Util.lower(q[0]));
                if (qc == null) continue;
                commands.add(q);
                cmds.add(qc);
            }
        } else {
            commands.add(Arrays.copyOf(argv, argc));
            cmds.add(cmd);
        }

        for (int i = 0; i < commands.size(); i++) {
            byte[][] margv = commands.get(i);
            Cmd mcmd = cmds.get(i);
            for (byte[] thiskey : getKeysFromCommand(mcmd, margv, margv.length)) {
                int thisslot = keyHashSlot(thiskey);
                if (firstkey == null) {
                    firstkey = thiskey;
                    slot = thisslot;
                    n = slots[slot];
                    if (n == null) {
                        out.code = REDIR_DOWN_UNBOUND;
                        return null;
                    }
                    if (n == myself && migratingSlotsTo[slot] != null) migratingSlot = true;
                    else if (importingSlotsFrom[slot] != null) importingSlot = true;
                } else {
                    if (!Arrays.equals(firstkey, thiskey)) {
                        if (slot != thisslot) {
                            out.code = REDIR_CROSS_SLOT;
                            return null;
                        }
                        multipleKeys = true;
                    }
                }
                if ((migratingSlot || importingSlot) && s.lookupKey(0, thiskey, true) == null)
                    missingKeys++;
            }
        }

        if (n == null) return myself;
        out.slot = slot;

        if ((migratingSlot || importingSlot) && cmd.name.equals("migrate")) return myself;

        if (migratingSlot && missingKeys > 0) {
            out.code = REDIR_ASK;
            return migratingSlotsTo[slot];
        }

        if (importingSlot && (c.asking || cmd.has("asking"))) {
            if (multipleKeys && missingKeys > 0) {
                out.code = REDIR_UNSTABLE;
                return null;
            }
            return myself;
        }

        if (c.readonlyMode && cmd.isReadonly() && myself.isSlave() && myself.slaveof == n)
            return myself;

        if (n != myself) out.code = REDIR_MOVED;
        return n;
    }

    /** db.c getKeysFromCommand(). */
    public static List<byte[]> getKeysFromCommand(Cmd cmd, byte[][] argv, int argc) {
        List<byte[]> keys = new ArrayList<>();
        switch (cmd.name) {
            case "eval": case "evalsha": case "eval_ro": case "evalsha_ro":
            case "fcall": case "fcall_ro": {
                if (argc < 3) return keys;
                Long num = Util.string2ll(argv[2]);
                if (num == null || num < 0 || num > argc - 3) return keys;
                for (int i = 0; i < num; i++) keys.add(argv[3 + i]);
                return keys;
            }
            case "zunionstore": case "zinterstore": case "zdiffstore": {
                if (argc < 4) return keys;
                keys.add(argv[1]);
                Long num = Util.string2ll(argv[2]);
                if (num == null || num < 1 || num > argc - 3) return keys;
                for (int i = 0; i < num; i++) keys.add(argv[3 + i]);
                return keys;
            }
            case "zunion": case "zinter": case "zdiff": case "zintercard":
            case "sintercard": case "lmpop": case "zmpop": {
                if (argc < 3) return keys;
                Long num = Util.string2ll(argv[1]);
                if (num == null || num < 1 || num > argc - 2) return keys;
                for (int i = 0; i < num; i++) keys.add(argv[2 + i]);
                return keys;
            }
            case "blmpop": case "bzmpop": {
                if (argc < 4) return keys;
                Long num = Util.string2ll(argv[2]);
                if (num == null || num < 1 || num > argc - 3) return keys;
                for (int i = 0; i < num; i++) keys.add(argv[3 + i]);
                return keys;
            }
            case "sort": case "sort_ro": {
                if (argc < 2) return keys;
                keys.add(argv[1]);
                for (int i = 2; i < argc; i++) {
                    if (Util.equalsIgnoreCase(argv[i], "store") && i + 1 < argc) {
                        keys.add(argv[i + 1]);
                        break;
                    }
                }
                return keys;
            }
            case "georadius": case "georadius_ro": case "georadiusbymember":
            case "georadiusbymember_ro": {
                if (argc < 2) return keys;
                keys.add(argv[1]);
                for (int i = 2; i < argc; i++) {
                    if ((Util.equalsIgnoreCase(argv[i], "store")
                            || Util.equalsIgnoreCase(argv[i], "storedist")) && i + 1 < argc) {
                        keys.add(argv[i + 1]);
                        i++;
                    }
                }
                return keys;
            }
            case "xread": case "xreadgroup": {
                int pos = -1;
                for (int i = 1; i < argc; i++)
                    if (Util.equalsIgnoreCase(argv[i], "streams")) { pos = i; break; }
                if (pos < 0) return keys;
                int num = (argc - pos - 1) / 2;
                for (int i = 0; i < num; i++) keys.add(argv[pos + 1 + i]);
                return keys;
            }
            case "migrate": {
                if (argc < 6) return keys;
                if (argv[3].length != 0) { keys.add(argv[3]); return keys; }
                for (int i = 6; i < argc; i++) {
                    if (Util.equalsIgnoreCase(argv[i], "keys")) {
                        for (int j = i + 1; j < argc; j++) keys.add(argv[j]);
                        break;
                    }
                }
                return keys;
            }
            case "memory": case "object": {
                if (argc >= 3 && (Util.equalsIgnoreCase(argv[1], "usage")
                        || Util.equalsIgnoreCase(argv[1], "encoding")
                        || Util.equalsIgnoreCase(argv[1], "refcount")
                        || Util.equalsIgnoreCase(argv[1], "freq"))) {
                    keys.add(argv[2]);
                }
                return keys;
            }
            default:
                break;
        }
        if (cmd.firstKey <= 0) return keys;
        int last = cmd.lastKey;
        if (last < 0) last = argc + last;
        int step = cmd.keyStep <= 0 ? 1 : cmd.keyStep;
        for (int i = cmd.firstKey; i <= last && i < argc; i += step) keys.add(argv[i]);
        return keys;
    }

    /**
     * Does this command reference keys at all? (cmd->getkeys_proc || cmd->firstkey)
     * Note that EXEC is NOT in this set: redis checks every command of the
     * transaction when it is queued, not again when EXEC runs.
     */
    public static boolean commandHasKeys(Cmd cmd) {
        if (cmd.firstKey != 0) return true;
        if (cmd.has("movablekeys")) return true;
        switch (cmd.name) {
            case "eval": case "evalsha": case "eval_ro": case "evalsha_ro":
            case "fcall": case "fcall_ro": case "zunionstore": case "zinterstore":
            case "zdiffstore": case "sort": case "sort_ro":
            case "georadius": case "georadiusbymember": case "migrate":
            case "xread": case "xreadgroup": case "lmpop": case "zmpop":
                return true;
            default:
                return false;
        }
    }

    public void redirectClient(Client c, Node n, int hashslot, int errorCode) {
        switch (errorCode) {
            case REDIR_CROSS_SLOT:
                c.addError("CROSSSLOT Keys in request don't hash to the same slot");
                break;
            case REDIR_UNSTABLE:
                c.addError("TRYAGAIN Multiple keys request during rehashing of slot");
                break;
            case REDIR_DOWN_STATE:
                c.addError("CLUSTERDOWN The cluster is down");
                break;
            case REDIR_DOWN_UNBOUND:
                c.addError("CLUSTERDOWN Hash slot not served");
                break;
            case REDIR_MOVED:
            case REDIR_ASK:
                c.addError((errorCode == REDIR_ASK ? "ASK " : "MOVED ") + hashslot + " "
                        + n.ip + ":" + n.port);
                break;
            default:
                c.addError("ERR getNodeByQuery() unknown error.");
        }
    }

    /**
     * server.c processCommand(): the cluster redirection check. Returns false
     * when a redirection error was emitted and the command must not run.
     */
    public boolean redirectCheck(Client c, Cmd cmd) {
        if (s.repl.isMasterClient(c)) return true;
        if (c.fake && masterIsLuaCaller()) return true;

        boolean ok = true;
        if (commandHasKeys(cmd)) {
            if (state != STATE_OK) {
                if (c.inMulti) c.multiError = true;
                redirectClient(c, null, 0, REDIR_DOWN_STATE);
                ok = false;
            } else {
                Redirect r = new Redirect();
                Node n = getNodeByQuery(c, cmd, c.argv, c.argc, r);
                if (n == null || n != myself) {
                    if (c.inMulti) c.multiError = true;
                    redirectClient(c, n, r.slot, r.code);
                    ok = false;
                }
            }
        }
        /* networking.c resetClient(): ASKING is a one-shot flag, cleared after
         * the next command unless we are inside a MULTI. */
        if (!c.inMulti && !cmd.name.equals("asking")) c.asking = false;
        return ok;
    }

    /** The Lua caller is the master link: replicated EVAL must not redirect. */
    private boolean masterIsLuaCaller() {
        try {
            Client caller = s.scripting == null ? null : s.scripting.currCaller();
            return caller != null && s.repl.isMasterClient(caller);
        } catch (RuntimeException e) {
            return false;
        }
    }
}
