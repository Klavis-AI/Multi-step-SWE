package redis;

import java.util.*;

/** Set value. `allInts` tracks whether every member parses as a long (intset). */
public final class RSet {
    public final LinkedHashSet<Key> set = new LinkedHashSet<>();
    public int size() { return set.size(); }
    public boolean add(byte[] v) { return set.add(new Key(v)); }
    public boolean remove(byte[] v) { return set.remove(new Key(v)); }
    public boolean contains(byte[] v) { return set.contains(new Key(v)); }
    public List<byte[]> members() {
        List<byte[]> out = new ArrayList<>(set.size());
        for (Key k : set) out.add(k.b);
        return out;
    }
    public boolean allInts() {
        for (Key k : set) if (Util.string2ll(k.b) == null) return false;
        return true;
    }
}
