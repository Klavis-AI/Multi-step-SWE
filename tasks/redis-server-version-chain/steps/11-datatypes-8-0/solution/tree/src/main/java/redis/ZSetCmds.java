package redis;

import java.math.BigDecimal;
import java.math.MathContext;
import java.math.RoundingMode;
import java.util.*;

/**
 * Sorted set commands: a port of redis src/t_zset.c (8.0.6).
 *
 * The backing container is {@link RZSet} (member -> score dict plus a
 * (score, member) ordered index), which plays the role of both the listpack
 * and the skiplist+dict encodings; {@code RObj.encoding} is maintained
 * separately so OBJECT ENCODING keeps reporting listpack/skiplist the way
 * redis does.
 */
public final class ZSetCmds {
    private ZSetCmds() {}

    /* ------------------------------------------------------------------ */
    /* zsetAdd() input / output flags                                      */
    /* ------------------------------------------------------------------ */
    private static final int IN_NX = 1, IN_XX = 2, IN_INCR = 4, IN_GT = 8, IN_LT = 16;
    private static final int OUT_NOP = 1, OUT_NAN = 2, OUT_ADDED = 4, OUT_UPDATED = 8;

    /* zrange_type */
    private static final int RT_AUTO = 0, RT_RANK = 1, RT_SCORE = 2, RT_LEX = 3;
    /* zrange_direction */
    private static final int DIR_AUTO = 0, DIR_FWD = 1, DIR_REV = 2;

    /* set operations */
    private static final int OP_UNION = 0, OP_INTER = 1, OP_DIFF = 2;
    private static final int AGG_SUM = 1, AGG_MIN = 2, AGG_MAX = 3;

    private static final String NAN_ERR = "ERR resulting score is not a number (NaN)";
    private static final String NOT_FLOAT_RANGE = "ERR min or max is not a float";
    private static final String NOT_LEX_RANGE = "ERR min or max not valid string range item";

    private static final Key EMPTY_KEY = new Key(Util.EMPTY);
    private static final Random RND = new Random();

    /* ------------------------------------------------------------------ */
    /* registration                                                        */
    /* ------------------------------------------------------------------ */
    public static void register() {
        CommandTable.reg("zadd", -4, "write denyoom fast", 1, 1, 1, c -> zaddGeneric(c, 0));
        CommandTable.reg("zincrby", 4, "write denyoom fast", 1, 1, 1, c -> zaddGeneric(c, IN_INCR));
        CommandTable.reg("zrem", -3, "write fast", 1, 1, 1, ZSetCmds::zrem);
        CommandTable.reg("zscore", 3, "readonly fast", 1, 1, 1, ZSetCmds::zscore);
        CommandTable.reg("zmscore", -3, "readonly fast", 1, 1, 1, ZSetCmds::zmscore);
        CommandTable.reg("zcard", 2, "readonly fast", 1, 1, 1, ZSetCmds::zcard);
        CommandTable.reg("zcount", 4, "readonly fast", 1, 1, 1, ZSetCmds::zcount);
        CommandTable.reg("zlexcount", 4, "readonly fast", 1, 1, 1, ZSetCmds::zlexcount);
        CommandTable.reg("zrank", -3, "readonly fast", 1, 1, 1, c -> zrankGeneric(c, false));
        CommandTable.reg("zrevrank", -3, "readonly fast", 1, 1, 1, c -> zrankGeneric(c, true));

        CommandTable.reg("zrange", -4, "readonly", 1, 1, 1,
                c -> zrangeGeneric(c, 1, false, RT_AUTO, DIR_AUTO));
        CommandTable.reg("zrangestore", -5, "write denyoom", 1, 2, 1,
                c -> zrangeGeneric(c, 2, true, RT_AUTO, DIR_AUTO));
        CommandTable.reg("zrangebyscore", -4, "readonly", 1, 1, 1,
                c -> zrangeGeneric(c, 1, false, RT_SCORE, DIR_FWD));
        CommandTable.reg("zrevrangebyscore", -4, "readonly", 1, 1, 1,
                c -> zrangeGeneric(c, 1, false, RT_SCORE, DIR_REV));
        CommandTable.reg("zrangebylex", -4, "readonly", 1, 1, 1,
                c -> zrangeGeneric(c, 1, false, RT_LEX, DIR_FWD));
        CommandTable.reg("zrevrangebylex", -4, "readonly", 1, 1, 1,
                c -> zrangeGeneric(c, 1, false, RT_LEX, DIR_REV));
        CommandTable.reg("zrevrange", -4, "readonly", 1, 1, 1,
                c -> zrangeGeneric(c, 1, false, RT_RANK, DIR_REV));

        CommandTable.reg("zremrangebyrank", 4, "write", 1, 1, 1, c -> zremrangeGeneric(c, RT_RANK));
        CommandTable.reg("zremrangebyscore", 4, "write", 1, 1, 1, c -> zremrangeGeneric(c, RT_SCORE));
        CommandTable.reg("zremrangebylex", 4, "write", 1, 1, 1, c -> zremrangeGeneric(c, RT_LEX));

        CommandTable.reg("zunionstore", -4, "write denyoom movablekeys", 1, 1, 1,
                c -> zunionInterDiffGeneric(c, c.arg(1), 2, OP_UNION, false));
        CommandTable.reg("zinterstore", -4, "write denyoom movablekeys", 1, 1, 1,
                c -> zunionInterDiffGeneric(c, c.arg(1), 2, OP_INTER, false));
        CommandTable.reg("zdiffstore", -4, "write denyoom movablekeys", 1, 1, 1,
                c -> zunionInterDiffGeneric(c, c.arg(1), 2, OP_DIFF, false));
        CommandTable.reg("zunion", -3, "readonly movablekeys", 0, 0, 0,
                c -> zunionInterDiffGeneric(c, null, 1, OP_UNION, false));
        CommandTable.reg("zinter", -3, "readonly movablekeys", 0, 0, 0,
                c -> zunionInterDiffGeneric(c, null, 1, OP_INTER, false));
        CommandTable.reg("zdiff", -3, "readonly movablekeys", 0, 0, 0,
                c -> zunionInterDiffGeneric(c, null, 1, OP_DIFF, false));
        CommandTable.reg("zintercard", -3, "readonly movablekeys", 0, 0, 0,
                c -> zunionInterDiffGeneric(c, null, 1, OP_INTER, true));

        CommandTable.reg("zpopmin", -2, "write fast", 1, 1, 1, c -> zpopMinMax(c, false));
        CommandTable.reg("zpopmax", -2, "write fast", 1, 1, 1, c -> zpopMinMax(c, true));
        CommandTable.reg("bzpopmin", -3, "write fast blocking", 1, -2, 1, c -> bzpopMinMax(c, false));
        CommandTable.reg("bzpopmax", -3, "write fast blocking", 1, -2, 1, c -> bzpopMinMax(c, true));
        CommandTable.reg("zmpop", -4, "write movablekeys", 0, 0, 0, c -> zmpopGeneric(c, 1, false));
        CommandTable.reg("bzmpop", -5, "write blocking movablekeys", 0, 0, 0, c -> zmpopGeneric(c, 2, true));

        CommandTable.reg("zrandmember", -2, "readonly random", 1, 1, 1, ZSetCmds::zrandmember);

        Blocking.READY.put("zset", ZSetCmds::serveBlocked);
        Blocking.TIMEOUT.put("zset", (s, c) -> c.addNilArray());
    }

