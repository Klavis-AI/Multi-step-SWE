package redis;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Shared helpers over the value containers. */
public final class Types {
    private Types() {}

    /** aof.c AOF_REWRITE_ITEMS_PER_CMD */
    public static final int ITEMS_PER_CMD = 64;

    /**
     * Emit the commands that recreate one key, the way rewriteAppendOnlyFileRio()
     * does: SET for strings, and batches of at most 64 items per RPUSH / SADD /
     * ZADD / HSET for the containers.
     */
    public static void emitRestoreCommands(Server s, Buf b, Key k, RObj o) {
        if (o == null) return;
        switch (o.type) {
            case RObj.STRING:
                Aof.writeCmd(b, Util.b("SET"), k.b, o.str());
                return;
            case RObj.LIST:
                emitBatched(b, Util.b("RPUSH"), k.b, ((RList) o.ptr).asList(), 1);
                return;
            case RObj.SET:
                emitBatched(b, Util.b("SADD"), k.b, ((RSet) o.ptr).members(), 1);
                return;
            case RObj.ZSET: {
                RZSet z = (RZSet) o.ptr;
                List<byte[]> args = new ArrayList<>(z.size() * 2);
                for (RZSet.Entry e : z.all(false)) {
                    args.add(Util.d2string(e.score));
                    args.add(e.member.b);
                }
                emitBatched(b, Util.b("ZADD"), k.b, args, 2);
                return;
            }
            case RObj.HASH: {
                RHash h = (RHash) o.ptr;
                if (Rdb.minExpire(h) == Rdb.NO_EXPIRE) {
                    List<byte[]> args = new ArrayList<>(h.size() * 2);
                    for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                        args.add(e.getKey().b);
                        args.add(e.getValue());
                    }
                    /* aof.c rewriteHashObject() emits HMSET batches. */
                    emitBatched(b, Util.b("HMSET"), k.b, args, 2);
                } else {
                    /* With field TTLs redis emits one field per command so the
                     * matching HPEXPIREAT can follow it. */
                    for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                        Aof.writeCmd(b, Util.b("HSET"), k.b, e.getKey().b, e.getValue());
                        Long t = h.ttl(e.getKey());
                        if (t != null) {
                            Aof.writeCmd(b, Util.b("HPEXPIREAT"), k.b, Util.ll2string(t),
                                    Util.b("FIELDS"), Util.b("1"), e.getKey().b);
                        }
                    }
                }
                return;
            }
            case RObj.STREAM:
                StreamCmds.emitAofRewrite(s, b, k, o);
                return;
            default:
                // Module values have no command based representation here.
                return;
        }
    }

    /**
     * Write `args` as a sequence of `CMD key ...` commands, each carrying at most
     * ITEMS_PER_CMD logical items (an item is `group` consecutive arguments).
     */
    private static void emitBatched(Buf b, byte[] cmd, byte[] key, List<byte[]> args, int group) {
        int items = args.size() / group;
        int i = 0;
        while (i < items) {
            int n = Math.min(ITEMS_PER_CMD, items - i);
            byte[][] argv = new byte[2 + n * group][];
            argv[0] = cmd;
            argv[1] = key;
            for (int j = 0; j < n * group; j++) argv[2 + j] = args.get(i * group + j);
            Aof.writeCmd(b, argv);
            i += n;
        }
    }

    /**
     * Recompute `o.encoding` from the current contents and the configured
     * listpack/intset thresholds, the way redis's *TypeTryConversion helpers do.
     */
    public static void updateEncoding(Server s, RObj o) {
        if (o == null) return;
        switch (o.type) {
            case RObj.LIST: o.encoding = Rdb.listEncoding(s, (RList) o.ptr); break;
            case RObj.SET: o.encoding = Rdb.setEncoding(s, (RSet) o.ptr); break;
            case RObj.ZSET: o.encoding = Rdb.zsetEncoding(s, (RZSet) o.ptr); break;
            case RObj.HASH: {
                RHash h = (RHash) o.ptr;
                int enc = Rdb.hashEncoding(s, h);
                if (enc == RObj.ENC_LISTPACK && Rdb.minExpire(h) != Rdb.NO_EXPIRE)
                    enc = RObj.ENC_LISTPACK_EX;
                o.encoding = enc;
                break;
            }
            default: break;
        }
    }
}
