package redis;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.util.*;

/** One connection: query buffer, RESP parser, reply buffer and per-client state. */
public final class Client {
    public static final int PROTO_INLINE_MAX = 64 * 1024;
    /** networking.c PROTO_MBULK_BIG_ARG */
    public static final int PROTO_MBULK_BIG_ARG = 32 * 1024;

    public long id;
    public SocketChannel chan;
    /** non null when this connection speaks TLS (see {@link Tls}) */
    public Tls.Conn tls;
    public SelectionKey skey;
    public String addr = "";
    public String laddr = "";
    public int fd = -1;
    public boolean unixSocket;

    public final Buf query = new Buf(1024);
    public final Buf out = new Buf(1024);
    public byte[][] argv = null;
    public int argc = 0;
    /** multibulk parse state */
    private int multibulklen = 0;
    private long bulklen = -1;
    private final List<byte[]> pending = new ArrayList<>();

    public int db = 0;
    public int resp = 2;
    public String name = "";
    public String libName = "", libVer = "";
    public long createTime = System.currentTimeMillis();
    public long lastInteraction = System.currentTimeMillis();
    public boolean closeAfterReply = false;
    public boolean shouldClose = false;
    public boolean authenticated = true;
    public String user = "default";

    // MULTI
    public boolean inMulti = false;
    public boolean dirtyCas = false;
    public boolean multiError = false;
    public final List<byte[][]> multiQueue = new ArrayList<>();
    public final List<Key> watched = new ArrayList<>();
    public final List<Integer> watchedDbs = new ArrayList<>();

    // pub/sub
    public final LinkedHashSet<Key> subChannels = new LinkedHashSet<>();
    public final LinkedHashSet<Key> subPatterns = new LinkedHashSet<>();
    public final LinkedHashSet<Key> subShard = new LinkedHashSet<>();

    // blocking
    public boolean blocked = false;
    public long blockTimeoutMs = 0;
    public String blockType = "";
    public List<Key> blockKeys = new ArrayList<>();
    public Object blockState;

    // replication
    public boolean isReplica = false;
    public boolean isMonitor = false;
    public int replAckPort = 0;
    public long replAckOffset = 0;
    public long replAckTime = 0;
    public String replState = "";

    public boolean fake = false;   // Lua / AOF loading pseudo-client

    public Client() {}

    public String cmdName() {
        return argc > 0 ? Util.lower(argv[0]) : "";
    }

    // ---------------- reply helpers ----------------
    public void addOk() { Reply.ok(out); }
    public void addSimple(String s) { Reply.simple(out, s); }
    public void addError(String s) { Reply.error(out, s); }
    public void addErr(byte[] s) { Reply.error(out, s); }
    public void addInt(long v) { Reply.integer(out, v); }
    public void addBulk(byte[] v) { Reply.bulk(out, v); }
    public void addBulk(String v) { Reply.bulk(out, v); }
    public void addNil() { Reply.nil(out, resp); }
    public void addNilArray() { Reply.nilArray(out, resp); }
    public void addArray(int n) { Reply.array(out, n); }
    public void addMap(int n) { Reply.map(out, n, resp); }
    public void addSet(int n) { Reply.set(out, n, resp); }
    /** networking.c CLIENT_PUSHING: a push is never suppressed by CLIENT REPLY OFF/SKIP. */
    public boolean pushing = false;
    public void addPush(int n) { pushing = true; Reply.push(out, n, resp); }
    public void addBool(boolean b) { Reply.bool(out, b, resp); }
    public void addDouble(double d) { Reply.dbl(out, d, resp); }
    public void addVerbatim(String fmt, byte[] v) { Reply.verbatim(out, fmt, v, resp); }
    public void addBigNumber(String v) { Reply.bigNumber(out, v, resp); }
    public void addEmptyArray() { Reply.emptyArray(out); }
    public void addRaw(byte[] b) { out.put(b); }

