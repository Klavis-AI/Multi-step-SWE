package redis;

import java.util.*;

/**
 * Hash type commands: a port of redis 8.0 src/t_hash.c, including the
 * Redis 7.4 hash-field-expiration (HFE) family.
 *
 * HSET HSETNX HMSET HGET HMGET HDEL HLEN HSTRLEN HKEYS HVALS HGETALL HEXISTS
 * HINCRBY HINCRBYFLOAT HRANDFIELD HGETEX HGETDEL
 * HEXPIRE HPEXPIRE HEXPIREAT HPEXPIREAT HPERSIST HTTL HPTTL HEXPIRETIME HPEXPIRETIME
 */
public final class HashCmds {
    private HashCmds() {}

    private static final Random RND = new Random();

    /* Reserve 2 bits out of the hash-field expiration time (EB_EXPIRE_TIME_MAX >> 2). */
    private static final long HFE_MAX_ABS_TIME_MSEC = 0x0000FFFFFFFFFFFFL >> 2;

    /* hashTypeSetEx() results. */
    private static final int HSETEX_OK = 1, HSETEX_NO_FIELD = -2,
                             HSETEX_NO_CONDITION_MET = 0, HSETEX_DELETED = 2;
    /* httlGenericCommand() results. */
    private static final int HFE_GET_NO_FIELD = -2, HFE_GET_NO_TTL = -1;
    /* hpersistCommand() results. */
    private static final int HFE_PERSIST_NO_FIELD = -2, HFE_PERSIST_NO_TTL = -1, HFE_PERSIST_OK = 1;

    /* [NX | XX | GT | LT] */
    private static final int HFE_NX = 1, HFE_XX = 2, HFE_GT = 4, HFE_LT = 8;
    /* HGETEX options */
    private static final int HFE_EX = 1 << 0, HFE_PX = 1 << 1, HFE_EXAT = 1 << 2,
                             HFE_PXAT = 1 << 3, HFE_PERSIST = 1 << 4;
    /* HSETEX only: KEEPTTL shares the "one expiration argument" slot. */
    private static final int HFE_KEEPTTL = 1 << 5;
    /* HSETEX [FNX | FXX] */
    private static final int HSETEX_FNX = 1, HSETEX_FXX = 2;

    /* hashTypeGetValue() results. */
    private static final int GETF_OK = 0, GETF_NOT_FOUND = 1, GETF_EXPIRED = 2, GETF_EXPIRED_HASH = 3;
    /* HFE_LAZY_* lookup flags. */
    private static final int LAZY_NO_NOTIFICATION = 1, LAZY_NO_SIGNAL = 2, LAZY_AVOID_HASH_DEL = 4;

    private static final byte[] HDEL_CMD = Util.b("HDEL");
    private static final byte[] HSET_CMD = Util.b("HSET");
    private static final byte[] HSETEX_CMD = Util.b("HSETEX");
    private static final byte[] HPEXPIREAT_CMD = Util.b("HPEXPIREAT");
    private static final byte[] HPERSIST_CMD = Util.b("HPERSIST");
    private static final byte[] FIELDS_ARG = Util.b("FIELDS");
    private static final byte[] PXAT_ARG = Util.b("PXAT");
    private static final byte[] KEEPTTL_ARG = Util.b("KEEPTTL");
    private static final byte[][] NO_PROPAGATION = new byte[0][];

    // ------------------------------------------------------------------
    // small helpers
    // ------------------------------------------------------------------
    private static Server S() { return Server.I; }

    /**
     * commandTimeSnapshot(): redis reads the clock once per command, so a TTL of
     * `now + 1` can never be seen as already expired by the command that set it.
     */
    private static long cmdTime;

    private static long now() { return cmdTime; }

    private static Cmd.Handler snap(Cmd.Handler h) {
        return c -> { cmdTime = Server.I.mstime(); h.run(c); };
    }

    private static long cfg(String name, long dflt) {
        String v = Server.I.config.get(name);
        if (v == null || v.isEmpty()) return dflt;
        return Config.parseMemory(v);
    }

    private static RHash hash(RObj o) { return (RHash) o.ptr; }

    private static RObj createHash() { return new RObj(RObj.HASH, RObj.ENC_LISTPACK, new RHash()); }

    private static void notify(char cls, String event, byte[] key, int db) {
        S().notifyKeyspaceEvent(cls, event, key, db);
    }

