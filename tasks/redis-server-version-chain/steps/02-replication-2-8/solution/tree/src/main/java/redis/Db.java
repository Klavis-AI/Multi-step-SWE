package redis;

import java.util.*;

/** One numbered database: the keyspace dict plus the expires dict. */
public final class Db {
    public final int id;
    /** The keyspace. Every mutation feeds the {@link Mem} dataset model. */
    public final Keyspace dict = new Keyspace();

    /**
     * db->keys: a LinkedHashMap that keeps the approximate serialized footprint
     * of the keyspace up to date on every add/replace/remove, which is what
     * makes used_memory (and therefore maxmemory) observable.
     */
    public static final class Keyspace extends LinkedHashMap<Key, RObj> {
        @Override public RObj put(Key k, RObj v) {
            RObj old = super.put(k, v);
            Mem.onPut(k, old, v);
            return old;
        }
        @Override public void putAll(Map<? extends Key, ? extends RObj> m) {
            for (Map.Entry<? extends Key, ? extends RObj> e : m.entrySet()) put(e.getKey(), e.getValue());
        }
        @Override public RObj remove(Object k) {
            RObj old = super.remove(k);
            if (old != null && k instanceof Key) Mem.onRemove((Key) k, old);
            return old;
        }
        @Override public void clear() {
            for (Map.Entry<Key, RObj> e : entrySet()) Mem.onRemove(e.getKey(), e.getValue());
            super.clear();
        }
    }
    public final HashMap<Key, Long> expires = new HashMap<>();
    /**
     * db->hexpires: the keys of the hashes that own at least one field TTL.
     * Redis 7.4 keeps an ebuckets index ordered by the next expiration; this port
     * keeps only the membership, which is all the active cycle needs to avoid
     * walking the whole keyspace. Entries are best effort and are validated (and
     * dropped) by {@link HashCmds#activeExpireHashFields}.
     */
    public final LinkedHashSet<Key> hexpires = new LinkedHashSet<>();
    /** keys watched by clients, for WATCH/EXEC */
    public final HashMap<Key, List<Client>> watchedKeys = new HashMap<>();
    /** clients blocked on keys (BLPOP and friends) */
    public final HashMap<Key, List<Client>> blockingKeys = new HashMap<>();
    public final LinkedHashSet<Key> readyKeys = new LinkedHashSet<>();

    public Db(int id) { this.id = id; }

    public int size() { return dict.size(); }

    public RObj lookup(Key k) { return dict.get(k); }

    public void add(Key k, RObj o) { dict.put(k, o); }

    public boolean delete(Key k) {
        expires.remove(k);
        hexpires.remove(k);
        return dict.remove(k) != null;
    }

    public void clear() { dict.clear(); expires.clear(); hexpires.clear(); }

    public Long getExpire(Key k) { return expires.get(k); }

    public void setExpire(Key k, long whenMs) { expires.put(k, whenMs); }

    public void removeExpire(Key k) { expires.remove(k); }

    public Key randomKey() {
        if (dict.isEmpty()) return null;
        int n = ThreadLocalRandomHolder.RND.nextInt(dict.size());
        Iterator<Key> it = dict.keySet().iterator();
        Key k = null;
        for (int i = 0; i <= n && it.hasNext(); i++) k = it.next();
        return k;
    }

    static final class ThreadLocalRandomHolder {
        static final Random RND = new Random();
    }
}