    // ---------------- argument helpers ----------------
    public byte[] arg(int i) { return argv[i]; }
    public String argStr(int i) { return Util.s(argv[i]); }
    public String argLower(int i) { return Util.lower(argv[i]); }

    public long argLong(int i) {
        Long v = Util.string2ll(argv[i]);
        if (v == null) throw RedisError.notInt();
        return v;
    }

    public long argLong(int i, String err) {
        Long v = Util.string2ll(argv[i]);
        if (v == null) throw new RedisError(err);
        return v;
    }

    public double argDouble(int i) {
        Double v = Util.string2d(argv[i]);
        if (v == null) throw RedisError.notFloat();
        return v;
    }

    public boolean argIs(int i, String s) { return Util.equalsIgnoreCase(argv[i], s); }

    // ---------------- protocol parsing ----------------
    /**
     * Bytes of the command currently being executed. From redis 6.0 the query
     * buffer is not trimmed as it is parsed -- `c->qb_pos` advances instead and
     * `sdslen(c->querybuf)` still counts the command in flight, which is why
     * CLIENT INFO run by a client reports qbuf=26 for its own `CLIENT INFO`.
     * This parser consumes as it goes, so the figure is accumulated here.
     */
    public long curCmdBytes;
    private long cmdBytesAcc;

    private void consumeQuery(int n) {
        query.consume(n);
        cmdBytesAcc += n;
    }

    /** Returns true when a full command was parsed into argv/argc. */
    public boolean parseNext() {
        boolean got;
        if (multibulklen == 0 && bulklen == -1) {
            if (query.len == 0) return false;
            got = (query.a[0] == '*') ? parseMultibulk() : parseInline();
        } else {
            got = parseMultibulk();
        }
        if (got) { curCmdBytes = cmdBytesAcc; cmdBytesAcc = 0; }
        return got;
    }

    private int findCrlf(int from) {
        for (int i = from; i + 1 < query.len; i++) {
            if (query.a[i] == '\r' && query.a[i + 1] == '\n') return i;
        }
        return -1;
    }

    private boolean parseInline() {
        int nl = -1;
        for (int i = 0; i < query.len; i++) if (query.a[i] == '\n') { nl = i; break; }
        if (nl < 0) {
            if (query.len > PROTO_INLINE_MAX) {
                protocolError("too big inline request");
            }
            return false;
        }
        int end = nl;
        if (end > 0 && query.a[end - 1] == '\r') end--;
        String line = Util.s(query.a, 0, end);
        consumeQuery(nl + 1);
        List<String> parts;
        try {
            parts = Config.splitArgs(line);
        } catch (IllegalArgumentException e) {
            protocolError("unbalanced quotes in request");
            return false;
        }
        // an empty inline line is an empty command; keep parsing (see above)
        if (parts.isEmpty()) { argc = 0; argv = null; return true; }
        argv = new byte[parts.size()][];
        for (int i = 0; i < parts.size(); i++) argv[i] = Util.b(parts.get(i));
        argc = argv.length;
        return true;
    }

