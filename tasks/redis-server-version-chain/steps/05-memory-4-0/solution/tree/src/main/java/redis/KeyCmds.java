package redis;

import java.math.BigInteger;
import java.util.*;

/**
 * Generic keyspace commands: DEL/UNLINK/EXISTS/TYPE/KEYS/RANDOMKEY/RENAME/MOVE/COPY,
 * the database commands (SELECT/SWAPDB/DBSIZE/FLUSHDB/FLUSHALL), the expire family,
 * the SCAN family, SORT and the OBJECT container command, plus DUMP/RESTORE.
 *
 * Ported from redis 8.0.6 src/db.c, src/expire.c, src/sort.c, src/object.c and the
 * DUMP/RESTORE argument handling of src/cluster.c.
 */
public final class KeyCmds {
    private KeyCmds() {}

    private static Server srv() { return Server.I; }

    // ------------------------------------------------------------------
    // registration
    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("del", -2, "write", 1, -1, 1, c -> delGeneric(c, false));
        CommandTable.reg("unlink", -2, "write fast", 1, -1, 1, c -> delGeneric(c, true));
        CommandTable.reg("exists", -2, "readonly fast", 1, -1, 1, KeyCmds::existsCmd);
        CommandTable.reg("type", 2, "readonly fast", 1, 1, 1, KeyCmds::typeCmd);
        CommandTable.reg("keys", 2, "readonly", 0, 0, 0, KeyCmds::keysCmd);
        CommandTable.reg("randomkey", 1, "readonly random", 0, 0, 0, KeyCmds::randomkeyCmd);
        CommandTable.reg("rename", 3, "write", 1, 2, 1, c -> renameGeneric(c, false));
        CommandTable.reg("renamenx", 3, "write fast", 1, 2, 1, c -> renameGeneric(c, true));
        CommandTable.reg("move", 3, "write fast", 1, 1, 1, KeyCmds::moveCmd);
        CommandTable.reg("copy", -3, "write denyoom", 1, 2, 1, KeyCmds::copyCmd);
        CommandTable.reg("dbsize", 1, "readonly fast", 0, 0, 0, KeyCmds::dbsizeCmd);
        CommandTable.reg("flushdb", -1, "write", 0, 0, 0, c -> flushCmd(c, false));
        CommandTable.reg("flushall", -1, "write", 0, 0, 0, c -> flushCmd(c, true));
        CommandTable.reg("select", 2, "loading stale fast", 0, 0, 0, KeyCmds::selectCmd);
        CommandTable.reg("swapdb", 3, "write fast", 0, 0, 0, KeyCmds::swapdbCmd);
        CommandTable.reg("touch", -2, "readonly fast", 1, -1, 1, KeyCmds::touchCmd);
        CommandTable.reg("persist", 2, "write fast", 1, 1, 1, KeyCmds::persistCmd);

        CommandTable.reg("expire", -3, "write fast", 1, 1, 1,
                c -> expireGeneric(c, srv().mstime(), true));
        CommandTable.reg("pexpire", -3, "write fast", 1, 1, 1,
                c -> expireGeneric(c, srv().mstime(), false));
        CommandTable.reg("expireat", -3, "write fast", 1, 1, 1,
                c -> expireGeneric(c, 0, true));
        CommandTable.reg("pexpireat", -3, "write fast", 1, 1, 1,
                c -> expireGeneric(c, 0, false));

        CommandTable.reg("ttl", 2, "readonly fast", 1, 1, 1, c -> ttlGeneric(c, false, false));
        CommandTable.reg("pttl", 2, "readonly fast", 1, 1, 1, c -> ttlGeneric(c, true, false));
        CommandTable.reg("expiretime", 2, "readonly fast", 1, 1, 1, c -> ttlGeneric(c, false, true));
        CommandTable.reg("pexpiretime", 2, "readonly fast", 1, 1, 1, c -> ttlGeneric(c, true, true));

        CommandTable.reg("scan", -2, "readonly random", 0, 0, 0, KeyCmds::scanCmd);
        CommandTable.reg("hscan", -3, "readonly random", 1, 1, 1, KeyCmds::hscanCmd);
        CommandTable.reg("sscan", -3, "readonly random", 1, 1, 1, KeyCmds::sscanCmd);
        CommandTable.reg("zscan", -3, "readonly random", 1, 1, 1, KeyCmds::zscanCmd);

        CommandTable.reg("sort", -2, "write denyoom movablekeys", 1, 1, 1, c -> sortGeneric(c, false));
        CommandTable.reg("sort_ro", -2, "readonly movablekeys", 1, 1, 1, c -> sortGeneric(c, true));

        CommandTable.reg("dump", 2, "readonly", 1, 1, 1, KeyCmds::dumpCmd);
        CommandTable.reg("restore", -4, "write denyoom", 1, 1, 1, KeyCmds::restoreCmd);
        CommandTable.reg("migrate", -6, "write movablekeys", 3, 3, 1, KeyCmds::migrateCmd);

