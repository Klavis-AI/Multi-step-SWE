package redis;

import java.util.*;

/** Hash value: field -> value, with optional per-field expiration (Redis 7.4). */
public final class RHash {
    public final LinkedHashMap<Key, byte[]> map = new LinkedHashMap<>();
    public HashMap<Key, Long> ttls;   // absolute ms, lazily created

    public int size() { return map.size(); }

    public Long ttl(Key f) { return ttls == null ? null : ttls.get(f); }

    public void setTtl(Key f, long whenMs) {
        if (ttls == null) ttls = new HashMap<>();
        ttls.put(f, whenMs);
    }

    public void clearTtl(Key f) { if (ttls != null) ttls.remove(f); }

    public boolean hasTtls() { return ttls != null && !ttls.isEmpty(); }

    /** Drop fields whose TTL has passed; returns the number removed. */
    public int expireFields(long now) {
        if (ttls == null || ttls.isEmpty()) return 0;
        List<Key> dead = new ArrayList<>();
        for (Map.Entry<Key, Long> e : ttls.entrySet()) if (now >= e.getValue()) dead.add(e.getKey());
        for (Key k : dead) { ttls.remove(k); map.remove(k); }
        return dead.size();
    }
}