    /* ------------------------------------------------------------------ */
    /* small helpers                                                       */
    /* ------------------------------------------------------------------ */
    private static RZSet zs(RObj o) { return (RZSet) o.ptr; }

    private static long maxListpackEntries() {
        return Server.I.config.getLong("zset-max-listpack-entries");
    }

    private static long maxListpackValue() {
        return Server.I.config.getLong("zset-max-listpack-value");
    }

    /** zsetTypeCreate() */
    private static RObj createZset(long sizeHint, long valLenHint) {
        int enc = (sizeHint <= maxListpackEntries() && valLenHint <= maxListpackValue())
                ? RObj.ENC_LISTPACK : RObj.ENC_SKIPLIST;
        return new RObj(RObj.ZSET, enc, new RZSet());
    }

    /** zsetTypeMaybeConvert() */
    private static void maybeConvert(RObj o, long sizeHint) {
        if (o.encoding == RObj.ENC_LISTPACK && sizeHint > maxListpackEntries())
            o.encoding = RObj.ENC_SKIPLIST;
    }

    /** zsetConvertToListpackIfNeeded() */
    private static void convertToListpackIfNeeded(RObj o, long maxelelen) {
        if (o.encoding == RObj.ENC_LISTPACK) return;
        if (zs(o).size() <= maxListpackEntries() && maxelelen <= maxListpackValue())
            o.encoding = RObj.ENC_LISTPACK;
    }

    /**
     * redis string2d(): like {@link Util#string2d} but also rejecting the
     * ERANGE overflow strtod reports for out-of-range numeric literals.
     */
    private static Double parseDouble(byte[] b) {
        Double d = Util.string2d(b);
        if (d == null) return null;
        if (d.isInfinite()) {
            String s = Util.s(b).toLowerCase();
            if (s.startsWith("+") || s.startsWith("-")) s = s.substring(1);
            if (!s.equals("inf") && !s.equals("infinity")) return null;
        }
        return d;
    }

    /** redis 2.8 d2string()/addReplyDouble(): "%.17g" with an integer fast path. */
    private static String d2string(double value) {
        return Util.d2str(value);
    }

    /** addReplyDouble(): a RESP3 double, or a d2string bulk on RESP2. */
    private static void addDouble(Client c, double d) {
        String s = d2string(d);
        if (c.resp >= 3) c.addRaw(Util.b("," + s + "\r\n"));
        else c.addBulk(Util.b(s));
    }

    /**
     * A sorted set stores its scores through d2string/strtod (listpack), which
     * normalises -0 to 0; keeping that here also makes -0 and 0 compare equal
     * the way the skiplist does.
     */
    private static void zsetInsert(RZSet z, byte[] member, double score) {
        z.add(member, score == 0.0 ? 0.0 : score);
    }

    /** strtod() as zslParseRange() uses it: leading spaces skipped, "" is 0. */
    private static Double strtodRange(byte[] b) {
        if (b.length == 0) return 0.0;
        int i = 0;
        while (i < b.length) {
            byte ch = b[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == 0x0b) i++;
            else break;
        }
        if (i == b.length) return null;
        return Util.string2d(i == 0 ? b : Arrays.copyOfRange(b, i, b.length));
    }

    private static double argScore(Client c, int idx) {
        Double d = parseDouble(c.arg(idx));
        if (d == null) throw RedisError.notFloat();
        return d;
    }

    /** getLongFromObjectOrReply() / getRangeLongFromObjectOrReply() */
    private static long rangeLong(Client c, int idx, long min, long max, String msg) {
        Long v = Util.string2ll(c.arg(idx));
        if (v == null)
            throw new RedisError(msg != null ? msg : "ERR value is not an integer or out of range");
        if (v < min || v > max) {
            if (msg != null) throw new RedisError(msg);
            throw new RedisError("ERR value is out of range, value must between " + min + " and " + max);
        }
        return v;
    }

    private static long argIndex(Client c, int idx) {
        Long v = Util.string2ll(c.arg(idx));
        if (v == null) throw RedisError.notInt();
        return v;
    }

