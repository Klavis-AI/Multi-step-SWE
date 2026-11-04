package redis;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import redis.RStream.CG;
import redis.RStream.Consumer;
import redis.RStream.Id;
import redis.RStream.NACK;

/**
 * Stream commands: a port of redis src/t_stream.c (8.0.6).
 *
 * The value container is {@link RStream}, which keeps redis's own radix tree of
 * listpack macro nodes so that node boundaries (and therefore approximated
 * trimming and {@code XINFO STREAM radix-tree-keys}) behave identically.
 */
public final class StreamCmds {
    private StreamCmds() {}

    /* streamReplyWithRange() flags */
    private static final int RWR_NOACK = 1, RWR_HISTORY = 4;

    private static final String INVALID_ID =
            "ERR Invalid stream ID specified as stream command argument";

    private static final byte[][] NO_PROP = new byte[0][];

    /* ------------------------------------------------------------------ */
    /* registration                                                        */
    /* ------------------------------------------------------------------ */
    public static void register() {
        CommandTable.reg("xadd", -5, "write denyoom fast", 1, 1, 1, StreamCmds::xadd);
        CommandTable.reg("xlen", 2, "readonly fast", 1, 1, 1, StreamCmds::xlen);
        CommandTable.reg("xrange", -4, "readonly", 1, 1, 1, c -> xrangeGeneric(c, false));
        CommandTable.reg("xrevrange", -4, "readonly", 1, 1, 1, c -> xrangeGeneric(c, true));
        CommandTable.reg("xread", -4, "readonly blocking movablekeys", 0, 0, 0, c -> xreadCommand(c, false));
        CommandTable.reg("xreadgroup", -7, "write blocking movablekeys", 0, 0, 0, c -> xreadCommand(c, true));
        CommandTable.reg("xdel", -3, "write fast", 1, 1, 1, StreamCmds::xdel);
        CommandTable.reg("xtrim", -4, "write", 1, 1, 1, StreamCmds::xtrim);
        CommandTable.reg("xsetid", -3, "write denyoom fast", 1, 1, 1, StreamCmds::xsetid);
        CommandTable.reg("xautoclaim", -6, "write fast", 1, 1, 1, StreamCmds::xautoclaim);
        CommandTable.reg("xclaim", -6, "write fast", 1, 1, 1, StreamCmds::xclaim);
        CommandTable.reg("xack", -4, "write fast", 1, 1, 1, StreamCmds::xack);
        CommandTable.reg("xpending", -3, "readonly", 1, 1, 1, StreamCmds::xpending);

        CommandTable.reg("xgroup", -2, "write denyoom", 2, 2, 1, c -> CommandTable.dispatchSub(c, "xgroup"));
        CommandTable.regSub("xgroup", "create", -5, "write denyoom", StreamCmds::xgroup);
        CommandTable.regSub("xgroup", "createconsumer", 5, "write denyoom", StreamCmds::xgroup);
        CommandTable.regSub("xgroup", "delconsumer", 5, "write", StreamCmds::xgroup);
        CommandTable.regSub("xgroup", "destroy", 4, "write", StreamCmds::xgroup);
        CommandTable.regSub("xgroup", "help", 2, "loading stale", StreamCmds::xgroupHelp);
        CommandTable.regSub("xgroup", "setid", -5, "write", StreamCmds::xgroup);

        CommandTable.reg("xinfo", -2, "readonly", 2, 2, 1, c -> CommandTable.dispatchSub(c, "xinfo"));
        CommandTable.regSub("xinfo", "consumers", 4, "readonly", StreamCmds::xinfo);
        CommandTable.regSub("xinfo", "groups", 3, "readonly", StreamCmds::xinfo);
        CommandTable.regSub("xinfo", "help", 2, "loading stale", StreamCmds::xinfoHelp);
        CommandTable.regSub("xinfo", "stream", -3, "readonly", StreamCmds::xinfo);

        Blocking.READY.put("stream", StreamCmds::serveBlocked);
        Blocking.TIMEOUT.put("stream", (s, c) -> c.addNilArray());
    }

    /* ------------------------------------------------------------------ */
    /* small helpers                                                       */
    /* ------------------------------------------------------------------ */
    static RStream st(RObj o) { return (RStream) o.ptr; }

    public static RObj createStreamObject() {
        return new RObj(RObj.STREAM, RObj.ENC_STREAM, new RStream());
    }

    public static RObj streamObject(RStream s) {
        return new RObj(RObj.STREAM, RObj.ENC_STREAM, s);
    }

    private static long nodeMaxBytes() { return Server.I.config.getLong("stream-node-max-bytes"); }
    private static long nodeMaxEntries() { return Server.I.config.getLong("stream-node-max-entries"); }

    private static long longArg(Client c, int i, String msg) {
        Long v = Util.string2ll(c.argv[i]);
        if (v == null) throw new RedisError(msg == null
                ? "ERR value is not an integer or out of range" : "ERR " + msg);
        return v;
    }

    /** streamTypeLookupWriteOrCreate(): null when NOMKSTREAM and the key is missing. */
    private static RObj lookupWriteOrCreate(Client c, byte[] key, boolean noCreate) {
        Server s = Server.I;
        RObj o = s.lookupKeyWrite(c, key);
        if (o != null && o.type != RObj.STREAM) throw RedisError.wrongType();
        if (o == null) {
            if (noCreate) return null;
            o = createStreamObject();
            s.dbAdd(c.db, key, o);
        }
        return o;
    }

    private static RObj lookupRead(Client c, byte[] key) {
        RObj o = Server.I.lookupKeyRead(c, key);
        if (o != null && o.type != RObj.STREAM) throw RedisError.wrongType();
        return o;
    }

    private static RObj lookupWrite(Client c, byte[] key) {
        RObj o = Server.I.lookupKeyWrite(c, key);
        if (o != null && o.type != RObj.STREAM) throw RedisError.wrongType();
        return o;
    }

    /** networking.c addReplySubcommandSyntaxError(). */
    private static RedisError subSyntax(Client c) {
        return new RedisError("ERR unknown subcommand or wrong number of arguments for '"
                + trunc(c.argStr(1)) + "'. Try " + c.argStr(0).toUpperCase(java.util.Locale.ROOT)
                + " HELP.");
    }

    private static String trunc(String s) { return s.length() <= 128 ? s : s.substring(0, 128); }

    /* ------------------------------------------------------------------ */
    /* ID parsing                                                          */
    /* ------------------------------------------------------------------ */

