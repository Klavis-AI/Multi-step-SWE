package java.lang;


/** java.lang.Class. Instances are created only by the VM. */
public final class Class<T> implements java.lang.reflect.Type {
    private long vmclass;
    private Class() {}

    private native String getName0();
    public String getName() { return getName0(); }

    public String toString() {
        return (isInterface() ? "interface " : (isPrimitive() ? "" : "class ")) + getName();
    }

    public String getSimpleName() {
        if (isArray()) return getComponentType().getSimpleName() + "[]";
        String n = getName();
        int i = n.lastIndexOf('$');
        if (i >= 0) {
            int j = i + 1;
            while (j < n.length() && n.charAt(j) >= '0' && n.charAt(j) <= '9') j++;
            if (j == n.length()) return "";
            return n.substring(j);
        }
        i = n.lastIndexOf('.');
        return i < 0 ? n : n.substring(i + 1);
    }

    public native boolean isInterface();
    public native boolean isArray();
    public native boolean isPrimitive();
    public native boolean isInstance(Object obj);
    public native boolean isAssignableFrom(Class<?> cls);
    public native Class<? super T> getSuperclass();
    public native Class<?>[] getInterfaces();
    public native Class<?> getComponentType();
    public native int getModifiers();
    public native boolean isEnum();
    public native Object newInstance() throws InstantiationException, IllegalAccessException;

    public boolean desiredAssertionStatus() { return false; }

    public T cast(Object obj) {
        if (obj != null && !isInstance(obj))
            throw new ClassCastException("Cannot cast " + obj.getClass().getName() + " to " + getName());
        return (T) obj;
    }

    private static native Class<?> forName0(String name, boolean initialize);
    public static Class<?> forName(String className) throws ClassNotFoundException {
        Class<?> c = forName0(className, true);
        if (c == null) throw new ClassNotFoundException(className);
        return c;
    }
    public static Class<?> forName(String name, boolean initialize, ClassLoader loader) throws ClassNotFoundException {
        Class<?> c = forName0(name, initialize);
        if (c == null) throw new ClassNotFoundException(name);
        return c;
    }
    public native ClassLoader getClassLoader();
    static native Class<?> getPrimitiveClass(String name);
}
