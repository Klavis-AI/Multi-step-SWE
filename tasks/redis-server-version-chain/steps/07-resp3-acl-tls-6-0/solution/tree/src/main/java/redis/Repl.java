package redis;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.StandardSocketOptions;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;
import java.util.*;

/**
 * Replication: master side (SYNC/PSYNC + replication backlog) and replica side
 * (the master link).  Everything runs on the single NIO event loop thread: the
 * master link channel is registered with Server.selector and driven from
 * MasterLink.onEvent(SelectionKey).
 *
 * Ported from redis src/replication.c (8.0.6, with the 2.8.24 circular backlog).
 */
public final class Repl {
    /** minimum backlog size, CONFIG_REPL_BACKLOG_MIN_SIZE in the C source */
    public static final int BACKLOG_MIN_SIZE = 16 * 1024;
    public static final int RUN_ID_SIZE = 40;

    // repl_state values of the replica side state machine (server.repl_state)
    public static final int ST_NONE = 0;
    public static final int ST_CONNECT = 1;
    public static final int ST_CONNECTING = 2;
    public static final int ST_RECEIVE_PING = 3;
    public static final int ST_RECEIVE_HANDSHAKE = 4;
    public static final int ST_RECEIVE_PSYNC = 5;
    public static final int ST_TRANSFER = 6;
    public static final int ST_CONNECTED = 7;

    public final Server s;

    // ------------------------------------------------------------------
    // replica side ("we have a master")
    // ------------------------------------------------------------------
    public String masterHost = null;
    public int masterPort = 0;
    public boolean masterLinkUp = false;
    public MasterLink link;
    /** offset of the master stream we already applied (master->reploff) */
    public long replOffset = 0;

    public int state = ST_NONE;
    /** the fake client that carries the master link command stream */
    public Client masterClient = null;

    SocketChannel mchan;
    SelectionKey mkey;
    final Buf inBuf = new Buf(64 * 1024);
    final Buf outBuf = new Buf(1024);

    long masterLastIo = 0;          // ms, master->lastinteraction
    long transferLastIo = 0;        // ms, repl_transfer_lastio
    long downSince = 0;             // ms, repl_down_since (0 = never)
    long transferSize = -1;         // repl_transfer_size (-1 = header not read)
    Buf rdbBuf;                     // accumulated RDB payload
    boolean useMark = false;        // diskless "$EOF:<40 bytes>" form
    byte[] eofMark;
    String masterReplid = "";       // server.master_replid
    long masterInitialOffset = -1;
    int handshakeRepliesPending = 0;
    long lastAckSent = 0;

    // cached master, used to try a partial resynchronization
    boolean haveCachedMaster = false;
    String cachedReplid = "";
    long cachedOffset = 0;
    /** the SELECTed database of the cached master client (replicationCacheMaster) */
    int cachedDb = 0;

    // ------------------------------------------------------------------
    // master side
    // ------------------------------------------------------------------
    /** circular replication backlog, null when not active */
    public byte[] backlog = null;
    public int backlogSize = 0;
    public long backlogHistlen = 0;
    public long backlogOff = 0;
    public int backlogIdx = 0;
    /** last db selected in the replication stream (server.slaveseldb) */
    public int replSeldb = -1;
    public long replStreamLastIo = System.currentTimeMillis();
    public long noReplicasSince = System.currentTimeMillis();
    /** set by the FAILOVER implementation; shown in INFO */
    public String failoverState = "no-failover";

    public long statSyncFull = 0, statSyncPartialOk = 0, statSyncPartialErr = 0;

    long lastGetackSent = 0;
    long lastConnectAttempt = 0;

    /** per replica-client bookkeeping that does not fit in Client */
    public static final class ReplicaState {
        public boolean capaEof, capaPsync2, prePsync;
        public String announceIp;
    }
    public final Map<Long, ReplicaState> rstate = new HashMap<>();

    public Repl(Server s) { this.s = s; }

    public ReplicaState rs(Client c) { return rstate.computeIfAbsent(c.id, x -> new ReplicaState()); }

    // ==================================================================
    // replication ID helpers
    // ==================================================================
    public void changeReplicationId() { s.replId = Server.randomHex(RUN_ID_SIZE); }