    private boolean parseMultibulk() {
        if (multibulklen == 0) {
            int nl = findCrlf(0);
            if (nl < 0) {
                if (query.len > PROTO_INLINE_MAX) protocolError("too big mbulk count string");
                return false;
            }
            String head = Util.s(query.a, 1, nl - 1);
            long n;
            try { n = Long.parseLong(head); }
            catch (NumberFormatException e) { protocolError("invalid multibulk length"); return false; }
            if (n > 1024L * 1024L) { protocolError("invalid multibulk length"); return false; }
            consumeQuery(nl + 2);
            // networking.c: a multibulk count <= 0 is an empty command -- the
            // client is reset and the loop goes on to whatever follows in the
            // same buffer. Returning false here stalled a pipelined PING until
            // more bytes arrived (unit/protocol "Negative multibulk length"
            // under load, wave-A 2026-10-12 oracle-a1b).
            if (n <= 0) { argc = 0; argv = null; return true; }
            multibulklen = (int) n;
            pending.clear();
            bulklen = -1;
        }
        while (multibulklen > 0) {
            if (bulklen == -1) {
                int nl = findCrlf(0);
                if (nl < 0) {
                    if (query.len > PROTO_INLINE_MAX) protocolError("too big bulk count string");
                    return false;
                }
                if (query.a[0] != '$') {
                    protocolError("expected '$', got '" + (char) query.a[0] + "'");
                    return false;
                }
                long n;
                try { n = Long.parseLong(Util.s(query.a, 1, nl - 1)); }
                catch (NumberFormatException e) { protocolError("invalid bulk length"); return false; }
                if (n < 0 || n > Server.I.protoMaxBulkLen()) { protocolError("invalid bulk length"); return false; }
                consumeQuery(nl + 2);
                bulklen = n;
                /* networking.c processMultibulkBuffer(): for a big argument the
                 * query buffer is grown to the exact size right away, which is
                 * what makes the client's memory usage visible (and evictable)
                 * before the argument has fully arrived. */
                if (bulklen >= PROTO_MBULK_BIG_ARG && query.len < bulklen + 2)
                    query.ensure((int) (bulklen + 2 - query.len));
            }
            if (query.len < bulklen + 2) return false;
            byte[] a = new byte[(int) bulklen];
            System.arraycopy(query.a, 0, a, 0, (int) bulklen);
            consumeQuery((int) bulklen + 2);
            pending.add(a);
            bulklen = -1;
            multibulklen--;
        }
        argv = pending.toArray(new byte[0][]);
        argc = argv.length;
        pending.clear();
        return true;
    }

    public void protocolError(String msg) {
        addError("ERR Protocol error: " + msg);
        closeAfterReply = true;
        query.clear();
        multibulklen = 0;
        bulklen = -1;
    }

    public void resetParser() { multibulklen = 0; bulklen = -1; pending.clear(); }

    public boolean hasPendingInput() { return query.len > 0; }

    /** catClientInfoString(): the CLIENT LIST line, field order and names
     *  exactly as redis 2.8 src/networking.c emits them. */
    public String infoLine() {
        long now = System.currentTimeMillis();
        StringBuilder sb = new StringBuilder(200);
        sb.append("addr=").append(addr)
          .append(" fd=").append(fd)
          .append(" name=").append(name)
          .append(" age=").append((now - createTime) / 1000)
          .append(" idle=").append((now - lastInteraction) / 1000)
          .append(" flags=").append(flagsString())
          .append(" db=").append(db)
          .append(" sub=").append(subChannels.size())
          .append(" psub=").append(subPatterns.size())
          .append(" multi=").append(inMulti ? multiQueue.size() : -1)
          .append(" qbuf=").append(query.len)
          .append(" qbuf-free=").append(Math.max(0, query.a.length - query.len))
          .append(" obl=").append(out.len)
          .append(" oll=0 omem=0")
          .append(" events=r")
          .append(" cmd=").append(lastCmd.isEmpty() ? "NULL" : lastCmd);
        /* networking.c catClientInfoString() grew a lot of fields after 2.8.
         * They are appended here, after the fields the old harness matches
         * positionally, so that both the old and the new shape are readable. */
        sb.append(" user=").append(user)
          .append(" redir=").append(tracking ? trackingRedir : -1)
          .append(" resp=").append(resp)
          .append(" lib-name=").append(libName)
          .append(" lib-ver=").append(libVer)
          .append(" id=").append(id)
          .append(" laddr=").append(laddr)
          .append(" ssub=").append(subShard.size())
          .append(" watch=").append(watched.size())
          .append(" argv-mem=").append(argvMem())
          .append(" multi-mem=").append(multiMem())
          .append(" rbs=").append(out.a.length)
          .append(" rbp=").append(rbPeak)
          .append(" tot-net-in=").append(totNetIn)
          .append(" tot-net-out=").append(totNetOut)
          .append(" tot-mem=").append(memoryUsage());
        return sb.toString();
    }

