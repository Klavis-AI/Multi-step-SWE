package redis;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The value behind a `vectorset` key.
 *
 * The module stores every vector L2-normalised and quantised, keeping the norm
 * and the quantisation range alongside so VEMB can hand back an approximation
 * of what went in; we do exactly the same. The neighbour search is exact here
 * rather than HNSW-approximate, which can only make VSIM more accurate than
 * the module, never less.
 */
public final class RVector implements ModVal {
    public static final int Q_INT8 = 0, Q_NONE = 1, Q_BIN = 2;
    public static final String[] Q_NAMES = {"int8", "f32", "bin"};

    public static final class Node {
        public byte[] element;
        public byte[] quants;      // int8 quantised, normalised vector
        public float range;        // max abs component after normalisation
        public float l2;           // original norm
        public float[] vec;        // the normalised vector, for NOQUANT
        public byte[] attr;
    }

    public int dim;
    public int inputDim;           // != 0 only when REDUCE projected the input
    public int quant = Q_INT8;
    public int m = 16;
    public final LinkedHashMap<Key, Node> nodes = new LinkedHashMap<>();
    public long uid;
    public long maxNodeUid;

    @Override public String moduleTypeName() { return "vectorset"; }
    @Override public int moduleId() { return ModTypes.ID_VECTOR; }

    public int attributesCount() {
        int n = 0;
        for (Node nd : nodes.values()) if (nd.attr != null) n++;
        return n;
    }

    /** quantize_to_q8() over the normalised vector. */
    public Node makeNode(byte[] element, float[] raw) {
        Node n = new Node();
        n.element = element;
        double sum = 0;
        for (float v : raw) sum += (double) v * v;
        float l2 = (float) Math.sqrt(sum);
        n.l2 = l2;
        float[] norm = new float[raw.length];
        for (int i = 0; i < raw.length; i++) norm[i] = l2 == 0 ? 0 : (float) (raw[i] / l2);
        n.vec = norm;
        float maxAbs = 0;
        for (float v : norm) maxAbs = Math.max(maxAbs, Math.abs(v));
        n.range = maxAbs;
        n.quants = new byte[raw.length];
        if (maxAbs != 0) {
            float scale = 127.0f / maxAbs;
            for (int i = 0; i < raw.length; i++) n.quants[i] = (byte) Math.round(norm[i] * scale);
        }
        return n;
    }

    /** hnsw_get_node_vector(): de-quantise, then de-normalise. */
    public float[] emb(Node n) {
        float[] out = new float[dim];
        for (int j = 0; j < dim; j++) {
            float v;
            if (quant == Q_NONE) v = n.vec[j];
            else if (quant == Q_BIN) v = n.quants[j] >= 0 ? 1.0f : -1.0f;
            else v = (n.quants[j] * n.range) / 127.0f;
            out[j] = v * n.l2;
        }
        return out;
    }

    /** The normalised vector the distance is measured on. */
    public float[] unit(Node n) {
        float[] out = new float[dim];
        for (int j = 0; j < dim; j++) {
            if (quant == Q_NONE) out[j] = n.vec[j];
            else if (quant == Q_BIN) out[j] = n.quants[j] >= 0 ? 1.0f : -1.0f;
            else out[j] = (n.quants[j] * n.range) / 127.0f;
        }
        return out;
    }

    /** The module reports similarity as (1 + cosine) / 2, so 1 means identical. */
    public static double similarity(float[] a, float[] b) {
        double dot = 0, na = 0, nb = 0;
        for (int i = 0; i < a.length; i++) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
        if (na == 0 || nb == 0) return 0.5;
        double cos = dot / (Math.sqrt(na) * Math.sqrt(nb));
        if (cos > 1) cos = 1;
        if (cos < -1) cos = -1;
        return (1.0 + cos) / 2.0;
    }

    /** A stable pseudo-random projection so REDUCE survives a reload. */
    public static float[] project(float[] in, int outDim) {
        float[] out = new float[outDim];
        for (int i = 0; i < outDim; i++) {
            double acc = 0;
            for (int j = 0; j < in.length; j++) {
                long h = RBloom.murmur64(Util.b(i + ":" + j), 0x5bd1e995L);
                double w = ((h >>> 11) / (double) (1L << 53)) * 2.0 - 1.0;
                acc += in[j] * w;
            }
            out[i] = (float) acc;
        }
        return out;
    }

    @Override public byte[] moduleSave() {
        ModTypes.Out o = new ModTypes.Out();
        o.u8(1);
        o.i32(dim); o.i32(inputDim); o.i32(quant); o.i32(m);
        o.i64(uid); o.i64(maxNodeUid);
        o.i32(nodes.size());
        for (Node n : nodes.values()) {
            o.blob(n.element);
            o.blob(n.quants);
            o.dbl(n.range);
            o.dbl(n.l2);
            o.i32(n.vec.length);
            for (float v : n.vec) o.dbl(v);
            o.u8(n.attr == null ? 0 : 1);
            if (n.attr != null) o.blob(n.attr);
        }
        return o.bytes();
    }

    public static Object moduleLoad(byte[] payload) {
        ModTypes.In in = new ModTypes.In(payload);
        in.u8();
        RVector v = new RVector();
        v.dim = in.i32(); v.inputDim = in.i32(); v.quant = in.i32(); v.m = in.i32();
        v.uid = in.i64(); v.maxNodeUid = in.i64();
        int n = in.i32();
        for (int i = 0; i < n; i++) {
            Node nd = new Node();
            nd.element = in.blob();
            nd.quants = in.blob();
            nd.range = (float) in.dbl();
            nd.l2 = (float) in.dbl();
            int k = in.i32();
            nd.vec = new float[k];
            for (int j = 0; j < k; j++) nd.vec[j] = (float) in.dbl();
            if (in.u8() != 0) nd.attr = in.blob();
            v.nodes.put(new Key(nd.element), nd);
        }
        return v;
    }

    /** Every member sorted by descending similarity to `q`. */
    public List<Object[]> search(float[] q) {
        List<Object[]> out = new ArrayList<>();
        for (Map.Entry<Key, Node> e : nodes.entrySet()) {
            out.add(new Object[]{e.getValue(), similarity(unit(e.getValue()), q)});
        }
        out.sort((a, b) -> Double.compare((Double) b[1], (Double) a[1]));
        return out;
    }
}