    public void clearReplicationId2() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < RUN_ID_SIZE; i++) sb.append('0');
        s.replId2 = sb.toString();
        s.secondReplOffset = -1;
    }

    public void shiftReplicationId() {
        s.replId2 = s.replId;
        s.secondReplOffset = s.masterReplOffset + 1;
        changeReplicationId();
        s.logNotice("Setting secondary replication ID to " + s.replId2
                + ", valid up to offset: " + s.secondReplOffset + ". New replication ID is " + s.replId);
    }

    // ==================================================================
    // replication backlog
    // ==================================================================
    public int configuredBacklogSize() {
        long v = s.config.getLong("repl-backlog-size");
        if (v < BACKLOG_MIN_SIZE) v = BACKLOG_MIN_SIZE;
        if (v > Integer.MAX_VALUE - 8) v = Integer.MAX_VALUE - 8;
        return (int) v;
    }

    public void createBacklog() {
        if (backlog != null) return;
        backlogSize = configuredBacklogSize();
        backlog = new byte[backlogSize];
        backlogHistlen = 0;
        backlogIdx = 0;
        backlogOff = s.masterReplOffset + 1;
    }

    public void freeBacklog() {
        backlog = null;
        backlogHistlen = 0;
        backlogIdx = 0;
        backlogOff = 0;
    }

    /** Called when the first replica attaches (createReplicationBacklogIfNeeded). */
    public void createBacklogIfNeeded() {
        if (backlog != null) return;
        // A brand new backlog means no valid replication history, so start a new
        // one.  A replica must keep the replication ID it inherited from its
        // master, though.
        if (s.replicas.size() == 1 && masterHost == null) {
            changeReplicationId();
            clearReplicationId2();
        }
        createBacklog();
        s.logNotice("Replication backlog created, my new replication IDs are '"
                + s.replId + "' and '" + s.replId2 + "'");
    }

    void resizeBacklogIfNeeded() {
        if (backlog == null) return;
        int want = configuredBacklogSize();
        if (want == backlogSize) return;
        // Like redis 2.8: flush the old buffer, it refills incrementally.
        backlogSize = want;
        backlog = new byte[backlogSize];
        backlogHistlen = 0;
        backlogIdx = 0;
        backlogOff = s.masterReplOffset + 1;
    }

    /** Append raw replication stream bytes to the backlog, advancing the offset. */
    void backlogFeed(byte[] p, int off, int len) {
        s.masterReplOffset += len;
        while (len > 0) {
            int thislen = backlogSize - backlogIdx;
            if (thislen > len) thislen = len;
            System.arraycopy(p, off, backlog, backlogIdx, thislen);
            backlogIdx += thislen;
            if (backlogIdx == backlogSize) backlogIdx = 0;
            len -= thislen;
            off += thislen;
            backlogHistlen += thislen;
        }
        if (backlogHistlen > backlogSize) backlogHistlen = backlogSize;
        backlogOff = s.masterReplOffset - backlogHistlen + 1;
    }

    /** Copy the backlog starting at the absolute offset into a replica output buffer. */
    long addBacklogToClient(Client c, long offset) {
        if (backlogHistlen == 0) return 0;
        long skip = offset - backlogOff;
        long j = (backlogIdx + (backlogSize - backlogHistlen)) % backlogSize;
        j = (j + skip) % backlogSize;
        long len = backlogHistlen - skip;
        long total = len;
        while (len > 0) {
            long thislen = Math.min(backlogSize - j, len);
            c.out.put(backlog, (int) j, (int) thislen);
            len -= thislen;
            j = 0;
        }
        return total;
    }

    // ==================================================================
    // feeding the replication stream (master side)
    // ==================================================================

    /** Write raw bytes to the backlog and to every replica output buffer. */
    void feedRaw(byte[] p, int off, int len) {
        if (len <= 0) return;
        if (backlog != null) backlogFeed(p, off, len);
        else s.masterReplOffset += len;
        for (int i = 0; i < s.replicas.size(); i++) {
            Client r = s.replicas.get(i);
            if (r.chan == null) continue;
            if (!"online".equals(r.replState)) continue;
            r.out.put(p, off, len);
        }
    }

    private static void appendCmd(Buf b, byte[][] argv) {
        b.put((byte) '*').putLong(argv.length).crlf();
        for (byte[] a : argv) b.put((byte) '$').putLong(a.length).crlf().put(a).crlf();
    }

    /**
     * replicationFeedSlaves(): emit SELECT when the db changed, then the command.
     * dbid == -1 means "no SELECT needed" (PING, REPLCONF GETACK).
     */
    public void feed(int dbid, byte[][] argv) {
        // Sub-replicas of a replica receive the verbatim master stream instead,
        // see feedFromMasterStream().
        if (masterHost != null) return;
        if (argv == null || argv.length == 0) return;
        // shouldPropagate(): nothing is replicated while loading a dataset.
        if (s.loading) return;
        if (backlog == null && s.replicas.isEmpty()) {
            // Redis still advances the offset when the AOF is on, it is used to
            // track AOF fsyncs (see shouldPropagate()/replicationFeedSlaves()).
            if (s.aof != null && s.aof.isEnabled()) s.masterReplOffset += 1;
            return;
        }
        replStreamLastIo = System.currentTimeMillis();
        if (dbid != -1 && replSeldb != dbid) {
            Buf sel = new Buf(32);
            appendCmd(sel, new byte[][]{Util.b("SELECT"), Util.b(Integer.toString(dbid))});
            feedRaw(sel.a, 0, sel.len);
            replSeldb = dbid;
        }
        Buf b = new Buf(64);
        appendCmd(b, argv);
        feedRaw(b.a, 0, b.len);
    }

    /** replicationFeedStreamFromMasterStream(): proxy the master stream verbatim. */
    void feedFromMasterStream(byte[] p, int off, int len) {
        if (backlog == null) return;
        replStreamLastIo = System.currentTimeMillis();
        feedRaw(p, off, len);
    }

    /**
     * Kept for the call site in Server.expireIfNeeded(): feed() already advanced
     * the master replication offset for that propagation.
     */
    public void incrOffsetForPropagation() { }

    // ==================================================================
    // master side: SYNC / PSYNC
    // ==================================================================

    public static String ipOf(Client c) {
        String a = c.addr == null ? "" : c.addr;
        int i = a.lastIndexOf(':');
        return i < 0 ? a : a.substring(0, i);
    }

    public String replicaName(Client c) {
        ReplicaState st = rstate.get(c.id);
        String ip = (st != null && st.announceIp != null) ? st.announceIp : ipOf(c);
        if (c.replAckPort != 0) return ip + ":" + c.replAckPort;
        return "client id=" + c.id + " addr=" + c.addr;
    }

    /** Returns true when a partial resynchronization was granted. */
    boolean tryPartialResync(Client c, String replid, long psyncOffset) {
        if (!replid.equalsIgnoreCase(s.replId)
                && (!replid.equalsIgnoreCase(s.replId2) || psyncOffset > s.secondReplOffset)) {
            if (!replid.startsWith("?")) {
                if (!replid.equalsIgnoreCase(s.replId) && !replid.equalsIgnoreCase(s.replId2)) {
                    s.logNotice("Partial resynchronization not accepted: Replication ID mismatch"
                            + " (Replica asked for '" + replid + "', my replication IDs are '"
                            + s.replId + "' and '" + s.replId2 + "')");
                } else {
                    s.logNotice("Partial resynchronization not accepted: Requested offset for"
                            + " second ID was " + psyncOffset + ", but I can reply up to "
                            + s.secondReplOffset);
                }
            } else {
                s.logNotice("Full resync requested by replica " + replicaName(c));
            }
            return false;
        }
        if (backlog == null || psyncOffset < backlogOff
                || psyncOffset > (backlogOff + backlogHistlen)) {
            s.logNotice("Unable to partial resync with replica " + replicaName(c)
                    + " for lack of backlog (Replica request was: " + psyncOffset + ").");
            if (psyncOffset > s.masterReplOffset) {
                s.logWarn("Warning: replica " + replicaName(c) + " tried to PSYNC with an offset"
                        + " that is greater than the master replication offset.");
            }
            return false;
        }

        c.isReplica = true;
        c.replState = "online";
        c.replAckTime = System.currentTimeMillis();
        if (!s.replicas.contains(c)) s.replicas.add(c);
        ReplicaState st = rs(c);
        if (st.capaPsync2) c.addRaw(Util.b("+CONTINUE " + s.replId + "\r\n"));
        else c.addRaw(Util.b("+CONTINUE\r\n"));
        long len = addBacklogToClient(c, psyncOffset);
        s.logNotice("Partial resynchronization request from " + replicaName(c)
                + " accepted. Sending " + len + " bytes of backlog starting from offset "
                + psyncOffset + ".");
        return true;
    }

    /** Implements SYNC and PSYNC; called from ReplCmds. */
    public void syncCommand(Client c) {
        boolean psync = c.argIs(0, "psync");
        if (c.isReplica) return;

        if (psync && c.argc > 3 && c.argIs(3, "failover")) {
            s.logNotice("Failover request received for replid " + c.argStr(1) + ".");
            if (masterHost == null) {
                c.addError("ERR PSYNC FAILOVER can't be sent to a master.");
                return;
            }
            if (c.argStr(1).equalsIgnoreCase(s.replId)) {
                unsetMaster();
                s.logNotice("MASTER MODE enabled (failover request from '" + c.addr + "')");
            } else {
                c.addError("ERR PSYNC FAILOVER replid must match my replid.");
                return;
            }
        }

        if (!"no-failover".equals(failoverState)) {
            c.addError("NOMASTERLINK Can't SYNC while failing over");
            return;
        }
        if (masterHost != null && state != ST_CONNECTED) {
            c.addError("NOMASTERLINK Can't SYNC while not connected with my master");
            return;
        }
        if (c.out.len != 0) {
            c.addError("ERR SYNC and PSYNC are invalid with pending output");
            return;
        }

        s.logNotice("Replica " + replicaName(c) + " asks for synchronization");

        if (psync) {
            Long off = Util.string2ll(c.argv[2]);
            if (off == null) {
                s.logWarn("Replica " + replicaName(c)
                        + " asks for synchronization but with a wrong offset");
                c.addError("ERR value is not an integer or out of range");
                return;
            }
            if (tryPartialResync(c, c.argStr(1), off)) {
                statSyncPartialOk++;
                return;
            }
            if (!c.argStr(1).startsWith("?")) statSyncPartialErr++;
        } else {
            rs(c).prePsync = true;
        }

        // ---- full resynchronization ----
        statSyncFull++;
        c.isReplica = true;
        c.replState = "wait_bgsave";
        c.replAckTime = System.currentTimeMillis();
        if (!s.replicas.contains(c)) s.replicas.add(c);
        createBacklogIfNeeded();

        long offset = s.masterReplOffset;
        // Force a SELECT to be re-emitted at the head of the new stream.
        replSeldb = -1;

        byte[] rdb;
        try {
            rdb = Rdb.serializeDataset(s);
        } catch (Exception e) {
            s.logWarn("Failed to produce the RDB payload for replica " + replicaName(c) + ": " + e);
            s.replicas.remove(c);
            c.isReplica = false;
            c.replState = "";
            c.addError("ERR Unable to perform a full resynchronization");
            return;
        }

        s.logNotice("Starting BGSAVE for SYNC with target: disk");
        s.logNotice("Background saving started by pid " + ProcessHandle.current().pid());
        if (psync) {
            c.addRaw(Util.b("+FULLRESYNC " + s.replId + " " + offset + "\r\n"));
        }
        c.out.put((byte) '$').putLong(rdb.length).crlf();
        c.out.put(rdb);
        s.logNotice("Background saving terminated with success");
        c.replState = "online";
        s.logNotice("Synchronization with replica " + replicaName(c) + " succeeded");
    }

    // ==================================================================
    // master side: REPLCONF
    // ==================================================================
    public void replconfCommand(Client c) {
        if ((c.argc % 2) == 0) {
            c.addError("ERR syntax error");
            return;
        }
        for (int j = 1; j < c.argc; j += 2) {
            if (c.argIs(j, "listening-port")) {
                Long port = Util.string2ll(c.argv[j + 1]);
                if (port == null) { c.addError("ERR value is not an integer or out of range"); return; }
                c.replAckPort = (int) (long) port;
            } else if (c.argIs(j, "ip-address")) {
                rs(c).announceIp = c.argStr(j + 1);
            } else if (c.argIs(j, "capa")) {
                if (c.argIs(j + 1, "eof")) rs(c).capaEof = true;
                else if (c.argIs(j + 1, "psync2")) rs(c).capaPsync2 = true;
                // capabilities we do not understand are ignored on purpose
            } else if (c.argIs(j, "ack")) {
                if (!c.isReplica) return;
                Long off = Util.string2ll(c.argv[j + 1]);
                if (off == null) return;
                if (off > c.replAckOffset) c.replAckOffset = off;
                /* 7.2: REPLCONF ACK <offset> FACK <fsynced offset> */
                if (c.argc > j + 3 && c.argIs(j + 2, "fack")) {
                    Long faof = Util.string2ll(c.argv[j + 3]);
                    if (faof != null && faof > c.replAofOffset) c.replAofOffset = faof;
                }
                c.replAckTime = System.currentTimeMillis();
                // REPLCONF ACK never replies.
                return;
            } else if (c.argIs(j, "getack")) {
                if (masterHost != null && masterClient != null) sendAck();
                return;
            } else if (c.argIs(j, "rdb-only")) {
                Long v = Util.string2ll(c.argv[j + 1]);
                if (v == null || v < 0 || v > 1) {
                    c.addError("ERR value is out of range, must be between 0 and 1 inclusive");
                    return;
                }
            } else {
                c.addError("ERR Unrecognized REPLCONF option: " + c.argStr(j));
                return;
            }
        }
        c.addOk();
    }

    // ==================================================================
    // WAIT support
    // ==================================================================
    public int countAcks(long offset) {
        int n = 0;
        for (Client r : s.replicas) {
            if (!"online".equals(r.replState)) continue;
            if (r.replAckOffset >= offset) n++;
        }
        return n;
    }

    /** replication.c replicationCountAOFAcksByOffset(). */
    public int countAofAcks(long offset) {
        int n = 0;
        for (Client r : s.replicas) {
            if (!"online".equals(r.replState)) continue;
            if (r.replAofOffset >= offset) n++;
        }
        return n;
    }

    /** Broadcast REPLCONF GETACK * to every replica. */
    public void requestAckFromReplicas() {
        if (masterHost != null) return;
        if (s.replicas.isEmpty()) return;
        feed(-1, new byte[][]{Util.b("REPLCONF"), Util.b("GETACK"), Util.b("*")});
        lastGetackSent = System.currentTimeMillis();
    }

    // ==================================================================
    // replica side
    // ==================================================================
    public boolean isMasterClient(Client c) {
        return c != null && masterClient != null && c == masterClient;
    }

    public boolean inHandshake() {
        return state >= ST_RECEIVE_PING && state <= ST_RECEIVE_PSYNC;
    }

    public void setMaster(String host, int port) {
        boolean wasMaster = (masterHost == null);
        freeMasterLink();
        masterHost = host;
        masterPort = port;
        masterLinkUp = false;
        disconnectAllBlockedClients();
        if (wasMaster) {
            // replicationCacheMasterUsingMyself(): try a partial resync with the
            // new master using our own replid/offset.
            s.logNotice("Before turning into a replica, using my own master parameters"
                    + " to synthesize a cached master: I may be able to synchronize with"
                    + " the new master with just a partial transfer.");
            cachedReplid = s.replId;
            cachedOffset = s.masterReplOffset;
            cachedDb = 0;
            haveCachedMaster = true;
            masterReplid = s.replId;
            replOffset = s.masterReplOffset;
        }
        if (downSince == 0) downSince = System.currentTimeMillis();
        state = ST_CONNECT;
        s.logNotice("Connecting to MASTER " + masterHost + ":" + masterPort);
        connectWithMaster();
    }

    public void unsetMaster() {
        if (masterHost == null) return;
        masterHost = null;
        masterPort = 0;
        freeMasterLink();
        discardCachedMaster();
        shiftReplicationId();
        disconnectReplicas();
        state = ST_NONE;
        masterLinkUp = false;
        replSeldb = -1;
        noReplicasSince = System.currentTimeMillis();
        downSince = 0;
    }

    /**
     * replication.c disconnectAllBlockedClients(): a client blocked while this
     * instance was a master must not be served by the replicated stream.
     */
    public void disconnectAllBlockedClients() {
        for (Client bc : new ArrayList<>(s.clients.values())) {
            if (!bc.blocked) continue;
            bc.addError("UNBLOCKED force unblock from blocking operation, "
                    + "instance state changed (master -> slave?)");
            s.unblockClient(bc);
            bc.closeAfterReply = true;
        }
    }

    public void discardCachedMaster() {
        haveCachedMaster = false;
        cachedReplid = "";
        cachedOffset = 0;
        cachedDb = 0;
    }

    public void disconnectReplicas() {
        for (Client r : new ArrayList<>(s.replicas)) s.freeClient(r);
        s.replicas.clear();
    }

    /** Tear the master link down without touching masterHost. */
    void freeMasterLink() {
        if (mkey != null) { try { mkey.cancel(); } catch (Exception ignored) {} mkey = null; }
        if (mchan != null) { try { mchan.close(); } catch (IOException ignored) {} mchan = null; }
        if (masterClient != null) s.clients.remove(masterClient.id);
        masterClient = null;
        masterLinkUp = false;
        link = null;
        inBuf.clear();
        outBuf.clear();
        rdbBuf = null;
        transferSize = -1;
        useMark = false;
        eofMark = null;
        handshakeRepliesPending = 0;
    }

    /** Cache the master state so the next connection can try a partial resync. */
    void cacheMaster() {
        if (masterClient == null) return;
        cachedReplid = masterReplid;
        cachedOffset = replOffset;
        /* replicationResurrectCachedMaster() reuses the very same client, so the
         * database it had SELECTed must survive the disconnection. */
        cachedDb = masterClient.db;
        haveCachedMaster = true;
        s.logNotice("Caching the disconnected master state.");
    }

    void cancelHandshake(String why) {
        if (why != null) s.logWarn(why);
        boolean wasConnected = (state == ST_CONNECTED);
        if (wasConnected) cacheMaster();
        freeMasterLink();
        if (masterHost != null) {
            state = ST_CONNECT;
            if (downSince == 0) downSince = System.currentTimeMillis();
            if (wasConnected) s.logNotice("Reconnecting to MASTER " + masterHost + ":" + masterPort);
        } else {
            state = ST_NONE;
        }
    }

    void connectWithMaster() {
        lastConnectAttempt = System.currentTimeMillis();
        try {
            mchan = SocketChannel.open();
            mchan.configureBlocking(false);
            try { mchan.setOption(StandardSocketOptions.TCP_NODELAY, true); } catch (Exception ignored) {}
            link = new MasterLink();
            mkey = mchan.register(s.selector, SelectionKey.OP_CONNECT, link);
            state = ST_CONNECTING;
            transferLastIo = System.currentTimeMillis();
            boolean done = mchan.connect(new InetSocketAddress(masterHost, masterPort));
            s.logNotice("MASTER <-> REPLICA sync started");
            if (done) link.onConnected();
        } catch (Exception e) {
            s.logWarn("Unable to connect to MASTER: " + e);
            freeMasterLink();
            state = ST_CONNECT;
        }
    }

    // ---- writing to the master ----
    void sendToMaster(String... args) {
        Buf b = new Buf(64);
        b.put((byte) '*').putLong(args.length).crlf();
        for (String a : args) {
            byte[] x = Util.b(a);
            b.put((byte) '$').putLong(x.length).crlf().put(x).crlf();
        }
        outBuf.put(b.a, 0, b.len);
        pushOut();
    }

    void pushOut() {
        try {
            if (mchan != null && outBuf.len > 0) {
                ByteBuffer bb = ByteBuffer.wrap(outBuf.a, 0, outBuf.len);
                int w = mchan.write(bb);
                if (w > 0) outBuf.consume(w);
            }
        } catch (IOException e) {
            cancelHandshake("I/O error writing to MASTER: " + e);
            return;
        }
        updateMasterInterest();
    }

    void updateMasterInterest() {
        if (mkey == null || !mkey.isValid()) return;
        int ops;
        if (state == ST_CONNECTING) ops = SelectionKey.OP_CONNECT;
        else {
            ops = SelectionKey.OP_READ;
            if (outBuf.len > 0) ops |= SelectionKey.OP_WRITE;
        }
        if (mkey.interestOps() != ops) mkey.interestOps(ops);
    }

    /** REPLCONF ACK <offset> [FACK <fsynced offset>] back to the master. */
    public void sendAck() {
        if (mchan == null || masterClient == null) return;
        if (s.aof != null && s.aof.fsyncedReploff != -1) {
            sendToMaster("REPLCONF", "ACK", Long.toString(replOffset),
                    "FACK", Long.toString(s.aof.fsyncedReploff));
        } else {
            sendToMaster("REPLCONF", "ACK", Long.toString(replOffset));
        }
        lastAckSent = System.currentTimeMillis();
    }

    // ==================================================================
    // the master link event handler
    // ==================================================================
    public final class MasterLink {

        public void onEvent(SelectionKey k) {
            try {
                if (!k.isValid()) return;
                if (k.isConnectable()) {
                    boolean ok;
                    try { ok = mchan.finishConnect(); }
                    catch (IOException e) {
                        cancelHandshake("Error condition on socket for SYNC: " + e.getMessage());
                        return;
                    }
                    if (!ok) return;
                    onConnected();
                    return;
                }
                if (k.isValid() && k.isWritable()) pushOut();
                if (k.isValid() && k.isReadable()) readFromMaster();
            } catch (Exception e) {
                cancelHandshake("Error in the master link: " + e);
            }
        }

        void onConnected() {
            s.logNotice("Non blocking connect for SYNC fired the event.");
            state = ST_RECEIVE_PING;
            transferLastIo = System.currentTimeMillis();
            masterLastIo = transferLastIo;
            updateMasterInterest();
            sendToMaster("PING");
        }

        void readFromMaster() {
            byte[] tmp = new byte[64 * 1024];
            ByteBuffer bb = ByteBuffer.wrap(tmp);
            int n;
            try { n = mchan.read(bb); }
            catch (IOException e) {
                cancelHandshake("I/O error trying to sync with MASTER: " + e.getMessage());
                return;
            }
            if (n < 0) {
                if (state == ST_CONNECTED) s.logWarn("MASTER <-> REPLICA sync: connection lost");
                cancelHandshake(null);
                return;
            }
            if (n == 0) return;
            inBuf.put(tmp, 0, n);
            masterLastIo = System.currentTimeMillis();
            transferLastIo = masterLastIo;
            drive();
        }
    }

    /** Read one CRLF/LF terminated line, or null when it is not complete yet. */
    String readLine() {
        for (int i = 0; i < inBuf.len; i++) {
            if (inBuf.a[i] == '\n') {
                int end = i;
                if (end > 0 && inBuf.a[end - 1] == '\r') end--;
                String line = Util.s(inBuf.a, 0, end);
                inBuf.consume(i + 1);
                return line;
            }
        }
        return null;
    }

    /** Drive the replica side state machine over whatever is buffered. */
    void drive() {
        boolean progress = true;
        while (progress) {
            progress = false;
            switch (state) {
                case ST_RECEIVE_PING: {
                    String line = readLine();
                    if (line == null) return;
                    if (line.isEmpty()) { progress = true; break; }
                    if (line.charAt(0) != '+' && !line.startsWith("-NOAUTH")
                            && !line.startsWith("-NOPERM")
                            && !line.startsWith("-ERR operation not permitted")) {
                        cancelHandshake("Error reply to PING from master: '" + line + "'");
                        return;
                    }
                    s.logNotice("Master replied to PING, replication can continue...");
                    sendHandshake();
                    progress = true;
                    break;
                }
                case ST_RECEIVE_HANDSHAKE: {
                    while (handshakeRepliesPending > 0) {
                        String line = readLine();
                        if (line == null) return;
                        if (line.isEmpty()) continue;
                        handshakeRepliesPending--;
                        if (line.charAt(0) == '-') {
                            s.logNotice("(Non critical) Master does not understand a REPLCONF"
                                    + " option: " + line);
                        }
                    }
                    sendPsync();
                    progress = true;
                    break;
                }
                case ST_RECEIVE_PSYNC: {
                    String line = readLine();
                    if (line == null) return;
                    if (line.isEmpty()) { progress = true; break; }
                    if (!handlePsyncReply(line)) return;
                    progress = true;
                    break;
                }
                case ST_TRANSFER: {
                    if (!readBulkPayload()) return;
                    progress = true;
                    break;
                }
                case ST_CONNECTED: {
                    applyMasterStream();
                    return;
                }
                default:
                    return;
            }
        }
    }

    void sendHandshake() {
        handshakeRepliesPending = 0;
        String auth = s.config.getOr("masterauth", "");
        if (auth != null && !auth.isEmpty()) {
            String user = s.config.getOr("masteruser", "");
            if (user != null && !user.isEmpty()) sendToMaster("AUTH", user, auth);
            else sendToMaster("AUTH", auth);
            handshakeRepliesPending++;
        }
        sendToMaster("REPLCONF", "listening-port", Integer.toString(announcePort()));
        handshakeRepliesPending++;
        String ip = s.config.getOr("replica-announce-ip", "");
        if (ip != null && !ip.isEmpty()) {
            sendToMaster("REPLCONF", "ip-address", ip);
            handshakeRepliesPending++;
        }
        sendToMaster("REPLCONF", "capa", "eof", "capa", "psync2");
        handshakeRepliesPending++;
        state = ST_RECEIVE_HANDSHAKE;
    }

    int announcePort() {
        int p = (int) s.config.getLong("replica-announce-port");
        if (p != 0) return p;
        if (s.port != 0) return s.port;
        return s.tlsPort;
    }

    void sendPsync() {
        masterInitialOffset = -1;
        if (haveCachedMaster) {
            s.logNotice("Trying a partial resynchronization (request " + cachedReplid + ":"
                    + (cachedOffset + 1) + ").");
            sendToMaster("PSYNC", cachedReplid, Long.toString(cachedOffset + 1));
        } else {
            s.logNotice("Partial resynchronization not possible (no cached master)");
            sendToMaster("PSYNC", "?", "-1");
        }
        state = ST_RECEIVE_PSYNC;
    }

    /** Returns false when the caller must stop driving (link torn down / need data). */
    boolean handlePsyncReply(String line) {
        if (line.startsWith("+FULLRESYNC")) {
            String[] parts = line.split(" ");
            if (parts.length >= 3 && parts[1].length() == RUN_ID_SIZE) {
                masterReplid = parts[1];
                try { masterInitialOffset = Long.parseLong(parts[2].trim()); }
                catch (NumberFormatException e) { masterInitialOffset = 0; }
                s.logNotice("Full resync from master: " + masterReplid + ":" + masterInitialOffset);
            } else {
                s.logWarn("Master replied with wrong +FULLRESYNC syntax.");
                masterReplid = "";
                masterInitialOffset = 0;
            }
            state = ST_TRANSFER;
            transferSize = -1;
            rdbBuf = new Buf(1 << 16);
            useMark = false;
            return true;
        }
        if (line.startsWith("+CONTINUE")) {
            s.logNotice("Successful partial resynchronization with master.");
            String rest = line.length() > 9 ? line.substring(9).trim() : "";
            if (rest.length() == RUN_ID_SIZE && !rest.equals(cachedReplid)) {
                s.logNotice("Master replication ID changed to " + rest);
                s.replId2 = cachedReplid;
                s.secondReplOffset = s.masterReplOffset + 1;
                s.replId = rest;
                cachedReplid = rest;
                disconnectReplicas();
            }
            resurrectCachedMaster();
            s.logNotice("MASTER <-> REPLICA sync: Master accepted a Partial Resynchronization.");
            return true;
        }
        if (line.startsWith("-NOMASTERLINK") || line.startsWith("-LOADING")) {
            cancelHandshake("Master is currently unable to PSYNC but should be in the future: " + line);
            return false;
        }
        // Master does not understand PSYNC: fall back to the old SYNC command.
        s.logNotice("Retrying with SYNC...");
        outBuf.put(Util.b("SYNC\r\n"));
        pushOut();
        haveCachedMaster = false;
        masterReplid = "";
        masterInitialOffset = 0;
        state = ST_TRANSFER;
        transferSize = -1;
        rdbBuf = new Buf(1 << 16);
        useMark = false;
        return true;
    }

    void resurrectCachedMaster() {
        replOffset = cachedOffset;
        masterReplid = cachedReplid;
        s.replId = cachedReplid;
        createMasterClient(cachedDb);
        state = ST_CONNECTED;
        masterLinkUp = true;
        downSince = 0;
        masterLastIo = System.currentTimeMillis();
        if (backlog == null) createBacklog();
        sendAck();
    }

    static int indexOf(byte[] hay, int hayLen, byte[] needle, int from) {
        if (needle == null || needle.length == 0) return -1;
        int last = hayLen - needle.length;
        for (int i = Math.max(0, from); i <= last; i++) {
            int j = 0;
            while (j < needle.length && hay[i + j] == needle[j]) j++;
            if (j == needle.length) return i;
        }
        return -1;
    }

    /** Returns true when progress was made and drive() should loop again. */
    boolean readBulkPayload() {
        if (transferSize == -1) {
            String line = readLine();
            if (line == null) return false;
            if (line.isEmpty()) {
                // A newline is used as a keep-alive while the master prepares the RDB.
                transferLastIo = System.currentTimeMillis();
                return true;
            }
            if (line.charAt(0) == '-') {
                cancelHandshake("MASTER aborted replication with an error: " + line.substring(1));
                return false;
            }
            if (line.charAt(0) != '$') {
                cancelHandshake("Bad protocol from MASTER, the first byte is not '$' (we received '"
                        + line + "'), are you sure the host and port are right?");
                return false;
            }
            if (line.startsWith("$EOF:") && line.length() >= 5 + RUN_ID_SIZE) {
                useMark = true;
                eofMark = Util.b(line.substring(5, 5 + RUN_ID_SIZE));
                transferSize = 0;
                s.logNotice("MASTER <-> REPLICA sync: receiving streamed RDB from master with EOF to parser");
            } else {
                useMark = false;
                try { transferSize = Long.parseLong(line.substring(1).trim()); }
                catch (NumberFormatException e) {
                    cancelHandshake("Bad protocol from MASTER, invalid bulk length: " + line);
                    return false;
                }
                s.logNotice("MASTER <-> REPLICA sync: receiving " + transferSize
                        + " bytes from master to parser");
            }
            rdbBuf = new Buf(1 << 16);
            return true;
        }

        byte[] payload;
        if (useMark) {
            if (inBuf.len == 0) return false;
            int oldLen = rdbBuf.len;
            rdbBuf.put(inBuf.a, 0, inBuf.len);
            inBuf.clear();
            int idx = indexOf(rdbBuf.a, rdbBuf.len, eofMark, Math.max(0, oldLen - (RUN_ID_SIZE - 1)));
            if (idx < 0) return false;
            payload = Arrays.copyOf(rdbBuf.a, idx);
            int lo = idx + RUN_ID_SIZE;
            if (lo < rdbBuf.len) inBuf.put(rdbBuf.a, lo, rdbBuf.len - lo);
        } else {
            long left = transferSize - rdbBuf.len;
            int take = (int) Math.min(left, inBuf.len);
            if (take > 0) {
                rdbBuf.put(inBuf.a, 0, take);
                inBuf.consume(take);
            }
            if (rdbBuf.len < transferSize) return false;
            payload = rdbBuf.toBytes();
        }
        rdbBuf = null;
        loadTransferredRdb(payload);
        return true;
    }

    void loadTransferredRdb(byte[] payload) {
        s.logNotice("MASTER <-> REPLICA sync: Flushing old data");
        // replicationAttachToNewMaster(): our sub-replicas must resync too.
        discardCachedMaster();
        disconnectReplicas();
        freeBacklog();
        for (Db d : s.dbs) {
            d.clear();
            d.readyKeys.clear();
        }
        for (int i = 0; i < s.dbs.length; i++) s.touchAllWatchedKeys(i);

        s.logNotice("MASTER <-> REPLICA sync: Loading DB in memory");
        boolean ok = true;
        s.loading = true;
        try {
            Rdb.loadFromBytes(s, payload);
        } catch (Exception e) {
            ok = false;
            s.logWarn("Failed trying to load the MASTER synchronization DB from disk: " + e);
        } finally {
            s.loading = false;
        }
        if (!ok) {
            cancelHandshake(null);
            return;
        }
        s.dirty++;

        createMasterClient(0);
        state = ST_CONNECTED;
        masterLinkUp = true;
        downSince = 0;
        masterLastIo = System.currentTimeMillis();

        if (masterReplid != null && masterReplid.length() == RUN_ID_SIZE) s.replId = masterReplid;
        s.masterReplOffset = masterInitialOffset < 0 ? 0 : masterInitialOffset;
        replOffset = s.masterReplOffset;
        clearReplicationId2();
        createBacklog();
        replSeldb = -1;

        s.logNotice("MASTER <-> REPLICA sync: Finished with success");
        /* replication.c restartAOFAfterSYNC(): the dataset is a brand new one,
         * so the AOF has to be rewritten from it.  This is also what moves the
         * fsynced replication offset onto the new master's stream (WAITAOF). */
        if (s.aof != null && s.aof.state != Aof.OFF) {
            s.aof.stopAppendOnly();
            if (!s.aof.startAppendOnly()) {
                s.logWarn("Failed to enable the AOF after successfully loading "
                        + "the new dataset from the master.");
            }
        }
        // Put ourselves online on the master right away.
        sendAck();
    }

    void createMasterClient(int dbid) {
        Client m = new Client();
        m.id = s.nextClientId++;
        m.fake = true;
        m.chan = null;
        m.skey = null;
        m.db = dbid;
        m.resp = 2;
        m.authenticated = true;
        m.user = "default";
        m.addr = masterHost + ":" + masterPort;
        m.laddr = "";
        m.lastInteraction = System.currentTimeMillis();
        masterClient = m;
        /* The master is a regular client of ours: CLIENT LIST must show it and
         * CLIENT KILL must be able to drop the link (replication.c). */
        s.clients.put(m.id, m);
    }

    /**
     * networking.c freeClient(): losing the link with our master caches its
     * state so the next connection can ask for a partial resynchronization.
     */
    public void masterLinkLost() {
        s.logWarn("Connection with master lost.");
        cancelHandshake(null);
    }

    // ---- apply the streamed commands ----
    private byte[][] parsedArgv;

    /**
     * Parse one RESP array out of inBuf. Returns the number of bytes it
     * occupies, 0 when the command is not complete yet, or -1 on a protocol
     * error.
     */
    int parseOne() {
        parsedArgv = null;
        if (inBuf.len == 0) return 0;
        if (inBuf.a[0] != '*') return -1;
        int p = 0;
        int nl = findCrlf(p);
        if (nl < 0) return 0;
        long n;
        try { n = Long.parseLong(Util.s(inBuf.a, p + 1, nl - p - 1)); }
        catch (NumberFormatException e) { return -1; }
        if (n < 0 || n > 1024 * 1024) return -1;
        p = nl + 2;
        byte[][] argv = new byte[(int) n][];
        for (int i = 0; i < n; i++) {
            if (p >= inBuf.len) return 0;
            if (inBuf.a[p] != '$') return -1;
            int nl2 = findCrlf(p);
            if (nl2 < 0) return 0;
            long len;
            try { len = Long.parseLong(Util.s(inBuf.a, p + 1, nl2 - p - 1)); }
            catch (NumberFormatException e) { return -1; }
            if (len < 0 || len > 512L * 1024 * 1024) return -1;
            p = nl2 + 2;
            if (p + len + 2 > inBuf.len) return 0;
            byte[] a = new byte[(int) len];
            System.arraycopy(inBuf.a, p, a, 0, (int) len);
            argv[i] = a;
            p += len + 2;
        }
        parsedArgv = argv;
        return p;
    }

    int findCrlf(int from) {
        for (int i = from; i + 1 < inBuf.len; i++) {
            if (inBuf.a[i] == '\r' && inBuf.a[i + 1] == '\n') return i;
        }
        return -1;
    }

    void applyMasterStream() {
        while (masterClient != null && inBuf.len > 0) {
            // Masters may send bare newlines as keep-alives; they do not count.
            while (inBuf.len > 0 && (inBuf.a[0] == '\n' || inBuf.a[0] == '\r')) inBuf.consume(1);
            if (inBuf.len == 0) return;
            int consumed = parseOne();
            if (consumed == 0) return;
            if (consumed < 0) {
                cancelHandshake("Protocol error from MASTER, closing the master connection.");
                return;
            }
            byte[][] argv = parsedArgv;
            byte[] raw = new byte[consumed];
            System.arraycopy(inBuf.a, 0, raw, 0, consumed);
            inBuf.consume(consumed);

            executeFromMaster(argv);
            // The applied offset only moves once the command completed: this is
            // what makes REPLCONF GETACK reply with the pre-GETACK offset.
            replOffset += consumed;
            feedFromMasterStream(raw, 0, raw.length);
            if (masterClient == null) return;   // link was torn down by the command
        }
    }

    void executeFromMaster(byte[][] argv) {
        Client m = masterClient;
        if (m == null || argv == null || argv.length == 0) return;
        m.argv = argv;
        m.argc = argv.length;
        m.lastInteraction = System.currentTimeMillis();
        try {
            s.processCommand(m);
        } catch (RedisError e) {
            s.logWarn("Error processing command from master: " + Util.s(e.msg));
        } catch (Exception e) {
            s.logWarn("Error processing command from master: " + e);
        }
        if (m.blocked) s.unblockClient(m);
        // Masters never receive replies.
        m.out.clear();
        m.argv = null;
        m.argc = 0;
    }

    // ==================================================================
    // cron
    // ==================================================================
    public void cron() {
        long now = System.currentTimeMillis();
        long replTimeout = Math.max(1, s.config.getLong("repl-timeout"));

        pruneReplicaStates();

        // ---- replica side ----
        if (masterHost != null) {
            if ((state == ST_CONNECTING || inHandshake())
                    && now - transferLastIo > replTimeout * 1000L) {
                cancelHandshake("Timeout connecting to the MASTER...");
            } else if (state == ST_TRANSFER && now - transferLastIo > replTimeout * 1000L) {
                cancelHandshake("Timeout receiving bulk data from MASTER... If the problem"
                        + " persists try to set the 'repl-timeout' parameter in redis.conf"
                        + " to a larger value.");
            } else if (state == ST_CONNECTED && now - masterLastIo > replTimeout * 1000L) {
                cancelHandshake("MASTER timeout: no data nor PING received...");
            }
            if (state == ST_CONNECT && now - lastConnectAttempt >= 1000) {
                s.logNotice("Connecting to MASTER " + masterHost + ":" + masterPort);
                connectWithMaster();
            }
            if (state == ST_CONNECTED && masterClient != null && now - lastAckSent >= 1000) {
                sendAck();
            }
        }

        // ---- master side ----
        if (masterHost == null && !s.replicas.isEmpty()) {
            long period = Math.max(1, s.config.getLong("repl-ping-replica-period"));
            if (now >= replStreamLastIo + period * 1000L) {
                feed(-1, new byte[][]{Util.b("PING")});
            }
        }

        // disconnect timed out replicas
        if (!s.replicas.isEmpty()) {
            for (Client r : new ArrayList<>(s.replicas)) {
                ReplicaState st = rstate.get(r.id);
                if (st != null && st.prePsync) continue;
                if (!"online".equals(r.replState)) continue;
                if (r.replAckTime == 0) { r.replAckTime = now; continue; }
                if (now - r.replAckTime > replTimeout * 1000L) {
                    s.logWarn("Disconnecting timedout replica (streaming sync): " + replicaName(r));
                    s.freeClient(r);
                }
            }
        }

        if (s.replicas.isEmpty()) {
            if (noReplicasSince == 0) noReplicasSince = now;
        } else {
            noReplicasSince = 0;
        }

        resizeBacklogIfNeeded();

        // free the backlog after repl-backlog-ttl seconds without replicas
        long ttl = s.config.getLong("repl-backlog-ttl");
        if (s.replicas.isEmpty() && ttl > 0 && backlog != null && masterHost == null
                && noReplicasSince != 0 && now - noReplicasSince > ttl * 1000L) {
            changeReplicationId();
            clearReplicationId2();
            freeBacklog();
            s.logNotice("Replication backlog freed after " + ttl
                    + " seconds without connected replicas.");
        }

        processClientsWaitingReplicas(now);
    }

    void pruneReplicaStates() {
        if (rstate.isEmpty()) return;
        Iterator<Map.Entry<Long, ReplicaState>> it = rstate.entrySet().iterator();
        while (it.hasNext()) {
            Long id = it.next().getKey();
            if (!s.clients.containsKey(id)) it.remove();
        }
    }

    void processClientsWaitingReplicas(long now) {
        boolean any = false;
        for (Client c : new ArrayList<>(s.clients.values())) {
            if (!c.blocked) continue;
            boolean isAof = "waitaof".equals(c.blockType);
            if (!isAof && !"wait".equals(c.blockType)) continue;
            long[] st = (long[]) c.blockState;
            if (st == null) continue;
            if (isAof) {
                /* The AOF may have been turned off while we were blocked. */
                if (st[2] > 0 && (s.aof == null || !s.aof.isEnabled())) {
                    c.addError("ERR WAITAOF cannot be used when numlocal is set but "
                            + "appendonly is disabled.");
                    s.unblockClient(c);
                    s.updateInterest(c);
                    continue;
                }
                long local = (s.aof != null && s.aof.fsyncedReploff >= st[0]) ? 1 : 0;
                int n = countAofAcks(st[0]);
                if (n >= st[1] && local >= st[2]) {
                    c.addArray(2);
                    c.addInt(local);
                    c.addInt(n);
                    s.unblockClient(c);
                    s.updateInterest(c);
                } else {
                    any = true;
                }
                continue;
            }
            int n = countAcks(st[0]);
            if (n >= st[1]) {
                c.addInt(n);
                s.unblockClient(c);
                s.updateInterest(c);
            } else {
                any = true;
            }
        }
        if (any && now - lastGetackSent >= 1000) requestAckFromReplicas();
    }

    // ==================================================================
    // INFO
    // ==================================================================
    public String role() { return masterHost == null ? "master" : "slave"; }

    public int connectedSlaves() { return s.replicas.size(); }

    public String masterLinkStatus() { return state == ST_CONNECTED ? "up" : "down"; }

    public int masterLastIoSecondsAgo() {
        if (masterClient == null) return -1;
        return (int) ((System.currentTimeMillis() - masterLastIo) / 1000);
    }

    public int masterSyncInProgress() { return state == ST_TRANSFER ? 1 : 0; }

    public long slaveReplOffset() {
        if (masterClient != null) return replOffset;
        if (haveCachedMaster) return cachedOffset;
        return 1;
    }

    public long slaveReadReplOffset() {
        if (masterClient != null) return replOffset + inBuf.len;
        if (haveCachedMaster) return cachedOffset;
        return 1;
    }

    public int slavePriority() { return (int) s.config.getLong("replica-priority"); }

    public int slaveReadOnly() { return s.config.getBool("replica-read-only") ? 1 : 0; }

    public int replicaAnnounced() { return s.config.getBool("replica-announced") ? 1 : 0; }

    public String masterReplid() { return s.replId; }

    public String masterReplid2() { return s.replId2; }

    public long masterReplOffset() { return s.masterReplOffset; }

    public long secondReplOffset() { return s.secondReplOffset; }

    public int backlogActive() { return backlog != null ? 1 : 0; }

    public long backlogSizeCfg() { return configuredBacklogSize(); }

    public long backlogFirstByteOffset() { return backlog != null ? backlogOff : 0; }

    public long backlogHistlen() { return backlog != null ? backlogHistlen : 0; }

    public String replicaState(Client r) {
        String st = r.replState;
        return (st == null || st.isEmpty()) ? "online" : st;
    }

    public int goodReplicasCount() {
        long maxLag = s.config.getLong("min-replicas-max-lag");
        long now = System.currentTimeMillis() / 1000;
        int good = 0;
        for (Client r : s.replicas) {
            long lag = now - r.replAckTime / 1000;
            if ("online".equals(r.replState) && lag <= maxLag) good++;
        }
        return good;
    }

    /** The "slaveN:..." lines of the INFO replication section. */
    public String replicaInfoLines() {
        StringBuilder sb = new StringBuilder();
        int id = 0;
        long now = System.currentTimeMillis() / 1000;
        for (Client r : s.replicas) {
            ReplicaState st = rstate.get(r.id);
            String ip = (st != null && st.announceIp != null) ? st.announceIp : ipOf(r);
            String state0 = replicaState(r);
            long lag = "online".equals(state0) ? Math.max(0, now - r.replAckTime / 1000) : 0;
            sb.append("slave").append(id).append(":ip=").append(ip)
              .append(",port=").append(r.replAckPort)
              .append(",state=").append(state0)
              .append(",offset=").append(r.replAckOffset)
              .append(",lag=").append(lag).append("\r\n");
            id++;
        }
        return sb.toString();
    }

    /** The whole "# Replication" INFO section, header included. */
    public String infoSection() {
        return "# Replication\r\n" + infoBody();
    }

    /** The "# Replication" INFO fields without the section header. */
    public String infoBody() {
        StringBuilder sb = new StringBuilder();
        sb.append("role:").append(role()).append("\r\n");
        if (masterHost != null) {
            sb.append("master_host:").append(masterHost).append("\r\n");
            sb.append("master_port:").append(masterPort).append("\r\n");
            sb.append("master_link_status:").append(masterLinkStatus()).append("\r\n");
            sb.append("master_last_io_seconds_ago:").append(masterLastIoSecondsAgo()).append("\r\n");
            sb.append("master_sync_in_progress:").append(masterSyncInProgress()).append("\r\n");
            sb.append("slave_read_repl_offset:").append(slaveReadReplOffset()).append("\r\n");
            sb.append("slave_repl_offset:").append(slaveReplOffset()).append("\r\n");
            sb.append("replica_full_sync_buffer_size:0\r\n");
            sb.append("replica_full_sync_buffer_peak:0\r\n");
            if (state == ST_TRANSFER) {
                long total = transferSize < 0 ? 0 : transferSize;
                long read = rdbBuf == null ? 0 : rdbBuf.len;
                double perc = total != 0 ? ((double) read / total) * 100 : 0;
                sb.append("master_sync_total_bytes:").append(total).append("\r\n");
                sb.append("master_sync_read_bytes:").append(read).append("\r\n");
                sb.append("master_sync_left_bytes:").append(total - read).append("\r\n");
                sb.append(String.format(Locale.US, "master_sync_perc:%.2f\r\n", perc));
                sb.append("master_sync_last_io_seconds_ago:")
                  .append((System.currentTimeMillis() - transferLastIo) / 1000).append("\r\n");
            }
            if (state != ST_CONNECTED) {
                sb.append("master_link_down_since_seconds:")
                  .append(downSince != 0 ? (System.currentTimeMillis() - downSince) / 1000 : -1)
                  .append("\r\n");
            }
            sb.append("slave_priority:").append(slavePriority()).append("\r\n");
            sb.append("slave_read_only:").append(slaveReadOnly()).append("\r\n");
            sb.append("replica_announced:").append(replicaAnnounced()).append("\r\n");
        }
        sb.append("connected_slaves:").append(connectedSlaves()).append("\r\n");
        if (s.config.getLong("min-replicas-to-write") != 0
                && s.config.getLong("min-replicas-max-lag") != 0) {
            sb.append("min_slaves_good_slaves:").append(goodReplicasCount()).append("\r\n");
        }
        sb.append(replicaInfoLines());
        sb.append("master_failover_state:").append(failoverState).append("\r\n");
        sb.append("master_replid:").append(masterReplid()).append("\r\n");
        sb.append("master_replid2:").append(masterReplid2()).append("\r\n");
        sb.append("master_repl_offset:").append(masterReplOffset()).append("\r\n");
        sb.append("second_repl_offset:").append(secondReplOffset()).append("\r\n");
        sb.append("repl_backlog_active:").append(backlogActive()).append("\r\n");
        sb.append("repl_backlog_size:").append(backlogSizeCfg()).append("\r\n");
        sb.append("repl_backlog_first_byte_offset:").append(backlogFirstByteOffset()).append("\r\n");
        sb.append("repl_backlog_histlen:").append(backlogHistlen()).append("\r\n");
        return sb.toString();
    }

    /** State string used by the ROLE command on a replica. */
    public String replStateName() {
        if (inHandshake()) return "handshake";
        switch (state) {
            case ST_NONE: return "none";
            case ST_CONNECT: return "connect";
            case ST_CONNECTING: return "connecting";
            case ST_TRANSFER: return "sync";
            case ST_CONNECTED: return "connected";
            default: return "unknown";
        }
    }
}