    /**
     * catClientInfoString() as it stood from redis 6.0, which is the shape
     * CLIENT INFO -- introduced in that same release -- has always had.
     *
     * This port serves the whole release train from one binary, and the two
     * shapes cannot be reconciled in a single string: 2.8 asks for
     * `... omem=0 events=r ...` with `qbuf=0`, while 6.0 asks for
     * `... omem=0 tot-mem=<n> events=r ...` with `qbuf` counting the command in
     * flight. Splitting them along the subcommand boundary is exact for each:
     * CLIENT LIST keeps the 2.8 line (see infoLine()), CLIENT INFO -- a command
     * that does not exist before 6.0 -- reports the 6.0 one.
     *
     * The fields added after 6.2 (ssub, watch, lib-name, lib-ver) are appended
     * at the end rather than in their upstream 7.x positions, which keeps the
     * 6.0/6.2 field order intact while still reporting every later field.
     */
    public String infoLineModern() {
        long now = System.currentTimeMillis();
        StringBuilder sb = new StringBuilder(256);
        sb.append("id=").append(id)
          .append(" addr=").append(addr)
          .append(" laddr=").append(laddr)
          .append(" fd=").append(fd)
          .append(" name=").append(name)
          .append(" age=").append((now - createTime) / 1000)
          .append(" idle=").append((now - lastInteraction) / 1000)
          .append(" flags=").append(flagsString())
          .append(" db=").append(db)
          .append(" sub=").append(subChannels.size())
          .append(" psub=").append(subPatterns.size())
          .append(" multi=").append(inMulti ? multiQueue.size() : -1)
          .append(" qbuf=").append(curCmdBytes + query.len)
          .append(" qbuf-free=").append(Math.max(0, query.a.length - query.len))
          .append(" argv-mem=").append(argvMem())
          .append(" multi-mem=").append(multiMem())
          .append(" tot-net-in=").append(totNetIn)
          .append(" tot-net-out=").append(totNetOut)
          .append(" rbs=").append(out.a.length)
          .append(" rbp=").append(rbPeak)
          .append(" obl=").append(out.len)
          .append(" oll=0 omem=0")
          .append(" tot-mem=").append(memoryUsage())
          .append(" events=r")
          .append(" cmd=").append(lastCmd.isEmpty() ? "NULL" : lastCmd)
          .append(" user=").append(user)
          .append(" redir=").append(tracking ? trackingRedir : -1)
          .append(" resp=").append(resp)
          .append(" ssub=").append(subShard.size())
          .append(" watch=").append(watched.size())
          .append(" lib-name=").append(libName)
          .append(" lib-ver=").append(libVer);
        return sb.toString();
    }

    /**
     * networking.c getClientMemoryUsage(): the query buffer, the reply buffer,
     * the current argument vector and the queued MULTI commands.  redis shrinks
     * both buffers from its cron, our Buf only grows, so the *used* length is
     * the honest figure here.
     */
    public long memoryUsage() {
        long mem = query.a.length + out.a.length + argvMem() + multiMem() + CLIENT_STRUCT_SIZE;
        /* multiStateMemOverhead(): the watched key list. */
        mem += watched.size() * 64L;
        /* pubsubMemOverhead(): the per client channel/pattern dictionaries. */
        mem += (subChannels.size() + subPatterns.size() + subShard.size()) * 64L;
        /* the client side caching prefixes (an underestimation in redis too). */
        mem += trackingPrefixes.size() * 128L;
        return mem;
    }