    /** lookupKeyRead() + checkType(OBJ_HASH). */
    private static RObj lookupHashRead(Client c, byte[] key) {
        RObj o = S().lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != RObj.HASH) throw RedisError.wrongType();
        return o;
    }

    /** lookupKeyWrite() + checkType(OBJ_HASH). */
    private static RObj lookupHashWrite(Client c, byte[] key) {
        RObj o = S().lookupKeyWrite(c, key);
        if (o == null) return null;
        if (o.type != RObj.HASH) throw RedisError.wrongType();
        return o;
    }

    /** hashTypeLookupWriteOrCreate(). */
    private static RObj lookupWriteOrCreate(Client c, byte[] key) {
        RObj o = lookupHashWrite(c, key);
        if (o == null) { o = createHash(); S().dbAdd(c.db, key, o); }
        return o;
    }

    /** hashTypeTryConversion(): widen to a hashtable once the listpack limits are crossed. */
    private static void tryConversion(RObj o, byte[] field, byte[] value) {
        if (o.encoding == RObj.ENC_HT) return;
        long maxVal = cfg("hash-max-listpack-value", 64);
        if ((field != null && field.length > maxVal) || (value != null && value.length > maxVal)) {
            o.encoding = RObj.ENC_HT;
            return;
        }
        if (hash(o).size() > cfg("hash-max-listpack-entries", 128)) o.encoding = RObj.ENC_HT;
    }

    /**
     * hashTypeSetExInit(): a listpack hash that gains field TTLs is a listpackex.
     *
     * The key is also registered in the db's hash-field-expiry set, which is this
     * port's stand-in for redis 7.4's global `db->hexpires` ebuckets index: it is
     * what lets {@link #activeExpireHashFields} find the hashes that own a TTL
     * without walking the whole keyspace.  Registration is best effort -- a stale
     * entry (the key was deleted, renamed or overwritten with another type) is
     * dropped by the cycle itself when it validates the entry.
     */
    private static void enableHfe(Client c, byte[] key, RObj o) {
        RHash h = hash(o);
        if (h.ttls == null) h.ttls = new HashMap<>();
        if (o.encoding == RObj.ENC_LISTPACK) o.encoding = RObj.ENC_LISTPACK_EX;
        S().dbs[c.db].hexpires.add(new Key(key));
    }

    /**
     * t_hash.c activeExpireHashFieldCycle(): drop every field of `k` whose TTL has
     * passed, propagate one HDEL per field, fire a single `hexpired` event for the
     * hash and delete the hash itself once it is empty.  Returns false when the
     * entry should be removed from the db's registry (the key is gone, is no
     * longer a hash, or no longer owns any field TTL).
     */
    public static boolean activeExpireHashFields(Server s, Db d, Key k) {
        RObj o = d.dict.get(k);
        if (o == null || o.type != RObj.HASH) return false;
        RHash h = (RHash) o.ptr;
        if (!h.hasTtls()) return false;
        long now = s.mstime();
        List<byte[]> dead = new ArrayList<>();
        for (Map.Entry<Key, Long> e : h.ttls.entrySet())
            if (now >= e.getValue()) dead.add(e.getKey().b);
        if (dead.isEmpty()) return true;

        h.expireFields(now);
        for (byte[] f : dead) s.propagateExtra(d.id, new byte[][]{HDEL_CMD, k.b, f});
        s.notifyKeyspaceEvent('h', "hexpired", k.b, d.id);
        if (h.size() == 0) {
            s.notifyKeyspaceEvent('g', "del", k.b, d.id);
            s.dbDelete(d.id, k.b);
        }
        s.signalModifiedKey(d.id, k);
        return h.hasTtls();
    }

    /** hashTypeSet(). Returns true when an existing field was updated. */
    private static boolean hashSet(RObj o, byte[] field, byte[] value, boolean keepTtl) {
        RHash h = hash(o);
        Key fk = new Key(field);
        boolean update = h.map.containsKey(fk);
        h.map.put(fk, value);
        if (update && !keepTtl) h.clearTtl(fk);
        tryConversion(o, field, value);
        return update;
    }

    /**
     * string2ld(): strtold() with full-string validation. Unlike
     * Double.parseDouble() this rejects surrounding whitespace, embedded NULs
     * and the `d`/`f`/hex forms Java accepts but C's strtold does not.
     */
    private static Double string2ld(byte[] p) {
        if (p == null || p.length == 0 || p.length >= 5 * 1024) return null;
        String t = Util.s(p);
        String lower = t.toLowerCase();
        if (lower.equals("inf") || lower.equals("+inf")
                || lower.equals("infinity") || lower.equals("+infinity")) return Double.POSITIVE_INFINITY;
        if (lower.equals("-inf") || lower.equals("-infinity")) return Double.NEGATIVE_INFINITY;
        for (int i = 0; i < t.length(); i++) {
            char ch = t.charAt(i);
            if (ch == 'd' || ch == 'D' || ch == 'f' || ch == 'F' || ch == 'x' || ch == 'X') return null;
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == 0x0b || ch == 0)
                return null;
        }
        try {
            double d = Double.parseDouble(t);
            return Double.isNaN(d) ? null : d;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /** Is this field logically expired right now? */
    private static boolean isExpired(RHash h, Key fk, long now) {
        Long when = h.ttl(fk);
        return when != null && now >= when;
    }

    /**
     * hashTypeGetValue(): look a field up, lazily expiring it when its TTL has
     * passed. The value is stored in out[0] on GETF_OK.
     */
    private static int hashGetValue(Client c, byte[] key, RObj o, byte[] field,
                                    int flags, byte[][] out) {
        Server s = S();
        RHash h = hash(o);
        Key fk = new Key(field);
        byte[] v = h.map.get(fk);
        if (v == null) return GETF_NOT_FOUND;
        if (!isExpired(h, fk, now())) {
            if (out != null) out[0] = v;
            return GETF_OK;
        }

        /* Delete the field and propagate the deletion. */
        h.map.remove(fk);
        h.clearTtl(fk);
        s.propagateExtra(c.db, new byte[][]{HDEL_CMD, key, field});

        int res = GETF_EXPIRED;
        if ((flags & LAZY_NO_NOTIFICATION) == 0) notify('h', "hexpired", key, c.db);
        if (h.size() == 0 && (flags & LAZY_AVOID_HASH_DEL) == 0) {
            if ((flags & LAZY_NO_NOTIFICATION) == 0) notify('g', "del", key, c.db);
            s.dbDelete(c.db, key);
            res = GETF_EXPIRED_HASH;
        }
        if ((flags & LAZY_NO_SIGNAL) == 0) s.signalModifiedKey(c.db, new Key(key));
        return res;
    }

    /** addHashFieldToReply(). */
    private static int addHashFieldToReply(Client c, byte[] key, RObj o, byte[] field, int flags) {
        if (o == null) { c.addNil(); return GETF_NOT_FOUND; }
        byte[][] out = new byte[1][];
        int res = hashGetValue(c, key, o, field, flags, out);
        if (res == GETF_OK) c.addBulk(out[0]); else c.addNil();
        return res;
    }

    /**
     * hashTypeExpireIfNeeded(): drop every expired field of the hash, propagating
     * one HDEL per field. Returns true when the hash itself was deleted.
     */
    private static boolean expireIfNeeded(Client c, byte[] key, RObj o) {
        Server s = S();
        RHash h = hash(o);
        if (h.ttls == null || h.ttls.isEmpty()) return false;
        long now = now();
        List<byte[]> dead = new ArrayList<>();
        for (Map.Entry<Key, Long> e : h.ttls.entrySet())
            if (now >= e.getValue()) dead.add(e.getKey().b);
        if (dead.isEmpty()) return false;

        h.expireFields(now);
        for (byte[] f : dead) s.propagateExtra(c.db, new byte[][]{HDEL_CMD, key, f});
        notify('h', "hexpired", key, c.db);
        boolean deleted = false;
        if (h.size() == 0) {
            notify('g', "del", key, c.db);
            s.dbDelete(c.db, key);
            deleted = true;
        }
        s.signalModifiedKey(c.db, new Key(key));
        return deleted;
    }

    /** hashTypeLength(o, 1): the number of fields not logically expired at `now`. */
    private static int liveLength(RObj o, long now) {
        RHash h = hash(o);
        if (h.ttls == null || h.ttls.isEmpty()) return h.size();
        int n = 0;
        for (Key k : h.map.keySet()) if (!isExpired(h, k, now)) n++;
        return n;
    }

    /** getRangeLongFromObjectOrReply() with a custom message for both failures. */
    private static long rangeLong(Client c, int idx, long min, long max, String msg) {
        Long v = Util.string2ll(c.arg(idx));
        if (v == null) throw new RedisError("ERR " + msg);
        if (v < min || v > max) throw new RedisError("ERR " + msg);
        return v;
    }

    /** getRangeLongFromObjectOrReply() with a NULL message. */
    private static long rangeLong(Client c, int idx, long min, long max) {
        Long v = Util.string2ll(c.arg(idx));
        if (v == null) throw RedisError.notInt();
        if (v < min || v > max)
            throw new RedisError("ERR value is out of range, value must between " + min + " and " + max);
        return v;
    }

    private static RedisError badExpireTime(Client c) {
        return new RedisError("ERR invalid expire time in '" + c.cmdName() + "' command");
    }

    /** parseExpireTime(): read a TTL argument and return it as an absolute ms timestamp. */
    private static long parseExpireTime(Client c, int idx, boolean seconds, long basetime) {
        Long val = Util.string2ll(c.arg(idx));
        if (val == null) throw RedisError.notInt();
        long v = val;
        if (v < 0) throw new RedisError("ERR invalid expire time, must be >= 0");
        if (seconds) {
            if (v > HFE_MAX_ABS_TIME_MSEC / 1000) throw badExpireTime(c);
            v *= 1000;
        }
        if (v > HFE_MAX_ABS_TIME_MSEC - basetime) throw badExpireTime(c);
        return v + basetime;
    }

    private static final String ERR_FIELDS =
            "ERR Mandatory argument FIELDS is missing or not at the right position";
    private static final String ERR_NUMFIELDS_MATCH =
            "ERR The `numfields` parameter must match the number of arguments";
    private static final String MSG_NUMFIELDS_POSITIVE = "Number of fields must be a positive integer";
    private static final String MSG_NUMFIELDS_GT0 = "Parameter `numFields` should be greater than 0";
    /* HSETEX reports a bad numfields differently from the HFE family. */
    private static final String MSG_NUMFIELDS_INVALID = "invalid number of fields";
    private static final String ERR_HSETEX_ONE_TTL =
            "ERR Only one of EX, PX, EXAT, PXAT or KEEPTTL arguments can be specified";

    // ------------------------------------------------------------------
    // registration
    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("hset", -4, "write denyoom fast", 1, 1, 1, snap(HashCmds::hset));
        CommandTable.reg("hsetnx", 4, "write denyoom fast", 1, 1, 1, snap(HashCmds::hsetnx));
        CommandTable.reg("hmset", -4, "write denyoom fast", 1, 1, 1, snap(HashCmds::hset));
        CommandTable.reg("hget", 3, "readonly fast", 1, 1, 1, snap(HashCmds::hget));
        CommandTable.reg("hmget", -3, "readonly fast", 1, 1, 1, snap(HashCmds::hmget));
        CommandTable.reg("hdel", -3, "write fast", 1, 1, 1, snap(HashCmds::hdel));
        CommandTable.reg("hlen", 2, "readonly fast", 1, 1, 1, snap(HashCmds::hlen));
        CommandTable.reg("hstrlen", 3, "readonly fast", 1, 1, 1, snap(HashCmds::hstrlen));
        CommandTable.reg("hkeys", 2, "readonly", 1, 1, 1, snap(HashCmds::hkeys));
        CommandTable.reg("hvals", 2, "readonly", 1, 1, 1, snap(HashCmds::hvals));
        CommandTable.reg("hgetall", 2, "readonly", 1, 1, 1, snap(HashCmds::hgetall));
        CommandTable.reg("hexists", 3, "readonly fast", 1, 1, 1, snap(HashCmds::hexists));
        CommandTable.reg("hincrby", 4, "write denyoom fast", 1, 1, 1, snap(HashCmds::hincrby));
        CommandTable.reg("hincrbyfloat", 4, "write denyoom fast", 1, 1, 1, snap(HashCmds::hincrbyfloat));
        CommandTable.reg("hrandfield", -2, "readonly", 1, 1, 1, snap(HashCmds::hrandfield));
        CommandTable.reg("hgetex", -5, "write fast", 1, 1, 1, snap(HashCmds::hgetex));
        CommandTable.reg("hgetdel", -5, "write fast", 1, 1, 1, snap(HashCmds::hgetdel));
        /* Arity, flags and key spec match the HSETEX row of CommandMeta. */
        CommandTable.reg("hsetex", -6, "write denyoom fast", 1, 1, 1, snap(HashCmds::hsetex));

        CommandTable.reg("hexpire", -6, "write fast", 1, 1, 1,
                snap(c -> hexpireGeneric(c, now(), true)));
        CommandTable.reg("hpexpire", -6, "write fast", 1, 1, 1,
                snap(c -> hexpireGeneric(c, now(), false)));
        CommandTable.reg("hexpireat", -6, "write fast", 1, 1, 1,
                snap(c -> hexpireGeneric(c, 0, true)));
        CommandTable.reg("hpexpireat", -6, "write fast", 1, 1, 1,
                snap(c -> hexpireGeneric(c, 0, false)));
        CommandTable.reg("httl", -5, "readonly fast", 1, 1, 1,
                snap(c -> httlGeneric(c, now(), true)));
        CommandTable.reg("hpttl", -5, "readonly fast", 1, 1, 1,
                snap(c -> httlGeneric(c, now(), false)));
        CommandTable.reg("hexpiretime", -5, "readonly fast", 1, 1, 1,
                snap(c -> httlGeneric(c, 0, true)));
        CommandTable.reg("hpexpiretime", -5, "readonly fast", 1, 1, 1,
                snap(c -> httlGeneric(c, 0, false)));
        CommandTable.reg("hpersist", -5, "write fast", 1, 1, 1, snap(HashCmds::hpersist));
    }

    // ------------------------------------------------------------------
    // HSET key field value [field value ...]   /   HMSET
    // ------------------------------------------------------------------
    static void hset(Client c) {
        Server s = S();
        if ((c.argc % 2) == 1) throw RedisError.wrongArgs(c.cmdName());

        RObj o = lookupWriteOrCreate(c, c.arg(1));
        int created = 0;
        for (int i = 2; i < c.argc; i += 2)
            if (!hashSet(o, c.arg(i), c.arg(i + 1), false)) created++;

        /* HMSET (deprecated) and HSET return values differ. */
        if (c.cmdName().equals("hmset")) c.addOk(); else c.addInt(created);

        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        notify('h', "hset", c.arg(1), c.db);
        s.dirty += (c.argc - 2) / 2;
    }

    // ------------------------------------------------------------------
    // HSETNX key field value
    // ------------------------------------------------------------------
    static void hsetnx(Client c) {
        Server s = S();
        RObj o = lookupWriteOrCreate(c, c.arg(1));

        int res = hashGetValue(c, c.arg(1), o, c.arg(2), 0, null);
        if (res == GETF_OK) { c.addInt(0); return; }
        /* The field expired and the hash went with it: create a new one. */
        if (res == GETF_EXPIRED_HASH) { o = createHash(); s.dbAdd(c.db, c.arg(1), o); }

        hashSet(o, c.arg(2), c.arg(3), false);
        c.addInt(1);
        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        notify('h', "hset", c.arg(1), c.db);
        s.dirty++;
    }

    // ------------------------------------------------------------------
    // HGET key field
    // ------------------------------------------------------------------
    static void hget(Client c) {
        RObj o = lookupHashRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        addHashFieldToReply(c, c.arg(1), o, c.arg(2), 0);
    }

    // ------------------------------------------------------------------
    // HMGET key field [field ...]
    // ------------------------------------------------------------------
    static void hmget(Client c) {
        RObj o = lookupHashRead(c, c.arg(1));
        int expired = 0;
        boolean deleted = false;

        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) {
            if (!deleted) {
                int res = addHashFieldToReply(c, c.arg(1), o, c.arg(i), LAZY_NO_NOTIFICATION);
                if (res == GETF_EXPIRED) expired++;
                if (res == GETF_EXPIRED_HASH) { expired++; deleted = true; }
            } else {
                /* The hash was lazily removed: fill the rest with nulls. */
                c.addNil();
            }
        }
        if (expired > 0) {
            notify('h', "hexpired", c.arg(1), c.db);
            if (deleted) notify('g', "del", c.arg(1), c.db);
        }
    }

    // ------------------------------------------------------------------
    // HDEL key field [field ...]
    // ------------------------------------------------------------------
    static void hdel(Client c) {
        Server s = S();
        RObj o = lookupHashWrite(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        RHash h = hash(o);

        int deleted = 0;
        boolean keyRemoved = false;
        for (int j = 2; j < c.argc; j++) {
            Key fk = new Key(c.arg(j));
            if (h.map.remove(fk) != null) {
                h.clearTtl(fk);
                deleted++;
                if (h.size() == 0) { s.dbDelete(c.db, c.arg(1)); keyRemoved = true; break; }
            }
        }
        if (deleted > 0) {
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            notify('h', "hdel", c.arg(1), c.db);
            if (keyRemoved) notify('g', "del", c.arg(1), c.db);
            s.dirty += deleted;
        }
        c.addInt(deleted);
    }

    // ------------------------------------------------------------------
    // HLEN key
    // ------------------------------------------------------------------
    static void hlen(Client c) {
        RObj o = lookupHashRead(c, c.arg(1));
        c.addInt(o == null ? 0 : hash(o).size());
    }

    // ------------------------------------------------------------------
    // HSTRLEN key field
    // ------------------------------------------------------------------
    static void hstrlen(Client c) {
        RObj o = lookupHashRead(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        byte[][] out = new byte[1][];
        int res = hashGetValue(c, c.arg(1), o, c.arg(2), 0, out);
        c.addInt(res == GETF_OK ? out[0].length : 0);
    }

    // ------------------------------------------------------------------
    // HKEYS / HVALS / HGETALL
    // ------------------------------------------------------------------
    static void hkeys(Client c) { genericHgetall(c, true, false); }

    static void hvals(Client c) { genericHgetall(c, false, true); }

    static void hgetall(Client c) { genericHgetall(c, true, true); }

    private static void genericHgetall(Client c, boolean wantKeys, boolean wantVals) {
        boolean both = wantKeys && wantVals;
        RObj o = lookupHashRead(c, c.arg(1));
        if (o == null) { if (both) c.addMap(0); else c.addEmptyArray(); return; }

        /* One clock reading for both the announced length and the body. */
        long now = now();
        int length = liveLength(o, now);
        if (length == 0) { if (both) c.addMap(0); else c.addEmptyArray(); return; }

        /* A map when both fields and values were asked for, a flat array otherwise. */
        if (both) c.addMap(length); else c.addArray(length);

        RHash h = hash(o);
        for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
            if (isExpired(h, e.getKey(), now)) continue;
            if (wantKeys) c.addBulk(e.getKey().b);
            if (wantVals) c.addBulk(e.getValue());
        }
    }

    // ------------------------------------------------------------------
    // HEXISTS key field
    // ------------------------------------------------------------------
    static void hexists(Client c) {
        RObj o = lookupHashRead(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        c.addInt(hashGetValue(c, c.arg(1), o, c.arg(2), 0, null) == GETF_OK ? 1 : 0);
    }

    // ------------------------------------------------------------------
    // HINCRBY key field increment
    // ------------------------------------------------------------------
    static void hincrby(Client c) {
        Server s = S();
        Long incrArg = Util.string2ll(c.arg(3));
        if (incrArg == null) throw RedisError.notInt();
        long incr = incrArg;

        RObj o = lookupWriteOrCreate(c, c.arg(1));
        byte[][] out = new byte[1][];
        int res = hashGetValue(c, c.arg(1), o, c.arg(2), 0, out);
        long value;
        if (res == GETF_OK) {
            Long parsed = Util.string2ll(out[0]);
            if (parsed == null) throw new RedisError("ERR hash value is not an integer");
            value = parsed;
        } else if (res == GETF_NOT_FOUND || res == GETF_EXPIRED) {
            value = 0;
        } else {
            /* Field expired and in turn the hash was deleted. Create a new one! */
            o = createHash();
            s.dbAdd(c.db, c.arg(1), o);
            value = 0;
        }

        long oldvalue = value;
        if ((incr < 0 && oldvalue < 0 && incr < (Long.MIN_VALUE - oldvalue))
                || (incr > 0 && oldvalue > 0 && incr > (Long.MAX_VALUE - oldvalue)))
            throw new RedisError("ERR increment or decrement would overflow");
        value += incr;

        hashSet(o, c.arg(2), Util.ll2string(value), true);
        c.addInt(value);
        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        notify('h', "hincrby", c.arg(1), c.db);
        s.dirty++;
    }

    // ------------------------------------------------------------------
    // HINCRBYFLOAT key field increment
    // ------------------------------------------------------------------
    static void hincrbyfloat(Client c) {
        Server s = S();
        Double incrArg = string2ld(c.arg(3));
        if (incrArg == null) throw RedisError.notFloat();
        double incr = incrArg;
        if (Double.isNaN(incr) || Double.isInfinite(incr))
            throw new RedisError("ERR value is NaN or Infinity");

        RObj o = lookupWriteOrCreate(c, c.arg(1));
        byte[][] out = new byte[1][];
        int res = hashGetValue(c, c.arg(1), o, c.arg(2), 0, out);
        double value;
        if (res == GETF_OK) {
            Double parsed = string2ld(out[0]);
            if (parsed == null) throw new RedisError("ERR hash value is not a float");
            value = parsed;
        } else if (res == GETF_NOT_FOUND || res == GETF_EXPIRED) {
            value = 0;
        } else {
            o = createHash();
            s.dbAdd(c.db, c.arg(1), o);
            value = 0;
        }

        value += incr;
        if (Double.isNaN(value) || Double.isInfinite(value))
            throw new RedisError("ERR increment would produce NaN or Infinity");

        byte[] newval = Util.b(Util.ld2str(value));
        hashSet(o, c.arg(2), newval, true);
        c.addBulk(newval);
        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        notify('h', "hincrbyfloat", c.arg(1), c.db);
        s.dirty++;

        /* Always replicate with the final value so that float formatting
         * differences cannot diverge a replica or an AOF reload. The field TTL
         * is preserved by re-applying it after the HSET. */
        byte[][] set = new byte[][]{HSET_CMD, c.arg(1), c.arg(2), newval};
        Long ttl = hash(o).ttl(new Key(c.arg(2)));
        if (ttl == null) {
            s.replOverride = set;
        } else {
            s.propagateExtra(c.db, set);
            s.replOverride = new byte[][]{HPEXPIREAT_CMD, c.arg(1), Util.ll2string(ttl),
                    FIELDS_ARG, Util.b("1"), c.arg(2)};
        }
    }

    // ------------------------------------------------------------------
    // HRANDFIELD key [count [WITHVALUES]]
    // ------------------------------------------------------------------
    static void hrandfield(Client c) {
        if (c.argc >= 3) {
            long l = rangeLong(c, 2, -Long.MAX_VALUE, Long.MAX_VALUE);
            boolean withvalues = false;
            if (c.argc > 4 || (c.argc == 4 && !c.argIs(3, "withvalues"))) throw RedisError.syntax();
            if (c.argc == 4) {
                withvalues = true;
                if (l < -Long.MAX_VALUE / 2 || l > Long.MAX_VALUE / 2)
                    throw new RedisError("ERR value is out of range");
            }
            hrandfieldWithCount(c, l, withvalues);
            return;
        }

        RObj o = lookupHashRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        /* Delete all expired fields. If the whole hash went away, reply null. */
        if (expireIfNeeded(c, c.arg(1), o)) { c.addNil(); return; }
        List<Key> fields = new ArrayList<>(hash(o).map.keySet());
        c.addBulk(fields.get(RND.nextInt(fields.size())).b);
    }

    private static void hrandfieldWithCount(Client c, long l, boolean withvalues) {
        RObj o = lookupHashRead(c, c.arg(1));
        if (o == null) { c.addEmptyArray(); return; }

        boolean uniq = l >= 0;
        long lcount = uniq ? l : -l;

        /* Delete all expired fields. If the whole hash went away, reply empty. */
        if (expireIfNeeded(c, c.arg(1), o)) { c.addEmptyArray(); return; }

        RHash h = hash(o);
        int size = h.size();
        if (lcount == 0 || size == 0) { c.addEmptyArray(); return; }

        List<Key> fields = new ArrayList<>(h.map.keySet());

        /* CASE 1: a negative count (or a single element): sample with repetition. */
        if (!uniq || lcount == 1) {
            int count = (int) Math.min(lcount, Integer.MAX_VALUE);
            replyLen(c, count, withvalues);
            for (int i = 0; i < count; i++) emit(c, h, fields.get(RND.nextInt(size)), withvalues);
            return;
        }

        int replySize = (int) Math.min(lcount, size);
        replyLen(c, replySize, withvalues);

        /* CASE 2: at least the whole hash was requested: return all of it. */
        if (lcount >= size) {
            for (Key k : fields) emit(c, h, k, withvalues);
            return;
        }

        /* CASE 3/4: a distinct random subset. */
        for (int i = 0; i < replySize; i++) {
            int j = i + RND.nextInt(fields.size() - i);
            Key tmp = fields.get(i); fields.set(i, fields.get(j)); fields.set(j, tmp);
            emit(c, h, fields.get(i), withvalues);
        }
    }

    /** RESP2 flattens WITHVALUES into a 2n array, RESP3 nests 2-element arrays. */
    private static void replyLen(Client c, int count, boolean withvalues) {
        if (withvalues && c.resp == 2) c.addArray(count * 2);
        else c.addArray(count);
    }

    private static void emit(Client c, RHash h, Key field, boolean withvalues) {
        if (withvalues && c.resp > 2) c.addArray(2);
        c.addBulk(field.b);
        if (withvalues) c.addBulk(h.map.get(field));
    }

    // ------------------------------------------------------------------
    // HGETDEL key FIELDS numfields field [field ...]
    // ------------------------------------------------------------------
    static void hgetdel(Client c) {
        Server s = S();
        long dirty0 = s.dirty;
        RObj o = lookupHashWrite(c, c.arg(1));

        if (!c.argIs(2, "FIELDS")) throw new RedisError(ERR_FIELDS);
        long numFields = rangeLong(c, 3, 1, Long.MAX_VALUE, MSG_NUMFIELDS_POSITIVE);
        if (numFields != c.argc - 4) throw new RedisError(ERR_NUMFIELDS_MATCH);

        int expired = 0, deleted = 0;
        final int flags = LAZY_NO_NOTIFICATION | LAZY_NO_SIGNAL | LAZY_AVOID_HASH_DEL;

        c.addArray((int) numFields);
        for (int i = 4; i < c.argc; i++) {
            int res = addHashFieldToReply(c, c.arg(1), o, c.arg(i), flags);
            if (res == GETF_EXPIRED) expired++;
            if (res == GETF_OK) {
                deleted++;
                Key fk = new Key(c.arg(i));
                hash(o).map.remove(fk);
                hash(o).clearTtl(fk);
            }
        }

        /* Return if no modification has been made. */
        if (expired == 0 && deleted == 0) return;

        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        if (expired > 0) notify('h', "hexpired", c.arg(1), c.db);
        if (deleted > 0) {
            notify('h', "hdel", c.arg(1), c.db);
            s.dirty += deleted;
            /* Propagate as HDEL <key> field1 field2 ... */
            byte[][] prop = new byte[2 + (c.argc - 4)][];
            prop[0] = HDEL_CMD;
            prop[1] = c.arg(1);
            for (int i = 4; i < c.argc; i++) prop[i - 2] = c.arg(i);
            s.replOverride = prop;
        }

        /* The key may have become empty through deletion or lazy expiry. */
        if (hash(o).size() == 0) {
            s.dbDelete(c.db, c.arg(1));
            notify('g', "del", c.arg(1), c.db);
        }
        if (s.dirty == dirty0) s.replOverride = null;
    }

    // ------------------------------------------------------------------
    // HGETEX key [EX s|PX ms|EXAT ts|PXAT ts|PERSIST] FIELDS numfields field ...
    // ------------------------------------------------------------------
    static void hgetex(Client c) {
        Server s = S();
        long dirty0 = s.dirty;
        RObj o = lookupHashWrite(c, c.arg(1));

        int cond = 0;
        int numFieldsPos = 3;
        if (c.argIs(2, "ex")) cond = HFE_EX;
        else if (c.argIs(2, "px")) cond = HFE_PX;
        else if (c.argIs(2, "exat")) cond = HFE_EXAT;
        else if (c.argIs(2, "pxat")) cond = HFE_PXAT;
        else if (c.argIs(2, "persist")) cond = HFE_PERSIST;

        long expireTime = 0;
        if ((cond & (HFE_EX | HFE_PX | HFE_EXAT | HFE_PXAT)) != 0) {
            numFieldsPos += 2;
            boolean seconds = (cond & (HFE_EX | HFE_EXAT)) != 0;
            long basetime = (cond & (HFE_EX | HFE_PX)) != 0 ? now() : 0;
            expireTime = parseExpireTime(c, 3, seconds, basetime);
        } else if ((cond & HFE_PERSIST) != 0) {
            numFieldsPos += 1;
        }

        if (numFieldsPos >= c.argc) throw RedisError.wrongArgs(c.cmdName());
        if (!c.argIs(numFieldsPos - 1, "FIELDS")) throw new RedisError(ERR_FIELDS);
        long numFields = rangeLong(c, numFieldsPos, 1, Long.MAX_VALUE, MSG_NUMFIELDS_POSITIVE);
        if (numFields != c.argc - numFieldsPos - 1) throw new RedisError(ERR_NUMFIELDS_MATCH);

        /* Non-existing keys and empty hashes are the same thing. */
        if (o == null) {
            c.addArray((int) numFields);
            for (int i = 0; i < numFields; i++) c.addNil();
            return;
        }

        if (cond != 0) enableHfe(c, c.arg(1), o);

        int expired = 0, deleted = 0, updated = 0;
        final int flags = LAZY_NO_NOTIFICATION | LAZY_NO_SIGNAL | LAZY_AVOID_HASH_DEL;

        c.addArray((int) numFields);
        for (int i = numFieldsPos + 1; i < c.argc; i++) {
            int res = addHashFieldToReply(c, c.arg(1), o, c.arg(i), flags);
            if (res == GETF_EXPIRED) expired++;
            if (res == GETF_OK && cond != 0) {
                int sres = hashSetEx(c, c.arg(1), o, c.arg(i), expireTime, 0,
                        (cond & HFE_PERSIST) != 0);
                if (sres == HSETEX_DELETED) deleted++;
                if (sres == HSETEX_OK) updated++;
            }
        }

        /* Exit early if no modification has been made. */
        if (expired == 0 && deleted == 0 && updated == 0) return;

        s.dirty += deleted + updated;
        s.signalModifiedKey(c.db, new Key(c.arg(1)));

        if (expired > 0) notify('h', "hexpired", c.arg(1), c.db);
        if (updated > 0) {
            if ((cond & HFE_PERSIST) != 0) {
                notify('h', "hpersist", c.arg(1), c.db);
                /* Propagate as HPERSIST <key> FIELDS <numfields> field ... */
                byte[][] prop = new byte[3 + (c.argc - numFieldsPos)][];
                prop[0] = HPERSIST_CMD;
                prop[1] = c.arg(1);
                prop[2] = FIELDS_ARG;
                for (int i = numFieldsPos; i < c.argc; i++) prop[i - numFieldsPos + 3] = c.arg(i);
                s.replOverride = prop;
            } else {
                notify('h', "hexpire", c.arg(1), c.db);
                /* Propagate as HPEXPIREAT <key> <ms> FIELDS <numfields> field ... */
                byte[][] prop = new byte[4 + (c.argc - numFieldsPos)][];
                prop[0] = HPEXPIREAT_CMD;
                prop[1] = c.arg(1);
                prop[2] = Util.ll2string(expireTime);
                prop[3] = FIELDS_ARG;
                for (int i = numFieldsPos; i < c.argc; i++) prop[i - numFieldsPos + 4] = c.arg(i);
                s.replOverride = prop;
            }
        } else if (deleted > 0) {
            /* The HDELs were already propagated by hashSetEx(). */
            notify('h', "hdel", c.arg(1), c.db);
            s.replOverride = NO_PROPAGATION;
        }

        /* The key may have become empty through lazy expiry or a past timestamp. */
        if (hash(o).size() == 0) {
            s.dbDelete(c.db, c.arg(1));
            notify('g', "del", c.arg(1), c.db);
        }
        if (s.dirty == dirty0) s.replOverride = null;
    }

    // ------------------------------------------------------------------
    // HSETEX key [FNX | FXX] [EX s | PX ms | EXAT ts | PXAT ts | KEEPTTL]
    //        FIELDS numfields field value [field value ...]
    //
    // t_hash.c hsetexCommand(): sets every field/value pair and, in the same
    // atomic step, replaces their TTL.  Returns 1 when the whole write happened
    // and 0 when FNX/FXX stopped it; in the 0 case nothing at all is written.
    // Without an expiration argument the fields lose any TTL they had; KEEPTTL
    // keeps it; a timestamp in the past deletes the fields (and, once the hash
    // is empty, the key) and still reports 1.
    // ------------------------------------------------------------------
    static void hsetex(Client c) {
        Server s = S();
        byte[] key = c.arg(1);

        int cond = 0, ttlOpt = 0;
        long expireAt = 0;
        int i = 2;
        while (i < c.argc && !c.argIs(i, "fields")) {
            if (c.argIs(i, "fnx") || c.argIs(i, "fxx")) {
                if (cond != 0)
                    throw new RedisError("ERR Only one of FXX or FNX arguments can be specified");
                cond = c.argIs(i, "fnx") ? HSETEX_FNX : HSETEX_FXX;
                i++;
            } else if (c.argIs(i, "keepttl")) {
                if (ttlOpt != 0) throw new RedisError(ERR_HSETEX_ONE_TTL);
                ttlOpt = HFE_KEEPTTL;
                i++;
            } else if (c.argIs(i, "ex") || c.argIs(i, "px")
                    || c.argIs(i, "exat") || c.argIs(i, "pxat")) {
                if (ttlOpt != 0) throw new RedisError(ERR_HSETEX_ONE_TTL);
                boolean seconds = c.argIs(i, "ex") || c.argIs(i, "exat");
                boolean relative = c.argIs(i, "ex") || c.argIs(i, "px");
                ttlOpt = seconds ? (relative ? HFE_EX : HFE_EXAT)
                                 : (relative ? HFE_PX : HFE_PXAT);
                if (i + 1 >= c.argc) throw RedisError.wrongArgs(c.cmdName());
                /* Parsed here, before FIELDS is even looked for: a bad expire
                 * time is reported as such and not as a missing FIELDS. */
                expireAt = parseExpireTime(c, i + 1, seconds, relative ? now() : 0);
                i += 2;
            } else {
                String bad = Server.trunc(Util.s(c.arg(i)), 128)
                        .replace('\r', ' ').replace('\n', ' ');
                throw new RedisError("ERR unknown argument: " + bad);
            }
        }
        if (i >= c.argc) throw new RedisError(ERR_FIELDS);
        if (i + 1 >= c.argc) throw RedisError.wrongArgs(c.cmdName());
        long numFields = rangeLong(c, i + 1, 1, Long.MAX_VALUE, MSG_NUMFIELDS_INVALID);
        final int first = i + 2;
        final int rest = c.argc - first;
        /* numfields counts PAIRS, so the tail must be exactly twice as long. */
        if ((rest & 1) != 0 || numFields != rest / 2) throw RedisError.wrongArgs(c.cmdName());

        RObj o = lookupHashWrite(c, key);
        /* hashTypeSetExInit() expires the hash first, so FNX/FXX and the write
         * itself see only fields that are still alive. */
        if (o != null && expireIfNeeded(c, key, o)) o = null;
        if (o != null && hash(o).size() == 0) o = null;

        if (cond != 0) {
            boolean met = true;
            for (int j = first; j < c.argc && met; j += 2) {
                boolean exists = o != null && hash(o).map.containsKey(new Key(c.arg(j)));
                if (cond == HSETEX_FXX ? !exists : exists) met = false;
            }
            if (!met) { c.addInt(0); return; }
        }

        if (o == null) { o = createHash(); s.dbAdd(c.db, key, o); }
        RHash h = hash(o);
        boolean keepTtl = (ttlOpt == HFE_KEEPTTL);
        boolean setTtl = (ttlOpt != 0 && !keepTtl);
        if (setTtl) enableHfe(c, key, o);

        int deleted = 0;
        for (int j = first; j < c.argc; j += 2) {
            byte[] f = c.arg(j);
            /* hashTypeSet(): without KEEPTTL an overwritten field loses its TTL. */
            hashSet(o, f, c.arg(j + 1), keepTtl);
            if (setTtl) {
                Key fk = new Key(f);
                if (expireAt <= now()) { h.map.remove(fk); h.clearTtl(fk); deleted++; }
                else h.setTtl(fk, expireAt);
            }
        }

        c.addInt(1);
        s.dirty += (int) numFields;
        s.signalModifiedKey(c.db, new Key(key));
        notify('h', "hset", key, c.db);
        if (setTtl) notify('h', deleted > 0 ? "hdel" : "hexpire", key, c.db);
        if (h.size() == 0) {
            s.dbDelete(c.db, key);
            notify('g', "del", key, c.db);
        }

        /* Propagate with an absolute PXAT so a relative EX/PX cannot drift on a
         * replica that applies it later; the condition was already decided here
         * and is not replayed. */
        List<byte[]> prop = new ArrayList<>();
        prop.add(HSETEX_CMD);
        prop.add(key);
        if (keepTtl) prop.add(KEEPTTL_ARG);
        else if (setTtl) { prop.add(PXAT_ARG); prop.add(Util.ll2string(expireAt)); }
        prop.add(FIELDS_ARG);
        prop.add(Util.ll2string(numFields));
        for (int j = first; j < c.argc; j++) prop.add(c.arg(j));
        s.replOverride = prop.toArray(new byte[0][]);
    }

    // ------------------------------------------------------------------
    // hashTypeSetEx()
    // ------------------------------------------------------------------
    private static int hashSetEx(Client c, byte[] key, RObj o, byte[] field,
                                 long expireAt, int setCond, boolean persist) {
        Server s = S();
        RHash h = hash(o);
        Key fk = new Key(field);
        if (!h.map.containsKey(fk)) return HSETEX_NO_FIELD;

        Long prev = h.ttl(fk);
        /* Persisting a field that has no TTL is "condition not met". */
        if (persist) {
            if (prev == null) return HSETEX_NO_CONDITION_MET;
            h.clearTtl(fk);
            return HSETEX_OK;
        }

        if (prev == null) {
            /* For fields without expiry, the LT condition is considered valid. */
            if ((setCond & (HFE_XX | HFE_GT)) != 0) return HSETEX_NO_CONDITION_MET;
        } else {
            if ((setCond == HFE_GT && prev >= expireAt)
                    || (setCond == HFE_LT && prev <= expireAt)
                    || setCond == HFE_NX)
                return HSETEX_NO_CONDITION_MET;
        }

        /* If the time is in the past, delete the field and propagate the deletion. */
        if (expireAt <= now()) {
            s.propagateExtra(c.db, new byte[][]{HDEL_CMD, key, field});
            h.map.remove(fk);
            h.clearTtl(fk);
            return HSETEX_DELETED;
        }

        h.setTtl(fk, expireAt);
        return HSETEX_OK;
    }

    // ------------------------------------------------------------------
    // HEXPIRE / HPEXPIRE / HEXPIREAT / HPEXPIREAT
    // ------------------------------------------------------------------
    private static void hexpireGeneric(Client c, long basetime, boolean seconds) {
        Server s = S();
        long dirty0 = s.dirty;
        RObj o = lookupHashWrite(c, c.arg(1));

        long expire = parseExpireTime(c, 2, seconds, basetime);

        int setCond = 0;
        int numFieldsAt = 4;
        if (c.argIs(3, "nx")) { setCond = HFE_NX; numFieldsAt++; }
        else if (c.argIs(3, "xx")) { setCond = HFE_XX; numFieldsAt++; }
        else if (c.argIs(3, "gt")) { setCond = HFE_GT; numFieldsAt++; }
        else if (c.argIs(3, "lt")) { setCond = HFE_LT; numFieldsAt++; }

        if (numFieldsAt - 1 >= c.argc || !c.argIs(numFieldsAt - 1, "FIELDS"))
            throw new RedisError(ERR_FIELDS);
        if (numFieldsAt >= c.argc) throw RedisError.wrongArgs(c.cmdName());
        long numFields = rangeLong(c, numFieldsAt, 1, Long.MAX_VALUE, MSG_NUMFIELDS_GT0);
        if (numFields != c.argc - numFieldsAt - 1) throw new RedisError(ERR_NUMFIELDS_MATCH);

        /* Non-existing keys and empty hashes are the same thing. */
        if (o == null) {
            c.addArray((int) numFields);
            for (int i = 0; i < numFields; i++) c.addInt(HSETEX_NO_FIELD);
            return;
        }

        enableHfe(c, c.arg(1), o);

        int updated = 0, deleted = 0;
        List<byte[]> okFields = new ArrayList<>();
        c.addArray((int) numFields);
        for (int fieldAt = numFieldsAt + 1; fieldAt < c.argc; fieldAt++) {
            int res = hashSetEx(c, c.arg(1), o, c.arg(fieldAt), expire, setCond, false);
            if (res == HSETEX_OK) { updated++; okFields.add(c.arg(fieldAt)); }
            if (res == HSETEX_DELETED) deleted++;
            c.addInt(res);
        }

        if (deleted + updated > 0) {
            s.dirty += deleted + updated;
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            notify('h', deleted > 0 ? "hdel" : "hexpire", c.arg(1), c.db);
        }

        if (hash(o).size() == 0) {
            s.dbDelete(c.db, c.arg(1));
            notify('g', "del", c.arg(1), c.db);
        }

        /* Nothing was updated: the HDELs (if any) were already propagated. */
        if (updated == 0) {
            if (s.dirty != dirty0) s.replOverride = NO_PROPAGATION;
            return;
        }

        /* Propagate as HPEXPIREAT <key> <ms> [NX|XX|GT|LT] FIELDS <n> <fields...> */
        List<byte[]> prop = new ArrayList<>();
        prop.add(HPEXPIREAT_CMD);
        prop.add(c.arg(1));
        prop.add(Util.ll2string(expire));
        if (numFieldsAt == 5) prop.add(c.arg(3));
        prop.add(FIELDS_ARG);
        prop.add(Util.ll2string(updated));
        prop.addAll(okFields);
        s.replOverride = prop.toArray(new byte[0][]);
    }

    // ------------------------------------------------------------------
    // HTTL / HPTTL / HEXPIRETIME / HPEXPIRETIME
    // ------------------------------------------------------------------
    private static void httlGeneric(Client c, long basetime, boolean seconds) {
        RObj o = lookupHashRead(c, c.arg(1));

        if (!c.argIs(2, "FIELDS")) throw new RedisError(ERR_FIELDS);
        long numFields = rangeLong(c, 3, 1, Long.MAX_VALUE, MSG_NUMFIELDS_POSITIVE);
        if (numFields != c.argc - 4) throw new RedisError(ERR_NUMFIELDS_MATCH);

        if (o == null) {
            c.addArray((int) numFields);
            for (int i = 0; i < numFields; i++) c.addInt(HFE_GET_NO_FIELD);
            return;
        }

        RHash h = hash(o);
        long now = now();
        c.addArray((int) numFields);
        for (int i = 4; i < c.argc; i++) {
            Key fk = new Key(c.arg(i));
            if (!h.map.containsKey(fk)) { c.addInt(HFE_GET_NO_FIELD); continue; }
            Long when = h.ttl(fk);
            if (when == null) { c.addInt(HFE_GET_NO_TTL); continue; }
            if (when <= now) { c.addInt(HFE_GET_NO_FIELD); continue; }
            if (seconds) c.addInt((when + 999 - basetime) / 1000);
            else c.addInt(when - basetime);
        }
    }

    // ------------------------------------------------------------------
    // HPERSIST key FIELDS numfields field [field ...]
    // ------------------------------------------------------------------
    static void hpersist(Client c) {
        Server s = S();
        RObj o = lookupHashWrite(c, c.arg(1));

        if (!c.argIs(2, "FIELDS")) throw new RedisError(ERR_FIELDS);
        long numFields = rangeLong(c, 3, 1, Long.MAX_VALUE, MSG_NUMFIELDS_POSITIVE);
        if (numFields != c.argc - 4) throw new RedisError(ERR_NUMFIELDS_MATCH);

        if (o == null) {
            c.addArray((int) numFields);
            for (int i = 0; i < numFields; i++) c.addInt(HFE_PERSIST_NO_FIELD);
            return;
        }

        RHash h = hash(o);
        long now = now();
        boolean changed = false;
        c.addArray((int) numFields);
        for (int i = 4; i < c.argc; i++) {
            Key fk = new Key(c.arg(i));
            if (!h.map.containsKey(fk)) { c.addInt(HFE_PERSIST_NO_FIELD); continue; }
            Long when = h.ttl(fk);
            if (when == null) { c.addInt(HFE_PERSIST_NO_TTL); continue; }
            /* Already expired: pretend there is no such field. */
            if (when <= now) { c.addInt(HFE_PERSIST_NO_FIELD); continue; }
            h.clearTtl(fk);
            c.addInt(HFE_PERSIST_OK);
            changed = true;
        }

        if (changed) {
            notify('h', "hpersist", c.arg(1), c.db);
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            s.dirty++;
        }
    }
}
