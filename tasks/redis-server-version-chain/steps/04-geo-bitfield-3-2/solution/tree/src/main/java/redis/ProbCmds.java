package redis;

import java.util.ArrayList;
import java.util.List;

/**
 * The RedisBloom command surface: BF.*, CF.*, CMS.*, TOPK.* and TDIGEST.*.
 *
 * Argument handling follows src/rebloom.c, src/rm_cms.c and src/rm_topk.c of
 * modsrc/RedisBloom, including the module's habit of replying with a bare
 * message (no "ERR " code) wherever it calls RedisModule_ReplyWithError with a
 * string that has no code of its own.
 */
public final class ProbCmds {
    private ProbCmds() {}

    private static Server S() { return Server.I; }

    /* rm_config defaults (src/config.c). */
    static final double BF_ERROR_RATE = 0.01, BF_ERROR_MIN = 0.0, BF_ERROR_MAX = 1.0;
    static final double BF_ERROR_RATE_CAP = 0.25;
    static final long BF_INITIAL_SIZE = 100, BF_SIZE_MIN = 1, BF_SIZE_MAX = 1L << 30;
    static final long BF_EXPANSION = 2, BF_EXPANSION_MIN = 0, BF_EXPANSION_MAX = 32768;
    static final long CF_BUCKET_SIZE = 2, CF_BUCKET_MIN = 1, CF_BUCKET_MAX = 255;
    static final long CF_INITIAL_SIZE = 1024, CF_INITIAL_MIN = 4, CF_INITIAL_MAX = 1L << 30;
    static final long CF_MAX_ITERATIONS = 20, CF_ITER_MIN = 1, CF_ITER_MAX = 65535;
    static final long CF_EXPANSION = 1, CF_EXP_MIN = 0, CF_EXP_MAX = 32768;

    static final String E_NOTFOUND = "ERR not found";
    static final String E_EXISTS = "ERR item exists";
    static final String E_WRONGTYPE = "WRONGTYPE Operation against a key holding the wrong kind of value";

    /** Deterministic decay source: redis's own rand() stream is seeded once. */
    private static final java.util.Random TOPK_RND = new java.util.Random(1);

    // ------------------------------------------------------------------
    // lookup helpers
    // ------------------------------------------------------------------

