package redis;

import java.util.HashMap;
import java.util.Map;

/**
 * The registry that turns a {@link ModVal} back into an object when an RDB
 * file or a RESTORE payload is read.
 *
 * Ids are frozen: a dump written by an earlier rung must keep loading, so new
 * types take the next free number and old numbers are never reused.
 */
public final class ModTypes {
    private ModTypes() {}

    public static final int ID_JSON = 1;
    public static final int ID_BLOOM = 2;
    public static final int ID_CUCKOO = 3;
    public static final int ID_CMS = 4;
    public static final int ID_TOPK = 5;
    public static final int ID_TDIGEST = 6;
    public static final int ID_TS = 7;
    public static final int ID_VECTOR = 8;

    public interface Loader { Object load(byte[] payload); }

    private static final Map<Integer, Loader> LOADERS = new HashMap<>();

    public static void register(int id, Loader l) { LOADERS.put(id, l); }

    public static Object load(int id, byte[] payload) {
        Loader l = LOADERS.get(id);
        if (l == null) throw new RedisError("ERR Bad data format");
        return l.load(payload);
    }

    /** Fill the registry; called once from CommandTable.init(). */
    public static void init() {
        register(ID_JSON, RJson::moduleLoad);
        register(ID_BLOOM, RBloom::loadBloom);
        register(ID_CUCKOO, RBloom::loadCuckoo);
        register(ID_CMS, RBloom::loadCms);
        register(ID_TOPK, RBloom::loadTopk);
        register(ID_TDIGEST, RBloom::loadTDigest);
        register(ID_TS, RTs::moduleLoad);
        register(ID_VECTOR, RVector::moduleLoad);
    }

    /** Wrap a module value in an RObj. */
    public static RObj obj(ModVal v) { return new RObj(RObj.MODULE, RObj.ENC_RAW, v); }

    // ---- small binary helpers shared by every module serializer ----

    public static final class Out {
        public final Buf b = new Buf(64);
        public Out u8(int v) { b.put((byte) v); return this; }
        public Out i32(int v) {
            b.put((byte) (v >>> 24)).put((byte) (v >>> 16)).put((byte) (v >>> 8)).put((byte) v);
            return this;
        }
        public Out i64(long v) {
            for (int i = 56; i >= 0; i -= 8) b.put((byte) (v >>> i));
            return this;
        }
        public Out dbl(double v) { return i64(Double.doubleToRawLongBits(v)); }
        public Out blob(byte[] v) { i32(v.length); b.put(v); return this; }
        public Out str(String v) { return blob(Util.b(v)); }
        public byte[] bytes() { return b.toBytes(); }
    }

    public static final class In {
        private final byte[] a;
        private int p;
        public In(byte[] a) { this.a = a; }
        public int u8() { return a[p++] & 0xff; }
        public int i32() {
            return ((a[p++] & 0xff) << 24) | ((a[p++] & 0xff) << 16) | ((a[p++] & 0xff) << 8) | (a[p++] & 0xff);
        }
        public long i64() {
            long v = 0;
            for (int i = 0; i < 8; i++) v = (v << 8) | (a[p++] & 0xff);
            return v;
        }
        public double dbl() { return Double.longBitsToDouble(i64()); }
        public byte[] blob() {
            int n = i32();
            byte[] r = new byte[n];
            System.arraycopy(a, p, r, 0, n);
            p += n;
            return r;
        }
        public String str() { return Util.s(blob()); }
        public boolean more() { return p < a.length; }
    }
}
