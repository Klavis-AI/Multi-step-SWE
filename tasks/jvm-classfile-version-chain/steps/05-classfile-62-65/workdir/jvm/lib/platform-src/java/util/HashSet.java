package java.util;
public class HashSet<E> implements Set<E>, Cloneable, java.io.Serializable {
    static boolean objEquals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }
    static int objHash(Object o) { return o != null ? o.hashCode() : 0; }

    final HashMap<E, Object> map;
    private static final Object PRESENT = new Object();
    public HashSet() { map = new HashMap<E, Object>(); }
    public HashSet(int initialCapacity) { map = new HashMap<E, Object>(initialCapacity); }
    public HashSet(Collection<? extends E> c) { map = new HashMap<E, Object>(); addAll(c); }
    HashSet(HashMap<E, Object> m) { map = m; }
    public int size() { return map.size(); }
    public boolean isEmpty() { return map.isEmpty(); }
    public boolean contains(Object o) { return map.containsKey(o); }
    public boolean add(E e) { return map.put(e, PRESENT) == null; }
    public boolean remove(Object o) { return map.remove(o) != null; }
    public void clear() { map.clear(); }
    public Iterator<E> iterator() { return map.keySet().iterator(); }
    public Object[] toArray() {
        Object[] r = new Object[size()];
        int i = 0;
        for (Iterator<E> it = iterator(); it.hasNext(); ) r[i++] = it.next();
        return r;
    }
    public boolean addAll(Collection<? extends E> c) {
        boolean changed = false;
        for (Iterator<? extends E> it = c.iterator(); it.hasNext(); ) changed |= add(it.next());
        return changed;
    }
    public boolean containsAll(Collection<?> c) {
        for (Iterator<?> it = c.iterator(); it.hasNext(); ) if (!contains(it.next())) return false;
        return true;
    }
    public String toString() {
        StringBuilder sb = new StringBuilder("[");
        boolean first = true;
        for (Iterator<E> it = iterator(); it.hasNext(); ) {
            if (!first) sb.append(", ");
            first = false;
            sb.append(String.valueOf(it.next()));
        }
        return sb.append(']').toString();
    }
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Set)) return false;
        Set<?> s = (Set<?>) o;
        return s.size() == size() && containsAll(s);
    }
    public int hashCode() {
        int h = 0;
        for (Iterator<E> it = iterator(); it.hasNext(); ) h += objHash(it.next());
        return h;
    }
}
