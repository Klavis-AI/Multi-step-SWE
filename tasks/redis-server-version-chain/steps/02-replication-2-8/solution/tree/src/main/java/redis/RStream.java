package redis;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.TreeMap;

/**
 * The stream value container: a port of the {@code stream} structure of
 * redis src/t_stream.c and src/stream.h (8.0.6).
 *
 * Redis stores a stream as a radix tree of "macro nodes", each node being a
 * listpack whose first entries form a "master entry" (count, deleted,
 * num-master-fields, the master fields, and a zero terminator) followed by the
 * real entries, delta encoded against the node's master ID.  We keep exactly
 * the same layout -- {@link Node#lp} is the flat list of listpack elements --
 * because the node boundaries are observable: {@code XADD ... MAXLEN ~} only
 * drops whole nodes, {@code XINFO STREAM} reports {@code radix-tree-keys}, and
 * the RDB format stores the listpacks verbatim.
 */
public final class RStream {

    /* ------------------------------------------------------------------ */
    /* stream IDs                                                          */
    /* ------------------------------------------------------------------ */

    /** streamID: two 64 bit *unsigned* counters. */
    public static final class Id implements Comparable<Id> {
        public long ms, seq;

        public Id() {}
        public Id(long ms, long seq) { this.ms = ms; this.seq = seq; }
        public Id(Id o) { this.ms = o.ms; this.seq = o.seq; }

        public Id copy() { return new Id(ms, seq); }

        /** streamCompareID() */
        @Override public int compareTo(Id o) {
            int c = Long.compareUnsigned(ms, o.ms);
            if (c != 0) return c;
            return Long.compareUnsigned(seq, o.seq);
        }

        @Override public boolean equals(Object o) {
            return (o instanceof Id) && ((Id) o).ms == ms && ((Id) o).seq == seq;
        }

        @Override public int hashCode() { return (int) (ms * 31 + seq); }

        /** createStreamIDString(): "%U-%U" */
        @Override public String toString() {
            return Long.toUnsignedString(ms) + "-" + Long.toUnsignedString(seq);
        }

        public byte[] bytes() { return Util.b(toString()); }

        /** streamIDEqZero() */
        public boolean isZero() { return ms == 0 && seq == 0; }

        public boolean isMax() { return ms == -1L && seq == -1L; }

        /** streamIncrID(): false (C_ERR) when the ID was already the maximum. */
        public boolean incr() {
            if (seq == -1L) {
                if (ms == -1L) { ms = 0; seq = 0; return false; }
                ms++;
                seq = 0;
            } else {
                seq++;
            }
            return true;
        }

        /** streamDecrID(): false (C_ERR) when the ID was already 0-0. */
        public boolean decr() {
            if (seq == 0) {
                if (ms == 0) { ms = -1L; seq = -1L; return false; }
                ms--;
                seq = -1L;
            } else {
                seq--;
            }
            return true;
        }

        /** streamEncodeID(): the 128 bit big endian form used as a rax key. */
        public byte[] encode() {
            byte[] b = new byte[16];
            for (int i = 0; i < 8; i++) {
                b[i] = (byte) (ms >>> (56 - 8 * i));
                b[8 + i] = (byte) (seq >>> (56 - 8 * i));
            }
            return b;
        }

        /** streamDecodeID() */
        public static Id decode(byte[] b, int off) {
            long ms = 0, seq = 0;
            for (int i = 0; i < 8; i++) {
                ms = (ms << 8) | (b[off + i] & 0xffL);
                seq = (seq << 8) | (b[off + 8 + i] & 0xffL);
            }
            return new Id(ms, seq);
        }
    }

    public static Id minId() { return new Id(0, 0); }
    public static Id maxId() { return new Id(-1L, -1L); }

    /* ------------------------------------------------------------------ */
    /* entry flags (t_stream.c)                                            */
    /* ------------------------------------------------------------------ */
    public static final int FLAG_NONE = 0, FLAG_DELETED = 1, FLAG_SAMEFIELDS = 2;

    /** listpack.c limits mirrored by t_stream.c. */
    static final long STREAM_LISTPACK_MAX_SIZE = 1L << 30;

    /* ------------------------------------------------------------------ */
    /* macro nodes                                                         */
    /* ------------------------------------------------------------------ */