        CommandTable.reg("object", -2, "", 0, 0, 0, KeyCmds::objectCmd);
        CommandTable.regSub("object", "encoding", 3, "readonly", KeyCmds::objectEncodingCmd);
        CommandTable.regSub("object", "refcount", 3, "readonly", KeyCmds::objectRefcountCmd);
        CommandTable.regSub("object", "idletime", 3, "readonly", KeyCmds::objectIdletimeCmd);
        CommandTable.regSub("object", "freq", 3, "readonly", KeyCmds::objectFreqCmd);
        CommandTable.regSub("object", "help", 2, "loading stale", KeyCmds::objectHelpCmd);
    }

    // ------------------------------------------------------------------
    // small helpers
    // ------------------------------------------------------------------

    /** getIntFromObjectOrReply(c, o, &target, msg) */
    private static int argInt(Client c, int idx, String msg) {
        Long v = Util.string2ll(c.argv[idx]);
        if (v == null) {
            throw new RedisError(msg != null ? "ERR " + msg : "ERR value is not an integer or out of range");
        }
        if (v < Integer.MIN_VALUE || v > Integer.MAX_VALUE) {
            throw new RedisError(msg != null ? "ERR " + msg
                    : "ERR value is out of range, value must between -2147483648 and 2147483647");
        }
        return (int) (long) v;
    }

    private static void notifyNew(int dbid, byte[] key) {
        // NOTIFY_NEW ('n') is deliberately excluded from the 'A' class in redis, so only
        // fire it when the flag was requested explicitly.
        String flags = srv().config.getOr("notify-keyspace-events", "");
        if (flags != null && flags.indexOf('n') >= 0) srv().notifyKeyspaceEvent('n', "new", key, dbid);
    }

    /** dbAdd(): add the value, wake blocked clients and fire the "new key" event. */
    private static void dbAddNotify(int dbid, byte[] key, RObj o) {
        srv().dbAdd(dbid, key, o);
        srv().signalKeyAsReady(dbid, key);
        notifyNew(dbid, key);
    }

    private static long nowSeconds() { return srv().mstime() / 1000; }

    private static boolean lfuPolicy() {
        String p = srv().config.getOr("maxmemory-policy", "noeviction");
        return p != null && p.toLowerCase().contains("lfu");
    }

    // ------------------------------------------------------------------
    // DEL / UNLINK / EXISTS / TYPE / KEYS / RANDOMKEY / DBSIZE / TOUCH
    // ------------------------------------------------------------------
    private static void delGeneric(Client c, boolean lazy) {
        Server s = srv();
        Db d = s.dbs[c.db];
        int numdel = 0;
        for (int j = 1; j < c.argc; j++) {
            Key k = new Key(c.arg(j));
            if (s.expireIfNeeded(d, k)) continue;
            if (d.delete(k)) {
                s.signalModifiedKey(c.db, k);
                s.notifyKeyspaceEvent('g', "del", k.b, c.db);
                s.dirty++;
                numdel++;
            }
        }
        c.addInt(numdel);
    }

    private static void existsCmd(Client c) {
        Server s = srv();
        long count = 0;
        for (int j = 1; j < c.argc; j++) {
            if (s.lookupKeyRead(c, c.arg(j)) != null) count++;
        }
        c.addInt(count);
    }

    private static void typeCmd(Client c) {
        /* typeCommand(): LOOKUP_NOTOUCH, TYPE must not alter the access time. */
        RObj o = srv().lookupKeyReadNoTouch(c, c.arg(1));
        c.addSimple(o == null ? "none" : o.typeName());
    }

    private static void keysCmd(Client c) {
        Server s = srv();
        Db d = s.dbs[c.db];
        byte[] pattern = c.arg(1);
        boolean allkeys = pattern.length == 1 && pattern[0] == '*';
        List<byte[]> out = new ArrayList<>();
        for (Key k : new ArrayList<>(d.dict.keySet())) {
            if (allkeys || Util.stringmatch(pattern, k.b, false)) {
                if (!s.keyIsExpired(d, k)) out.add(k.b);
            }
        }
        c.addArray(out.size());
        for (byte[] b : out) c.addBulk(b);
    }

    private static void randomkeyCmd(Client c) {
        Server s = srv();
        Db d = s.dbs[c.db];
        int maxtries = 100;
        boolean allvolatile = d.dict.size() == d.expires.size();
        while (true) {
            Key k = d.randomKey();
            if (k == null) { c.addNil(); return; }
            if (allvolatile && s.repl.masterHost != null && --maxtries == 0) { c.addBulk(k.b); return; }
            if (s.expireIfNeeded(d, k) || s.keyIsExpired(d, k)) continue;
            c.addBulk(k.b);
            return;
        }
    }

    private static void dbsizeCmd(Client c) { c.addInt(srv().dbs[c.db].size()); }

    private static void touchCmd(Client c) {
        Server s = srv();
        int touched = 0;
        long now = nowSeconds();
        for (int j = 1; j < c.argc; j++) {
            RObj o = s.lookupKeyRead(c, c.arg(j));
            if (o != null) { o.lru = now; touched++; }
        }
        c.addInt(touched);
    }

    // ------------------------------------------------------------------
    // SELECT / SWAPDB / FLUSHDB / FLUSHALL
    // ------------------------------------------------------------------
    private static void selectCmd(Client c) {
        /* selectCommand(): getLongFromObjectOrReply(..., "invalid DB index") and
         * the same message again when selectDb() rejects the index. */
        int id = argInt(c, 1, "invalid DB index");
        if (id < 0 || id >= srv().dbs.length) throw new RedisError("ERR invalid DB index");
        c.db = id;
        c.addOk();
    }

    private static void swapdbCmd(Client c) {
        Server s = srv();
        int id1 = argInt(c, 1, "invalid first DB index");
        int id2 = argInt(c, 2, "invalid second DB index");
        if (id1 < 0 || id1 >= s.dbs.length || id2 < 0 || id2 >= s.dbs.length)
            throw new RedisError("ERR DB index is out of range");
        if (id1 != id2) {
            Db d1 = s.dbs[id1], d2 = s.dbs[id2];
            // Swapping databases must make every running transaction fail.
            s.touchAllWatchedKeys(id1);
            s.touchAllWatchedKeys(id2);

            LinkedHashMap<Key, RObj> tmpDict = new LinkedHashMap<>(d1.dict);
            HashMap<Key, Long> tmpExp = new HashMap<>(d1.expires);
            /* db->hexpires travels with the keyspace it indexes. */
            LinkedHashSet<Key> tmpHfe = new LinkedHashSet<>(d1.hexpires);
            d1.dict.clear(); d1.dict.putAll(d2.dict);
            d1.expires.clear(); d1.expires.putAll(d2.expires);
            d1.hexpires.clear(); d1.hexpires.addAll(d2.hexpires);
            d2.dict.clear(); d2.dict.putAll(tmpDict);
            d2.expires.clear(); d2.expires.putAll(tmpExp);
            d2.hexpires.clear(); d2.hexpires.addAll(tmpHfe);

            scanDatabaseForReadyKeys(s, d1);
            scanDatabaseForReadyKeys(s, d2);
        }
        s.dirty++;
        c.addOk();
    }

    private static void scanDatabaseForReadyKeys(Server s, Db d) {
        /* scanDatabaseForChangedKeys(): every key someone blocks on has to be
         * re-examined, including the ones that just stopped existing. */
        for (Key k : new ArrayList<>(d.blockingKeys.keySet())) s.signalKeyAsReady(d.id, k.b);
    }

    /** getFlushCommandFlags(): only SYNC / ASYNC are accepted. */
    private static void flushCmd(Client c, boolean all) {
        if (c.argc == 2) {
            if (!c.argIs(1, "sync") && !c.argIs(1, "async")) throw RedisError.syntax();
        } else if (c.argc != 1) {
            throw RedisError.syntax();
        }
        Server s = srv();
        long removed = 0;
        s.touchWatchedKeysOnFlush(all ? -1 : c.db);
        if (all) {
            for (Db d : s.dbs) removed += emptyDb(s, d);
        } else {
            removed += emptyDb(s, s.dbs[c.db]);
        }
        s.dirty += removed;
        // FLUSHALL/FLUSHDB are propagated even when the data set was already empty.
        if (removed == 0) s.propagateExtra(c.db, Arrays.copyOf(c.argv, c.argc));
        c.addOk();
    }

    private static long emptyDb(Server s, Db d) {
        long n = d.dict.size();
        d.clear();
        /* emptyDbStructure() -> scanDatabaseForDeletedKeys(): clients blocked
         * until a key disappears must be woken up. */
        scanDatabaseForReadyKeys(s, d);
        return n;
    }

    // ------------------------------------------------------------------
    // RENAME / RENAMENX / MOVE / COPY
    // ------------------------------------------------------------------
    private static void renameGeneric(Client c, boolean nx) {
        Server s = srv();
        Db d = s.dbs[c.db];
        byte[] src = c.arg(1), dst = c.arg(2);

        /* renameGenericCommand(): to use the same key as src and dst is an error. */
        if (Arrays.equals(src, dst))
            throw new RedisError("ERR source and destination objects are the same");

        RObj o = s.lookupKeyWrite(c, src);
        if (o == null) throw RedisError.noSuchKey();

        Long expire = d.getExpire(new Key(src));
        if (s.lookupKeyWrite(c, dst) != null) {
            if (nx) { c.addInt(0); return; }
            s.dbDelete(c.db, dst);
        }
        dbAddNotify(c.db, dst, o);
        if (expire != null) d.setExpire(new Key(dst), expire);
        d.delete(new Key(src));

        s.signalModifiedKey(c.db, new Key(src));
        s.signalModifiedKey(c.db, new Key(dst));
        s.notifyKeyspaceEvent('g', "rename_from", src, c.db);
        s.notifyKeyspaceEvent('g', "rename_to", dst, c.db);
        s.dirty++;
        if (nx) c.addInt(1); else c.addOk();
    }

    private static void moveCmd(Client c) {
        Server s = srv();
        int srcid = c.db;
        /* moveCommand(): a bad DB index, whatever the reason, is shared.outofrangeerr. */
        int dbid = argInt(c, 2, "index out of range");
        if (dbid < 0 || dbid >= s.dbs.length) throw new RedisError("ERR index out of range");
        if (dbid == srcid) throw new RedisError("ERR source and destination objects are the same");

        Db src = s.dbs[srcid], dst = s.dbs[dbid];
        byte[] key = c.arg(1);
        Key k = new Key(key);

        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) { c.addInt(0); return; }
        Long expire = src.getExpire(k);

        if (s.lookupKey(dbid, key, false) != null) { c.addInt(0); return; }

        dbAddNotify(dbid, key, o);
        if (expire != null) dst.setExpire(k, expire);
        src.delete(k);

        s.signalModifiedKey(srcid, k);
        s.signalModifiedKey(dbid, k);
        s.notifyKeyspaceEvent('g', "move_from", key, srcid);
        s.notifyKeyspaceEvent('g', "move_to", key, dbid);
        s.dirty++;
        c.addInt(1);
    }

    private static void copyCmd(Client c) {
        Server s = srv();
        int srcid = c.db, dbid = c.db;
        boolean replace = false;
        for (int j = 3; j < c.argc; j++) {
            int additional = c.argc - j - 1;
            if (c.argIs(j, "replace")) {
                replace = true;
            } else if (c.argIs(j, "db") && additional >= 1) {
                dbid = argInt(c, j + 1, null);
                if (dbid < 0 || dbid >= s.dbs.length) throw new RedisError("ERR DB index is out of range");
                j++;
            } else {
                throw RedisError.syntax();
            }
        }

        byte[] key = c.arg(1), newkey = c.arg(2);
        if (srcid == dbid && Arrays.equals(key, newkey))
            throw new RedisError("ERR source and destination objects are the same");

        RObj o = s.lookupKeyRead(c, key);
        if (o == null) { c.addInt(0); return; }
        if (o.type == RObj.MODULE && (!(o.ptr instanceof ModVal) || !((ModVal) o.ptr).moduleCopyable()))
            throw new RedisError("ERR not supported for this module key");
        Long expire = s.dbs[srcid].getExpire(new Key(key));

        boolean delete = false;
        if (s.lookupKey(dbid, newkey, false) != null) {
            if (replace) delete = true;
            else { c.addInt(0); return; }
        }

        RObj newobj = dupObject(o);
        if (delete) s.dbDelete(dbid, newkey);
        dbAddNotify(dbid, newkey, newobj);
        if (expire != null) s.dbs[dbid].setExpire(new Key(newkey), expire);

        s.signalModifiedKey(dbid, new Key(newkey));
        s.notifyKeyspaceEvent('g', "copy_to", newkey, dbid);
        s.dirty++;
        c.addInt(1);
    }

    /** Deep copy of a value so that the copy is fully independent of the original. */
    static RObj dupObject(RObj o) {
        RObj n;
        switch (o.type) {
            case RObj.STRING: {
                if (o.encoding == RObj.ENC_INT) n = new RObj(RObj.STRING, RObj.ENC_INT, o.ptr);
                else n = new RObj(RObj.STRING, o.encoding, Util.copy((byte[]) o.ptr));
                break;
            }
            case RObj.LIST: {
                RList src = (RList) o.ptr;
                RList cp = new RList();
                for (byte[] e : src.l) cp.l.addLast(Util.copy(e));
                n = new RObj(RObj.LIST, o.encoding, cp);
                break;
            }
            case RObj.SET: {
                RSet src = (RSet) o.ptr;
                RSet cp = new RSet();
                for (Key k : src.set) cp.set.add(new Key(Util.copy(k.b)));
                n = new RObj(RObj.SET, o.encoding, cp);
                break;
            }
            case RObj.ZSET: {
                RZSet src = (RZSet) o.ptr;
                RZSet cp = new RZSet();
                for (RZSet.Entry e : src.sorted) cp.add(Util.copy(e.member.b), e.score);
                n = new RObj(RObj.ZSET, o.encoding, cp);
                break;
            }
            case RObj.HASH: {
                RHash src = (RHash) o.ptr;
                RHash cp = new RHash();
                for (Map.Entry<Key, byte[]> e : src.map.entrySet())
                    cp.map.put(new Key(Util.copy(e.getKey().b)), Util.copy(e.getValue()));
                if (src.ttls != null) {
                    cp.ttls = new HashMap<>();
                    for (Map.Entry<Key, Long> e : src.ttls.entrySet())
                        cp.ttls.put(new Key(Util.copy(e.getKey().b)), e.getValue());
                }
                n = new RObj(RObj.HASH, o.encoding, cp);
                break;
            }
            default: {
                // Streams and anything else: round-trip through the RDB serializer.
                n = Rdb.restorePayload(Rdb.dumpPayload(o));
                break;
            }
        }
        n.lru = o.lru;
        n.lfu = o.lfu;
        return n;
    }

    // ------------------------------------------------------------------
    // expires
    // ------------------------------------------------------------------
    private static final int EXPIRE_NX = 1, EXPIRE_XX = 2, EXPIRE_GT = 4, EXPIRE_LT = 8;

    private static int parseExpireFlags(Client c) {
        int flags = 0;
        boolean nx = false, xx = false, gt = false, lt = false;
        for (int j = 3; j < c.argc; j++) {
            if (c.argIs(j, "nx")) { flags |= EXPIRE_NX; nx = true; }
            else if (c.argIs(j, "xx")) { flags |= EXPIRE_XX; xx = true; }
            else if (c.argIs(j, "gt")) { flags |= EXPIRE_GT; gt = true; }
            else if (c.argIs(j, "lt")) { flags |= EXPIRE_LT; lt = true; }
            else throw new RedisError("ERR Unsupported option " + c.argStr(j));
        }
        if ((nx && xx) || (nx && gt) || (nx && lt))
            throw new RedisError("ERR NX and XX, GT or LT options at the same time are not compatible");
        if (gt && lt)
            throw new RedisError("ERR GT and LT options at the same time are not compatible");
        return flags;
    }

    private static void expireGeneric(Client c, long basetime, boolean seconds) {
        Server s = srv();
        Db d = s.dbs[c.db];
        byte[] key = c.arg(1);
        Key k = new Key(key);

        int flag = parseExpireFlags(c);
        long when = c.argLong(2);

        String cmdName = c.argLower(0);
        if (seconds) {
            if (when > Long.MAX_VALUE / 1000 || when < Long.MIN_VALUE / 1000)
                throw new RedisError("ERR invalid expire time in '" + cmdName + "' command");
            when *= 1000;
        }
        if (when > Long.MAX_VALUE - basetime)
            throw new RedisError("ERR invalid expire time in '" + cmdName + "' command");
        when += basetime;

        if (s.lookupKeyWrite(c, key) == null) { c.addInt(0); return; }

        if (flag != 0) {
            Long cur = d.getExpire(k);
            long currentExpire = cur == null ? -1 : cur;
            if ((flag & EXPIRE_NX) != 0 && currentExpire != -1) { c.addInt(0); return; }
            if ((flag & EXPIRE_XX) != 0 && currentExpire == -1) { c.addInt(0); return; }
            if ((flag & EXPIRE_GT) != 0 && (when <= currentExpire || currentExpire == -1)) { c.addInt(0); return; }
            if ((flag & EXPIRE_LT) != 0 && currentExpire != -1 && when >= currentExpire) { c.addInt(0); return; }
        }

        if (checkAlreadyExpired(s, when)) {
            boolean lazy = s.config.getBool("lazyfree-lazy-expire");
            d.delete(k);
            s.dirty++;
            s.replOverride = new byte[][]{Util.b(lazy ? "UNLINK" : "DEL"), key};
            s.signalModifiedKey(c.db, k);
            s.notifyKeyspaceEvent('g', "del", key, c.db);
            c.addInt(1);
            return;
        }

        d.setExpire(k, when);
        c.addInt(1);
        // Always propagated as an absolute PEXPIREAT; any NX/XX/GT/LT flags are kept.
        byte[][] repl = new byte[c.argc][];
        System.arraycopy(c.argv, 0, repl, 0, c.argc);
        repl[0] = Util.b("PEXPIREAT");
        repl[2] = Util.ll2string(when);
        s.replOverride = repl;
        s.signalModifiedKey(c.db, k);
        s.notifyKeyspaceEvent('g', "expire", key, c.db);
        s.dirty++;
    }

    private static boolean checkAlreadyExpired(Server s, long when) {
        return when <= s.mstime() && !s.loading && s.repl.masterHost == null;
    }

    private static void ttlGeneric(Client c, boolean outputMs, boolean outputAbs) {
        Server s = srv();
        /* ttlGenericCommand(): LOOKUP_NOTOUCH. */
        if (s.lookupKeyReadNoTouch(c, c.arg(1)) == null) { c.addInt(-2); return; }
        Long expire = s.dbs[c.db].getExpire(new Key(c.arg(1)));
        long ttl = -1;
        if (expire != null) {
            ttl = outputAbs ? expire : expire - s.mstime();
            if (ttl < 0) ttl = 0;
        }
        if (ttl == -1) c.addInt(-1);
        else c.addInt(outputMs ? ttl : ((ttl + 500) / 1000));
    }

    private static void persistCmd(Client c) {
        Server s = srv();
        Db d = s.dbs[c.db];
        Key k = new Key(c.arg(1));
        if (s.lookupKeyWrite(c, c.arg(1)) != null) {
            if (d.expires.remove(k) != null) {
                s.signalModifiedKey(c.db, k);
                s.notifyKeyspaceEvent('g', "persist", c.arg(1), c.db);
                c.addInt(1);
                s.dirty++;
            } else {
                c.addInt(0);
            }
        } else {
            c.addInt(0);
        }
    }

    // ------------------------------------------------------------------
    // SCAN / HSCAN / SSCAN / ZSCAN
    // ------------------------------------------------------------------

    /** One reverse-binary cursor increment over a table whose bucket mask is `mask`. */
    private static long nextCursor(long v, long mask) {
        v |= ~mask;
        v = Long.reverse(v);
        v += 1;
        v = Long.reverse(v);
        return v;
    }

    /**
     * The bucket mask of the hash table redis would be using for `n` elements:
     * dict tables are powers of two and grow as soon as used >= size.
     */
    private static long tableMask(int n) {
        long size = 4;
        if (n > 0) {
            size = Long.highestOneBit(n) << 1;
            if (size < 4) size = 4;
        }
        return size - 1;
    }

    /** Result of one dictScan() pass. */
    private static final class ScanStep {
        long cursor;
        final List<Key> keys = new ArrayList<>();
    }

    /**
     * Port of dict.c dictScan() over a fixed size bucket array derived from the
     * collection size: emits every key of the bucket the cursor points at, then
     * returns the next reverse-binary cursor. Keeps visiting buckets until at
     * least `count` elements have been sampled, like scanGenericCommand() does.
     */
    private static ScanStep bucketScan(Collection<Key> all, long cursor, long count) {
        ScanStep r = new ScanStep();
        int n = all.size();
        if (n == 0) { r.cursor = 0; return r; }
        long mask = tableMask(n);
        long size = mask + 1;
        long want = count < 1 ? 1 : count;
        int planLen = (int) Math.min(want, size);
        if (planLen < 1) planLen = 1;

        long[] bucket = new long[planLen];
        long[] after = new long[planLen];
        int k = 0;
        long v = cursor;
        while (k < planLen) {
            bucket[k] = v & mask;
            v = nextCursor(v, mask);
            after[k] = v;
            k++;
            if (v == 0) break;
        }

        Set<Long> wanted = new HashSet<>();
        for (int i = 0; i < k; i++) wanted.add(bucket[i]);
        Map<Long, List<Key>> groups = new HashMap<>();
        for (Key key : all) {
            long b = key.hashCode() & mask;
            if (wanted.contains(b)) groups.computeIfAbsent(b, x -> new ArrayList<>()).add(key);
        }

        long sampled = 0;
        for (int i = 0; i < k; i++) {
            List<Key> lst = groups.get(bucket[i]);
            if (lst != null) { r.keys.addAll(lst); sampled += lst.size(); }
            if (after[i] == 0 || sampled >= want) { r.cursor = after[i]; return r; }
        }
        r.cursor = after[k - 1];
        return r;
    }

    /** string2ull() semantics plus the "ERR invalid cursor" reply. */
    private static long parseCursor(byte[] a) {
        Long v = Util.string2ll(a);
        if (v != null && v >= 0) return v;
        String t = Util.s(a);
        int i = 0;
        while (i < t.length() && isCSpace(t.charAt(i))) i++;
        boolean neg = false;
        if (i < t.length() && (t.charAt(i) == '+' || t.charAt(i) == '-')) {
            neg = t.charAt(i) == '-';
            i++;
        }
        if (i >= t.length()) throw new RedisError("ERR invalid cursor");
        for (int j = i; j < t.length(); j++) {
            char ch = t.charAt(j);
            if (ch < '0' || ch > '9') throw new RedisError("ERR invalid cursor");
        }
        BigInteger bi;
        try { bi = new BigInteger(t.substring(i)); }
        catch (NumberFormatException e) { throw new RedisError("ERR invalid cursor"); }
        if (bi.compareTo(BigInteger.ONE.shiftLeft(64).subtract(BigInteger.ONE)) > 0)
            throw new RedisError("ERR invalid cursor");
        long r = bi.longValue();
        return neg ? -r : r;
    }

    private static boolean isCSpace(char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == 0x0b;
    }

    private static final byte[] EMPTY_SCAN = Util.b("*2\r\n$1\r\n0\r\n*0\r\n");

    private static void scanCmd(Client c) {
        long cursor = parseCursor(c.arg(1));
        scanGeneric(c, null, cursor);
    }

    private static void hscanCmd(Client c) {
        long cursor = parseCursor(c.arg(2));
        RObj o = srv().lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addRaw(EMPTY_SCAN); return; }
        if (o.type != RObj.HASH) throw RedisError.wrongType();
        scanGeneric(c, o, cursor);
    }

    private static void sscanCmd(Client c) {
        long cursor = parseCursor(c.arg(2));
        RObj o = srv().lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addRaw(EMPTY_SCAN); return; }
        if (o.type != RObj.SET) throw RedisError.wrongType();
        scanGeneric(c, o, cursor);
    }

    private static void zscanCmd(Client c) {
        long cursor = parseCursor(c.arg(2));
        RObj o = srv().lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addRaw(EMPTY_SCAN); return; }
        if (o.type != RObj.ZSET) throw RedisError.wrongType();
        scanGeneric(c, o, cursor);
    }

    private static void scanGeneric(Client c, RObj o, long cursor) {
        Server s = srv();
        long count = 10;
        byte[] pat = null;
        boolean usePattern = false;
        String typename = null;
        boolean noValues = false;

        int i = (o == null) ? 2 : 3;
        while (i < c.argc) {
            int j = c.argc - i;
            if (c.argIs(i, "count") && j >= 2) {
                count = c.argLong(i + 1);
                if (count < 1) throw RedisError.syntax();
                i += 2;
            } else if (c.argIs(i, "match") && j >= 2) {
                pat = c.arg(i + 1);
                usePattern = !(pat.length == 1 && pat[0] == '*');
                i += 2;
            } else if (c.argIs(i, "type") && o == null && j >= 2) {
                typename = c.argLower(i + 1);
                i += 2;
            } else if (c.argIs(i, "novalues")) {
                if (o == null || o.type != RObj.HASH)
                    throw new RedisError("ERR NOVALUES option can only be used in HSCAN");
                noValues = true;
                i++;
            } else {
                throw RedisError.syntax();
            }
        }

        long now = s.mstime();
        List<byte[]> out = new ArrayList<>();

        if (o == null) {
            Db d = s.dbs[c.db];
            ScanStep st = bucketScan(new ArrayList<>(d.dict.keySet()), cursor, count);
            List<byte[]> matched = new ArrayList<>();
            for (Key k : st.keys) {
                if (usePattern && !Util.stringmatch(pat, k.b, false)) continue;
                matched.add(k.b);
            }
            // Step 3: drop (and lazily delete) keys that are gone or of the wrong type.
            for (byte[] kb : matched) {
                Key k = new Key(kb);
                if (typename != null) {
                    RObj v = s.lookupKey(c.db, kb, true);
                    /* objectTypeCompare() uses strcasecmp, which matters for the
                     * mixed-case names the bundled module types report. */
                    if (v == null || !v.typeName().equalsIgnoreCase(typename)) continue;
                    out.add(kb);
                    continue;
                }
                if (s.expireIfNeeded(d, k) || s.keyIsExpired(d, k)) continue;
                out.add(kb);
            }
            replyScan(c, st.cursor, out);
            return;
        }

        String enc = encodingOf(o);

        if (o.type == RObj.SET) {
            RSet set = (RSet) o.ptr;
            if (enc.equals("hashtable")) {
                ScanStep st = bucketScan(new ArrayList<>(set.set), cursor, count);
                for (Key k : st.keys) {
                    if (usePattern && !Util.stringmatch(pat, k.b, false)) continue;
                    out.add(k.b);
                }
                replyScan(c, st.cursor, out);
            } else {
                for (Key k : set.set) {
                    if (usePattern && !Util.stringmatch(pat, k.b, false)) continue;
                    out.add(k.b);
                }
                replyScan(c, 0, out);
            }
            return;
        }

        if (o.type == RObj.HASH) {
            RHash h = (RHash) o.ptr;
            if (enc.equals("hashtable")) {
                ScanStep st = bucketScan(new ArrayList<>(h.map.keySet()), cursor, count);
                for (Key k : st.keys) {
                    byte[] val = h.map.get(k);
                    if (val == null) continue;
                    Long tt = h.ttl(k);
                    if (tt != null && now >= tt) continue;
                    if (usePattern && !Util.stringmatch(pat, k.b, false)) continue;
                    out.add(k.b);
                    if (!noValues) out.add(val);
                }
                replyScan(c, st.cursor, out);
            } else {
                for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                    Long tt = h.ttl(e.getKey());
                    if (tt != null && now >= tt) continue;
                    if (usePattern && !Util.stringmatch(pat, e.getKey().b, false)) continue;
                    out.add(e.getKey().b);
                    if (!noValues) out.add(e.getValue());
                }
                replyScan(c, 0, out);
            }
            return;
        }

        // ZSET
        RZSet z = (RZSet) o.ptr;
        if (enc.equals("skiplist")) {
            ScanStep st = bucketScan(new ArrayList<>(z.dict.keySet()), cursor, count);
            for (Key k : st.keys) {
                Double sc = z.dict.get(k);
                if (sc == null) continue;
                if (usePattern && !Util.stringmatch(pat, k.b, false)) continue;
                out.add(k.b);
                out.add(Util.b(Util.fmtG(sc, 17)));
            }
            replyScan(c, st.cursor, out);
        } else {
            for (RZSet.Entry e : z.sorted) {
                if (usePattern && !Util.stringmatch(pat, e.member.b, false)) continue;
                out.add(e.member.b);
                out.add(Util.d2string(e.score));
            }
            replyScan(c, 0, out);
        }
    }

    private static void replyScan(Client c, long cursor, List<byte[]> items) {
        c.addArray(2);
        c.addBulk(Util.b(Long.toUnsignedString(cursor)));
        c.addArray(items.size());
        for (byte[] b : items) c.addBulk(b);
    }

    // ------------------------------------------------------------------
    // OBJECT
    // ------------------------------------------------------------------
    private static void objectCmd(Client c) {
        Map<String, Cmd> m = CommandTable.SUBS.get("object");
        Cmd cmd = m == null ? null : m.get(c.argLower(1));
        if (cmd == null || (cmd.arity > 0 && cmd.arity != c.argc) || (c.argc < -cmd.arity)) {
            String name = c.argStr(1);
            if (name.length() > 128) name = name.substring(0, 128);
            throw new RedisError("ERR unknown subcommand or wrong number of arguments for '"
                    + name + "'. Try OBJECT HELP.");
        }
        cmd.handler.run(c);
    }

    /** objectCommandLookup(): no LRU update, no keymiss notification. */
    private static RObj objectLookup(Client c) {
        Db d = srv().dbs[c.db];
        Key k = new Key(c.arg(2));
        srv().expireIfNeeded(d, k);
        if (srv().keyIsExpired(d, k)) return null;
        return d.dict.get(k);
    }

    private static void objectEncodingCmd(Client c) {
        RObj o = objectLookup(c);
        if (o == null) { c.addNil(); return; }
        c.addBulk(Util.b(encodingOf(o)));
    }

    private static void objectRefcountCmd(Client c) {
        RObj o = objectLookup(c);
        if (o == null) { c.addNil(); return; }
        c.addInt(refcountOf(o));
    }

    private static void objectIdletimeCmd(Client c) {
        RObj o = objectLookup(c);
        if (o == null) { c.addNil(); return; }
        if (lfuPolicy()) throw new RedisError("ERR An LFU maxmemory policy is selected, idle time not "
                + "tracked. Please note that when switching between policies at runtime LRU and LFU data "
                + "will take some time to adjust.");
        long idle = nowSeconds() - o.lru;
        if (idle < 0) idle = 0;
        c.addInt(idle);
    }

    private static void objectFreqCmd(Client c) {
        RObj o = objectLookup(c);
        if (o == null) { c.addNil(); return; }
        if (!lfuPolicy()) throw new RedisError("ERR An LFU maxmemory policy is not selected, access "
                + "frequency not tracked. Please note that when switching between policies at runtime LRU "
                + "and LFU data will take some time to adjust.");
        c.addInt(Mem.lfuDecrAndReturn(o));
    }

    private static void objectHelpCmd(Client c) {
        String[] help = {
            "OBJECT <subcommand> [<arg> [value] [opt] ...]. Subcommands are:",
            "ENCODING <key>",
            "    Return the kind of internal representation used in order to store the value",
            "    associated with a <key>.",
            "FREQ <key>",
            "    Return the access frequency index of the <key>. The returned integer is",
            "    proportional to the logarithm of the recent access frequency of the key.",
            "IDLETIME <key>",
            "    Return the idle time of the <key>, that is the approximated number of",
            "    seconds elapsed since the last access to the key.",
            "REFCOUNT <key>",
            "    Return the number of references of the value associated with the specified",
            "    <key>.",
            "HELP",
            "    Print this help.",
        };
        c.addArray(help.length);
        for (String l : help) c.addSimple(l);
    }

    static long refcountOf(RObj o) {
        if (o.type == RObj.STRING && o.encoding == RObj.ENC_INT && o.ptr instanceof Long) {
            long v = (Long) o.ptr;
            long maxmem = srv().config.getLong("maxmemory");
            String pol = srv().config.getOr("maxmemory-policy", "noeviction").toLowerCase();
            boolean noShared = pol.contains("lru") || pol.contains("lfu");
            if ((maxmem == 0 || !noShared) && v >= 0 && v < 10000) return 2147483647L;
        }
        return 1;
    }

    // ------------------------------------------------------------------
    // OBJECT ENCODING: derive the name redis would use for this content
    // ------------------------------------------------------------------
    private static final long[] QL_OPT_LEVEL = {4096, 8192, 16384, 32768, 65536};
    private static final long QL_SIZE_SAFETY_LIMIT = 8192;

    /**
     * object.c strEncoding(): the encoding name redis 2.8 reports. The encoding
     * itself is maintained incrementally by the type implementations (a ziplist
     * that outgrows its limits is converted once and never converted back), so
     * it is simply read off the object here.
     */
    public static String encodingOf(RObj o) {
        return o.encodingName();
    }

    /** listpack.c lpBytes() for a sequence of entries. */
    static long lpBytes(Collection<byte[]> items) {
        long total = 6 + 1; /* LP_HDR_SIZE + LP_EOF */
        for (byte[] e : items) {
            long enclen = lpEntryEncLen(e);
            total += enclen + lpBacklenBytes(enclen);
        }
        return total;
    }

    private static long lpEntryEncLen(byte[] e) {
        Long v = Util.string2ll(e);
        if (v != null) {
            long x = v;
            if (x >= 0 && x <= 127) return 1;
            if (x >= -4096 && x <= 4095) return 2;
            if (x >= -32768 && x <= 32767) return 3;
            if (x >= -8388608 && x <= 8388607) return 4;
            if (x >= -2147483648L && x <= 2147483647L) return 5;
            return 9;
        }
        int size = e.length;
        if (size < 64) return 1 + size;
        if (size < 4096) return 2 + size;
        return 5 + (long) size;
    }

    private static long lpBacklenBytes(long l) {
        if (l <= 127) return 1;
        if (l < 16383) return 2;
        if (l < 2097151) return 3;
        if (l < 268435455) return 4;
        return 5;
    }

    private static int listEncodingConst(RObj o) {
        return Rdb.listEncoding(srv(), (RList) o.ptr);
    }

    // ------------------------------------------------------------------
    // DUMP / RESTORE
    // ------------------------------------------------------------------
    private static void dumpCmd(Client c) {
        RObj o = srv().lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        c.addBulk(Rdb.dumpPayload(o));
    }

    private static void restoreCmd(Client c) {
        Server s = srv();
        Db d = s.dbs[c.db];
        boolean replace = false, absttl = false;
        long lfuFreq = -1, lruIdle = -1;

        for (int j = 4; j < c.argc; j++) {
            int additional = c.argc - j - 1;
            if (c.argIs(j, "replace")) {
                replace = true;
            } else if (c.argIs(j, "absttl")) {
                absttl = true;
            } else if (c.argIs(j, "idletime") && additional >= 1 && lfuFreq == -1) {
                lruIdle = c.argLong(j + 1);
                if (lruIdle < 0) throw new RedisError("ERR Invalid IDLETIME value, must be >= 0");
                j++;
            } else if (c.argIs(j, "freq") && additional >= 1 && lruIdle == -1) {
                lfuFreq = c.argLong(j + 1);
                if (lfuFreq < 0 || lfuFreq > 255)
                    throw new RedisError("ERR Invalid FREQ value, must be >= 0 and <= 255");
                j++;
            } else {
                throw RedisError.syntax();
            }
        }

        byte[] key = c.arg(1);
        Key k = new Key(key);
        if (!replace && s.lookupKeyWrite(c, key) != null)
            throw new RedisError("BUSYKEY Target key name already exists.");

        long ttl = c.argLong(2);
        if (ttl < 0) throw new RedisError("ERR Invalid TTL value, must be >= 0");

        RObj obj = Rdb.restorePayload(c.arg(3));
        if (obj == null) throw new RedisError("ERR Bad data format");

        boolean deleted = false;
        if (replace) deleted = d.delete(k);

        if (ttl != 0 && !absttl) ttl += s.mstime();
        if (ttl != 0 && checkAlreadyExpired(s, ttl)) {
            if (deleted) {
                boolean lazy = s.config.getBool("lazyfree-lazy-server-del");
                s.replOverride = new byte[][]{Util.b(lazy ? "UNLINK" : "DEL"), key};
                s.signalModifiedKey(c.db, k);
                s.notifyKeyspaceEvent('g', "del", key, c.db);
                s.dirty++;
            }
            c.addOk();
            return;
        }

        dbAddNotify(c.db, key, obj);
        if (ttl != 0) {
            d.setExpire(k, ttl);
            if (!absttl) {
                // Propagate the TTL as an absolute timestamp.
                byte[][] repl = new byte[c.argc + 1][];
                System.arraycopy(c.argv, 0, repl, 0, c.argc);
                repl[2] = Util.ll2string(ttl);
                repl[c.argc] = Util.b("ABSTTL");
                s.replOverride = repl;
            }
        }
        objectSetLRUOrLFU(obj, lfuFreq, lruIdle);
        s.signalModifiedKey(c.db, k);
        s.notifyKeyspaceEvent('g', "restore", key, c.db);
        c.addOk();
        s.dirty++;
    }

    // ------------------------------------------------------------------
    // MIGRATE (cluster.c migrateCommand)
    // ------------------------------------------------------------------

    /**
     * MIGRATE host port key|"" destination-db timeout [COPY] [REPLACE]
     *         [AUTH password | AUTH2 username password] [KEYS key ...]
     *
     * Connects to the target instance, SELECTs the destination database and
     * RESTOREs every serialized value with its remaining TTL, then deletes the
     * local keys unless COPY was given. The command is replicated as a DEL of
     * the keys the target acknowledged.
     */
    private static void migrateCmd(Client c) {
        Server s = srv();
        boolean copy = false, replace = false;
        byte[] username = null, password = null;
        int firstKey = 3;
        int numKeys = 1;

        for (int j = 6; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "copy")) {
                copy = true;
            } else if (c.argIs(j, "replace")) {
                replace = true;
            } else if (c.argIs(j, "auth")) {
                if (moreargs < 1) throw RedisError.syntax();
                password = c.arg(++j);
            } else if (c.argIs(j, "auth2")) {
                if (moreargs < 2) throw RedisError.syntax();
                username = c.arg(++j);
                password = c.arg(++j);
            } else if (c.argIs(j, "keys")) {
                if (c.arg(3).length != 0)
                    throw new RedisError("ERR When using MIGRATE KEYS option, the key argument"
                            + " must be set to the empty string");
                firstKey = j + 1;
                numKeys = c.argc - j - 1;
                break;
            } else {
                throw RedisError.syntax();
            }
        }

        long timeout = argLongOrErr(c, 5);
        long dbid = argLongOrErr(c, 4);
        if (timeout <= 0) timeout = 1000;

        /* Collect the keys that are actually here. */
        List<byte[]> kv = new ArrayList<>();
        List<RObj> ov = new ArrayList<>();
        for (int j = 0; j < numKeys; j++) {
            RObj o = s.lookupKeyRead(c, c.arg(firstKey + j));
            if (o != null) { ov.add(o); kv.add(c.arg(firstKey + j)); }
        }
        if (kv.isEmpty()) { c.addSimple("NOKEY"); return; }

        /* Build the pipeline of commands to send. */
        Buf cmd = new Buf(1024);
        if (password != null) {
            if (username != null) writeCmd(cmd, Util.b("AUTH"), username, password);
            else writeCmd(cmd, Util.b("AUTH"), password);
        }
        writeCmd(cmd, Util.b("SELECT"), Util.ll2string(dbid));

        List<byte[]> sent = new ArrayList<>();
        for (int j = 0; j < kv.size(); j++) {
            long ttl = 0;
            Long expireat = s.dbs[c.db].getExpire(new Key(kv.get(j)));
            if (expireat != null) {
                ttl = expireat - s.mstime();
                if (ttl < 0) continue;
                if (ttl < 1) ttl = 1;
            }
            sent.add(kv.get(j));
            byte[] payload = Rdb.dumpPayload(ov.get(j));
            /* cluster.c migrateCommand(): in cluster mode the target may be
             * importing the slot, so the RESTORE has to carry the ASKING flag. */
            byte[] restore = Util.b(s.cluster != null ? "RESTORE-ASKING" : "RESTORE");
            if (replace) {
                writeCmd(cmd, restore, kv.get(j), Util.ll2string(ttl), payload,
                        Util.b("REPLACE"));
            } else {
                writeCmd(cmd, restore, kv.get(j), Util.ll2string(ttl), payload);
            }
        }

        String host = c.argStr(1);
        int port;
        try { port = Integer.parseInt(c.argStr(2).trim()); }
        catch (NumberFormatException e) { port = 0; }

        java.net.Socket sock = new java.net.Socket();
        java.io.InputStream in;
        java.io.OutputStream out;
        try {
            sock.connect(new java.net.InetSocketAddress(host, port), (int) Math.min(timeout, 600000));
            sock.setTcpNoDelay(true);
            sock.setSoTimeout((int) Math.min(timeout, 600000));
            in = sock.getInputStream();
            out = sock.getOutputStream();
        } catch (Exception e) {
            try { sock.close(); } catch (Exception ignored) {}
            throw new RedisError("IOERR error or timeout connecting to the client");
        }

        boolean writeError = false;
        String targetErr = null;
        List<byte[]> deleted = new ArrayList<>();
        boolean socketError = false;
        try {
            out.write(cmd.a, 0, cmd.len);
            out.flush();
        } catch (Exception e) {
            writeError = true;
            socketError = true;
        }

        String authReply = null, selectReply = null;
        if (!socketError && password != null) {
            authReply = readLine(in);
            if (authReply == null) socketError = true;
        }
        if (!socketError) {
            selectReply = readLine(in);
            if (selectReply == null) socketError = true;
        }

        if (!socketError) {
            for (byte[] key : sent) {
                String reply = readLine(in);
                if (reply == null) { socketError = true; break; }
                boolean err = (authReply != null && authReply.startsWith("-"))
                        || (selectReply != null && selectReply.startsWith("-"))
                        || reply.startsWith("-");
                if (err) {
                    if (targetErr == null) {
                        String eb;
                        if (authReply != null && authReply.startsWith("-")) eb = authReply;
                        else if (selectReply != null && selectReply.startsWith("-")) eb = selectReply;
                        else eb = reply;
                        targetErr = eb.substring(1);
                    }
                } else if (!copy) {
                    s.dbDelete(c.db, key);
                    s.notifyKeyspaceEvent('g', "del", key, c.db);
                    s.dirty++;
                    deleted.add(key);
                }
            }
        }
        try { sock.close(); } catch (Exception ignored) {}

        if (!copy && !deleted.isEmpty()) {
            /* Translate MIGRATE into DEL for replication and the AOF. */
            byte[][] repl = new byte[deleted.size() + 1][];
            repl[0] = Util.b("DEL");
            for (int i = 0; i < deleted.size(); i++) repl[i + 1] = deleted.get(i);
            s.replOverride = repl;
        }

        if (targetErr != null) throw new RedisError("ERR Target instance replied with error: " + targetErr);
        if (socketError)
            throw new RedisError("IOERR error or timeout " + (writeError ? "writing" : "reading")
                    + " to target instance");
        c.addOk();
    }

    /** getLongFromObjectOrReply(c, o, &target, NULL) */
    private static long argLongOrErr(Client c, int idx) {
        Long v = Util.string2ll(c.argv[idx]);
        if (v == null) throw RedisError.notInt();
        return v;
    }

    /** rioWriteBulkCount()/rioWriteBulkString(): one command in RESP form. */
    private static void writeCmd(Buf b, byte[]... argv) {
        b.put((byte) '*').putLong(argv.length).crlf();
        for (byte[] a : argv) b.put((byte) '$').putLong(a.length).crlf().put(a).crlf();
    }

    /** connSyncReadLine(): one CRLF terminated line, or null on error/timeout. */
    private static String readLine(java.io.InputStream in) {
        StringBuilder sb = new StringBuilder();
        try {
            while (sb.length() < 1024) {
                int ch = in.read();
                if (ch < 0) return null;
                if (ch == '\n') break;
                if (ch != '\r') sb.append((char) ch);
            }
        } catch (Exception e) {
            return null;
        }
        return sb.toString();
    }

    private static void objectSetLRUOrLFU(RObj o, long lfuFreq, long lruIdle) {
        if (lfuPolicy()) {
            if (lfuFreq >= 0) o.lfu = (int) lfuFreq;
        } else if (lruIdle >= 0) {
            o.lru = nowSeconds() - lruIdle;
        }
    }

    // ------------------------------------------------------------------
    // SORT / SORT_RO
    // ------------------------------------------------------------------
    private static final class SortItem {
        byte[] obj;
        double score;
        byte[] cmpobj;
    }

    private static void sortGeneric(Client c, boolean readonly) {
        Server s = srv();
        boolean desc = false, alpha = false, dontsort = false;
        long limitStart = 0, limitCount = -1;
        byte[] sortby = null, storekey = null;
        List<byte[]> getops = new ArrayList<>();

        /* sort.c: the BY and GET patterns can name any key, so they are only
         * allowed when the user has unrestricted read access to the key space. */
        Cmd self = CommandTable.lookup(readonly ? "sort_ro" : "sort");
        boolean fullKeyAccess = self == null || AclState.userHasFullKeyAccess(c, self);

        int j = 2;
        while (j < c.argc) {
            int leftargs = c.argc - j - 1;
            if (c.argIs(j, "asc")) {
                desc = false;
            } else if (c.argIs(j, "desc")) {
                desc = true;
            } else if (c.argIs(j, "alpha")) {
                alpha = true;
            } else if (c.argIs(j, "limit") && leftargs >= 2) {
                limitStart = c.argLong(j + 1);
                limitCount = c.argLong(j + 2);
                j += 2;
            } else if (!readonly && c.argIs(j, "store") && leftargs >= 1) {
                storekey = c.arg(j + 1);
                j++;
            } else if (c.argIs(j, "by") && leftargs >= 1) {
                sortby = c.arg(j + 1);
                if (indexOf(sortby, (byte) '*', 0) < 0) dontsort = true;
                else if (!fullKeyAccess)
                    throw new RedisError("ERR BY option of SORT denied due to insufficient ACL permissions.");
                j++;
            } else if (c.argIs(j, "get") && leftargs >= 1) {
                if (!fullKeyAccess)
                    throw new RedisError("ERR GET option of SORT denied due to insufficient ACL permissions.");
                getops.add(c.arg(j + 1));
                j++;
            } else {
                throw RedisError.syntax();
            }
            j++;
        }

        RObj sortval = s.lookupKeyRead(c, c.arg(1));
        if (sortval != null && sortval.type != RObj.SET && sortval.type != RObj.LIST
                && sortval.type != RObj.ZSET) {
            throw RedisError.wrongType();
        }
        int valType = sortval == null ? RObj.LIST : sortval.type;

        // Sorting a set with no explicit order must be deterministic when the output
        // is stored or when the caller is a script.
        if (dontsort && valType == RObj.SET && (storekey != null || c.fake)) {
            dontsort = false;
            alpha = true;
            sortby = null;
        }

        int fullLen;
        if (sortval == null) fullLen = 0;
        else if (valType == RObj.LIST) fullLen = ((RList) sortval.ptr).size();
        else if (valType == RObj.SET) fullLen = ((RSet) sortval.ptr).size();
        else fullLen = ((RZSet) sortval.ptr).size();

        int start = (int) Math.min(Math.max(limitStart, 0), fullLen);
        long lcount = Math.min(Math.max(limitCount, -1), fullLen);
        long endL = (lcount < 0) ? fullLen - 1 : start + lcount - 1;
        if (start >= fullLen) { start = fullLen - 1; endL = fullLen - 2; }
        if (endL >= fullLen) endL = fullLen - 1;
        int end = (int) endL;

        int vectorlen = fullLen;
        if ((valType == RObj.ZSET || valType == RObj.LIST) && dontsort
                && (start != 0 || end != fullLen - 1)) {
            vectorlen = Math.max(0, end - start + 1);
        }

        List<SortItem> vec = new ArrayList<>(Math.max(0, vectorlen));

        if (valType == RObj.LIST && dontsort) {
            if (end >= start && sortval != null) {
                List<byte[]> all = new ArrayList<>(((RList) sortval.ptr).l);
                if (desc) {
                    for (int idx = fullLen - start - 1; idx >= 0 && vec.size() < vectorlen; idx--)
                        vec.add(item(all.get(idx)));
                } else {
                    for (int idx = start; idx < fullLen && vec.size() < vectorlen; idx++)
                        vec.add(item(all.get(idx)));
                }
            }
            end -= start;
            start = 0;
        } else if (valType == RObj.LIST) {
            if (sortval != null) for (byte[] e : ((RList) sortval.ptr).l) vec.add(item(e));
        } else if (valType == RObj.SET) {
            for (Key k : ((RSet) sortval.ptr).set) vec.add(item(k.b));
        } else if (dontsort) {
            List<RZSet.Entry> all = ((RZSet) sortval.ptr).all(desc);
            for (int idx = start; idx < all.size() && vec.size() < vectorlen; idx++)
                vec.add(item(all.get(idx).member.b));
            end -= start;
            start = 0;
        } else {
            for (RZSet.Entry e : ((RZSet) sortval.ptr).sorted) vec.add(item(e.member.b));
        }

        boolean conversionError = false;
        if (!dontsort) {
            for (SortItem it : vec) {
                byte[] byval;
                if (sortby != null) {
                    byval = lookupKeyByPattern(c, sortby, it.obj);
                    if (byval == null) continue;
                } else {
                    byval = it.obj;
                }
                if (alpha) {
                    if (sortby != null) it.cmpobj = byval;
                } else {
                    Double dv = sortStrtod(byval);
                    if (dv == null) conversionError = true;
                    else it.score = dv;
                }
            }
            final boolean fDesc = desc, fAlpha = alpha;
            final boolean byPattern = sortby != null;
            vec.sort((a, b) -> {
                int cmp;
                if (!fAlpha) {
                    if (a.score > b.score) cmp = 1;
                    else if (a.score < b.score) cmp = -1;
                    else cmp = compareBytes(a.obj, b.obj);
                } else if (byPattern) {
                    if (a.cmpobj == null || b.cmpobj == null) {
                        if (a.cmpobj == b.cmpobj) cmp = 0;
                        else if (a.cmpobj == null) cmp = -1;
                        else cmp = 1;
                    } else {
                        cmp = compareBytes(a.cmpobj, b.cmpobj);
                    }
                } else {
                    cmp = compareBytes(a.obj, b.obj);
                }
                return fDesc ? -cmp : cmp;
            });
        }

        int span = end - start + 1;
        if (span < 0) span = 0;
        int outputlen = getops.isEmpty() ? span : getops.size() * span;

        if (conversionError) {
            throw new RedisError("ERR One or more scores can't be converted into double");
        }

        if (storekey == null) {
            c.addArray(outputlen);
            for (int idx = start; idx <= end; idx++) {
                SortItem it = vec.get(idx);
                if (getops.isEmpty()) c.addBulk(it.obj);
                for (byte[] pattern : getops) {
                    byte[] val = lookupKeyByPattern(c, pattern, it.obj);
                    if (val == null) c.addNil();
                    else c.addBulk(val);
                }
            }
            return;
        }

        RList out = new RList();
        for (int idx = start; idx <= end; idx++) {
            SortItem it = vec.get(idx);
            if (getops.isEmpty()) {
                out.l.addLast(Util.copy(it.obj));
            } else {
                for (byte[] pattern : getops) {
                    byte[] val = lookupKeyByPattern(c, pattern, it.obj);
                    out.l.addLast(val == null ? Util.EMPTY : Util.copy(val));
                }
            }
        }
        if (outputlen > 0) {
            RObj lobj = new RObj(RObj.LIST, RObj.ENC_LISTPACK, out);
            lobj.encoding = listEncodingConst(lobj);
            s.setKey(c, storekey, lobj);
            s.signalKeyAsReady(c.db, storekey);
            s.notifyKeyspaceEvent('l', "sortstore", storekey, c.db);
            s.dirty += outputlen;
        } else if (s.dbDelete(c.db, storekey)) {
            s.signalModifiedKey(c.db, new Key(storekey));
            s.notifyKeyspaceEvent('g', "del", storekey, c.db);
            s.dirty++;
        }
        c.addInt(outputlen);
    }

    private static SortItem item(byte[] b) {
        SortItem it = new SortItem();
        it.obj = b;
        return it;
    }

    private static int compareBytes(byte[] a, byte[] b) {
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) {
            int d = (a[i] & 0xff) - (b[i] & 0xff);
            if (d != 0) return d;
        }
        return a.length - b.length;
    }

    private static int indexOf(byte[] hay, byte needle, int from) {
        for (int i = from; i < hay.length; i++) if (hay[i] == needle) return i;
        return -1;
    }

    private static int indexOfArrow(byte[] hay, int from) {
        for (int i = from; i + 1 < hay.length; i++) {
            if (hay[i] == '-' && hay[i + 1] == '>') return i;
        }
        return -1;
    }

    /** sort.c lookupKeyByPattern(). */
    private static byte[] lookupKeyByPattern(Client c, byte[] pattern, byte[] subst) {
        Server s = srv();
        if (pattern.length == 1 && pattern[0] == '#') return subst;

        int star = indexOf(pattern, (byte) '*', 0);
        if (star < 0) return null;

        int fieldlen = 0;
        byte[] field = null;
        int f = indexOfArrow(pattern, star + 1);
        if (f >= 0 && f + 2 < pattern.length) {
            fieldlen = pattern.length - f - 2;
            field = Arrays.copyOfRange(pattern, f + 2, pattern.length);
        }

        int prefixlen = star;
        int postfixlen = pattern.length - (prefixlen + 1) - (fieldlen != 0 ? fieldlen + 2 : 0);
        if (postfixlen < 0) return null;
        byte[] keyb = new byte[prefixlen + subst.length + postfixlen];
        System.arraycopy(pattern, 0, keyb, 0, prefixlen);
        System.arraycopy(subst, 0, keyb, prefixlen, subst.length);
        System.arraycopy(pattern, star + 1, keyb, prefixlen + subst.length, postfixlen);

        RObj o = s.lookupKeyRead(c, keyb);
        if (o == null) return null;

        if (field != null) {
            if (o.type != RObj.HASH) return null;
            RHash h = (RHash) o.ptr;
            Key fk = new Key(field);
            byte[] v = h.map.get(fk);
            if (v == null) return null;
            Long tt = h.ttl(fk);
            if (tt != null && s.mstime() >= tt) return null;
            return v;
        }
        if (o.type != RObj.STRING) return null;
        return o.str();
    }

    /**
     * strtod() plus the full-consumption check redis performs on SORT weights.
     * Returns null when redis would flag "One or more scores can't be converted".
     */
    private static Double sortStrtod(byte[] b) {
        if (b.length == 0) return 0.0;  /* strtod converts nothing but endptr is the NUL */
        String t = Util.s(b);
        int i = 0;
        while (i < t.length() && isCSpace(t.charAt(i))) i++;
        String u = t.substring(i);
        if (u.isEmpty()) return null;
        if (isCSpace(u.charAt(u.length() - 1))) return null;
        String lu = u.toLowerCase();
        if (lu.equals("inf") || lu.equals("+inf") || lu.equals("infinity") || lu.equals("+infinity"))
            return Double.POSITIVE_INFINITY;
        if (lu.equals("-inf") || lu.equals("-infinity")) return Double.NEGATIVE_INFINITY;
        if (lu.equals("nan") || lu.equals("-nan") || lu.equals("+nan")) return null;
        for (int k = 0; k < u.length(); k++) {
            char ch = u.charAt(k);
            if (ch == 'd' || ch == 'D' || ch == 'f' || ch == 'F' || ch == 'x' || ch == 'X') return null;
        }
        try {
            double dv = Double.parseDouble(u);
            if (Double.isNaN(dv)) return null;
            return dv;
        } catch (NumberFormatException e) {
            return null;
        }
    }
}