    private static int cmpBytes(byte[] a, byte[] b) {
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) {
            int d = (a[i] & 0xff) - (b[i] & 0xff);
            if (d != 0) return d;
        }
        return a.length - b.length;
    }

    /* ------------------------------------------------------------------ */
    /* range specs                                                         */
    /* ------------------------------------------------------------------ */
    private static final class ScoreRange {
        double min, max;
        boolean minex, maxex;
    }

    /** zslParseRange(); returns null on a malformed spec. */
    private static ScoreRange parseScoreRange(byte[] minB, byte[] maxB) {
        ScoreRange r = new ScoreRange();
        Double v;
        if (minB.length > 0 && minB[0] == '(') {
            v = strtodRange(Arrays.copyOfRange(minB, 1, minB.length));
            r.minex = true;
        } else {
            v = strtodRange(minB);
        }
        if (v == null) return null;
        r.min = v;
        if (maxB.length > 0 && maxB[0] == '(') {
            v = strtodRange(Arrays.copyOfRange(maxB, 1, maxB.length));
            r.maxex = true;
        } else {
            v = strtodRange(maxB);
        }
        if (v == null) return null;
        r.max = v;
        return r;
    }

    private static boolean gteMin(double v, ScoreRange r) {
        return r.minex ? v > r.min : v >= r.min;
    }

    private static boolean lteMax(double v, ScoreRange r) {
        return r.maxex ? v < r.max : v <= r.max;
    }

    /** One side of a lex range: either a sentinel (-inf/+inf string) or a value. */
    private static final class LexItem {
        byte[] val;        // null when a sentinel
        boolean minInf, maxInf, ex;
    }

    private static final class LexRange {
        LexItem min, max;
    }

    /** zslParseLexRangeItem(); returns null on error. */
    private static LexItem parseLexItem(byte[] b) {
        if (b.length == 0) return null;
        LexItem it = new LexItem();
        switch (b[0]) {
            case '+':
                if (b.length != 1) return null;
                it.ex = true; it.maxInf = true; return it;
            case '-':
                if (b.length != 1) return null;
                it.ex = true; it.minInf = true; return it;
            case '(':
                it.ex = true; it.val = Arrays.copyOfRange(b, 1, b.length); return it;
            case '[':
                it.ex = false; it.val = Arrays.copyOfRange(b, 1, b.length); return it;
            default:
                return null;
        }
    }

    /** zslParseLexRange(); returns null on error. */
    private static LexRange parseLexRange(byte[] minB, byte[] maxB) {
        LexItem mn = parseLexItem(minB);
        LexItem mx = parseLexItem(maxB);
        if (mn == null || mx == null) return null;
        LexRange r = new LexRange();
        r.min = mn;
        r.max = mx;
        return r;
    }

    /** sdscmplex() with a concrete value on the left hand side. */
    private static int cmpLex(byte[] v, LexItem it) {
        if (it.minInf) return 1;
        if (it.maxInf) return -1;
        return cmpBytes(v, it.val);
    }

    private static boolean lexGteMin(byte[] v, LexRange r) {
        return r.min.ex ? cmpLex(v, r.min) > 0 : cmpLex(v, r.min) >= 0;
    }

    private static boolean lexLteMax(byte[] v, LexRange r) {
        return r.max.ex ? cmpLex(v, r.max) < 0 : cmpLex(v, r.max) <= 0;
    }

    /** All entries with a score inside the range, in ascending order. */
    private static List<RZSet.Entry> byScore(RZSet z, ScoreRange r) {
        List<RZSet.Entry> out = new ArrayList<>();
        if (z.sorted.isEmpty()) return out;
        for (RZSet.Entry e : z.sorted.tailSet(new RZSet.Entry(r.min, EMPTY_KEY), true)) {
            if (!gteMin(e.score, r)) continue;
            if (!lteMax(e.score, r)) break;
            out.add(e);
        }
        return out;
    }

    /** sdscmplex() between the two ends of a range. */
    private static int cmpLexItems(LexItem a, LexItem b) {
        if ((a.minInf && b.minInf) || (a.maxInf && b.maxInf)) return 0;
        if (a.minInf || b.maxInf) return -1;
        if (a.maxInf || b.minInf) return 1;
        return cmpBytes(a.val, b.val);
    }

    /** zslIsInLexRange(): is any part of the zset inside the range? */
    private static boolean isInLexRange(RZSet z, LexRange r) {
        int cmp = cmpLexItems(r.min, r.max);
        if (cmp > 0 || (cmp == 0 && (r.min.ex || r.max.ex))) return false;
        if (z.sorted.isEmpty()) return false;
        if (!lexGteMin(z.sorted.last().member.b, r)) return false;
        if (!lexLteMax(z.sorted.first().member.b, r)) return false;
        return true;
    }

    /** All entries inside the lex range, in ascending order. */
    private static List<RZSet.Entry> byLex(RZSet z, LexRange r) {
        if (!isInLexRange(z, r)) return new ArrayList<>();
        return byLexNoCheck(z, r);
    }

    /** As byLex() but without the zslIsInLexRange() pre-check (ZREMRANGEBYLEX). */
    private static List<RZSet.Entry> byLexNoCheck(RZSet z, LexRange r) {
        List<RZSet.Entry> out = new ArrayList<>();
        for (RZSet.Entry e : z.sorted) {
            byte[] v = e.member.b;
            if (!lexGteMin(v, r)) continue;
            if (!lexLteMax(v, r)) break;
            out.add(e);
        }
        return out;
    }

    /* ------------------------------------------------------------------ */
    /* zsetAdd                                                             */
    /* ------------------------------------------------------------------ */

    /**
     * Port of zsetAdd(): returns the output flags. A result carrying
     * {@link #OUT_NAN} means the C function returned 0 (failure).
     */
    private static int zsetAdd(RObj zobj, double score, byte[] ele, int inFlags, double[] newscore) {
        boolean incr = (inFlags & IN_INCR) != 0;
        boolean nx = (inFlags & IN_NX) != 0;
        boolean xx = (inFlags & IN_XX) != 0;
        boolean gt = (inFlags & IN_GT) != 0;
        boolean lt = (inFlags & IN_LT) != 0;

        if (Double.isNaN(score)) return OUT_NAN;

        RZSet z = zs(zobj);
        Double cur = z.score(ele);
        if (cur != null) {
            if (nx) return OUT_NOP;
            double curscore = cur;
            if (incr) {
                score += curscore;
                if (Double.isNaN(score)) return OUT_NAN;
            }
            if ((lt && score >= curscore) || (gt && score <= curscore)) return OUT_NOP;
            if (newscore != null) newscore[0] = score;
            if (score != curscore) {
                zsetInsert(z, ele, score);
                return OUT_UPDATED;
            }
            return 0;
        } else if (!xx) {
            if (zobj.encoding == RObj.ENC_LISTPACK
                    && (z.size() + 1 > maxListpackEntries() || ele.length > maxListpackValue())) {
                zobj.encoding = RObj.ENC_SKIPLIST;
            }
            zsetInsert(z, ele, score);
            if (newscore != null) newscore[0] = score;
            return OUT_ADDED;
        } else {
            return OUT_NOP;
        }
    }

    /* ------------------------------------------------------------------ */
    /* ZADD / ZINCRBY                                                      */
    /* ------------------------------------------------------------------ */
    private static void zaddGeneric(Client c, int flags) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        boolean ch = false;
        int scoreidx = 2;

        while (scoreidx < c.argc) {
            byte[] opt = c.arg(scoreidx);
            if (Util.equalsIgnoreCase(opt, "nx")) flags |= IN_NX;
            else if (Util.equalsIgnoreCase(opt, "xx")) flags |= IN_XX;
            else if (Util.equalsIgnoreCase(opt, "ch")) ch = true;
            else if (Util.equalsIgnoreCase(opt, "incr")) flags |= IN_INCR;
            else if (Util.equalsIgnoreCase(opt, "gt")) flags |= IN_GT;
            else if (Util.equalsIgnoreCase(opt, "lt")) flags |= IN_LT;
            else break;
            scoreidx++;
        }

        boolean incr = (flags & IN_INCR) != 0;
        boolean nx = (flags & IN_NX) != 0;
        boolean xx = (flags & IN_XX) != 0;
        boolean gt = (flags & IN_GT) != 0;
        boolean lt = (flags & IN_LT) != 0;

        int elements = c.argc - scoreidx;
        if (elements % 2 != 0 || elements == 0) throw RedisError.syntax();
        elements /= 2;

        if (nx && xx)
            throw new RedisError("ERR XX and NX options at the same time are not compatible");
        if ((gt && nx) || (lt && nx) || (gt && lt))
            throw new RedisError("ERR GT, LT, and/or NX options at the same time are not compatible");
        if (incr && elements > 1)
            throw new RedisError("ERR INCR option supports a single increment-element pair");

        double[] scores = new double[elements];
        for (int j = 0; j < elements; j++) scores[j] = argScore(c, scoreidx + j * 2);

        int added = 0, updated = 0, processed = 0;
        double score = 0;
        boolean nan = false;

        RObj zobj = s.lookupKeyWrite(c, key);
        if (zobj != null && zobj.type != RObj.ZSET) throw RedisError.wrongType();

        if (zobj == null) {
            if (!xx) {
                zobj = createZset(elements, c.arg(scoreidx + 1).length);
                s.dbAdd(c.db, key, zobj);
            }
        } else {
            maybeConvert(zobj, elements);
        }

        if (zobj != null) {
            double[] newscore = new double[1];
            for (int j = 0; j < elements; j++) {
                byte[] ele = c.arg(scoreidx + 1 + j * 2);
                int ret = zsetAdd(zobj, scores[j], ele, flags, newscore);
                if ((ret & OUT_NAN) != 0) { nan = true; break; }
                if ((ret & OUT_ADDED) != 0) added++;
                if ((ret & OUT_UPDATED) != 0) updated++;
                if ((ret & OUT_NOP) == 0) processed++;
                score = newscore[0];
            }
            s.dirty += added + updated;
        }

        if (added > 0 || updated > 0) {
            s.signalModifiedKey(c.db, new Key(key));
            s.notifyKeyspaceEvent('z', incr ? "zincr" : "zadd", key, c.db);
        }
        if (added > 0) s.signalKeyAsReady(c.db, key);

        if (nan) throw new RedisError(NAN_ERR);

        if (incr) {
            if (processed > 0) addDouble(c, score);
            else c.addNil();
        } else {
            c.addInt(ch ? added + updated : added);
        }
    }

    /* ------------------------------------------------------------------ */
    /* ZREM                                                                */
    /* ------------------------------------------------------------------ */
    private static void zrem(Client c) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        RObj zobj = s.lookupTypeWrite(c, key, RObj.ZSET);
        if (zobj == null) { c.addInt(0); return; }

        RZSet z = zs(zobj);
        int deleted = 0;
        boolean keyremoved = false;
        for (int j = 2; j < c.argc; j++) {
            if (z.remove(c.arg(j))) deleted++;
            if (z.size() == 0) {
                s.dbDelete(c.db, key);
                keyremoved = true;
                break;
            }
        }

        if (deleted > 0) {
            s.notifyKeyspaceEvent('z', "zrem", key, c.db);
            if (keyremoved) s.notifyKeyspaceEvent('g', "del", key, c.db);
            s.signalModifiedKey(c.db, new Key(key));
            s.dirty += deleted;
        }
        c.addInt(deleted);
    }

    /* ------------------------------------------------------------------ */
    /* ZSCORE / ZMSCORE / ZCARD                                            */
    /* ------------------------------------------------------------------ */
    private static void zscore(Client c) {
        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        if (zobj == null) { c.addNil(); return; }
        Double sc = zs(zobj).score(c.arg(2));
        if (sc == null) c.addNil();
        else addDouble(c, sc);
    }

    private static void zmscore(Client c) {
        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        c.addArray(c.argc - 2);
        for (int j = 2; j < c.argc; j++) {
            Double sc = zobj == null ? null : zs(zobj).score(c.arg(j));
            if (sc == null) c.addNil();
            else addDouble(c, sc);
        }
    }

    private static void zcard(Client c) {
        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        c.addInt(zobj == null ? 0 : zs(zobj).size());
    }

    /* ------------------------------------------------------------------ */
    /* ZCOUNT / ZLEXCOUNT                                                  */
    /* ------------------------------------------------------------------ */
    private static void zcount(Client c) {
        ScoreRange range = parseScoreRange(c.arg(2), c.arg(3));
        if (range == null) throw new RedisError(NOT_FLOAT_RANGE);
        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        if (zobj == null) { c.addInt(0); return; }
        c.addInt(byScore(zs(zobj), range).size());
    }

    private static void zlexcount(Client c) {
        LexRange range = parseLexRange(c.arg(2), c.arg(3));
        if (range == null) throw new RedisError(NOT_LEX_RANGE);
        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        if (zobj == null) { c.addInt(0); return; }
        c.addInt(byLex(zs(zobj), range).size());
    }

    /* ------------------------------------------------------------------ */
    /* ZRANK / ZREVRANK                                                    */
    /* ------------------------------------------------------------------ */
    private static void zrankGeneric(Client c, boolean reverse) {
        String name = reverse ? "zrevrank" : "zrank";
        if (c.argc > 4) throw RedisError.wrongArgs(name);
        boolean withscore = false;
        if (c.argc > 3) {
            if (c.argIs(3, "withscore")) withscore = true;
            else throw RedisError.syntax();
        }

        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        if (zobj == null) {
            if (withscore) c.addNilArray(); else c.addNil();
            return;
        }

        RZSet z = zs(zobj);
        byte[] ele = c.arg(2);
        Double sc = z.score(ele);
        if (sc == null) {
            if (withscore) c.addNilArray(); else c.addNil();
            return;
        }
        int rank = z.rank(ele, reverse);
        if (withscore) c.addArray(2);
        c.addInt(rank);
        if (withscore) addDouble(c, sc);
    }

    /* ------------------------------------------------------------------ */
    /* ZRANGE family                                                       */
    /* ------------------------------------------------------------------ */

    /** Accumulates the range result before it is emitted or stored. */
    private static final class RangeResult {
        final List<byte[]> members = new ArrayList<>();
        final List<Double> scores = new ArrayList<>();

        void add(byte[] m, double s) { members.add(m); scores.add(s); }
        int size() { return members.size(); }
    }

    private static void replyRange(Client c, RangeResult r, boolean withscores) {
        int n = r.size();
        if (withscores && c.resp == 2) c.addArray(n * 2);
        else c.addArray(n);
        for (int i = 0; i < n; i++) {
            if (withscores && c.resp > 2) c.addArray(2);
            c.addBulk(r.members.get(i));
            if (withscores) addDouble(c, r.scores.get(i));
        }
    }

    /** zrangeResultFinalizeStore(): store the result under dstkey. */
    private static void storeRange(Client c, byte[] dstkey, RangeResult r, long lengthHint, String event) {
        Server s = Server.I;
        if (r.size() > 0) {
            RObj o = createZset(Math.max(lengthHint, 0), 0);
            RZSet z = zs(o);
            for (int i = 0; i < r.size(); i++) {
                byte[] ele = r.members.get(i);
                if (o.encoding == RObj.ENC_LISTPACK
                        && (z.size() + 1 > maxListpackEntries() || ele.length > maxListpackValue()))
                    o.encoding = RObj.ENC_SKIPLIST;
                zsetInsert(z, ele, r.scores.get(i));
            }
            s.setKey(c, dstkey, o);
            s.signalKeyAsReady(c.db, dstkey);
            c.addInt(z.size());
            s.notifyKeyspaceEvent('z', event, dstkey, c.db);
            s.dirty++;
        } else {
            c.addInt(0);
            if (s.dbDelete(c.db, dstkey)) {
                s.notifyKeyspaceEvent('g', "del", dstkey, c.db);
                s.dirty++;
            }
        }
    }

    /**
     * zrangeGenericCommand(): ZRANGE / ZRANGESTORE plus the deprecated
     * Z[REV]RANGE[BYSCORE|BYLEX] variants.
     */
    private static void zrangeGeneric(Client c, int argcStart, boolean store, int rangetype, int direction) {
        Server s = Server.I;
        byte[] key = c.arg(argcStart);
        byte[] dstkey = store ? c.arg(1) : null;
        int minidx = argcStart + 1;
        int maxidx = argcStart + 2;

        long optStart = 0, optEnd = 0;
        boolean optWithscores = false;
        long optOffset = 0;
        long optLimit = -1;

        for (int j = argcStart + 3; j < c.argc; j++) {
            int leftargs = c.argc - j - 1;
            if (!store && c.argIs(j, "withscores")) {
                optWithscores = true;
            } else if (c.argIs(j, "limit") && leftargs >= 2) {
                optOffset = argIndex(c, j + 1);
                optLimit = argIndex(c, j + 2);
                j += 2;
            } else if (direction == DIR_AUTO && c.argIs(j, "rev")) {
                direction = DIR_REV;
            } else if (rangetype == RT_AUTO && c.argIs(j, "bylex")) {
                rangetype = RT_LEX;
            } else if (rangetype == RT_AUTO && c.argIs(j, "byscore")) {
                rangetype = RT_SCORE;
            } else {
                throw RedisError.syntax();
            }
        }

        if (direction == DIR_AUTO) direction = DIR_FWD;
        if (rangetype == RT_AUTO) rangetype = RT_RANK;
        boolean reverse = direction == DIR_REV;

        if (optLimit != -1 && rangetype == RT_RANK)
            throw new RedisError(
                "ERR syntax error, LIMIT is only supported in combination with either BYSCORE or BYLEX");
        if (optWithscores && rangetype == RT_LEX)
            throw new RedisError("ERR syntax error, WITHSCORES not supported in combination with BYLEX");

        if (reverse && (rangetype == RT_SCORE || rangetype == RT_LEX)) {
            int tmp = maxidx; maxidx = minidx; minidx = tmp;
        }

        ScoreRange srange = null;
        LexRange lrange = null;
        switch (rangetype) {
            case RT_RANK:
                optStart = argIndex(c, minidx);
                optEnd = argIndex(c, maxidx);
                break;
            case RT_SCORE:
                srange = parseScoreRange(c.arg(minidx), c.arg(maxidx));
                if (srange == null) throw new RedisError(NOT_FLOAT_RANGE);
                break;
            default:
                lrange = parseLexRange(c.arg(minidx), c.arg(maxidx));
                if (lrange == null) throw new RedisError(NOT_LEX_RANGE);
                break;
        }

        boolean emitScores = optWithscores || store;

        RObj zobj = s.lookupKeyRead(c, key);
        if (zobj == null) {
            if (store) storeRange(c, dstkey, new RangeResult(), -1, "zrangestore");
            else c.addEmptyArray();
            return;
        }
        if (zobj.type != RObj.ZSET) throw RedisError.wrongType();

        RZSet z = zs(zobj);
        RangeResult res = new RangeResult();
        long lengthHint = -1;

        if (rangetype == RT_RANK) {
            long llen = z.size();
            long start = optStart, end = optEnd;
            if (start < 0) start = llen + start;
            if (end < 0) end = llen + end;
            if (start < 0) start = 0;
            if (start > end || start >= llen) {
                lengthHint = 0;
            } else {
                if (end >= llen) end = llen - 1;
                lengthHint = end - start + 1;
                List<RZSet.Entry> all = z.all(reverse);
                for (long i = start; i <= end; i++) {
                    RZSet.Entry e = all.get((int) i);
                    res.add(e.member.b, e.score);
                }
            }
        } else {
            List<RZSet.Entry> in = rangetype == RT_SCORE ? byScore(z, srange) : byLex(z, lrange);
            if (reverse) Collections.reverse(in);
            long offset = optOffset;
            long limit = optLimit;
            if (offset >= 0 && limit != 0) {
                for (long i = offset; i < in.size(); i++) {
                    if (limit >= 0 && res.size() >= limit) break;
                    RZSet.Entry e = in.get((int) i);
                    res.add(e.member.b, e.score);
                }
            }
        }

        if (store) storeRange(c, dstkey, res, lengthHint, "zrangestore");
        else replyRange(c, res, emitScores);
    }

    /* ------------------------------------------------------------------ */
    /* ZREMRANGEBYRANK / BYSCORE / BYLEX                                   */
    /* ------------------------------------------------------------------ */
    private static void zremrangeGeneric(Client c, int rangetype) {
        Server s = Server.I;
        byte[] key = c.arg(1);
        String notify;
        ScoreRange srange = null;
        LexRange lrange = null;
        long start = 0, end = 0;

        if (rangetype == RT_RANK) {
            notify = "zremrangebyrank";
            start = argIndex(c, 2);
            end = argIndex(c, 3);
        } else if (rangetype == RT_SCORE) {
            notify = "zremrangebyscore";
            srange = parseScoreRange(c.arg(2), c.arg(3));
            if (srange == null) throw new RedisError(NOT_FLOAT_RANGE);
        } else {
            notify = "zremrangebylex";
            lrange = parseLexRange(c.arg(2), c.arg(3));
            if (lrange == null) throw new RedisError(NOT_LEX_RANGE);
        }

        RObj zobj = s.lookupTypeWrite(c, key, RObj.ZSET);
        if (zobj == null) { c.addInt(0); return; }
        RZSet z = zs(zobj);

        List<RZSet.Entry> victims;
        if (rangetype == RT_RANK) {
            long llen = z.size();
            if (start < 0) start = llen + start;
            if (end < 0) end = llen + end;
            if (start < 0) start = 0;
            if (start > end || start >= llen) { c.addInt(0); return; }
            if (end >= llen) end = llen - 1;
            List<RZSet.Entry> all = z.all(false);
            victims = new ArrayList<>(all.subList((int) start, (int) end + 1));
        } else if (rangetype == RT_SCORE) {
            victims = byScore(z, srange);
        } else {
            victims = byLexNoCheck(z, lrange);
        }

        int deleted = 0;
        for (RZSet.Entry e : victims) if (z.remove(e.member.b)) deleted++;

        boolean keyremoved = false;
        if (z.size() == 0) {
            s.dbDelete(c.db, key);
            keyremoved = true;
        }

        if (deleted > 0) {
            s.signalModifiedKey(c.db, new Key(key));
            s.notifyKeyspaceEvent('z', notify, key, c.db);
            if (keyremoved) s.notifyKeyspaceEvent('g', "del", key, c.db);
        }
        s.dirty += deleted;
        c.addInt(deleted);
    }

    /* ------------------------------------------------------------------ */
    /* ZUNIONSTORE / ZINTERSTORE / ZDIFFSTORE                              */
    /* ZUNION / ZINTER / ZDIFF / ZINTERCARD                                */
    /* ------------------------------------------------------------------ */
    private static final class Src {
        RObj o;
        double weight = 1.0;

        long length() {
            if (o == null) return 0;
            return o.type == RObj.SET ? ((RSet) o.ptr).size() : ((RZSet) o.ptr).size();
        }

        /** zuiFind(): the member's score, or null when absent. */
        Double find(byte[] m) {
            if (o == null) return null;
            if (o.type == RObj.SET) return ((RSet) o.ptr).contains(m) ? Double.valueOf(1.0) : null;
            return ((RZSet) o.ptr).score(m);
        }

        /** zuiNext(): every (member, score) pair of this source. */
        List<RZSet.Entry> entries() {
            if (o == null) return Collections.emptyList();
            List<RZSet.Entry> out = new ArrayList<>();
            if (o.type == RObj.SET) {
                for (Key k : ((RSet) o.ptr).set) out.add(new RZSet.Entry(1.0, k));
            } else {
                out.addAll(((RZSet) o.ptr).sorted);
            }
            return out;
        }
    }

    private static double aggregate(double target, double val, int agg) {
        if (agg == AGG_SUM) {
            target = target + val;
            if (Double.isNaN(target)) target = 0.0;
            return target;
        } else if (agg == AGG_MIN) {
            return val < target ? val : target;
        } else {
            return val > target ? val : target;
        }
    }

    private static void zunionInterDiffGeneric(Client c, byte[] dstkey, int numkeysIndex,
                                               int op, boolean cardinalityOnly) {
        Server s = Server.I;
        Cmd cmd = CommandTable.lookup(c.cmdName());
        String fullname = cmd == null ? c.cmdName() : cmd.fullName;

        Long sn = Util.string2ll(c.arg(numkeysIndex));
        if (sn == null) throw RedisError.notInt();
        long setnum = sn;
        if (setnum < 1)
            throw new RedisError("ERR at least 1 input key is needed for '" + fullname + "' command");
        if (setnum > c.argc - (numkeysIndex + 1)) throw RedisError.syntax();

        int n = (int) setnum;
        List<Src> src = new ArrayList<>(n);
        int j = numkeysIndex + 1;
        for (int i = 0; i < n; i++, j++) {
            Src sr = new Src();
            RObj obj = s.lookupKeyRead(c, c.arg(j));
            if (obj != null) {
                if (obj.type != RObj.ZSET && obj.type != RObj.SET) throw RedisError.wrongType();
                sr.o = obj;
            }
            src.add(sr);
        }

        int agg = AGG_SUM;
        boolean withscores = false;
        long limit = 0;

        if (j < c.argc) {
            int remaining = c.argc - j;
            while (remaining > 0) {
                if (op != OP_DIFF && !cardinalityOnly && remaining >= n + 1 && c.argIs(j, "weights")) {
                    j++; remaining--;
                    for (int i = 0; i < n; i++, j++, remaining--) {
                        Double w = parseDouble(c.arg(j));
                        if (w == null) throw new RedisError("ERR weight value is not a float");
                        src.get(i).weight = w;
                    }
                } else if (op != OP_DIFF && !cardinalityOnly && remaining >= 2 && c.argIs(j, "aggregate")) {
                    j++; remaining--;
                    if (c.argIs(j, "sum")) agg = AGG_SUM;
                    else if (c.argIs(j, "min")) agg = AGG_MIN;
                    else if (c.argIs(j, "max")) agg = AGG_MAX;
                    else throw RedisError.syntax();
                    j++; remaining--;
                } else if (remaining >= 1 && dstkey == null && !cardinalityOnly && c.argIs(j, "withscores")) {
                    j++; remaining--;
                    withscores = true;
                } else if (cardinalityOnly && remaining >= 2 && c.argIs(j, "limit")) {
                    j++; remaining--;
                    limit = rangeLong(c, j, 0, Long.MAX_VALUE, "ERR LIMIT can't be negative");
                    j++; remaining--;
                } else {
                    throw RedisError.syntax();
                }
            }
        }

        if (op != OP_DIFF) src.sort(Comparator.comparingLong(Src::length));

        /* The C implementation accumulates into a temporary skiplist zset; we
         * keep the raw aggregated scores here and sort with the skiplist's own
         * ordering (which, unlike Double.compare, treats -0 and 0 as equal). */
        LinkedHashMap<Key, double[]> acc = new LinkedHashMap<>();
        long cardinality = 0;
        long maxelelen = 0;

        if (op == OP_INTER) {
            if (src.get(0).length() > 0) {
                for (RZSet.Entry e : src.get(0).entries()) {
                    byte[] m = e.member.b;
                    double score = src.get(0).weight * e.score;
                    if (Double.isNaN(score)) score = 0;
                    int k;
                    for (k = 1; k < n; k++) {
                        Double v = src.get(k).find(m);
                        if (v == null) break;
                        score = aggregate(score, v * src.get(k).weight, agg);
                    }
                    if (k != n) continue;
                    if (cardinalityOnly) {
                        cardinality++;
                        if (limit != 0 && cardinality >= limit) break;
                    } else {
                        acc.put(e.member, new double[]{score});
                        if (m.length > maxelelen) maxelelen = m.length;
                    }
                }
            }
        } else if (op == OP_UNION) {
            for (int i = 0; i < n; i++) {
                Src sr = src.get(i);
                if (sr.length() == 0) continue;
                for (RZSet.Entry e : sr.entries()) {
                    double score = sr.weight * e.score;
                    if (Double.isNaN(score)) score = 0;
                    double[] slot = acc.get(e.member);
                    if (slot == null) {
                        acc.put(e.member, new double[]{score});
                        if (e.member.b.length > maxelelen) maxelelen = e.member.b.length;
                    } else {
                        slot[0] = aggregate(slot[0], score, agg);
                    }
                }
            }
        } else {
            /* SET_OP_DIFF: elements of the first set not present in any other. */
            if (src.get(0).length() > 0) {
                for (RZSet.Entry e : src.get(0).entries()) {
                    byte[] m = e.member.b;
                    boolean exists = false;
                    for (int k = 1; k < n; k++) {
                        if (src.get(k).o != null && src.get(k).o == src.get(0).o) { exists = true; break; }
                        if (src.get(k).find(m) != null) { exists = true; break; }
                    }
                    if (!exists) {
                        acc.put(e.member, new double[]{e.score});
                        if (m.length > maxelelen) maxelelen = m.length;
                    }
                }
            }
        }

        List<Map.Entry<Key, double[]>> ordered = new ArrayList<>(acc.entrySet());
        ordered.sort((x, y) -> {
            double a = x.getValue()[0], b = y.getValue()[0];
            if (a < b) return -1;
            if (a > b) return 1;
            return x.getKey().compareTo(y.getKey());
        });

        if (dstkey != null) {
            String event = op == OP_UNION ? "zunionstore" : (op == OP_INTER ? "zinterstore" : "zdiffstore");
            if (!ordered.isEmpty()) {
                RZSet dst = new RZSet();
                for (Map.Entry<Key, double[]> en : ordered) zsetInsert(dst, en.getKey().b, en.getValue()[0]);
                RObj o = new RObj(RObj.ZSET, RObj.ENC_SKIPLIST, dst);
                convertToListpackIfNeeded(o, maxelelen);
                s.setKey(c, dstkey, o);
                s.signalKeyAsReady(c.db, dstkey);
                c.addInt(dst.size());
                s.notifyKeyspaceEvent('z', event, dstkey, c.db);
                s.dirty++;
            } else {
                c.addInt(0);
                if (s.dbDelete(c.db, dstkey)) {
                    s.notifyKeyspaceEvent('g', "del", dstkey, c.db);
                    s.dirty++;
                }
            }
        } else if (cardinalityOnly) {
            c.addInt(cardinality);
        } else {
            RangeResult r = new RangeResult();
            for (Map.Entry<Key, double[]> en : ordered) r.add(en.getKey().b, en.getValue()[0]);
            replyRange(c, r, withscores);
        }
    }

    /* ------------------------------------------------------------------ */
    /* ZPOPMIN / ZPOPMAX / BZPOPMIN / BZPOPMAX / ZMPOP / BZMPOP            */
    /* ------------------------------------------------------------------ */

    /**
     * genericZpopCommand(). Returns the equivalent ZPOP[MIN|MAX] command for
     * replication, or null when nothing was popped.
     */
    private static byte[][] genericZpop(Client c, List<byte[]> keys, boolean max, boolean emitkey,
                                        long count, boolean useNested, boolean nilWhenEmpty,
                                        boolean propCount) {
        Server s = Server.I;
        byte[] key = null;
        RObj zobj = null;
        for (byte[] k : keys) {
            key = k;
            zobj = s.lookupKeyWrite(c, k);
            if (zobj == null) continue;
            if (zobj.type != RObj.ZSET) throw RedisError.wrongType();
            break;
        }

        if (zobj == null) {
            if (nilWhenEmpty) c.addNilArray();
            else c.addEmptyArray();
            return null;
        }
        if (count == 0) { c.addEmptyArray(); return null; }
        if (count == -1) count = 1;

        RZSet z = zs(zobj);
        long llen = z.size();
        long rangelen = Math.min(count, llen);

        if (!useNested && !emitkey) {
            c.addArray((int) (rangelen * 2));
        } else if (useNested && !emitkey) {
            c.addArray((int) rangelen);
        } else if (!useNested) {
            c.addArray((int) (rangelen * 2 + 1));
            c.addBulk(key);
        } else {
            c.addArray(2);
            c.addBulk(key);
            c.addArray((int) rangelen);
        }

        long popped = 0;
        while (popped < rangelen) {
            RZSet.Entry e = max ? z.sorted.last() : z.sorted.first();
            byte[] ele = e.member.b;
            double score = e.score;
            z.remove(ele);
            s.dirty++;
            if (popped == 0) s.notifyKeyspaceEvent('z', max ? "zpopmax" : "zpopmin", key, c.db);
            if (useNested) c.addArray(2);
            c.addBulk(ele);
            addDouble(c, score);
            popped++;
        }

        if (z.size() == 0) {
            s.dbDelete(c.db, key);
            s.notifyKeyspaceEvent('g', "del", key, c.db);
        }
        s.signalModifiedKey(c.db, new Key(key));

        byte[] name = Util.b(max ? "ZPOPMAX" : "ZPOPMIN");
        if (propCount) return new byte[][]{name, key, Util.ll2string(rangelen)};
        return new byte[][]{name, key};
    }

    private static void zpopMinMax(Client c, boolean max) {
        if (c.argc > 3) throw RedisError.syntax();
        long count = -1;
        if (c.argc == 3) count = positiveCount(c, 2);
        boolean useNested = c.resp > 2 && count != -1;
        genericZpop(c, Collections.singletonList(c.arg(1)), max, false, count, useNested, false, false);
    }

    /** getPositiveLongFromObjectOrReply() with the default message. */
    private static long positiveCount(Client c, int idx) {
        return rangeLong(c, idx, 0, Long.MAX_VALUE, "ERR value is out of range, must be positive");
    }

    /** State kept while a client is blocked in BZPOPMIN/BZPOPMAX/BZMPOP. */
    private static final class BState {
        final List<byte[]> keys;
        final boolean max;
        final long count;
        final boolean useNested;
        final boolean nilWhenEmpty;
        final boolean propCount;

        BState(List<byte[]> keys, boolean max, long count, boolean useNested,
               boolean nilWhenEmpty, boolean propCount) {
            this.keys = keys; this.max = max; this.count = count;
            this.useNested = useNested; this.nilWhenEmpty = nilWhenEmpty; this.propCount = propCount;
        }
    }

    private static boolean denyBlocking(Client c) {
        return c.fake || Server.I.execDepth > 0;
    }

    /** blockingGenericZpopCommand(). */
    private static void blockingZpop(Client c, List<byte[]> keys, boolean max, int timeoutIdx,
                                     long count, boolean useNested, boolean nilWhenEmpty) {
        Server s = Server.I;
        double timeout = Blocking.parseTimeout(c, timeoutIdx, true);

        for (byte[] k : keys) {
            RObj o = s.lookupKeyWrite(c, k);
            if (o == null) continue;
            if (o.type != RObj.ZSET) throw RedisError.wrongType();
            if (zs(o).size() == 0) continue;
            byte[][] prop = genericZpop(c, Collections.singletonList(k), max, true, count,
                    useNested, nilWhenEmpty, count != -1);
            if (prop != null) s.replOverride = prop;
            return;
        }

        if (denyBlocking(c)) { c.addNilArray(); return; }

        BState st = new BState(keys, max, count, useNested, nilWhenEmpty, count != -1);
        Blocking.blockOnKeys(s, c, keys, timeout, "zset", st);
    }

    private static void bzpopMinMax(Client c, boolean max) {
        List<byte[]> keys = new ArrayList<>();
        for (int j = 1; j < c.argc - 1; j++) keys.add(c.arg(j));
        blockingZpop(c, keys, max, c.argc - 1, -1, false, false);
    }

    private static void zmpopGeneric(Client c, int numkeysIdx, boolean isBlock) {
        long numkeys = rangeLong(c, numkeysIdx, 1, Long.MAX_VALUE, "ERR numkeys should be greater than 0");
        if (numkeys > c.argc) throw RedisError.syntax();
        long whereIdx = numkeysIdx + numkeys + 1;
        if (whereIdx >= c.argc) throw RedisError.syntax();

        boolean max;
        if (c.argIs((int) whereIdx, "MIN")) max = false;
        else if (c.argIs((int) whereIdx, "MAX")) max = true;
        else throw RedisError.syntax();

        long count = -1;
        for (int j = (int) whereIdx + 1; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (count == -1 && c.argIs(j, "COUNT") && moreargs > 0) {
                j++;
                count = rangeLong(c, j, 1, Long.MAX_VALUE, "ERR count should be greater than 0");
            } else {
                throw RedisError.syntax();
            }
        }
        if (count == -1) count = 1;

        List<byte[]> keys = new ArrayList<>();
        for (int i = 0; i < numkeys; i++) keys.add(c.arg(numkeysIdx + 1 + i));

        if (isBlock) {
            blockingZpop(c, keys, max, 1, count, true, true);
        } else {
            byte[][] prop = genericZpop(c, keys, max, true, count, true, true, true);
            if (prop != null) Server.I.replOverride = prop;
        }
    }

    /** Blocking.READY handler: a key this client waits on received data. */
    private static boolean serveBlocked(Server s, Client c, Key ready) {
        if (!(c.blockState instanceof BState)) return false;
        BState st = (BState) c.blockState;

        for (byte[] k : st.keys) {
            RObj o = s.lookupKey(c.db, k, false);
            if (o == null || o.type != RObj.ZSET || zs(o).size() == 0) continue;
            byte[][] prop = genericZpop(c, Collections.singletonList(k), st.max, true, st.count,
                    st.useNested, st.nilWhenEmpty, st.propCount);
            if (prop != null) s.propagateExtra(c.db, prop);
            return true;
        }
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* ZRANDMEMBER                                                         */
    /* ------------------------------------------------------------------ */
    private static void zrandmember(Client c) {
        if (c.argc >= 3) {
            long l = rangeLong(c, 2, -Long.MAX_VALUE, Long.MAX_VALUE, null);
            boolean withscores = false;
            if (c.argc > 4 || (c.argc == 4 && !c.argIs(3, "withscores"))) {
                throw RedisError.syntax();
            } else if (c.argc == 4) {
                withscores = true;
                if (l < -Long.MAX_VALUE / 2 || l > Long.MAX_VALUE / 2)
                    throw new RedisError("ERR value is out of range");
            }
            zrandmemberWithCount(c, l, withscores);
            return;
        }

        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        if (zobj == null) { c.addNil(); return; }
        List<RZSet.Entry> all = zs(zobj).all(false);
        c.addBulk(all.get(RND.nextInt(all.size())).member.b);
    }

    private static void zrandmemberWithCount(Client c, long l, boolean withscores) {
        RObj zobj = Server.I.lookupType(c, c.arg(1), RObj.ZSET);
        if (zobj == null) { c.addEmptyArray(); return; }
        RZSet z = zs(zobj);
        int size = z.size();

        long count;
        boolean uniq;
        if (l >= 0) { count = l; uniq = true; }
        else { count = -l; uniq = false; }

        if (count == 0) { c.addEmptyArray(); return; }

        List<RZSet.Entry> all = z.all(false);

        if (!uniq || count == 1) {
            int n = (int) Math.min(count, Integer.MAX_VALUE);
            if (withscores && c.resp == 2) c.addArray(n * 2);
            else c.addArray(n);
            for (int i = 0; i < n; i++) {
                RZSet.Entry e = all.get(RND.nextInt(size));
                if (withscores && c.resp > 2) c.addArray(2);
                c.addBulk(e.member.b);
                if (withscores) addDouble(c, e.score);
            }
            return;
        }

        int replySize = (int) Math.min(count, size);
        if (withscores && c.resp == 2) c.addArray(replySize * 2);
        else c.addArray(replySize);

        if (count >= size) {
            /* Whole zset, in the reverse iteration order redis uses. */
            for (int i = size - 1; i >= 0; i--) {
                RZSet.Entry e = all.get(i);
                if (withscores && c.resp > 2) c.addArray(2);
                c.addBulk(e.member.b);
                if (withscores) addDouble(c, e.score);
            }
            return;
        }

        /* Unique sub-sample: partial Fisher-Yates over the element indexes. */
        int[] idx = new int[size];
        for (int i = 0; i < size; i++) idx[i] = i;
        for (int i = 0; i < replySize; i++) {
            int r = i + RND.nextInt(size - i);
            int t = idx[i]; idx[i] = idx[r]; idx[r] = t;
            RZSet.Entry e = all.get(idx[i]);
            if (withscores && c.resp > 2) c.addArray(2);
            c.addBulk(e.member.b);
            if (withscores) addDouble(c, e.score);
        }
    }
}
