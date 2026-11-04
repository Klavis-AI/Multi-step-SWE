package java.util;
public interface Collection<E> extends Iterable<E> {
    int size();
    boolean isEmpty();
    boolean contains(Object o);
    boolean add(E e);
    boolean remove(Object o);
    void clear();
    Object[] toArray();
    boolean addAll(Collection<? extends E> c);
    boolean containsAll(Collection<?> c);
}
