package java.util;
public class LinkedHashSet<E> implements Set<E>, Cloneable, java.io.Serializable {
    static boolean objEquals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }
    static int objHash(Object o) { return o != null ? o.hashCode() : 0; }

    private final ArrayList<E> items = new ArrayList<E>();
    public LinkedHashSet() {}
    public LinkedHashSet(Collection<? extends E> c) { addAll(c); }
    public int size() { return items.size(); }
    public boolean isEmpty() { return items.isEmpty(); }
    public boolean contains(Object o) { return items.contains(o); }
    public boolean add(E e) {
        if (items.contains(e)) return false;
        items.add(e);
        return true;
    }
    public boolean remove(Object o) { return items.remove(o); }
    public void clear() { items.clear(); }
    public Iterator<E> iterator() { return items.iterator(); }
    public Object[] toArray() { return items.toArray(); }
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
        for (int i = 0; i < items.size(); i++) {
            if (i > 0) sb.append(", ");
            sb.append(String.valueOf(items.get(i)));
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
        for (int i = 0; i < items.size(); i++) h += objHash(items.get(i));
        return h;
    }
}
