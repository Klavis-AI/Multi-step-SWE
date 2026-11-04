package redis;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Vector sets: VADD / VSIM / VREM / VEMB / VDIM / VCARD / VISMEMBER /
 * VGETATTR / VSETATTR / VLINKS / VRANDMEMBER / VINFO.
 *
 * Ported from srctrees/8.0.6/modules/vector-sets/vset.c. The module names its
 * commands in upper case, and RedisModule_WrongArity quotes that spelling, so
 * the arity errors below say 'VADD' rather than 'vadd'.
 */
public final class VectorCmds {
    private VectorCmds() {}

    private static Server S() { return Server.I; }

    /** vset-uid is handed out by a per-server counter. */
    private static long nextUid = 0;

    private static RVector get(Client c, byte[] key, boolean write) {
        RObj o = write ? S().lookupKeyWrite(c, key) : S().lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != RObj.MODULE || !(o.ptr instanceof RVector)) throw RedisError.wrongType();
        return (RVector) o.ptr;
    }

    private static void touched(Client c, byte[] key, String event) {
        S().signalModifiedKey(c.db, new Key(key));
        S().notifyKeyspaceEvent('m', event, key, c.db);
        S().dirty++;
    }

    private static RedisError wrongArity(String name) {
        return new RedisError("ERR wrong number of arguments for '" + name + "' command");
    }

    private static final String E_VECSPEC = "ERR invalid vector specification";

    /** A double the way RedisModule_ReplyWithDouble prints it on RESP2. */
    private static void addDouble(Client c, double v) {
        if (c.resp >= 3) c.addDouble(v); else c.addBulk(ProbCmds.dtoa(v));
    }

    // ------------------------------------------------------------------
    // VADD
    // ------------------------------------------------------------------

    private static void vadd(Client c) {
        if (c.argc < 5) throw wrongArity("VADD");
        int i = 2;
        int reduce = 0;
        if (c.argIs(i, "REDUCE")) {
            if (i + 1 >= c.argc) throw new RedisError(E_VECSPEC);
            Long v = Util.string2ll(c.arg(i + 1));
            if (v == null || v <= 0) throw new RedisError(E_VECSPEC);
            reduce = (int) (long) v;
            i += 2;
        }
        float[] raw;
        if (i < c.argc && c.argIs(i, "VALUES")) {
            if (i + 1 >= c.argc) throw new RedisError(E_VECSPEC);
            Long n = Util.string2ll(c.arg(i + 1));
            if (n == null || n <= 0 || i + 1 + n >= c.argc) throw new RedisError(E_VECSPEC);
            raw = new float[(int) (long) n];
            for (int j = 0; j < n; j++) {
                Double d = Util.string2d(c.arg(i + 2 + j));
                if (d == null) throw new RedisError(E_VECSPEC);
                raw[j] = (float) (double) d;
            }
            i += 2 + n;
        } else if (i < c.argc && c.argIs(i, "FP32")) {
            if (i + 1 >= c.argc) throw new RedisError(E_VECSPEC);
            byte[] blob = c.arg(i + 1);
            if (blob.length == 0 || blob.length % 4 != 0) throw new RedisError(E_VECSPEC);
            raw = new float[blob.length / 4];
            for (int j = 0; j < raw.length; j++) {
                int bits = (blob[j * 4] & 0xff) | ((blob[j * 4 + 1] & 0xff) << 8)
                        | ((blob[j * 4 + 2] & 0xff) << 16) | ((blob[j * 4 + 3] & 0xff) << 24);
                raw[j] = Float.intBitsToFloat(bits);
            }
            i += 2;
        } else {
            throw new RedisError(E_VECSPEC);
        }
        if (i >= c.argc) throw new RedisError(E_VECSPEC);
        byte[] element = c.arg(i++);

        Integer quant = null;
        Integer m = null;
        byte[] setattr = null;
        while (i < c.argc) {
            if (c.argIs(i, "CAS")) i++;
            else if (c.argIs(i, "NOQUANT")) { quant = RVector.Q_NONE; i++; }
            else if (c.argIs(i, "Q8")) { quant = RVector.Q_INT8; i++; }
            else if (c.argIs(i, "BIN")) { quant = RVector.Q_BIN; i++; }
            else if (c.argIs(i, "EF")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                i += 2;
            } else if (c.argIs(i, "M")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                Long v = Util.string2ll(c.arg(i + 1));
                if (v == null || v < 4 || v > 1024) throw new RedisError("ERR invalid M");
                m = (int) (long) v;
                i += 2;
            } else if (c.argIs(i, "SETATTR")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                setattr = c.arg(i + 1);
                i += 2;
            } else {
                throw RedisError.syntax();
            }
        }

        float[] vec = reduce > 0 ? RVector.project(raw, reduce) : raw;
        RVector v = get(c, c.arg(1), true);
        if (v == null) {
            v = new RVector();
            v.dim = vec.length;
            v.inputDim = reduce > 0 ? raw.length : 0;
            v.quant = quant == null ? RVector.Q_INT8 : quant;
            v.m = m == null ? 16 : m;
            v.uid = ++nextUid;
            S().setKey(c, c.arg(1), ModTypes.obj(v));
        } else {
            if (quant != null && quant != v.quant)
                throw new RedisError("ERR asked quantization mismatch with existing vector set");
            /* M is not optional as far as the check goes: VADD always carries
             * one (16 by default) and compares it with the set's. */
            if ((m == null ? 16 : m) != v.m)
                throw new RedisError("ERR asked M value mismatch with existing vector set");
            if (vec.length != v.dim)
                throw new RedisError("ERR Vector dimension mismatch - got " + vec.length
                        + " but set has " + v.dim);
        }
        Key k = new Key(element);
        boolean isNew = !v.nodes.containsKey(k);
        RVector.Node node = v.makeNode(element.clone(), vec);
        if (!isNew) node.attr = v.nodes.get(k).attr;
        if (setattr != null) node.attr = setattr.length == 0 ? null : setattr.clone();
        v.nodes.put(k, node);
        v.maxNodeUid++;
        touched(c, c.arg(1), "vadd");
        c.addInt(isNew ? 1 : 0);
    }

    // ------------------------------------------------------------------
    // simple accessors
    // ------------------------------------------------------------------

    private static void vcard(Client c) {
        if (c.argc != 2) throw wrongArity("VCARD");
        RVector v = get(c, c.arg(1), false);
        c.addInt(v == null ? 0 : v.nodes.size());
    }

    private static void vdim(Client c) {
        if (c.argc != 2) throw wrongArity("VDIM");
        RVector v = get(c, c.arg(1), false);
        if (v == null) throw new RedisError("ERR key does not exist");
        c.addInt(v.dim);
    }

    private static void vismember(Client c) {
        if (c.argc != 3) throw wrongArity("VISMEMBER");
        RVector v = get(c, c.arg(1), false);
        c.addInt(v != null && v.nodes.containsKey(new Key(c.arg(2))) ? 1 : 0);
    }

    private static void vemb(Client c) {
        if (c.argc < 3) throw wrongArity("VEMB");
        boolean raw = c.argc > 3 && c.argIs(3, "RAW");
        RVector v = get(c, c.arg(1), false);
        if (v == null) { c.addNil(); return; }
        RVector.Node n = v.nodes.get(new Key(c.arg(2)));
        if (n == null) { c.addNil(); return; }
        if (raw) {
            c.addArray(v.quant == RVector.Q_NONE ? 3 : 4);
            c.addSimple(RVector.Q_NAMES[v.quant]);
            c.addBulk(n.quants);
            addDouble(c, n.l2);
            if (v.quant != RVector.Q_NONE) addDouble(c, n.range);
            return;
        }
        float[] e = v.emb(n);
        c.addArray(e.length);
        for (float x : e) addDouble(c, x);
    }

    private static void vgetattr(Client c) {
        if (c.argc != 3) throw wrongArity("VGETATTR");
        RVector v = get(c, c.arg(1), false);
        if (v == null) { c.addNil(); return; }
        RVector.Node n = v.nodes.get(new Key(c.arg(2)));
        if (n == null || n.attr == null) { c.addNil(); return; }
        c.addBulk(n.attr);
    }

    private static void vsetattr(Client c) {
        if (c.argc != 4) throw wrongArity("VSETATTR");
        RVector v = get(c, c.arg(1), true);
        if (v == null) { c.addInt(0); return; }
        RVector.Node n = v.nodes.get(new Key(c.arg(2)));
        if (n == null) { c.addInt(0); return; }
        n.attr = c.arg(3).length == 0 ? null : c.arg(3).clone();
        touched(c, c.arg(1), "vsetattr");
        c.addInt(1);
    }

    private static void vrem(Client c) {
        if (c.argc != 3) throw wrongArity("VREM");
        RVector v = get(c, c.arg(1), true);
        if (v == null) { c.addInt(0); return; }
        boolean removed = v.nodes.remove(new Key(c.arg(2))) != null;
        if (removed) {
            touched(c, c.arg(1), "vrem");
            if (v.nodes.isEmpty()) {
                S().dbDelete(c.db, c.arg(1));
                S().notifyKeyspaceEvent('g', "del", c.arg(1), c.db);
            }
        }
        c.addInt(removed ? 1 : 0);
    }

    private static void vinfo(Client c) {
        if (c.argc != 2) throw wrongArity("VINFO");
        RVector v = get(c, c.arg(1), false);
        if (v == null) { c.addNil(); return; }
        c.addMap(9);
        c.addSimple("quant-type"); c.addSimple(RVector.Q_NAMES[v.quant]);
        c.addSimple("hnsw-m"); c.addInt(v.m);
        c.addSimple("vector-dim"); c.addInt(v.dim);
        c.addSimple("projection-input-dim"); c.addInt(v.inputDim);
        c.addSimple("size"); c.addInt(v.nodes.size());
        c.addSimple("max-level"); c.addInt(0);
        c.addSimple("attributes-count"); c.addInt(v.attributesCount());
        c.addSimple("vset-uid"); c.addInt(v.uid);
        c.addSimple("hnsw-max-node-uid"); c.addInt(v.maxNodeUid);
    }

    /** Our index is flat, so every member is a neighbour of every other one. */
    private static void vlinks(Client c) {
        if (c.argc < 3) throw wrongArity("VLINKS");
        boolean withScores = c.argc > 3 && c.argIs(3, "WITHSCORES");
        RVector v = get(c, c.arg(1), false);
        if (v == null) { c.addNil(); return; }
        RVector.Node n = v.nodes.get(new Key(c.arg(2)));
        if (n == null) { c.addNil(); return; }
        List<Object[]> hits = v.search(v.unit(n));
        List<Object[]> others = new ArrayList<>();
        for (Object[] h : hits) if (h[0] != n) others.add(h);
        c.addArray(1);
        c.addArray(withScores ? others.size() * 2 : others.size());
        for (Object[] h : others) {
            c.addBulk(((RVector.Node) h[0]).element);
            if (withScores) addDouble(c, (Double) h[1]);
        }
    }

    private static void vrandmember(Client c) {
        if (c.argc < 2 || c.argc > 3) throw wrongArity("VRANDMEMBER");
        RVector v = get(c, c.arg(1), false);
        if (c.argc == 2) {
            if (v == null || v.nodes.isEmpty()) { c.addNil(); return; }
            List<RVector.Node> all = new ArrayList<>(v.nodes.values());
            c.addBulk(all.get(RND.nextInt(all.size())).element);
            return;
        }
        Long cnt = Util.string2ll(c.arg(2));
        if (cnt == null) throw RedisError.notInt();
        if (v == null || v.nodes.isEmpty()) { c.addEmptyArray(); return; }
        List<RVector.Node> all = new ArrayList<>(v.nodes.values());
        long n = cnt;
        if (n >= 0) {
            java.util.Collections.shuffle(all, RND);
            int k = (int) Math.min(n, all.size());
            c.addArray(k);
            for (int i = 0; i < k; i++) c.addBulk(all.get(i).element);
        } else {
            int k = (int) -n;
            c.addArray(k);
            for (int i = 0; i < k; i++) c.addBulk(all.get(RND.nextInt(all.size())).element);
        }
    }

    private static final java.util.Random RND = new java.util.Random();

    // ------------------------------------------------------------------
    // VSIM
    // ------------------------------------------------------------------

    private static void vsim(Client c) {
        if (c.argc < 4) throw wrongArity("VSIM");
        RVector v = get(c, c.arg(1), false);
        int i = 2;
        float[] q = null;
        byte[] ele = null;
        if (c.argIs(i, "VALUES")) {
            if (i + 1 >= c.argc) throw new RedisError(E_VECSPEC);
            Long n = Util.string2ll(c.arg(i + 1));
            if (n == null || n <= 0 || i + 1 + n >= c.argc) throw new RedisError(E_VECSPEC);
            q = new float[(int) (long) n];
            for (int j = 0; j < n; j++) {
                Double d = Util.string2d(c.arg(i + 2 + j));
                if (d == null) throw new RedisError(E_VECSPEC);
                q[j] = (float) (double) d;
            }
            i += 2 + n;
        } else if (c.argIs(i, "ELE")) {
            if (i + 1 >= c.argc) throw new RedisError(E_VECSPEC);
            ele = c.arg(i + 1);
            i += 2;
        } else if (c.argIs(i, "FP32")) {
            if (i + 1 >= c.argc) throw new RedisError(E_VECSPEC);
            byte[] blob = c.arg(i + 1);
            if (blob.length == 0 || blob.length % 4 != 0) throw new RedisError(E_VECSPEC);
            q = new float[blob.length / 4];
            for (int j = 0; j < q.length; j++) {
                int bits = (blob[j * 4] & 0xff) | ((blob[j * 4 + 1] & 0xff) << 8)
                        | ((blob[j * 4 + 2] & 0xff) << 16) | ((blob[j * 4 + 3] & 0xff) << 24);
                q[j] = Float.intBitsToFloat(bits);
            }
            i += 2;
        } else {
            throw new RedisError(E_VECSPEC);
        }

        boolean withScores = false;
        boolean withAttribs = false;
        long count = 10;
        while (i < c.argc) {
            if (c.argIs(i, "WITHSCORES")) { withScores = true; i++; }
            else if (c.argIs(i, "WITHATTRIBS")) { withAttribs = true; i++; }
            else if (c.argIs(i, "COUNT")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                Long n = Util.string2ll(c.arg(i + 1));
                if (n == null || n <= 0) throw RedisError.syntax();
                count = n;
                i += 2;
            } else if (c.argIs(i, "EF") || c.argIs(i, "FILTER") || c.argIs(i, "FILTER-EF")
                    || c.argIs(i, "EPSILON")) {
                if (i + 1 >= c.argc) throw RedisError.syntax();
                i += 2;
            } else if (c.argIs(i, "TRUTH") || c.argIs(i, "NOTHREAD")) {
                i++;
            } else {
                throw RedisError.syntax();
            }
        }

        if (v == null) { c.addEmptyArray(); return; }
        if (ele != null) {
            RVector.Node n = v.nodes.get(new Key(ele));
            if (n == null) throw new RedisError("ERR element not found in set");
            q = v.unit(n);
        } else {
            if (q.length != v.dim && v.inputDim == 0)
                throw new RedisError("ERR Vector dimension mismatch - got " + q.length
                        + " but set has " + v.dim);
            if (v.inputDim != 0 && q.length == v.inputDim) q = RVector.project(q, v.dim);
            double sum = 0;
            for (float x : q) sum += (double) x * x;
            double l2 = Math.sqrt(sum);
            if (l2 != 0) for (int j = 0; j < q.length; j++) q[j] = (float) (q[j] / l2);
        }
        List<Object[]> hits = v.search(q);
        int k = (int) Math.min(count, hits.size());
        int per = 1 + (withScores ? 1 : 0) + (withAttribs ? 1 : 0);
        c.addArray(k * per);
        for (int j = 0; j < k; j++) {
            RVector.Node nd = (RVector.Node) hits.get(j)[0];
            c.addBulk(nd.element);
            if (withScores) addDouble(c, (Double) hits.get(j)[1]);
            // WITHATTRIBS: the attribute follows each element, and is a null
            // reply for an element that has none -- not an empty string.
            if (withAttribs) {
                if (nd.attr == null) c.addNil(); else c.addBulk(nd.attr);
            }
        }
    }

    // ------------------------------------------------------------------
    public static void register() {
        CommandTable.reg("vadd", -1, "write denyoom module", 1, 1, 1, VectorCmds::vadd);
        CommandTable.reg("vcard", -1, "readonly module fast", 1, 1, 1, VectorCmds::vcard);
        CommandTable.reg("vdim", -1, "readonly module fast", 1, 1, 1, VectorCmds::vdim);
        CommandTable.reg("vemb", -1, "readonly module fast", 1, 1, 1, VectorCmds::vemb);
        CommandTable.reg("vismember", -1, "readonly module fast", 1, 1, 1, VectorCmds::vismember);
        CommandTable.reg("vgetattr", -1, "readonly module fast", 1, 1, 1, VectorCmds::vgetattr);
        CommandTable.reg("vsetattr", -1, "write module fast", 1, 1, 1, VectorCmds::vsetattr);
        CommandTable.reg("vrem", -1, "write module", 1, 1, 1, VectorCmds::vrem);
        CommandTable.reg("vinfo", -1, "readonly module fast", 1, 1, 1, VectorCmds::vinfo);
        CommandTable.reg("vlinks", -1, "readonly module fast", 1, 1, 1, VectorCmds::vlinks);
        CommandTable.reg("vrandmember", -1, "readonly module", 1, 1, 1, VectorCmds::vrandmember);
        CommandTable.reg("vsim", -1, "readonly module", 1, 1, 1, VectorCmds::vsim);
    }
}
