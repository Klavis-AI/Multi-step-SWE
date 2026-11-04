package java.lang.invoke;
public final class LambdaMetafactory {
    public static final int FLAG_SERIALIZABLE = 1 << 0;
    public static final int FLAG_MARKERS = 1 << 1;
    public static final int FLAG_BRIDGES = 1 << 2;
    private LambdaMetafactory() {}
    public static native CallSite metafactory(MethodHandles.Lookup caller,
                                              String interfaceMethodName,
                                              MethodType factoryType,
                                              MethodType interfaceMethodType,
                                              MethodHandle implementation,
                                              MethodType dynamicMethodType) throws LambdaConversionException;
    public static native CallSite altMetafactory(MethodHandles.Lookup caller,
                                                 String interfaceMethodName,
                                                 MethodType factoryType,
                                                 Object... args) throws LambdaConversionException;
}
