package redis;

import java.util.*;

/**
 * Set type commands: a port of redis 8.0 src/t_set.c.
 *
 * SADD SREM SMOVE SISMEMBER SMISMEMBER SCARD SPOP SRANDMEMBER SINTER
 * SINTERCARD SINTERSTORE SUNION SUNIONSTORE SDIFF SDIFFSTORE SMEMBERS
 */
public final class SetCmds {
    private SetCmds() {}

    private static final Random RND = new Random();

    private static final int SET_OP_UNION = 0, SET_OP_DIFF = 1;

    /** SPOP replicates as SREM batches of at most this many members. */
    private static final int SPOP_PROP_BATCH = 1024;

    // ------------------------------------------------------------------
    // small helpers
    // ------------------------------------------------------------------
    private static Server S() { return Server.I; }

    private static long cfg(String name, long dflt) {
        String v = Server.I.config.get(name);
        if (v == null || v.isEmpty()) return dflt;
        return Config.parseMemory(v);
    }

    private static long maxIntsetEntries() {
        long m = cfg("set-max-intset-entries", 512);
        if (m >= (1 << 30)) m = 1 << 30;
        return m;
    }

    /* Redis 2.8 sets are either an intset or a hash table: there is no compact
     * listpack encoding for sets before 7.2, so the listpack thresholds are
     * always zero here and setTypeAdd() converts straight to a hash table. */
    private static long maxListpackEntries() { return 0; }

    private static long maxListpackValue() { return 0; }

    private static RSet set(RObj o) { return (RSet) o.ptr; }

    /** setTypeCreate(): pick the initial encoding from the first value and a size hint. */
    private static RObj createSet(byte[] value, long sizeHint) {
        if (value != null && Util.string2ll(value) != null && sizeHint <= maxIntsetEntries())
            return new RObj(RObj.SET, RObj.ENC_INTSET, new RSet());
        if (sizeHint <= maxListpackEntries())
            return new RObj(RObj.SET, RObj.ENC_LISTPACK, new RSet());
        return new RObj(RObj.SET, RObj.ENC_HT, new RSet());
    }

    private static RObj newSetObject(int encoding) {
        return new RObj(RObj.SET, encoding, new RSet());
    }

    /** setTypeMaybeConvert(): widen the encoding up front when the size hint is big. */
    private static void maybeConvert(RObj o, long sizeHint) {
        if ((o.encoding == RObj.ENC_LISTPACK && sizeHint > maxListpackEntries())
                || (o.encoding == RObj.ENC_INTSET && sizeHint > maxIntsetEntries()))
            o.encoding = RObj.ENC_HT;
    }

    /** Longest member currently stored, ignoring one member. */
    private static int maxMemberLen(RSet s, byte[] skip) {
        int max = 0;
        for (Key k : s.set) {
            if (skip != null && Arrays.equals(k.b, skip)) continue;
            if (k.b.length > max) max = k.b.length;
        }
        return max;
    }

    /** setTypeAdd(): add a member and maintain the object encoding. */
    private static boolean setAdd(RObj o, byte[] v) {
        RSet s = set(o);
        if (!s.add(v)) return false;
        int n = s.size();                 // size after the insert
        if (o.encoding == RObj.ENC_INTSET) {
            if (Util.string2ll(v) == null) {
                /* Not integer encodable: intset -> listpack or hashtable. */
                if (n - 1 < maxListpackEntries() && v.length <= maxListpackValue()
                        && maxMemberLen(s, v) <= maxListpackValue())
                    o.encoding = RObj.ENC_LISTPACK;
                else
                    o.encoding = RObj.ENC_HT;
            } else if (n > maxIntsetEntries()) {
                o.encoding = RObj.ENC_HT;
            }
        } else if (o.encoding == RObj.ENC_LISTPACK) {
            if (!(n - 1 < maxListpackEntries() && v.length <= maxListpackValue()))
                o.encoding = RObj.ENC_HT;
        }
        return true;
    }

