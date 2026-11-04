package java.util;
public final class Collections {
    private Collections() {}
    public static <T> void sort(List<T> list) {
        Object[] a = list.toArray();
        Arrays.sort(a);
        for (int i = 0; i < a.length; i++) list.set(i, (T) a[i]);
    }
    public static <T> void sort(List<T> list, Comparator<? super T> c) {
        Object[] a = list.toArray();
        Arrays.sort((T[]) a, (Comparator<? super T>) c);
        for (int i = 0; i < a.length; i++) list.set(i, (T) a[i]);
    }
    public static void reverse(List<?> list) {
        List<Object> l = (List<Object>) list;
        for (int i = 0, j = l.size() - 1; i < j; i++, j--) {
            Object t = l.get(i);
            l.set(i, l.get(j));
            l.set(j, t);
        }
    }
    public static <T> List<T> emptyList() { return new ArrayList<T>(); }
    public static <K, V> Map<K, V> emptyMap() { return new HashMap<K, V>(); }
    public static <T> List<T> unmodifiableList(List<T> l) { return l; }
    public static <K, V> Map<K, V> unmodifiableMap(Map<K, V> m) { return m; }
    public static <T> List<T> singletonList(T o) {
        ArrayList<T> l = new ArrayList<T>();
        l.add(o);
        return l;
    }
    public static int frequency(Collection<?> c, Object o) {
        int n = 0;
        for (Iterator<?> it = c.iterator(); it.hasNext(); ) { Object v = it.next(); if (o == v || (o != null && o.equals(v))) n++; }
        return n;
    }
}
