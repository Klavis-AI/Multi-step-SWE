package java.util;
public final class Optional<T> {
    private static final Optional<?> EMPTY = new Optional<Object>(null);
    private final T value;
    private Optional(T value) { this.value = value; }
    public static <T> Optional<T> empty() { return (Optional<T>) EMPTY; }
    public static <T> Optional<T> of(T value) {
        if (value == null) throw new NullPointerException();
        return new Optional<T>(value);
    }
    public static <T> Optional<T> ofNullable(T value) { return value == null ? Optional.<T>empty() : of(value); }
    public T get() {
        if (value == null) throw new NoSuchElementException("No value present");
        return value;
    }
    public T orElse(T other) { return value != null ? value : other; }
    public boolean isPresent() { return value != null; }
    public boolean isEmpty() { return value == null; }
    public String toString() { return value != null ? "Optional[" + value + "]" : "Optional.empty"; }
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Optional)) return false;
        Object v = ((Optional<?>) o).value;
        return value == null ? v == null : value.equals(v);
    }
    public int hashCode() { return value == null ? 0 : value.hashCode(); }
}
