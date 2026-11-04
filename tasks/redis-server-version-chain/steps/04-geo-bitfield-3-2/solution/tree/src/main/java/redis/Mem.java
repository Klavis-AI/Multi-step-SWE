package redis;

import java.util.*;

/**
 * The dataset memory model plus the maxmemory eviction machinery.
 *
 * Redis reports {@code used_memory} straight from its allocator
 * (zmalloc_used_memory()); a JVM cannot do that, so instead we keep an
 * approximation of the same number computed from the keyspace: the allocator
 * rounded footprint of every key and value, maintained incrementally as values
 * change, using exactly the accounting shape of objectComputeSize()
 * (src/object.c) -- sds allocation sizes, per entry dict/skiplist overhead and
 * listpack byte counts.
 */
public final class Mem {
    private Mem() {}

    /** used_memory_startup: what an empty redis reports right after boot. */
    public static final long STARTUP = 669200L;

    /** OBJ_COMPUTE_SIZE_DEF_SAMPLES */
    public static final int DEF_SAMPLES = 5;

    /** The accumulated footprint of every key and value of every database. */
    public static long dataset = 0;
    public static long peak = 0;

    /** Kill switch for the incremental accounting (used by the unit checks). */
    public static boolean tracking = true;

    /** A config value that also works before the server is fully up. */
    static long cfg(String name, long def) {
        Server s = Server.I;
        if (s == null || s.config == null) return def;
        try { return s.config.getLong(name); } catch (Throwable t) { return def; }
    }

    /* ------------------------------------------------------------------ */
    /* allocator model                                                     */
    /* ------------------------------------------------------------------ */

    /**
     * zmalloc_size(): glibc rounds an allocation up to a 16 byte aligned chunk
     * that also holds an 8 byte header, with a 32 byte minimum chunk.
     */
    public static long allocSize(long n) {
        if (n <= 0) return 0;
        long chunk = ((n + 8 + 15) / 16) * 16;
        if (chunk < 32) chunk = 32;
        return chunk - 8;
    }

    /** sdsHdrSize() for the type sdsReqType() picks. */
    private static int sdsHdrLen(long len) {
        if (len < 32) return 1;         /* sdshdr5 */
        if (len < 256) return 3;        /* sdshdr8 */
        if (len < 65536) return 5;      /* sdshdr16 */
        if (len < (1L << 32)) return 9; /* sdshdr32 */
        return 17;                      /* sdshdr64 */
    }

    /** sdsZmallocSize() */
    public static long sdsAllocSize(long len) { return allocSize(sdsHdrLen(len) + len + 1); }

    private static final long ROBJ = 16;        /* sizeof(robj) */
    private static final long DICT_ENTRY = 24;  /* dictEntryMemUsage() */
    private static final long DICT = 56;        /* sizeof(dict) */
    private static final long BUCKET = 8;       /* sizeof(dictEntry*) */

    /* ------------------------------------------------------------------ */
    /* listpack byte counts                                                */
    /* ------------------------------------------------------------------ */

    /** lpEncodeGetType() + lpEncodeBacklen(): the bytes one element occupies. */
    public static int lpEntrySize(byte[] e) {
        int enc;
        Long v = Util.string2ll(e);
        if (v != null) {
            long x = v;
            if (x >= 0 && x <= 127) enc = 1;
            else if (x >= -4096 && x <= 4095) enc = 2;
            else if (x >= -32768 && x <= 32767) enc = 3;
            else if (x >= -8388608 && x <= 8388607) enc = 4;
            else if (x >= Integer.MIN_VALUE && x <= Integer.MAX_VALUE) enc = 5;
            else enc = 9;
        } else {
            int l = e.length;
            if (l < 64) enc = 1 + l;
            else if (l < 4096) enc = 2 + l;
            else enc = 5 + l;
        }
        int back = enc < 128 ? 1 : (enc < 16384 ? 2 : (enc < (1 << 21) ? 3 : (enc < (1 << 28) ? 4 : 5)));
        return enc + back;
    }

    /** LP_HDR_SIZE + terminator. */
    private static final int LP_OVERHEAD = 7;

