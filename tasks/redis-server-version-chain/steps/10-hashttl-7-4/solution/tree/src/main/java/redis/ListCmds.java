package redis;

import java.util.*;

/**
 * List type commands, ported from redis 8.0.6 src/t_list.c (plus the blocking
 * bookkeeping of blocked.c and the command table of server.c/commands.def).
 */
public final class ListCmds {
    private ListCmds() {}

    /** listTypePush/Pop "where": LIST_HEAD / LIST_TAIL. */
    private static final int HEAD = 0, TAIL = 1;

    /** The Blocking type string owned by this file. */
    private static final String BTYPE = "list";

    // kinds of blocked list operation
    private static final int K_POP = 0;   // BLPOP / BRPOP
    private static final int K_MPOP = 1;  // BLMPOP
    private static final int K_MOVE = 2;  // BLMOVE / BRPOPLPUSH

    /** Everything a blocked client needs in order to be served later. */
    static final class BState {
        int kind;
        int where;              // K_POP/K_MPOP: side popped from
        long count;             // K_MPOP: max elements to pop
        List<byte[]> keys;      // keys the client waits on (in command order)
        byte[] dst;             // K_MOVE destination
        int wherefrom, whereto; // K_MOVE sides
        byte[] fromArg, toArg;  // K_MOVE: the client's own LEFT/RIGHT tokens
        boolean lmoveForm;      // K_MOVE: replicate as LMOVE (else RPOPLPUSH)
    }

    // ------------------------------------------------------------------
    // registration
    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("rpush", -3, "write denyoom fast", 1, 1, 1, c -> pushGeneric(c, TAIL, false));
        CommandTable.reg("lpush", -3, "write denyoom fast", 1, 1, 1, c -> pushGeneric(c, HEAD, false));
        CommandTable.reg("rpushx", -3, "write denyoom fast", 1, 1, 1, c -> pushGeneric(c, TAIL, true));
        CommandTable.reg("lpushx", -3, "write denyoom fast", 1, 1, 1, c -> pushGeneric(c, HEAD, true));
        CommandTable.reg("linsert", 5, "write denyoom", 1, 1, 1, ListCmds::linsertCommand);
        CommandTable.reg("rpop", -2, "write fast", 1, 1, 1, c -> popGeneric(c, TAIL));
        CommandTable.reg("lpop", -2, "write fast", 1, 1, 1, c -> popGeneric(c, HEAD));
        CommandTable.reg("lmpop", -4, "write movablekeys", 0, 0, 0, c -> lmpopGeneric(c, 1, false));
        CommandTable.reg("blmpop", -5, "write blocking movablekeys", 0, 0, 0, c -> lmpopGeneric(c, 2, true));
        CommandTable.reg("blpop", -3, "write blocking", 1, -2, 1,
                c -> { requireIntegerTimeout(c, c.argc - 1);
                       blockingPopGeneric(c, 1, c.argc - 2, HEAD, c.argc - 1, -1); });
        CommandTable.reg("brpop", -3, "write blocking", 1, -2, 1,
                c -> { requireIntegerTimeout(c, c.argc - 1);
                       blockingPopGeneric(c, 1, c.argc - 2, TAIL, c.argc - 1, -1); });
        CommandTable.reg("blmove", 6, "write denyoom blocking", 1, 2, 1, ListCmds::blmoveCommand);
        CommandTable.reg("brpoplpush", 4, "write denyoom blocking", 1, 2, 1, ListCmds::brpoplpushCommand);
        CommandTable.reg("rpoplpush", 3, "write denyoom", 1, 2, 1, c -> lmoveGeneric(c, TAIL, HEAD));
        CommandTable.reg("lmove", 5, "write denyoom", 1, 2, 1, ListCmds::lmoveCommand);
        CommandTable.reg("llen", 2, "readonly fast", 1, 1, 1, ListCmds::llenCommand);
        CommandTable.reg("lindex", 3, "readonly", 1, 1, 1, ListCmds::lindexCommand);
        CommandTable.reg("lset", 4, "write denyoom", 1, 1, 1, ListCmds::lsetCommand);
        CommandTable.reg("lrange", 4, "readonly", 1, 1, 1, ListCmds::lrangeCommand);
        CommandTable.reg("ltrim", 4, "write", 1, 1, 1, ListCmds::ltrimCommand);
        CommandTable.reg("lpos", -3, "readonly", 1, 1, 1, ListCmds::lposCommand);
        CommandTable.reg("lrem", 4, "write", 1, 1, 1, ListCmds::lremCommand);