    /** One radix tree node: the flat listpack element list plus its lpBytes(). */
    public static final class Node {
        public final ArrayList<byte[]> lp;
        /** lpBytes(): header + entries + the EOF byte. */
        public long bytes;

        public Node() { lp = new ArrayList<>(); bytes = Listpack.LP_HDR_SIZE + 1; }

        public Node(List<byte[]> elems) {
            lp = new ArrayList<>(elems);
            bytes = Listpack.LP_HDR_SIZE + 1;
            for (byte[] e : lp) bytes += elemBytes(e);
        }

        void add(byte[] e) { lp.add(e); bytes += elemBytes(e); }

        void set(int i, byte[] e) {
            bytes -= elemBytes(lp.get(i));
            lp.set(i, e);
            bytes += elemBytes(e);
        }

        long getInt(int i) {
            Long v = Util.string2ll(lp.get(i));
            return v == null ? 0 : v;
        }

        void setInt(int i, long v) { set(i, Util.ll2string(v)); }

        public byte[] encode() { return Listpack.encode(lp); }
    }

    /** lpEntrySizeInteger()/lpEncodeGetType(): bytes one element occupies. */
    static long elemBytes(byte[] e) {
        Long v = Util.string2ll(e);
        long enclen;
        if (v != null) enclen = intEncLen(v);
        else enclen = strEncLen(e.length);
        return enclen + backlenBytes(enclen);
    }

    private static int intEncLen(long v) {
        if (v >= 0 && v <= 127) return 1;
        if (v >= -4096 && v <= 4095) return 2;
        if (v >= -32768 && v <= 32767) return 3;
        if (v >= -8388608 && v <= 8388607) return 4;
        if (v >= -2147483648L && v <= 2147483647L) return 5;
        return 9;
    }

    private static int strEncLen(int size) {
        if (size < 64) return 1 + size;
        if (size < 4096) return 2 + size;
        return 5 + size;
    }

    private static int backlenBytes(long l) {
        if (l <= 127) return 1;
        if (l < 16383) return 2;
        if (l < 2097151) return 3;
        if (l < 268435455) return 4;
        return 5;
    }

    /* ------------------------------------------------------------------ */
    /* consumer groups                                                     */
    /* ------------------------------------------------------------------ */

    public static final long INVALID_ENTRIES_READ = -1;

    public static final class NACK {
        public long deliveryTime;
        public long deliveryCount;
        public Consumer consumer;

        /** streamCreateNACK() */
        public NACK(Consumer c) {
            deliveryTime = Server.I == null ? System.currentTimeMillis() : Server.I.mstime();
            deliveryCount = 1;
            consumer = c;
        }

        public NACK(Consumer c, long time, long count) {
            deliveryTime = time; deliveryCount = count; consumer = c;
        }
    }

    public static final class Consumer {
        public byte[] name;
        public long seenTime;
        public long activeTime = -1;
        public final TreeMap<Id, NACK> pel = new TreeMap<>();

        public Consumer(byte[] name) { this.name = name; }
    }

    public static final class CG {
        public Id lastId = new Id(0, 0);
        public long entriesRead = INVALID_ENTRIES_READ;
        public final TreeMap<Id, NACK> pel = new TreeMap<>();
        /** rax order: lexicographic by consumer name. */
        public final TreeMap<Key, Consumer> consumers = new TreeMap<>();
    }

    /* ------------------------------------------------------------------ */
    /* the stream itself                                                   */
    /* ------------------------------------------------------------------ */

    public final TreeMap<Id, Node> rax = new TreeMap<>();
    public long length = 0;
    public Id lastId = new Id(0, 0);
    public Id firstId = new Id(0, 0);
    public Id maxDeletedEntryId = new Id(0, 0);
    public long entriesAdded = 0;
    /** Created on demand, like redis does. */
    public TreeMap<Key, CG> cgroups = null;

    public CG lookupCG(byte[] name) {
        if (cgroups == null) return null;
        return cgroups.get(new Key(name));
    }

    /** streamCreateCG(): null when the group already exists. */
    public CG createCG(byte[] name, Id id, long entriesRead) {
        if (cgroups == null) cgroups = new TreeMap<>();
        Key k = new Key(name);
        if (cgroups.containsKey(k)) return null;
        CG cg = new CG();
        cg.lastId = id.copy();
        cg.entriesRead = entriesRead;
        cgroups.put(k, cg);
        return cg;
    }

