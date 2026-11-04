package java.lang.invoke;
public final class ConstantBootstraps {
    private ConstantBootstraps() {}
    public static native Object nullConstant(MethodHandles.Lookup lookup, String name, Class<?> type);
    public static native Class<?> primitiveClass(MethodHandles.Lookup lookup, String name, Class<?> type);
    public static native Object enumConstant(MethodHandles.Lookup lookup, String name, Class<?> type);
    public static native Object getStaticFinal(MethodHandles.Lookup lookup, String name, Class<?> type, Class<?> declaringClass);
    public static native Object getStaticFinal(MethodHandles.Lookup lookup, String name, Class<?> type);
    public static native Object invoke(MethodHandles.Lookup lookup, String name, Class<?> type, MethodHandle handle, Object... args) throws Throwable;
}