    /**
     * server.c clientsCronResizeQueryBuffer(): give back a query buffer that is
     * much larger than what the client actually needs, so that one big command
     * does not keep the connection "expensive" forever.
     */
    public void resizeQueryBuffer() {
        if (query.a.length > PROTO_MBULK_BIG_ARG && query.len < query.a.length / 2) {
            int cap = Math.max(query.len, 1024);
            byte[] n = new byte[cap];
            System.arraycopy(query.a, 0, n, 0, query.len);
            query.a = n;
        }
        /* clientsCronResizeOutputBuffer(): shrink a reply buffer whose peak
         * usage over the last cycle was well below what is allocated.
         * DEBUG REPLYBUFFER RESIZING 0 turns this off for the tests. */
        if (replyBufferResizing && out.len == 0 && out.a.length > 1024
                && rbPeak < out.a.length / 2) out.a = new byte[1024];
        rbPeak = 0;
    }

    /** sizeof(client) in redis 7.x, near enough for the eviction accounting. */
    public static final long CLIENT_STRUCT_SIZE = 20512;

    /** the peak of the reply buffer since the last reset (CLIENT INFO rbp) */
    public long rbPeak = 1024;

    /** debug.c DEBUG REPLYBUFFER RESIZING <0|1> */
    public static boolean replyBufferResizing = true;

    public long totNetIn = 0, totNetOut = 0;

    /**
     * The arguments of a multibulk command that has been partially received:
     * they have left the query buffer but are not a command yet, and redis
     * counts them as part of the client's query buffer.
     */
    public long pendingMem() {
        long n = 0;
        for (byte[] a : pending) if (a != null) n += a.length;
        return n;
    }

    long argvMem() {
        long n = 10;
        if (argv == null) return n;
        /* multi.c queueMultiCommand() moves argv into the transaction state and
         * leaves the client without one, so a queued command is accounted once. */
        if (!multiQueue.isEmpty() && multiQueue.get(multiQueue.size() - 1) == argv) return n;
        for (byte[] a : argv) if (a != null) n += a.length + 8;
        return n;
    }

    long multiMem() {
        long n = 0;
        for (byte[][] cmd : multiQueue) {
            if (cmd == null) continue;
            for (byte[] a : cmd) if (a != null) n += a.length;
        }
        return n;
    }

    public String lastCmd = "";
    public long trackingRedir = -1;
    public boolean tracking = false;
    public boolean trackingBcast = false;
    public boolean trackingOptIn = false, trackingOptOut = false, trackingNoloop = false;
    public boolean trackingCachingOn = false;
    public boolean trackingBrokenRedir = false;
    public final List<byte[]> trackingPrefixes = new ArrayList<>();
    public boolean noEvict = false, noTouch = false;
    /** replication.c: the master offset right after this client's last write. */
    public long woff = 0;
    /** replication.c client->repl_aof_off: REPLCONF ACK ... FACK <offset>. */
    public long replAofOffset = 0;
    public boolean replyOff = false, replySkipNext = false, replySkip = false;
    /** REDIS_ASKING: set by ASKING, consumed by the next command (cluster.c). */
    public boolean asking = false;
    /** REDIS_READONLY: set by READONLY, cleared by READWRITE. */
    public boolean readonlyMode = false;

    /** networking.c catClientInfoString() flag characters (redis 2.8). */
    public String flagsString() {
        StringBuilder f = new StringBuilder();
        if (isReplica || isMonitor) f.append(isMonitor ? 'O' : 'S');
        if (Server.I != null && Server.I.repl != null && Server.I.repl.isMasterClient(this)) f.append('M');
        if (inMulti) f.append('x');
        if (blocked) f.append('b');
        if (dirtyCas) f.append('d');
        if (closeAfterReply) f.append('c');
        if (unixSocket) f.append('U');
        if (readonlyMode) f.append('r');
        if (noEvict) f.append('e');
        if (noTouch) f.append('T');
        if (f.length() == 0) f.append('N');
        return f.toString();
    }

    public void write() throws IOException {
        if (out.len > rbPeak) rbPeak = out.len;
        if (chan == null) return;
        if (tls != null) { Tls.write(this); return; }
        if (out.len == 0) return;
        ByteBuffer bb = ByteBuffer.wrap(out.a, 0, out.len);
        int w = chan.write(bb);
        if (w > 0) out.consume(w);
    }
}