    /**
     * A listpack never holds more than a few hundred entries before redis
     * converts the value, so walking one is cheap; the cap only protects the
     * incremental model from a pathological configuration.
     */
    private static final int LP_WALK_MAX = 512;

    /* ------------------------------------------------------------------ */
    /* objectComputeSize()                                                 */
    /* ------------------------------------------------------------------ */

    public static long objectComputeSize(RObj o) { return objectComputeSize(o, DEF_SAMPLES, false); }

    public static long objectComputeSize(RObj o, long sampleSize) { return objectComputeSize(o, sampleSize, true); }

    /**
     * objectComputeSize(). {@code exact} serializes module values to measure
     * them, which MEMORY USAGE can afford but the incremental model cannot.
     */
    public static long objectComputeSize(RObj o, long sampleSize, boolean exact) {
        if (o == null) return 0;
        if (sampleSize < 1) sampleSize = 1;
        switch (o.type) {
            case RObj.STRING: {
                if (o.encoding == RObj.ENC_INT) return ROBJ;
                int len = o.strlen();
                if (o.encoding == RObj.ENC_EMBSTR) return allocSize(ROBJ + 3 + len + 1);
                return ROBJ + sdsAllocSize(len);
            }
            case RObj.LIST:   return listSize(o, sampleSize);
            case RObj.SET:    return setSize(o, sampleSize);
            case RObj.ZSET:   return zsetSize(o, sampleSize);
            case RObj.HASH:   return hashSize(o, sampleSize);
            case RObj.STREAM: return streamSize(o);
            default:          return moduleSize(o, exact);
        }
    }

    private static long listSize(RObj o, long sampleSize) {
        RList l = (RList) o.ptr;
        int n = l.size();
        if (n == 0) return ROBJ + LP_OVERHEAD;
        if (o.encoding == RObj.ENC_LISTPACK) {
            long bytes = LP_OVERHEAD, seen = 0;
            for (byte[] e : l.l) {
                bytes += lpEntrySize(e);
                if (++seen >= LP_WALK_MAX) break;
            }
            if (seen > 0 && n > seen) bytes = LP_OVERHEAD + (bytes - LP_OVERHEAD) * n / seen;
            return ROBJ + allocSize(bytes);
        }
        /* quicklist: sizeof(robj)+sizeof(quicklist) plus one node per
         * list-max-listpack-size entries, each node a listpack. */
        long sampled = 0, elesize = 0;
        for (byte[] e : l.l) {
            elesize += lpEntrySize(e);
            if (++sampled >= 160) break;
        }
        double per = sampled == 0 ? 0 : (double) elesize / sampled;
        long fill = cfg("list-max-listpack-size", 128);
        if (fill <= 0) fill = 128;   /* size bounded nodes: ~128 small entries */
        long nodes = (n + fill - 1) / fill;
        return ROBJ + 40 + nodes * (48 + LP_OVERHEAD) + (long) (per * n);
    }

    private static long setSize(RObj o, long sampleSize) {
        RSet st = (RSet) o.ptr;
        final int n = st.size();
        if (o.encoding == RObj.ENC_INTSET) {
            int width = 2;
            for (Key k : st.set) {
                Long v = Util.string2ll(k.b);
                if (v == null) { width = 8; break; }
                if (v > Integer.MAX_VALUE || v < Integer.MIN_VALUE) { width = 8; break; }
                if (v > Short.MAX_VALUE || v < Short.MIN_VALUE) width = Math.max(width, 4);
            }
            return ROBJ + allocSize(8L + (long) n * width);
        }
        if (o.encoding == RObj.ENC_LISTPACK) {
            long bytes = LP_OVERHEAD, seen = 0;
            for (Key k : st.set) {
                bytes += lpEntrySize(k.b);
                if (++seen >= LP_WALK_MAX) break;
            }
            if (seen > 0 && n > seen) bytes = LP_OVERHEAD + (bytes - LP_OVERHEAD) * n / seen;
            return ROBJ + allocSize(bytes);
        }
        long asize = ROBJ + DICT + BUCKET * n;
        long samples = 0, elesize = 0;
        for (Key k : st.set) {
            elesize += DICT_ENTRY + sdsAllocSize(k.b.length);
            if (++samples >= sampleSize) break;
        }
        if (samples > 0) asize += (long) ((double) elesize / samples * n);
        return asize;
    }

