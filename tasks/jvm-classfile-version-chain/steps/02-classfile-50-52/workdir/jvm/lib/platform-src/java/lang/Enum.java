package java.lang;
public abstract class Enum<E extends Enum<E>> implements Comparable<E>, java.io.Serializable {
    private final String name;
    private final int ordinal;
    protected Enum(String name, int ordinal) { this.name = name; this.ordinal = ordinal; }
    public final String name() { return name; }
    public final int ordinal() { return ordinal; }
    public String toString() { return name; }
    public final boolean equals(Object other) { return this == other; }
    public final int hashCode() { return super.hashCode(); }
    protected final Object clone() throws CloneNotSupportedException { throw new CloneNotSupportedException(); }
    public final int compareTo(E o) { return ordinal - o.ordinal(); }
    public final Class<E> getDeclaringClass() {
        Class<?> c = getClass();
        Class<?> zuper = c.getSuperclass();
        return (Class<E>) (zuper == Enum.class ? c : zuper);
    }
    public static <T extends Enum<T>> T valueOf(Class<T> enumType, String name) {
        T[] vs = (T[]) enumValues(enumType);
        if (vs != null) for (int i = 0; i < vs.length; i++) if (vs[i].name().equals(name)) return vs[i];
        throw new IllegalArgumentException("No enum constant " + enumType.getName() + "." + name);
    }
    private static native Object[] enumValues(Class<?> enumType);
    protected final void finalize() {}
}
