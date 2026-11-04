package redis;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** The value behind a TSDB-TYPE key: samples, labels and compaction rules. */
public final class RTs implements ModVal {

    /** DUPLICATE_POLICY / ON_DUPLICATE. */
    public static final int DP_BLOCK = 0, DP_FIRST = 1, DP_LAST = 2, DP_MIN = 3, DP_MAX = 4, DP_SUM = 5;

    public static final String[] DP_NAMES = {"block", "first", "last", "min", "max", "sum"};

    public static int policyByName(String s) {
        for (int i = 0; i < DP_NAMES.length; i++) if (DP_NAMES[i].equals(s)) return i;
        return -1;
    }

    /** A compaction rule attached to a source series. */
    public static final class Rule {
        public byte[] dest;
        public long bucketDuration;
        public String agg;            // upper case name as TS.INFO prints it
        public long alignTimestamp;

        public Rule(byte[] dest, long bucketDuration, String agg, long alignTimestamp) {
            this.dest = dest; this.bucketDuration = bucketDuration;
            this.agg = agg; this.alignTimestamp = alignTimestamp;
        }
    }

    public final TreeMap<Long, Double> samples = new TreeMap<>();
    public long retention = 0;
    public long chunkSize = 4096;
    public int duplicatePolicy = DP_BLOCK;
    public boolean uncompressed = false;
    public final LinkedHashMap<Key, byte[]> labels = new LinkedHashMap<>();
    public byte[] sourceKey = null;
    public final List<Rule> rules = new ArrayList<>();
    public long ignoreMaxTimeDiff = 0;
    public double ignoreMaxValDiff = 0;

    @Override public String moduleTypeName() { return "TSDB-TYPE"; }
    @Override public boolean moduleCopyable() { return true; }
    @Override public int moduleId() { return ModTypes.ID_TS; }

    public long firstTimestamp() { return samples.isEmpty() ? 0 : samples.firstKey(); }
    public long lastTimestamp() { return samples.isEmpty() ? 0 : samples.lastKey(); }

    /** An estimate in the shape of the module's; the corpus normalises it. */
    public long memoryUsage() {
        long n = 760 + chunkSize;
        for (Map.Entry<Key, byte[]> e : labels.entrySet()) n += 64 + e.getKey().b.length + e.getValue().length;
        for (Rule r : rules) n += 64 + r.dest.length;
        return n;
    }

    /** Drop everything older than the retention window. */
    public void trim() {
        if (retention <= 0 || samples.isEmpty()) return;
        long cutoff = samples.lastKey() - retention;
        while (!samples.isEmpty() && samples.firstKey() < cutoff) samples.pollFirstEntry();
    }

    @Override public byte[] moduleSave() {
        ModTypes.Out o = new ModTypes.Out();
        o.u8(1);
        o.i64(retention);
        o.i64(chunkSize);
        o.i32(duplicatePolicy);
        o.u8(uncompressed ? 1 : 0);
        o.i64(ignoreMaxTimeDiff);
        o.dbl(ignoreMaxValDiff);
        o.u8(sourceKey == null ? 0 : 1);
        if (sourceKey != null) o.blob(sourceKey);
        o.i32(labels.size());
        for (Map.Entry<Key, byte[]> e : labels.entrySet()) { o.blob(e.getKey().b); o.blob(e.getValue()); }
        o.i32(rules.size());
        for (Rule r : rules) { o.blob(r.dest); o.i64(r.bucketDuration); o.str(r.agg); o.i64(r.alignTimestamp); }
        o.i32(samples.size());
        for (Map.Entry<Long, Double> e : samples.entrySet()) { o.i64(e.getKey()); o.dbl(e.getValue()); }
        return o.bytes();
    }

    public static Object moduleLoad(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        RTs t = new RTs();
        t.retention = in.i64();
        t.chunkSize = in.i64();
        t.duplicatePolicy = in.i32();
        t.uncompressed = in.u8() != 0;
        t.ignoreMaxTimeDiff = in.i64();
        t.ignoreMaxValDiff = in.dbl();
        if (in.u8() != 0) t.sourceKey = in.blob();
        int nl = in.i32();
        for (int i = 0; i < nl; i++) t.labels.put(new Key(in.blob()), in.blob());
        int nr = in.i32();
        for (int i = 0; i < nr; i++) t.rules.add(new Rule(in.blob(), in.i64(), in.str(), in.i64()));
        int ns = in.i32();
        for (int i = 0; i < ns; i++) t.samples.put(in.i64(), in.dbl());
        return t;
    }
}
