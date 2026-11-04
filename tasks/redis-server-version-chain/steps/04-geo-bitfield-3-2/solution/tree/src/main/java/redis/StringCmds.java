package redis;

import java.util.ArrayList;
import java.util.List;

/**
 * String type commands: a port of redis 8.0.6 src/t_string.c.
 *
 * SET/SETNX/SETEX/PSETEX/GET/GETSET/GETDEL/GETEX/MGET/MSET/MSETNX/APPEND/
 * STRLEN/SETRANGE/GETRANGE/SUBSTR/INCR/DECR/INCRBY/DECRBY/INCRBYFLOAT/LCS.
 */
public final class StringCmds {
    private StringCmds() {}

    /* Flags of setGenericCommand()/parseExtendedStringArgumentsOrReply(). */
    static final int OBJ_NO_FLAGS = 0;
    static final int OBJ_SET_NX = 1 << 0;
    static final int OBJ_SET_XX = 1 << 1;
    static final int OBJ_EX = 1 << 2;
    static final int OBJ_PX = 1 << 3;
    static final int OBJ_KEEPTTL = 1 << 4;
    static final int OBJ_SET_GET = 1 << 5;
    static final int OBJ_EXAT = 1 << 6;
    static final int OBJ_PXAT = 1 << 7;
    static final int OBJ_PERSIST = 1 << 8;

    static final int UNIT_SECONDS = 0, UNIT_MILLISECONDS = 1;
    private static final int COMMAND_GET = 0, COMMAND_SET = 1;

