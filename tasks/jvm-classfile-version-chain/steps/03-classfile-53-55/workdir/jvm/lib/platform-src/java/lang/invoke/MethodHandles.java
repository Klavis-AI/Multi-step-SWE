package java.lang.invoke;
public class MethodHandles {
    private MethodHandles() {}

    /** Caller sensitive: the VM supplies the immediate caller's lookup class. */
    public static native Lookup lookup();
    public static Lookup publicLookup() { return lookup(); }

    public static native MethodHandle arrayElementGetter(Class<?> arrayClass);
    public static native MethodHandle arrayElementSetter(Class<?> arrayClass);
    public static native MethodHandle constant(Class<?> type, Object value);
    public static native MethodHandle identity(Class<?> type);
    public static native MethodHandle insertArguments(MethodHandle target, int pos, Object... values);
    public static native MethodHandle dropArguments(MethodHandle target, int pos, Class<?>... valueTypes);
    public static native MethodHandle filterReturnValue(MethodHandle target, MethodHandle filter);
    public static native MethodHandle explicitCastArguments(MethodHandle target, MethodType newType);

    public static final class Lookup {
        private Class<?> lookupClass;
        private int allowedModes;
        private Lookup() {}
        Lookup(Class<?> c, int m) { lookupClass = c; allowedModes = m; }
        public Class<?> lookupClass() { return lookupClass; }
        public int lookupModes() { return allowedModes; }
        public String toString() { return lookupClass.getName(); }

        public static final int PUBLIC = 0x01;
        public static final int PRIVATE = 0x02;
        public static final int PROTECTED = 0x04;
        public static final int PACKAGE = 0x08;
        public static final int MODULE = 0x10;
        public static final int UNCONDITIONAL = 0x20;

        public native MethodHandle findStatic(Class<?> refc, String name, MethodType type) throws NoSuchMethodException, IllegalAccessException;
        public native MethodHandle findVirtual(Class<?> refc, String name, MethodType type) throws NoSuchMethodException, IllegalAccessException;
        public native MethodHandle findSpecial(Class<?> refc, String name, MethodType type, Class<?> specialCaller) throws NoSuchMethodException, IllegalAccessException;
        public native MethodHandle findConstructor(Class<?> refc, MethodType type) throws NoSuchMethodException, IllegalAccessException;
        public native MethodHandle findGetter(Class<?> refc, String name, Class<?> type) throws NoSuchFieldException, IllegalAccessException;
        public native MethodHandle findSetter(Class<?> refc, String name, Class<?> type) throws NoSuchFieldException, IllegalAccessException;
        public native MethodHandle findStaticGetter(Class<?> refc, String name, Class<?> type) throws NoSuchFieldException, IllegalAccessException;
        public native MethodHandle findStaticSetter(Class<?> refc, String name, Class<?> type) throws NoSuchFieldException, IllegalAccessException;
        public native Class<?> findClass(String targetName) throws ClassNotFoundException, IllegalAccessException;
        public native Class<?> defineClass(byte[] bytes) throws IllegalAccessException;
        public native Lookup in(Class<?> requestedLookupClass);
        public native void ensureInitialized(Class<?> targetClass) throws IllegalAccessException;

    }
}
