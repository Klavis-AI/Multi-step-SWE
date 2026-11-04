package redis;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * The TS.* family, mirroring the RedisTimeSeries module bundled with redis 8.0.
 *
 * Sample values go on the wire as RESP2 simple strings: the module formats a
 * double with the shortest round-tripping digits, plain while the value sits in
 * [1, 1e16) and in "d.dddEexp" form otherwise ({@link #fmt}).
 */
public final class TsCmds {
    private TsCmds() {}

    private static Server S() { return Server.I; }

    static final String E_NOKEY = "ERR TSDB: the key does not exist";
    static final String E_NOTTS = "ERR TSDB: the key is not a TSDB key";
    static final String E_EXISTS = "ERR TSDB: key already exists";

    // ------------------------------------------------------------------
    // double formatting
    // ------------------------------------------------------------------

    /** The module's sample formatting (uppercase exponent, no trailing ".0"). */
    static byte[] fmt(double v) {
        if (Double.isNaN(v)) return Util.b("NaN");
        if (Double.isInfinite(v)) return Util.b(v > 0 ? "inf" : "-inf");
        if (v == 0) return Util.b("0");
        boolean neg = v < 0;
        String[] dk = RJson.shortestDigits(Math.abs(v));
        String digits = dk[0];
        int kk = Integer.parseInt(dk[1]);
        StringBuilder sb = new StringBuilder();
        if (neg) sb.append('-');
        int n = digits.length();
        if (kk >= 1 && kk <= 16) {
            if (kk >= n) {
                sb.append(digits);
                for (int i = n; i < kk; i++) sb.append('0');
            } else {
                sb.append(digits, 0, kk).append('.').append(digits, kk, n);
            }
        } else {
            sb.append(digits.charAt(0));
            if (n > 1) sb.append('.').append(digits, 1, n);
            sb.append('E').append(kk - 1);
        }
        return sb.toString().getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
    }

    private static void addSample(Client c, long ts, double v) {
        c.addArray(2);
        c.addInt(ts);
        if (c.resp >= 3) c.addDouble(v); else c.addSimple(Util.s(fmt(v)));
    }

    // ------------------------------------------------------------------
    // lookup
    // ------------------------------------------------------------------

    private static RTs get(Client c, byte[] key, boolean write) {
        RObj o = write ? S().lookupKeyWrite(c, key) : S().lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != RObj.MODULE || !(o.ptr instanceof RTs)) throw new RedisError(E_NOTTS);
        return (RTs) o.ptr;
    }

    private static RTs need(Client c, byte[] key, boolean write) {
        RTs t = get(c, key, write);
        if (t == null) throw new RedisError(E_NOKEY);
        return t;
    }

    private static void touched(Client c, byte[] key, String event) {
        S().signalModifiedKey(c.db, new Key(key));
        S().notifyKeyspaceEvent('m', event, key, c.db);
        S().dirty++;
    }

    // ------------------------------------------------------------------
    // option parsing shared by CREATE / ALTER / ADD / INCRBY
    // ------------------------------------------------------------------

    private static final class Opts {
        Long retention, chunkSize;
        Integer policy;
        Boolean uncompressed;
        LinkedHashMap<Key, byte[]> labels;
        Long ignoreMaxTimeDiff;
        Double ignoreMaxValDiff;
        Integer onDuplicate;
        Long timestamp;
        boolean hasTimestamp;
    }

    /** Parse the trailing option words; `allow` lists what this command accepts. */
    private static Opts parseOpts(Client c, int from, String allow) {
        Opts o = new Opts();
        int i = from;
        while (i < c.argc) {
            if (allow.contains("R") && c.argIs(i, "RETENTION")) {
                if (i + 1 >= c.argc) throw new RedisError("ERR TSDB: Couldn't parse RETENTION");
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null) throw new RedisError("ERR TSDB: Couldn't parse RETENTION");
                if (v < 0) throw new RedisError("TSDB: Couldn't parse RETENTION");
                o.retention = v;
                i += 2;
            } else if (allow.contains("C") && c.argIs(i, "CHUNK_SIZE")) {
                if (i + 1 >= c.argc) throw new RedisError("TSDB: Couldn't parse CHUNK_SIZE");
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v < 48 || v > 1048576 || v % 8 != 0)
                    throw new RedisError("ERR TSDB: CHUNK_SIZE value must be a multiple of 8 in the range [48 .. 1048576]");
                o.chunkSize = v;
                i += 2;
            } else if (allow.contains("E") && c.argIs(i, "ENCODING")) {
                if (i + 1 >= c.argc) throw new RedisError("ERR TSDB: unknown ENCODING parameter");
                if (c.argIs(i + 1, "COMPRESSED")) o.uncompressed = false;
                else if (c.argIs(i + 1, "UNCOMPRESSED")) o.uncompressed = true;
                else throw new RedisError("ERR TSDB: unknown ENCODING parameter");
                i += 2;
            } else if (allow.contains("D") && c.argIs(i, "DUPLICATE_POLICY")) {
                if (i + 1 >= c.argc) throw new RedisError("ERR TSDB: Unknown DUPLICATE_POLICY");
                int p = RTs.policyByName(c.argLower(i + 1));
                if (p < 0) throw new RedisError("ERR TSDB: Unknown DUPLICATE_POLICY");
                o.policy = p;
                i += 2;
            } else if (allow.contains("O") && c.argIs(i, "ON_DUPLICATE")) {
                if (i + 1 >= c.argc) throw new RedisError("ERR TSDB: Unknown DUPLICATE_POLICY");
                int p = RTs.policyByName(c.argLower(i + 1));
                if (p < 0) throw new RedisError("ERR TSDB: Unknown DUPLICATE_POLICY");
                o.onDuplicate = p;
                i += 2;
            } else if (allow.contains("T") && c.argIs(i, "TIMESTAMP")) {
                if (i + 1 >= c.argc) throw new RedisError("ERR TSDB: invalid timestamp");
                o.timestamp = parseTimestamp(c.arg(i + 1));
                o.hasTimestamp = true;
                i += 2;
            } else if (allow.contains("I") && c.argIs(i, "IGNORE")) {
                if (i + 2 >= c.argc) throw new RedisError("ERR TSDB: IGNORE one or more arguments are missing");
                Long a = Util.string2ll(c.arg(i + 1));
                Double b = Util.string2d(c.arg(i + 2));
                if (a == null || a < 0 || b == null || b < 0)
                    throw new RedisError("ERR TSDB: IGNORE one or more arguments are invalid");
                o.ignoreMaxTimeDiff = a;
                o.ignoreMaxValDiff = b;
                i += 3;
            } else if (allow.contains("L") && c.argIs(i, "LABELS")) {
                o.labels = new LinkedHashMap<>();
                i++;
                while (i + 1 < c.argc) {
                    o.labels.put(new Key(c.arg(i)), c.arg(i + 1));
                    i += 2;
                }
                i = c.argc;
            } else {
                throw RedisError.syntax();
            }
        }
        return o;
    }

    private static long parseTimestamp(byte[] a) {
        if (Util.equalsIgnoreCase(a, "*")) return Server.I.mstime();
        Long v = Util.string2ll(a);
        if (v == null) throw new RedisError("ERR TSDB: invalid timestamp");
        if (v < 0) throw new RedisError("ERR TSDB: invalid timestamp, must be a nonnegative integer");
        return v;
    }

    private static void apply(RTs t, Opts o) {
        if (o.retention != null) t.retention = o.retention;
        if (o.chunkSize != null) t.chunkSize = o.chunkSize;
        if (o.policy != null) t.duplicatePolicy = o.policy;
        if (o.uncompressed != null) t.uncompressed = o.uncompressed;
        if (o.labels != null) { t.labels.clear(); t.labels.putAll(o.labels); }
        if (o.ignoreMaxTimeDiff != null) t.ignoreMaxTimeDiff = o.ignoreMaxTimeDiff;
        if (o.ignoreMaxValDiff != null) t.ignoreMaxValDiff = o.ignoreMaxValDiff;
    }

    // ------------------------------------------------------------------
    // TS.CREATE / TS.ALTER
    // ------------------------------------------------------------------

    private static void tsCreate(Client c) {
        if (c.argc < 2) throw RedisError.wrongArgs("ts.create");
        Opts o = parseOpts(c, 2, "RCEDLI");
        if (get(c, c.arg(1), true) != null) throw new RedisError(E_EXISTS);
        RTs t = new RTs();
        apply(t, o);
        S().setKey(c, c.arg(1), ModTypes.obj(t));
        S().dirty++;
        S().notifyKeyspaceEvent('m', "ts.create", c.arg(1), c.db);
        c.addOk();
    }

    private static void tsAlter(Client c) {
        if (c.argc < 2) throw RedisError.wrongArgs("ts.alter");
        Opts o = parseOpts(c, 2, "RCDLI");
        RTs t = need(c, c.arg(1), true);
        apply(t, o);
        t.trim();
        touched(c, c.arg(1), "ts.alter");
        c.addOk();
    }

    // ------------------------------------------------------------------
    // TS.ADD / TS.MADD / TS.INCRBY / TS.DECRBY
    // ------------------------------------------------------------------

    private static double parseValue(byte[] a) {
        Double v = Util.string2d(a);
        if (v == null || v.isNaN() || v.isInfinite()) throw new RedisError("ERR TSDB: invalid value");
        return v;
    }

    /** upsertSample(): returns false when DUPLICATE_POLICY blocks the write. */
    private static boolean upsert(RTs t, long ts, double v, int policy) {
        Double old = t.samples.get(ts);
        if (old == null) { t.samples.put(ts, v); return true; }
        switch (policy) {
            case RTs.DP_BLOCK: return false;
            case RTs.DP_FIRST: return true;
            case RTs.DP_LAST: t.samples.put(ts, v); return true;
            case RTs.DP_MIN: t.samples.put(ts, Math.min(old, v)); return true;
            case RTs.DP_MAX: t.samples.put(ts, Math.max(old, v)); return true;
            default: t.samples.put(ts, old + v); return true;
        }
    }

    private static long addSampleTo(Client c, byte[] key, RTs t, long ts, double v, Integer onDup) {
        if (t.retention > 0 && !t.samples.isEmpty() && ts < t.samples.lastKey() - t.retention)
            throw new RedisError("ERR TSDB: Timestamp is older than retention");
        int policy = onDup != null ? onDup : t.duplicatePolicy;
        if (!upsert(t, ts, v, policy))
            throw new RedisError("ERR TSDB: Error at upsert, update is not supported when "
                    + "DUPLICATE_POLICY is set to BLOCK mode");
        t.trim();
        runRules(c, t);
        touched(c, key, "ts.add");
        return ts;
    }

    private static void tsAdd(Client c) {
        if (c.argc < 4) throw RedisError.wrongArgs("ts.add");
        long ts = parseTimestamp(c.arg(2));
        double v = parseValue(c.arg(3));
        Opts o = parseOpts(c, 4, "RCEDLIO");
        RTs t = get(c, c.arg(1), true);
        if (t == null) {
            t = new RTs();
            apply(t, o);
            S().setKey(c, c.arg(1), ModTypes.obj(t));
            S().dirty++;
        }
        c.addInt(addSampleTo(c, c.arg(1), t, ts, v, o.onDuplicate));
    }

    private static void tsMadd(Client c) {
        if (c.argc < 4 || (c.argc - 1) % 3 != 0) throw RedisError.wrongArgs("ts.madd");
        int n = (c.argc - 1) / 3;
        c.addArray(n);
        for (int i = 0; i < n; i++) {
            byte[] key = c.arg(1 + i * 3);
            try {
                long ts = parseTimestamp(c.arg(2 + i * 3));
                double v = parseValue(c.arg(3 + i * 3));
                RTs t = get(c, key, true);
                if (t == null) throw new RedisError(E_NOTTS);
                c.addInt(addSampleTo(c, key, t, ts, v, null));
            } catch (RedisError e) {
                c.addErr(e.msg);
            }
        }
    }

    private static void tsIncrBy(Client c, boolean decr) {
        String name = decr ? "ts.decrby" : "ts.incrby";
        if (c.argc < 3) throw RedisError.wrongArgs(name);
        Double add = Util.string2d(c.arg(2));
        if (add == null || add.isNaN())
            throw new RedisError("ERR TSDB: invalid increase/decrease value");
        Opts o = parseOpts(c, 3, "RCEDLIT");
        RTs t = get(c, c.arg(1), true);
        if (t == null) {
            t = new RTs();
            apply(t, o);
            S().setKey(c, c.arg(1), ModTypes.obj(t));
            S().dirty++;
        }
        long ts = o.hasTimestamp ? o.timestamp : S().mstime();
        if (!t.samples.isEmpty() && ts < t.samples.lastKey())
            throw new RedisError("TSDB: timestamp must be equal to or higher than the maximum existing timestamp");
        double base = t.samples.isEmpty() ? 0 : t.samples.get(t.samples.lastKey());
        double v = decr ? base - add : base + add;
        t.samples.put(ts, v);
        t.trim();
        runRules(c, t);
        touched(c, c.arg(1), name);
        c.addInt(ts);
    }

    // ------------------------------------------------------------------
    // TS.DEL / TS.GET
    // ------------------------------------------------------------------

    private static void tsDel(Client c) {
        if (c.argc != 4) throw RedisError.wrongArgs("ts.del");
        RTs t = need(c, c.arg(1), true);
        Long from = Util.string2ll(c.arg(2));
        Long to = Util.string2ll(c.arg(3));
        if (from == null) throw new RedisError("ERR TSDB: wrong fromTimestamp");
        if (to == null) throw new RedisError("ERR TSDB: wrong toTimestamp");
        int n = 0;
        if (from > to) { c.addInt(0); return; }
        for (Long k : new ArrayList<>(t.samples.subMap(from, true, to, true).keySet())) {
            t.samples.remove(k);
            n++;
        }
        if (n > 0) touched(c, c.arg(1), "ts.del");
        c.addInt(n);
    }

    private static void tsGet(Client c) {
        if (c.argc < 2) throw RedisError.wrongArgs("ts.get");
        boolean latest = false;
        for (int i = 2; i < c.argc; i++) {
            if (!c.argIs(i, "LATEST")) throw RedisError.syntax();
            latest = true;
        }
        RTs t = need(c, c.arg(1), false);
        if (latest) {
            double[] open = openBucket(c, c.arg(1), t);
            if (open != null) { addSample(c, (long) open[0], open[1]); return; }
        }
        if (t.samples.isEmpty()) { c.addEmptyArray(); return; }
        long ts = t.samples.lastKey();
        addSample(c, ts, t.samples.get(ts));
    }

    /**
     * LATEST on a compaction destination also reports the bucket the source is
     * still filling, which no TS.ADD has flushed yet. Returns {timestamp,
     * value} or null. RedisTimeSeries only keeps this index for database 0.
     */
    private static double[] openBucket(Client c, byte[] key, RTs dst) {
        if (c.db != 0 || dst.sourceKey == null) return null;
        RObj o = S().dbs[c.db].dict.get(new Key(dst.sourceKey));
        if (o == null || !(o.ptr instanceof RTs)) return null;
        RTs srcTs = (RTs) o.ptr;
        RTs.Rule rule = null;
        for (RTs.Rule r : srcTs.rules) if (java.util.Arrays.equals(r.dest, key)) rule = r;
        if (rule == null || srcTs.samples.isEmpty()) return null;
        long last = srcTs.samples.lastKey();
        long start = bucketStart(last, rule.bucketDuration, rule.alignTimestamp);
        if (start + rule.bucketDuration <= last) return null;     /* already flushed */
        List<Long> ts = new ArrayList<>();
        List<Double> vs = new ArrayList<>();
        for (Map.Entry<Long, Double> e : srcTs.samples.entrySet()) {
            if (e.getKey() >= start && e.getKey() < start + rule.bucketDuration) {
                ts.add(e.getKey());
                vs.add(e.getValue());
            }
        }
        if (vs.isEmpty()) return null;
        String agg = rule.agg.toLowerCase(java.util.Locale.ROOT);
        double v = agg.equals("twa")
                ? twa(srcTs.samples, ts, vs, start, rule.bucketDuration)
                : aggregate(agg, ts, vs, start, rule.bucketDuration);
        return new double[]{start, v};
    }

    /** The series plus one extra sample, used for the LATEST bucket. */
    private static RTs withExtra(RTs t, long ts, double v) {
        RTs copy = new RTs();
        copy.samples.putAll(t.samples);
        copy.samples.put(ts, v);
        return copy;
    }

    // ------------------------------------------------------------------
    // range
    // ------------------------------------------------------------------

    private static final class RangeOpts {
        long from, to;
        long count = -1;
        String agg;
        long bucket;
        long align;
        boolean hasAlign;
        String alignMode = "";       // "start" / "end" / explicit
        int bucketTs = 0;            // 0 start, 1 end, 2 mid
        boolean empty;
        Double filterMin, filterMax;
        List<Long> filterTs;
        boolean latest;
        boolean explicitFrom, explicitTo;
    }

    private static RangeOpts parseRange(Client c, int from, String cmdName) {
        RangeOpts r = new RangeOpts();
        if (Util.equalsIgnoreCase(c.arg(from), "-")) { r.from = Long.MIN_VALUE; }
        else {
            Long v = Util.string2ll(c.arg(from));
            if (v == null || v < 0) throw new RedisError("ERR TSDB: wrong fromTimestamp");
            r.from = v;
            r.explicitFrom = true;
        }
        if (Util.equalsIgnoreCase(c.arg(from + 1), "+")) { r.to = Long.MAX_VALUE; }
        else {
            Long v = Util.string2ll(c.arg(from + 1));
            if (v == null || v < 0) throw new RedisError("ERR TSDB: wrong toTimestamp");
            r.to = v;
            r.explicitTo = true;
        }
        int i = from + 2;
        while (i < c.argc) {
            if (c.argIs(i, "COUNT")) {
                if (i + 1 >= c.argc) throw new RedisError("ERR TSDB: Invalid COUNT value");
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v < 1) throw new RedisError("ERR TSDB: Invalid COUNT value");
                r.count = v;
                i += 2;
            } else if (c.argIs(i, "LATEST")) {
                r.latest = true;
                i++;
            } else if (c.argIs(i, "EMPTY")) {
                r.empty = true;
                i++;
            } else if (c.argIs(i, "ALIGN")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                byte[] a = c.arg(i + 1);
                r.hasAlign = true;
                if (Util.equalsIgnoreCase(a, "-") || Util.equalsIgnoreCase(a, "start")) r.alignMode = "start";
                else if (Util.equalsIgnoreCase(a, "+") || Util.equalsIgnoreCase(a, "end")) r.alignMode = "end";
                else {
                    Long v = Util.string2ll(a);
                    if (v == null) throw new RedisError("ERR TSDB: wrong alignTimestamp");
                    r.align = v;
                    r.alignMode = "ts";
                }
                i += 2;
            } else if (c.argIs(i, "BUCKETTIMESTAMP")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                byte[] a = c.arg(i + 1);
                if (Util.equalsIgnoreCase(a, "-") || Util.equalsIgnoreCase(a, "start")) r.bucketTs = 0;
                else if (Util.equalsIgnoreCase(a, "+") || Util.equalsIgnoreCase(a, "end")) r.bucketTs = 1;
                else if (Util.equalsIgnoreCase(a, "~") || Util.equalsIgnoreCase(a, "mid")) r.bucketTs = 2;
                else throw new RedisError("ERR TSDB: unknown BUCKETTIMESTAMP parameter");
                i += 2;
            } else if (c.argIs(i, "FILTER_BY_VALUE")) {
                if (i + 2 >= c.argc)
                    throw new RedisError("ERR TSDB: FILTER_BY_VALUE one or more arguments are missing");
                Double a = Util.string2d(c.arg(i + 1));
                Double b = Util.string2d(c.arg(i + 2));
                if (a == null || b == null)
                    throw new RedisError("ERR TSDB: cannot parse filter value");
                r.filterMin = a; r.filterMax = b;
                i += 3;
            } else if (c.argIs(i, "FILTER_BY_TS")) {
                r.filterTs = new ArrayList<>();
                i++;
                while (i < c.argc) {
                    Long v = Util.string2ll(c.arg(i));
                    if (v == null) break;
                    r.filterTs.add(v);
                    i++;
                }
                if (r.filterTs.isEmpty())
                    throw new RedisError("ERR TSDB: FILTER_BY_TS one or more arguments are missing");
            } else if (c.argIs(i, "AGGREGATION")) {
                if (i + 2 >= c.argc) throw new RedisError("ERR TSDB: Couldn't parse AGGREGATION");
                r.agg = c.argLower(i + 1);
                if (!AGGS.contains(r.agg)) throw new RedisError("ERR TSDB: Unknown aggregation type");
                Long v = Util.string2ll(c.arg(i + 2));
                if (v == null) throw new RedisError("ERR TSDB: Couldn't parse bucketDuration");
                if (v <= 0) throw new RedisError("ERR TSDB: bucketDuration must be greater than zero");
                r.bucket = v;
                i += 3;
            } else {
                /* the module scans for the keywords it knows and leaves the
                 * rest alone, so an unknown word is simply skipped */
                i++;
            }
        }
        if (r.hasAlign) {
            if (r.alignMode.equals("start") && !r.explicitFrom)
                throw new RedisError("ERR TSDB: start alignment can only be used with explicit start timestamp");
            if (r.alignMode.equals("end") && !r.explicitTo)
                throw new RedisError("ERR TSDB: end alignment can only be used with explicit end timestamp");
        }
        return r;
    }

    private static final List<String> AGGS = java.util.Arrays.asList(
            "avg", "sum", "min", "max", "range", "count", "first", "last",
            "std.p", "std.s", "var.p", "var.s", "twa");

    /** The raw samples of `t` inside the requested window, after the filters. */
    private static TreeMap<Long, Double> pick(RTs t, RangeOpts r) {
        TreeMap<Long, Double> out = new TreeMap<>();
        long lo = r.from == Long.MIN_VALUE ? Long.MIN_VALUE : r.from;
        long hi = r.to == Long.MAX_VALUE ? Long.MAX_VALUE : r.to;
        for (Map.Entry<Long, Double> e : t.samples.entrySet()) {
            long ts = e.getKey();
            if (ts < lo || ts > hi) continue;
            if (r.filterMin != null && (e.getValue() < r.filterMin || e.getValue() > r.filterMax)) continue;
            if (r.filterTs != null && !r.filterTs.contains(ts)) continue;
            out.put(ts, e.getValue());
        }
        return out;
    }

    private static double aggregate(String agg, List<Long> ts, List<Double> vs, long bucketStart, long bucketDur) {
        int n = vs.size();
        switch (agg) {
            case "count": return n;
            case "first": return vs.get(0);
            case "last": return vs.get(n - 1);
            case "min": { double m = vs.get(0); for (double v : vs) m = Math.min(m, v); return m; }
            case "max": { double m = vs.get(0); for (double v : vs) m = Math.max(m, v); return m; }
            case "range": {
                double mn = vs.get(0), mx = vs.get(0);
                for (double v : vs) { mn = Math.min(mn, v); mx = Math.max(mx, v); }
                return mx - mn;
            }
            case "sum": { double s = 0; for (double v : vs) s += v; return s; }
            case "avg": { double s = 0; for (double v : vs) s += v; return s / n; }
            case "twa":
                /* handled in buckets(), which can see the neighbouring samples */
                return vs.get(0);
            default: {
                double s = 0;
                for (double v : vs) s += v;
                double mean = s / n;
                double sq = 0;
                for (double v : vs) sq += (v - mean) * (v - mean);
                switch (agg) {
                    case "var.p": return sq / n;
                    case "var.s": return n > 1 ? sq / (n - 1) : 0;
                    case "std.p": return Math.sqrt(sq / n);
                    default: return n > 1 ? Math.sqrt(sq / (n - 1)) : 0;
                }
            }
        }
    }

    private static TreeMap<Long, Double> buckets(RTs t, RangeOpts r) {
        TreeMap<Long, Double> raw = pick(t, r);
        if (r.agg == null) return raw;
        long align = 0;
        if (r.hasAlign) {
            if (r.alignMode.equals("start")) align = r.from;
            else if (r.alignMode.equals("end")) align = r.to;
            else align = r.align;
        }
        TreeMap<Long, List<Long>> bts = new TreeMap<>();
        TreeMap<Long, List<Double>> bvs = new TreeMap<>();
        for (Map.Entry<Long, Double> e : raw.entrySet()) {
            long start = bucketStart(e.getKey(), r.bucket, align);
            bts.computeIfAbsent(start, x -> new ArrayList<>()).add(e.getKey());
            bvs.computeIfAbsent(start, x -> new ArrayList<>()).add(e.getValue());
        }
        TreeMap<Long, Double> out = new TreeMap<>();
        if (bts.isEmpty()) return out;
        long first = bts.firstKey(), last = bts.lastKey();
        for (long b = first; b <= last; b += r.bucket) {
            List<Double> vs = bvs.get(b);
            long label = r.bucketTs == 0 ? b : (r.bucketTs == 1 ? b + r.bucket : b + r.bucket / 2);
            if (vs == null) {
                if (r.empty) out.put(label, Double.NaN);
                continue;
            }
            if (r.agg.equals("twa")) out.put(label, twa(raw, bts.get(b), vs, b, r.bucket));
            else out.put(label, aggregate(r.agg, bts.get(b), vs, b, r.bucket));
        }
        return out;
    }

    /**
     * AGG_TWA: the trapezoid rule over [bucketStart, bucketStart+duration],
     * with the value at each edge interpolated from the sample just outside the
     * bucket (or held constant when there is none).
     */
    private static double twa(TreeMap<Long, Double> all, List<Long> ts, List<Double> vs,
                              long bucketStart, long bucketDur) {
        long bucketEnd = bucketStart + bucketDur;
        List<Long> xs = new ArrayList<>();
        List<Double> ys = new ArrayList<>();
        Map.Entry<Long, Double> prev = all.lowerEntry(bucketStart);
        Map.Entry<Long, Double> next = all.ceilingEntry(bucketEnd);
        if (ts.get(0) != bucketStart) {
            double v0 = (prev != null)
                    ? interp(prev.getKey(), prev.getValue(), ts.get(0), vs.get(0), bucketStart)
                    : vs.get(0);
            xs.add(bucketStart);
            ys.add(v0);
        }
        xs.addAll(ts);
        ys.addAll(vs);
        int last = ts.size() - 1;
        double vEnd = (next != null)
                ? interp(ts.get(last), vs.get(last), next.getKey(), next.getValue(), bucketEnd)
                : vs.get(last);
        xs.add(bucketEnd);
        ys.add(vEnd);
        double area = 0;
        for (int i = 0; i + 1 < xs.size(); i++) {
            area += (ys.get(i) + ys.get(i + 1)) / 2.0 * (xs.get(i + 1) - xs.get(i));
        }
        return area / bucketDur;
    }

    private static double interp(long x0, double y0, long x1, double y1, long x) {
        if (x1 == x0) return y0;
        return y0 + (y1 - y0) * ((double) (x - x0) / (double) (x1 - x0));
    }

    private static long bucketStart(long ts, long bucket, long align) {
        long d = Math.floorMod(ts - align, bucket);
        return ts - d;
    }

    private static void rangeReply(Client c, byte[] key, RTs t, RangeOpts r, boolean rev) {
        if (r.latest) {
            double[] open = openBucket(c, key, t);
            if (open != null) t = withExtra(t, (long) open[0], open[1]);
        }
        TreeMap<Long, Double> res = buckets(t, r);
        List<Long> keys = new ArrayList<>(res.keySet());
        if (rev) Collections.reverse(keys);
        if (r.count >= 0 && keys.size() > r.count) keys = keys.subList(0, (int) r.count);
        c.addArray(keys.size());
        for (long k : keys) addSample(c, k, res.get(k));
    }

    private static void tsRange(Client c, boolean rev) {
        String name = rev ? "ts.revrange" : "ts.range";
        if (c.argc < 4) throw RedisError.wrongArgs(name);
        RangeOpts r = parseRange(c, 2, name);
        RTs t = need(c, c.arg(1), false);
        rangeReply(c, c.arg(1), t, r, rev);
    }

    // ------------------------------------------------------------------
    // label matchers
    // ------------------------------------------------------------------

    private static final class Matcher {
        byte[] label;
        boolean negate;
        boolean exists;                            // true: "label!=", false: "label="
        List<byte[]> values = new ArrayList<>();   // empty => presence test
        boolean equality;                          // counts towards the "one matcher" rule
    }

    private static Matcher parseMatcher(byte[] raw) {
        String s = Util.s(raw);
        int ne = s.indexOf("!=");
        Matcher m = new Matcher();
        int eq;
        if (ne >= 0) { m.negate = true; eq = ne; }
        else {
            eq = s.indexOf('=');
            if (eq < 0) throw new RedisError("ERR TSDB: failed parsing labels");
        }
        m.label = Util.b(s.substring(0, eq));
        String rest = s.substring(eq + (m.negate ? 2 : 1));
        if (rest.startsWith("(") && rest.endsWith(")")) {
            String inner = rest.substring(1, rest.length() - 1);
            if (!inner.isEmpty()) for (String v : inner.split(",", -1)) m.values.add(Util.b(v.trim()));
            m.equality = !m.negate;
        } else if (rest.isEmpty()) {
            /* "label=" selects the series without that label and "label!=" the
             * ones that have it; the latter counts as a positive matcher. */
            m.exists = m.negate;
            m.negate = false;
            m.equality = m.exists;
        } else {
            m.values.add(Util.b(rest));
            m.equality = !m.negate;
        }
        return m;
    }

    private static boolean matches(RTs t, List<Matcher> ms) {
        for (Matcher m : ms) {
            byte[] v = t.labels.get(new Key(m.label));
            boolean hit;
            if (m.values.isEmpty()) hit = (v != null) == m.exists;
            else {
                hit = false;
                if (v != null) for (byte[] w : m.values) if (java.util.Arrays.equals(v, w)) { hit = true; break; }
            }
            if (m.negate == hit) return false;
        }
        return true;
    }

    /** The keys matching `ms`, in key order, like the module's index. */
    private static List<byte[]> query(Client c, List<Matcher> ms) {
        boolean any = false;
        for (Matcher m : ms) if (m.equality) any = true;
        if (!any) throw new RedisError("ERR TSDB: please provide at least one matcher");
        List<Key> keys = new ArrayList<>(S().dbs[c.db].dict.keySet());
        Collections.sort(keys);
        List<byte[]> out = new ArrayList<>();
        for (Key k : keys) {
            RObj o = S().dbs[c.db].dict.get(k);
            if (o == null || o.type != RObj.MODULE || !(o.ptr instanceof RTs)) continue;
            if (matches((RTs) o.ptr, ms)) out.add(k.b);
        }
        return out;
    }

    private static void tsQueryIndex(Client c) {
        if (c.argc < 2) throw RedisError.wrongArgs("ts.queryindex");
        List<Matcher> ms = new ArrayList<>();
        for (int i = 1; i < c.argc; i++) ms.add(parseMatcher(c.arg(i)));
        List<byte[]> keys = query(c, ms);
        c.addArray(keys.size());
        for (byte[] k : keys) c.addBulk(k);
    }

    // ------------------------------------------------------------------
    // multi-key commands
    // ------------------------------------------------------------------

    private static final class MultiOpts {
        boolean withLabels;
        List<byte[]> selected;
        List<Matcher> filter = new ArrayList<>();
        byte[] groupBy;
        String reduce;
    }

    private static int parseMulti(Client c, int from, MultiOpts m, String name) {
        int i = from;
        boolean sawFilter = false;
        while (i < c.argc) {
            if (c.argIs(i, "WITHLABELS")) { m.withLabels = true; i++; }
            else if (c.argIs(i, "SELECTED_LABELS")) {
                m.selected = new ArrayList<>();
                i++;
                while (i < c.argc && !isMultiKeyword(c, i)) { m.selected.add(c.arg(i)); i++; }
                if (m.selected.isEmpty()) throw RedisError.wrongArgs(name);
            } else if (c.argIs(i, "FILTER")) {
                i++;
                while (i < c.argc && !c.argIs(i, "GROUPBY")) { m.filter.add(parseMatcher(c.arg(i))); i++; }
                if (m.filter.isEmpty()) throw RedisError.wrongArgs(name);
                sawFilter = true;
            } else if (c.argIs(i, "GROUPBY")) {
                if (i + 3 >= c.argc) throw RedisError.wrongArgs(name);
                m.groupBy = c.arg(i + 1);
                if (!c.argIs(i + 2, "REDUCE")) throw RedisError.syntax();
                m.reduce = c.argLower(i + 3);
                if (!AGGS.contains(m.reduce)) throw new RedisError("ERR TSDB: Invalid reducer type");
                i += 4;
            } else {
                return i;           /* caller-specific option */
            }
        }
        if (!sawFilter) throw RedisError.wrongArgs(name);
        return i;
    }

    private static boolean isMultiKeyword(Client c, int i) {
        return c.argIs(i, "FILTER") || c.argIs(i, "WITHLABELS") || c.argIs(i, "GROUPBY");
    }

    private static void replyLabels(Client c, RTs t, MultiOpts m) {
        if (m.withLabels) {
            c.addArray(t.labels.size());
            for (Map.Entry<Key, byte[]> e : t.labels.entrySet()) {
                c.addArray(2);
                c.addBulk(e.getKey().b);
                c.addBulk(e.getValue());
            }
        } else if (m.selected != null) {
            c.addArray(m.selected.size());
            for (byte[] name : m.selected) {
                c.addArray(2);
                c.addBulk(name);
                byte[] v = t.labels.get(new Key(name));
                if (v == null) c.addNil(); else c.addBulk(v);
            }
        } else {
            c.addEmptyArray();
        }
    }

    private static void tsMget(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("ts.mget");
        MultiOpts m = new MultiOpts();
        int i = 1;
        boolean latest = false;
        while (i < c.argc && c.argIs(i, "LATEST")) { latest = true; i++; }
        final boolean wantLatest = latest;
        parseMulti(c, i, m, "ts.mget");
        List<byte[]> keys = query(c, m.filter);
        c.addArray(keys.size());
        for (byte[] k : keys) {
            RTs t = (RTs) S().dbs[c.db].dict.get(new Key(k)).ptr;
            RTs view = t;
            if (wantLatest) {
                double[] open = openBucket(c, k, t);
                if (open != null) view = withExtra(t, (long) open[0], open[1]);
            }
            c.addArray(3);
            c.addBulk(k);
            replyLabels(c, t, m);
            if (view.samples.isEmpty()) c.addEmptyArray();
            else {
                long ts = view.samples.lastKey();
                c.addArray(2);
                c.addInt(ts);
                if (c.resp >= 3) c.addDouble(view.samples.get(ts));
                else c.addSimple(Util.s(fmt(view.samples.get(ts))));
            }
        }
    }

    private static void tsMrange(Client c, boolean rev) {
        String name = rev ? "ts.mrevrange" : "ts.mrange";
        if (c.argc < 5) throw RedisError.wrongArgs(name);
        /* the range options come first, then the multi-key options */
        int split = c.argc;
        for (int i = 2; i < c.argc; i++) {
            if (c.argIs(i, "FILTER") || c.argIs(i, "WITHLABELS") || c.argIs(i, "SELECTED_LABELS")) {
                split = i;
                break;
            }
        }
        RangeOpts r = parseRangeSlice(c, 0, split, name);
        MultiOpts m = new MultiOpts();
        parseMulti(c, split, m, name);
        List<byte[]> keys = query(c, m.filter);

        if (m.groupBy != null) {
            LinkedHashMap<String, List<byte[]>> groups = new LinkedHashMap<>();
            for (byte[] k : keys) {
                RTs t = (RTs) S().dbs[c.db].dict.get(new Key(k)).ptr;
                byte[] v = t.labels.get(new Key(m.groupBy));
                if (v == null) continue;
                groups.computeIfAbsent(Util.s(m.groupBy) + "=" + Util.s(v), x -> new ArrayList<>()).add(k);
            }
            c.addArray(groups.size());
            for (Map.Entry<String, List<byte[]>> g : groups.entrySet()) {
                TreeMap<Long, List<Double>> merged = new TreeMap<>();
                for (byte[] k : g.getValue()) {
                    RTs t = (RTs) S().dbs[c.db].dict.get(new Key(k)).ptr;
                    for (Map.Entry<Long, Double> e : buckets(t, r).entrySet())
                        merged.computeIfAbsent(e.getKey(), x -> new ArrayList<>()).add(e.getValue());
                }
                List<Long> ks = new ArrayList<>(merged.keySet());
                if (rev) Collections.reverse(ks);
                if (r.count >= 0 && ks.size() > r.count) ks = ks.subList(0, (int) r.count);
                c.addArray(3);
                c.addBulk(g.getKey());
                c.addEmptyArray();
                c.addArray(ks.size());
                for (long ts : ks) {
                    List<Double> vs = merged.get(ts);
                    addSample(c, ts, aggregate(m.reduce, Collections.singletonList(ts), vs, ts, 1));
                }
            }
            return;
        }

        c.addArray(keys.size());
        for (byte[] k : keys) {
            RTs t = (RTs) S().dbs[c.db].dict.get(new Key(k)).ptr;
            c.addArray(3);
            c.addBulk(k);
            replyLabels(c, t, m);
            rangeReply(c, k, t, r, rev);
        }
    }

    /** parseRange() over argv[2 .. end). */
    private static RangeOpts parseRangeSlice(Client c, int unused, int end, String name) {
        int saved = c.argc;
        c.argc = end;
        try {
            return parseRange(c, 1, name);
        } finally {
            c.argc = saved;
        }
    }

    // ------------------------------------------------------------------
    // compaction rules
    // ------------------------------------------------------------------

    private static void tsCreateRule(Client c) {
        if (c.argc < 6) throw RedisError.wrongArgs("ts.createrule");
        if (!c.argIs(3, "AGGREGATION")) throw RedisError.syntax();
        String agg = c.argLower(4);
        if (!AGGS.contains(agg)) throw new RedisError("ERR TSDB: Unknown aggregation type");
        Long bucket = Util.string2ll(c.arg(5));
        if (bucket == null || bucket <= 0)
            throw new RedisError("ERR TSDB: bucketDuration must be greater than zero");
        long align = 0;
        if (c.argc > 6) {
            Long v = Util.string2ll(c.arg(6));
            if (v == null) throw new RedisError("ERR TSDB: wrong alignTimestamp");
            align = v;
        }
        if (java.util.Arrays.equals(c.arg(1), c.arg(2)))
            throw new RedisError("ERR TSDB: the source key and destination key should be different");
        RTs src = need(c, c.arg(1), true);
        RTs dst = need(c, c.arg(2), true);
        if (dst.sourceKey != null) throw new RedisError("ERR TSDB: the destination key already has a src rule");
        for (RTs.Rule r : src.rules) {
            if (java.util.Arrays.equals(r.dest, c.arg(2)))
                throw new RedisError("ERR TSDB: the destination key already has a src rule");
        }
        src.rules.add(new RTs.Rule(c.arg(2).clone(), bucket, agg.toUpperCase(java.util.Locale.ROOT), align));
        dst.sourceKey = c.arg(1).clone();
        touched(c, c.arg(1), "ts.createrule");
        c.addOk();
    }

    private static void tsDeleteRule(Client c) {
        if (c.argc != 3) throw RedisError.wrongArgs("ts.deleterule");
        RTs src = need(c, c.arg(1), true);
        RTs.Rule found = null;
        for (RTs.Rule r : src.rules) if (java.util.Arrays.equals(r.dest, c.arg(2))) found = r;
        if (found == null) throw new RedisError("ERR TSDB: compaction rule does not exist");
        src.rules.remove(found);
        RTs dst = get(c, c.arg(2), true);
        if (dst != null) dst.sourceKey = null;
        touched(c, c.arg(1), "ts.deleterule");
        c.addOk();
    }

    /** Flush every bucket that the latest sample has closed into the destinations. */
    private static void runRules(Client c, RTs src) {
        if (src.rules.isEmpty() || src.samples.isEmpty()) return;
        long last = src.samples.lastKey();
        for (RTs.Rule r : src.rules) {
            RObj o = S().dbs[c.db].dict.get(new Key(r.dest));
            if (o == null || !(o.ptr instanceof RTs)) continue;
            RTs dst = (RTs) o.ptr;
            TreeMap<Long, List<Long>> bts = new TreeMap<>();
            TreeMap<Long, List<Double>> bvs = new TreeMap<>();
            for (Map.Entry<Long, Double> e : src.samples.entrySet()) {
                long start = bucketStart(e.getKey(), r.bucketDuration, r.alignTimestamp);
                bts.computeIfAbsent(start, x -> new ArrayList<>()).add(e.getKey());
                bvs.computeIfAbsent(start, x -> new ArrayList<>()).add(e.getValue());
            }
            for (Map.Entry<Long, List<Double>> e : bvs.entrySet()) {
                long start = e.getKey();
                if (start + r.bucketDuration > last) continue;      /* bucket still open */
                if (dst.samples.containsKey(start)) continue;
                dst.samples.put(start, aggregate(r.agg.toLowerCase(java.util.Locale.ROOT),
                        bts.get(start), e.getValue(), start, r.bucketDuration));
            }
            dst.trim();
            S().signalModifiedKey(c.db, new Key(r.dest));
        }
    }

    // ------------------------------------------------------------------
    // TS.INFO
    // ------------------------------------------------------------------

    private static void tsInfo(Client c) {
        if (c.argc < 2) throw RedisError.wrongArgs("ts.info");
        boolean debug = c.argc > 2 && c.argIs(2, "DEBUG");
        RTs t = need(c, c.arg(1), false);
        c.addMap(debug ? 16 : 14);
        c.addSimple("totalSamples"); c.addInt(t.samples.size());
        c.addSimple("memoryUsage"); c.addInt(t.memoryUsage());
        c.addSimple("firstTimestamp"); c.addInt(t.firstTimestamp());
        c.addSimple("lastTimestamp"); c.addInt(t.lastTimestamp());
        c.addSimple("retentionTime"); c.addInt(t.retention);
        c.addSimple("chunkCount"); c.addInt(1);
        c.addSimple("chunkSize"); c.addInt(t.chunkSize);
        c.addSimple("chunkType"); c.addSimple(t.uncompressed ? "uncompressed" : "compressed");
        c.addSimple("duplicatePolicy"); c.addSimple(RTs.DP_NAMES[t.duplicatePolicy]);
        c.addSimple("labels");
        c.addArray(t.labels.size());
        for (Map.Entry<Key, byte[]> e : t.labels.entrySet()) {
            c.addArray(2);
            c.addBulk(e.getKey().b);
            c.addBulk(e.getValue());
        }
        c.addSimple("sourceKey");
        if (t.sourceKey == null) c.addNil(); else c.addBulk(t.sourceKey);
        c.addSimple("rules");
        c.addArray(t.rules.size());
        for (RTs.Rule r : t.rules) {
            c.addArray(4);
            c.addBulk(r.dest);
            c.addInt(r.bucketDuration);
            c.addSimple(r.agg);
            c.addInt(r.alignTimestamp);
        }
        c.addSimple("ignoreMaxTimeDiff"); c.addInt(t.ignoreMaxTimeDiff);
        c.addSimple("ignoreMaxValDiff"); c.addBulk(ProbCmds.dtoa(t.ignoreMaxValDiff));
        if (debug) {
            /* TS.INFO DEBUG adds the chunk layout; we keep every sample in one
             * chunk, so there is exactly one entry. */
            c.addSimple("keySelfName"); c.addBulk(c.arg(1));
            c.addSimple("Chunks");
            c.addArray(1);
            c.addMap(5);
            c.addSimple("startTimestamp"); c.addInt(t.firstTimestamp());
            c.addSimple("endTimestamp"); c.addInt(t.lastTimestamp());
            c.addSimple("samples"); c.addInt(t.samples.size());
            c.addSimple("size"); c.addInt(t.chunkSize);
            c.addSimple("bytesPerSample");
            c.addBulk(ProbCmds.dtoa(t.samples.isEmpty() ? 0 : (double) t.chunkSize / t.samples.size()));
        }
    }

    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("ts.create", -1, "write denyoom module", 1, 1, 1, TsCmds::tsCreate);
        CommandTable.reg("ts.alter", -1, "write denyoom module", 1, 1, 1, TsCmds::tsAlter);
        CommandTable.reg("ts.add", -1, "write denyoom module", 1, 1, 1, TsCmds::tsAdd);
        CommandTable.reg("ts.madd", -1, "write denyoom module", 1, -1, 3, TsCmds::tsMadd);
        CommandTable.reg("ts.incrby", -1, "write denyoom module", 1, 1, 1, c -> tsIncrBy(c, false));
        CommandTable.reg("ts.decrby", -1, "write denyoom module", 1, 1, 1, c -> tsIncrBy(c, true));
        CommandTable.reg("ts.del", -1, "write module", 1, 1, 1, TsCmds::tsDel);
        CommandTable.reg("ts.get", -1, "readonly module", 1, 1, 1, TsCmds::tsGet);
        CommandTable.reg("ts.range", -1, "readonly module", 1, 1, 1, c -> tsRange(c, false));
        CommandTable.reg("ts.revrange", -1, "readonly module", 1, 1, 1, c -> tsRange(c, true));
        CommandTable.reg("ts.mget", -1, "readonly module", 0, 0, 0, TsCmds::tsMget);
        CommandTable.reg("ts.mrange", -1, "readonly module", 0, 0, 0, c -> tsMrange(c, false));
        CommandTable.reg("ts.mrevrange", -1, "readonly module", 0, 0, 0, c -> tsMrange(c, true));
        CommandTable.reg("ts.queryindex", -1, "readonly module", 0, 0, 0, TsCmds::tsQueryIndex);
        CommandTable.reg("ts.createrule", -1, "write module fast", 1, 1, 1, TsCmds::tsCreateRule);
        CommandTable.reg("ts.deleterule", -1, "write module", 1, 1, 1, TsCmds::tsDeleteRule);
        CommandTable.reg("ts.info", -1, "readonly module", 1, 1, 1, TsCmds::tsInfo);
    }
}