    public int cgroupCount() { return cgroups == null ? 0 : cgroups.size(); }

    /* ------------------------------------------------------------------ */
    /* node scanning                                                       */
    /* ------------------------------------------------------------------ */

    /** One entry as found inside a node's flat listpack element list. */
    public static final class Pos {
        public int flagsIdx;      /* index of the 'flags' element */
        public int flags;
        public Id id;
        public int numFields;
        public int valuesIdx;     /* index of the first field/value element */
        public boolean sameFields;
    }

    /** Master fields of a node start at index 3 and there are getInt(2) of them. */
    public static int masterFieldsCount(Node n) { return (int) n.getInt(2); }

    /** Walk a node and return every entry it holds (tombstones included). */
    public static List<Pos> scan(Id masterId, Node n) {
        List<Pos> out = new ArrayList<>();
        int mf = masterFieldsCount(n);
        int p = 3 + mf + 1;      /* skip master fields and the zero terminator */
        int size = n.lp.size();
        while (p < size) {
            Pos e = new Pos();
            e.flagsIdx = p;
            e.flags = (int) n.getInt(p);
            long msDelta = n.getInt(p + 1);
            long seqDelta = n.getInt(p + 2);
            e.id = new Id(masterId.ms + msDelta, masterId.seq + seqDelta);
            e.sameFields = (e.flags & FLAG_SAMEFIELDS) != 0;
            if (e.sameFields) {
                e.numFields = mf;
                e.valuesIdx = p + 3;
                p = e.valuesIdx + mf + 1;        /* values + lp-count */
            } else {
                e.numFields = (int) n.getInt(p + 3);
                e.valuesIdx = p + 4;
                p = e.valuesIdx + e.numFields * 2 + 1;
            }
            out.add(e);
        }
        return out;
    }

    /** The flattened field/value pairs of one entry. */
    public static List<byte[]> fieldsOf(Node n, Pos e) {
        List<byte[]> out = new ArrayList<>(e.numFields * 2);
        if (e.sameFields) {
            for (int i = 0; i < e.numFields; i++) {
                out.add(n.lp.get(3 + i));
                out.add(n.lp.get(e.valuesIdx + i));
            }
        } else {
            for (int i = 0; i < e.numFields * 2; i++) out.add(n.lp.get(e.valuesIdx + i));
        }
        return out;
    }

    /* ------------------------------------------------------------------ */
    /* iteration                                                           */
    /* ------------------------------------------------------------------ */

    /** One emitted entry. */
    public static final class Entry {
        public Id id;
        public int flags;
        public List<byte[]> fields;
        public Node node;
        public Pos pos;
        public Id masterId;
    }

    /** streamIterator: iterate the entries between start and end (inclusive). */
    public final class Iter {
        private final Id start, end;
        private final boolean rev;
        public boolean skipTombstones = true;
        private Iterator<Map.Entry<Id, Node>> nodeIt;
        private Id curMaster;
        private Node curNode;
        private List<Pos> cur;
        private int idx;
        private boolean finished;

        Iter(Id start, Id end, boolean rev) {
            this.start = start == null ? minId() : start;
            this.end = end == null ? maxId() : end;
            this.rev = rev;
            reseek();
        }

        private void reseek() {
            NavigableMap<Id, Node> m;
            if (!rev) {
                Id from = start.isZero() ? null : rax.floorKey(start);
                m = (from == null) ? rax : rax.tailMap(from, true);
                nodeIt = m.entrySet().iterator();
            } else {
                Id from = end.isMax() ? null : rax.floorKey(end);
                m = (from == null) ? rax : rax.headMap(from, true);
                nodeIt = m.descendingMap().entrySet().iterator();
            }
            cur = null;
        }

