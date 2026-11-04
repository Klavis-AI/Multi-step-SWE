package java.util;
public class ArrayList<E> implements List<E>, Cloneable, java.io.Serializable {
    static boolean objEquals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }
    static int objHash(Object o) { return o != null ? o.hashCode() : 0; }

    private Object[] elementData;
    private int size;
    protected transient int modCount;

    public ArrayList() { elementData = new Object[10]; }
    public ArrayList(int initialCapacity) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal Capacity: " + initialCapacity);
        elementData = new Object[initialCapacity];
    }
    public ArrayList(Collection<? extends E> c) {
        Object[] a = c.toArray();
        elementData = a;
        size = a.length;
        if (size == 0) elementData = new Object[10];
    }
    private void grow(int min) {
        if (min > elementData.length) {
            int cap = elementData.length + (elementData.length >> 1) + 1;
            if (cap < min) cap = min;
            elementData = Arrays.copyOf(elementData, cap);
        }
    }
    private void rangeCheck(int index) {
        if (index < 0 || index >= size) throw new IndexOutOfBoundsException("Index " + index + " out of bounds for length " + size);
    }
    public int size() { return size; }
    public boolean isEmpty() { return size == 0; }
    public boolean contains(Object o) { return indexOf(o) >= 0; }
    public int indexOf(Object o) {
        for (int i = 0; i < size; i++) if (objEquals(o, elementData[i])) return i;
        return -1;
    }
    public int lastIndexOf(Object o) {
        for (int i = size - 1; i >= 0; i--) if (objEquals(o, elementData[i])) return i;
        return -1;
    }
    public E get(int index) { rangeCheck(index); return (E) elementData[index]; }
    public E set(int index, E element) {
        rangeCheck(index);
        E old = (E) elementData[index];
        elementData[index] = element;
        return old;
    }
    public boolean add(E e) { modCount++; grow(size + 1); elementData[size++] = e; return true; }
    public void add(int index, E element) {
        if (index < 0 || index > size) throw new IndexOutOfBoundsException("Index " + index + " out of bounds for length " + size);
        modCount++;
        grow(size + 1);
        System.arraycopy(elementData, index, elementData, index + 1, size - index);
        elementData[index] = element;
        size++;
    }
    public E remove(int index) {
        rangeCheck(index);
        modCount++;
        E old = (E) elementData[index];
        System.arraycopy(elementData, index + 1, elementData, index, size - index - 1);
        elementData[--size] = null;
        return old;
    }
    public boolean remove(Object o) {
        int i = indexOf(o);
        if (i < 0) return false;
        remove(i);
        return true;
    }
    public void clear() {
        modCount++;
        for (int i = 0; i < size; i++) elementData[i] = null;
        size = 0;
    }
    public boolean addAll(Collection<? extends E> c) {
        boolean changed = false;
        for (Iterator<? extends E> it = c.iterator(); it.hasNext(); ) { add(it.next()); changed = true; }
        return changed;
    }
    public boolean containsAll(Collection<?> c) {
        for (Iterator<?> it = c.iterator(); it.hasNext(); ) if (!contains(it.next())) return false;
        return true;
    }
    public Object[] toArray() { return Arrays.copyOf(elementData, size); }
    public Iterator<E> iterator() { return new Itr(); }
    public String toString() {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < size; i++) {
            if (i > 0) sb.append(", ");
            sb.append(elementData[i] == this ? "(this Collection)" : String.valueOf(elementData[i]));
        }
        return sb.append(']').toString();
    }
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof List)) return false;
        List<?> other = (List<?>) o;
        if (other.size() != size) return false;
        for (int i = 0; i < size; i++) if (!objEquals(elementData[i], other.get(i))) return false;
        return true;
    }
    public int hashCode() {
        int h = 1;
        for (int i = 0; i < size; i++) h = 31 * h + objHash(elementData[i]);
        return h;
    }
    private class Itr implements Iterator<E> {
        int cursor;
        int expectedModCount = modCount;
        public boolean hasNext() { return cursor != size; }
        public E next() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            if (cursor >= size) throw new NoSuchElementException();
            return (E) elementData[cursor++];
        }
        public void remove() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            ArrayList.this.remove(--cursor);
            expectedModCount = modCount;
        }
    }
}
