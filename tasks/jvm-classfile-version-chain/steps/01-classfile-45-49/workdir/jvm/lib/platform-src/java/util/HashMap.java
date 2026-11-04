package java.util;
/**
 * Hash table implementation of Map, with the bucket layout, spreading function,
 * resize policy and iteration order specified by java.util.HashMap.
 */
public class HashMap<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {
    static boolean objEquals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }
    static int objHash(Object o) { return o != null ? o.hashCode() : 0; }

    static final int DEFAULT_INITIAL_CAPACITY = 16;
    static final float DEFAULT_LOAD_FACTOR = 0.75f;

    static class Node<K, V> implements Map.Entry<K, V> {
        final int hash;
        final K key;
        V value;
        Node<K, V> next;
        Node(int hash, K key, V value, Node<K, V> next) {
            this.hash = hash; this.key = key; this.value = value; this.next = next;
        }
        public K getKey() { return key; }
        public V getValue() { return value; }
        public V setValue(V v) { V old = value; value = v; return old; }
        public String toString() { return key + "=" + value; }
        public boolean equals(Object o) {
            if (o == this) return true;
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return objEquals(key, e.getKey()) && objEquals(value, e.getValue());
        }
        public int hashCode() { return objHash(key) ^ objHash(value); }
    }

    transient Node<K, V>[] table;
    transient int size;
    transient int modCount;
    int threshold;
    final float loadFactor;

    public HashMap() { loadFactor = DEFAULT_LOAD_FACTOR; }
    public HashMap(int initialCapacity) { this(initialCapacity, DEFAULT_LOAD_FACTOR); }
    public HashMap(int initialCapacity, float loadFactor) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal initial capacity: " + initialCapacity);
        this.loadFactor = loadFactor;
        this.threshold = tableSizeFor(initialCapacity);
    }
    public HashMap(Map<? extends K, ? extends V> m) {
        loadFactor = DEFAULT_LOAD_FACTOR;
        for (Iterator<? extends Map.Entry<? extends K, ? extends V>> it = ((Map<K, V>) m).entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<? extends K, ? extends V> e = it.next();
            put(e.getKey(), e.getValue());
        }
    }
    static int tableSizeFor(int cap) {
        int n = 1;
        while (n < cap) n <<= 1;
        return n;
    }
    static int hash(Object key) {
        int h;
        return (key == null) ? 0 : (h = key.hashCode()) ^ (h >>> 16);
    }
    public int size() { return size; }
    public boolean isEmpty() { return size == 0; }
    public V get(Object key) {
        Node<K, V> e = getNode(hash(key), key);
        return e == null ? null : e.value;
    }
    public V getOrDefault(Object key, V def) {
        Node<K, V> e = getNode(hash(key), key);
        return e == null ? def : e.value;
    }
    final Node<K, V> getNode(int hash, Object key) {
        Node<K, V>[] tab = table;
        if (tab == null || tab.length == 0) return null;
        Node<K, V> e = tab[(tab.length - 1) & hash];
        while (e != null) {
            if (e.hash == hash && (e.key == key || (key != null && key.equals(e.key)))) return e;
            e = e.next;
        }
        return null;
    }
    public boolean containsKey(Object key) { return getNode(hash(key), key) != null; }
    public boolean containsValue(Object value) {
        if (table != null)
            for (int i = 0; i < table.length; i++)
                for (Node<K, V> e = table[i]; e != null; e = e.next)
                    if (objEquals(value, e.value)) return true;
        return false;
    }
    public V put(K key, V value) { return putVal(hash(key), key, value, false); }
    public V putIfAbsent(K key, V value) { return putVal(hash(key), key, value, true); }
    final V putVal(int hash, K key, V value, boolean onlyIfAbsent) {
        if (table == null || table.length == 0) resize();
        int n = table.length;
        int i = (n - 1) & hash;
        Node<K, V> p = table[i];
        if (p == null) {
            table[i] = new Node<K, V>(hash, key, value, null);
        } else {
            Node<K, V> e = null;
            while (true) {
                if (p.hash == hash && (p.key == key || (key != null && key.equals(p.key)))) { e = p; break; }
                if (p.next == null) { p.next = new Node<K, V>(hash, key, value, null); break; }
                p = p.next;
            }
            if (e != null) {
                V old = e.value;
                if (!onlyIfAbsent || old == null) e.value = value;
                return old;
            }
        }
        modCount++;
        if (++size > threshold) resize();
        return null;
    }
    final void resize() {
        Node<K, V>[] oldTab = table;
        int oldCap = (oldTab == null) ? 0 : oldTab.length;
        int oldThr = threshold;
        int newCap, newThr = 0;
        if (oldCap > 0) {
            newCap = oldCap << 1;
            newThr = oldThr << 1;
        } else if (oldThr > 0) {
            newCap = oldThr;
        } else {
            newCap = DEFAULT_INITIAL_CAPACITY;
            newThr = (int) (DEFAULT_LOAD_FACTOR * DEFAULT_INITIAL_CAPACITY);
        }
        if (newThr == 0) newThr = (int) (newCap * loadFactor);
        threshold = newThr;
        Node<K, V>[] newTab = (Node<K, V>[]) new Node[newCap];
        table = newTab;
        if (oldTab != null) {
            for (int j = 0; j < oldCap; j++) {
                Node<K, V> e = oldTab[j];
                if (e == null) continue;
                oldTab[j] = null;
                if (e.next == null) {
                    newTab[e.hash & (newCap - 1)] = e;
                } else {
                    Node<K, V> loHead = null, loTail = null, hiHead = null, hiTail = null, next;
                    do {
                        next = e.next;
                        if ((e.hash & oldCap) == 0) {
                            if (loTail == null) loHead = e; else loTail.next = e;
                            loTail = e;
                        } else {
                            if (hiTail == null) hiHead = e; else hiTail.next = e;
                            hiTail = e;
                        }
                        e = next;
                    } while (e != null);
                    if (loTail != null) { loTail.next = null; newTab[j] = loHead; }
                    if (hiTail != null) { hiTail.next = null; newTab[j + oldCap] = hiHead; }
                }
            }
        }
    }
    public V remove(Object key) {
        Node<K, V> e = removeNode(hash(key), key);
        return e == null ? null : e.value;
    }
    final Node<K, V> removeNode(int hash, Object key) {
        if (table == null || table.length == 0) return null;
        int i = (table.length - 1) & hash;
        Node<K, V> p = table[i], prev = null;
        while (p != null) {
            if (p.hash == hash && (p.key == key || (key != null && key.equals(p.key)))) {
                if (prev == null) table[i] = p.next; else prev.next = p.next;
                modCount++;
                size--;
                return p;
            }
            prev = p;
            p = p.next;
        }
        return null;
    }
    public void clear() {
        modCount++;
        size = 0;
        if (table != null) for (int i = 0; i < table.length; i++) table[i] = null;
    }
    public Set<K> keySet() {
        LinkedHashSet<K> s = new LinkedHashSet<K>();
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) s.add(it.next().getKey());
        return s;
    }
    public Collection<V> values() {
        ArrayList<V> l = new ArrayList<V>();
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) l.add(it.next().getValue());
        return l;
    }
    public Set<Map.Entry<K, V>> entrySet() {
        LinkedHashSet<Map.Entry<K, V>> s = new LinkedHashSet<Map.Entry<K, V>>();
        if (table != null)
            for (int i = 0; i < table.length; i++)
                for (Node<K, V> e = table[i]; e != null; e = e.next) s.add(e);
        return s;
    }
    public String toString() {
        StringBuilder sb = new StringBuilder("{");
        boolean first = true;
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<K, V> e = it.next();
            if (!first) sb.append(", ");
            first = false;
            sb.append(e.getKey() == this ? "(this Map)" : String.valueOf(e.getKey()));
            sb.append('=');
            sb.append(e.getValue() == this ? "(this Map)" : String.valueOf(e.getValue()));
        }
        return sb.append('}').toString();
    }
    public int hashCode() {
        int h = 0;
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) h += it.next().hashCode();
        return h;
    }
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Map)) return false;
        Map<K, V> m = (Map<K, V>) o;
        if (m.size() != size) return false;
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<K, V> e = it.next();
            V v = m.get(e.getKey());
            if (!objEquals(v, e.getValue()) || (v == null && !m.containsKey(e.getKey()))) return false;
        }
        return true;
    }
}