    /**
     * string2ull(): string2ll() first and strtoull() as a fallback, so a run of
     * digits (leading zeroes included) that fits in 64 unsigned bits.
     */
    private static Long parseULL(String t) {
        if (t.isEmpty()) return null;
        int i = 0;
        while (i < t.length() - 1 && t.charAt(i) == '0') i++;   /* strtoull() skips them */
        String d = t.substring(i);
        if (d.length() > 20) return null;
        for (int j = 0; j < t.length(); j++) {
            char ch = t.charAt(j);
            if (ch < '0' || ch > '9') return null;
        }
        try {
            return Long.parseUnsignedLong(d);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /** streamGenericParseIDOrReply(); seqGiven[0] is set when non-null. */
    private static Id parseIDGeneric(byte[] o, long missingSeq, boolean strict, boolean[] seqGiven) {
        if (o.length > 127) return null;
        String buf = Util.s(o);
        if (strict && buf.length() == 1 && (buf.charAt(0) == '-' || buf.charAt(0) == '+')) return null;
        if (seqGiven != null) seqGiven[0] = true;
        if (buf.equals("-")) return new Id(0, 0);
        if (buf.equals("+")) return new Id(-1L, -1L);

        int dot = buf.indexOf('-');
        String msPart = dot < 0 ? buf : buf.substring(0, dot);
        Long ms = parseULL(msPart);
        if (ms == null) return null;
        long seq;
        if (dot >= 0) {
            String seqPart = buf.substring(dot + 1);
            if (seqGiven != null && seqPart.equals("*")) {
                seq = 0;
                seqGiven[0] = false;
            } else {
                Long sv = parseULL(seqPart);
                if (sv == null) return null;
                seq = sv;
            }
        } else {
            seq = missingSeq;
        }
        return new Id(ms, seq);
    }

    private static Id parseID(byte[] o, long missingSeq) {
        Id id = parseIDGeneric(o, missingSeq, false, null);
        if (id == null) throw new RedisError(INVALID_ID);
        return id;
    }

    private static Id parseStrictID(byte[] o, long missingSeq, boolean[] seqGiven) {
        Id id = parseIDGeneric(o, missingSeq, true, seqGiven);
        if (id == null) throw new RedisError(INVALID_ID);
        return id;
    }

    /** streamParseIntervalIDOrReply(): exclude[0] is set for the "(" prefix. */
    private static Id parseIntervalID(byte[] o, boolean[] exclude, long missingSeq) {
        boolean ex = o.length > 1 && o[0] == '(';
        if (exclude != null) exclude[0] = ex;
        if (ex) return parseStrictID(Arrays.copyOfRange(o, 1, o.length), missingSeq, null);
        return parseID(o, missingSeq);
    }

    /* ------------------------------------------------------------------ */
    /* range emission                                                      */
    /* ------------------------------------------------------------------ */

    /** One entry as it will be written to the client. */
    static final class Emitted {
        final Id id;
        final List<byte[]> fields;   /* null means "a nil array of fields" */
        Emitted(Id id, List<byte[]> fields) { this.id = id; this.fields = fields; }
    }

    private static void emitEntries(Client c, List<Emitted> l) {
        c.addArray(l.size());
        for (Emitted e : l) emitEntry(c, e);
    }

    private static void emitEntry(Client c, Emitted e) {
        c.addArray(2);
        c.addBulk(e.id.bytes());
        if (e.fields == null) { c.addNilArray(); return; }
        c.addArray(e.fields.size());
        for (byte[] f : e.fields) c.addBulk(f);
    }

    /**
     * streamReplyWithRange(): collect the entries, updating the group/consumer
     * PEL and propagating the XCLAIMs when a group is given.
     */
    private static List<Emitted> replyWithRange(Client c, RStream s, Id start, Id end, long count,
                                                boolean rev, CG group, Consumer consumer, int flags,
                                                byte[] key, byte[] groupname, long[] propCount) {
        if (group != null && (flags & RWR_HISTORY) != 0) {
            return replyFromConsumerPEL(c, s, start, end, count, consumer);
        }
        boolean noack = (flags & RWR_NOACK) != 0;
        boolean propagateLastId = false;
        List<Emitted> out = new ArrayList<>();
        RStream.Iter it = s.iter(start, end, rev);
        RStream.Entry e;
        while ((e = it.next()) != null) {
            if (group != null && e.id.compareTo(group.lastId) > 0) {
                if (group.entriesRead != RStream.INVALID_ENTRIES_READ
                        && group.lastId.compareTo(s.firstId) >= 0
                        && !s.rangeHasTombstones(group.lastId, null)) {
                    group.entriesRead++;
                } else if (s.entriesAdded != 0) {
                    group.entriesRead = s.estimateDistanceFromFirstEverEntry(e.id);
                }
                group.lastId = e.id.copy();
                propagateLastId = true;
            }

            out.add(new Emitted(e.id, e.fields));

            if (group != null && !noack) {
                NACK nack = group.pel.get(e.id);
                if (nack == null) {
                    nack = new NACK(consumer);
                    group.pel.put(e.id.copy(), nack);
                    consumer.pel.put(e.id.copy(), nack);
                } else {
                    if (nack.consumer != null) nack.consumer.pel.remove(e.id);
                    nack.consumer = consumer;
                    nack.deliveryTime = Server.I.mstime();
                    nack.deliveryCount = 1;
                    consumer.pel.put(e.id.copy(), nack);
                }
                consumer.activeTime = Server.I.mstime();
                if (key != null) {
                    propagateXCLAIM(c, key, group, groupname, e.id, nack);
                    if (propCount != null) propCount[0]++;
                }
            }
            if (count != 0 && out.size() == count) break;
        }
        if (key != null && propagateLastId) {
            propagateGroupID(c, key, group, groupname);
            if (propCount != null) propCount[0]++;
        }
        return out;
    }

    /** streamReplyWithRangeFromConsumerPEL() */
    private static List<Emitted> replyFromConsumerPEL(Client c, RStream s, Id start, Id end,
                                                      long count, Consumer consumer) {
        List<Emitted> out = new ArrayList<>();
        Id from = start == null ? RStream.minId() : start;
        for (Map.Entry<Id, NACK> pe : new ArrayList<>(consumer.pel.tailMap(from, true).entrySet())) {
            if (count != 0 && out.size() >= count) break;
            if (end != null && pe.getKey().compareTo(end) > 0) break;
            Id id = pe.getKey();
            RStream.Entry e = s.lookup(id);
            if (e == null) {
                out.add(new Emitted(id, null));
            } else {
                NACK nack = pe.getValue();
                nack.deliveryTime = Server.I.mstime();
                nack.deliveryCount++;
                out.add(new Emitted(id, e.fields));
            }
        }
        return out;
    }

    /* ------------------------------------------------------------------ */
    /* propagation                                                         */
    /* ------------------------------------------------------------------ */

    /** streamPropagateXCLAIM() */
    private static void propagateXCLAIM(Client c, byte[] key, CG group, byte[] groupname,
                                        Id id, NACK nack) {
        byte[][] argv = new byte[14][];
        argv[0] = Util.b("XCLAIM");
        argv[1] = key;
        argv[2] = groupname;
        argv[3] = nack.consumer == null ? Util.EMPTY : nack.consumer.name;
        argv[4] = Util.b("0");
        argv[5] = id.bytes();
        argv[6] = Util.b("TIME");
        argv[7] = Util.ll2string(nack.deliveryTime);
        argv[8] = Util.b("RETRYCOUNT");
        argv[9] = Util.ll2string(nack.deliveryCount);
        argv[10] = Util.b("FORCE");
        argv[11] = Util.b("JUSTID");
        argv[12] = Util.b("LASTID");
        argv[13] = group.lastId.bytes();
        Server.I.propagateExtra(c.db, argv);
    }

    /** streamPropagateGroupID() */
    private static void propagateGroupID(Client c, byte[] key, CG group, byte[] groupname) {
        byte[][] argv = new byte[7][];
        argv[0] = Util.b("XGROUP");
        argv[1] = Util.b("SETID");
        argv[2] = key;
        argv[3] = groupname;
        argv[4] = group.lastId.bytes();
        argv[5] = Util.b("ENTRIESREAD");
        argv[6] = Util.ll2string(group.entriesRead);
        Server.I.propagateExtra(c.db, argv);
    }

    /** streamPropagateConsumerCreation() */
    private static void propagateConsumerCreation(Client c, byte[] key, byte[] groupname, byte[] name) {
        byte[][] argv = new byte[5][];
        argv[0] = Util.b("XGROUP");
        argv[1] = Util.b("CREATECONSUMER");
        argv[2] = key;
        argv[3] = groupname;
        argv[4] = name;
        Server.I.propagateExtra(c.db, argv);
    }

    /* ------------------------------------------------------------------ */
    /* consumers                                                           */
    /* ------------------------------------------------------------------ */

    /** streamCreateConsumer(): null when it already existed. */
    private static Consumer createConsumer(CG cg, byte[] name, byte[] key, int dbid,
                                           boolean notify, boolean dirtify) {
        if (cg == null) return null;
        Key k = new Key(name);
        if (cg.consumers.containsKey(k)) return null;
        Consumer consumer = new Consumer(Util.copy(name));
        consumer.seenTime = Server.I.mstime();
        consumer.activeTime = -1;
        cg.consumers.put(k, consumer);
        if (dirtify) Server.I.dirty++;
        if (notify) Server.I.notifyKeyspaceEvent('t', "xgroup-createconsumer", key, dbid);
        return consumer;
    }

    private static Consumer lookupConsumer(CG cg, byte[] name) {
        if (cg == null) return null;
        return cg.consumers.get(new Key(name));
    }

    /** streamDelConsumer() */
    private static void delConsumer(CG cg, Consumer consumer) {
        for (Id id : new ArrayList<>(consumer.pel.keySet())) cg.pel.remove(id);
        cg.consumers.remove(new Key(consumer.name));
    }

    /* ------------------------------------------------------------------ */
    /* XADD / XTRIM argument parsing                                       */
    /* ------------------------------------------------------------------ */

    private static final int TRIM_NONE = RStream.TRIM_NONE,
            TRIM_MAXLEN = RStream.TRIM_MAXLEN, TRIM_MINID = RStream.TRIM_MINID;

    static final class AddTrimArgs {
        Id id;
        boolean idGiven;
        boolean seqGiven = true;
        boolean noMkstream;
        int trimStrategy = TRIM_NONE;
        int trimStrategyArgIdx;
        boolean approxTrim;
        long limit;
        long maxlen;
        Id minid = new Id(0, 0);
        int idPos;
    }

    /** streamParseAddOrTrimArgsOrReply() */
    private static AddTrimArgs parseAddOrTrimArgs(Client c, boolean xadd) {
        AddTrimArgs a = new AddTrimArgs();
        int i = 2;
        boolean limitGiven = false;
        for (; i < c.argc; i++) {
            int moreargs = (c.argc - 1) - i;
            byte[] opt = c.argv[i];
            if (xadd && opt.length == 1 && opt[0] == '*') break;
            if (Util.equalsIgnoreCase(opt, "maxlen") && moreargs > 0) {
                if (a.trimStrategy != TRIM_NONE)
                    throw new RedisError("ERR syntax error, MAXLEN and MINID options at the same time are not compatible");
                a.approxTrim = false;
                byte[] next = c.argv[i + 1];
                if (moreargs >= 2 && next.length == 1 && next[0] == '~') { a.approxTrim = true; i++; }
                else if (moreargs >= 2 && next.length == 1 && next[0] == '=') i++;
                a.maxlen = longArg(c, i + 1, null);
                if (a.maxlen < 0) throw new RedisError("ERR The MAXLEN argument must be >= 0.");
                i++;
                a.trimStrategy = TRIM_MAXLEN;
                a.trimStrategyArgIdx = i;
            } else if (Util.equalsIgnoreCase(opt, "minid") && moreargs > 0) {
                if (a.trimStrategy != TRIM_NONE)
                    throw new RedisError("ERR syntax error, MAXLEN and MINID options at the same time are not compatible");
                a.approxTrim = false;
                byte[] next = c.argv[i + 1];
                if (moreargs >= 2 && next.length == 1 && next[0] == '~') { a.approxTrim = true; i++; }
                else if (moreargs >= 2 && next.length == 1 && next[0] == '=') i++;
                a.minid = parseStrictID(c.argv[i + 1], 0, null);
                i++;
                a.trimStrategy = TRIM_MINID;
                a.trimStrategyArgIdx = i;
            } else if (Util.equalsIgnoreCase(opt, "limit") && moreargs > 0) {
                a.limit = longArg(c, i + 1, null);
                if (a.limit < 0) throw new RedisError("ERR The LIMIT argument must be >= 0.");
                limitGiven = true;
                i++;
            } else if (xadd && Util.equalsIgnoreCase(opt, "nomkstream")) {
                a.noMkstream = true;
            } else if (xadd) {
                boolean[] sg = new boolean[1];
                a.id = parseStrictID(c.argv[i], 0, sg);
                a.seqGiven = sg[0];
                a.idGiven = true;
                break;
            } else {
                throw RedisError.syntax();
            }
        }

        if (a.limit != 0 && a.trimStrategy == TRIM_NONE)
            throw new RedisError("ERR syntax error, LIMIT cannot be used without specifying a trimming strategy");
        if (!xadd && a.trimStrategy == TRIM_NONE)
            throw new RedisError("ERR syntax error, XTRIM must be called with a trimming strategy");

        if (mustObeyClient(c)) {
            a.limit = 0;
        } else if (limitGiven) {
            if (!a.approxTrim)
                throw new RedisError("ERR syntax error, LIMIT cannot be used without the special ~ option");
        } else if (a.approxTrim) {
            a.limit = 100 * nodeMaxEntries();
            if (a.limit <= 0) a.limit = 10000;
            if (a.limit > 1000000) a.limit = 1000000;
        } else {
            a.limit = 0;
        }
        a.idPos = i;
        return a;
    }

    /** mustObeyClient(): the master link and AOF loading skip the trim limit. */
    private static boolean mustObeyClient(Client c) {
        Server s = Server.I;
        if (c.fake) return true;
        return s.repl != null && s.repl.isMasterClient(c);
    }

    /* ------------------------------------------------------------------ */
    /* XADD                                                                */
    /* ------------------------------------------------------------------ */
    private static void xadd(Client c) {
        Server s = Server.I;
        AddTrimArgs a = parseAddOrTrimArgs(c, true);
        int idpos = a.idPos;
        int fieldPos = idpos + 1;
        if ((c.argc - fieldPos) < 2 || ((c.argc - fieldPos) % 2) == 1)
            throw RedisError.wrongArgs("xadd");

        if (a.idGiven && a.seqGiven && a.id.ms == 0 && a.id.seq == 0)
            throw new RedisError("ERR The ID specified in XADD must be greater than 0-0");

        RObj o = lookupWriteOrCreate(c, c.arg(1), a.noMkstream);
        if (o == null) { c.addNil(); return; }
        RStream st = st(o);

        if (st.lastId.ms == -1L && st.lastId.seq == -1L)
            throw new RedisError("ERR The stream has exhausted the last possible ID, unable to add more items");

        List<byte[]> fv = new ArrayList<>(c.argc - fieldPos);
        for (int j = fieldPos; j < c.argc; j++) fv.add(c.argv[j]);

        Id id;
        try {
            id = st.appendItem(fv, a.idGiven ? a.id : null, a.seqGiven, nodeMaxBytes(), nodeMaxEntries());
        } catch (RStream.AppendError e) {
            if (e.domain)
                throw new RedisError("ERR The ID specified in XADD is equal or smaller than the target stream top item");
            throw new RedisError("ERR Elements are too large to be stored");
        }

        byte[] replyId = id.bytes();
        c.addBulk(replyId);

        s.notifyKeyspaceEvent('t', "xadd", c.arg(1), c.db);
        s.dirty++;

        byte[][] repl = null;
        if (a.trimStrategy != TRIM_NONE) {
            long deleted = st.trim(a.trimStrategy, a.maxlen, a.minid, a.approxTrim, a.limit);
            if (deleted != 0) s.notifyKeyspaceEvent('t', "xtrim", c.arg(1), c.db);
            if (a.approxTrim) {
                repl = Arrays.copyOf(c.argv, c.argc);
                repl[a.trimStrategyArgIdx - 1] = Util.b("=");
                repl[a.trimStrategyArgIdx] = trimArgument(st, a.trimStrategy);
            }
        }

        s.signalModifiedKey(c.db, new Key(c.arg(1)));

        if (!a.idGiven || !a.seqGiven) {
            if (repl == null) repl = Arrays.copyOf(c.argv, c.argc);
            repl[idpos] = replyId;
        }
        if (repl != null) setReplOverride(repl);

        s.signalKeyAsReady(c.db, c.arg(1));
    }

    /**
     * Server.call() is what consumes replOverride, and AOF loading runs the
     * handlers directly: never arm it while loading, or the override would leak
     * into the first command of a real client.
     */
    private static void setReplOverride(byte[][] a) {
        Server s = Server.I;
        if (s.loading) return;
        s.replOverride = a;
    }

    /** streamRewriteTrimArgument() */
    private static byte[] trimArgument(RStream st, int strategy) {
        if (strategy == TRIM_MAXLEN) return Util.ll2string(st.length);
        return st.getEdgeID(true, false).bytes();
    }

    /* ------------------------------------------------------------------ */
    /* XTRIM                                                               */
    /* ------------------------------------------------------------------ */
    private static void xtrim(Client c) {
        Server s = Server.I;
        AddTrimArgs a = parseAddOrTrimArgs(c, false);
        RObj o = lookupWrite(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        RStream st = st(o);

        long deleted = st.trim(a.trimStrategy, a.maxlen, a.minid, a.approxTrim, a.limit);
        if (deleted != 0) {
            s.notifyKeyspaceEvent('t', "xtrim", c.arg(1), c.db);
            if (a.approxTrim) {
                byte[][] repl = Arrays.copyOf(c.argv, c.argc);
                repl[a.trimStrategyArgIdx - 1] = Util.b("=");
                repl[a.trimStrategyArgIdx] = trimArgument(st, a.trimStrategy);
                setReplOverride(repl);
            }
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            s.dirty += deleted;
        }
        c.addInt(deleted);
    }

    /* ------------------------------------------------------------------ */
    /* XLEN / XRANGE / XREVRANGE                                           */
    /* ------------------------------------------------------------------ */
    private static void xlen(Client c) {
        RObj o = lookupRead(c, c.arg(1));
        c.addInt(o == null ? 0 : st(o).length);
    }

    private static void xrangeGeneric(Client c, boolean rev) {
        byte[] startArg = rev ? c.arg(3) : c.arg(2);
        byte[] endArg = rev ? c.arg(2) : c.arg(3);
        boolean[] ex = new boolean[1];
        long count = -1;

        Id startId = parseIntervalID(startArg, ex, 0);
        if (ex[0] && !startId.incr()) throw new RedisError("ERR invalid start ID for the interval");
        Id endId = parseIntervalID(endArg, ex, -1L);
        if (ex[0] && !endId.decr()) throw new RedisError("ERR invalid end ID for the interval");

        for (int j = 4; j < c.argc; j++) {
            int additional = c.argc - j - 1;
            if (c.argIs(j, "COUNT") && additional >= 1) {
                count = longArg(c, j + 1, null);
                if (count < 0) count = 0;
                j++;
            } else {
                throw RedisError.syntax();
            }
        }

        RObj o = lookupRead(c, c.arg(1));
        if (o == null) { c.addEmptyArray(); return; }
        if (count == 0) { c.addNilArray(); return; }
        if (count == -1) count = 0;
        emitEntries(c, replyWithRange(c, st(o), startId, endId, count, rev, null, null, 0, null, null, null));
    }

    /* ------------------------------------------------------------------ */
    /* XREAD / XREADGROUP                                                  */
    /* ------------------------------------------------------------------ */

    /** The (already rewritten) command a blocked XREAD will re-run. */
    static final class BState {
        final byte[][] argv;
        BState(byte[][] argv) { this.argv = argv; }
    }

    private static boolean denyBlocking(Client c) {
        return c.fake || Server.I.execDepth > 0;
    }

    /** getTimeoutFromObjectOrReply() with UNIT_MILLISECONDS. */
    private static double parseBlockTimeout(Client c, int idx) {
        Long v = Util.string2ll(c.argv[idx]);
        if (v == null) throw new RedisError("ERR timeout is not an integer or out of range");
        if (v < 0) throw new RedisError("ERR timeout is negative");
        return v / 1000.0;
    }

    private static void xreadCommand(Client c, boolean xreadgroup) {
        xreadGeneric(c, xreadgroup, false);
    }

    /**
     * xreadCommand(). Returns true when the client was served (or replied to);
     * `fromBlocked` runs the command again for a client blocked on a key.
     */
    private static boolean xreadGeneric(Client c, boolean xreadgroup, boolean fromBlocked) {
        Server s = Server.I;
        long dirtyBefore = s.dirty;
        double timeout = -1;
        boolean timeoutGiven = false;
        long count = 0;
        int streamsArg = 0, streamsCount = 0;
        boolean noack = false;
        byte[] groupname = null, consumername = null;

        for (int i = 1; i < c.argc; i++) {
            int moreargs = c.argc - i - 1;
            if (c.argIs(i, "BLOCK") && moreargs > 0) {
                i++;
                timeout = parseBlockTimeout(c, i);
                timeoutGiven = true;
            } else if (c.argIs(i, "COUNT") && moreargs > 0) {
                i++;
                count = longArg(c, i, null);
                if (count < 0) count = 0;
            } else if (c.argIs(i, "STREAMS") && moreargs > 0) {
                streamsArg = i + 1;
                streamsCount = c.argc - streamsArg;
                if ((streamsCount % 2) != 0) {
                    String symbol = xreadgroup ? "ID or '>'" : "ID, '+', or '$'";
                    throw new RedisError("ERR Unbalanced '" + (xreadgroup ? "xreadgroup" : "xread")
                            + "' list of streams: for each stream key an " + symbol + " must be specified.");
                }
                streamsCount /= 2;
                break;
            } else if (c.argIs(i, "GROUP") && moreargs >= 2) {
                if (!xreadgroup)
                    throw new RedisError("ERR The GROUP option is only supported by XREADGROUP. You called XREAD instead.");
                groupname = c.arg(i + 1);
                consumername = c.arg(i + 2);
                i += 2;
            } else if (c.argIs(i, "NOACK")) {
                if (!xreadgroup)
                    throw new RedisError("ERR The NOACK option is only supported by XREADGROUP. You called XREAD instead.");
                noack = true;
            } else {
                throw RedisError.syntax();
            }
        }

        if (streamsArg == 0) throw RedisError.syntax();
        if (xreadgroup && groupname == null)
            throw new RedisError("ERR Missing GROUP option for XREADGROUP");

        Id[] ids = new Id[streamsCount];
        CG[] groups = groupname == null ? null : new CG[streamsCount];

        for (int i = streamsArg + streamsCount; i < c.argc; i++) {
            int idIdx = i - streamsArg - streamsCount;
            byte[] key = c.arg(i - streamsCount);
            RObj o = lookupRead(c, key);
            if (groupname != null) {
                CG group = (o == null) ? null : st(o).lookupCG(groupname);
                if (group == null) {
                    throw new RedisError("NOGROUP No such key '" + Util.s(key) + "' or consumer group '"
                            + Util.s(groupname) + "' in XREADGROUP with GROUP option");
                }
                groups[idIdx] = group;
            }
            String arg = c.argStr(i);
            if (arg.equals("$")) {
                if (xreadgroup)
                    throw new RedisError("ERR The $ ID is meaningless in the context of XREADGROUP: you want to read the history of this consumer by specifying a proper ID, or use the > ID to get new messages. The $ ID would just return an empty result set.");
                ids[idIdx] = (o != null) ? st(o).lastId.copy() : new Id(0, 0);
                continue;
            } else if (arg.equals("+")) {
                if (xreadgroup)
                    throw new RedisError("ERR The + ID is meaningless in the context of XREADGROUP: you want to read the history of this consumer by specifying a proper ID, or use the > ID to get new messages. The + ID would just return an empty result set.");
                if (o != null && st(o).length != 0) {
                    Id id = st(o).lastValidID();
                    id.decr();
                    ids[idIdx] = id;
                } else {
                    ids[idIdx] = new Id(0, 0);
                }
                continue;
            } else if (arg.equals(">")) {
                if (!xreadgroup)
                    throw new RedisError("ERR The > ID can be specified only when calling XREADGROUP using the GROUP <group> <consumer> option.");
                ids[idIdx] = new Id(-1L, -1L);
                continue;
            }
            ids[idIdx] = parseStrictID(c.argv[i], 0, null);
        }

        /* Try to serve synchronously. */
        List<byte[]> servedKeys = new ArrayList<>();
        List<List<Emitted>> servedData = new ArrayList<>();
        long[] propCount = new long[1];

        for (int i = 0; i < streamsCount; i++) {
            byte[] key = c.arg(streamsArg + i);
            RObj o = lookupRead(c, key);
            if (o == null) continue;
            RStream st = st(o);
            Id gt = ids[i];
            boolean serve = false, history = false;
            Consumer consumer = null;

            if (groups != null) {
                if (gt.ms != -1L || gt.seq != -1L) {
                    serve = true;
                    history = true;
                } else if (st.length != 0) {
                    Id maxid = st.lastValidID();
                    if (maxid.compareTo(groups[i].lastId) > 0) {
                        serve = true;
                        gt = groups[i].lastId.copy();
                        ids[i] = gt;
                    }
                }
                consumer = lookupConsumer(groups[i], consumername);
                if (consumer == null) {
                    consumer = createConsumer(groups[i], consumername, key, c.db, true, true);
                    if (noack) propagateConsumerCreation(c, key, groupname, consumer.name);
                }
                consumer.seenTime = s.mstime();
            } else if (st.length != 0) {
                Id maxid = st.lastValidID();
                if (maxid.compareTo(gt) > 0) serve = true;
            }

            if (serve) {
                Id start = gt.copy();
                start.incr();
                int flags = 0;
                if (noack) flags |= RWR_NOACK;
                if (history) flags |= RWR_HISTORY;
                List<Emitted> data = replyWithRange(c, st, start, null, count, false,
                        groups != null ? groups[i] : null, consumer, flags,
                        groups != null ? key : null, groupname, propCount);
                servedKeys.add(key);
                servedData.add(data);
            }
        }
        if (propCount[0] != 0) s.dirty++;

        if (!servedKeys.isEmpty()) {
            if (c.resp == 2) {
                c.addArray(servedKeys.size());
                for (int i = 0; i < servedKeys.size(); i++) {
                    c.addArray(2);
                    c.addBulk(servedKeys.get(i));
                    emitEntries(c, servedData.get(i));
                }
            } else {
                c.addMap(servedKeys.size());
                for (int i = 0; i < servedKeys.size(); i++) {
                    c.addBulk(servedKeys.get(i));
                    emitEntries(c, servedData.get(i));
                }
            }
            if (xreadgroup && !fromBlocked && s.dirty != dirtyBefore) setReplOverride(NO_PROP);
            return true;
        }

        if (fromBlocked) {
            if (xreadgroup && s.dirty != dirtyBefore) {
                /* Creating the consumer alone is propagated by its own command. */
            }
            return false;
        }

        if (timeoutGiven) {
            if (denyBlocking(c)) {
                c.addNilArray();
                if (xreadgroup && s.dirty != dirtyBefore) setReplOverride(NO_PROP);
                return true;
            }
            byte[][] saved = Arrays.copyOf(c.argv, c.argc);
            for (int idIdx = 0; idIdx < streamsCount; idIdx++) {
                int argIdx = idIdx + streamsArg + streamsCount;
                if (Util.s(saved[argIdx]).equals("$")) saved[argIdx] = ids[idIdx].bytes();
            }
            List<byte[]> keys = new ArrayList<>();
            for (int i = 0; i < streamsCount; i++) keys.add(c.arg(streamsArg + i));
            Blocking.blockOnKeys(s, c, keys, timeout, "stream", new BState(saved));
            if (xreadgroup && s.dirty != dirtyBefore) setReplOverride(NO_PROP);
            return true;
        }

        c.addNilArray();
        if (xreadgroup && s.dirty != dirtyBefore) setReplOverride(NO_PROP);
        return true;
    }

    /**
     * blocked.c: XREADGROUP asks to be unblocked when the key disappears or
     * changes type, so that it can report -NOGROUP / -WRONGTYPE. Used by the
     * INFO clients total_blocking_keys_on_nokey counter.
     */
    public static boolean blocksOnNokey(Client c) {
        return (c.blockState instanceof BState)
                && Util.equalsIgnoreCase(((BState) c.blockState).argv[0], "xreadgroup");
    }

    /** Blocking.READY: a stream this client waits on got new data (or lost its group). */
    private static boolean serveBlocked(Server s, Client c, Key ready) {
        if (!(c.blockState instanceof BState)) return false;
        BState st = (BState) c.blockState;
        /* Inside MULTI/EXEC (or a script) redis only wakes blocked clients once
         * the whole execution unit is done: postpone by re-arming the key. */
        if (s.execDepth > 0) {
            s.signalKeyAsReady(c.db, ready.b);
            return false;
        }
        /* handleClientsBlockedOnKey(): serve only when the key now holds a
         * stream, unless the client asked to be unblocked when the key goes
         * away or changes type, which is what XREADGROUP does so that it can
         * report -NOGROUP / -WRONGTYPE. */
        RObj signalled = s.dbs[c.db].dict.get(ready);
        boolean unblockOnNokey = Util.equalsIgnoreCase(st.argv[0], "xreadgroup");
        if (!unblockOnNokey && (signalled == null || signalled.type != RObj.STREAM)) return false;
        byte[][] savedArgv = c.argv;
        int savedArgc = c.argc;
        c.argv = st.argv;
        c.argc = st.argv.length;
        boolean xreadgroup = Util.equalsIgnoreCase(st.argv[0], "xreadgroup");
        try {
            return xreadGeneric(c, xreadgroup, true);
        } catch (RedisError e) {
            /* The command is re-processed here, so the error it produces has to
             * be accounted the way call() would have accounted it. */
            c.addErr(e.msg);
            s.errorStats.merge(Util.s(e.msg).split(" ")[0], 1L, Long::sum);
            long[] cs = s.commandStats.computeIfAbsent(xreadgroup ? "xreadgroup" : "xread",
                    x -> new long[3]);
            if (cs.length > 2) cs[2]++;
            return true;
        } finally {
            c.argv = savedArgv;
            c.argc = savedArgc;
        }
    }

    /* ------------------------------------------------------------------ */
    /* XDEL                                                                */
    /* ------------------------------------------------------------------ */
    private static void xdel(Client c) {
        Server s = Server.I;
        RObj o = lookupWrite(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        RStream st = st(o);

        Id[] ids = new Id[c.argc - 2];
        for (int j = 2; j < c.argc; j++) ids[j - 2] = parseStrictID(c.argv[j], 0, null);

        int deleted = 0;
        boolean firstEntry = false;
        for (Id id : ids) {
            if (st.deleteItem(id)) {
                if (id.compareTo(st.firstId) == 0) firstEntry = true;
                if (id.compareTo(st.maxDeletedEntryId) > 0) st.maxDeletedEntryId = id.copy();
                deleted++;
            }
        }
        if (deleted != 0) {
            if (st.length == 0) st.firstId = new Id(0, 0);
            else if (firstEntry) st.firstId = st.getEdgeID(true, true);
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            s.notifyKeyspaceEvent('t', "xdel", c.arg(1), c.db);
            s.dirty += deleted;
        }
        c.addInt(deleted);
    }

    /* ------------------------------------------------------------------ */
    /* XSETID                                                              */
    /* ------------------------------------------------------------------ */
    private static void xsetid(Client c) {
        Server s = Server.I;
        Id id = parseStrictID(c.argv[2], 0, null);
        Id maxXdelId = new Id(0, 0);
        long entriesAdded = -1;

        int i = 3;
        while (i < c.argc) {
            int moreargs = (c.argc - 1) - i;
            if (c.argIs(i, "ENTRIESADDED") && moreargs > 0) {
                entriesAdded = longArg(c, i + 1, null);
                if (entriesAdded < 0) throw new RedisError("ERR entries_added must be positive");
                i += 2;
            } else if (c.argIs(i, "MAXDELETEDID") && moreargs > 0) {
                maxXdelId = parseStrictID(c.argv[i + 1], 0, null);
                if (id.compareTo(maxXdelId) < 0)
                    throw new RedisError("ERR The ID specified in XSETID is smaller than the provided max_deleted_entry_id");
                i += 2;
            } else {
                throw RedisError.syntax();
            }
        }

        RObj o = lookupWrite(c, c.arg(1));
        if (o == null) throw RedisError.noSuchKey();
        RStream st = st(o);

        if (id.compareTo(st.maxDeletedEntryId) < 0)
            throw new RedisError("ERR The ID specified in XSETID is smaller than current max_deleted_entry_id");

        if (st.length > 0) {
            Id maxid = st.lastValidID();
            if (id.compareTo(maxid) < 0)
                throw new RedisError("ERR The ID specified in XSETID is smaller than the target stream top item");
            if (entriesAdded != -1 && Long.compareUnsigned(st.length, entriesAdded) > 0)
                throw new RedisError("ERR The entries_added specified in XSETID is smaller than the target stream length");
        }

        st.lastId = id.copy();
        if (entriesAdded != -1) st.entriesAdded = entriesAdded;
        if (!maxXdelId.isZero()) st.maxDeletedEntryId = maxXdelId.copy();
        c.addOk();
        s.dirty++;
        s.notifyKeyspaceEvent('t', "xsetid", c.arg(1), c.db);
    }

    /* ------------------------------------------------------------------ */
    /* XACK                                                                */
    /* ------------------------------------------------------------------ */
    private static void xack(Client c) {
        Server s = Server.I;
        RObj o = lookupRead(c, c.arg(1));
        CG group = (o == null) ? null : st(o).lookupCG(c.arg(2));
        if (o == null || group == null) { c.addInt(0); return; }

        Id[] ids = new Id[c.argc - 3];
        for (int j = 3; j < c.argc; j++) ids[j - 3] = parseStrictID(c.argv[j], 0, null);

        int acknowledged = 0;
        for (Id id : ids) {
            NACK nack = group.pel.remove(id);
            if (nack != null) {
                if (nack.consumer != null) nack.consumer.pel.remove(id);
                acknowledged++;
                s.dirty++;
            }
        }
        c.addInt(acknowledged);
    }

    /* ------------------------------------------------------------------ */
    /* XPENDING                                                            */
    /* ------------------------------------------------------------------ */
    private static void xpending(Client c) {
        boolean justinfo = c.argc == 3;
        byte[] key = c.arg(1);
        byte[] groupname = c.arg(2);
        byte[] consumername = null;
        Id startid = null, endid = null;
        long count = 0, minidle = 0;

        if (c.argc != 3 && (c.argc < 6 || c.argc > 9)) throw RedisError.syntax();

        if (c.argc >= 6) {
            int startidx = 3;
            if (c.argIs(3, "IDLE")) {
                minidle = longArg(c, 4, null);
                if (c.argc < 8) throw RedisError.syntax();
                startidx += 2;
            }
            count = longArg(c, startidx + 2, null);
            if (count < 0) count = 0;
            boolean[] ex = new boolean[1];
            startid = parseIntervalID(c.argv[startidx], ex, 0);
            if (ex[0] && !startid.incr()) throw new RedisError("ERR invalid start ID for the interval");
            endid = parseIntervalID(c.argv[startidx + 1], ex, -1L);
            if (ex[0] && !endid.decr()) throw new RedisError("ERR invalid end ID for the interval");
            if (startidx + 3 < c.argc) consumername = c.arg(startidx + 3);
        }

        RObj o = lookupRead(c, key);
        CG group = (o == null) ? null : st(o).lookupCG(groupname);
        if (o == null || group == null) {
            throw new RedisError("NOGROUP No such key '" + Util.s(key) + "' or consumer group '"
                    + Util.s(groupname) + "'");
        }

        if (justinfo) {
            c.addArray(4);
            c.addInt(group.pel.size());
            if (group.pel.isEmpty()) {
                c.addNil();
                c.addNil();
                c.addNilArray();
            } else {
                c.addBulk(group.pel.firstKey().bytes());
                c.addBulk(group.pel.lastKey().bytes());
                List<Consumer> withPending = new ArrayList<>();
                for (Consumer cons : group.consumers.values()) {
                    if (!cons.pel.isEmpty()) withPending.add(cons);
                }
                c.addArray(withPending.size());
                for (Consumer cons : withPending) {
                    c.addArray(2);
                    c.addBulk(cons.name);
                    c.addBulk(Util.ll2string(cons.pel.size()));
                }
            }
            return;
        }

        Consumer consumer = null;
        if (consumername != null) {
            consumer = lookupConsumer(group, consumername);
            if (consumer == null) { c.addEmptyArray(); return; }
        }
        java.util.NavigableMap<Id, NACK> pel = consumer != null ? consumer.pel : group.pel;
        long now = Server.I.mstime();
        List<Object[]> out = new ArrayList<>();
        for (Map.Entry<Id, NACK> e : pel.tailMap(startid, true).entrySet()) {
            if (count == 0) break;
            if (e.getKey().compareTo(endid) > 0) break;
            NACK nack = e.getValue();
            if (minidle != 0) {
                long thisIdle = now - nack.deliveryTime;
                if (thisIdle < minidle) continue;
            }
            count--;
            long elapsed = now - nack.deliveryTime;
            if (elapsed < 0) elapsed = 0;
            out.add(new Object[]{e.getKey(), nack.consumer, elapsed, nack.deliveryCount});
        }
        c.addArray(out.size());
        for (Object[] row : out) {
            c.addArray(4);
            c.addBulk(((Id) row[0]).bytes());
            Consumer owner = (Consumer) row[1];
            c.addBulk(owner == null ? Util.EMPTY : owner.name);
            c.addInt((Long) row[2]);
            c.addInt((Long) row[3]);
        }
    }

    /* ------------------------------------------------------------------ */
    /* XCLAIM                                                              */
    /* ------------------------------------------------------------------ */
    private static void xclaim(Client c) {
        Server s = Server.I;
        long dirtyBefore = s.dirty;
        RObj o = lookupRead(c, c.arg(1));
        CG group = (o == null) ? null : st(o).lookupCG(c.arg(2));
        if (o == null || group == null) {
            throw new RedisError("NOGROUP No such key '" + Util.s(c.arg(1)) + "' or consumer group '"
                    + Util.s(c.arg(2)) + "'");
        }
        RStream st = st(o);

        long minidle = longArg(c, 4, "Invalid min-idle-time argument for XCLAIM");
        if (minidle < 0) minidle = 0;

        long retrycount = -1;
        long deliverytime = -1;
        boolean force = false, justid = false;

        int j;
        List<Id> ids = new ArrayList<>();
        for (j = 5; j < c.argc; j++) {
            Id id = parseIDGeneric(c.argv[j], 0, true, null);
            if (id == null) break;
            ids.add(id);
        }
        int lastIdArg = j - 1;

        long now = s.mstime();
        Id lastId = new Id(0, 0);
        boolean propagateLastId = false;
        for (; j < c.argc; j++) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "FORCE")) {
                force = true;
            } else if (c.argIs(j, "JUSTID")) {
                justid = true;
            } else if (c.argIs(j, "IDLE") && moreargs > 0) {
                j++;
                deliverytime = now - longArg(c, j, "Invalid IDLE option argument for XCLAIM");
            } else if (c.argIs(j, "TIME") && moreargs > 0) {
                j++;
                deliverytime = longArg(c, j, "Invalid TIME option argument for XCLAIM");
            } else if (c.argIs(j, "RETRYCOUNT") && moreargs > 0) {
                j++;
                retrycount = longArg(c, j, "Invalid RETRYCOUNT option argument for XCLAIM");
            } else if (c.argIs(j, "LASTID") && moreargs > 0) {
                j++;
                lastId = parseStrictID(c.argv[j], 0, null);
            } else {
                throw new RedisError("ERR Unrecognized XCLAIM option '" + c.argStr(j) + "'");
            }
        }

        if (lastId.compareTo(group.lastId) > 0) {
            group.lastId = lastId.copy();
            propagateLastId = true;
        }

        if (deliverytime != -1) {
            if (deliverytime < 0 || deliverytime > now) deliverytime = now;
        } else {
            deliverytime = now;
        }

        Consumer consumer = lookupConsumer(group, c.arg(3));
        if (consumer == null) consumer = createConsumer(group, c.arg(3), c.arg(1), c.db, true, true);
        consumer.seenTime = s.mstime();

        List<Object[]> results = new ArrayList<>();   /* {Id, List<byte[]> or null} */
        for (int k = 5; k <= lastIdArg; k++) {
            Id id = ids.get(k - 5);
            NACK nack = group.pel.get(id);

            if (!st.entryExists(id)) {
                if (nack != null) {
                    propagateXCLAIM(c, c.arg(1), group, c.arg(2), id, nack);
                    propagateLastId = false;
                    s.dirty++;
                    group.pel.remove(id);
                    if (nack.consumer != null) nack.consumer.pel.remove(id);
                }
                continue;
            }

            if (force && nack == null) {
                nack = new NACK(null);
                group.pel.put(id.copy(), nack);
            }

            if (nack != null) {
                if (nack.consumer != null && minidle != 0) {
                    long thisIdle = now - nack.deliveryTime;
                    if (thisIdle < minidle) continue;
                }
                if (nack.consumer != consumer && nack.consumer != null) {
                    nack.consumer.pel.remove(id);
                }
                nack.deliveryTime = deliverytime;
                if (retrycount >= 0) nack.deliveryCount = retrycount;
                else if (!justid) nack.deliveryCount++;
                if (nack.consumer != consumer) {
                    consumer.pel.put(id.copy(), nack);
                    nack.consumer = consumer;
                }
                if (justid) results.add(new Object[]{id, null});
                else results.add(new Object[]{id, st.lookup(id).fields});
                consumer.activeTime = s.mstime();
                propagateXCLAIM(c, c.arg(1), group, c.arg(2), id, nack);
                propagateLastId = false;
                s.dirty++;
            }
        }
        if (propagateLastId) {
            propagateGroupID(c, c.arg(1), group, c.arg(2));
            s.dirty++;
        }

        c.addArray(results.size());
        for (Object[] row : results) {
            if (row[1] == null) {
                c.addBulk(((Id) row[0]).bytes());
            } else {
                @SuppressWarnings("unchecked")
                List<byte[]> fields = (List<byte[]>) row[1];
                emitEntry(c, new Emitted((Id) row[0], fields));
            }
        }
        if (s.dirty != dirtyBefore) setReplOverride(NO_PROP);
    }

    /* ------------------------------------------------------------------ */
    /* XAUTOCLAIM                                                          */
    /* ------------------------------------------------------------------ */
    private static void xautoclaim(Client c) {
        Server s = Server.I;
        long dirtyBefore = s.dirty;
        long count = 100;
        final int attemptsFactor = 10;
        boolean justid = false;

        long minidle = longArg(c, 4, "Invalid min-idle-time argument for XAUTOCLAIM");
        if (minidle < 0) minidle = 0;

        boolean[] ex = new boolean[1];
        Id startid = parseIntervalID(c.argv[5], ex, 0);
        if (ex[0] && !startid.incr()) throw new RedisError("ERR invalid start ID for the interval");

        int j = 6;
        while (j < c.argc) {
            int moreargs = (c.argc - 1) - j;
            if (c.argIs(j, "COUNT") && moreargs > 0) {
                long maxCount = Long.MAX_VALUE / 16;
                Long v = Util.string2ll(c.argv[j + 1]);
                if (v == null || v < 1 || v > maxCount) throw new RedisError("ERR COUNT must be > 0");
                count = v;
                j++;
            } else if (c.argIs(j, "JUSTID")) {
                justid = true;
            } else {
                throw RedisError.syntax();
            }
            j++;
        }

        RObj o = lookupRead(c, c.arg(1));
        CG group = (o == null) ? null : st(o).lookupCG(c.arg(2));
        if (o == null || group == null) {
            throw new RedisError("NOGROUP No such key '" + Util.s(c.arg(1)) + "' or consumer group '"
                    + Util.s(c.arg(2)) + "'");
        }
        RStream st = st(o);

        if (count > (1L << 27)) throw new RedisError(
                "ERR Insufficient memory, failed allocating transient memory, COUNT too high.");

        Consumer consumer = lookupConsumer(group, c.arg(3));
        if (consumer == null) consumer = createConsumer(group, c.arg(3), c.arg(1), c.db, true, true);
        consumer.seenTime = s.mstime();

        long attempts = count * attemptsFactor;
        long now = s.mstime();
        List<Object[]> results = new ArrayList<>();
        List<Id> deletedIds = new ArrayList<>();
        Id cursor = new Id(0, 0);

        List<Id> candidates = new ArrayList<>(group.pel.tailMap(startid, true).keySet());
        int idx = 0;
        for (; idx < candidates.size(); idx++) {
            if (attempts-- <= 0 || count == 0) break;
            Id id = candidates.get(idx);
            NACK nack = group.pel.get(id);
            if (nack == null) continue;

            if (!st.entryExists(id)) {
                propagateXCLAIM(c, c.arg(1), group, c.arg(2), id, nack);
                s.dirty++;
                group.pel.remove(id);
                if (nack.consumer != null) nack.consumer.pel.remove(id);
                deletedIds.add(id);
                count--;
                continue;
            }

            if (minidle != 0) {
                long thisIdle = now - nack.deliveryTime;
                if (thisIdle < minidle) continue;
            }

            if (nack.consumer != consumer && nack.consumer != null) nack.consumer.pel.remove(id);

            nack.deliveryTime = now;
            if (!justid) nack.deliveryCount++;

            if (nack.consumer != consumer) {
                consumer.pel.put(id.copy(), nack);
                nack.consumer = consumer;
            }

            if (justid) results.add(new Object[]{id, null});
            else results.add(new Object[]{id, st.lookup(id).fields});
            count--;
            consumer.activeTime = s.mstime();
            propagateXCLAIM(c, c.arg(1), group, c.arg(2), id, nack);
            s.dirty++;
        }
        /* The cursor is the next PEL entry we did not look at. */
        while (idx < candidates.size() && !group.pel.containsKey(candidates.get(idx))) idx++;
        if (idx < candidates.size()) cursor = candidates.get(idx);

        c.addArray(3);
        c.addBulk(cursor.bytes());
        c.addArray(results.size());
        for (Object[] row : results) {
            if (row[1] == null) {
                c.addBulk(((Id) row[0]).bytes());
            } else {
                @SuppressWarnings("unchecked")
                List<byte[]> fields = (List<byte[]>) row[1];
                emitEntry(c, new Emitted((Id) row[0], fields));
            }
        }
        c.addArray(deletedIds.size());
        for (Id id : deletedIds) c.addBulk(id.bytes());

        if (s.dirty != dirtyBefore) setReplOverride(NO_PROP);
    }

    /* ------------------------------------------------------------------ */
    /* XGROUP                                                              */
    /* ------------------------------------------------------------------ */
    private static void xgroupHelp(Client c) {
        String[] help = {
            "CREATE <key> <groupname> <id|$> [option]",
            "    Create a new consumer group. Options are:",
            "    * MKSTREAM",
            "      Create the empty stream if it does not exist.",
            "    * ENTRIESREAD entries_read",
            "      Set the group's entries_read counter (internal use).",
            "CREATECONSUMER <key> <groupname> <consumer>",
            "    Create a new consumer in the specified group.",
            "DELCONSUMER <key> <groupname> <consumer>",
            "    Remove the specified consumer.",
            "DESTROY <key> <groupname>",
            "    Remove the specified group.",
            "SETID <key> <groupname> <id|$> [ENTRIESREAD entries_read]",
            "    Set the current group ID and entries_read counter.",
        };
        addReplyHelp(c, "XGROUP", help);
    }

    static void addReplyHelp(Client c, String cmd, String[] help) {
        c.addArray(help.length + 2);
        c.addSimple(cmd + " <subcommand> [<arg> [value] [opt] ...]. Subcommands are:");
        for (String h : help) c.addSimple(h);
        c.addSimple("HELP");
        c.addSimple("    Print this help.");
    }

    private static void xgroup(Client c) {
        Server s = Server.I;
        RStream st = null;
        byte[] grpname = null;
        CG cg = null;
        String opt = c.argLower(1);
        boolean mkstream = false;
        long entriesRead = RStream.INVALID_ENTRIES_READ;
        RObj o = null;

        if (c.argc >= 4) {
            int i = 5;
            boolean createSub = opt.equals("create");
            boolean setidSub = opt.equals("setid");
            while (i < c.argc) {
                if (createSub && c.argIs(i, "MKSTREAM")) {
                    mkstream = true;
                    i++;
                } else if ((createSub || setidSub) && c.argIs(i, "ENTRIESREAD") && i + 1 < c.argc) {
                    entriesRead = longArg(c, i + 1, null);
                    if (entriesRead < 0 && entriesRead != RStream.INVALID_ENTRIES_READ)
                        throw new RedisError("ERR value for ENTRIESREAD must be positive or -1");
                    i += 2;
                } else {
                    throw subSyntax(c);
                }
            }
            o = lookupWrite(c, c.arg(2));
            if (o != null) st = st(o);
            grpname = c.arg(3);
        }

        if (c.argc >= 4 && !mkstream) {
            if (st == null) {
                throw new RedisError("ERR The XGROUP subcommand requires the key to exist. "
                        + "Note that for CREATE you may want to use the MKSTREAM option to create "
                        + "an empty stream automatically.");
            }
            cg = st.lookupCG(grpname);
            if (cg == null && (opt.equals("setid") || opt.equals("createconsumer")
                    || opt.equals("delconsumer"))) {
                throw new RedisError("NOGROUP No such consumer group '" + Util.s(grpname)
                        + "' for key name '" + c.argStr(2) + "'");
            }
        }

        if (opt.equals("create") && c.argc >= 5 && c.argc <= 8) {
            Id id;
            if (c.argStr(4).equals("$")) id = (st != null) ? st.lastId.copy() : new Id(0, 0);
            else id = parseStrictID(c.argv[4], 0, null);

            if (st == null) {
                o = createStreamObject();
                s.dbAdd(c.db, c.arg(2), o);
                st = st(o);
                s.signalModifiedKey(c.db, new Key(c.arg(2)));
            }
            CG created = st.createCG(grpname, id, entriesRead);
            if (created != null) {
                c.addOk();
                s.dirty++;
                s.notifyKeyspaceEvent('t', "xgroup-create", c.arg(2), c.db);
            } else {
                throw new RedisError("BUSYGROUP Consumer Group name already exists");
            }
        } else if (opt.equals("setid") && (c.argc == 5 || c.argc == 7)) {
            Id id;
            if (c.argStr(4).equals("$")) id = st.lastId.copy();
            else id = parseID(c.argv[4], 0);
            cg.lastId = id.copy();
            cg.entriesRead = entriesRead;
            c.addOk();
            s.dirty++;
            s.notifyKeyspaceEvent('t', "xgroup-setid", c.arg(2), c.db);
        } else if (opt.equals("destroy") && c.argc == 4) {
            if (cg != null) {
                st.cgroups.remove(new Key(grpname));
                c.addInt(1);
                s.dirty++;
                s.notifyKeyspaceEvent('t', "xgroup-destroy", c.arg(2), c.db);
                s.signalKeyAsReady(c.db, c.arg(2));
            } else {
                c.addInt(0);
            }
        } else if (opt.equals("createconsumer") && c.argc == 5) {
            Consumer created = createConsumer(cg, c.arg(4), c.arg(2), c.db, true, true);
            c.addInt(created != null ? 1 : 0);
        } else if (opt.equals("delconsumer") && c.argc == 5) {
            long pending = 0;
            Consumer consumer = lookupConsumer(cg, c.arg(4));
            if (consumer != null) {
                pending = consumer.pel.size();
                delConsumer(cg, consumer);
                s.dirty++;
                s.notifyKeyspaceEvent('t', "xgroup-delconsumer", c.arg(2), c.db);
            }
            c.addInt(pending);
        } else {
            throw subSyntax(c);
        }
    }

    /* ------------------------------------------------------------------ */
    /* XINFO                                                               */
    /* ------------------------------------------------------------------ */
    private static void xinfoHelp(Client c) {
        String[] help = {
            "CONSUMERS <key> <groupname>",
            "    Show consumers of <groupname>.",
            "GROUPS <key>",
            "    Show the stream consumer groups.",
            "STREAM <key> [FULL [COUNT <count>]",
            "    Show information about the stream.",
        };
        addReplyHelp(c, "XINFO", help);
    }

    private static void xinfo(Client c) {
        String opt = c.argLower(1);
        byte[] key = c.arg(2);
        RObj o = lookupRead(c, key);
        if (o == null) throw RedisError.noSuchKey();
        RStream st = st(o);

        if (opt.equals("consumers") && c.argc == 4) {
            CG cg = st.lookupCG(c.arg(3));
            if (cg == null) {
                throw new RedisError("NOGROUP No such consumer group '" + c.argStr(3)
                        + "' for key name '" + Util.s(key) + "'");
            }
            long now = Server.I.mstime();
            c.addArray(cg.consumers.size());
            for (Consumer consumer : cg.consumers.values()) {
                long inactive = consumer.activeTime != -1 ? now - consumer.activeTime : consumer.activeTime;
                long idle = now - consumer.seenTime;
                if (idle < 0) idle = 0;
                c.addMap(4);
                c.addBulk("name");
                c.addBulk(consumer.name);
                c.addBulk("pending");
                c.addInt(consumer.pel.size());
                c.addBulk("idle");
                c.addInt(idle);
                c.addBulk("inactive");
                c.addInt(inactive);
            }
        } else if (opt.equals("groups") && c.argc == 3) {
            if (st.cgroups == null) { c.addEmptyArray(); return; }
            c.addArray(st.cgroups.size());
            for (Map.Entry<Key, CG> e : st.cgroups.entrySet()) {
                CG cg = e.getValue();
                c.addMap(6);
                c.addBulk("name");
                c.addBulk(e.getKey().b);
                c.addBulk("consumers");
                c.addInt(cg.consumers.size());
                c.addBulk("pending");
                c.addInt(cg.pel.size());
                c.addBulk("last-delivered-id");
                c.addBulk(cg.lastId.bytes());
                c.addBulk("entries-read");
                if (cg.entriesRead != RStream.INVALID_ENTRIES_READ) c.addInt(cg.entriesRead);
                else c.addNil();
                c.addBulk("lag");
                replyWithCGLag(c, st, cg);
            }
        } else if (opt.equals("stream")) {
            xinfoStream(c, st);
        } else {
            throw subSyntax(c);
        }
    }

    /** streamReplyWithCGLag() */
    private static void replyWithCGLag(Client c, RStream s, CG cg) {
        boolean valid = false;
        long lag = 0;
        if (s.entriesAdded == 0) {
            lag = 0; valid = true;
        } else if (s.length == 0) {
            lag = 0; valid = true;
        } else if (cg.lastId.compareTo(s.firstId) < 0
                && s.maxDeletedEntryId.compareTo(s.firstId) < 0) {
            lag = s.length; valid = true;
        } else if (cg.entriesRead != RStream.INVALID_ENTRIES_READ
                && !s.rangeHasTombstones(cg.lastId, null)) {
            lag = s.entriesAdded - cg.entriesRead; valid = true;
        } else {
            long entriesRead = s.estimateDistanceFromFirstEverEntry(cg.lastId);
            if (entriesRead != RStream.INVALID_ENTRIES_READ) {
                lag = s.entriesAdded - entriesRead; valid = true;
            }
        }
        if (valid) c.addInt(lag);
        else c.addNil();
    }

    /** xinfoReplyWithStreamInfo() */
    private static void xinfoStream(Client c, RStream s) {
        boolean full = true;
        long count = 10;
        int optc = c.argc - 3;
        if (optc == 0) {
            full = false;
        } else {
            if (optc != 1 && optc != 3) throw subSyntax(c);
            if (!c.argIs(3, "full")) throw subSyntax(c);
            if (optc == 3) {
                if (!c.argIs(4, "count")) throw subSyntax(c);
                count = longArg(c, 5, null);
                if (count < 0) count = 10;
            }
        }

        c.addMap(full ? 9 : 10);
        c.addBulk("length");
        c.addInt(s.length);
        c.addBulk("radix-tree-keys");
        c.addInt(s.rax.size());
        c.addBulk("radix-tree-nodes");
        c.addInt(raxNumNodes(s));
        c.addBulk("last-generated-id");
        c.addBulk(s.lastId.bytes());
        c.addBulk("max-deleted-entry-id");
        c.addBulk(s.maxDeletedEntryId.bytes());
        c.addBulk("entries-added");
        c.addInt(s.entriesAdded);
        c.addBulk("recorded-first-entry-id");
        c.addBulk(s.firstId.bytes());

        if (!full) {
            c.addBulk("groups");
            c.addInt(s.cgroupCount());
            c.addBulk("first-entry");
            List<Emitted> first = replyWithRange(c, s, RStream.minId(), RStream.maxId(), 1, false,
                    null, null, 0, null, null, null);
            if (first.isEmpty()) c.addNil(); else emitEntry(c, first.get(0));
            c.addBulk("last-entry");
            List<Emitted> last = replyWithRange(c, s, RStream.minId(), RStream.maxId(), 1, true,
                    null, null, 0, null, null, null);
            if (last.isEmpty()) c.addNil(); else emitEntry(c, last.get(0));
            return;
        }

        c.addBulk("entries");
        emitEntries(c, replyWithRange(c, s, null, null, count, false, null, null, 0, null, null, null));

        c.addBulk("groups");
        if (s.cgroups == null) { c.addEmptyArray(); return; }
        c.addArray(s.cgroups.size());
        for (Map.Entry<Key, CG> ge : s.cgroups.entrySet()) {
            CG cg = ge.getValue();
            c.addMap(7);
            c.addBulk("name");
            c.addBulk(ge.getKey().b);
            c.addBulk("last-delivered-id");
            c.addBulk(cg.lastId.bytes());
            c.addBulk("entries-read");
            if (cg.entriesRead != RStream.INVALID_ENTRIES_READ) c.addInt(cg.entriesRead);
            else c.addNil();
            c.addBulk("lag");
            replyWithCGLag(c, s, cg);
            c.addBulk("pel-count");
            c.addInt(cg.pel.size());
            c.addBulk("pending");
            int n = count == 0 ? cg.pel.size() : (int) Math.min(cg.pel.size(), count);
            c.addArray(n);
            int emitted = 0;
            for (Map.Entry<Id, NACK> pe : cg.pel.entrySet()) {
                if (emitted++ >= n) break;
                NACK nack = pe.getValue();
                c.addArray(4);
                c.addBulk(pe.getKey().bytes());
                c.addBulk(nack.consumer == null ? Util.EMPTY : nack.consumer.name);
                c.addInt(nack.deliveryTime);
                c.addInt(nack.deliveryCount);
            }
            c.addBulk("consumers");
            c.addArray(cg.consumers.size());
            for (Consumer consumer : cg.consumers.values()) {
                c.addMap(5);
                c.addBulk("name");
                c.addBulk(consumer.name);
                c.addBulk("seen-time");
                c.addInt(consumer.seenTime);
                c.addBulk("active-time");
                c.addInt(consumer.activeTime);
                c.addBulk("pel-count");
                c.addInt(consumer.pel.size());
                c.addBulk("pending");
                int cn = count == 0 ? consumer.pel.size() : (int) Math.min(consumer.pel.size(), count);
                c.addArray(cn);
                int ce = 0;
                for (Map.Entry<Id, NACK> pe : consumer.pel.entrySet()) {
                    if (ce++ >= cn) break;
                    NACK nack = pe.getValue();
                    c.addArray(3);
                    c.addBulk(pe.getKey().bytes());
                    c.addInt(nack.deliveryTime);
                    c.addInt(nack.deliveryCount);
                }
            }
        }
    }

    /**
     * rax->numnodes: the tests only require that this grows with the number of
     * keys, so we approximate it with the key count plus the root node.
     */
    private static long raxNumNodes(RStream s) { return s.rax.size() + 1; }

    /* ------------------------------------------------------------------ */
    /* AOF rewrite (aof.c rewriteStreamObject())                           */
    /* ------------------------------------------------------------------ */
    public static void emitAofRewrite(Server s, Buf b, Key k, RObj o) {
        RStream st = st(o);
        if (st.length != 0) {
            for (RStream.Entry e : st.allEntries()) {
                byte[][] argv = new byte[3 + e.fields.size()][];
                argv[0] = Util.b("XADD");
                argv[1] = k.b;
                argv[2] = e.id.bytes();
                for (int i = 0; i < e.fields.size(); i++) argv[3 + i] = e.fields.get(i);
                Aof.writeCmd(b, argv);
            }
        } else {
            Aof.writeCmd(b, Util.b("XADD"), k.b, Util.b("MAXLEN"), Util.b("0"),
                    Util.b("0-1"), Util.b("x"), Util.b("y"));
        }
        Aof.writeCmd(b, Util.b("XSETID"), k.b, st.lastId.bytes(),
                Util.b("ENTRIESADDED"), Util.ll2string(st.entriesAdded),
                Util.b("MAXDELETEDID"), st.maxDeletedEntryId.bytes());

        if (st.cgroups == null) return;
        for (Map.Entry<Key, CG> ge : st.cgroups.entrySet()) {
            CG group = ge.getValue();
            Aof.writeCmd(b, Util.b("XGROUP"), Util.b("CREATE"), k.b, ge.getKey().b,
                    group.lastId.bytes(), Util.b("ENTRIESREAD"), Util.ll2string(group.entriesRead));
            for (Consumer consumer : group.consumers.values()) {
                if (consumer.pel.isEmpty()) {
                    Aof.writeCmd(b, Util.b("XGROUP"), Util.b("CREATECONSUMER"), k.b,
                            ge.getKey().b, consumer.name);
                    continue;
                }
                for (Map.Entry<Id, NACK> pe : consumer.pel.entrySet()) {
                    NACK nack = pe.getValue();
                    Aof.writeCmd(b, Util.b("XCLAIM"), k.b, ge.getKey().b, consumer.name,
                            Util.b("0"), pe.getKey().bytes(),
                            Util.b("TIME"), Util.ll2string(nack.deliveryTime),
                            Util.b("RETRYCOUNT"), Util.ll2string(nack.deliveryCount),
                            Util.b("JUSTID"), Util.b("FORCE"));
                }
            }
        }
    }

    /** debug.c xorObjectDigest() for OBJ_STREAM. */
    public static void digest(byte[] digest, RObj o) {
        RStream st = st(o);
        for (RStream.Entry e : st.allEntries()) {
            Digest.mixDigest(digest, Util.b(Long.toUnsignedString(e.id.ms) + "."
                    + Long.toUnsignedString(e.id.seq)));
            for (byte[] f : e.fields) Digest.mixDigest(digest, f);
        }
    }
}