        public Entry next() {
            if (finished) return null;
            while (true) {
                if (cur == null) {
                    if (!nodeIt.hasNext()) return null;
                    Map.Entry<Id, Node> e = nodeIt.next();
                    curMaster = e.getKey();
                    curNode = e.getValue();
                    cur = scan(curMaster, curNode);
                    idx = rev ? cur.size() - 1 : 0;
                }
                while (rev ? idx >= 0 : idx < cur.size()) {
                    Pos p = cur.get(idx);
                    idx += rev ? -1 : 1;
                    boolean tomb = (p.flags & FLAG_DELETED) != 0;
                    if (!rev) {
                        if (p.id.compareTo(start) >= 0 && (!skipTombstones || !tomb)) {
                            if (p.id.compareTo(end) > 0) { finished = true; return null; }
                            return emit(p);
                        }
                    } else {
                        if (p.id.compareTo(end) <= 0 && (!skipTombstones || !tomb)) {
                            if (p.id.compareTo(start) < 0) { finished = true; return null; }
                            return emit(p);
                        }
                    }
                }
                cur = null;
            }
        }

        private Entry emit(Pos p) {
            Entry e = new Entry();
            e.id = p.id;
            e.flags = p.flags;
            e.node = curNode;
            e.pos = p;
            e.masterId = curMaster;
            e.fields = fieldsOf(curNode, p);
            return e;
        }

    }

    /**
     * streamIteratorRemoveEntry(): mark the entry as deleted, or drop the whole
     * node when it held the last live entry.
     */
    public void removeEntry(Entry e) {
        Node n = e.node;
        n.setInt(e.pos.flagsIdx, e.pos.flags | FLAG_DELETED);
        long aux = n.getInt(0);
        if (aux == 1) {
            rax.remove(e.masterId);
        } else {
            n.setInt(0, aux - 1);
            n.setInt(1, n.getInt(1) + 1);
        }
        length--;
    }

    public Iter iter(Id start, Id end, boolean rev) { return new Iter(start, end, rev); }

    /* ------------------------------------------------------------------ */
    /* basic operations                                                    */
    /* ------------------------------------------------------------------ */

    /** streamGetEdgeID() */
    public Id getEdgeID(boolean first, boolean skipTombstones) {
        Iter it = new Iter(null, null, !first);
        it.skipTombstones = skipTombstones;
        Entry e = it.next();
        if (e == null) return first ? maxId() : minId();
        return e.id;
    }

    /** streamLastValidID() */
    public Id lastValidID() {
        Iter it = new Iter(null, null, true);
        Entry e = it.next();
        return e == null ? new Id(0, 0) : e.id;
    }

    /** streamEntryExists() */
    public boolean entryExists(Id id) {
        Iter it = new Iter(id, id, false);
        return it.next() != null;
    }

    public Entry lookup(Id id) {
        Iter it = new Iter(id, id, false);
        return it.next();
    }

    /** streamDeleteItem() */
    public boolean deleteItem(Id id) {
        Iter it = new Iter(id, id, false);
        Entry e = it.next();
        if (e == null) return false;
        removeEntry(e);
        return true;
    }

    /** Thrown by appendItem() the way streamAppendItem() sets errno. */
    public static final class AppendError extends RuntimeException {
        public final boolean domain;  /* EDOM: id <= last_id */
        public AppendError(boolean domain) { super(null, null, false, false); this.domain = domain; }
    }

    /** streamNextID() */
    private Id nextID() {
        long ms = Server.I == null ? System.currentTimeMillis() : Server.I.mstime();
        if (Long.compareUnsigned(ms, lastId.ms) > 0) return new Id(ms, 0);
        Id id = lastId.copy();
        id.incr();
        return id;
    }