    private static Object value(Client c, byte[] key, Class<?> want, boolean write) {
        RObj o = write ? S().lookupKeyWrite(c, key) : S().lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != RObj.MODULE || !(o.ptr instanceof ModVal)) throw new RedisError(E_WRONGTYPE);
        if (!want.isInstance(o.ptr)) throw new RedisError(E_WRONGTYPE);
        return o.ptr;
    }

    private static void store(Client c, byte[] key, ModVal v) {
        S().setKey(c, key, ModTypes.obj(v));
        S().dirty++;
    }

    private static void touched(Client c, byte[] key, String event) {
        S().signalModifiedKey(c.db, new Key(key));
        S().notifyKeyspaceEvent('m', event, key, c.db);
        S().dirty++;
    }

    private static long toLong(Client c, int i, String err) {
        Long v = Util.string2ll(c.arg(i));
        if (v == null) throw new RedisError(err);
        return v;
    }

    private static double toDouble(Client c, int i, String err) {
        Double v = Util.string2d(c.arg(i));
        if (v == null || v.isNaN()) throw new RedisError(err);
        return v;
    }

    /**
     * addReplyDouble() on RESP2: redis prints the shortest round-tripping form
     * through fpconv_dtoa, which is not the same as d2string()'s "%.17g".
     */
    static byte[] dtoa(double v) {
        if (Double.isNaN(v)) return Util.b("nan");
        if (Double.isInfinite(v)) return Util.b(v > 0 ? "inf" : "-inf");
        if (v == 0) return Util.b(1.0 / v < 0 ? "-0" : "0");
        boolean neg = v < 0;
        String[] dk = RJson.shortestDigits(Math.abs(v));
        String digits = dk[0];
        int kk = Integer.parseInt(dk[1]);
        return Util.b(Util.emitDigits(digits, kk - digits.length(), neg));
    }

    /** RMUtil_ArgIndex(): first case-insensitive match of `name` in argv. */
    private static int argIndex(Client c, String name) {
        for (int i = 0; i < c.argc; i++) if (c.argIs(i, name)) return i;
        return -1;
    }

    private static String fmtRange(double min, double max) {
        return "(" + String.format(java.util.Locale.ROOT, "%f", min) + ", "
                + String.format(java.util.Locale.ROOT, "%f", max) + ")";
    }

    // ==================================================================
    // BF.*
    // ==================================================================

    private static void bfReserve(Client c) {
        if (c.argc < 4 || c.argc > 7) throw RedisError.wrongArgs("bf.reserve");
        double error = toDouble(c, 2, "ERR bad error rate");
        if (!(error > BF_ERROR_MIN && error < BF_ERROR_MAX))
            throw new RedisError("ERR error rate must be in the range " + fmtRange(BF_ERROR_MIN, BF_ERROR_MAX));
        if (error > BF_ERROR_RATE_CAP) error = BF_ERROR_RATE_CAP;
        long capacity = toLong(c, 3, "ERR bad capacity");
        if (capacity < BF_SIZE_MIN || capacity > BF_SIZE_MAX)
            throw new RedisError("ERR capacity must be in the range [" + BF_SIZE_MIN + ", " + BF_SIZE_MAX + "]");

        boolean nonScaling = argIndex(c, "NONSCALING") != -1;
        long expansion = BF_EXPANSION;
        int ex = argIndex(c, "EXPANSION");
        if (ex + 1 == c.argc) throw new RedisError("ERR no expansion");
        if (ex != -1) {
            Long v = Util.string2ll(c.arg(ex + 1));
            if (v == null) throw new RedisError("ERR bad expansion");
            expansion = v;
            if (expansion == 0) nonScaling = true;
            else if (nonScaling) throw new RedisError("Nonscaling filters cannot expand");
            if (expansion < BF_EXPANSION_MIN || expansion > BF_EXPANSION_MAX)
                throw new RedisError("ERR expansion must be in the range ["
                        + BF_EXPANSION_MIN + ", " + BF_EXPANSION_MAX + "]");
        }
        Object cur = value(c, c.arg(1), RBloom.Bloom.class, true);
        if (cur != null) throw new RedisError(E_EXISTS);
        store(c, c.arg(1), RBloom.Bloom.create(capacity, error, (int) expansion, nonScaling));
        S().notifyKeyspaceEvent('m', "bf.reserve", c.arg(1), c.db);
        c.addOk();
    }

    /** BFInsertOptions. */
    private static final class BfOpt {
        long capacity = BF_INITIAL_SIZE;
        double error = BF_ERROR_RATE;
        boolean autocreate = true;
        boolean multi = true;
        long expansion = BF_EXPANSION;
        boolean nonScaling = false;
    }

    private static void bfInsertCommon(Client c, byte[] key, int from, int n, BfOpt o) {
        RBloom.Bloom sb = (RBloom.Bloom) value(c, key, RBloom.Bloom.class, true);
        boolean created = false;
        if (sb == null) {
            if (!o.autocreate) throw new RedisError(E_NOTFOUND);
            sb = RBloom.Bloom.create(o.capacity, o.error, (int) o.expansion, o.nonScaling);
            store(c, key, sb);
            created = true;
        }
        List<Integer> res = new ArrayList<>();
        String err = null;
        int changed = 0;
        for (int i = 0; i < n; i++) {
            int rv = sb.add(c.arg(from + i));
            if (rv == -2) { err = "ERR non scaling filter is full"; res.add(-2); break; }
            if (rv == -1) { err = "ERR problem inserting into filter"; res.add(-1); break; }
            if (rv != 0) changed++;
            res.add(rv);
        }
        if (changed > 0 || created) touched(c, key, "bf.add");
        if (o.multi) {
            c.addArray(res.size());
            for (int rv : res) {
                if (rv < 0) c.addError(rv == -2 ? "ERR non scaling filter is full"
                                                : "ERR problem inserting into filter");
                else c.addInt(rv != 0 ? 1 : 0);
            }
        } else if (err != null) {
            throw new RedisError(err);
        } else {
            c.addInt(res.get(0) != 0 ? 1 : 0);
        }
    }

    private static void bfAdd(Client c, boolean multi) {
        if ((multi && c.argc < 3) || (!multi && c.argc != 3))
            throw RedisError.wrongArgs(multi ? "bf.madd" : "bf.add");
        BfOpt o = new BfOpt();
        o.multi = multi;
        bfInsertCommon(c, c.arg(1), 2, c.argc - 2, o);
    }

    private static void bfInsert(Client c) {
        if (c.argc < 4) throw RedisError.wrongArgs("bf.insert");
        BfOpt o = new BfOpt();
        int itemsIndex = -1;
        int pos = 2;
        while (pos < c.argc && itemsIndex < 0) {
            byte[] a = c.arg(pos);
            if (a.length == 0) throw new RedisError("Unknown argument received");
            char k = Character.toLowerCase((char) (a[0] & 0xff));
            switch (k) {
                case 'i':
                    itemsIndex = ++pos;
                    break;
                case 'e': {
                    if (++pos == c.argc) throw RedisError.wrongArgs("bf.insert");
                    char second = a.length > 1 ? Character.toLowerCase((char) (a[1] & 0xff)) : 0;
                    if (second == 'r') {
                        Double v = Util.string2d(c.arg(pos++));
                        if (v == null || !(v > BF_ERROR_MIN && v < BF_ERROR_MAX))
                            throw new RedisError("Bad error rate");
                        o.error = Math.min(v, BF_ERROR_RATE_CAP);
                    } else {
                        Long v = Util.string2ll(c.arg(pos++));
                        if (v == null || v < BF_EXPANSION_MIN || v > BF_EXPANSION_MAX)
                            throw new RedisError("Bad expansion");
                        o.expansion = v;
                    }
                    break;
                }
                case 'c': {
                    if (++pos == c.argc) throw RedisError.wrongArgs("bf.insert");
                    Long v = Util.string2ll(c.arg(pos++));
                    if (v == null || v < BF_SIZE_MIN || v > BF_SIZE_MAX)
                        throw new RedisError("Bad capacity");
                    o.capacity = v;
                    break;
                }
                case 'n': {
                    char third = a.length > 2 ? Character.toLowerCase((char) (a[2] & 0xff)) : 0;
                    if (third == 'c') o.autocreate = false;
                    else o.nonScaling = true;
                    pos++;
                    break;
                }
                default:
                    throw new RedisError("Unknown argument received");
            }
        }
        if (o.expansion == 0) o.nonScaling = true;
        if (itemsIndex < 0 || itemsIndex == c.argc) throw RedisError.wrongArgs("bf.insert");
        bfInsertCommon(c, c.arg(1), itemsIndex, c.argc - itemsIndex, o);
    }

    private static void bfExists(Client c, boolean multi) {
        if ((multi && c.argc < 3) || (!multi && c.argc != 3))
            throw RedisError.wrongArgs(multi ? "bf.mexists" : "bf.exists");
        RBloom.Bloom sb = (RBloom.Bloom) value(c, c.arg(1), RBloom.Bloom.class, false);
        if (multi) c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) c.addInt(sb != null && sb.check(c.arg(i)) ? 1 : 0);
    }

    private static void bfCard(Client c) {
        if (c.argc != 2) throw RedisError.wrongArgs("bf.card");
        RBloom.Bloom sb = (RBloom.Bloom) value(c, c.arg(1), RBloom.Bloom.class, false);
        c.addInt(sb == null ? 0 : sb.size);
    }

    /** One field of an INFO reply: the name only shows up on RESP3. */
    private static void infoOne(Client c, String name) {
        if (c.resp >= 3) { c.addMap(1); c.addSimple(name); }
        else c.addArray(1);
    }

    private static void bfInfo(Client c) {
        if (c.argc < 2 || c.argc > 3) throw RedisError.wrongArgs("bf.info");
        RBloom.Bloom sb = (RBloom.Bloom) value(c, c.arg(1), RBloom.Bloom.class, false);
        if (sb == null) throw new RedisError(E_NOTFOUND);
        if (c.argc == 3) {
            /* RedisModule_ReplyWithMapOrArray(ctx, 1, false): on RESP2 a one
             * element array holding only the value, on RESP3 a real map. */
            String w = c.argLower(2);
            switch (w) {
                case "capacity": infoOne(c, "Capacity"); c.addInt(sb.capacity()); return;
                case "size": infoOne(c, "Size"); c.addInt(sb.memUsage()); return;
                case "filters": infoOne(c, "Number of filters"); c.addInt(sb.filters.size()); return;
                case "items": infoOne(c, "Number of items inserted"); c.addInt(sb.size); return;
                case "expansion":
                    infoOne(c, "Expansion rate");
                    if (sb.nonScaling) c.addNil(); else c.addInt(sb.growth);
                    return;
                default:
                    throw new RedisError("Invalid information value");
            }
        }
        c.addMap(5);
        c.addSimple("Capacity"); c.addInt(sb.capacity());
        c.addSimple("Size"); c.addInt(sb.memUsage());
        c.addSimple("Number of filters"); c.addInt(sb.filters.size());
        c.addSimple("Number of items inserted"); c.addInt(sb.size);
        c.addSimple("Expansion rate");
        if (sb.nonScaling) c.addNil(); else c.addInt(sb.growth);
    }

    /**
     * BF.SCANDUMP / BF.LOADCHUNK move a filter between servers. The module's
     * chunk layout is private to it, so ours carries our own encoding; the pair
     * still round-trips, which is what the commands promise.
     */
    private static void bfScanDump(Client c) {
        if (c.argc != 3) throw RedisError.wrongArgs("bf.scandump");
        RBloom.Bloom sb = (RBloom.Bloom) value(c, c.arg(1), RBloom.Bloom.class, false);
        if (sb == null) throw new RedisError(E_NOTFOUND);
        long iter = toLong(c, 2, "Second argument must be numeric");
        byte[] all = sb.moduleSave();
        if (iter != 0) { c.addArray(2); c.addInt(0); c.addNil(); return; }
        c.addArray(2);
        c.addInt(1);
        c.addBulk(all);
    }

    private static void bfLoadChunk(Client c) {
        if (c.argc != 4) throw RedisError.wrongArgs("bf.loadchunk");
        long iter = toLong(c, 2, "Second argument must be numeric");
        RBloom.Bloom cur = (RBloom.Bloom) value(c, c.arg(1), RBloom.Bloom.class, true);
        /* iterator 1 carries the header chunk and creates the filter; anything
         * else only makes sense for a filter that is already there. */
        if (iter != 1 && cur == null) throw new RedisError(E_NOTFOUND);
        try {
            store(c, c.arg(1), (ModVal) RBloom.loadBloom(c.arg(3)));
        } catch (RuntimeException e) {
            throw new RedisError("ERR received bad data");
        }
        c.addOk();
    }

    // ==================================================================
    // CF.*
    // ==================================================================

    private static void cfReserve(Client c) {
        if (c.argc < 3 || c.argc % 2 == 0) throw RedisError.wrongArgs("cf.reserve");
        long capacity = toLong(c, 2, "Bad capacity");
        long bucketSize = CF_BUCKET_SIZE, maxIter = CF_MAX_ITERATIONS, expansion = CF_EXPANSION;
        for (int i = 3; i + 1 < c.argc; i += 2) {
            if (c.argIs(i, "BUCKETSIZE")) {
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v < CF_BUCKET_MIN || v > CF_BUCKET_MAX)
                    throw new RedisError("BUCKETSIZE: value must be in the range ["
                            + CF_BUCKET_MIN + ", " + CF_BUCKET_MAX + "]");
                bucketSize = v;
            } else if (c.argIs(i, "MAXITERATIONS")) {
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v < CF_ITER_MIN || v > CF_ITER_MAX)
                    throw new RedisError("MAXITERATIONS: value must be in the range ["
                            + CF_ITER_MIN + ", " + CF_ITER_MAX + "]");
                maxIter = v;
            } else if (c.argIs(i, "EXPANSION")) {
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v < CF_EXP_MIN || v > CF_EXP_MAX)
                    throw new RedisError("EXPANSION: value must be in the range ["
                            + CF_EXP_MIN + ", " + CF_EXP_MAX + "]");
                expansion = v;
            } else {
                throw RedisError.wrongArgs("cf.reserve");
            }
        }
        if (bucketSize * 2 > capacity || capacity > CF_INITIAL_MAX)
            throw new RedisError("Capacity must be in the range [2 * BUCKETSIZE, " + CF_INITIAL_MAX + "]");
        Object cur = value(c, c.arg(1), RBloom.Cuckoo.class, true);
        if (cur != null) throw new RedisError(E_EXISTS);
        store(c, c.arg(1), RBloom.Cuckoo.create(capacity, (int) bucketSize, (int) maxIter, (int) expansion));
        S().notifyKeyspaceEvent('m', "cf.reserve", c.arg(1), c.db);
        c.addOk();
    }

    private static RBloom.Cuckoo cfOpen(Client c, byte[] key, boolean autocreate, long capacity) {
        RBloom.Cuckoo cf = (RBloom.Cuckoo) value(c, key, RBloom.Cuckoo.class, true);
        if (cf == null && autocreate) {
            cf = RBloom.Cuckoo.create(capacity, (int) CF_BUCKET_SIZE,
                    (int) CF_MAX_ITERATIONS, (int) CF_EXPANSION);
            store(c, key, cf);
        }
        return cf;
    }

    private static void cfAdd(Client c, boolean unique) {
        if (c.argc != 3) throw RedisError.wrongArgs(unique ? "cf.addnx" : "cf.add");
        RBloom.Cuckoo cf = cfOpen(c, c.arg(1), true, CF_INITIAL_SIZE);
        int rv = cf.insert(c.arg(2), unique);
        if (rv == -1) throw new RedisError("Filter is full");
        touched(c, c.arg(1), unique ? "cf.addnx" : "cf.add");
        c.addInt(rv == 1 ? 1 : 0);
    }

    private static void cfInsert(Client c, boolean unique) {
        String name = unique ? "cf.insertnx" : "cf.insert";
        if (c.argc < 4) throw RedisError.wrongArgs(name);
        long capacity = CF_INITIAL_SIZE;
        boolean autocreate = true;
        int items = -1;
        int pos = 2;
        while (pos < c.argc && items < 0) {
            if (c.argIs(pos, "CAPACITY")) {
                if (++pos == c.argc) throw RedisError.wrongArgs(name);
                Long v = Util.string2ll(c.arg(pos++));
                if (v == null || v < CF_INITIAL_MIN || v > CF_INITIAL_MAX)
                    throw new RedisError("Bad capacity");
                capacity = v;
            } else if (c.argIs(pos, "NOCREATE")) {
                autocreate = false;
                pos++;
            } else if (c.argIs(pos, "ITEMS")) {
                items = ++pos;
            } else {
                throw new RedisError("Unknown argument received");
            }
        }
        if (items < 0 || items == c.argc) throw RedisError.wrongArgs(name);
        RBloom.Cuckoo cf = cfOpen(c, c.arg(1), autocreate, capacity);
        if (cf == null) throw new RedisError(E_NOTFOUND);
        c.addArray(c.argc - items);
        for (int i = items; i < c.argc; i++) {
            int rv = cf.insert(c.arg(i), unique);
            if (rv == -1) c.addInt(-1);
            else c.addInt(rv == 1 ? 1 : 0);
        }
        touched(c, c.arg(1), name);
    }

    private static void cfExists(Client c, boolean multi) {
        if ((multi && c.argc < 3) || (!multi && c.argc != 3))
            throw RedisError.wrongArgs(multi ? "cf.mexists" : "cf.exists");
        RBloom.Cuckoo cf = (RBloom.Cuckoo) value(c, c.arg(1), RBloom.Cuckoo.class, false);
        if (multi) c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) c.addInt(cf != null && cf.check(c.arg(i)) ? 1 : 0);
    }

    private static void cfCount(Client c) {
        if (c.argc != 3) throw RedisError.wrongArgs("cf.count");
        RBloom.Cuckoo cf = (RBloom.Cuckoo) value(c, c.arg(1), RBloom.Cuckoo.class, false);
        c.addInt(cf == null ? 0 : cf.count(c.arg(2)));
    }

    private static void cfDel(Client c) {
        if (c.argc != 3) throw RedisError.wrongArgs("cf.del");
        RBloom.Cuckoo cf = (RBloom.Cuckoo) value(c, c.arg(1), RBloom.Cuckoo.class, true);
        if (cf == null) throw new RedisError("Not found");
        boolean r = cf.delete(c.arg(2));
        if (r) touched(c, c.arg(1), "cf.del");
        c.addInt(r ? 1 : 0);
    }

    private static void cfInfo(Client c) {
        if (c.argc != 2) throw RedisError.wrongArgs("cf.info");
        RBloom.Cuckoo cf = (RBloom.Cuckoo) value(c, c.arg(1), RBloom.Cuckoo.class, false);
        if (cf == null) throw new RedisError(E_NOTFOUND);
        c.addMap(8);
        c.addSimple("Size"); c.addInt(cf.memUsage());
        c.addSimple("Number of buckets"); c.addInt(cf.numBuckets);
        c.addSimple("Number of filters"); c.addInt(cf.filters.size());
        c.addSimple("Number of items inserted"); c.addInt(cf.numItems);
        c.addSimple("Number of items deleted"); c.addInt(cf.numDeletes);
        c.addSimple("Bucket size"); c.addInt(cf.bucketSize);
        c.addSimple("Expansion rate"); c.addInt(cf.expansion);
        c.addSimple("Max iterations"); c.addInt(cf.maxIterations);
    }

    private static void cfScanDump(Client c) {
        if (c.argc != 3) throw RedisError.wrongArgs("cf.scandump");
        RBloom.Cuckoo cf = (RBloom.Cuckoo) value(c, c.arg(1), RBloom.Cuckoo.class, false);
        if (cf == null) throw new RedisError(E_NOTFOUND);
        long iter = toLong(c, 2, "Invalid position");
        byte[] all = cf.moduleSave();
        if (iter != 0) { c.addArray(2); c.addInt(0); c.addNil(); return; }
        c.addArray(2);
        c.addInt(1);
        c.addBulk(all);
    }

    private static void cfLoadChunk(Client c) {
        if (c.argc != 4) throw RedisError.wrongArgs("cf.loadchunk");
        long iter = toLong(c, 2, "Invalid position");
        RBloom.Cuckoo cur = (RBloom.Cuckoo) value(c, c.arg(1), RBloom.Cuckoo.class, true);
        if (iter != 1 && cur == null) throw new RedisError("Invalid position");
        try {
            store(c, c.arg(1), (ModVal) RBloom.loadCuckoo(c.arg(3)));
        } catch (RuntimeException e) {
            throw new RedisError("ERR received bad data");
        }
        c.addOk();
    }

    // ==================================================================
    // CMS.*
    // ==================================================================

    private static void cmsInitByDim(Client c) {
        if (c.argc != 4) throw RedisError.wrongArgs("cms.initbydim");
        Long w = Util.string2ll(c.arg(2));
        Long d = Util.string2ll(c.arg(3));
        if (w == null || w < 1) throw new RedisError("CMS: invalid width");
        if (d == null || d < 1) throw new RedisError("CMS: invalid depth");
        if (value(c, c.arg(1), RBloom.Cms.class, true) != null)
            throw new RedisError("CMS: key already exists");
        store(c, c.arg(1), RBloom.Cms.create(w, d));
        S().notifyKeyspaceEvent('m', "cms.initbydim", c.arg(1), c.db);
        c.addOk();
    }

    private static void cmsInitByProb(Client c) {
        if (c.argc != 4) throw RedisError.wrongArgs("cms.initbyprob");
        Double over = Util.string2d(c.arg(2));
        Double prob = Util.string2d(c.arg(3));
        if (over == null || over <= 0 || over >= 1)
            throw new RedisError("CMS: invalid overestimation value");
        if (prob == null || prob <= 0 || prob >= 1) throw new RedisError("CMS: invalid prob value");
        if (value(c, c.arg(1), RBloom.Cms.class, true) != null)
            throw new RedisError("CMS: key already exists");
        long[] wd = RBloom.Cms.dimFromProb(over, prob);
        store(c, c.arg(1), RBloom.Cms.create(wd[0], wd[1]));
        S().notifyKeyspaceEvent('m', "cms.initbyprob", c.arg(1), c.db);
        c.addOk();
    }

    private static RBloom.Cms cms(Client c, byte[] key, boolean write) {
        RBloom.Cms v = (RBloom.Cms) value(c, key, RBloom.Cms.class, write);
        if (v == null) throw new RedisError("CMS: key does not exist");
        return v;
    }

    private static void cmsIncrBy(Client c) {
        if (c.argc < 4 || c.argc % 2 != 0) throw RedisError.wrongArgs("cms.incrby");
        long[] vals = new long[(c.argc - 2) / 2];
        for (int i = 3, j = 0; i < c.argc; i += 2, j++) {
            Long v = Util.string2ll(c.arg(i));
            if (v == null) throw new RedisError("CMS: Cannot parse number");
            if (v < 0) throw new RedisError("CMS: Number cannot be negative");
            vals[j] = v;
        }
        RBloom.Cms m = cms(c, c.arg(1), true);
        c.addArray(vals.length);
        long[] out = new long[1];
        for (int i = 2, j = 0; i < c.argc; i += 2, j++) {
            int rc = m.incrBy(c.arg(i), vals[j], out);
            if (rc != 0) c.addError("CMS: INCRBY overflow");
            else c.addInt(out[0]);
            /* the sketch counter took the increment either way */
        }
        touched(c, c.arg(1), "cms.incrby");
    }

    private static void cmsQuery(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("cms.query");
        RBloom.Cms m = cms(c, c.arg(1), false);
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) c.addInt(m.query(c.arg(i)));
    }

    private static void cmsMerge(Client c) {
        if (c.argc < 4) throw RedisError.wrongArgs("cms.merge");
        Long n = Util.string2ll(c.arg(2));
        if (n == null || n < 1) throw new RedisError("CMS: wrong number of keys");
        if (c.argc < 3 + n) throw new RedisError("CMS: wrong number of keys");
        RBloom.Cms dest = cms(c, c.arg(1), true);
        List<RBloom.Cms> src = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            RBloom.Cms s = cms(c, c.arg(3 + i), false);
            if (s.width != dest.width || s.depth != dest.depth)
                throw new RedisError("CMS: width/depth is not equal");
            src.add(s);
        }
        long[] weights = new long[(int) (long) n];
        java.util.Arrays.fill(weights, 1);
        int at = (int) (3 + n);
        if (at < c.argc) {
            if (!c.argIs(at, "WEIGHTS")) throw new RedisError("CMS: wrong number of keys/weights");
            if (c.argc - at - 1 != n) throw new RedisError("CMS: wrong number of keys/weights");
            for (int i = 0; i < n; i++) {
                Long w = Util.string2ll(c.arg(at + 1 + i));
                if (w == null) throw new RedisError("CMS: Cannot parse number");
                weights[i] = w;
            }
        }
        if (!dest.merge(src, weights)) throw new RedisError("CMS: MERGE overflow");
        touched(c, c.arg(1), "cms.merge");
        c.addOk();
    }

    private static void cmsInfo(Client c) {
        if (c.argc != 2) throw RedisError.wrongArgs("cms.info");
        RBloom.Cms m = cms(c, c.arg(1), false);
        c.addMap(3);
        c.addSimple("width"); c.addInt(m.width);
        c.addSimple("depth"); c.addInt(m.depth);
        c.addSimple("count"); c.addInt(m.counter);
    }

    // ==================================================================
    // TOPK.*
    // ==================================================================

    private static void topkReserve(Client c) {
        if (c.argc != 3 && c.argc != 6) throw RedisError.wrongArgs("topk.reserve");
        Long k = Util.string2ll(c.arg(2));
        if (k == null || k < 1) throw new RedisError("TopK: invalid k");
        long width = 8, depth = 7;
        double decay = 0.9;
        if (c.argc == 6) {
            Long w = Util.string2ll(c.arg(3));
            if (w == null || w < 1) throw new RedisError("TopK: invalid width");
            Long d = Util.string2ll(c.arg(4));
            if (d == null || d < 1) throw new RedisError("TopK: invalid depth");
            Double dc = Util.string2d(c.arg(5));
            if (dc == null || dc <= 0 || dc > 1)
                throw new RedisError("TopK: invalid decay value. must be '<= 1' & '> 0'");
            width = w; depth = d; decay = dc;
        }
        if (value(c, c.arg(1), RBloom.Topk.class, true) != null)
            throw new RedisError("TopK: key already exists");
        store(c, c.arg(1), RBloom.Topk.create((int) (long) k, (int) width, (int) depth, decay));
        S().notifyKeyspaceEvent('m', "topk.reserve", c.arg(1), c.db);
        c.addOk();
    }

    private static RBloom.Topk topk(Client c, byte[] key, boolean write) {
        RBloom.Topk v = (RBloom.Topk) value(c, key, RBloom.Topk.class, write);
        if (v == null) throw new RedisError("TopK: key does not exist");
        return v;
    }

    private static void topkAdd(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("topk.add");
        RBloom.Topk t = topk(c, c.arg(1), true);
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) {
            byte[] out = t.add(c.arg(i), 1, TOPK_RND);
            if (out == null) c.addNil(); else c.addBulk(out);
        }
        touched(c, c.arg(1), "topk.add");
    }

    private static void topkIncrBy(Client c) {
        if (c.argc < 4 || c.argc % 2 != 0) throw RedisError.wrongArgs("topk.incrby");
        RBloom.Topk t = topk(c, c.arg(1), true);
        c.addArray((c.argc - 2) / 2);
        for (int i = 2; i + 1 < c.argc; i += 2) {
            Long v = Util.string2ll(c.arg(i + 1));
            if (v == null || v < 0 || v > 100000) {
                c.addError("TopK: increment must be an integer greater or equal to 0"
                        + "                            and smaller or equal to 100,000");
                continue;
            }
            byte[] out = t.add(c.arg(i), v, TOPK_RND);
            if (out == null) c.addNil(); else c.addBulk(out);
        }
        touched(c, c.arg(1), "topk.incrby");
    }

    private static void topkQuery(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("topk.query");
        RBloom.Topk t = topk(c, c.arg(1), false);
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) c.addInt(t.query(c.arg(i)) ? 1 : 0);
    }

    private static void topkCount(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("topk.count");
        RBloom.Topk t = topk(c, c.arg(1), false);
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) c.addInt(t.count(c.arg(i)));
    }

    private static void topkList(Client c) {
        if (c.argc != 2 && c.argc != 3) throw RedisError.wrongArgs("topk.list");
        boolean withCount = c.argc == 3;
        if (withCount && !c.argIs(2, "WITHCOUNT"))
            throw new RedisError("WITHCOUNT keyword expected");
        RBloom.Topk t = topk(c, c.arg(1), false);
        int[] order = t.listOrder();
        int n = 0;
        for (int i : order) if (t.heapItem[i] != null) n++;
        c.addArray(withCount ? n * 2 : n);
        for (int i : order) {
            if (t.heapItem[i] == null) continue;
            c.addBulk(t.heapItem[i]);
            if (withCount) c.addInt(t.heapCount[i]);
        }
    }

    private static void topkInfo(Client c) {
        if (c.argc != 2) throw RedisError.wrongArgs("topk.info");
        RBloom.Topk t = topk(c, c.arg(1), false);
        c.addMap(4);
        c.addSimple("k"); c.addInt(t.k);
        c.addSimple("width"); c.addInt(t.width);
        c.addSimple("depth"); c.addInt(t.depth);
        c.addSimple("decay"); c.addBulk(dtoa(t.decay));
    }

    // ==================================================================
    // TDIGEST.*
    // ==================================================================

    private static RBloom.TDigest td(Client c, byte[] key, boolean write) {
        RBloom.TDigest v = (RBloom.TDigest) value(c, key, RBloom.TDigest.class, write);
        if (v == null) throw new RedisError("ERR T-Digest: key does not exist");
        return v;
    }

    private static void tdCreate(Client c) {
        if (c.argc != 2 && c.argc != 4) throw RedisError.wrongArgs("tdigest.create");
        long compression = 100;
        if (c.argc == 4) {
            if (!c.argIs(2, "COMPRESSION")) throw new RedisError("ERR T-Digest: wrong keyword");
            Long v = Util.string2ll(c.arg(3));
            if (v == null || v <= 0) throw new RedisError("ERR T-Digest: wrong compression parameter");
            compression = v;
        }
        if (value(c, c.arg(1), RBloom.TDigest.class, true) != null)
            throw new RedisError("ERR T-Digest: key already exists");
        RBloom.TDigest t = new RBloom.TDigest();
        t.compression = compression;
        store(c, c.arg(1), t);
        c.addOk();
    }

    private static void tdAdd(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("tdigest.add");
        RBloom.TDigest t = td(c, c.arg(1), true);
        double[] vs = new double[c.argc - 2];
        for (int i = 2; i < c.argc; i++) {
            Double v = Util.string2d(c.arg(i));
            if (v == null || v.isNaN()) throw new RedisError("ERR T-Digest: error parsing val parameter");
            vs[i - 2] = v;
        }
        for (double v : vs) t.add(v, 1);
        touched(c, c.arg(1), "tdigest.add");
        c.addOk();
    }

    private static void tdReset(Client c) {
        if (c.argc != 2) throw RedisError.wrongArgs("tdigest.reset");
        RBloom.TDigest t = td(c, c.arg(1), true);
        t.reset();
        touched(c, c.arg(1), "tdigest.reset");
        c.addOk();
    }

    private static void tdMinMax(Client c, boolean min) {
        if (c.argc != 2) throw RedisError.wrongArgs(min ? "tdigest.min" : "tdigest.max");
        RBloom.TDigest t = td(c, c.arg(1), false);
        t.compress();
        if (t.centroids.isEmpty()) { c.addBulk("nan"); return; }
        c.addBulk(dtoa(min ? t.min : t.max));
    }

    /** The value at rank floor(q * n), clamped to the last one. */
    private static double quantileOf(RBloom.TDigest t, double q) {
        int n = t.centroids.size();
        if (n == 0) return Double.NaN;
        int ix = (int) Math.floor(q * n);
        if (ix >= n) ix = n - 1;
        if (ix < 0) ix = 0;
        return t.centroids.get(ix)[0];
    }

    private static void tdQuantile(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("tdigest.quantile");
        RBloom.TDigest t = td(c, c.arg(1), false);
        t.compress();
        double[] qs = new double[c.argc - 2];
        for (int i = 2; i < c.argc; i++) {
            Double q = Util.string2d(c.arg(i));
            if (q == null) throw new RedisError("ERR T-Digest: error parsing quantile");
            if (q < 0 || q > 1) throw new RedisError("ERR T-Digest: quantile should be in [0,1]");
            qs[i - 2] = q;
        }
        c.addArray(qs.length);
        for (double q : qs) {
            double v = quantileOf(t, q);
            c.addBulk(Double.isNaN(v) ? Util.b("nan") : dtoa(v));
        }
    }

    /** TDIGEST.RANK / TDIGEST.REVRANK: -1 / -2 below the range, n above it. */
    private static void tdRank(Client c, boolean rev) {
        String name = rev ? "tdigest.revrank" : "tdigest.rank";
        if (c.argc < 3) throw RedisError.wrongArgs(name);
        RBloom.TDigest t = td(c, c.arg(1), false);
        t.compress();
        int n = t.centroids.size();
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) {
            Double x = Util.string2d(c.arg(i));
            if (x == null) throw new RedisError("ERR T-Digest: error parsing val parameter");
            if (n == 0) { c.addInt(-2); continue; }
            if (!rev) {
                if (x < t.min) { c.addInt(-1); continue; }
                if (x > t.max) { c.addInt(n); continue; }
                int cnt = 0;
                for (double[] cen : t.centroids) if (cen[0] <= x) cnt++;
                c.addInt(cnt - 1);
            } else {
                if (x > t.max) { c.addInt(-1); continue; }
                if (x < t.min) { c.addInt(n); continue; }
                int cnt = 0;
                for (double[] cen : t.centroids) if (cen[0] >= x) cnt++;
                c.addInt(cnt - 1);
            }
        }
    }

    private static void tdByRank(Client c, boolean rev) {
        String name = rev ? "tdigest.byrevrank" : "tdigest.byrank";
        if (c.argc < 3) throw RedisError.wrongArgs(name);
        RBloom.TDigest t = td(c, c.arg(1), false);
        t.compress();
        long[] ranks = new long[c.argc - 2];
        for (int i = 2; i < c.argc; i++) {
            Long v = Util.string2ll(c.arg(i));
            if (v == null) throw new RedisError("ERR T-Digest: error parsing rank");
            if (v < 0) throw new RedisError("ERR T-Digest: rank needs to be non negative");
            ranks[i - 2] = v;
        }
        int n = t.centroids.size();
        c.addArray(ranks.length);
        for (long r : ranks) {
            if (n == 0) { c.addBulk("nan"); continue; }
            if (r >= n) { c.addBulk(rev ? "-inf" : "inf"); continue; }
            int ix = rev ? (int) (n - 1 - r) : (int) r;
            c.addBulk(dtoa(t.centroids.get(ix)[0]));
        }
    }

    private static void tdTrimmedMean(Client c) {
        if (c.argc != 4) throw RedisError.wrongArgs("tdigest.trimmed_mean");
        RBloom.TDigest t = td(c, c.arg(1), false);
        t.compress();
        Double lo = Util.string2d(c.arg(2));
        Double hi = Util.string2d(c.arg(3));
        if (lo == null || hi == null) throw new RedisError("ERR T-Digest: error parsing quantile");
        if (lo < 0 || lo > 1 || hi < 0 || hi > 1)
            throw new RedisError("ERR T-Digest: quantile should be in [0,1]");
        int n = t.centroids.size();
        if (n == 0) { c.addBulk("nan"); return; }
        int from = (int) Math.floor(lo * n);
        int to = (int) Math.ceil(hi * n) - 1;
        if (to >= n) to = n - 1;
        if (from > to) { c.addBulk("nan"); return; }
        double sum = 0;
        for (int i = from; i <= to; i++) sum += t.centroids.get(i)[0];
        c.addBulk(dtoa(sum / (to - from + 1)));
    }

    private static void tdMerge(Client c) {
        if (c.argc < 4) throw RedisError.wrongArgs("tdigest.merge");
        Long n = Util.string2ll(c.arg(2));
        if (n == null || n < 1) throw new RedisError("ERR T-Digest: numkeys must be a positive integer");
        if (c.argc < 3 + n) throw RedisError.wrongArgs("tdigest.merge");
        long compression = 0;
        boolean override = false;
        int i = (int) (3 + n);
        while (i < c.argc) {
            if (c.argIs(i, "COMPRESSION")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v <= 0) throw new RedisError("ERR T-Digest: wrong compression parameter");
                compression = v;
                i += 2;
            } else if (c.argIs(i, "OVERRIDE")) {
                override = true;
                i++;
            } else {
                throw RedisError.syntax();
            }
        }
        List<RBloom.TDigest> src = new ArrayList<>();
        for (int j = 0; j < n; j++) src.add(td(c, c.arg(3 + j), false));
        RBloom.TDigest dest = (RBloom.TDigest) value(c, c.arg(1), RBloom.TDigest.class, true);
        if (dest == null) {
            dest = new RBloom.TDigest();
            dest.compression = compression > 0 ? compression : 100;
            store(c, c.arg(1), dest);
        } else if (override) {
            dest.reset();
            if (compression > 0) dest.compression = compression;
        } else if (compression > 0) {
            dest.compression = compression;
        }
        for (RBloom.TDigest s : src) {
            dest.compress();
            for (double[] cen : s.centroids) dest.add(cen[0], cen[1]);
        }
        touched(c, c.arg(1), "tdigest.merge");
        c.addOk();
    }

    private static void tdCdf(Client c) {
        if (c.argc < 3) throw RedisError.wrongArgs("tdigest.cdf");
        RBloom.TDigest t = td(c, c.arg(1), false);
        t.compress();
        c.addArray(c.argc - 2);
        for (int i = 2; i < c.argc; i++) {
            Double x = Util.string2d(c.arg(i));
            if (x == null) throw new RedisError("ERR T-Digest: error parsing val parameter");
            if (t.centroids.isEmpty()) { c.addBulk("nan"); continue; }
            double below = 0, equal = 0;
            for (double[] cen : t.centroids) {
                if (cen[0] < x) below += cen[1];
                else if (cen[0] == x) equal += cen[1];
            }
            c.addBulk(dtoa((below + equal / 2.0) / t.totalWeight));
        }
    }

    private static void tdInfo(Client c) {
        if (c.argc != 2) throw RedisError.wrongArgs("tdigest.info");
        RBloom.TDigest t = td(c, c.arg(1), false);
        long merged = Math.min(t.mergedCentroids, t.centroids.size());
        long unmerged = t.centroids.size() - merged;
        c.addMap(9);
        c.addSimple("Compression"); c.addInt(t.compression);
        c.addSimple("Capacity"); c.addInt(6 * t.compression + 10);
        c.addSimple("Merged nodes"); c.addInt(merged);
        c.addSimple("Unmerged nodes"); c.addInt(unmerged);
        c.addSimple("Merged weight"); c.addInt(merged);
        c.addSimple("Unmerged weight"); c.addInt(unmerged);
        c.addSimple("Observations"); c.addInt(t.observations);
        c.addSimple("Total compressions"); c.addInt(t.compressions);
        c.addSimple("Memory usage"); c.addInt(16L * (6 * t.compression + 10) + 8);
    }

    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("bf.reserve", -1, "write denyoom module", 1, 1, 1, ProbCmds::bfReserve);
        CommandTable.reg("bf.add", -1, "write denyoom module", 1, 1, 1, c -> bfAdd(c, false));
        CommandTable.reg("bf.madd", -1, "write denyoom module", 1, 1, 1, c -> bfAdd(c, true));
        CommandTable.reg("bf.insert", -1, "write denyoom module", 1, 1, 1, ProbCmds::bfInsert);
        CommandTable.reg("bf.exists", -1, "readonly module fast", 1, 1, 1, c -> bfExists(c, false));
        CommandTable.reg("bf.mexists", -1, "readonly module fast", 1, 1, 1, c -> bfExists(c, true));
        CommandTable.reg("bf.card", -1, "readonly module fast", 1, 1, 1, ProbCmds::bfCard);
        CommandTable.reg("bf.info", -1, "readonly module fast", 1, 1, 1, ProbCmds::bfInfo);
        CommandTable.reg("bf.scandump", -1, "readonly module fast", 1, 1, 1, ProbCmds::bfScanDump);
        CommandTable.reg("bf.loadchunk", -1, "write denyoom module", 1, 1, 1, ProbCmds::bfLoadChunk);

        CommandTable.reg("cf.reserve", -1, "write denyoom module", 1, 1, 1, ProbCmds::cfReserve);
        CommandTable.reg("cf.add", -1, "write denyoom module", 1, 1, 1, c -> cfAdd(c, false));
        CommandTable.reg("cf.addnx", -1, "write denyoom module", 1, 1, 1, c -> cfAdd(c, true));
        CommandTable.reg("cf.insert", -1, "write denyoom module", 1, 1, 1, c -> cfInsert(c, false));
        CommandTable.reg("cf.insertnx", -1, "write denyoom module", 1, 1, 1, c -> cfInsert(c, true));
        CommandTable.reg("cf.exists", -1, "readonly module fast", 1, 1, 1, c -> cfExists(c, false));
        CommandTable.reg("cf.mexists", -1, "readonly module fast", 1, 1, 1, c -> cfExists(c, true));
        CommandTable.reg("cf.count", -1, "readonly module fast", 1, 1, 1, ProbCmds::cfCount);
        CommandTable.reg("cf.del", -1, "write module fast", 1, 1, 1, ProbCmds::cfDel);
        CommandTable.reg("cf.info", -1, "readonly module fast", 1, 1, 1, ProbCmds::cfInfo);
        CommandTable.reg("cf.scandump", -1, "readonly module fast", 1, 1, 1, ProbCmds::cfScanDump);
        CommandTable.reg("cf.loadchunk", -1, "write denyoom module", 1, 1, 1, ProbCmds::cfLoadChunk);

        CommandTable.reg("cms.initbydim", -1, "write denyoom module", 1, 1, 1, ProbCmds::cmsInitByDim);
        CommandTable.reg("cms.initbyprob", -1, "write denyoom module", 1, 1, 1, ProbCmds::cmsInitByProb);
        CommandTable.reg("cms.incrby", -1, "write denyoom module", 1, 1, 1, ProbCmds::cmsIncrBy);
        CommandTable.reg("cms.query", -1, "readonly module", 1, 1, 1, ProbCmds::cmsQuery);
        CommandTable.reg("cms.merge", -1, "write denyoom module", 1, 1, 1, ProbCmds::cmsMerge);
        CommandTable.reg("cms.info", -1, "readonly module", 1, 1, 1, ProbCmds::cmsInfo);

        CommandTable.reg("topk.reserve", -1, "write denyoom module", 1, 1, 1, ProbCmds::topkReserve);
        CommandTable.reg("topk.add", -1, "write denyoom module", 1, 1, 1, ProbCmds::topkAdd);
        CommandTable.reg("topk.incrby", -1, "write denyoom module", 1, 1, 1, ProbCmds::topkIncrBy);
        CommandTable.reg("topk.query", -1, "readonly module", 1, 1, 1, ProbCmds::topkQuery);
        CommandTable.reg("topk.count", -1, "readonly module", 1, 1, 1, ProbCmds::topkCount);
        CommandTable.reg("topk.list", -1, "readonly module", 1, 1, 1, ProbCmds::topkList);
        CommandTable.reg("topk.info", -1, "readonly module", 1, 1, 1, ProbCmds::topkInfo);

        CommandTable.reg("tdigest.create", -1, "write denyoom module", 1, 1, 1, ProbCmds::tdCreate);
        CommandTable.reg("tdigest.add", -1, "write denyoom module", 1, 1, 1, ProbCmds::tdAdd);
        CommandTable.reg("tdigest.reset", -1, "write denyoom module", 1, 1, 1, ProbCmds::tdReset);
        CommandTable.reg("tdigest.min", -1, "readonly module", 1, 1, 1, c -> tdMinMax(c, true));
        CommandTable.reg("tdigest.max", -1, "readonly module", 1, 1, 1, c -> tdMinMax(c, false));
        CommandTable.reg("tdigest.quantile", -1, "readonly module", 1, 1, 1, ProbCmds::tdQuantile);
        CommandTable.reg("tdigest.cdf", -1, "readonly module", 1, 1, 1, ProbCmds::tdCdf);
        CommandTable.reg("tdigest.info", -1, "readonly module", 1, 1, 1, ProbCmds::tdInfo);
        CommandTable.reg("tdigest.rank", -1, "readonly module", 1, 1, 1, c -> tdRank(c, false));
        CommandTable.reg("tdigest.revrank", -1, "readonly module", 1, 1, 1, c -> tdRank(c, true));
        CommandTable.reg("tdigest.byrank", -1, "readonly module", 1, 1, 1, c -> tdByRank(c, false));
        CommandTable.reg("tdigest.byrevrank", -1, "readonly module", 1, 1, 1, c -> tdByRank(c, true));
        CommandTable.reg("tdigest.trimmed_mean", -1, "readonly module", 1, 1, 1, ProbCmds::tdTrimmedMean);
        CommandTable.reg("tdigest.merge", -1, "write denyoom module", 1, 1, 1, ProbCmds::tdMerge);
    }
}