        Blocking.READY.put(BTYPE, ListCmds::serveBlocked);
        Blocking.TIMEOUT.put(BTYPE, (s, c) -> {
            Object st = c.blockState;
            if (st instanceof BState && ((BState) st).kind == K_MOVE) c.addNil();
            else c.addNilArray();
        });
    }

    // ------------------------------------------------------------------
    // small helpers over the RList container
    // ------------------------------------------------------------------
    private static RObj newList() { return new RObj(RObj.LIST, RObj.ENC_LISTPACK, new RList()); }

    private static RList rl(RObj o) { return (RList) o.ptr; }

    private static void push(RList r, byte[] v, int where) {
        if (where == HEAD) r.l.addFirst(v); else r.l.addLast(v);
    }

    private static byte[] pop(RList r, int where) {
        return where == HEAD ? r.l.pollFirst() : r.l.pollLast();
    }

    /** listTypeDelRange() restricted to the two shapes t_list.c actually uses. */
    private static void delRangeHead(RList r, long n) {
        for (long i = 0; i < n && !r.l.isEmpty(); i++) r.l.pollFirst();
    }

    private static void delRangeTail(RList r, long n) {
        for (long i = 0; i < n && !r.l.isEmpty(); i++) r.l.pollLast();
    }

    private static void delRange(RList r, int where, long n) {
        if (where == HEAD) delRangeHead(r, n); else delRangeTail(r, n);
    }

    private static byte[] elementAt(RList r, long index) {
        long n = r.size();
        if (index < 0) index += n;
        if (index < 0 || index >= n) return null;
        Iterator<byte[]> it = r.l.iterator();
        for (long i = 0; i < index; i++) it.next();
        return it.next();
    }

    private static void replaceAt(RList r, long index, byte[] v) {
        List<byte[]> tmp = r.asList();
        tmp.set((int) index, v);
        r.l.clear();
        r.l.addAll(tmp);
    }

    // ------------------------------------------------------------------
    // encoding: listpack <-> quicklist, mirroring listTypeTryConversion*()
    // ------------------------------------------------------------------
    private static final long[] OPTIMIZATION_LEVEL = {4096, 8192, 16384, 32768, 65536};
    private static final long SIZE_SAFETY_LIMIT = 8192;

    private static int listFill() {
        return Server.I.config.getInt("list-max-listpack-size");
    }

    /** quicklistNodeLimit() size part; -1 stands for SIZE_MAX (no size limit). */
    private static long nodeSizeLimit(int fill) {
        if (fill >= 0) return -1;
        int off = (-fill) - 1;
        if (off < 0 || off >= OPTIMIZATION_LEVEL.length) off = OPTIMIZATION_LEVEL.length - 1;
        return OPTIMIZATION_LEVEL[off];
    }

    /** quicklistNodeLimit() count part; -1 stands for UINT_MAX (no count limit). */
    private static long nodeCountLimit(int fill) {
        if (fill >= 0) return fill == 0 ? 1 : fill;
        return -1;
    }

    /** lpEncodeGetType()+lpEncodeBacklen(): the exact number of bytes one entry takes. */
    private static long lpEntryBytes(byte[] v) {
        long enclen;
        Long iv = Util.string2ll(v);
        if (iv != null) {
            long x = iv;
            if (x >= 0 && x <= 127) enclen = 1;
            else if (x >= -4096 && x <= 4095) enclen = 2;
            else if (x >= -32768 && x <= 32767) enclen = 3;
            else if (x >= -8388608 && x <= 8388607) enclen = 4;
            else if (x >= -2147483648L && x <= 2147483647L) enclen = 5;
            else enclen = 9;
        } else {
            int size = v.length;
            if (size < 64) enclen = 1 + size;
            else if (size < 4096) enclen = 2 + size;
            else enclen = 5 + (long) size;
        }
        return enclen + backlenBytes(enclen);
    }

    private static long backlenBytes(long l) {
        if (l <= 127) return 1;
        if (l < 16383) return 2;
        if (l < 2097151) return 3;
        if (l < 268435455) return 4;
        return 5;
    }

    /** lpBytes(), but stops counting as soon as the total passes 'cap'. */
    private static long lpBytesCapped(RList r, long cap) {
        long total = 7; /* LP_HDR_SIZE + the EOF byte */
        for (byte[] v : r.l) {
            total += lpEntryBytes(v);
            if (total > cap) return total;
        }
        return total;
    }

    /** quicklistNodeExceedsLimit() */
    private static boolean exceedsLimit(int fill, long newSz, long newCount) {
        long szLimit = nodeSizeLimit(fill);
        long countLimit = nodeCountLimit(fill);
        if (szLimit != -1) return newSz > szLimit;
        if (newSz > SIZE_SAFETY_LIMIT) return true;
        return newCount > countLimit;
    }

    private static long maxZiplistEntries() { return Server.I.config.getLong("list-max-ziplist-entries"); }

    private static long maxZiplistValue() { return Server.I.config.getLong("list-max-ziplist-value"); }

    /**
     * t_list.c listTypePush(): a ziplist encoded list becomes a linkedlist as
     * soon as one pushed value is longer than list-max-ziplist-value, or the
     * list already holds list-max-ziplist-entries elements. Redis 2.8 never
     * converts a linkedlist back.
     */
    private static void tryConversionAppend(RObj o, byte[][] argv, int start, int end) {
        if (o.encoding != RObj.ENC_LISTPACK) return;
        long entries = rl(o).size();
        if (argv != null) {
            for (int i = start; i <= end; i++) {
                if (argv[i].length > maxZiplistValue()) { o.encoding = RObj.ENC_QUICKLIST; return; }
                if (entries >= maxZiplistEntries()) { o.encoding = RObj.ENC_QUICKLIST; return; }
                entries++;
            }
        }
    }

    private static void tryConversionAppendValue(RObj o, byte[] value) {
        if (o.encoding != RObj.ENC_LISTPACK) return;
        if (value.length > maxZiplistValue() || rl(o).size() >= maxZiplistEntries())
            o.encoding = RObj.ENC_QUICKLIST;
    }

    /** Redis 2.8 has no shrinking conversion: a linkedlist stays a linkedlist. */
    private static void tryConversionShrink(RObj o) {
    }

    // ------------------------------------------------------------------
    // argument parsing helpers (object.c)
    // ------------------------------------------------------------------
    private static long rangeLong(Client c, int idx, long min, long max, String msg) {
        Long v = Util.string2ll(c.argv[idx]);
        if (v == null) {
            throw new RedisError(msg != null ? "ERR " + msg : "ERR value is not an integer or out of range");
        }
        if (v < min || v > max) {
            if (msg != null) throw new RedisError("ERR " + msg);
            throw new RedisError("ERR value is out of range, value must between " + min + " and " + max);
        }
        return v;
    }

    private static long positiveLong(Client c, int idx, String msg) {
        return rangeLong(c, idx, 0, Long.MAX_VALUE,
                msg != null ? msg : "value is out of range, must be positive");
    }

    /** getListPositionFromObjectOrReply() */
    private static int listPosition(Client c, int idx) {
        if (c.argIs(idx, "right")) return TAIL;
        if (c.argIs(idx, "left")) return HEAD;
        throw RedisError.syntax();
    }

    /**
     * BLPOP / BRPOP take a whole number of seconds: getTimeoutFromObjectOrReply()
     * runs the argument through getLongLongFromObjectOrReply() first.
     */
    private static void requireIntegerTimeout(Client c, int idx) {
        if (Util.string2ll(c.argv[idx]) == null)
            throw new RedisError("ERR timeout is not an integer or out of range");
    }

    /** getTimeoutFromObjectOrReply(..., UNIT_SECONDS): returns the timeout in seconds. */
    private static double parseTimeout(Client c, int idx) {
        Double d = strtold(c.argv[idx]);
        if (d == null) throw new RedisError("ERR timeout is not a float or out of range");
        double ftval = d * 1000.0;
        if (ftval > (double) Long.MAX_VALUE) throw new RedisError("ERR timeout is out of range");
        long tval = (long) ftval;
        if (tval < 0) throw new RedisError("ERR timeout is negative");
        return tval / 1000.0;
    }

    /** string2ld(): like Util.string2d but strtod also accepts C hexadecimal floats. */
    private static Double strtold(byte[] b) {
        if (b == null || b.length == 0) return null;
        String t = Util.s(b);
        int i = 0;
        boolean neg = false;
        if (t.charAt(0) == '+' || t.charAt(0) == '-') { neg = t.charAt(0) == '-'; i = 1; }
        if (t.length() > i + 1 && t.charAt(i) == '0'
                && (t.charAt(i + 1) == 'x' || t.charAt(i + 1) == 'X')) {
            String rest = t.substring(i + 2);
            int p = -1;
            for (int k = 0; k < rest.length(); k++) {
                char ch = rest.charAt(k);
                if (ch == 'p' || ch == 'P') { p = k; break; }
            }
            String mant = p < 0 ? rest : rest.substring(0, p);
            String exp = p < 0 ? "0" : rest.substring(p + 1);
            if (mant.isEmpty() || mant.equals(".")) return null;
            int dots = 0;
            for (int k = 0; k < mant.length(); k++) {
                char ch = mant.charAt(k);
                if (ch == '.') { if (++dots > 1) return null; }
                else if (Character.digit(ch, 16) < 0) return null;
            }
            int e = 0;
            if (exp.isEmpty()) return null;
            if (exp.charAt(0) == '+' || exp.charAt(0) == '-') e = 1;
            if (exp.length() == e) return null;
            for (int k = e; k < exp.length(); k++) if (exp.charAt(k) < '0' || exp.charAt(k) > '9') return null;
            try {
                double v = Double.parseDouble("0x" + mant + "p" + exp);
                if (Double.isNaN(v)) return null;
                return neg ? -v : v;
            } catch (NumberFormatException ex) {
                return null;
            }
        }
        return Util.string2d(b);
    }

    // ------------------------------------------------------------------
    // reply helpers
    // ------------------------------------------------------------------
    /** addListRangeReply() */
    private static void addListRangeReply(Client c, RObj o, long start, long end, boolean reverse) {
        RList r = rl(o);
        long llen = r.size();
        if (start < 0) start = llen + start;
        if (end < 0) end = llen + end;
        if (start < 0) start = 0;
        if (start > end || start >= llen) { c.addEmptyArray(); return; }
        if (end >= llen) end = llen - 1;
        int rangelen = (int) (end - start + 1);
        byte[][] buf = new byte[rangelen][];
        Iterator<byte[]> it = r.l.iterator();
        for (long i = 0; i < start; i++) it.next();
        for (int i = 0; i < rangelen; i++) buf[i] = it.next();
        c.addArray(rangelen);
        if (reverse) for (int i = rangelen - 1; i >= 0; i--) c.addBulk(buf[i]);
        else for (int i = 0; i < rangelen; i++) c.addBulk(buf[i]);
    }

    /** listElementsRemoved(): notifications, key deletion and server.dirty. */
    private static void listElementsRemoved(Client c, byte[] key, int where, RObj o, long count, boolean signal) {
        Server s = Server.I;
        String event = (where == HEAD) ? "lpop" : "rpop";
        long llen = rl(o).size();
        s.notifyKeyspaceEvent('l', event, key, c.db);
        if (llen == 0) {
            s.dbDelete(c.db, key);
            s.notifyKeyspaceEvent('g', "del", key, c.db);
        } else {
            tryConversionShrink(o);
        }
        if (signal) s.signalModifiedKey(c.db, new Key(key));
        s.dirty += count;
    }

    /** listPopRangeAndReplyWithKey(): the [key, [elements...]] reply used by L/BLMPOP. */
    private static void popRangeAndReplyWithKey(Client c, RObj o, byte[] key, int where, long count) {
        RList r = rl(o);
        long llen = r.size();
        long rangelen = Math.min(count, llen);
        long rangestart = (where == HEAD) ? 0 : -rangelen;
        long rangeend = (where == HEAD) ? rangelen - 1 : -1;
        boolean reverse = (where != HEAD);

        c.addArray(2);
        c.addBulk(key);
        addListRangeReply(c, o, rangestart, rangeend, reverse);

        delRange(r, where, rangelen);
        listElementsRemoved(c, key, where, o, rangelen, true);
    }

    private static byte[][] popCmd(int where, byte[] key) {
        return new byte[][]{Util.b(where == HEAD ? "LPOP" : "RPOP"), key};
    }

    private static byte[][] popCountCmd(int where, byte[] key, long count) {
        return new byte[][]{Util.b(where == HEAD ? "LPOP" : "RPOP"), key, Util.ll2string(count)};
    }

    // ------------------------------------------------------------------
    // LPUSH / RPUSH / LPUSHX / RPUSHX
    // ------------------------------------------------------------------
    private static void pushGeneric(Client c, int where, boolean xx) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        RObj lobj = s.lookupKeyWrite(c, key);
        if (lobj != null && lobj.type != RObj.LIST) throw RedisError.wrongType();
        if (lobj == null) {
            if (xx) { c.addInt(0); return; }
            lobj = newList();
            s.dbAdd(c.db, key, lobj);
        }

        tryConversionAppend(lobj, c.argv, 2, c.argc - 1);
        RList r = rl(lobj);
        for (int j = 2; j < c.argc; j++) {
            push(r, c.arg(j), where);
            s.dirty++;
        }
        c.addInt(r.size());

        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('l', where == HEAD ? "lpush" : "rpush", key, c.db);
        s.signalKeyAsReady(c.db, key);
    }

    // ------------------------------------------------------------------
    // LINSERT
    // ------------------------------------------------------------------
    private static void linsertCommand(Client c) {
        Server s = Server.I;
        int where;
        if (c.argIs(2, "after")) where = TAIL;
        else if (c.argIs(2, "before")) where = HEAD;
        else throw RedisError.syntax();

        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) { c.addInt(0); return; }
        if (o.type != RObj.LIST) throw RedisError.wrongType();

        tryConversionAppend(o, c.argv, 4, 4);

        RList r = rl(o);
        List<byte[]> tmp = r.asList();
        byte[] pivot = c.arg(3);
        int idx = -1;
        for (int i = 0; i < tmp.size(); i++) {
            if (Arrays.equals(tmp.get(i), pivot)) { idx = i; break; }
        }
        if (idx < 0) { c.addInt(-1); return; }

        tmp.add(where == HEAD ? idx : idx + 1, c.arg(4));
        r.l.clear();
        r.l.addAll(tmp);

        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('l', "linsert", key, c.db);
        s.dirty++;
        c.addInt(r.size());
    }

    // ------------------------------------------------------------------
    // LLEN / LINDEX / LSET
    // ------------------------------------------------------------------
    private static void llenCommand(Client c) {
        RObj o = Server.I.lookupType(c, c.arg(1), RObj.LIST);
        c.addInt(o == null ? 0 : rl(o).size());
    }

    private static void lindexCommand(Client c) {
        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addNil(); return; }
        if (o.type != RObj.LIST) throw RedisError.wrongType();
        long index = c.argLong(2);
        byte[] v = elementAt(rl(o), index);
        if (v != null) c.addBulk(v); else c.addNil();
    }

    private static void lsetCommand(Client c) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) throw RedisError.noSuchKey();
        if (o.type != RObj.LIST) throw RedisError.wrongType();
        long index = c.argLong(2);

        tryConversionAppend(o, c.argv, 3, 3);

        RList r = rl(o);
        long n = r.size();
        long i = index < 0 ? index + n : index;
        if (i < 0 || i >= n) throw RedisError.indexOutOfRange();
        replaceAt(r, i, c.arg(3));
        tryConversionShrink(o);

        c.addOk();
        s.signalModifiedKey(c.db, new Key(key));
        s.notifyKeyspaceEvent('l', "lset", key, c.db);
        s.dirty++;
    }

    // ------------------------------------------------------------------
    // LPOP / RPOP
    // ------------------------------------------------------------------
    private static void popGeneric(Client c, int where) {
        Server s = Server.I;
        boolean hascount = (c.argc == 3);
        long count = 0;

        if (c.argc > 3) throw RedisError.wrongArgs(where == HEAD ? "lpop" : "rpop");
        if (hascount) count = positiveLong(c, 2, null);

        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) {
            if (hascount) c.addNilArray(); else c.addNil();
            return;
        }
        if (o.type != RObj.LIST) throw RedisError.wrongType();

        if (hascount && count == 0) { c.addEmptyArray(); return; }

        RList r = rl(o);
        if (count == 0) {
            byte[] value = pop(r, where);
            c.addBulk(value);
            listElementsRemoved(c, key, where, o, 1, true);
        } else {
            long llen = r.size();
            long rangelen = Math.min(count, llen);
            long rangestart = (where == HEAD) ? 0 : -rangelen;
            long rangeend = (where == HEAD) ? rangelen - 1 : -1;
            boolean reverse = (where != HEAD);
            addListRangeReply(c, o, rangestart, rangeend, reverse);
            delRange(r, where, rangelen);
            listElementsRemoved(c, key, where, o, rangelen, true);
        }
    }

    // ------------------------------------------------------------------
    // LRANGE / LTRIM
    // ------------------------------------------------------------------
    private static void lrangeCommand(Client c) {
        long start = c.argLong(2);
        long end = c.argLong(3);
        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addEmptyArray(); return; }
        if (o.type != RObj.LIST) throw RedisError.wrongType();
        addListRangeReply(c, o, start, end, false);
    }

    private static void ltrimCommand(Client c) {
        Server s = Server.I;
        long start = c.argLong(2);
        long end = c.argLong(3);
        byte[] key = c.arg(1);

        RObj o = s.lookupKeyWrite(c, key);
        if (o == null) { c.addOk(); return; }
        if (o.type != RObj.LIST) throw RedisError.wrongType();

        RList r = rl(o);
        long llen = r.size();
        if (start < 0) start = llen + start;
        if (end < 0) end = llen + end;
        if (start < 0) start = 0;

        long ltrim, rtrim;
        if (start > end || start >= llen) {
            ltrim = llen;
            rtrim = 0;
        } else {
            if (end >= llen) end = llen - 1;
            ltrim = start;
            rtrim = llen - end - 1;
        }

        delRangeHead(r, ltrim);
        delRangeTail(r, rtrim);

        s.notifyKeyspaceEvent('l', "ltrim", key, c.db);
        if (r.size() == 0) {
            s.dbDelete(c.db, key);
            s.notifyKeyspaceEvent('g', "del", key, c.db);
        } else {
            tryConversionShrink(o);
        }
        s.signalModifiedKey(c.db, new Key(key));
        s.dirty += (ltrim + rtrim);
        c.addOk();
    }

    // ------------------------------------------------------------------
    // LPOS
    // ------------------------------------------------------------------
    private static void lposCommand(Client c) {
        byte[] ele = c.arg(2);
        boolean fromTail = false;
        long rank = 1, count = -1, maxlen = 0;

        for (int j = 3; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "RANK") && moreargs > 0) {
                j++;
                rank = rangeLong(c, j, -Long.MAX_VALUE, Long.MAX_VALUE, null);
                if (rank == 0) {
                    throw new RedisError("ERR RANK can't be zero: use 1 to start from "
                            + "the first match, 2 from the second ... "
                            + "or use negative to start from the end of the list");
                }
            } else if (c.argIs(j, "COUNT") && moreargs > 0) {
                j++;
                count = positiveLong(c, j, "COUNT can't be negative");
            } else if (c.argIs(j, "MAXLEN") && moreargs > 0) {
                j++;
                maxlen = positiveLong(c, j, "MAXLEN can't be negative");
            } else {
                throw RedisError.syntax();
            }
        }

        if (rank < 0) { rank = -rank; fromTail = true; }

        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) {
            if (count != -1) c.addEmptyArray(); else c.addNil();
            return;
        }
        if (o.type != RObj.LIST) throw RedisError.wrongType();

        RList r = rl(o);
        long llen = r.size();
        Iterator<byte[]> it = fromTail ? r.l.descendingIterator() : r.l.iterator();
        List<Long> found = count != -1 ? new ArrayList<>() : null;
        long index = 0, matches = 0, matchindex = -1;

        while (it.hasNext() && (maxlen == 0 || index < maxlen)) {
            byte[] cur = it.next();
            if (Arrays.equals(cur, ele)) {
                matches++;
                matchindex = fromTail ? (llen - index - 1) : index;
                if (matches >= rank) {
                    if (found != null) {
                        found.add(matchindex);
                        if (count != 0 && matches - rank + 1 >= count) break;
                    } else {
                        break;
                    }
                }
            }
            index++;
            matchindex = -1;
        }

        if (found != null) {
            c.addArray(found.size());
            for (long v : found) c.addInt(v);
        } else {
            if (matchindex != -1) c.addInt(matchindex); else c.addNil();
        }
    }

    // ------------------------------------------------------------------
    // LREM
    // ------------------------------------------------------------------
    private static void lremCommand(Client c) {
        Server s = Server.I;
        byte[] obj = c.arg(3);
        long toremove = rangeLong(c, 2, -Long.MAX_VALUE, Long.MAX_VALUE, null);
        byte[] key = c.arg(1);

        RObj subject = s.lookupKeyWrite(c, key);
        if (subject == null) { c.addInt(0); return; }
        if (subject.type != RObj.LIST) throw RedisError.wrongType();

        boolean fromTail = false;
        if (toremove < 0) { toremove = -toremove; fromTail = true; }

        RList r = rl(subject);
        List<byte[]> src = r.asList();
        boolean[] dead = new boolean[src.size()];
        long removed = 0;

        if (fromTail) {
            for (int i = src.size() - 1; i >= 0; i--) {
                if (Arrays.equals(src.get(i), obj)) {
                    dead[i] = true;
                    s.dirty++;
                    removed++;
                    if (toremove != 0 && removed == toremove) break;
                }
            }
        } else {
            for (int i = 0; i < src.size(); i++) {
                if (Arrays.equals(src.get(i), obj)) {
                    dead[i] = true;
                    s.dirty++;
                    removed++;
                    if (toremove != 0 && removed == toremove) break;
                }
            }
        }

        if (removed > 0) {
            r.l.clear();
            for (int i = 0; i < src.size(); i++) if (!dead[i]) r.l.addLast(src.get(i));

            s.notifyKeyspaceEvent('l', "lrem", key, c.db);
            if (r.size() == 0) {
                s.dbDelete(c.db, key);
                s.notifyKeyspaceEvent('g', "del", key, c.db);
            } else {
                tryConversionShrink(subject);
            }
            s.signalModifiedKey(c.db, new Key(key));
        }
        c.addInt(removed);
    }

    // ------------------------------------------------------------------
    // LMOVE / RPOPLPUSH (and the body shared with BLMOVE / BRPOPLPUSH)
    // ------------------------------------------------------------------
    private static void lmoveCommand(Client c) {
        int wherefrom = listPosition(c, 3);
        int whereto = listPosition(c, 4);
        lmoveGeneric(c, wherefrom, whereto);
    }

    /** lmoveHandlePush() */
    private static void lmoveHandlePush(Client c, byte[] dstkey, RObj dstobj, byte[] value, int where) {
        Server s = Server.I;
        if (dstobj == null) {
            dstobj = newList();
            s.dbAdd(c.db, dstkey, dstobj);
        }
        tryConversionAppendValue(dstobj, value);
        push(rl(dstobj), value, where);
        s.signalModifiedKey(c.db, new Key(dstkey));
        s.notifyKeyspaceEvent('l', where == HEAD ? "lpush" : "rpush", dstkey, c.db);
        s.signalKeyAsReady(c.db, dstkey);
        c.addBulk(value);
    }

    private static void lmoveGeneric(Client c, int wherefrom, int whereto) {
        Server s = Server.I;
        byte[] srckey = c.arg(1);
        byte[] dstkey = c.arg(2);

        RObj sobj = s.lookupKeyWrite(c, srckey);
        if (sobj == null) { c.addNil(); return; }
        if (sobj.type != RObj.LIST) throw RedisError.wrongType();

        if (rl(sobj).size() == 0) {
            /* Only reachable through very old RDB files. */
            c.addNil();
            return;
        }
        RObj dobj = s.lookupKeyWrite(c, dstkey);
        if (dobj != null && dobj.type != RObj.LIST) throw RedisError.wrongType();

        byte[] value = pop(rl(sobj), wherefrom);
        lmoveHandlePush(c, dstkey, dobj, value, whereto);
        listElementsRemoved(c, srckey, wherefrom, sobj, 1, true);
    }

    // ------------------------------------------------------------------
    // LMPOP / BLMPOP
    // ------------------------------------------------------------------
    /** mpopGenericCommand() */
    private static void mpopGeneric(Client c, int keysStart, int numkeys, int where, long count) {
        Server s = Server.I;
        for (int j = 0; j < numkeys; j++) {
            byte[] key = c.arg(keysStart + j);
            RObj o = s.lookupKeyWrite(c, key);
            if (o == null) continue;
            if (o.type != RObj.LIST) throw RedisError.wrongType();
            long llen = rl(o).size();
            if (llen == 0) continue;

            popRangeAndReplyWithKey(c, o, key, where, count);
            /* Replicate it as [LR]POP <key> <count>. */
            s.replOverride = popCountCmd(where, key, Math.min(count, llen));
            return;
        }
        c.addNilArray();
    }

    /** lmpopGenericCommand() */
    private static void lmpopGeneric(Client c, int numkeysIdx, boolean isBlock) {
        long numkeys = rangeLong(c, numkeysIdx, 1, Long.MAX_VALUE, "numkeys should be greater than 0");
        long whereIdx = (long) numkeysIdx + numkeys + 1;
        if (whereIdx >= c.argc) throw RedisError.syntax();
        int where = listPosition(c, (int) whereIdx);

        long count = -1;
        for (int j = (int) whereIdx + 1; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (count == -1 && c.argIs(j, "COUNT") && moreargs > 0) {
                j++;
                count = rangeLong(c, j, 1, Long.MAX_VALUE, "count should be greater than 0");
            } else {
                throw RedisError.syntax();
            }
        }
        if (count == -1) count = 1;

        if (isBlock) blockingPopGeneric(c, numkeysIdx + 1, (int) numkeys, where, 1, count);
        else mpopGeneric(c, numkeysIdx + 1, (int) numkeys, where, count);
    }

    // ------------------------------------------------------------------
    // BLPOP / BRPOP / BLMPOP
    // ------------------------------------------------------------------
    /** CLIENT_DENY_BLOCKING: Lua / AOF-load pseudo clients and MULTI/EXEC. */
    private static boolean denyBlocking(Client c) {
        return c.fake || Server.I.execDepth > 0;
    }

    /** blockingPopGenericCommand(): count == -1 means a plain single-element pop. */
    private static void blockingPopGeneric(Client c, int keysStart, int numkeys, int where,
                                           int timeoutIdx, long count) {
        Server s = Server.I;
        double timeout = parseTimeout(c, timeoutIdx);

        for (int j = 0; j < numkeys; j++) {
            byte[] key = c.arg(keysStart + j);
            RObj o = s.lookupKeyWrite(c, key);
            if (o == null) continue;
            if (o.type != RObj.LIST) throw RedisError.wrongType();
            long llen = rl(o).size();
            if (llen == 0) continue;

            if (count != -1) {
                popRangeAndReplyWithKey(c, o, key, where, count);
                s.replOverride = popCountCmd(where, key, Math.min(count, llen));
                return;
            }
            byte[] value = pop(rl(o), where);
            c.addArray(2);
            c.addBulk(key);
            c.addBulk(value);
            listElementsRemoved(c, key, where, o, 1, true);
            s.replOverride = popCmd(where, key);
            return;
        }

        if (denyBlocking(c)) { c.addNilArray(); return; }

        List<byte[]> keys = new ArrayList<>(numkeys);
        for (int j = 0; j < numkeys; j++) keys.add(c.arg(keysStart + j));

        BState st = new BState();
        st.kind = (count != -1) ? K_MPOP : K_POP;
        st.where = where;
        st.count = count;
        st.keys = keys;
        Blocking.blockOnKeys(s, c, keys, timeout, BTYPE, st);
    }

    // ------------------------------------------------------------------
    // BLMOVE / BRPOPLPUSH
    // ------------------------------------------------------------------
    private static void blmoveCommand(Client c) {
        int wherefrom = listPosition(c, 3);
        int whereto = listPosition(c, 4);
        double timeout = parseTimeout(c, 5);
        blmoveGeneric(c, wherefrom, whereto, timeout, true);
    }

    private static void brpoplpushCommand(Client c) {
        double timeout = parseTimeout(c, 3);
        blmoveGeneric(c, TAIL, HEAD, timeout, false);
    }

    private static byte[][] moveCmd(BState st) {
        if (st.lmoveForm) {
            return new byte[][]{Util.b("LMOVE"), st.keys.get(0), st.dst, st.fromArg, st.toArg};
        }
        return new byte[][]{Util.b("RPOPLPUSH"), st.keys.get(0), st.dst};
    }

    private static void blmoveGeneric(Client c, int wherefrom, int whereto, double timeout, boolean lmoveForm) {
        Server s = Server.I;
        RObj src = s.lookupKeyWrite(c, c.arg(1));
        if (src != null && src.type != RObj.LIST) throw RedisError.wrongType();

        if (src == null) {
            if (denyBlocking(c)) { c.addNil(); return; }
            BState st = new BState();
            st.kind = K_MOVE;
            st.keys = new ArrayList<>(Collections.singletonList(c.arg(1)));
            st.dst = c.arg(2);
            st.wherefrom = wherefrom;
            st.whereto = whereto;
            if (lmoveForm) { st.fromArg = c.arg(3); st.toArg = c.arg(4); }
            st.lmoveForm = lmoveForm;
            st.count = -1;
            Blocking.blockOnKeys(s, c, st.keys, timeout, BTYPE, st);
            return;
        }

        long before = s.dirty;
        lmoveGeneric(c, wherefrom, whereto);
        if (s.dirty != before) {
            /* Replicate as the non-blocking variant. */
            if (lmoveForm) {
                s.replOverride = new byte[][]{Util.b("LMOVE"), c.arg(1), c.arg(2), c.arg(3), c.arg(4)};
            } else {
                s.replOverride = new byte[][]{Util.b("RPOPLPUSH"), c.arg(1), c.arg(2)};
            }
        }
    }

    // ------------------------------------------------------------------
    // serving clients blocked on a list key
    // ------------------------------------------------------------------
    /** Guard for the recursive drain used by chained BLMOVEs. */
    private static int chainDepth = 0;

    private static void chainReadyKeys(Server s) {
        if (chainDepth >= 32) return;
        chainDepth++;
        try { s.handleReadyKeys(); } finally { chainDepth--; }
    }

    private static boolean serveBlocked(Server s, Client c, Key key) {
        /* Inside MULTI/EXEC (or a script) redis only wakes blocked clients once
         * the whole transaction is done: postpone by re-arming the ready key. */
        if (s.execDepth > 0) {
            s.signalKeyAsReady(c.db, key.b);
            return false;
        }
        Object raw = c.blockState;
        if (!(raw instanceof BState)) return false;
        BState st = (BState) raw;

        /* handleClientsBlockedOnKey(): only serve when the ready key really
         * holds a list right now (LPUSH+DEL, or LPUSH+DEL+SET inside MULTI). */
        RObj signalled = s.dbs[c.db].dict.get(key);
        if (signalled == null || signalled.type != RObj.LIST || rl(signalled).size() == 0) return false;

        try {
            if (st.kind == K_MOVE) return serveBlockedMove(s, c, st);
            return serveBlockedPop(s, c, st);
        } catch (RedisError e) {
            s.unblockClient(c);
            c.addErr(e.msg);
            return true;
        }
    }

    private static boolean serveBlockedPop(Server s, Client c, BState st) {
        for (byte[] key : st.keys) {
            RObj o = s.dbs[c.db].dict.get(new Key(key));
            if (o == null) continue;
            if (o.type != RObj.LIST) throw RedisError.wrongType();
            long llen = rl(o).size();
            if (llen == 0) continue;

            /* Unblock before mutating: keeps a self-referential chain finite. */
            s.unblockClient(c);

            if (st.kind == K_MPOP) {
                popRangeAndReplyWithKey(c, o, key, st.where, st.count);
                s.propagateExtra(c.db, popCountCmd(st.where, key, Math.min(st.count, llen)));
            } else {
                byte[] value = pop(rl(o), st.where);
                c.addArray(2);
                c.addBulk(key);
                c.addBulk(value);
                listElementsRemoved(c, key, st.where, o, 1, true);
                s.propagateExtra(c.db, popCmd(st.where, key));
            }
            return true;
        }
        return false;
    }

    private static boolean serveBlockedMove(Server s, Client c, BState st) {
        byte[] srckey = st.keys.get(0);
        RObj sobj = s.dbs[c.db].dict.get(new Key(srckey));
        if (sobj == null) return false;
        if (sobj.type != RObj.LIST) throw RedisError.wrongType();
        if (rl(sobj).size() == 0) return false;

        RObj dobj = s.dbs[c.db].dict.get(new Key(st.dst));

        /* Unblock first so a destination that feeds this very client terminates. */
        s.unblockClient(c);

        if (dobj != null && dobj.type != RObj.LIST) throw RedisError.wrongType();

        byte[] value = pop(rl(sobj), st.wherefrom);
        lmoveHandlePush(c, st.dst, dobj, value, st.whereto);
        listElementsRemoved(c, srckey, st.wherefrom, sobj, 1, true);
        s.propagateExtra(c.db, moveCmd(st));

        /* The push above may have made the destination ready for another
         * blocked client (linked/circular LMOVEs): drain it now so the whole
         * chain completes inside this command. */
        chainReadyKeys(s);
        return true;
    }
}