    /**
     * streamAppendItem(): append one entry made of the flattened
     * field/value pairs in `fv`.
     */
    public Id appendItem(List<byte[]> fv, Id useId, boolean seqGiven,
                         long nodeMaxBytes, long nodeMaxEntries) {
        int numfields = fv.size() / 2;
        Id id;
        if (useId != null) {
            if (seqGiven) {
                id = useId.copy();
            } else {
                if (lastId.ms == useId.ms) {
                    if (lastId.seq == -1L) throw new AppendError(true);
                    id = lastId.copy();
                    id.seq++;
                } else {
                    id = useId.copy();
                }
            }
        } else {
            id = nextID();
        }

        if (id.compareTo(lastId) <= 0) throw new AppendError(true);

        long totelelen = 0;
        for (byte[] e : fv) totelelen += e.length;
        if (totelelen > STREAM_LISTPACK_MAX_SIZE) throw new AppendError(false);

        Map.Entry<Id, Node> tail = rax.lastEntry();
        Node n = tail == null ? null : tail.getValue();
        Id masterId = tail == null ? null : tail.getKey();

        if (n != null) {
            boolean newNode = false;
            long maxBytes = nodeMaxBytes;
            if (maxBytes == 0 || maxBytes > STREAM_LISTPACK_MAX_SIZE) maxBytes = STREAM_LISTPACK_MAX_SIZE;
            if (n.bytes + totelelen >= maxBytes) {
                newNode = true;
            } else if (nodeMaxEntries != 0) {
                long count = n.getInt(0) + n.getInt(1);
                if (count >= nodeMaxEntries) newNode = true;
            }
            if (newNode) { n = null; masterId = null; }
        }

        int flags = FLAG_NONE;
        if (n == null) {
            masterId = id.copy();
            n = new Node();
            n.add(Util.ll2string(1));            /* one item */
            n.add(Util.ll2string(0));            /* zero deleted */
            n.add(Util.ll2string(numfields));
            for (int i = 0; i < numfields; i++) n.add(fv.get(i * 2));
            n.add(Util.ll2string(0));            /* master entry terminator */
            rax.put(masterId, n);
            flags |= FLAG_SAMEFIELDS;
        } else {
            n.setInt(0, n.getInt(0) + 1);
            int mf = masterFieldsCount(n);
            if (numfields == mf) {
                int i = 0;
                for (; i < mf; i++) {
                    byte[] field = fv.get(i * 2);
                    if (!java.util.Arrays.equals(field, n.lp.get(3 + i))) break;
                }
                if (i == mf) flags |= FLAG_SAMEFIELDS;
            }
        }

        n.add(Util.ll2string(flags));
        n.add(Util.ll2string(id.ms - masterId.ms));
        n.add(Util.ll2string(id.seq - masterId.seq));
        if ((flags & FLAG_SAMEFIELDS) == 0) n.add(Util.ll2string(numfields));
        for (int i = 0; i < numfields; i++) {
            if ((flags & FLAG_SAMEFIELDS) == 0) n.add(fv.get(i * 2));
            n.add(fv.get(i * 2 + 1));
        }
        long lpCount = numfields + 3;
        if ((flags & FLAG_SAMEFIELDS) == 0) lpCount += numfields + 1;
        n.add(Util.ll2string(lpCount));

        length++;
        entriesAdded++;
        lastId = id.copy();
        if (length == 1) firstId = id.copy();
        return id;
    }

    /* ------------------------------------------------------------------ */
    /* trimming                                                            */
    /* ------------------------------------------------------------------ */

    public static final int TRIM_NONE = 0, TRIM_MAXLEN = 1, TRIM_MINID = 2;

    /** streamTrim(): returns the number of entries removed. */
    public long trim(int strategy, long maxlen, Id minid, boolean approx, long limit) {
        if (strategy == TRIM_NONE) return 0;

        long deleted = 0;
        List<Id> keys = new ArrayList<>(rax.keySet());
        for (Id masterId : keys) {
            Node n = rax.get(masterId);
            if (n == null) continue;
            if (strategy == TRIM_MAXLEN && Long.compareUnsigned(length, maxlen) <= 0) break;

            long entries = n.getInt(0);
            if (limit != 0 && (deleted + entries) > limit) break;

            boolean removeNode;
            if (strategy == TRIM_MAXLEN) {
                removeNode = (length - entries) >= maxlen;
            } else {
                Id lastInNode = nodeLastId(masterId, n);
                removeNode = lastInNode != null && lastInNode.compareTo(minid) < 0;
            }

            if (removeNode) {
                rax.remove(masterId);
                length -= entries;
                deleted += entries;
                continue;
            }

            if (approx) break;

            /* Trim entries from within this node. */
            long deletedFromLp = 0;
            for (Pos p : scan(masterId, n)) {
                boolean stop;
                if (strategy == TRIM_MAXLEN) stop = Long.compareUnsigned(length, maxlen) <= 0;
                else stop = p.id.compareTo(minid) >= 0;
                if (stop) break;
                if ((p.flags & FLAG_DELETED) == 0) {
                    n.setInt(p.flagsIdx, p.flags | FLAG_DELETED);
                    deletedFromLp++;
                    length--;
                }
            }
            deleted += deletedFromLp;
            n.setInt(0, entries - deletedFromLp);
            n.setInt(1, n.getInt(1) + deletedFromLp);
            break;
        }

        if (length == 0) {
            firstId = new Id(0, 0);
        } else if (deleted != 0) {
            firstId = getEdgeID(true, true);
        }
        return deleted;
    }