    private static long zsetSize(RObj o, long sampleSize) {
        RZSet z = (RZSet) o.ptr;
        int n = z.size();
        if (o.encoding == RObj.ENC_LISTPACK) {
            long bytes = LP_OVERHEAD, seen = 0;
            for (RZSet.Entry e : z.sorted) {
                bytes += lpEntrySize(e.member.b);
                bytes += lpEntrySize(Util.d2string(e.score));
                if (++seen >= LP_WALK_MAX) break;
            }
            if (seen > 0 && n > seen) bytes = LP_OVERHEAD + (bytes - LP_OVERHEAD) * n / seen;
            return ROBJ + allocSize(bytes);
        }
        /* sizeof(zset)+sizeof(zskiplist)+sizeof(dict)+buckets+header node */
        long asize = ROBJ + 16 + 32 + DICT + BUCKET * n + 552;
        long samples = 0, elesize = 0;
        for (RZSet.Entry e : z.sorted) {
            elesize += sdsAllocSize(e.member.b.length) + DICT_ENTRY + 48;
            if (++samples >= sampleSize) break;
        }
        if (samples > 0) asize += (long) ((double) elesize / samples * n);
        return asize;
    }

    private static long hashSize(RObj o, long sampleSize) {
        RHash h = (RHash) o.ptr;
        int n = h.size();
        if (o.encoding == RObj.ENC_LISTPACK || o.encoding == RObj.ENC_LISTPACK_EX) {
            long bytes = LP_OVERHEAD, seen = 0;
            for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                bytes += lpEntrySize(e.getKey().b);
                bytes += lpEntrySize(e.getValue());
                if (o.encoding == RObj.ENC_LISTPACK_EX) bytes += 9;
                if (++seen >= LP_WALK_MAX) break;
            }
            if (seen > 0 && n > seen) bytes = LP_OVERHEAD + (bytes - LP_OVERHEAD) * n / seen;
            return ROBJ + allocSize(bytes);
        }
        long asize = ROBJ + DICT + BUCKET * n;
        long samples = 0, elesize = 0;
        for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
            elesize += DICT_ENTRY + sdsAllocSize(e.getKey().b.length) + sdsAllocSize(e.getValue().length);
            if (++samples >= sampleSize) break;
        }
        if (samples > 0) asize += (long) ((double) elesize / samples * n);
        return asize;
    }

    private static long streamSize(RObj o) {
        RStream st = (RStream) o.ptr;
        long asize = ROBJ + 128;
        long samples = 0, lpsize = 0;
        for (RStream.Node n : st.rax.values()) {
            lpsize += allocSize(n.bytes);
            if (++samples >= DEF_SAMPLES) break;
        }
        int nodes = st.rax.size();
        if (nodes <= samples) asize += lpsize;
        else if (samples > 0) asize += lpsize / samples * nodes;
        asize += (long) nodes * 96;   /* rax nodes */
        if (st.cgroups != null) {
            for (RStream.CG cg : st.cgroups.values()) {
                asize += 128;
                asize += (long) cg.pel.size() * 96;
                asize += (long) cg.consumers.size() * 128;
            }
        }
        return asize;
    }

    private static long moduleSize(RObj o, boolean exact) {
        if (exact && o.ptr instanceof ModVal) {
            try {
                return ROBJ + allocSize(((ModVal) o.ptr).moduleSave().length);
            } catch (Throwable t) {
                return ROBJ + 64;
            }
        }
        /* Serializing a module value on every mutation would make every write
         * O(value size); the dataset model uses a flat estimate instead. */
        return ROBJ + 64;
    }

    /** The per key overhead of the main dict: the copied key plus its entry. */
    public static long keyOverhead(int keyLen) {
        return sdsAllocSize(keyLen) + DICT_ENTRY;
    }

    /** MEMORY USAGE key [SAMPLES n] */
    public static long memoryUsage(Key k, RObj o, long samples) {
        return objectComputeSize(o, samples) + sdsAllocSize(k.b.length) + DICT_ENTRY;
    }

    /* ------------------------------------------------------------------ */
    /* incremental accounting                                              */
    /* ------------------------------------------------------------------ */

    /** LinkedHashMap.put() on a keyspace dict. */
    static void onPut(Key k, RObj old, RObj nw) {
        if (!tracking) return;
        if (old == null) dataset += keyOverhead(k.b.length);
        else dataset -= old.memSize;
        nw.memSize = objectComputeSize(nw);
        dataset += nw.memSize;
    }

    /** LinkedHashMap.remove() on a keyspace dict. */
    static void onRemove(Key k, RObj old) {
        if (!tracking) return;
        dataset -= keyOverhead(k.b.length) + old.memSize;
        if (dataset < 0) dataset = 0;
    }

    /**
     * Recompute the footprint of a value that was mutated in place (SADD,
     * RPUSH, APPEND, ...). Cheap: every container type is sampled.
     */
    public static void touch(int dbid, byte[] key) {
        if (!tracking) return;
        Server s = Server.I;
        if (s == null || dbid < 0 || dbid >= s.dbs.length) return;
        RObj o = s.dbs[dbid].dict.get(new Key(key));
        if (o == null || o.type == RObj.MODULE) return;
        long now = objectComputeSize(o);
        dataset += now - o.memSize;
        o.memSize = now;
    }

    /**
     * server.c call(): a command that changed the dataset may have mutated a
     * value in place, so refresh the cached footprint of the keys it declares.
     */
    static void touchCommandKeys(Server s, Client c, Cmd cmd) {
        if (!tracking || cmd == null || c.argv == null) return;
        int first = cmd.firstKey, last = cmd.lastKey, step = cmd.keyStep;
        if (first <= 0 || step <= 0) return;
        if (last < 0) last = c.argc + last;
        if (last >= c.argc) last = c.argc - 1;
        for (int i = first; i <= last; i += step) {
            if (i <= 0 || i >= c.argc) break;
            touch(c.db, c.argv[i]);
        }
    }

    /** Recompute every cached size; used after a bulk load. */
    public static void recomputeAll(Server s) {
        long total = 0;
        for (Db d : s.dbs) {
            for (Map.Entry<Key, RObj> e : d.dict.entrySet()) {
                e.getValue().memSize = objectComputeSize(e.getValue());
                total += keyOverhead(e.getKey().b.length) + e.getValue().memSize;
            }
        }
        dataset = total;
    }

    /* ------------------------------------------------------------------ */
    /* used_memory                                                         */
    /* ------------------------------------------------------------------ */

    /** The hash table (bucket array + expires entries) overhead of every db. */
    public static long keyspaceOverhead(Server s) {
        long m = 0;
        for (Db d : s.dbs) {
            int n = d.dict.size();
            if (n != 0) m += DICT + BUCKET * n;
            int e = d.expires.size();
            if (e != 0) m += DICT + (BUCKET + DICT_ENTRY) * e;
        }
        return m;
    }

    /**
     * freeMemoryGetNotCountedMemory(): the AOF buffer and the replica output
     * buffers. They are part of used_memory but must not drive eviction, or
     * the DELs we push into them would feed back into more eviction.
     */
    public static long notCountedForEvict(Server s) {
        long m = 0;
        try {
            if (s.aof != null) m += s.aof.buf.len;
            for (Client r : s.replicas) m += bufferedBytes(r);
        } catch (Throwable t) {
            /* best effort */
        }
        return m;
    }

    /**
     * The bytes a connection currently holds server side: the unparsed part of
     * its query buffer plus the reply bytes not yet written out, plus the
     * current argument vector and any queued MULTI.  Capacities are
     * deliberately not used: a Buf never shrinks, so counting them would make
     * used_memory ratchet upwards forever.
     */
    public static long bufferedBytes(Client c) {
        long m = 0;
        try {
            m += c.query.len;
            m += c.pendingMem();
            m += c.out.len;
            if (c.argv != null) for (int i = 0; i < c.argc; i++) if (c.argv[i] != null) m += c.argv[i].length;
            for (byte[][] q : c.multiQueue) for (byte[] a : q) if (a != null) m += a.length;
        } catch (Throwable t) {
            /* best effort */
        }
        return m;
    }

    /** zmalloc_used_memory(): startup allocation + dataset + client buffers. */
    public static long usedMemory(Server s) {
        long m = STARTUP + dataset + keyspaceOverhead(s) + clientsMemory(s);
        if (m > peak) peak = m;
        return m;
    }

    /** The server side buffers of every connected client. */
    public static long clientsMemory(Server s) {
        long m = 0;
        try {
            for (Client c : s.clients.values()) m += bufferedBytes(c);
        } catch (Throwable t) {
            /* best effort */
        }
        return m;
    }

    /** The INFO mem_clients_* accounting of one connection. */
    public static long clientMemUsage(Client c) { return bufferedBytes(c); }

    /* ------------------------------------------------------------------ */
    /* LRU / LFU                                                           */
    /* ------------------------------------------------------------------ */

    public static final int LFU_INIT_VAL = 5;
    private static final Random RND = new Random();

    public static long lruClock() { return System.currentTimeMillis() / 1000; }
    public static int lfuNowMinutes() { return (int) ((System.currentTimeMillis() / 60000) & 65535); }

    /** LFUTimeElapsed() */
    public static int lfuTimeElapsed(int ldt) {
        int now = lfuNowMinutes();
        if (now >= ldt) return now - ldt;
        return 65535 - ldt + now;
    }

    /** LFUDecrAndReturn() */
    public static int lfuDecrAndReturn(RObj o) {
        long decay = cfg("lfu-decay-time", 1);
        int counter = o.lfu;
        long periods = decay != 0 ? lfuTimeElapsed(o.lfuLdt) / decay : 0;
        if (periods != 0) counter = periods > counter ? 0 : (int) (counter - periods);
        return counter;
    }

    /** LFULogIncr() */
    public static int lfuLogIncr(int counter) {
        if (counter == 255) return 255;
        double r = RND.nextDouble();
        double baseval = counter - LFU_INIT_VAL;
        if (baseval < 0) baseval = 0;
        double factor = cfg("lfu-log-factor", 10);
        double p = 1.0 / (baseval * factor + 1);
        if (r < p) counter++;
        return counter;
    }

    public static boolean lfuPolicy(Server s) {
        return s.config.getOr("maxmemory-policy", "noeviction").endsWith("lfu");
    }

    /** lookupKey(): refresh the access information of a value. */
    public static void updateOnAccess(Server s, RObj o) {
        if (o == null) return;
        /* CLIENT NO-TOUCH (7.2): commands from such a client must not refresh
         * the LRU/LFU access information of the keys they read. */
        if (Tracking.current != null && Tracking.current.noTouch) return;
        if (lfuPolicy(s)) {
            o.lfu = lfuLogIncr(lfuDecrAndReturn(o));
            o.lfuLdt = lfuNowMinutes();
        } else {
            o.lru = lruClock();
        }
    }

    /** estimateObjectIdleTime(), in milliseconds. */
    public static long idleTimeMs(RObj o) {
        long d = lruClock() - o.lru;
        return d < 0 ? 0 : d * 1000L;
    }

    /* ------------------------------------------------------------------ */
    /* eviction                                                            */
    /* ------------------------------------------------------------------ */

    public static final int NO_EVICTION = 0, ALLKEYS_LRU = 1, ALLKEYS_LFU = 2, ALLKEYS_RANDOM = 3,
            VOLATILE_LRU = 4, VOLATILE_LFU = 5, VOLATILE_RANDOM = 6, VOLATILE_TTL = 7;

    public static int policy(Server s) {
        String p = s.config.getOr("maxmemory-policy", "noeviction").toLowerCase(Locale.ROOT);
        switch (p) {
            case "allkeys-lru": return ALLKEYS_LRU;
            case "allkeys-lfu": return ALLKEYS_LFU;
            case "allkeys-random": return ALLKEYS_RANDOM;
            case "volatile-lru": return VOLATILE_LRU;
            case "volatile-lfu": return VOLATILE_LFU;
            case "volatile-random": return VOLATILE_RANDOM;
            case "volatile-ttl": return VOLATILE_TTL;
            default: return NO_EVICTION;
        }
    }

    private static boolean allkeys(int p) {
        return p == ALLKEYS_LRU || p == ALLKEYS_LFU || p == ALLKEYS_RANDOM;
    }

    /**
     * getMaxmemoryState(): returns the number of bytes that must be freed, or
     * 0 when the dataset already fits.
     */
    public static long memToFree(Server s) {
        long maxmemory = s.config.getLong("maxmemory");
        if (maxmemory <= 0) return 0;
        long reported = usedMemory(s);
        if (reported <= maxmemory) return 0;
        long overhead = notCountedForEvict(s);
        long used = reported > overhead ? reported - overhead : 0;
        if (used <= maxmemory) return 0;
        return used - maxmemory;
    }

    /**
     * performEvictions(): free memory until the dataset fits into maxmemory.
     * Returns false when the limit is still exceeded, which makes every
     * denyoom command reply with the OOM error.
     */
    public static boolean performEvictions(Server s) {
        /* isSafeToPerformEvictions(): nothing is evicted while a dataset is
         * being loaded, and a replica leaves the eviction to its master
         * (replica-ignore-maxmemory) and never reports OOM. */
        if (s.loading) return true;
        if (s.repl.masterHost != null) return true;
        long toFree = memToFree(s);
        if (toFree == 0) return true;
        int p = policy(s);
        if (p == NO_EVICTION) return false;

        long samples = s.config.getLong("maxmemory-samples");
        if (samples <= 0) samples = 5;
        long freed = 0;
        int guard = 0;

        while (freed < toFree) {
            if (++guard > 10_000_000) break;
            Key best = null;
            Db bestDb = null;
            long bestScore = -1;

            for (Db db : s.dbs) {
                Collection<Key> from = allkeys(p) ? db.dict.keySet() : db.expires.keySet();
                if (from.isEmpty()) continue;
                int taken = 0;
                for (Key cand : from) {
                    if (taken++ >= samples) break;
                    RObj o = db.dict.get(cand);
                    if (o == null) continue;
                    long score;
                    switch (p) {
                        case ALLKEYS_LRU:
                        case VOLATILE_LRU:
                            score = idleTimeMs(o);
                            break;
                        case ALLKEYS_LFU:
                        case VOLATILE_LFU:
                            score = 255 - lfuDecrAndReturn(o);
                            break;
                        case VOLATILE_TTL: {
                            Long when = db.expires.get(cand);
                            score = when == null ? 0 : Long.MAX_VALUE - when;
                            break;
                        }
                        default:
                            score = 0;
                            break;
                    }
                    if (best == null || score > bestScore) {
                        best = cand;
                        bestDb = db;
                        bestScore = score;
                    }
                }
            }
            if (best == null) return false;   /* nothing left to evict */

            freed += evictKey(s, bestDb, best);
        }
        return true;
    }

    /** deleteEvictedKeyAndPropagate() */
    private static long evictKey(Server s, Db db, Key k) {
        RObj o = db.dict.get(k);
        long before = dataset + keyspaceOverhead(s);
        byte[] key = k.b;
        db.delete(k);
        long after = dataset + keyspaceOverhead(s);
        long freed = before - after;
        if (freed <= 0) freed = 1;

        s.propagateExtra(db.id, new byte[][]{Util.b("DEL"), key});
        try {
            s.repl.incrOffsetForPropagation();
        } catch (Throwable t) {
            /* best effort */
        }
        s.signalModifiedKey(db.id, k);
        try {
            Tracking.invalidateKey(s, null, key, true);
        } catch (Throwable t) {
            /* best effort */
        }
        s.notifyKeyspaceEvent('e', "evicted", key, db.id);
        s.statEvictedKeys++;
        if (o != null) s.dirty++;
        return freed;
    }
}
