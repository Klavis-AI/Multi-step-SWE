package redis;

import java.util.*;

/** BLPOP/BRPOP/BLMOVE/BLMPOP/BZPOPMIN/BZPOPMAX/WAIT bookkeeping. */
public final class Blocking {
    public interface Ready { boolean tryServe(Server s, Client c, Key key); }

    public static void blockOnKeys(Server s, Client c, List<byte[]> keys, double timeoutSec,
                                   String type, Object state) {
        c.blocked = true;
        c.blockType = type;
        c.blockState = state;
        c.blockTimeoutMs = timeoutSec <= 0 ? 0 : System.currentTimeMillis() + (long) (timeoutSec * 1000);
        c.blockKeys = new ArrayList<>();
        for (byte[] kb : keys) {
            Key k = new Key(kb);
            c.blockKeys.add(k);
            s.dbs[c.db].blockingKeys.computeIfAbsent(k, x -> new ArrayList<>()).add(c);
        }
    }

    public static double parseTimeout(Client c, int idx, boolean seconds) {
        Double d = Util.string2d(c.argv[idx]);
        if (d == null) throw new RedisError("ERR timeout is not a float or out of range");
        if (d < 0 || d.isNaN()) throw new RedisError("ERR timeout is negative");
        if (d.isInfinite()) throw new RedisError("ERR timeout is out of range");
        return seconds ? d : d / 1000.0;
    }

    public static void serveClientsBlockedOnKey(Server s, Db d, Key k) {
        List<Client> l = d.blockingKeys.get(k);
        if (l == null) return;
        List<Client> copy = new ArrayList<>(l);
        for (Client c : copy) {
            if (!c.blocked) continue;
            Ready r = READY.get(c.blockType);
            if (r == null) continue;
            if (r.tryServe(s, c, k)) {
                s.unblockClient(c);
                s.updateInterest(c);
            }
        }
    }

    public static final Map<String, Ready> READY = new HashMap<>();

    public static void checkTimeouts(Server s) {
        long now = System.currentTimeMillis();
        for (Client c : new ArrayList<>(s.clients.values())) {
            if (!c.blocked || c.blockTimeoutMs == 0) continue;
            if (now >= c.blockTimeoutMs) {
                Timeout t = TIMEOUT.get(c.blockType);
                if (t != null) t.onTimeout(s, c);
                else c.addNilArray();
                s.unblockClient(c);
                s.updateInterest(c);
            }
        }
    }

    public interface Timeout { void onTimeout(Server s, Client c); }
    public static final Map<String, Timeout> TIMEOUT = new HashMap<>();
}