    /** lpGetEdgeStreamID(lp, 0, ...): last entry of a node, tombstones included. */
    private static Id nodeLastId(Id masterId, Node n) {
        List<Pos> all = scan(masterId, n);
        if (all.isEmpty()) return null;
        return all.get(all.size() - 1).id;
    }

    /* ------------------------------------------------------------------ */
    /* consumer group bookkeeping helpers                                  */
    /* ------------------------------------------------------------------ */

    /** streamRangeHasTombstones() */
    public boolean rangeHasTombstones(Id start, Id end) {
        if (length == 0 || maxDeletedEntryId.isZero()) return false;
        Id s = start == null ? minId() : start;
        Id e = end == null ? maxId() : end;
        return s.compareTo(maxDeletedEntryId) <= 0 && maxDeletedEntryId.compareTo(e) <= 0;
    }

    /** streamEstimateDistanceFromFirstEverEntry() */
    public long estimateDistanceFromFirstEverEntry(Id id) {
        if (entriesAdded == 0) return 0;
        if (length == 0 && id.compareTo(lastId) < 1) return entriesAdded;
        if (!id.isZero() && id.compareTo(maxDeletedEntryId) < 0) return INVALID_ENTRIES_READ;

        int cmpLast = id.compareTo(lastId);
        if (cmpLast == 0) return entriesAdded;
        if (cmpLast > 0) return INVALID_ENTRIES_READ;

        int cmpIdFirst = id.compareTo(firstId);
        int cmpXdelFirst = maxDeletedEntryId.compareTo(firstId);
        if (maxDeletedEntryId.isZero() || cmpXdelFirst < 0) {
            if (cmpIdFirst < 0) return entriesAdded - length;
            if (cmpIdFirst == 0) return entriesAdded - length + 1;
        }
        return INVALID_ENTRIES_READ;
    }

    /** COPY: streamDup(). */
    public RStream dup() {
        RStream d = new RStream();
        for (Map.Entry<Id, Node> e : rax.entrySet()) {
            d.rax.put(e.getKey().copy(), new Node(e.getValue().lp));
        }
        d.length = length;
        d.lastId = lastId.copy();
        d.firstId = firstId.copy();
        d.maxDeletedEntryId = maxDeletedEntryId.copy();
        d.entriesAdded = entriesAdded;
        if (cgroups != null) {
            d.cgroups = new TreeMap<>();
            for (Map.Entry<Key, CG> ge : cgroups.entrySet()) {
                CG src = ge.getValue();
                CG cg = new CG();
                cg.lastId = src.lastId.copy();
                cg.entriesRead = src.entriesRead;
                for (Map.Entry<Key, Consumer> ce : src.consumers.entrySet()) {
                    Consumer sc = ce.getValue();
                    Consumer nc = new Consumer(Util.copy(sc.name));
                    nc.seenTime = sc.seenTime;
                    nc.activeTime = sc.activeTime;
                    cg.consumers.put(new Key(nc.name), nc);
                }
                for (Map.Entry<Id, NACK> pe : src.pel.entrySet()) {
                    NACK sn = pe.getValue();
                    Consumer owner = sn.consumer == null ? null
                            : cg.consumers.get(new Key(sn.consumer.name));
                    NACK nn = new NACK(owner, sn.deliveryTime, sn.deliveryCount);
                    cg.pel.put(pe.getKey().copy(), nn);
                    if (owner != null) owner.pel.put(pe.getKey().copy(), nn);
                }
                d.cgroups.put(new Key(Util.copy(ge.getKey().b)), cg);
            }
        }
        return d;
    }

    /** Every entry, in order, with tombstones skipped (AOF rewrite / digest). */
    public List<Entry> allEntries() {
        List<Entry> out = new ArrayList<>();
        Iter it = new Iter(null, null, false);
        Entry e;
        while ((e = it.next()) != null) out.add(e);
        return out;
    }
}
