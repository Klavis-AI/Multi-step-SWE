package redis;

import java.security.MessageDigest;
import java.util.Map;

/**
 * The dataset digest used by DEBUG DIGEST (debug.c computeDatasetDigest()).
 *
 * Aggregates that have no intrinsic order (the keyspace itself, sets, hash
 * fields, sorted set members) are folded together with xorDigest() so the
 * iteration order cannot change the result; ordered aggregates (lists) use
 * mixDigest(), which feeds the running digest back into SHA1.
 */
public final class Digest {
    private Digest() {}

    static byte[] sha1(byte[] data, int off, int len) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-1");
            md.update(data, off, len);
            return md.digest();
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    /** digest ^= SHA1(data) */
    static void xorDigest(byte[] digest, byte[] data) {
        byte[] h = sha1(data, 0, data.length);
        for (int j = 0; j < 20; j++) digest[j] ^= h[j];
    }

    /** digest = SHA1(digest ^ SHA1(data)) */
    static void mixDigest(byte[] digest, byte[] data) {
        xorDigest(digest, data);
        byte[] h = sha1(digest, 0, 20);
        System.arraycopy(h, 0, digest, 0, 20);
    }

    static byte[] be32(int v) {
        return new byte[]{(byte) (v >>> 24), (byte) (v >>> 16), (byte) (v >>> 8), (byte) v};
    }

    /** The digest of one value, as xorObjectDigest()/mixObjectDigest() build it. */
    public static void valueDigest(byte[] digest, RObj o) {
        switch (o.type) {
            case RObj.STRING:
                mixDigest(digest, o.str());
                return;
            case RObj.LIST:
                for (byte[] e : ((RList) o.ptr).l) mixDigest(digest, e);
                return;
            case RObj.SET:
                for (Key k : ((RSet) o.ptr).set) xorDigest(digest, k.b);
                return;
            case RObj.ZSET: {
                RZSet z = (RZSet) o.ptr;
                for (Map.Entry<Key, Double> e : z.dict.entrySet()) {
                    byte[] ele = new byte[20];
                    mixDigest(ele, e.getKey().b);
                    mixDigest(ele, Util.b(Util.fmtG(e.getValue(), 17)));
                    xorDigest(digest, ele);
                }
                return;
            }
            case RObj.HASH: {
                RHash h = (RHash) o.ptr;
                for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                    byte[] ele = new byte[20];
                    mixDigest(ele, e.getKey().b);
                    mixDigest(ele, e.getValue());
                    xorDigest(digest, ele);
                }
                return;
            }
            case RObj.STREAM:
                StreamCmds.digest(digest, o);
                return;
            default:
                return;
        }
    }

    /** computeDatasetDigest(): 20 raw bytes covering every database. */
    public static byte[] dataset(Server s) {
        byte[] fin = new byte[20];
        for (int j = 0; j < s.dbs.length; j++) {
            Db db = s.dbs[j];
            if (db.dict.isEmpty()) continue;
            mixDigest(fin, be32(j));
            for (Map.Entry<Key, RObj> e : new java.util.ArrayList<>(db.dict.entrySet())) {
                Key k = e.getKey();
                RObj o = e.getValue();
                byte[] d = new byte[20];
                mixDigest(d, k.b);
                mixDigest(d, be32(o.type));
                valueDigest(d, o);
                if (db.expires.containsKey(k)) xorDigest(d, Util.b("!!expire!!"));
                xorDigest(fin, d);
            }
        }
        return fin;
    }

    public static String hex(byte[] d) {
        StringBuilder sb = new StringBuilder(d.length * 2);
        for (byte b : d) sb.append(Character.forDigit((b >> 4) & 0xf, 16))
                           .append(Character.forDigit(b & 0xf, 16));
        return sb.toString();
    }
}
