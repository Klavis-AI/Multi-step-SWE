package java.lang;
public abstract class ClassLoader {
    private final ClassLoader parent;
    protected ClassLoader() { this(null); }
    protected ClassLoader(ClassLoader parent) { this.parent = parent; }
    public final ClassLoader getParent() { return parent; }
    public String getName() { return null; }
    public String toString() { return getClass().getName() + "@" + Integer.toHexString(hashCode()); }
    public static native ClassLoader getSystemClassLoader();
}