    /** maybeConvertToIntset(): a result holding only integers is stored as an intset. */
    private static void maybeConvertToIntset(RObj o) {
        if (o.encoding == RObj.ENC_INTSET) return;
        RSet s = set(o);
        if (s.size() > maxIntsetEntries()) return;
        for (Key k : s.set) if (Util.string2ll(k.b) == null) return;
        o.encoding = RObj.ENC_INTSET;
    }

    /** Members in iteration order; an intset iterates in ascending numeric order. */
    private static List<byte[]> members(RObj o) {
        List<byte[]> out = set(o).members();
        if (o.encoding == RObj.ENC_INTSET) {
            out.sort((a, b) -> {
                Long x = Util.string2ll(a), y = Util.string2ll(b);
                if (x == null || y == null) return 0;
                return Long.compare(x, y);
            });
        }
        return out;
    }

    private static RObj lookupSetRead(Client c, byte[] key) {
        RObj o = S().lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != RObj.SET) throw RedisError.wrongType();
        return o;
    }

    private static RObj lookupSetWrite(Client c, byte[] key) {
        RObj o = S().lookupKeyWrite(c, key);
        if (o == null) return null;
        if (o.type != RObj.SET) throw RedisError.wrongType();
        return o;
    }

    /** getRangeLongFromObjectOrReply() with a NULL message. */
    private static long rangeLong(Client c, int idx, long min, long max) {
        Long v = Util.string2ll(c.arg(idx));
        if (v == null) throw RedisError.notInt();
        if (v < min || v > max)
            throw new RedisError("ERR value is out of range, value must between " + min + " and " + max);
        return v;
    }

    /** getRangeLongFromObjectOrReply() with a custom message (used for both failures). */
    private static long rangeLong(Client c, int idx, long min, long max, String msg) {
        Long v = Util.string2ll(c.arg(idx));
        if (v == null) throw new RedisError("ERR " + msg);
        if (v < min || v > max) throw new RedisError("ERR " + msg);
        return v;
    }

    private static void notify(char cls, String event, byte[] key, int db) {
        S().notifyKeyspaceEvent(cls, event, key, db);
    }

    /** Drop an emptied key and fire the generic `del` notification. */
    private static void deleteEmpty(Client c, byte[] key) {
        S().dbDelete(c.db, key);
        notify('g', "del", key, c.db);
    }

    // ------------------------------------------------------------------
    // registration
    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("sadd", -3, "write denyoom fast", 1, 1, 1, SetCmds::sadd);
        CommandTable.reg("srem", -3, "write fast", 1, 1, 1, SetCmds::srem);
        CommandTable.reg("smove", 4, "write fast", 1, 2, 1, SetCmds::smove);
        CommandTable.reg("sismember", 3, "readonly fast", 1, 1, 1, SetCmds::sismember);
        CommandTable.reg("smismember", -3, "readonly fast", 1, 1, 1, SetCmds::smismember);
        CommandTable.reg("scard", 2, "readonly fast", 1, 1, 1, SetCmds::scard);
        CommandTable.reg("spop", -2, "write fast", 1, 1, 1, SetCmds::spop);
        CommandTable.reg("srandmember", -2, "readonly", 1, 1, 1, SetCmds::srandmember);
        CommandTable.reg("sinter", -2, "readonly", 1, -1, 1, SetCmds::sinter);
        CommandTable.reg("sintercard", -3, "readonly movablekeys", 0, 0, 0, SetCmds::sintercard);
        CommandTable.reg("sinterstore", -3, "write denyoom", 1, -1, 1, SetCmds::sinterstore);
        CommandTable.reg("sunion", -2, "readonly", 1, -1, 1, SetCmds::sunion);
        CommandTable.reg("sunionstore", -3, "write denyoom", 1, -1, 1, SetCmds::sunionstore);
        CommandTable.reg("sdiff", -2, "readonly", 1, -1, 1, SetCmds::sdiff);
        CommandTable.reg("sdiffstore", -3, "write denyoom", 1, -1, 1, SetCmds::sdiffstore);
        CommandTable.reg("smembers", 2, "readonly", 1, 1, 1, SetCmds::smembers);
    }

    // ------------------------------------------------------------------
    // SADD key member [member ...]
    // ------------------------------------------------------------------
    static void sadd(Client c) {
        Server s = S();
        RObj o = lookupSetWrite(c, c.arg(1));
        if (o == null) {
            o = createSet(c.arg(2), c.argc - 2);
            s.dbAdd(c.db, c.arg(1), o);
        } else {
            maybeConvert(o, c.argc - 2);
        }
        int added = 0;
        for (int j = 2; j < c.argc; j++) if (setAdd(o, c.arg(j))) added++;
        if (added > 0) {
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            notify('s', "sadd", c.arg(1), c.db);
        }
        s.dirty += added;
        c.addInt(added);
    }

    // ------------------------------------------------------------------
    // SREM key member [member ...]
    // ------------------------------------------------------------------
    static void srem(Client c) {
        Server s = S();
        RObj o = lookupSetWrite(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        RSet set = set(o);
        int deleted = 0;
        boolean keyRemoved = false;
        for (int j = 2; j < c.argc; j++) {
            if (set.remove(c.arg(j))) {
                deleted++;
                if (set.size() == 0) { s.dbDelete(c.db, c.arg(1)); keyRemoved = true; break; }
            }
        }
        if (deleted > 0) {
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            notify('s', "srem", c.arg(1), c.db);
            if (keyRemoved) notify('g', "del", c.arg(1), c.db);
            s.dirty += deleted;
        }
        c.addInt(deleted);
    }

    // ------------------------------------------------------------------
    // SMOVE source destination member
    // ------------------------------------------------------------------
    static void smove(Client c) {
        Server s = S();
        RObj src = s.lookupKeyWrite(c, c.arg(1));
        RObj dst = s.lookupKeyWrite(c, c.arg(2));
        byte[] ele = c.arg(3);

        /* If the source key does not exist return 0 */
        if (src == null) { c.addInt(0); return; }

        if (src.type != RObj.SET) throw RedisError.wrongType();
        if (dst != null && dst.type != RObj.SET) throw RedisError.wrongType();

        /* If srcset and dstset are equal, SMOVE is a no-op */
        if (src == dst) { c.addInt(set(src).contains(ele) ? 1 : 0); return; }

        /* If the element cannot be removed from the src set, return 0. */
        if (!set(src).remove(ele)) { c.addInt(0); return; }
        notify('s', "srem", c.arg(1), c.db);

        if (set(src).size() == 0) deleteEmpty(c, c.arg(1));

        if (dst == null) {
            dst = createSet(ele, 1);
            s.dbAdd(c.db, c.arg(2), dst);
        }

        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        s.dirty++;

        /* An extra key has changed when ele was successfully added to dstset */
        if (setAdd(dst, ele)) {
            s.dirty++;
            s.signalModifiedKey(c.db, new Key(c.arg(2)));
            notify('s', "sadd", c.arg(2), c.db);
        }
        c.addInt(1);
    }

    // ------------------------------------------------------------------
    // SISMEMBER key member
    // ------------------------------------------------------------------
    static void sismember(Client c) {
        RObj o = lookupSetRead(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        c.addInt(set(o).contains(c.arg(2)) ? 1 : 0);
    }

    // ------------------------------------------------------------------
    // SMISMEMBER key member [member ...]
    // ------------------------------------------------------------------
    static void smismember(Client c) {
        RObj o = lookupSetRead(c, c.arg(1));
        c.addArray(c.argc - 2);
        for (int j = 2; j < c.argc; j++)
            c.addInt(o != null && set(o).contains(c.arg(j)) ? 1 : 0);
    }

    // ------------------------------------------------------------------
    // SCARD key
    // ------------------------------------------------------------------
    static void scard(Client c) {
        RObj o = lookupSetRead(c, c.arg(1));
        c.addInt(o == null ? 0 : set(o).size());
    }

    // ------------------------------------------------------------------
    // SPOP key [count]
    // ------------------------------------------------------------------
    static void spop(Client c) {
        if (c.argc == 3) { spopWithCount(c); return; }
        if (c.argc > 3) throw RedisError.syntax();

        Server s = S();
        RObj o = lookupSetWrite(c, c.arg(1));
        if (o == null) { c.addNil(); return; }

        List<byte[]> all = members(o);
        byte[] ele = all.get(RND.nextInt(all.size()));
        set(o).remove(ele);

        notify('s', "spop", c.arg(1), c.db);
        c.addBulk(ele);

        /* Delete the set if it's empty */
        if (set(o).size() == 0) deleteEmpty(c, c.arg(1));

        s.signalModifiedKey(c.db, new Key(c.arg(1)));
        s.dirty++;
        /* Replicate/AOF this command as an SREM operation. */
        s.replOverride = new byte[][]{Util.b("SREM"), c.arg(1), ele};
    }

    private static void spopWithCount(Client c) {
        Server s = S();
        long l = rangeLong(c, 2, 0, Long.MAX_VALUE, "value is out of range, must be positive");

        RObj o = lookupSetWrite(c, c.arg(1));
        if (o == null) { c.addSet(0); return; }

        /* If count is zero, serve an empty set ASAP. */
        if (l == 0) { c.addSet(0); return; }

        int size = set(o).size();
        int count = (int) Math.min(l, Integer.MAX_VALUE);
        int toRemove = count >= size ? size : count;

        notify('s', "spop", c.arg(1), c.db);
        s.dirty += toRemove;

        /* CASE 1: we return (and delete) the whole set. */
        if (count >= size) {
            List<byte[]> all = members(o);
            c.addSet(all.size());
            for (byte[] m : all) c.addBulk(m);
            deleteEmpty(c, c.arg(1));
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            /* Propagate this command as a DEL or UNLINK operation. */
            byte[] aux = Util.b(s.config.getBool("lazyfree-lazy-server-del") ? "UNLINK" : "DEL");
            s.replOverride = new byte[][]{aux, c.arg(1)};
            return;
        }

        /* CASE 2: extract `count` random members and return them. */
        List<byte[]> all = members(o);
        List<byte[]> popped = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            int j = i + RND.nextInt(all.size() - i);
            byte[] tmp = all.get(i); all.set(i, all.get(j)); all.set(j, tmp);
            popped.add(all.get(i));
        }
        c.addSet(count);
        for (byte[] m : popped) { c.addBulk(m); set(o).remove(m); }

        /* Replicate/AOF as one or more SREM commands. */
        List<byte[][]> batches = new ArrayList<>();
        for (int i = 0; i < popped.size(); i += SPOP_PROP_BATCH) {
            int end = Math.min(popped.size(), i + SPOP_PROP_BATCH);
            byte[][] cmd = new byte[2 + (end - i)][];
            cmd[0] = Util.b("SREM");
            cmd[1] = c.arg(1);
            for (int j = i; j < end; j++) cmd[2 + j - i] = popped.get(j);
            batches.add(cmd);
        }
        for (int i = 0; i < batches.size() - 1; i++) s.propagateExtra(c.db, batches.get(i));
        s.replOverride = batches.get(batches.size() - 1);

        s.signalModifiedKey(c.db, new Key(c.arg(1)));
    }

    // ------------------------------------------------------------------
    // SRANDMEMBER key [count]
    // ------------------------------------------------------------------
    static void srandmember(Client c) {
        if (c.argc == 3) { srandmemberWithCount(c); return; }
        if (c.argc > 3) throw RedisError.syntax();

        RObj o = lookupSetRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        List<byte[]> all = members(o);
        c.addBulk(all.get(RND.nextInt(all.size())));
    }

    private static void srandmemberWithCount(Client c) {
        long l = rangeLong(c, 2, -Long.MAX_VALUE, Long.MAX_VALUE);
        boolean uniq = true;
        long lcount;
        if (l >= 0) {
            lcount = l;
        } else {
            /* A negative count means: return the same elements multiple times. */
            lcount = -l;
            uniq = false;
        }

        RObj o = lookupSetRead(c, c.arg(1));
        if (o == null) { c.addEmptyArray(); return; }
        int size = set(o).size();

        if (lcount == 0) { c.addEmptyArray(); return; }

        List<byte[]> all = members(o);

        /* CASE 1: sample the whole set every time; members may repeat. */
        if (!uniq || lcount == 1) {
            int count = (int) Math.min(lcount, Integer.MAX_VALUE);
            c.addArray(count);
            for (int i = 0; i < count; i++) c.addBulk(all.get(RND.nextInt(size)));
            return;
        }

        /* CASE 2: more members requested than we have: return them all. */
        if (lcount >= size) {
            c.addArray(size);
            for (byte[] m : all) c.addBulk(m);
            return;
        }

        /* CASE 3/4: a distinct random subset. */
        int count = (int) lcount;
        for (int i = 0; i < count; i++) {
            int j = i + RND.nextInt(all.size() - i);
            byte[] tmp = all.get(i); all.set(i, all.get(j)); all.set(j, tmp);
        }
        c.addArray(count);
        for (int i = 0; i < count; i++) c.addBulk(all.get(i));
    }

    // ------------------------------------------------------------------
    // SINTER / SINTERCARD / SINTERSTORE
    // ------------------------------------------------------------------
    static void sinter(Client c) { sinterGeneric(c, 1, c.argc - 1, null, false, 0); }

    static void sinterstore(Client c) { sinterGeneric(c, 2, c.argc - 2, c.arg(1), false, 0); }

    static void sintercard(Client c) {
        long numkeys = rangeLong(c, 1, 1, Long.MAX_VALUE, "numkeys should be greater than 0");
        if (numkeys > c.argc - 2)
            throw new RedisError("ERR Number of keys can't be greater than number of args");

        long limit = 0;
        for (int j = (int) (2 + numkeys); j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "LIMIT") && moreargs > 0) {
                j++;
                limit = rangeLong(c, j, 0, Long.MAX_VALUE, "LIMIT can't be negative");
            } else {
                throw RedisError.syntax();
            }
        }
        sinterGeneric(c, 2, (int) numkeys, null, true, limit);
    }

    private static void sinterGeneric(Client c, int firstKey, int setnum, byte[] dstkey,
                                      boolean cardinalityOnly, long limit) {
        Server s = S();
        RObj[] sets = new RObj[setnum];
        int empty = 0;

        for (int j = 0; j < setnum; j++) {
            RObj o = s.lookupKeyRead(c, c.arg(firstKey + j));
            if (o == null) { empty++; sets[j] = null; continue; }
            if (o.type != RObj.SET) throw RedisError.wrongType();
            sets[j] = o;
        }

        /* Intersecting with an empty set always yields an empty set. */
        if (empty > 0) {
            if (dstkey != null) {
                if (s.dbDelete(c.db, dstkey)) {
                    s.signalModifiedKey(c.db, new Key(dstkey));
                    notify('g', "del", dstkey, c.db);
                    s.dirty++;
                }
                c.addInt(0);
            } else if (cardinalityOnly) {
                c.addInt(0);
            } else {
                c.addSet(0);
            }
            return;
        }

        /* Sort the sets from the smallest to the largest. */
        RObj[] sorted = sets.clone();
        Arrays.sort(sorted, Comparator.comparingInt(a -> set(a).size()));

        List<byte[]> result = new ArrayList<>();
        long cardinality = 0;
        for (byte[] m : members(sorted[0])) {
            int j;
            for (j = 1; j < setnum; j++) {
                if (sorted[j] == sorted[0]) continue;
                if (!set(sorted[j]).contains(m)) break;
            }
            if (j != setnum) continue;
            cardinality++;
            if (cardinalityOnly) {
                /* Stop searching once the limit is reached. */
                if (limit != 0 && cardinality >= limit) break;
            } else {
                result.add(m);
            }
        }

        if (cardinalityOnly) { c.addInt(cardinality); return; }

        if (dstkey == null) {
            c.addSet(result.size());
            for (byte[] m : result) c.addBulk(m);
            return;
        }

        if (!result.isEmpty()) {
            RObj dstset = newSetObject(sorted[0].encoding == RObj.ENC_INTSET
                    ? RObj.ENC_INTSET : RObj.ENC_HT);
            for (byte[] m : result) setAdd(dstset, m);
            maybeConvertToIntset(dstset);
            s.setKey(c, dstkey, dstset);
            c.addInt(set(dstset).size());
            notify('s', "sinterstore", dstkey, c.db);
            s.dirty++;
        } else {
            c.addInt(0);
            if (s.dbDelete(c.db, dstkey)) {
                s.dirty++;
                s.signalModifiedKey(c.db, new Key(dstkey));
                notify('g', "del", dstkey, c.db);
            }
        }
    }

    // ------------------------------------------------------------------
    // SUNION / SUNIONSTORE / SDIFF / SDIFFSTORE
    // ------------------------------------------------------------------
    static void sunion(Client c) { unionDiffGeneric(c, 1, c.argc - 1, null, SET_OP_UNION); }

    static void sunionstore(Client c) { unionDiffGeneric(c, 2, c.argc - 2, c.arg(1), SET_OP_UNION); }

    static void sdiff(Client c) { unionDiffGeneric(c, 1, c.argc - 1, null, SET_OP_DIFF); }

    static void sdiffstore(Client c) { unionDiffGeneric(c, 2, c.argc - 2, c.arg(1), SET_OP_DIFF); }

    private static void unionDiffGeneric(Client c, int firstKey, int setnum, byte[] dstkey, int op) {
        Server s = S();
        RObj[] sets = new RObj[setnum];
        int dstEncoding = RObj.ENC_INTSET;
        boolean sameset = false;

        for (int j = 0; j < setnum; j++) {
            RObj o = s.lookupKeyRead(c, c.arg(firstKey + j));
            if (o == null) { sets[j] = null; continue; }
            if (o.type != RObj.SET) throw RedisError.wrongType();
            if (dstkey == null && dstEncoding == RObj.ENC_INTSET
                    && (o.encoding == RObj.ENC_LISTPACK || o.encoding == RObj.ENC_HT))
                dstEncoding = RObj.ENC_HT;
            sets[j] = o;
            if (j > 0 && sets[0] == sets[j]) sameset = true;
        }

        RObj dstset = newSetObject(dstEncoding);

        if (op == SET_OP_UNION) {
            for (int j = 0; j < setnum; j++) {
                if (sets[j] == null) continue;   /* non existing keys are like empty sets */
                for (byte[] m : members(sets[j])) setAdd(dstset, m);
            }
        } else if (sameset) {
            /* One of the other sets is the first one: the difference is empty. */
        } else if (sets[0] != null) {
            for (byte[] m : members(sets[0])) {
                int j;
                for (j = 1; j < setnum; j++) {
                    if (sets[j] == null) continue;      /* no key is an empty set */
                    if (sets[j] == sets[0]) break;      /* same set! */
                    if (set(sets[j]).contains(m)) break;
                }
                if (j == setnum) setAdd(dstset, m);
            }
        }

        if (dstkey == null) {
            List<byte[]> out = members(dstset);
            c.addSet(out.size());
            for (byte[] m : out) c.addBulk(m);
            return;
        }

        if (set(dstset).size() > 0) {
            s.setKey(c, dstkey, dstset);
            c.addInt(set(dstset).size());
            notify('s', op == SET_OP_UNION ? "sunionstore" : "sdiffstore", dstkey, c.db);
            s.dirty++;
        } else {
            c.addInt(0);
            if (s.dbDelete(c.db, dstkey)) {
                s.dirty++;
                s.signalModifiedKey(c.db, new Key(dstkey));
                notify('g', "del", dstkey, c.db);
            }
        }
    }

    // ------------------------------------------------------------------
    // SMEMBERS key
    // ------------------------------------------------------------------
    static void smembers(Client c) {
        RObj o = lookupSetRead(c, c.arg(1));
        if (o == null) { c.addSet(0); return; }
        List<byte[]> all = members(o);
        c.addSet(all.size());
        for (byte[] m : all) c.addBulk(m);
    }
}
