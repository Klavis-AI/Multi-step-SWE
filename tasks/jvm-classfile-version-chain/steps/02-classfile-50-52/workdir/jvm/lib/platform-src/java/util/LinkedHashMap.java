package java.util;
public class LinkedHashMap<K, V> extends HashMap<K, V> {
    static boolean objEquals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }
    static int objHash(Object o) { return o != null ? o.hashCode() : 0; }

    private final ArrayList<K> order = new ArrayList<K>();
    public LinkedHashMap() { super(); }
    public LinkedHashMap(int initialCapacity) { super(initialCapacity); }
    public V put(K key, V value) {
        boolean had = containsKey(key);
        V old = super.put(key, value);
        if (!had) order.add(key);
        return old;
    }
    public V remove(Object key) {
        if (containsKey(key)) order.remove(key);
        return super.remove(key);
    }
    public void clear() { super.clear(); order.clear(); }
    public Set<Map.Entry<K, V>> entrySet() {
        LinkedHashSet<Map.Entry<K, V>> s = new LinkedHashSet<Map.Entry<K, V>>();
        for (int i = 0; i < order.size(); i++) {
            K k = order.get(i);
            s.add(new SimpleEntry<K, V>(k, get(k)));
        }
        return s;
    }
    public Set<K> keySet() {
        LinkedHashSet<K> s = new LinkedHashSet<K>();
        for (int i = 0; i < order.size(); i++) s.add(order.get(i));
        return s;
    }
    public Collection<V> values() {
        ArrayList<V> l = new ArrayList<V>();
        for (int i = 0; i < order.size(); i++) l.add(get(order.get(i)));
        return l;
    }
    static class SimpleEntry<K, V> implements Map.Entry<K, V> {
        final K k;
        V v;
        SimpleEntry(K k, V v) { this.k = k; this.v = v; }
        public K getKey() { return k; }
        public V getValue() { return v; }
        public V setValue(V nv) { V o = v; v = nv; return o; }
        public String toString() { return k + "=" + v; }
        public boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return objEquals(k, e.getKey()) && objEquals(v, e.getValue());
        }
        public int hashCode() { return objHash(k) ^ objHash(v); }
    }
}