    // ------------------------------------------------------------------
    // command table
    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("set", -3, "write denyoom", 1, 1, 1, StringCmds::set);
        CommandTable.reg("setnx", 3, "write denyoom fast", 1, 1, 1, StringCmds::setnx);
        CommandTable.reg("setex", 4, "write denyoom", 1, 1, 1, StringCmds::setex);
        CommandTable.reg("psetex", 4, "write denyoom", 1, 1, 1, StringCmds::psetex);
        CommandTable.reg("get", 2, "readonly fast", 1, 1, 1, StringCmds::get);
        CommandTable.reg("getex", -2, "write fast", 1, 1, 1, StringCmds::getex);
        CommandTable.reg("getdel", 2, "write fast", 1, 1, 1, StringCmds::getdel);
        CommandTable.reg("getset", 3, "write denyoom fast", 1, 1, 1, StringCmds::getset);
        CommandTable.reg("mget", -2, "readonly fast", 1, -1, 1, StringCmds::mget);
        CommandTable.reg("mset", -3, "write denyoom", 1, -1, 2, StringCmds::mset);
        CommandTable.reg("msetnx", -3, "write denyoom", 1, -1, 2, StringCmds::msetnx);
        CommandTable.reg("append", 3, "write denyoom fast", 1, 1, 1, StringCmds::append);
        CommandTable.reg("strlen", 2, "readonly fast", 1, 1, 1, StringCmds::strlen);
        CommandTable.reg("setrange", 4, "write denyoom", 1, 1, 1, StringCmds::setrange);
        CommandTable.reg("getrange", 4, "readonly", 1, 1, 1, StringCmds::getrange);
        CommandTable.reg("substr", 4, "readonly", 1, 1, 1, StringCmds::getrange);
        CommandTable.reg("incr", 2, "write denyoom fast", 1, 1, 1, StringCmds::incr);
        CommandTable.reg("decr", 2, "write denyoom fast", 1, 1, 1, StringCmds::decr);
        CommandTable.reg("incrby", 3, "write denyoom fast", 1, 1, 1, StringCmds::incrby);
        CommandTable.reg("decrby", 3, "write denyoom fast", 1, 1, 1, StringCmds::decrby);
        CommandTable.reg("incrbyfloat", 3, "write denyoom fast", 1, 1, 1, StringCmds::incrbyfloat);
        CommandTable.reg("lcs", -3, "readonly", 1, 2, 1, StringCmds::lcs);
        /* STRALGO is the 6.0/6.2 spelling of LCS; 7.0 replaced it with LCS.
         * It declares no key positions -- the keys arrive behind the KEYS
         * keyword, which is why COMMAND GETKEYS could not serve it either. */
        CommandTable.reg("stralgo", -2, "readonly", 0, 0, 0, StringCmds::stralgo);
    }

    // ------------------------------------------------------------------
    // shared helpers (also used by BitCmds)
    // ------------------------------------------------------------------

    /** mustObeyClient(): the AOF loading client and the master link bypass size limits. */
    static boolean mustObey(Client c) {
        Server s = Server.I;
        if (s.loading) return true;
        return s.repl != null && s.repl.isMasterClient(c);
    }

    /** checkStringLength(): enforce proto-max-bulk-len on the resulting value. */
    static void checkStringLength(Client c, long size, long append) {
        if (mustObey(c)) return;
        long total = size + append;
        if (total > Server.I.protoMaxBulkLen() || total < size || total < append)
            throw new RedisError("ERR string exceeds maximum allowed size (proto-max-bulk-len)");
    }

    /** tryObjectEncoding() over a command argument: int / embstr / raw. */
    static RObj newStr(byte[] v) {
        Long l = Util.string2ll(v);
        if (l != null && v.length <= 20) return new RObj(RObj.STRING, RObj.ENC_INT, l);
        if (v.length <= 44) return new RObj(RObj.STRING, RObj.ENC_EMBSTR, v);
        /* Raw values get mutated in place by APPEND/SETRANGE/SETBIT: never alias argv. */
        return new RObj(RObj.STRING, RObj.ENC_RAW, Util.copy(v));
    }

    /** dbUnshareStringValue(): make sure the value is a private, raw byte array. */
    static byte[] unshareRaw(RObj o) {
        if (o.encoding != RObj.ENC_RAW) {
            byte[] v = o.str();
            if (o.encoding != RObj.ENC_INT) v = Util.copy(v);
            o.encoding = RObj.ENC_RAW;
            o.ptr = v;
        }
        return (byte[]) o.ptr;
    }

    /** setKey(): store the value, optionally keeping the current TTL. */
    static void storeValue(Client c, byte[] key, RObj o, boolean keepttl) {
        Server s = Server.I;
        Db d = s.dbs[c.db];
        Key k = new Key(key);
        d.dict.put(k, o);
        if (!keepttl) d.expires.remove(k);
        s.signalModifiedKey(c.db, k);
    }

    /**
     * string2ld(): strict long double parsing. strtold() refuses leading whitespace and
     * any trailing garbage, and an embedded NUL makes eptr stop short of the end, so
     * every one of those is rejected here before delegating the numeric conversion
     * (java's Double.parseDouble() would silently trim whitespace).
     */
    static Double string2ld(byte[] p) {
        if (p == null || p.length == 0 || p.length >= 5 * 1024) return null;
        for (int i = 0; i < p.length; i++) {
            if ((p[i] & 0xff) <= ' ') return null;
        }
        return Util.string2d(p);
    }

    /** checkAlreadyExpired(): only masters outside of loading turn a past TTL into a DEL. */
    static boolean checkAlreadyExpired(long when) {
        Server s = Server.I;
        return when <= s.mstime() && !s.loading && (s.repl == null || s.repl.masterHost == null);
    }

    // ------------------------------------------------------------------
    // SET and friends
    // ------------------------------------------------------------------

    /** Parse state of parseExtendedStringArgumentsOrReply(). */
    private static final class Ext {
        int flags = OBJ_NO_FLAGS;
        int unit = UNIT_SECONDS;
        byte[] expire = null;
    }

    private static Ext parseExtendedStringArguments(Client c, int commandType) {
        Ext e = new Ext();
        int j = commandType == COMMAND_GET ? 2 : 3;
        for (; j < c.argc; j++) {
            boolean hasNext = j != c.argc - 1;
            if (c.argIs(j, "nx") && (e.flags & OBJ_SET_XX) == 0 && commandType == COMMAND_SET) {
                e.flags |= OBJ_SET_NX;
            } else if (c.argIs(j, "xx") && (e.flags & OBJ_SET_NX) == 0 && commandType == COMMAND_SET) {
                e.flags |= OBJ_SET_XX;
            } else if (c.argIs(j, "get") && commandType == COMMAND_SET) {
                e.flags |= OBJ_SET_GET;
            } else if (c.argIs(j, "keepttl") && (e.flags & OBJ_PERSIST) == 0
                    && (e.flags & OBJ_EX) == 0 && (e.flags & OBJ_EXAT) == 0
                    && (e.flags & OBJ_PX) == 0 && (e.flags & OBJ_PXAT) == 0
                    && commandType == COMMAND_SET) {
                e.flags |= OBJ_KEEPTTL;
            } else if (c.argIs(j, "persist") && commandType == COMMAND_GET
                    && (e.flags & OBJ_EX) == 0 && (e.flags & OBJ_EXAT) == 0
                    && (e.flags & OBJ_PX) == 0 && (e.flags & OBJ_PXAT) == 0
                    && (e.flags & OBJ_KEEPTTL) == 0) {
                e.flags |= OBJ_PERSIST;
            } else if (c.argIs(j, "ex") && (e.flags & OBJ_KEEPTTL) == 0 && (e.flags & OBJ_PERSIST) == 0
                    && (e.flags & OBJ_EXAT) == 0 && (e.flags & OBJ_PX) == 0
                    && (e.flags & OBJ_PXAT) == 0 && hasNext) {
                e.flags |= OBJ_EX;
                e.expire = c.arg(j + 1);
                j++;
            } else if (c.argIs(j, "px") && (e.flags & OBJ_KEEPTTL) == 0 && (e.flags & OBJ_PERSIST) == 0
                    && (e.flags & OBJ_EX) == 0 && (e.flags & OBJ_EXAT) == 0
                    && (e.flags & OBJ_PXAT) == 0 && hasNext) {
                e.flags |= OBJ_PX;
                e.unit = UNIT_MILLISECONDS;
                e.expire = c.arg(j + 1);
                j++;
            } else if (c.argIs(j, "exat") && (e.flags & OBJ_KEEPTTL) == 0 && (e.flags & OBJ_PERSIST) == 0
                    && (e.flags & OBJ_EX) == 0 && (e.flags & OBJ_PX) == 0
                    && (e.flags & OBJ_PXAT) == 0 && hasNext) {
                e.flags |= OBJ_EXAT;
                e.expire = c.arg(j + 1);
                j++;
            } else if (c.argIs(j, "pxat") && (e.flags & OBJ_KEEPTTL) == 0 && (e.flags & OBJ_PERSIST) == 0
                    && (e.flags & OBJ_EX) == 0 && (e.flags & OBJ_EXAT) == 0
                    && (e.flags & OBJ_PX) == 0 && hasNext) {
                e.flags |= OBJ_PXAT;
                e.unit = UNIT_MILLISECONDS;
                e.expire = c.arg(j + 1);
                j++;
            } else {
                throw RedisError.syntax();
            }
        }
        return e;
    }

    /** getExpireMillisecondsOrReply(): relative/absolute TTL to absolute unix ms. */
    private static long getExpireMilliseconds(byte[] expire, int flags, int unit, String cmdName) {
        Long v = Util.string2ll(expire);
        if (v == null) throw RedisError.notInt();
        long ms = v;
        if (ms <= 0 || (unit == UNIT_SECONDS && ms > Long.MAX_VALUE / 1000))
            throw new RedisError("ERR invalid expire time in '" + cmdName + "' command");
        if (unit == UNIT_SECONDS) ms *= 1000;
        if ((flags & OBJ_PX) != 0 || (flags & OBJ_EX) != 0) ms += Server.I.mstime();
        if (ms <= 0) throw new RedisError("ERR invalid expire time in '" + cmdName + "' command");
        return ms;
    }

    /** getGenericCommand(): reply with the string value or nil, WRONGTYPE otherwise. */
    private static void getGeneric(Client c) {
        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        if (o.type != RObj.STRING) throw RedisError.wrongType();
        c.addBulk(o.str());
    }

    /**
     * setGenericCommand(). 'numericReplies' selects SETNX's :1 / :0 replies instead of
     * SET's +OK / null.
     */
    private static void setGeneric(Client c, int flags, byte[] key, byte[] val, byte[] expire,
                                   int unit, boolean numericReplies, String cmdName) {
        Server s = Server.I;
        long milliseconds = 0;
        if (expire != null) milliseconds = getExpireMilliseconds(expire, flags, unit, cmdName);

        if ((flags & OBJ_SET_GET) != 0) getGeneric(c);

        boolean found = s.lookupKeyWrite(c, key) != null;

        if (((flags & OBJ_SET_NX) != 0 && found) || ((flags & OBJ_SET_XX) != 0 && !found)) {
            if ((flags & OBJ_SET_GET) == 0) {
                if (numericReplies) c.addInt(0);
                else c.addNil();
            }
            return;
        }

        boolean keepttl = (flags & OBJ_KEEPTTL) != 0 || expire != null;
        storeValue(c, key, newStr(val), keepttl);
        s.dirty++;
        s.notifyKeyspaceEvent('$', "set", key, c.db);

        if (expire != null) {
            s.dbs[c.db].setExpire(new Key(key), milliseconds);
            /* Propagate as SET key value PXAT <ms> unless PXAT was used already. */
            if ((flags & OBJ_PXAT) == 0) {
                s.replOverride = new byte[][]{
                        Util.b("SET"), key, val, Util.b("PXAT"), Util.ll2string(milliseconds)};
            }
            s.notifyKeyspaceEvent('g', "expire", key, c.db);
        }

        if ((flags & OBJ_SET_GET) == 0) {
            if (numericReplies) c.addInt(1);
            else c.addOk();
        }

        /* Propagate without the GET argument (not needed when we rewrote for the expire). */
        if ((flags & OBJ_SET_GET) != 0 && expire == null) {
            List<byte[]> out = new ArrayList<>(c.argc);
            for (int j = 0; j < c.argc; j++) {
                if (j >= 3 && c.argIs(j, "get")) continue;
                out.add(c.argv[j]);
            }
            s.replOverride = out.toArray(new byte[0][]);
        }
    }

    static void set(Client c) {
        Ext e = parseExtendedStringArguments(c, COMMAND_SET);
        setGeneric(c, e.flags, c.arg(1), c.arg(2), e.expire, e.unit, false, "set");
    }

    static void setnx(Client c) {
        setGeneric(c, OBJ_SET_NX, c.arg(1), c.arg(2), null, UNIT_SECONDS, true, "setnx");
    }

    static void setex(Client c) {
        setGeneric(c, OBJ_EX, c.arg(1), c.arg(3), c.arg(2), UNIT_SECONDS, false, "setex");
    }

    static void psetex(Client c) {
        setGeneric(c, OBJ_PX, c.arg(1), c.arg(3), c.arg(2), UNIT_MILLISECONDS, false, "psetex");
    }

    static void get(Client c) {
        getGeneric(c);
    }

    static void getex(Client c) {
        Server s = Server.I;
        Ext e = parseExtendedStringArguments(c, COMMAND_GET);

        RObj o = s.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        if (o.type != RObj.STRING) throw RedisError.wrongType();

        long milliseconds = 0;
        if (e.expire != null) milliseconds = getExpireMilliseconds(e.expire, e.flags, e.unit, "getex");

        /* Reply before we possibly drop the key. */
        c.addBulk(o.str());

        byte[] key = c.arg(1);
        Key k = new Key(key);
        if (((e.flags & OBJ_PXAT) != 0 || (e.flags & OBJ_EXAT) != 0) && checkAlreadyExpired(milliseconds)) {
            boolean lazy = s.config.getBool("lazyfree-lazy-expire");
            s.dbs[c.db].delete(k);
            s.replOverride = new byte[][]{Util.b(lazy ? "UNLINK" : "DEL"), key};
            s.signalModifiedKey(c.db, k);
            s.notifyKeyspaceEvent('g', "del", key, c.db);
            s.dirty++;
        } else if (e.expire != null) {
            s.dbs[c.db].setExpire(k, milliseconds);
            s.replOverride = new byte[][]{Util.b("PEXPIREAT"), key, Util.ll2string(milliseconds)};
            s.signalModifiedKey(c.db, k);
            s.notifyKeyspaceEvent('g', "expire", key, c.db);
            s.dirty++;
        } else if ((e.flags & OBJ_PERSIST) != 0) {
            if (s.dbs[c.db].expires.remove(k) != null) {
                s.signalModifiedKey(c.db, k);
                s.replOverride = new byte[][]{Util.b("PERSIST"), key};
                s.notifyKeyspaceEvent('g', "persist", key, c.db);
                s.dirty++;
            }
        }
    }

    static void getdel(Client c) {
        Server s = Server.I;
        getGeneric(c);
        byte[] key = c.arg(1);
        Key k = new Key(key);
        if (s.dbs[c.db].delete(k)) {
            s.replOverride = new byte[][]{Util.b("DEL"), key};
            s.signalModifiedKey(c.db, k);
            s.notifyKeyspaceEvent('g', "del", key, c.db);
            s.dirty++;
        }
    }

    static void getset(Client c) {
        Server s = Server.I;
        getGeneric(c);
        byte[] key = c.arg(1);
        storeValue(c, key, newStr(c.arg(2)), false);
        s.notifyKeyspaceEvent('$', "set", key, c.db);
        s.dirty++;
        /* Propagate as SET. */
        s.replOverride = new byte[][]{Util.b("SET"), key, c.arg(2)};
    }

    // ------------------------------------------------------------------
    // SETRANGE / GETRANGE
    // ------------------------------------------------------------------
    static void setrange(Client c) {
        Server s = Server.I;
        byte[] value = c.arg(3);
        int valueLen = value.length;

        Long off = Util.string2ll(c.arg(2));
        if (off == null) throw RedisError.notInt();
        long offset = off;
        if (offset < 0) throw new RedisError("ERR offset is out of range");

        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) {
            if (valueLen == 0) { c.addInt(0); return; }
            checkStringLength(c, offset, valueLen);
            o = new RObj(RObj.STRING, RObj.ENC_RAW, new byte[(int) (offset + valueLen)]);
            s.dbAdd(c.db, key, o);
        } else {
            if (o.type != RObj.STRING) throw RedisError.wrongType();
            int oldLen = o.strlen();
            if (valueLen == 0) { c.addInt(oldLen); return; }
            checkStringLength(c, offset, valueLen);
            unshareRaw(o);
        }

        byte[] buf = (byte[]) o.ptr;
        long needed = offset + valueLen;
        if (buf.length < needed) {
            byte[] nb = new byte[(int) needed];
            System.arraycopy(buf, 0, nb, 0, buf.length);
            o.ptr = nb;
            buf = nb;
        }
        System.arraycopy(value, 0, buf, (int) offset, valueLen);
        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('$', "setrange", key, c.db);
        s.dirty++;
        c.addInt(buf.length);
    }

    static void getrange(Client c) {
        Long s1 = Util.string2ll(c.arg(2));
        if (s1 == null) throw RedisError.notInt();
        Long e1 = Util.string2ll(c.arg(3));
        if (e1 == null) throw RedisError.notInt();
        long start = s1, end = e1;

        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addBulk(Util.EMPTY); return; }
        if (o.type != RObj.STRING) throw RedisError.wrongType();

        byte[] str = o.str();
        long strlen = str.length;

        if (start < 0 && end < 0 && start > end) { c.addBulk(Util.EMPTY); return; }
        if (start < 0) start = strlen + start;
        if (end < 0) end = strlen + end;
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if (end >= strlen) end = strlen - 1;

        if (start > end || strlen == 0) {
            c.addBulk(Util.EMPTY);
        } else {
            byte[] out = new byte[(int) (end - start + 1)];
            System.arraycopy(str, (int) start, out, 0, out.length);
            c.addBulk(out);
        }
    }

    // ------------------------------------------------------------------
    // MGET / MSET / MSETNX
    // ------------------------------------------------------------------
    static void mget(Client c) {
        c.addArray(c.argc - 1);
        for (int j = 1; j < c.argc; j++) {
            RObj o = Server.I.lookupKeyRead(c, c.arg(j));
            if (o == null || o.type != RObj.STRING) c.addNil();
            else c.addBulk(o.str());
        }
    }

    private static void msetGeneric(Client c, boolean nx) {
        Server s = Server.I;
        if ((c.argc % 2) == 0) throw RedisError.wrongArgs(nx ? "msetnx" : "mset");

        if (nx) {
            for (int j = 1; j < c.argc; j += 2) {
                if (s.lookupKeyWrite(c, c.arg(j)) != null) { c.addInt(0); return; }
            }
        }

        for (int j = 1; j < c.argc; j += 2) {
            storeValue(c, c.arg(j), newStr(c.arg(j + 1)), false);
            s.notifyKeyspaceEvent('$', "set", c.arg(j), c.db);
        }
        s.dirty += (c.argc - 1) / 2;
        if (nx) c.addInt(1);
        else c.addOk();
    }

    static void mset(Client c) { msetGeneric(c, false); }

    static void msetnx(Client c) { msetGeneric(c, true); }

    // ------------------------------------------------------------------
    // APPEND / STRLEN
    // ------------------------------------------------------------------
    static void append(Client c) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        byte[] add = c.arg(2);
        long totlen;

        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) {
            o = newStr(add);
            s.dbAdd(c.db, key, o);
            totlen = o.strlen();
        } else {
            if (o.type != RObj.STRING) throw RedisError.wrongType();
            checkStringLength(c, o.strlen(), add.length);
            byte[] cur = unshareRaw(o);
            byte[] nb = new byte[cur.length + add.length];
            System.arraycopy(cur, 0, nb, 0, cur.length);
            System.arraycopy(add, 0, nb, cur.length, add.length);
            o.ptr = nb;
            totlen = nb.length;
        }
        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('$', "append", key, c.db);
        s.dirty++;
        c.addInt(totlen);
    }

    static void strlen(Client c) {
        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        if (o.type != RObj.STRING) throw RedisError.wrongType();
        c.addInt(o.strlen());
    }

    // ------------------------------------------------------------------
    // INCR / DECR family
    // ------------------------------------------------------------------
    private static void incrDecr(Client c, long incr) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();

        long value = 0;
        if (o != null) {
            if (o.encoding == RObj.ENC_INT) {
                value = (Long) o.ptr;
            } else {
                Long v = Util.string2ll((byte[]) o.ptr);
                if (v == null) throw RedisError.notInt();
                value = v;
            }
        }

        long oldvalue = value;
        if ((incr < 0 && oldvalue < 0 && incr < (Long.MIN_VALUE - oldvalue))
                || (incr > 0 && oldvalue > 0 && incr > (Long.MAX_VALUE - oldvalue))) {
            throw new RedisError("ERR increment or decrement would overflow");
        }
        value += incr;

        if (o != null && o.encoding == RObj.ENC_INT) {
            o.ptr = value;
        } else {
            RObj nw = new RObj(RObj.STRING, RObj.ENC_INT, value);
            if (o != null) s.dbs[c.db].dict.put(new Key(key), nw);
            else s.dbAdd(c.db, key, nw);
        }
        c.addInt(value);
        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('$', "incrby", key, c.db);
        s.dirty++;
    }

    static void incr(Client c) { incrDecr(c, 1); }

    static void decr(Client c) { incrDecr(c, -1); }

    static void incrby(Client c) {
        Long incr = Util.string2ll(c.arg(2));
        if (incr == null) throw RedisError.notInt();
        incrDecr(c, incr);
    }

    static void decrby(Client c) {
        Long incr = Util.string2ll(c.arg(2));
        if (incr == null) throw RedisError.notInt();
        /* Negating LLONG_MIN would overflow. */
        if (incr == Long.MIN_VALUE) throw new RedisError("ERR decrement would overflow");
        incrDecr(c, -incr);
    }

    static void incrbyfloat(Client c) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();

        double value = 0;
        if (o != null) {
            if (o.encoding == RObj.ENC_INT) {
                value = (Long) o.ptr;
            } else {
                Double d = string2ld((byte[]) o.ptr);
                if (d == null) throw RedisError.notFloat();
                value = d;
            }
        }
        Double incrD = string2ld(c.arg(2));
        if (incrD == null) throw RedisError.notFloat();

        value += incrD;
        if (Double.isNaN(value) || Double.isInfinite(value))
            throw new RedisError("ERR increment would produce NaN or Infinity");

        byte[] nv = Util.b(Util.ld2str(value));
        RObj nw = new RObj(RObj.STRING, nv.length <= 44 ? RObj.ENC_EMBSTR : RObj.ENC_RAW, nv);
        if (o != null) s.dbs[c.db].dict.put(new Key(key), nw);
        else s.dbAdd(c.db, key, nw);

        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('$', "incrbyfloat", key, c.db);
        s.dirty++;
        c.addBulk(nv);

        /* Replicate as SET key <newvalue> KEEPTTL so float formatting can't drift. */
        s.replOverride = new byte[][]{Util.b("SET"), key, nv, Util.b("KEEPTTL")};
    }

    // ------------------------------------------------------------------
    // LCS
    // ------------------------------------------------------------------
    static void lcs(Client c) {
        Server s = Server.I;
        RObj obja = s.lookupKeyRead(c, c.arg(1));
        RObj objb = s.lookupKeyRead(c, c.arg(2));
        if ((obja != null && obja.type != RObj.STRING) || (objb != null && objb.type != RObj.STRING))
            throw new RedisError("ERR The specified keys must contain string values");

        byte[] a = obja != null ? obja.str() : Util.EMPTY;
        byte[] b = objb != null ? objb.str() : Util.EMPTY;

        long minmatchlen = 0;
        boolean getlen = false, getidx = false, withmatchlen = false;

        for (int j = 3; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "idx")) {
                getidx = true;
            } else if (c.argIs(j, "len")) {
                getlen = true;
            } else if (c.argIs(j, "withmatchlen")) {
                withmatchlen = true;
            } else if (c.argIs(j, "minmatchlen") && moreargs > 0) {
                Long v = Util.string2ll(c.arg(j + 1));
                if (v == null) throw RedisError.notInt();
                minmatchlen = v;
                if (minmatchlen < 0) minmatchlen = 0;
                j++;
            } else {
                throw RedisError.syntax();
            }
        }

        lcsExec(c, a, b, minmatchlen, getlen, getidx, withmatchlen);
    }

    // ------------------------------------------------------------------
    // STRALGO LCS [KEYS k1 k2 | STRINGS s1 s2] [LEN] [IDX] [MINMATCHLEN n]
    //             [WITHMATCHLEN]
    //
    // t_string.c stralgoCommand()/stralgoLCS() as they stood at 6.0 and 6.2.
    // Redis 7.0 promoted this to the top level `LCS key1 key2 ...` form, which
    // `lcs()` above implements; the two share the algorithm and differ only in
    // how the pair of strings is named.
    // ------------------------------------------------------------------
    static void stralgo(Client c) {
        if (!c.argIs(1, "lcs")) {
            String sub = Server.trunc(Util.s(c.arg(1)), 128)
                    .replace('\r', ' ').replace('\n', ' ');
            throw new RedisError("ERR Unknown STRALGO subcommand or wrong number of arguments for '"
                    + sub + "'");
        }
        Server s = Server.I;
        byte[] a = null, b = null;
        long minmatchlen = 0;
        boolean getlen = false, getidx = false, withmatchlen = false;

        for (int j = 2; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "idx")) {
                getidx = true;
            } else if (c.argIs(j, "len")) {
                getlen = true;
            } else if (c.argIs(j, "withmatchlen")) {
                withmatchlen = true;
            } else if (c.argIs(j, "minmatchlen") && moreargs > 0) {
                Long v = Util.string2ll(c.arg(j + 1));
                if (v == null) throw RedisError.notInt();
                minmatchlen = v;
                if (minmatchlen < 0) minmatchlen = 0;
                j++;
            } else if (c.argIs(j, "keys") && moreargs > 1) {
                if (a != null || b != null) throw new RedisError("ERR Either use STRINGS or KEYS");
                RObj obja = s.lookupKeyRead(c, c.arg(j + 1));
                RObj objb = s.lookupKeyRead(c, c.arg(j + 2));
                if ((obja != null && obja.type != RObj.STRING)
                        || (objb != null && objb.type != RObj.STRING))
                    throw new RedisError("ERR The specified keys must contain string values");
                a = obja != null ? obja.str() : Util.EMPTY;
                b = objb != null ? objb.str() : Util.EMPTY;
                j += 2;
            } else if (c.argIs(j, "strings") && moreargs > 1) {
                if (a != null || b != null) throw new RedisError("ERR Either use STRINGS or KEYS");
                a = c.arg(j + 1);
                b = c.arg(j + 2);
                j += 2;
            } else {
                throw RedisError.syntax();
            }
        }

        if (a == null || b == null)
            throw new RedisError("ERR Please specify two strings: "
                    + "STRINGS or KEYS options are mandatory");

        lcsExec(c, a, b, minmatchlen, getlen, getidx, withmatchlen);
    }

    /** The shared LCS engine behind both LCS and STRALGO LCS. */
    private static void lcsExec(Client c, byte[] a, byte[] b, long minmatchlen,
                                boolean getlen, boolean getidx, boolean withmatchlen) {
        Server s = Server.I;
        if (getlen && getidx)
            throw new RedisError("ERR If you want both the length and indexes, please just use IDX.");

        int alen = a.length, blen = b.length;
        long lcssize = (long) (alen + 1) * (blen + 1);
        long lcsalloc = lcssize * 4L;
        if (lcsalloc > s.protoMaxBulkLen())
            throw new RedisError("ERR Insufficient memory, transient memory for LCS exceeds proto-max-bulk-len");
        if (lcssize > Integer.MAX_VALUE)
            throw new RedisError("ERR Insufficient memory, failed allocating transient memory for LCS");

        int[] lcs;
        try {
            lcs = new int[(int) lcssize];
        } catch (OutOfMemoryError err) {
            throw new RedisError("ERR Insufficient memory, failed allocating transient memory for LCS");
        }

        for (int i = 0; i <= alen; i++) {
            int rowBase = i * (blen + 1);
            int prevBase = rowBase - (blen + 1);
            for (int j = 0; j <= blen; j++) {
                if (i == 0 || j == 0) {
                    lcs[rowBase + j] = 0;
                } else if (a[i - 1] == b[j - 1]) {
                    lcs[rowBase + j] = lcs[prevBase + j - 1] + 1;
                } else {
                    int l1 = lcs[prevBase + j];
                    int l2 = lcs[rowBase + j - 1];
                    lcs[rowBase + j] = l1 > l2 ? l1 : l2;
                }
            }
        }

        int total = lcs[alen * (blen + 1) + blen];
        boolean computelcs = getidx || !getlen;
        byte[] result = computelcs ? new byte[total] : null;
        int idx = total;

        long arangeStart = alen, arangeEnd = 0, brangeStart = 0, brangeEnd = 0;
        List<long[]> matches = getidx ? new ArrayList<>() : null;

        int i = alen, j = blen;
        while (computelcs && i > 0 && j > 0) {
            boolean emitRange = false;
            if (a[i - 1] == b[j - 1]) {
                result[idx - 1] = a[i - 1];
                if (arangeStart == alen) {
                    arangeStart = i - 1;
                    arangeEnd = i - 1;
                    brangeStart = j - 1;
                    brangeEnd = j - 1;
                } else {
                    if (arangeStart == i && brangeStart == j) {
                        arangeStart--;
                        brangeStart--;
                    } else {
                        emitRange = true;
                    }
                }
                if (arangeStart == 0 || brangeStart == 0) emitRange = true;
                idx--; i--; j--;
            } else {
                int l1 = lcs[(i - 1) * (blen + 1) + j];
                int l2 = lcs[i * (blen + 1) + (j - 1)];
                if (l1 > l2) i--;
                else j--;
                if (arangeStart != alen) emitRange = true;
            }

            long matchLen = arangeEnd - arangeStart + 1;
            if (emitRange) {
                if (minmatchlen == 0 || matchLen >= minmatchlen) {
                    if (matches != null)
                        matches.add(new long[]{arangeStart, arangeEnd, brangeStart, brangeEnd, matchLen});
                }
                arangeStart = alen;
            }
        }

        if (matches != null) {
            c.addMap(2);
            c.addBulk("matches");
            c.addArray(matches.size());
            for (long[] m : matches) {
                c.addArray(withmatchlen ? 3 : 2);
                c.addArray(2);
                c.addInt(m[0]);
                c.addInt(m[1]);
                c.addArray(2);
                c.addInt(m[2]);
                c.addInt(m[3]);
                if (withmatchlen) c.addInt(m[4]);
            }
            c.addBulk("len");
            c.addInt(total);
        } else if (getlen) {
            c.addInt(total);
        } else {
            c.addBulk(result);
        }
    }
}
